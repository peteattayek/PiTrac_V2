"""Save three native calibration frames without changing camera controls."""
from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import os
import re
from pathlib import Path
import time
from typing import Any
from urllib.parse import urlsplit
from urllib.request import Request, urlopen
from uuid import uuid4
from zipfile import ZipFile

import cv2
import numpy as np

from validate_record import ROOT, load_json, validate

MAX_ARCHIVE_BYTES = 64 * 1024 * 1024


def local_url(value: str) -> str:
    parsed = urlsplit(value)
    if (parsed.scheme != "http" or parsed.hostname not in ("127.0.0.1", "localhost", "::1")
            or parsed.username or parsed.password or parsed.query or parsed.fragment
            or parsed.path not in ("", "/")):
        raise ValueError("Use a loopback HTTP preview URL through the existing SSH tunnel")
    return value.rstrip("/")


def request_json(url: str) -> dict[str, Any]:
    with urlopen(url, timeout=10) as response:
        data = response.read(65537)
        if len(data) > 65536:
            raise ValueError("Oversized preview status")
    return json.loads(data)


def check_status(status: dict[str, Any], session: dict[str, Any]) -> str:
    acquisition = session["acquisition"]
    camera = next((c for c in status["cameras"] if c["name"] == acquisition["capture_alias"]), None)
    if camera is None or camera["state"] != "live":
        raise ValueError("Selected camera is not live")
    expected = (
        "Y12P", 12, 1600, 1400, acquisition["exposure_us"], acquisition["gain"],
    )
    actual = tuple(camera[key] for key in (
        "fourcc", "native_bit_depth", "width", "height", "requested_exposure_us", "analogue_gain",
    ))
    if actual != expected:
        raise ValueError(f"Camera settings differ from the frozen session: {actual!r}")
    epoch = status["session_id"]
    if session["preview_session_id"] is not None and session["preview_session_id"] != epoch:
        raise ValueError("Preview session changed; review the optical state and start a new calibration session")
    return epoch


def check_archive(data: bytes, session: dict[str, Any], epoch: str) -> dict[str, Any]:
    alias = session["acquisition"]["capture_alias"]
    with ZipFile(io.BytesIO(data)) as archive:
        if len(archive.infolist()) > 32 or sum(info.file_size for info in archive.infolist()) > MAX_ARCHIVE_BYTES:
            raise ValueError("Oversized unpacked capture")
        if archive.testzip() is not None:
            raise ValueError("Capture archive checksum failed")
        metadata = json.loads(archive.read("metadata.json"))
        if metadata["session_id"] != epoch or len(metadata["cameras"]) != 1:
            raise ValueError("Capture came from a different preview session or camera selection")
        camera = metadata["cameras"][0]
        if camera["name"] != alias or (
            camera["fourcc"], camera["native_bit_depth"], camera["png_bit_depth"],
            camera["width"], camera["height"], camera["stride"], camera["raw_bytes"],
            camera["requested_exposure_us"], camera["analogue_gain"],
        ) != (
            "Y12P", 12, 16, 1600, 1400, 2400, 3360000,
            session["acquisition"]["exposure_us"], session["acquisition"]["gain"],
        ):
            raise ValueError("Capture settings or native precision differ from the calibration session")
        png = archive.read(camera["png_file"])
        raw = archive.read(camera["raw_file"])
        if (hashlib.sha256(png).hexdigest() != camera["png_sha256"]
                or hashlib.sha256(raw).hexdigest() != camera["raw_sha256"]):
            raise ValueError("Capture member SHA-256 mismatch")
        image = cv2.imdecode(np.frombuffer(png, np.uint8), cv2.IMREAD_UNCHANGED)
        if image is None or image.shape != (1400, 1600) or image.dtype != np.uint16 or png[24] != 16:
            raise ValueError("Capture is not the expected native 16-bit grayscale PNG")
        if len(raw) != 3360000:
            raise ValueError("Incorrect RAW12 buffer size")
        groups = np.frombuffer(raw, np.uint8).reshape(1400, 800, 3)
        decoded = np.empty((1400, 1600), np.uint16)
        decoded[:, 0::2] = (groups[:, :, 0].astype(np.uint16) << 4) | (groups[:, :, 2] & 15)
        decoded[:, 1::2] = (groups[:, :, 1].astype(np.uint16) << 4) | (groups[:, :, 2] >> 4)
        if not np.array_equal(image, decoded):
            raise ValueError("PNG samples differ from the native RAW12 buffer")
    return {
        "frame_id": camera["frame_id"], "capture_id": metadata["capture_id"],
        "selected_utc": metadata["selected_utc"],
    }


