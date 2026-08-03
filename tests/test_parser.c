/*
 * test_parser.c - Unit tests for the command parser (src/parser.c).
 *
 * The parser validates a Token stream against the fixed-arity grammar and
 * packs it into a Command struct. Valid commands must produce the exact
 * Command fields; malformed streams must produce the exact ParseResult.
 * Malformed streams are built by hand as Token arrays (the Token type is
 * public) because some of them can never be produced by the lexer.
 */

#include "unity.h"

#include "parser.h"
#include "lexer.h"

/* --------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------ */

/* Lex and parse one command string in one step (for well-formed inputs). */
static ParseResult parse(const char* input, Command* cmd) {
    Token tokens[LEXER_MAX_TOKENS];
    size_t count = 0;
    if (lex(input, tokens, &count) != LEX_OK) {
        return PARSE_ERROR_UNKNOWN_COMMAND;  /* not reached by valid inputs */
    }
    return parse_command(tokens, count, cmd);
}

/* Hand-built token constructors, so tests can craft malformed streams. */
static Token kw(Keyword k) {
    Token t;
    t.type = TOKEN_KEYWORD;
    t.keyword = k;
    t.int_value = 0;
    return t;
}

static Token num(int v) {
    Token t;
    t.type = TOKEN_INTEGER;
    t.keyword = KEYWORD_UNKNOWN;
    t.int_value = v;
    return t;
}

static Token end_token(void) {
    Token t;
    t.type = TOKEN_END;
    t.keyword = KEYWORD_UNKNOWN;
    t.int_value = 0;
    return t;
}

/* --------------------------------------------------------------------------
 * Every valid command maps to the exact Command fields
 * ------------------------------------------------------------------------ */

void test_parse_all_valid_commands(void) {
    Command cmd;

    TEST_ASSERT_EQUAL_INT(PARSE_OK, parse("INSERT 42 100", &cmd));
    TEST_ASSERT_EQUAL_INT(COMMAND_INSERT, cmd.type);
    TEST_ASSERT_EQUAL_INT(42, cmd.key);
    TEST_ASSERT_EQUAL_INT(100, cmd.value);

    TEST_ASSERT_EQUAL_INT(PARSE_OK, parse("SELECT 7", &cmd));
    TEST_ASSERT_EQUAL_INT(COMMAND_SELECT, cmd.type);
    TEST_ASSERT_EQUAL_INT(7, cmd.key);

    TEST_ASSERT_EQUAL_INT(PARSE_OK, parse("DELETE 3", &cmd));
    TEST_ASSERT_EQUAL_INT(COMMAND_DELETE, cmd.type);
    TEST_ASSERT_EQUAL_INT(3, cmd.key);

    TEST_ASSERT_EQUAL_INT(PARSE_OK, parse("RANGE 10 20", &cmd));
    TEST_ASSERT_EQUAL_INT(COMMAND_RANGE, cmd.type);
    TEST_ASSERT_EQUAL_INT(10, cmd.key);
    TEST_ASSERT_EQUAL_INT(20, cmd.key_end);

    TEST_ASSERT_EQUAL_INT(PARSE_OK, parse("PRINT", &cmd));
    TEST_ASSERT_EQUAL_INT(COMMAND_PRINT, cmd.type);

    TEST_ASSERT_EQUAL_INT(PARSE_OK, parse("EXIT", &cmd));
    TEST_ASSERT_EQUAL_INT(COMMAND_EXIT, cmd.type);

    TEST_ASSERT_EQUAL_INT(PARSE_OK, parse("HELP", &cmd));
    TEST_ASSERT_EQUAL_INT(COMMAND_HELP, cmd.type);
}

/* --------------------------------------------------------------------------
 * Arity errors
 * ------------------------------------------------------------------------ */

void test_parse_missing_argument(void) {
    Command cmd;

    /* Fewer arguments than the grammar requires. */
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR_MISSING_ARGUMENT,
                          parse("INSERT 42", &cmd));
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR_MISSING_ARGUMENT,
                          parse("SELECT", &cmd));
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR_MISSING_ARGUMENT,
                          parse("DELETE", &cmd));
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR_MISSING_ARGUMENT,
                          parse("RANGE 10", &cmd));
}

void test_parse_extra_argument(void) {
    Command cmd;

    /* More arguments than the grammar allows. */
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR_EXTRA_ARGUMENT,
                          parse("INSERT 1 2 3", &cmd));
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR_EXTRA_ARGUMENT,
                          parse("SELECT 1 2", &cmd));
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR_EXTRA_ARGUMENT,
                          parse("RANGE 1 2 3", &cmd));

    /* A zero-argument command must not accept any trailing token. */
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR_EXTRA_ARGUMENT,
                          parse("PRINT 1", &cmd));
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR_EXTRA_ARGUMENT,
                          parse("EXIT 1", &cmd));
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR_EXTRA_ARGUMENT,
                          parse("HELP 1", &cmd));
}

