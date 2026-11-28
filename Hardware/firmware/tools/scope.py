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

Needs firmware 1.23 or later (1.17 to 1.22 had it too, but none of them was ever
flashed): every request carries a fresh `id=`, which the board
says back in the record's header, and only that record is taken (review finding
V116-F3). Older firmware ignores the id, so this tool finds no record of its own and
says so. Use it only when nothing else is using the board's console: a byte that
arrives while any command runs -- the Pi's over the link too -- stops that command.

What it cannot tell apart (review note V118-N1, accepted for this bench): an earlier
request with the same command line AND the same id leaves an echo and a record
identical to this one's, and if they are still on the console they are taken. Ids
count up by one from a random start, so a run repeats one only after 2^32 requests,
and two runs share one by chance (1 in 2^32 for any one pending id). This relies on
one supervised user and one program at the console at a time. On Windows a COM port
has one opener; on Linux nothing here stops a second program opening it.
"""

import argparse
import os
import random
import re
import struct
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
    return read_block_on(ser, Request.plain(mask, samples, rate), timeout)


class CaptureRejected(Exception):
    """The board's reply is not one whole, well-formed record of this request, so no
    samples are returned. The message says why."""


ADC_CAPTURE_MAX_SAMPLES = 16384     # src/adc_engine.h
ADC_RATE_MAX_HZ = 500000            # src/adc_engine.h
ADC_CLK_HZ = 48000000.0             # src/adc_engine.c


def _f32(x):
    """x rounded to a C float, as the firmware's single-precision arithmetic rounds it."""
    return struct.unpack("<f", struct.pack("<f", x))[0]


def adc_rate_for(rate_hz):
    """The rate the board's ADC runs at for a requested one, which the header gives:
    src/adc_ring_math.h's adc_div_for_rate() and adc_rate_for_div() and adc_engine.c's
    rounding, step by step in single precision. 0 for a rate it cannot make. A request
    between 494.8 and 500 kHz runs at 494.8 kHz (the 96-cycle floor); elsewhere the
    divider's 8-bit fraction moves it by under 0.01 %."""
    if rate_hz <= 0 or rate_hz > ADC_RATE_MAX_HZ:
        return 0
    clk = _f32(ADC_CLK_HZ)
    d = 0
    if rate_hz < ADC_RATE_MAX_HZ:
        f = _f32(_f32(clk / _f32(rate_hz)) - 1.0)
        if f < 96.0:
            f = 96.0
        if not f < 65536.0:
            return 0
        d = int(_f32(f * 256.0))
    r = _f32(clk / 96.0) if d == 0 else _f32(clk / _f32(1.0 + _f32(d / 256.0)))
    return int(_f32(r + 0.5))


_last_id = [random.getrandbits(32)]


def _new_id():
    """A fresh id: random for the first request of a run, then one more each time,
    modulo 2^32: a run repeats one only after 2^32 requests (image 1.18; image 1.19,
    review note V118-N1: 1.18 said "ever")."""
    _last_id[0] = (_last_id[0] + 1) & 0xFFFFFFFF
    return _last_id[0]


def _trig_offset(n, pre_pct):
    """Where `capture trig` puts the trigger in its record: adc_capture_triggered()'s
    (size_t)(n * pre_frac), with pre_frac = pre / 100.0f, in single precision
    (src/adc_engine.c; the CLI takes 0-90 %)."""
    frac = _f32(_f32(pre_pct) / 100.0)
    return int(_f32(_f32(n) * frac))


class Request:
    """One capture request: the line sent -- ending in a fresh id=0x<8 hex> that the
    board says back in the record's header (firmware 1.17, review finding V116-F3) --
    and the record it must produce: mask, sample count and rate, as the firmware rounds
    them, and for `capture trig` the trigger's offset as the firmware places it.
    `echo` matches this command's own echo at the USB console: the line exactly as
    sent, after any prompts (image 1.18, review finding V117-F2)."""

    def __init__(self, words, mask, n, rate, trig_at=None, ident=None):
        self.ident = _new_id() if ident is None else ident
        self.token = f"id=0x{self.ident:08x}"           # as sent, and so as echoed
        self.command = f"{words} {self.token}"
        self.echo = re.compile(r"^(?:>\s*)*" + re.escape(self.command) + r"$")
        self.mask, self.n, self.rate = mask, n, rate
        self.trig = trig_at is not None
        self.trig_at = trig_at

    @classmethod
    def plain(cls, mask, samples, rate, ident=None):
        """`capture`: the count is cut to the buffer, then down to whole rows of the
        mask's channels (adc_capture())."""
        nch = bin(mask & 0xFF).count("1")
        n = min(samples, ADC_CAPTURE_MAX_SAMPLES)
        if nch:
            n -= n % nch
        return cls(f"capture 0x{mask:02x} {samples} {rate}", mask, n, adc_rate_for(rate),
                   ident=ident)

    @classmethod
    def triggered(cls, ch, threshold, rate, pre_pct, timeout_s, ident=None):
        """`capture trig`: one channel, always the whole buffer, with trig= at the
        pre-trigger share of it and base= a 12-bit ADC code."""
        return cls(f"capture trig {ch} {threshold} {rate} {pre_pct} {timeout_s}", 1 << ch,
                   ADC_CAPTURE_MAX_SAMPLES, adc_rate_for(rate),
                   trig_at=_trig_offset(ADC_CAPTURE_MAX_SAMPLES, pre_pct), ident=ident)

    def owns(self, header_line):
        return _header_fields(header_line).get("id") == f"{self.ident:08x}"


