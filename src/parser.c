/*
 * parser.c - Parser for the MiniDB command language.
 *
 * The parser consumes the flat token stream produced by lex() (see lexer.h)
 * and turns it into a Command struct: a tiny, flat, proto-AST. This is the
 * grammar-level front end; the lexer has already done the tedious char-by-
 * char scanning, so parsing here is just shape-checking a small array.
 *
 * WHY the parser looks the way it does:
 *   - A real SQL parser builds a syntax tree. MiniDB's statements are all of
 *     the form "verb + a fixed number of integers", so the "tree" collapses
 *     to one flat node. Keeping the struct flat is the teaching trade-off:
 *     students can see every field of every command at a glance, and the
 *     "how to extend" note below shows where tree-ness would return.
 *
 * HOW the walker works:
 *   parse_command() treats the token array as a cursor. Token [0] must be
 *   the command keyword; the argument walker starts at index 1 and advances
 *   one slot per expected integer. Arity is validated by construction: a
 *   single helper loop consumes EXACTLY `argc` integers, then requires the
 *   very next token to be TOKEN_END. Because both the count and the terminator
 *   are checked, no token between the keyword and the END sentinel can be
 *   silently missed, and nothing can sneak in past the declared arity.
 *
 * ASCII map -- token slots to Command fields:
 *
 *   tokens: (KEYWORD: INSERT) (INTEGER: 42) (INTEGER: 100) (END)
 *                                  |              |
 *                                  v              v
 *   Command: { type = COMMAND_INSERT, key = 42, value = 100, key_end = 0 }
 *
 *   tokens: (KEYWORD: RANGE) (INTEGER: 10) (INTEGER: 20) (END)
 *                                  |              |
 *                                  v              v
 *   Command: { type = COMMAND_RANGE, key = 10, value = 0, key_end = 20 }
 *
 *   tokens: (KEYWORD: PRINT) (END)
 *                                  |
 *                                  v
 *   Command: { type = COMMAND_PRINT, key = 0, value = 0, key_end = 0 }
 *
 * HOW TO EXTEND (teaching note):
 *   - New verb?      Add the Keyword to the enum in lexer.h and match it in
 *                    lex(), then add a case here and a CommandType in parser.h.
 *   - New arity?     Change the `argc` passed to parse_fixed_arguments();
 *                    exact-arity checking is centralized in that one helper.
 *   - Variable-length lists (a SELECT column list)? The walker would stop on
 *                    a delimiter token (comma, closing paren) instead of a
 *                    fixed count -- parse_fixed_arguments() is the single
 *                    place that would change shape.
 *   - Nested syntax (a WHERE clause)? Command would grow pointers to child
 *                    nodes, and the walker would recurse into them instead of
 *                    advancing linearly. That is the moment the proto-AST
 *                    becomes a real tree.
 */

#include "parser.h"
#include "lexer.h"

#include <stddef.h>   /* size_t */

/*
 * parse_fixed_arguments() is the exact-arity engine shared by every command.
 *
 * The cursor starts at `start` (index 1 in the common case: just past the
 * keyword). It must find exactly `argc` consecutive TOKEN_INTEGER tokens;
 * then the next token MUST be TOKEN_END. This single helper therefore covers
 * three failure modes at once:
 *   - stream ends (or hits TOKEN_END) before all args  -> MISSING_ARGUMENT
 *   - a non-integer token where an integer is expected -> BAD_ARGUMENT
 *   - any token remaining after the expected count     -> EXTRA_ARGUMENT
 *
 * Note the deliberate NULL-command/truncated-array defensiveness: a correct
 * lex() run always appends TOKEN_END, so the "ran off the array" branches
 * below only trigger if a caller hand-builds a malformed stream.
 */
static ParseResult parse_fixed_arguments(const Token* tokens, size_t token_count,
                                         size_t start, size_t argc, int* args) {
    size_t i;

    for (i = 0; i < argc; i++) {
        /* Ran past the caller's array: even the END sentinel is missing. */
        if (start >= token_count) {
            return PARSE_ERROR_MISSING_ARGUMENT;
        }
        /* Hit the END sentinel early: the argument list is too short. */
        if (tokens[start].type == TOKEN_END) {
            return PARSE_ERROR_MISSING_ARGUMENT;
        }
        /* A keyword (or anything else) where the grammar demands an int. */
        if (tokens[start].type != TOKEN_INTEGER) {
            return PARSE_ERROR_BAD_ARGUMENT;
        }
        args[i] = tokens[start].int_value;
        start++;
    }

    /* All `argc` arguments consumed. The stream must end right here. */
    if (start >= token_count) {
        /* Array truncated exactly after the last argument: sentinel gone. */
        return PARSE_ERROR_MISSING_ARGUMENT;
    }
    if (tokens[start].type != TOKEN_END) {
        /* At least one token beyond the declared arity: too many. */
        return PARSE_ERROR_EXTRA_ARGUMENT;
    }

    return PARSE_OK;
}

