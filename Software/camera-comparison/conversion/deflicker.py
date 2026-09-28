# SPDX-License-Identifier: GPL-3.0-or-later
"""Recording-bound, display-only additive illumination correction."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image

from .convert import ConversionError, Recording, sha256


def timestamp_digest(timestamps: tuple[int, ...]) -> str:
    return hashlib.sha256(np.asarray(timestamps, dtype="<i8").tobytes()).hexdigest()


def quantization_offsets(width: int, height: int) -> np.ndarray:
    pattern = np.array([[0, 2], [3, 1]], dtype=np.float32)
    for _ in range(2):
        pattern = np.block([[4 * pattern, 4 * pattern + 2], [4 * pattern + 3, 4 * pattern + 1]])
    return np.tile((pattern + .5) / 64, ((height + 7) // 8, (width + 7) // 8))[:height, :width]


class IlluminationCorrection:
    def __init__(self, path: Path, record: Recording) -> None:
        with np.load(path, allow_pickle=False) as model:
            coefficient = np.asarray(model["coefficient"], dtype=np.float32)
            self.amounts = np.asarray(model["amounts"], dtype=np.float32)
            metadata = json.loads(str(model["metadata"]))
        stat = record.raw.stat()
        if (
            metadata["schema"] != "pitrac-additive-deflicker-v1" or record.fourcc != "GREY"
            or metadata["timestamps_sha256"] != timestamp_digest(record.timestamps_us)
            or metadata["source_dimensions"] != [record.width, record.height]
            or metadata["source_raw_bytes"] != stat.st_size
            or metadata["source_raw_mtime_ns"] != stat.st_mtime_ns
            or coefficient.ndim != 2 or not coefficient.size
            or coefficient.shape[0] > record.height or coefficient.shape[1] > record.width
            or self.amounts.shape != (len(record.timestamps_us),)
            or not np.isfinite(coefficient).all() or not np.isfinite(self.amounts).all()
            or np.any(coefficient < 0)
        ):
            raise ConversionError("Illumination model is invalid or belongs to another raw recording")
        maximum = float(np.max(np.abs(self.amounts)) * coefficient.max())
        if maximum > 8:
            raise ConversionError("Illumination model exceeds the supported eight-level display correction")
        self.size = (record.width, record.height)
        self.quantization = quantization_offsets(*self.size)
        with Image.fromarray(coefficient) as small:
            with small.resize(self.size, Image.Resampling.BILINEAR) as full:
                self.coefficient = np.array(full, dtype=np.float32)
        self.metadata = {
            "model": str(path), "model_sha256": sha256(path),
            "method": "add spatial response * (mean reference illumination - frame reference illumination)",
            "maximum_absolute_native_level_adjustment": maximum,
            "camera_frames_blended": False,
            "quantization": "fixed spatial ordered dither below one native level; no temporal random noise",
            "measurement_pixels": False,
            "limitations": "Display-only brightness correction; cannot restore clipped shadow detail or guarantee removal on moving surfaces.",
            "calibration": metadata,
        }

    def apply(self, image: Image.Image, source_index: int) -> None:
        """Correct only the caller's temporary gray view, never the source raw buffer."""
        if image.mode != "L" or image.size != self.size or not 0 <= source_index < len(self.amounts):
            raise ConversionError("Illumination correction received an incompatible image or source index")
        pixels = np.asarray(image, dtype=np.float32) + self.amounts[source_index] * self.coefficient
        # Preserve fractional low-light corrections without visible one-level contours.
        pixels = np.clip(np.floor(pixels + self.quantization), 0, 255).astype(np.uint8)
        with Image.fromarray(pixels) as corrected:
            image.paste(corrected)
