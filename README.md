# MiniDB -- A Tiny SQL-Like Language, Written In C

MiniDB is a small, in-memory key-value store that is built the way a real
database is built: a *language front end* (lexer + parser) turns your typed
commands into structured commands, and a *storage engine* (an order-3 B+ tree)
executes them against live data. Every piece is under 700 lines, every line is
commented with *why* it exists, and a 33-test Unity suite pins down the
behaviour of each layer. It is 100% educational: there is no network, no disk,
no SQL -- just a clean, readable pipeline you can grow yourself.

This README is a tutorial about the **language side** -- the lexer and the
parser -- because that is the part you can realistically extend into a real
mini-SQL. The B+ tree gets only a crash course at the end; its own detailed
diagrams live inside `src/btree.h` and `src/btree.c`.

---

## 1. The pipeline

Every line you type travels the whole pipeline. Read it top to bottom; each
stage takes the previous stage's output as its input.

```
"INSERT 42 100"                       (raw string from the REPL)
      |
      v
+----------------------+   one character at a time,
|       LEXER          |   grouped into tokens:   (KEYWORD:INSERT)(INT:42)(INT:100)(END)
+----------------------+
      |
      v
+----------------------+   a flat, bounded array of Tokens
|   Token stream       |
+----------------------+
      |
      v
+----------------------+   validate shape & arity ->
|       PARSER         |   { type=COMMAND_INSERT, key=42, value=100 }
+----------------------+
      |
      v
+----------------------+   a Command struct (the proto-AST)
|      Command         |
+----------------------+
      |
      v
+----------------------+   dispatch on command type -> B+ tree call:
|     db_execute       |   btree_insert(&tree, 42, 100)
+----------------------+
      |
      v
+----------------------+   { ok=true, message="inserted key 42 = 100" }
|     DbResult         |   -> printed back to you by the REPL
+----------------------+
```

Each stage owns exactly one job:

| Stage      | Job                                                        |
|------------|------------------------------------------------------------|
| Lexer      | Text -> tokens. Dumb character grouping, no grammar.       |
| Token      | A passive array; both the lexer's output and parser's input.|
| Parser     | Tokens -> command. Enforces the grammar and the arities.    |
| Command    | The parser's output; the database's input.                 |
| db_execute | Command -> result. The seam between language and storage.   |
| B+ tree    | The storage engine: keeps keys sorted and searchable.       |
| DbResult   | Uniform reply to the REPL: ok/found/value/matched/message.  |

---

## 2. How to build & run

You need a C compiler (`gcc` by default, `clang` works too). Everything is
standard C11 with no external dependencies.

```
make            # builds the ./minidb REPL binary
./minidb        # start the REPL
make test       # builds ./minidb_test and runs the 33-test suite
make clean      # removes both binaries and any .o files
make help       # prints the target list
```

Here is what a session looks like:

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

Type `HELP` inside the REPL, or `Ctrl-D` on an empty line, to quit.

---

## 3. The language: a complete token table

The whole language is **one verb plus a fixed number of integers**. The lexer
produces four token types:

| TokenType     | Meaning                                            |
|---------------|----------------------------------------------------|
| `TOKEN_KEYWORD` | A command verb; the token also carries a `Keyword`.|
| `TOKEN_INTEGER` | A bare decimal literal; the token carries the `int`.|
| `TOKEN_END`    | A sentinel marking the end of the stream.          |
| `TOKEN_ERROR`  | Reserved; `lex()` reports errors via its return code instead. |

And the seven verbs, with the arity the parser enforces:

| Keyword | Grammar              | Arity | Meaning                          |
|---------|----------------------|-------|----------------------------------|
| INSERT  | `INSERT k v`         | 2     | Store `v` under key `k`.         |
| SELECT  | `SELECT k`           | 1     | Fetch the value stored under `k`.|
| DELETE  | `DELETE k`           | 1     | Remove key `k` if present.       |
| RANGE   | `RANGE lo hi`        | 2     | Count keys in `[lo, hi]`, sorted.|
| PRINT   | `PRINT`              | 0     | Dump the whole B+ tree as ASCII. |
| EXIT    | `EXIT`               | 0     | Leave the REPL.                  |
| HELP    | `HELP`               | 0     | Print the command list.          |