ParseResult parse_command(const Token* tokens, size_t token_count, Command* command) {
    int args[2];              /* largest arity in the grammar is two */
    ParseResult r;

    /* Initialize every field first so unused fields are always defined;
     * parse_fixed_arguments() (or nothing, for the arity-zero commands)
     * then fills in only the fields this particular command uses. */
    command->type = COMMAND_NONE;
    command->key = 0;
    command->value = 0;
    command->key_end = 0;

    /* Empty input: a well-formed empty stream is just the END sentinel. */
    if (token_count == 0 || tokens[0].type == TOKEN_END) {
        return PARSE_ERROR_EMPTY;
    }

    /* Every command begins with a keyword. A bare integer is not a verb. */
    if (tokens[0].type != TOKEN_KEYWORD) {
        return PARSE_ERROR_UNKNOWN_COMMAND;
    }

    /*
     * Dispatch on the keyword. Each case states its arity in one place --
     * that number is the entire grammar rule. The walker index `1` is where
     * the argument list starts, always immediately after the keyword.
     */
    switch (tokens[0].keyword) {
    case KEYWORD_INSERT:
        r = parse_fixed_arguments(tokens, token_count, 1, 2, args);
        if (r != PARSE_OK) return r;
        command->type = COMMAND_INSERT;
        command->key = args[0];
        command->value = args[1];
        return PARSE_OK;

    case KEYWORD_SELECT:
        r = parse_fixed_arguments(tokens, token_count, 1, 1, args);
        if (r != PARSE_OK) return r;
        command->type = COMMAND_SELECT;
        command->key = args[0];
        return PARSE_OK;

    case KEYWORD_DELETE:
        r = parse_fixed_arguments(tokens, token_count, 1, 1, args);
        if (r != PARSE_OK) return r;
        command->type = COMMAND_DELETE;
        command->key = args[0];
        return PARSE_OK;

    case KEYWORD_RANGE:
        r = parse_fixed_arguments(tokens, token_count, 1, 2, args);
        if (r != PARSE_OK) return r;
        command->type = COMMAND_RANGE;
        command->key = args[0];
        command->key_end = args[1];
        return PARSE_OK;

    /* The three zero-argument commands all reduce to: END must follow. */
    case KEYWORD_PRINT:
        r = parse_fixed_arguments(tokens, token_count, 1, 0, args);
        if (r != PARSE_OK) return r;
        command->type = COMMAND_PRINT;
        return PARSE_OK;

    case KEYWORD_EXIT:
        r = parse_fixed_arguments(tokens, token_count, 1, 0, args);
        if (r != PARSE_OK) return r;
        command->type = COMMAND_EXIT;
        return PARSE_OK;

    case KEYWORD_HELP:
        r = parse_fixed_arguments(tokens, token_count, 1, 0, args);
        if (r != PARSE_OK) return r;
        command->type = COMMAND_HELP;
        return PARSE_OK;

    default:
        /* KEYWORD_UNKNOWN (and any enum value the grammar doesn't know). */
        return PARSE_ERROR_UNKNOWN_COMMAND;
    }
}

const char* parse_result_string(ParseResult result) {
    switch (result) {
    case PARSE_OK:                     return "OK";
    case PARSE_ERROR_EMPTY:            return "empty input";
    case PARSE_ERROR_UNKNOWN_COMMAND:  return "unknown command";
    case PARSE_ERROR_MISSING_ARGUMENT: return "missing argument";
    case PARSE_ERROR_EXTRA_ARGUMENT:   return "extra argument";
    case PARSE_ERROR_BAD_ARGUMENT:     return "bad argument";
    default:                           return "unknown";
    }
}

const char* command_type_string(CommandType type) {
    switch (type) {
    case COMMAND_INSERT: return "INSERT";
    case COMMAND_SELECT: return "SELECT";
    case COMMAND_DELETE: return "DELETE";
    case COMMAND_RANGE:  return "RANGE";
    case COMMAND_PRINT:  return "PRINT";
    case COMMAND_EXIT:   return "EXIT";
    case COMMAND_HELP:   return "HELP";
    case COMMAND_NONE:   return "NONE";
    default:             return "UNKNOWN";
    }
}
