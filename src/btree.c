/*
 *  ============================================================================
 *  btree.c -- Order-3 B+ tree implementation
 *  ============================================================================
 *
 *  STYLE CONTRACT (enforced for this whole project):
 *    - Every malloc / free / pointer assignment / struct modification is
 *      commented with WHY it happens. This file is meant to be read as a
 *      step-by-step textbook.
 *    - NO "clever" pointer shortcuts: all element moves use plain array
 *      indexing on explicit lines.
 *    - size_t is used STRICTLY for: (a) the key_count fields and (b) the loop
 *      counters that iterate a node's keys[] / children[] arrays. Every other
 *      loop counter in the project is a plain int.
 *
 *  ============================================================================
 *  INSERTION -- SPLIT & PROMOTE  (order 3: a node may hold at most 2 keys)
 *  ============================================================================
 *
 *  We always insert into a leaf, keeping keys sorted inside it. If the leaf
 *  would end up holding 3 keys (overflow), we split it and COPY the middle
 *  key up to the parent as a new separator:
 *
 *   (1) sorted insert overflows the leaf:
 *         before:  [ 30 | 40 ]   ->  after:  [ 30 | 40 | 42 ]   (3 > 2 !)
 *
 *   (2) SPLIT the leaf; COPY the middle key upward:
 *
 *                         [ 40 ]            <- promoted separator (copied)
 *                        /       \
 *                 [ 30 ]        [ 40 | 42 ]     new right leaf, chained via next
 *
 *   (3) If the parent already has 2 keys it overflows too, so the split
 *       propagates upward: a full internal node (3 keys / 4 children) splits
 *       into two nodes of 1 key / 2 children and PUSHES the middle key up.
 *       When the ROOT splits, a new root is created and the tree grows by
 *       exactly one level -- that is the only way the tree gets taller.
 *
 *  ============================================================================
 *  DELETION -- BORROW or MERGE  (order 3: a non-root node needs >= 1 key)
 *  ============================================================================
 *
 *  Deleting from a leaf can empty it (0 keys < minimum 1). Repair, in order:
 *
 *   (a) BORROW: if a sibling has 2 keys, take one across and fix the parent
 *       separator so the tree keeps its shape:
 *
 *                    [ 40 | 60 ]              [ 42 | 60 ]
 *                   /     |     \            /     |     \
 *            [ 30 ] [40|42] [60]    ->   [ 30 ] [40]  [60]    (42 moves up)
 *
 *   (b) MERGE: no sibling can spare a key, so merge the empty leaf with a
 *       sibling and DROP the separator out of the parent:
 *
 *                    [ 40 | 60 ]              [ 60 ]
 *                   /     |     \            /      \
 *            [ 30 ] [  ]  [60]    ->    [30|40]    [60]
 *
 *  The same borrow-or-merge rule repairs underflowed internal nodes (which
 *  must keep >= 2 children). If the root ends up with a single child, that
 *  child becomes the new root and the tree shrinks by one level.
 *  ============================================================================
 */

#include "btree.h"

#include <stdio.h>
#include <stdlib.h>

/* --------------------------------------------------------------------------
 * Node lifecycle helpers
 * ------------------------------------------------------------------------ */

/*
 * Allocates a single node with malloc(). WHY: every node is independent heap
 * memory whose lifetime is managed explicitly (freed in btree_destroy or when
 * two nodes merge). Caller takes ownership of the returned pointer.
 */
static BTreeNode* btree_node_create(BTreeNodeType type) {
    BTreeNode* node = malloc(sizeof(BTreeNode));
    /* malloc can legitimately return NULL on OOM; a toy DB simply bails out. */
    if (node == NULL) {
        fprintf(stderr, "btree: out of memory\n");
        exit(EXIT_FAILURE);
    }

    /* A fresh node starts empty; the invariant "key_count = number of live
     * keys" must hold from the very first moment the node is reachable. */
    node->type = type;
    node->key_count = 0;
    node->next = NULL;
    return node;
}

/*
 * Recursively frees the whole subtree below node, then node itself. WHY: the
 * tree owns every node; a correct teardown must release the children before
 * the parent, exactly like unwinding a stack (bottom-up).
 */
static void btree_destroy_node(BTreeNode* node) {
    if (node->type == BTREE_NODE_INTERNAL) {
        /* Free each child subtree first; children[0..key_count] = key_count+1. */
        for (size_t i = 0; i <= node->key_count; i++) {
            btree_destroy_node(node->u.children[i]);
        }
    }
    /* Only after every descendant is freed is it safe to release this node. */
    free(node);
}

