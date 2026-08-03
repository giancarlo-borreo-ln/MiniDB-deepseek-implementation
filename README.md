# MiniDB, From The Ground Up

### *A Textbook-Style Tour of a Tiny SQL-Like Database Written in C*

---

> MiniDB is a small, in-memory key-value store built the way a real database is
> built: a *language front end* (lexer + parser) turns your typed commands into
> structured commands, and a *storage engine* (an order-3 B+ tree) executes them
> against live data. Every piece is under 700 lines, every meaningful line is
> commented with *why* it exists, and a 33-test Unity suite pins down the
> behaviour of each layer.

This document is not a standard README. It is a mini-book: each chapter reads
like a lecture, assumes nothing beyond basic C, and walks through the actual
source files line by line with diagrams. When you finish, you will understand
not just *what* MiniDB does, but *why* it is shaped the way it is -- and you
will be able to grow it yourself.

---

## How to Read This Book

Three conventions make this book faster to use:

1. **Chapter status.** The Table of Contents marks each chapter
   `[complete]` or `[pending]`. The book is being authored chapter by
   chapter; anything marked pending has not been written yet.

2. **Code is quoted, then explained.** Every fragment is copied verbatim from
   `src/` so the line numbers and the text agree. When a claim matters, a
   `file:line` reference tells you exactly where to look.

3. **Diagrams are the point.** The architecture and the B+ tree are spatial
   ideas. Read the ASCII art first, then the prose -- the prose explains the
   pictures.

---

## The Style Contract (read this twice)

Before any code, the project declares how it wants to be written. This
contract, quoted from `src/btree.c:6`, is enforced by human discipline and
checked by the compiler's warnings:

> - Every `malloc` / `free` / pointer assignment / struct modification is
>   commented with WHY it happens. This file is meant to be read as a
>   step-by-step textbook.
> - NO "clever" pointer shortcuts: all element moves use plain array
>   indexing on explicit lines.
> - `size_t` is used STRICTLY for: (a) the `key_count` fields and (b) the loop
>   counters that iterate a node's `keys[]` / `children[]` arrays. Every other
>   loop counter in the project is a plain `int`.

Chapter 2 exists to explain *why* those three rules exist and what they cost
you (and save you). Until then, just know they are there.

---

# Master Table of Contents

**Front Matter**
- How to Read This Book ................................... [complete]
- The Style Contract ....................................... [complete]

**Part I -- Orientation**
- Chapter 1. Introduction & Architecture Overview .......... [complete]
  The pipeline from SQL string to DB memory, and the reason it is layered.
- Chapter 2. Safe Memory & Explicit C ...................... [complete]
  Pointer rules, allocation ownership, and the `size_t` discipline.

**Part II -- The Two Halves**
- Chapter 3. The SQL Frontend .............................. [complete]
  Building the lexer state machine and the parser's proto-AST step by step,
  with the full token/transition diagram.
- Chapter 4. The B+ Tree Backend ........................... [complete]
  Internal vs. leaf nodes, routing rules, insertion splitting and deletion
  merging, drawn key by key.

**Part III -- Wiring It Together**
- Chapter 5. Integration ................................... [complete]
  How the parser's `Command` becomes a `DbResult` through `db_execute()`,
  including the callback contract for range scans.
- Chapter 6. Test-Driven Systems ........................... [complete]
  How the Unity suite works, what each of the 33 tests proves, and how to
  write database assertions of your own.
- Chapter 7. The Build System .............................. [complete]
  A line-by-line breakdown of the Makefile: two binaries, one set of sources,
  one `main()` per executable.

**Back Matter** *(pending)*
- Appendix A. Command & Token Reference
- Appendix B. Error Reference (LexResult / ParseResult)
- Appendix C. The Style Contract Checklist
- Appendix D. Index of Diagrams

---

# Chapter 1 -- Introduction & Architecture Overview

*The pipeline from SQL string to DB memory.*

---

## 1.1 The pitch: a database is a pipeline

At the highest level, every database is a translation machine with two halves.

