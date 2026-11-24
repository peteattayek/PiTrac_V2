# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
"""A stand-in for `v4l2-ctl --stream-mmap --stream-to=FILE|- --verbose` (tests/test-trigger-capture.sh).

It streams free-running frames every 11.2 ms until the fake sensor (FAKE_I2C, the files the
fake i2ctransfer keeps) is switched to slave (0x1003 = 0x08), then emits one frame for each
trigger-N file the test creates. FAKE_MODE=stuck keeps it free-running after the switch.
With two cameras (bench 7c.2) each sensor keeps its state in FAKE_I2C/bus<N>, N from
`-d /dev/video<N>`, and the trigger-N files stay in FAKE_I2C itself: one line triggers both.
Each frame is FAKE_SIZE bytes of its sequence number, written, then logged to stderr in
v4l2-ctl's format with a time.monotonic() timestamp, as the real one is CLOCK_MONOTONIC.

The output buffering is the real one's. To a named file, glibc stdio holds the tail of the
data in a 4 KB buffer until more arrives or the program exits normally, so a TERM loses it
(board 1, 2026-11-18). To '-' under `stdbuf -o0`, every frame goes straight out.
"""

import os
from pathlib import Path
import re
import sys
import time

BLOCK = 4096
out = next(a.split("=", 1)[1] for a in sys.argv[1:] if a.startswith("--stream-to="))
args = sys.argv[1:]
# Only the number: Git Bash rewrites /dev/video<N> into a Windows path for a Windows Python.
number = re.search(r"video(\d+)$", args[args.index("-d") + 1]) if "-d" in args else None
triggers = Path(os.environ["FAKE_I2C"])
state = triggers / f"bus{number.group(1)}" if number else triggers
if not state.is_dir():
    state = triggers
size = int(os.environ["FAKE_SIZE"])
stuck = os.environ.get("FAKE_MODE") == "stuck"
direct = out == "-"
raw = sys.stdout.buffer if direct else open(out, "wb", buffering=0)
pending = b""
seq = 0
handled = 0


def emit() -> None:
    global seq, pending
    frame = bytes([seq % 256]) * size
    if direct:
        raw.write(frame)
        raw.flush()
    else:
        pending += frame
        cut = len(pending) // BLOCK * BLOCK
        raw.write(pending[:cut])
        pending = pending[cut:]
    print(f"cap dqbuf: 0 seq: {seq:6d} bytesused: {size} ts: {time.monotonic():.6f} "
          "delta: 11.200 ms (ts-monotonic, ts-src-eof)", file=sys.stderr, flush=True)
    seq += 1


while True:
    register = state / "reg_1003"
    slave = register.exists() and register.read_text().strip() == "0x08" and not stuck
    if not slave:
        emit()
        time.sleep(0.0112)
        continue
    if len(list(triggers.glob("trigger-*"))) > handled:
        handled += 1
        emit()
    time.sleep(0.01)
