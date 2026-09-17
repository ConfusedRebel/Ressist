# windowcmd

`windowcmd` is a lightweight command palette for Hyprland. It stays in the
background and opens as a centered Wayland layer when you invoke its control
command. The focused window is captured before the palette is shown, so every
window action targets the original Hyprland address even after the palette has
keyboard focus.

## Features

- Native GTK4 and Wayland layer-shell UI.
- Instant fuzzy search with keyboard-first navigation.
- Context-aware window actions: floating, fullscreen, pinning, workspace,
  resize, opacity, movement, focus, pseudo tiling, split, monitor, and more.
- Cached PATH executable suggestions.
- Explicit custom commands and shell commands with target metadata in the
  environment.
- Persistent, bounded command history with application-aware ranking.
- Automatic close when the captured target window disappears.

## Dependencies

Install a C compiler and development packages providing GTK4,
`gtk4-layer-shell-0`, `json-glib-1.0`, and GLib/GIO. The required
`pkg-config` modules are `gtk4`, `gtk4-layer-shell-0`, `json-glib-1.0`, and
`glib-2.0`.

## Build and install

```sh
make
make test
sudo make install
```

For a user-local install:

```sh
make install PREFIX="$HOME/.local"
```

The install target places the binary in `bin`, the stylesheet and example
config under `share/windowcmd`, and the user service under
`lib/systemd/user`. It does not write a user configuration during build.

## Start and Hyprland bind

Start the persistent service once from Hyprland autostart, a user service, or
a terminal:

```ini
exec-once = windowcmd start
```

Bind the palette toggle, for example:

```ini
bind = SUPER, SPACE, exec, windowcmd toggle
```

The control commands are:

```text
windowcmd start
windowcmd toggle
windowcmd show
windowcmd hide
windowcmd status
windowcmd init
```

`show` and `toggle` query `hyprctl activewindow -j` first. If there is no
active target, or if the target address disappears while the palette is open,
the palette stays hidden and no unscoped action runs.

## Keyboard controls

The search field is focused when the palette opens.

- Up/down: move selection
- Ctrl+n/Ctrl+p: move selection
- Tab: accept the highlighted inline completion
- Enter: execute
- Escape: hide

The result area has a fixed height based on the configured maximum result
count. It does not use a scroll wheel or a moving scroll container; visible
results keep their descriptions on screen while only the highlight changes.

Examples of built-in command syntax include:

```text
workspace 4
move workspace 4
resize 1200 800
opacity 0.9
monitor HDMI-A-1
```

Built-in window actions are dispatched with structured `hyprctl` arguments and
the captured address. A normal command typed into the palette is a shell
command, not an implicit window action.

## Configuration and history

Create the default config with:

```sh
windowcmd init
```

The config file is `$XDG_CONFIG_HOME/windowcmd/config.json`, or
`$HOME/.config/windowcmd/config.json` when `XDG_CONFIG_HOME` is unset. The
example is [config/windowcmd.example.json](config/windowcmd.example.json).

Supported settings include width, result count, position, font, border
radius, transparency, history size, shell, PATH inclusion, disabled built-ins,
aliases, custom commands, and ranking weights.

History is stored at `$XDG_STATE_HOME/windowcmd/history.json`, or
`$HOME/.local/state/windowcmd/history.json` when `XDG_STATE_HOME` is unset.
It is capped by `history_size` and written atomically.

## Target environment

PATH and custom shell commands receive:

```text
HYPR_TARGET_ADDRESS
HYPR_TARGET_CLASS
HYPR_TARGET_TITLE
HYPR_TARGET_PID
HYPR_TARGET_WORKSPACE
```

The example custom command uses `${HYPR_TARGET_PID}`. Only commands explicitly
entered or configured by the user go through the configured shell. Window
titles, classes, and addresses are treated as data by built-in actions.

## User service

After installing, enable the service if your session exports a PATH containing
the installed binary:

```sh
systemctl --user daemon-reload
systemctl --user enable --now windowcmd.service
```

Alternatively use `exec-once = windowcmd start` in Hyprland. Only one process
can own the per-user runtime socket.

## Troubleshooting

Use `windowcmd status` to check whether the service is running. If it reports
that the service is not running, start `windowcmd start` and check the
terminal/service journal. If the palette does not open, verify that the
session is Wayland/Hyprland and that `hyprctl activewindow -j` returns a
window. Unsupported Hyprland dispatchers are omitted or reported instead of
being sent with guessed syntax.
