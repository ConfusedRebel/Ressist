CC ?= cc
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -D_GNU_SOURCE
GLIB_CFLAGS := $(shell pkg-config --cflags glib-2.0)
GLIB_LIBS := $(shell pkg-config --libs glib-2.0)

TEST_BIN := build/test_core

.PHONY: all test clean

all: test

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): tests/test_core.c src/model.h src/parser.h src/ranking.h src/parser.c src/ranking.c | build
	$(CC) $(CFLAGS) $(GLIB_CFLAGS) -Isrc tests/test_core.c src/parser.c src/ranking.c $(GLIB_LIBS) -o $@

build:
	mkdir -p $@

clean:
	rm -rf build
