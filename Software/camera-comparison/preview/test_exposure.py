# SPDX-License-Identifier: GPL-3.0-or-later
from dataclasses import replace
import http.client
import json
from pathlib import Path
import tempfile
import threading
import unittest

from .exposure import ControlError, Controls, ExposureError, SensorControls, exposure_setting
from .server import CameraConfig, CaptureWorker, ExposureConflict, ExposureController, FrameStore, PreviewHTTPServer, PreviewState


def config_for(name: str) -> CameraConfig:
    setting = exposure_setting(name, 1000)
    return CameraConfig(
        name, "/dev/video0", 4, 2, "GREY", 4, 8, setting.fps,
        setting.requested_us, setting.estimated_us, "/dev/v4l-subdev2",
        setting.lines, setting.vblank,
    )


class MemoryControls(SensorControls):
    def __init__(self, name: str) -> None:
        super().__init__("/dev/v4l-subdev2", 1 if name == "mira220" else 0)
        config = config_for(name)
        self.current = Controls(config.exposure_lines, config.vblank, self.unity_gain)
        self.commands: list[str] = []
        self.failures = 0
        self.wrong_readback = False

    def _run(self, argument: str) -> str:
        self.commands.append(argument)
        if argument.startswith("--get"):
            return (f"exposure: {self.current.exposure}\nvertical_blanking: {self.current.vblank}\n"
                    f"analogue_gain: {self.current.gain}\n")
        if self.failures:
            self.failures -= 1
            raise ControlError("Simulated control write failure")
        if not self.wrong_readback:
            name, value = argument.removeprefix("--set-ctrl=").split("=")
            self.current = replace(
                self.current, **{("vblank" if name == "vertical_blanking" else name): int(value)}
            )
        return ""


class MemoryWorker(CaptureWorker):
    def __init__(self, store: FrameStore) -> None:
        super().__init__(store, 32)
        self.starts = 0
        self.stops = 0

    def start(self) -> None:
        self.starts += 1

    def stop(self) -> None:
        self.stops += 1
        self.store.stop()


class TimingTests(unittest.TestCase):
    def test_original_short_exposure_profile_is_preserved(self) -> None:
        mira = exposure_setting("mira220", 1000)
        imx = exposure_setting("imx296", 1000)
        self.assertEqual((mira.lines, mira.vblank), (124, 18))
        self.assertEqual((imx.lines, imx.vblank), (67, 30))
        self.assertAlmostEqual(mira.estimated_us, 999.761667, places=5)
        self.assertAlmostEqual(imx.estimated_us, 1006.852593, places=5)

    def test_long_exposure_increases_frame_window_without_gain(self) -> None:
        for name, height, row, margin in (
            ("mira220", 1400, 304 / 38.4, 7), ("imx296", 1088, 1100 / 74.25, 4)
        ):
            for requested in (30, 1000, 10000, 20000, 100000, 500000):
                setting = exposure_setting(name, requested)
                self.assertGreaterEqual(height + setting.vblank - setting.lines, margin)
                self.assertLessEqual(abs(setting.estimated_us - requested), row / 2 + 1e-6)
                self.assertLess(setting.estimated_us, 1e6 / setting.fps)
            self.assertAlmostEqual(exposure_setting(name, 500000).fps, 2, places=2)

    def test_invalid_requests_are_rejected(self) -> None:
        for value in (True, False, None, "1000", 29, 500001, 10**500, float("nan"), float("inf")):
            with self.subTest(value=value), self.assertRaises(ExposureError):
                exposure_setting("mira220", value)

    def test_grow_and_shrink_write_order_and_readback(self) -> None:
        io = MemoryControls("mira220")
        long = exposure_setting("mira220", 500000)
        io.write(Controls(long.lines, long.vblank, 1))
        self.assertTrue(io.commands[1].startswith("--set-ctrl=vertical_blanking="))
        self.assertTrue(io.commands[2].startswith("--set-ctrl=exposure="))
        io.commands.clear()
        short = exposure_setting("mira220", 1000)
        io.write(Controls(short.lines, short.vblank, 1))
        self.assertTrue(io.commands[1].startswith("--set-ctrl=exposure="))
        self.assertTrue(io.commands[2].startswith("--set-ctrl=vertical_blanking="))
        io.wrong_readback = True
        with self.assertRaisesRegex(ControlError, "readback mismatch"):
            io.write(Controls(long.lines, long.vblank, 1))


class ControllerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.state = PreviewState({name: FrameStore(config_for(name)) for name in ("mira220", "imx296")})
        self.workers = [MemoryWorker(store) for store in self.state.stores.values()]
        self.controller = ExposureController(self.state, list(self.workers))
        self.controls = {name: MemoryControls(name) for name in self.state.stores}
        self.controller.controls = dict(self.controls)
        self.state.exposure_control = self.controller

    def payload(self, mira: float = 10000, imx: float = 10000, linked: bool = True) -> dict[str, object]:
        return {"linked": linked, "exposures_us": {"mira220": mira, "imx296": imx},
                "revision": self.controller.revision, "session_id": self.state.session_id}

    def test_linked_defaults_and_updates_both_sensors(self) -> None:
        self.assertTrue(self.controller.public()["linked"])
        previous_session = self.state.session_id
        self.controller.apply(self.payload())
        for store in self.state.stores.values():
            self.assertEqual(store.config.requested_exposure_us, 10000)
        self.assertNotEqual(previous_session, self.state.session_id)
        self.assertEqual(self.controller.revision, 1)

    def test_unlinked_then_relinked_values(self) -> None:
        self.controller.apply(self.payload(1000, 500000, False))
        self.assertEqual(self.state.stores["mira220"].config.target_fps, config_for("mira220").target_fps)
        self.assertLess(self.state.stores["imx296"].config.target_fps, 2.01)
        self.assertFalse(self.controller.linked)
        self.controller.apply(self.payload(20000, 20000, True))
        self.assertTrue(self.controller.linked)
        self.assertEqual(self.state.stores["imx296"].config.requested_exposure_us, 20000)

    def test_linked_mismatch_and_bad_revision_do_not_touch_hardware(self) -> None:
        with self.assertRaises(ExposureError):
            self.controller.apply(self.payload(1000, 2000))
        payload = self.payload()
        payload["revision"] = 99
        with self.assertRaises(ExposureConflict):
            self.controller.apply(payload)
        self.assertFalse(any(io.commands for io in self.controls.values()))

    def test_partial_dual_failure_rolls_back_both_and_does_not_commit_link_state(self) -> None:
        self.controls["imx296"].failures = 1
        with self.assertRaisesRegex(ControlError, "previous settings restored"):
            self.controller.apply(self.payload(30000, 50000, False))
        self.assertTrue(self.controller.linked)
        self.assertEqual(self.controller.revision, 0)
        for name, io in self.controls.items():
            config = config_for(name)
            self.assertEqual(io.current, Controls(config.exposure_lines, config.vblank, io.unity_gain))
            self.assertEqual(self.state.stores[name].config, config)

    def test_failed_rollback_marks_cameras_error(self) -> None:
        self.controls["imx296"].failures = 3
        with self.assertRaisesRegex(ControlError, "rollback failed"):
            self.controller.apply(self.payload())
        self.assertIsNotNone(self.controller.error)
        self.assertFalse(self.controller.public()["available"])
        self.assertTrue(all(store.status()["state"] == "error" for store in self.state.stores.values()))

    def test_stale_frames_cleared_and_startup_controls_restored_on_close(self) -> None:
        for store in self.state.stores.values():
            store.publish(b"abcdefgh")
        self.controller.apply(self.payload(500000, 500000))
        self.assertTrue(all(store.status()["frame_id"] is None for store in self.state.stores.values()))
        self.controller.close()
        for name, io in self.controls.items():
            config = config_for(name)
            self.assertEqual(io.current.exposure, config.exposure_lines)
            self.assertEqual(io.current.vblank, config.vblank)


class APITests(ControllerTests):
    def setUp(self) -> None:
        super().setUp()
        self.directory = tempfile.TemporaryDirectory()
        self.server = PreviewHTTPServer(0, self.state, Path(self.directory.name))
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=3)
        self.directory.cleanup()

    def post(self, body: object, origin: str | None = "local", content_type: str = "application/json") -> int:
        host = f"127.0.0.1:{self.server.server_port}"
        headers = {"Content-Type": content_type}
        if origin is not None:
            headers["Origin"] = "http://" + host if origin == "local" else origin
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=3)
        try:
            connection.request("POST", "/api/exposure", json.dumps(body), headers)
            response = connection.getresponse()
            response.read()
            return response.status
        finally:
            connection.close()

    def test_api_success_input_validation_and_revision_conflict(self) -> None:
        payload = self.payload()
        self.assertEqual(self.post(payload), 200)
        self.assertEqual(self.post(payload), 409)
        self.assertEqual(self.post(self.payload(0, 0)), 400)
        self.assertEqual(self.post({"unexpected": True}), 400)

    def test_post_requires_same_origin_and_json(self) -> None:
        payload = self.payload()
        self.assertEqual(self.post(payload, None), 403)
        self.assertEqual(self.post(payload, "https://untrusted.example"), 403)
        self.assertEqual(self.post(payload, content_type="text/plain"), 415)
        self.assertFalse(any(io.commands for io in self.controls.values()))


if __name__ == "__main__":
    unittest.main()
