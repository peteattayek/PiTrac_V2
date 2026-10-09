# Handoff — PiTrac V2 firmware bring-up

**For a person or model taking over this work with no prior context.** Written 2026-09-17.
At writing, the last bench session was 2026-08-31. For subsequent bench work, use
`PROGRESS.md` §10, not this historical date.

**Since this was written** (conventions only; status is in §10): the strobe interlock lesson
(§8 "Interlocks"), the Release-vs-Debug check and Serial Monitor file logging into `captures/`
(§4), the `EEXIST` CMake Tools trap (§7), the doc-discipline rules for `PROGRESS_ARCHIVE.md` and
for keeping dated results out of the `BENCH_*.md` procedures (§6), and the 2026-10-05 rule that
**every numeric CLI argument goes through `parse_u32` / `parse_float`** in `cli.c` — never a
bare `strtoul`/`strtof`. **2026-10-05 → 10-09:** probe-point safety (§3 rule 8), live strobe
current (§3 rule 9), probe grounding and scope bandwidth (§8 "Probing"), the host-test,
build-stamp and doc-link-check commands (§4), an independent review before any safety-relevant
firmware is handed over (§5 rule 8), and the PowerShell / MSVC / KiCad traps (§7). Updated for
a change of model/harness on 2026-10-09: nothing here depends on the previous harness.

This file holds what the other docs do *not*: how the work is done, the traps on this machine,
and the lessons that cost real bench time. **It deliberately does not hold live status** — that
lives in `PROGRESS.md` §10 and changes every session. If this file and §10 ever disagree about
the state of the board, **§10 wins**.

---

## 1. What this is

Firmware for the **RP2354B** on the PiTrac V2 launch-monitor board, in `Hardware/firmware/`.
C, Pico SDK 2.3.0, ARM. The board detects a golf ball optically (a 104.17 kHz modulated IR beam,
lock-in detection, a comparator), will fire a high-current IR strobe, and powers and talks to a
Raspberry Pi 5.

Work proceeds as **phased bench bring-up**: each phase has a `BENCH_*.md` procedure, and results
go into `PROGRESS.md`. The owner runs the bench; the model reads captures, analyses them,
writes firmware, and keeps the docs true.

---

## 2. Read in this order

`AGENTS.md` and `CLAUDE.md` at the repo root (identical, added 2026-10-09) are short pointers
that harnesses read automatically; they send the reader here. Keep them in sync and short.

| # | File | Why |
|---|---|---|
| 1 | **this file** | conventions and traps |
| 2 | `PROGRESS.md` **§0** | one-table status: boards, firmware, what is done |
| 3 | `PROGRESS.md` **§10** (top block) | **the live resume point** and the open-items table |
| 4 | `PROGRESS.md` **§0.5** | what is SETTLED (never re-derive) vs what REPEATS per board |
| 5 | `PROGRESS.md` **§1** | hard safety rules |
| 6 | the `BENCH_*.md` for the phase in hand | the step-by-step procedure |
| 7 | `BRINGUP_NEW_BOARD.md` | only if a **new board** is being brought up — it is the driver for that |

Reference, as needed: `PROGRESS.md` §2 (verified hardware facts), §6 (measurement log — entries
before 2026-09-18 are in `PROGRESS_ARCHIVE.md` §6), §8
(decisions — **do not re-litigate these**), `NEXT_BOARD_REV.md` (hardware change requests),
`ARCHITECTURE.md` (what runs on PIO/PWM/DMA vs the CPU), and `HARDWARE_REFERENCE.md` **at the repo
root** (generated from the KiCad netlist by `tools/netlist_report.py`).

The netlist is `Hardware/The_Second_Board_To_Rule_Them_All/The_Second_Board_To_Rule_Them_All.net`.
**When a doc and the netlist disagree, the netlist is right** — several real bugs came from
hand-written hardware summaries that had drifted.

---

## 3. Safety — non-negotiable

From `PROGRESS.md` §1, plus rules learned the hard way. None of these is relaxed without an
explicit decision from the owner.

