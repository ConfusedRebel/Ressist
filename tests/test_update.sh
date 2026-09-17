#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

test -f "$ROOT/update"
sh -n "$ROOT/update"
grep -Fq 'make test' "$ROOT/update"
grep -Fq 'make install' "$ROOT/update"
grep -Fq 'systemctl --user restart windowcmd.service' "$ROOT/update"
grep -Fq 'pkill -x windowcmd' "$ROOT/update"

printf '%s\n' 'update helper checks passed'
