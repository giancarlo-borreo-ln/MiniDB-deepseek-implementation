/*
 * lexer.c - Implementation of the MiniDB command-language lexer.
 *
 * The lexer is a tiny character-level state machine. It walks the input
 * string once, left to right, skipping whitespace and grouping consecutive
 * digits or letters into single tokens. It deliberately performs NO
 * dynamic allocation (see lexer.h for the design rationale): the caller
 * owns the token array, and lex() just fills it up to a fixed bound.
 *
 * Example token stream:
 *
 *   "INSERT 42 100"  =>  (KEYWORD: INSERT) (INTEGER: 42) (INTEGER: 100) (END)
 *   "select 7"       =>  (KEYWORD: SELECT) (INTEGER: 7) (END)
 *   "  exit  "       =>  (KEYWORD: EXIT) (END)
 *
 * Error-handling conventions (documented, and worth reading twice):
 *   - A run of letters that does not spell a known keyword yields
 *     LEX_ERROR_UNKNOWN_WORD.  (Covers identifiers like "frobnicate".)
 *   - A run of digits that would overflow int yields LEX_ERROR_BAD_NUMBER.
 *   - ANY stray symbol ( '$', ';', '"', '?', ... ) yields
 *     LEX_ERROR_UNKNOWN_WORD.  It would be nice to have a distinct
 *     "unexpected character" code, but the public contract keeps exactly
 *     three error kinds, and "unknown word" is the best generic match:
 *     the offending character is, from the grammar's point of view, a word
 *     that was never defined. This keeps the error surface tiny for a
 *     teaching codebase.
 *   - Exceeding LEXER_MAX_TOKENS yields LEX_ERROR_TOO_MANY_TOKENS.
 *
 * On ANY error lex() stops immediately and returns; the caller must treat
 * the partially-filled token stream as garbage and not use it.
 */

#include "lexer.h"

#include <ctype.h>   /* isspace, isdigit, isalpha, tolower  */
#include <limits.h>  /* INT_MAX                              */
#include <string.h>  /* strlen, memcmp                       */

/*
 * The canonical, uppercase spellings of the six recognized keywords.
 * Indexing with the Keyword enum values keeps keyword matching a simple
 * memcmp, and lets keyword_to_string() reuse the same table.
 */
static const char* const KEYWORD_TABLE[KEYWORD_UNKNOWN] = {
    "INSERT",   /* KEYWORD_INSERT */
    "SELECT",   /* KEYWORD_SELECT */
    "DELETE",   /* KEYWORD_DELETE */
    "RANGE",    /* KEYWORD_RANGE  */
    "PRINT",    /* KEYWORD_PRINT  */
    "EXIT",     /* KEYWORD_EXIT   */
    "HELP"      /* KEYWORD_HELP   */
};

const char* keyword_to_string(Keyword keyword) {
    /*
     * Out-of-range enum values (or KEYWORD_UNKNOWN itself) collapse to the
     * literal "UNKNOWN". The comparison guards against the (theoretical)
     * caller passing a garbage integer.
     */
    if (keyword < KEYWORD_INSERT || keyword >= KEYWORD_UNKNOWN) {
        return "UNKNOWN";
    }
    return KEYWORD_TABLE[keyword];
}

/*
 * buffer_starts_with_upper() compares a fixed-length buffer (not
 * NUL-terminated) against the uppercase keyword string. The keyword's
 * length is given explicitly, so memcmp sees exactly the word's own bytes
 * and cannot read past the end of the buffer.
 */
static int buffer_starts_with_upper(const char* buffer, size_t len,
                                    const char* keyword_upper) {
    return len == strlen(keyword_upper) &&
           memcmp(buffer, keyword_upper, len) == 0;
}

/*
 * append_token() is the ONLY place tokens enter the output array. It checks
 * the caller's fixed capacity and reports overflow through the shared
 * helper return convention used by the scanner below: LEX_OK, or
 * LEX_ERROR_TOO_MANY_TOKENS if there is no room.
 */
static LexResult append_token(Token* tokens, size_t* token_count, Token t) {
    if (*token_count >= LEXER_MAX_TOKENS) {
        return LEX_ERROR_TOO_MANY_TOKENS;
    }
    tokens[*token_count] = t;
    (*token_count)++;
    return LEX_OK;
}

