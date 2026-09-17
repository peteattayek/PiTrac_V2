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
import os
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
        return read_capture_on(ser, mask, samples, rate, timeout)


def read_capture_on(ser, mask, samples, rate, timeout=60.0):
    """One capture on an ALREADY-OPEN port.

    Roll mode must not reopen the port per block: the open plus the 0.3 s
    settle above costs more dead time than the capture itself, and every
    reopen resets the board's CDC endpoint.
    """
    return read_block_on(ser, f"capture 0x{mask:02x} {samples} {rate}", timeout)


def read_block_on(ser, command, timeout=60.0):
    """Send one command and collect its `# capture` ... `# end` CSV block."""
    ser.reset_input_buffer()
    ser.write((command + "\r\n").encode())

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
        if line.startswith("NO TRIGGER") or line.startswith("aborted"):
            print(f"board said: {line}")
            return {}, [], []
        if line.startswith("armed:") or line.startswith("  window"):
            print(f"  {line}")
            continue
        if not started:
            continue
        if line.startswith("#") or line.startswith(">"):
            continue

        try:
            rows.append([int(v) for v in line.split(",")])
        except ValueError:
            pass  # echoed command characters etc.

    return header, columns, rows


def out_path(name):
    """Where auto-named captures go: ./captures/, created on demand.

    They used to land in the current directory, which during a bench session is
    the firmware dir -- i.e. inside the repo, where .gitignore covered only
    *.csv.out. A run of snaps would quietly show up in `git status`. Anything
    the user names explicitly with --csv is left exactly where they asked.
    """
    if os.path.dirname(name):
        return os.path.abspath(name)
    os.makedirs("captures", exist_ok=True)
    return os.path.abspath(os.path.join("captures", name))


def dump_window(blocks, args):
    """Write every block currently in the rolling window to one CSV.

    Blocks keep their real timeline positions, so the gaps are visible in the
    t_s column as jumps. A `block` column is included because rows either side
    of a gap are NOT adjacent in time and averaging across one is wrong.
    """
    fn = out_path(time.strftime("roll_%Y%m%d_%H%M%S.csv"))
    with open(fn, "w", encoding="utf-8") as f:
        f.write("t_s,block,ch\n")
        for k, (b0, bdt, bv) in enumerate(blocks):
            for i, v in enumerate(bv):
                f.write(f"{b0 + i*bdt:.9f},{k},{v}\n")
    return fn


