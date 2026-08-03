/*
 * test_btree.c - Unit tests for the order-3 B+ tree (src/btree.c, src/btree.h).
 *
 * WHY each test exists (educational walk-through of what is being checked):
 *   - insert/search            : the basic contract of a key-value store.
 *   - duplicate insert         : re-inserting a key UPDATES, it never grows
 *                                the tree (B+ trees keep one entry per key).
 *   - ascending / descending   : bulk loads force leaf, internal AND root
 *                                splits; every inserted key must still be
 *                                found afterwards, in both orderings.
 *   - range scan               : inclusive bounds, ascending order, and
 *                                windows that match nothing.
 *   - delete                   : shrink paths, deleting keys that act as
 *                                internal separators too.
 *   - empty-tree safety        : every API must be a no-op on an empty tree.
 *   - differential test        : ~20k random inserts/deletes checked against
 *                                a plain sorted array as ground truth. If the
 *                                tree ever diverges, this test catches it.
 */

#include "unity.h"

#include "btree.h"

#include <stdio.h>
#include <stdlib.h>

/* --------------------------------------------------------------------------
 * Range-scan collector
 *
 * btree_range() fires a callback per matching key. This collector copies the
 * visited (key, value) pairs into caller-supplied buffers so the tests can
 * assert on the exact ordered result set.
 * ------------------------------------------------------------------------ */

typedef struct {
    int* keys;
    int* values;
    int  count;
    int  capacity;
} RangeCollector;

static void collect_visit(int key, int value, void* context) {
    RangeCollector* c = (RangeCollector*)context;
    TEST_ASSERT(c->count < c->capacity);  /* guard: never overflow the buffers */
    c->keys[c->count] = key;
    c->values[c->count] = value;
    c->count++;
}

static void range_to_buffers(const BTree* tree, int lo, int hi,
                             int* keys, int* values, int* count) {
    RangeCollector c;
    c.keys = keys;
    c.values = values;
    c.count = 0;
    c.capacity = 512;
    btree_range(tree, lo, hi, collect_visit, &c);
    *count = c.count;
}

/* --------------------------------------------------------------------------
 * Insert + search
 * ------------------------------------------------------------------------ */

void test_btree_insert_search(void) {
    BTree tree;
    btree_init(&tree);

    /* Newly inserted keys must be searchable and must return their value. */
    TEST_ASSERT_TRUE(btree_insert(&tree, 42, 100));
    TEST_ASSERT_TRUE(btree_insert(&tree, 7, 3));
    TEST_ASSERT_TRUE(btree_insert(&tree, 1000, -5));
    TEST_ASSERT_EQUAL_UINT(3u, (unsigned)btree_size(&tree));

    int* v = btree_search(&tree, 42);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_INT(100, *v);

    v = btree_search(&tree, 7);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_INT(3, *v);

    v = btree_search(&tree, 1000);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_INT(-5, *v);

    /* Keys that were never inserted must come back NULL, not a value. */
    TEST_ASSERT_NULL(btree_search(&tree, 41));
    TEST_ASSERT_NULL(btree_search(&tree, 43));
    TEST_ASSERT_NULL(btree_search(&tree, 0));
    TEST_ASSERT_NULL(btree_search(&tree, 999));
    TEST_ASSERT_NULL(btree_search(&tree, -1));

    btree_destroy(&tree);
}

/* --------------------------------------------------------------------------
 * Duplicate insert = update, never a second entry
 * ------------------------------------------------------------------------ */

void test_btree_duplicate_updates_value(void) {
    BTree tree;
    btree_init(&tree);

    TEST_ASSERT_TRUE(btree_insert(&tree, 42, 100));
    TEST_ASSERT_EQUAL_UINT(1u, (unsigned)btree_size(&tree));

    /* Re-inserting the same key reports false (not a new key) and does NOT
     * grow the tree -- this is the whole "duplicate = update" contract. */
    TEST_ASSERT_FALSE(btree_insert(&tree, 42, 200));
    TEST_ASSERT_EQUAL_UINT(1u, (unsigned)btree_size(&tree));

    int* v = btree_search(&tree, 42);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_INT(200, *v);  /* the stored value was overwritten */

    btree_destroy(&tree);
}

