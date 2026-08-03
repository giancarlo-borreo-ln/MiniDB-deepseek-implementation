# ==============================================================================
# MiniDB Makefile
#
# Targets:
#   all   - build the minidb REPL binary
#   test  - build the minidb_test binary and run the test suite
#   clean - remove both binaries and any .o files
#   help  - print the available targets
#
# Layout note:
#   - The REPL is src/main.c linked against the four library sources.
#   - The test binary is the SAME library sources plus Unity and the test
#     suites, but WITHOUT src/main.c: run_tests.c supplies its own main().
#     One main() per executable -- a classic C project structure.
# ==============================================================================

# Compiler (override with `make CC=clang` if you like) and shared flags.
CC     ?= gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -g -Isrc -Itests

# The four library sources shared by BOTH executables.
LIBSRC  = src/btree.c src/lexer.c src/parser.c src/database.c

# Every test suite source; the tests provide their own main via run_tests.c.
TESTSRC = $(wildcard tests/test_*.c)

# The two binaries this project knows how to produce.
BIN     = minidb
TESTBIN = minidb_test

# "help" and "clean" do not produce files, so they are always considered out
# of date and re-run every time.
.PHONY: all test clean help

# Default target: build the interactive REPL.
all: $(BIN)

# Link the REPL: src/main.c plus the four library sources (one main() here).
$(BIN): src/main.c $(LIBSRC)
	$(CC) $(CFLAGS) -o $@ src/main.c $(LIBSRC)

# Link the test runner: Unity, the test main, every suite, and the libraries
# (everything EXCEPT src/main.c -- run_tests.c has the main() for the tests).
$(TESTBIN): tests/unity.c tests/run_tests.c $(TESTSRC) $(LIBSRC)
	$(CC) $(CFLAGS) -o $@ tests/unity.c tests/run_tests.c $(TESTSRC) $(LIBSRC)

# Build the test binary (if needed), then run it so `make test` reports the
# full "N Tests M Failures" summary.
test: $(TESTBIN)
	./$(TESTBIN)

# Remove both binaries and any object files that may have been produced.
clean:
	rm -f $(BIN) $(TESTBIN) *.o src/*.o tests/*.o

# Print the available targets and what each one does.
help:
	@echo "MiniDB build targets:"
	@echo "  all   - build the $(BIN) REPL binary"
	@echo "  test  - build and run the $(TESTBIN) test suite"
	@echo "  clean - remove binaries and object files"
	@echo "  help  - show this help"
