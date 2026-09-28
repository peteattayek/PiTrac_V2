# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
"""Sensor-unit exposure conversion and checked V4L2 controls for focusing."""

from dataclasses import dataclass
import math
import re
import subprocess

MIN_EXPOSURE_US = 30
MAX_EXPOSURE_US = 500_000


class ExposureError(ValueError):
    """Invalid requested exposure."""


class ControlError(RuntimeError):
    """A hardware write/readback failed; the previous state must be restored."""


@dataclass(frozen=True)
class ExposureSetting:
    requested_us: float
    estimated_us: float
    lines: int
    vblank: int
    fps: float


@dataclass(frozen=True)
class Controls:
    exposure: int
    vblank: int
    gain: int


def exposure_setting(sensor: str, requested: object) -> ExposureSetting:
    if isinstance(requested, bool) or not isinstance(requested, (int, float)):
        raise ExposureError("Exposure must be a number of microseconds.")
    if not MIN_EXPOSURE_US <= requested <= MAX_EXPOSURE_US or not math.isfinite(requested):
        raise ExposureError(f"Exposure must be {MIN_EXPOSURE_US}..{MAX_EXPOSURE_US} microseconds.")
    if sensor == "mira220":
        row, offset, height, minimum_blank, margin = 304 / 38.4, 18.095, 1400, 18, 7
    elif sensor == "imx296":
        row, offset, height, minimum_blank, margin = 1100 / 74.25, 14.26, 1088, 30, 4
    else:
        raise ExposureError(f"Unsupported exposure sensor: {sensor}")
    lines = math.floor((requested - offset) / row + 0.5)
    vblank = max(minimum_blank, lines + margin - height)
    return ExposureSetting(float(requested), lines * row + offset, lines, vblank, 1e6 / (row * (height + vblank)))


class SensorControls:
    def __init__(self, subdevice: str, unity_gain: int) -> None:
        if not re.fullmatch(r"/dev/v4l-subdev\d+", subdevice):
            raise ExposureError("Invalid sensor subdevice path.")
        self.subdevice = subdevice
        self.unity_gain = unity_gain

    def _run(self, argument: str) -> str:
        try:
            result = subprocess.run(
                ["v4l2-ctl", "-d", self.subdevice, argument],
                capture_output=True, text=True, timeout=5, check=False,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise ControlError(f"{self.subdevice}: control command failed: {error}") from error
        if result.returncode:
            detail = (result.stderr or result.stdout).strip()[:400]
            raise ControlError(f"{self.subdevice}: v4l2-ctl exited {result.returncode}: {detail}")
        return result.stdout

    def read(self) -> Controls:
        output = self._run("--get-ctrl=exposure,vertical_blanking,analogue_gain")
        values: dict[str, int] = {}
        for line in output.splitlines():
            match = re.fullmatch(r"(exposure|vertical_blanking|analogue_gain): (\d+)(?: \(.*\))?", line)
            if match:
                if match[1] in values:
                    raise ControlError(f"{self.subdevice}: duplicate control readback.")
                values[match[1]] = int(match[2])
        if set(values) != {"exposure", "vertical_blanking", "analogue_gain"}:
            raise ControlError(f"{self.subdevice}: incomplete exposure/gain readback.")
        if values["analogue_gain"] != self.unity_gain:
            raise ControlError(f"{self.subdevice}: analogue gain is no longer unity.")
        return Controls(values["exposure"], values["vertical_blanking"], values["analogue_gain"])

    def write(self, desired: Controls) -> None:
        if desired.gain != self.unity_gain:
            raise ControlError("Preview cannot change analogue gain.")
        current = self.read()
        # Grow the available integration window before lengthening exposure.
        # Shorten exposure first when shrinking it to avoid driver clamping.
        writes = (
            (("vertical_blanking", desired.vblank), ("exposure", desired.exposure))
            if desired.vblank > current.vblank
            else (("exposure", desired.exposure), ("vertical_blanking", desired.vblank))
        )
        for name, value in writes:
            self._run(f"--set-ctrl={name}={value}")
        actual = self.read()
        if actual != desired:
            raise ControlError(f"{self.subdevice}: exposure readback mismatch: requested {desired}, got {actual}.")