/* --------------------------------------------------------------------------
 * Bulk loads that force splits (both orderings)
 * ------------------------------------------------------------------------ */

void test_btree_ascending_insert_splits(void) {
    BTree tree;
    btree_init(&tree);

    /* 1..200 with a fan-out of 3 forces leaf splits, internal splits, and
     * finally root splits. The count must reflect every successful insert. */
    for (int k = 1; k <= 200; k++) {
        TEST_ASSERT_TRUE(btree_insert(&tree, k, k * 10));
    }
    TEST_ASSERT_EQUAL_UINT(200u, (unsigned)btree_size(&tree));

    /* After all that reshuffling every single key must still be reachable. */
    for (int k = 1; k <= 200; k++) {
        int* v = btree_search(&tree, k);
        TEST_ASSERT_NOT_NULL(v);
        TEST_ASSERT_EQUAL_INT(k * 10, *v);
    }

    /* Keys just outside the inserted range must be absent. */
    TEST_ASSERT_NULL(btree_search(&tree, 0));
    TEST_ASSERT_NULL(btree_search(&tree, 201));

    btree_destroy(&tree);
}

void test_btree_descending_insert_splits(void) {
    BTree tree;
    btree_init(&tree);

    /* Descending order puts every new minimum on the far-left path, which
     * stresses the separator-promotion logic just as hard as ascending. */
    for (int k = 200; k >= 1; k--) {
        TEST_ASSERT_TRUE(btree_insert(&tree, k, k * 10));
    }
    TEST_ASSERT_EQUAL_UINT(200u, (unsigned)btree_size(&tree));

    for (int k = 200; k >= 1; k--) {
        int* v = btree_search(&tree, k);
        TEST_ASSERT_NOT_NULL(v);
        TEST_ASSERT_EQUAL_INT(k * 10, *v);
    }

    TEST_ASSERT_NULL(btree_search(&tree, 0));
    TEST_ASSERT_NULL(btree_search(&tree, 201));

    btree_destroy(&tree);
}

/* --------------------------------------------------------------------------
 * Range scan: ordered, inclusive, empty windows
 * ------------------------------------------------------------------------ */

void test_btree_range_scan(void) {
    BTree tree;
    btree_init(&tree);

    for (int k = 1; k <= 20; k++) {
        btree_insert(&tree, k, k * 10);
    }

    int keys[512];
    int values[512];
    int count;

    /* Inclusive bounds: BOTH endpoints appear, and the order is ascending. */
    range_to_buffers(&tree, 5, 10, keys, values, &count);
    TEST_ASSERT_EQUAL_INT(6, count);
    for (int i = 0; i < count; i++) {
        TEST_ASSERT_EQUAL_INT(5 + i, keys[i]);
        TEST_ASSERT_EQUAL_INT((5 + i) * 10, values[i]);
    }

    /* A single-point window yields exactly that one key. */
    range_to_buffers(&tree, 5, 5, keys, values, &count);
    TEST_ASSERT_EQUAL_INT(1, count);
    TEST_ASSERT_EQUAL_INT(5, keys[0]);
    TEST_ASSERT_EQUAL_INT(50, values[0]);

    /* The full window covers the entire tree in sorted order. */
    range_to_buffers(&tree, 1, 20, keys, values, &count);
    TEST_ASSERT_EQUAL_INT(20, count);
    TEST_ASSERT_EQUAL_INT(1, keys[0]);
    TEST_ASSERT_EQUAL_INT(20, keys[19]);

    /* Windows that do not overlap the data must report zero matches. */
    range_to_buffers(&tree, 0, 0, keys, values, &count);
    TEST_ASSERT_EQUAL_INT(0, count);
    range_to_buffers(&tree, 21, 30, keys, values, &count);
    TEST_ASSERT_EQUAL_INT(0, count);
    range_to_buffers(&tree, -50, -1, keys, values, &count);
    TEST_ASSERT_EQUAL_INT(0, count);

    /* Inverted bounds (lo > hi) must also yield an empty result set. */
    range_to_buffers(&tree, 15, 5, keys, values, &count);
    TEST_ASSERT_EQUAL_INT(0, count);

    btree_destroy(&tree);
}

