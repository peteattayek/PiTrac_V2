# SPDX-License-Identifier: GPL-3.0-or-later
from __future__ import annotations

import gzip
from dataclasses import replace
import http.client
import io
import json
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch

from .server import (
    CameraConfig,
    FrameStore,
    PreviewError,
    PreviewHTTPServer,
    PreviewState,
    accepts_gzip,
    read_config,
    read_frame,
    display_pixels,
    encode_jpeg,
    Image,
)


def small_config() -> CameraConfig:
    return CameraConfig("mira220", "/dev/video0", 3, 2, "GREY", 4, 8, 89.08, 1000, 999.761667)


class FragmentedInput(io.BytesIO):
    def read(self, size: int = -1) -> bytes:
        return super().read(min(size, 3) if size >= 0 else 3)


class FrameTests(unittest.TestCase):
    def test_fragmented_reads_preserve_exact_frame_boundaries(self) -> None:
        source = FragmentedInput(b"abcdefghABCDEFGH")
        self.assertEqual(read_frame(source, 8), b"abcdefgh")
        self.assertEqual(read_frame(source, 8), b"ABCDEFGH")
        self.assertIsNone(read_frame(source, 8))

    def test_partial_frame_is_an_error(self) -> None:
        with self.assertRaisesRegex(PreviewError, "partial frame"):
            read_frame(FragmentedInput(b"short"), 8)

    def test_latest_frame_replaces_older_frame(self) -> None:
        store = FrameStore(small_config())
        store.publish(b"abcdefgh")
        store.publish(b"ABCDEFGH")
        self.assertEqual(store.snapshot(), (2, b"ABCDEFGH"))
        self.assertIsNone(store.snapshot(after=2))
        self.assertEqual(store.status()["frames_received"], 2)

    def test_wrong_size_rejected(self) -> None:
        with self.assertRaisesRegex(PreviewError, "frame length"):
            FrameStore(small_config()).publish(b"short")

    def test_starting_and_stale_are_not_live(self) -> None:
        store = FrameStore(small_config())
        self.assertEqual(store.status()["state"], "starting")
        with self.assertRaises(PreviewError):
            store.snapshot()
        with patch("preview.server.time.monotonic", return_value=10):
            store.publish(b"abcdefgh")
            self.assertEqual(store.status()["state"], "live")
        with patch("preview.server.time.monotonic", return_value=13):
            self.assertEqual(store.status()["state"], "error")
            with self.assertRaisesRegex(PreviewError, "not live"):
                store.snapshot()

    def test_error_or_stop_prevents_serving_last_frame(self) -> None:
        store = FrameStore(small_config())
        store.publish(b"abcdefgh")
        store.fail("camera failed")
        with self.assertRaisesRegex(PreviewError, "camera failed"):
            store.snapshot()
        store.stop()
        self.assertEqual(store.status()["state"], "error")
        other = FrameStore(small_config())
        other.publish(b"abcdefgh")
        other.stop()
        self.assertEqual(other.status()["state"], "stopped")
        with self.assertRaises(PreviewError):
            other.snapshot()

    def test_capture_rate_and_warning_are_separate_from_preview(self) -> None:
        store = FrameStore(small_config())
        for index in range(5):
            store.diagnostic(f"cap dqbuf: 0 seq: {index} bytesused: 8 ts: {90 + index:.6f} field: none (ts-monotonic)")
        self.assertEqual(store.status()["timing_warnings"], 0)
        for index, timestamp in enumerate((100.0, 100.011226, 100.022452)):
            store.diagnostic(f"cap dqbuf: 0 seq: {index} bytesused: 8 ts: {timestamp:.6f} field: none (ts-monotonic)")
        fps = store.status()["capture_fps"]
        assert isinstance(fps, float)
        self.assertAlmostEqual(fps, 89.08, places=2)
        store.diagnostic("cap dqbuf: 0 seq: 3 bytesused: 8 ts: 100.1 field: none (ts-monotonic)")
        self.assertEqual(store.status()["timing_warnings"], 1)
        store.diagnostic("cap dqbuf: 0 seq: 4 bytesused: 8 ts: 100.2 field: none (error, ts-monotonic)")
        self.assertEqual(store.status()["state"], "error")

    def test_unexpected_capture_size_or_metadata_is_not_live(self) -> None:
        for line in (
            "cap dqbuf: 0 seq: 1 bytesused: 7 ts: 100.000000 field: none (ts-monotonic)",
            "cap dqbuf: unrecognized",
        ):
            store = FrameStore(small_config())
            store.diagnostic(line)
            self.assertEqual(store.status()["state"], "error")

    def test_diagnostics_are_bounded(self) -> None:
        store = FrameStore(small_config())
        for number in range(100):
            store.diagnostic(f"diagnostic {number}")
        diagnostics = store.status()["diagnostics"]
        assert isinstance(diagnostics, list)
        self.assertEqual(len(diagnostics), 12)

    def test_lossless_gzip_negotiation(self) -> None:
        self.assertTrue(accepts_gzip("br, gzip, deflate"))
        self.assertTrue(accepts_gzip("gzip;q=0.5"))
        self.assertFalse(accepts_gzip("gzip;q=0"))
        self.assertFalse(accepts_gzip("gzip;q=invalid"))
        self.assertFalse(accepts_gzip("br"))


