# Handoff — PiTrac V2 firmware bring-up

**For a person or model taking over this work with no prior context.** Written 2026-09-17.
The last bench session was 2026-08-31.

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

| # | File | Why |
|---|---|---|
| 1 | **this file** | conventions and traps |
| 2 | `PROGRESS.md` **§0** | one-table status: boards, firmware, what is done |
| 3 | `PROGRESS.md` **§10** (top block) | **the live resume point** and the open-items table |
| 4 | `PROGRESS.md` **§0.5** | what is SETTLED (never re-derive) vs what REPEATS per board |
| 5 | `PROGRESS.md` **§1** | hard safety rules |
| 6 | the `BENCH_*.md` for the phase in hand | the step-by-step procedure |
| 7 | `BRINGUP_NEW_BOARD.md` | only if a **new board** is being brought up — it is the driver for that |

Reference, as needed: `PROGRESS.md` §2 (verified hardware facts), §6 (measurement log), §8
(decisions — **do not re-litigate these**), `NEXT_BOARD_REV.md` (hardware change requests),
`ARCHITECTURE.md` (what runs on PIO/PWM/DMA vs the CPU), and `HARDWARE_REFERENCE.md` **at the repo
root** (generated from the KiCad netlist by `tools/netlist_report.py`).

The netlist is `Hardware/The_Second_Board_To_Rule_Them_All/The_Second_Board_To_Rule_Them_All.net`.
**When a doc and the netlist disagree, the netlist is right** — several real bugs came from
hand-written hardware summaries that had drifted.

---

## 3. Safety — non-negotiable

From `PROGRESS.md` §1, plus two rules learned the hard way. None of these is relaxed without an
explicit decision from the owner.

1. 🔴 **Nothing conductive near D12.** Its cathode sits at **36 V** (VIR through R77); its anode is
   the TIA summing node. Foil laid across it destroyed **U11B on board 1** (2026-08-17, §11).
   Board 1 is out of service. When suggesting an optical block or baffle, say this explicitly.
2. 🔴 **`PULSE_LIMIT_DISABLE` (GPIO27) stays 0.** Written in exactly one place, `safe_state()`. No
   CLI path, no config flag. It defeats the strobe's hardware pulse-width watchdog.
3. 🔴 **No Pi 5 on J8 until Phase 7c.** An RP2354 reset is a hard power cut to the Pi.
4. 🔴 **Never run the +5 V rail from USB.** Firmware enforces it twice; don't design around it.
5. ⚠ **Beam duty: 25 % operating, 35 % is a destruction ceiling, not a setting** (CR-12 thermal:
   junction 123–133 °C at 30 % against 145 °C max). The beam powers up at 2 % by design.
6. ⚠ **Do not connect J4 (camera)** — the Mira220 I/O is 1.8 V with no 3.3 V tolerance (CR-09).

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

**Flash:** type `bootsel` at the board's CLI (it disarms the watchdog itself), or hold SW1 and tap
SW2. Drag `build/pitrac.uf2` onto the `RPI-RP2` drive.

**Serial:** USB CDC on J6. The owner's port is **COM11**. `help` lists every command, and also
flags any command in the dispatch table that its own text fails to document.

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

---

## 6. Doc discipline — what goes where

| Doc | Holds | Rule |
|---|---|---|
| `PROGRESS.md` | everything done, every decision, every measurement, every error | **the record.** Append; mark superseded entries rather than deleting them |
| `BRINGUP_NEW_BOARD.md` | the ordered per-board procedure and the 3-board sign-off table | **the driver** for a new board |
| `BENCH_*.md` | how to run each phase once | procedures carry a board-state table and pass criteria |
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
| **Logic-analyser CSVs are 0.6–1.5 GB.** | Stream line-by-line and block-reduce; never load whole. Only numpy is installed — **no pandas**. |
| **Python 3.14 / numpy 2.x**: `ndarray.ptp()` is gone. | Use `np.ptp(arr)`. |
| **Primary shell is PowerShell 5.1**; bash (Git Bash) is also available. | Don't mix their syntax in one command. |

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

**The detection chain**
- **The demodulated response is a trapezoid, not a cosine** (a duty-D pulse against a 50 % square).
  Odd harmonics only; at 25 % duty h3 = 1/9 and the 4th harmonic is a structural null.
- **Compare the chain DELAY between boards, never `phase_ticks`** — ticks carry a duty/period term.
- **A beam-step droop is not switch leakage.** Measure leakage with `hpf test`.
- **`+2V5` is literally `+5VA/2`**, so in HOLD a rail step reaches the comparator at **×7.43**
  (Q8 / CR-02). Keep the rail stiff while armed.

---

## 9. Where the bench data is

| What | Where |
|---|---|
| Logic-analyser captures | `C:\Users\ATTAYEKP\Downloads\BeamTiming\<name>\analog.csv` (owner supplies the path) |
| Mic / ADC captures from `scope.py` | `Hardware/firmware/captures/` — gitignored |
| Screenshots | owner's `Downloads` folder, supplied per question |

---

## 10. Keeping this file honest

Update it when a **convention**, a **trap**, or a **hard-won lesson** changes. Do not put board
status, next steps or measured values here — they go stale within a session and already have a
home in `PROGRESS.md`.
