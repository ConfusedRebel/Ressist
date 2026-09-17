#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

grep -Fq 'g_signal_connect(ui->entry, "activate", G_CALLBACK(entry_activated), ui);' \
    "$ROOT/src/ui.c"

printf '%s\n' 'entry activation checks passed'
