# SPDX-License-Identifier: GPL-3.0-or-later
from __future__ import annotations

from dataclasses import replace
import hashlib
import http.client
import io
import json
from pathlib import Path
import struct
import threading
import unittest
from unittest.mock import patch
from zipfile import ZipFile

from .server import (
    CameraConfig, ExposureConflict, ExposureController, FrameStore, Image,
    PreviewError, PreviewHTTPServer, PreviewState, encode_png,
)


def raw10_frame(config: CameraConfig) -> tuple[bytes, bytes]:
    frame = bytearray([199] * config.sizeimage)
    expected = bytearray(config.width * config.height * 2)
    for y in range(config.height):
        for x in range(0, config.width, 4):
            values = [(y * config.width + x + p) % 1024 for p in range(4)]
            offset = y * config.stride + x // 4 * 5
            frame[offset:offset + 4] = bytes(v >> 2 for v in values)
            frame[offset + 4] = sum((v & 3) << (p * 2) for p, v in enumerate(values))
            struct.pack_into("<4H", expected, (y * config.width + x) * 2, *values)
    return bytes(frame), bytes(expected)


def pair() -> PreviewState:
    mira = CameraConfig("mira220", "/dev/video0", 3, 2, "GREY", 4, 8, 89, 1000, 1000,
                        subdevice="/dev/v4l-subdev2")
    imx = CameraConfig("imx296", "/dev/video8", 4, 2, "Y10P", 8, 16, 60, 1000, 1000,
                       subdevice="/dev/v4l-subdev5")
    stores = {config.name: FrameStore(config) for config in (mira, imx)}
    stores["mira220"].publish(bytes([0, 127, 255, 199, 1, 2, 3, 199]))
    stores["imx296"].publish(raw10_frame(imx)[0])
    return PreviewState(stores, transport="jpeg")


def raw12_frame(config: CameraConfig) -> tuple[bytes, bytes]:
    frame = bytearray([199] * config.sizeimage)
    expected = bytearray(config.width * config.height * 2)
    for y in range(config.height):
        for x in range(0, config.width, 2):
            values = [(y * config.width + x + p) % 4096 for p in range(2)]
            offset = y * config.stride + x // 2 * 3
            frame[offset:offset + 3] = bytes([values[0] >> 4, values[1] >> 4,
                                            (values[0] & 15) | ((values[1] & 15) << 4)])
            struct.pack_into("<2H", expected, (y * config.width + x) * 2, *values)
    return bytes(frame), bytes(expected)


