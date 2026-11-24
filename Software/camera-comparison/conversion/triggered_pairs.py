# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
"""Pair two Mira220s' triggered frames (scripts/trigger-capture.sh, two --config; PiTrac V2 bench 7c.2).

The board's one REQ EXP line triggers both cameras, so each trigger should give one frame on
each, with device timestamps (CLOCK_MONOTONIC) close together. A triggered frame of the first
camera and one of the second are a pair when each is the other's nearest, within PAIR_WINDOW_US.
Anything left over is unpaired: a trigger one camera missed, or a frame without a trigger.

Reads each camera's frames.tsv (conversion/triggered_frames.py); writes pairs.tsv and
summary.tsv in --out, and prints the summary. The timestamps show which frames belong
together; how far apart the two exposures really start is the analyser's job (the strobes).
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import sys

from .convert import ConversionError, timestamp_us

# Triggers typed at a CLI come seconds apart; the two frames of one trigger come within a frame time.
PAIR_WINDOW_US = 50_000


@dataclass(frozen=True)
class Shot:
    seq: int
    ts_us: int
    pgm: str
    mean: str


def read_triggered(path: Path) -> list[Shot]:
    lines = path.read_text(encoding="utf-8").splitlines()
    if not lines:
        raise ConversionError(f"{path} is empty")
    header = lines[0].split("\t")
    try:
        col = {name: header.index(name) for name in ("seq", "device_ts_s", "kind", "pgm", "mean")}
    except ValueError as error:
        raise ConversionError(f"{path} is not a triggered_frames report") from error
    shots = []
    for line in lines[1:]:
        cells = line.split("\t")
        if len(cells) != len(header):
            raise ConversionError(f"{path}: a row has {len(cells)} cells, not {len(header)}")
        if cells[col["kind"]] == "triggered":
            shots.append(Shot(int(cells[col["seq"]]), timestamp_us(cells[col["device_ts_s"]]),
                              cells[col["pgm"]], cells[col["mean"]]))
    return shots


def nearest(ts_us: int, shots: list[Shot]) -> int | None:
    if not shots:
        return None
    return min(range(len(shots)), key=lambda k: (abs(shots[k].ts_us - ts_us), k))


def pair(a: list[Shot], b: list[Shot]) -> list[tuple[int, int]]:
    """(index in a, index in b) for each mutual-nearest match within PAIR_WINDOW_US, in a's order."""
    pairs = []
    for i, shot in enumerate(a):
        j = nearest(shot.ts_us, b)
        if j is None or abs(b[j].ts_us - shot.ts_us) > PAIR_WINDOW_US:
            continue
        if nearest(b[j].ts_us, a) == i:
            pairs.append((i, j))
    return pairs


def report(out: Path, cameras: list[tuple[str, Path]]) -> list[str]:
    if len(cameras) != 2 or cameras[0][0] == cameras[1][0]:
        raise ConversionError("Give exactly two cameras with different names")
    (name_a, path_a), (name_b, path_b) = cameras
    a, b = read_triggered(path_a), read_triggered(path_b)
    pairs = pair(a, b)
    rows = [f"pair\t{name_a}_seq\t{name_a}_ts_s\t{name_a}_pgm\t{name_a}_mean\t"
            f"{name_b}_seq\t{name_b}_ts_s\t{name_b}_pgm\t{name_b}_mean\tdelta_us"]
    deltas = []
    for number, (i, j) in enumerate(pairs, start=1):
        delta = b[j].ts_us - a[i].ts_us
        deltas.append(delta)
        rows.append("\t".join([str(number), str(a[i].seq), f"{a[i].ts_us / 1e6:.6f}", a[i].pgm, a[i].mean,
                               str(b[j].seq), f"{b[j].ts_us / 1e6:.6f}", b[j].pgm, b[j].mean, str(delta)]))
    (out / "pairs.tsv").write_text("\n".join(rows) + "\n", encoding="utf-8")

    used_a = {i for i, _ in pairs}
    used_b = {j for _, j in pairs}
    summary = [
        ("cameras", f"{name_a},{name_b}"),
        (f"triggered_{name_a}", str(len(a))),
        (f"triggered_{name_b}", str(len(b))),
        ("pairs", str(len(pairs))),
        (f"unpaired_{name_a}", str(len(a) - len(used_a))),
        (f"unpaired_{name_b}", str(len(b) - len(used_b))),
        ("delta_us_min", str(min(deltas)) if deltas else ""),
        ("delta_us_max", str(max(deltas)) if deltas else ""),
    ]
    notes = [f"{name} seq {s.seq} at {s.ts_us / 1e6:.6f} s has no frame from the other camera"
             for name, shots, used in ((name_a, a, used_a), (name_b, b, used_b))
             for k, s in enumerate(shots) if k not in used]
    summary += [("note", note) for note in notes]
    (out / "summary.tsv").write_text("".join(f"{k}\t{v}\n" for k, v in summary), encoding="utf-8")
    lines = [f"{k}: {v}" for k, v in summary]
    lines.append(f"delta_us is {name_b}'s device time minus {name_a}'s, per pair (pairs.tsv)")
    return lines


def camera_arg(text: str) -> tuple[str, Path]:
    name, sep, path = text.partition("=")
    if not sep or not name or not path or not name.replace("_", "").isalnum():
        raise argparse.ArgumentTypeError("expected NAME=FRAMES_TSV, NAME letters, digits or _")
    return name, Path(path)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--camera", type=camera_arg, action="append", required=True,
                        help="NAME=FRAMES_TSV, twice")
    args = parser.parse_args(argv)
    try:
        lines = report(args.out, args.camera)
    except (ConversionError, OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
