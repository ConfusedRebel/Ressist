CC ?= cc
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -D_GNU_SOURCE
GLIB_CFLAGS := $(shell pkg-config --cflags glib-2.0)
GLIB_LIBS := $(shell pkg-config --libs glib-2.0)
JSON_CFLAGS := $(shell pkg-config --cflags json-glib-1.0)
JSON_LIBS := $(shell pkg-config --libs json-glib-1.0)

TEST_BIN := build/test_core

.PHONY: all test clean

all: test

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): tests/test_core.c src/model.h src/parser.h src/parser.c src/ranking.h src/ranking.c src/config.h src/config.c src/history.h src/history.c src/hyprland.h src/hyprland.c | build
	$(CC) $(CFLAGS) $(GLIB_CFLAGS) $(JSON_CFLAGS) -Isrc tests/test_core.c src/parser.c src/ranking.c src/config.c src/history.c src/hyprland.c $(GLIB_LIBS) $(JSON_LIBS) -o $@

build:
	mkdir -p $@

clean:
	rm -rf build