def _header_fields(line):
    """The key=value pairs of a `# capture ...` line."""
    fields = {}
    for tok in line.split()[2:]:
        if "=" in tok:
            k, v = tok.split("=", 1)
            fields[k] = v
    return fields


def _checked_header(line, req):
    """The header of this request's record, checked against what was asked."""
    header = _header_fields(line)
    try:
        want = int(header["n"])
        mask = int(header["mask"], 16)
        rate = int(header["rate"])
        if want < 1 or rate < 1:
            raise ValueError
    except (KeyError, ValueError):
        raise CaptureRejected(f"a header without a usable n, mask or rate: {line}")
    if mask != req.mask:
        raise CaptureRejected(f"the record is of mask 0x{mask:02x}, not the 0x{req.mask:02x} "
                              f"asked for: {line}")
    if want != req.n:
        raise CaptureRejected(f"the record declares {want} samples where this request makes "
                              f"{req.n}: {line}")
    if abs(rate - req.rate) > 1:
        raise CaptureRejected(f"the record's rate is {rate} Hz where this request runs at "
                              f"{req.rate} Hz: {line}")
    if req.trig:
        # The trigger where this request's pre-trigger share puts it, and a baseline
        # that is an ADC code (image 1.18, review note V117-N3: 1.17 took any trig in
        # the record and any base from 0 up, and the plot is placed by both).
        try:
            trig, base = int(header["trig"]), int(header["base"])
        except (KeyError, ValueError):
            raise CaptureRejected(f"a triggered record without a usable trig and base: {line}")
        if trig != req.trig_at:
            raise CaptureRejected(f"the record puts its trigger at {trig}, where this request's "
                                  f"pre-trigger share puts it at {req.trig_at}: {line}")
        if not 0 <= base <= 4095:
            raise CaptureRejected(f"the record's baseline {base} is not an ADC code (0-4095): "
                                  f"{line}")
    elif "trig" in header:
        raise CaptureRejected(f"a triggered record for a plain capture: {line}")
    return header


