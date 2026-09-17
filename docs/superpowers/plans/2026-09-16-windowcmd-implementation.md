# windowcmd Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a persistent native GTK4/Wayland command palette that captures and safely targets the focused Hyprland window.

**Architecture:** A single C process owns a per-user Unix socket and a GTK event loop. A Hyprland adapter captures/validates window snapshots and dispatches structured actions; providers produce immutable result records; ranking combines fuzzy/context/history scores; the layer-shell UI consumes the results and launches only explicitly selected shell commands through the configured shell.

**Tech Stack:** C17, GLib/GIO, GTK4, `gtk4-layer-shell-0`, JSON-GLib, `hyprctl`, POSIX Unix sockets/process APIs, GLib Test.

**Spec:** `docs/superpowers/specs/2026-09-16-windowcmd-design.md`

## Global Constraints

- Use a native GTK4 + `gtk4-layer-shell` Wayland surface; do not add X11-only behavior.
- Capture the focused window before showing the layer surface.
- Address is the primary target identifier; validate it before every window action.
- Window actions use structured `hyprctl` argv; only explicit/configured shell commands use a shell.
- The target-closing condition closes the palette automatically.
- Persist bounded JSON history under XDG state and configuration under XDG config.
- Cache executable names and refresh only when the effective `PATH` changes or its refresh interval expires.
- Do not hardcode user-specific paths.
- Every non-trivial behavior gets a focused GLib Test test written and observed failing before production code is added.

---

### Task 1: Project skeleton, shared models, parser, and ranking

**Files:**
- Create: `Makefile`
- Create: `src/model.h`
- Create: `src/parser.h`
- Create: `src/parser.c`
- Create: `src/ranking.h`
- Create: `src/ranking.c`
- Create: `tests/test_core.c`

**Interfaces:**
- `WindowSnapshot` contains `address`, `class_name`, `title`, `pid`, `workspace`, `workspace_name`, `monitor`, `monitor_name`, `floating`, `fullscreen`, `pinned`, `pseudo`, and `split_locked`.
- `CommandKind` is `COMMAND_WINDOW`, `COMMAND_SHELL`, or `COMMAND_CUSTOM`.
- `HistoryEntry` contains `command`, `timestamp`, `uses`, `application_class`, and `CommandKind`; it is declared in `model.h` so ranking can consume it before persistence is implemented.
- `ParsedAction` contains `WindowActionKind kind`, `workspace`, `width`, `height`, `opacity`, `monitor`, and `direction`; fields not used by the selected action stay empty or zero.
- `CommandResult` contains owned strings `id`, `title`, `description`, `keywords`, `icon`, `provider`, `command_text`, an integer `base_score`, and a `CommandKind`.
- `parse_window_command(const char *text, const WindowSnapshot *target, ParsedAction *out, GError **error)` returns false for invalid/incomplete arguments and never builds a shell string.
- `fuzzy_score(const char *query, const char *haystack)` returns `-1` for no match and a non-negative score for a match, with exact and prefix matches scoring above subsequence matches.
- `rank_result(const CommandResult *, const char *query, const WindowSnapshot *, const HistoryEntry *history)` returns a double score.

- [ ] **Step 1: Write failing core tests.** Add tests asserting `workspace 4`, `move workspace 4`, `resize 1200 800`, and `opacity 0.9` parse into typed actions; reject missing/invalid arguments; score `float` above `fit` for query `flo`; and score an exact prefix above a scattered fuzzy match.
- [ ] **Step 2: Run the tests to verify the missing implementation fails.**

Run: `make test`

Expected: compilation fails because `src/parser.c` and `src/ranking.c` do not yet provide the declared functions.

- [ ] **Step 3: Add the minimal model and parser.** Use enums and fixed-size numeric fields for action arguments. Recognize the action names and aliases from the spec, parse with `g_strsplit_set`, validate workspace as a non-empty identifier, dimensions as positive integers, opacity in `[0, 1]`, and monitor as a non-empty identifier.
- [ ] **Step 4: Add the minimal fuzzy scorer and contextual ranker.** Normalize ASCII case with GLib string helpers, reward exact/prefix/ordered subsequence matches, then add configurable-free default boosts for a history match and target-state relevance.
- [ ] **Step 5: Run the focused and full tests.**