/* --------------------------------------------------------------------------
 * Navigation helpers (all return indices over node->keys[])
 * ------------------------------------------------------------------------ */

/*
 * Returns the first index i such that node->keys[i] > key.
 * If every key is <= key, returns node->key_count (== one past the last key).
 * WHY the rule "first key strictly greater": an internal separator k_i is a
 * COPY of the smallest key in subtree children[i+1], so a search for k_i must
 * continue into children[i+1]; descending only while keys[i] <= key does that.
 */
static size_t btree_first_greater(const BTreeNode* node, int key) {
    size_t i = 0;
    while (i < node->key_count && node->keys[i] <= key) {
        i++;
    }
    return i;
}

/* --------------------------------------------------------------------------
 * Insertion
 * ------------------------------------------------------------------------ */

/*
 * Recursive insert into the subtree rooted at 'node'.
 *
 * Returns true if 'node' OVERFLOWED and had to split; in that case:
 *     *promoted_key   = the middle key that must travel upward,
 *     *promoted_child = the newly allocated right half of the split.
 * If it returns false, *promoted_* are left untouched.
 *
 * *inserted reports whether a brand-new key was stored (false => an existing
 * key's value was just overwritten).
 */
static bool btree_insert_into_node(BTreeNode* node, int key, int value,
                                   bool* inserted,
                                   int* promoted_key,
                                   BTreeNode** promoted_child) {
    if (node->type == BTREE_NODE_LEAF) {
        /* Find the sorted position: first index whose key is not < key. */
        size_t i = 0;
        while (i < node->key_count && node->keys[i] < key) {
            i++;
        }

        /* Duplicate key: keep one entry per key, just update the value. */
        if (i < node->key_count && node->keys[i] == key) {
            node->u.values[i] = value;
            *inserted = false;
            return false;
        }

        /* Shift every key/value at/after i one slot to the right to open i. */
        for (size_t j = node->key_count; j > i; j--) {
            node->keys[j] = node->keys[j - 1];
            node->u.values[j] = node->u.values[j - 1];
        }
        node->keys[i] = key;
        node->u.values[i] = value;
        node->key_count++;
        *inserted = true;

        /* No overflow: the node stays within [1, 2] keys. */
        if (node->key_count <= BTREE_MAX_KEYS) {
            return false;
        }

        /* Overflow (3 keys). SPLIT: left keeps keys[0], a new right leaf takes
         * keys[1] and keys[2]; the middle key (= right's first key) is COPIED
         * upward as the separator. */
        BTreeNode* right = btree_node_create(BTREE_NODE_LEAF);
        size_t right_count = node->key_count - 1; /* drop the promoted key */
        for (size_t j = 0; j < right_count; j++) {
            right->keys[j] = node->keys[1 + j];
            right->u.values[j] = node->u.values[1 + j];
        }
        right->key_count = right_count;

        /* Copy (not move) the middle key up to the parent. */
        *promoted_key = right->keys[0];
        *promoted_child = right;

        /* Left leaf keeps only keys[0] (indexes 0..0). */
        node->key_count = 1;

        /* Splice the new leaf into the leaf linked list, right after 'node'. */
        right->next = node->next;
        node->next = right;

        return true;
    }

    /* ---- INTERNAL node ---- */
    /* Descend into the child that may contain key. */
    size_t i = btree_first_greater(node, key);
    BTreeNode* child = node->u.children[i];

    bool child_inserted = false;
    int child_promoted_key = 0;
    BTreeNode* child_promoted_child = NULL;
    bool child_split = btree_insert_into_node(child, key, value,
                                              &child_inserted,
                                              &child_promoted_key,
                                              &child_promoted_child);
    *inserted = child_inserted;

    if (!child_split) {
        return false;
    }

    /* Child split: absorb the promoted key + child into this internal node.
     * The promoted key becomes keys[i], the new child sits right after it. */
    for (size_t j = node->key_count; j > i; j--) {
        node->keys[j] = node->keys[j - 1];
    }
    for (size_t j = node->key_count + 1; j > i + 1; j--) {
        node->u.children[j] = node->u.children[j - 1];
    }
    node->keys[i] = child_promoted_key;
    node->u.children[i + 1] = child_promoted_child;
    node->key_count++;

    if (node->key_count <= BTREE_MAX_KEYS) {
        return false;
    }

    /* Overflow (3 keys / 4 children). SPLIT internal node in half:
     *   left  keeps keys[0]      and children[0], children[1]   (1 key)
     *   right takes keys[2]      and children[2], children[3]   (1 key)
     *   keys[1] (the middle key) is PUSHED upward. */
    size_t mid = node->key_count / 2;          /* 3 / 2 == 1 */
    *promoted_key = node->keys[mid];

    BTreeNode* right = btree_node_create(BTREE_NODE_INTERNAL);
    size_t right_key_count = node->key_count - mid - 1; /* keys above mid */
    for (size_t j = 0; j < right_key_count; j++) {
        right->keys[j] = node->keys[mid + 1 + j];
    }
    for (size_t j = 0; j <= right_key_count; j++) {
        right->u.children[j] = node->u.children[mid + 1 + j];
    }
    right->key_count = right_key_count;

    /* Trim the left half: drop the promoted key and the children it routed. */
    node->key_count = mid;

    *promoted_child = right;
    return true;
}