/* --------------------------------------------------------------------------
 * Delete: separators, neighbours, no-op deletes
 * ------------------------------------------------------------------------ */

void test_btree_delete_keys(void) {
    BTree tree;
    btree_init(&tree);

    for (int k = 1; k <= 50; k++) {
        btree_insert(&tree, k, k);
    }
    TEST_ASSERT_EQUAL_UINT(50u, (unsigned)btree_size(&tree));

    /* Delete a spread of keys. Some of these (e.g. 20, 30) act as internal
     * separator copies, so deleting them exercises the borrow/merge repair
     * paths on non-leaf nodes. */
    for (int victim = 10; victim <= 40; victim += 10) {
        TEST_ASSERT_TRUE(btree_delete(&tree, victim));
    }

    TEST_ASSERT_EQUAL_UINT(46u, (unsigned)btree_size(&tree));

    /* The victims must be gone... */
    TEST_ASSERT_NULL(btree_search(&tree, 10));
    TEST_ASSERT_NULL(btree_search(&tree, 20));
    TEST_ASSERT_NULL(btree_search(&tree, 30));
    TEST_ASSERT_NULL(btree_search(&tree, 40));

    /* ...while their immediate neighbours survive with intact values. */
    TEST_ASSERT_EQUAL_INT(9, *btree_search(&tree, 9));
    TEST_ASSERT_EQUAL_INT(11, *btree_search(&tree, 11));
    TEST_ASSERT_EQUAL_INT(19, *btree_search(&tree, 19));
    TEST_ASSERT_EQUAL_INT(21, *btree_search(&tree, 21));
    TEST_ASSERT_EQUAL_INT(39, *btree_search(&tree, 39));
    TEST_ASSERT_EQUAL_INT(41, *btree_search(&tree, 41));
    TEST_ASSERT_EQUAL_INT(25, *btree_search(&tree, 25));

    /* Deleting an absent key is a safe no-op that leaves the size alone. */
    TEST_ASSERT_FALSE(btree_delete(&tree, 10));
    TEST_ASSERT_FALSE(btree_delete(&tree, 500));
    TEST_ASSERT_EQUAL_UINT(46u, (unsigned)btree_size(&tree));

    btree_destroy(&tree);
}

void test_btree_delete_all_empties_tree(void) {
    BTree tree;
    btree_init(&tree);

    for (int k = 1; k <= 30; k++) {
        btree_insert(&tree, k, k * 2);
    }

    /* Deleting every key in ascending order empties the tree completely and
     * shrinks the root back down as the tree loses levels. */
    for (int k = 1; k <= 30; k++) {
        TEST_ASSERT_TRUE(btree_delete(&tree, k));
    }

    TEST_ASSERT_EQUAL_UINT(0u, (unsigned)btree_size(&tree));
    for (int k = 1; k <= 30; k++) {
        TEST_ASSERT_NULL(btree_search(&tree, k));
    }

    btree_destroy(&tree);
}

/* --------------------------------------------------------------------------
 * Empty-tree safety
 * ------------------------------------------------------------------------ */

void test_btree_empty_tree_safe(void) {
    BTree tree;
    btree_init(&tree);

    /* An empty tree: size 0, no key found, delete is a no-op, ranges empty. */
    TEST_ASSERT_EQUAL_UINT(0u, (unsigned)btree_size(&tree));
    TEST_ASSERT_NULL(btree_search(&tree, 1));
    TEST_ASSERT_FALSE(btree_delete(&tree, 1));

    int keys[8];
    int values[8];
    int count;
    range_to_buffers(&tree, 0, 100, keys, values, &count);
    TEST_ASSERT_EQUAL_INT(0, count);

    /* The empty tree must still accept inserts afterwards. */
    TEST_ASSERT_TRUE(btree_insert(&tree, 1, 11));
    TEST_ASSERT_EQUAL_UINT(1u, (unsigned)btree_size(&tree));
    TEST_ASSERT_EQUAL_INT(11, *btree_search(&tree, 1));

    btree_destroy(&tree);
}

/* --------------------------------------------------------------------------
 * Re-insert after delete
 * ------------------------------------------------------------------------ */

