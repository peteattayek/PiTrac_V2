#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
"""
pilot_analysis.py -- summarise one triggered ADC5 ball-pass capture (BENCH_P3 3.7).

Input: a serial log (or CSV) holding exactly ONE `capture trig 5 ...` block, e.g. the
board-3 pilot of 2026-09-18 (`capture trig 5 64 100000 25 10`). It validates the block
(sample count, termination, 12-bit range), then reports:
  - mean / sigma / min / max in windows around the trigger (-40..-10, -10..-1, 0..10,
    100..120 ms), plus 10 ms windows from -40 to +120 ms;
  - the peak (absolute, and above the -40..-10 ms baseline) and its time;
  - a FWHM on a 0.2 ms box average (an explicitly bandwidth-limited width);
  - samples at the rails (<= 1 code, >= 4080 codes) as a clipping check.
Codes are converted at the nominal 3300 mV / 4095.

    python tools/pilot_analysis.py captures/<log>.txt

Writes <input-stem>-pilot.json and <input-stem>-pilot.svg next to the input. Kept for the
3.7 20-pass set (PROGRESS 9). The 2026-09-18 pilot gave 3.102 V peak, ~22.7 ms FWHM.
"""
import json
import math
from pathlib import Path
import statistics
import sys


def read_capture(path):
    captures = []
    current = None
    with path.open(encoding="utf-8-sig") as stream:
        for line_number, raw in enumerate(stream, 1):
            line = raw.strip()
            if line.startswith("# capture "):
                if current is not None:
                    raise ValueError(f"Nested capture at line {line_number}")
                header = dict(token.split("=", 1) for token in line.split()[2:])
                current = {"header": header, "samples": [], "line": line_number}
            elif current is not None:
                if line == "# end":
                    assert len(current["samples"]) == int(current["header"]["n"])
                    captures.append(current)
                    current = None
                elif line.startswith("# columns:"):
                    assert line == "# columns: ch5"
                else:
                    value = int(line)
                    assert 0 <= value <= 4095
                    current["samples"].append(value)
    assert current is None
    assert len(captures) == 1
    return captures[0]