bool btree_insert(BTree* tree, int key, int value) {
    bool inserted = false;
    int promoted_key = 0;
    BTreeNode* promoted_child = NULL;

    bool root_split = btree_insert_into_node(tree->root, key, value,
                                             &inserted, &promoted_key,
                                             &promoted_child);
    if (root_split) {
        /* The root overflowed: grow a new root above the two halves. WHY a new
         * node: the old root must keep its identity as a child of the new one. */
        BTreeNode* new_root = btree_node_create(BTREE_NODE_INTERNAL);
        new_root->keys[0] = promoted_key;
        new_root->u.children[0] = tree->root;
        new_root->u.children[1] = promoted_child;
        new_root->key_count = 1;
        tree->root = new_root;
    }

    if (inserted) {
        tree->count++;
    }
    return inserted;
}

/* --------------------------------------------------------------------------
 * Deletion (borrow-or-merge, propagating underflow upward)
 * ------------------------------------------------------------------------ */

/* Frees the underflowed child at parent->u.children[child_index], OR the
 * sibling it merges with, and rewires the parent. Only called when that child
 * is underflowed; returns nothing (the caller re-checks its own key_count). */
static void btree_rebalance_at(BTreeNode* parent, size_t child_index) {
    BTreeNode* child = parent->u.children[child_index];

    /* --- 1. try to BORROW from the LEFT sibling (has 2 keys) --- */
    if (child_index > 0) {
        BTreeNode* left = parent->u.children[child_index - 1];
        if (left->key_count > 1) {
            if (child->type == BTREE_NODE_LEAF) {
                /* Move left's largest entry into the front of the empty leaf. */
                child->keys[0] = left->keys[left->key_count - 1];
                child->u.values[0] = left->u.values[left->key_count - 1];
                left->key_count--;
                child->key_count = 1;
                /* The separator must equal the NEW MINIMUM of the right child
                 * (the key we just borrowed), because every separator in this
                 * tree is a copy of the smallest key of the subtree to its
                 * right, and the search rule routes "key == separator" to the
                 * right. Using the left child's new maximum instead would
                 * break that invariant and lose equal-to-separator lookups. */
                parent->keys[child_index - 1] = child->keys[0];
            } else {
                /* Rotate: parent separator drops into child's front, left's
                 * last key rises into the parent, left's last child slides
                 * across into child's front. Child goes 0 keys/1 child -> 1/2. */
                child->u.children[1] = child->u.children[0];
                child->u.children[0] = left->u.children[left->key_count];
                child->keys[0] = parent->keys[child_index - 1];
                parent->keys[child_index - 1] = left->keys[left->key_count - 1];
                left->key_count--;
                child->key_count = 1;
            }
            return;
        }
    }

    /* --- 2. try to BORROW from the RIGHT sibling (has 2 keys) --- */
    if (child_index < parent->key_count) {
        BTreeNode* right = parent->u.children[child_index + 1];
        if (right->key_count > 1) {
            if (child->type == BTREE_NODE_LEAF) {
                /* Move right's smallest entry into the empty leaf. */
                child->keys[0] = right->keys[0];
                child->u.values[0] = right->u.values[0];
                for (size_t j = 0; j + 1 < right->key_count; j++) {
                    right->keys[j] = right->keys[j + 1];
                    right->u.values[j] = right->u.values[j + 1];
                }
                right->key_count--;
                child->key_count = 1;
                /* The separator must equal the new smallest key of 'right'. */
                parent->keys[child_index] = right->keys[0];
            } else {
                /* Rotate: parent separator drops into child's back, right's
                 * first key rises into the parent, right's first child slides
                 * across into child's back. Child goes 0 keys/1 child -> 1/2. */
                child->keys[0] = parent->keys[child_index];
                child->u.children[1] = right->u.children[0];
                parent->keys[child_index] = right->keys[0];
                for (size_t j = 0; j + 1 < right->key_count; j++) {
                    right->keys[j] = right->keys[j + 1];
                }
                for (size_t j = 0; j + 1 < right->key_count + 1; j++) {
                    right->u.children[j] = right->u.children[j + 1];
                }
                right->key_count--;
                child->key_count = 1;
            }
            return;
        }
    }

    /* --- 3. no sibling can spare a key: MERGE --- */
    if (child_index > 0) {
        /* Merge 'child' INTO the left sibling, dropping the separator. */
        BTreeNode* left = parent->u.children[child_index - 1];
        if (child->type == BTREE_NODE_LEAF) {
            for (size_t j = 0; j < child->key_count; j++) {
                left->keys[left->key_count + j] = child->keys[j];
                left->u.values[left->key_count + j] = child->u.values[j];
            }
            left->key_count += child->key_count;
            left->next = child->next;   /* unlink the dead leaf from the chain */
        } else {
            /* Pull the separator down between the two internal halves. */
            left->keys[left->key_count] = parent->keys[child_index - 1];
            for (size_t j = 0; j < child->key_count; j++) {
                left->keys[left->key_count + 1 + j] = child->keys[j];
            }
            for (size_t j = 0; j < child->key_count + 1; j++) {
                left->u.children[left->key_count + 1 + j] = child->u.children[j];
            }
            left->key_count = left->key_count + 1 + child->key_count;
        }

        /* Release the merged-away child now that no pointers reference it. */
        free(child);

        /* Drop the separator key and the merged child pointer from the parent. */
        for (size_t j = child_index - 1; j + 1 < parent->key_count; j++) {
            parent->keys[j] = parent->keys[j + 1];
        }
        for (size_t j = child_index; j + 1 <= parent->key_count; j++) {
            parent->u.children[j] = parent->u.children[j + 1];
        }
        parent->key_count--;
    } else {
        /* child is at index 0: merge the RIGHT sibling INTO child instead. */
        BTreeNode* right = parent->u.children[child_index + 1];
        if (child->type == BTREE_NODE_LEAF) {
            for (size_t j = 0; j < right->key_count; j++) {
                child->keys[child->key_count + j] = right->keys[j];
                child->u.values[child->key_count + j] = right->u.values[j];
            }
            child->key_count += right->key_count;
            child->next = right->next;   /* unlink the dead leaf from the chain */
        } else {
            child->keys[child->key_count] = parent->keys[0];
            for (size_t j = 0; j < right->key_count; j++) {
                child->keys[child->key_count + 1 + j] = right->keys[j];
            }
            for (size_t j = 0; j < right->key_count + 1; j++) {
                child->u.children[child->key_count + 1 + j] = right->u.children[j];
            }
            child->key_count = child->key_count + 1 + right->key_count;
        }

        free(right);

        /* Drop separator keys[0] and the right child pointer from the parent. */
        for (size_t j = 0; j + 1 < parent->key_count; j++) {
            parent->keys[j] = parent->keys[j + 1];
        }
        for (size_t j = 1; j + 1 <= parent->key_count; j++) {
            parent->u.children[j] = parent->u.children[j + 1];
        }
        parent->key_count--;
    }
}

