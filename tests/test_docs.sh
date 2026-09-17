#!/bin/sh
set -eu

test -f README.md
grep -F 'make' README.md >/dev/null
grep -F 'windowcmd toggle' README.md >/dev/null
grep -F 'bind = SUPER, SPACE, exec, windowcmd toggle' README.md >/dev/null
grep -F 'XDG_CONFIG_HOME' README.md >/dev/null
grep -F 'XDG_STATE_HOME' README.md >/dev/null
grep -F 'HYPR_TARGET_ADDRESS' README.md >/dev/null
grep -F 'HYPR_TARGET_CLASS' README.md >/dev/null
grep -F 'HYPR_TARGET_TITLE' README.md >/dev/null
grep -F 'HYPR_TARGET_PID' README.md >/dev/null
grep -F 'HYPR_TARGET_WORKSPACE' README.md >/dev/null