Keywords are **case-insensitive**: `insert`, `INSERT`, `iNsErT` all work. Keys
and values are plain `int`s; anything else (a word that is not a keyword, a
stray `$` or `;`, a number that would overflow `int`) is a lex error.

---

## 4. Chapter: Writing a Lexer in C

A lexer's job is boring but crucial: it turns one long string into small,
labelled pieces. It does **no** grammar checking -- it cannot know that
`INSERT 42 100` is valid, only that `INSERT` is a word and `42` is a number.
There are two very different ways to write one.

### 4.1 Approach A: string splitting (`strtok`-style)

The tempting shortcut. Split on whitespace, then classify each piece:

```c
char *word = strtok(line, " \t\n");          /* "INSERT" */
while (word) {
    if (isdigit(word[0])) handle_number(word); /* "42"  */
    else                  handle_word(word);   /* "INSERT" */
    word = strtok(NULL, " \t\n");
}
```

It works for toy inputs, then it bites you:

- **It mutates the input.** `strtok` writes `'\0'` over every delimiter, so
  your original line is destroyed. You cannot keep the source around for error
  messages or position tracking.
- **It collapses repeated delimiters.** `"a  b"` and `"a b"` both give two
  pieces, so you lose the ability to detect "two spaces where one was allowed".
- **It cannot separate types cleanly.** `"SELECT 42"` -- is `42` a number, or
  a word that happens to be made of digits? `strtok` gives you both as the
  same kind of thing (a string); you then have to re-scan each piece and pick
  a type. Mixed tokens (`id`, `42`, `'hello'`, `(`, `)`, `>=`) get painful
  fast.
- **No position tracking.** Where did the error happen? Which character?
  `strtok`'s pieces carry no offsets, so your error messages are vague.

### 4.2 Approach B: a character-level state machine (what MiniDB uses)

Instead of splitting, walk the string **one character at a time** and let the
*current character* tell you which mini-state you are in. The states are just
the things you can find at the start of a token:

```
             +----------> skip whitespace, keep looking
             |                  |
             |                  v
   (start of token) ---- is digit ? ----> NUMBER mode: eat digit, digit, ...
             |                  |
             |                  v
             +---- is letter ? -----> WORD mode: eat letter, letter, ...
             |
             +---- anything else -----> ERROR (stray symbol)
```

MiniDB's actual states are exactly those: **skip-whitespace**, **reading a
number**, **reading a word**, **done**. The loop in `src/lexer.c` reads:

```c
LexResult lex(const char* input, Token* tokens, size_t* token_count) {
    const char* p = input;
    size_t count = 0;

    while (*p != '\0') {

        /* STATE: whitespace -- never significant, just skip it. */
        if (isspace((unsigned char)*p)) { p++; continue; }

        /* STATE: integer literal -- consume the whole run of digits. */
        if (isdigit((unsigned char)*p)) {
            int value = 0;
            while (isdigit((unsigned char)*p)) {
                int digit = *p - '0';
                if (value > (INT_MAX - digit) / 10)   /* overflow guard */
                    return LEX_ERROR_BAD_NUMBER;
                value = value * 10 + digit;
                p++;
            }
            append_token(tokens, &count, (Token){ .type = TOKEN_INTEGER,
                                                 .int_value = value });
            continue;
        }

        /* STATE: keyword -- buffer the whole word, match it case-insensitively. */
        if (isalpha((unsigned char)*p)) {
            char word[8]; size_t wlen = 0;
            while (isalpha((unsigned char)*p)) {
                if (wlen >= sizeof(word) - 1) return LEX_ERROR_UNKNOWN_WORD;
                word[wlen++] = (char)toupper((unsigned char)*p); /* normalize */
                p++;
            }
            word[wlen] = '\0';
            if (buffer_starts_with_upper(word, wlen, "INSERT")) {
                append_token(tokens, &count, (Token){ .type = TOKEN_KEYWORD,
                                                     .keyword = KEYWORD_INSERT });
                continue;
            }
            /* ...repeat for SELECT, DELETE, RANGE, PRINT, EXIT, HELP... */
            return LEX_ERROR_UNKNOWN_WORD;   /* a word nobody defined */
        }

        return LEX_ERROR_UNKNOWN_WORD;       /* '$', ';', '"' ... */
    }

    /* STATE: done -- always append the END sentinel so the parser never
     * reads past the last real token. */
    append_token(tokens, &count, (Token){ .type = TOKEN_END,
                                          .keyword = KEYWORD_UNKNOWN });
    *token_count = count;
    return LEX_OK;
}
```

