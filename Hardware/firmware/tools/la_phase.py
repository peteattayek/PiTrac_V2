#!/usr/bin/env python3
"""
la_phase.py -- analyse a 2-channel logic-analyser CSV of the beam carrier and
demodulator clock (BENCH_P2_BEAM.md Phase 2a, checks 1-7).

Expects a transition-logged CSV as exported by most LAs:

    Time [s],Channel 0,Channel 1
    9.769041591,0,1
    ...

Channels are auto-identified by duty cycle: the ~50 % one is the demod clock
(GPIO39), the narrow one is the carrier (GPIO31). Order on the LA does not
matter.

Usage
-----
    python la_phase.py digital.csv                  # report what you measured
    python la_phase.py digital.csv --ticks 720      # and check against a
                                                    # commanded 'beam phase'

    # check 7 -- reproducibility across reconfiguration:
    python la_phase.py run*.csv --ticks 0 --compare

Sign convention
---------------
beam.c preloads the demod counter to (period - phase), so the DEMOD rising edge
arrives `phase` ticks AFTER the carrier's. This tool therefore measures

    offset = (demod_rise - carrier_rise)  mod  period

which compares directly against the commanded `beam phase <ticks>`. All maths is
circular: at phase 0 and phase TOP the offset sits either side of the wrap, and
at phase TOP/2 the two edges are equidistant, so a naive "nearest edge" search
gives nonsense at exactly the values you most want to check.

Why the pass criterion is "spread < 1 tick"
-------------------------------------------
An LA samples asynchronously, so every edge is quantised to a sample boundary
and the measured offset dithers by +/-1 sample even when the hardware lock is
exact. What matters is whether the scatter is bounded by the sample period
(quantisation, fine) or is much larger (the two PWM slices are not starting on
the same clock edge, which would invalidate every Phase 3 phase calibration).
"""

import argparse
import csv
import sys
from collections import Counter

SYSCLK_HZ = 150_000_000
TICK_NS = 1e9 / SYSCLK_HZ          # 6.667 ns


def _wrap_half(x, period):
    """Wrap x into [-period/2, +period/2)."""
    return (x + period / 2.0) % period - period / 2.0


def _circular_mean(vals, period):
    """Mean of values living on a circle of circumference `period`, in [0, period)."""
    import math
    a = [2 * math.pi * v / period for v in vals]
    c = sum(math.cos(x) for x in a) / len(a)
    s_ = sum(math.sin(x) for x in a) / len(a)
    return (math.atan2(s_, c) % (2 * math.pi)) / (2 * math.pi) * period


def load(path):
    rows = []
    with open(path, newline="") as f:
        r = csv.reader(f)
        header = next(r)
        for rec in r:
            if len(rec) < 3:
                continue
            try:
                rows.append((float(rec[0]), int(rec[1]), int(rec[2])))
            except ValueError:
                continue
    if len(rows) < 8:
        sys.exit(f"{path}: not enough transitions ({len(rows)})")
    return header, rows


def edges(rows, ch, rising=True):
    """Timestamps of rising (or falling) edges on channel ch (1 or 2)."""
    out = []
    prev = rows[0][ch]
    for rec in rows[1:]:
        v = rec[ch]
        if rising and prev == 0 and v == 1:
            out.append(rec[0])
        elif not rising and prev == 1 and v == 0:
            out.append(rec[0])
        prev = v
    return out


def high_times(rise, fall):
    out = []
    fi = 0
    for t in rise:
        while fi < len(fall) and fall[fi] <= t:
            fi += 1
        if fi < len(fall):
            out.append(fall[fi] - t)
    return out


