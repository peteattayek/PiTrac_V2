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
**2026-10-20:** the image version, try-before-you-buy reflashing, the A/B partition table and
what may never be loaded onto a board that has it (§4).

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

The netlist is `Hardware/The_One_Board_To_Rule_Them_All/exports/The_One_Board_To_Rule_Them_All.net`.
**When a doc and the netlist disagree, the netlist is right** — several real bugs came from
hand-written hardware summaries that had drifted. The netlist is the design, though, not the
copper: whether two pads actually connect on the board is a DRC question (§7).

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
3. 🔴 **A Pi 5 on J8 only for Phase 8 and Phase 7c** (`BENCH_P8_PI.md` 8.0–8.5, 8.8, 8.9; 8.10,
   live strobe current with a Pi, approved 2026-11-12 and passed on board 1 with image 1.11
   the same day; and `BENCH_P5_P7_MIC_CAMERA.md` 7c, approved by the owner 2026-11-13,
   `PROGRESS.md` §8). The owner relaxed
   "not before Phase 7c" on 2026-11-09 (`PROGRESS.md` §8). An RP2354 reset is a hard power cut
   to the Pi: SW2 taped over, no `reset force` or `bootsel force`, no SWD reset, and the Pi's
   own USB-C unplugged. Every other phase runs with the Pi off the board unless its procedure
   says otherwise; remove it with PSU and USB off.
4. 🔴 **Never run the +5 V rail from USB.** Firmware enforces it twice; don't design around it.
5. ⚠ **Beam duty: 25 % operating, 35 % is a destruction ceiling, not a setting** (CR-12 thermal:
   junction 123–133 °C at 30 % against 145 °C max). The beam powers up at 2 % by design.
6. ⚠ **Never connect J4 to a camera directly.** The camera's trigger input and strobe output
   are 1.8 V, with no 3.3 V tolerance (CR-09; confirmed by the owner, 2026-11-11). Only through
   a TXS0108E level-shifter module, wired as `BRINGUP_NEW_BOARD.md` Step 13.4: its 1.8 V side
   on the camera, its 3.3 V side on the board. A camera goes on J4 only for
   `BENCH_P5_P7_MIC_CAMERA.md` 7c (owner, 2026-11-13); every other bench phase runs with J4
   empty.
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
   `The_One_Board_To_Rule_Them_All.kicad_pcb` (parse it; §7), and `PROGRESS.md` already
   records several (R61, R64, R65, TP2, TP3, TP4). The 2026-10-07 R65 ground advice ignored a
   gap that was already written down (§6 2026-10-08). The current lead plan, with a
   do-not-solder list, is in `BENCH_P6_STROBE.md` 6c, "Leads to solder for 6c/6d".
