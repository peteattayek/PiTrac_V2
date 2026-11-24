<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 PiTrac contributors -->

# Changelog

What each commit brings in, newest first. This is a summary. The dated record of every
measurement, review and decision is [`Hardware/firmware/PROGRESS.md`](Hardware/firmware/PROGRESS.md)
§6 and §8, and the live status is its §0 and the top block of §10.

Earlier history is in `git log`. This file starts with the commit after `b467166`.

---

## 2026-11-24: firmware 1.12, Board 5, 7c.0–7c.3 with two Mira220s, and focusing by the strobe

- **A route to Phase 7c.1** (PROGRESS §6, 2026-11-16).
  - Upstream PiTrac's `feature/pico` branch switches a streaming Mira220 to slave mode with a
    few I²C register writes after each stream start.
  - The finding and the register sequence are recorded in PROGRESS and in
    `BENCH_P5_P7_MIC_CAMERA.md` 7c.1.
  - No code from upstream is copied: it is GPL-2.0-only.
- **Phase 7c.1 written** (`BENCH_P5_P7_MIC_CAMERA.md`): the Mira220 in slave mode, triggered
  by the board. 7c.2 (the second camera) and 7c.3 (the geometry check) are outlined.
- **Two new Pi tools,** in `Software/camera-comparison/scripts/`; see `TRIGGER.md`:
  - `mira220-trigger.sh` switches a streaming Mira220 between master and slave mode.
  - `trigger-capture.sh` keeps the frames the board's triggers produce.
  - The report is `conversion/triggered_frames.py`.
  - Tests: `tests/test-trigger.sh`, `tests/test-trigger-capture.sh` and
    `conversion/test_triggered_frames.py`. They ran on a PC first, and on the Pi from 2026-11-23
    (7c.2 Step 0).