@unittest.skipIf(Image is None, "Lossless PNG capture requires Pillow")
class PNGTests(unittest.TestCase):
    def check_png(self, config: CameraConfig, raw: bytes, expected: bytes, depth: int) -> None:
        png = encode_png(config, raw)
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(png[24:26], bytes([depth, 0]))  # IHDR: sample depth, grayscale.
        assert Image is not None
        with Image.open(io.BytesIO(png)) as decoded:
            self.assertEqual(decoded.size, (config.width, config.height))
            self.assertEqual(decoded.tobytes(), expected)

    def test_every_raw10_value_and_low_bit_survives_padded_rows(self) -> None:
        config = CameraConfig("imx296", "/dev/video8", 1024, 2, "Y10P", 1284, 2568, 60, 1000, 1000)
        raw, expected = raw10_frame(config)
        self.check_png(config, raw, expected, 16)

    def test_every_raw12_value_and_low_nibble_survives_padded_rows(self) -> None:
        config = CameraConfig("mira220", "/dev/video0", 4096, 2, "Y12P", 6160, 12320, 89, 1000, 1000)
        raw, expected = raw12_frame(config)
        self.check_png(config, raw, expected, 16)

    def test_raw12_full_resolution_is_lossless(self) -> None:
        config = CameraConfig("mira220", "/dev/video0", 1600, 1400, "Y12P", 2400, 3360000, 89, 1000, 1000)
        raw, expected = raw12_frame(config)
        self.check_png(config, raw, expected, 16)

    def test_full_native_dimensions_and_all_pixel_values_are_lossless(self) -> None:
        mira = CameraConfig("mira220", "/dev/video0", 1600, 1400, "GREY", 1600, 2240000, 89, 1000, 1000)
        raw = (bytes(range(256)) * (mira.sizeimage // 256 + 1))[:mira.sizeimage]
        self.check_png(mira, raw, raw, 8)
        imx = CameraConfig("imx296", "/dev/video8", 1456, 1088, "Y10P", 1824, 1984512, 60, 1000, 1000)
        raw, expected = raw10_frame(imx)
        self.check_png(imx, raw, expected, 16)

    def test_grey_padding_is_not_saved_as_pixels(self) -> None:
        state = pair()
        config = state.stores["mira220"].config
        self.check_png(config, bytes([0, 127, 255, 199, 1, 2, 3, 199]), bytes([0, 127, 255, 1, 2, 3]), 8)

    def test_missing_pillow_bad_length_and_unsupported_format_are_errors(self) -> None:
        config = pair().stores["mira220"].config
        with self.assertRaisesRegex(PreviewError, "length"):
            encode_png(config, b"short")
        with self.assertRaisesRegex(PreviewError, "Unsupported"):
            encode_png(replace(config, fourcc="Y16"), b"abcdefgh")
        with patch("preview.server.Image", None):
            with self.assertRaisesRegex(PreviewError, "Pillow"):
                encode_png(config, b"abcdefgh")


@unittest.skipIf(Image is None, "Lossless PNG capture requires Pillow")
class ArchiveTests(unittest.TestCase):
    def setUp(self) -> None:
        self.state = pair()

    def test_archive_is_byte_exact_with_metadata_and_unscaled_pixels(self) -> None:
        state = self.state
        with patch("preview.server.time.monotonic", return_value=100):
            for store in state.stores.values():
                snapshot = store.snapshot()
                assert snapshot is not None
                store.publish(snapshot[1])
        with patch("preview.server.time.monotonic", return_value=100.02):
            filename, data = state.capture_archive(state.session_id)
        self.assertRegex(filename, r"^pitrac-capture-\d{8}T\d{12}Z-[a-f0-9]{32}\.zip$")
        with ZipFile(io.BytesIO(data)) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(set(archive.namelist()), {
                "mira220.png", "imx296.png", "mira220.raw", "imx296.raw", "metadata.json", "README.txt",
            })
            metadata = json.loads(archive.read("metadata.json"))
            self.assertFalse(metadata["hardware_synchronized"])
            self.assertEqual(metadata["session_id"], state.session_id)
            self.assertEqual(metadata["receive_time_difference_ms"], 0)
            for camera in metadata["cameras"]:
                name = camera["name"]
                raw = archive.read(camera["raw_file"])
                png = archive.read(camera["png_file"])
                self.assertEqual(len(raw), state.stores[name].config.sizeimage)
                self.assertEqual(hashlib.sha256(raw).hexdigest(), camera["raw_sha256"])
                self.assertEqual(hashlib.sha256(png).hexdigest(), camera["png_sha256"])
                self.assertAlmostEqual(camera["age_at_selection_ms"], 20)
                self.assertEqual(camera["frame_id"], 2)
                with patch("preview.server.time.monotonic", return_value=100.02):
                    self.assertEqual(raw, state.stores[name].snapshot()[1])
                assert Image is not None
                with Image.open(io.BytesIO(png)) as image:
                    expected = bytes([0, 127, 255, 1, 2, 3]) if name == "mira220" else raw10_frame(state.stores[name].config)[1]
                    self.assertEqual(image.tobytes(), expected)
            self.assertEqual(metadata["cameras"][1]["native_bit_depth"], 10)
            self.assertEqual(metadata["cameras"][1]["png_bit_depth"], 16)
            self.assertIn(b"may look dark", archive.read("README.txt"))

    def test_selected_mira12_archive_reports_native12_png16_and_preserves_every_sample(self) -> None:
        config = CameraConfig("mira220", "/dev/video0", 8, 2, "Y12P", 16, 32, 89, 1000, 1000)
        raw, expected = raw12_frame(config)
        store = FrameStore(config)
        store.publish(raw)
        state = PreviewState({"mira220": store})
        self.assertEqual(state.status()["cameras"][0]["native_bit_depth"], 12)
        _, data = state.capture_archive(state.session_id, "mira220")
        with ZipFile(io.BytesIO(data)) as archive:
            metadata = json.loads(archive.read("metadata.json"))["cameras"][0]
            self.assertEqual((metadata["native_bit_depth"], metadata["png_bit_depth"]), (12, 16))
            self.assertEqual(archive.read("mira220.raw"), raw)
            assert Image is not None
            with Image.open(io.BytesIO(archive.read("mira220.png"))) as image:
                self.assertEqual(image.tobytes(), expected)
            self.assertIn(b"0..4095", archive.read("README.txt"))

    def test_failed_missing_stopped_starting_and_stale_camera_reject_whole_pair(self) -> None:
        for condition in ("failed", "missing", "stopped", "starting", "stale"):
            with self.subTest(condition=condition):
                state = pair()
                store = state.stores["imx296"]
                if condition == "failed":
                    store.fail("sensor failed")
                elif condition == "missing":
                    del state.stores["imx296"]
                elif condition == "stopped":
                    store.stop()
                elif condition == "starting":
                    store.reconfigure(store.config)
                else:
                    with patch("preview.server.time.monotonic", return_value=0):
                        store.publish(raw10_frame(store.config)[0])
                with patch("preview.server.encode_png") as encoder:
                    with self.assertRaises(PreviewError):
                        state.capture_archive(state.session_id)
                    encoder.assert_not_called()
                self.assertFalse(state.status()["snapshot_capture"]["available"])

    def test_session_mismatch_and_missing_pillow_are_explicit(self) -> None:
        with self.assertRaises(ExposureConflict):
            self.state.capture_archive("old-session")
        with patch("preview.server.Image", None):
            self.assertFalse(self.state.status()["snapshot_capture"]["available"])
            with self.assertRaisesRegex(PreviewError, "Pillow"):
                self.state.capture_archive(self.state.session_id)

    def test_single_camera_capture_does_not_require_the_other_camera(self) -> None:
        for name, other in (("mira220", "imx296"), ("imx296", "mira220")):
            with self.subTest(camera=name):
                state = pair()
                del state.stores[other]
                status = state.status()["snapshot_capture"]
                self.assertFalse(status["available"])
                self.assertTrue(status["by_camera"][name]["available"])
                self.assertFalse(status["by_camera"][other]["available"])
                _, data = state.capture_archive(state.session_id, name)
                with ZipFile(io.BytesIO(data)) as archive:
                    self.assertEqual(set(archive.namelist()), {
                        f"{name}.png", f"{name}.raw", "metadata.json", "README.txt",
                    })
                    self.assertIsNone(archive.testzip())
                    metadata = json.loads(archive.read("metadata.json"))
                    self.assertIsNone(metadata["receive_time_difference_ms"])
                    self.assertEqual([camera["name"] for camera in metadata["cameras"]], [name])
                    self.assertEqual(archive.read(f"{name}.raw"), state.stores[name].snapshot()[1])
                with self.assertRaisesRegex(PreviewError, "both"):
                    state.capture_archive(state.session_id)
                with self.assertRaisesRegex(PreviewError, "not configured"):
                    state.capture_archive(state.session_id, other)

    def test_single_camera_ignores_failed_other_camera_but_rejects_failed_selection(self) -> None:
        state = pair()
        state.stores["imx296"].fail("sensor failed")
        state.capture_archive(state.session_id, "mira220")
        with self.assertRaises(PreviewError):
            state.capture_archive(state.session_id, "imx296")
        with self.assertRaisesRegex(PreviewError, "Unknown"):
            state.capture_archive(state.session_id, "unknown")

    def test_encoding_cannot_mix_frames_or_settings_after_exposure_changes(self) -> None:
        state = self.state
        original_session = state.session_id
        original = {name: store.snapshot()[1] for name, store in state.stores.items()}

        def change_exposure(config: CameraConfig, raw: bytes) -> bytes:
            state.session_id = "changed"
            for store in state.stores.values():
                store.reconfigure(replace(store.config, requested_exposure_us=20000))
                store.publish(b"\x00" * store.config.sizeimage)
            return encode_png(config, raw)

        with patch("preview.server.encode_png", side_effect=change_exposure):
            _, data = state.capture_archive(original_session)
        with ZipFile(io.BytesIO(data)) as archive:
            metadata = json.loads(archive.read("metadata.json"))
            self.assertEqual(metadata["session_id"], original_session)
            for camera in metadata["cameras"]:
                self.assertEqual(camera["requested_exposure_us"], 1000)
                self.assertEqual(archive.read(camera["raw_file"]), original[camera["name"]])

    def test_snapshot_selection_uses_exposure_lock(self) -> None:
        state = self.state
        state.exposure_control = ExposureController(state, [])
        started = threading.Event()
        finished = threading.Event()
        failures: list[Exception] = []

        def capture() -> None:
            started.set()
            try:
                state.capture_archive(state.session_id)
            except Exception as error:
                failures.append(error)
            finally:
                finished.set()

        with state.exposure_control.lock:
            thread = threading.Thread(target=capture)
            thread.start()
            self.assertTrue(started.wait(1))
            self.assertFalse(finished.wait(0.05))
        thread.join(timeout=3)
        self.assertTrue(finished.is_set())
        self.assertEqual(failures, [])


@unittest.skipIf(Image is None, "Lossless PNG capture requires Pillow")
class CaptureHTTPTests(unittest.TestCase):
    def setUp(self) -> None:
        self.state = pair()
        root = Path(__file__).resolve().parent.parent / "web"
        self.server = PreviewHTTPServer(0, self.state, root)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=3)

    def post(self, body: object, headers: dict[str, str] | None = None) -> tuple[int, dict[str, str], bytes]:
        host = f"127.0.0.1:{self.server.server_port}"
        request_headers = {"Origin": f"http://{host}", "Content-Type": "application/json"}
        if headers:
            request_headers.update(headers)
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=3)
        try:
            connection.request("POST", "/api/capture", json.dumps(body), request_headers)
            response = connection.getresponse()
            return response.status, dict(response.getheaders()), response.read()
        finally:
            connection.close()

    def test_download_has_safe_filename_correct_headers_and_both_cameras(self) -> None:
        code, headers, data = self.post({"session_id": self.state.session_id})
        self.assertEqual(code, 200)
        self.assertEqual(headers["Content-Type"], "application/zip")
        self.assertEqual(headers["Cache-Control"], "no-store")
        self.assertEqual(headers["X-Preview-Session"], self.state.session_id)
        self.assertRegex(headers["Content-Disposition"], r'^attachment; filename="pitrac-capture-.*\.zip"$')
        with ZipFile(io.BytesIO(data)) as archive:
            self.assertIn("mira220.raw", archive.namelist())
            self.assertIn("imx296.png", archive.namelist())
        self.assertTrue(self.state.status()["snapshot_capture"]["available"])

    def test_validation_origin_and_busy_errors_never_return_an_archive(self) -> None:
        for invalid in (None, [], {}, {"session_id": 1}, {"session_id": "a", "other": True},
                        {"session_id": "a", "camera": None}, {"session_id": "a", "camera": "unknown"}):
            self.assertEqual(self.post(invalid)[0], 400)
        payload = {"session_id": self.state.session_id}
        self.assertEqual(self.post(payload, {"Origin": ""})[0], 403)
        self.assertEqual(self.post(payload, {"Origin": "https://untrusted.example"})[0], 403)
        self.assertEqual(self.post(payload, {"Host": "untrusted.example"})[0], 403)
        self.assertEqual(self.post(payload, {"Content-Type": "text/plain"})[0], 415)
        self.assertEqual(self.post({"session_id": "old"})[0], 409)
        with self.state.capture_lock:
            self.assertEqual(self.post(payload)[0], 409)
        self.assertEqual(self.post(payload)[0], 200)

    def test_selected_camera_download_and_new_browser_assets(self) -> None:
        del self.state.stores["imx296"]
        code, _, data = self.post({"session_id": self.state.session_id, "camera": "mira220"})
        self.assertEqual(code, 200)
        with ZipFile(io.BytesIO(data)) as archive:
            self.assertIn("mira220.png", archive.namelist())
            self.assertNotIn("imx296.png", archive.namelist())
        self.assertEqual(self.post({"session_id": self.state.session_id})[0], 503)
        for asset in ("pupil.js", "archive.js"):
            connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=3)
            try:
                connection.request("GET", f"/{asset}")
                response = connection.getresponse()
                self.assertEqual(response.status, 200)
                self.assertIn(b"export", response.read())
            finally:
                connection.close()

    def test_camera_failure_and_encoder_failure_release_capture_slot(self) -> None:
        payload = {"session_id": self.state.session_id}
        with patch("preview.server.encode_png", side_effect=OSError("encoder failure")):
            code, headers, body = self.post(payload)
        self.assertEqual(code, 500)
        self.assertEqual(headers["Content-Type"], "application/json")
        self.assertIn("encoding failed", json.loads(body)["error"])
        self.assertEqual(self.post(payload)[0], 200)
        self.state.stores["imx296"].stop()
        self.assertEqual(self.post(payload)[0], 503)


if __name__ == "__main__":
    unittest.main()
