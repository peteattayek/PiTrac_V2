# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
"""Convert verified native camera recordings to MP4 using every recorded timestamp."""

from __future__ import annotations

import argparse
from bisect import bisect_left
import csv
from dataclasses import dataclass, replace
from fractions import Fraction
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import sys

TIME_BASE = Fraction(1, 1_000_000)
NAMES = ("mira220", "imx296")
CAPTURE_LINE = re.compile(
    r"^\s*cap dqbuf:.*?\bseq:\s*(\d+)\s+bytesused:\s*(\d+).*?\bts:\s*(\d+\.\d+)"
)


class ConversionError(ValueError):
    pass


@dataclass(frozen=True)
class Recording:
    name: str
    raw: Path
    width: int
    height: int
    stride: int
    sizeimage: int
    fourcc: str
    timestamps_us: tuple[int, ...]
    sequences: tuple[int, ...]
    flags: tuple[str, ...]
    clip_end_us: int | None = None

    def durations_us(self) -> tuple[int, ...]:
        intervals = tuple(b - a for a, b in zip(self.timestamps_us, self.timestamps_us[1:]))
        # There is no timestamp for the end of the final frame. State this
        # estimate explicitly; all earlier durations come from adjacent timestamps.
        last = (
            self.clip_end_us - self.timestamps_us[-1] if self.clip_end_us is not None
            else int(statistics.median_low(intervals))
        )
        return (*intervals, last)


def first_seconds(record: Recording, seconds: str | None) -> Recording:
    if seconds is None:
        return record
    duration = Fraction(seconds) * 1_000_000
    if duration.denominator != 1 or duration <= 0:
        raise ConversionError("Clip duration must be positive seconds with at most microsecond precision")
    end = record.timestamps_us[0] + duration.numerator
    if end > record.timestamps_us[-1] + record.durations_us()[-1]:
        raise ConversionError(f"{record.name}: recording is shorter than the requested clip")
    count = bisect_left(record.timestamps_us, end)
    if count < 2:
        raise ConversionError("Requested clip must include at least two source frames")
    return replace(record, timestamps_us=record.timestamps_us[:count], sequences=record.sequences[:count],
                   flags=record.flags[:count], clip_end_us=end)


def read_kv(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    with path.open(encoding="utf-8", newline="") as source:
        for row in csv.reader(source, delimiter="\t"):
            if len(row) != 2 or not all(row) or row[0] in result:
                raise ConversionError(f"Malformed/duplicate field in {path.name}")
            result[row[0]] = row[1]
    return result


def timestamp_us(text: str) -> int:
    if not re.fullmatch(r"\d+\.\d{1,6}", text):
        raise ConversionError(f"Timestamp must be recorded decimal seconds with microsecond precision: {text!r}")
    exact = Fraction(text) * 1_000_000
    if exact.denominator != 1:
        raise ConversionError("Timestamp cannot be represented exactly in microseconds")
    return exact.numerator


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_manifest(run: Path, names: list[str]) -> None:
    required = {"run.tsv", "storage.tsv", *(f"{name}.tsv" for name in names)}
    seen: set[str] = set()
    for line in (run / "manifest.sha256").read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"([a-fA-F0-9]{64}) [ *]([a-zA-Z0-9_.-]+)", line)
        if not match or match[2] not in required or match[2] in seen:
            raise ConversionError("Unsupported, duplicate or unsafe manifest entry")
        if sha256(run / match[2]) != match[1].lower():
            raise ConversionError(f"Input manifest checksum mismatch: {match[2]}")
        seen.add(match[2])
    if seen != required:
        raise ConversionError("Recording manifest is incomplete")


