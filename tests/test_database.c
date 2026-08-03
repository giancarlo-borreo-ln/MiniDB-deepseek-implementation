/*
 * test_database.c - End-to-end tests for the database layer (src/database.c).
 *
 * db_execute() is the seam where a parsed Command meets the B+ tree. These
 * tests drive the full stack (lexer -> parser -> database) with command
 * strings, exactly like the REPL would, and check the DbResult fields.
 */

#include "unity.h"

#include "database.h"
#include "parser.h"
#include "lexer.h"

#include <stdio.h>

/* Lex, parse, and execute one command string against the database. */
static void exec(Database* db, const char* input, DbResult* result) {
    Token tokens[LEXER_MAX_TOKENS];
    size_t count = 0;
    Command cmd;

    TEST_ASSERT_EQUAL_INT(LEX_OK, lex(input, tokens, &count));
    TEST_ASSERT_EQUAL_INT(PARSE_OK, parse_command(tokens, count, &cmd));
    db_execute(db, &cmd, result);
}

/* --------------------------------------------------------------------------
 * Insert -> select round trip, then delete -> miss
 * ------------------------------------------------------------------------ */

void test_db_insert_select_delete(void) {
    Database db;
    db_init(&db);
    DbResult r;

    exec(&db, "INSERT 42 100", &r);
    TEST_ASSERT_TRUE(r.ok);

    exec(&db, "SELECT 42", &r);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.found);
    TEST_ASSERT_EQUAL_INT(100, r.value);

    exec(&db, "DELETE 42", &r);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.found);

    /* After the delete, the very same select must miss. */
    exec(&db, "SELECT 42", &r);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_FALSE(r.found);

    db_close(&db);
}

/* --------------------------------------------------------------------------
 * Updating a key changes the stored value
 * ------------------------------------------------------------------------ */

void test_db_update_value(void) {
    Database db;
    db_init(&db);
    DbResult r;

    exec(&db, "INSERT 42 100", &r);
    exec(&db, "INSERT 42 999", &r);   /* same key: an update, not an insert */
    TEST_ASSERT_TRUE(r.ok);

    exec(&db, "SELECT 42", &r);
    TEST_ASSERT_TRUE(r.found);
    TEST_ASSERT_EQUAL_INT(999, r.value);

    db_close(&db);
}

/* --------------------------------------------------------------------------
 * RANGE reports the correct matched count
 * ------------------------------------------------------------------------ */

void test_db_range_matched_count(void) {
    Database db;
    db_init(&db);
    DbResult r;

    for (int k = 1; k <= 20; k++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "INSERT %d %d", k, k * 10);
        exec(&db, buf, &r);
        TEST_ASSERT_TRUE(r.ok);
    }

    exec(&db, "RANGE 5 10", &r);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT(6, r.matched);

    exec(&db, "RANGE 1 20", &r);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT(20, r.matched);

    /* A window with no keys inside reports zero. */
    exec(&db, "RANGE 21 30", &r);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT(0, r.matched);

    db_close(&db);
}

/* --------------------------------------------------------------------------
 * db_close then re-init gives a fresh, empty database
 * ------------------------------------------------------------------------ */

void test_db_reinit_after_close(void) {
    Database db;
    db_init(&db);
    DbResult r;

    exec(&db, "INSERT 1 10", &r);
    TEST_ASSERT_TRUE(r.ok);

    db_close(&db);                 /* release every node... */
    db_init(&db);                  /* ...then start over fresh */

    exec(&db, "SELECT 1", &r);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_FALSE(r.found);    /* the old data is gone */

    exec(&db, "INSERT 2 20", &r);
    TEST_ASSERT_TRUE(r.ok);

    exec(&db, "SELECT 2", &r);
    TEST_ASSERT_TRUE(r.found);
    TEST_ASSERT_EQUAL_INT(20, r.value);

    db_close(&db);
}

/* --------------------------------------------------------------------------
 * PRINT (and the REPL-only commands) are acknowledged with ok = true
 * ------------------------------------------------------------------------ */

void test_db_print_reports_ok(void) {
    Database db;
    db_init(&db);
    DbResult r;

    exec(&db, "INSERT 7 70", &r);

    /* PRINT dumps the tree to stdout; the REPL only cares that it was ok. */
    exec(&db, "PRINT", &r);
    TEST_ASSERT_TRUE(r.ok);

    /* EXIT/HELP are handled by the REPL; the database just acks them. */
    exec(&db, "EXIT", &r);
    TEST_ASSERT_TRUE(r.ok);
    exec(&db, "HELP", &r);
    TEST_ASSERT_TRUE(r.ok);

    db_close(&db);
}

/* --------------------------------------------------------------------------
 * Missing keys: select/delete report not-found without failing
 * ------------------------------------------------------------------------ */

void test_db_select_missing_key(void) {
    Database db;
    db_init(&db);
    DbResult r;

    exec(&db, "SELECT 404", &r);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_FALSE(r.found);

    exec(&db, "DELETE 404", &r);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_FALSE(r.found);

    db_close(&db);
}
