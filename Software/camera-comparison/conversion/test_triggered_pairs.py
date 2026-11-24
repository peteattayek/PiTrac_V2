# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
from pathlib import Path
import tempfile
import unittest

from .convert import ConversionError
from .triggered_pairs import PAIR_WINDOW_US, main, pair, read_triggered, report, Shot

HEADER = "index\tseq\tdevice_ts_s\tgap_ms\tkind\tpgm\tmean\tmin\tp01\tp99\tmax\tsaturated_fraction"


def frames_tsv(path: Path, triggered: list[str], free: int = 2) -> Path:
    """A triggered_frames report: `free` free-running frames, then one row per triggered time."""
    rows = [HEADER]
    for k in range(free):
        rows.append(f"{k}\t{k}\t{100 + k * 0.0112:.6f}\t\tfree-running" + "\t" * 7)
    for n, ts in enumerate(triggered, start=1):
        k = free + n - 1
        rows.append(f"{k}\t{k}\t{ts}\t1000.000\ttriggered\ttrigger-{n:02d}.pgm\t{40 + n}.00\t0\t1\t200\t255\t0.000010")
    path.write_text("\n".join(rows) + "\n", encoding="utf-8")
    return path


def shots(*times_us: int) -> list[Shot]:
    return [Shot(k, t, "", "") for k, t in enumerate(times_us)]


def summary(root: Path) -> dict[str, str]:
    out: dict[str, str] = {}
    for line in (root / "summary.tsv").read_text(encoding="utf-8").splitlines():
        key, value = line.split("\t")
        if key != "note":
            out[key] = value
    return out


class PairTests(unittest.TestCase):
    def test_one_frame_each_per_trigger(self) -> None:
        self.assertEqual(pair(shots(1_000_000, 4_000_000), shots(1_000_040, 4_000_030)), [(0, 0), (1, 1)])

    def test_a_missed_trigger_does_not_shift_the_rest(self) -> None:
        # The second camera missed the middle trigger: pairing by index would be wrong twice.
        self.assertEqual(pair(shots(1_000_000, 4_000_000, 7_000_000), shots(1_000_010, 7_000_020)),
                         [(0, 0), (2, 1)])

    def test_outside_the_window_is_unpaired(self) -> None:
        self.assertEqual(pair(shots(1_000_000), shots(1_000_000 + PAIR_WINDOW_US + 1)), [])
        self.assertEqual(pair(shots(1_000_000), shots(1_000_000 + PAIR_WINDOW_US)), [(0, 0)])

    def test_only_mutual_nearest_frames_pair(self) -> None:
        # b's one frame is nearest to a's second; a's first must not claim it too.
        self.assertEqual(pair(shots(1_000_000, 1_030_000), shots(1_029_000)), [(1, 0)])

    def test_empty(self) -> None:
        self.assertEqual(pair([], shots(1)), [])
        self.assertEqual(pair(shots(1), []), [])


class ReportTests(unittest.TestCase):
    def test_report(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            a = frames_tsv(root / "a.tsv", ["103.500000", "107.250000", "111.000000"])
            b = frames_tsv(root / "b.tsv", ["103.500120", "111.000090"])
            lines = report(root, [("bus11", a), ("bus10", b)])
            s = summary(root)
            self.assertEqual(s["cameras"], "bus11,bus10")
            self.assertEqual((s["triggered_bus11"], s["triggered_bus10"], s["pairs"]), ("3", "2", "2"))
            self.assertEqual((s["unpaired_bus11"], s["unpaired_bus10"]), ("1", "0"))
            self.assertEqual((s["delta_us_min"], s["delta_us_max"]), ("90", "120"))
            self.assertIn("note\tbus11 seq 3 at 107.250000 s has no frame from the other camera",
                          (root / "summary.tsv").read_text(encoding="utf-8"))
            pairs = (root / "pairs.tsv").read_text(encoding="utf-8").splitlines()
            self.assertEqual(pairs[0].split("\t")[0:3], ["pair", "bus11_seq", "bus11_ts_s"])
            self.assertEqual(pairs[1].split("\t"),
                             ["1", "2", "103.500000", "trigger-01.pgm", "41.00",
                              "2", "103.500120", "trigger-01.pgm", "41.00", "120"])
            self.assertEqual(len(pairs), 3)
            self.assertTrue(any(line.startswith("pairs: 2") for line in lines))

    def test_no_triggered_frames(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            report(root, [("x", frames_tsv(root / "a.tsv", [])), ("y", frames_tsv(root / "b.tsv", []))])
            s = summary(root)
            self.assertEqual((s["pairs"], s["delta_us_min"]), ("0", ""))

    def test_refusals(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            a = frames_tsv(root / "a.tsv", ["103.500000"])
            with self.assertRaises(ConversionError):
                report(root, [("same", a), ("same", a)])
            with self.assertRaises(ConversionError):
                report(root, [("only", a)])
            (root / "bad.tsv").write_text("index\tseq\n0\t0\n", encoding="utf-8")
            with self.assertRaises(ConversionError):
                read_triggered(root / "bad.tsv")
            (root / "short.tsv").write_text(HEADER + "\n0\t0\t100.000000\n", encoding="utf-8")
            with self.assertRaises(ConversionError):
                read_triggered(root / "short.tsv")

    def test_main(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            a = frames_tsv(root / "a.tsv", ["103.500000"])
            b = frames_tsv(root / "b.tsv", ["103.500010"])
            self.assertEqual(main(["--out", tmp, "--camera", f"bus11={a}", "--camera", f"bus10={b}"]), 0)
            self.assertEqual(summary(root)["pairs"], "1")
            self.assertEqual(main(["--out", tmp, "--camera", f"bus11={a}", "--camera", f"bus11={b}"]), 1)
            with self.assertRaises(SystemExit):
                main(["--out", tmp, "--camera", "no-equals-sign"])


if __name__ == "__main__":
    unittest.main()
