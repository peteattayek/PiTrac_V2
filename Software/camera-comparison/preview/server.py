# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
"""Latest-frame, loopback-only preview of the existing raw V4L2 profiles."""

from __future__ import annotations

import argparse
from collections import deque
from contextlib import nullcontext
import csv
from dataclasses import dataclass, field, replace
from datetime import datetime, timezone
import gzip
import hashlib
import io
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import logging
import os
from pathlib import Path
import re
import signal
import socket
import subprocess
import sys
import threading
import time
from typing import BinaryIO
from urllib.parse import parse_qs, urlsplit
from uuid import uuid4
from zipfile import ZIP_DEFLATED, ZIP_STORED, ZipFile
from .exposure import (
    MAX_EXPOSURE_US, MIN_EXPOSURE_US, ControlError, Controls, ExposureError,
    SensorControls, exposure_setting,
)

try:
    from PIL import Image, ImageChops
except ImportError:
    Image = None  # Raw transport still works without Pillow.
    ImageChops = None

LOG = logging.getLogger("pitrac.focus")
MAX_FRAME_AGE = 2.0
CAPTURE_LINE = re.compile(
    r"^\s*cap dqbuf:.*?\bseq:\s*(\d+)\s+bytesused:\s*(\d+).*?\bts:\s*(\d+\.\d+)"
)


class PreviewError(ValueError):
    """An actionable preview configuration or capture error."""


@dataclass(frozen=True)
class CameraConfig:
    name: str
    device: str
    width: int
    height: int
    fourcc: str
    stride: int
    sizeimage: int
    target_fps: float
    requested_exposure_us: float
    estimated_exposure_us: float
    subdevice: str = ""
    exposure_lines: int = 0
    vblank: int = 0

    def public(self) -> dict[str, object]:
        return {
            "name": self.name,
            "width": self.width,
            "height": self.height,
            "fourcc": self.fourcc,
            "stride": self.stride,
            "sizeimage": self.sizeimage,
            "target_fps": self.target_fps,
            "requested_exposure_us": self.requested_exposure_us,
            "estimated_exposure_us": self.estimated_exposure_us,
            "analogue_gain": 1.0,
            "exposure_lines": self.exposure_lines,
            "vertical_blanking": self.vblank,
        }


def read_config(path: Path, name: str) -> CameraConfig:
    fields: dict[str, str] = {}
    with path.open(encoding="utf-8", newline="") as source:
        for number, row in enumerate(csv.reader(source, delimiter="\t"), 1):
            if len(row) != 2 or not row[0] or not row[1] or row[0] in fields:
                raise PreviewError(f"{path}:{number}: malformed or duplicate configuration field")
            fields[row[0]] = row[1]
    try:
        config = CameraConfig(
            name, fields["video"], int(fields["width"]), int(fields["height"]),
            fields["fourcc"], int(fields["stride"]), int(fields["sizeimage"]),
            float(fields["fps"]), float(fields["requested_exposure_us"]),
            float(fields["estimated_exposure_us"]),
            fields["subdev"], int(fields["exposure_lines"]), int(fields["vblank"]),
        )
        gain = int(fields["gain_code"])
        identity_valid = fields["schema"] == "1" and fields["sensor"] == name
    except (KeyError, ValueError) as error:
        raise PreviewError(f"{path}: missing or invalid camera configuration: {error}") from error
    profiles = {
        "mira220": (1600, 1400, "GREY", 1, 1600),
        "imx296": (1456, 1088, "Y10P", 0, 1820),
    }
    if name not in profiles:
        raise PreviewError(f"Unsupported camera: {name}")
    width, height, fourcc, unity, minimum_stride = profiles[name]
    if (
        not identity_valid
        or (config.width, config.height, config.fourcc, gain) != (width, height, fourcc, unity)
        or not re.fullmatch(r"/dev/video\d+", config.device)
        or not re.fullmatch(r"/dev/v4l-subdev\d+", config.subdevice)
        or config.exposure_lines < 1 or config.vblank < 1
        or not minimum_stride <= config.stride <= 65536
        or config.stride % 16
        or config.sizeimage != config.stride * config.height
        or not 0 < config.target_fps <= 100
        or not 0 < config.requested_exposure_us <= 12000
        or not 0 < config.estimated_exposure_us <= 12000
    ):
        raise PreviewError(f"{path}: unsupported raw preview profile")
    return config


def read_frame(source: BinaryIO, size: int) -> bytes | None:
    """Read a whole native frame without interpreting or normalizing its pixels."""
    chunks: list[bytes] = []
    remaining = size
    while remaining:
        chunk = source.read(remaining)
        if not chunk:
            if chunks:
                raise PreviewError(f"Capture ended in a partial frame ({size - remaining}/{size} bytes)")
            return None
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def accepts_gzip(header: str) -> bool:
    for item in header.lower().split(","):
        parts = [part.strip() for part in item.split(";")]
        if parts[0] != "gzip":
            continue
        quality = 1.0
        for parameter in parts[1:]:
            if parameter.startswith("q="):
                try:
                    quality = float(parameter[2:])
                except ValueError:
                    return False
        return 0 < quality <= 1
    return False