Run: `make test`

Expected: all core tests pass with exit status 0.

- [ ] **Step 6: Commit the core layer.**

Run: `git add Makefile src/model.h src/parser.h src/parser.c src/ranking.h src/ranking.c tests/test_core.c && git commit -m "feat: add command parsing and fuzzy ranking"`

### Task 2: XDG configuration and bounded history

**Files:**
- Create: `src/config.h`
- Create: `src/config.c`
- Create: `src/history.h`
- Create: `src/history.c`
- Modify: `tests/test_core.c`
- Modify: `Makefile`
- Create: `config/windowcmd.example.json`

**Interfaces:**
- `Config` contains `width`, `max_results`, `position`, `font`, `border_radius`, `transparency`, `history_size`, `shell`, `include_path_commands`, `path_refresh_seconds`, string arrays `disabled_builtins`, aliases/custom command arrays, and ranking weights.
- `config_load(const char *path, Config *out, GError **error)` loads defaults then overrides valid fields.
- `config_default_path(void)` and `history_default_path(void)` return newly allocated XDG paths.
- `HistoryStore` owns a `GPtrArray` of `HistoryEntry` values and the configured size.
- `history_load`, `history_record`, and `history_save` maintain at most `history_size` records and replace the file through a temporary sibling file plus `g_rename`.

- [ ] **Step 1: Write failing persistence tests.** Use `g_test_build_filename`/temporary directories and assert missing config gets defaults, valid JSON overrides width/shell, malformed custom entries are skipped, recording the same command increments uses, and saving trims more than the configured maximum.
- [ ] **Step 2: Run `make test` and observe the expected missing-symbol failure.**
- [ ] **Step 3: Implement defaults and JSON-GLib loading.** Use `$XDG_CONFIG_HOME` and `$XDG_STATE_HOME`, falling back to `$HOME/.config` and `$HOME/.local/state`; create parent directories only when saving or initializing.
- [ ] **Step 4: Implement bounded history and atomic save.** Match records by command, provider kind, and application class; update timestamp/use count; sort newest/useful records before trimming; write JSON with restrictive user-only file permissions where supported.
- [ ] **Step 5: Add the documented example JSON and `windowcmd init` target contract.** Include the requested visual, ranking, shell, alias, disabled-command, and custom-command fields without user-specific paths.
- [ ] **Step 6: Run all tests and commit.**

Run: `make test && git add src/config.h src/config.c src/history.h src/history.c tests/test_core.c Makefile config/windowcmd.example.json && git commit -m "feat: add XDG config and command history"`

### Task 3: Hyprland JSON adapter and safe dispatch

**Files:**
- Create: `src/hyprland.h`
- Create: `src/hyprland.c`
- Create: `tests/fixtures/activewindow.json`
- Create: `tests/fixtures/clients.json`
- Modify: `tests/test_core.c`
- Modify: `Makefile`

**Interfaces:**
- `typedef struct Hyprland Hyprland;` is the opaque adapter handle declared by `hyprland.h` and owned by the application.
- `hyprland_capture_active(WindowSnapshot *out, GError **error)` runs `hyprctl activewindow -j` without a shell.
- `hyprland_target_exists(const char *address, gboolean *exists, GError **error)` checks `hyprctl clients -j`.
- `hyprland_dispatch_window(const WindowSnapshot *, const ParsedAction *, GError **error)` emits separate argv values using the address selector.
- `hyprland_notify(const char *message, GError **error)` emits a bounded user-facing notification without interpolating untrusted data into a shell.
- `hyprland_refresh_capabilities(void)` caches the installed dispatcher/property surface for the process.