Why the state machine is the better foundation for a real language:

- **Input stays untouched** -- `lex()` only reads, never writes to `input`.
- **Types emerge naturally.** A leading digit enters number mode, a leading
  letter enters word mode; the very first character *is* the discriminator.
- **You see every byte.** A stray `$` is caught precisely because no state
  claims it. The error is exact and you know *where* you were.
- **It generalizes.** Add a `"` state to read string literals, an operator
  state for `=`, `>`, `>=`, and MiniDB's lexer becomes a real tokenizer. The
  shape never changes -- you just add states.

Walking through the real behaviour:

- **Whitespace skip**: `isspace()` covers space, tab, `\n`, `\r`; leading,
  trailing and doubled whitespace are all invisible, so `"  INSERT \t 42 "`
  lexes exactly like `"INSERT 42"`.
- **Digit runs with an overflow guard**: `value = value*10 + digit` would
  overflow silently on `2147483648`, so the lexer checks
  `value > (INT_MAX - digit) / 10` *before* every multiply and bails with
  `LEX_ERROR_BAD_NUMBER`.
- **Word runs, matched case-insensitively**: each letter is upper-cased with
  `toupper()` into a small stack buffer, then compared with `memcmp` against
  the canonical uppercase spellings in `KEYWORD_TABLE`. Both sides are
  uppercase, so the comparison is effectively case-insensitive.
- **The `TOKEN_END` sentinel**: every valid stream ends with one. The parser
  can now stop on an explicit marker instead of fiddling with counts.
- **Allocation-free by design**: the caller owns a fixed `Token[16]` array
  (`LEXER_MAX_TOKENS`). `lex()` fills it and reports `LEX_ERROR_TOO_MANY_TOKENS`
  when it is full. For an interactive line this is plenty, and it keeps the
  memory reasoning simple -- see "What to study next" for the `realloc` upgrade.

---

## 5. Chapter: Writing a Parser in C

Where the lexer was a dumb *scanner*, the parser is a smart *validator*. It
takes the flat token stream and answers two questions: **is this a legal
command, and what does it mean?** The usual technique is **recursive descent**:
a parser that walks the token stream left to right, deciding what to do next by
looking at the *current* token, and recursively descending into sub-rules for
nested syntax. The mental model to keep in your head is: **consume tokens,
produce a structure.** Every parser, from this 200-liner to a full PostgreSQL
front end, is that sentence with different amounts of recursion.

MiniDB's grammar is so flat that the parser barely needs recursion at all. The
whole language is:

```
statement := KEYWORD args?
args      := INTEGER, INTEGER, ...   (a fixed count per keyword)
```

So `parse_command()` in `src/parser.c` works in three moves:

1. **Dispatch on the first token.** Token `[0]` must be a `TOKEN_KEYWORD`; a
   `switch` on `tokens[0].keyword` picks the grammar rule. A bare integer
   first is `PARSE_ERROR_UNKNOWN_COMMAND` -- an integer is not a verb.
2. **Exact-arity checking.** Each `case` calls `parse_fixed_arguments()` with
   the number of integers that command expects. That one helper consumes
   exactly `argc` integers and then *requires the very next token to be
   `TOKEN_END`* -- so it catches all three argument failures in one place:
   - stream ends before the args are done -> `PARSE_ERROR_MISSING_ARGUMENT`
   - a keyword where an integer is expected -> `PARSE_ERROR_BAD_ARGUMENT`
   - anything left over after the declared count -> `PARSE_ERROR_EXTRA_ARGUMENT`
3. **Build the `Command`.** On success, the parsed integers are copied into the
   right fields and `command->type` is set.