1. 🔴 **Nothing conductive near D12.** Its cathode sits at **36 V** (VIR through R77); its anode is
   the TIA summing node. Foil laid across it destroyed **U11B on board 1** (2026-08-17, §11).
   Board 1 is out of service **for optics**; since 2026-10-06 it is the **strobe** board.
   When suggesting an optical block or baffle, say this explicitly.
2. 🔴 **`PULSE_LIMIT_DISABLE` (GPIO27) stays 0.** Written in exactly one place, `safe_state()`. No
   CLI path, no config flag. It defeats the strobe's hardware pulse-width watchdog.
3. 🔴 **No Pi 5 on J8 until Phase 7c.** An RP2354 reset is a hard power cut to the Pi.
4. 🔴 **Never run the +5 V rail from USB.** Firmware enforces it twice; don't design around it.
5. ⚠ **Beam duty: 25 % operating, 35 % is a destruction ceiling, not a setting** (CR-12 thermal:
   junction 123–133 °C at 30 % against 145 °C max). The beam powers up at 2 % by design.
6. ⚠ **Do not connect J4 (camera)** — the Mira220 I/O is 1.8 V with no 3.3 V tolerance (CR-09).
7. **Power-command acknowledgements are not rail-state evidence.** Always check `stat`
   for **STANDBY / latch 0** after shutdown before adjusting the exposed fixture.
   A stale `on` request caused an unexpected restart on 2026-09-18; the active workaround
   and fix status belong in `PROGRESS.md` §10.
8. 🔴 **No hand-held probe tips on a powered board where pads are under ~1.5 mm apart.** A probe
   on R61 sparked on **board 2 (2026-10-05)**: R61's pads sit 0.58–1.15 mm from +12 V (C54) and
   GND pads. Attach clips or soldered wires with the board **unpowered** (PSU *and* USB off — USB
   alone keeps +3V3 live), then power up. When a bench step names a probe point, the
   instruction must say what that point sits next to — the same omission as the foil on D12.
   **Check the PCB, not memory:** pad positions and the nearest other-net copper come from
   `The_Second_Board_To_Rule_Them_All.kicad_pcb` (parse it; §7), and `PROGRESS.md` already
   records several (R61, R64, R65, TP2, TP3, TP4). The 2026-10-07 R65 ground advice ignored a
   gap that was already written down (§6 2026-10-08). The current lead plan, with a
   do-not-solder list, is in `BENCH_P6_STROBE.md` 6c, "Leads to solder for 6c/6d".
9. 🔴 **Live strobe current (6c onward) is the most dangerous thing on this board** — up to ~9 A
   from 36 V through Q9 in linear mode. It happens only in firmware LIVE mode
   (`strobe live on confirm`); the dry default cannot command current whatever is on J3. Run it
   only by `BENCH_P6_STROBE.md` 6c, from its board-state table: FLIR on Q9/HS1, J3 connected or
   disconnected only with PSU **and** USB off, scope on TP4 and TP3. **Never raise a live limit
   to make a test pass** (`STROBE_LIVE_*`, `STROBE_CAL_*`, `BURST_CHARGE_MAX_MC`, the ceiling,
   the staircase): a limit that blocks a test is a finding to report. Known gap: a zero-current
   firing counts as "within limits", so with an open load the staircase climbs — the 6c Step 3
   "nothing by 30 %" stop rule covers it until the firmware fix (PROGRESS §9).

---

## 4. Build, flash, talk to the board

**The toolchain is private to the VS Code Pico extension and is NOT on PATH.** A bare
`cmake --build` fails with "command not found".

```bash
cd Hardware/firmware
export PATH="$USERPROFILE/.pico-sdk/cmake/v4.3.4/bin:$USERPROFILE/.pico-sdk/ninja/v1.13.2:$USERPROFILE/.pico-sdk/toolchain/15_2_Rel1/bin:$PATH"
cmake --build build
arm-none-eabi-size build/pitrac.elf
```

`build/` is already configured. A fresh configure needs
`-DPICO_BOARD=pitrac_ltb_v1 -DPICO_PLATFORM=rp2350`.

The same in **PowerShell 5.1** (the primary shell; no `&&`):