def save_session(path: Path, record: dict[str, Any], expected_hash: str) -> str:
    validate(record)
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected_hash:
        raise RuntimeError("Session changed during capture; saved archive is preserved for manual reconciliation")
    data = (json.dumps(record, indent=2, allow_nan=False) + "\n").encode("utf-8")
    temporary = path.with_name(path.name + "." + uuid4().hex + ".tmp")
    try:
        with temporary.open("xb") as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()
    return hashlib.sha256(data).hexdigest()


def capture_pose(path: Path, pose_id: str, count: int = 3) -> list[Path]:
    if not 1 <= count <= 3:
        raise ValueError("Repeat count must be 1..3")
    path = path.resolve(strict=True)
    expected_hash = hashlib.sha256(path.read_bytes()).hexdigest()
    session = load_json(path)
    validate(session)
    if session["status"] not in ("ready", "capturing"):
        raise ValueError("Complete the operator checklist and set session status to ready before capture")
    with (ROOT / "templates" / "capture-poses.csv").open(newline="", encoding="utf-8") as source:
        poses = {row["pose_id"]: row for row in csv.DictReader(source)}
    if pose_id in poses:
        split = poses[pose_id]["split"]
    elif re.fullmatch(r"V[0-9]{2,}", pose_id) and int(pose_id[1:]) > 10:
        split = "validation"
        print(f"Fresh holdout {pose_id}: use a new physical pose, not a reused validation image.")
    else:
        raise ValueError("Choose a checklist pose ID, or a fresh validation ID V11 or later")
    previous = [c for c in session["captures"] if c["pose_id"] == pose_id]
    repeats = {c["repeat"] for c in previous}
    needed = [i for i in range(1, count + 1) if i not in repeats]
    if not needed:
        raise FileExistsError("Requested pose repeats are already recorded; originals will not be overwritten")
    for capture in previous:
        old = (path.parent / capture["archive"]).resolve()
        if not old.is_relative_to(path.parent) or hashlib.sha256(old.read_bytes()).hexdigest() != capture["archive_sha256"]:
            raise ValueError("Previously recorded capture is missing, changed or outside the session directory")
    url = local_url(session["acquisition"]["preview_url"])
    output_dir = path.parent / "images"
    if output_dir.is_symlink() or not output_dir.resolve().is_relative_to(path.parent):
        raise ValueError("The images directory must not redirect outside the calibration run")
    output_dir.mkdir(exist_ok=True)
    saved = []
    seen = {c["frame_id"] for c in session["captures"]}
    for repeat in needed:
        status = request_json(url + "/api/status")
        epoch = check_status(status, session)
        payload = json.dumps({
            "session_id": epoch, "camera": session["acquisition"]["capture_alias"],
        }).encode()
        request = Request(url + "/api/capture", data=payload, method="POST",
                          headers={"Content-Type": "application/json", "Origin": url})
        with urlopen(request, timeout=60) as response:
            if (response.headers.get_content_type() != "application/zip"
                    or response.headers.get("X-Preview-Session") != epoch):
                raise ValueError("Unexpected capture response type or preview session")
            data = response.read(MAX_ARCHIVE_BYTES + 1)
        if len(data) > MAX_ARCHIVE_BYTES:
            raise ValueError("Oversized capture response")
        capture = check_archive(data, session, epoch)
        if capture["frame_id"] in seen:
            raise ValueError("Duplicate native frame; this is not an independent repeat")
        output = output_dir / f"{pose_id}-R{repeat}.zip"
        created = False
        try:
            with output.open("xb") as destination:
                created = True
                destination.write(data)
                destination.flush()
                os.fsync(destination.fileno())
        except OSError:
            if created and output.exists() and output.stat().st_size != len(data):
                output.unlink()
            raise
        session["status"] = "capturing"
        session["preview_session_id"] = epoch
        session["captures"].append({
            "pose_id": pose_id, "repeat": repeat, "split": split,
            "archive": output.relative_to(path.parent).as_posix(),
            "archive_sha256": hashlib.sha256(data).hexdigest(), **capture,
        })
        expected_hash = save_session(path, session, expected_hash)
        seen.add(capture["frame_id"])
        saved.append(output)
        print(f"Captured {pose_id}/R{repeat}: {output}")
        time.sleep(.1)
    return saved


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, required=True)
    parser.add_argument("--pose", required=True)
    parser.add_argument("--count", type=int, default=3)
    args = parser.parse_args()
    capture_pose(args.session, args.pose, args.count)


if __name__ == "__main__":
    main()
