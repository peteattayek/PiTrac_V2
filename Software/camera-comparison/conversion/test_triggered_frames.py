# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
from pathlib import Path
import tempfile
import unittest

from .convert import ConversionError
from .triggered_frames import free_running_after_ready, main, parse_log, report, stats

W, H, STRIDE = 8, 4, 16
SIZE = STRIDE * H


def line(seq: int, ts: str, used: int = SIZE) -> str:
    return (f"cap dqbuf: 0 seq: {seq:6d} bytesused: {used} ts: {ts} delta: 11.226 ms "
            "fps: 89.08 (ts-monotonic, ts-src-eof)")


def frame_bytes(value: int) -> bytes:
    # Each row: W pixels of value, then padding the converter must drop.
    return (bytes([value]) * W + b"\xee" * (STRIDE - W)) * H


class Capture:
    """Three free-running frames 11.2 ms apart, READY, then two triggers seconds apart."""

    times = ("100.000000", "100.011200", "100.022400", "103.500000", "107.250000")
    ready = "100.300000"

    def __init__(self, root: Path, frames: int = 5, extra: bytes = b"") -> None:
        self.root = root
        self.log = root / "capture.log"
        self.raw = root / "mira220.raw"
        self.log.write_text("VIDIOC_STREAMON returned 0 (Success)\n" +
                            "\n".join(line(i, t) for i, t in enumerate(self.times)) + "\n")
        self.raw.write_bytes(b"".join(frame_bytes(10 * (i + 1)) for i in range(frames)) + extra)

    def run(self, **overrides) -> list[str]:
        args = dict(log=self.log, raw=self.raw, out=self.root, ready=self.ready,
                    width=W, height=H, stride=STRIDE, sizeimage=SIZE, fourcc="GREY")
        args.update(overrides)
        return report(**args)

    def tsv(self, name: str) -> list[list[str]]:
        return [row.split("\t") for row in (self.root / name).read_text().splitlines()]


class ParseTests(unittest.TestCase):
    def test_classifies_by_ready_time_and_keeps_order(self) -> None:
        text = "\n".join(line(i, t) for i, t in enumerate(Capture.times))
        frames = parse_log(text, 100_300_000)
        self.assertEqual([f.triggered for f in frames], [False, False, False, True, True])
        self.assertEqual([f.index for f in frames], list(range(5)))
        self.assertIsNone(frames[0].gap_us)
        self.assertEqual(frames[1].gap_us, 11_200)
        self.assertEqual(frames[4].gap_us, 3_750_000)

    def test_rejects_partial_writes_and_backwards_time(self) -> None:
        with self.assertRaises(ConversionError):
            parse_log(line(0, "1.000000") + "\n2240000 != 1000\n", 0)
        with self.assertRaises(ConversionError):
            parse_log(line(0, "2.000000") + "\n" + line(1, "1.000000"), 0)

    def test_free_running_after_ready_needs_five_fast_frames(self) -> None:
        def frames(gaps_ms: list[float]) -> list:
            t, text = 10.0, [line(0, "10.000000")]
            for i, gap in enumerate(gaps_ms, 1):
                t += gap / 1000
                text.append(line(i, f"{t:.6f}"))
            return parse_log("\n".join(text), 9_000_000)
        self.assertTrue(free_running_after_ready(frames([11.2] * 4)))
        self.assertFalse(free_running_after_ready(frames([11.2] * 3)))
        self.assertFalse(free_running_after_ready(frames([11.2, 11.2, 900, 11.2, 11.2])))


class ReportTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_triggered_frames_become_pgms_without_row_padding(self) -> None:
        cap = Capture(self.root)
        lines = cap.run()
        first = (self.root / "trigger-01.pgm").read_bytes()
        self.assertEqual(first, f"P5\n{W} {H}\n255\n".encode() + bytes([40]) * (W * H))
        self.assertEqual((self.root / "trigger-02.pgm").read_bytes()[-1], 50)
        self.assertFalse((self.root / "trigger-03.pgm").exists())
        rows = cap.tsv("frames.tsv")
        self.assertEqual([r[4] for r in rows[1:]], ["free-running"] * 3 + ["triggered"] * 2)
        self.assertEqual(rows[4][5:8], ["trigger-01.pgm", "40.00", "40"])
        summary = dict(cap.tsv("summary.tsv"))
        self.assertEqual(summary["triggered_frames"], "2")
        self.assertEqual(summary["free_running_frames"], "3")
        self.assertEqual(summary["free_running_median_interval_ms"], "11.200")
        self.assertEqual(summary["first_triggered_after_ready_s"], "3.200")
        self.assertEqual(summary["free_running_after_ready"], "no")
        self.assertIn("triggered frame: seq 4, device time 107.250000 s, 3.750 s after the previous frame",
                      lines)

    def test_short_raw_file_reports_frames_it_cannot_convert(self) -> None:
        cap = Capture(self.root, frames=4, extra=b"\x01" * 7)
        cap.run()
        self.assertTrue((self.root / "trigger-01.pgm").exists())
        self.assertFalse((self.root / "trigger-02.pgm").exists())
        notes = [v for k, v in cap.tsv("summary.tsv") if k == "note"]
        self.assertIn("the raw file holds 4 whole frames of 5 logged; "
                      "the missing ones are reported but not converted", notes)
        self.assertIn("7 trailing bytes of a partial frame are ignored", notes)

    def test_refuses_other_formats_and_layouts(self) -> None:
        cap = Capture(self.root)
        with self.assertRaises(ConversionError):
            cap.run(fourcc="Y12P")
        with self.assertRaises(ConversionError):
            cap.run(sizeimage=SIZE + 1)

    def test_cli_reports_errors_without_a_traceback(self) -> None:
        cap = Capture(self.root)
        common = ["--log", str(cap.log), "--raw", str(cap.raw), "--out", str(self.root),
                  "--ready-ts", cap.ready, "--width", str(W), "--height", str(H),
                  "--stride", str(STRIDE), "--sizeimage", str(SIZE)]
        self.assertEqual(main(common + ["--fourcc", "Y12P"]), 1)
        self.assertEqual(main(common + ["--fourcc", "GREY"]), 0)


class StatsTests(unittest.TestCase):
    def test_percentiles_and_saturation(self) -> None:
        pixels = bytes(range(100)) + bytes([255]) * 100
        s = stats(pixels)
        self.assertEqual((s["min"], s["max"]), (0, 255))
        self.assertEqual(s["p01"], 1)
        self.assertEqual(s["p99"], 255)
        self.assertAlmostEqual(s["mean"], (sum(range(100)) + 25500) / 200)
        self.assertAlmostEqual(s["saturated_fraction"], 0.5)


if __name__ == "__main__":
    unittest.main()
