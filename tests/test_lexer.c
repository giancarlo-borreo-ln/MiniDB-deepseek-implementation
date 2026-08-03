/*
 * test_lexer.c - Unit tests for the command-language lexer (src/lexer.c).
 *
 * The lexer turns a raw command string into a flat, bounded Token stream.
 * These tests pin down: token types and values for a simple command, keyword
 * recognition (case-insensitive), whitespace tolerance, the three error
 * conditions (bad number, unknown word, too many tokens), and the empty
 * input special case.
 */

#include "unity.h"

#include "lexer.h"

/* Lex `input` into `tokens`; returns the LexResult. Caller provides storage. */
static LexResult lex_to(const char* input, Token* tokens, size_t* count) {
    *count = 0;
    return lex(input, tokens, count);
}

/* --------------------------------------------------------------------------
 * Basic stream shape
 * ------------------------------------------------------------------------ */

void test_lex_insert_basic(void) {
    Token tokens[LEXER_MAX_TOKENS];
    size_t count = 0;

    /* "INSERT 42 100" => (KEYWORD INSERT) (INTEGER 42) (INTEGER 100) (END). */
    TEST_ASSERT_EQUAL_INT(LEX_OK, lex_to("INSERT 42 100", tokens, &count));
    TEST_ASSERT_EQUAL_UINT(4u, (unsigned)count);

    TEST_ASSERT_EQUAL_INT(TOKEN_KEYWORD, tokens[0].type);
    TEST_ASSERT_EQUAL_INT(KEYWORD_INSERT, tokens[0].keyword);

    TEST_ASSERT_EQUAL_INT(TOKEN_INTEGER, tokens[1].type);
    TEST_ASSERT_EQUAL_INT(42, tokens[1].int_value);

    TEST_ASSERT_EQUAL_INT(TOKEN_INTEGER, tokens[2].type);
    TEST_ASSERT_EQUAL_INT(100, tokens[2].int_value);

    TEST_ASSERT_EQUAL_INT(TOKEN_END, tokens[3].type);
}

/* --------------------------------------------------------------------------
 * Keyword mapping + case insensitivity
 * ------------------------------------------------------------------------ */

void test_lex_keyword_case_insensitive(void) {
    Token tokens[LEXER_MAX_TOKENS];
    size_t count = 0;

    /* Every recognized verb, in several mixings of case, must map to the
     * same Keyword enum as the all-uppercase spelling. */
    struct {
        const char* word;
        Keyword expected;
    } cases[] = {
        { "INSERT", KEYWORD_INSERT }, { "insert", KEYWORD_INSERT },
        { "Insert", KEYWORD_INSERT }, { "iNsErT", KEYWORD_INSERT },
        { "SELECT", KEYWORD_SELECT }, { "select", KEYWORD_SELECT },
        { "sElEcT", KEYWORD_SELECT },
        { "DELETE", KEYWORD_DELETE }, { "delete", KEYWORD_DELETE },
        { "RANGE",  KEYWORD_RANGE  }, { "range",  KEYWORD_RANGE  },
        { "PRINT",  KEYWORD_PRINT  }, { "print",  KEYWORD_PRINT  },
        { "EXIT",   KEYWORD_EXIT   }, { "exit",   KEYWORD_EXIT   },
        { "HELP",   KEYWORD_HELP   }, { "help",   KEYWORD_HELP   }
    };

    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        count = 0;
        TEST_ASSERT_EQUAL_INT(LEX_OK, lex_to(cases[i].word, tokens, &count));
        /* One keyword token followed by the END sentinel. */
        TEST_ASSERT_EQUAL_UINT(2u, (unsigned)count);
        TEST_ASSERT_EQUAL_INT(TOKEN_KEYWORD, tokens[0].type);
        TEST_ASSERT_EQUAL_INT(cases[i].expected, tokens[0].keyword);
        TEST_ASSERT_EQUAL_INT(TOKEN_END, tokens[1].type);
    }
}

/* --------------------------------------------------------------------------
 * Whitespace tolerance
 * ------------------------------------------------------------------------ */

void test_lex_whitespace_tolerance(void) {
    Token tokens[LEXER_MAX_TOKENS];
    size_t count = 0;

    /* Leading/trailing spaces, tabs and newlines between tokens must all be
     * invisible to the tokenizer. */
    TEST_ASSERT_EQUAL_INT(LEX_OK,
                          lex_to("   INSERT \t 42 \n 100 \t ", tokens, &count));
    TEST_ASSERT_EQUAL_UINT(4u, (unsigned)count);
    TEST_ASSERT_EQUAL_INT(TOKEN_KEYWORD, tokens[0].type);
    TEST_ASSERT_EQUAL_INT(KEYWORD_INSERT, tokens[0].keyword);
    TEST_ASSERT_EQUAL_INT(TOKEN_INTEGER, tokens[1].type);
    TEST_ASSERT_EQUAL_INT(42, tokens[1].int_value);
    TEST_ASSERT_EQUAL_INT(TOKEN_INTEGER, tokens[2].type);
    TEST_ASSERT_EQUAL_INT(100, tokens[2].int_value);
    TEST_ASSERT_EQUAL_INT(TOKEN_END, tokens[3].type);

    /* A command surrounded only by newlines and tabs still lexes cleanly. */
    count = 0;
    TEST_ASSERT_EQUAL_INT(LEX_OK, lex_to("\n\t\n  EXIT  ", tokens, &count));
    TEST_ASSERT_EQUAL_UINT(2u, (unsigned)count);
    TEST_ASSERT_EQUAL_INT(KEYWORD_EXIT, tokens[0].keyword);
    TEST_ASSERT_EQUAL_INT(TOKEN_END, tokens[1].type);
}