def roll(args, mask):
    """Repeatedly capture and live-plot a rolling window.

    THIS IS NOT CONTINUOUS AND THE PLOT SAYS SO. `capture` is a block command:
    the board fills its buffer, then dumps it over CDC as decimal text, and
    nothing is being sampled during that dump. So the timeline has holes, and
    they are drawn as breaks in the trace rather than closed up -- a joined-up
    plot would silently misrepresent when things happened.

    Coverage is set by the SAMPLE RATE, not by the block size. Capture time per
    sample is 1/rate; dump time per sample is a fixed ~5 bytes over CDC. Both
    scale with the sample count, so it cancels:

        duty = (1/rate) / (1/rate + bytes_per_sample / cdc_throughput)

    Lower rate -> longer capture per byte dumped -> better coverage. 250 ksps
    spends most of its time talking; 50 ksps spends most of it listening. The
    measured duty is printed every block so you are never guessing.
    """
    import collections
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        sys.exit("matplotlib not installed:  python -m pip install matplotlib")

    nch_expected = bin(mask).count("1")
    if nch_expected != 1:
        sys.exit("--roll supports a single channel; use --channel N")

    blocks = collections.deque()      # (t0, dt, [codes])
    t_start = time.time()
    n_block = 0
    busy = 0.0
    saved = 0

    plt.ion()
    fig, ax = plt.subplots(figsize=(12, 4))
    (line,) = ax.plot([], [], linewidth=0.7)
    ax.set_xlabel("time (s)")
    ax.set_ylabel("V" if args.volts else "code")
    ax.grid(alpha=0.3)
    hint = "  (breaks = board was dumping, not sampling)"
    fig.canvas.manager.set_window_title("PiTrac scope -- roll")

    # PAUSE. While paused we issue no captures AND stop touching the axis
    # limits -- that second half is the point. The rolling redraw calls
    # set_xlim() every frame, so matplotlib's own zoom/pan would be stamped
    # back to the live window a few hundred ms after you drew it. Freezing the
    # limits is what makes the toolbar usable.
    state = {"paused": False, "since": 0.0, "paused_total": 0.0, "save": False}

    def on_key(ev):
        if ev.key == " ":
            state["paused"] = not state["paused"]
            if state["paused"]:
                state["since"] = time.time()
                print("  -- PAUSED. Zoom/pan with the toolbar; space to resume.")
            else:
                state["paused_total"] += time.time() - state["since"]
                ax.set_autoscaley_on(True)
                print("  -- resumed.")
        elif ev.key in ("s", "S"):
            state["save"] = True

    fig.canvas.mpl_connect("key_press_event", on_key)

    print(f"rolling {args.window:.1f} s window, {args.samples} samples @ "
          f"{args.rate} Hz = {args.samples/args.rate*1000:.0f} ms per block")
    print("SPACE = pause/resume (zoom with the toolbar while paused)")
    print("s     = write everything currently in the window to CSV")
    if args.pause_on_event:
        print(f"auto-pausing on any block with peak deviation >= {args.event} codes")
    print("Ctrl-C to stop.\n")
    print(f"{'blk':>4} {'t':>7} {'base':>6} {'peak dev':>9} {'duty':>6}  note")

    try:
        with serial.Serial(args.port, args.baud, timeout=1.0) as ser:
            time.sleep(0.3)
            while plt.fignum_exists(fig.number):
                if state["save"]:
                    state["save"] = False
                    fn = dump_window(blocks, args)
                    print(f"  -- wrote {fn}")

                if state["paused"]:
                    ax.set_title(f"ch{args.channel} microphone -- PAUSED"
                                 "   (space = resume, toolbar to zoom)",
                                 fontsize=9, loc="left")
                    plt.pause(0.05)     # keep the UI alive; send nothing
                    continue

                t0 = time.time()
                hdr, cols, rows = read_capture_on(ser, mask, args.samples,
                                                  args.rate, timeout=30.0)
                t1 = time.time()
                if not rows:
                    print("  (no samples -- is the board still there?)")
                    continue

                n_block += 1
                rate = int(hdr.get("rate", args.rate))
                dt = 1.0 / rate
                vals = [r[0] for r in rows]
                cap_s = len(vals) * dt
                busy += cap_s

                # The block was being SAMPLED for cap_s ending at t1, so place
                # it on the timeline accordingly rather than at t0 -- the dump
                # happened after the samples, not during them.
                blocks.append((t1 - t_start - cap_s, dt, vals))
                cutoff = (time.time() - t_start) - args.window
                while blocks and blocks[0][0] + len(blocks[0][2]) * blocks[0][1] < cutoff:
                    blocks.popleft()

                s = sorted(vals)
                base = s[len(s) // 2]
                dev = max(abs(v - base) for v in vals)
                clipped = (min(vals) <= 1) or (max(vals) >= 4094)
                # Paused time is not dead time -- it is time we chose not to
                # sample -- so it must not count against the coverage figure.
                active = (t1 - t_start) - state["paused_total"]
                duty = busy / max(active, 1e-9)

                note = ""
                if clipped:
                    note = "*** CLIPPED -- gain too high for this stimulus"
                elif dev >= args.event:
                    note = "<<< EVENT"
                if dev >= args.event and args.save_events:
                    fn = out_path(f"{args.save_events}_{n_block:03d}.csv")
                    with open(fn, "w", encoding="utf-8") as f:
                        f.write("t_s,ch\n")
                        for i, v in enumerate(vals):
                            f.write(f"{i*dt:.9f},{v}\n")
                    saved += 1
                    note += f"  -> {fn}"

                print(f"{n_block:>4} {t1-t_start:>7.1f} {base:>6} {dev:>9} "
                      f"{duty*100:>5.0f}%  {note}")

                # Auto-pause is the reason this mode is usable for a clap: by
                # the time you have seen the transient and reached for the
                # spacebar it has already rolled off the window.
                if args.pause_on_event and dev >= args.event and not state["paused"]:
                    state["paused"] = True
                    state["since"] = time.time()
                    print(f"  -- PAUSED on event (block {n_block}). "
                          "Zoom with the toolbar; space to resume.")

                xs, ys = [], []
                for b0, bdt, bv in blocks:
                    xs.extend(b0 + i * bdt for i in range(len(bv)))
                    ys.extend((v * ADC_VREF / ADC_FULL) if args.volts else v
                              for v in bv)
                    xs.append(float("nan"))       # break, not a join
                    ys.append(float("nan"))
                line.set_data(xs, ys)
                now = time.time() - t_start
                ax.set_xlim(max(0.0, now - args.window), max(args.window, now))
                ax.relim()
                ax.autoscale_view(scalex=False)
                ax.set_title(f"ch{args.channel} microphone -- {duty*100:.0f}% coverage"
                             + hint, fontsize=9, loc="left")
                plt.pause(0.001)
    except KeyboardInterrupt:
        print("\nstopped.")

    if state["paused"]:
        state["paused_total"] += time.time() - state["since"]
    active = (time.time() - t_start) - state["paused_total"]
    print(f"\n{n_block} blocks, {busy:.1f} s sampled of {active:.1f} s running "
          f"({busy/max(active,1e-9)*100:.0f}% coverage"
          + (f", {state['paused_total']:.0f} s paused)" if state["paused_total"] else ")"))
    if saved:
        print(f"{saved} event block(s) written")
    print("\nCoverage is set by --rate, not --samples. Halve the rate to roughly\n"
          "double the coverage; --rate 50000 is a good balance for the mic, whose\n"
          "front end rolls off at 24.1 kHz anyway.")


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
    ap.add_argument("--rate", type=int, default=None,
                    help="Hz, aggregate. Default 250000, or 500000 with --trig "
                         "(the board's own default) so a triggered shot is not "
                         "silently taken at half rate.")
    ap.add_argument("--volts", action="store_true", help="y axis in volts at the pin")
    ap.add_argument("--csv", help="also write the raw samples here")
    ap.add_argument("--no-plot", action="store_true")
    ap.add_argument("--roll", action="store_true",
                    help="capture repeatedly and live-plot a rolling window. "
                         "NOT continuous -- gaps are drawn as breaks.")
    ap.add_argument("--window", type=float, default=5.0,
                    help="roll mode: seconds of history to show (default 5)")
    ap.add_argument("--event", type=int, default=200,
                    help="roll mode: peak deviation from baseline, in codes, "
                         "that counts as an event (default 200)")
    ap.add_argument("--save-events", metavar="PREFIX",
                    help="roll mode: write any event block to PREFIX_NNN.csv")
    ap.add_argument("--pause-on-event", action="store_true",
                    help="roll mode: pause automatically when a block exceeds "
                         "--event, so the transient stays on screen to zoom into")
    ap.add_argument("--trig", type=int, metavar="CODES",
                    help="hardware-triggered single shot: arm the board and wait "
                         "for a deviation of CODES from baseline. Needs --channel.")
    ap.add_argument("--pre", type=float, default=0.25,
                    help="--trig: fraction of the window kept BEFORE the trigger "
                         "(default 0.25)")
    ap.add_argument("--trig-timeout", type=float, default=30.0,
                    help="--trig: seconds to wait for the sound (default 30)")
    args = ap.parse_args()
    if args.rate is None:
        args.rate = 500000 if args.trig is not None else 250000

    mask = (1 << args.channel) if args.channel is not None else args.mask

    if args.roll:
        roll(args, mask)
        return

    if args.trig is not None:
        if args.channel is None:
            sys.exit("--trig needs --channel N (a trigger on interleaved channels "
                     "would be ambiguous)")
        cmd = (f"capture trig {args.channel} {args.trig} {args.rate} "
               f"{int(args.pre*100)} {int(args.trig_timeout)}")
        print(f"> {cmd}")
        with serial.Serial(args.port, args.baud, timeout=1.0) as ser:
            time.sleep(0.3)
            header, columns, rows = read_block_on(
                ser, cmd, timeout=args.trig_timeout + 30.0)
        if not rows:
            sys.exit("no trigger.")
        plot_block(args, header, columns, rows)
        return

    header, columns, rows = read_capture(args.port, args.baud, mask,
                                         args.samples, args.rate)
    if not rows:
        sys.exit("no samples returned -- check the port, and that the firmware is running")
    plot_block(args, header, columns, rows)


def plot_block(args, header, columns, rows):
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

    # A triggered block reports where the trigger landed. Put t = 0 THERE, so
    # pre-trigger history reads as negative time -- otherwise the onset sits at
    # some arbitrary offset and every capture has to be re-aligned by eye.
    trig = int(header["trig"]) if "trig" in header else None
    t0_i = trig if trig is not None else 0
    if trig is not None:
        base = int(header.get("base", 0))
        pk = max(abs(r[0] - base) for r in rows)
        print(f"trigger  : sample {trig}  (t=0 there; {trig*dt*1e3:.2f} ms of "
              f"pre-trigger history)")
        print(f"baseline : {base} codes   peak deviation: {pk} codes "
              f"({pk*ADC_VREF/ADC_FULL*1e3:.1f} mV)")
        if max(r[0] for r in rows) >= 4094 or min(r[0] for r in rows) <= 1:
            print("*** CLIPPED -- back off the stimulus; do not tune a detector on this")

    if args.csv:
        with open(args.csv, "w", encoding="utf-8") as f:
            f.write(",".join(["t_s"] + columns) + "\n")
            for i, r in enumerate(rows):
                f.write(",".join([f"{(i-t0_i)*dt:.9f}"] + [str(v) for v in r]) + "\n")
        print(f"wrote {os.path.abspath(args.csv)}")

    if args.no_plot:
        return

    try:
        import matplotlib.pyplot as plt
    except ImportError:
        sys.exit("matplotlib not installed (use --csv --no-plot, or pip install matplotlib)")

    t = [(i - t0_i) * dt for i in range(len(rows))]
    fig, axes = plt.subplots(nch, 1, sharex=True, figsize=(11, 2.4 * nch), squeeze=False)

    for k in range(nch):
        ax = axes[k][0]
        y = [r[k] for r in rows]
        if args.volts:
            y = [v * ADC_VREF / ADC_FULL for v in y]
        ax.plot(t, y, linewidth=0.8)
        if trig is not None:
            ax.axvline(0.0, color="crimson", linewidth=0.8, alpha=0.7)
        label = columns[k] if k < len(columns) else f"col{k}"
        try:
            chnum = int(label.replace("ch", ""))
            label = f"{label} -- {CH_NAMES.get(chnum, '?')}"
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