def display_pixels(config: CameraConfig, frame: bytes) -> bytes:
    """Same 8-bit display mapping as decoder.js, without row padding or enhancement."""
    if len(frame) != config.sizeimage:
        raise PreviewError("Incorrect native frame length")
    width, height, stride = config.width, config.height, config.stride
    if config.fourcc == "GREY":
        return b"".join(frame[y * stride:y * stride + width] for y in range(height))
    if config.fourcc != "Y10P" or width % 4:
        raise PreviewError("Unsupported display format")
    pixels = bytearray(width * height)
    for y in range(height):
        row = frame[y * stride:y * stride + width // 4 * 5]
        # Four high bytes are exactly the native RAW10 values >> 2.
        for pixel in range(4):
            pixels[y * width + pixel:(y + 1) * width:4] = row[pixel::5]
    return bytes(pixels)


def encode_jpeg(config: CameraConfig, frame: bytes, quality: int) -> bytes:
    if Image is None:
        raise PreviewError("JPEG preview needs Pillow (python3-pil); raw viewing is available.")
    with Image.frombytes("L", (config.width, config.height), display_pixels(config, frame)) as picture:
        with io.BytesIO() as output:
            picture.save(output, format="JPEG", quality=quality)
            return output.getvalue()


def encode_png(config: CameraConfig, frame: bytes) -> bytes:
    """Preserve native sample values, including RAW10 low bits, without scaling."""
    if Image is None or ImageChops is None:
        raise PreviewError("Lossless PNG capture needs Pillow (python3-pil).")
    if len(frame) != config.sizeimage:
        raise PreviewError("Incorrect native frame length")
    if config.fourcc == "GREY":
        mode, pixels = "L", display_pixels(config, frame)
    elif config.fourcc == "Y10P" and config.width % 4 == 0:
        mode = "I;16"
        size = (config.width, config.height)
        low_bits = bytearray(config.width * config.height)
        tables = [bytes((value >> (pixel * 2)) & 3 for value in range(256)) for pixel in range(4)]
        for y in range(config.height):
            row = frame[y * config.stride + 4:y * config.stride + config.width // 4 * 5:5]
            for pixel, table in enumerate(tables):
                low_bits[y * config.width + pixel:(y + 1) * config.width:4] = row.translate(table)
        # Rebuild byte planes in bulk rather than blocking capture threads in a per-pixel loop.
        unpacked = bytearray(config.width * config.height * 2)
        with Image.frombytes("L", size, display_pixels(config, frame)) as high, \
                Image.frombytes("L", size, bytes(low_bits)) as low:
            with high.point([(value << 2) & 255 for value in range(256)]) as base, \
                    high.point([value >> 6 for value in range(256)]) as upper, \
                    ImageChops.add(base, low) as lower:
                unpacked[0::2] = lower.tobytes()
                unpacked[1::2] = upper.tobytes()
        pixels = bytes(unpacked)
    else:
        raise PreviewError("Unsupported lossless capture format")
    with Image.frombytes(mode, (config.width, config.height), pixels) as picture:
        with io.BytesIO() as output:
            picture.save(output, format="PNG", compress_level=1)
            return output.getvalue()


class FrameStore:
    def __init__(self, config: CameraConfig) -> None:
        self.config = config
        self._lock = threading.Lock()
        self._frame: bytes | None = None
        self._frame_id = 0
        self._received_at: float | None = None
        self._error: str | None = None
        self._stopped = False
        self._timestamps: deque[float] = deque(maxlen=120)
        self._diagnostics: deque[str] = deque(maxlen=12)
        self._timing_warnings = 0
        self._diagnostic_frames = 0
        self._generation = 0

    def reconfigure(self, config: CameraConfig) -> None:
        with self._lock:
            self.config = config
            self._generation += 1
            self._frame = None
            self._received_at = None
            self._error = None
            self._stopped = False
            self._timestamps.clear()
            self._diagnostics.clear()
            self._timing_warnings = 0
            self._diagnostic_frames = 0

    @property
    def generation(self) -> int:
        with self._lock:
            return self._generation

    def publish(self, frame: bytes) -> None:
        if len(frame) != self.config.sizeimage:
            raise PreviewError("Capture frame length differs from the configured sizeimage")
        with self._lock:
            if self._error is not None or self._stopped:
                return
            self._frame = frame
            self._frame_id += 1
            self._received_at = time.monotonic()

    def fail(self, message: str) -> None:
        with self._lock:
            if self._error is None:
                LOG.error("%s: %s", self.config.name, message)
                self._error = message

    def stop(self) -> None:
        with self._lock:
            self._stopped = True

    def diagnostic(self, line: str) -> None:
        match = CAPTURE_LINE.search(line)
        with self._lock:
            if match:
                self._diagnostic_frames += 1
                if "error" in line.lower() or int(match.group(2)) != self.config.sizeimage:
                    self._diagnostics.append(line.strip()[:500])
                    self._error = "Capture reported an error or unexpected frame size; stop and inspect the camera."
                    LOG.error("%s: %s", self.config.name, self._error)
                    return
                if self._diagnostic_frames <= 5:
                    return
                timestamp = float(match.group(3))
                if self._timestamps:
                    interval = timestamp - self._timestamps[-1]
                    if not 0.5 / self.config.target_fps <= interval <= 1.5 / self.config.target_fps:
                        self._timing_warnings += 1
                self._timestamps.append(timestamp)
            elif line.lstrip().startswith("cap dqbuf:"):
                self._diagnostics.append(line.strip()[:500])
                self._error = "Unsupported capture metadata format; preview cannot report reliable camera state."
                LOG.error("%s: %s", self.config.name, self._error)
            elif line.strip():
                self._diagnostics.append(line.strip()[:500])

    def snapshot(self, after: int = 0) -> tuple[int, bytes] | None:
        result = self.snapshot_with_time(after)
        return None if result is None else result[:2]

    def snapshot_with_time(self, after: int = 0) -> tuple[int, bytes, float] | None:
        with self._lock:
            if self._error is not None:
                raise PreviewError(self._error)
            if self._stopped:
                raise PreviewError("Preview capture is stopped.")
            if self._frame is None or self._received_at is None:
                raise PreviewError("Waiting for the first raw frame.")
            if time.monotonic() - self._received_at > MAX_FRAME_AGE:
                raise PreviewError("No recent raw frame; this camera is not live.")
            if self._frame_id <= after:
                return None
            return self._frame_id, self._frame, self._received_at

    def status(self) -> dict[str, object]:
        with self._lock:
            age = None if self._received_at is None else time.monotonic() - self._received_at
            error = self._error
            if error is not None:
                state = "error"
            elif self._stopped:
                state = "stopped"
            elif age is None:
                state = "starting"
            elif age > MAX_FRAME_AGE:
                state, error = "error", "No recent raw frame; this camera is not live."
            else:
                state = "live"
            fps = None
            if len(self._timestamps) > 1:
                span = self._timestamps[-1] - self._timestamps[0]
                if span > 0:
                    fps = (len(self._timestamps) - 1) / span
            return {
                **self.config.public(),
                "state": state,
                "error": error,
                "frames_received": self._frame_id,
                "frame_id": self._frame_id if self._frame is not None else None,
                "frame_age_ms": None if age is None else round(age * 1000, 1),
                "capture_fps": fps,
                "timing_warnings": self._timing_warnings,
                "diagnostics": list(self._diagnostics),
                "exposure_generation": self._generation,
            }


class CaptureWorker:
    def __init__(self, store: FrameStore, buffers: int) -> None:
        self.store = store
        self.buffers = buffers
        self.process: subprocess.Popen[bytes] | None = None
        self._threads: list[threading.Thread] = []
        self._stopping = threading.Event()
        self._started_at = 0.0

    def start(self) -> None:
        self._stopping.clear()
        read_fd, write_fd = os.pipe()
        try:
            process = subprocess.Popen(
                [
                    "v4l2-ctl", "-d", self.store.config.device, "--verbose",
                    "--stream-no-query", f"--stream-mmap={self.buffers}",
                    "--stream-poll", "--stream-skip=5", "--stream-count=0",
                    f"--stream-to=/proc/self/fd/{write_fd}",
                ],
                pass_fds=(write_fd,),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
        except OSError:
            os.close(read_fd)
            raise
        finally:
            os.close(write_fd)
        self.process = process
        self._started_at = time.monotonic()
        assert process.stdout is not None
        data = os.fdopen(read_fd, "rb", buffering=0)
        self._threads = [
            threading.Thread(target=self._read_frames, args=(data,), daemon=True),
            threading.Thread(target=self._read_diagnostics, args=(process.stdout,), daemon=True),
        ]
        for thread in self._threads:
            thread.start()

    def _read_frames(self, source: BinaryIO) -> None:
        try:
            with source:
                while not self._stopping.is_set():
                    frame = read_frame(source, self.store.config.sizeimage)
                    if frame is None:
                        if not self._stopping.is_set():
                            self.store.fail("Camera capture ended unexpectedly. See capture diagnostics.")
                        return
                    self.store.publish(frame)
        except (OSError, PreviewError) as error:
            if not self._stopping.is_set():
                self.store.fail(str(error))

    def _read_diagnostics(self, source: BinaryIO) -> None:
        try:
            with source:
                for line in source:
                    self.store.diagnostic(line.decode("utf-8", errors="replace").rstrip())
        except OSError as error:
            if not self._stopping.is_set():
                self.store.fail(f"Cannot read capture diagnostics: {error}")

    def check(self) -> None:
        if self.process is None or self._stopping.is_set():
            return
        if self.process.poll() is not None:
            self.store.fail(f"v4l2-ctl exited with status {self.process.returncode}. See diagnostics.")
        status = self.store.status()
        if status["state"] == "starting" and time.monotonic() - self._started_at > 10:
            self.store.fail("Timed out waiting for the first camera frame.")
        if self.store.status()["state"] == "error":
            error = self.store.status()["error"]
            self.store.fail(error if isinstance(error, str) else "Camera capture failed.")
            self.stop()

    def stop(self) -> None:
        self._stopping.set()
        process = self.process
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                LOG.warning("%s: forcing a stalled capture process to exit", self.store.config.name)
                process.kill()
                process.wait(timeout=3)
        for thread in self._threads:
            thread.join(timeout=3)
            if thread.is_alive():
                raise ControlError(f"{self.store.config.name}: capture reader did not stop cleanly.")
        self.store.stop()


@dataclass
class PreviewState:
    stores: dict[str, FrameStore]
    preview_fps: int = 5
    transport: str = "raw"
    jpeg_quality: int = 90
    session_id: str = field(default_factory=lambda: uuid4().hex)
    exposure_control: ExposureController | None = None
    capture_lock: threading.Lock = field(default_factory=threading.Lock, init=False, repr=False)

    def status(self) -> dict[str, object]:
        with self.exposure_control.lock if self.exposure_control is not None else nullcontext():
            cameras = [store.status() for store in self.stores.values()]
            capture_error = None
            if Image is None:
                capture_error = "Lossless PNG capture needs Pillow (python3-pil)."
            elif not {"mira220", "imx296"} <= self.stores.keys():
                capture_error = "Capture requires both Mira220 and IMX296."
            elif any(camera["state"] != "live" for camera in cameras):
                capture_error = "Waiting for recent raw frames from both cameras."
            result: dict[str, object] = {
                "preview_fps": self.preview_fps,
                "preview_transport": self.transport,
                "transports": ["raw", "jpeg"] if Image is not None else ["raw"],
                "jpeg_quality": self.jpeg_quality,
                "session_id": self.session_id,
                "server_monotonic": time.monotonic(),
                "saves_frames": False,
                "snapshot_capture": {
                    "available": capture_error is None,
                    "error": capture_error,
                    "destination": "browser-download",
                },
                "isp": False,
                "cameras": cameras,
            }
            if self.exposure_control is not None:
                result["exposure_control"] = self.exposure_control.public()
            return result

    def capture_archive(self, requested_session: str) -> tuple[str, bytes]:
        frames: list[tuple[CameraConfig, int, bytes, float]] = []
        with self.exposure_control.lock if self.exposure_control is not None else nullcontext():
            if requested_session != self.session_id:
                raise ExposureConflict("Preview or exposure changed; wait for fresh status and capture again.")
            if Image is None:
                raise PreviewError("Lossless PNG capture needs Pillow (python3-pil).")
            if not {"mira220", "imx296"} <= self.stores.keys():
                raise PreviewError("Capture requires both Mira220 and IMX296.")
            for name in ("mira220", "imx296"):
                store = self.stores[name]
                try:
                    snapshot = store.snapshot_with_time()
                except PreviewError as error:
                    raise PreviewError(f"{name}: {error}") from error
                if snapshot is None:
                    raise PreviewError(f"{name}: no raw frame is available.")
                frames.append((store.config, *snapshot))
            selected_at = time.monotonic()
            selected_utc = datetime.now(timezone.utc)
        # Immutable buffers and configs stay paired even if exposure changes while encoding.
        capture_id = uuid4().hex
        filename = f"pitrac-capture-{selected_utc:%Y%m%dT%H%M%S%fZ}-{capture_id}.zip"
        cameras: list[dict[str, object]] = []
        metadata = {
            "schema": 1,
            "capture_id": capture_id,
            "session_id": requested_session,
            "selected_utc": selected_utc.isoformat(),
            "selected_monotonic_seconds": selected_at,
            "selection": "Latest live native frame from each camera when the request is handled.",
            "hardware_synchronized": False,
            "timestamp_meaning": "Pi frame-receipt times, not sensor exposure timestamps.",
            "receive_time_difference_ms": abs(frames[0][3] - frames[1][3]) * 1000,
            "isp": False,
            "cameras": cameras,
        }
        with io.BytesIO() as output:
            with ZipFile(output, "w", compression=ZIP_DEFLATED, compresslevel=1) as archive:
                for config, frame_id, raw, received_at in frames:
                    png = encode_png(config, raw)
                    raw_name, png_name = f"{config.name}.raw", f"{config.name}.png"
                    archive.writestr(raw_name, raw)
                    archive.writestr(png_name, png, compress_type=ZIP_STORED)
                    cameras.append({
                        **config.public(),
                        "frame_id": frame_id,
                        "received_monotonic_seconds": received_at,
                        "age_at_selection_ms": (selected_at - received_at) * 1000,
                        "native_bit_depth": 8 if config.fourcc == "GREY" else 10,
                        "png_bit_depth": 8 if config.fourcc == "GREY" else 16,
                        "png_sample_mapping": "Native unsigned values unchanged; no shift or scaling.",
                        "raw_file": raw_name, "raw_bytes": len(raw),
                        "raw_sha256": hashlib.sha256(raw).hexdigest(),
                        "png_file": png_name, "png_bytes": len(png),
                        "png_sha256": hashlib.sha256(png).hexdigest(),
                    })
                archive.writestr("metadata.json", json.dumps(metadata, indent=2) + "\n")
                archive.writestr("README.txt",
                    "Paired full-resolution native camera frames; NOT hardware synchronized.\n"
                    "Mira220 PNG: unchanged 8-bit grayscale samples (0..255).\n"
                    "IMX296 PNG: all 10-bit samples (0..1023) in a 16-bit grayscale PNG.\n"
                    "IMX296 is NOT stretched to 65535 and may look dark in normal viewers.\n"
                    "No resize, gamma, brightness normalization, JPEG or ISP processing.\n"
                    "RAW files are byte-exact native buffers, including row padding.\n"
                    "Use metadata.json for dimensions, stride, format, exposure and SHA-256.\n"
                    "Timestamps describe receipt on the Pi, not sensor exposure timing.\n"
                    "Preview transport and display zoom do not affect these files.\n")
            return filename, output.getvalue()


class ExposureConflict(PreviewError):
    """Another browser or preview generation changed the controls."""


class ExposureController:
    def __init__(self, state: PreviewState, workers: list[CaptureWorker]) -> None:
        self.state = state
        self.workers = workers
        self.lock = threading.RLock()
        self.original = {name: store.config for name, store in state.stores.items()}
        self.controls = {
            name: SensorControls(config.subdevice, 1 if name == "mira220" else 0)
            for name, config in self.original.items()
        }
        self.linked = True
        self.revision = 0
        self.error: str | None = None
        self.closed = False

    def public(self) -> dict[str, object]:
        with self.lock:
            return {
                "linked": self.linked, "revision": self.revision,
                "min_us": MIN_EXPOSURE_US, "max_us": MAX_EXPOSURE_US,
                "error": self.error, "available": not self.closed and self.error is None,
                "restores_on_stop": True,
                "requested_us": {name: store.config.requested_exposure_us for name, store in self.state.stores.items()},
            }

    def _expected(self, name: str, config: CameraConfig) -> Controls:
        return Controls(config.exposure_lines, config.vblank, self.controls[name].unity_gain)

    def _stop_workers(self) -> None:
        for worker in self.workers:
            worker.stop()

    def _start_workers(self, configs: dict[str, CameraConfig]) -> None:
        self.state.session_id = uuid4().hex
        for name, config in configs.items():
            self.state.stores[name].reconfigure(config)
        for worker in self.workers:
            worker.start()

    def check(self) -> None:
        with self.lock:
            for worker in self.workers:
                worker.check()

    def apply(self, payload: object) -> dict[str, object]:
        with self.lock:
            if self.closed or self.error:
                raise ControlError(self.error or "Preview is stopping.")
            if not isinstance(payload, dict) or set(payload) != {"linked", "exposures_us", "revision", "session_id"}:
                raise ExposureError("Expected linked, exposures_us, revision and session_id.")
            if type(payload["linked"]) is not bool or type(payload["revision"]) is not int:
                raise ExposureError("Invalid link state or revision.")
            if payload["revision"] != self.revision or payload["session_id"] != self.state.session_id:
                raise ExposureConflict("Controls changed or preview restarted. Refresh the values and retry.")
            requested = payload["exposures_us"]
            if not isinstance(requested, dict) or set(requested) != set(self.state.stores):
                raise ExposureError("Supply one exposure for each configured camera.")
            settings = {name: exposure_setting(name, value) for name, value in requested.items()}
            if payload["linked"] and len({setting.requested_us for setting in settings.values()}) != 1:
                raise ExposureError("Linked cameras must use the same requested exposure.")
            before = {name: store.config for name, store in self.state.stores.items()}
            desired = {
                name: replace(before[name], requested_exposure_us=setting.requested_us,
                              estimated_exposure_us=setting.estimated_us, exposure_lines=setting.lines,
                              vblank=setting.vblank, target_fps=setting.fps)
                for name, setting in settings.items()
            }
            changed = any(desired[name] != before[name] for name in before)
            if changed:
                for name, config in before.items():
                    if self.controls[name].read() != self._expected(name, config):
                        raise ControlError(f"{name}: controls changed outside preview. Stop and reconfigure.")
                try:
                    self._stop_workers()
                    for name, config in desired.items():
                        self.controls[name].write(self._expected(name, config))
                    self._start_workers(desired)
                except (OSError, ControlError, subprocess.SubprocessError) as failure:
                    LOG.error("Exposure update failed: %s; restoring previous controls", failure)
                    recovery_errors: list[str] = []
                    try:
                        self._stop_workers()
                    except (OSError, ControlError, subprocess.SubprocessError) as error:
                        recovery_errors.append(str(error))
                    for name, config in before.items():
                        try:
                            self.controls[name].write(self._expected(name, config))
                        except ControlError as error:
                            recovery_errors.append(str(error))
                    if not recovery_errors:
                        try:
                            self._start_workers(before)
                        except (OSError, ControlError, subprocess.SubprocessError) as error:
                            recovery_errors.append(str(error))
                    if recovery_errors:
                        self.error = "Exposure update and rollback failed: " + "; ".join(recovery_errors)
                        for store in self.state.stores.values():
                            store.fail(self.error)
                        LOG.error("%s", self.error)
                        raise ControlError(self.error) from failure
                    raise ControlError(f"Exposure update failed; previous settings restored: {failure}") from failure
            self.linked = payload["linked"]
            self.revision += 1
            return self.public()

    def close(self) -> None:
        with self.lock:
            self.closed = True
            self._stop_workers()
            failures: list[str] = []
            for name, config in self.original.items():
                try:
                    self.controls[name].write(self._expected(name, config))
                except ControlError as error:
                    failures.append(str(error))
            if failures:
                raise ControlError("Could not restore startup exposure settings: " + "; ".join(failures))


class PreviewHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, port: int, state: PreviewState, web_root: Path) -> None:
        self._slots = threading.BoundedSemaphore(8)
        super().__init__(("127.0.0.1", port), make_handler(state, web_root))

    def process_request(self, request: socket.socket, client_address: tuple[str, int]) -> None:
        request.settimeout(5)
        request.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        if not self._slots.acquire(blocking=False):
            try:
                request.sendall(b"HTTP/1.0 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n")
            except (BrokenPipeError, ConnectionResetError, TimeoutError):
                LOG.debug("Busy preview client disconnected")
            finally:
                self.shutdown_request(request)
            return
        try:
            super().process_request(request, client_address)
        except RuntimeError:
            self._slots.release()
            raise

    def process_request_thread(self, request: socket.socket, client_address: tuple[str, int]) -> None:
        try:
            super().process_request_thread(request, client_address)
        finally:
            self._slots.release()


def make_handler(state: PreviewState, web_root: Path) -> type[BaseHTTPRequestHandler]:
    static = {
        "/": ("index.html", "text/html; charset=utf-8"),
        "/index.html": ("index.html", "text/html; charset=utf-8"),
        "/preview.js": ("preview.js", "text/javascript; charset=utf-8"),
        "/decoder.js": ("decoder.js", "text/javascript; charset=utf-8"),
        "/stream.js": ("stream.js", "text/javascript; charset=utf-8"),
        "/exposure.js": ("exposure.js", "text/javascript; charset=utf-8"),
        "/capture.js": ("capture.js", "text/javascript; charset=utf-8"),
        "/preview.css": ("preview.css", "text/css; charset=utf-8"),
    }

    class Handler(BaseHTTPRequestHandler):
        server_version = "PiTracFocus/1"

        def log_message(self, format: str, *args: object) -> None:
            LOG.debug(format, *args)

        def _send(
            self, code: HTTPStatus, body: bytes, content_type: str,
            extra: dict[str, str] | None = None,
        ) -> None:
            self.send_response(code)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Referrer-Policy", "no-referrer")
            self.send_header("Content-Security-Policy", "default-src 'self'; frame-ancestors 'none'; object-src 'none'")
            if extra:
                for name, value in extra.items():
                    self.send_header(name, value)
            self.end_headers()
            if body:
                self.wfile.write(body)

        def _json(self, code: HTTPStatus, value: dict[str, object]) -> None:
            self._send(code, json.dumps(value).encode(), "application/json")

        def _stream(self, name: str) -> None:
            if name not in state.stores:
                self._json(HTTPStatus.NOT_FOUND, {"error": "Unknown camera"})
                return
            if Image is None:
                self._json(HTTPStatus.SERVICE_UNAVAILABLE, {"error": "JPEG streaming needs python3-pil"})
                return
            store = state.stores[name]
            generation = store.generation
            try:
                store.snapshot()
            except PreviewError as error:
                self._json(HTTPStatus.SERVICE_UNAVAILABLE, {"error": str(error)})
                return
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=pitrac-frame")
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("X-Preview-Session", state.session_id)
            self.send_header("Connection", "close")
            self.end_headers()
            self.close_connection = True
            # Bound unsent data. A slow consumer blocks this sender, then receives
            # the newest capture; there is no application queue of encoded frames.
            self.connection.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 65536)
            self.connection.settimeout(2)
            after = 0
            period = 1 / state.preview_fps
            while store.generation == generation:
                started = time.monotonic()
                try:
                    snapshot = store.snapshot_with_time(after)
                except PreviewError:
                    return  # End stream on capture failure; never repeat a frozen JPEG.
                if snapshot is not None:
                    frame_id, raw, captured_at = snapshot
                    frame = encode_jpeg(store.config, raw, state.jpeg_quality)
                    if store.generation != generation:
                        return
                    header = ("--pitrac-frame\r\nContent-Type: image/jpeg\r\n"
                              f"Content-Length: {len(frame)}\r\nX-Frame-Id: {frame_id}\r\n"
                              f"X-Capture-Monotonic: {captured_at:.9f}\r\n\r\n").encode("ascii")
                    self.wfile.write(header + frame + b"\r\n")
                    after = frame_id
                time.sleep(max(0, period - (time.monotonic() - started)))

        def do_GET(self) -> None:
            try:
                self._get()
            except (BrokenPipeError, ConnectionResetError, TimeoutError):
                LOG.debug("Preview client disconnected")

        def _local_request(self, require_origin: bool = False) -> bool:
            try:
                authority = urlsplit("//" + self.headers.get("Host", ""))
                if authority.hostname not in ("127.0.0.1", "localhost", "::1") or authority.username is not None:
                    self._json(HTTPStatus.FORBIDDEN, {"error": "Use a localhost SSH tunnel."})
                    return False
                origin = self.headers.get("Origin")
                if (require_origin or origin is not None) and origin != "http://" + self.headers.get("Host", ""):
                    self._json(HTTPStatus.FORBIDDEN, {"error": "Cross-origin requests are not allowed."})
                    return False
            except ValueError:
                self._json(HTTPStatus.BAD_REQUEST, {"error": "Invalid request URL"})
                return False
            return True

        def do_POST(self) -> None:
            try:
                self._post()
            except (BrokenPipeError, ConnectionResetError, TimeoutError):
                LOG.debug("Preview POST client disconnected; check downloads or exposure status")

        def _capture(self, payload: object) -> None:
            if (not isinstance(payload, dict) or set(payload) != {"session_id"}
                    or not isinstance(payload["session_id"], str)):
                self._json(HTTPStatus.BAD_REQUEST, {"error": "Capture requires the current session_id."})
                return
            if not state.capture_lock.acquire(blocking=False):
                self._json(HTTPStatus.CONFLICT, {"error": "Another capture download is in progress; try again shortly."})
                return
            try:
                try:
                    filename, body = state.capture_archive(payload["session_id"])
                except ExposureConflict as error:
                    self._json(HTTPStatus.CONFLICT, {"error": str(error)})
                    return
                except PreviewError as error:
                    self._json(HTTPStatus.SERVICE_UNAVAILABLE, {"error": str(error)})
                    return
                except OSError:
                    LOG.exception("Could not encode lossless capture")
                    self._json(HTTPStatus.INTERNAL_SERVER_ERROR, {"error": "Lossless capture encoding failed; inspect the preview log."})
                    return
                self._send(HTTPStatus.OK, body, "application/zip", {
                    "Content-Disposition": f'attachment; filename="{filename}"',
                    "X-Preview-Session": payload["session_id"],
                })
            finally:
                state.capture_lock.release()

        def _post(self) -> None:
            if not self._local_request(require_origin=True):
                return
            if self.path not in ("/api/exposure", "/api/capture"):
                self._json(HTTPStatus.NOT_FOUND, {"error": "Not found"})
                return
            if self.headers.get("Content-Type", "").split(";")[0].strip().lower() != "application/json":
                self._json(HTTPStatus.UNSUPPORTED_MEDIA_TYPE, {"error": "Preview POST requests require JSON."})
                return
            length = self.headers.get("Content-Length", "")
            if self.headers.get("Transfer-Encoding") is not None or not re.fullmatch(r"\d{1,4}", length) or not 0 < int(length) <= 2048:
                self._json(HTTPStatus.BAD_REQUEST, {"error": "Invalid preview request length."})
                return
            try:
                payload = json.loads(self.rfile.read(int(length)))
            except (ValueError, UnicodeError):
                self._json(HTTPStatus.BAD_REQUEST, {"error": "Malformed preview request."})
                return
            if self.path == "/api/capture":
                self._capture(payload)
                return
            if state.exposure_control is None:
                self._json(HTTPStatus.SERVICE_UNAVAILABLE, {"error": "Exposure controls are unavailable."})
                return
            try:
                state.exposure_control.apply(payload)
            except ExposureError as error:
                self._json(HTTPStatus.BAD_REQUEST, {"error": str(error)})
                return
            except ExposureConflict as error:
                self._json(HTTPStatus.CONFLICT, {"error": str(error)})
                return
            except ControlError as error:
                LOG.error("Exposure control: %s", error)
                self._json(HTTPStatus.SERVICE_UNAVAILABLE, {"error": str(error)})
                return
            self._json(HTTPStatus.OK, state.status())

        def _get(self) -> None:
            if not self._local_request():
                return
            try:
                url = urlsplit(self.path)
            except ValueError:
                self._json(HTTPStatus.BAD_REQUEST, {"error": "Invalid request URL"})
                return
            if url.path == "/api/status":
                self._json(HTTPStatus.OK, state.status())
                return
            if url.path.startswith("/api/stream/"):
                if url.query:
                    self._json(HTTPStatus.BAD_REQUEST, {"error": "Stream does not accept query parameters"})
                    return
                self._stream(url.path.removeprefix("/api/stream/"))
                return
            if url.path.startswith("/api/frame/"):
                name = url.path.removeprefix("/api/frame/")
                if name not in state.stores:
                    self._json(HTTPStatus.NOT_FOUND, {"error": "Unknown camera"})
                    return
                query = parse_qs(url.query, keep_blank_values=True)
                after_values = query.get("after", ["0"])
                formats = query.get("format", ["raw"])
                if (set(query) - {"after", "format"} or len(after_values) != 1
                        or not re.fullmatch(r"\d{1,16}", after_values[0])
                        or len(formats) != 1 or formats[0] not in ("raw", "jpeg")):
                    self._json(HTTPStatus.BAD_REQUEST, {"error": "Invalid frame cursor"})
                    return
                frame_session = state.session_id
                try:
                    snapshot = state.stores[name].snapshot(int(after_values[0]))
                except PreviewError as error:
                    self._json(HTTPStatus.SERVICE_UNAVAILABLE, {"error": str(error)})
                    return
                if snapshot is None:
                    self._send(HTTPStatus.NO_CONTENT, b"", "application/octet-stream")
                    return
                frame_id, frame = snapshot
                headers = {"X-Frame-Id": str(frame_id), "X-Preview-Session": frame_session}
                content_type = "application/octet-stream"
                if formats[0] == "jpeg":
                    try:
                        frame = encode_jpeg(state.stores[name].config, frame, state.jpeg_quality)
                    except PreviewError as error:
                        self._json(HTTPStatus.SERVICE_UNAVAILABLE, {"error": str(error)})
                        return
                    content_type = "image/jpeg"
                elif accepts_gzip(self.headers.get("Accept-Encoding", "")):
                    frame = gzip.compress(frame, compresslevel=1, mtime=0)
                    headers["Content-Encoding"] = "gzip"
                headers["Vary"] = "Accept-Encoding"
                if frame_session != state.session_id:
                    self._json(HTTPStatus.CONFLICT, {"error": "Exposure changed; request a new frame."})
                    return
                self._send(HTTPStatus.OK, frame, content_type, headers)
                return
            if url.path in static:
                filename, content_type = static[url.path]
                try:
                    body = (web_root / filename).read_bytes()
                except OSError as error:
                    LOG.error("Cannot serve preview asset %s: %s", filename, error)
                    self._json(HTTPStatus.INTERNAL_SERVER_ERROR, {"error": "Preview asset unavailable"})
                    return
                self._send(HTTPStatus.OK, body, content_type)
                return
            self._json(HTTPStatus.NOT_FOUND, {"error": "Not found"})

    return Handler


def verify_inherited_lock() -> None:
    if sys.platform != "linux":
        raise PreviewError("Live capture requires the Pi. Use focus-preview.sh, not this module directly.")
    import fcntl

    runtime = Path(os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}"))
    expected = (runtime / "pitrac-camera-comparison.lock").stat()
    try:
        held = os.fstat(9)
    except OSError as error:
        raise PreviewError("Camera lock is missing. Launch with scripts/focus-preview.sh.") from error
    if (held.st_dev, held.st_ino, held.st_uid) != (expected.st_dev, expected.st_ino, os.getuid()):
        raise PreviewError("Camera lock identity is wrong. Launch with scripts/focus-preview.sh.")
    fcntl.flock(9, fcntl.LOCK_EX | fcntl.LOCK_NB)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--preview-fps", type=int, default=10)
    parser.add_argument("--buffers", type=int, default=32)
    parser.add_argument("--transport", choices=("jpeg", "raw"), default="jpeg")
    parser.add_argument("--jpeg-quality", type=int, default=90)
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    if (not 1024 <= args.port <= 65535 or not 1 <= args.preview_fps <= 10
            or not 8 <= args.buffers <= 32 or not 60 <= args.jpeg_quality <= 95):
        parser.error("Unsupported port, preview rate or buffer count")
    try:
        verify_inherited_lock()
        if args.transport == "jpeg" and Image is None:
            raise PreviewError("JPEG preview needs python3-pil. Install it or select --transport raw.")
        configs = [
            read_config(args.config / f"{name}.tsv", name)
            for name in ("mira220", "imx296")
            if (args.config / f"{name}.tsv").is_file()
        ]
        if not configs:
            raise PreviewError("No camera configurations found")
        state = PreviewState({config.name: FrameStore(config) for config in configs},
                             args.preview_fps, args.transport, args.jpeg_quality)
        web_root = Path(__file__).resolve().parent.parent / "web"
        for asset in ("index.html", "preview.js", "decoder.js", "stream.js", "exposure.js", "preview.css"):
            if not (web_root / asset).is_file():
                raise PreviewError(f"Missing preview asset: {asset}")
        server = PreviewHTTPServer(args.port, state, web_root)
    except (OSError, PreviewError) as error:
        LOG.error("%s", error)
        return 1
    workers = [CaptureWorker(store, args.buffers) for store in state.stores.values()]
    exposure_control = ExposureController(state, workers)
    state.exposure_control = exposure_control
    stop = threading.Event()
    signal.signal(signal.SIGTERM, lambda _signum, _frame: stop.set())
    signal.signal(signal.SIGINT, lambda _signum, _frame: stop.set())
    http_thread = threading.Thread(target=server.serve_forever, daemon=True)
    http_thread.start()
    result = 0
    try:
        for worker in workers:
            worker.start()
        LOG.info("Raw focus preview: http://127.0.0.1:%d (SSH tunnel required from another computer)", args.port)
        LOG.info("No image files are written. Stop this preview before recording.")
        LOG.info("Browser default: %s at up to %d fps; capture settings unchanged", args.transport, args.preview_fps)
        while not stop.wait(0.2):
            exposure_control.check()
    except (OSError, ControlError, subprocess.SubprocessError) as error:
        LOG.error("Could not start preview capture: %s", error)
        result = 1
    finally:
        try:
            exposure_control.close()
        except (OSError, ControlError, subprocess.SubprocessError) as error:
            LOG.error("Preview shutdown: %s. Reconfigure cameras before recording.", error)
            result = 1
        server.shutdown()
        server.server_close()
        http_thread.join(timeout=5)
    return result


if __name__ == "__main__":
    raise SystemExit(main())