LexResult lex(const char* input, Token* tokens, size_t* token_count) {
    const char* p = input;
    size_t count = 0;

    while (*p != '\0') {

        /* ---- State: skip whitespace ---------------------------------- */
        /* Whitespace is never significant; advance until a real character
         * (or end of input) appears. isspace() covers space, tab, \n, \r. */
        if (isspace((unsigned char)*p)) {
            p++;
            continue;
        }

        /* ---- State: integer literal ----------------------------------- */
        /* A leading digit enters integer mode. Consume the whole run of
         * digits; after the loop we are either past the run or at '\0'. */
        if (isdigit((unsigned char)*p)) {
            int value = 0;

            while (isdigit((unsigned char)*p)) {
                int digit = *p - '0';

                /*
                 * Overflow guard: value grows by 10x each step. If the next
                 * multiplication would exceed INT_MAX, abort the whole scan.
                 * (value > INT_MAX/10 catches the multiply; the digit term
                 * can additionally push an already-10x value past the top.)
                 */
                if (value > (INT_MAX - digit) / 10) {
                    return LEX_ERROR_BAD_NUMBER;
                }
                value = value * 10 + digit;
                p++;
            }

            /* Done with the run: emit one integer token and continue. */
            {
                Token t;
                t.type = TOKEN_INTEGER;
                t.int_value = value;
                if (append_token(tokens, &count, t) != LEX_OK) {
                    return LEX_ERROR_TOO_MANY_TOKENS;
                }
            }
            continue;
        }

        /* ---- State: keyword / word ------------------------------------ */
        /* A leading letter enters word mode. We buffer the whole word into
         * a fixed stack array (bounded by the longest keyword + NUL), then
         * case-insensitively match it against the keyword table. */
        if (isalpha((unsigned char)*p)) {
            char word[8];     /* longest keyword is 6 chars ("DELETE") + NUL */
            size_t wlen = 0;

            while (isalpha((unsigned char)*p)) {
                /*
                 * If the word grows beyond our stack buffer it cannot be a
                 * keyword anyway, so bail early and treat it as unknown.
                 * (The capacity guard keeps buffer writes in bounds.)
                 */
                if (wlen >= sizeof(word) - 1) {
                    return LEX_ERROR_UNKNOWN_WORD;
                }
                word[wlen] = (char)toupper((unsigned char)*p);
                wlen++;
                p++;
            }
            word[wlen] = '\0';

            /*
             * Word mode transition -> keyword or error. Normalize the input
             * to uppercase (toupper above) so it matches the uppercase
             * spellings in KEYWORD_TABLE exactly; memcmp is then equivalent
             * to a case-insensitive comparison (both operands are in the
             * same case). " 5" inputs cannot reach here.
             */
            if (buffer_starts_with_upper(word, wlen, "INSERT")) {
                Token t;
                t.type = TOKEN_KEYWORD;
                t.keyword = KEYWORD_INSERT;
                if (append_token(tokens, &count, t) != LEX_OK) {
                    return LEX_ERROR_TOO_MANY_TOKENS;
                }
                continue;
            }
            if (buffer_starts_with_upper(word, wlen, "SELECT")) {
                Token t;
                t.type = TOKEN_KEYWORD;
                t.keyword = KEYWORD_SELECT;
                if (append_token(tokens, &count, t) != LEX_OK) {
                    return LEX_ERROR_TOO_MANY_TOKENS;
                }
                continue;
            }
            if (buffer_starts_with_upper(word, wlen, "DELETE")) {
                Token t;
                t.type = TOKEN_KEYWORD;
                t.keyword = KEYWORD_DELETE;
                if (append_token(tokens, &count, t) != LEX_OK) {
                    return LEX_ERROR_TOO_MANY_TOKENS;
                }
                continue;
            }
            if (buffer_starts_with_upper(word, wlen, "RANGE")) {
                Token t;
                t.type = TOKEN_KEYWORD;
                t.keyword = KEYWORD_RANGE;
                if (append_token(tokens, &count, t) != LEX_OK) {
                    return LEX_ERROR_TOO_MANY_TOKENS;
                }
                continue;
            }
            if (buffer_starts_with_upper(word, wlen, "PRINT")) {
                Token t;
                t.type = TOKEN_KEYWORD;
                t.keyword = KEYWORD_PRINT;
                if (append_token(tokens, &count, t) != LEX_OK) {
                    return LEX_ERROR_TOO_MANY_TOKENS;
                }
                continue;
            }
            if (buffer_starts_with_upper(word, wlen, "EXIT")) {
                Token t;
                t.type = TOKEN_KEYWORD;
                t.keyword = KEYWORD_EXIT;
                if (append_token(tokens, &count, t) != LEX_OK) {
                    return LEX_ERROR_TOO_MANY_TOKENS;
                }
                continue;
            }
            if (buffer_starts_with_upper(word, wlen, "HELP")) {
                Token t;
                t.type = TOKEN_KEYWORD;
                t.keyword = KEYWORD_HELP;
                if (append_token(tokens, &count, t) != LEX_OK) {
                    return LEX_ERROR_TOO_MANY_TOKENS;
                }
                continue;
            }

            /* No keyword matched: the word is not part of the language. */
            return LEX_ERROR_UNKNOWN_WORD;
        }

        /* ---- State: stray symbol --------------------------------------- */
        /* Any remaining byte ( '$', ';', '"', '@', ... ) is not valid in
         * the grammar. Following the documented convention, report it as
         * LEX_ERROR_UNKNOWN_WORD: to the grammar this character is simply
         * a one-character "word" nobody defined. */
        return LEX_ERROR_UNKNOWN_WORD;
    }

    /* ---- State: end of input ------------------------------------------- */
    /* Always append the TOKEN_END sentinel so the parser has an explicit
     * terminator and never reads past the last real token. The sentinel
     * counts toward LEXER_MAX_TOKENS, so even an otherwise-valid stream of
     * exactly LEXER_MAX_TOKENS real tokens is an overflow (capacity leaves
     * room for the END marker). */
    {
        Token end;
        end.type = TOKEN_END;
        end.keyword = KEYWORD_UNKNOWN;
        end.int_value = 0;
        if (append_token(tokens, &count, end) != LEX_OK) {
            return LEX_ERROR_TOO_MANY_TOKENS;
        }
    }

    *token_count = count;
    return LEX_OK;
}
