/*
 * run_tests.c - Test main: registers and runs every suite in this project.
 *
 * Each suite file declares its test functions non-static; they are all
 * declared here (via test_suites.h) and handed to RUN_TEST().
 */

#include "unity.h"

#include "test_suites.h"

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();

    /* ---- B+ tree ---- */
    RUN_TEST(test_btree_insert_search);
    RUN_TEST(test_btree_duplicate_updates_value);
    RUN_TEST(test_btree_ascending_insert_splits);
    RUN_TEST(test_btree_descending_insert_splits);
    RUN_TEST(test_btree_range_scan);
    RUN_TEST(test_btree_delete_keys);
    RUN_TEST(test_btree_delete_all_empties_tree);
    RUN_TEST(test_btree_empty_tree_safe);
    RUN_TEST(test_btree_reinsert_after_delete);
    RUN_TEST(test_btree_differential_random);

    /* ---- Lexer ---- */
    RUN_TEST(test_lex_insert_basic);
    RUN_TEST(test_lex_keyword_case_insensitive);
    RUN_TEST(test_lex_whitespace_tolerance);
    RUN_TEST(test_lex_bad_number_overflow);
    RUN_TEST(test_lex_unknown_word);
    RUN_TEST(test_lex_stray_symbol);
    RUN_TEST(test_lex_empty_input);
    RUN_TEST(test_lex_too_many_tokens);
    RUN_TEST(test_lex_keyword_to_string);

    /* ---- Parser ---- */
    RUN_TEST(test_parse_all_valid_commands);
    RUN_TEST(test_parse_missing_argument);
    RUN_TEST(test_parse_extra_argument);
    RUN_TEST(test_parse_bad_argument);
    RUN_TEST(test_parse_unknown_command);
    RUN_TEST(test_parse_empty);
    RUN_TEST(test_parse_result_and_type_strings);
    RUN_TEST(test_parse_error_resets_command);

    /* ---- Database (end to end) ---- */
    RUN_TEST(test_db_insert_select_delete);
    RUN_TEST(test_db_update_value);
    RUN_TEST(test_db_range_matched_count);
    RUN_TEST(test_db_reinit_after_close);
    RUN_TEST(test_db_print_reports_ok);
    RUN_TEST(test_db_select_missing_key);

    return UNITY_END();
}