void test_btree_reinsert_after_delete(void) {
    BTree tree;
    btree_init(&tree);

    btree_insert(&tree, 42, 100);
    btree_delete(&tree, 42);
    TEST_ASSERT_NULL(btree_search(&tree, 42));

    /* A deleted key can come back with a brand-new value, like a fresh key. */
    TEST_ASSERT_TRUE(btree_insert(&tree, 42, 500));
    TEST_ASSERT_EQUAL_UINT(1u, (unsigned)btree_size(&tree));
    TEST_ASSERT_EQUAL_INT(500, *btree_search(&tree, 42));

    btree_destroy(&tree);
}

/* --------------------------------------------------------------------------
 * Randomized differential test (the critical one)
 *
 * Ground truth: a plain, sorted, fixed-size array of {key, value} pairs with
 * an int counter. The B+ tree and the reference must agree on the total size
 * and on the result of EVERY lookup in [0, 512) after every 500 operations.
 * Any structural bug (wrong separator, broken leaf chain, lost key) that the
 * hand-picked tests miss will almost certainly show up here.
 * ------------------------------------------------------------------------ */

#define REF_MAX 512

static int ref_keys[REF_MAX];
static int ref_values[REF_MAX];
static int ref_count;

static void ref_clear(void) {
    ref_count = 0;
}

/* Insert key/value into the reference array, keeping keys sorted. */
static void ref_insert(int key, int value) {
    int i = 0;
    while (i < ref_count && ref_keys[i] < key) {
        i++;
    }
    if (i < ref_count && ref_keys[i] == key) {
        ref_values[i] = value;   /* duplicate: update in place, count unchanged */
        return;
    }
    for (int j = ref_count; j > i; j--) {
        ref_keys[j] = ref_keys[j - 1];
        ref_values[j] = ref_values[j - 1];
    }
    ref_keys[i] = key;
    ref_values[i] = value;
    ref_count++;
}

static int ref_find(int key) {
    for (int i = 0; i < ref_count; i++) {
        if (ref_keys[i] == key) {
            return i;
        }
    }
    return -1;
}

static void ref_delete(int key) {
    int i = ref_find(key);
    if (i < 0) {
        return;   /* absent: nothing to do */
    }
    for (int j = i; j + 1 < ref_count; j++) {
        ref_keys[j] = ref_keys[j + 1];
        ref_values[j] = ref_values[j + 1];
    }
    ref_count--;
}

/* Full agreement check: sizes must match and every lookup must agree. */
static void assert_tree_matches_reference(BTree* tree) {
    TEST_ASSERT_EQUAL_UINT((unsigned)ref_count, (unsigned)btree_size(tree));

    for (int key = 0; key < REF_MAX; key++) {
        int i = ref_find(key);
        int* v = btree_search(tree, key);
        if (i >= 0) {
            /* Reference says present: the tree must have it, with same value. */
            TEST_ASSERT_NOT_NULL_MESSAGE(
                v, "key present in reference but missing in the tree");
            if (v != NULL) {
                TEST_ASSERT_EQUAL_INT(ref_values[i], *v);
            }
        } else {
            /* Reference says absent: the tree must NOT return it. */
            TEST_ASSERT_NULL_MESSAGE(
                v, "key absent in reference but found in the tree");
        }
    }
}

void test_btree_differential_random(void) {
    BTree tree;
    btree_init(&tree);
    ref_clear();

    srand(42);   /* fixed seed: the sequence is fully reproducible */

    const int ops = 20000;
    const int check_every = 500;

    for (int op = 0; op < ops; op++) {
        int key = rand() % REF_MAX;

        if ((rand() % 2) == 0) {
            int value = rand() % 100000;
            btree_insert(&tree, key, value);
            ref_insert(key, value);
        } else {
            btree_delete(&tree, key);
            ref_delete(key);
        }

        /* Every 500 operations, prove the tree still agrees with the truth. */
        if ((op + 1) % check_every == 0) {
            assert_tree_matches_reference(&tree);
        }
    }

    /* And one final full sweep at the very end. */
    assert_tree_matches_reference(&tree);

    btree_destroy(&tree);
}