/* --------------------------------------------------------------------------
 * Error conditions
 * ------------------------------------------------------------------------ */

void test_lex_bad_number_overflow(void) {
    Token tokens[LEXER_MAX_TOKENS];
    size_t count = 0;

    /* "2147483648" is exactly INT_MAX + 1 on a 32-bit int: must be refused
     * as a number that would overflow, not silently wrapped around. */
    TEST_ASSERT_EQUAL_INT(LEX_ERROR_BAD_NUMBER,
                          lex_to("2147483648", tokens, &count));

    /* The same overflow caught mid-command. */
    count = 0;
    TEST_ASSERT_EQUAL_INT(LEX_ERROR_BAD_NUMBER,
                          lex_to("INSERT 99999999999999999 1", tokens, &count));

    /* INT_MAX itself is the boundary and must be accepted. */
    count = 0;
    TEST_ASSERT_EQUAL_INT(LEX_OK, lex_to("2147483647", tokens, &count));
    TEST_ASSERT_EQUAL_INT(TOKEN_INTEGER, tokens[0].type);
    TEST_ASSERT_EQUAL_INT(2147483647, tokens[0].int_value);
    TEST_ASSERT_EQUAL_INT(TOKEN_END, tokens[1].type);
}

void test_lex_unknown_word(void) {
    Token tokens[LEXER_MAX_TOKENS];
    size_t count = 0;

    /* A word that is not a keyword is rejected. */
    TEST_ASSERT_EQUAL_INT(LEX_ERROR_UNKNOWN_WORD,
                          lex_to("FOO", tokens, &count));

    /* ...even when it appears after valid tokens. */
    count = 0;
    TEST_ASSERT_EQUAL_INT(LEX_ERROR_UNKNOWN_WORD,
                          lex_to("INSERT 1 FOO", tokens, &count));

    /* A keyword with extra letters ("INSERTX") is not the keyword itself. */
    count = 0;
    TEST_ASSERT_EQUAL_INT(LEX_ERROR_UNKNOWN_WORD,
                          lex_to("INSERTX 1 2", tokens, &count));
}

void test_lex_stray_symbol(void) {
    Token tokens[LEXER_MAX_TOKENS];
    size_t count = 0;

    /* Punctuation is not part of the grammar: reported as unknown word. */
    TEST_ASSERT_EQUAL_INT(LEX_ERROR_UNKNOWN_WORD, lex_to("$", tokens, &count));

    count = 0;
    TEST_ASSERT_EQUAL_INT(LEX_ERROR_UNKNOWN_WORD,
                          lex_to("INSERT 42 $", tokens, &count));

    count = 0;
    TEST_ASSERT_EQUAL_INT(LEX_ERROR_UNKNOWN_WORD, lex_to(";", tokens, &count));

    count = 0;
    TEST_ASSERT_EQUAL_INT(LEX_ERROR_UNKNOWN_WORD,
                          lex_to("SELECT ?", tokens, &count));
}

/* --------------------------------------------------------------------------
 * Empty input
 * ------------------------------------------------------------------------ */

void test_lex_empty_input(void) {
    Token tokens[LEXER_MAX_TOKENS];
    size_t count = 0;

    /* "" is a perfectly valid (if useless) stream: just the END sentinel. */
    TEST_ASSERT_EQUAL_INT(LEX_OK, lex_to("", tokens, &count));
    TEST_ASSERT_EQUAL_UINT(1u, (unsigned)count);
    TEST_ASSERT_EQUAL_INT(TOKEN_END, tokens[0].type);
}

/* --------------------------------------------------------------------------
 * Token-budget overflow
 * ------------------------------------------------------------------------ */

void test_lex_too_many_tokens(void) {
    Token tokens[LEXER_MAX_TOKENS];
    size_t count = 0;

    /* LEXER_MAX_TOKENS (16) includes the END sentinel, so 15 integers leave
     * exactly one slot free for the END marker: this stream is fine. */
    const char* max_ok =
        "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15";
    TEST_ASSERT_EQUAL_INT(LEX_OK, lex_to(max_ok, tokens, &count));
    TEST_ASSERT_EQUAL_UINT(16u, (unsigned)count);
    TEST_ASSERT_EQUAL_INT(TOKEN_END, tokens[15].type);

    /* 16 integers consume every slot, leaving no room for END: overflow. */
    const char* too_many =
        "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16";
    count = 0;
    TEST_ASSERT_EQUAL_INT(LEX_ERROR_TOO_MANY_TOKENS,
                          lex_to(too_many, tokens, &count));
}

/* --------------------------------------------------------------------------
 * keyword_to_string()
 * ------------------------------------------------------------------------ */

void test_lex_keyword_to_string(void) {
    /* The reverse mapping must return stable, uppercase spellings. */
    TEST_ASSERT_EQUAL_STRING("INSERT", keyword_to_string(KEYWORD_INSERT));
    TEST_ASSERT_EQUAL_STRING("SELECT", keyword_to_string(KEYWORD_SELECT));
    TEST_ASSERT_EQUAL_STRING("DELETE", keyword_to_string(KEYWORD_DELETE));
    TEST_ASSERT_EQUAL_STRING("RANGE",  keyword_to_string(KEYWORD_RANGE));
    TEST_ASSERT_EQUAL_STRING("PRINT",  keyword_to_string(KEYWORD_PRINT));
    TEST_ASSERT_EQUAL_STRING("EXIT",   keyword_to_string(KEYWORD_EXIT));
    TEST_ASSERT_EQUAL_STRING("HELP",   keyword_to_string(KEYWORD_HELP));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", keyword_to_string(KEYWORD_UNKNOWN));
}
