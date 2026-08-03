#ifndef MINIDB_BTREE_H
#define MINIDB_BTREE_H

#include <stdbool.h>
#include <stddef.h>

/*
 *  ============================================================================
 *  ORDER-3 B+ TREE -- CONCEPT
 *  ============================================================================
 *
 *  A B+ tree is a balanced multiway search tree with two properties that
 *  make it ideal for a database index:
 *
 *    1. EVERY key is stored in a LEAF node, together with its value. Internal
 *       nodes hold only "separator" keys used to route a search downward, so
 *       a search always ends at a leaf where the answer actually lives.
 *    2. Leaves are chained together with 'next' pointers into a sorted linked
 *       list. That chain lets a range scan visit the matching keys in sorted
 *       order without re-walking the tree.
 *
 *  "Order 3" fixes the maximum fan-out of a node:
 *      - every node holds at most  ORDER - 1 = 2 keys;
 *      - every internal node holds at most ORDER = 3 children;
 *      - every non-root node holds at least 1 key (2 children for internal
 *        nodes), so nodes stay reasonably full.
 *
 *                            INTERNAL node: routes, no values
 *
 *                                +-------+-------+
 *                                |  15   |  27   |
 *                                +---+---+---+---+
 *                                |       |       |
 *                          keys <15 15<k<27   keys>27
 *                                |       |       |
 *                                v       v       v
 *                     +-----+-----+  +-----+-----+  +-----+-----+
 *                     |  3  |  9  |  | 15  | 21  |  | 27  | 33  |
 *                     | 100 |  42 |  |  7  |  9  |  |  3  |  1  |  <- LEAF: key|value
 *                     +-----+-----+  +-----+-----+  +-----+-----+
 *                     |                 |                 |
 *                     |  next ----------+-----------------+  (leaf->next chain)
 *                     |                 |                 |
 *
 *  All leaves sit at the same depth. Splitting overfull nodes and repairing
 *  underfull ones keeps that depth uniform -- see the SPLIT/PROMOTE and
 *  BORROW/MERGE diagrams in btree.c.
 *
 *  Memory model: every node is heap-allocated with malloc() exactly once and
 *  freed exactly once (btree_destroy / node merges). Keys and values are plain
 *  ints stored inline inside the node struct, so there is no per-key heap
 *  allocation to leak.
 *  ============================================================================
 */

#define BTREE_ORDER        3
#define BTREE_MAX_KEYS     (BTREE_ORDER - 1)     /* 2 keys per node  */
#define BTREE_MAX_CHILDREN BTREE_ORDER           /* 3 children max   */

typedef enum {
    BTREE_NODE_LEAF,
    BTREE_NODE_INTERNAL
} BTreeNodeType;

typedef struct BTreeNode {
    BTreeNodeType type;
    size_t        key_count;                       /* how many keys are live (0..2) */

    /*
     * The arrays are sized MAX + 1 (a "spare slot"). WHY: during a split a node
     * briefly holds 3 keys / 4 children before it is trimmed back to 2 / 3. If
     * the arrays were exactly MAX elements, the transient 3rd key / 4th child
     * would be written out of bounds, straight into the neighbouring struct
     * field and silently corrupt pointers. The spare slot absorbs that overflow.
     * Persistent nodes never exceed BTREE_MAX_KEYS keys / BTREE_MAX_CHILDREN
     * children; the +1 slots are only ever touched mid-split.
     */
    int           keys[BTREE_MAX_KEYS + 1];        /* the node's own key slots   */

    /*
     * LEAF     -> u.values[i] is the value stored for keys[i]
     * INTERNAL -> u.children[i] is the subtree whose keys are below keys[i-1]
     */
    union {
        int               values[BTREE_MAX_KEYS + 1];       /* LEAF payload       */
        struct BTreeNode* children[BTREE_MAX_CHILDREN + 1]; /* INTERNAL children  */
    } u;

    struct BTreeNode* next;  /* LEAF only: pointer to the next leaf on the chain */
} BTreeNode;

typedef struct BTree {
    BTreeNode* root;  /* empty tree => root is an empty LEAF (key_count == 0) */
    size_t     count; /* total number of live key/value pairs in the tree    */
} BTree;

/* --------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------ */

/* Initialises an empty tree: allocates the empty root leaf. */
void btree_init(BTree* tree);

/* Frees every node in the tree, then resets the BTree to an empty state. */
void btree_destroy(BTree* tree);

/*
 * Inserts key/value. Returns true if a NEW key was stored; false if an
 * existing key's value was merely overwritten (a duplicate INSERT = update).
 */
bool btree_insert(BTree* tree, int key, int value);

/* Removes key if present. Returns true if the key was found and removed. */
bool btree_delete(BTree* tree, int key);

/*
 * Returns a pointer to the stored value for key, or NULL if absent.
 * WARNING: the pointer aliases memory inside the tree; it is only valid until
 * the next insertion/deletion that touches that leaf.
 */
int* btree_search(BTree* tree, int key);

/* Visits every key in [key_min, key_max] in ascending order. */
void btree_range(const BTree* tree, int key_min, int key_max,
                 void (*visit)(int key, int value, void* context),
                 void* context);

/* Total number of live key/value pairs. */
size_t btree_size(const BTree* tree);

/* Prints an ASCII dump of the tree structure (REPL "PRINT" command). */
void btree_print(const BTree* tree);

#endif /* MINIDB_BTREE_H */