The dispatch, in skeletal form:

```c
switch (tokens[0].keyword) {
case KEYWORD_INSERT:
    r = parse_fixed_arguments(tokens, token_count, 1, 2, args);  /* exactly 2 */
    if (r != PARSE_OK) return r;
    command->type = COMMAND_INSERT;
    command->key = args[0];
    command->value = args[1];
    return PARSE_OK;
case KEYWORD_SELECT:
    r = parse_fixed_arguments(tokens, token_count, 1, 1, args);  /* exactly 1 */
    if (r != PARSE_OK) return r;
    command->type = COMMAND_SELECT;
    command->key = args[0];
    return PARSE_OK;
/* ...RANGE (2), DELETE (1), PRINT/EXIT/HELP (0)... */
}
```

### 5.1 `Command` is a proto-AST

A real SQL parser builds a *syntax tree*. MiniDB collapses that to one flat
node, because every statement is one verb plus at most two integers -- there is
nothing to nest. `Command` is the **proto-AST**: the seed of a tree. The moment
you add a `WHERE` clause, the tree-ness returns:

```c
/* What MiniDB has today: one flat node. */
typedef struct {
    CommandType type;
    int key;        /* INSERT/SELECT/DELETE/RANGE: the primary key */
    int value;      /* INSERT only                                 */
    int key_end;    /* RANGE only                                  */
} Command;

/* What "SELECT price FROM items WHERE id = 42" would grow into. */
typedef struct Expr Expr;
struct Expr {
    ExprKind kind;        /* EXPR_CONST, EXPR_COLUMN, EXPR_BINARY   */
    int       int_value;  /* EXPR_CONST: 42                         */
    const char* column;   /* EXPR_COLUMN: "price"                   */
    Expr*     left;       /* EXPR_BINARY: <left>  <right>           */
    Expr*     right;
};

typedef struct SelectStmt {
    const char** columns; /* SELECT list: "price", "name", ...      */
    const char*  from;    /* FROM items                             */
    Expr*        where;   /* WHERE id = 42                          */
} SelectStmt;
```

Notice the structure: `SELECT k` stays a node, but the "args" stop being
flat integers and start being *pointers to sub-structures* (`Expr* where`).
That is exactly where recursion returns to the parser -- a function
`parse_expr()` would call itself for the left and right sides of `=`.

### 5.2 Error handling: why MISSING vs EXTRA vs BAD

Three different ways to get a command wrong produce three different messages:

```
minidb> SELECT            Parse error: missing argument. Type HELP for commands.
minidb> SELECT 1 2        Parse error: extra argument.   Type HELP for commands.
minidb> INSERT x 5        Lex error: unknown word or stray symbol
```

Distinguishing them matters for a REPL because the **recovery hint is
different**: "you owe me an argument" suggests the user forgot a number, "you
gave me too many" suggests a typo'd command, "that isn't a number" suggests a
name where an integer belongs. A parser that reports "syntax error" for all
three is a parser that cannot teach its user anything. MiniDB keeps the error
types in an enum (`ParseResult`) and prints their friendly strings via
`parse_result_string()` -- this is exactly the granularity a real front end
wants.

---

## 6. How to add a new command (worked example: `COUNT`)

The language has exactly one way to grow, and every new command touches the
same five places. We'll add `COUNT`, which reports how many keys currently
exist (the tree already exposes `btree_size(&tree)` for this).

### 6.1 Lexer: recognize the word

Add the keyword to the enum in `src/lexer.h`. It must go **before**
`KEYWORD_UNKNOWN`, because `KEYWORD_UNKNOWN` doubles as the "how many known
keywords" sentinel that sizes `KEYWORD_TABLE`:

```c
typedef enum {
    KEYWORD_INSERT, KEYWORD_SELECT, KEYWORD_DELETE, KEYWORD_RANGE,
    KEYWORD_PRINT, KEYWORD_EXIT, KEYWORD_HELP, KEYWORD_COUNT, KEYWORD_UNKNOWN
} Keyword;
```

Register the spelling in `KEYWORD_TABLE` in `src/lexer.c` (the enum index is
the table index), and add the matching `if` in `lex()` alongside the others:

