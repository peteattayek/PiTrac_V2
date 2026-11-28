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
   gap that was already written down (§6 2026-10-08). **The test points are fine from the
   back** (2026-11-23):
   - TP1–TP10 are through-hole, and on the back no other-net pad lies within 6 mm;
   - every via is tented. The nearest are mask-covered GND vias: TP8 0.45 mm, TP3 0.62 mm,
     TP10 1.24 mm;
   - so read them from the back with a hand-held tip (`BRINGUP_NEW_BOARD.md` §2.1). The current lead plan, with a
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
10. 🔴 **Image 1.23 lets the Pi run every CLI command** over the link (`pitrac-link cli`,
    the owner's decision of 2026-11-24, `PROGRESS.md` §8). 1.21 failed independent
    confirm review (V121-F1–F4, 2026-11-27); 1.22 also failed (V122-F1). 1.23 and its
    matching Pi client passed software review; §8.11 is the separate owner-run hardware check. 1.20 (V120-F1–F4) and 1.19
    (V119-F1–F3), both 2026-11-27, 1.18
    (V118-F1, F2), 1.17 (V117-F1–F3), 1.16 (V116-F1–F4) and 1.15 (V115-F1–F7), all 2026-11-26,
    1.14 (V114-F1–F8, 2026-11-25) and 1.13 (V113-F1–F6) failed theirs and never left this PC.
    - **The firmware's own guards apply to it unchanged.**
    - **Keep beam duty at or below 25 %.** The owner requires firmware enforcement on USB and
      the Pi. From 1.16 it is on the PWM pin's high time, and from 1.17 the carrier stays within
      1–200 kHz and no change made while the beam runs stretches a pulse on the pin. **It binds
      the pin, not the LED** (the owner, 2026-11-26): the LED adds the drive chain's stretch,
      which `BRINGUP_NEW_BOARD.md` Step 6.3b measures per board on the MSO54B, at the carrier,
      150 and 200 kHz, with its uncertainty (V117-F1, V118-F2, V119-F3). 1.14 and 1.15 bounded the LED
      with an assumed U9 width (V114-F3, V115-F2), and 1.16 claimed the pin's bound for the LED
      at any frequency (V116-F1). Keep the procedural limit on released firmware.
    - **The rules above that are procedure, not firmware, bind Pi software too:**
      - the 30 % open-load stop on a staircase (rule 9's gap);
      - eyes away from the LED bank;
      - the Phase 6c board state for live current.

      A Pi tool that drives the strobe carries them in its own code, as
      `tools/strobe_focus.ps1` does on the PC.
    - **From the Pi, what would cut the Pi's power needs a trailing `confirm`:** `forceoff`,
      `reset force`, `bootsel force`, `adc5vcal <volts>`, and `fault clear` while a fault holds
      the rail up. Plain `off` runs only in RUNNING, as the orderly shutdown.
    - **Every Pi command shows on the USB console** as `[pi] <command>`.
    - **`pitrac-link`'s exit codes say how a command ended** (`pi/README.md`): it ran; nothing
      ran (cancelled before it ran, or stopped before the line went out); it stopped early; it
      ran to its end despite a stop; output was lost; or the outcome is unknown, with the
      ticket to ask about. Protocol 4 (1.16 on) names the session in every answer. Candidate 1.15
      could report the wrong outcome (V115-F4, F6, F7), and 1.16's `cli-cancel` could on a port
      it could not open (V116-F4), and 1.17's on a Ctrl-C while opening or a failing close
      (V117-F3). A restart loses the board's record of a ticket: then only a log of the USB
      console tells.

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

**Host tests** (native, MSVC, separate `tests/build`; 349 cases as of 2026-11-27, image 1.23). Run
them after any change to `power_fsm.c`, `strobe_plan.c`, `strobe_live.c`, `service.c`,
`telemetry.c`, `config_store.c`, `cal_math.c`, `adc_ring_math.h`, `fw_update.c`, `link_frame.c`,
`link_proto.c`, `link_cli.c`, `pi_link.c`, `sha256.c`, `cam_plan.c`, `strobe.c`, `camera.c`,
`shot.c`, `beam.c`, `cal.c`'s `cal_demod_model()` and `cal_scan_carrier()`, `cli.c`'s line
editor (`cli_service()`), `gpio`, `reset`, `bootsel`, `off`, `fault`, `panel`, `capture`,
`capture trig` and `strobe wave` commands, the Pi power rule or `beam sweep`'s steps,
`tools/scope.py`, or any `.pio` file. `stop_test` (image 1.14, extended in 1.15–1.18) runs
the real `beam.c` and `service.c` with `cli.c`'s line editor, `panel`, the sweep, the captures
and `strobe wave` and their print-out, and `cal.c`'s model fit and carrier scan, copied out by
`tests/extract_functions.py` (functions, and `#NAME` defines, `$NAME` statics and `@NAME`
typedefs). From 1.17 its beam slices run in time and the carrier pin is traced; from 1.18
it also runs `config_store.c`'s `cfg_apply_beam()`. Its
`stop_test console` mode is the board's console end to end, for `tools/tests/test_scope.py`:
`scope.py`'s own reader against it (CTest `scope.reader`, which needs Python; two of its 38
cases need pyserial, and are skipped without it; 44 from 1.18). `pi_link_test`
(1.15) compiles the unchanged `pi_link.c` against `tests/link_standin/`, a clock, UART and DMA
ring in which a byte lands after the write index passes it. The camera tests run the PIO
programs themselves: the test build calls the SDK's `pioasm` (`%USERPROFILE%\.pico-sdk\tools\2.3.0\pioasm`)
on the same `.pio` sources and runs the words in `tests/pio_sim.c`. Without pioasm the configure
step warns and those tests are not built. `engine_test` also runs the unchanged `strobe.c`,
`camera.c` and `shot.c` against a register model (`tests/sdk_standin/`), with the `gpio` command
copied out of `cli.c` by a Python script at build time: no Python, no `engine_test`. The Pi's link tool has a Python suite, standard library only, any
machine: `python -m unittest discover -s pi/tests -p test_pitrac_link.py` (141 tests, 1.23,
about 2 min; the port-lock test, `cli-cancel` run as a process against a held port, a port
that is no tty, a process with no stdout or no stdin, and a write to a real tty that takes
nothing need POSIX, so they run on the Pi and are skipped on Windows). Its `AsAProcess` tests
run the tool as its own process, through `pi/tests/fake_board_process.py`, with OS pipes for
stdout and stderr that are closed or already full, and with thread starts refused, on both;
its `Sweep` tests put a real SIGINT at every line each command runs. Its fake board keeps the
firmware's update state (UPD_STATUS, BEGIN refused while armed), and its port runs the tool's
own bounded write, with a transport that can stall and resume (1.21).
The suite puts a stand-in in for `systemctl poweroff` at import: it never shuts the machine
down. The Pi's
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
**1.23 independent confirm review PASS**, protocol 4, with its matching Pi client.
[Independent verdict](captures/review_2026-11-27_v1.23/INDEPENDENT_REVIEW_v1.23.md): V122-F1 closed. A stop counted during a zero-byte
failed retry is checked before returning; the client attempts the original ticket's
abort. No delivery guarantee. Release `Nov 27 2026 22:52:29`, BIN 256484 B
`20C16DD5…2570A641`, UF2 513024 B `6F5606E7…6A210028`. Not flashed.
[Review brief](captures/review_2026-11-27_v1.23/REVIEW_BRIEF_v1.23.md).
**1.22 independent confirm review NOT PASSED (V122-F1).** [1.22 report](captures/review_2026-11-27_v1.22/INDEPENDENT_REVIEW_v1.22.md).
Release `Nov 27 2026 21:43:38`, BIN 256484 B `AE54FB09…0E77945C`, UF2 513024 B
`AFE6D160…D5B21C16`. No MCU change from 1.21 except stamp and version. The Pi client
distinguishes a stopped retry from a stopped first send, keeps accepted-prefix evidence
through I/O errors, checks the frame deadline before each write attempt, and distinguishes
APPLY accepted from unknown. One actual producer required. **Not flashed.**
1.21 failed independent confirm review ([report](captures/review_2026-11-27_v1.21/INDEPENDENT_REVIEW_v1.21.md)).
The minor is a number: 1.10 to 1.22 are above 1.9.
- **As submitted (claims superseded by V121-F1–F4): 1.21 was 1.20's machine code with only the version changed; the prior review's findings were all
  in `pi/pitrac_link.py`** (`PROGRESS.md` §6, 2026-11-27). The `.bin` differs from 1.20's in 11
  bytes: the minor in the same seven places, and four digits of the build time. An update asks
  the board's update state first and drops only what its own BEGIN started (V120-F1); two
  Ctrl-Cs still send the stop before the tool stops waiting (V120-F2); every frame is written
  in bounded time, a stop seen until the port takes its first byte (V120-F3); a writer thread
  that cannot start leaves its stream unavailable, and the exit stands (V120-F4); output
  progress counts per 4 KB (V120-N1); a Ctrl-C just before a prompt waits ends it (V120-N2).
  Step 6.3b checks that the scope counts every edge (V120-N3). 349/349 host tests; 44/44
  `scope.py`; the Pi tool's 131 (all on the Pi; 125 on the PC, where six need POSIX); 197
  mutations, all caught (192 on the PC, the five POSIX ones on the Pi). Release
  `Nov 27 2026 12:42:37`, `.bin` SHA256 `17F5694B…3621635C`. **Not flashed: awaiting its
  confirm review at submission; subsequently rejected (V121-F1–F4).**
- **1.20 independent confirm review NOT PASSED (V120-F1–F4, N1–N3).** Both artifacts
  reproduced; 349/349 host tests; all 15 differing bytes explained. [Independent review](captures/review_2026-11-27_v1.20/INDEPENDENT_REVIEW_v1.20.md)
- **1.20 was 1.19's machine code with only the version changed; its review's findings were all
  off the board again** (`PROGRESS.md` §6, 2026-11-27). The `.bin` differs from 1.19's in 15
  bytes: the minor in the same seven places, and the build stamp (the day as well as the time,
  in two date strings). `pitrac_link.py` counts Ctrl-C from `main()` on and acts on it only
  while an outcome is pending, with one decision point per exit (V119-F1); a stdout or stderr
  that blocks is given up after 2 s, a command's output never holds up its stop, and after an
  accepted apply the poweroff request comes before any line (V119-F2); no stdin is no answer
  (V119-N1). `BRINGUP_NEW_BOARD.md` Step 6.3b's records (200 µs at 6.25 GS/s, on the MSO54B)
  hold the periods they measure (V119-F3). 349/349 host tests; 44/44 `scope.py`; the Pi tool's
  110 (all on the Pi; 105 on the PC, where five need POSIX); 173 mutations, all caught (168 on
  the PC, the five POSIX ones on the Pi). Release `Nov 27 2026 10:59:33`,
  `.bin` SHA256 `2D0F191A…E9A491B9`. Never flashed.
- **1.19 independent confirm review NOT PASSED (V119-F1–F3, N1).** Both artifacts reproduced;
  349/349 host tests; all 13 differing bytes explained. [Independent review](captures/review_2026-11-26_v1.19/INDEPENDENT_REVIEW_v1.19.md)
- **1.19 was 1.18's machine code with only the version changed; its review's findings were all
  off the board** (`PROGRESS.md` §6, 2026-11-26). The `.bin` differs from 1.18's in 13 bytes:
  the minor in seven places and the build stamp. `pitrac_link.py` keeps an outcome once had,
  whatever stdout, stderr, a Ctrl-C or the close then do (V118-F1). `BRINGUP_NEW_BOARD.md`
  Step 6.3b takes the pin's high time from the period on the same trace, and claims nothing
  between its three settings (V118-F2). `scope.py` words its id's limit (V118-N1). 349/349 host
  tests; the Pi tool's 87/87 on the Pi (83 on the PC, where four need POSIX); mutations:
  all 153 caught, 149 on the PC and the four POSIX-only ones (P7, P8, P31, P36) among the Pi tool's 58 on the Pi. Release `Nov 26 2026 22:28:40`, `.bin` SHA256 `1A18BDEC…03409826`. Never
  flashed.
- **1.18 independent confirm review NOT PASSED (V118-F1, F2).** Both artifacts reproduced;
  349/349 host tests. [Independent review](captures/review_2026-11-26_v1.18/INDEPENDENT_REVIEW_v1.18.md)
- **1.18 was 1.17 with V117-F2/F3 and N1–N3 fixed, and V117-F1 answered in the procedure**
  (`PROGRESS.md` §6, 2026-11-26). `BRINGUP_NEW_BOARD.md` Step 6.3b measures at the carrier
  itself, with its uncertainty. `scope.py` takes a record only after its command's own echo.
  `cli-cancel` is UNKNOWN with the ticket through its whole run. The beam's pin rejoins after
  its dark with no counter wait; a saved carrier comes back as saved. 349/349 host tests; the Pi
  tool's 73/73 on the Pi (70 on the PC, where three need POSIX); mutations: all 130 caught, 126 on
  the PC and the Pi tool's 36 on the Pi. Release
  `Nov 26 2026 18:30:12`, `.bin` SHA256 `B8A5EA64…2A6B059C`. Never flashed.
- **1.17 independent confirm review NOT PASSED (V117-F1–F3).** Both artifacts reproduced;
  348/348 host tests. [Independent review](captures/review_2026-11-26_v1.17/INDEPENDENT_REVIEW_v1.17.md)
- **1.17 is 1.16 with V116-F1–F4 fixed and N1/N2 addressed** (`PROGRESS.md` §6, 2026-11-26):
  the carrier held to 1–200 kHz on every path; the beam's pin held low through any change made
  while it runs, so no pulse is stretched; `capture ... id=<n>`. On the PC and Pi side,
  `scope.py` joins a line a read timeout cut and takes only its own record, and `pitrac-link
  cli-cancel` that cannot open the port is UNKNOWN with the ticket. 348/348 host tests; the Pi
  tool's 70 pass on the PC but for the two POSIX ones; 116 of 116 mutations run on the PC
  caught. P7, P8 and P31 need the Pi, which did not answer this round. Release `Nov 26 2026 17:38:39`,
  `.bin` SHA256 `B4DFE0B6…CBE0A108`. **Failed its confirm review (above); never flashed.**
- **1.16 independent confirm review NOT PASSED (V116-F1–F4).** Both artifacts reproduced;
  345/345 host tests. Keep Board 5 on bought 1.12 in A with r5 in B. Hold §8.11.
  [Independent review](captures/review_2026-11-26/INDEPENDENT_REVIEW_v1.16.md)
- **1.16 is 1.15 with V115-F1–F7 and N1/N2 fixed** (`PROGRESS.md` §6, 2026-11-26): CR LF is one
  Enter at the USB console; a stop on a dump's last line is too late to cut it; the beam's 25 %
  is on the PWM's high time; `cal model` and `scan carrier` put the carrier back register for
  register; link protocol 4 names the session in every CLI answer; a long `capture` is refused
  under the watchdog. On the PC and Pi side, `scope.py` and `pitrac-link`. 345/345 host tests;
  the Pi tool's 66 tests pass on the PC but for the POSIX port lock; 88 of 88 mutations run on
  the PC caught. P7 and P8, the port's two locks, need the Pi, which did not answer this round;
  that code is unchanged since 1.15, where both were caught on the Pi. Release
  `Nov 26 2026 12:11:17`, `.bin` SHA256 `EE5338B0…933E18AB`. **Failed its confirm review
  (above); never flashed.**
- **1.15 independent confirm review NOT PASSED (V115-F1–F7).** Both artifacts reproduced.
  Firmware tests passed 336/336. Keep Board 5 on bought 1.12 in A with r5 in B. Hold §8.11. [Independent review](captures/review_2026-11-25/INDEPENDENT_REVIEW_v1.15.md)
- 1.14 introduced tickets and Pi power-command guards. **Independent review NOT PASSED (V114-F1–F8).** Keep Board 5 on bought 1.12 in A with r5 in B. The submitted binary reproduced and all 323 host tests passed. [Independent review](captures/review_2026-11-24/INDEPENDENT_REVIEW_v1.14.md)
- 1.13 was protocol 2. **Independent review NOT PASSED (V113-F1 through F6); never left this
  PC.**
- 1.12 is 1.11 with text only:
  - the `J4: NO CAMERA (CR-09)` printouts and comments retired after 7c;
  - 7c.1's measured `t_cam` in comments;
  - V111-N1/N2's wording.
  300/300 host tests and 2/2 mutations. Its machine code is identical to 1.11 apart from the
  version constant. **Independent software review PASS with wording notes**
  (`PROGRESS.md` §6, "Image 1.12"). On Board 5 since 2026-11-23 (partition A; r5 in B).
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
  `build_v1.9/` (rejected), `build_v1.10/` (rejected), `build_v1.11/` and `build_v1.12/` (review passed),
  `build_v1.13/` (the Pi's CLI over the link; independent review NOT PASSED, 2026-11-24),
  `build_v1.14/` (protocol 3, independent review NOT PASSED, 2026-11-25), `build_v1.15/`
  (independent confirm review NOT PASSED, 2026-11-26), `build_v1.16/` (protocol 4, independent
  confirm review NOT PASSED, 2026-11-26), `build_v1.17/` (independent confirm review NOT
  PASSED, 2026-11-26), `build_v1.18/` (independent confirm review NOT PASSED, 2026-11-26),
  `build_v1.19/` (independent confirm review NOT PASSED, 2026-11-27), `build_v1.20/`
  (independent confirm review NOT PASSED, 2026-11-27), `build_v1.21/` (submitted for
  independent confirm review and rejected, 2026-11-27), `build_v1.22/` (rejected, V122-F1),
  `build_v1.23/` (software review passed; hardware acceptance pending).
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

### Preparing an image for its independent review (how 1.14–1.23 were done)

The owner pastes the reviewer's verdict; the full report is
`captures/review_<date>_v<X>/INDEPENDENT_REVIEW_v<X>.md`. **`captures/` is gitignored:** the
review folders (briefs, patches, mutation runners and their logs, `old_tool_check.py`) and
`captures/review_tooling/` exist only on this PC. Never delete them; the next round starts
from the last one. Nothing is flashed or committed in a round. A round, for image 1.N after
1.X failed:
1. **Record the verdict:** a `PROGRESS.md` §6 "NOT PASSED" entry (each finding and how it was
   reproduced), §0's row, a §10 top block; `HANDOFF.md`'s version status.
2. **Before any edit, snapshot the code paths:** `python captures/review_tooling/snapshot_round.py
   captures/<1.X's folder>/v1.12_to_v1.X.patch <scratch>/v1X`.
3. **Read every line each finding names**, and the firmware it reaches. Fix the cause, not a
   guard after it, and word each claim at the layer it is tested (§5 rule 10).
4. **Test at the layer the finding was reproduced:** through `main()`, as a process
   (`pi/tests/fake_board_process.py`), at every line (`Sweep`), against a fake board that keeps
   the state the claim touches. Then show the new checks fail on 1.X's tool: the latest
   `old_tool_check.py` is the pattern (1.X's tool is `1522bd7`'s blob + 1.X's full patch).
5. **The image:** `PITRAC_FW_VERSION_MINOR` + 1, with a history line in `CMakeLists.txt`; the
   "first image" strings (`pi/pitrac_link.py`'s "image 1.N on" and firmware-before wording,
   `tools/scope.py` and `tools/tests/test_scope.py`). Build into `build_v1.N/` with
   `$env:SOURCE_DATE_EPOCH` set and the configure line `-G Ninja -DPICO_BOARD=pitrac_ltb_v1
   -DPICO_PLATFORM=rp2350-arm-s -DCMAKE_BUILD_TYPE=Release`. Check the stamp's date is today's
   (the epoch is read as UTC). Rebuild in a clean folder (`%TEMP%`) and compare both files byte
   for byte; `captures/<last>/bindiff.py` against 1.X's `.bin` must show only the minor (seven
   places) and stamp digits. Host tests, `scope.py` tests, the Pi suite with
   `-W error::ResourceWarning`.
6. **Mutations:** copy the last `mutate_v1X.py`; run `captures/review_tooling/check_mutations.py`
   on it first; retarget what no longer applies, retire what the round removed, add one per new
   safeguard, each naming what removing the code actually does. Hash the tree
   (`review_tooling/tree_hash.py`) before and after; run all on the PC (`python -B mutate_v1N.py
   .`, the pyserial wheel on `PYTHONPATH`, in the background: ~5 min). Anything but "caught" is
   a defect in the test or the mutation. Never edit `src/`, `tests/`, `pi/` or `tools/` while
   it runs.
7. **The Pi:** `review_tooling/stage_pi.py`, `scp`, `unpack.sh`, then the review folder's
   `run_on_pi.sh` (the suite, then every P mutation, as the ordinary user `pitrac2`, no sudo).
   Copy `pi_run.txt` and `pi_tests.txt` back, then remove the folder on the Pi.
8. **The patches:** `review_tooling/make_patches.py --prev <1.X's full patch> --snapshot
   <step 2's copy> --out <new review folder> --old 1.X --new 1.N` → `PROBLEMS: none`.
9. **The brief and the docs:** `REVIEW_BRIEF_v1.N.md` (the last brief is the template: findings
   table, identity, patches, evidence, what to check, a hash table of every file). `PROGRESS.md`
   §0/§6 (with an "I was wrong" list)/§10, this file's version status, §3 rule 10 and §5 rule 10,
   `CHANGELOG.md`, `DEVELOPER_GUIDE.md`, `pi/README.md`, `BENCH_P8_PI.md` 8.11's identity and
   `scp` names. `tools/check_doc_links.py`, `review_tooling/eols.py` on every touched file,
   `AGENTS.md` == `CLAUDE.md`.
10. **Report to the owner:** what changed, the evidence, what I got wrong, the owner's open
    decisions, and a paste-ready message for the reviewer (the last one is in `PROGRESS.md` §6's
    submission entry and the brief's header).

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
10. **Word each claim at the layer it was tested, and test at the layer the claim names.**
    Images 1.14 to 1.18 failed five independent reviews in a row (`PROGRESS.md` §6), mostly on
    claims one layer above their tests:
    - a compare register for the LED;
    - one frequency for a range, and a nominal time for a measured one;
    - an id for a transaction;
    - a zero-time model for a real delay;
    - a printing callback for the process's real stdout.

    Before writing "X holds", list every entry point and every layer between the test and X.
    Then test each one: the process entry point with real pipes, every command and not only the
    one reported, the physical quantity where the claim puts it, with its uncertainty. Or word
    the claim at the layer tested, and say what it does not cover. A safeguard no test can
    reach is either given a test or removed.

    Image 1.19 failed a sixth time the same way (V119-F1–F3): a guard applied after the hazard
    leaves a window before it, however small. Two habits from it:
    - **Remove the hazard, then sweep it.** If something asynchronous (a signal, a stall) can
      land between two lines, design so it cannot act there, and test with it at *every* line
      the run executes, not at the lines you thought of (`pi/tests/test_pitrac_link.py`
      `Sweep`: a real SIGINT at each line event).
    - **Read every number in a procedure as the instrument will.** A timebase is also a record
      length: check that what the step asks for fits in what it sets (V119-F3), and nothing
      that must happen waits behind something that can stall (V119-F2).

    Image 1.20 failed a seventh time (V120-F1–F4). Three habits from it:
    - **Make the fake as stateful as the claim.** The tests' board answered UPD_STATUS as
      always idle and took a BEGIN while armed, so no test could see an abort drop an update
      the run had not begun. Model the state the claim touches as the firmware keeps it.
    - **List every call on the path that can block or fail, and bound or handle each one.**
      The serial write had no deadline, so "an apply is waited for, 8 × 0.3 s at most" was not
      true; a thread start could raise after the exit was decided.
    - **A sweep must assert the property, not only an allowed exit.** "Exit 5 is allowed for
      two Ctrl-Cs" passed runs that sent no stop at all. Check what must have happened: the
      stop frame, the board's state.

    **1.21 failed on neighboring paths (V121-F1–F4).** Before coding, fix the
    acceptance contract: distinguish this write attempt from the operation across
    retries, and known zero from missing evidence. Cross accepted prefixes with timeout,
    write error and wait error. Advance the clock during positive progress as well as a
    stall. On old and new clients record ordered frames, final board-model state, OS
    exit and the **whole** operator diagnostic together. A stop check is not atomic
    with a syscall; a deadline check is not an OS scheduling bound.

    **1.22:** test the signal and the transport fault *together*. Put the signal
    inside the wait that raises, with the original command already running. A
    ticket and UNKNOWN exit do not prove an abort was attempted; record its exact
    frame even when the port accepts zero bytes of the abort.

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
| **PowerShell 5.1 to `ssh` (the Pi, host `pitrac2` in `~/.ssh/config`)**: double quotes inside the remote command are stripped, so `$(...)`, `|` and quoted words break; and text piped into `ssh` arrives with a UTF-8 BOM in front and a CR at the end (a sudo password piped that way is rejected, 2026-11-18). | Write each remote step as a bash script with the file-writing tool, `scp` it, and run `ssh -o BatchMode=yes pitrac2 bash <script>`. Strip `\xEF\xBB\xBF` and `\r` from anything a remote script reads from stdin. When `pitrac2.local` does not resolve (it has not since 2026-11-25), the Pi answers over Tailscale: add `-o HostName=100.108.88.114 -o HostKeyAlias=pitrac2.local` to `ssh` and `scp` (owner, 2026-11-26). Give a remote script `< /dev/null`, so nothing in it can wait on the terminal. |
| **.NET file calls in PowerShell (`[IO.File]::ReadAllText("rel\path")`) resolve relative paths against the process directory, not the PowerShell location.** | Always pass absolute paths to `[IO.File]` methods. |
| **Host tests build with MSVC `/W4 /WX`**: a runtime check on a compile-time constant (`CHECK(CONST_A <= CONST_B)`) is warning C4127 → error. | Use file-scope `_Static_assert` for relations between `board.h` constants; keep `CHECK` for computed values. |
| **KiCad 10 `.kicad_pcb` syntax**: a pad's net is `(net "GND")` with no net number (older parsers expect `(net 12 "GND")`); pad `(at x y rot)` is relative to the footprint and must be rotated by the footprint's angle (positive = counter-clockwise on screen, y down). The JLC CPL CSV has **no test points** (TP1–TP6). | Parse footprints between `\n\t(footprint "` markers; read positions from the `.kicad_pcb`, not the CPL. Verify a pad position against one known part before trusting the rest. |
| **The netlist says what should connect, not what the copper does.** J8.17 is on +3V3_Pi5 in the netlist but has no copper; a bench step built on the netlist failed (2026-10-30, `NEXT_BOARD_REV.md` CR-27). | Before telling the owner two pads connect, run `& "C:/Program Files/KiCad/10.0/bin/kicad-cli.exe" pcb drc --format json --severity-all -o <scratchpad>/drc.json <the .kicad_pcb>` (read-only) and check `unconnected_items`; on these boards J8.17 is the only one. The fab gerbers name each copper object's net (`%TO.N,<net>*%`). |
| **The view tool / an editor can show stale content right after a write.** | Verify an edit with a search, not by re-reading the view. |
| **This PC's Python has no `pyserial`; WSL is blocked by policy.** | Put the wheel on `PYTHONPATH` for a run (`%TEMP%\pyserial_probe\*.whl`; if gone, `python -m pip download pyserial==3.5 --no-deps -d %TEMP%\pyserial_probe`); never install into the owner's Python. POSIX-only tests and mutations run on the Pi. |
| **`git apply` in a plain folder writes CRLF here** (`core.autocrlf=true`). | In scratch folders run git with `-c core.autocrlf=false`, and compare files in git's stored form (CRLF folded to LF; check first that no `1522bd7` blob holds a CR). `captures/review_tooling/make_patches.py` does this. |
| **Mutation runs lie in quiet ways.** | Run Python with `-B` and clear `__pycache__` (a reused `.pyc` ran the wrong mutation once); touch every test that `#include`s a `.c` (MSBuild skips files under `%TEMP%`); skip lists by exact name, never `startswith`; bound every wait in the tests themselves, so a mutation fails rather than hangs (the runner's HUNG is a failure); keep any real side effect out of reach (the suite replaces `systemctl poweroff` at import); a new safeguard can make an old one unreachable: remove it and retire its mutation. Verify the tree's hashes after every run. |
| **The Pi 5's kernel uses 16 KB pages.** A pipe holds 256 KB and frees room a page at a time. | Pipe-timing tests move ≥ 64 KB per step, and their reader threads are non-blocking and joined before the test ends (one that outlived its test read the next test's pipe by its reused descriptor). |
| **sudo on the Pi.** | The tests and mutations need none. If a step does, ask the owner; never put a password into a file, a command line, a doc or a message. |
| **`picotool reboot -u` returns before the board has re-enumerated.** The next command fails: `...returned an error: rebooting`, or `Communication with RP2350 device failed` (Board 5, 2026-11-23). | `Start-Sleep -Seconds 5` after every `reboot -u`, before the next picotool command. A failed command wrote nothing; repeat it. |

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
| Host analysis tools | `tools/scope.py` (captures), `tools/la_phase.py` (LA phase CSVs), `tools/pilot_analysis.py` (one triggered ADC5 pass: peak, FWHM, clipping — §3.7), `tools/netlist_report.py`, `tools/check_doc_links.py`, `tools/p8_cycles.ps1` (BENCH_P8_PI.md 8.5: the 20 Pi boot/shutdown cycles over the CLI), `tools/strobe_focus.ps1` (live: paced LED flashes for focusing the lenses, with `focus-preview.sh --hold-flash`), `tools/focus_meter.py` (the sharpness score for it) |

---

## 10. Keeping this file honest

Update it when a **convention**, a **trap**, or a **hard-won lesson** changes. Do not put board
status, next steps or measured values here — they go stale within a session and already have a
home in `PROGRESS.md`.