- **A front end** that understands a language (SQL, or MiniDB's tiny dialect)
  and converts what you type into a structured description of *what you want*.
- **A back end** that owns the data and knows *how to do it*, given that
  description.

When you type `INSERT 42 100` into MiniDB and press Enter, the bytes of that
string travel through four small programs, each transforming them into a more
structured form. Nothing is stored until the very last stage; nothing knows
about the string format after the second stage. This is not an accident -- it
is the single most important architectural decision in the project, and it is
the exact decision SQLite, PostgreSQL, and MySQL make at a grander scale.

Here is the whole journey, compressed into one picture. You should read this
diagram every time you get lost; every later chapter is a zoom into one of its
boxes.

```
        USER
         |
         |  types:   INSERT 42 100  + presses ENTER
         v
+-----------------------------------------------------------------+
| STAGE 0 :  THE REPL  (src/main.c)                               |
|           fgets() reads the line into char line[256]            |
+-----------------------------------------------------------------+
         |
         |  the raw string, one byte at a time
         v
+-----------------------------------------------------------------+
| STAGE 1 :  THE LEXER  (src/lexer.c)                             |
|           a character-level state machine. Groups bytes into    |
|           small labelled pieces called tokens.                  |
|                                                                 |
|           "INSERT 42 100"                                       |
|               ->  (TOKEN_KEYWORD: INSERT)                       |
|               ->  (TOKEN_INTEGER:  42   )                       |
|               ->  (TOKEN_INTEGER:  100  )                       |
|               ->  (TOKEN_END:      <sentinel>)                  |
+-----------------------------------------------------------------+
         |
         |  Token tokens[16]   (a flat, bounded array)
         v
+-----------------------------------------------------------------+
| THE TOKEN STREAM                                                |
|           a passive buffer between two active stages. No code   |
|           lives here; it is the lexer's output and the parser's |
|           input.                                                |
+-----------------------------------------------------------------+
         |
         |  Token tokens[16], size_t token_count
         v
+-----------------------------------------------------------------+
| STAGE 2 :  THE PARSER  (src/parser.c)                           |
|           validates the stream's SHAPE and ARITY against the    |
|           grammar; packs the integers into a Command struct.    |
|                                                                 |
|           { type = COMMAND_INSERT, key = 42, value = 100 }      |
+-----------------------------------------------------------------+
         |
         |  a Command (the proto-AST)
         v
+-----------------------------------------------------------------+
| STAGE 3 :  THE QUERY ENGINE  (src/database.c)                   |
|           db_execute() dispatches on command->type and calls    |
|           the storage engine.                                   |
|                                                                 |
|           COMMAND_INSERT  ->  btree_insert(&db->tree, 42, 100)  |
+-----------------------------------------------------------------+
         |
         |  the pair (42, 100) is inserted into a leaf
         v
+-----------------------------------------------------------------+
| STAGE 4 :  THE B+ TREE  (src/btree.c)                           |
|           the in-memory storage engine: sorted leaves chained   |
|           into a linked list, internal nodes that route         |
|           searches downward.                                    |
+-----------------------------------------------------------------+
         |
         |  a DbResult reply struct travels back up
         v
+-----------------------------------------------------------------+
| DbResult { ok=true, found=false, value=0, matched=0,            |
|           message="inserted key 42 = 100" }                     |
+-----------------------------------------------------------------+
         |
         |  the REPL prints result.message
         v
       OUTPUT:  "inserted key 42 = 100"
```

Observe the alternation: **an active stage always hands a passive value to the
next active stage.** Active stages are functions with loops; passive values are
structs sitting on the stack. The string becomes tokens, tokens become a
`Command`, a `Command` becomes tree mutation, tree mutation becomes a
`DbResult`. At each boundary, the representation is *more structured and less
ambiguous* than the one before it:

| Boundary   | Representation          | What was decided                     |
|------------|-------------------------|--------------------------------------|
| keystrokes | a raw string            | nothing (the user's problem)         |
| lex        | a token stream          | where the word breaks are            |
| parse      | a `Command` struct      | what the command *means*             |
| execute    | tree mutation           | what the command *does*              |
| reply      | a `DbResult` struct     | what the user should be told         |

Everything from here to the end of this chapter is a close look at one box in
this diagram.

---

## 1.2 Why build a database from scratch at all?

A database is one of the few systems where the same concepts show up again and
again, each time with real consequences: **layering** (the front end does not
know how data is stored), **invariants** (the tree must always be balanced and
sorted), **ownership** (someone must free every byte), and **contracts**
(every function declares what it returns and when). You can study all of these
in a thousand-line toy that you can read end to end in an evening -- a lesson
you cannot get from PostgreSQL's million lines.

MiniDB deliberately omits the hard parts of a real database so the essential
parts stay visible:

- **No disk.** Everything lives in RAM; there is no file format, no page cache,
  no recovery.
- **No network.** There is one user, the REPL, sitting next to the process.
- **No transactions.** No locks, no rollback, no concurrency. Operations are
  atomic only in the sense that they are single-threaded.
- **No real SQL.** Seven fixed commands over integer keys and values.

What remains is the *skeleton* every real database shares: a language front
end, a storage back end, and a seam between them. Everything you learn here
transfers.

---

## 1.3 The system at a glance

MiniDB is five C programs wearing two hats. Four of them are *library
sources* -- they export functions but no `main()`. One of them, `src/main.c`,
is the executable's entry point. The test binary reuses the same four library
sources and supplies its own `main()`.

| File            | Lines | Role                                                     |
|-----------------|------:|----------------------------------------------------------|
| `src/lexer.c`   |   265 | Front end, part 1: string -> tokens.                     |
| `src/parser.c`  |   218 | Front end, part 2: tokens -> `Command`.                  |
| `src/btree.c`   |   612 | Back end: the order-3 B+ tree storage engine.            |
| `src/database.c`|   180 | The seam: `Command` -> tree call -> `DbResult`.          |
| `src/main.c`    |   125 | The REPL that drives the whole pipeline.                 |
| `tests/*`       |  1189 | 33 Unity tests across four suites.                       |
| `Makefile`      |    62 | Builds both binaries.                                    |

Total: under 3,000 lines for a complete, tested, documented database. Every
file fits in one sitting; the whole project fits in an afternoon.

---

## 1.4 Stage 0: the REPL (`src/main.c`)

A REPL -- **R**ead-**E**val-**P**rint **L**oop -- is the loop that turns "type
a line" into "do the work and print the answer". It is the simplest program in
the project, and the only one allowed to print directly to the user (the tree's
`btree_print` is the one exception).

`main.c` opens a `Database`, then loops forever:

```
   +------> print "minidb> "  and flush stdout
   |                  |
   |                  v
   |          fgets(line, 256, stdin)
   |                  |
   |                  +-- EOF (Ctrl-D) ?  --> print newline, break, close db
   |                  |
   |                  v
   |          blank line ?  --------------> continue (skip)
   |                  |
   |                  v
   |      LEXER: lex(line, tokens, &count) -- error ? print, continue
   |                  |
   |                  v
   |      PARSER: parse_command(...)      -- error ? print, continue
   |                  |
   |                  v
   |      cmd.type == COMMAND_EXIT ? -----> print "Goodbye!", break
   |                  |
   |                  v
   |      db_execute(&db, &cmd, &result) ; print result.message
   +------------------+
```

Three details in `main.c` are worth internalizing now because they recur:

1. **The stack owns the buffers.** `Token tokens[16]`, `Command cmd`,
   `DbResult result`, `Database db`, and `char line[256]` are all local
   variables. The lexer, the parser, and the query engine *write into* these
   caller-owned objects; none of them allocate. Memory reasoning is therefore
   trivial in the front end: stack in, stack out, nothing to free. (Chapter 2
   makes this a rule.)

2. **Every stage gates the next.** A lex error prints a message and `continue`s
   without parsing; a parse error prints and continues without executing. A
   bad line can never reach the tree. The tree is protected from the user by
   two walls of validation.

3. **`EXIT` is REPL control flow, not a database command.** The REPL checks
   `cmd.type == COMMAND_EXIT` *before* calling `db_execute`. The database still
   knows about `COMMAND_EXIT` (it acknowledges it with a "no-op"), but the
   decision to leave the loop lives where the loop lives.

The prompt-flush line is a classic C gotcha worth a sentence:

```c
printf("minidb> ");
fflush(stdout);
```

When stdout is a pipe (not a terminal) it becomes fully buffered, and without
the `fflush` the prompt would sit in a buffer while the program waited for
input that never visibly arrived. Interactive software must flush.

---

## 1.5 Stage 1: the lexer (`src/lexer.c`)

The lexer answers one question: **where do the meaningful chunks of this
string begin and end, and what kind of chunk is each?**

It is deliberately dumb. It does not know that `INSERT 42 100` is a legal
command; it only knows that `INSERT` is a run of letters, `42` is a run of
digits, and so on. It cannot reject `INSERT 42 100 3` (that is the parser's
job) and it should not try.

It walks the input one character at a time and lets the *current character*
decide what to do next -- a miniature state machine with four states:

```
          (start of token) -------- whitespace? ---- skip it, keep looking
                 |
                 |-- isdigit(byte) ? ---> INTEGER mode: eat digit, digit, ...
                 |
                 |-- isalpha(byte) ? ---> WORD mode:    eat letter, letter, ...
                 |
                 +-- anything else ----> ERROR: stray symbol ( $ ; " ? ... )
```

The three exit doors out of `lex()`:

| Return code                | Meaning                                   |
|----------------------------|-------------------------------------------|
| `LEX_OK`                   | Stream fully tokenized; `TOKEN_END` appended. |
| `LEX_ERROR_BAD_NUMBER`     | A digit run would overflow `int`.         |
| `LEX_ERROR_UNKNOWN_WORD`   | An unknown word, or any stray symbol.     |
| `LEX_ERROR_TOO_MANY_TOKENS`| The stream exceeded `LEXER_MAX_TOKENS`.   |

The lexer produces four kinds of token (from `src/lexer.h:46`):

```c
typedef enum {
    TOKEN_KEYWORD, TOKEN_INTEGER, TOKEN_END, TOKEN_ERROR
} TokenType;
```

`TOKEN_ERROR` is reserved; the lexer reports problems through its return code
rather than emitting error tokens, so the stream is either all-good-plus-END
or garbage, never a mix.

The token itself is a small discriminated union of the possible payloads:

```c
typedef struct {
    TokenType type;
    Keyword   keyword;    /* valid when type == TOKEN_KEYWORD */
    int       int_value;  /* valid when type == TOKEN_INTEGER */
} Token;
```

One `Token` is 12-ish bytes and can be copied by value -- no pointers, no
lifetimes. The full grammar of what the lexer recognizes is a single table:
seven uppercase verbs (Chapter 3 dissects the state machine and each
transition).

---

## 1.6 The token stream

After `lex()` returns, the REPL owns:

```c
Token tokens[LEXER_MAX_TOKENS];   /* LEXER_MAX_TOKENS == 16 */
size_t token_count;               /* number of live slots, END included */
```

This array is the *currency* between the lexer and the parser. It is a
passive buffer -- there is no "token stream object", no iterator, no method.
The parser receives a pointer and a count and walks it with its own cursor.
That plain-array design is a conscious choice: it keeps every byte of the
front end visible and allocation-free (Chapter 2).

A subtle invariant the parser relies on: **every valid stream ends with a
`TOKEN_END` sentinel.** The lexer appends it unconditionally, and the parser
treats it as an explicit terminator, so the parser never reads past the last
real token and never has to guess whether it has consumed everything.

---

## 1.7 Stage 2: the parser (`src/parser.c`)

Where the lexer was a scanner, the parser is a *validator*. It answers two
questions: **is this stream a legal command, and what does it mean?**

MiniDB's grammar is the smallest possible:

```
statement := KEYWORD args?
args      := INTEGER, INTEGER, ...   (a fixed count per keyword)
```

There is nothing to nest, so the parser barely needs recursion at all. It
works in three moves:

1. **Dispatch on token[0].** It must be a `TOKEN_KEYWORD`; a `switch` on
   `tokens[0].keyword` selects the rule. A bare integer first is
   `PARSE_ERROR_UNKNOWN_COMMAND` -- an integer is not a verb.
2. **Exact-arity check.** Each case calls `parse_fixed_arguments()` with the
   number of integers that command expects. That single helper consumes
   exactly `argc` integers, then *requires the very next token to be
   `TOKEN_END`*, catching all three argument failures in one place:
   - stream ends early  -> `PARSE_ERROR_MISSING_ARGUMENT`
   - non-integer where an int belongs -> `PARSE_ERROR_BAD_ARGUMENT`
   - anything left over -> `PARSE_ERROR_EXTRA_ARGUMENT`
3. **Build the `Command`.** On success, the parsed integers are copied into the
   right fields and `command->type` is set.

The parser's output is a flat, proto-AST:

```c
typedef struct {
    CommandType type;
    int key;      /* INSERT/SELECT/DELETE/RANGE: the primary key   */
    int value;    /* INSERT only: the value to store               */
    int key_end;  /* RANGE only: the inclusive upper bound         */
} Command;
```

Calling it a *proto-*AST is a hint about the future: a real `SELECT price FROM
items WHERE id = 42` would need nested expressions, so `Command` would grow
pointers to sub-structures and the parser would recurse. MiniDB's statements
are all one verb plus at most two integers, so the tree collapses to one node
-- but the seam where the tree-ness would return is identified explicitly in
`src/parser.c:52`. Chapter 3 builds the parser line by line; Chapter 5 shows
the same `Command` arriving at the database.

---

## 1.8 Stage 3: the query engine (`src/database.c`)

`db_execute()` is the seam: the single function where the *language* meets the
*storage*. It is a switch over `command->type`, and each case is a one-line
call into the B+ tree API plus a one-line reply message:

| Command       | Tree call                             | Reply                          |
|---------------|---------------------------------------|--------------------------------|
| `INSERT`      | `btree_insert(&db->tree, key, value)` | "inserted/updated key k = v"   |
| `SELECT`      | `btree_search(&db->tree, key)`        | "key k = v" / "key k not found"|
| `DELETE`      | `btree_delete(&db->tree, key)`        | "deleted key k" / "not found"  |
| `RANGE`       | `btree_range(&db->tree, lo, hi, cb)`  | "n keys in [lo, hi]"           |
| `PRINT`       | `btree_print(&db->tree)`              | "tree printed"                 |
| `EXIT`/`HELP` | (none)                                | "no-op"                        |

This is where a real database would grow a **planner**: rewrite the command,
pick an access path, compile a query plan. MiniDB's "plan" is one `switch`
case, but its *architectural position* is identical -- `src/database.c:8`
makes that explicit. The query engine knows nothing about tokens or grammar; by
the time it runs, the parser has already done all the language work.

Every command funnels its outcome into one struct, the uniform reply channel:

```c
typedef struct {
    bool  ok;           /* false => the command failed */
    bool  found;        /* SELECT: key existed; DELETE: key was removed  */
    int   value;        /* SELECT: the stored value                      */
    int   matched;      /* RANGE: number of keys visited                 */
    char  message[128]; /* human-readable result/error text for the REPL */
} DbResult;
```

Because every outcome travels in the same shape, the REPL has exactly one
print statement for the whole language: `printf("%s\n", result.message);`.

---

## 1.9 Stage 4: the B+ tree (`src/btree.c`)

The storage engine is an **order-3 B+ tree**: a balanced multiway search tree
with two structural facts that make it ideal as a database index:

1. **Every key lives in a leaf** alongside its value. Internal nodes hold only
   *separator* keys that route a search downward -- so a lookup always ends at
   a leaf where the answer actually lives.
2. **Leaves are chained** with `next` pointers into a sorted linked list, so a
   range scan visits matching keys in order without re-walking the tree.

Order 3 means each node holds at most `ORDER - 1 = 2` keys and an internal
node holds at most `ORDER = 3` children. Overflowing nodes split; underflowing
nodes borrow or merge. The whole invariant -- every leaf at the same depth,
keys sorted everywhere -- is Chapter 4's subject; this chapter only places the
tree in the pipeline.

A mental model before you ever read the code:

```
            INTERNAL node: routes, holds no values
               +-------+-------+
               |  15   |  27   |
               +---+---+---+---+
               |       |       |
         keys<15 15<k<27   keys>27
               |       |       |
               v       v       v
      +-----+-----+  +-----+-----+  +-----+-----+
      |  3  |  9  |  | 15  | 21  |  | 27  | 33  |
      | 100 |  42 |  |  7  |  9  |  |  3  |  1  |   <- LEAF: key|value
      +-----+-----+  +-----+-----+  +-----+-----+
      |                 |                 |
      |  next ----------+-----------------+  (leaf->next chain)
```

Note that `15` appears twice: once as an internal separator, once as the first
key of a leaf. Separators are *copies* of a leaf's smallest key, and that
single rule -- "a separator is a copy, and a search for `== separator` goes
right" -- is what keeps every lookup correct. Chapter 4 derives all of the
routing from that one sentence.

---

## 1.10 The language contract

The whole language is **one verb plus a fixed number of integers**:

| Keyword | Grammar              | Arity | Meaning                            |
|---------|----------------------|-------|------------------------------------|
| INSERT  | `INSERT k v`         | 2     | Store `v` under key `k`.           |
| SELECT  | `SELECT k`           | 1     | Fetch the value stored under `k`.  |
| DELETE  | `DELETE k`           | 1     | Remove key `k` if present.         |
| RANGE   | `RANGE lo hi`        | 2     | Count keys in `[lo, hi]`, sorted.  |
| PRINT   | `PRINT`              | 0     | Dump the whole B+ tree as ASCII.   |
| EXIT    | `EXIT`               | 0     | Leave the REPL.                    |
| HELP    | `HELP`               | 0     | Print the command list.            |

Keywords are case-insensitive (`insert`, `INSERT`, `iNsErT` are all the same);
keys and values are plain `int`s. Duplicate `INSERT` of an existing key is an
*update*, not an error -- the tree keeps one entry per key and overwrites the
value.

---

## 1.11 Dependency layering: who knows about whom

The include graph is a strict, acyclic stack, and the arrow directions are the
entire architecture:

```
        src/main.c
           |
           |  uses only
           v
       src/database.h        <-- the "seam" header
         /         \
        v           v
  src/parser.h    src/btree.h
        |
        v
   src/lexer.h
```

Read the arrows as "depends on":

- `main.c` depends on `database.h`, and pulls in `lexer.h`/`parser.h` only for
  the types it feeds the pipeline.
- `database.h` depends on `parser.h` (for `Command`) and `btree.h` (for the
  tree it owns).
- `parser.h` depends on `lexer.h` (for `Token`).
- `lexer.h` depends on nothing but `<stddef.h>`.
- `btree.h` depends on nothing but `<stdbool.h>` and `<stddef.h>`.

Three consequences follow, and they are the point:

1. **The lexer knows nothing about the parser.** It could feed a different
   grammar, or a different program entirely.
2. **The parser knows nothing about the tree.** It validates streams against
   the grammar; it has never heard of leaves or separators.
3. **The tree knows nothing about the language.** It stores `(int, int)` pairs;
   it cannot tell `SELECT` from `DELETE` and does not need to.

That last point is the classic front-end/back-end decoupling. The tree does
not care whether a command was typed `SELECT 42` or, someday,
`SELECT price FROM items WHERE id = 42`; by the time the tree sees anything,
the parser has already turned it into a structure. **Grow the language, and
the database stays put.** This is the sentence to tattoo on the project.

---

## 1.12 A complete walkthrough: `INSERT 42 100`

Let us follow the actual bytes, top to bottom, so the abstract pipeline
becomes concrete. You can reproduce every one of these steps by reading the
four library sources in order.

**Step 1 -- the string.** `fgets` puts `'I','N','S','E','R','T',' ','4','2',
' ','1','0','0','\n','\0'` into `char line[256]`.

**Step 2 -- lex.** The scanner walks the bytes:
- `I` is a letter -> word mode. It absorbs `INSERT`, upper-casing as it goes,
  compares against the keyword table, and emits
  `{type=TOKEN_KEYWORD, keyword=KEYWORD_INSERT}`.
- space -> skipped.
- `4` is a digit -> integer mode. It absorbs `42`, building
  `value = 4*10 + 2 = 42`, and emits `{type=TOKEN_INTEGER, int_value=42}`.
- space -> skipped.
- `1` is a digit -> integer mode. It absorbs `100` and emits
  `{type=TOKEN_INTEGER, int_value=100}`.
- `'\n'` is whitespace -> skipped; `'\0'` ends the loop.
- The END sentinel is appended.

The stream in the caller's array:

```
index:  0        1        2         3
      (KEYWORD) (INTEGER) (INTEGER) (END)
       INSERT      42       100
token_count == 4
```

**Step 3 -- parse.** `parse_command()` sees `tokens[0].type == TOKEN_KEYWORD`
with `keyword == KEYWORD_INSERT`. It calls `parse_fixed_arguments(tokens, 4,
1, 2, args)`: consume two integers (`args[0]=42`, `args[1]=100`), require the
next token to be `END` (it is). It fills the command:

```
Command { type = COMMAND_INSERT, key = 42, value = 100, key_end = 0 }
```

**Step 4 -- execute.** `db_execute()` switches on `COMMAND_INSERT` and calls
`btree_insert(&db->tree, 42, 100)`. Inside the tree, key `42` is placed in the
sorted position inside a leaf. If the leaf already holds 42, its value is
overwritten (that is an "update"); otherwise a new entry appears and the tree
may split (Chapter 4). `btree_insert` returns `true` (new key), so the message
is "inserted key 42 = 100".

**Step 5 -- reply.** `DbResult { ok=true, message="inserted key 42 = 100" }`
comes back to the REPL, which prints `inserted key 42 = 100`.

One typed line, five stages, four structs, zero heap allocations in the front
end, and the data now lives in a balanced, sorted, searchable tree.

---

## 1.13 A real session

```
minidb> INSERT 42 100
minidb> inserted key 42 = 100
minidb> SELECT 42
minidb> key 42 = 100
minidb> INSERT 7 70
minidb> inserted key 7 = 70
minidb> RANGE 1 50
minidb> 2 keys in [1, 50]
minidb> DELETE 7
minidb> deleted key 7
minidb> SELECT 7
minidb> key 7 not found
minidb> EXIT
minidb> Goodbye!
```

---

## 1.14 Project layout

```
MiniDB/
|-- Makefile              builds both binaries (`make`, `make test`)
|-- README.md             this book
|-- src/
|   |-- btree.h           B+ tree concept, node structs, public API
|   |-- btree.c           tree implementation: insert/delete/search/range/print
|   |-- lexer.h           Token, TokenType, Keyword, lex() contract
|   |-- lexer.c           the character-level state machine (this book, ch. 3)
|   |-- parser.h          Command (proto-AST), grammar, parse_command()
|   |-- parser.c          token-walking parser with exact-arity checking (ch. 3)
|   |-- database.h        Database, DbResult, db_execute() seam
|   |-- database.c        query engine: dispatches a Command onto the tree (ch. 5)
|   `-- main.c            the REPL that drives the whole pipeline (this chapter)
`-- tests/
    |-- unity.h/.c/_internals.h   the Unity test framework (vendored, don't edit)
    |-- run_tests.c       test main: registers every suite (has main())
    |-- test_suites.h     declarations of every test function
    |-- test_btree.c      B+ tree unit tests (10 tests)
    |-- test_lexer.c      lexer tests (9 tests)
    |-- test_parser.c     parser tests (8 tests)
    `-- test_database.c   end-to-end tests (6 tests)
```

33 tests, four suites, one runner. Chapter 6 explains why each exists.

---

## 1.15 Chapter summary

- A database is a **pipeline**: string -> tokens -> `Command` -> tree mutation
  -> `DbResult`. Active stages alternate with passive values.
- The four library stages have **one job each**: lexer groups bytes, parser
  validates shape and arity, query engine dispatches, tree stores.
- The layers depend **one-directionally**; the tree never learns the language,
  and the front end never touches storage.
- Everything the user can say is **seven fixed-arity commands** over integers.
- The REPL guards the tree with two validation walls: a lex error or parse
  error never reaches `db_execute`.

**Questions to test yourself**

1. Where in `main.c` could a lex error still reach the tree? (Hint: it
   cannot -- trace the `continue`s.)
2. Why does the lexer emit an END sentinel instead of letting the parser stop
   at `token_count`?
3. What would break if `parser.c` learned to call `btree_insert` directly?
4. The tree stores only `(int, int)`. Where would strings change the pipeline?

---

# Chapter 2 -- Safe Memory & Explicit C

*Pointer rules, allocation ownership, and the `size_t` discipline.*

---

## 2.1 The problem with C

C gives you two gifts that no garbage-collected language will: you can address
memory directly, and you can decide exactly when it is born and when it dies.
Both gifts are also curses. The three ways C programs silently destroy
themselves, in order of frequency:

1. **Out-of-bounds writes.** Writing past the end of an array does not raise
   an exception. It writes into whatever memory happens to sit next door -- in
   a struct, that is another field; on the heap, it is another allocation or
   a heap header. The corruption is often *latent*: the program keeps working
   for hours, then crashes in unrelated code.
2. **Dangling and double frees.** Freeing memory twice, or using a pointer
   after its owner was freed, produces nondeterministic, debugger-hostile
   behaviour.
3. **Signed/unsigned traps.** Mixing `int` and `size_t` in comparisons and
   array subscripts produces surprising promotions and warnings; negative
   indices and giant loop bounds follow.

MiniDB's answer is not to avoid these hazards by using a different language. It
is to write a **style contract** that makes the hazards *structurally
unlikely* and the remaining hazards *locally visible*. This chapter is that
contract, justified line by line.

The contract, again (verbatim from `src/btree.c:6`):

> - Every `malloc` / `free` / pointer assignment / struct modification is
>   commented with WHY it happens. This file is meant to be read as a
>   step-by-step textbook.
> - NO "clever" pointer shortcuts: all element moves use plain array
>   indexing on explicit lines.
> - `size_t` is used STRICTLY for: (a) the `key_count` fields and (b) the loop
>   counters that iterate a node's `keys[]` / `children[]` arrays. Every other
>   loop counter in the project is a plain `int`.

Three rules. Everything that follows is their justification and their
consequence.

---

## 2.2 Rule 1 -- no clever pointer arithmetic

### 2.2.1 What "clever" means

Shifting elements inside an array is something the tree does constantly. When
an insert lands at position `i` of a leaf, every key from `i` onward must move
one slot right to make room. Two idioms do this job:

```c
/* Idiom A: array indexing (what MiniDB does) */
for (size_t j = node->key_count; j > i; j--) {
    node->keys[j]   = node->keys[j - 1];
    node->u.values[j] = node->u.values[j - 1];
}

/* Idiom B: pointer arithmetic (what MiniDB forbids) */
int* kp = &node->keys[node->key_count];
for (; kp > &node->keys[i]; kp--) {
    *kp = *(kp - 1);
}
int* vp = &node->u.values[node->key_count];
for (; vp > &node->u.values[i]; vp--) {
    *vp = *(vp - 1);
}
```

Both are correct. The second is what an experienced C programmer might call
"idiomatic" -- it shortens the loop body and leans on pointer comparison. Why
does MiniDB ban it?

**Readability.** The index version says exactly what the data is: "element j
of the key array takes element j-1." The pointer version says what the *memory
addresses* are, which requires the reader to track which array a pointer was
taken from and recompute the base on every trip around the loop. In a teaching
codebase, the index version survives first contact with a reader who is
solving a different problem (understanding the split).

**Correction-proofness.** The pointer idiom has more places to get wrong: the
initialization (`&keys[key_count]` vs `&keys[key_count-1]`), the loop
condition (`>` vs `>=`), and the decrement. All three must agree. The index
loop has one moving part: the loop bound. When a bug appears in a merge or a
borrow, the index version is debuggable with a print statement; the pointer
version is debugged by squinting.

**Rigour for free.** With array indexing, the compiler -- aided by
`-Warray-bounds` (implicit in `-Wall`) and static analyzers -- can see the
index and the declared bound together. With pointers, all provenance is lost;
the compiler cannot tell whether `kp` is inside `keys[]` or points to the moon.

**Uniformity.** The whole file uses one style, so a reader develops one habit:
"indexes, not pointers." The cognitive cost of switching idioms mid-file is
zero because the file never switches.

The rule is stated as a ban because "clever" is an anti-goal. C code that is
written to impress is code that is written to be debugged for three days. The
project's bar is: **the line should look exactly like what it does.**

### 2.2.2 Where pointers are still allowed

Banning *pointer arithmetic* is not banning *pointers*. The codebase is full
of pointers, and each use is a deliberate, documented contract:

| Pointer use                       | Where                              | Why it is fine               |
|-----------------------------------|------------------------------------|------------------------------|
| `BTreeNode*` links                | `btree.c`                          | Nodes are heap objects; a pointer is the node's identity. |
| `int*` returned by `btree_search` | `btree.h:121` / `database.c:105`   | A view *into* tree-owned storage, with a documented expiry. |
| `void*` callback context          | `btree_range` / `database.c:38`    | The standard "out-parameter without a global" idiom. |
| `const char*` input               | `lex()`, `parse_command()`         | The input is borrowed, never modified. |
| caller-owned `Token*` / `Command*`| front end                          | The caller lends a buffer; the callee fills it. |

The distinction is exactly the one C itself draws: **pointers to objects** are
the language's identity mechanism; **pointer arithmetic** is an indexing
shortcut. MiniDB keeps the first and bans the second.

---

## 2.3 Rule 2 -- the `size_t` discipline

### 2.3.1 What `size_t` is

`size_t` is the unsigned integer type returned by `sizeof` and used by the C
library for lengths and array dimensions. It is guaranteed to hold the size
of any object on the platform -- typically 64 bits on a modern desktop, 32 on
a 32-bit target. It is *unsigned*, which is both its strength (it can express
the full range of a size) and its trap (it can silently wrap and make negative
values enormous).

### 2.3.2 The rule, stated precisely

The project draws a bright line between two kinds of counters:

- **A counter that is a length, or that indexes an array, is `size_t`.**
  It cannot usefully be negative, it is compared against `sizeof`/array
  bounds, and `int` might be too small for it on a 32-bit platform.
- **A counter that is not a length or an array index is a plain `int`.**
  Examples in this codebase: tree `depth`, indentation counts, loop counters
  that drive test iterations (`for (int k = 1; k <= 200; k++)`), and -- most
  importantly -- the *data itself*: keys and values are `int`.

Within the B+ tree this collapses to the phrasing in the contract: `size_t`
for `key_count` fields and for the loops that walk `keys[]` / `children[]`;
`int` for everything else.

### 2.3.3 Why `size_t` for node counts and node loops

Three concrete reasons.

**(a) The fields hold counts, and counts are unsigned quantities.** A node's
`key_count` ranges over `0..2` and can never be negative. Declaring it
`size_t` *says* that invariant in the type: "this cannot be a negative
number." A reader never has to ask whether `key_count == -1` is a possibility;
the type rules it out.

**(b) The fields are compared against array dimensions, which are `size_t`.**
The arrays are `keys[BTREE_MAX_KEYS + 1]`. The guard `node->key_count <=
BTREE_MAX_KEYS` compares a `size_t` against an `int` constant that is promoted
to `size_t` -- a clean, warning-free comparison. If `key_count` were `int`,
the same comparison would still be warning-free here, but every *index* into
the array (`node->keys[node->key_count]`) would promote `int` to `size_t`
implicitly, and a negative index would be silently reinterpreted as a
gigantic `size_t` -- an out-of-bounds write with a smile. Using `size_t`
for the indexer makes "negative index" *unrepresentable*, not merely checked.

**(c) Loop counters that walk the arrays inherit the array's type.** The
loops are `for (size_t j = ...; j < node->key_count; ...)`. Both sides of the
comparison are `size_t`, so there is no mixed-sign comparison, no `-Wsign-
compare` warning, and no chance of one side wrapping. The loop bound and the
index have the same type by construction.

### 2.3.4 Why `int` for everything else

Keys and values are `int` because the language's grammar defines them as
integers and the tree stores them verbatim. The REPL's tree `depth` is `int`
because it is neither a length nor an array index -- it is a *level counter*
used for indentation. Test loops like `for (int k = 1; k <= 200; k++)` iterate
over the *keys being inserted*, not over array slots, so `int` is right and
`size_t` would be noise. `main.c`'s `is_blank_line` uses `int i` to scan a
string -- but the moment a value is a *length* (token counts, word lengths),
it is `size_t` even outside the tree.

> **The one-sentence rule that generalizes:** *type the thing as what it is.
> If it is a size or an array subscript, make it `size_t`; if it is a number
> that lives in your data, or a plain counting variable, make it `int`.*

### 2.3.5 The traps this rule prevents

The classic bugs that a mixed-sign regime invites:

```
/* BUG 1: signed counter vs unsigned bound -- the classic "size_t trap" */
for (int i = node->key_count - 1; i >= 0; i--) { ... }
/* node->key_count is size_t (say 0). 0-1 wraps to SIZE_MAX. It is then
   converted to int... and the loop behaves unpredictably. With size_t for
   the loop variable this whole class of "off-by-wraparound" disappears
   because we never subtract from an unsigned 0 without checking first. */

/* BUG 2: negative index into an array */
node->keys[negative_index];   /* negative_index promotes to a huge size_t */
```

The project's loops avoid subtracting from `0` by construction: downward loops
always have the form `for (size_t j = node->key_count; j > i; j--)`, where the
termination condition compares two `size_t`s and the body indexes with `j`
and `j - 1` only while `j > i >= 0` guarantees `j - 1` is in range.

---

## 2.4 Rule 3 -- ownership: the allocation ledger

### 2.4.1 Two memories, one rule

Every piece of memory in the process lives in exactly one of two regions:

```
+------------------+       +--------------------------------------+
|      STACK       |       |              HEAP                    |
|   (automatic)    |       |   (malloc/free, caller-managed)      |
+------------------+       +--------------------------------------+
| char line[256]   |       |  BTreeNode nodes                     |
| Token tokens[16] |       |    (allocated in btree_node_create)  |
| Command cmd      |       |                                      |
| DbResult result  |       |   EVERY malloc must eventually        |
| Database db      |       |   be matched by a free, and the       |
| BTree tree       |       |   code comments WHY each happens.     |
+------------------+       +--------------------------------------+
| lifetimes:       |       | lifetime: from malloc() to the        |
| function scope   |       | matching free() -- documented near    |
| (auto cleanup)   |       | both call sites.                      |
+------------------+       +--------------------------------------+
```

The **front end** (lexer, parser, REPL) uses almost no heap at all. It is
allocation-free by design: the caller supplies the arrays, the stages fill
them. You can audit the front end's memory behaviour by reading a single
`main()` function: everything is on the stack, nothing leaks, nothing dangles.

The **back end** (the tree) is where memory is born and dies. Its rule is
stated in `src/btree.h:49`:

> Memory model: every node is heap-allocated with `malloc()` exactly once and
> freed exactly once (`btree_destroy` / node merges). Keys and values are plain
> `ints` stored inline inside the node struct, so there is no per-key heap
> allocation to leak.

Two halves to that sentence, both load-bearing:

- **Each node is malloc'd once, freed once.** The tree never copies nodes, so
  no two `free`s can race to the same pointer, and no node is reachable after
  its `free`.
- **Keys and values live inline, not as pointers.** A `BTreeNode` owns its
  key and value slots as plain arrays. There is no `char*` per key, no string
  pool, no per-entry allocation that could leak. The node *is* the unit of
  ownership: `malloc(node)` / `free(node)` is the whole ledger.

### 2.4.2 The complete allocation table

Every `malloc` in the project happens in exactly one function,
`btree_node_create()` (`src/btree.c:79`). Every `free` happens in exactly two
places: `btree_destroy_node()` (recursive teardown) and `btree_rebalance_at()`
(freeing a merged-away child). That is the entire ledger:

| Allocation                       | Created by                    | Freed by                       | Why                               |
|----------------------------------|-------------------------------|--------------------------------|-----------------------------------|
| empty root leaf                  | `btree_init()`                | `btree_destroy()`              | root is never NULL (see 2.4.3)    |
| right half of a split            | `btree_insert_into_node()`    | `btree_destroy()` / merges     | a split creates new storage       |
| new root above a split root      | `btree_insert()`              | `btree_destroy()`              | growing the tree one level        |
| a child merged away              | (its own earlier split)       | `btree_rebalance_at()`         | merging removes a node            |

You can prove the ledger balances by walking it: every node that enters the
tree through a create exits through a destroy or a merge-free, and the only
exit routes are the two `free` sites. The `WHY` comments at each site exist so
this audit is not just possible but *invited*.

### 2.4.3 The empty-root-leaf design decision

`btree_init()` does not set `tree->root = NULL`. It allocates a single empty
leaf:

```c
void btree_init(BTree* tree) {
    tree->root = btree_node_create(BTREE_NODE_LEAF);
    tree->count = 0;
}
```

Why? Consider the alternative. If the root could be `NULL`, then every
operation -- search, insert, range -- would begin with `if (root == NULL)
special_case();`. Search would return "not found", insert would create a root,
range would return immediately. That is five special cases scattered across
five functions, and every one is a place where a future edit forgets the
`NULL` check and crashes.

By making the empty tree a single empty leaf, **all paths are uniform**:
search walks down to a leaf with `key_count == 0` and simply finds nothing;
insert finds the sorted position, writes `keys[0]`, and sets `key_count = 1`;
range descends to the empty leaf and visits nothing. The price is one extra
leaf that must be freed at teardown -- a cost measured in bytes, paid once,
in exchange for removing a whole class of null-pointer bugs. The test
`test_btree_empty_tree_safe` pins this down: the empty tree is safe to search,
delete from, and range over, and still accepts inserts afterwards.

### 2.4.4 The aliasing contract: `btree_search`

The one sharp edge in the API is `btree_search`, and the code owns the danger
by documenting it (`src/btree.h:118`):

> Returns a pointer to the stored value for key, or NULL if absent.
> WARNING: the pointer aliases memory inside the tree; it is only valid until
> the next insertion/deletion that touches that leaf.

`btree_search` returns `&node->u.values[i]` -- a pointer to a slot *inside a
heap node*. It does not copy. This is deliberate: copying costs a `malloc` and
complicates the API with an ownership question ("who frees my answer?").
Returning a raw view is the cheapest correct contract. The cost is that the
pointer's lifetime is the tree's business, not the caller's.

The query engine shows the right way to consume it (`src/database.c:105`):

```c
int* v = btree_search(&db->tree, command->key);
result->ok = true;
if (v != NULL) {
    result->found = true;
    result->value = *v;   /* copy OUT of the tree into the result */
    ...
}
```

The value is dereferenced *immediately* and copied into `result->value`, which
the REPL owns. The aliasing pointer is never stored, never returned to the
REPL, never outlives the call. **Copy out fast, or hold the view briefly --
but never let the view escape.** That sentence is the whole aliasing rule.

### 2.4.5 The allocation-free front end

The lexer's header states the design decision plainly (`src/lexer.h:8`):

> Why no malloc/free here (design decision):
>   - The teaching focus of this codebase is memory management inside the DB
>     core (the B+ tree pages), not the CLI front-end. Keeping the lexer
>     allocation-free lets students reason about the parser with plain C
>     arrays and explicit bounds.
>   - A fixed capacity (LEXER_MAX_TOKENS) is acceptable for an interactive
>     command line: real-world commands are short, and the error path is
>     explicit.

A real database would grow the token buffer with `realloc` to support
arbitrarily long statements. MiniDB fixes the capacity at 16 tokens and
reports `LEX_ERROR_TOO_MANY_TOKENS` when full -- a bounded, honest failure
instead of a silent overflow. The one overflow path is explicit and tested
(`test_lex_too_many_tokens`). This is the "make the error surface tiny"
principle from Chapter 1 applied to memory.

---

## 2.5 Anatomy of a node: the struct and the spare slot

The heart of the tree's memory design is one struct (`src/btree.h:65`). Draw
it once, and most of Chapter 4's mechanics become obvious:

```
        BTreeNode (one malloc() of sizeof(BTreeNode); fixed at compile time)
   +----------------------------------------------------------------------+
   | type        : BTreeNodeType     LEAF or INTERNAL                     |
   | key_count   : size_t            0..2 live keys (the invariant field) |
   +----------------------------------------------------------------------+
   | keys[3]     : int x3            BTREE_MAX_KEYS=2, PLUS ONE SPARE     |
   +----------------------------------------------------------------------+
   | union u     :                                                       |
   |   values[3]       : int x3       LEAF payload (key -> value)         |
   |   children[4]     : BTreeNode* x4  INTERNAL routing (key_count+1)    |
   +----------------------------------------------------------------------+
   | next        : BTreeNode*        LEAF only: next leaf on the chain    |
   +----------------------------------------------------------------------+
```

Two details deserve emphasis, because they are the two places this struct
*cheats* on size and why it is safe to do so.

### 2.5.1 Why `keys[3]` when a node only ever holds 2 keys?

Look at the leaf insertion path. A sorted insert can take a node from 2 keys
to 3 keys *before* the split trims it back to 1. During that moment,
`keys[2]` and `values[2]` are written for real. If the arrays were exactly
`BTREE_MAX_KEYS` elements, that write would step one `int` past the array's
end -- and because `keys[]` is a struct field, the victim would be whatever
field the compiler placed after it: in the leaf case, `u.values[0]`; in the
internal case, `u.children[0]`. A silent, delayed pointer corruption.

The `+1` slots absorb that transient overflow. The invariant is stated in
`src/btree.h:69`:

> The arrays are sized MAX + 1 (a "spare slot"). WHY: during a split a node
> briefly holds 3 keys / 4 children before it is trimmed back to 2 / 3. If
> the arrays were exactly MAX elements, the transient 3rd key / 4th child
> would be written out of bounds, straight into the neighbouring struct field
> and silently corrupt pointers. The spare slot absorbs that overflow.
> Persistent nodes never exceed BTREE_MAX_KEYS keys / BTREE_MAX_CHILDREN
> children; the +1 slots are only ever touched mid-split.

The same reasoning sizes `children[4]` instead of `children[3]`: during an
internal split a node holds 4 children (3 real + the arriving promoted child)
before trimming.

### 2.5.2 Why a union for values/children?

A leaf stores values; an internal node stores children. They never coexist,
so they share memory through a `union`. Two consequences:

- **One node, one payload.** A leaf does not carry four `BTreeNode*` worth of
  dead weight; an internal node does not carry unused value slots. (At order
  3 the savings are small, but the principle scales -- real B+ trees
  routinely use unions or page types for exactly this.)
- **The type field is the discriminator.** The `type` member tells you which
  half of the union is live, mirroring the `TokenType`/`Token` pattern from
  Chapter 1. The union itself is *undefined behaviour* if you read the wrong
  member -- and the code keeps every read guarded by a `type` check. This is
  the same "tagged union" idiom as the token, promoted from stack values to
  heap nodes.

---

## 2.6 Case study 1: a leaf split, done safely

Let us watch the memory moves during the first split of a leaf, with the
`size_t` rules and the spare slot both in action. Start with a leaf holding
`[30 | 40]`, values `[100 | 200]`, and insert key `42`.

**Step 1 -- sorted position.** `i` scans while `keys[i] < 42`, so `i = 2`.

**Step 2 -- shift right to open slot 2.** The loop
`for (size_t j = key_count; j > i; j--)` copies `keys[2] = keys[1]`,
`values[2] = values[1]`:

```
keys:    [ 30 | 40 | 40 ]   values: [ 100 | 200 | 200 ]   key_count still 2
```

The write to `keys[2]` is the transient write the spare slot was built for.
It is in bounds because the array is `keys[3]`.

**Step 3 -- write the new entry.**

```
keys:    [ 30 | 40 | 42 ]   values: [ 100 | 200 | 300 ]   key_count -> 3
```

Now `key_count == 3 > BTREE_MAX_KEYS == 2`: overflow. Split.

**Step 4 -- create the right leaf and copy `keys[1..2]` into it.**

```
left  (same node):  keys [ 30 ]        key_count -> 1
right (new node):   keys [ 40 | 42 ]   values [ 200 | 300 ]  key_count = 2
```

**Step 5 -- promote a copy of the middle key** (`right->keys[0]` = 40) to the
parent as the new separator, and splice `right` into the leaf chain with
`right->next = node->next; node->next = right;`.

```
                        [ 40 ]          <- copied separator (also right's min)
                       /       \
            [ 30 ]             [ 40 | 42 ]
            (100)               (200) (300)
```

Every move was `array[j] = array[j-1]` -- no pointer arithmetic, no loop
counter that can go negative, one out-of-bounds write eliminated by the spare
slot, and each `malloc`/free surrounded by a WHY comment.

---

## 2.7 Case study 2: the integer overflow guard

The `size_t` rules govern *counters*. Data values have their own hazard:
`int` arithmetic can overflow. The lexer converts a run of digits into an
`int` and must refuse numbers that would not fit. The guard (`src/lexer.c:122`):

```c
if (value > (INT_MAX - digit) / 10) {
    return LEX_ERROR_BAD_NUMBER;
}
value = value * 10 + digit;
```

Why this particular inequality? Naively, one might write
`if (value * 10 + digit > INT_MAX)` -- but evaluating `value * 10 + digit`
already overflows *before* the comparison, and signed overflow is undefined
behaviour; the compiler may do anything. The guard must detect the overflow
*without ever performing it*.

Rearrange: we want to reject `value * 10 + digit > INT_MAX`. Subtract
`digit`: `value * 10 > INT_MAX - digit`. Divide by 10 (positive, safe):
`value > (INT_MAX - digit) / 10`. Both sides are now values that fit in
`int`, so the comparison is well-defined and the multiply is only executed
when it is known to be safe. Let us check the boundary cases:

- `214748364` (`INT_MAX/10`): `214748364 > (2147483647 - 7)/10 = 214748364` is
  false, so a following `7` is allowed and gives `2147483647 = INT_MAX`. Good.
- `2147483648` (`INT_MAX+1`): the guard trips on the final digit: `214748364
  > (2147483647 - 8)/10 = 214748363` is *true*, so the lexer reports
  `LEX_ERROR_BAD_NUMBER`. Good -- and the test `test_lex_bad_number_overflow`
  pins both sides of the boundary.

This is the same design ethos as the memory rules: **check the dangerous
condition before doing the dangerous thing, and make the check itself
overflow-free.**

---

## 2.8 What the compiler enforces

The Makefile compiles with

```
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -g -Isrc -Itests
```

- `-Wall` and `-Wextra` turn on the standard warning battery, including
  `-Wsign-compare` (mixed `int`/`size_t` comparisons), `-Warray-bounds`
  (indexing past a declared array), and `-Wuninitialized`. These are the
  *machine-readable half* of the style contract: an accidental `int` loop
  counter walking a `size_t` array draws a warning, so the discipline is
  partly enforced for you.
- `-Wpedantic` refuses non-standard constructs and keeps the code portable
  C11.
- `-g` keeps debug info so every diagram in this book can be checked with a
  debugger.

The build is clean: `make test` compiles with zero warnings. That is a
property, not an accident. The contract is written for humans; the flags are
written for the compiler; both are checked on every build.

---

## 2.9 Writing new code: the checklist

If you extend MiniDB, run this list before you write a line:

1. **Before you `malloc`, answer "who frees this and when?"** Add the answer
   as a comment at the allocation site. If you cannot answer it, you are not
   ready to allocate.
2. **Before you move elements in an array, write it as `array[j] = array[j-1]`**
   with explicit index variables -- never `*p = *(p-1)`.
3. **Type your counters by what they are.** Walking `keys[]`/`children[]` or
   holding a length? `size_t`. Counting plain iterations or holding data? `int`.
4. **Keep the invariant `key_count <= BTREE_MAX_KEYS` visible.** The spare
   slot is a safety net for mid-split transients, not a licence to store 3
   live keys.
5. **Document every aliasing pointer** with its expiry, like `btree_search`.
   Copy out fast, or hold the view briefly.
6. **Compile with `-Wall -Wextra -Wpedantic` and fix every warning.** A clean
   build is a test.

---

## 2.10 Chapter summary

- C's three great hazards are **out-of-bounds writes, dangling/double frees,
  and signed/unsigned traps**; MiniDB's style contract is engineered against
  all three.
- **No clever pointer arithmetic**: element moves are explicit array indexing,
  so the bounds are visible and the compiler can check them. Pointers to
  objects remain central; pointer arithmetic is banned.
- **`size_t` discipline**: lengths and array subscripts are `size_t`;
  everything else (data, depth, plain counters) is `int`. Mixed-sign
  comparisons and negative-index writes become unrepresentable.
- **Ownership is a two-line ledger**: every node is `malloc`'d once in
  `btree_node_create` and `free`'d once in one of two sites. The WHY comment
  is part of the contract.
- The **empty-root-leaf** design removes a class of NULL bugs; the **spare
  slot** absorbs transient split overflow; the **aliasing warning** on
  `btree_search` makes the one sharp pointer explicit.
- The compiler flags are the contract's enforcement arm, and the build is
  warning-clean.

**Questions to test yourself**

1. What field would a 2-slot `keys[]` corrupt when a leaf splits? Draw the
   struct and point.
2. `btree_search` returns a pointer into a heap node. Under what tree
   operation is that pointer invalidated, and why does the query engine
   dereference it immediately?
3. Why is `for (size_t j = node->key_count; j > i; j--)` safe from
   wraparound while `for (int i = node->key_count - 1; i >= 0; i--)` is not?
4. Find all four `free` call sites in `src/btree.c`. For each, name the node
   being freed and the reason it is still safe to free (no dangling pointers).
5. The lexer guard rejects `INT_MAX + 1` *before* the multiply. Write the
   three-line algebra that proves the inequality is equivalent to
   `value * 10 + digit > INT_MAX`.

---

# Chapter 3 -- The SQL Frontend

*Building the lexer state machine and the parser's proto-AST, step by step.*

---

## 3.1 Where we are in the pipeline

Chapter 1 handed you the pipeline and Chapter 2 handed you the rules for
writing C without hurting yourself. This chapter is the first deep dive: the
**front end**, the two stages that turn typed text into a structured command.

```
        RAW STRING                 TOKEN STREAM                 COMMAND
   "INSERT 42 100"  ------->  (KEYWORD:INSERT)(INT:42)(INT:100)(END)  ----->
                           LEXER                                  PARSER
```

The division of labour is absolute:

- The **lexer** (scanner) answers *"where do the words and numbers start and
  end, and what kind is each?"* It performs no grammar checking. It cannot
  know that `INSERT 42` is incomplete or that `PRINT 5` is illegal -- those
  are shape questions.
- The **parser** (validator) answers *"does this stream obey the grammar, and
  what does the command mean?"* It does no character scanning; the lexer has
  already done that. It walks a pre-tokenized array and checks structure.

Neither allocates memory, both write into caller-owned buffers, and both
report failures through a return code rather than exceptions or exit().
Everything you read here should be checked against the two source files --
`src/lexer.c` (265 lines) and `src/parser.c` (218 lines).

---

## 3.2 Two ways to scan: `strtok` vs. a state machine

Before the lexer's code, it is worth killing the tempting shortcut, because
you will see it in a hundred tutorials and it is a trap for exactly the kind
of system we are building.

### 3.2.1 Approach A: string splitting (`strtok`-style)

```c
char *word = strtok(line, " \t\n");       /* "INSERT" */
while (word) {
    if (isdigit(word[0])) handle_number(word);  /* "42"  */
    else                  handle_word(word);    /* "INSERT" */
    word = strtok(NULL, " \t\n");
}
```

It works on toy inputs, then bites you on five separate occasions:

1. **It mutates the input.** `strtok` writes `'\0'` over every delimiter,
   destroying the source string. You cannot keep the original around for error
   messages or position tracking.
2. **It collapses repeated delimiters.** `"a  b"` and `"a b"` both give two
   pieces, so you lose any chance of detecting "two spaces where the grammar
   allowed one".
3. **It cannot separate types cleanly.** `"SELECT 42"` -- is `42` a number or
   a word made of digits? `strtok` hands both back as the same thing (a
   string), forcing you to re-scan every piece and reclassify. When tokens
   start to include `(`, `)`, `,`, `>=`, `'hello'`, this re-scanning becomes
   the whole parser.
4. **No position tracking.** Pieces carry no offsets, so your error messages
   cannot say *where* a problem occurred.
5. **Single-token lookahead is impossible.** The parser needs to see the
   token *after* the one it is consuming. A `strtok` loop hands you tokens one
   at a time and throws the source away, making peek/consume patterns
   awkward.

### 3.2.2 Approach B: a character-level state machine (what MiniDB uses)

Instead of splitting, walk the string **one byte at a time** and let the
*current byte* decide which mini-state you are in. The state is simply "what
kind of token am I currently chewing?" -- and the very first byte of a token
determines it. This gives you, for free, everything `strtok` took away:

- **The input is never modified.** `lex()` only reads `input`; it never
  writes into it. The original line survives for error messages.
- **Types emerge from the discriminator byte.** A leading digit *is* a
  number; a leading letter *is* a word. No re-scanning.
- **Every byte is witnessed.** A stray `$` is caught precisely because no
  state claims it. Errors can point at the offending position.
- **It generalizes by adding states.** A `"` state reads string literals; an
  operator state reads `=`, `>=`, `!=`. The shape of the loop never changes;
  you just add states. A real tokenizer is this loop with more boxes.

That last point is the strategic one. MiniDB's lexer is a *foundation shape*:
the loop you are about to read is the same loop a SQL lexer uses, with more
states in it.

---

## 3.3 The full automaton

Here is `lex()` drawn as a finite state machine. Every arrow is one branch in
the `while` loop of `src/lexer.c:97`:

```
                        (START of token)
                              |
          +-------------------+----------------------+
          |                                          |
          v                                          v
   byte is WHITESPACE?                       byte is DIGIT?
   (space/tab/\n/\r)                         (isdigit)
          |                                          |
          | yes                                      v
          v                                    [INTEGER MODE]
   advance one byte                                  |
          |                                          |  advance while isdigit
          +--(back to START)---<---+                 |
                                    |                 | overflow?   end of run
                                    |                 v             v
                                    |         LEX_ERROR       emit
                                    |         BAD_NUMBER      TOKEN_INTEGER
                                    |                                |
                                    +--------------(back to START)<--+
                                    |
          +-------------------------+-------------------------+
          |                                                   |
          v                                                   v
   byte is LETTER?                                    byte is '\0' (end of input)
   (isalpha)                                                   |
          |                                                   v
          | yes                                      append TOKEN_END sentinel
          v                                           *token_count = count
    [WORD MODE]                                       return LEX_OK
          |
          |  advance while isalpha; uppercase each
          |  letter into a stack buffer
          |
          |   buffer overflow?            end of run
          |        |                           |
          |        v                           v
          |  LEX_ERROR                  memcmp(buffer, keyword, len)
          |  UNKNOWN_WORD               against each of the 7 verbs
          |                                    |
          |                          match?     | no match
          |                                |    |
          |                                |    +-----> LEX_ERROR
          |                                |             UNKNOWN_WORD
          |                                v
          |                       emit TOKEN_KEYWORD
          |                                |
          +----------------(back to START)<-+
          |
          +---> any OTHER byte ($ ; " ? ...)
                  |
                  v
           LEX_ERROR UNKNOWN_WORD
```

Four genuine modes (**skip-whitespace, integer, word, end-of-input**), one
dead state (stray symbol), and the entire language falls out of it. Notice
what is *not* in the diagram: there is no "number followed by letter" state.
What happens if a user types `12AB`? The digit run `12` is consumed and a
`TOKEN_INTEGER` is emitted; then the `A` re-enters `START` as a word and the
lexer tries to match `AB` against the keywords, which fails with
`LEX_ERROR_UNKNOWN_WORD`. The lexer does not fuse `12AB` into one token,
because the states never blend -- each run is consumed by exactly one mode.
That sharp cut between modes is a *feature*: it is what keeps a stray symbol
from being silently swallowed.

---

## 3.4 The token vocabulary

Before the states, the vocabulary. All of this lives in `src/lexer.h`.

### 3.4.1 `TokenType` -- what kind of piece is this?

```c
typedef enum {
    TOKEN_KEYWORD, TOKEN_INTEGER, TOKEN_END, TOKEN_ERROR
} TokenType;
```

| Value            | Meaning                                              |
|------------------|------------------------------------------------------|
| `TOKEN_KEYWORD`  | A command verb; the token also carries a `Keyword`.  |
| `TOKEN_INTEGER`  | A bare decimal literal; the token carries the `int`. |
| `TOKEN_END`      | Sentinel marking the end of the stream.              |
| `TOKEN_ERROR`    | Reserved; `lex()` reports errors via its return code.|

`TOKEN_ERROR` exists but is never emitted. Keeping it in the enum makes the
type space complete (a `Token` can be *any* of the four), but the design
deliberately reports failures through `LexResult` instead, so the stream is
either all-good-plus-END or garbage -- never a mix where a caller might
stumble over a mid-stream error token.

### 3.4.2 `Keyword` -- the seven verbs

```c
typedef enum {
    KEYWORD_INSERT, KEYWORD_SELECT, KEYWORD_DELETE, KEYWORD_RANGE,
    KEYWORD_PRINT, KEYWORD_EXIT, KEYWORD_HELP, KEYWORD_UNKNOWN
} Keyword;
```

`KEYWORD_UNKNOWN` does double duty: it is both the "this word is not a
keyword" placeholder *and* the sentinel that sizes the keyword table. That
second role is subtle and important -- see 3.5.2.

### 3.4.3 `Token` -- the piece itself

```c
typedef struct {
    TokenType type;
    Keyword   keyword;    /* valid when type == TOKEN_KEYWORD */
    int       int_value;  /* valid when type == TOKEN_INTEGER */
} Token;
```

A `Token` is a small **discriminated union**: `type` says which payload field
is live, and the other field is simply ignored. (The tree's `BTreeNode` uses
exactly the same idiom, promoted from stack values to heap nodes -- see
Chapter 2.5.) Because a token is only a few ints, it can be copied by value;
no token is ever heap-allocated or pointed to across calls.

---

## 3.5 The lexer, state by state

The loop skeleton, from `src/lexer.c:93`:

```c
LexResult lex(const char* input, Token* tokens, size_t* token_count) {
    const char* p = input;
    size_t count = 0;

    while (*p != '\0') {
        /* ...one state per branch, each ends with `continue` or `return`... */
    }

    /* end-of-input: append TOKEN_END, publish the count, return LEX_OK */
    *token_count = count;
    return LEX_OK;
}
```

Two local facts to hold onto: `p` is a `const char*` walking forward (never
backward, never writing), and `count` is the number of tokens placed in the
caller's array. Let us walk each branch.

### 3.5.1 State: skip whitespace

```c
if (isspace((unsigned char)*p)) {
    p++;
    continue;
}
```

`isspace` covers space, tab, `\n`, `\r`. Whitespace is never significant:
leading, trailing, and doubled whitespace all vanish, so `"  INSERT \t 42 "`
lexes exactly like `"INSERT 42"` (the test `test_lex_whitespace_tolerance`
pins this down). The `(unsigned char)` cast matters: `ctype` functions are
only defined for values representable as `unsigned char` plus `EOF`, and `p`
may be pointing at arbitrary bytes; passing a plain `char` (signed on many
platforms) to `isspace` is technically undefined behaviour for negative
values. The cast is the one-line fix.

### 3.5.2 State: integer mode (digit run)

```c
if (isdigit((unsigned char)*p)) {
    int value = 0;
    while (isdigit((unsigned char)*p)) {
        int digit = *p - '0';
        if (value > (INT_MAX - digit) / 10) {
            return LEX_ERROR_BAD_NUMBER;
        }
        value = value * 10 + digit;
        p++;
    }
    { Token t; t.type = TOKEN_INTEGER; t.int_value = value;
      if (append_token(tokens, &count, t) != LEX_OK)
          return LEX_ERROR_TOO_MANY_TOKENS; }
    continue;
}
```

Three pieces to understand.

**Accumulation.** `value = value * 10 + digit` folds the run left to right:
`"42"` becomes `((0*10)+4)*10+2 = 42`. Standard, but each step multiplies by
10, so a long run of digits grows toward `INT_MAX` fast -- which is why the
guard exists.

**The overflow guard.** Revisited from Chapter 2.7 in context: `value * 10 +
digit` would overflow `int` before the comparison could be made, and signed
overflow is undefined behaviour. So the test is algebraically rearranged:

```
we want to reject:  value * 10 + digit > INT_MAX
subtract digit:     value * 10        > INT_MAX - digit
divide by 10:       value             > (INT_MAX - digit) / 10
```

`(INT_MAX - digit)` fits in an `int` (it is at most `INT_MAX - 0`), the
division by 10 is safe, and the comparison is now between two values that
definitely fit. The multiply only runs when it is known not to overflow. The
boundary is exercised on both sides by `test_lex_bad_number_overflow`:
`2147483647` (the maximum) is accepted; `2147483648` (`INT_MAX + 1`) trips the
guard on its final digit.

**Emitting the token.** The token is built with a designated initializer and
handed to `append_token`, the *only* function that may touch the output array
(see 3.6). Note the `continue`: after a number the loop resumes at the top,
so `"42 43"` produces two integer tokens, and `"42abc"` would produce an
integer token followed by an unknown-word error.

### 3.5.3 State: word mode (letter run) and case-insensitive matching

```c
if (isalpha((unsigned char)*p)) {
    char word[8];                 /* longest keyword is 6 chars + NUL */
    size_t wlen = 0;

    while (isalpha((unsigned char)*p)) {
        if (wlen >= sizeof(word) - 1) {
            return LEX_ERROR_UNKNOWN_WORD;   /* can't be a keyword anyway */
        }
        word[wlen] = (char)toupper((unsigned char)*p);  /* normalize */
        wlen++;
        p++;
    }
    word[wlen] = '\0';

    if (buffer_starts_with_upper(word, wlen, "INSERT")) { /* ...emit... */ }
    if (buffer_starts_with_upper(word, wlen, "SELECT")) { /* ...emit... */ }
    /* ...DELETE, RANGE, PRINT, EXIT, HELP... */
    return LEX_ERROR_UNKNOWN_WORD;
}
```

Three design points.

**The fixed stack buffer.** The word is buffered into `char word[8]`, sized
for the longest keyword (`DELETE`, 6 letters) plus the NUL terminator. The
guard `wlen >= sizeof(word) - 1` bails out early if a word grows past the
buffer -- and it is correct to do so, because *any* word longer than 6
letters cannot be a keyword in this language. The overflow check doubles as a
faster rejection of junk like `SUPERCALIFRAGILISTIC`.

**Uppercase normalization.** Each letter is stored via
`toupper((unsigned char)*p)`, so whatever case the user typed, the buffer ends
up all-caps. That makes the later comparison a pure `memcmp` against the
all-caps spellings in the keyword table -- two strings that are both uppercase
compare equal iff the original word matched case-insensitively. `iNsErT` and
`INSERT` both normalize to `INSERT`. `test_lex_keyword_case_insensitive`
pins this across every verb in several mixed cases.

**The comparison helper.** `buffer_starts_with_upper` is where correctness
against buffer overruns is guaranteed (`src/lexer.c:72`):

```c
static int buffer_starts_with_upper(const char* buffer, size_t len,
                                    const char* keyword_upper) {
    return len == strlen(keyword_upper) &&
           memcmp(buffer, keyword_upper, len) == 0;
}
```

The buffer is *not* NUL-terminated as far as `memcmp` cares -- wait, it is,
`word[wlen] = '\0'` was written, but the length is still passed explicitly.
The first clause, `len == strlen(keyword_upper)`, rejects words of the wrong
length (so `INSERTX` never matches `INSERT`), and `memcmp` reads exactly `len`
bytes, so it cannot run past the end of the 8-byte stack buffer. Length check
first, exact-bytes compare second: the two together are the whole matching
rule.

The seven `if`s that follow are a small, unrolled lookup. Each either emits a
`TOKEN_KEYWORD` carrying the right `Keyword` value and `continue`s, or falls
through. If all seven fail, the last line returns
`LEX_ERROR_UNKNOWN_WORD`.

### 3.5.4 State: stray symbol (the dead end)

```c
return LEX_ERROR_UNKNOWN_WORD;
```

This is the last line of the loop body, reached only when the current byte is
not whitespace, not a digit, not a letter -- i.e. any of `$ ; " ? @ = ...`.
The error code is deliberately *not* a new "unexpected character" kind; the
public contract keeps exactly three error kinds, and an unknown symbol is, to
the grammar, a one-character word nobody defined. The trade-off is stated in
`src/lexer.c:20`: a tiny error surface is worth more than pedantic precision
in a teaching codebase. `test_lex_stray_symbol` covers `$`, `;`, and `?`.

### 3.5.5 State: end of input -- the `TOKEN_END` sentinel

```c
{
    Token end;
    end.type = TOKEN_END;
    end.keyword = KEYWORD_UNKNOWN;
    end.int_value = 0;
    if (append_token(tokens, &count, end) != LEX_OK)
        return LEX_ERROR_TOO_MANY_TOKENS;
}
*token_count = count;
return LEX_OK;
```

Every valid stream ends with a `TOKEN_END` sentinel. This single decision
removes a whole class of parser complexity: the parser never has to "peek at
the count and guess", because it can always ask *"is the current token the END
marker?"*. The sentinel also participates in the token budget: it counts
toward `LEXER_MAX_TOKENS`, which is why a stream of exactly 16 real tokens is
an overflow (there is no room left for the END). `test_lex_too_many_tokens`
tests exactly that boundary.

The empty input `""` is a valid (if useless) stream: zero real tokens, then
the END sentinel. `test_lex_empty_input` confirms the count is 1 and the sole
token is `TOKEN_END` -- which the parser will reject as
`PARSE_ERROR_EMPTY` (3.8).

---

## 3.6 The token budget and the single write point

```c
static LexResult append_token(Token* tokens, size_t* token_count, Token t) {
    if (*token_count >= LEXER_MAX_TOKENS) {
        return LEX_ERROR_TOO_MANY_TOKENS;
    }
    tokens[*token_count] = t;
    (*token_count)++;
    return LEX_OK;
}
```

Two rules are concentrated here.

**One write point.** Every token in the whole lexer enters the caller's array
through this one function. There is no `tokens[i] = ...` anywhere else in
`lexer.c`. Consequences: capacity is checked exactly once per write, the count
is bumped exactly once per write, and an audit of the lexer's output is an
audit of one function.

**An explicit, bounded failure.** `LEXER_MAX_TOKENS == 16` (the caller
stack-allocates exactly that many). Rather than overflowing the buffer, the
lexer refuses with `LEX_ERROR_TOO_MANY_TOKENS`. For an interactive command
line this is more than generous -- the widest command in the language is
`RANGE 1 2`, five tokens including END -- and the failure mode is explicit and
tested. A production lexer would `realloc` here instead; Chapter 1.14 flagged
that as the one natural growth path, and this is the single function that
would change shape.

---

## 3.7 A worked trace: `"INSERT 42 100"`

Let us simulate the loop against the actual bytes. I will show the position
`p`, the state that claims the byte, and the output array after each emission.
The input is `I N S E R T \s 4 2 \s 1 0 0 \n` (the `\n` from `fgets`).

```
  p   byte   mode         what happens                                tokens[]
  -   ----   ------------ ------------------------------------------  -------------------------
  0   'I'    whitespace   not ws -> not digit -> is letter -> WORD    (empty)
       ...   WORD         absorb I,N,S,E,R,T; uppercase each letter
       ' '   WORD->END    run ends at space; "INSERT" matches
  1   ' '    whitespace   skip                                         0:(KEYWORD:INSERT)
  2   '4'    INTEGER      absorb 4; value=4
  3   '2'    INTEGER      absorb 2; value=4*10+2=42
  4   ' '    INTEGER->END run ends at space; emit                        1:(INTEGER:42)
  5   ' '    whitespace   skip
  6   '1'    INTEGER      absorb 1,0,0 -> value=100
  9   '\n'   INTEGER->END run ends at newline; emit                      2:(INTEGER:100)
  9   '\n'   whitespace   skip (isspace('\n') is true)
 10   '\0'   end          loop exits; append END sentinel               3:(TOKEN_END)
                                                                       4: count = 4
```

Final stream, as the parser will see it:

```
index:  0        1        2        3
      (KEYWORD) (INTEGER) (INTEGER)(END)
       INSERT      42      100
```

Now the same method for three failing inputs, so you see the error paths fire
at the exact byte:

```
  "INSERT 1 FOO":   INSERT -> (KEYWORD:INSERT); 1 -> (INTEGER:1);
                    FOO is a letter run, uppercases to "FOO", matches none of
                    the seven verbs -> LEX_ERROR_UNKNOWN_WORD (lexer stops;
                    parser never runs)

  "INSERT 2147483648 1":  INSERT -> keyword; then the digit run 2147483648:
                    the guard trips when appending the final '8' because
                    214748364 > (INT_MAX - 8)/10 == 214748363
                    -> LEX_ERROR_BAD_NUMBER (lexer stops mid-run)

  "INSERT 42 $":    INSERT -> keyword; 42 -> integer; '$' is not whitespace,
                    digit, or letter -> LEX_ERROR_UNKNOWN_WORD
```

In every failure case the caller (the REPL) prints a friendly message and
moves on; the tree never sees a partially-lexed line.

---

## 3.8 The parser: from stream to `Command`

Where the lexer was a scanner, the parser is a *validator*. It receives the
flat token array and answers: **is this a legal command, and what does it
mean?** The grammar is the smallest possible -- one verb plus a fixed number
of integers:

```
statement := KEYWORD args?
args      := INTEGER, INTEGER, ...   (fixed count per keyword)
```

`parse_command()` (`src/parser.c:111`) works in four moves:

1. **Zero the output.** `command` is initialized to `COMMAND_NONE` with every
   integer field set to `0`, *before* any logic runs. This makes every
   possible outcome well-defined: on error the caller receives a fully
   initialized "nothing" command, never a partially-filled struct.
2. **Reject emptiness.** If the stream has no tokens, or its first token is
   already `TOKEN_END`, return `PARSE_ERROR_EMPTY`. (This is the fate of the
   lexer's output for `""`.)
3. **Require a verb first.** If `tokens[0].type != TOKEN_KEYWORD`, return
   `PARSE_ERROR_UNKNOWN_COMMAND`. A bare integer is not a verb; `KEYWORD_UNKNOWN`
   is also caught here by the `default` arm of the switch.
4. **Dispatch by verb, with exact arity.** A `switch` on `tokens[0].keyword`
   calls `parse_fixed_arguments()` with the verb's arity, then copies the
   parsed integers into the right `Command` fields.

The dispatch, in full shape (`src/parser.c:138`):

```c
switch (tokens[0].keyword) {
case KEYWORD_INSERT:
    r = parse_fixed_arguments(tokens, token_count, 1, 2, args); /* exactly 2 */
    if (r != PARSE_OK) return r;
    command->type = COMMAND_INSERT;
    command->key = args[0];
    command->value = args[1];
    return PARSE_OK;

case KEYWORD_SELECT:
    r = parse_fixed_arguments(tokens, token_count, 1, 1, args); /* exactly 1 */
    if (r != PARSE_OK) return r;
    command->type = COMMAND_SELECT;
    command->key = args[0];
    return PARSE_OK;

case KEYWORD_DELETE:
    r = parse_fixed_arguments(tokens, token_count, 1, 1, args);
    if (r != PARSE_OK) return r;
    command->type = COMMAND_DELETE;
    command->key = args[0];
    return PARSE_OK;

case KEYWORD_RANGE:
    r = parse_fixed_arguments(tokens, token_count, 1, 2, args);
    if (r != PARSE_OK) return r;
    command->type = COMMAND_RANGE;
    command->key = args[0];
    command->key_end = args[1];
    return PARSE_OK;

/* PRINT, EXIT, HELP: all arity 0 -- the END must simply follow. */
default:
    return PARSE_ERROR_UNKNOWN_COMMAND;
}
```

The arity appears in exactly one place per case -- the `2` or `1` or `0`
passed to `parse_fixed_arguments`. That number *is* the grammar rule. The
helper is where all the real validation lives.

---

## 3.9 `parse_fixed_arguments`: the exact-arity engine

One helper, three failure modes. This is the single most important function
in the parser, and it rewards a careful read (`src/parser.c:77`):

```c
static ParseResult parse_fixed_arguments(const Token* tokens, size_t token_count,
                                         size_t start, size_t argc, int* args) {
    size_t i;

    for (i = 0; i < argc; i++) {
        if (start >= token_count)              return PARSE_ERROR_MISSING_ARGUMENT;
        if (tokens[start].type == TOKEN_END)   return PARSE_ERROR_MISSING_ARGUMENT;
        if (tokens[start].type != TOKEN_INTEGER) return PARSE_ERROR_BAD_ARGUMENT;
        args[i] = tokens[start].int_value;
        start++;
    }

    if (start >= token_count)                  return PARSE_ERROR_MISSING_ARGUMENT;
    if (tokens[start].type != TOKEN_END)       return PARSE_ERROR_EXTRA_ARGUMENT;

    return PARSE_OK;
}
```

Picture the walker as a cursor sliding over the token array:

```
   tokens: (KEYWORD)  (INT)  (INT)  (END)
            ^ start=1                        cursor supplied by the caller
                      ^                      cursor advances per argument
                                     ^       after the loop, the cursor must
                                              be sitting on TOKEN_END
```

**The `argc` loop -- "I expect exactly this many integers".** Each iteration
performs two guards before accepting a token:

- `start >= token_count` -- we ran off the caller's array (even the sentinel
  is missing). Only reachable with a hand-built malformed stream, but the
  check makes the parser crash-proof against such streams. `test_parse_empty`
  even verifies that `parse_command(NULL, 0, ...)` returns gracefully.
- `tokens[start].type == TOKEN_END` -- the stream *ended* before the declared
  arguments were delivered: `MISSING_ARGUMENT`. E.g. `SELECT` with nothing
  after it.
- `tokens[start].type != TOKEN_INTEGER` -- a keyword (or anything else) where
  the grammar demands an integer: `BAD_ARGUMENT`. The lexer cannot produce
  this (it never emits a keyword mid-stream), so the tests hand-build such
  streams directly.

**The trailing check -- "and then the stream must end".** After exactly
`argc` integers are consumed, the cursor is advanced past them. The next
token *must* be `TOKEN_END`:

- if we ran off the array, that is a missing sentinel: `MISSING_ARGUMENT`;
- if the next token is anything else, there is more than the grammar allows:
  `EXTRA_ARGUMENT`. E.g. `INSERT 1 2 3`, `SELECT 1 2`, `PRINT 1`.

Because *both* the count and the terminator are checked, nothing between the
keyword and the END can be silently skipped, and nothing can sneak in past the
declared arity. The three errors are distinguishable by design, and they lead
to three different user-facing hints (see 3.11).

The `args[]` buffer is `int args[2]` in `parse_command` -- sized for the
largest arity in the grammar, two. Each case copies exactly the integers it
needs into `Command`'s fields, leaving the unused fields as the zeros written
in step 1.

---

## 3.10 `Command` -- the proto-AST, and why it is flat

```c
typedef struct {
    CommandType type;
    int key;      /* INSERT/SELECT/DELETE/RANGE: the primary key   */
    int value;    /* INSERT only: the value to store               */
    int key_end;  /* RANGE only: the inclusive upper bound         */
} Command;
```

A real SQL parser produces a *tree*: `SELECT a FROM t WHERE a > 10` has a
`SELECT` node holding a column list, a `FROM` name, and a `WHERE` expression
sub-tree. MiniDB's statements are all one verb plus at most two integers, so
the tree collapses to **one flat node**. The struct is the *proto*-AST: the
seed of a tree.

```
  "INSERT 42 100"  ->  { type = COMMAND_INSERT, key = 42, value = 100, key_end = 0 }
  "RANGE 10 20"    ->  { type = COMMAND_RANGE,  key = 10, value = 0,   key_end = 20 }
  "PRINT"          ->  { type = COMMAND_PRINT,  key = 0,  value = 0,   key_end = 0 }
```

Notice the discipline: **the parser fills only the fields a command actually
uses, and every field it does not use is a zero from step 1.** So
`db_execute` can safely read `value` for an `INSERT` and `key_end` for a
`RANGE` without worrying that an unrelated command left garbage there. That
is the entire contract between parser and query engine (Chapter 5).

Where would tree-ness return? The moment a statement needs nesting -- a
`WHERE id = 42` expression, a column list, a subquery. `Command` would grow
pointers to child nodes, and the parser's walker would *recurse* into them
instead of advancing linearly. `src/parser.c:52` marks exactly where. For
today's grammar, flatness is a teaching gift: every field of every command is
visible at a glance.

---

## 3.11 The error taxonomy: MISSING vs EXTRA vs BAD

Three ways to get a command wrong, three distinguishable errors, three
different recovery hints:

```
minidb> SELECT            Parse error: missing argument. Type HELP for commands.
minidb> SELECT 1 2        Parse error: extra argument.   Type HELP for commands.
minidb> INSERT x 5        Lex error: unknown word or stray symbol
```

| Input         | Where it dies | Error                          | The hint this implies              |
|---------------|---------------|--------------------------------|------------------------------------|
| `SELECT`      | parser        | `PARSE_ERROR_MISSING_ARGUMENT` | "you owe me an argument"           |
| `SELECT 1 2`  | parser        | `PARSE_ERROR_EXTRA_ARGUMENT`   | "you gave me too many"             |
| `INSERT x 5`  | lexer         | `LEX_ERROR_UNKNOWN_WORD`       | "that isn't a number/verb"         |
| `2147483648`  | lexer         | `LEX_ERROR_BAD_NUMBER`         | "that number is too large"         |
| `FOO`         | lexer         | `LEX_ERROR_UNKNOWN_WORD`       | "not a command I know"             |
| `` (empty)    | parser        | `PARSE_ERROR_EMPTY`            | "type a command"                   |

The distinction is not pedantry. A REPL that prints "syntax error" for all of
them cannot teach its user anything; a REPL that says *"you forgot an
argument"* versus *"you typed too many"* tells the user exactly where to look
next time. The parser exposes the granularity through its `ParseResult` enum
and `parse_result_string()`, and the REPL prints the label plus a hint. This
is the same "error surface as user experience" reasoning that justified the
three lexer error kinds in 3.5.4.

The tests in `test_parser.c` exercise every one of these paths: the valid
commands (`test_parse_all_valid_commands`), each arity error
(`test_parse_missing_argument`, `test_parse_extra_argument`), hand-built
streams that could never come from the lexer (`test_parse_bad_argument`,
`test_parse_unknown_command`), the empty case (`test_parse_empty`), the string
labels (`test_parse_result_and_type_strings`), and -- crucially -- the hygiene
contract that a failed parse resets the `Command` to `COMMAND_NONE`
(`test_parse_error_resets_command`).

---

## 3.12 How the front end can grow

The single most useful property of this design is that the front end has
exactly **one way to grow**, and it touches the same five places every time.
Chapter 6 will do it for real with a test; here is the map.

- **New verb.** (a) Add a `Keyword` in `lexer.h` *before* `KEYWORD_UNKNOWN`
  (the sentinel sizes the table), (b) add its spelling to `KEYWORD_TABLE` in
  `lexer.c` -- the enum index *is* the table index, so the two can never
  drift -- (c) add an `if` in `lex()` to match and emit it, (d) add a
  `CommandType` in `parser.h` and a `case` in the dispatch with the verb's
  arity, (e) add a `case` in `db_execute` (Chapter 5). That is the whole
  recipe.
- **New arity.** Change the number passed to `parse_fixed_arguments`. Exact
  arity checking is centralized in that one helper, so a grammar change is a
  one-line edit per command.
- **Variable-length lists** (a `SELECT` column list). The walker would stop on
  a delimiter token (comma, close paren) instead of a fixed count --
  `parse_fixed_arguments` is the single function that would change shape.
- **Nested syntax** (a `WHERE` clause). `Command` grows pointers to child
  nodes and the walker recurses. That is the moment the proto-AST becomes a
  real tree.

None of these touch the lexer's *shape*; adding string literals just adds a
`"` state to the automaton in 3.3. The pipeline and the memory rules from
Chapter 2 keep holding at every size.

---

## 3.13 Chapter summary

- The lexer is a **character-level state machine** with four genuine modes --
  skip-whitespace, integer, word, end-of-input -- plus a dead end for stray
  symbols. Its shape is the shape of every real tokenizer.
- It **never mutates its input**, groups types by the discriminator byte, and
  reports three error kinds through its return code.
- **Integers** are accumulated with a rearranged overflow guard so `INT_MAX +
  1` is rejected *before* any undefined overflow.
- **Words** are uppercased into a fixed 8-byte buffer and matched with a
  length-checked `memcmp`, giving case-insensitive keywords with no buffer
  overrun.
- Every token enters the caller's array through one `append_token`, which is
  where the fixed `LEXER_MAX_TOKENS` budget is enforced.
- The parser is a **validator over a flat array**: dispatch on the first
  keyword, consume exactly the declared arity, require the `TOKEN_END`
  sentinel immediately after.
- `parse_fixed_arguments` centralizes all arity checking and distinguishes
  **missing** (stream ended early), **extra** (stream continued past the
  arity), and **bad** (non-integer where an integer belongs).
- `Command` is a flat **proto-AST**: every field is defined (zeros when
  unused), and the seam where tree-ness would return is identified.

**Questions to test yourself**

1. Why does `lex()` cast every `ctype` argument to `unsigned char`? (See
   3.5.1.)
2. Trace `lex("RANGE 10 20")` by hand in the style of 3.7. How many tokens,
   and what are they?
3. `"12AB"` produces an integer token then an error. Why does the lexer not
   fuse them into one token? Which state transition would you add to change
   that behaviour?
4. `INSERTX 1 2` must fail. Which *two* clauses in
   `buffer_starts_with_upper` make it fail, and where exactly?
5. `parse_fixed_arguments` is called with `start = 1` by every command. Why
   can the walker never run off the array in a lexer-produced stream, and
   what is the remaining defensive branch for?
6. Draw the token stream, the cursor position, and the failure for
   `parse_command` on `RANGE 10`. Which branch fires, and which error is
   returned?

---

# Chapter 4 -- The B+ Tree Backend

*Internal vs. leaf nodes, routing, insertion splitting, and deletion
merging, drawn key by key.*

---

## 4.1 Why a B+ tree?

The back end answers one question: **given integer keys and values, store them
so that all four operations stay fast as the dataset grows.** MiniDB needs:

- `btree_insert(key, value)` -- store, keeping keys sorted;
- `btree_search(key)` -- fetch a value, fast, from any size tree;
- `btree_delete(key)` -- remove a key, keeping everything sorted;
- `btree_range(lo, hi)` -- enumerate a whole window *in sorted order*.

A sorted **array** gives fast search (binary search) but slow insert/delete
(shifting a million entries by one). A **linked list** gives fast insert/delete
but slow search (linear scan). A **balanced tree** gives all four in
`O(log n)` -- the classic compromise, and the one real databases use for
indexes.

The B+ tree is a specific flavour with two properties that matter enormously
for a database index (`src/btree.h:7`):

1. **Every key lives in a leaf**, next to its value. Internal nodes hold only
   *separator* keys used to route a search downward. A lookup always ends at a
   leaf -- the place where the answer actually lives. (This is the *+*: in a
   plain B-tree the value can live at any level, which complicates every
   operation.)
2. **Leaves are chained** into a sorted linked list via `next` pointers. A
   range scan visits the window in sorted order by walking the chain, without
   re-walking the tree at all.

The whole engine is `src/btree.c` (612 lines) built on one struct. This
chapter draws it until it is obvious.

---

## 4.2 The invariants: the contract the tree never breaks

A B+ tree is only as good as its discipline. Everything in `btree.c` exists
to maintain these five invariants, and every operation (insert, delete,
search, range) both *relies on* them and *preserves* them:

```
INVARIANT 1 -- Every node holds at most BTREE_MAX_KEYS = 2 keys.
              (At most 3 children for an internal node.)

INVARIANT 2 -- Every non-root node holds at least 1 key.
              (At least 2 children for an internal node.)
              => overfull nodes SPLIT; underfull nodes BORROW or MERGE.

INVARIANT 3 -- Keys inside a node are sorted ascending.
              Insertion shifts to keep the order; never append blindly.

INVARIANT 4 -- All leaves sit at the SAME depth.
              The tree only grows by splitting the root (one level up);
              it only shrinks by collapsing the root (one level down).

INVARIANT 5 -- A separator key in an internal node is a COPY of the
              smallest key in the subtree to its right.
              This single rule makes search, insert, and delete all
              descend the same way (section 4.4).
```

Invariants 1-2 are what "order 3" means. Order 3 fixes the maximum fan-out:
2 keys, 3 children. The *minimum* (1 key / 2 children for non-root nodes)
keeps nodes reasonably full so the tree stays shallow. Invariant 4 is what
makes every operation `O(log n)`: depth is uniform, so no key is ever farther
from the root than any other. Invariant 5 is the keystone of the whole file --
it deserves its own section.

---

## 4.3 Anatomy of a node (recap, drawn)

The node struct from Chapter 2.5, with the routing perspective added
(`src/btree.h:65`):

```
   BTreeNode
  +----------------+--------------------------------------------------+
  | type           | BTREE_NODE_LEAF or BTREE_NODE_INTERNAL           |
  | key_count      | size_t: number of LIVE keys (0..2)               |
  +----------------+--------------------------------------------------+
  | keys[3]        | int x3 (2 live + 1 spare slot)                   |
  +----------------+--------------------------------------------------+
  | union u        |                                                  |
  |   values[3]    | int x3        -- LEAF payload: key -> value      |
  |   children[4]  | BTreeNode* x4 -- INTERNAL routing pointers        |
  +----------------+--------------------------------------------------+
  | next           | BTreeNode*    -- LEAF only: next leaf in chain   |
  +----------------+--------------------------------------------------+

   LEAF (a data record):                     INTERNAL (a routing signpost):
   +----+----+---+                          +----+----+---+
   | 15 | 21 |   |   keys                   | 15 | 27 |   |   keys (separators)
   |  7 |  9 |   |   values                 +----+----+---+
   +----+----+---+                          | c0 | c1 | c2 |   children
   | next ---> ...                          +----+----+---+
```

The two node types **never mix their payloads**. A leaf's `u.values[]` holds
ints; an internal node's `u.children[]` holds pointers. The `type` field is
the discriminator that says which half of the union is live -- the same
tagged-union idiom as the front end's `Token`. Reading the wrong half would
be undefined behaviour, so every read is guarded by a `type` check.

The internal node draws its identity from a counting rule: **an internal node
with `k` keys has `k + 1` children.** Two keys, three pointers. The children
divide the number line into `k + 1` regions -- each child owns the keys
between two neighbouring separators. That is the entire job of an internal
node: it is a signpost, not a store.

---

## 4.4 The routing rule: `btree_first_greater`

Every downward walk in the tree -- search, insert, delete, range -- uses the
same two-line helper (`src/btree.c:122`):

```c
static size_t btree_first_greater(const BTreeNode* node, int key) {
    size_t i = 0;
    while (i < node->key_count && node->keys[i] <= key) {
        i++;
    }
    return i;
}
```

It returns the first index `i` whose key is *strictly greater* than `key` --
or, if every key is `<= key`, it returns `key_count` (one past the last key).
That index IS the child to descend into.

Why does that one rule always pick the right child? Re-read Invariant 5: the
separator `keys[i]` is a copy of the *smallest* key stored in `children[i+1]`.
So the key `keys[i]` actually lives in `children[i+1]`, not in any node at
`keys[i]`'s own level. Consider a search for some key `k` at a node whose
separators are `[15 | 27]`:

```
                    node: [ 15 | 27 ]        (separators = copies of the
                     /     |     \            minima of the middle/right
                    /      |      \           subtrees)
                   v       v       v
      children[0]     children[1]     children[2]
      keys < 15       15 <= k < 27    keys >= 27
```

- Searching for `k = 10`: `15 <= 10`? No. Loop stops at `i = 0` ->
  `children[0]`. Correct: `10` is `< 15`.
- Searching for `k = 15`: `15 <= 15`? Yes, advance to `i = 1`. `27 <= 15`?
  No. Descend into `children[1]`. Correct: the *copy* `15` at the root routes
  us to the subtree where the real `15` lives.
- Searching for `k = 27`: `15 <= 27`, then `27 <= 27`, so `i = 2` ->
  `children[2]`. Correct: `27` is the minimum of the rightmost subtree.

The subtle line is `keys[i] <= key` (not `<`). It is exactly the "equal-to-
separator goes right" rule, and it falls out of the copy invariant for free:
a separator is not a value at this level, so searching for it must continue
into the subtree that owns the copy. This single helper, unchanged, is what
makes `btree_search`, `btree_insert_into_node`, `btree_erase_node`, and
`btree_range` all agree on where any key lives.

`size_t i` is chosen per the style contract: `i` indexes `node->keys[]`, and
it is compared against `node->key_count` (also `size_t`). No mixed-sign
comparisons, no negative-index hazard (Chapter 2.3).

---

## 4.5 Search: walking down, then scanning

`btree_search` is the shortest function in the file, and it is the one to
read first (`src/btree.c:503`):

```c
int* btree_search(BTree* tree, int key) {
    BTreeNode* node = tree->root;

    while (node->type == BTREE_NODE_INTERNAL) {
        size_t i = btree_first_greater(node, key);
        node = node->u.children[i];          /* the descent step */
    }

    for (size_t i = 0; i < node->key_count; i++) {
        if (node->keys[i] == key) {
            return &node->u.values[i];       /* address of the live slot */
        }
    }
    return NULL;
}
```

Trace `btree_search(&tree, 21)` on the example tree:

```
   root  [ 15 | 27 ]
          |        |
          v        v
     leaf [3 | 9]  leaf [15 | 21]  leaf [27 | 33]
     (values 100)  (values 7 | 9)   (values 3 | 1)

   step 1  node = root (internal)
           btree_first_greater(root, 21):
             15 <= 21 -> advance; 27 <= 21? no -> i = 1
           node = root->children[1]          (now the middle leaf)
   step 2  node is a LEAF -> exit the loop
           scan keys: keys[0]=15 != 21; keys[1]=21 == 21  -> FOUND
           return &node->u.values[1]
```

The pointer returned is **an address inside a heap node** -- the aliasing
contract from Chapter 2.4.4: it is valid only until the tree mutates that
leaf. `btree_search` never allocates; the caller copies the value out
immediately (as `db_execute` does) or holds the view briefly.

Two structural details worth noting:

- The loop is a **descent, not a recursion**. Each iteration replaces
  `node` with one of its children. The tree could be 100 levels deep; the
  function uses one local pointer and no stack growth.
- At most `BTREE_MAX_KEYS = 2` keys need scanning at the leaf. That tiny
  constant upper bound is what keeps the final scan free: `O(log n)` for the
  descent plus `O(2)` for the scan.

---

## 4.6 Insertion, part 1: descent and the sorted shift

Insertion is `btree_insert` -> `btree_insert_into_node` (recursive). At a
leaf, it does four things in order (`src/btree.c:149`):

```c
/* (1) find the sorted position: first index whose key is not < key */
size_t i = 0;
while (i < node->key_count && node->keys[i] < key) {
    i++;
}

/* (2) duplicate key: keep one entry per key, just update the value */
if (i < node->key_count && node->keys[i] == key) {
    node->u.values[i] = value;
    *inserted = false;
    return false;
}

/* (3) shift every key/value at or after i one slot right to open i */
for (size_t j = node->key_count; j > i; j--) {
    node->keys[j]   = node->keys[j - 1];
    node->u.values[j] = node->u.values[j - 1];
}
node->keys[i] = key;
node->u.values[i] = value;
node->key_count++;

/* (4) no overflow? done. overflow? split (section 4.7). */
if (node->key_count <= BTREE_MAX_KEYS) return false;
```

The three pointerless moves are the heart of the *insert in place* idea.
Insert key `30` into the leaf `[10 | 20]`:

```
   before:  keys  [ 10 | 20 |  - ]      key_count = 2
            i = 2   (both keys < 30)
            shift right: keys[2]=keys[1]=20, values[2]=values[1]
   mid:     keys  [ 10 | 20 | 20 ]
            write keys[2]=30, values[2]=v30
   after:   keys  [ 10 | 20 | 30 ]      key_count = 3
```

The shift writes `keys[2]` -- the **spare slot** from Chapter 2.5, absorbing
the transient 3rd key so the write never runs past the array. Notice the loop
runs `key_count` **down to** `i` (exclusive), copying each element one slot
right without ever overwriting a value it still needs: it starts from the
right end, so `keys[key_count-1]` is copied into `keys[key_count]` first,
freeing `keys[key_count-1]` for the next copy.

The duplicate case (step 2) is why `btree_insert` returns a `bool`: if the key
already exists, the tree **updates the value and does not grow**. One entry
per key, always. `test_btree_duplicate_updates_value` pins this: a second
`INSERT 42 200` reports `false` (not a new key) and leaves `btree_size` at 1.

---

## 4.7 Insertion, part 2: the leaf split and the promote

When a leaf holds 3 keys (`key_count > BTREE_MAX_KEYS`), it cannot hold the
overflow -- Invariant 1 must be restored. The fix is a **split**:
(`src/btree.c:178`)

```c
/* left keeps keys[0]; a new right leaf takes keys[1] and keys[2]; the
   middle key (= right's first key) is COPIED upward as the separator. */
BTreeNode* right = btree_node_create(BTREE_NODE_LEAF);
size_t right_count = node->key_count - 1;          /* drop the promoted key */
for (size_t j = 0; j < right_count; j++) {
    right->keys[j]   = node->keys[1 + j];
    right->u.values[j] = node->u.values[1 + j];
}
right->key_count = right_count;

*promoted_key  = right->keys[0];    /* COPY the middle key up */
*promoted_child = right;            /* hand the new node to the parent */

node->key_count = 1;                /* left keeps only keys[0] */

right->next = node->next;           /* splice the new leaf into the chain */
node->next = right;
return true;                        /* tell the parent: "I split" */
```

Watch it live. Leaf `[30 | 40]` (values `[100 | 200]`) receives `42`:

```
   (a) after the sorted insert:
       keys [ 30 | 40 | 42 ]   values [ 100 | 200 | 300 ]   key_count = 3
              ^    ^    ^
              left kept    right takes    (promoted = 40)

   (b) allocate right; copy node->keys[1..2] and node->values[1..2]:
       right = { keys [ 40 | 42 ], values [ 200 | 300 ], key_count = 2 }
       *promoted_key  = 40          (right->keys[0] -- the middle key)
       *promoted_child = right

   (c) trim the left node:
       left node: keys [ 30 ], values [ 100 ], key_count = 1

   (d) the parent absorbs (section 4.8):

                             [ 40 ]            <- separator, a COPY
                            /       \
                 [ 30 ]             [ 40 | 42 ]
                 (100)               (200) (300)
```

Three things to notice.

**The middle key is COPIED, not moved.** `*promoted_key = right->keys[0]`
reads the value; it does not clear it. The separator is a *copy*, which is
precisely Invariant 5: the same `40` lives both as a routing key at the parent
and as the first real key in the right leaf. `test_btree_insert_search`
checks this end to end: after the tree grows, `btree_search(&tree, 40)` still
finds the value in the leaf.

**The leaf chain is spliced with two pointer writes.** Before the splice,
`node->next` pointed at whatever leaf followed in the sorted order. The new
right leaf must sit immediately after `node`, so:

```
   before:  ... -> [30|40] -> next_leaf -> ...
                      |
   after:   ... -> [30] -> [40|42] -> next_leaf -> ...
                    |        |
                    +--------+--> (old node->next now belongs to right)
```

`right->next = node->next; node->next = right;` is the classic two-step
linked-list insertion. `btree_range` later walks this chain, so the order of
these two writes is what keeps the window sorted.

**The function returns `true` ("I overflowed").** That boolean is how the
split propagates: the caller (the parent internal node) receives the promoted
key and child and must absorb them -- or, if the parent is itself full, split
again. This is the recursion that makes a split ripple upward.

---

## 4.8 Insertion, part 3: internal split and root split

When a child splits, the parent internal node absorbs the promoted pair
(`src/btree.c:223`):

```c
/* shift the parent's keys and children to open a slot at index i */
for (size_t j = node->key_count; j > i; j--) {
    node->keys[j] = node->keys[j - 1];
}
for (size_t j = node->key_count + 1; j > i + 1; j--) {
    node->u.children[j] = node->u.children[j - 1];
}
node->keys[i] = child_promoted_key;
node->u.children[i + 1] = child_promoted_child;
node->key_count++;
```

The promoted key slides into `keys[i]`; the promoted child slides into
`children[i+1]` -- exactly the slot the routing rule would send future
searches to. If that makes the parent hold 3 keys / 4 children, it splits
too, but the *internal* split is different from the leaf split
(`src/btree.c:237`):

```c
size_t mid = node->key_count / 2;              /* 3 / 2 == 1 */
*promoted_key = node->keys[mid];               /* PUSH the middle key up */

BTreeNode* right = btree_node_create(BTREE_NODE_INTERNAL);
size_t right_key_count = node->key_count - mid - 1;  /* keys above mid */
for (size_t j = 0; j < right_key_count; j++)
    right->keys[j] = node->keys[mid + 1 + j];
for (size_t j = 0; j <= right_key_count; j++)
    right->u.children[j] = node->u.children[mid + 1 + j];
right->key_count = right_key_count;

node->key_count = mid;                         /* left keeps the bottom half */
*promoted_child = right;
```

For an internal node with keys `[9 | 15 | 21]` and children `[c0 | c1 | c2 |
c3]`:

```
   keys     [ 9 | 15 | 21 ]     children  [ c0 | c1 | c2 | c3 ]
              ^    ^    ^
              left      right
              (kept)    (taken)
                       promoted = 15

   after:   left   = keys [ 9 ]      children [ c0 | c1 ]
            right  = keys [ 21 ]     children [ c2 | c3 ]   <- + 1 child!
            promoted key  = 15  (PUSHED to the parent)
```

Compare with the leaf split: a leaf keeps 1 key and passes 2 keys down; an
internal node keeps 1 key/2 children and passes 1 key/2 children up, **pushing
the middle key** rather than copying it. The difference is forced by the
counting rule: a 3-key/4-child node splits into two 1-key/2-child nodes, and
the key that would have been orphaned -- the middle one -- travels up. Notice
the invariant that survives: `right` has `right_key_count` keys and
`right_key_count + 1` children. The `for (j <= right_key_count)` child loop
(the `<=` is the giveaway) preserves "k keys, k+1 children" in the right half.

**The root split -- the only way the tree grows taller.** If the *root* is
the node that overflows, there is no parent to absorb the promoted pair.
Instead `btree_insert` builds a brand-new root above the old one
(`src/btree.c:269`):

```c
if (root_split) {
    BTreeNode* new_root = btree_node_create(BTREE_NODE_INTERNAL);
    new_root->keys[0] = promoted_key;
    new_root->u.children[0] = tree->root;   /* old root stays as left child */
    new_root->u.children[1] = promoted_child;
    new_root->key_count = 1;
    tree->root = new_root;
}
```

```
   before (root was the full leaf):        after (a new internal root):
   [30 | 40 | 42]                          [ 40 ]
                                              /     \
                                    [30]           [40 | 42]
                                    (old root)     (promoted child)
```

This is Invariant 4's machinery: **depth increases only at the root, by
exactly one level, and every leaf is still at the same depth.** The old root
is not freed -- it becomes the new root's left child. The `WHY` comment in the
code says it plainly: "the old root must keep its identity as a child of the
new one." That is why `btree_insert_into_node` hands the new node back to the
caller instead of mutating the tree directly: only the caller knows whether
the node it recursed into was the root.

---

## 4.9 A full build-up, key by key

Insert `3, 9, 15, 21, 27, 33` in that order, and watch the tree grow. This is
the concrete story of everything above. Leaves are drawn as `[k:v]`, internal
nodes as `[k]` over their children.

```
 insert 3:  root is the empty leaf
            [3]

 insert 9:  sorted insert, no overflow
            [3 | 9]

 insert 15: leaf overflows (3 > 2) -> SPLIT:
            left [3], right [9 | 15], promote 9; new root
                      [ 9 ]
                     /     \
                  [3]     [9 | 15]

 insert 21: descend to [9|15], sorted insert -> overflow -> SPLIT:
            left [9], right [15 | 21], promote 15
            parent absorbs -> [9 | 15] over its three children
                      [ 9 | 15 ]
                     /    |    \
                  [3]  [9]  [15 | 21]

 insert 27: descend to [15|21] -> overflow -> SPLIT:
            left [15], right [21 | 27], promote 21
            parent absorbs -> [9 | 15 | 21] -> overflow -> SPLIT (internal):
            mid=1, push 15 up, left [9] (children c0,c1),
            right [21] (children c2,c3); new root
                         [ 15 ]
                        /      \
                   [ 9 ]        [ 21 ]
                   /   \        /    \
                 [3]  [9]    [15]  [21 | 27]

 insert 33: descend root: 15<=33 -> children[1] = [21];
            descend [21]: 21<=33 -> children[1] = [21|27];
            insert -> overflow -> SPLIT: left [21], right [27 | 33],
            promote 27; parent [21] absorbs -> [21 | 27] (no overflow)

                         [ 15 ]
                        /      \
                   [ 9 ]        [ 21 | 27 ]
                   /   \        /    |    \
                 [3]  [9]    [15]  [21]  [27 | 33]

   leaf chain:  [3] -> [9] -> [15] -> [21] -> [27 | 33]   (sorted!)
```

Every leaf is at depth 2. `btree_size(&tree)` counts 6. `btree_search(&tree,
33)` takes 3 pointer steps and one 2-element scan. `btree_range(&tree, 9, 21)`
descends to the first leaf in range and walks the chain, visiting 9, 15, 21 --
in order. This is the tree you will see when you type `PRINT` into the REPL,
in `btree_print`'s recursive indented form.

---

## 4.10 Deletion: borrow, merge, and the shrink

Deletion is insertion's mirror image: it also descends to the leaf, removes a
key, and then repairs any *underflow*. A non-root node needs at least 1 key
(Invariant 2); a leaf emptied by deletion must be repaired by its parent.

`btree_delete` has a cheap pre-check first (`src/btree.c:480`):

```c
if (btree_search(tree, key) == NULL) {
    return false;     /* absent: do nothing at all */
}
btree_erase_node(tree->root, key);
tree->count--;
```

Deleting an absent key is a no-op -- `test_btree_delete_keys` verifies the
size is untouched. Then `btree_erase_node` does the work. At a leaf, it finds
the key, shifts the later entries **left** to close the gap, decrements
`key_count`, and reports whether it underflowed:

```c
for (size_t j = i; j + 1 < node->key_count; j++) {
    node->keys[j]   = node->keys[j + 1];
    node->u.values[j] = node->u.values[j + 1];
}
node->key_count--;
return node->key_count == 0;      /* true means "parent must fix me" */
```

If a child underflows, the parent calls `btree_rebalance_at` -- one function
with three tactics, tried in order (`src/btree.c:293`).

**Tactic 1 -- BORROW from the left sibling.** If the left sibling has 2 keys,
it can spare one. For a leaf, the sibling's *largest* entry moves into the
front of the starved child:

```
           parent: [ 40 | 60 ]              parent: [ 42 | 60 ]
                     /   |   \                /     |     \
             [30]  [40|42] [60]    ->    [30]    [40]    [60]
                   ^ emptied              (42 moves left-to-right
                                          and becomes the new separator)
```

The separator update is the subtle line: `parent->keys[child_index - 1] =
child->keys[0]`. The separator must equal the child's new *minimum* (42),
because every separator is a copy of the right subtree's minimum -- Invariant
5 again. If the code updated the separator to the left sibling's new *maximum*
(40), a search for `42` would mis-route. The comment in `src/btree.c:306`
spells this out.

For an internal underflow, the borrow is a rotation through the parent: the
parent separator drops into the child's front, the sibling's last key rises
into the parent, and the sibling's last child slides across.

**Tactic 2 -- BORROW from the right sibling.** The mirror image, moving the
right sibling's *smallest* entry into the child's back, and updating
`parent->keys[child_index]` to the right sibling's new minimum.

**Tactic 3 -- MERGE.** If neither sibling can spare a key, two nodes merge
into one and the separating key is dropped from the parent. Merging the empty
leaf into the left sibling:

```
           parent: [ 40 | 60 ]              parent: [ 60 ]
                     /   |   \                /      \
             [30]  [  ]  [60]    ->    [30|40]     [60]
                   ^ emptied            (separator 40 is dropped)
```

The code copies the child's entries onto the left sibling's tail, then
`left->next = child->next` to unlink the dead leaf from the chain, then
`free(child)` -- the *one* place (besides teardown) where a node is freed
(Chapter 2.4.2). The parent then shifts its keys/children left to drop the
separator and the child pointer, and decrements `key_count`.

Because the parent itself may now underflow, `btree_erase_node` returns
`node->key_count == 0` at every level, so the underflow can propagate upward
-- `btree_rebalance_at` is called by whichever parent sees an underflowed
child. The cascade stops at the root.

**The root shrink.** When the root is an internal node that ends up with a
single child, the tree has lost a level and the root is redundant:

```c
if (tree->root->type == BTREE_NODE_INTERNAL && tree->root->key_count == 0) {
    BTreeNode* old_root = tree->root;
    tree->root = old_root->u.children[0];   /* promote the only child */
    free(old_root);                          /* and release the old root */
}
```

Invariant 4's shrink path, the mirror of the root split in 4.8. `test_btree_
delete_all_empties_tree` deletes every key and confirms the tree returns to a
single empty root leaf, size 0, ready for reuse.

---

## 4.11 Range scan: the leaf chain pays for itself

`btree_range` is the operation that the *+* in B+ is for (`src/btree.c:525`):

```c
BTreeNode* node = tree->root;

/* descend to the leaf that could hold key_min (or the next key after) */
while (node->type == BTREE_NODE_INTERNAL) {
    size_t i = btree_first_greater(node, key_min);
    node = node->u.children[i];
}

/* walk the sorted leaf chain and report every key inside the window */
while (node != NULL) {
    for (size_t i = 0; i < node->key_count; i++) {
        if (node->keys[i] > key_max) return;   /* sorted: done early */
        if (node->keys[i] >= key_min) {
            visit(node->keys[i], node->u.values[i], context);
        }
    }
    node = node->next;
}
```

Two phases, both cheap. **Phase 1** is one descent with the routing rule to
land on the first leaf that could contain `key_min` -- the same `btree_first_
greater` walk used by search and insert. **Phase 2** walks the `next` chain,
visiting keys in ascending order. Two early exits keep it tight: the moment a
key exceeds `key_max`, everything after it on the chain is larger too, so the
scan stops (`return`); and keys below `key_min` inside the starting leaf are
skipped (`>= key_min` guard) -- the descent lands on the *candidate* leaf,
not necessarily exactly on the window's left edge.

```
   RANGE 9 21  on the tree built in 4.9
   (leaf chain:  [3] -> [9] -> [15] -> [21] -> [27|33])

   descend:  root [15|27] -- first_greater(9) = 0 -> children[0] = [3]
             leaf [3]:    keys[0] = 3 < 9, skip (below window)
   chain:    next = [9]:  visit(9)     (>= 9)
             next = [15]: visit(15)
             next = [21]: visit(21)
             next = [27|33]: keys[0] = 27 > 21  -> return (early exit)

   Result: 3 visits, in ascending order:  9, 15, 21
```

The window yields exactly the matching keys, in order, and the scan stops
the instant it proves nothing further can match -- no leaf past `[27|33]` is
even examined.

The callback signature is the front-end-to-back-end seam in miniature:

```c
void btree_range(const BTree* tree, int key_min, int key_max,
                 void (*visit)(int key, int value, void* context),
                 void* context);
```

The tree does not tell you *what to do* with matching keys; it *fires a
callback* for each one and lets the caller decide (Chapter 5 shows `db_execute`
counting them). The `void* context` is how the callback talks back to the
caller's stack -- the "out-parameter without a global" idiom from Chapter 2.

---

## 4.12 Reading the `PRINT` output

`btree_print` renders the tree so you can verify every claim in this chapter
against the live REPL. The recursive form prints internal nodes as
`internal [k0 k1]` with each child indented beneath, and leaves as
`leaf [k:v ...]`; the second half prints the flattened leaf chain. For the
4.9 tree:

```
-- tree (count=6) --
internal [15]
  internal [9]
    leaf [3:30]
    leaf [9:90]
  internal [21 27]
    leaf [15:150]
    leaf [21:210]
    leaf [27:270 33:330]
leaf chain (sorted): 3:30 9:90 15:150 21:210 27:270 33:330
```

(Values are `k*10` here, for illustration.) Reading it bottom-up: the chain
tells you the true sorted contents -- that is what `RANGE` walks; the tree
shape tells you the routing -- that is what `SEARCH` walks. If the two ever
disagree, the tree is corrupt. The differential test in Chapter 6 exists
precisely to catch such disagreements.

---

## 4.13 The style contract in action

Chapter 2's rules are not decoration -- this file is where they earn their
keep. Watch for them while you read `btree.c`:

- **`size_t` for `key_count` and for every loop that walks `keys[]` /
  `children[]`.** `btree_first_greater`, the shift loops, the split copy
  loops, the merge loops, `btree_search`, `btree_range` -- every one uses
  `size_t j`/`size_t i` compared against `key_count`. The only `int` counters
  in the file are `btree_print_node`'s `depth` and `d`, which count
  *indentation*, not array slots. Mixed-sign comparisons would draw
  `-Wsign-compare` warnings; there are none, by construction.
- **Plain array indexing, never pointer arithmetic.** Every element move is
  `node->keys[j] = node->keys[j - 1]` or `right->keys[j] = node->keys[mid + 1
  + j]`. Bounds are visible; the compiler can check them; the code survives
  casual reading.
- **The spare slot.** Every transient 3rd key / 4th child write lands in
  `keys[2]` / `children[3]`, which exist for exactly that reason.
- **One allocation site, two free sites.** `btree_node_create` allocates
  every node; `btree_destroy_node` and `btree_rebalance_at` free them. The
  ledger from Chapter 2.4.2 balances by construction.

---

## 4.14 Chapter summary

- A B+ tree keeps all four operations fast by being **sorted, balanced, and
  leaf-chained**. The + is the leaf chain, which makes range scans linear.
- Five invariants define the discipline; **Invariant 5** -- separators are
  *copies* of right-subtree minima -- makes every descent use the same rule:
  descend while `keys[i] <= key`.
- `btree_search` is a descent plus a tiny leaf scan, returning an aliasing
  pointer the caller copies out immediately.
- Insertion shifts into place, then **splits** overfull leaves (copy the
  middle key up), **splits** overfull internal nodes (push the middle key
  up), and at the root **grows a new level** -- the only way the tree gets
  taller.
- Deletion shifts to close the gap, then repairs underflow by **borrowing**
  (left then right) or **merging**, with the underflow able to propagate
  upward until the root **shrinks** -- the only way the tree gets shorter.
- `btree_range` descends once, then walks the leaf chain with two early exits,
  firing a callback per match.
- `btree_print` exposes the whole tree for inspection, and the style contract
  from Chapter 2 is what makes all of it auditable.

**Questions to test yourself**

1. `btree_first_greater` uses `keys[i] <= key`. What would break if it used
   `<` instead? Find the concrete key where a search would go to the wrong
   child.
2. In the leaf split, why must the separator be *copied* from the right leaf
   rather than moved? Which invariant does the copy preserve?
3. In the internal split, why does the right child loop use `<=
   right_key_count` while the right key loop uses `< right_key_count`?
4. During a merge, which node is freed, and why is it guaranteed no live
   pointer still references it? (Hint: what did `left->next = child->next`
   do before the `free`?)
5. Trace `btree_delete(&tree, 9)` on the 4.9 tree by hand. Does it borrow,
   merge, or shrink? Draw the result.
6. `btree_range` stops at the first key `> key_max`. Why is that safe, given
   leaves are sorted and chained?

---

# Chapter 5 -- Integration

*Wiring the parser's `Command` to the B+ tree API through `db_execute()`.*

---

## 5.1 The seam, and why it exists

Chapters 3 and 4 built the two halves of the system: the front end turns text
into a `Command`, and the back end stores `(int, int)` pairs in a B+ tree.
This chapter is the glue -- `src/database.c` (180 lines) -- the single place
where the two halves meet.

The architectural deal, stated one more time because it is the whole point of
the project:

```
        THE FRONT END                          THE BACK END
   "SELECT 42"  ->  Command  ----------------  btree_search(...)
   (knows grammar,                     (knows nodes, routing,
    knows nothing                     splitting, merging --
    about storage)                     knows nothing about
                                       the language)
                     ^
                     |
              db_execute()  =  THE SEAM
              (translates ONE Command into ONE tree call
               and ONE DbResult)
```

The database layer is deliberately thin. It does not parse, it does not
balance trees. It does exactly one job: **accept a validated `Command`, call
the right tree function, and package the outcome into a uniform reply.** That
job is the *query engine* -- and as the header comment says, it is exactly
where a real database would grow a planner/optimiser (`src/database.c:8`).
Section 5.7 will make that concrete.

Two structs carry the whole layer (`src/database.h`):

```c
typedef struct {
    BTree tree;         /* the B+ tree that stores all key/value pairs */
} Database;

typedef struct {
    bool  ok;           /* false => the command failed */
    bool  found;        /* SELECT: key existed; DELETE: key was removed  */
    int   value;        /* SELECT: the stored value                      */
    int   matched;      /* RANGE: number of keys visited                 */
    char  message[128]; /* human-readable result/error text for the REPL */
} DbResult;
```

`Database` is a one-field wrapper today. It exists as a *separate type* so
that when you grow the system -- a second tree, a schema, a transaction log --
there is a stable place to hang it without changing the REPL or the tests.
`DbResult` is the entire reply vocabulary: five fields, every outcome the REPL
could possibly need to print, whether the command found a key, removed a key,
counted a range, or failed.

---

## 5.2 Lifecycle: `db_init` and `db_close`

```c
void db_init(Database* db) {
    memset(db, 0, sizeof(Database));   /* zero the whole struct first */
    btree_init(&db->tree);             /* lay down the empty root leaf */
}

void db_close(Database* db) {
    btree_destroy(&db->tree);          /* hand every node back to the heap */
}
```

The `memset` first is defensive discipline: it guarantees no field of `db`
starts with garbage, so even a future bug that reads an uninitialized field
reads a zero, not a random value. Then `btree_init` allocates the empty root
leaf (Chapter 2.4.3). Order matters: `btree_init` only touches `tree`, and
`tree` is inside `db` -- so zeroing `db` first, then initializing `tree`, is a
two-step "whole first, then the part" sequence.

`db_close` is the mirror: `btree_destroy` frees every node recursively (the
teardown ledger from Chapter 2.4.2). Afterwards `db` is a harmless, empty
struct that the REPL simply stops using. `test_db_reinit_after_close` proves
the full round trip: `db_close` then `db_init` yields a fresh database with no
surviving data.

---

## 5.3 The defensive default: every result starts as "no"

Before any dispatch, `db_execute` zeroes the whole reply
(`src/database.c:65`):

```c
result->ok = false;
result->found = false;
result->value = 0;
result->matched = 0;
result->message[0] = '\0';
```

Then each `case` overwrites only the fields it cares about. Two consequences:

1. **Every outcome is well-defined.** Even an exotic future `CommandType`
   that no case handles (a typo in the enum, a half-finished extension)
   yields `ok = false` with an empty message -- never uninitialized stack
   memory returned to the REPL. The REPL prints `"\n"` after a message, and
   an empty message is at least *defined*.
2. **Cases can be minimal.** `INSERT` sets `ok` and `message`; it never
   touches `found`, `value`, or `matched`, which already hold their defaults.
   The reader of any one `case` sees exactly what that command reports.

This mirrors the parser's "zero the `Command` first" convention (Chapter 3.8):
*initialize everything, then specialize.* The two layers use the same idiom,
so the habit transfers.

---

## 5.4 The dispatch table

The switch is the heart of the layer. Read it as a translation table: one
`CommandType` in, one tree call and one message out.

```
   COMMAND_INSERT  -->  btree_insert(&db->tree, key, value)
   COMMAND_SELECT  -->  btree_search(&db->tree, key)
   COMMAND_DELETE  -->  btree_delete(&db->tree, key)
   COMMAND_RANGE   -->  btree_range(&db->tree, lo, hi, range_visit, &acc)
   COMMAND_PRINT   -->  btree_print(&db->tree)
   COMMAND_EXIT    -->  (nothing)   -- REPL's own control flow
   COMMAND_HELP    -->  (nothing)
   COMMAND_NONE    -->  (nothing)   -- the parser's "unused" default
```

Every case does three things, in a fixed order: **call the tree, set `ok`,
set a message.** The tree calls all come from `btree.h`'s public API
(Chapter 4); the layer never reaches inside a node. If you squint, the switch
*is* the query plan -- one `case` per command, each a single storage call.

### 5.4.1 `COMMAND_INSERT`: new key vs. update

```c
case COMMAND_INSERT: {
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
```

`btree_insert` returns `true` for a *new* key and `false` for an overwrite of
an existing key (Chapter 4.6). That boolean is all the layer needs to pick the
verb: "inserted" versus "updated". The user sees the difference because the
difference matters -- re-running `INSERT 42 100` should *tell you* it changed
an existing row rather than pretending it added a new one. `test_db_update_
value` drives this from the top: insert `42 100`, insert `42 999`, select,
and the message/state must reflect the overwrite.

Notice the message builds from the *parsed* `command->key` / `command->value`,
not from the string -- by this point the integers are real `int`s, and
`snprintf("%d")` round-trips them exactly.

### 5.4.2 `COMMAND_SELECT`: the aliasing pointer, used correctly

```c
case COMMAND_SELECT: {
    int* v = btree_search(&db->tree, command->key);
    result->ok = true;
    if (v != NULL) {
        result->found = true;
        result->value = *v;   /* copy OUT of the tree into the result */
        snprintf(result->message, sizeof(result->message),
                 "key %d = %d", command->key, *v);
    } else {
        snprintf(result->message, sizeof(result->message),
                 "key %d not found", command->key);
    }
    break;
}
```

This is Chapter 2.4.4's aliasing contract put into practice, line by line.
`btree_search` returns a pointer into a heap leaf (Chapter 4.5). The layer
does exactly the one safe thing: it dereferences it **immediately** to copy
the value into `result->value`, then lets the pointer go out of scope. It
never stores `v`, never passes it on, never keeps it alive across the call.
The REPL receives a *copy* it owns; the tree keeps its memory; nobody dangles.

The `NULL` case is a *successful* command with a *miss*: `ok` stays `true`
(the command ran fine), `found` is `false` (no such key). The distinction is
what lets the REPL print `key 7 not found` without treating it as an error --
and `test_db_select_missing_key` asserts exactly that split: `ok` true,
`found` false.

### 5.4.3 `COMMAND_DELETE`: the boolean that doubles as a flag

```c
case COMMAND_DELETE: {
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
```

`btree_delete` returns whether the key was actually present and removed
(Chapter 4.10 -- an absent key is a safe no-op). The layer copies that boolean
straight into `result->found`, so the REPL can distinguish "the command
failed" (`ok == false`) from "the command ran, key was absent"
(`ok == true`, `found == false`). Two booleans, two orthogonal questions --
that is the whole ergonomics of the reply struct.

### 5.4.4 `COMMAND_RANGE`: the callback contract

The richest case, because `btree_range` does not return a count -- it *fires a
callback* per match (Chapter 4.11). The layer provides that callback and an
accumulator (full code in 5.5). In miniature:

```c
case COMMAND_RANGE: {
    RangeAccumulator acc;
    acc.matched = 0;
    btree_range(&db->tree, command->key, command->key_end,
                range_visit, &acc);
    result->ok = true;
    result->matched = acc.matched;
    snprintf(result->message, sizeof(result->message),
             "%d keys in [%d, %d]", acc.matched, command->key,
             command->key_end);
    break;
}
```

The accumulator lives on the *layer's* stack, is handed to the tree as
`void* context`, and the callback bumps it through that pointer. When
`btree_range` returns, `acc.matched` holds the count and the layer copies it
into `result->matched`. `Command`'s `key`/`key_end` are the inclusive window
bounds -- the fields the parser filled for exactly this command (Chapter 3.10).

### 5.4.5 `COMMAND_PRINT`, and the three acknowledgements

```c
case COMMAND_PRINT:
    btree_print(&db->tree);    /* the tree dumps itself to stdout */
    result->ok = true;
    snprintf(result->message, sizeof(result->message), "tree printed");
    break;

case COMMAND_EXIT:
case COMMAND_HELP:
case COMMAND_NONE:
    result->ok = true;
    snprintf(result->message, sizeof(result->message), "no-op");
    break;
```

`PRINT` is the one command where the *tree* owns the output -- `btree_print`
writes the ASCII dump itself (Chapter 4.12) -- and the layer just acknowledges
it. `EXIT`/`HELP`/`NONE` never touch storage: `EXIT` is the REPL's own control
flow (Chapter 1.14) and `NONE` is the parser's unused default. They all get
`ok = true` with a "no-op" ack, so every accepted command produces a uniform,
well-formed reply. The REPL treats `EXIT` specially *before* calling
`db_execute`, so these cases are mostly a courtesy that keeps the switch
total.

---

## 5.5 The callback idiom, dissected

`RANGE` is the one place in the whole project where control briefly leaves the
linear call stack, so it deserves its own look. The pieces (`src/database.c:
27`):

```c
typedef struct {
    int matched;   /* number of keys visited inside [lo, hi] */
} RangeAccumulator;

static void range_visit(int key, int value, void* context) {
    RangeAccumulator* acc = (RangeAccumulator*)context;
    (void)key;      /* we only count; the key itself is not needed */
    (void)value;    /* ditto for the value                          */
    acc->matched++;
}
```

Here is what happens at runtime, step by step:

```
   REPL:      db_execute(&db, &cmd, &result)
                |
                v
   db_execute: RangeAccumulator acc;  acc.matched = 0;
               btree_range(&db->tree, 10, 20, range_visit, &acc);
                  |
                  |   (tree walks leaves; for each key k in [10,20] it calls:)
                  v
   tree:       range_visit(k, v, &acc)
                  |   (acc is a void* pointing at db_execute's stack frame)
                  v
               acc->matched++   -- mutates db_execute's local acc
                  |
                  |   (control returns to the tree, which resumes its walk)
                  v
   tree:       ... continues ... until the window is exhausted
                  |
                  |   (returns)
                  v
   db_execute: result->matched = acc.matched;   -- reads its own acc
```

Two ideas make this safe and idiomatic:

**1. `context` is how a callback talks back to the caller's stack.** The
tree cannot return a value mid-walk, so it hands the callback a pointer the
caller supplied. The callback casts it back to the real type and mutates it.
This is the standard C idiom for "out-parameters without globals": the
accumulator lives where it is *used*, not in a global variable that would
poison re-entrancy.

**2. The `(void)key; (void)value;` lines are a deliberate no-op.** The
callback is called with `(key, value, context)` whether it wants them or not.
The casts tell both the compiler (`-Wextra` would warn about unused
parameters) and the reader: "the signature provides these, and this callback
genuinely has no use for them." A future `SUM` or `MIN` command would use
`value` here instead of counting.

Why does this *work* -- why is `acc->matched++` safe while the tree is
mid-walk? Because `acc` lives on `db_execute`'s stack frame, which is live for
the entire duration of the `btree_range` call. The tree walks its own heap
nodes and touches only `acc` through the callback; the callback touches only
`acc` and nothing of the tree's internals. Neither side pokes the other's
privates. That mutual respect is the whole contract, and it is the same
reason `btree_range` is `const`-correct (it cannot modify the tree, only walk
it) while the callback is free to count.

---

## 5.6 A full end-to-end trace: `SELECT 42`

Let us follow a *read* through all four stages, because `SELECT` shows off the
whole seam including the aliasing pointer. The tree has `42 -> 100` stored.

```
 STAGE     WHAT HAPPENS                                          VALUE
 -------   --------------------------------------------------   ----------------
 REPL      user types "SELECT 42"; fgets reads the line          "SELECT 42\n"
 LEXER     lex(line,...)  ->  0:(KEYWORD:SELECT) 1:(INT:42)     token_count = 3
                              2:(END)
 PARSER    parse_command: dispatch on KEYWORD_SELECT,            PARSE_OK
           consume exactly 1 integer, require END after it
           ->  Command { type=COMMAND_SELECT, key=42,
                        value=0, key_end=0 }
 db_execute switch: case COMMAND_SELECT:
           int* v = btree_search(&db->tree, 42);
              (descent: root internal -> first_greater(42)
               -> right child -> leaf, scan keys, key 42 found)
              v == &leaf->u.values[i]   (pointer INTO a heap leaf)
           result->found = true
           result->value = *v            (copy OUT, value = 100)
           snprintf: "key 42 = 100"
 REPL      printf("%s\n", result.message)                        "key 42 = 100"
```

Four functions, four structs, and exactly one tree mutation call across the
whole pipeline is impossible here -- `SELECT` never mutates. The layer
borrowed a pointer, copied out, and returned control without ever owning tree
memory.

Now the same trace for `RANGE 10 20`, to see the two-directional flow:

```
 db_execute: case COMMAND_RANGE:
   RangeAccumulator acc; acc.matched = 0;
   btree_range(&db->tree, 10, 20, range_visit, &acc);
      |  tree descends to the first leaf >= 10
      |  for each key in [10,20], in sorted order:
      |       range_visit(key, value, &acc)  ->  acc.matched++
      |  (early exit when a key > 20)
      v
   result->matched = acc.matched;      (e.g. 6)
   snprintf: "6 keys in [10, 20]"
```

The data flows in and the count flows back -- up through the callback, out
through `acc`, into the reply. Both directions cross the seam cleanly.

---

## 5.7 The seam as a growth point: what a planner looks like

The header comment is worth reading in full, because it is the project's
honest statement of ambition (`src/database.c:7`):

> `db_execute()` is the QUERY ENGINE of MiniDB. It is the single seam where
> the parsed frontend (a Command, produced by lexer + parser) meets the
> storage backend (the B+ tree). Every REPL command funnels through this one
> switch, which is exactly where a real database would grow a
> planner/optimiser: it would rewrite the Command, choose access paths, or
> compile a query plan before touching the tree. Here the "plan" is trivial --
> one switch case, one tree call -- but the architectural position is the
> same.

Concretely: the day MiniDB grows a `WHERE` clause, the parser will produce a
richer `Command`, and the *planner* in `db_execute` would translate it into a
sequence of tree calls -- "descend via separator 15, then walk the chain from
leaf [15] filtering by the predicate." The tree stays untouched; the plan
lives in the seam. That is the division of labour real databases draw between
the parser, the planner, and the storage engine, compressed into 180 lines you
can read in one sitting.

The same seam is where the two booleans earn their keep. `result->ok` answers
"did the command execute?"; `result->found` answers "did the data exist?"; a
planner that rewrites a `DELETE` into a `SELECT`+`DELETE` pair can reuse the
same reply vocabulary without changing the REPL's printing code at all.

---

## 5.8 Chapter summary

- `database.c` is the **seam**: it translates exactly one `Command` into
  exactly one tree call and one `DbResult`. It neither parses nor balances.
- `Database` is a one-field wrapper today -- a stable place for future
  growth; `DbResult` is the entire reply vocabulary.
- `db_init`/`db_close` are the lifecycle: zero the struct, allocate the empty
  root leaf; free every node on the way out.
- Every result starts **defensively zeroed**, then each case overwrites only
  the fields it owns -- so every outcome is defined.
- Each case is a fixed three-step: **call the tree, set `ok`, set a message**.
  `btree_insert`'s boolean picks "inserted" vs "updated"; `btree_search`'s
  aliasing pointer is copied out immediately; `btree_delete`'s boolean doubles
  as `found`; `RANGE` uses a callback with a stack accumulator.
- The callback idiom -- `void* context` pointing at the caller's frame -- is
  the standard "out-parameter without a global", and the two-directional data
  flow crosses the seam cleanly.
- The switch is the trivial case of a **query planner**; growing the language
  grows the seam, not the tree.

**Questions to test yourself**

1. In `COMMAND_SELECT`, why is it safe to dereference `v` but wrong to store
   it in `result`? What could invalidate it after `db_execute` returns?
2. Why does `result->found` mean different things for `SELECT` and `DELETE`,
   and why is that a feature rather than a bug? Where does the REPL
   distinguish "command failed" from "key absent"?
3. `RANGE` puts its accumulator on the stack and hands its address to the
   tree. What would break if the accumulator were a global instead?
4. Trace `INSERT 7 70` through all four stages like 5.6. At which stage does
   the tree first *mutate*, and what does `btree_insert` return?
5. `PRINT` dumps the tree to stdout from inside `db_execute`. Why is that a
   slight break from the "the REPL owns all output" rule, and why is it
   acceptable here?
6. Add a `COUNT` command mentally: which fields of `Command` would the parser
   set, which tree call would the seam make, and which `DbResult` field would
   carry the answer?

---

# Chapter 6 -- Test-Driven Systems

*How the Unity suite works, what each of the 33 tests proves, and how to
write database assertions of your own.*

---

## 6.1 Why a database needs tests more than most programs

The B+ tree's whole value is its **discipline** -- the five invariants from
Chapter 4.2. Invariants are invisible when they hold; you only see them when
they break, and by then they have usually been broken for a while. A buggy
separator can sit in a tree for a thousand inserts, mis-routing only the
occasional lookup, before anyone notices. A misplaced `size_t` can write one
slot past an array and corrupt a neighbouring field silently (Chapter 2.5.1
explained why the spare slot exists *because* of that). These are exactly the
bugs that unit tests exist to catch, and they are exactly the bugs that a
single happy-path run will miss.

So MiniDB comes with a **33-test Unity suite** that pins down every layer's
contract from the bottom up:

| Suite             | Tests | Pins down                                     |
|-------------------|------:|-----------------------------------------------|
| `test_btree.c`    |    10 | The tree's contract: insert, search, update,  |
|                   |       | splits, range, delete, safety, differential.  |
| `test_lexer.c`    |     9 | Token shapes, case-insensitivity, whitespace, |
|                   |       | every error path, the token budget.           |
| `test_parser.c`   |     8 | Every valid command, every arity error,       |
|                   |       | hand-built malformed streams, error hygiene.  |
| `test_database.c` |     6 | End-to-end: string -> tree -> reply.          |
| **Total**         |  **33** |                                              |

One command runs them all:

```
$ make test
... 33 Tests 0 Failures 0 Ignored
OK
```

When you finish reading this chapter you will be able to *explain* any one of
those 33 tests, and to *write* a new one that follows the same shape.

---

## 6.2 What Unity is, and the shape of a test

**Unity** is a tiny, vendored C test framework (the three files
`tests/unity.h`, `tests/unity.c`, `tests/unity_internals.h` are copies of a
public, MIT-licensed project -- "vendored" means *checked in as-is, never
edited*). It is the smallest thing that can possibly work: a collection of
`TEST_ASSERT_*` macros plus a runner. No registration framework, no
reflection, no build tooling -- you call a test function directly.

The anatomy of every test in this project is the same three-part shape:

```c
void test_btree_insert_search(void) {     /* 1. a plain function, no args   */
    BTree tree;                            /* 2. arrange: set up the world    */
    btree_init(&tree);

    TEST_ASSERT_TRUE(btree_insert(&tree, 42, 100));    /* 3. act + assert   */
    TEST_ASSERT_EQUAL_UINT(3u, (unsigned)btree_size(&tree));

    int* v = btree_search(&tree, 42);
    TEST_ASSERT_NOT_NULL(v);               /* assert the *behaviour*         */
    TEST_ASSERT_EQUAL_INT(100, *v);

    btree_destroy(&tree);                  /* 4. cleanup (or a test macro)    */
}
```

Arrange, act, assert, clean up. The macros are the interesting part:

| Macro                               | Asserts                                  |
|-------------------------------------|------------------------------------------|
| `TEST_ASSERT_TRUE(x)`               | `x` is true                              |
| `TEST_ASSERT_FALSE(x)`              | `x` is false                             |
| `TEST_ASSERT_NULL(p)`               | `p == NULL`                              |
| `TEST_ASSERT_NOT_NULL(p)`           | `p != NULL`                              |
| `TEST_ASSERT_EQUAL_INT(a, b)`       | `a == b` as ints (prints both on failure) |
| `TEST_ASSERT_EQUAL_UINT(a, b)`      | `a == b` as unsigned                      |
| `TEST_ASSERT_EQUAL_STRING(a, b)`    | the strings match (used for messages)     |
| `TEST_ASSERT_NOT_NULL_MESSAGE(p, m)`| `p != NULL`, plus a custom failure text   |

Why `TEST_ASSERT_EQUAL_INT(42, tokens[1].int_value)` instead of a bare
`assert(tokens[1].int_value == 42)`? Because Unity's macros **report which
test, which line, and what the two values actually were** when they fail. A
bare `assert` aborts the whole binary at the first failure; Unity continues
past a failing assertion within a test, records it, and runs every remaining
test, then prints the summary. That is what makes the suite *actionable*: one
bad change shows up as "2 Failures", with the exact file:line and expected vs
actual of each.

The framework also provides `setUp()` and `tearDown()`, which run before and
after every test when defined. MiniDB's are empty (each test initializes and
destroys its own state explicitly -- see 6.3), but their presence is what
keeps Unity happy.

---

## 6.3 The test binary: one `main()`, many suites

The test executable is built from the *same four library sources* as the
REPL, but with different glue (Chapter 7 dissects the Makefile):

```
   minidb_test  =  unity.c  +  run_tests.c  +  tests/test_*.c
               +  src/btree.c  src/lexer.c  src/parser.c  src/database.c
```

`run_tests.c` is the whole harness (`tests/run_tests.c`):

```c
void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();

    /* ---- B+ tree ---- */
    RUN_TEST(test_btree_insert_search);
    RUN_TEST(test_btree_duplicate_updates_value);
    RUN_TEST(test_btree_ascending_insert_splits);
    /* ... */

    /* ---- Lexer ---- */
    RUN_TEST(test_lex_insert_basic);
    /* ... */

    return UNITY_END();
}
```

`RUN_TEST(fn)` runs the function and records pass/fail; `UNITY_END()` prints
the summary and returns an exit code a build script can check. The list of
names is the entire suite, written by hand -- an explicit, greppable registry.
Each test function is declared in `tests/test_suites.h` (so the compiler sees
a prototype) and defined non-static in its suite file (so it can be linked
in). Every test appears in exactly two places: its definition and one
`RUN_TEST` line. There is no magic; adding a test means adding those two
lines plus the definition.

Note the division: `run_tests.c` is the *only* file with a `main()` in the
test binary, and `src/main.c` (the REPL's `main()`) is deliberately **not**
linked in. One `main()` per executable -- the classic C rule the Makefile
respects, and the reason the library sources must never define a `main`.

Each test manages its own state (call `btree_init`, then `btree_destroy`),
which is why `setUp`/`tearDown` are empty: the tests do not share mutable
globals, so there is nothing to reset between them. The one deliberate
exception is the differential test's reference array, which it clears itself.

---

## 6.4 The lexer suite: pinning the state machine

`test_lexer.c` tests `lex()` as a pure function: feed a string, assert on the
tokens and count, or on the exact error code. Nine tests, each aimed at one
behaviour from Chapter 3:

| Test                                   | What it pins                                    |
|----------------------------------------|-------------------------------------------------|
| `test_lex_insert_basic`                | `"INSERT 42 100"` -> exactly `(KEYWORD:INSERT) (INT:42) (INT:100) (END)`, count 4. |
| `test_lex_keyword_case_insensitive`    | Every verb in several mixed cases maps to the same `Keyword` value. |
| `test_lex_whitespace_tolerance`        | Spaces, tabs, newlines around and between tokens are invisible. |
| `test_lex_bad_number_overflow`         | `2147483647` accepted; `2147483648` rejected as `LEX_ERROR_BAD_NUMBER`. |
| `test_lex_unknown_word`                | `FOO`, `INSERT 1 FOO`, `INSERTX 1 2` all rejected. |
| `test_lex_stray_symbol`                | `$`, `;`, `?` rejected as unknown-word errors.  |
| `test_lex_empty_input`                 | `""` -> exactly the END sentinel, count 1.      |
| `test_lex_too_many_tokens`             | 15 ints fit (room for END); 16 ints overflow.   |
| `test_lex_keyword_to_string`           | The reverse map returns stable uppercase strings. |

Two of these are worth a closer look because they show *how* to test a state
machine.

**Testing case-insensitivity by table.** Rather than sixteen hand-written
assertions, the test packs all the cases into a small struct array and loops:

```c
struct { const char* word; Keyword expected; } cases[] = {
    { "INSERT", KEYWORD_INSERT }, { "insert", KEYWORD_INSERT },
    { "Insert", KEYWORD_INSERT }, { "iNsErT", KEYWORD_INSERT },
    { "SELECT", KEYWORD_SELECT }, { "select", KEYWORD_SELECT },
    { "sElEcT", KEYWORD_SELECT },
    /* ... */
};
for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
    count = 0;
    TEST_ASSERT_EQUAL_INT(LEX_OK, lex_to(cases[i].word, tokens, &count));
    TEST_ASSERT_EQUAL_UINT(2u, (unsigned)count);   /* keyword + END */
    TEST_ASSERT_EQUAL_INT(TOKEN_KEYWORD, tokens[0].type);
    TEST_ASSERT_EQUAL_INT(cases[i].expected, tokens[0].keyword);
    TEST_ASSERT_EQUAL_INT(TOKEN_END, tokens[1].type);
}
```

The pattern -- *data table + loop + a small assertion set* -- is the compact
way to test "many inputs, one contract". If a future developer adds a keyword
and forgets the case-insensitive path, the failing row names the word.

**Testing the boundary of a hard limit.** `test_lex_too_many_tokens` pushes
the capacity precisely: 15 integers leave exactly one slot free for the END
sentinel (success, count 16); 16 integers consume every slot (overflow). The
test encodes the *design decision* from Chapter 3.6 -- that END counts toward
`LEXER_MAX_TOKENS` -- so any refactor that changes the accounting fails
loudly.

The suite also leans on a tiny helper, `lex_to`, that just zeroes the count
and calls `lex` -- a one-line convenience that keeps every test body about
*behaviour*, not plumbing.

---

## 6.5 The parser suite: valid commands and every error path

`test_parser.c` tests `parse_command` two ways: through real lexed strings
(via a `parse()` helper that lexes then parses) and through **hand-built
Token arrays**, because some malformed streams can never be produced by the
lexer (Chapter 3.9). Eight tests:

| Test                              | What it pins                                   |
|-----------------------------------|------------------------------------------------|
| `test_parse_all_valid_commands`   | Every command parses to the exact `Command` fields. |
| `test_parse_missing_argument`     | `INSERT 42`, `SELECT`, `DELETE`, `RANGE 10` -> `MISSING_ARGUMENT`. |
| `test_parse_extra_argument`       | `INSERT 1 2 3`, `SELECT 1 2`, `PRINT 1`, ... -> `EXTRA_ARGUMENT`. |
| `test_parse_bad_argument`         | Hand-built streams with a keyword mid-args -> `BAD_ARGUMENT`. |
| `test_parse_unknown_command`      | Stream starting with an int, or with `KEYWORD_UNKNOWN`. |
| `test_parse_empty`                | `""`, a lone END token, and even `(NULL, 0)`.   |
| `test_parse_result_and_type_strings` | Every label helper returns its exact string. |
| `test_parse_error_resets_command` | A failed parse leaves `Command` as `COMMAND_NONE` + zeros. |

The last one is the contract that keeps `db_execute` safe (Chapter 3.10): the
parser promises that on any error, `command` is a fully-zeroed "nothing". The
test *first* fills `cmd` with garbage (`type = COMMAND_INSERT; key = 99; ...`),
then parses a failing stream, then asserts every field was reset:

```c
cmd.type = COMMAND_INSERT;
cmd.key = 99; cmd.value = 99; cmd.key_end = 99;
TEST_ASSERT_EQUAL_INT(PARSE_ERROR_MISSING_ARGUMENT, parse("INSERT 42", &cmd));
TEST_ASSERT_EQUAL_INT(COMMAND_NONE, cmd.type);
TEST_ASSERT_EQUAL_INT(0, cmd.key);
TEST_ASSERT_EQUAL_INT(0, cmd.value);
TEST_ASSERT_EQUAL_INT(0, cmd.key_end);
```

The poisoned-input trick is a strong pattern: **to prove "on error the output
is well-defined", start from a deliberately bad state and assert the reset.**
A parser that forgot its zeroing step would fail this test instantly.

The hand-built token helpers are worth noting too:

```c
static Token kw(Keyword k)   { Token t; t.type = TOKEN_KEYWORD; t.keyword = k; ... }
static Token num(int v)      { Token t; t.type = TOKEN_INTEGER; t.int_value = v; ... }
static Token end_token(void) { Token t; t.type = TOKEN_END; ... }
```

These constructors let the tests fabricate streams the lexer could never make
-- e.g. `{ kw(INSERT), num(5), kw(SELECT), end_token() }`, a keyword where an
integer belongs. That stream would be `LEX_ERROR` from the lexer, so the only
way to test the parser's `BAD_ARGUMENT` branch is to bypass the lexer and hand
the parser the tokens directly. This is a real division of labour: the parser
must be robust against *any* stream, not only against lexer-produced ones.

---

## 6.6 The tree suite: contracts, stress, and ground truth

`test_btree.c` is the largest suite, and it earns its size: the tree is where
structural bugs live. Ten tests, in roughly increasing ambition:

| Test                                   | What it pins                                   |
|----------------------------------------|------------------------------------------------|
| `test_btree_insert_search`             | Basic insert/search contract, hits and misses. |
| `test_btree_duplicate_updates_value`   | Re-insert updates; size never grows.           |
| `test_btree_ascending_insert_splits`   | 200 ascending keys force leaf/internal/root splits; all findable. |
| `test_btree_descending_insert_splits`  | Same in reverse order, stressing the leftmost path. |
| `test_btree_range_scan`                | Inclusive bounds, ordering, empty and inverted windows. |
| `test_btree_delete_keys`               | Deleting separators and neighbours; no-op deletes. |
| `test_btree_delete_all_empties_tree`   | Emptying the tree shrinks it back to one empty root. |
| `test_btree_empty_tree_safe`           | Every API is a no-op on an empty tree.         |
| `test_btree_reinsert_after_delete`     | A deleted key comes back like a fresh insert.  |
| `test_btree_differential_random`       | ~20k random ops checked against a sorted array.|

The hand-written tests deliberately **drive the interesting structural
moments**. `test_btree_ascending_insert_splits` inserts `1..200`: with a
fan-out of 3, that forces leaf splits (every 3rd insert), internal splits, and
root splits (when the root overflows), and then asserts every one of the 200
keys is still findable with its value. Inserting *descending* (`200..1`)
exercises the mirror image: every new minimum lands on the far-left path,
which stresses separator promotion differently. Together they cover the two
extremes of the "where does the new key go" distribution.

`test_btree_range_scan` checks the exact contract of the range operation
from Chapter 4.11: inclusive bounds (both endpoints appear), ascending order
(the callback fires in sorted sequence), a single-point window, the full
window, and windows that overlap nothing (`[0,0]`, `[21,30]`, `[-50,-1]`) --
plus the inverted `[15,5]` case. To assert on the *ordered sequence* rather
than just a count, it uses a `RangeCollector` that copies the visited pairs
into caller buffers:

```c
static void collect_visit(int key, int value, void* context) {
    RangeCollector* c = (RangeCollector*)context;
    TEST_ASSERT(c->count < c->capacity);   /* guard: never overflow the buffers */
    c->keys[c->count] = key;
    c->values[c->count] = value;
    c->count++;
}
```

This is the Chapter 5 callback idiom turned into a *test instrument*: the
test supplies its own accumulator and inspects the exact sequence. The guard
`TEST_ASSERT(c->count < c->capacity)` makes the collector itself safe to run
inside a test.

`test_btree_empty_tree_safe` pins the design decision from Chapter 2.4.3:
the empty tree is a single empty leaf, so searching, deleting, and ranging
over it must all be safe no-ops -- and it must still accept inserts
afterwards. It exists to prevent a regression to the `NULL`-root design with
its scattered special cases.

---

## 6.7 The differential test: ground truth for a tree

The most valuable test in the suite is the last one, `test_btree_differential
_random`. The problem it solves: a 200-key stress test proves the tree handles
*that* workload, but a real bug -- a wrong separator, a broken chain link, a
key silently dropped in a merge -- can be triggered only by a *specific*
sequence of operations. Hunting for that sequence by hand is hopeless. So the
test delegates the hunting to `rand()`:

```c
const int ops = 20000;
srand(42);   /* fixed seed: the sequence is fully reproducible */

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
    if ((op + 1) % check_every == 0) {
        assert_tree_matches_reference(&tree);   /* every 500 ops */
    }
}
assert_tree_matches_reference(&tree);           /* and a final sweep */
```

The idea is the oldest in testing: **keep a naive, obviously-correct
reference implementation, and check the clever one against it.** The reference
here is a plain sorted array of `{key, value}` pairs (`ref_keys`,
`ref_values`, `ref_count`) with three dumb operations: linear insertion
keeping sort order, linear find, linear delete. It is slow, it is simple, and
it is *right* -- it is the "sorted array" straw man from Chapter 4.1, used as
a witness rather than a rival.

Every operation is mirrored into the reference, and every 500 operations the
tree is required to agree with it in full:

```c
static void assert_tree_matches_reference(BTree* tree) {
    TEST_ASSERT_EQUAL_UINT((unsigned)ref_count, (unsigned)btree_size(tree));
    for (int key = 0; key < REF_MAX; key++) {
        int i = ref_find(key);
        int* v = btree_search(tree, key);
        if (i >= 0) {
            TEST_ASSERT_NOT_NULL_MESSAGE(v, "key present in reference but missing in the tree");
            if (v != NULL) TEST_ASSERT_EQUAL_INT(ref_values[i], *v);
        } else {
            TEST_ASSERT_NULL_MESSAGE(v, "key absent in reference but found in the tree");
        }
    }
}
```

For every key in `[0, 512)`: if the reference says *present*, the tree must
return it with the same value; if the reference says *absent*, the tree must
return `NULL`. Any divergence -- a dropped key, a stale separator routing a
lookup astray, a duplicate entry surviving a merge -- fails the test with a
message that says which side was wrong.

Two details make this test trustworthy rather than merely random:

- **`srand(42)` fixes the seed.** The sequence is fully reproducible: the
  same 20,000 pseudo-random operations happen on every run, on every machine.
  A failure is reproducible and debuggable, and the same sequence now guards
  every future change to the tree.
- **The reference is independent in kind.** It shares no code with the tree
  (no `btree_insert` calls inside), so a bug in the tree cannot be mirrored
  into the reference and cancel out. Any shared misunderstanding -- say, "both
  silently drop negative keys" -- is caught by the hand-written tests that
  assert on specific known values.

The file's header comment states the philosophy (`test_btree.c:15`):
*"Any structural bug (wrong separator, broken leaf chain, lost key) that the
hand-picked tests miss will almost certainly show up here."* This is the
strength of property-style testing with a reference: you trade a few lines of
simple, obviously-correct code for a search that covers ~20,000 operations
across insertions, deletions, splits, borrows, and merges on every run.

---

## 6.8 The database suite: end-to-end through the whole pipeline

`test_database.c` is the "integration test" tier. The tree and front-end
suites test their layers in isolation; this suite drives the *entire stack*
-- lexer, parser, and tree -- with real command strings, exactly as the REPL
does. Its workhorse is a helper that performs all three stages in one call
(`test_database.c:18`):

```c
static void exec(Database* db, const char* input, DbResult* result) {
    Token tokens[LEXER_MAX_TOKENS];
    size_t count = 0;
    Command cmd;

    TEST_ASSERT_EQUAL_INT(LEX_OK, lex(input, tokens, &count));
    TEST_ASSERT_EQUAL_INT(PARSE_OK, parse_command(tokens, count, &cmd));
    db_execute(db, &cmd, result);
}
```

The assertions embedded in `exec` are a *contract in code*: it is an
unrecoverable test failure if the input does not lex or does not parse. So a
test can write `exec(&db, "INSERT 42 100", &r)` and trust that the string made
it all the way to the tree. The six tests then assert on `DbResult` fields --
the reply vocabulary from Chapter 5.1:

```c
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

    exec(&db, "SELECT 42", &r);       /* after the delete... */
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_FALSE(r.found);       /* ...the same select must miss */

    db_close(&db);
}
```

Notice what this test proves that no lower-level test can: that the **whole
chain agrees on semantics**. `DELETE` followed by `SELECT` *in the language*
yields `found = false`; the parser's `Command`, the tree's deletion, and the
layer's message all cooperate. A test at the tree level would call
`btree_delete` directly and could never catch a mismatch between the parser's
interpretation of `DELETE 42` and the tree's notion of key `42`.

The suite covers the round trips: insert-select-delete (above), update
semantics (`test_db_update_value`), range counts (`test_db_range_matched_
count`: `RANGE 5 10` -> `matched == 6`, `RANGE 1 20` -> `matched == 20`,
`RANGE 21 30` -> `matched == 0`), lifecycle (`test_db_reinit_after_close`),
the PRINT ack (`test_db_print_reports_ok`), and missing keys (`test_db_select
_missing_key`: `ok` true but `found` false for both SELECT and DELETE of
absent keys).

---

## 6.9 How to write a database assertion: a worked example

The best way to internalize all of this is to extend the database and pin the
extension with tests. Let us add a `COUNT` command -- "how many keys exist
right now" -- following the recipe from Chapter 3.12, and *test every layer
as we go*. The tree already exposes `btree_size()`, so the storage side is
free; the work is language and tests.

**Step 1 -- lexer.** Add `KEYWORD_COUNT` to the enum in `lexer.h` *before*
`KEYWORD_UNKNOWN` (the sentinel sizes the table), add `"COUNT"` to
`KEYWORD_TABLE` in `lexer.c` (the enum index is the table index), and add a
matching `if` in `lex()`.

**Step 2 -- parser.** Add `COMMAND_COUNT` to `CommandType` in `parser.h`
before `COMMAND_NONE`, a `case` in the dispatch with arity 0 (same shape as
`PRINT`/`EXIT`/`HELP`), and a label in `command_type_string`.

**Step 3 -- database.** Add a `case COMMAND_COUNT` in `db_execute`:

```c
case COMMAND_COUNT:
    result->ok = true;
    snprintf(result->message, sizeof(result->message),
             "%zu keys", btree_size(&db->tree));
    break;
```

**Step 4 -- tests, bottom to top.** Every claim that this feature makes must
be asserted at the layer that could break it.

Lexer test (in `test_lexer.c`, appended to the keyword table or as a row in
the case-insensitivity loop):

```c
TEST_ASSERT_EQUAL_INT(LEX_OK, lex_to("COUNT", tokens, &count));
TEST_ASSERT_EQUAL_INT(TOKEN_KEYWORD, tokens[0].type);
TEST_ASSERT_EQUAL_INT(KEYWORD_COUNT, tokens[0].keyword);
TEST_ASSERT_EQUAL_INT(TOKEN_END, tokens[1].type);
```

Parser test (in `test_parse_all_valid_commands`):

```c
TEST_ASSERT_EQUAL_INT(PARSE_OK, parse("COUNT", &cmd));
TEST_ASSERT_EQUAL_INT(COMMAND_COUNT, cmd.type);
```

Database test -- the assertion that actually matters, exercising the whole
pipeline and checking the *reply*:

```c
void test_db_count_keys(void) {
    Database db;
    db_init(&db);
    DbResult r;

    exec(&db, "COUNT", &r);                       /* empty database */
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_STRING("0 keys", r.message);

    exec(&db, "INSERT 1 10", &r);
    exec(&db, "INSERT 2 20", &r);
    exec(&db, "COUNT", &r);
    TEST_ASSERT_EQUAL_STRING("2 keys", r.message);

    db_close(&db);
}
```

`TEST_ASSERT_EQUAL_STRING` on `r.message` is the database assertion style:
the reply string is part of the contract (the REPL prints it verbatim), so
the test pins the *exact* wording. Then wire it up: declare
`void test_db_count_keys(void);` in `test_suites.h` and add
`RUN_TEST(test_db_count_keys);` in `run_tests.c` under the database section.

**Step 5 -- run it.** `make test` now reports **34 Tests 0 Failures**.
Each layer's test lives beside the code it tests, so a regression in any
single layer -- the lexer stops recognizing `COUNT`, the parser rejects it,
the database computes the wrong number -- fails one specific test at the layer
where the bug sits.

That five-step loop is the whole test-driven method in miniature: **write the
behaviour, pin it at every layer, run the suite.** Every future command
(`CLEAR`, `MIN`, `SUM`, ...) follows the identical recipe.

---

## 6.10 Running the suite and reading the verdict

`make test` does two things: rebuilds `minidb_test` if needed, then runs it.
Unity prints one `PASS` line per test with its location, then the summary:

```
tests/run_tests.c:19:test_btree_insert_search:PASS
tests/run_tests.c:20:test_btree_duplicate_updates_value:PASS
... (one line per test, grouped by suite) ...

-----------------------
33 Tests 0 Failures 0 Ignored
OK
```

The exit code of the binary is non-zero on any failure, so a CI script (or a
pre-commit hook) can `make test && echo good` and trust the result. When a
test *does* fail, Unity prints the expected and actual values with the exact
`file:line`, e.g.:

```
tests/run_tests.c:37:test_lex_too_many_tokens:FAIL: Expected 1 Was 0
```

Because every test is reproducible (no timers, no I/O, fixed seed in the
differential test), a failing test is a *reproducible* failing test: you can
start a debugger, step through the failing assertion, and fix the code with
confidence.

The one piece of noise in the output is expected: `test_db_print_reports_ok`
calls `PRINT`, so the tree's ASCII dump scrolls by mid-suite. That is fine --
it is the tree's own output under test, not a failure.

---

## 6.11 Chapter summary

- A database's value is its **invariants**, and invariants only show up when
  they break -- so MiniDB pins every layer's contract with 33 Unity tests.
- **Unity** is a vendored micro-framework of assert macros plus a runner; the
  macros report exact expected/actual values and let the suite keep running
  past a failure.
- The test binary links the **same four library sources** with `run_tests.c`
  supplying the `main()`; `src/main.c` is excluded -- one `main()` per
  executable.
- **Lexer tests** pin token shapes, case-insensitivity (via a data table),
  whitespace tolerance, every error code, and the exact token budget.
- **Parser tests** pin every valid command and every arity error, using
  hand-built streams for cases the lexer can never produce, and poisoned
  inputs to prove error hygiene.
- **Tree tests** force the structural moments -- splits, borrows, merges,
  root growth and shrink -- and the **differential test** checks ~20k random
  operations against a plain sorted array as ground truth, with a fixed seed.
- **Database tests** drive the whole pipeline through an `exec()` helper and
  assert on the `DbResult` reply -- the integration tier that catches
  cross-layer semantic mismatches.
- New behaviour is added **bottom to top**: pin the lexer, pin the parser,
  pin the database, register, run. The suite grows by one test per layer.

**Questions to test yourself**

1. Why does the test binary exclude `src/main.c`? What would break if it were
   linked in?
2. The differential test uses a fixed seed. What debugging property does that
   buy, and what would change if the seed were `time(NULL)`?
3. `test_parse_bad_argument` hand-builds a stream the lexer can never emit.
   Why is that necessary, and what layer does it isolate?
4. In `test_btree_range_scan`, why does the collector assert
   `c->count < c->capacity` before writing? What would a naive collector risk?
5. `exec()` asserts `LEX_OK` and `PARSE_OK` inside the helper. Why is that
   better than having each test check them separately?
6. Extend the COUNT recipe to a `SUM` command that totals all values. Which
   tree primitive would the database case need, and how would the differential
   test's reference need to change (if at all)?

---

# Chapter 7 -- The Build System

*A line-by-line breakdown of the Makefile.*

---

## 7.1 What the build has to accomplish

The project needs two different executables from one shared set of sources:

```
   minidb        the interactive REPL   (src/main.c + 4 libraries)
   minidb_test   the 33-test runner     (tests/*.c + 4 libraries, NO main.c)
```

The catch is the classic one: **each executable needs exactly one `main()`**,
and the two executables want different `main()`s. The Makefile solves this
the simple, classic way -- by declaring which files each binary is built from.
The entire file is 62 lines, and it has no generated object files, no
dependency files, no libraries, no install rules. That spareness is
deliberate: for a project this size, the Makefile should be *readable as a
spec*, not a build framework. Chapter 7 walks it top to bottom.

Here is the file in full, then we will take it apart line by line
(`Makefile`):

```makefile
# ==============================================================================
# MiniDB Makefile
#
# Targets:
#   all   - build the minidb REPL binary
#   test  - build the minidb_test binary and run the test suite
#   clean - remove both binaries and any .o files
#   help  - print the available targets
# ==============================================================================

CC     ?= gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -g -Isrc -Itests

LIBSRC  = src/btree.c src/lexer.c src/parser.c src/database.c

TESTSRC = $(wildcard tests/test_*.c)

BIN     = minidb
TESTBIN = minidb_test

.PHONY: all test clean help

all: $(BIN)

$(BIN): src/main.c $(LIBSRC)
	$(CC) $(CFLAGS) -o $@ src/main.c $(LIBSRC)

$(TESTBIN): tests/unity.c tests/run_tests.c $(TESTSRC) $(LIBSRC)
	$(CC) $(CFLAGS) -o $@ tests/unity.c tests/run_tests.c $(TESTSRC) $(LIBSRC)

test: $(TESTBIN)
	./$(TESTBIN)

clean:
	rm -f $(BIN) $(TESTBIN) *.o src/*.o tests/*.o

help:
	@echo "MiniDB build targets:"
	@echo "  all   - build the $(BIN) REPL binary"
	@echo "  test  - build and run the $(TESTBIN) test suite"
	@echo "  clean - remove binaries and object files"
	@echo "  help  - show this help"
```

---

## 7.2 Variables: one place for everything that repeats

```makefile
CC     ?= gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -g -Isrc -Itests
```

**`CC ?= gcc`** -- the `?=` operator means "assign only if not already set".
Make has a built-in variable named `CC`; by default it is `cc`. Writing
`CC ?= gcc` overrides that default to `gcc` on a clean environment, but if the
user invokes `make CC=clang`, the command line wins and the build uses clang
without editing a single line of the file. This is the idiomatic way to make a
compiler choice *overridable* rather than *hard-coded*.

**`CFLAGS`** is the single shared flag string:

| Flag        | What it does                                             |
|-------------|----------------------------------------------------------|
| `-std=c11`  | Compile as ISO C11 -- the project's portability baseline.|
| `-Wall`     | Enable the standard warning battery.                     |
| `-Wextra`   | Enable the extended warnings (incl. mixed-sign compares).|
| `-Wpedantic`| Refuse non-standard constructs.                          |
| `-g`        | Emit debug info, so every diagram in this book is debuggable. |
| `-Isrc`     | Add `src/` to the include path (`#include "btree.h"`).   |
| `-Itests`   | Add `tests/` to the include path (`#include "unity.h"`). |

The `-Wall -Wextra -Wpedantic` trio is Chapter 2.8's *enforcement arm* made
concrete: the style contract is partly compiled in. A change that introduces
a mixed `int`/`size_t` comparison or an uninitialized read no longer just
"works anyway" -- it warns, and the project's rule is that a clean build is a
test.

```makefile
LIBSRC  = src/btree.c src/lexer.c src/parser.c src/database.c
TESTSRC = $(wildcard tests/test_*.c)
BIN     = minidb
TESTBIN = minidb_test
```

Four more variables, each a decision in disguise:

- **`LIBSRC`** is the set of library sources *shared by both binaries*. The
  comment in the file calls this out: "The four library sources shared by BOTH
  executables." Listing them once means the two build rules cannot drift apart
  -- if a fifth library file appears, it is added in one place.
- **`TESTSRC = $(wildcard tests/test_*.c)`** is make's glob. Instead of
  naming the four test suites by hand, make expands any `test_*.c` in the
  `tests/` directory. Add a new suite file and the test build picks it up
  with no Makefile edit -- a small taste of automation that keeps the file
  honest.
- **`BIN` / `TESTBIN`** name the two products. Using variables everywhere
  means `clean` and `help` and the build rules all reference the same names,
  so renaming a binary is a one-line change.

---

## 7.3 The dependency graph the rules declare

Make is driven by **rules**: `target: prerequisites` followed by the recipe
(the indented shell lines) that builds the target from its prerequisites. The
heart of the Makefile is its two build rules:

```
             all
              |
              v
           minidb                 minidb_test
      src/main.c | LIBSRC    tests/unity.c | tests/run_tests.c
                           TESTSRC (4 suites) | LIBSRC

   minidb is built from:  src/main.c + the 4 library sources
   minidb_test is built from: unity.c + run_tests.c + 4 test suites
                          + the 4 library sources   (NO src/main.c)
```

Make's contract: **a target is out of date if any prerequisite is newer than
it.** So `minidb` is rebuilt when `src/main.c` or any library source changes;
`minidb_test` is rebuilt when any test file or library source changes. This
is make's answer to "did anything change?" -- the comparison is by file
timestamp, not by scanning contents.

```makefile
all: $(BIN)

$(BIN): src/main.c $(LIBSRC)
	$(CC) $(CFLAGS) -o $@ src/main.c $(LIBSRC)
```

The **default target** is whatever rule appears first. `all: $(BIN)` is the
first rule, so a bare `make` builds `minidb`. The `$@` in the recipe is make's
automatic variable for "the name of the target being built" -- a quiet way to
avoid typing `minidb` twice. The recipe is one line: compile `main.c` plus
all four libraries into the single `minidb` executable in one pass. (Chapter
7.5 explains why there are no intermediate `.o` files.)

```makefile
$(TESTBIN): tests/unity.c tests/run_tests.c $(TESTSRC) $(LIBSRC)
	$(CC) $(CFLAGS) -o $@ tests/unity.c tests/run_tests.c $(TESTSRC) $(LIBSRC)
```

The test rule is the *same* recipe with a different `main()`. It compiles
Unity, the test runner, every suite, and the libraries -- and pointedly omits
`src/main.c`, because `run_tests.c` brings its own `main()` (Chapter 6.3).
One `main()` per executable, guaranteed by listing files, not by any tool.

---

## 7.4 The user-facing targets

```makefile
.PHONY: all test clean help
```

The `.PHONY` declaration is make's way of saying "these names are *actions*,
not files." Without it, make would refuse to run `clean` if a file named
`clean` existed, and would always consider a rule "up to date" if a file with
its target name were present. Marking the four targets `.PHONY` forces make to
run their recipes unconditionally -- the right semantics for actions.

```makefile
test: $(TESTBIN)
	./$(TESTBIN)
```

`make test` builds the test binary (reusing the dependency check from 7.3 --
if nothing changed, it links nothing and just runs), then executes it. The
recipe *runs* the binary so the `make test` session ends with Unity's verdict
-- the full "33 Tests 0 Failures" summary plus a non-zero exit code on any
failure (which propagates to the shell, making `make test` scriptable).

```makefile
clean:
	rm -f $(BIN) $(TESTBIN) *.o src/*.o tests/*.o

help:
	@echo "MiniDB build targets:"
	@echo "  all   - build the $(BIN) REPL binary"
	@echo "  test  - build and run the $(TESTBIN) test suite"
	@echo "  clean - remove binaries and object files"
	@echo "  help  - show this help"
```

`clean` removes both binaries and any stray `.o` files -- including ones the
current Makefile never produces (see 7.5) -- so a directory cleaned by an
older or modified build is still fully restored. The `@` prefix on the `echo`
lines in `help` suppresses make's default "printing the command being run"
behaviour: `help` should print *only* its text, not the `@echo` line itself.
This is the one place the file is fiddly on purpose -- output hygiene.

The comment block at the top ties the whole design together (`Makefile:10`):

> The test binary is the SAME library sources plus Unity and the test suites,
> but WITHOUT src/main.c: run_tests.c supplies its own main(). One main() per
> executable -- a classic C project structure.

That sentence is the summary of the entire file: *two binaries, one shared
library, one `main()` each, and the separation expressed as a file list.*

---

## 7.5 Why no object files?

A larger project would compile each `.c` to a `.o` object file and link them
at the end. MiniDB deliberately does not. The trade-off is worth making
explicit because it is a *choice*, not an oversight.

**What object files buy you (at scale):** incremental builds. Change one file
and only that file recompiles; everything else links from its `.o`. For a
large codebase the time saving is enormous.

**What they cost (at this scale):** two Makefile rules per library (one for
the `.o`, one for the link), a pattern rule or a per-file list, and
dependency tracking so headers changes rebuild the right objects. That is
more machinery than the project's *entire* build deserves.

**What MiniDB does instead:** a single compile+link pass per executable
(`gcc ... main.c btree.c lexer.c parser.c database.c -o minidb`). With five
small sources and `-g`, the whole REPL compiles in well under a second, so an
incremental build would save nothing worth the added complexity. `clean`
still `rm -f`s `*.o src/*.o tests/*.o` -- an insurance line in case someone
compiles by hand and leaves object files behind.

This is the same judgment call the whole project makes: **choose the simple
structure that stays correct and readable at the current scale, and know
exactly what you are trading away.** The "what to study next" section at the
end of this book points at the first steps toward a proper object-file build.

---

## 7.6 What running it looks like

A fresh build, in order:

```
$ make
gcc -std=c11 -Wall -Wextra -Wpedantic -g -Isrc -Itests -o minidb \
    src/main.c src/btree.c src/lexer.c src/parser.c src/database.c
$ ./minidb
minidb> INSERT 42 100
minidb> inserted key 42 = 100
minidb> EXIT
minidb> Goodbye!
```

A second `make` with nothing changed prints nothing and exits 0 -- the targets
are up to date. Running the suite:

```
$ make test
gcc -std=c11 -Wall -Wextra -Wpedantic -g -Isrc -Itests -o minidb_test \
    tests/unity.c tests/run_tests.c tests/test_btree.c tests/test_lexer.c \
    tests/test_parser.c tests/test_database.c src/btree.c src/lexer.c \
    src/parser.c src/database.c
tests/run_tests.c:19:test_btree_insert_search:PASS
... (all 33) ...
-----------------------
33 Tests 0 Failures 0 Ignored
OK
```

Editing any single library source, then `make test`, rebuilds `minidb_test`
(the library changed) but not `minidb` unless `main.c` changed too -- the
dependency rule from 7.3 doing its quiet job.

---

## 7.7 Chapter summary

- The Makefile is **62 lines and readable as a spec**: variables, two build
  rules, four actions, no framework.
- `CC ?= gcc` makes the compiler **overridable**; `CFLAGS` carries the
  `-Wall -Wextra -Wpedantic` trio that enforces the style contract, plus
  `-std=c11`, `-g`, and the two include paths.
- `LIBSRC` lists the four shared library sources once; `TESTSRC` globs the
  test suites with `$(wildcard ...)`.
- Two rules build two binaries from the same library: `minidb` with
  `src/main.c`, `minidb_test` with `run_tests.c` -- **one `main()` per
  executable, enforced by file lists**.
- `.PHONY` makes the four actions run unconditionally; `test` builds then
  runs the suite so its verdict and exit code flow to the shell.
- There are **no object files** -- a deliberate scale trade-off, with
  `clean` still prepared to remove any that appear.

**Questions to test yourself**

1. What happens if you run `make CC=clang test`? Trace which variable wins and
   why.
2. Why must `test` appear in `.PHONY` even though it depends on a real file
   (`minidb_test`)?
3. If a new file `src/schema.c` is added, exactly how many lines of the
   Makefile must change -- and which rule would silently break if you missed
   one?
4. Why does the test rule list `tests/run_tests.c` and `tests/unity.c` but
   not `tests/unity_internals.h`?
5. What would you add to convert this to an object-file build, and what would
   you gain or lose?
6. `make test` runs `./$(TESTBIN)`. How does a failing test suite stop a CI
   script that runs `make test && deploy`?

---

# Part IV -- Back Matter

## Appendix A: Command & Token Reference

```
 Grammar:   statement := KEYWORD args?
            args     := INTEGER, INTEGER, ...   (fixed count per keyword)

 Command    Arity   Meaning                        Tree call
 ---------  -----   ----------------------------   ----------------------------
 INSERT k v    2    store v under key k            btree_insert
 SELECT k      1    fetch value under key k        btree_search
 DELETE k      1    remove key k if present        btree_delete
 RANGE lo hi   2    count keys in [lo, hi] sorted  btree_range
 PRINT         0    dump the tree as ASCII         btree_print
 EXIT          0    leave the REPL                 (none)
 HELP          0    print the command list         (none)

 Keywords are case-insensitive; keys/values are plain ints;
 a duplicate INSERT updates the existing value.

 Token types:  TOKEN_KEYWORD, TOKEN_INTEGER, TOKEN_END, TOKEN_ERROR
 Lexer errors: LEX_OK, LEX_ERROR_UNKNOWN_WORD, LEX_ERROR_BAD_NUMBER,
               LEX_ERROR_TOO_MANY_TOKENS
 Parser errors: PARSE_OK, PARSE_ERROR_EMPTY, PARSE_ERROR_UNKNOWN_COMMAND,
               PARSE_ERROR_MISSING_ARGUMENT, PARSE_ERROR_EXTRA_ARGUMENT,
               PARSE_ERROR_BAD_ARGUMENT
```

## Appendix B: Error Reference

| Where | Error                            | When                                     | Recovery hint          |
|-------|----------------------------------|------------------------------------------|------------------------|
| lexer | `LEX_ERROR_UNKNOWN_WORD`         | unknown word, or a stray symbol          | "not a command I know" |
| lexer | `LEX_ERROR_BAD_NUMBER`           | digit run would overflow `int`           | "number too large"     |
| lexer | `LEX_ERROR_TOO_MANY_TOKENS`      | stream exceeds `LEXER_MAX_TOKENS`        | "too many tokens"      |
| parser| `PARSE_ERROR_EMPTY`              | stream is only the END sentinel          | "type a command"       |
| parser| `PARSE_ERROR_UNKNOWN_COMMAND`    | first token is not a known verb          | "unknown command"      |
| parser| `PARSE_ERROR_MISSING_ARGUMENT`   | stream ends before the declared arity    | "you owe me an argument" |
| parser| `PARSE_ERROR_EXTRA_ARGUMENT`     | more tokens than the arity allows        | "you gave me too many" |
| parser| `PARSE_ERROR_BAD_ARGUMENT`       | non-integer where an integer is expected | "that isn't a number"  |

## Appendix C: The Style Contract Checklist

Before writing code into this project, confirm each line:

- [ ] Every `malloc`/`free`/pointer assignment/struct modification has a
      WHY comment.
- [ ] Element moves use plain array indexing (`array[j] = array[j-1]`), never
      pointer arithmetic.
- [ ] `size_t` is used for lengths and array subscripts (node `key_count`,
      loops over `keys[]`/`children[]`, token counts); `int` everywhere else
      (data values, depth, plain counters).
- [ ] Every node is `malloc`'d once and `free`'d once; the allocation and its
      matching free are both documented.
- [ ] Aliasing pointers (like `btree_search`'s) document their expiry; callers
      copy out immediately.
- [ ] Every index write respects its array's declared bound (spare slot only
      absorbs mid-split transients).
- [ ] The build is clean under `-Wall -Wextra -Wpedantic`.

## Appendix D: Index of Diagrams

| Diagram                                   | Chapter | Section |
|-------------------------------------------|---------|---------|
| The full pipeline (string to DB memory)   | 1       | 1.1     |
| The REPL loop                             | 1       | 1.4     |
| Dependency/include graph                  | 1       | 1.11    |
| Stack vs. heap memory model               | 2       | 2.4     |
| `BTreeNode` struct anatomy                | 2       | 2.5     |
| Leaf split (spare slot in action)         | 2       | 2.6     |
| Lexer automaton                           | 3       | 3.3     |
| Parser cursor over the token stream       | 3       | 3.9     |
| Tree build-up: 3,9,15,21,27,33            | 4       | 4.9     |
| Range scan over the leaf chain            | 4       | 4.11    |
| `db_execute` dispatch table               | 5       | 5.4     |
| Callback context flow                     | 5       | 5.5     |
| Two-binary build graph                    | 7       | 7.3     |

---

*This concludes the MiniDB mini-book. All seven chapters and the back matter
are complete. Total: roughly 26,000 words. The definitive next step is in your
hands: open `src/btree.c` with the Chapter 4 diagrams beside you, or add the
first command from Chapter 6's recipe -- the skeleton is ready to grow.*
