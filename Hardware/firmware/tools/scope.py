#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
"""
scope.py -- pull an ADC block capture off the board and plot it.

This is the host half of the `capture` CLI command. Together they make the
board its own logging oscilloscope, which is what makes phases 3-5 workable
without clipping a probe onto every node.

    pip install pyserial matplotlib

Examples
--------
Detect signal (ADC5) at 250 ksps, 8000 samples:
    python scope.py --port COM7 --mask 0x20 --samples 8000 --rate 250000

Detect + mic interleaved, exactly as the ARMED mode runs them:
    python scope.py --port COM7 --mask 0xa0 --samples 8000 --rate 500000

Raw TIA carrier (ADC2). Note you only get ~4.8 samples per period at a
104 kHz carrier, so this shows amplitude/health, not waveform shape:
    python scope.py --port COM7 --mask 0x04 --samples 4000 --rate 500000

Channels: 0=strobe current  1=+5V_IN/2  2=TIA raw  5=detect  7=mic
(3 and 4 are digital outputs on this board -- the firmware will reject them.)
"""

import argparse
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial not installed:  python -m pip install pyserial")

CH_NAMES = {
    0: "strobe current (135 mV/A)",
    1: "+5V_IN / 2",
    2: "TIA raw carrier",
    5: "detect signal (post-filter)",
    7: "microphone",
}

ADC_VREF = 3.3
ADC_FULL = 4095.0


def read_capture(port, baud, mask, samples, rate, timeout=60.0):
    with serial.Serial(port, baud, timeout=1.0) as ser:
        time.sleep(0.3)
        ser.reset_input_buffer()
        cmd = f"capture 0x{mask:02x} {samples} {rate}\r\n"
        ser.write(cmd.encode())

        header = {}
        columns = []
        rows = []
        deadline = time.time() + timeout
        started = False

        while time.time() < deadline:
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", "replace").strip()
            if not line:
                continue

            if line.startswith("# capture"):
                for tok in line.split()[2:]:
                    if "=" in tok:
                        k, v = tok.split("=", 1)
                        header[k] = v
                started = True
                continue
            if line.startswith("# columns:"):
                columns = [c.strip() for c in line.split(":", 1)[1].split()]
                continue
            if line.startswith("# end"):
                break
            if line.startswith("ERR") or line.startswith("usage:"):
                sys.exit(f"board said: {line}")
            if not started:
                continue
            if line.startswith("#") or line.startswith(">"):
                continue

            try:
                rows.append([int(v) for v in line.split(",")])
            except ValueError:
                pass  # echoed command characters etc.

        return header, columns, rows


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True, help="COM7, /dev/ttyACM0, ...")
    ap.add_argument("--baud", type=int, default=115200, help="ignored by USB-CDC")
    ap.add_argument("--mask", type=lambda s: int(s, 0), default=0x20,
                    help="channel bitmask, e.g. 0x20 for ADC5 (default)")
    ap.add_argument("--channel", type=int, default=None,
                    help="convenience: single channel number instead of --mask")
    ap.add_argument("--samples", type=int, default=4000)
    ap.add_argument("--rate", type=int, default=250000, help="Hz, aggregate")
    ap.add_argument("--volts", action="store_true", help="y axis in volts at the pin")
    ap.add_argument("--csv", help="also write the raw samples here")
    ap.add_argument("--no-plot", action="store_true")
    args = ap.parse_args()

    mask = (1 << args.channel) if args.channel is not None else args.mask

    header, columns, rows = read_capture(args.port, args.baud, mask,
                                         args.samples, args.rate)
    if not rows:
        sys.exit("no samples returned -- check the port, and that the firmware is running")

    print(f"header : {header}")
    print(f"columns: {columns}")
    print(f"rows   : {len(rows)}")

    if header.get("overran") == "1":
        print("\n*** WARNING: ADC FIFO overran. The round-robin channel phase is lost,\n"
              "***          so every sample after the overrun is mislabeled.\n"
              "***          Lower --rate or --samples and retry.\n")

    rate = int(header.get("rate", args.rate))
    nch = len(columns) if columns else len(rows[0])
    per_ch_rate = rate / nch
    dt = 1.0 / per_ch_rate
    print(f"per-channel rate: {per_ch_rate:.0f} Hz  ({dt*1e6:.2f} us/sample)")

    if args.csv:
        with open(args.csv, "w", encoding="utf-8") as f:
            f.write(",".join(["t_s"] + columns) + "\n")
            for i, r in enumerate(rows):
                f.write(",".join([f"{i*dt:.9f}"] + [str(v) for v in r]) + "\n")
        print(f"wrote {args.csv}")

    if args.no_plot:
        return

    try:
        import matplotlib.pyplot as plt
    except ImportError:
        sys.exit("matplotlib not installed (use --csv --no-plot, or pip install matplotlib)")

    t = [i * dt for i in range(len(rows))]
    fig, axes = plt.subplots(nch, 1, sharex=True, figsize=(11, 2.4 * nch), squeeze=False)

    for k in range(nch):
        ax = axes[k][0]
        y = [r[k] for r in rows]
        if args.volts:
            y = [v * ADC_VREF / ADC_FULL for v in y]
        ax.plot(t, y, linewidth=0.8)
        label = columns[k] if k < len(columns) else f"col{k}"
        try:
            chnum = int(label.replace("ch", ""))
            label = f"{label} â€” {CH_NAMES.get(chnum, '?')}"
        except ValueError:
            pass
        ax.set_ylabel("V" if args.volts else "code")
        ax.set_title(label, fontsize=9, loc="left")
        ax.grid(alpha=0.3)

    axes[-1][0].set_xlabel("time (s)")
    fig.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()
