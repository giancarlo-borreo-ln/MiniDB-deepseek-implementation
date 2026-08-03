/*
 * database.c - Implementation of the MiniDB integration layer.
 *
 * db_execute() is the QUERY ENGINE of MiniDB. It is the single seam where the
 * parsed frontend (a Command, produced by lexer + parser) meets the storage
 * backend (the B+ tree). Every REPL command funnels through this one switch,
 * which is exactly where a real database would grow a planner/optimiser: it
 * would rewrite the Command, choose access paths, or compile a query plan
 * before touching the tree. Here the "plan" is trivial -- one switch case, one
 * tree call -- but the architectural position is the same.
 *
 * The switch is deliberately a simple dispatch table:
 *   - each case translates one CommandType into one BTree call;
 *   - each case sets result->ok and a short human-readable message;
 *   - nothing here knows about tokens or grammar, only about Commands.
 */

#include "database.h"
#include "parser.h"   /* Command/CommandType (also pulled in via database.h) */
#include "btree.h"    /* BTree API (also pulled in via database.h)          */

#include <stdio.h>    /* snprintf                    */
#include <string.h>   /* memset                      */

/*
 * range_visit() is the callback btree_range() fires for every matching key.
 * BTree passes (key, value, context) for each key inside the window; the
 * caller-controlled `context` pointer is how a callback talks back to the
 * stack. We only need a running count, so the accumulator lives on the caller's
 * stack and we mutate it through the void* context. This is the standard C
 * idiom for a "out-parameters without globals" callback contract.
 */
typedef struct {
    int matched;   /* number of keys visited inside [lo, hi] */
} RangeAccumulator;

static void range_visit(int key, int value, void* context) {
    RangeAccumulator* acc = (RangeAccumulator*)context; /* caller's stack frame */
    (void)key;      /* we only count; the key itself is not needed */
    (void)value;    /* ditto for the value                          */
    acc->matched++; /* one more key satisfied the window predicate  */
}

void db_init(Database* db) {
    /* Zero the whole struct first so no field starts with garbage; then let
     * btree_init() lay down its empty root leaf inside tree. Order matters:
     * btree_init() only touches tree, and tree is inside db. */
    memset(db, 0, sizeof(Database));
    btree_init(&db->tree);
}

void db_close(Database* db) {
    /* Hand every node back to the heap; afterwards the tree is empty and
     * db itself is a thin, harmless struct the REPL simply stops using. */
    btree_destroy(&db->tree);
}

void db_execute(Database* db, const Command* command, DbResult* result) {
    /*
     * Defensive defaults FIRST, before any dispatch: every branch below
     * overwrites only the fields it knows about, so an exotic future
     * CommandType still yields a well-formed (ok=false) result instead of
     * returning uninitialised stack memory to the REPL.
     */
    result->ok = false;
    result->found = false;
    result->value = 0;
    result->matched = 0;
    result->message[0] = '\0';

    /*
     * The dispatch table. command->type is set by the parser; the fields we
     * dereference on `command` are exactly the ones the parser filled for that
     * command's arity (key/value for INSERT, key/key_end for RANGE, ...).
     * Reading the wrong field would be reading parser-initialised zeros,
     * never random memory -- the parser zeroes the whole Command first.
     */
    switch (command->type) {

    case COMMAND_INSERT: {
        /*
         * btree_insert takes a BTree* (it rewrites the tree), hence &db->tree.
         * Its return tells us whether the key was NEW (true) or an UPDATE of
         * an existing key (false); that boolean picks the message verb.
         */
        bool created = btree_insert(&db->tree, command->key, command->value);
        result->ok = true;
        if (created) {
            snprintf(result->message, sizeof(result->message),
                     "inserted key %d = %d", command->key, command->value);
        } else {
            snprintf(result->message, sizeof(result->message),
                     "updated key %d to %d", command->key, command->value);
        }
        break;
    }

    case COMMAND_SELECT: {
        /*
         * btree_search returns a pointer into the tree's own leaf storage (see
         * btree.h: valid only until the next modifying call). Dereferencing it
         * copies the value OUT of the tree into result->value so the REPL owns
         * its copy; we never keep the aliasing pointer alive across the call.
         */
        int* v = btree_search(&db->tree, command->key);
        result->ok = true;
        if (v != NULL) {
            result->found = true;
            result->value = *v; /* the stored value, now owned by the result */
            snprintf(result->message, sizeof(result->message),
                     "key %d = %d", command->key, *v);
        } else {
            snprintf(result->message, sizeof(result->message),
                     "key %d not found", command->key);
        }
        break;
    }

    case COMMAND_DELETE: {
        /*
         * btree_delete's bool doubles as our `found` flag: true means the key
         * was actually present and removed. The REPL can branch on result->ok
         * vs result->found to distinguish "command failed" from "key absent".
         */
        bool removed = btree_delete(&db->tree, command->key);
        result->ok = true;
        result->found = removed;
        if (removed) {
            snprintf(result->message, sizeof(result->message),
                     "deleted key %d", command->key);
        } else {
            snprintf(result->message, sizeof(result->message),
                     "key %d not found", command->key);
        }
        break;
    }

    case COMMAND_RANGE: {
        /*
         * The accumulator lives on OUR stack; btree_range calls range_visit
         * with &acc as context, and the callback bumps acc.matched through
         * that pointer. Passing &acc means "write your count back here".
         * Command's key/key_end are the inclusive window bounds.
         */
        RangeAccumulator acc;
        acc.matched = 0;
        btree_range(&db->tree, command->key, command->key_end,
                    range_visit, &acc);
        result->ok = true;
        result->matched = acc.matched; /* the count the REPL cares about */
        snprintf(result->message, sizeof(result->message),
                 "%d keys in [%d, %d]", acc.matched, command->key,
                 command->key_end);
        break;
    }

    case COMMAND_PRINT:
        /*
         * btree_print owns the output: it dumps the whole tree to stdout
         * itself. We do no further printing here; the REPL still prints our
         * short message afterwards to mark the command as handled.
         */
        btree_print(&db->tree);
        result->ok = true;
        snprintf(result->message, sizeof(result->message), "tree printed");
        break;

    case COMMAND_EXIT:
    case COMMAND_HELP:
    case COMMAND_NONE:
        /*
         * These three never touch storage: the REPL handles EXIT/HELP and
         * NONE is the parser's "unused" default. Acknowledge them so the
         * REPL sees a uniform ok=true result for every accepted command.
         */
        result->ok = true;
        snprintf(result->message, sizeof(result->message), "no-op");
        break;
    }
}
