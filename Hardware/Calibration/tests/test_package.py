from __future__ import annotations

from copy import deepcopy
import csv
import hashlib
from http.client import HTTPMessage
import io
import json
from pathlib import Path
import re
import sys
import tempfile
import unittest
from unittest.mock import patch
from xml.etree import ElementTree
from zipfile import ZipFile, ZIP_DEFLATED

import cv2
import numpy as np
from jsonschema import ValidationError

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import capture_pose
import generate_targets
import validate_record

cv2.setNumThreads(1)


def ready_session() -> dict:
    record = validate_record.load_json(ROOT / "templates" / "camera-session-CAM0.json")
    record["status"] = "ready"
    record["session_id"] = "CAM0_TEST_ONLY"
    record["created_utc"] = "2026-10-02T12:00:00Z"
    record["camera"].update(
        module_id="TEST module", lens_id="TEST lens", holder_id="TEST holder",
        filter_stack_id="TEST filter", identity_confirmed=True, focus_locked=True,
    )
    record["acquisition"].update(exposure_us=12000, illumination_id="TEST diffuse850")
    record["target"].update(
        measured_square_x_mm=20, measured_square_y_mm=20,
        print_scale_x_100_mm=100, print_scale_y_100_mm=100,
        flatness_verified=True, ir_contrast_verified=True,
    )
    validate_record.validate(record)
    return record


def snapshot(frame_id: int) -> bytes:
    samples = (np.arange(1600 * 1400, dtype=np.uint32).reshape(1400, 1600) % 4096).astype(np.uint16)
    groups = np.empty((1400, 800, 3), np.uint8)
    groups[:, :, 0] = samples[:, 0::2] >> 4
    groups[:, :, 1] = samples[:, 1::2] >> 4
    groups[:, :, 2] = (samples[:, 0::2] & 15) | ((samples[:, 1::2] & 15) << 4)
    raw = groups.tobytes()
    ok, encoded = cv2.imencode(".png", samples)
    assert ok
    png = encoded.tobytes()
    camera = {
        "name": "mira220", "fourcc": "Y12P", "native_bit_depth": 12, "png_bit_depth": 16,
        "width": 1600, "height": 1400, "stride": 2400, "raw_bytes": 3360000,
        "requested_exposure_us": 12000, "analogue_gain": 1,
        "png_file": "mira220.png", "raw_file": "mira220.raw",
        "png_sha256": hashlib.sha256(png).hexdigest(), "raw_sha256": hashlib.sha256(raw).hexdigest(),
        "frame_id": frame_id,
    }
    metadata = {
        "session_id": "epoch", "capture_id": f"TEST-frame-{frame_id}",
        "selected_utc": "2026-10-02T12:00:00Z", "cameras": [camera],
    }
    output = io.BytesIO()
    with ZipFile(output, "w", compression=ZIP_DEFLATED) as archive:
        archive.writestr("mira220.png", png)
        archive.writestr("mira220.raw", raw)
        archive.writestr("metadata.json", json.dumps(metadata))
    return output.getvalue()


class Response(io.BytesIO):
    def __init__(self, data: bytes) -> None:
        super().__init__(data)
        self.headers = HTTPMessage()
        self.headers["Content-Type"] = "application/zip"
        self.headers["X-Preview-Session"] = "epoch"


