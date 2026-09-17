CC ?= cc
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -D_GNU_SOURCE
GLIB_CFLAGS := $(shell pkg-config --cflags glib-2.0)
GLIB_LIBS := $(shell pkg-config --libs glib-2.0)
JSON_CFLAGS := $(shell pkg-config --cflags json-glib-1.0)
JSON_LIBS := $(shell pkg-config --libs json-glib-1.0)
GTK_CFLAGS := $(shell pkg-config --cflags gtk4 gtk4-layer-shell-0)
GTK_LIBS := $(shell pkg-config --libs gtk4 gtk4-layer-shell-0)

TEST_BIN := build/test_core
CONTROL_TEST_BIN := build/test_control
APP_BIN := windowcmd
APP_SRCS := src/main.c src/control.c src/hyprland.c src/ui.c src/providers.c src/parser.c src/ranking.c src/config.c src/history.c

.PHONY: all test clean

all: $(APP_BIN)

test: $(TEST_BIN) $(CONTROL_TEST_BIN)
	./$(TEST_BIN)
	./$(CONTROL_TEST_BIN)

$(TEST_BIN): tests/test_core.c src/model.h src/parser.h src/parser.c src/ranking.h src/ranking.c src/config.h src/config.c src/history.h src/history.c src/hyprland.h src/hyprland.c src/providers.h src/providers.c src/ui.h src/ui.c | build
	$(CC) $(CFLAGS) $(GLIB_CFLAGS) $(JSON_CFLAGS) $(GTK_CFLAGS) -Isrc tests/test_core.c src/parser.c src/ranking.c src/config.c src/history.c src/hyprland.c src/providers.c src/ui.c $(GLIB_LIBS) $(JSON_LIBS) $(GTK_LIBS) -o $@

$(CONTROL_TEST_BIN): tests/test_control.c src/control.h src/control.c src/model.h src/hyprland.h src/hyprland.c src/ui.h src/ui.c src/providers.c src/parser.c src/ranking.c src/config.c src/history.c | build
	$(CC) $(CFLAGS) $(GLIB_CFLAGS) $(JSON_CFLAGS) $(GTK_CFLAGS) -Isrc tests/test_control.c src/control.c src/hyprland.c src/ui.c src/providers.c src/parser.c src/ranking.c src/config.c src/history.c $(GLIB_LIBS) $(JSON_LIBS) $(GTK_LIBS) -o $@

$(APP_BIN): $(APP_SRCS) src/model.h src/control.h src/hyprland.h src/ui.h src/providers.h src/parser.h src/ranking.h src/config.h src/history.h | build
	$(CC) $(CFLAGS) $(GLIB_CFLAGS) $(JSON_CFLAGS) $(GTK_CFLAGS) -Isrc $(APP_SRCS) $(GLIB_LIBS) $(JSON_LIBS) $(GTK_LIBS) -o $@

build:
	mkdir -p $@

clean:
	rm -rf build
