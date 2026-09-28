# SPDX-License-Identifier: GPL-3.0-or-later
from dataclasses import replace
from pathlib import Path
import shutil
import tempfile
import unittest

from .convert import (
    ConversionError, Recording, encode, first_seconds, gray8, load_recording, timestamp_us, verify_mp4,
)

try:
    import av
except ImportError:
    av = None


def fixture(raw: Path) -> Recording:
    times = (1_100_000, 1_111_223, 1_122_450, 1_133_660, 1_144_890)
    return Recording("mira220", raw, 16, 16, 16, 256, "GREY", times, tuple(range(5)),
                     tuple("(ts-monotonic, ts-src-eof)" for _ in times))


class PixelTests(unittest.TestCase):
    def test_microseconds_are_exact_without_float_rounding(self) -> None:
        self.assertEqual(timestamp_us("13789.400375"), 13789400375)
        for invalid in ("NaN", "-1", "1.1234567", "1e5"):
            with self.assertRaises(ConversionError):
                timestamp_us(invalid)

    def test_grey_padding_is_removed_without_normalization(self) -> None:
        record = replace(fixture(Path("unused")), width=2, height=2, stride=4, sizeimage=8)
        self.assertEqual(gray8(record, bytes([0, 10, 99, 99, 20, 255, 99, 99])), bytes([0, 10, 20, 255]))
        with self.assertRaises(ConversionError):
            gray8(record, b"short")

    def test_raw10_all_values_match_unpack_then_shift(self) -> None:
        record = replace(fixture(Path("unused")), name="imx296", width=1024, height=2,
                         stride=1296, sizeimage=2592, fourcc="Y10P")
        raw = bytearray([211] * record.sizeimage)
        expected = bytearray()
        for row in range(2):
            for x in range(0, 1024, 4):
                values = [x + i if row == 0 else 1023 - x - i for i in range(4)]
                position = row * record.stride + x // 4 * 5
                raw[position:position + 4] = bytes(value >> 2 for value in values)
                raw[position + 4] = sum((value & 3) << (2 * i) for i, value in enumerate(values))
                expected.extend(value >> 2 for value in values)
        self.assertEqual(gray8(record, bytes(raw)), bytes(expected))

    def test_clip_keeps_source_pts_and_ends_exactly_at_requested_time(self) -> None:
        record = first_seconds(fixture(Path("unused")), "0.04")
        self.assertEqual(record.timestamps_us, (1_100_000, 1_111_223, 1_122_450, 1_133_660))
        self.assertEqual(record.durations_us(), (11223, 11227, 11210, 6340))
        self.assertEqual(sum(record.durations_us()), 40000)
        with self.assertRaises(ConversionError):
            first_seconds(record, "10")
        with self.assertRaises(ConversionError):
            first_seconds(record, "0.0000001")


class InputTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.run = Path(self.directory.name)
        (self.run / "mira220.tsv").write_text(
            "schema\t1\nsensor\tmira220\nwidth\t1600\nheight\t1400\n"
            "fourcc\tGREY\nstride\t1600\nsizeimage\t2240000\n", encoding="utf-8",
        )
        self.table = "sequence\ttimestamp_seconds\tbytesused\tflags\n"
        self.log = ""
        for index in range(8):
            timestamp = f"10.{index * 11226:06d}"
            self.log += f"cap dqbuf: 0 seq: {index} bytesused: 2240000 ts: {timestamp} field: none (ts-monotonic, ts-src-eof)\n"
            if index >= 5:
                self.table += f"{index}\t{timestamp}\t2240000\t(ts-monotonic, ts-src-eof)\n"
        (self.run / "mira220.frames.tsv").write_text(self.table, encoding="utf-8")
        (self.run / "mira220.capture.log").write_text(self.log, encoding="utf-8")
        with (self.run / "mira220.raw").open("wb") as output:
            output.truncate(3 * 2240000)

    def tearDown(self) -> None:
        self.directory.cleanup()

    def test_original_log_and_frame_table_are_both_checked(self) -> None:
        record = load_recording(self.run, "mira220", 3, 5)
        self.assertEqual(record.timestamps_us[0], 10056130)
        (self.run / "mira220.frames.tsv").write_text(self.table.replace("10.067356", "10.067357"), encoding="utf-8")
        with self.assertRaisesRegex(ConversionError, "differs from its original"):
            load_recording(self.run, "mira220", 3, 5)

    def test_wrong_file_size_is_rejected(self) -> None:
        with (self.run / "mira220.raw").open("wb") as output:
            output.truncate(1)
        with self.assertRaisesRegex(ConversionError, "count/file size"):
            load_recording(self.run, "mira220", 3, 5)

    def test_non_monotonic_or_error_frame_is_rejected(self) -> None:
        for table in (
            self.table.replace("10.067356", "10.056130"),
            self.table.replace("(ts-monotonic", "(error, ts-monotonic"),
            self.table.replace("2240000\t", "1\t"),
        ):
            (self.run / "mira220.frames.tsv").write_text(table, encoding="utf-8")
            with self.assertRaises(ConversionError):
                load_recording(self.run, "mira220", 3, 5)


@unittest.skipIf(av is None or shutil.which("ffprobe") is None, "Needs PyAV and ffprobe")
class EncoderTests(unittest.TestCase):
    def test_every_vfr_pts_and_duration_survives_mp4_muxing(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            raw = root / "source.raw"
            raw.write_bytes(b"".join(bytes([index * 40]) * 256 for index in range(5)))
            full = fixture(raw)
            for name, record, origin in (
                ("all", full, full.timestamps_us[0]),
                ("clip", first_seconds(full, "0.04"), full.timestamps_us[0]),
            ):
                with self.subTest(name=name):
                    destination = root / f"{name}.mp4"
                    encode(record, destination, origin, 18, "veryfast", 1)
                    probe = shutil.which("ffprobe")
                    assert probe is not None
                    verified = verify_mp4(record, destination, origin, probe)
                    self.assertTrue(verified["every_pts_and_duration_exact"])
                    self.assertEqual(verified["verified_frames"], len(record.timestamps_us))
                    if name != "all":
                        self.assertEqual(verified["playback_duration_us"], 40000)
                    self.assertEqual(raw.stat().st_size, 5 * 256)
                    with self.assertRaises(FileExistsError):
                        encode(record, destination, origin, 18, "veryfast", 1)
            with self.assertRaisesRegex(ConversionError, "first source timestamp"):
                encode(full, root / "offset.mp4", 1_000_000, 18, "veryfast", 1)


if __name__ == "__main__":
    unittest.main()
