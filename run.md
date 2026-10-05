# Running MiniDB

## Build

```
make
```

This produces the `minidb` binary.

## Run the program

```
./minidb
```

The REPL supports `INSERT <key> <value>`, `SELECT <key>`, `DELETE <key>`, `PRINT`, and `EXIT`:

```
minidb> INSERT 42 100
minidb> inserted key 42 = 100
minidb> SELECT 42
minidb> key 42 = 100
minidb> EXIT
minidb> Goodbye!
```

It also accepts a program on stdin:

```
printf 'INSERT 42 100\nSELECT 42\nEXIT\n' | ./minidb
```

## Run the tests

```
make test
```

This builds the `minidb_test` binary and runs the 33-test Unity suite:

```
-----------------------
33 Tests 0 Failures 0 Ignored
OK
```

## Other targets

```
make clean   # remove both binaries and any object files
make help    # print the available targets
make CC=clang test   # override the compiler
```