def load_recording(run: Path, name: str, expected_count: int, warmup: int) -> Recording:
    profile = read_kv(run / f"{name}.tsv")
    width, height = int(profile["width"]), int(profile["height"])
    stride, size = int(profile["stride"]), int(profile["sizeimage"])
    expected = {"mira220": (1600, 1400, "GREY", 1600), "imx296": (1456, 1088, "Y10P", 1820)}[name]
    if (
        profile["sensor"] != name or profile["schema"] != "1"
        or (width, height, profile["fourcc"]) != expected[:3]
        or not expected[3] <= stride <= 65536 or stride % 16
        or size != stride * height or not 2 <= expected_count <= 100_000
    ):
        raise ConversionError(f"Unsupported {name} native frame layout/count")
    timestamps: list[int] = []
    sequences: list[int] = []
    flags: list[str] = []
    with (run / f"{name}.frames.tsv").open(encoding="utf-8", newline="") as source:
        reader = csv.DictReader(source, delimiter="\t")
        if reader.fieldnames != ["sequence", "timestamp_seconds", "bytesused", "flags"]:
            raise ConversionError(f"Unsupported {name} frame table")
        for row in reader:
            if len(timestamps) >= expected_count:
                raise ConversionError(f"Too many {name} frame rows")
            if set(row) != set(reader.fieldnames) or any(value is None for value in row.values()):
                raise ConversionError(f"Malformed {name} frame table row")
            timestamp = timestamp_us(row["timestamp_seconds"])
            sequence = int(row["sequence"])
            if (
                not 0 <= sequence < 2**32 or int(row["bytesused"]) != size
                or "ts-monotonic" not in row["flags"] or "error" in row["flags"].lower()
                or (timestamps and timestamp <= timestamps[-1])
                or (sequences and sequence != (sequences[-1] + 1) % 2**32)
            ):
                raise ConversionError(f"Invalid {name} frame metadata at row {len(timestamps) + 1}")
            timestamps.append(timestamp)
            sequences.append(sequence)
            flags.append(row["flags"])
    raw = run / f"{name}.raw"
    if len(timestamps) != expected_count or raw.stat().st_size != size * expected_count:
        raise ConversionError(f"{name} frame count/file size does not match the run manifest")
    record = Recording(name, raw, width, height, stride, size, profile["fourcc"],
                       tuple(timestamps), tuple(sequences), tuple(flags))
    observed = 0
    with (run / f"{name}.capture.log").open(encoding="utf-8") as source:
        for line in source:
            if not line.lstrip().startswith("cap dqbuf:"):
                if re.search(r"\d+ != \d+", line):
                    raise ConversionError(f"{name} capture log contains a partial-write error")
                continue
            match = CAPTURE_LINE.search(line)
            if not match or "error" in line.lower() or "ts-monotonic" not in line:
                raise ConversionError(f"{name} capture log contains invalid frame evidence")
            if observed >= warmup:
                index = observed - warmup
                if (
                    index >= expected_count or int(match[1]) != sequences[index]
                    or int(match[2]) != size or timestamp_us(match[3]) != timestamps[index]
                    or not line.rstrip().endswith(flags[index])
                ):
                    raise ConversionError(f"{name} frame table differs from its original capture log")
            observed += 1
    if observed != warmup + expected_count:
        raise ConversionError(f"{name} capture log frame count differs from metadata")
    return record


def load_run(run: Path) -> list[Recording]:
    status = (run / "status").read_text(encoding="utf-8").strip()
    if status not in ("VERIFIED_RECORDING", "VERIFIED_RECORDING_REDUCED_HEADROOM"):
        raise ConversionError("Only completed, verified disk recordings can be converted")
    manifest = read_kv(run / "run.tsv")
    completion = read_kv(run / "completion.tsv")
    names = manifest["cameras"].split()
    if (
        manifest["schema"] != "1" or manifest["sink"] != "0" or manifest["warmup"] != "5"
        or completion["durable"] != "1" or completion["controls_readback_passed"] != "1"
        or not names or len(set(names)) != len(names) or any(name not in NAMES for name in names)
    ):
        raise ConversionError("Unsupported or incomplete recording manifest")
    validate_manifest(run, names)
    records = [load_recording(run, name, int(manifest[f"{name}.frames"]), 5) for name in names]
    if sum(record.sizeimage * len(record.timestamps_us) for record in records) != int(manifest["expected_total_bytes"]):
        raise ConversionError("Combined native byte count does not match the run manifest")
    return records


