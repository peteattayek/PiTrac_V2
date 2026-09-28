# SPDX-License-Identifier: GPL-3.0-or-later
"""Mira220 above trailing and overview waveforms, rendered directly at 60 fps."""
from __future__ import annotations

import argparse
from bisect import bisect_left
import csv
from fractions import Fraction
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys

import av
from PIL import Image, ImageDraw

from .beam_composite import Layout, Trace, native_gray, window_bounds
from .convert import ConversionError, load_run, sha256, write_json
from .deflicker import IlluminationCorrection

FPS = 60
TIME_BASE = Fraction(1, FPS)


def nearest_indices(timestamps: tuple[int, ...], seconds: float,
                    sampling_phase_ms: float = 0) -> tuple[int, ...]:
    if (not math.isfinite(seconds) or not 0 < seconds <= 30
            or not math.isfinite(sampling_phase_ms) or not 0 <= sampling_phase_ms < 1000 / FPS):
        raise ConversionError("Invalid duration or 60 fps source sampling phase")
    count = round(seconds * FPS)
    if not count or not math.isclose(count / FPS, seconds, rel_tol=0, abs_tol=1e-9):
        raise ConversionError("Duration must contain a whole number of 60 fps frames")
    if (len(timestamps) < 2 or any(b <= a for a, b in zip(timestamps, timestamps[1:]))
            or timestamps[0] + seconds * 1e6 > timestamps[-1]):
        raise ConversionError("Ordered source timestamps must cover the full requested duration")
    phase = Fraction(str(sampling_phase_ms)) * 1000
    indices = []
    for frame in range(count):
        target = timestamps[0] + Fraction(frame * 1_000_000, FPS) + phase
        right = bisect_left(timestamps, target)
        candidates = range(max(0, right - 1), min(len(timestamps), right + 1))
        indices.append(min(candidates, key=lambda index: (abs(timestamps[index] - target), index)))
    if len(set(indices)) != len(indices):
        raise ConversionError("Source cadence cannot supply a distinct nearest frame for every 60 fps tick")
    return tuple(indices)