```powershell
cd Hardware\firmware
$env:PATH = "$env:USERPROFILE\.pico-sdk\ninja\v1.13.2;$env:USERPROFILE\.pico-sdk\toolchain\15_2_Rel1\bin;$env:PATH"
& "$env:USERPROFILE\.pico-sdk\cmake\v4.3.4\bin\cmake.exe" --build build
arm-none-eabi-size build\pitrac.elf
```

**Host tests** (native, MSVC, separate `tests/build`; 78 cases as of 2026-10-07). Run them
after any change to `power_fsm.c`, `strobe_plan.c`, `strobe_live.c` or `service.c`:

```powershell
cd Hardware\firmware
& "$env:USERPROFILE\.pico-sdk\cmake\v4.3.4\bin\cmake.exe" --build tests\build --config Release
cd tests\build; & "$env:USERPROFILE\.pico-sdk\cmake\v4.3.4\bin\ctest.exe" -C Release
```

**The build stamp is the image's identity.** `id` prints `built : <__DATE__ __TIME__>` of the
last `cli.c` compile, so **any rebuild that recompiles `cli.c` changes it**, and the docs then
name the wrong image. After a firmware change: build, read the stamp from the binary (regex
`[A-Z][a-z]{2} [ 0-9]\d \d{4} \d\d:\d\d:\d\d` over `build/pitrac.bin`), confirm **Release** with
`%USERPROFILE%\.pico-sdk\picotool\2.3.0\picotool\picotool.exe info -a build\pitrac.uf2`, take
the UF2's SHA256, and write all three into `PROGRESS.md` §0/§6/§10 and the bench doc's status
box. Don't rebuild a handed-over image just to check it: a doc-only change needs no rebuild.

**Doc links:** `python tools/check_doc_links.py` checks every relative link and `#anchor` in the
firmware docs (exit 1 on a break). Run it after moving or renaming sections.

**CMake Tools:** use the private CMake/Ninja/ARM tools, the existing `build/` directory,
and the **Release** variant for the established bench build. `[Unspecified]` lets the
Pico SDK select its ARM toolchain; do not choose a desktop Visual Studio kit. Verify
`CMAKE_BUILD_TYPE` in `build/CMakeCache.txt`: the extension can default to **Debug**.
Selecting Release may only reconfigure; **CMake: Build** must then finish. Check the UF2
timestamp and `picotool info -a` build attributes as well as the cache before approving flash.

**Flash:** type `bootsel` at the board's CLI (it disarms the watchdog itself), or hold SW1 and tap
SW2. Drag `build/pitrac.uf2` onto the `RPI-RP2` drive.

**Serial:** USB CDC on J6. The port depends on the board and the PC: board 3 has been
**COM11**; board 1 has been **COM5** (2026-10-06 onward). Logs in `captures/` are named
`COM<n>_<date>...txt`, so the COM number also tells you which board a log came from. `help` lists every command, and also
flags any command in the dispatch table that its own text fails to document.

**Save serial data to a file, not by copying scrollback.** Microsoft Serial Monitor 0.13.1
defaults to **9000 lines**, less than a normal ADC capture. With monitoring stopped, set
**Log File Directory** to `captures/`, enable **Toggle File Logging**, then start monitoring.
After the capture ends, stop and use **Open the last used log file**. Verify with a short
`id` log first. Logging only saves subsequent output; increasing scrollback cannot recover
discarded lines. **Logging Verbosity** controls extension diagnostics, not serial capture.
Stop monitoring before giving COM11 to the host scope tool.

**Host tool:** `tools/scope.py` (needs `pyserial`, `matplotlib`). Block capture, `--roll` live
view, and `--trig` hardware-triggered single shot. Auto-named output goes to `captures/`
(gitignored). `plt.show()` blocks until the plot window is closed — that is not a hang.

---

## 5. How the owner works — follow these

These came from direct feedback. They are not style preferences; each one exists because the
alternative cost time.

1. 🔴 **Say "reflash first" at the TOP of any instructions that follow a firmware change**, and say
   what is in the pending build. Running a 6-minute procedure on stale firmware and discovering it
   afterwards is the failure this prevents.
