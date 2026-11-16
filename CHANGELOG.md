<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 PiTrac contributors -->

# Changelog

What each commit brings in, newest first. This is a summary. The dated record of every
measurement, review and decision is [`Hardware/firmware/PROGRESS.md`](Hardware/firmware/PROGRESS.md)
§6 and §8, and the live status is its §0 and the top block of §10.

Earlier history is in `git log`. This file starts with the commit after `b467166`.

---

## 2026-11-16: firmware 1.11, the Pi link and updates from the Pi, Phases 6–8 on board 1

Everything since `b467166` ("Working live pulses after rework", 2026-10-15).

### Where things stand at this commit

- **Board 1** (UID `a764f5332ca5ac53`) runs **image 1.11** in partition A. Image 1.7 is in B as the fallback.
  - **Phase 6 (strobe):** complete (2026-11-10).
  - **Phase 8 (real Pi):** first seating, 8.0b and 8.1–8.10 all PASS (2026-11-02 to 2026-11-12).
  - **Phase 7 (cameras):** 7a and 7b PASS (2026-11-13). 7c.0 is next.
- **Board 4** (one of three additional original boards) shipped on image 1.5, in partition A
  (2026-11-11).
- **Not yet written:**
  - reported ball velocity;
  - the Phase 5 onset detector and mic veto;
  - a strobe fired from a detected ball pass;
  - the shot messages on the Pi link;
  - 7c.1 onward, which needs the Mira220 in external-trigger mode.

### Firmware (`Hardware/firmware/src/`)

Every image went through an independent review. Images that failed review (r3, r4, 1.4, 1.6,
1.8, 1.9, 1.10) were fixed in the next image and never ran on a board. Host tests went from
78 at `a8e08b3` to **299** at 1.11, with mutation checks on every image.