void test_parse_bad_argument(void) {
    Command cmd;

    /* A non-integer token where the grammar demands an integer. The lexer
     * would never produce this stream, so it is hand-built. */
    Token bad_mid[] = {
        kw(KEYWORD_INSERT), num(5), kw(KEYWORD_SELECT), end_token()
    };
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR_BAD_ARGUMENT,
                          parse_command(bad_mid, 4, &cmd));

    Token bad_second[] = {
        kw(KEYWORD_RANGE), num(5), kw(KEYWORD_HELP), end_token()
    };
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR_BAD_ARGUMENT,
                          parse_command(bad_second, 4, &cmd));
}

void test_parse_unknown_command(void) {
    Command cmd;

    /* A stream whose first token is an integer is not a command at all. */
    Token starts_with_int[] = { num(5), end_token() };
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR_UNKNOWN_COMMAND,
                          parse_command(starts_with_int, 2, &cmd));

    /* KEYWORD_UNKNOWN as the verb is equally unrecognized. */
    Token unknown_verb[] = { kw(KEYWORD_UNKNOWN), num(5), end_token() };
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR_UNKNOWN_COMMAND,
                          parse_command(unknown_verb, 3, &cmd));
}

void test_parse_empty(void) {
    Command cmd;

    /* A stream containing only the END sentinel is "empty input". */
    Token only_end[] = { end_token() };
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR_EMPTY, parse_command(only_end, 1, &cmd));

    /* The defensive zero-length path must not crash. */
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR_EMPTY, parse_command(NULL, 0, &cmd));

    /* And the real lexer output for "" feeds straight into the same error. */
    TEST_ASSERT_EQUAL_INT(PARSE_ERROR_EMPTY, parse("", &cmd));
}

/* --------------------------------------------------------------------------
 * Error hygiene: a failed parse must reset the Command to COMMAND_NONE
 * ------------------------------------------------------------------------ */

void test_parse_error_resets_command(void) {
    Command cmd;
    cmd.type = COMMAND_INSERT;
    cmd.key = 99;
    cmd.value = 99;
    cmd.key_end = 99;

    TEST_ASSERT_EQUAL_INT(PARSE_ERROR_MISSING_ARGUMENT,
                          parse("INSERT 42", &cmd));

    /* The contract: on any error the command is COMMAND_NONE with zeros. */
    TEST_ASSERT_EQUAL_INT(COMMAND_NONE, cmd.type);
    TEST_ASSERT_EQUAL_INT(0, cmd.key);
    TEST_ASSERT_EQUAL_INT(0, cmd.value);
    TEST_ASSERT_EQUAL_INT(0, cmd.key_end);
}

/* --------------------------------------------------------------------------
 * The string label helpers
 * ------------------------------------------------------------------------ */

void test_parse_result_and_type_strings(void) {
    TEST_ASSERT_EQUAL_STRING("OK", parse_result_string(PARSE_OK));
    TEST_ASSERT_EQUAL_STRING("empty input",
                             parse_result_string(PARSE_ERROR_EMPTY));
    TEST_ASSERT_EQUAL_STRING("unknown command",
                             parse_result_string(PARSE_ERROR_UNKNOWN_COMMAND));
    TEST_ASSERT_EQUAL_STRING("missing argument",
                             parse_result_string(PARSE_ERROR_MISSING_ARGUMENT));
    TEST_ASSERT_EQUAL_STRING("extra argument",
                             parse_result_string(PARSE_ERROR_EXTRA_ARGUMENT));
    TEST_ASSERT_EQUAL_STRING("bad argument",
                             parse_result_string(PARSE_ERROR_BAD_ARGUMENT));

    TEST_ASSERT_EQUAL_STRING("INSERT", command_type_string(COMMAND_INSERT));
    TEST_ASSERT_EQUAL_STRING("SELECT", command_type_string(COMMAND_SELECT));
    TEST_ASSERT_EQUAL_STRING("DELETE", command_type_string(COMMAND_DELETE));
    TEST_ASSERT_EQUAL_STRING("RANGE",  command_type_string(COMMAND_RANGE));
    TEST_ASSERT_EQUAL_STRING("PRINT",  command_type_string(COMMAND_PRINT));
    TEST_ASSERT_EQUAL_STRING("EXIT",   command_type_string(COMMAND_EXIT));
    TEST_ASSERT_EQUAL_STRING("HELP",   command_type_string(COMMAND_HELP));
    TEST_ASSERT_EQUAL_STRING("NONE",   command_type_string(COMMAND_NONE));
}
