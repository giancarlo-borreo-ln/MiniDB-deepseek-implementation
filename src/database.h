/*
 * database.h - The integration seam of MiniDB.
 *
 * This is the single header that ties the three subsystems together:
 *   - btree.h  : the B+ tree storage backend (the "engine");
 *   - parser.h : the frontend that turns user input into a Command;
 *   - lexer.h  : pulled in transitively through parser.h.
 *
 * A Database owns exactly one BTree. A DbResult is the uniform reply channel
 * from db_execute() back to the REPL: every outcome (found or not, range hit
 * count, error text) travels in one small struct, so the REPL never has to
 * poke at the tree directly.
 */

#ifndef MINIDB_DATABASE_H
#define MINIDB_DATABASE_H

#include "btree.h"
#include "parser.h"

/* One live database instance: currently a thin wrapper over a single BTree. */
typedef struct {
    BTree tree;         /* the B+ tree that stores all key/value pairs */
} Database;

/*
 * The result of executing one Command.
 *
 *   ok      : false => the command failed; message holds the error text.
 *   found   : SELECT -> key existed; DELETE -> key was removed.
 *   value   : SELECT -> the stored value (meaningful only when found).
 *   matched : RANGE  -> number of keys visited inside the window.
 *   message : human-readable one-liner the REPL prints verbatim.
 */
typedef struct {
    bool  ok;           /* false => the command failed */
    bool  found;        /* SELECT: key existed; DELETE: key was removed  */
    int   value;        /* SELECT: the stored value                      */
    int   matched;      /* RANGE: number of keys visited                 */
    char  message[128]; /* human-readable result/error text for the REPL */
} DbResult;

/* Initialises an empty database (allocates the empty BTree root leaf). */
void db_init(Database* db);

/* Frees every node owned by the database's BTree and resets it. */
void db_close(Database* db);

/*
 * The query engine (see database.c for the WHY comment): dispatches one
 * parsed Command onto the BTree and packs the outcome into result.
 */
void db_execute(Database* db, const Command* command, DbResult* result);

#endif /* MINIDB_DATABASE_H */