- **Live strobe readback fix** (image `Oct 15 2026 22:09:11`, on board 1 2026-10-16):
  - A pulse is now a run above an absolute floor (baseline + 12 codes, about 72 mA).
  - Each pulse reports a settled level and a held level.
  - OVERCURRENT gains the held level over 10.8 A alongside the existing plateau and
    single-sample checks. It also latches when any two of three consecutive samples exceed
    10.8 A (the owner's choice).
  - A new SHAPE verdict ends live mode when a pulse overshoots or dips.
  - Fixes the 2026-10-13 finding: a turn-on spike could hide the pulse, or a stuck-on current.
- **Phase 8 preparation** (r2, 2026-10-19):
  - J8.37 is driven only while a Pi's 3V3 is up.
  - The USB reboot paths are closed, so `picotool -f` can no longer reboot a running board.
  - Halt telemetry (`hist`, `telemetry.c`).
  - A strobe session cap derived from `strobe cal`.
  - A config-store sequence fix.
- **The Pi serial link and firmware updates from the Pi** (r3–r5, version 1.1, 2026-10-20):
  - UART1 framing with CRC-16 and a sequence byte (`link_frame.c`), the protocol
    (`link_proto.c`) and the service loop (`pi_link.c`).
  - An A/B partition table (`partition_table.json`, built as `pitrac_pt.uf2`).
  - Try-before-you-buy images: an image buys itself at the end of init, and one that never gets
    there is rolled back within about 17 s.
  - `fw_update.c` and `fw_layout.c` write an image sent from the Pi and verify it with SHA-256
    (`sha256.c`).
  - The update note and the telemetry mirror live in RAM that survives the reboot into the new
    image. `tools/check_retained.cmake` stops the build if either one moves.
- **1.2:** r5's code with only the version changed, for the update test. **1.3:** the 10 s
  shutdown holdoff (owner) and two label fixes.
- **Phase 7: the camera handshake** (1.4, fixed in 1.5):
  - PIO1 runs the handshake (`cam_handshake.pio`): the trigger on GPIO10, both camera strobes on
    GPIO8/9, and `t_cam`.
  - FIRE comes at max(first delay, t_cam). `CAM_TIMEOUT` fires nothing.
  - `strobe_gate.pio` gates a dry burst on FIRE.
  - New commands: `cam`, `cam shot` and `cam trig`. `gpio 10 1` is refused while the rail is down,
    a fault is latched or a handshake is running.
  - Code: `camera.c`, `cam_plan.c`.
- **Calibration fixes for board 4** (1.6, released as 1.7):
  - `cal demod` gains a 30 % floor and restores the phase.
  - The saved detect threshold is restored at boot.
  - `cfg save` names its fix.
  - `level` is back to reading the current phase only.
  - The pure maths is split into `cal_math.c` so it can be host-tested.
- **The strobe with a Pi present** (1.8 to 1.11):
  - Live mode may run with a Pi powered. It then arms no watchdog (the owner's decision), and the
    session ends if a Pi appears or goes away.
  - Pi presence is read fresh: the 3V3 sensed now, plus the power FSM's classification.
  - `reset`, `bootsel` and `cfg save` treat a sensed 3V3 as a Pi.
  - `reset` and `bootsel` check again right before the reboot, and refuse while POWERING_ON.
- **Build** (`CMakeLists.txt`):
  - The version is in the IMAGE_DEF (now 1.11).
  - `PITRAC_TBYB` is on by default.
  - `PITRAC_TEST_TRIAL_HANG` builds a test image for rollback.
  - The partition table is built as a UF2.
  - The retained-RAM check runs after every link.

### Host tests (`Hardware/firmware/tests/`)

- **New suites:** `cal_math`, `cam_pio`, `cam_plan`, `config_store`, `engine`, `fw_update`,
  `link`, `strobe_fsm` and `telemetry`. Existing suites (power FSM, strobe live, strobe plan,
  ADC ring, service) gained cases.
- **`pio_sim.c`** runs the PIO programs in a cycle model.
- **`rom_scan.c`** reproduces the boot ROM's partition scanner, as the oracle for update tests.
- **`sdk_standin/`** lets real sources compile on the host.

### The Pi side (`Hardware/firmware/pi/`, new)

- **`pitrac-rpi5-on.service`** raises RPI5_ON (J8.15) once userspace is up.
- **`pitrac-rpi5-on-shutdown.sh`** is the shutdown hook.
- **`90-pitrac-persistent.conf`** keeps the journal across power cycles.
- **`pitrac_link.py`** (`pitrac-link`) is the serial link and the firmware updater.
- **`install.sh`** installs the above. Tests: Python, and the shutdown hook.

### Tools (`Hardware/firmware/tools/`)

- **`pulsegen/`** (new): Pico or Pico 2 test equipment that answers the camera trigger with a
  delayed strobe pair, for 7b. Measured 10–18 ns late on a Pico 2.
- **`p8_cycles.ps1`** (new): runs and checks the 8.5 boot/shutdown cycles over the board's CLI.
- **`check_retained.cmake`** (new): see Build.
- **`flash_swd.sh`** now refuses by default. SWD overwrites the partition table, and a
  try-before-you-buy image never starts after an SWD reset.
- **`check_doc_links.py`** also checks `Hardware/firmware/pi/*.md` and this file.

### Bench results

- **Phase 6, board 1: complete (2026-11-10).**
  - The readback fix was checked live (2026-10-16).
  - 6c Step 3 reached 30 % (2.9 A), with ADC0 within 0.5 % of the Saleae.
  - Step 4: `strobe cal 9` solved level 439 (42.87 %), confirmed at 9.02 A.
  - Steps 4b and 5: bursts at 9 A. The VIR boost goes into hiccup after 50 µs and 100 µs
    pulses (CR-25), and the bank is about 600 µF.
  - Then the session-cap check and Step 6.
- **Phase 8, board 1, with a new Pi 5** (prepared off the board 2026-10-26):
  - 8.0b, 2026-11-02: the no-Pi firmware checks.
  - 8.7, 2026-11-09: the partition table, try-before-you-buy and rollback.
  - First seating and 8.1–8.6, 2026-11-09.
  - 8.8 and 8.9, 2026-11-10: the serial link, and image 1.2 installed from the Pi.
  - 8.9b, 2026-11-12: image 1.7 from the Pi.
  - 8.10, 2026-11-12: the strobe with a Pi present, on 1.11.
- **Phase 7, board 1, 2026-11-13: 7a and 7b PASS.**
  - In 7b, `t_cam` reads 44–63 ns above the analyser.
  - The burst starts 4.0–4.9 µs after FIRE.
  - One stray trigger edge at PSU-on came from the test Pico's input (RP2350 erratum E9), not
    from the board.
- **Board 4:** bring-up started 2026-11-11 (owner).
- **The additional original boards:**
  - A rework plan and check protocol: 162 kΩ across R80, 2 pF C0G across C68/C70, and 1 µF at
    Q9's gate.
  - The COM10 board (UID `6093584a7790419d`) clips at 8 % and 25 % duty. Its optical acceptance
    is pending.

### Decisions recorded (PROGRESS §8)

All by the owner.

- **2026-10-16, the strobe:**
  - A session gate ceiling comes from the calibration, with 70 % kept as the outer bound.
  - `BURST_CHARGE_MAX_MC` stays at 6 mC.
  - Phase 6 thermals are closed on board 1.
  - The next board keeps the boost's hiccup and adds a VIR monitor (CR-25).
- **2026-10-19:** firmware updates from a board-powered Pi are done in firmware: A/B
  partitions and try-before-you-buy, with no hold-up hardware.
- **2026-11-09:**
  - A Pi may sit on J8 for Phase 8 before Phases 2–7 are complete.
  - After an unrequested Pi halt, the rail stays up.
- **2026-11-10:** the shutdown holdoff is 10 s.
- **2026-11-12:**
  - This revision's five prototype boards are brought up by hand. The guided calibration
    (CR-29) waits for the next revision.
  - Live strobe mode arms no watchdog while a Pi is powered.
  - `BENCH_P8_PI.md` §8.10 is approved.
- **2026-11-13:** for 7c, a Pi may sit on J8 and a camera on J4, the camera only through a
  TXS0108E module. The safety rules in `HANDOFF.md`, `CLAUDE.md` and `AGENTS.md` follow.

### Documents

- **[`BRINGUP_NEW_BOARD.md`](Hardware/firmware/BRINGUP_NEW_BOARD.md):** rewritten as a
  step-by-step protocol with a per-board sign-off.
- **[`BENCH_P8_PI.md`](Hardware/firmware/BENCH_P8_PI.md):** new 8.0b, first seating, 8.7, 8.8,
  8.9, 8.9b and 8.10.
- **[`BENCH_P5_P7_MIC_CAMERA.md`](Hardware/firmware/BENCH_P5_P7_MIC_CAMERA.md):** Phase 7
  procedures 7a, 7b and 7c.0, and the outline of 7c.1.
- **[`BENCH_P6_STROBE.md`](Hardware/firmware/BENCH_P6_STROBE.md):** 6c after the readback fix
  (Steps 4, 4b and 5, and the session cap).
- **[`NEXT_BOARD_REV.md`](Hardware/firmware/NEXT_BOARD_REV.md):**
  - **New change requests:**
    - CR-25: the boost hiccup;
    - CR-26: recovering the RP2354 from a seated Pi;
    - CR-27: J8.17 has no copper;
    - CR-28: a temperature sensor at the beam LED;
    - CR-29: a guided self-calibration.
  - **CR-09:** the camera does not level-shift, and the trigger must be held low through boot.
  - **Rework:** existing-board rework with the owner's available parts.
  - **A correction:** the OPA4323's bandwidth is 20 MHz, not 8 MHz.
- **New:**
  - [`REWORK_ORIGINAL_BOARDS.md`](Hardware/firmware/REWORK_ORIGINAL_BOARDS.md) and
    [`BENCH_REWORK_CHECK.md`](Hardware/firmware/BENCH_REWORK_CHECK.md);
  - [`pi/README.md`](Hardware/firmware/pi/README.md).
- **Flashing under try-before-you-buy and the partition table:** `SETUP.md`, `START_HERE.md`,
  `BENCH.md`, `HANDOFF.md`. Use `picotool load -x`, or `bootsel` at the CLI.
- **`BENCH_P3_DETECT.md`:** the sizing correction, and the `level` command.
- **`BENCH_P2_BEAM.md`:** the 35 % effective-duty ceiling.
- **`ARCHITECTURE.md`** and **[`DEVELOPER_GUIDE.md`](DEVELOPER_GUIDE.md):**
  - the new modules;
  - the UART protocol (now implemented) and firmware updates from the Pi;
  - the roadmap.
- **[`Hardware/README.md`](Hardware/README.md):** the phase table.
- **New reference documents at the repo root:**
  - [`WIRING_DIAGRAM.md`](WIRING_DIAGRAM.md): the system harness and pinout, including the
    cameras through one TXS0108E;
  - [`LIGHT_SHEET_TRIGGER_THEORY.md`](LIGHT_SHEET_TRIGGER_THEORY.md): the light-sheet trigger's
    theory and ball-speed estimation;
  - [`PITRAC_V2_VS_V1.md`](PITRAC_V2_VS_V1.md): a comparison with the current PiTrac;
  - `PITRAC_V2_TECHNICAL_GUIDE.html`: a single-page reference combining the three.
- **Repo hygiene:**
  - `.gitattributes` keeps `Hardware/firmware/pi/**` LF, because systemd units break on CRLF.
  - `.gitignore` covers `build_hang/` and `build_v*/`.
  - This changelog.

**Not in the commit:** analyser captures (`Hardware/firmware/captures/`) and build outputs
(`*.uf2`, `*.elf`, `build*/`) are gitignored.
