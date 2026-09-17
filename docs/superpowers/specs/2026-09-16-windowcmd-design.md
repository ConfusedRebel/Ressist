# windowcmd design

Date: 2026-09-16

## Goal

`windowcmd` is a persistent native Wayland command palette for Hyprland. A
Hyprland keybind invokes `windowcmd toggle`; the process captures the focused
window before showing its own layer surface, presents contextual and fuzzy
searchable commands, executes the selected command against the captured
window, and hides immediately afterward.

If the captured window disappears while the palette is open, the palette
closes automatically and no action is executed against another window.

## Choices

- Language: C, using the system GTK4 and `gtk4-layer-shell` libraries.
- Surface: a non-exclusive keyboard-interactive Wayland layer-shell surface,
  centered on the target window's monitor.
- Control: a per-user Unix socket, with `toggle`, `show`, `hide`, and `status`
  commands. The long-lived process owns the socket and rejects a second
  instance.
- Persistence: bounded JSON files under XDG config/state directories; no
  database dependency.
- Runtime integration: `hyprctl` JSON queries and dispatch commands, with
  runtime capability checks and graceful handling of unavailable dispatchers.

## Components

### Main and control socket

`main.c` initializes GTK, parses command-line modes, creates the per-user
runtime socket, and owns the event loop. A running process is required for
`toggle`, `show`, `hide`, and `status`; `start` launches the background mode.
The socket protocol is a newline-delimited command and response protocol,
restricted to the current user's runtime directory.

### Hyprland adapter

`hyprland.c` invokes `hyprctl` without a shell and parses JSON output. The
active-window snapshot stores at least:

- address (the primary stable target identifier)
- class and title
- PID
- workspace identity/name
- monitor identity/name
- floating, fullscreen, pinned, pseudo, and split state when exposed

The adapter validates the target address against the current client list
before each window action. It uses the modern installed `hyprctl` interface,
including `dispatch`, `setprop`, `getprop`, `eval`, and `instances` as
appropriate. Dispatcher/property capability checks are cached for the process
and unsupported actions are omitted or reported through a notification.

Window actions pass the address as a structured argument. Window metadata is
never interpolated into a shell command.

### Provider and result model

`providers.c` defines the provider boundary and a result record containing:

- title
- description
- keywords
- optional icon/path
- provider/category
- action kind and action payload
- base/contextual ranking data

The initial providers are contextual Hyprland window actions, cached
executables from `PATH`, and user-defined custom commands. The boundary is
intentionally small so clipboard actions, macros, scripts, or plugins can be
added later without changing the UI.

### Built-in actions

The built-in provider includes close, kill, toggle floating, toggle fullscreen,
pin/unpin, center, workspace move, silent workspace move, directional move,
resize, focus, pseudo tiling, split toggle where supported, opacity, monitor
move, always-on-top where supported, and other safe dispatchers exposed by the
installed Hyprland interface.

Natural input forms are parsed into structured actions. Examples include
`workspace 4`, `move workspace 4`, `resize 1200 800`, `opacity 0.9`, and
`monitor HDMI-A-1`. Contextual aliases include `float`/`tile`,
`fullscreen`/`exit fullscreen`, and `pin`/`unpin`. Invalid or incomplete
arguments remain searchable but cannot execute.

### PATH and custom commands

The PATH provider scans executable names once at startup, deduplicates them,
and refreshes only when the effective `PATH` changes or the configured cache
refresh interval expires. A command result stores the executable separately
from its display text.

Only an explicitly selected or explicitly configured shell command is run via
the configured shell. The target snapshot is exported as:

```text
HYPR_TARGET_ADDRESS
HYPR_TARGET_CLASS
HYPR_TARGET_TITLE
HYPR_TARGET_PID
HYPR_TARGET_WORKSPACE
```

Custom commands may reference target properties through documented template
variables. Values are substituted as environment data or shell-quoted
template values; no window property is blindly concatenated into a Hyprland
dispatch string.

## UI and interaction

The layer surface is hidden until a target snapshot succeeds. On show, the UI
sets its monitor from the snapshot and displays a compact centered panel with
configurable width, result limit, font, radius, transparency, and position.
The panel contains a target header with class/title and an icon when one can
be resolved, a search field, and a scrollable result list.

Search results are rebuilt on each text change from cached provider data.
Keyboard behavior is:

- Up/down: move selection
- Ctrl+n/Ctrl+p: move selection
- Enter: execute selected result
- Escape: hide without execution

The selected result is visibly highlighted. The panel hides as soon as an
execution is accepted. Errors are sent to a Hyprland notification and the
process log; the target is revalidated before a window action starts.

## Ranking and history

`ranking.c` normalizes text and computes a lightweight fuzzy score with strong
exact/prefix matches. It adds configurable boosts for prior use, frequency,
recency, matching application class, and current target state. Empty input
starts with recent/contextually useful actions; a typed query favors direct
matches.

`history.c` stores command, timestamp, use count, application class, and
provider kind in `$XDG_STATE_HOME/windowcmd/history.json` (falling back to
`~/.local/state`). Entries are updated after accepted execution and trimmed
to the configured maximum, so history cannot grow indefinitely. Writes use a
temporary file and rename for crash-safe replacement.

## Configuration

Configuration is read from `$XDG_CONFIG_HOME/windowcmd/config.json` (falling
back to `~/.config`). Missing configuration uses safe defaults and
`windowcmd init` writes a commented/documented example-compatible JSON file.
Supported settings are:

- width, maximum results, position, font, border radius, transparency
- history size
- custom commands
- aliases
- disabled built-ins
- shell
- PATH-command inclusion
- ranking weights

The repository includes an example configuration. User paths are never
hardcoded.

## Failure and security behavior

- No Hyprland socket or active window: `show`/`toggle` report failure and do
  not show an unscoped palette.
- Target address no longer appears in `clients -j`: close the palette and
  discard its captured action context.
- Malformed config/custom command: use defaults for the invalid section and
  notify/log the error.
- Hyprland action arguments are validated and passed as separate argv values.
- Shell execution is limited to commands explicitly entered or configured by
  the user and runs with the configured shell and target environment.
- Titles, classes, and addresses are treated as data and are never used to
  construct an unescaped shell command.

## Testing and delivery

Unit tests cover fuzzy scoring, ranking factors, command parsing, aliases,
template expansion, config defaults, bounded history, and structured command
construction. Fixture JSON tests cover active-window parsing, target
validation, and state-aware labels without requiring a compositor. Runtime
checks cover `--help`, `status`, invalid configuration, missing compositor,
duplicate instance, and clean shutdown.

The README documents dependencies, build/install commands, XDG locations,
CLI usage, example configuration, background startup, and a Hyprland keybind
such as:

```ini
bind = SUPER, SPACE, exec, windowcmd toggle
```

