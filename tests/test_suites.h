#ifndef MINIDB_TEST_SUITES_H
#define MINIDB_TEST_SUITES_H

/*
 * Declarations for every test function, one per suite file. run_tests.c
 * includes this header and RUN_TEST()s each function by name, so all tests
 * must be defined non-static in their suite file.
 */

/* test_btree.c */
void test_btree_insert_search(void);
void test_btree_duplicate_updates_value(void);
void test_btree_ascending_insert_splits(void);
void test_btree_descending_insert_splits(void);
void test_btree_range_scan(void);
void test_btree_delete_keys(void);
void test_btree_delete_all_empties_tree(void);
void test_btree_empty_tree_safe(void);
void test_btree_reinsert_after_delete(void);
void test_btree_differential_random(void);

/* test_lexer.c */
void test_lex_insert_basic(void);
void test_lex_keyword_case_insensitive(void);
void test_lex_whitespace_tolerance(void);
void test_lex_bad_number_overflow(void);
void test_lex_unknown_word(void);
void test_lex_stray_symbol(void);
void test_lex_empty_input(void);
void test_lex_too_many_tokens(void);
void test_lex_keyword_to_string(void);

/* test_parser.c */
void test_parse_all_valid_commands(void);
void test_parse_missing_argument(void);
void test_parse_extra_argument(void);
void test_parse_bad_argument(void);
void test_parse_unknown_command(void);
void test_parse_empty(void);
void test_parse_result_and_type_strings(void);
void test_parse_error_resets_command(void);

/* test_database.c */
void test_db_insert_select_delete(void);
void test_db_update_value(void);
void test_db_range_matched_count(void);
void test_db_reinit_after_close(void);
void test_db_print_reports_ok(void);
void test_db_select_missing_key(void);

#endif /* MINIDB_TEST_SUITES_H */
