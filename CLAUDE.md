# Agent instructions — PiTrac V2

This repo holds the PiTrac V2 golf launch monitor: hardware (KiCad), RP2354B firmware
(C, Pico SDK 2.3.0) and Pi 5 camera software. Active work is **firmware bench bring-up** in
`Hardware/firmware/`. The owner runs the bench; the model analyses captures, writes firmware
and keeps the docs true.

## Read first, in this order

1. **`Hardware/firmware/HANDOFF.md`** — binding: conventions, safety rules, build/flash/test
   commands, tooling traps on this machine, expensive measurement lessons.
2. **`Hardware/firmware/PROGRESS.md` §0** (status table) and **the top block of §10** (the live
   resume point). §10 wins over any other doc about board state.
3. The `Hardware/firmware/BENCH_*.md` procedure for the phase in hand (today:
   `BENCH_P6_STROBE.md`, 6c).

`DEVELOPER_GUIDE.md` (repo root) is the whole-repo map. `HARDWARE_REFERENCE.md` is generated from
the netlist. When a doc and the netlist disagree, the netlist is right.

## Rules that matter most (details in HANDOFF.md)

- **Do not commit** unless the owner asks. Leave the tree ready and say so.
- After any firmware change, put **"reflash first"** at the top of the next instructions and say
  what is in the build (stamp, size, SHA256).
- Bench steps state the board state, exact commands, expected output and pass criterion, in
  physical units. If something you said was wrong, say so plainly and log it in `PROGRESS.md`.
- After any change, audit the docs **and** the firmware's printed output and comments for
  anything stale; run `python Hardware/firmware/tools/check_doc_links.py`.
- Safety: nothing conductive near **D12 (36 V)**; **GPIO27 `PULSE_LIMIT_DISABLE` stays 0**; no Pi
  on J8 and no camera on J4; beam duty ≤ 25 %; never run +5 V from USB; attach probes only with
  the board unpowered (PSU and USB off); **never raise a live-strobe limit to make a test pass**.

The two files `AGENTS.md` and `CLAUDE.md` at the repo root are identical pointers for different
harnesses — keep them in sync, and keep them short: conventions live in `HANDOFF.md`, status in
`PROGRESS.md`.