class ConfigTests(unittest.TestCase):
    def test_mira12_profile_requires_correct_depth_and_stride(self) -> None:
        fields = {
            "schema": "1", "sensor": "mira220", "video": "/dev/video0",
            "width": "1600", "height": "1400", "fourcc": "Y12P", "native_depth": "12",
            "stride": "2400", "sizeimage": "3360000", "fps": "89.080246455",
            "requested_exposure_us": "1000", "estimated_exposure_us": "999.761667",
            "gain_code": "1", "subdev": "/dev/v4l-subdev2", "exposure_lines": "124", "vblank": "18",
        }
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "mira220.tsv"
            for bad in ({}, {"native_depth": "8"}, {"native_depth": None}, {"stride": "1600"}):
                actual = {**fields, **bad}
                path.write_text("".join(f"{key}\t{value}\n" for key, value in actual.items()
                                        if value is not None), encoding="utf-8")
                if bad:
                    with self.assertRaises(PreviewError):
                        read_config(path, "mira220")
                else:
                    config = read_config(path, "mira220")
                    self.assertEqual(config.public()["native_bit_depth"], 12)

    def test_supported_profile_and_duplicate_rejection(self) -> None:
        fields = {
            "schema": "1", "sensor": "imx296", "video": "/dev/video8",
            "width": "1456", "height": "1088", "fourcc": "Y10P",
            "stride": "1824", "sizeimage": "1984512", "fps": "60.375670841",
            "requested_exposure_us": "1000", "estimated_exposure_us": "1006.852593",
            "gain_code": "0",
            "subdev": "/dev/v4l-subdev5", "exposure_lines": "67", "vblank": "30",
        }
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "imx296.tsv"
            path.write_text("".join(f"{key}\t{value}\n" for key, value in fields.items()), encoding="utf-8")
            config = read_config(path, "imx296")
            self.assertEqual(config.fourcc, "Y10P")
            self.assertEqual(config.stride, 1824)
            with path.open("a", encoding="utf-8") as destination:
                destination.write("stride\t1824\n")
            with self.assertRaisesRegex(PreviewError, "duplicate"):
                read_config(path, "imx296")

    def test_bad_profile_not_silently_converted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "mira220.tsv"
            path.write_text(
                "schema\t1\nsensor\tmira220\nvideo\t/dev/video0\nwidth\t1600\nheight\t1400\n"
                "fourcc\tY16\nstride\t3200\nsizeimage\t4480000\nfps\t89.08\n"
                "requested_exposure_us\t1000\nestimated_exposure_us\t999.76\ngain_code\t1\n"
                "subdev\t/dev/v4l-subdev2\nexposure_lines\t124\nvblank\t18\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(PreviewError, "unsupported"):
                read_config(path, "mira220")


class DisplayTests(unittest.TestCase):
    def test_raw12_display_preserves_high_bytes_without_normalization(self) -> None:
        from .test_capture import raw12_frame
        config = CameraConfig("mira220", "/dev/video0", 4096, 2, "Y12P", 6160, 12320, 89, 1000, 1000)
        raw, _ = raw12_frame(config)
        expected = bytes((value % 4096) >> 4 for value in range(8192))
        self.assertEqual(display_pixels(config, raw), expected)
        with self.assertRaises(PreviewError):
            display_pixels(replace(config, width=4095), raw)

    def test_grey_removes_padding_without_changing_values(self) -> None:
        self.assertEqual(display_pixels(small_config(), bytes([0, 127, 255, 88, 1, 2, 3, 99])),
                         bytes([0, 127, 255, 1, 2, 3]))

    def test_raw10_all_values_and_row_padding_match_browser_mapping(self) -> None:
        config = CameraConfig("imx296", "/dev/video8", 1024, 2, "Y10P", 1284, 2568, 60, 10000, 10000)
        frame = bytearray([199] * config.sizeimage)
        expected = bytearray()
        for y in range(2):
            for x in range(0, 1024, 4):
                values = [x + p if y == 0 else 1023 - x - p for p in range(4)]
                off = y * config.stride + x // 4 * 5
                frame[off:off + 4] = bytes(v >> 2 for v in values)
                frame[off + 4] = sum((v & 3) << (p * 2) for p, v in enumerate(values))
                expected.extend(v >> 2 for v in values)
        original = bytes(frame)
        self.assertEqual(display_pixels(config, original), bytes(expected))
        self.assertEqual(bytes(frame), original)
        with self.assertRaises(PreviewError):
            display_pixels(config, original[:-1])

    @unittest.skipIf(Image is None, "JPEG mode requires Pillow")
    def test_jpeg_is_native_sized_grayscale_and_does_not_normalize(self) -> None:
        config = replace(small_config(), width=16, height=16, stride=16, sizeimage=256)
        for value in (0, 15, 127, 255):
            frame = bytes([value] * 256)
            encoded = encode_jpeg(config, frame, 90)
            with Image.open(io.BytesIO(encoded)) as picture:
                self.assertEqual((picture.size, picture.mode), ((16, 16), "L"))
                self.assertLessEqual(abs(picture.getpixel((8, 8)) - value), 1)

    def test_missing_pillow_is_explicit_and_raw_remains_available(self) -> None:
        with patch("preview.server.Image", None):
            state = PreviewState({"mira220": FrameStore(small_config())})
            self.assertEqual(state.status()["transports"], ["raw"])
            with self.assertRaisesRegex(PreviewError, "Pillow"):
                encode_jpeg(small_config(), b"abcdefgh", 90)

    def test_server_instances_have_distinct_session_ids(self) -> None:
        self.assertNotEqual(PreviewState({}).session_id, PreviewState({}).session_id)


class HTTPTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        (self.root / "index.html").write_text("<!doctype html><title>Fixture</title>", encoding="utf-8")
        self.store = FrameStore(small_config())
        self.server = PreviewHTTPServer(0, PreviewState({"mira220": self.store}), self.root)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=3)
        self.directory.cleanup()

    def request(self, path: str, headers: dict[str, str] | None = None) -> tuple[int, dict[str, str], bytes]:
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=3)
        try:
            connection.request("GET", path, headers=headers or {})
            response = connection.getresponse()
            return response.status, dict(response.getheaders()), response.read()
        finally:
            connection.close()

    def test_status_is_explicit_about_raw_and_no_saving(self) -> None:
        code, headers, body = self.request("/api/status")
        self.assertEqual(code, 200)
        status = json.loads(body)
        self.assertFalse(status["isp"])
        self.assertFalse(status["saves_frames"])
        self.assertEqual(status["preview_fps"], 5)
        self.assertEqual(headers["Cache-Control"], "no-store")

    def test_native_frame_and_gzip_are_byte_exact(self) -> None:
        frame = b"\x00\x10\xff\xaa\x01\x02\x03\xbb"
        self.store.publish(frame)
        code, headers, body = self.request("/api/frame/mira220")
        self.assertEqual((code, body), (200, frame))
        self.assertEqual(headers["X-Frame-Id"], "1")
        code, headers, body = self.request("/api/frame/mira220", {"Accept-Encoding": "br, gzip"})
        self.assertEqual(code, 200)
        self.assertEqual(headers["Content-Encoding"], "gzip")
        self.assertEqual(gzip.decompress(body), frame)
        self.assertEqual(self.request("/api/frame/mira220?after=1")[0], 204)

    @unittest.skipIf(Image is None, "JPEG mode requires Pillow")
    def test_jpeg_transport_leaves_native_frame_available(self) -> None:
        frame = b"\x00\x10\xff\xaa\x01\x02\x03\xbb"
        self.store.publish(frame)
        code, headers, body = self.request("/api/frame/mira220?format=jpeg", {"Accept-Encoding": "gzip"})
        self.assertEqual(code, 200)
        self.assertEqual(headers["Content-Type"], "image/jpeg")
        self.assertNotIn("Content-Encoding", headers)
        self.assertEqual(headers["X-Frame-Id"], "1")
        self.assertTrue(headers["X-Preview-Session"])
        with Image.open(io.BytesIO(body)) as picture:
            self.assertEqual(picture.size, (3, 2))
        self.assertEqual(self.request("/api/frame/mira220?format=raw")[2], frame)
        self.assertEqual(self.request("/api/frame/mira220?format=jpeg&after=1")[0], 204)
        self.store.fail("capture failed")
        self.assertEqual(self.request("/api/frame/mira220?format=jpeg")[0], 503)

    def test_transport_parameters_are_strict(self) -> None:
        for query in ("format=png", "format=jpeg&format=raw", "format=", "other=1"):
            self.assertEqual(self.request("/api/frame/mira220?" + query)[0], 400)

    def test_unavailable_bad_cursor_and_unknown_camera(self) -> None:
        self.assertEqual(self.request("/api/frame/mira220")[0], 503)
        self.assertEqual(self.request("/api/frame/mira220?after=bad")[0], 400)
        self.assertEqual(self.request("/api/frame/mira220?after=1&after=2")[0], 400)
        self.assertEqual(self.request("/api/frame/unknown")[0], 404)

    def test_routes_cannot_read_arbitrary_files(self) -> None:
        self.assertEqual(self.request("/../server.py")[0], 404)
        self.assertEqual(self.request("/api/frame/../../server.py")[0], 404)
        self.assertEqual(self.request("/")[0], 200)

    def test_rebinding_and_cross_origin_are_rejected(self) -> None:
        self.assertEqual(self.request("/api/status", {"Host": "untrusted.example"})[0], 403)
        self.assertEqual(self.request("/api/status", {"Origin": "https://untrusted.example"})[0], 403)


if __name__ == "__main__":
    unittest.main()