def stats(values):
    return {
        "n": len(values),
        "mean_codes": statistics.fmean(values),
        "sigma_codes": statistics.pstdev(values),
        "min_codes": min(values),
        "max_codes": max(values),
    }


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: pilot_analysis.py <serial log or CSV with one 'capture trig 5' block>")
    source = Path(sys.argv[1])
    output = source.parent
    stem = source.stem
    record = read_capture(source)
    header = record["header"]
    samples = record["samples"]
    rate = int(header["rate"])
    trigger = int(header["trig"])
    factor = 3300 / 4095

    def interval(lo_ms, hi_ms):
        start = max(0, math.ceil(trigger + lo_ms * rate / 1000))
        stop = min(len(samples), math.ceil(trigger + hi_ms * rate / 1000))
        assert stop > start
        return samples[start:stop]

    windows = {}
    for lo, hi in [(-40, -10), (-10, -1), (0, 10), (100, 120)]:
        windows[f"{lo}_to_{hi}_ms"] = stats(interval(lo, hi))

    overall = stats(samples)
    peak_index = max(range(len(samples)), key=samples.__getitem__)
    baseline = windows["-40_to_-10_ms"]["mean_codes"]
    summary = {
        "source": str(source),
        "header": header,
        "integrity": "count and termination validated",
        "duration_ms": len(samples) / rate * 1000,
        "nominal_mV_per_code": factor,
        "overall": overall,
        "windows": windows,
        "peak_time_ms": (peak_index - trigger) / rate * 1000,
        "peak_absolute_mV": samples[peak_index] * factor,
        "peak_above_local_baseline_mV": (samples[peak_index] - baseline) * factor,
        "sample_at_trigger": samples[trigger],
        "floor_samples_le_1": sum(value <= 1 for value in samples),
        "high_samples_ge_4080": sum(value >= 4080 for value in samples),
    }

    # A short box average gives a separate, explicitly bandwidth-limited width.
    smoothing = max(1, round(rate * 0.0002))
    smoothed = [
        statistics.fmean(samples[start:start + smoothing])
        for start in range(0, len(samples) - smoothing + 1, smoothing)
    ]
    smooth_times = [
        (i * smoothing + (smoothing - 1) / 2 - trigger) * 1000 / rate
        for i in range(len(smoothed))
    ]
    top = max(range(len(smoothed)), key=smoothed.__getitem__)
    half = baseline + (smoothed[top] - baseline) / 2
    left = top
    right = top
    while left > 0 and smoothed[left] >= half:
        left -= 1
    while right < len(smoothed) - 1 and smoothed[right] >= half:
        right += 1
    if smoothed[left] < half and smoothed[right] < half:
        def crossing(a, b):
            fraction = (half - smoothed[a]) / (smoothed[b] - smoothed[a])
            return smooth_times[a] + fraction * (smooth_times[b] - smooth_times[a])

        rise = crossing(left, left + 1)
        fall = crossing(right - 1, right)
        summary["box_0_2ms_fwhm"] = {
            "rise_ms": rise, "fall_ms": fall, "width_ms": fall - rise,
            "baseline_codes": baseline, "half_level_codes": half,
        }
    else:
        summary["box_0_2ms_fwhm"] = "both crossings not contained"

    print(json.dumps(summary, indent=2))
    print("\n10 ms windows: start_ms, mean_mV, min_mV, max_mV")
    for lo in range(-40, 120, 10):
        values = interval(lo, lo + 10)
        print(f"{lo:4d}, {statistics.fmean(values)*factor:9.3f}, "
              f"{min(values)*factor:9.3f}, {max(values)*factor:9.3f}")

    json_path = output / f"{stem}-pilot.json"
    json_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    width, height = 1100, 490
    xmin, xmax = -trigger / rate * 1000, (len(samples) - 1 - trigger) / rate * 1000
    ymin, ymax = 0.0, max(100.0, math.ceil(max(samples) * factor / 100) * 100)

    def x(t):
        return 85 + (t - xmin) / (xmax - xmin) * 975

    def y(v):
        return 405 - (v - ymin) / (ymax - ymin) * 340

    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        '<g font-family="Arial, sans-serif" font-size="13" fill="#222">',
        f'<text x="85" y="25" font-size="19">ADC5 pass - {stem}, {rate // 1000} ksps</text>',
        '<text x="85" y="46">10-sample min/max envelope (0.1 ms bins); green = pre-event baseline; dashed = trigger time</text>',
    ]
    for step in range(6):
        value = ymax * step / 5
        yy = y(value)
        svg += [
            f'<line x1="85" x2="1060" y1="{yy:.2f}" y2="{yy:.2f}" stroke="#ddd"/>',
            f'<text x="75" y="{yy+4:.2f}" text-anchor="end">{value:.0f}</text>',
        ]
    for time_ms in range(-40, 121, 20):
        xx = x(time_ms)
        svg += [
            f'<line x1="{xx:.2f}" x2="{xx:.2f}" y1="65" y2="405" stroke="#eee"/>',
            f'<text x="{xx:.2f}" y="427" text-anchor="middle">{time_ms}</text>',
        ]
    path = []
    for start in range(0, len(samples), 10):
        block = samples[start:start + 10]
        xx = x((start + (len(block) - 1) / 2 - trigger) / rate * 1000)
        path.append(f'M{xx:.2f},{y(min(block)*factor):.2f}V{y(max(block)*factor):.2f}')
    svg += [
        f'<path d="{" ".join(path)}" stroke="#245db0" fill="none" stroke-width="1"/>',
        f'<line x1="85" x2="1060" y1="{y(baseline*factor):.2f}" y2="{y(baseline*factor):.2f}" stroke="#258340"/>',
        f'<line x1="{x(0):.2f}" x2="{x(0):.2f}" y1="65" y2="405" stroke="#bb3333" stroke-dasharray="5,4"/>',
        '<text x="540" y="457" text-anchor="middle">Time relative to ADC trigger (ms)</text>',
        '<text x="20" y="245" transform="rotate(-90 20 245)" text-anchor="middle">ADC5 (mV, nominal 3.3 V reference)</text>',
        '</g></svg>',
    ]
    svg_path = output / f"{stem}-pilot.svg"
    svg_path.write_text("\n".join(svg), encoding="utf-8")
    print(f"\nSaved {json_path}\nSaved {svg_path}")


if __name__ == "__main__":
    main()