/*
 * Erases 'key' from the subtree rooted at 'node'.
 * Returns true if 'node' itself UNDERFLOWED (its parent must rebalance it).
 * A missing key is a no-op that reports no underflow.
 */
static bool btree_erase_node(BTreeNode* node, int key) {
    if (node->type == BTREE_NODE_LEAF) {
        /* Locate the key, if present. */
        size_t i = 0;
        while (i < node->key_count && node->keys[i] < key) {
            i++;
        }
        if (i >= node->key_count || node->keys[i] != key) {
            return false; /* absent: nothing to remove */
        }

        /* Close the gap by shifting every later key/value one slot left. */
        for (size_t j = i; j + 1 < node->key_count; j++) {
            node->keys[j] = node->keys[j + 1];
            node->u.values[j] = node->u.values[j + 1];
        }
        node->key_count--;

        /* A non-root leaf needs >= 1 key; 0 means the parent must fix us. */
        return node->key_count == 0;
    }

    /* ---- INTERNAL node ---- */
    /* Descend into the child that owns 'key', using the same rule as search:
     * child = children[first index whose key is strictly greater than 'key'].
     * A key that EQUALS a separator automatically routes to the child on the
     * separator's right, which is the subtree that actually holds that key's
     * leaf. No special-casing of separator keys is needed: separators are only
     * routing copies, and a separator left stale by a deletion remains a legal
     * divider (its value still sits between the max of the left child and the
     * min of the right child), so search order stays correct. */
    size_t i = btree_first_greater(node, key);
    BTreeNode* child = node->u.children[i];
    bool underflow = btree_erase_node(child, key);
    if (underflow) {
        btree_rebalance_at(node, i);
    }

    /* An internal node needs >= 1 key; if a merge took our last one, signal
     * the caller (which may be our own parent) to rebalance us too. */
    return node->key_count == 0;
}