def gray8(record: Recording, frame: bytes) -> bytes:
    """Linear viewing mapping; RAW10 low bits are discarded, never normalized."""
    if len(frame) != record.sizeimage:
        raise ConversionError("Partial native frame")
    width, height, stride = record.width, record.height, record.stride
    if record.fourcc == "GREY":
        return b"".join(frame[y * stride:y * stride + width] for y in range(height))
    if record.fourcc != "Y10P" or width % 4:
        raise ConversionError("Unsupported raw pixel format")
    pixels = bytearray(width * height)
    for y in range(height):
        row = frame[y * stride:y * stride + width // 4 * 5]
        # Each high byte equals the decoded native RAW10 value >> 2.
        for pixel in range(4):
            pixels[y * width + pixel:(y + 1) * width:4] = row[pixel::5]
    return bytes(pixels)


def av_module():
    try:
        import av
    except ImportError as error:
        raise ConversionError("PyAV is required. On Pi OS install python3-av (see CONVERT.md).") from error
    return av


def encode(record: Recording, destination: Path, origin_us: int, crf: int, preset: str, threads: int) -> dict[str, object]:
    av = av_module()
    durations = record.durations_us()
    pts = tuple(timestamp - origin_us for timestamp in record.timestamps_us)
    if origin_us != record.timestamps_us[0]:
        raise ConversionError("Each viewing clip must start at its first source timestamp; original offsets are kept in the report")
    duration_by_pts = dict(zip(pts, durations))
    emitted = 0
    raw_hash = hashlib.sha256()
    original_stat = record.raw.stat()
    options = {
        "video_track_timescale": "1000000", "movie_timescale": "1000000",
        "use_editlist": "1", "avoid_negative_ts": "disabled",
    }
    with destination.open("xb") as output:
        with av.open(output, mode="w", format="mp4", options=options) as container:
            stream = container.add_stream("libx264", rate=Fraction(1_000_000, durations[-1]))
            stream.width, stream.height = record.width, record.height
            stream.pix_fmt = "yuv420p"
            stream.time_base = TIME_BASE
            stream.codec_context.time_base = TIME_BASE
            stream.codec_context.max_b_frames = 0
            stream.codec_context.thread_count = threads
            stream.options = {"crf": str(crf), "preset": preset}
            stream.metadata["title"] = f"{record.name}: timestamp-faithful raw viewing copy"

            def mux(packets) -> None:
                nonlocal emitted
                for packet in packets:
                    if packet.pts is None or packet.time_base is None:
                        raise ConversionError("Encoder omitted packet timing")
                    packet_us = Fraction(packet.pts) * packet.time_base / TIME_BASE
                    if packet_us.denominator != 1 or packet_us.numerator not in duration_by_pts:
                        raise ConversionError("Encoder changed source presentation timestamps")
                    duration = Fraction(duration_by_pts[packet_us.numerator]) * TIME_BASE / packet.time_base
                    if duration.denominator != 1:
                        raise ConversionError("Encoder cannot represent the source frame duration exactly")
                    packet.duration = duration.numerator
                    container.mux(packet)
                    emitted += 1

            with record.raw.open("rb") as source:
                for index, timestamp in enumerate(pts):
                    raw = source.read(record.sizeimage)
                    raw_hash.update(raw)
                    pixels = gray8(record, raw)
                    frame = av.VideoFrame(record.width, record.height, "gray")
                    plane = frame.planes[0]
                    if plane.line_size == record.width and plane.buffer_size == len(pixels):
                        plane.update(pixels)
                    else:
                        padded = bytearray(plane.buffer_size)
                        for row in range(record.height):
                            padded[row * plane.line_size:row * plane.line_size + record.width] = pixels[row * record.width:(row + 1) * record.width]
                        plane.update(padded)
                    frame = frame.reformat(format="yuv420p")
                    frame.pts, frame.time_base = timestamp, TIME_BASE
                    mux(stream.encode(frame))
                    if index % 300 == 0:
                        print(f"{record.name}: encoded {index + 1}/{len(pts)} frames", flush=True)
                if record.clip_end_us is None and source.read(1):
                    raise ConversionError("Raw file grew during conversion")
            mux(stream.encode(None))
        output.flush()
        os.fsync(output.fileno())
    current_stat = record.raw.stat()
    if (original_stat.st_size, original_stat.st_mtime_ns) != (current_stat.st_size, current_stat.st_mtime_ns):
        raise ConversionError("Raw input changed during conversion")
    if emitted != len(pts):
        raise ConversionError("Encoder packet count differs from source frame count")
    return {"selected_raw_prefix_sha256": raw_hash.hexdigest(), "encoded_packets": emitted}


def verify_mp4(record: Recording, path: Path, origin_us: int, ffprobe: str) -> dict[str, object]:
    result = subprocess.run(
        [ffprobe, "-v", "error", "-select_streams", "v:0", "-show_streams", "-show_packets",
         "-show_entries", "stream=codec_name,width,height,time_base,nb_frames,start_pts:packet=pts,dts,duration",
         "-of", "json", str(path)], capture_output=True, text=True, timeout=120, check=False,
    )
    if result.returncode or result.stderr.strip():
        raise ConversionError(f"ffprobe failed: {result.stderr.strip()}")
    probe = json.loads(result.stdout)
    streams, packets = probe["streams"], probe["packets"]
    if len(streams) != 1:
        raise ConversionError("Output does not have one selected video stream")
    stream = streams[0]
    if (
        stream["codec_name"] != "h264" or stream["width"] != record.width
        or stream["height"] != record.height or Fraction(stream["time_base"]) != TIME_BASE
        or len(packets) != len(record.timestamps_us)
        or int(stream["nb_frames"]) != len(record.timestamps_us)
    ):
        raise ConversionError("Output format/timebase/frame count does not match the recording")
    durations = record.durations_us()
    for index, packet in enumerate(packets):
        expected = record.timestamps_us[index] - origin_us
        if any(key not in packet for key in ("pts", "dts", "duration")):
            raise ConversionError(f"{record.name} frame {index}: MP4 omitted required timing")
        if int(packet["pts"]) != expected or int(packet["dts"]) != expected or int(packet["duration"]) != durations[index]:
            raise ConversionError(f"{record.name} frame {index}: encoded timing differs from the recorded microsecond timeline")
    return {
        "verified_frames": len(packets), "time_base": "1/1000000",
        "every_pts_and_duration_exact": True, "start_offset_us": record.timestamps_us[0] - origin_us,
        "source_first_timestamp_us": record.timestamps_us[0],
        "source_last_timestamp_us": record.timestamps_us[-1],
        "measured_frame_span_us": record.timestamps_us[-1] - record.timestamps_us[0],
        "last_frame_duration_us": durations[-1],
        "last_frame_duration_method": (
            "clipped_to_requested_endpoint" if record.clip_end_us is not None
            else "median_low_of_recorded_intervals (end timestamp unavailable)"
        ),
        "playback_duration_us": record.timestamps_us[-1] - record.timestamps_us[0] + durations[-1],
    }


def write_json(path: Path, value: dict[str, object]) -> None:
    with path.open("x", encoding="utf-8") as destination:
        json.dump(value, destination, indent=2)
        destination.write("\n")
        destination.flush()
        os.fsync(destination.fileno())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=Path, required=True)
    parser.add_argument("--output", type=Path, help="New output directory; default RUN/mp4-timed")
    parser.add_argument("--camera", choices=("both", *NAMES), default="both")
    parser.add_argument("--seconds", help="Keep only the first N seconds of each camera")
    parser.add_argument("--crf", type=int, default=18)
    parser.add_argument("--preset", choices=("ultrafast", "veryfast", "fast", "medium"), default="veryfast")
    parser.add_argument("--threads", type=int, default=2)
    args = parser.parse_args()
    if not 0 <= args.crf <= 51 or not 1 <= args.threads <= 4:
        parser.error("CRF must be 0..51 and threads 1..4")
    output: Path | None = None
    handled_errors: tuple[type[Exception], ...] = (OSError, ValueError, KeyError, subprocess.SubprocessError)
    try:
        ffprobe = shutil.which("ffprobe")
        if ffprobe is None:
            raise ConversionError("ffprobe is required; install the ffmpeg package")
        av = av_module()
        handled_errors = (*handled_errors, av.error.FFmpegError)
        av.codec.Codec("libx264", "w")
        run = args.run.resolve(strict=True)
        records = load_run(run)
        origin = min(record.timestamps_us[0] for record in records)
        selected = [first_seconds(record, args.seconds) for record in records if args.camera == "both" or record.name == args.camera]
        if not selected:
            raise ConversionError("Requested camera was not recorded")
        candidate = args.output or run / ("mp4-timed" if args.seconds is None else f"mp4-first-{Fraction(args.seconds)}s".replace("/", "_"))
        candidate.mkdir(mode=0o700, parents=False, exist_ok=False)
        output = candidate.resolve()
        (output / "status").write_text("INCOMPLETE\n", encoding="utf-8")
        report: dict[str, object] = {
            "source_run": str(run), "timeline_origin_us": origin, "pyav_version": av.__version__,
            "mode": "per-frame recorded monotonic timestamps; no dropped/duplicated frames within selected interval",
            "timeline": "per-camera zero; original shared-origin offsets retained below",
            "requested_seconds_per_camera": args.seconds,
            "viewing_copy": True, "crf": args.crf, "preset": args.preset,
            "pixel_mapping": "GREY unchanged; Y10P native value >> 2; H.264 yuv420p viewing copy",
            "timestamp_meaning": "Recorded V4L2 timestamps (see source flags), not measured optical exposure synchronization",
        }
        for record in selected:
            temporary = output / f"{record.name}.incomplete.mp4"
            hashes = {
                filename: sha256(run / filename)
                for filename in ("run.tsv", f"{record.name}.tsv", f"{record.name}.frames.tsv", f"{record.name}.capture.log")
            }
            video_origin = record.timestamps_us[0]
            encoded = encode(record, temporary, video_origin, args.crf, args.preset, args.threads)
            verified = verify_mp4(record, temporary, video_origin, ffprobe)
            for filename, digest in hashes.items():
                if sha256(run / filename) != digest:
                    raise ConversionError(f"Input metadata changed during conversion: {filename}")
            final = output / f"{record.name}.mp4"
            temporary.rename(final)
            camera_report = {**encoded, **verified, "input_metadata_sha256": hashes,
                             "original_start_offset_us": record.timestamps_us[0] - origin,
                             "mp4_sha256": sha256(final), "bytes": final.stat().st_size}
            report[record.name] = camera_report
            write_json(output / f"{record.name}.timing.json", camera_report)
            print(f"VERIFIED: {final} ({verified['verified_frames']} exact frame timestamps)", flush=True)
        write_json(output / "conversion.json", report)
        with (output / "status").open("w", encoding="utf-8") as marker:
            marker.write("VERIFIED_TIMESTAMPED_MP4\n")
            marker.flush()
            os.fsync(marker.fileno())
        if hasattr(os, "O_DIRECTORY"):
            directory_fd = os.open(output, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(directory_fd)
            finally:
                os.close(directory_fd)
        return 0
    except handled_errors as error:
        print(f"ERROR: {error}", file=sys.stderr)
        if output is not None:
            (output / "status").write_text(f"INVALID: {error}\n", encoding="utf-8")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