class TargetTests(unittest.TestCase):
    def test_definition_is_bound_to_actual_opencv_layout(self) -> None:
        definition = validate_record.load_json(ROOT / "targets" / "board.json")
        board = generate_targets.make_board()
        self.assertEqual(definition["board_id"], generate_targets.BOARD_ID)
        self.assertEqual(len(definition["markers"]), 44)
        self.assertEqual(len(definition["chessboard_corners"]), 70)
        self.assertEqual([m["id"] for m in definition["markers"]], board.getIds().tolist())
        self.assertEqual(
            definition["dictionary_bytes_sha256"], hashlib.sha256(board.getDictionary().bytesList.tobytes()).hexdigest(),
        )
        self.assertTrue(np.array_equal(
            generate_targets.raster(definition),
            board.generateImage((3080, 2240), marginSize=0, borderBits=1),
        ))

    def test_svg_physical_dimensions_scale_bars_and_rendered_pattern(self) -> None:
        definition = validate_record.load_json(ROOT / "targets" / "board.json")
        namespace = {"s": "http://www.w3.org/2000/svg"}
        for paper, (width, height) in generate_targets.PAPERS.items():
            svg = ElementTree.parse(ROOT / "targets" / f"charuco-{paper}.svg").getroot()
            self.assertEqual(svg.attrib["width"], f"{width}mm")
            self.assertEqual(svg.attrib["height"], f"{height}mm")
            g = generate_targets.page_geometry(paper)
            self.assertGreaterEqual(min(g["ox"], g["oy"]), 15)
            pattern = svg.find("s:g[@data-role='pattern']", namespace)
            self.assertIsNotNone(pattern)
            rendered = np.full((2240, 3080), 255, np.uint8)
            assert pattern is not None
            for rectangle in pattern:
                x, y, w, h = [float(rectangle.attrib[key]) for key in ["x", "y", "width", "height"]]
                left, top, right, bottom = np.rint(np.array([x, y, x+w, y+h]) * 14).astype(int)
                rendered[top:bottom, left:right] = 0
            self.assertTrue(np.array_equal(rendered, generate_targets.raster(definition)))
            scales = svg.findall("s:g[@data-role='scale-checks']/s:line[@data-scale]", namespace)
            self.assertEqual(len(scales), 2)
            for line in scales:
                length = np.hypot(float(line.attrib["x2"])-float(line.attrib["x1"]),
                                  float(line.attrib["y2"])-float(line.attrib["y1"]))
                self.assertAlmostEqual(length, 100, places=8)

    def test_pdf_pages_are_vector_and_have_correct_media_boxes(self) -> None:
        for paper, (width, height) in generate_targets.PAPERS.items():
            data = (ROOT / "targets" / f"charuco-{paper}.pdf").read_bytes()
            self.assertNotIn(b"/Subtype /Image", data)
            match = re.search(rb"/MediaBox\s*\[\s*0\s+0\s+([\d.]+)\s+([\d.]+)\s*\]", data)
            self.assertIsNotNone(match)
            assert match is not None
            self.assertAlmostEqual(float(match[1]) / generate_targets.mm, width, places=3)
            self.assertAlmostEqual(float(match[2]) / generate_targets.mm, height, places=3)

    def test_detector_recovers_all_corners_and_partial_views(self) -> None:
        image = cv2.imread(str(ROOT / "targets" / "board-raster.png"), cv2.IMREAD_GRAYSCALE)
        full = np.pad(image, 210, constant_values=255)
        detector = cv2.aruco.CharucoDetector(generate_targets.make_board())
        corners, identifiers, _, marker_ids = detector.detectBoard(full)
        self.assertEqual(len(identifiers), 70)
        self.assertEqual(len(marker_ids), 44)
        expected = generate_targets.make_board().getChessboardCorners()[:, :2] * 14 + 210
        actual = corners.reshape(-1, 2)
        self.assertLess(np.max(np.linalg.norm(actual-expected[identifiers.ravel()], axis=1)), .05)
        partial = full[:, :2000]
        _, partial_ids, _, _ = detector.detectBoard(partial)
        self.assertGreater(len(partial_ids), 20)
        self.assertLess(len(partial_ids), 70)


class RecordTests(unittest.TestCase):
    def test_templates_are_blank_valid_and_cannot_claim_readiness(self) -> None:
        for name in ["camera-session-CAM0.json", "camera-session-CAM1.json", "calibration-result.json"]:
            record = validate_record.load_json(ROOT / "templates" / name)
            validate_record.validate(record)
            record["status"] = "ready" if "session" in name else "validated"
            with self.assertRaises(ValidationError):
                validate_record.validate(record)

    def test_ready_gates_native_dimensions_identity_and_optical_state(self) -> None:
        original = ready_session()
        for section, field, bad in [
            ("camera", "focus_locked", False), ("camera", "identity_confirmed", False),
            ("camera", "module_id", ""), ("camera", "filter_stack_id", None),
            ("camera", "image_size", [800, 700]),
            ("acquisition", "native_bit_depth", 8), ("acquisition", "exposure_us", None),
            ("acquisition", "illumination_nm", 550), ("target", "flatness_verified", False),
            ("target", "ir_contrast_verified", False),
        ]:
            record = deepcopy(original)
            record[section][field] = bad
            with self.assertRaises(ValidationError):
                validate_record.validate(record)

    def test_camera_role_and_identifier_prefixes_cannot_be_mixed(self) -> None:
        session = ready_session()
        session["session_id"] = "CAM1_TEST_ONLY"
        with self.assertRaises(ValidationError):
            validate_record.validate(session)
        result = validate_record.load_json(ROOT / "templates" / "calibration-result.json")
        for field in ("session_id", "calibration_id"):
            bad = deepcopy(result)
            bad[field] = "CAM1_TEST_ONLY"
            with self.assertRaises(ValidationError):
                validate_record.validate(bad)

    def test_result_model_order_shape_and_nonfinite_values_fail(self) -> None:
        template = validate_record.load_json(ROOT / "templates" / "calibration-result.json")
        for bad in [[None]*4, [None]*6]:
            record = deepcopy(template)
            record["model"]["distortion_coefficients"] = bad
            with self.assertRaises(ValidationError):
                validate_record.validate(record)
        record = deepcopy(template)
        record["model"]["coefficient_order"] = ["k1", "k2", "k3", "p1", "p2"]
        with self.assertRaises(ValidationError):
            validate_record.validate(record)
        for bad in [float("nan"), float("inf")]:
            record = deepcopy(template)
            record["model"]["distortion_coefficients"][0] = bad
            with self.assertRaises(ValueError):
                validate_record.validate(record)

    def test_pose_groups_are_disjoint_and_cover_every_zone(self) -> None:
        with (ROOT / "templates" / "capture-poses.csv").open(newline="") as source:
            rows = list(csv.DictReader(source))
        self.assertEqual(len(rows), 40)
        self.assertEqual(sum(row["split"] == "fit" for row in rows), 30)
        self.assertEqual(sum(row["split"] == "validation" for row in rows), 10)
        self.assertEqual(len({row["pose_id"] for row in rows}), 40)
        self.assertTrue(all(row["repeats"] == "3" for row in rows))
        for split in ["fit", "validation"]:
            self.assertEqual(len({row["zone"] for row in rows if row["split"] == split}), 9)


class CaptureTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / "session.json"
        self.session = ready_session()
        self.path.write_text(json.dumps(self.session))
        self.status = {
            "session_id": "epoch",
            "cameras": [{"name": "mira220", "state": "live", "fourcc": "Y12P",
                         "native_bit_depth": 12, "width": 1600, "height": 1400,
                         "requested_exposure_us": 12000, "analogue_gain": 1}],
        }
        self.snapshots = [snapshot(i) for i in range(1, 4)]

    def tearDown(self) -> None:
        self.directory.cleanup()

    def run_capture(self, responses: list[bytes], pose: str = "T01") -> list[Path]:
        with patch.object(capture_pose, "request_json", return_value=self.status), \
                patch.object(capture_pose, "urlopen", side_effect=[Response(data) for data in responses]), \
                patch.object(capture_pose.time, "sleep"):
            return capture_pose.capture_pose(self.path, pose)

    def test_three_native_frames_are_preserved_with_pose_split_and_hashes(self) -> None:
        saved = self.run_capture(self.snapshots)
        record = validate_record.load_json(self.path)
        validate_record.validate(record)
        self.assertEqual([c["frame_id"] for c in record["captures"]], [1, 2, 3])
        self.assertEqual(record["preview_session_id"], "epoch")
        self.assertTrue(all(c["split"] == "fit" for c in record["captures"]))
        for path, source in zip(saved, self.snapshots):
            self.assertEqual(path.read_bytes(), source)
        with self.assertRaises(FileExistsError):
            capture_pose.capture_pose(self.path, "T01")

    def test_existing_unrecorded_archive_is_never_overwritten_or_removed(self) -> None:
        output = self.path.parent / "images" / "T01-R1.zip"
        output.parent.mkdir()
        output.write_bytes(b"KEEP ORIGINAL")
        with self.assertRaises(FileExistsError):
            self.run_capture(self.snapshots)
        self.assertEqual(output.read_bytes(), b"KEEP ORIGINAL")
        self.assertEqual(validate_record.load_json(self.path)["captures"], [])

    def test_bad_second_frame_preserves_first_and_partial_pose_can_resume(self) -> None:
        with self.assertRaises(ValueError):
            self.run_capture([self.snapshots[0], self.snapshots[0]])
        original = (self.path.parent / "images" / "T01-R1.zip").read_bytes()
        self.assertEqual(len(validate_record.load_json(self.path)["captures"]), 1)
        saved = self.run_capture(self.snapshots[1:])
        self.assertEqual(len(saved), 2)
        self.assertEqual((self.path.parent / "images" / "T01-R1.zip").read_bytes(), original)
        self.assertEqual(len(validate_record.load_json(self.path)["captures"]), 3)

    def test_changed_settings_epoch_or_nonlocal_url_are_rejected(self) -> None:
        self.status["cameras"][0]["requested_exposure_us"] = 10000
        with self.assertRaises(ValueError):
            self.run_capture(self.snapshots)
        self.status["cameras"][0]["requested_exposure_us"] = 12000
        record = ready_session()
        record["preview_session_id"] = "different-epoch"
        self.path.write_text(json.dumps(record))
        with self.assertRaises(ValueError):
            self.run_capture(self.snapshots)
        for url in ["https://example.com", "http://192.168.0.17:8765", "http://127.0.0.1:8765/path"]:
            with self.assertRaises(ValueError):
                capture_pose.local_url(url)

    def test_new_holdout_id_is_marked_validation(self) -> None:
        self.run_capture(self.snapshots, "V11")
        record = validate_record.load_json(self.path)
        self.assertTrue(all(c["split"] == "validation" for c in record["captures"]))


if __name__ == "__main__":
    unittest.main()
