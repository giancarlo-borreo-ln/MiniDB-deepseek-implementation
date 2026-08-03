/*
 * parser.h - Grammar-level front end for the MiniDB command language.
 *
 * Sits on top of the lexer: lex() splits a command string into a flat token
 * stream; parse_command() validates that stream against the tiny fixed-arity
 * grammar below and packs the result into a Command struct.
 *
 * Grammar (one keyword + a FIXED number of integer arguments):
 *   INSERT <key> <value>
 *   SELECT <key>
 *   DELETE <key>
 *   RANGE  <key_lo> <key_hi>     -- inclusive bounds
 *   PRINT
 *   EXIT
 *   HELP
 *
 * A Command is the database's proto-AST: a single flat node because every
 * statement is exactly one verb and at most two integers. A real SQL parser
 * would grow it into a tree (WHERE expressions, joins, projections); see
 * parser.c for the "how to extend" note.
 */

#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>   /* size_t */
#include "lexer.h"    /* Token -- the parser's input unit */

/* Every grammar rule maps onto exactly one of these verbs. */
typedef enum {
    COMMAND_INSERT, COMMAND_SELECT, COMMAND_DELETE, COMMAND_RANGE,
    COMMAND_PRINT, COMMAND_EXIT, COMMAND_HELP, COMMAND_NONE
} CommandType;

/* Proto-AST: a flat node describing one validated command. */
typedef struct {
    CommandType type;
    int key;      /* INSERT/SELECT/DELETE/RANGE: the primary key   */
    int value;    /* INSERT only: the value to store               */
    int key_end;  /* RANGE only: the inclusive upper bound         */
} Command;

/* Outcome of a parse pass; maps 1:1 onto the grammar rules above. */
typedef enum {
    PARSE_OK,
    PARSE_ERROR_EMPTY,
    PARSE_ERROR_UNKNOWN_COMMAND,
    PARSE_ERROR_MISSING_ARGUMENT,
    PARSE_ERROR_EXTRA_ARGUMENT,
    PARSE_ERROR_BAD_ARGUMENT
} ParseResult;

/*
 * parse_command() validates `tokens` (length `token_count`, including the
 * trailing TOKEN_END sentinel) against the grammar. On PARSE_OK it fills
 * `command`; on any error the command struct is left as COMMAND_NONE with
 * all integer fields zeroed.
 */
ParseResult parse_command(const Token* tokens, size_t token_count, Command* command);

/* Human-readable label for a ParseResult ("OK", "empty input", ...). */
const char* parse_result_string(ParseResult result);

/* Uppercase verb for a CommandType ("INSERT", ..., "NONE"). */
const char* command_type_string(CommandType type);

#endif /* PARSER_H */