- **7c.0 and 7c.1 analyser plans fitted to four channels** (the owner's analyser). 7c.1 moves two
  probes, so there is a power-down between them. The capture commands wait 1800 s.
- **7c.0 says how to set up a Pi that lacks the Mira220 driver** (board 1's did).
- **Board 1's Pi is set up for 7c** (PROGRESS §6, 2026-11-18). The records: the sensor powers
  down 1 s after a stream stops (trigger tool, `TRIGGER.md`, 7c.1), and a PowerShell-to-ssh
  trap (`HANDOFF.md` §7).
- **7c.0 PASS on board 1** (2026-11-18).
  - 7c.1 now requires channel 5 bridged onto the trigger channel, with a new Step 0 that checks
    the level.
  - `WIRING_DIAGRAM.md` §7.3 now recommends the bridge.
- **7c.1 PASS on board 1** (2026-11-18): the Mira220 in slave mode, one frame per trigger, and
  `t_cam` about 0.2 µs, not the assumed 100–300 µs. The docs carry the measured value.
- **`trigger-capture.sh` fix:** it lost the last frame's tail, up to 4 KB held in v4l2-ctl's
  stdio buffer.
  - The frames now go out on stdout, unbuffered (`stdbuf -o0 … --stream-to=-`).
  - The test fake holds back each frame's tail the same way, and the test checks that every
    frame is whole.
- **Firmware image 1.12 passed independent software review** with three nonblocking wording findings. Installation on board 1 is pending: `BENCH_P8_PI.md` §8.9c.
  - It retires the `J4: NO CAMERA (CR-09)` printouts and comments.
  - It quotes 7c.1's measured `t_cam` (~0.2 µs) where comments assumed 100–300 µs.
  - It makes the 1.11 review's V111-N1 (watchdog bound) and V111-N2 (late-refusal message)
    wording changes.
  - It adds a new host-test case, `fsm.reset_rise_in_message`: 300/300 pass.
  - An independent build reproduced the BIN and UF2 byte-for-byte. The two version loads are
    the only changed instructions. The review findings are recorded in `PROGRESS.md` §6.
- **A correction:** the Mira220 driver writes `0x1001 = 0x41`, not 0. The PROGRESS 2026-11-13
  entry and 7c.0 Step 2 said otherwise; both are corrected.
- **The stand's geometry, and the reasons for it** (`Hardware/Stand/`; PROGRESS §6, 2026-11-19).
  - `README.md`: each part's position relative to the ball, the optimizer's decision chain, the
    check against the built CAD, the two timing models, and how to make a new layout.
  - `tools/stand_geometry.py` reads the assembly STEP and the board. Its output is
    `built/STAND_GEOMETRY_RH.csv`.
  - `reference_003/` holds byte-exact copies of the optimizer's scene and datums.
  - The CAD files are not in the repo yet: the assembly STEP alone is 171.7 MB.
- **`DEVELOPER_GUIDE.md` §4.6** lists the design doc's beam and freeze geometry as not
  reconciled with the stand.
  - The owner confirms the sheet is about 3 cm, not the doc's 3 mm.
  - The doc's 150 mm first-freeze distance was a loose starting value. How early the first
    freeze can really come is limited by the live readback (~0.2 ms): `Hardware/Stand/README.md`
    §4.1.
- **`Hardware/README.md`** lists the stand. Its rules 1 and 4 now match the owner's 7c and
  Phase 8 permissions.
- **A correction:** the impact sound does not always arrive before FIRE. See PROGRESS §6,
  2026-11-19.
- **`Hardware/Stand/README.md` points to `Github-Personal\Faceplate\Faceplate_Design`**, outside
  this repo: the cleaned-up optimizer, weighted020's record, and the stand's CAD.
  - The copy reproduces the recorded scores exactly (PROGRESS §6, 2026-11-19).
  - The README's description of the optimizer's hash checks is corrected.
- **`PITRAC_V2_STAND_GEOMETRY.html`** (new, repo root): the stand's geometry and why it was chosen, as
  one illustrated page. It has to-scale drawings, the decision timeline, and an interactive model of
  the flash positions and the impact sound.
- **`PITRAC_V2_TECHNICAL_GUIDE.html`:**
  - Its speed text and simulator now describe the 50 %-of-peak length for the ~30 mm sheet:
    a modelled ~38 mm, to be calibrated. 72.67 mm, set on 2026-11-19, was the full-contact
    length and wrong for this use (PROGRESS §6, 2026-11-20).
  - Its stand-off label now gives the built stand's 368.8 mm.
- **`Hardware/firmware/SHOT_PATH_PLAN.md`** (new): the workbench's firmware compatibility handoff,
  checked against the firmware and the controller file.
  - It lists the owner's decisions D1–D8 and work packages WP0–WP6 for the detected-shot
    path.
  - It records the owner's direction: the first flash as soon as reasonable, at any speed
    (§2a, D2, D8).
  - A plan; no firmware changed.
- **7c.3 PASS** (`BENCH_P5_P7_MIC_CAMERA.md`; desk work, 2026-11-20). With 7c.1's `t_cam`, the
  first freeze clears the sheet at every speed. Four whole-ball freezes fit CAM0's view at
  100 m/s if the first flash comes within 289 µs of the trailing edge (47.33 mm spacing).
- **A correction:** the live readback's baseline is ready about 0.2 ms after the switch to
  BURST (198–200 µs on board 1), not 0.4 ms. The 440 µs figure was a whole record, taken while
  the ADC ran at half rate.
  - Corrected: the plan, the Stand README, the stand page, `ARCHITECTURE.md` and
    `DEVELOPER_GUIDE.md`.
  - Three firmware comments wait for the next image (PROGRESS §6, 2026-11-20).
- **The owner decided D1–D8** for the detected-shot path (`SHOT_PATH_PLAN.md` §3a; PROGRESS §8).
  - The ADC switches to current monitoring at the trailing edge (D2).
  - The live fallback is upstream PiTrac's driver pattern (D4).
  - Pulse 0 is fixed at the width that keeps a 100 m/s shot sharp, about 9 µs (D8).
  - **D3 changes a reviewed live-strobe limit:** the span rises from 30 ms to 150 ms, with only
    the first 30 ms watched. It is made in WP5, under independent review.
  - No firmware changed.
- **A TXS0108E per camera** (the owner, 2026-11-20). One module fed from camera 0 would have driven
  an unpowered camera 1, because a Mira220's 1.8 V is up only while it streams.
  - Changed in `WIRING_DIAGRAM.md` (with a new drawing), `BRINGUP_NEW_BOARD.md` Step 13.4 and the
    technical guide's text.
  - The guide's Figures 2.1 and 2.2 were redrawn on 2026-11-24 with a module per camera, with
    B5/A5 jumpered onto B1/A1 on both modules. Figure 2.2 shows each breakout pin by pin.
- **Phase 7c.2 written** (`BENCH_P5_P7_MIC_CAMERA.md`): two Mira220s triggered by the board. No
  firmware change.
- **The Pi tools take two Mira220s** (`Software/camera-comparison`, `TRIGGER.md`):
  - `configure-cameras.sh --bus N`;
  - `trigger-capture.sh` with two `--config`;
  - a new pair report, `conversion/triggered_pairs.py`.
  - Tested under Git Bash on a PC: 109 + 30 + 47 checks and 17 unit tests. On the Pi
    (2026-11-23): the 30 + 47 checks and the 17 unit tests pass.
- **Board 5 brought up through §12** (2026-11-23). It is the COM10 board of the rework batch. The
  partition table is in, r5 is in B and 1.12 in A.
  - A 5 s wait after `picotool reboot -u` is now in `BRINGUP_NEW_BOARD.md` Step 1.3,
    `BENCH_P8_PI.md` §8.7 and `HANDOFF.md` §7. A command sent sooner fails.
  - Step 1.3 covers a board that arrives flashed flat.
  - `BENCH_REWORK_CHECK.md` §3 now takes each board's image from `PROGRESS.md` §10, not r5 by
    rule.
  - The test points can be probed from the back of a powered board: no other-net pad within
    6 mm there, and every via tented (`BRINGUP_NEW_BOARD.md` §2.1, Steps 7.1 and 12.2;
    `HANDOFF.md` §3 rule 8).
  - Steps 6.2 and 6.3 have a method without a scope or analyser: readback and §9 for 6.2, and
    TP5 on a DMM for U9's clamp.
  - **§8–§11 PASS:** phase 1320 ticks (400 ns), vref 3.270 V, Vos +6.2 mV. `BRINGUP_NEW_BOARD.md`
    9.4 and 10.5 add Boards 4 and 5, and 10.3 accepts an ADC5 reading up to ~3700.
  - Board 5 has no soldered leads, so `BRINGUP_NEW_BOARD.md` 12.2 and `NEXT_BOARD_REV.md` no
    longer name it as the lab board.
  - ADC0 at rest: "under 10 codes" was tighter than the ADC's own offset (board 1 read 9–12).
    It is now "about 10, 12 at most", plus a DMM on TP4 (under 2 mV) for a small standing
    current (`BRINGUP_NEW_BOARD.md` 12.3, `BENCH_P6_STROBE.md`, `BENCH_REWORK_CHECK.md`).
  - **§12 PASS:** U5's clamp 134 µs; 9 A at level 436 (42.58 %).
  - `BENCH_P8_PI.md` "First seating" gives R39's true edge gaps (0.38–0.57 mm, clips only). It
    adds a no-clip equivalent: J8.37 and J8.10 read in `STANDBY` through jumper wires in the
    empty J8 socket.
  - **Board 1's Pi on Board 5:** one clean `on`/`off`, and 1000 link pings with 0 failed.
  - **7c.2 Step 0 PASS:** the second camera's overlay is in, and both Mira220s are listed,
    configured and streaming. A Pi reboot keeps the rail up.
  - **7c.2 Steps 3–5 PASS on the firmware and Pi side** (2026-11-24): 12 handshakes, `t_cam`
    0.207–0.240 µs, and every frame paired. A 50-shot soak followed: 50 of 50, all paired.
    Camera 0 is on bus 10 (CAM/DISP 0). The analyser parts are still to run.
  - **The first live session found no LED current** (2026-11-24): an open load at J3. Once the
    owner fixed J3, 9 A solved at level 437 (42.68 %), and the flashes light both cameras.
- **Focusing by the board's strobe** (2026-11-24, `Software/camera-comparison/FOCUS.md`):
  - **`Hardware/firmware/tools/strobe_focus.ps1`** (new): paced live flashes, after BRINGUP
    12.3's checks and staircase and `strobe cal`. Every pulse is judged, and it always ends with
    `strobe live off`.
  - **`focus-preview.sh --hold-flash`:** the preview shows the latest flash-lit frame for up to 3
    s. Four new tests; all 66 preview tests pass on the PC and on the Pi.
  - **`Hardware/firmware/tools/focus_meter.py`** (new): a live sharpness score of each full
    flash-lit frame, for the peak-and-lock focus of `The_One_Board_To_Rule_Them_All.md` §18.2.
  - Both cameras focused (2026-11-24). **`strobe_focus.ps1 -WidthUs "100,20,10"`** compares
    pulse widths in one session. On this bench at 9 A the ball reads 90 / 17.5 / 9 codes (SNR
    64 / 23 / 19), linear in width.
  - **The Mira220 has no analogue gain** (the driver fixes it at 1×). **12-bit output** raised
    the ball's SNR ×1.07–1.29, most at 10 µs, at the same 89 fps. The ball 7 cm closer: ×1.2–1.3
    brighter, still in focus.
- **CR-09's next-layout plan corrected** (`NEXT_BOARD_REV.md`, 2026-11-21):
  - a translator per camera, each fed from its own camera's 1.8 V (a VDDIO pin per camera on
    J4). One translator on a local 1.8 V regulator would drive a camera that is off;
  - "the strobe inputs never read high" becomes "never guaranteed to read high".

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
