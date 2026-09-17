#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
set -euo pipefail
SCRIPTS=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../scripts" && pwd)
source "$SCRIPTS/common.sh"
need mbuffer mkfifo timeout dd cmp stat
fixture=$(mktemp -d "${TMPDIR:-/tmp}/pitrac-writer-tests.XXXXXXXX")
pids=()
cleanup() {
    local rc=$? pid
    for pid in "${pids[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then kill -TERM "$pid" 2>/dev/null || true; fi
        wait "$pid" 2>/dev/null || true
    done
    rm -r -- "$fixture"
    exit "$rc"
}
trap cleanup EXIT
checks=0
dd if=/dev/urandom of="$fixture/input" bs=1048576 count=3 status=none
printf 'unaligned final bytes' >> "$fixture/input"
mkfifo -m 600 "$fixture/input.fifo"
timeout --kill-after=2s 20s bash "$SCRIPTS/buffered-writer.sh" \
    "$fixture/input.fifo" "$fixture/output" > "$fixture/writer.log" 2>&1 &
pids+=("$!")
timeout --kill-after=2s 20s dd if="$fixture/input" of="$fixture/input.fifo" bs=65536 status=none &
pids+=("$!")
for pid in "${pids[@]}"; do
    if ! wait "$pid"; then cat "$fixture/writer.log" >&2; die "FIFO writer fixture failed."; fi
done
pids=()
cmp "$fixture/input" "$fixture/output"
((checks+=1))
[[ $(stat -c %s "$fixture/input") == "$(stat -c %s "$fixture/output")" ]] || die "Writer padded or truncated the last block."
((checks+=1))
if bash "$SCRIPTS/buffered-writer.sh" "$fixture/input.fifo" "$fixture/output" > "$fixture/error.log" 2>&1; then
    die "Writer overwrote an existing output file."
fi
((checks+=1))
cmp "$fixture/input" "$fixture/output"
((checks+=1))
if bash "$SCRIPTS/buffered-writer.sh" "$fixture/input" "$fixture/new-output" > "$fixture/error.log" 2>&1; then
    die "Writer accepted a regular file as its FIFO input."
fi
((checks+=1))
echo "All $checks buffered-writer checks passed (real mbuffer/FIFO transfer, including an unaligned final block)."