9. 🔴 **Live strobe current (6c onward) is the most dangerous thing on this board** — up to ~9 A
   from 36 V through Q9 in linear mode. It happens only in firmware LIVE mode
   (`strobe live on confirm`); the dry default cannot command current whatever is on J3. Run it
   only by `BENCH_P6_STROBE.md` 6c, from its board-state table: FLIR on Q9/HS1, J3 connected or
   disconnected only with PSU **and** USB off, scope on TP4 and TP3. **Never raise a live limit
   to make a test pass** (`STROBE_LIVE_*`, `STROBE_CAL_*`, `BURST_CHARGE_MAX_MC`, the ceiling,
   the staircase, the session cap's `STROBE_SESSION_CAP_A_PER_LEVEL`): a limit that blocks a
   test is a finding to report. Known gap: a zero-current
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

**Host tests** (native, MSVC, separate `tests/build`; 299 cases as of 2026-11-12, image 1.11). Run
them after any change to `power_fsm.c`, `strobe_plan.c`, `strobe_live.c`, `service.c`,
`telemetry.c`, `config_store.c`, `cal_math.c`, `adc_ring_math.h`, `fw_update.c`, `link_frame.c`,
`link_proto.c`, `sha256.c`, `cam_plan.c`, `strobe.c`, `camera.c`, `shot.c`, `cli.c`'s `gpio`,
`reset` and `bootsel` commands, or any `.pio` file. The camera tests run the PIO
programs themselves: the test build calls the SDK's `pioasm` (`%USERPROFILE%\.pico-sdk\tools\2.3.0\pioasm`)
on the same `.pio` sources and runs the words in `tests/pio_sim.c`. Without pioasm the configure
step warns and those tests are not built. `engine_test` also runs the unchanged `strobe.c`,
`camera.c` and `shot.c` against a register model (`tests/sdk_standin/`), with the `gpio` command
copied out of `cli.c` by a Python script at build time: no Python, no `engine_test`. The Pi's link tool has a Python suite, standard library only, any
machine: `python -m unittest discover -s pi/tests -p test_pitrac_link.py` (23 tests). The Pi's
shutdown hook has its own POSIX-shell test, which runs under WSL here (on the Pi: `sh
pi/tests/test_shutdown_hook.sh`):
`MSYS_NO_PATHCONV=1 wsl.exe -e sh /mnt/c/<repo>/Hardware/firmware/pi/tests/test_shutdown_hook.sh`.
Git Bash rewrites the `/mnt/c/...` path without `MSYS_NO_PATHCONV=1`. A newly added test file or case needs a reconfigure
(`cmake tests -B tests\build`) before the build:

```powershell
cd Hardware\firmware
& "$env:USERPROFILE\.pico-sdk\cmake\v4.3.4\bin\cmake.exe" --build tests\build --config Release
cd tests\build; & "$env:USERPROFILE\.pico-sdk\cmake\v4.3.4\bin\ctest.exe" -C Release
```

**The build stamp is the image's identity.** `id` prints `built : <__DATE__ __TIME__>` of the
last `cli.c` compile, so **any rebuild that recompiles `cli.c` changes it**, and the docs then
name the wrong image. The converse bites too (2026-11-10): a rebuild that does **not** recompile
`cli.c` keeps the old stamp on a changed image, so touch `cli.c` before the build you hand over.
After a firmware change: build, read the stamp from the binary (regex
`[A-Z][a-z]{2} [ 0-9]\d \d{4} \d\d:\d\d:\d\d` over `build/pitrac.bin`), confirm **Release** with
`%USERPROFILE%\.pico-sdk\picotool\2.3.0\picotool\picotool.exe info -a build\pitrac.uf2`, take
the UF2's SHA256, and write all three into `PROGRESS.md` §0/§6/§10 and the bench doc's status
box. Don't rebuild a handed-over image just to check it: a doc-only change needs no rebuild.

**The image also has a version** (major.minor, from 1.1 on): `PITRAC_FW_VERSION_*` in
`CMakeLists.txt`, written into the image's IMAGE_DEF. `id`, `upd` and `picotool info` show it;
the boot ROM compares it between the two A/B partitions. **Bump the minor for every image that
leaves this PC.** An image no newer than the running one still installs, but buying it erases
the other partition's image whenever the old one would otherwise win the next normal boot: always for an older version, and for an equal one when the old image is in A (equal versions boot A). Then there is no fallback.
`CMakeLists.txt` says **1.11** (2026-11-12). The minor is a number: 1.10 and 1.11 are above
1.9.
- 1.11 is 1.10 with V110-F1 fixed. `reset` and `bootsel` check again after their 50 ms
  output wait, immediately before the reboot, and disarm the watchdog only then. They
  refuse while the board is POWERING_ON. 299/299 host tests, 30/30 mutations.
  **Independent confirm review PASS** for the sampled guard rules and prescribed workflow
  (`PROGRESS.md` §10); V110-F1 closed, V111-N1/N2 nonblocking wording notes. Physical
  reset/BOOTSEL and Pi/strobe acceptance still run by §8.10, with its Pi-off reset state.
- 1.10 is 1.9 with V19-F1 fixed. Plain `reset`, `bootsel` and `cfg save` count a Pi's 3V3
  sensed now as a Pi, through `power_pi_maybe_present()`; so do the telemetry save and an
  update's install, which sit behind `cfg save`'s guard. The FSM itself is unchanged.
  298/298 host tests, 25/25 mutations. **Confirm review NOT PASSED (V110-F1):** a Pi
  can raise 3V3 during the reset commands' 50 ms wait after that check. Keep 1.10 off
  every board; full V19-F1 closure remains incomplete (`PROGRESS.md` §10).
- 1.9 is 1.8 with the strobe's Pi observed fresh (V18-F1): the 3V3 sensed now and the
  power FSM's classification together, UNSETTLED when they disagree, which neither arms
  live mode nor lets a session run on. Its new `strobe_fsm_test` links the real
  `power_fsm.c`, `service.c` and `strobe.c`. **V18-F1/N1 confirmed closed, but overall
  review NOT PASSED (V19-F1):** pre-existing plain reset/bootsel and config-save guards
  still trust cached Pi presence. Keep 1.9 off every board (`PROGRESS.md` §10).
- 1.8 is 1.7 with the strobe allowed to fire with a Pi powered. Live mode then arms no
  watchdog (owner, 2026-11-12), and a live session ends if a Pi appears or goes away. It
  also words `level` as an aiming aid (V17-N1). **Independent review NOT PASSED (V18-F1):**
  cached Pi presence defeats the intended session/watchdog rule. Keep it off every board
  (`PROGRESS.md` §10). §8.10 is owner-approved but blocked until a corrected candidate
  passes independent review.
- 1.7 is 1.6 without 1.6's all-phase `level`, back to 1.5's current-phase reading, and with
  1.6 review's comment note.
- It keeps the `cal demod` floor, the saved threshold restored at boot, and `cfg save` naming
  its fix.
- It is Board 4's next image; Board 4 shipped on 1.5.
- **Its independent review passed** (2026-11-12; `PROGRESS.md` §10), with nonblocking
  wording note V17-N1: use `level` relatively and the sweep for final acceptance.
  This software review does not record a completed flash or hardware acceptance.
- 1.6 failed its review (V16-F1) and never left this PC. 1.5 (2026-11-10) is Phase 7 after its
  review's fixes; 1.2, 1.3 and the rejected 1.4 came the same day.
- `build/` and `build_hang/` still hold r5 (1.1) for the other boards' §8.0b and §8.7: do not
  rebuild them.
- Later images are built apart, in `build_v<version>/`: `build_v1.2/`, `build_hang_v1.2/`,
  `build_v1.3/`, `build_v1.5/`, `build_v1.6/` (rejected), `build_v1.7/`, `build_v1.8/` (rejected),
  `build_v1.9/` (rejected), `build_v1.10/` (rejected) and `build_v1.11/`.
  They are
  gitignored (`PROGRESS.md` §6).
A build folder reused for unreviewed work no longer holds what was reviewed: give the next image
a new version and folder rather than rebuilding a reviewed one (`build_v1.4/` was removed for
this, 2026-11-10). Configure one with
`cmake -B build_v<x.y> -G Ninja -DPICO_BOARD=pitrac_ltb_v1 -DPICO_PLATFORM=rp2350-arm-s -DCMAKE_BUILD_TYPE=Release`.

**Doc links:** `python tools/check_doc_links.py` checks every relative link and `#anchor` in the
firmware docs (exit 1 on a break). Run it after moving or renaming sections.

**CMake Tools:** use the private CMake/Ninja/ARM tools, the existing `build/` directory,
and the **Release** variant for the established bench build. `[Unspecified]` lets the
Pico SDK select its ARM toolchain; do not choose a desktop Visual Studio kit. Verify
`CMAKE_BUILD_TYPE` in `build/CMakeCache.txt`: the extension can default to **Debug**.
Selecting Release may only reconfigure; **CMake: Build** must then finish. Check the UF2
timestamp and `picotool info -a` build attributes as well as the cache before approving flash.

**Flash:** type `bootsel` at the board's CLI (it disarms the watchdog itself), or hold SW1 and tap
SW2. Drag `build/pitrac.uf2` onto the `RP2350` drive (the datasheet's name for it, §5.5; these
docs said `RPI-RP2`, the RP2040's, until 2026-10-20). From the 2026-10-19 image on, **no USB
path can reset the board**: `picotool … -f` and a 1200-baud open do nothing to a running board,
and `picotool info`/`load` need it in BOOTSEL first.

**If the PC blocks writes to the `RP2350` drive** (a policy restriction on the owner's PC, found
2026-11-11), load with picotool instead. It uses the PICOBOOT interface, not the drive:
- `picotool load -p <n> -v -x <file.uf2>`, where `<n>` is the partition that is **not**
  running, from `upd`'s `layout` line: A = 0, B = 1. The first image after the table goes
  into 1.
- `-x` makes the reboot a flash-update boot, which is what lets a try-before-you-buy image run
  on trial.
- On Board 4, `picotool load` followed by `reboot -u` left the image `tbyb: not bought`, and
  it never ran.
- Confirmed on Board 4, 2026-11-11: r5 into B with `-p 1 -v -x` → `ON TRIAL: bought itself`.

**From image 1.1 (2026-10-20) the image is try-before-you-buy and the flash can be split A/B:**
- **Every reflash is a trial.** The boot ROM runs a just-loaded image on a flash-update boot;
  the image buys itself at the end of its init (`upd` shows `ON TRIAL: bought itself`). One that
  never gets there is not run again: the previous image comes back by itself within ~17 s.
- **Once per board, the partition table** (`build/pitrac_pt.uf2`, from
  `partition_table.json`), by `BENCH_P8_PI.md` §8.7. After it, each drag goes into the
  partition that is not running, so reflashes alternate A and B. The first one after the table
  goes into B: with nothing in B the boot ROM would boot A. The config sectors sit outside
  both and are never touched.
- 🔴 **Never load an image older than 1.1 onto a board with the table.** It does not know about
  the partitions, bus-faults at its first config read, and its boot erases the good image.
  Recovery: BOOTSEL and any 1.1+ UF2.
- **SWD (`tools/flash_swd.sh`) refuses by default:** it writes at flash offset 0, over the
  table, and a try-before-you-buy image never starts after an SWD reset.
- **From a Pi on the board:** `pitrac-link update` (`pi/README.md`; `BENCH_P8_PI.md` §8.9). The
  board's one reset comes after it has shut the Pi down.
- **The update note and the telemetry mirror are a contract between images.** They sit in
  no-init RAM: the image that installs an update writes them, and the image it installs reads
  them at *its* addresses. Moved silently, an update into the new image is not reported and the
  Pi is not powered back on. So the build stops if either moves or changes size
  (`tools/check_retained.cmake`, `PITRAC_RETAINED` in `CMakeLists.txt`); the script says what to
  do when a move is deliberate.

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
🔴 **The file log can reorder output** (2026-10-12): whole chunks of a `strobe` status landed
after the prompt, with nothing lost, while the on-screen scrollback had the true order. A USB
CDC stream cannot reorder, so this is the logger. Where order is the evidence (a per-firing
readback, a `strobe wave` CSV), the **scrollback** is the record: raise its line limit above the
output's length, then copy it into `captures/`. Keep the file log as a backup.
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
| **The netlist says what should connect, not what the copper does.** J8.17 is on +3V3_Pi5 in the netlist but has no copper; a bench step built on the netlist failed (2026-10-30, `NEXT_BOARD_REV.md` CR-27). | Before telling the owner two pads connect, run `& "C:/Program Files/KiCad/10.0/bin/kicad-cli.exe" pcb drc --format json --severity-all -o <scratchpad>/drc.json <the .kicad_pcb>` (read-only) and check `unconnected_items`; on these boards J8.17 is the only one. The fab gerbers name each copper object's net (`%TO.N,<net>*%`). |
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
- 🔴 **A detection threshold set from the record's own peak lets a spike hide the signal.**
  - The live readback counts samples above half the window's peak.
  - At gate 24 % (2026-10-13), a 3.3 A turn-on spike put that line at ~1.5 A. The 0.72 A,
    20 µs pulse under it was printed as "no current", and the staircase raised its limit.
  - Detect against an absolute floor, and treat a peak far above the settled level as a
    finding of its own.
- 🔴 **Measure the rate you think you configured.** Every ADC ring mode ran at *half* its stated
  rate from the day the ring was written until 2026-10-12. DIV = 95 for "500 ksps" lands each
  trigger as the previous 96-cycle conversion ends, and the ADC ignores it (datasheet 12.4.3.2:
  n ≥ 96, or DIV = 0 for back to back).
  - Nothing checked the real rate. Every ring timestamp used the configured one.
  - It surfaced as an unexplained "late ring start" and cost a day of review rounds aimed at the
    wrong mechanism, until `adc ringtest` printed samples against time.
  - When a measured count disagrees with time × rate, suspect the rate first.
- 🔴 **A tolerance in a safety check must be derived or measured, never guessed.**
  - The 2026-10-12 ADC0 coverage check failed independent review three times:
    - a ±4-sample allowance hid a 6 µs gap;
    - an "exact" ±60-cycle check assumed its own snapshot latency;
    - and that check ran from flash, where a cache-miss stall could cancel one missing sample.
  - Each fix only narrowed the assumption. What closed it (round 6) was resting the proof on
    a hardware guarantee instead of timing: the datasheet says a lost ADC conversion sets the
    sticky FCS.OVER, so "no sample missing" is read from flags. The SRAM-bracketed timing
    check stays as a cross-check that can only reject.
  - Ask what the check would accept in the worst case, and make the code prove that rather
    than assume it.
- **Time the measurement window from the instant the data stopped, not from a clock read
  before it.** An interrupt between the two shifts every derived boundary (the 6c baseline split,
  found by review 2026-10-07): read the clock and stop the ADC with interrupts disabled.
- **A beam-step droop is not switch leakage.** Measure leakage with `hpf test`.
- **`+2V5` is literally `+5VA/2`**, so in HOLD a rail step reaches the comparator at **×7.43**
  (Q8 / CR-02). Keep the rail stiff while armed.

**Assembly**
- 🔴 **A rail can be right at its capacitor and absent at the connector.** On board 1
  (2026-10-13) both J3 pins were cold joints.
  - C26 read 36 V; the J3 screw read 7 V.
  - Two full 6c staircases fired into an open LED bank, and every firing read "no current".
  - This board's pours connect to pads solid (no thermal relief), so treat every hand-soldered
    joint on one as suspect.
  - Measure a supply where the load connects (the screw, not the pad), and inspect pour joints
    before power (`BRINGUP_NEW_BOARD.md` §1).
  - Zero current from a stage that should conduct is an open circuit until proven otherwise.

---

## 9. Where the bench data is

| What | Where |
|---|---|
| Logic-analyser captures | `C:\Users\ATTAYEKP\Downloads\BeamTiming\<name>\analog.csv` (owner supplies the path) |
| Mic / ADC captures from `scope.py`, and **Serial Monitor logs** (`COM<n>_<date>.txt`) | `Hardware/firmware/captures/` — gitignored |
| Screenshots and scope photos | owner's `Downloads` folder, usually `Photos-1-001 (<n>)\`, supplied per question; they stay there (§6 entries record the folder) |
| Instruments | listed in `PROGRESS.md` §0 "Bench equipment" (MSO54B 500 MHz, TDS 1002, Saleae Logic Pro 8, DMM, PSU, FLIR) |
| Host analysis tools | `tools/scope.py` (captures), `tools/la_phase.py` (LA phase CSVs), `tools/pilot_analysis.py` (one triggered ADC5 pass: peak, FWHM, clipping — §3.7), `tools/netlist_report.py`, `tools/check_doc_links.py`, `tools/p8_cycles.ps1` (BENCH_P8_PI.md 8.5: the 20 Pi boot/shutdown cycles over the CLI) |

---

## 10. Keeping this file honest

Update it when a **convention**, a **trap**, or a **hard-won lesson** changes. Do not put board
status, next steps or measured values here — they go stale within a session and already have a
home in `PROGRESS.md`.