bool btree_delete(BTree* tree, int key) {
    /* Cheap pre-check: if the key is not in the tree, do nothing at all. */
    if (btree_search(tree, key) == NULL) {
        return false;
    }

    btree_erase_node(tree->root, key);
    tree->count--;

    /* If the root became an internal node with a single child, promote that
     * child to be the new root and release the old root (tree shrinks). */
    if (tree->root->type == BTREE_NODE_INTERNAL && tree->root->key_count == 0) {
        BTreeNode* old_root = tree->root;
        tree->root = old_root->u.children[0];
        free(old_root);
    }
    return true;
}

/* --------------------------------------------------------------------------
 * Search
 * ------------------------------------------------------------------------ */

int* btree_search(BTree* tree, int key) {
    BTreeNode* node = tree->root;

    /* Walk down: at each internal node, follow the child that owns 'key'. */
    while (node->type == BTREE_NODE_INTERNAL) {
        size_t i = btree_first_greater(node, key);
        node = node->u.children[i];
    }

    /* We are at the one leaf that could contain the key; scan its <= 2 keys. */
    for (size_t i = 0; i < node->key_count; i++) {
        if (node->keys[i] == key) {
            return &node->u.values[i]; /* address of the live value slot */
        }
    }
    return NULL;
}

/* --------------------------------------------------------------------------
 * Range scan
 * ------------------------------------------------------------------------ */

void btree_range(const BTree* tree, int key_min, int key_max,
                 void (*visit)(int key, int value, void* context),
                 void* context) {
    BTreeNode* node = tree->root;

    /* Descend to the leaf that could hold key_min (or the next key after). */
    while (node->type == BTREE_NODE_INTERNAL) {
        size_t i = btree_first_greater(node, key_min);
        node = node->u.children[i];
    }

    /* Walk the sorted leaf chain and report every key inside the window. */
    while (node != NULL) {
        for (size_t i = 0; i < node->key_count; i++) {
            if (node->keys[i] > key_max) {
                return; /* sorted order: nothing further can match */
            }
            if (node->keys[i] >= key_min) {
                visit(node->keys[i], node->u.values[i], context);
            }
        }
        node = node->next;
    }
}

/* --------------------------------------------------------------------------
 * Misc
 * ------------------------------------------------------------------------ */

size_t btree_size(const BTree* tree) {
    return tree->count;
}

void btree_init(BTree* tree) {
    /* The empty tree is a single empty LEAF so that the root is never NULL and
     * every operation (search/insert/range) can treat the root uniformly. */
    tree->root = btree_node_create(BTREE_NODE_LEAF);
    tree->count = 0;
}

void btree_destroy(BTree* tree) {
    btree_destroy_node(tree->root);
    tree->root = NULL;
    tree->count = 0;
}

/* Recursively prints one node; internal nodes are shown as [k0 k1] with their
 * children indented underneath, leaves as [k0:v0 k1:v1]. */
static void btree_print_node(const BTreeNode* node, int depth) {
    for (int d = 0; d < depth; d++) {
        printf("  ");
    }
    if (node->type == BTREE_NODE_LEAF) {
        printf("leaf [");
        for (size_t i = 0; i < node->key_count; i++) {
            printf("%d:%d%s", node->keys[i], node->u.values[i],
                   i + 1 < node->key_count ? " " : "");
        }
        printf("]\n");
    } else {
        printf("internal [");
        for (size_t i = 0; i < node->key_count; i++) {
            printf("%d%s", node->keys[i], i + 1 < node->key_count ? " " : "");
        }
        printf("]\n");
        for (size_t i = 0; i <= node->key_count; i++) {
            btree_print_node(node->u.children[i], depth + 1);
        }
    }
}

void btree_print(const BTree* tree) {
    printf("-- tree (count=%zu) --\n", tree->count);
    btree_print_node(tree->root, 0);

    printf("leaf chain (sorted): ");
    BTreeNode* leaf = tree->root;
    while (leaf->type == BTREE_NODE_INTERNAL) {
        leaf = leaf->u.children[0];
    }
    while (leaf != NULL) {
        for (size_t i = 0; i < leaf->key_count; i++) {
            printf("%d:%d ", leaf->keys[i], leaf->u.values[i]);
        }
        leaf = leaf->next;
    }
    printf("\n");
}
