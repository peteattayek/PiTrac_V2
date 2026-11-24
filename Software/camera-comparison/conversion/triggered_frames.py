# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
"""Report a triggered Mira220 capture (scripts/trigger-capture.sh; PiTrac V2 bench 7c.1).

The capture holds every frame v4l2-ctl dequeued, in order: free-running frames from the
stream's start, then the frames REQ EXP triggers produced after the switch to slave mode.
A frame whose device timestamp (CLOCK_MONOTONIC, like the "ready" time the script took
after the switch) is later than "ready" counts as triggered. This writes frames.tsv
(every frame), trigger-NN.pgm (each triggered frame, 8-bit, viewable anywhere) and
summary.tsv, and prints the summary.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import sys

from .convert import CAPTURE_LINE, ConversionError, timestamp_us

PARTIAL_WRITE = re.compile(r"\b(\d+) != (\d+)\b")
# Triggered frames closer together than this are free-running, not triggers typed at a CLI.
FREE_RUNNING_GAP_US = 50_000
FREE_RUNNING_RUN = 5


@dataclass(frozen=True)
class Frame:
    index: int          # position in the raw file
    seq: int
    bytesused: int
    ts_us: int
    gap_us: int | None  # from the previous frame; None for the first
    triggered: bool


def parse_log(text: str, ready_us: int) -> list[Frame]:
    frames: list[Frame] = []
    previous = None
    for line in text.splitlines():
        if PARTIAL_WRITE.search(line) and "dqbuf" not in line:
            raise ConversionError(f"v4l2-ctl reported a partial write: {line.strip()}")
        match = CAPTURE_LINE.match(line)
        if not match:
            continue
        ts = timestamp_us(match.group(3))
        if previous is not None and ts <= previous:
            raise ConversionError("Device timestamps do not increase")
        frames.append(Frame(len(frames), int(match.group(1)), int(match.group(2)), ts,
                            None if previous is None else ts - previous, ts > ready_us))
        previous = ts
    return frames


def free_running_after_ready(frames: list[Frame]) -> bool:
    """True if FREE_RUNNING_RUN triggered frames in a row came at a free-running rate."""
    run = 0
    for frame in frames:
        if frame.triggered and frame.gap_us is not None and frame.gap_us < FREE_RUNNING_GAP_US:
            run += 1
            if run + 1 >= FREE_RUNNING_RUN:
                return True
        else:
            run = 0
    return False


def gray_rows(frame: bytes, width: int, height: int, stride: int) -> bytes:
    if len(frame) < stride * height:
        raise ConversionError("Partial frame")
    return b"".join(frame[y * stride:y * stride + width] for y in range(height))


def stats(pixels: bytes) -> dict[str, float]:
    hist = [pixels.count(bytes((v,))) for v in range(256)]
    total = len(pixels)

    def percentile(p: float) -> int:
        limit, seen = p * total, 0
        for value, count in enumerate(hist):
            seen += count
            if seen >= limit:
                return value
        return 255

    present = [v for v, c in enumerate(hist) if c]
    return {
        "mean": sum(v * c for v, c in enumerate(hist)) / total,
        "min": present[0], "p01": percentile(0.01), "p99": percentile(0.99), "max": present[-1],
        "saturated_fraction": hist[255] / total,
    }


def report(log: Path, raw: Path, out: Path, ready: str, width: int, height: int,
           stride: int, sizeimage: int, fourcc: str) -> list[str]:
    if fourcc != "GREY":
        raise ConversionError("Only the 8-bit GREY profile is supported (configure without --mira-bit-depth 12)")
    if stride < width or sizeimage != stride * height:
        raise ConversionError("Unsupported buffer layout")
    ready_us = timestamp_us(ready)
    frames = parse_log(log.read_text(encoding="utf-8", errors="replace"), ready_us)
    size = raw.stat().st_size if raw.exists() else 0
    complete = size // sizeimage
    notes = []
    if complete < len(frames):
        notes.append(f"the raw file holds {complete} whole frames of {len(frames)} logged; "
                     "the missing ones are reported but not converted")
    if size % sizeimage:
        notes.append(f"{size % sizeimage} trailing bytes of a partial frame are ignored")
    for frame in frames:
        if frame.bytesused != sizeimage:
            notes.append(f"frame {frame.index} (seq {frame.seq}) has {frame.bytesused} bytes, not {sizeimage}")

    rows = ["index\tseq\tdevice_ts_s\tgap_ms\tkind\tpgm\tmean\tmin\tp01\tp99\tmax\tsaturated_fraction"]
    triggered = [f for f in frames if f.triggered]
    source = raw.open("rb") if complete else None
    try:
        number = 0
        for frame in frames:
            gap = "" if frame.gap_us is None else f"{frame.gap_us / 1000:.3f}"
            cells = [str(frame.index), str(frame.seq), f"{frame.ts_us / 1e6:.6f}", gap,
                     "triggered" if frame.triggered else "free-running"]
            if frame.triggered and source is not None and frame.index < complete:
                number += 1
                source.seek(frame.index * sizeimage)
                pixels = gray_rows(source.read(sizeimage), width, height, stride)
                name = f"trigger-{number:02d}.pgm"
                (out / name).write_bytes(f"P5\n{width} {height}\n255\n".encode() + pixels)
                s = stats(pixels)
                cells += [name, f"{s['mean']:.2f}", str(s["min"]), str(s["p01"]), str(s["p99"]),
                          str(s["max"]), f"{s['saturated_fraction']:.6f}"]
            else:
                cells += [""] * 7
            rows.append("\t".join(cells))
    finally:
        if source is not None:
            source.close()
    (out / "frames.tsv").write_text("\n".join(rows) + "\n", encoding="utf-8")

    free = [f for f in frames if not f.triggered]
    intervals = [f.gap_us for f in free if f.gap_us is not None]
    summary = [
        ("frames_logged", str(len(frames))),
        ("frames_in_raw", str(complete)),
        ("free_running_frames", str(len(free))),
        ("free_running_median_interval_ms",
         f"{sorted(intervals)[len(intervals) // 2] / 1000:.3f}" if intervals else ""),
        ("ready_ts_s", ready),
        ("triggered_frames", str(len(triggered))),
        ("first_triggered_after_ready_s",
         f"{(triggered[0].ts_us - ready_us) / 1e6:.3f}" if triggered else ""),
        ("free_running_after_ready", "yes" if free_running_after_ready(frames) else "no"),
    ]
    for note in notes:
        summary.append(("note", note))
    (out / "summary.tsv").write_text("".join(f"{k}\t{v}\n" for k, v in summary), encoding="utf-8")
    lines = [f"{k}: {v}" for k, v in summary]
    for frame in triggered:
        gap = "" if frame.gap_us is None else f", {frame.gap_us / 1e6:.3f} s after the previous frame"
        lines.append(f"triggered frame: seq {frame.seq}, device time {frame.ts_us / 1e6:.6f} s{gap}")
    return lines


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--raw", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--ready-ts", required=True, help="CLOCK_MONOTONIC seconds, 6 decimals")
    for name in ("width", "height", "stride", "sizeimage"):
        parser.add_argument(f"--{name}", type=int, required=True)
    parser.add_argument("--fourcc", required=True)
    args = parser.parse_args(argv)
    try:
        lines = report(args.log, args.raw, args.out, args.ready_ts, args.width, args.height,
                       args.stride, args.sizeimage, args.fourcc)
    except (ConversionError, OSError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