```c
static const char* const KEYWORD_TABLE[KEYWORD_UNKNOWN] = {
    "INSERT", "SELECT", "DELETE", "RANGE",
    "PRINT",  "EXIT",   "HELP",   "COUNT"     /* <-- new */
};
```

```c
if (buffer_starts_with_upper(word, wlen, "COUNT")) {
    Token t;
    t.type = TOKEN_KEYWORD;
    t.keyword = KEYWORD_COUNT;
    if (append_token(tokens, &count, t) != LEX_OK) {
        return LEX_ERROR_TOO_MANY_TOKENS;
    }
    continue;
}
```

(`COUNT` is 5 letters; it fits the `char word[8]` buffer, sized for the
longest keyword, `DELETE`, plus NUL.)

### 6.2 Parser: accept the command

Add a `CommandType` in `src/parser.h` (before `COMMAND_NONE`), and a case in
`parse_command()` with arity **0** -- the same shape as `PRINT`/`EXIT`/`HELP`:

```c
typedef enum {
    COMMAND_INSERT, COMMAND_SELECT, COMMAND_DELETE, COMMAND_RANGE,
    COMMAND_PRINT, COMMAND_EXIT, COMMAND_HELP, COMMAND_COUNT, COMMAND_NONE
} CommandType;
```

```c
case KEYWORD_COUNT:
    r = parse_fixed_arguments(tokens, token_count, 1, 0, args); /* no args */
    if (r != PARSE_OK) return r;
    command->type = COMMAND_COUNT;
    return PARSE_OK;
```

Don't forget the label helper so `command_type_string()` stays complete:

```c
case COMMAND_COUNT: return "COUNT";
```

### 6.3 Database: execute it

Add a case in `db_execute()` in `src/database.c`. The tree already tracks its
own size, so this is the whole dispatch:

```c
case COMMAND_COUNT:
    result->ok = true;
    snprintf(result->message, sizeof(result->message),
             "%zu keys", btree_size(&db->tree));
    break;
```

### 6.4 Test: pin it down

Add assertions to the matching suites, then wire them into the runner.

In `tests/test_lexer.c` (append to the existing keyword case table, or add a
dedicated test):

```c
TEST_ASSERT_EQUAL_INT(LEX_OK, lex_to("COUNT", tokens, &count));
TEST_ASSERT_EQUAL_INT(TOKEN_KEYWORD, tokens[0].type);
TEST_ASSERT_EQUAL_INT(KEYWORD_COUNT, tokens[0].keyword);
TEST_ASSERT_EQUAL_INT(TOKEN_END, tokens[1].type);
```

In `tests/test_parser.c`, inside `test_parse_all_valid_commands`:

```c
TEST_ASSERT_EQUAL_INT(PARSE_OK, parse("COUNT", &cmd));
TEST_ASSERT_EQUAL_INT(COMMAND_COUNT, cmd.type);
```

In `tests/test_database.c`, a new end-to-end test (using the same
`exec()` helper as the other db tests):

```c
void test_db_count_keys(void) {
    Database db;
    db_init(&db);
    DbResult r;

    exec(&db, "COUNT", &r);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_STRING("0 keys", r.message);

    exec(&db, "INSERT 1 10", &r);
    exec(&db, "INSERT 2 20", &r);
    exec(&db, "COUNT", &r);
    TEST_ASSERT_EQUAL_STRING("2 keys", r.message);

    db_close(&db);
}
```

Finally declare it in `tests/test_suites.h` and register it in
`tests/run_tests.c` with `RUN_TEST(test_db_count_keys);`. Run `make test`;
your suite should now report **34 Tests 0 Failures**.

That is the entire recipe -- **enum, table, lex case, parse case, execute
case, test, run**. Every future command (`CLEAR`, `MIN`, `SUM`, ...) follows
the identical six steps.

---

## 7. B+ tree crash course (very brief)

The storage engine is an **order-3 B+ tree**: a balanced multiway search tree
where every key lives in a leaf node alongside its value, leaves are chained
into a sorted linked list, and internal nodes hold only "separator" keys that
route searches downward.

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
          | 100 | 42  |  |  7  |  9  |  |  3  |  1  |   <- LEAF: key|value
          +-----+-----+  +-----+-----+  +-----+-----+
          |                 |                 |
          |  next ----------+-----------------+  (leaf->next chain)
