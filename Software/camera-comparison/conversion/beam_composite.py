# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
"""Stack raw cameras with a scrolling analog trace and optional full-capture overview."""
from __future__ import annotations

import argparse
from bisect import bisect_left, bisect_right
import csv
from fractions import Fraction
import itertools
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys

import av
import numpy as np
from PIL import Image, ImageDraw, ImageFont

from .convert import ConversionError, Recording, TIME_BASE, load_run, sha256, write_json


def prepare_trace(source: Path, destination: Path, samples_per_bin: int = 50) -> dict[str, object]:
    if destination.exists():
        raise ConversionError("Trace cache already exists; choose a new path")
    if not 1 <= samples_per_bin <= 1000:
        raise ConversionError("Trace bin size must be 1..1000 samples")
    minima, maxima, counts = [], [], []
    first: float | None = None
    previous: float | None = None
    total = 0
    original_stat = source.stat()
    period = 1e-6
    with source.open(encoding="utf-8") as stream:
        header = next(csv.reader([stream.readline()]))
        if len(header) != 2 or header[0] != "Time [s]":
            raise ConversionError("Expected a two-column analog CSV: Time [s], channel")
        while lines := list(itertools.islice(stream, samples_per_bin * 20_000)):
            data = np.loadtxt(lines, delimiter=",", dtype=np.float64, ndmin=2)
            if data.shape[1] != 2 or not np.isfinite(data).all():
                raise ConversionError("Invalid analog samples")
            if first is None:
                first = float(data[0, 0])
            if previous is not None and abs(float(data[0, 0]) - previous - period) > 2e-9:
                raise ConversionError("Non-contiguous analog CSV chunks")
            if len(data) > 1 and np.max(np.abs(np.diff(data[:, 0]) - period)) > 2e-9:
                raise ConversionError("This renderer requires the inspected 1 MS/s export, without missing samples")
            previous = float(data[-1, 0])
            values = data[:, 1]
            complete = len(values) // samples_per_bin
            if complete:
                blocks = values[:complete * samples_per_bin].reshape(-1, samples_per_bin)
                minima.append(blocks.min(axis=1))
                maxima.append(blocks.max(axis=1))
                counts.append(np.full(complete, samples_per_bin, dtype=np.int32))
            if remainder := len(values) % samples_per_bin:
                minima.append(np.array([values[-remainder:].min()]))
                maxima.append(np.array([values[-remainder:].max()]))
                counts.append(np.array([remainder], dtype=np.int32))
            total += len(values)
    if total < 2 or first is None or previous is None:
        raise ConversionError("Trace has insufficient samples")
    current_stat = source.stat()
    if (current_stat.st_size, current_stat.st_mtime_ns) != (original_stat.st_size, original_stat.st_mtime_ns):
        raise ConversionError("CSV changed while it was being read")
    metadata: dict[str, object] = {
        "source_csv": str(source.resolve()), "source_sha256": sha256(source),
        "channel": header[1], "unit": "V", "sample_rate_hz": 1_000_000,
        "samples_per_bin": samples_per_bin, "samples": total,
        "start_seconds": first, "last_sample_seconds": previous,
        "end_exclusive_seconds": first + total * period,
        "method": "min/max of every source sample in each bin; no point subsampling",
    }
    with destination.open("xb") as output:
        np.savez_compressed(output, minimum=np.concatenate(minima), maximum=np.concatenate(maxima),
                            counts=np.concatenate(counts), metadata=json.dumps(metadata))
    write_json(destination.with_suffix(".json"), metadata)
    return metadata