2. **Bench procedures are step-by-step, and every step states the board state**: rail up/down, beam
   on/off, carrier, duty, HPF TRACK/HOLD, `adcmode`, target geometry. Then the exact serial
   commands, then what the output should look like, then the pass criterion.
3. **Quantitative, in physical units.** Microseconds, millivolts, picoamps — not "looks fine". The
   owner sends logic-analyser CSVs and plot screenshots and expects real analysis of them.
4. **When a claim turns out wrong, retract it plainly and record the error in `PROGRESS.md`.** The
   error and why it happened is often the most reusable line in the log. The owner will catch
   mistakes (a wrong probe pad, an instrument-bandwidth comparison) — check the objection
   properly rather than defending the original.
5. **After any change, audit both the docs AND the firmware's printed output and comments for
   staleness.** A CLI message that asserts an old number, or a doc step superseded by a warning box
   above it, has caused real bench errors here more than once.
6. **The owner commits.** Do not commit unless asked. Leave the tree ready and say so.
7. **One carrier frequency for every board and user**: 104166.67 Hz. `scan carrier` is a
   *verification* that nothing folds in, never a way to pick a frequency.
8. **Safety-relevant firmware gets an independent review before it is handed over** — a second,
   fresh read of the change (another model, or a reviewer with no stake in it) aimed at real
   defects, plus host tests for every fix it finds, and a deliberate mutation per fix to prove
   the test catches it. Both 6c defects (a double conduction raising the staircase; an
   interrupt-dependent baseline split) were found this way on 2026-10-07, not by the author.
9. **Read the owner's serial logs before assuming board state.** `captures/COM<n>_*.txt` are
   the Serial Monitor file logs. They have answered "which image is flashed?" when no `id` was
   sent: printed wording differs between builds.

---

## 6. Doc discipline — what goes where

| Doc | Holds | Rule |
|---|---|---|
| `PROGRESS.md` | live status (§0, §10 top block), settled facts, rules, decisions, recent log, backlog | **the record, with `PROGRESS_ARCHIVE.md`.** Append; mark superseded entries rather than deleting them. When it grows unwieldy, **move** closed history to the archive **verbatim**, under the same § number, and leave a pointer — never rewrite it (first done 2026-10-05) |
| `PROGRESS_ARCHIVE.md` | closed history moved out of PROGRESS.md | verbatim only; nothing in it is current status |
| `BRINGUP_NEW_BOARD.md` | the ordered per-board procedure and the 3-board sign-off table | **the driver** for a new board; links to the `BENCH_*.md` procedure rather than copying it |
| `BENCH_*.md` | how to run each phase once | procedures carry a board-state table and pass criteria. **Dated results go in PROGRESS**, not here — a bench doc keeps only a short status box linking to them |
| `NEXT_BOARD_REV.md` | hardware change requests (CR-xx) | a CR raised on data states what would close it |
| `START_HERE.md` | first-time toolchain and flash walkthrough | not a status doc beyond one line |

**Keep one-off design validation separate from per-board steps** (`PROGRESS.md` §0.5). Before
calling a step per-board, ask: *what would have to change for this answer to change?* If only a
design respin would change it, it is done once.

---

## 7. Tooling traps on this machine

