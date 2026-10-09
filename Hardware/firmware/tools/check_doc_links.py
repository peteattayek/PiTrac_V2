#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
"""
check_doc_links.py -- check every relative link and #anchor in the firmware docs.

Covers DEVELOPER_GUIDE.md, HARDWARE_REFERENCE.md, Hardware/README.md and every
Hardware/firmware/*.md. Anchors follow GitHub's heading-slug rules (lower case,
spaces to '-', punctuation and emoji dropped, duplicates suffixed -1, -2 ...).
Links inside ``` fences and absolute URLs are skipped.

    python tools/check_doc_links.py      # exit 1 if anything is broken

Run it after any doc restructure: the 2026-10-05 PROGRESS/ARCHIVE split and every
later doc audit used it (it lived outside the repo until 2026-10-09).
"""
import pathlib
import re
import sys
import unicodedata

ROOT = pathlib.Path(__file__).resolve().parents[3]
files = [ROOT / "DEVELOPER_GUIDE.md", ROOT / "HARDWARE_REFERENCE.md", ROOT / "Hardware" / "README.md"]
files += sorted((ROOT / "Hardware" / "firmware").glob("*.md"))

LINK = re.compile(r"(?<!\!)\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
HEAD = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")


def slug(text):
    text = re.sub(r"`([^`]*)`", r"\1", text)
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = text.strip().lower()
    out = []
    for ch in text:
        cat = unicodedata.category(ch)
        if ch in "-_ " or cat[0] in "LN":
            out.append("-" if ch == " " else ch)
        # everything else (punctuation, symbols, emoji) is dropped
    return "".join(out)


def anchors(path):
    seen, result, fence = {}, set(), False
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.lstrip().startswith("```"):
            fence = not fence
            continue
        if fence:
            continue
        m = HEAD.match(line)
        if not m:
            continue
        s = slug(m.group(2))
        n = seen.get(s, 0)
        result.add(s if n == 0 else f"{s}-{n}")
        seen[s] = n + 1
    return result


def main():
    cache, bad = {}, 0
    for f in files:
        if not f.exists():
            print(f"missing doc: {f.relative_to(ROOT)}")
            bad += 1
            continue
        fence = False
        for no, line in enumerate(f.read_text(encoding="utf-8").splitlines(), 1):
            if line.lstrip().startswith("```"):
                fence = not fence
                continue
            if fence:
                continue
            for target in LINK.findall(line):
                if re.match(r"^[a-z]+:", target):
                    continue
                path_part, _, frag = target.partition("#")
                dest = f if not path_part else (f.parent / path_part).resolve()
                if not dest.exists():
                    print(f"{f.relative_to(ROOT)}:{no}: missing file -> {target}")
                    bad += 1
                    continue
                if frag and dest.suffix == ".md":
                    if dest not in cache:
                        cache[dest] = anchors(dest)
                    if frag.lower() not in cache[dest]:
                        print(f"{f.relative_to(ROOT)}:{no}: missing anchor -> {target}")
                        bad += 1
    print(f"checked {len(files)} files; {bad} problem(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