class Trace:
    def __init__(self, path: Path) -> None:
        with np.load(path, allow_pickle=False) as cache:
            self.low = np.asarray(cache["minimum"], dtype=np.float64)
            self.high = np.asarray(cache["maximum"], dtype=np.float64)
            self.counts = np.asarray(cache["counts"], dtype=np.int64)
            self.metadata = json.loads(str(cache["metadata"]))
        self.start = float(self.metadata["start_seconds"])
        self.rate = int(self.metadata["sample_rate_hz"])
        self.bin_samples = int(self.metadata["samples_per_bin"])
        self.end = float(self.metadata["end_exclusive_seconds"])
        if (
            len(self.low) != len(self.high) or len(self.low) != len(self.counts)
            or not len(self.low) or not np.isfinite(self.low).all()
            or not np.isfinite(self.high).all() or np.any(self.low > self.high)
            or self.rate != 1_000_000 or np.any(self.counts <= 0)
            or np.any(self.counts > self.bin_samples)
            or int(self.counts.sum()) != int(self.metadata["samples"])
        ):
            raise ConversionError("Invalid trace cache")
        self.step = self.bin_samples / self.rate

    def envelope(self, left: float, right: float, columns: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        """Aggregate all intersecting cache bins into pixels, preserving narrow peaks."""
        edges = np.linspace(left, right, columns + 1)
        low = np.full(columns, np.nan)
        high = np.full(columns, np.nan)
        valid = np.zeros(columns, dtype=bool)
        for index in range(columns):
            if edges[index + 1] <= self.start or edges[index] >= self.end:
                continue
            start = max(0, math.floor((edges[index] - self.start) / self.step))
            stop = min(len(self.low), math.ceil((edges[index + 1] - self.start) / self.step))
            if stop > start:
                low[index] = self.low[start:stop].min()
                high[index] = self.high[start:stop].max()
                valid[index] = True
        return low, high, valid


def output_timeline(records: list[Recording], start: int, duration: int) -> tuple[int, ...]:
    end = start + duration
    if duration <= 0:
        raise ConversionError("Duration must be positive")
    times = {start}
    for record in records:
        if start < record.timestamps_us[0] or end > record.timestamps_us[-1]:
            raise ConversionError("Requested common interval is not covered by both cameras")
        times.update(timestamp for timestamp in record.timestamps_us if start < timestamp < end)
    return tuple(sorted(times))


def interpolation_pair(timestamps: tuple[int, ...], timestamp: int) -> tuple[int, int, float]:
    if timestamp < timestamps[0] or timestamp > timestamps[-1]:
        raise ConversionError("Interpolation time is outside the camera recording")
    left = bisect_right(timestamps, timestamp)-1
    if timestamp == timestamps[left] or left == len(timestamps)-1:
        return left, left, 0.0
    right = left+1
    return left, right, (timestamp-timestamps[left])/(timestamps[right]-timestamps[left])


def master_timeline(record: Recording, earliest: int, end: int) -> tuple[int, ...]:
    times = record.timestamps_us[bisect_left(record.timestamps_us, earliest):bisect_left(record.timestamps_us, end)]
    if len(times) < 2:
        raise ConversionError("Requested interval has fewer than two master-camera frames")
    return times


def native_gray(record: Recording, raw: bytes) -> Image.Image:
    if len(raw) != record.sizeimage:
        raise ConversionError("Incomplete source frame")
    rows = np.frombuffer(raw, dtype=np.uint8).reshape(record.height, record.stride)
    if record.fourcc == "GREY":
        pixels = rows[:, :record.width]
    elif record.fourcc == "Y10P" and record.width % 4 == 0:
        pixels = rows[:, :record.width // 4 * 5].reshape(record.height, record.width // 4, 5)[:, :, :4].reshape(record.height, record.width)
    else:
        raise ConversionError("Unsupported camera format")
    return Image.fromarray(pixels)


def find_font(size: int) -> ImageFont.FreeTypeFont:
    for path in (Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
                 Path(r"C:\Windows\Fonts\arial.ttf")):
        if path.is_file():
            return ImageFont.truetype(str(path), size)
    raise ConversionError("Install DejaVu Sans or supply a supported system font")


def window_bounds(center: float, window: float, position: str = "center") -> tuple[float, float]:
    if position == "right":
        return center-window, center
    if position != "center":
        raise ConversionError("Window position must be center or right")
    return center-window/2, center+window/2


def overview_positions(center: float, window: float, start: float, end: float,
                       left: int, right: int, position: str = "center") -> tuple[float, float, float]:
    if not all(math.isfinite(value) for value in (center, window, start, end)) or end <= start or window <= 0 or right <= left:
        raise ConversionError("Invalid overview time range or plot bounds")
    before, after = window_bounds(center, window, position)
    return tuple(left+(time-start)/(end-start)*(right-left) for time in (before, center, after))


class Layout:
    def __init__(self, records: dict[str, Recording], width: int, window: float,
                 overview_range: tuple[float, float] | None = None, mira_only: bool = False,
                 hide_detail_cursor: bool = False, window_position: str = "center",
                 overview_time_offset: float = 0.0, mira_first: bool = False,
                 voltage_max: float | None = None) -> None:
        if mira_first and not mira_only:
            raise ConversionError("Camera-first layout requires Mira-only display")
        if voltage_max is not None and (not math.isfinite(voltage_max) or voltage_max <= 0):
            raise ConversionError("Voltage axis maximum must be finite and positive")
        self.voltage_max = voltage_max
        self.mira_first = mira_first
        self.width = width
        self.window = window
        self.overview_range = overview_range
        self.hide_detail_cursor = hide_detail_cursor
        self.window_position = window_position
        self.overview_time_offset = overview_time_offset
        self.top_height = (0 if mira_only else
                           round(records["imx296"].height * width / records["imx296"].width / 2) * 2)
        self.bottom_height = round(records["mira220"].height * width / records["mira220"].width / 2) * 2
        self.header = 42
        self.plot_height = 360
        self.overview_height = 280 if overview_range is not None else 0
        self.overview_y = 0 if mira_only else self.header + self.top_height
        self.plot_y = self.overview_y + self.overview_height
        self.bottom_y = self.plot_y + self.plot_height
        self.height = self.bottom_y + self.header + self.bottom_height
        if mira_first:
            self.bottom_y = 0
            self.plot_y = self.header + self.bottom_height
            self.overview_y = self.plot_y + self.plot_height
            self.height = self.overview_y + self.overview_height
        self.font = find_font(19)
        self.small = find_font(16)
        self._overview_base: Image.Image | None = None
        self._overview_trace: Trace | None = None

    def _waveform(self, draw: ImageDraw.ImageDraw, trace: Trace, start: float, end: float,
                  top: int, bottom: int) -> tuple[int, int]:
        left, right = 72, self.width-25
        vmin = -0.1
        vmax = self.voltage_max if self.voltage_max is not None else max(0.5, math.ceil(float(trace.high.max())*2)/2)
        def y(value: float) -> int:
            if self.voltage_max is not None:
                value = min(vmax, max(vmin, value))
            return round(bottom-(value-vmin)/(vmax-vmin)*(bottom-top))
        ticks = np.arange(0, vmax+0.001, 1 if vmax > 2 else 0.1)
        if self.voltage_max is not None:
            ticks = ticks[ticks < vmax]
            ticks = np.append(ticks, vmax)
        for value in ticks:
            draw.line((left, y(value), right, y(value)), fill="#d4dce5")
            draw.text((18, y(value)-9), f"{value:g}", font=self.small, fill="#34546b")
        low, high, valid = trace.envelope(start, end, right-left+1)
        for x in range(right-left+1):
            if valid[x]:
                draw.line((left+x, y(float(high[x])), left+x, y(float(low[x]))), fill="#087caa")
            else:
                draw.line((left+x, top, left+x, bottom), fill="#d9dce0")
        return left, right

    def overview(self, trace: Trace, analyzer_time: float) -> Image.Image:
        if self.overview_range is None:
            raise ConversionError("Overview panel is not enabled")
        start, end = self.overview_range
        top, bottom = 72, self.overview_height-66
        left, right = 72, self.width-25
        before, center, after = overview_positions(analyzer_time, self.window, start, end, left, right, self.window_position)
        if self._overview_base is None or self._overview_trace is not trace:
            base = Image.new("RGB", (self.width, self.overview_height), "#f5f7fa")
            draw = ImageDraw.Draw(base)
            self._waveform(draw, trace, start, end, top, bottom)
            draw.text((16, 8), f"{trace.metadata['channel']} [V]  |  full {end-start:.3f} s capture overview  |  moving cursor",
                      font=self.font, fill="#152b43")
            for fraction in np.linspace(0, 1, 7):
                x = round(left+fraction*(right-left))
                draw.line((x, bottom, x, bottom+5), fill="#34546b")
                draw.text((x-23, bottom+9), f"{self.overview_time_offset+fraction*(end-start):.2f}", font=self.small, fill="#34546b")
            detail_direction = "above" if self.mira_first else "below"
            draw.text((16, self.overview_height-25),
                      f"Original video seconds  |  dotted boundaries = the same zoomed window shown {detail_direction}",
                      font=self.small, fill="#34546b")
            self._overview_base, self._overview_trace = base, trace
        image = self._overview_base.copy()
        draw = ImageDraw.Draw(image, "RGBA")
        selection_left, selection_right = max(left, round(before)), min(right, round(after))
        if selection_left <= selection_right:
            draw.rectangle((selection_left, top, selection_right, bottom), fill=(235, 152, 35, 42))
        for boundary in (before, after):
            if left <= boundary <= right:
                x = round(boundary)
                for y in range(top-4, bottom+5, 10):
                    draw.line((x, y, x, min(y+4, bottom+4)), fill="#88530b", width=2)
        if left <= center <= right:
            if self.window_position == "right":
                # Keep the coincident dotted right boundary visible.
                draw.line((round(center), top-9, round(center), top-5), fill="#d43835", width=3)
                draw.line((round(center), bottom+5, round(center), bottom+9), fill="#d43835", width=3)
            else:
                draw.line((round(center), top-5, round(center), bottom+5), fill="#d43835", width=3)
        video_time = analyzer_time-start+self.overview_time_offset
        before_s, after_s = window_bounds(video_time, self.window, self.window_position)
        draw.text((16, 34),
                  f"Video {video_time:7.3f} s  |  zoom window {before_s:.3f} to {after_s:.3f} s  |  analyzer {analyzer_time:.3f} s",
                  font=self.small, fill="#34546b")
        return image

    def plot(self, trace: Trace, analyzer_time: float, camera_time: float) -> Image.Image:
        image = Image.new("RGB", (self.width, self.plot_height), "#f5f7fa")
        draw = ImageDraw.Draw(image)
        left, right = 72, self.width - 25
        top, bottom = 72, self.plot_height - 73
        cursor_label = ("current time at right edge" if self.window_position == "right" else
                        "current time centered" if self.hide_detail_cursor else "fixed time cursor")
        draw.text((16, 8), f"{trace.metadata['channel']} [V]  |  rolling {self.window:.3f} s window  |  {cursor_label}", font=self.font, fill="#152b43")
        draw.text((16, 34), f"Camera timeline {camera_time:7.3f} s    Analyzer {analyzer_time:7.3f} s    Event-based alignment (approximate)", font=self.small, fill="#34546b")
        t0, t1 = window_bounds(analyzer_time, self.window, self.window_position)
        self._waveform(draw, trace, t0, t1, top, bottom)
        for fraction in np.linspace(0, 1, 5):
            x = round(left + fraction*(right-left))
            t = t0 + fraction*self.window
            draw.line((x, bottom, x, bottom+5), fill="#34546b")
            draw.text((x-29, bottom+9), f"{t:.2f}", font=self.small, fill="#34546b")
        if not self.hide_detail_cursor:
            center = right if self.window_position == "right" else (left+right)//2
            draw.line((center, top-5, center, bottom+5), fill="#d43835", width=3)
        draw.text((16, self.plot_height-28), "1 MS/s source  |  min/max envelope (all samples retained in extrema)  |  analyzer seconds", font=self.small, fill="#34546b")
        if not trace.start <= analyzer_time < trace.end:
            draw.text((left+20, top+20), "NO CSV DATA AT CURRENT TIME", font=self.font, fill="#8b2320")
        return image


def render(run: Path, cache: Path, output: Path, offset: float, seconds: float, width: int, window: float,
           clock: str = "mira220", imx_resample: str = "blend", stop_at_trace_end: bool = False,
           overview: bool = False, mira_only: bool = False,
           hide_detail_cursor: bool = False, window_position: str = "center") -> dict[str, object]:
    records = {record.name: record for record in load_run(run)}
    if set(records) != {"imx296", "mira220"}:
        raise ConversionError("Both raw cameras are required")
    if not math.isfinite(offset) or not 0 < seconds <= 30 or width < 640 or width > 1600 or width % 2 or not 0.02 <= window <= 5:
        raise ConversionError("Unsupported offset, duration, output width or waveform window")
    trace = Trace(cache)
    reference_origin = min(record.timestamps_us[0] for record in records.values())
    start = max(record.timestamps_us[0] for record in records.values())
    if clock == "mira220":
        start = records["mira220"].timestamps_us[bisect_left(records["mira220"].timestamps_us, start)]
    end = start + round(seconds*1e6)
    if stop_at_trace_end:
        end = min(end, reference_origin + round((trace.end-offset)*1e6))
    duration = end-start
    output_timeline(list(records.values()), start, duration)
    timeline = (master_timeline(records["mira220"], start, end) if clock == "mira220"
                else output_timeline(list(records.values()), start, duration))
    durations = [b-a for a,b in zip(timeline, (*timeline[1:], start+duration))]
    overview_range = ((start-reference_origin)/1e6+offset, (end-reference_origin)/1e6+offset) if overview else None
    layout = Layout(records, width, window, overview_range, mira_only, hide_detail_cursor, window_position)
    output.mkdir(mode=0o700, parents=False, exist_ok=False)
    (output / "status").write_text("INCOMPLETE\n")
    temporary = output / "beam-composite.incomplete.mp4"
    expected_pts = [timestamp-start for timestamp in timeline]
    duration_by_pts = dict(zip(expected_pts, durations))
    displayed_cameras = ("mira220",) if mira_only else ("imx296", "mira220")
    streams = {name: records[name].raw.open("rb") for name in displayed_cameras}
    cached_index = {name: -1 for name in records}
    pictures: dict[str, dict[int, Image.Image]] = {name: {} for name in records}
    imx_mapping = [None, None, None] if mira_only else [0, 0, 0.0]
    map_path = output / "frame-map.tsv"
    raw_stats = {name: (record.raw.stat().st_size, record.raw.stat().st_mtime_ns) for name, record in records.items()}
    packet_count = 0
    def source_picture(name: str, index: int) -> Image.Image:
        if index not in pictures[name]:
            record = records[name]
            streams[name].seek(index*record.sizeimage)
            native = native_gray(record, streams[name].read(record.sizeimage))
            target_height = layout.top_height if name == "imx296" else layout.bottom_height
            pictures[name][index] = native.resize((width, target_height), Image.Resampling.LANCZOS)
            while len(pictures[name]) > 2:
                del pictures[name][min(pictures[name])]
        return pictures[name][index]

    def assemble(timestamp: int) -> Image.Image:
        canvas = Image.new("RGB", (layout.width, layout.height), "#14202d")
        draw = ImageDraw.Draw(canvas)
        for name in displayed_cameras:
            record = records[name]
            index = bisect_right(record.timestamps_us, timestamp)-1
            picture = source_picture(name, index)
            cached_index[name] = index
            y = 0 if name == "imx296" else layout.bottom_y
            camera_time = (record.timestamps_us[index]-record.timestamps_us[0])/1e6
            title = f"{name.upper()}  |  native {record.width}x{record.height} {record.fourcc}  |  frame {index}  |  camera {camera_time:.6f} s"
            if name == "imx296":
                left, right, alpha = interpolation_pair(record.timestamps_us, timestamp)
                if imx_resample == "blend" and left != right:
                    picture = Image.blend(picture, source_picture(name, right), alpha)
                else:
                    right, alpha = left, 0.0
                imx_mapping[:] = [left, right, alpha]
                title = f"IMX296  |  60.38 fps source  |  display-only {imx_resample}  |  frames {left}/{right}, w={alpha:.3f}"
            draw.text((14, y+10), title, font=layout.font, fill="#f5f9ff")
            canvas.paste(picture, (0, y+layout.header))
        reference_time = (timestamp-reference_origin)/1e6
        if overview:
            canvas.paste(layout.overview(trace, reference_time+offset), (0, layout.overview_y))
        canvas.paste(layout.plot(trace, reference_time+offset, reference_time), (0, layout.plot_y))
        return canvas
    try:
        with map_path.open("x", encoding="utf-8", newline="") as frame_map:
            writer = csv.writer(frame_map, delimiter="\t")
            writer.writerow(["output_pts_us", "camera_reference_us", "analyzer_time_s",
                             "imx296_frame_prev", "imx296_frame_next", "imx296_blend_weight", "mira220_frame"])
            with av.open(str(temporary), "w", options={"video_track_timescale":"1000000", "movie_timescale":"1000000"}) as container:
                period = int(statistics.median_low(np.diff(records["mira220"].timestamps_us)))
                stream = container.add_stream("libx264", rate=Fraction(1_000_000, period) if clock == "mira220" else 150)
                stream.width, stream.height, stream.pix_fmt = layout.width, layout.height, "yuv420p"
                stream.time_base = stream.codec_context.time_base = TIME_BASE
                stream.codec_context.max_b_frames = 0
                stream.codec_context.thread_count = 3
                stream.options = {"crf":"18", "preset":"veryfast"}
                def mux(packets):
                    nonlocal packet_count
                    for packet in packets:
                        if packet.pts not in duration_by_pts or packet.time_base != TIME_BASE:
                            raise ConversionError("Composite encoder altered the timeline")
                        packet.duration = duration_by_pts[packet.pts]
                        container.mux(packet)
                        packet_count += 1
                for index, timestamp in enumerate(timeline):
                    picture = assemble(timestamp)
                    frame = av.VideoFrame.from_image(picture)
                    frame.pts, frame.time_base = timestamp-start, TIME_BASE
                    mux(stream.encode(frame))
                    writer.writerow([timestamp-start, timestamp-reference_origin,
                                     f"{(timestamp-reference_origin)/1e6+offset:.9f}",
                                     *imx_mapping, cached_index["mira220"]])
                    if index % 300 == 0:
                        print(f"Composite {index+1}/{len(timeline)} frames", flush=True)
                    if index == 0:
                        picture.save(output / "layout-first-frame.jpg", quality=95)
                mux(stream.encode(None))
            frame_map.flush()
            os.fsync(frame_map.fileno())
        if packet_count != len(timeline):
            raise ConversionError("Composite output frame count differs from shared timeline")
        probe = subprocess.run(["ffprobe","-v","error","-select_streams","v:0","-show_streams","-show_packets",
                                "-show_entries","stream=width,height,time_base,nb_frames,duration:packet=pts,duration",
                                "-of","json",str(temporary)],capture_output=True,text=True,check=True,timeout=120)
        report = json.loads(probe.stdout)
        info, packets = report["streams"][0], report["packets"]
        if (int(info["nb_frames"]) != len(timeline) or Fraction(info["time_base"]) != TIME_BASE
                or info["width"] != layout.width or info["height"] != layout.height
                or len(packets) != len(timeline)):
            raise ConversionError("Composite stream verification failed")
        for index, packet in enumerate(packets):
            if int(packet["pts"]) != expected_pts[index] or int(packet["duration"]) != durations[index]:
                raise ConversionError(f"Composite frame {index} timing verification failed")
        if Fraction(info["duration"])*1_000_000 != duration:
            raise ConversionError("Composite duration changed")
        for name, record in records.items():
            stat = record.raw.stat()
            if (stat.st_size, stat.st_mtime_ns) != raw_stats[name]:
                raise ConversionError("Source raw file changed during rendering")
        final = output / "beam-composite.mp4"
        temporary.rename(final)
        metadata = {
            "source_run":str(run), "requested_duration_seconds":seconds,"duration_seconds":duration/1e6,
            "output_width":layout.width,"output_height":layout.height,
            "frames":len(timeline), "master_clock":clock,
            "frame_timing":"original Mira220 timestamps, no omitted/interpolated Mira frames" if clock == "mira220"
                           else "union of original camera timestamps",
            "mean_frame_rate":(len(timeline)-1)*1e6/(timeline[-1]-timeline[0]),
            "imx296_resample":None if mira_only else imx_resample,
            "interpolation_note":("no camera-frame interpolation" if mira_only else
                                  "temporal linear blending may ghost moving objects; it does not create new measurements"),
            "stop_at_csv_end":stop_at_trace_end,
            "reference_origin_us":reference_origin, "common_start_offset_us":start-reference_origin,
            "analyzer_time_equation":f"(source_camera_timestamp_us - {reference_origin}) / 1000000 + {offset:.9f}",
            "alignment_method":"operator-approved event-pattern alignment, not hardware synchronization",
            "alignment_uncertainty":"camera-frame/exposure precision; do not infer microsecond optical synchronization",
            "offset_seconds":offset,"plot_window_seconds":window,"trace":trace.metadata,
            "detail_cursor_visible":not hide_detail_cursor,
            "window_position":window_position,
            "panels":(([] if mira_only else ["imx296"]) +
                      (["full_capture_overview"] if overview else []) + ["scrolling_detail", "mira220"]),
            "overview_analyzer_range_seconds":overview_range,
            "overview_axis":"elapsed composite seconds" if overview else None,
            "overview_selection":(("dotted boundaries from current frame time minus window to current frame time"
                                   if window_position == "right" else "dotted current time +/- half the detail window")
                                  + "; clipped at overview edges") if overview else None,
            "no_data_behavior":"shade unknown regions; no fabricated zeros beyond the supplied CSV coverage",
            "pixel_mapping":"native raw -> linear gray8 (RAW10 >> 2), aspect-preserving viewing resize; no brightness normalization",
            "output_sha256":sha256(final), "frame_map_sha256":sha256(map_path),
        }
        write_json(output / "composite.json", metadata)
        (output / "status").write_text("VERIFIED_COMPOSITE\n")
        print(json.dumps(metadata, indent=2), flush=True)
        return metadata
    except (OSError, ValueError, KeyError, subprocess.SubprocessError, av.error.FFmpegError) as error:
        (output / "status").write_text(f"INVALID: {error}\n")
        raise
    finally:
        for source in streams.values():
            source.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    prep = commands.add_parser("prepare")
    prep.add_argument("--csv", type=Path, required=True)
    prep.add_argument("--cache", type=Path, required=True)
    prep.add_argument("--samples-per-bin", type=int, default=50)
    video = commands.add_parser("render")
    video.add_argument("--run", type=Path, required=True)
    video.add_argument("--cache", type=Path, required=True)
    video.add_argument("--output", type=Path, required=True)
    video.add_argument("--offset-seconds", type=float, required=True,
                       help="Analyzer seconds minus seconds since the earlier camera first frame")
    video.add_argument("--seconds", type=float, default=30)
    video.add_argument("--width", type=int, default=1280)
    video.add_argument("--window-seconds", type=float, default=1)
    video.add_argument("--clock", choices=("mira220", "union"), default="mira220")
    video.add_argument("--imx-resample", choices=("blend", "hold"), default="blend")
    video.add_argument("--stop-at-trace-end", action="store_true")
    video.add_argument("--overview", action="store_true",
                       help="Add a full-capture overview above the scrolling detail with dotted zoom-window boundaries")
    video.add_argument("--mira-only", action="store_true",
                       help="Hide the IMX296 panel while preserving the existing common synchronized interval")
    video.add_argument("--hide-detail-cursor", action="store_true",
                       help="Remove the center line from the scrolling plot, retaining overview markers")
    video.add_argument("--window-position", choices=("center", "right"), default="center",
                       help="Place current frame at the center or right edge of the rolling window")
    args = parser.parse_args()
    try:
        if args.command == "prepare":
            print(json.dumps(prepare_trace(args.csv, args.cache, args.samples_per_bin), indent=2))
        else:
            render(args.run, args.cache, args.output, args.offset_seconds, args.seconds, args.width, args.window_seconds,
                   args.clock, args.imx_resample, args.stop_at_trace_end, args.overview, args.mira_only,
                   args.hide_detail_cursor, args.window_position)
        return 0
    except (OSError, ValueError, KeyError, subprocess.SubprocessError, av.error.FFmpegError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
