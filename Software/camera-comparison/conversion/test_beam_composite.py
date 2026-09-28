# SPDX-License-Identifier: GPL-3.0-or-later
import json
from fractions import Fraction
from dataclasses import replace
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

import av
import numpy as np
from PIL import Image, ImageDraw

from .beam_composite import Layout, Trace, interpolation_pair, master_timeline, native_gray, output_timeline, prepare_trace
from .beam_mira60 import nearest_indices, render as render_mira60
from .beam_slow_motion import render as render_slow_motion
from .convert import ConversionError, Recording
from .deflicker import IlluminationCorrection, timestamp_digest


def record(name: str, times: tuple[int, ...]) -> Recording:
    return Recording(name, Path("unused"), 4, 2, 8, 16, "Y10P", times,
                     tuple(range(len(times))), tuple("ts-monotonic" for _ in times))


def correction_model(path: Path, camera: Recording, amounts: list[float]) -> Path:
    stat = camera.raw.stat()
    np.savez(path, coefficient=np.ones((2, 2), dtype=np.float32), amounts=amounts,
             metadata=json.dumps({"schema": "pitrac-additive-deflicker-v1",
                                  "timestamps_sha256": timestamp_digest(camera.timestamps_us),
                                  "source_dimensions": [camera.width, camera.height],
                                  "source_raw_bytes": stat.st_size, "source_raw_mtime_ns": stat.st_mtime_ns}))
    return path