def summarise(path, want_ticks=None, quiet=False):
    _, rows = load(path)
    span_ms = (rows[-1][0] - rows[0][0]) * 1e3

    ch = {}
    for c in (1, 2):
        r, f = edges(rows, c, True), edges(rows, c, False)
        if len(r) < 2:
            sys.exit(f"{path}: channel {c-1} has no activity -- check probes and ground")
        per = [b - a for a, b in zip(r, r[1:])]
        hi = high_times(r, f)
        mean_per = sum(per) / len(per)
        ch[c] = dict(rise=r, per=per, hi=hi, mean_per=mean_per,
                     duty=(sum(hi) / len(hi)) / mean_per if hi else 0.0)

    # 50 %-ish channel is the demod clock; the other is the carrier.
    dem = 1 if abs(ch[1]["duty"] - 0.5) < abs(ch[2]["duty"] - 0.5) else 2
    car = 2 if dem == 1 else 1

    # LA sample period, inferred from the finest timestamp granularity present.
    alldt = sorted({round(b - a, 12) for a, b in zip([r[0] for r in rows],
                                                     [r[0] for r in rows[1:]])
                    if b > a})
    samp_ns = alldt[0] * 1e9 if alldt else float("nan")

    # phase: (demod_rise - carrier_rise) mod period, in ns.
    # Modular by construction so phase 0 / TOP/2 / TOP all behave.
    per_ns = ch[car]["mean_per"] * 1e9
    dr = ch[dem]["rise"]
    off = []
    for t in ch[car]["rise"]:
        nearest = min(dr, key=lambda x: abs(x - t))
        off.append(((nearest - t) * 1e9) % per_ns)

    mean_off = _circular_mean(off, per_ns)
    devs = [_wrap_half(o - mean_off, per_ns) for o in off]
    spread = max(devs) - min(devs)
    off_sorted = sorted(off)

    if not quiet:
        print(f"\n=== {path} ===")
        print(f"  {len(rows)} transitions over {span_ms:.3f} ms   "
              f"LA sample period ~{samp_ns:.1f} ns")
        print(f"  channel {car-1} = CARRIER (GPIO31)   "
              f"channel {dem-1} = DEMOD (GPIO39)")

        for name, c in (("carrier", car), ("demod", dem)):
            d = ch[c]
            hi = sorted(d["hi"])
            per = sorted(d["per"])
            print(f"  {name:<8} period {d['mean_per']*1e9:9.2f} ns "
                  f"(spread {(per[-1]-per[0])*1e9:5.1f})   "
                  f"f = {1/d['mean_per']:10.1f} Hz")
            print(f"           high   {sum(hi)/len(hi)*1e9:9.2f} ns "
                  f"(spread {(hi[-1]-hi[0])*1e9:5.1f})   "
                  f"duty = {d['duty']*100:6.3f} %  "
                  f"= {sum(hi)/len(hi)*1e9/TICK_NS:7.1f} ticks")

        print(f"  PHASE (demod_rise - carrier_rise) mod period, n={len(off)}")
        print(f"           min {off_sorted[0]:8.1f}  max {off_sorted[-1]:8.1f}  "
              f"circular mean {mean_off:8.2f} ns = {mean_off/TICK_NS:7.2f} ticks")
        print(f"           spread {spread:.1f} ns = {spread/TICK_NS:.2f} ticks "
              f"= {spread/samp_ns:.1f} LA samples")
        hist = dict(sorted(Counter(round(x, 1) for x in off).items()))
        print(f"           histogram {hist}")

        verdict = "PASS" if spread < TICK_NS else "FAIL"
        print(f"           spread < 1 tick ({TICK_NS:.2f} ns)?  {verdict}")
        if verdict == "FAIL":
            print("           ^ the two slices are NOT starting on the same clock")
            print("             edge. Phase 3 calibration would be built on sand.")

        if want_ticks is not None:
            per_ns = ch[car]["mean_per"] * 1e9
            expect_ns = (want_ticks * TICK_NS) % per_ns
            err = _wrap_half(mean_off - expect_ns, per_ns)
            print(f"  COMMANDED phase {want_ticks} ticks = {expect_ns:.1f} ns")
            print(f"           measured {mean_off:+.2f} ns   error {err:+.2f} ns "
                  f"= {err/TICK_NS:+.2f} ticks   "
                  f"{'OK' if abs(err) < TICK_NS else '*** OFF BY MORE THAN A TICK ***'}")
            if want_ticks:
                print(f"           implied tick scale = {expect_ns and mean_off/want_ticks:.4f} "
                      f"ns/tick (nominal {TICK_NS:.4f})")

    return dict(path=path, mean=mean_off, spread=spread, samp_ns=samp_ns,
                period=per_ns)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv", nargs="+")
    ap.add_argument("--ticks", type=int, default=None,
                    help="commanded 'beam phase' value, to check against")
    ap.add_argument("--compare", action="store_true",
                    help="check 7: compare the offset across several captures")
    a = ap.parse_args()

    res = [summarise(p, a.ticks, quiet=False) for p in a.csv]

    if a.compare and len(res) > 1:
        # Circular here too: two captures sitting either side of the wrap
        # (0.0 ns and period-0.8 ns) are 0.8 ns apart, not a whole period.
        per = sum(r["period"] for r in res) / len(res)
        means = [r["mean"] for r in res]
        ref = _circular_mean(means, per)
        devs = [_wrap_half(m - ref, per) for m in means]
        spread = max(devs) - min(devs)
        samp = max(r["samp_ns"] for r in res)
        print("\n=== CHECK 7 -- reproducibility across reconfiguration ===")
        import os
        for r, d in zip(res, devs):
            print(f"  {os.path.basename(os.path.dirname(r['path'])) or r['path']:<24}"
                  f" offset {r['mean']:9.2f} ns   ({d:+6.2f} ns from the group mean)")
        print(f"  run-to-run spread {spread:.2f} ns = {spread/TICK_NS:.2f} ticks "
              f"= {spread/samp:.1f} LA samples")
        if spread < TICK_NS:
            print("  PASS -- the atomic enable is working; the phase relationship")
            print("         is reproducible to better than one 6.67 ns tick.")
        else:
            print("  *** FAIL -- the offset moves between reconfigurations. The two")
            print("      slices are not being enabled on the same clock edge, so")
            print("      every Phase 3 phase calibration would be invalid.")
    elif a.compare:
        print("\n--compare needs more than one CSV")


if __name__ == "__main__":
    main()