def render(run: Path, cache: Path, output: Path, offset: float, seconds: float = 30,
           width: int = 1280, sampling_phase_ms: float = 0,
           deflicker_model: Path | None = None,
           voltage_max: float | None = None) -> dict[str, object]:
    if not math.isfinite(offset) or not 640 <= width <= 1600 or width % 2:
        raise ConversionError("Invalid analyzer offset or output width")
    records = {record.name: record for record in load_run(run)}
    if "mira220" not in records:
        raise ConversionError("A verified Mira220 raw recording is required")
    mira = records["mira220"]
    correction = IlluminationCorrection(deflicker_model, mira) if deflicker_model is not None else None
    indices = nearest_indices(mira.timestamps_us, seconds, sampling_phase_ms)
    origin = mira.timestamps_us[0]
    trace = Trace(cache)
    window = 1.0
    analyzer_range = (offset, offset + seconds)
    if analyzer_range[0] - window < trace.start or analyzer_range[1] > trace.end:
        raise ConversionError("Analyzer CSV must cover the video and its preceding one-second history")
    layout = Layout(records, width, window, analyzer_range, mira_only=True,
                    hide_detail_cursor=True, window_position="right", mira_first=True,
                    voltage_max=voltage_max)
    output.mkdir(mode=0o700, parents=False, exist_ok=False)
    (output / "status").write_text("INCOMPLETE\n")
    temporary = output / "beam-composite-60fps.incomplete.mp4"
    mapping = output / "frame-map.tsv"
    initial_raw, initial_cache = mira.raw.stat(), cache.stat()
    max_error_ms = 0.0
    try:
        with mira.raw.open("rb") as raw, mapping.open("x", encoding="utf-8", newline="") as table:
            writer = csv.writer(table, delimiter="\t")
            writer.writerow(["output_frame", "output_time_s", "sampling_camera_time_s",
                             "mira220_frame", "source_timestamp_us", "camera_time_s",
                             "analyzer_time_s", "window_start_analyzer_s", "window_end_analyzer_s",
                             "source_raw_frame_sha256"])
            with av.open(str(temporary), "w", options={"video_track_timescale": "60000",
                                                      "movflags": "+faststart"}) as container:
                stream = container.add_stream("libx264", rate=FPS)
                stream.width, stream.height, stream.pix_fmt = layout.width, layout.height, "yuv420p"
                stream.time_base = stream.codec_context.time_base = TIME_BASE
                stream.codec_context.max_b_frames = 0
                stream.codec_context.thread_count = 3
                stream.options = {"crf": "18", "preset": "veryfast"}
                for frame_number, source_index in enumerate(indices):
                    timestamp = mira.timestamps_us[source_index]
                    camera_time = (timestamp - origin) / 1e6
                    sample_time = frame_number / FPS + sampling_phase_ms / 1000
                    max_error_ms = max(max_error_ms, abs(camera_time - sample_time) * 1000)
                    analyzer_time = camera_time + offset
                    window_start, window_end = window_bounds(analyzer_time, window, "right")
                    raw.seek(source_index * mira.sizeimage)
                    data = raw.read(mira.sizeimage)
                    with native_gray(mira, data) as native:
                        if correction is not None:
                            correction.apply(native, source_index)
                        camera = native.resize((layout.width, layout.bottom_height), Image.Resampling.LANCZOS)
                    with Image.new("RGB", (layout.width, layout.height), "#14202d") as canvas:
                        canvas.paste(camera, (0, layout.header))
                        camera.close()
                        with layout.plot(trace, analyzer_time, camera_time) as plot:
                            canvas.paste(plot, (0, layout.plot_y))
                        with layout.overview(trace, analyzer_time) as overview:
                            canvas.paste(overview, (0, layout.overview_y))
                        ImageDraw.Draw(canvas).text(
                            (14, 10), f"MIRA220  |  native {mira.width}x{mira.height} {mira.fourcc}"
                            f"  |  frame {source_index}  |  camera {camera_time:.6f} s  |  60 fps"
                            + ("  |  deflickered" if correction is not None else ""),
                            font=layout.font, fill="#f5f9ff")
                        frame = av.VideoFrame.from_image(canvas)
                        frame.pts, frame.time_base = frame_number, TIME_BASE
                        for packet in stream.encode(frame):
                            container.mux(packet)
                        if frame_number == 0:
                            canvas.save(output / "layout-first-frame.jpg", quality=95)
                    writer.writerow([frame_number, f"{frame_number / FPS:.9f}", f"{sample_time:.9f}",
                                     source_index, timestamp, f"{camera_time:.9f}", f"{analyzer_time:.9f}",
                                     f"{window_start:.9f}", f"{window_end:.9f}",
                                     hashlib.sha256(data).hexdigest()])
                    if frame_number % 300 == 0:
                        print(f"Mira60 {frame_number + 1}/{len(indices)} frames", flush=True)
                for packet in stream.encode(None):
                    container.mux(packet)
            table.flush()
            os.fsync(table.fileno())
        probe = subprocess.run(
            ["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_streams", "-show_packets",
             "-show_entries", "stream=width,height,avg_frame_rate,time_base,nb_frames,duration:packet=pts,duration",
             "-of", "json", str(temporary)],
            capture_output=True, text=True, check=True, timeout=120)
        report = json.loads(probe.stdout)
        info, packets = report["streams"][0], report["packets"]
        time_base = Fraction(info["time_base"])
        if (Fraction(info["avg_frame_rate"]) != FPS or len(packets) != len(indices)
                or int(info["nb_frames"]) != len(indices)
                or (info["width"], info["height"]) != (layout.width, layout.height)
                or not math.isclose(float(info["duration"]), len(indices) / FPS, rel_tol=0, abs_tol=1e-6)):
            raise ConversionError("Encoded stream dimensions, frame count, rate or duration changed")
        for index, packet in enumerate(packets):
            if (int(packet["pts"]) * time_base != index * TIME_BASE
                    or int(packet["duration"]) * time_base != TIME_BASE):
                raise ConversionError(f"Frame {index} failed exact 60 fps cadence verification")
        for path, original in ((mira.raw, initial_raw), (cache, initial_cache)):
            current = path.stat()
            if (original.st_size, original.st_mtime_ns) != (current.st_size, current.st_mtime_ns):
                raise ConversionError(f"Input changed during rendering: {path}")
        final = output / "beam-composite-60fps.mp4"
        temporary.rename(final)
        metadata: dict[str, object] = {
            "file": final.name, "source_run": str(run), "source_frames": len(mira.timestamps_us),
            "source_raw_bytes": initial_raw.st_size, "source_raw_mtime_ns": initial_raw.st_mtime_ns,
            "reference_origin_us": origin, "output_fps": FPS, "output_frames": len(indices),
            "duration_seconds": len(indices) / FPS, "output_width": layout.width, "output_height": layout.height,
            "panels": ["mira220", "scrolling_detail", "full_video_overview"],
            "frame_index_convention": "zero-based",
            "source_sampling_phase_ms": sampling_phase_ms,
            "frame_selection": "nearest actual source timestamp at phase + output_frame/60; earlier frame wins ties",
            "camera_interpolation": "none; native frames omitted, never blended or repeated",
            "maximum_sampling_error_ms": max_error_ms,
            "plot_clock": "selected raw frame timestamp, not nominal output sampling time",
            "analyzer_time_equation": f"(source_timestamp_us - {origin}) / 1000000 + {offset:.9f}",
            "offset_seconds": offset, "plot_window_seconds": window, "window_position": "right",
            "plot_voltage_max": voltage_max,
            "voltage_out_of_range": "clipped at plot boundaries" if voltage_max is not None else "automatic upper limit",
            "overview_analyzer_range_seconds": analyzer_range,
            "overview_selection": "dotted trailing-window boundaries; clipped at overview edges",
            "pixel_mapping": ("native GREY plus display-only additive illumination correction; aspect-preserving LANCZOS resize"
                              if correction is not None else
                              "native linear GREY; aspect-preserving LANCZOS viewing resize; no brightness gain or normalization"),
            "deflicker": correction.metadata if correction is not None else None,
            "alignment_uncertainty": "event-based, limited by camera frame spacing/exposure; not hardware synchronization",
            "trace": trace.metadata, "trace_cache_sha256": sha256(cache),
            "output_sha256": sha256(final), "frame_map_sha256": sha256(mapping),
        }
        write_json(output / "composite-60fps.json", metadata)
        (output / "status").write_text("VERIFIED_COMPOSITE\n")
        print(json.dumps(metadata, indent=2), flush=True)
        return metadata
    except (OSError, ValueError, KeyError, subprocess.SubprocessError, av.error.FFmpegError) as error:
        (output / "status").write_text(f"INVALID: {error}\n")
        raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--offset-seconds", type=float, required=True,
                        help="Analyzer time minus time since the first retained Mira220 frame")
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--sampling-phase-ms", type=float, default=0)
    parser.add_argument("--voltage-max", type=float,
                        help="Fixed upper voltage limit for both plots; out-of-range values are visually clipped")
    parser.add_argument("--deflicker-model", type=Path,
                        help="Optional recording-specific display correction; raw files and trace data stay unchanged")
    args = parser.parse_args()
    try:
        render(args.run, args.cache, args.output, args.offset_seconds, args.seconds,
               args.width, args.sampling_phase_ms, args.deflicker_model, args.voltage_max)
        return 0
    except (OSError, ValueError, KeyError, subprocess.SubprocessError, av.error.FFmpegError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
