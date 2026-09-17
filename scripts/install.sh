#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
INSTALL_PREFIX=${PREFIX:-"$HOME/.local"}

cd "$PROJECT_DIR"
make install PREFIX="$INSTALL_PREFIX"
printf '%s\n' "Installed windowcmd to $INSTALL_PREFIX/bin/windowcmd"
printf '%s\n' 'Add this to hyprland.conf:'
printf '%s\n' 'exec-once = windowcmd start'
printf '%s\n' 'bind = SUPER, SPACE, exec, windowcmd toggle'