| Trap | What to do |
|---|---|
| **Shell heredocs and `-c` strings eat backticks and backslashes.** Markdown and C string patches arrive corrupted. | Write the patch as a Python script with the file-writing tool, and build `` ` `` and `\` with `chr(96)` / `chr(92)`. |
| **Files mix CRLF and LF.** An exact-match replace silently finds nothing. | Detect the file's EOL, convert the search string to it, and **assert exactly one match** before replacing. |
| **Encoding damage recurs.** Double-encoded em-dashes (`â€"`) and BOMs appeared in 13 source files and printed garbage to the serial console. A BOM before a shebang breaks the script. | Sweep `src/` and `tools/` for non-ASCII after edits. Source should be ASCII apart from deliberate emoji in comments. |
| **The shell's working directory can reset between tool calls.** | Use absolute paths, or `cd` in the same command. |
| **CMake Tools can retain an `EEXIST` failure creating `build/.cmake/api`, even when it is a normal directory.** | Inspect the path first. On 2026-09-18, selecting a kit and temporarily moving the generated metadata did not recover it; **Developer: Reload Window** did. No build-directory deletion was needed. Recheck the **Release** variant afterwards. |
| **Logic-analyser CSVs are 0.6–1.5 GB.** | Stream line-by-line and block-reduce; never load whole. Only numpy is installed — **no pandas**. |
| **Python 3.14 / numpy 2.x**: `ndarray.ptp()` is gone. | Use `np.ptp(arr)`. |
| **Primary shell is PowerShell 5.1**; bash (Git Bash) is also available. | Don't mix their syntax in one command. No `&&`/`||` in PowerShell 5.1: use `;` and `if ($?) { ... }`. |
| **Piping a Python script into `python -` from PowerShell mangles non-ASCII** (emoji, `—`, `µ`, `§`): the match string no longer matches, or the file gets `?` written into it. | Keep piped scripts' search strings ASCII-only, or write the script to a file first; for edits around emoji, use a plain editor/edit tool. Assert one match before replacing. |
| **.NET file calls in PowerShell (`[IO.File]::ReadAllText("rel\path")`) resolve relative paths against the process directory, not the PowerShell location.** | Always pass absolute paths to `[IO.File]` methods. |
| **Host tests build with MSVC `/W4 /WX`**: a runtime check on a compile-time constant (`CHECK(CONST_A <= CONST_B)`) is warning C4127 → error. | Use file-scope `_Static_assert` for relations between `board.h` constants; keep `CHECK` for computed values. |
| **KiCad 10 `.kicad_pcb` syntax**: a pad's net is `(net "GND")` with no net number (older parsers expect `(net 12 "GND")`); pad `(at x y rot)` is relative to the footprint and must be rotated by the footprint's angle (positive = counter-clockwise on screen, y down). The JLC CPL CSV has **no test points** (TP1–TP6). | Parse footprints between `\n\t(footprint "` markers; read positions from the `.kicad_pcb`, not the CPL. Verify a pad position against one known part before trusting the rest. |
| **The view tool / an editor can show stale content right after a write.** | Verify an edit with a search, not by re-reading the view. |

---

## 8. Measurement lessons that were expensive

Every one of these produced a confident wrong answer at least once.

**Settling**
- 🔴 **The gated HPF has τ ≈ 0.66 s (measured ~0.75 s).** Any measurement taken shortly after a
  beam step or an HPF mode change measures the HPF recovering, not the thing you wanted. This bug
  shipped **twice** — `hpf test` with 200 ms, `scan carrier` with 50 ms. Settle **≥ 5 s (7.6 τ)**.
- **Signature of a settling artifact: a "noise" figure that tracks the signal at a fixed ratio.**
  Real additive noise does not know how big the signal is.
- **First-row bias:** if one row of a sweep differs wildly, check whether it alone was preceded by
  a settled state.
- **A monotonic trend across the rows of a sweep is usually time, not the swept parameter.**
  `scan carrier` chops the beam, the LED cools, and its output climbs row by row.

**Instruments and saturation**
- 🔴 **A saturated stage reports a SMALL number, which reads as a pass.** U12B is single-supply with
  its gain taken to ground, so its quiescent output is the bottom of its range (~20 mV headroom).
  **Step a stimulus in both directions.**
- **Unity-gain, frequency-flat correlation between two different nets is a shared-ground artifact**,
  not circuit coupling. The logic analyser's common mode has done this repeatedly.
- **Never compare ringing amplitude across instruments of different bandwidth.**
- **Check an instrument's own noise floor versus frequency before believing a null result.**
- **A number the circuit forbids is an analysis bug.** An "875 Hz ring" behind a 2.41 kHz high-pass
  was an FFT measuring the decay envelope. Use zero-crossing rate for ring frequency.

