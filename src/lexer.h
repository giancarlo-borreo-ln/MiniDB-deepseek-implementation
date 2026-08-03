/*
 * lexer.h - Tokenizer for the MiniDB command language.
 *
 * Turns a raw command string (e.g. "INSERT 42 100") into a flat, bounded
 * stream of Tokens. The lexer is a pure function: it allocates nothing and
 * frees nothing. The CALLER supplies the token array and its element count.
 *
 * Why no malloc/free here (design decision):
 *   - The teaching focus of this codebase is memory management inside the DB
 *     core (the B+ tree pages), not the CLI front-end. Keeping the lexer
 *     allocation-free lets students reason about the parser with plain C
 *     arrays and explicit bounds.
 *   - A fixed capacity (LEXER_MAX_TOKENS) is acceptable for an interactive
 *     command line: real-world commands are short, and the error path is
 *     explicit. A production database would instead grow the token buffer
 *     dynamically (realloc) to support arbitrarily long statements.
 */

#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>   /* size_t  */

/*
 * Maximum number of Tokens the lexer will ever emit, INCLUDING the final
 * TOKEN_END sentinel. Kept small and fixed so the caller can stack-allocate
 * the array.
 */
#define LEXER_MAX_TOKENS 16

/* The recognized command verbs of the MiniDB language. */
typedef enum {
    KEYWORD_INSERT, KEYWORD_SELECT, KEYWORD_DELETE, KEYWORD_RANGE,
    KEYWORD_PRINT, KEYWORD_EXIT, KEYWORD_HELP, KEYWORD_UNKNOWN
} Keyword;

/*
 * TokenType is the discriminator for a Token.
 *
 *   TOKEN_KEYWORD  -> the token names a command verb; read `keyword`.
 *   TOKEN_INTEGER  -> the token is a bare decimal literal; read `int_value`.
 *   TOKEN_END      -> sentinel marking the end of the stream.
 *   TOKEN_ERROR    -> reserved; lex() reports errors via its return code
 *                     instead of emitting error tokens.
 */
typedef enum {
    TOKEN_KEYWORD, TOKEN_INTEGER, TOKEN_END, TOKEN_ERROR
} TokenType;

/* A single lexical unit produced by lex(). */
typedef struct {
    TokenType type;
    Keyword   keyword;    /* valid when type == TOKEN_KEYWORD */
    int       int_value;  /* valid when type == TOKEN_INTEGER */
} Token;

/* Result of a lexing pass. LEX_OK means success. */
typedef enum {
    LEX_OK,
    LEX_ERROR_UNKNOWN_WORD,      /* unrecognized word, or a stray symbol    */
    LEX_ERROR_BAD_NUMBER,        /* digit run that would overflow int       */
    LEX_ERROR_TOO_MANY_TOKENS    /* stream exceeded LEXER_MAX_TOKENS tokens */
} LexResult;

/*
 * lex() scans `input` (NUL-terminated) one character at a time, producing
 * tokens into the caller-owned array `tokens` (capacity LEXER_MAX_TOKENS).
 * On success it appends a TOKEN_END sentinel and stores the total token
 * count (including the sentinel) in `*token_count`, returning LEX_OK.
 *
 * On failure it returns a LexResult describing the problem; tokens and
 * *token_count are left in whatever state they reached before the error
 * (callers must treat the stream as invalid on any non-LEX_OK return).
 */
LexResult lex(const char* input, Token* tokens, size_t* token_count);

/*
 * keyword_to_string() maps a Keyword back to a stable, human-readable
 * uppercase string ("INSERT", "SELECT", ...; "UNKNOWN" for
 * KEYWORD_UNKNOWN). Used for echoing parsed commands to the user.
 */
const char* keyword_to_string(Keyword keyword);

#endif /* LEXER_H */