- [ ] **Step 1: Write fixture tests first.** Assert the fixture snapshot reads the exact address, class, title, PID, workspace, monitor, and state booleans; assert target validation distinguishes the address from another client; assert a dispatch argv builder keeps a title containing quotes/semicolons out of the argv payload.
- [ ] **Step 2: Run `make test` and observe failure before adapter implementation.**
- [ ] **Step 3: Implement a `g_spawn_sync` wrapper.** Set `HYPRLAND_INSTANCE_SIGNATURE` naturally from the environment, capture stdout/stderr, and convert non-zero exit status or invalid JSON into `GError`.
- [ ] **Step 4: Parse activewindow/clients JSON with JSON-GLib.** Accept missing optional state fields as false/unknown and treat a missing address as no target.
- [ ] **Step 5: Implement dispatcher mappings.** Use address-qualified forms for focus, close, kill, floating, fullscreen, pin, workspace, movewindow, resizeactive, pseudo, split, opacity, and monitor actions. Probe command help/capability once and omit unsupported actions.
- [ ] **Step 6: Run all tests and commit.**

Run: `make test && git add src/hyprland.h src/hyprland.c tests/fixtures tests/test_core.c Makefile && git commit -m "feat: target windows through Hyprland IPC"`

### Task 4: Providers, PATH cache, aliases, and target templates

**Files:**
- Create: `src/providers.h`
- Create: `src/providers.c`
- Modify: `tests/test_core.c`
- Modify: `Makefile`

**Interfaces:**
- `provider_collect(const Config *, const WindowSnapshot *, const HistoryStore *, GPtrArray **out, GError **error)` returns owned `CommandResult` values.
- `path_cache_refresh_if_needed(PathCache *, const char *path, guint refresh_seconds, GError **error)` refreshes only on changed PATH or expiry.
- `expand_target_template(const char *template, const WindowSnapshot *, char **out, GError **error)` expands documented target variables while preserving shell safety.
- `command_result_execute(const CommandResult *, const WindowSnapshot *, const Config *, GError **error)` routes window/custom/shell commands through distinct execution paths.

- [ ] **Step 1: Write failing provider tests.** Assert contextual labels change from `FLOAT` to `TILE` based on state, disabled built-ins are absent, PATH names are deduplicated, a custom template receives target values, and a normal shell result is classified as `COMMAND_SHELL` rather than a window action.
- [ ] **Step 2: Run `make test` and observe failure.**
- [ ] **Step 3: Implement built-in result generation.** Create descriptions from class/title with GTK/GLib markup escaping at display time; include action keywords and current-state aliases.
- [ ] **Step 4: Implement PATH caching and custom/alias expansion.** Use `g_get_environ`, `G_FILE_TEST_IS_EXECUTABLE`, and a hash table keyed by executable basename; preserve the executable/argument payload separately from its display label.
- [ ] **Step 5: Implement execution routing and environment export.** Window results call the Hyprland adapter; shell/custom results use `shell -c command` with `HYPR_TARGET_*` variables and no title/class/address concatenation into Hyprland commands.
- [ ] **Step 6: Run all tests and commit.**

Run: `make test && git add src/providers.h src/providers.c tests/test_core.c Makefile && git commit -m "feat: add contextual command providers"`

### Task 5: GTK4 layer-shell palette UI

**Files:**
- Create: `src/ui.h`
- Create: `src/ui.c`
- Create: `data/windowcmd.css`
- Modify: `src/model.h`
- Modify: `Makefile`

**Interfaces:**
- `Ui *ui_new(const Config *, Hyprland *hyprland, HistoryStore *, GError **error)` creates the hidden layer surface.
- `void ui_show(Ui *, const WindowSnapshot *)` sets the target monitor, rebuilds results, and focuses the entry only after target capture.
- `void ui_hide(Ui *)` hides the surface and clears transient target state.
- `gboolean ui_is_visible(const Ui *)` reports current visibility.
- `void ui_refresh_target(Ui *)` validates the target and calls `ui_hide` when it disappears.

- [ ] **Step 1: Write a headless UI-model test.** Assert result filtering is instant and stable for empty/query input, selection wraps at the list boundaries, Enter returns the selected result, and Escape requests hide.
- [ ] **Step 2: Run `make test` and observe the missing UI-model failure.**
- [ ] **Step 3: Implement the UI model and GTK widgets.** Use `GtkWindow`, a centered panel box, `GtkEntry`, `GtkListBox`, and a `GtkScrolledWindow`; render title/description/provider rows and apply a selected CSS class.
- [ ] **Step 4: Configure layer-shell.** Set overlay layer, keyboard interactivity, no exclusive zone, target monitor, configured margins/anchors, and hide-on-close behavior through GTK4 layer-shell APIs.
- [ ] **Step 5: Wire key handling and live refresh.** Map Up/Down/Ctrl+n/Ctrl+p/Enter/Escape; on text change filter/rank/repopulate; install a short periodic target check only while visible so a closed target closes promptly without daemon-wide polling.
- [ ] **Step 6: Run all tests and commit.**

