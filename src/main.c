/*
 * main.c - The MiniDB REPL (Read-Eval-Print Loop).
 *
 * Each input line travels the whole pipeline, from raw text to storage:
 *
 * "INSERT 42 100"  (raw string)
 *      |
 *      v   +-----------------------------------------------+
 * [ Lexer ]  | one character at a time -> tokens:           |
 *      |    |   (KEYWORD:INSERT)(INT:42)(INT:100)(END)      |
 *      v    +-----------------------------------------------+
 * [ Token stream ]
 *      |
 *      v   +-----------------------------------------------+
 *[ Parser ] | validate shape & arity -> Command (mini AST): |
 *      |    |   { type=COMMAND_INSERT, key=42, value=100 }  |
 *      v    +-----------------------------------------------+
 * [ Command ]
 *      |
 *      v   +-----------------------------------------------+
 *[ db_execute ] | dispatch on type -> B+ tree call:          |
 *      |    |   btree_insert(&tree, 42, 100)                 |
 *      v    +-----------------------------------------------+
 * [ DbResult ]  { ok=true, message="inserted key 42 = 100" }
 *
 * The REPL drives that pipeline: read a line, lex it, parse it, execute it,
 * print the DbResult.message, repeat -- until EXIT or EOF.
 */

#include <stdio.h>     /* printf, fgets, fflush, stdin, stdout */
#include <string.h>    /* strlen                                  */
#include "database.h"  /* Database, DbResult, db_*               */
#include "lexer.h"     /* lex, LexResult, Token, LEXER_MAX_TOKENS */
#include "parser.h"    /* parse_command, parse_result_string      */

/* Friendly one-line description for each LexResult, echoed on a bad line. */
static const char* lex_result_string(LexResult r) {
    switch (r) {
    case LEX_OK:                    return "OK";
    case LEX_ERROR_UNKNOWN_WORD:    return "unknown word or stray symbol";
    case LEX_ERROR_BAD_NUMBER:      return "number too large";
    case LEX_ERROR_TOO_MANY_TOKENS: return "too many tokens";
    default:                        return "unknown lex error";
    }
}

/* Returns 1 when the line contains nothing but whitespace (incl. newline). */
static int is_blank_line(const char* line) {
    int i;
    for (i = 0; line[i] != '\0'; i++) {
        /* Any non-whitespace character means the line is worth lexing. */
        if (line[i] != ' ' && line[i] != '\t' &&
            line[i] != '\n' && line[i] != '\r') {
            return 0;
        }
    }
    return 1;
}

int main(void) {
    Token tokens[LEXER_MAX_TOKENS]; /* lexer fills this; capacity is fixed */
    size_t token_count = 0;         /* tokens consumed, incl. END sentinel */
    Command cmd;                    /* parser output (the proto-AST)       */
    DbResult result;                /* db_execute output (the reply)       */
    Database db;                    /* the one live B+ tree of the session */
    char line[256];                 /* one input line, newline included    */

    db_init(&db);

    for (;;) {
        /* Prompt without a trailing newline, then flush so it shows up even
         * when stdin is a pipe (fully buffered stdout would otherwise wait). */
        printf("minidb> ");
        fflush(stdout);

        /* EOF (Ctrl-D or a closed pipe): leave with a clean newline so the
         * shell prompt does not land on the same line as our last output. */
        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }

        /* Blank/whitespace-only lines are noise; skip them silently. */
        if (is_blank_line(line)) {
            continue;
        }

        /* Stage 1: LEX. A failed scan invalidates the token stream, so on any
         * non-LEX_OK we print a friendly message and move to the next line. */
        token_count = 0; /* fresh array state before every lex pass */
        {
            LexResult lr = lex(line, tokens, &token_count);
            if (lr != LEX_OK) {
                printf("Lex error: %s\n", lex_result_string(lr));
                continue;
            }
        }

        /* Stage 2: PARSE. On failure cmd is left as COMMAND_NONE (see
         * parser.c); show the reason plus a hint so the user can recover. */
        {
            ParseResult pr = parse_command(tokens, token_count, &cmd);
            if (pr != PARSE_OK) {
                printf("Parse error: %s. Type HELP for commands.\n",
                       parse_result_string(pr));
                continue;
            }
        }

        /* EXIT is the REPL's own control flow, not a storage operation. */
        if (cmd.type == COMMAND_EXIT) {
            printf("Goodbye!\n");
            break;
        }

        /* Stage 3: EXECUTE. db_execute dispatches onto the B+ tree and fills
         * result; we just echo its message. (For PRINT the tree itself was
         * already dumped to stdout by btree_print.) */
        db_execute(&db, &cmd, &result);
        printf("%s\n", result.message);
    }

    db_close(&db);
    return 0;
}