class CompositeTests(unittest.TestCase):
    def test_deflicker_removes_illumination_without_blending_away_a_flash(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            frames = np.full((3, 8, 16), 20, dtype=np.uint8)
            frames[0] -= 4
            frames[1] += 4
            frames[1, 4, 8] = 204
            raw = path / "mira.raw"
            original_bytes = frames.tobytes()
            raw.write_bytes(original_bytes)
            camera = Recording("mira220", raw, 16, 8, 16, 128, "GREY",
                               (0, 10000, 20000), (0, 1, 2), ("ts-monotonic",) * 3)
            model = correction_model(path / "model.npz", camera, [4, -4, 0])
            correction = IlluminationCorrection(model, camera)
            for index in range(3):
                with native_gray(camera, frames[index].tobytes()) as image:
                    correction.apply(image, index)
                    expected = np.full((8, 16), 20, dtype=np.uint8)
                    if index == 1:
                        expected[4, 8] = 200
                    np.testing.assert_array_equal(np.asarray(image), expected)
            self.assertEqual(raw.read_bytes(), original_bytes)
            fractional_model = correction_model(path / "fractional.npz", camera, [.5, 0, 0])
            fractional = IlluminationCorrection(fractional_model, camera)
            with Image.new("L", (16, 8), 20) as first, Image.new("L", (16, 8), 20) as second:
                fractional.apply(first, 0)
                fractional.apply(second, 0)
                self.assertEqual(float(np.asarray(first).mean()), 20.5)
                self.assertEqual(first.tobytes(), second.tobytes())
                fractional.apply(second, 1)
                self.assertEqual(first.tobytes(), second.tobytes())
            with self.assertRaises(ConversionError):
                IlluminationCorrection(model, replace(camera, timestamps_us=(0, 10001, 20000)))
            for amounts in ([9, 0, 0], [float("nan"), 0, 0], [1, 2]):
                correction_model(model, camera, amounts)
                with self.assertRaises(ConversionError):
                    IlluminationCorrection(model, camera)
            with Image.new("L", (16, 8)) as image, self.assertRaises(ConversionError):
                correction.apply(image, -1)
            with Image.new("RGB", (16, 8)) as image, self.assertRaises(ConversionError):
                correction.apply(image, 0)

    def test_nearest_cfr_uses_timestamps_and_sampling_phase(self):
        times = tuple(range(0, 120001, 10000))
        self.assertEqual(nearest_indices(times, 0.1), (0, 2, 3, 5, 7, 8))
        self.assertEqual(nearest_indices(times, 0.1, 3), (0, 2, 4, 5, 7, 9))
        self.assertEqual(nearest_indices(times, 0.1, 5)[0], 0)
        for duration, phase in ((0, 0), (float("nan"), 0), (31, 0), (0.11, 0),
                                (0.1, -1), (0.1, 17), (0.1, float("inf"))):
            with self.subTest(duration=duration, phase=phase), self.assertRaises(ConversionError):
                nearest_indices(times, duration, phase)
        for invalid in ((), (0, 0, 120000), tuple(range(0, 120001, 50000)), (0, 10000)):
            with self.subTest(timestamps=invalid), self.assertRaises(ConversionError):
                nearest_indices(invalid, 0.1)

    def test_camera_first_layout_does_not_require_imx296(self):
        cameras = {"mira220": record("mira220", (0, 1))}
        layout = Layout(cameras, 1280, 1, (2, 32), mira_only=True, mira_first=True)
        self.assertEqual(layout.bottom_y, 0)
        self.assertEqual(layout.plot_y, layout.header + layout.bottom_height)
        self.assertEqual(layout.overview_y, layout.plot_y + layout.plot_height)
        self.assertEqual(layout.height, layout.overview_y + layout.overview_height)
        legacy = Layout(cameras, 1280, 1, (2, 32), mira_only=True)
        self.assertEqual(legacy.overview_y, 0)
        self.assertEqual(legacy.plot_y, legacy.overview_height)
        self.assertEqual(legacy.bottom_y, legacy.overview_height + legacy.plot_height)
        with self.assertRaises(ConversionError):
            Layout(cameras, 1280, 1, mira_first=True)

    def test_fixed_voltage_axis_clips_both_panels_without_changing_trace(self):
        cameras = {"mira220": record("mira220", (0, 1))}
        for invalid in (0, -5, float("nan"), float("inf")):
            with self.subTest(voltage_max=invalid), self.assertRaises(ConversionError):
                Layout(cameras, 640, .0004, mira_only=True, voltage_max=invalid)
        with tempfile.TemporaryDirectory() as folder:
            cache = Path(folder) / "trace.npz"
            values = np.array([0, 2.5, 5, 10.394, 0, 2.5, 5, 10.394])
            np.savez(cache, minimum=values, maximum=values, counts=np.full(8, 50),
                     metadata=json.dumps({"start_seconds": 0, "end_exclusive_seconds": .0004,
                                          "sample_rate_hz": 1000000, "samples_per_bin": 50,
                                          "samples": 400, "channel": "Channel 3 (x2)"}))
            original_cache = cache.read_bytes()
            trace = Trace(cache)
            layout = Layout(cameras, 640, .0004, (0, .0004), mira_only=True,
                            hide_detail_cursor=True, window_position="right", voltage_max=5)
            for panel, bottom in ((layout.plot(trace, .0004, .0004), 287),
                                  (layout.overview(trace, .002), 214)):
                with panel:
                    pixels = np.array(panel)
                cyan = np.all(pixels == (8, 124, 170), axis=2)
                self.assertFalse(cyan[:72].any())
                self.assertFalse(cyan[bottom + 1:].any())
                for index, value in enumerate(values):
                    x = 72 + round((index + .5) * 544 / 8)
                    y = round(bottom - (min(value, 5) + .1) / 5.1 * (bottom - 72))
                    self.assertTrue(cyan[y, x], (index, value, y))
                self.assertTupleEqual(tuple(pixels[72, 106]), (212, 220, 229))
            with Image.new("RGB", (640, 360)) as panel:
                draw = ImageDraw.Draw(panel)
                with patch.object(draw, "text", wraps=draw.text) as labels:
                    layout._waveform(draw, trace, 0, .0004, 72, 287)
                self.assertIn(((18, 63), "5"), [call.args[:2] for call in labels.call_args_list])
                self.assertEqual([call.args[1] for call in labels.call_args_list], ["0", "1", "2", "3", "4", "5"])
            auto = Layout(cameras, 640, .0004, mira_only=True, hide_detail_cursor=True, window_position="right")
            explicit_auto = Layout(cameras, 640, .0004, mira_only=True, hide_detail_cursor=True,
                                   window_position="right", voltage_max=None)
            with auto.plot(trace, .0004, .0004) as first, explicit_auto.plot(trace, .0004, .0004) as second:
                self.assertEqual(first.tobytes(), second.tobytes())
                self.assertTupleEqual(first.getpixel((72 + 102, round(287 - 2.6 / 10.6 * 215))), (8, 124, 170))
            np.testing.assert_array_equal(trace.low, values)
            np.testing.assert_array_equal(trace.high, values)
            self.assertEqual(cache.read_bytes(), original_cache)

    @unittest.skipUnless(shutil.which("ffprobe"), "ffprobe is needed for encoded-output validation")
    def test_mira60_render_has_exact_cadence_native_brightness_and_trace_mapping(self):
        import csv
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            raw, cache, output = root / "mira220.raw", root / "trace.npz", root / "output"
            times = tuple(range(1000000, 1120001, 10000))
            raw.write_bytes(b"".join(bytes([index * 10]) * 224 for index in range(len(times))))
            camera = Recording("mira220", raw, 16, 14, 16, 224, "GREY", times,
                               tuple(range(len(times))), tuple("ts-monotonic" for _ in times))
            np.savez(cache, minimum=np.zeros(40000), maximum=np.zeros(40000),
                     counts=np.full(40000, 50),
                     metadata=json.dumps({"start_seconds": 0, "end_exclusive_seconds": 2,
                                          "sample_rate_hz": 1000000, "samples_per_bin": 50,
                                          "samples": 2000000, "channel": "Channel 3"}))
            with patch("conversion.beam_mira60.load_run", return_value=[camera]):
                with self.assertRaises(ConversionError):
                    render_mira60(root, cache, output, 0.5, 0.1, 640, 3)
                report = render_mira60(root, cache, output, 1.1, 0.1, 640, 3)
                fixed_output = root / "fixed-axis"
                fixed = render_mira60(root, cache, fixed_output, 1.1, 0.1, 640, 3, voltage_max=5)
            self.assertEqual(fixed["plot_voltage_max"], 5)
            self.assertEqual(fixed["voltage_out_of_range"], "clipped at plot boundaries")
            self.assertEqual((output / "frame-map.tsv").read_bytes(), (fixed_output / "frame-map.tsv").read_bytes())
            self.assertEqual((report["output_frames"], report["output_fps"], report["duration_seconds"]), (6, 60, 0.1))
            self.assertEqual(report["panels"], ["mira220", "scrolling_detail", "full_video_overview"])
            with (output / "frame-map.tsv").open() as table:
                rows = list(csv.DictReader(table, delimiter="\t"))
            self.assertEqual([int(row["mira220_frame"]) for row in rows], [0, 2, 4, 5, 7, 9])
            for row in rows:
                self.assertAlmostEqual(float(row["analyzer_time_s"]), float(row["camera_time_s"]) + 1.1)
                self.assertAlmostEqual(float(row["window_end_analyzer_s"]), float(row["analyzer_time_s"]))
                self.assertAlmostEqual(float(row["window_end_analyzer_s"]) - float(row["window_start_analyzer_s"]), 1)
            with av.open(str(output / "beam-composite-60fps.mp4")) as video:
                frames = list(video.decode(video=0))
            self.assertEqual(len(frames), 6)
            for index, frame in enumerate(frames):
                self.assertEqual(frame.pts * frame.time_base, Fraction(index, 60))
                self.assertEqual((frame.width, frame.height), (640, 1242))
                pixels = frame.to_ndarray(format="rgb24")
                self.assertLessEqual(abs(float(pixels[80:500, 30:600].mean()) - int(rows[index]["mira220_frame"]) * 10), 2)
            self.assertEqual((output / "status").read_text().strip(), "VERIFIED_COMPOSITE")
            model = correction_model(root / "deflicker.npz", camera, [5] * len(times))
            corrected_output = root / "corrected"
            with patch("conversion.beam_mira60.load_run", return_value=[camera]):
                corrected = render_mira60(root, cache, corrected_output, 1.1, 0.1, 640, 3, model)
            self.assertEqual((output / "frame-map.tsv").read_bytes(), (corrected_output / "frame-map.tsv").read_bytes())
            self.assertIsNotNone(corrected["deflicker"])
            with av.open(str(corrected_output / "beam-composite-60fps.mp4")) as video:
                first = next(video.decode(video=0)).to_ndarray(format="rgb24")
            self.assertLessEqual(abs(float(first[80:500, 30:600].mean()) - 5), 2)
            with patch("conversion.beam_mira60.load_run", return_value=[camera]), self.assertRaises(FileExistsError):
                render_mira60(root, cache, output, 1.1, 0.1, 640, 3)

    def test_mira_master_keeps_only_original_mira_frame_times(self):
        camera = record("mira220", (100, 110, 121, 132, 144))
        self.assertEqual(master_timeline(camera, 104, 140), (110, 121, 132))
        self.assertEqual(interpolation_pair((104, 120, 136), 110), (0, 1, 0.375))
        self.assertEqual(interpolation_pair((104, 120, 136), 120), (1, 1, 0.0))
        with self.assertRaises(ConversionError):
            interpolation_pair((104, 120, 136), 103)

    @unittest.skipUnless(shutil.which("ffprobe"), "ffprobe is needed for encoded-output validation")
    def test_slow_motion_keeps_every_raw_frame_at_ten_fps_and_preserves_legacy_repeats(self):
        import csv
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            raw, cache, reference = root / "mira220.raw", root / "trace.npz", root / "reference.json"
            times = tuple(range(1000000, 1050001, 10000))
            raw.write_bytes(b"".join(bytes([index * 20]) * 128 for index in range(len(times))))
            camera = Recording("mira220", raw, 16, 8, 16, 128, "GREY", times,
                               tuple(range(len(times))), tuple("ts-monotonic" for _ in times))
            np.savez(cache, minimum=np.zeros(40000), maximum=np.zeros(40000),
                     counts=np.full(40000, 50),
                     metadata=json.dumps({"start_seconds": 0, "end_exclusive_seconds": 2,
                                          "sample_rate_hz": 1000000, "samples_per_bin": 50,
                                          "samples": 2000000, "channel": "Channel 3"}))
            for output_fps, mira_first in ((10, True), (30, False)):
                ref = {"reference_origin_us": 1000000, "offset_seconds": 1.1}
                if mira_first:
                    ref["panels"] = ["mira220", "scrolling_detail", "full_video_overview"]
                else:
                    ref["common_start_offset_us"] = 0
                reference.write_text(json.dumps(ref))
                output = root / f"fps-{output_fps}"
                with patch("conversion.beam_slow_motion.load_run", return_value=[camera]):
                    report = render_slow_motion(root, cache, reference, output, .01, .031,
                                                10, output_fps, .1, mira_first)
                repeats = output_fps // 10
                self.assertEqual(report["source_frame_indices"], [1, 2, 3])
                self.assertEqual(report["raw_frames"], 3)
                self.assertEqual(report["output_frames"], 3 * repeats)
                self.assertEqual(report["duration_seconds"], .3)
                self.assertEqual(report["repeats_per_raw_frame"], repeats)
                self.assertEqual(report["slowdown_factor"], 10)
                self.assertEqual(report["panels"][0], "mira220" if mira_first else "full_video_overview")
                self.assertEqual(report["overview_analyzer_range_seconds"], (1.11, 1.131))
                with (output / "frame-map.tsv").open() as table:
                    rows = list(csv.DictReader(table, delimiter="\t"))
                self.assertEqual([int(row["mira220_frame"]) for row in rows],
                                 [index for index in (1, 2, 3) for _ in range(repeats)])
                for row in rows:
                    plot_time = float(row["plot_original_video_time_s"])
                    raw_time = float(row["raw_original_video_time_s"])
                    if repeats == 1 or int(row["repeat_index"]) == 0:
                        self.assertEqual(plot_time, raw_time)
                    else:
                        self.assertGreater(plot_time, raw_time)
                    self.assertAlmostEqual(float(row["analyzer_time_s"]), plot_time + 1.1)
                    self.assertAlmostEqual(float(row["window_end_analyzer_s"]), plot_time + 1.1)
                    self.assertAlmostEqual(float(row["window_start_analyzer_s"]), plot_time + 1)
                with av.open(str(output / report["file"])) as video:
                    frames = list(video.decode(video=0))
                self.assertEqual(len(frames), 3 * repeats)
                for index, frame in enumerate(frames):
                    self.assertEqual(frame.pts * frame.time_base, Fraction(index, output_fps))
                    camera_y = 42 if mira_first else 682
                    pixels = frame.to_ndarray(format="rgb24")[camera_y + 30:camera_y + 400, 30:1200]
                    self.assertLessEqual(abs(float(pixels.mean()) - int(rows[index]["mira220_frame"]) * 20), 2)
                self.assertEqual((output / "status").read_text().strip(), "VERIFIED_SLOW_MOTION")
                if mira_first:
                    model = correction_model(root / "deflicker.npz", camera, [5] * len(times))
                    corrected_output = root / "corrected"
                    with patch("conversion.beam_slow_motion.load_run", return_value=[camera]):
                        corrected = render_slow_motion(root, cache, reference, corrected_output, .01, .031,
                                                       10, 10, .1, True, model)
                    self.assertEqual((output / "frame-map.tsv").read_bytes(),
                                     (corrected_output / "frame-map.tsv").read_bytes())
                    self.assertEqual(report["source_frame_sha256"], corrected["source_frame_sha256"])
                    with av.open(str(corrected_output / corrected["file"])) as video:
                        first = next(video.decode(video=0)).to_ndarray(format="rgb24")
                    self.assertLessEqual(abs(float(first[80:500, 30:1200].mean()) - 25), 2)
            reference.write_text(json.dumps({"reference_origin_us": 1000000, "offset_seconds": 1.1}))
            with patch("conversion.beam_slow_motion.load_run", return_value=[camera]), self.assertRaises(ConversionError):
                render_slow_motion(root, cache, reference, root / "unsupported", .01, .031)

    def test_union_timeline_keeps_every_native_update(self):
        cameras = [record("mira220", (100, 110, 121, 132, 144)),
                   record("imx296", (104, 120, 136, 152))]
        self.assertEqual(output_timeline(cameras, 104, 30), (104, 110, 120, 121, 132))
        with self.assertRaises(ConversionError):
            output_timeline(cameras, 100, 30)
        with self.assertRaises(ConversionError):
            output_timeline(cameras, 104, 100)

    def test_raw10_padding_and_low_bits_do_not_change_linear_display(self):
        raw = bytes([0, 63, 127, 255, 228, 99, 99, 99, 255, 127, 63, 0, 27, 99, 99, 99])
        image = native_gray(record("imx296", (0, 1)), raw)
        self.assertEqual(image.size, (4, 2))
        self.assertEqual(image.tobytes(), bytes([0, 63, 127, 255, 255, 127, 63, 0]))

    def test_grey_view_preserves_native_brightness_and_ignores_padding(self):
        camera = Recording("mira220", Path("unused"), 3, 2, 4, 8, "GREY",
                           (0, 1), (0, 1), ("ts-monotonic", "ts-monotonic"))
        with native_gray(camera, bytes([0, 1, 63, 200, 64, 127, 255, 200])) as image:
            self.assertEqual(image.tobytes(), bytes([0, 1, 63, 64, 127, 255]))

    def test_peak_preserving_envelope_and_missing_data_regions(self):
        with tempfile.TemporaryDirectory() as folder:
            source, cache = Path(folder)/"analog.csv", Path(folder)/"trace.npz"
            values = [0.0]*100
            values[13], values[75] = 5.0, -0.25
            source.write_text("Time [s],Channel 3\n" + "".join(
                f"{index/1e6:.9f},{value}\n" for index, value in enumerate(values)
            ))
            metadata = prepare_trace(source, cache, 10)
            self.assertEqual(metadata["samples"], 100)
            trace = Trace(cache)
            low, high, valid = trace.envelope(-50e-6, 150e-6, 4)
            self.assertEqual(valid.tolist(), [False, True, True, False])
            self.assertEqual(float(high[1]), 5.0)
            self.assertEqual(float(low[2]), -0.25)
            self.assertTrue(np.isnan(low[0]))
            with self.assertRaises(ConversionError):
                prepare_trace(source, cache, 10)

    def test_gaps_in_csv_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            source, cache = Path(folder)/"analog.csv", Path(folder)/"trace.npz"
            source.write_text("Time [s],Channel 3\n0.000000,0\n0.000002,1\n")
            with self.assertRaises(ConversionError):
                prepare_trace(source, cache)

    def test_inconsistent_cache_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            cache = Path(folder)/"bad.npz"
            np.savez(cache, minimum=[2], maximum=[1], counts=[50],
                     metadata=json.dumps({"start_seconds":0,"end_exclusive_seconds":50e-6,
                                          "sample_rate_hz":1000000,"samples_per_bin":50,"samples":50}))
            with self.assertRaises(ConversionError):
                Trace(cache)


if __name__ == "__main__":
    unittest.main()