**Probing (the strobe-area lessons, 2026-10-07/08)**
- 🔴 **Where the probe ground goes decides what you see.** Ground on TP1 — 72 mm from TP3 with the
  36 V boost (U1/L1/D2) between them — put ±70–85 mV boost bursts on a node whose real ripple is
  < 7 mV p-p. Ground on a local GND pad (R66's, the low side of the sense resistors) with soldered
  leads. TP1 and TP5 are not probe grounds in the strobe area.
- **At full bandwidth the probe's ground lead rings at ~450 MHz** (lead inductance against the
  probe's 3.9 pF) on every boost edge: ±15–40 mV on TP3, ±0.1–0.3 A apparent on TP4. Read DC
  levels and current plateaus with the **20 MHz bandwidth limit**; use full bandwidth only to look
  for fast edges, and compare against a no-signal capture.
- **Decide stability with long-window AC RMS against a reference state, not Pk-Pk.** Pk-Pk on a
  2 ms record is set by whichever rare spike landed in it (108–201 mV for the same node); AC RMS
  was 3.6–3.8 mV at every gate setting. The reference is the state where the suspect cannot be
  active (gate 0: follower idle).
- **A narrow line near 850 MHz on an FFT is probably the phone taking the photo** (cellular
  uplink). Nothing on this board oscillates there.
- **An FFT's span is set by the timebase.** At 1 GS/s the whole sub-MHz loop band is in the first
  bin; to see a loop, take the FFT at a slow timebase (e.g. 25 µs/div: 0–5 MHz, ~5 kHz bins).

**The detection chain**
- **The demodulated response is a trapezoid, not a cosine** (a duty-D pulse against a 50 % square).
  Odd harmonics only; at 25 % duty h3 = 1/9 and the 4th harmonic is a structural null.
- **Compare the chain DELAY between boards, never `phase_ticks`** — ticks carry a duty/period term.

**Interlocks**
- 🔴 **A register at zero is not a node at zero.** The first strobe interlock (2026-10-02) called
  the gate "provably zero" from the PWM compare and pin function alone, while Q9's gate was still
  decaying through a 2.62 ms RC ladder. An independent review caught it before it ran. Any
  interlock on an analog output must account for that output's settling time.
- 🔴 **An interlock that learns from measurements must treat "no signal" as unknown, not as
  safe.** The live staircase raises its limit after any firing judged within limits — including
  one that measured zero current, which is what an open load looks like (found 2026-10-07).
- **Time the measurement window from the instant the data stopped, not from a clock read
  before it.** An interrupt between the two shifts every derived boundary (the 6c baseline split,
  found by review 2026-10-07): read the clock and stop the ADC with interrupts disabled.
- **A beam-step droop is not switch leakage.** Measure leakage with `hpf test`.
- **`+2V5` is literally `+5VA/2`**, so in HOLD a rail step reaches the comparator at **×7.43**
  (Q8 / CR-02). Keep the rail stiff while armed.

---

## 9. Where the bench data is

| What | Where |
|---|---|
| Logic-analyser captures | `C:\Users\ATTAYEKP\Downloads\BeamTiming\<name>\analog.csv` (owner supplies the path) |
| Mic / ADC captures from `scope.py`, and **Serial Monitor logs** (`COM<n>_<date>.txt`) | `Hardware/firmware/captures/` — gitignored |
| Screenshots and scope photos | owner's `Downloads` folder, usually `Photos-1-001 (<n>)\`, supplied per question; they stay there (§6 entries record the folder) |
| Instruments | listed in `PROGRESS.md` §0 "Bench equipment" (MSO54B 500 MHz, TDS 1002, Saleae Logic Pro 8, DMM, PSU, FLIR) |
| Host analysis tools | `tools/scope.py` (captures), `tools/la_phase.py` (LA phase CSVs), `tools/pilot_analysis.py` (one triggered ADC5 pass: peak, FWHM, clipping — §3.7), `tools/netlist_report.py`, `tools/check_doc_links.py` |

---

## 10. Keeping this file honest

Update it when a **convention**, a **trap**, or a **hard-won lesson** changes. Do not put board
status, next steps or measured values here — they go stale within a session and already have a
home in `PROGRESS.md`.