def read_block_on(ser, req, timeout=60.0):
    """Send one Request and return its `# capture` ... `# end` CSV block as
    (header, columns, rows): ONLY a whole record OF THIS REQUEST, or CaptureRejected.

    THIS REQUEST'S (image 1.17, review finding V116-F3; image 1.18, V117-F2): the first
    record after THIS COMMAND'S OWN ECHO at the USB console -- the line exactly as sent,
    after any prompts -- and its header must say back the request's id. Until 1.16 the
    first well-formed block after the request was taken, and the console carries
    others: the Pi's commands print there too, and a record still draining from an
    earlier request looks like the next one. 1.17 took any header with the id, even
    before the echo, and counted any line holding the id as the echo, a `[pi] ...`
    one too: a record from before, with the id reused, was taken. Now:
    - everything before the echo is passed over, records included, whatever their id
      (said, not used);
    - a line holding the id that is not a clean echo, and not the Pi's, means this
      command did not reach the board as one line: rejected;
    - after the echo, the first record must carry the id, else it is rejected;
    - no echo by the timeout: rejected.
    An earlier request's whole transaction with the same line and the same id is the
    one thing this cannot tell from this one (module docstring; review note V118-N1).
    The header must also give the mask asked for, the count and rate as the firmware
    rounds them (Request), and for `capture trig` only, the trigger's offset as its
    pre-trigger share puts it and a 12-bit baseline. The board's refusals count only
    after the echo: an earlier REFUSED was another command's.

    WHOLE means (image 1.16, review finding V115-F5): a header with n, mask and
    rate; a `# columns:` line naming exactly the mask's channels; exactly n
    samples, in rows of exactly that many values, each 0-4095; then an end line
    that is exactly `# end`. A `# end PARTIAL ...` (a stop cut the print-out short),
    a missing or extra row, a short or long row, a stray line inside the data, or
    no end line before the timeout: all rejected. An `ERR` or `usage:` line exits.

    LINES ARE WHOLE (image 1.17, review finding V116-F2): readline() returns what it
    has when the port's timeout runs out, which can be part of a line. 1.16 took each
    piece for a line, and a row `1234` cut by a pause came back as rows `12` and `34`,
    making a record one row short look whole. Pieces are joined until a line ends.

    The command ends in ONE terminator, CR (V115-F1): the board runs a command on
    the CR, and the LF of the CR LF this sent until 1.15 was still waiting --
    firmware 1.15 read it as a stop, and a 4096-sample capture ended after one
    sample. Firmware 1.16 takes CR LF as one Enter; either way, one byte is sent.
    """
    ser.reset_input_buffer()
    ser.write((req.command + "\r").encode())

    header, columns, rows = None, None, []
    nch = want = 0
    ours = False          # this command's echo has gone by: what follows is its output
    others = 0            # records of other commands passed over
    piece = b""           # a line not yet ended
    deadline = time.time() + timeout

    while time.time() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        piece += raw
        if not piece.endswith(b"\n"):
            continue                                  # cut by the read timeout: more to come
        line = piece.decode("utf-8", "replace").strip()
        piece = b""
        if not line:
            continue

        if header is None:
            if not ours:
                # Before this command's echo: the prompt, other commands' output and
                # records -- one carrying this id too, if an id was ever reused.
                if req.echo.match(line):
                    ours = True
                elif req.token in line and not line.startswith("[pi]"):
                    raise CaptureRejected(f"this command's echo is not one clean line -- a "
                                          f"command already running took its first byte as a "
                                          f"stop, or other output cut into it: {line}")
                elif line.startswith("# capture"):
                    others += 1
                    print(f"  (passed over a record from before this command's echo: {line})")
                continue
            if line.startswith("# capture"):
                if not req.owns(line):
                    if "id" not in _header_fields(line):
                        raise CaptureRejected(f"a record without an id, after this command: the "
                                              f"board's firmware is older than 1.23, which says "
                                              f"the id back: {line}")
                    raise CaptureRejected(f"a record that is not this request's, after its "
                                          f"command: {line}")
                header = _checked_header(line, req)
                want = req.n
                continue
            if line.startswith("ERR") or line.startswith("usage:"):
                sys.exit(f"board said: {line}")
            if (line.startswith("REFUSED") or line.startswith("NO TRIGGER")
                    or line.startswith("aborted") or line.startswith("? '")):
                raise CaptureRejected(f"board said: {line}")
            if line.startswith("armed:") or line.startswith("window"):
                print(f"  {line}")
            continue

        if columns is None:
            if not line.startswith("# columns:"):
                raise CaptureRejected(f"no `# columns:` line after the header: {line}")
            columns = line.split(":", 1)[1].split()
            named = [f"ch{c}" for c in range(8) if req.mask & (1 << c)]
            if not columns or columns != named:
                raise CaptureRejected(f"columns {columns} are not the mask's {named}")
            nch = len(columns)
            if want % nch:
                raise CaptureRejected(f"n={want} is not a whole number of {nch}-channel rows")
            continue

        if line.startswith("#"):
            if line == "# end":
                if len(rows) * nch != want:
                    raise CaptureRejected(f"the end line came after {len(rows) * nch} of "
                                          f"{want} samples")
                return header, columns, rows
            raise CaptureRejected(f"board said: {line}")      # # end PARTIAL -- ..., or other
        values = line.split(",")
        try:
            row = [int(v) for v in values]
        except ValueError:
            raise CaptureRejected(f"a line inside the data that is not a row of samples: {line}")
        if len(row) != nch or any(v < 0 or v > 4095 for v in row):
            raise CaptureRejected(f"row {len(rows) + 1} is not {nch} samples of 0-4095: {line}")
        rows.append(row)
        if len(rows) * nch > want:
            raise CaptureRejected(f"more than the {want} samples the header declared")

    got = len(rows) * nch
    cut = f" (and a line cut short: {piece[:40]!r})" if piece else ""
    if header is not None:
        raise CaptureRejected("no end line: %d of %d samples before the %.0f s timeout%s"
                              % (got, want, timeout, cut))
    if not ours:
        raise CaptureRejected("no echo of this command before the %.0f s timeout%s%s" % (
            timeout, cut, ("; %d record(s) from before it passed over" % others) if others else ""))
    raise CaptureRejected("no `# capture` block after this command before the %.0f s timeout%s"
                          % (timeout, cut))


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
                try:
                    hdr, cols, rows = read_capture_on(ser, mask, args.samples,
                                                      args.rate, timeout=30.0)
                except CaptureRejected as e:
                    print(f"  (block rejected: {e})")
                    continue
                t1 = time.time()

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
        req = Request.triggered(args.channel, args.trig, args.rate, int(args.pre * 100),
                                int(args.trig_timeout))
        print(f"> {req.command}")
        with serial.Serial(args.port, args.baud, timeout=1.0) as ser:
            time.sleep(0.3)
            try:
                header, columns, rows = read_block_on(
                    ser, req, timeout=args.trig_timeout + 30.0)
            except CaptureRejected as e:
                sys.exit(f"no record: {e}")
        plot_block(args, header, columns, rows)
        return

    try:
        header, columns, rows = read_capture(args.port, args.baud, mask,
                                             args.samples, args.rate)
    except CaptureRejected as e:
        sys.exit(f"no record: {e} -- check the port, and that the firmware is running")
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