```

Order 3 means every node holds at most 2 keys; overfull nodes **split** and
promote a separator upward (the only way the tree grows taller), and underfull
nodes **borrow** or **merge** with a sibling. The full SPLIT/PROMOTE and
BORROW/MERGE diagrams, plus the memory model, are the top of `src/btree.h` and
`src/btree.c`. Read those before touching `database.c`.

---

## 8. Project layout

```
MiniDB/
|-- Makefile              builds both binaries (`make`, `make test`)
|-- README.md             this file
|-- src/
|   |-- btree.h           B+ tree concept, node structs, public API
|   |-- btree.c           tree implementation: insert/delete/search/range/print
|   |-- lexer.h           Token, TokenType, Keyword, lex() contract
|   |-- lexer.c           the character-level state machine (this README ch. 4)
|   |-- parser.h          Command (proto-AST), grammar, parse_command()
|   |-- parser.c          token-walking parser with exact-arity checking (ch. 5)
|   |-- database.h        Database, DbResult, db_execute() seam
|   |-- database.c        query engine: dispatches a Command onto the tree
|   `-- main.c            the REPL that drives the whole pipeline (ch. 1 diagram)
`-- tests/
    |-- unity.h/.c/_internals.h   the Unity test framework (vendored, don't edit)
    |-- run_tests.c       test main: registers every suite (has main())
    |-- test_suites.h     declarations of every test function
    |-- test_btree.c      B+ tree unit tests
    |-- test_lexer.c      lexer tests (token shapes, errors, case-insensitivity)
    |-- test_parser.c     parser tests (valid commands + all error paths)
    `-- test_database.c   end-to-end tests through lexer -> parser -> tree
```

The dependency direction is worth internalizing: `main.c` uses `database.h`,
which pulls in `parser.h`, which pulls in `lexer.h`; `database.c` additionally
uses `btree.h`. The lexer knows nothing about the tree; the tree knows nothing
about the language. Each layer depends only on the one below it -- the same
layering a real database uses.

---

## 9. What to study next

You now have a complete, tested language front end. Good next steps, roughly in
difficulty order:

- **Add string values.** Change `value` from `int` to a `char*`, store strings
  in the tree, and grow `INSERT` into `INSERT k "hello"`. You will need...
- **Quoted string tokens.** Add a `"` state to the lexer that scans until the
  closing quote (and handles `\"` escapes). This is the classic second lexer
  state and it exercises everything you learned in chapter 4.
- **`SELECT k, v` expressions.** Let SELECT take a *list* of columns: change
  `parse_fixed_arguments()` from a fixed count into a loop that stops on a
  comma token. This is the moment the parser stops being fixed-arity.
- **`WHERE` clauses.** Turn `Command` into a real tree (`Expr* where`) and
  teach the parser a `parse_expr()` that recurses. See the sketch in 5.1.
- **Multi-statement input.** Let the lexer split on `;` so the REPL can accept
  `INSERT 1 10; SELECT 1;` on one line.
- **Prepared statements.** `SELECT ?` -- parse once into a `Command`, then
  fill the `?` placeholders at execute time, avoiding re-lexing.
- **Dynamic token buffer.** Replace the fixed `Token[16]` with `realloc` when
  full, and drop `LEXER_MAX_TOKENS`. The overflow path in the lexer is the
  only place that changes -- good practice for ownership/`free()` reasoning.
- **A real mini-SQL.** Combine the above: `SELECT a, b FROM t WHERE a > 10`
  needs column identifiers, comparison operators in the lexer, a recursive
  parser, and a `FROM` name. You will have re-built the front end of SQLite's
  smaller cousin -- with this codebase as the skeleton.

The whole point of MiniDB is that you can do all of this without touching the
storage engine. The B+ tree does not care whether a command was typed as
`SELECT 42` or `SELECT price FROM items WHERE id = 42`; by the time it sees
anything, the parser has already turned it into a structure. Grow the language,
and the database stays put.