Run: `make test && git add src/ui.h src/ui.c data/windowcmd.css src/model.h Makefile && git commit -m "feat: add Wayland command palette UI"`

### Task 6: Background process, socket commands, and CLI

**Files:**
- Create: `src/main.c`
- Create: `src/control.h`
- Create: `src/control.c`
- Create: `tests/test_control.c`
- Modify: `Makefile`

**Interfaces:**
- `ControlServer *control_server_new(AppState *, GError **error)` binds `$XDG_RUNTIME_DIR/windowcmd.sock` with mode `0600`.
- `gboolean control_handle_line(AppState *, const char *line, char **response)` handles `toggle`, `show`, `hide`, and `status`.
- `int app_run(int argc, char **argv)` implements `start`, `toggle`, `show`, `hide`, `status`, `init`, and `--help`.

- [ ] **Step 1: Write failing control tests.** Assert `status` reports hidden/visible state, `toggle` alternates it, `show` is idempotent, `hide` is idempotent, unknown commands return an error, and a second server cannot bind the same socket.
- [ ] **Step 2: Run `make test` and observe failure.**
- [ ] **Step 3: Implement the newline socket server.** Use `GSocketService` and per-connection line handling; reject clients whose peer credentials are not the current UID where supported, and remove the socket on shutdown.
- [ ] **Step 4: Implement startup and CLI routing.** Daemon mode initializes config/history/providers/UI before entering GTK main; client modes connect, send one command, print one response, and return a non-zero status for daemon errors.
- [ ] **Step 5: Wire the show order.** `show` first calls `hyprland_capture_active`, then calls `ui_show`; `toggle` hides if visible and otherwise uses that same capture-first path.
- [ ] **Step 6: Run unit tests and command smoke checks, then commit.**

Run: `make test && make windowcmd && ./windowcmd --help && ./windowcmd status; git add src/main.c src/control.h src/control.c tests/test_control.c Makefile && git commit -m "feat: add persistent daemon and CLI control"`

### Task 7: Packaging, documentation, and final integration verification

**Files:**
- Create: `README.md`
- Create: `systemd/windowcmd.service`
- Create: `scripts/install.sh`
- Modify: `Makefile`
- Modify: `config/windowcmd.example.json`

**Interfaces:**
- `make`, `make test`, `make install PREFIX=...`, and `make uninstall PREFIX=...` are the supported build/install entry points.
- `systemd/windowcmd.service` starts the user daemon after graphical-session preparation without embedding a user-specific path.
- `scripts/install.sh` installs the binary, example config, and service asset, then prints the Hyprland bind.

- [ ] **Step 1: Write documentation smoke assertions.** Add a shell test that checks README contains build, `windowcmd toggle`, `bind = SUPER, SPACE, exec, windowcmd toggle`, XDG config/state paths, and the five `HYPR_TARGET_*` variables.
- [ ] **Step 2: Run the documentation test and observe failure before docs are complete.**
- [ ] **Step 3: Write the README and service/install assets.** Document dependencies (`gtk4`, `gtk4-layer-shell-0`, `json-glib-1.0`), build commands, autostart, config schema, command syntax, target safety, and troubleshooting when Hyprland is unavailable.
- [ ] **Step 4: Add install/uninstall targets using `DESTDIR`, `PREFIX`, and `XDG_CONFIG_HOME`-safe paths.** Do not write into a user home directory during build.
- [ ] **Step 5: Run the full verification suite.**

Run: `make clean && make test && make && ./windowcmd --help && ./windowcmd status; git diff --check; git status --short`

Expected: build and tests exit 0; `status` returns a clear not-running/Hyprland-unavailable response rather than crashing; `git diff --check` emits no whitespace errors.

- [ ] **Step 6: Commit the finished delivery.**

Run: `git add README.md systemd scripts Makefile config && git commit -m "docs: package and document windowcmd"`
