# PiTrac RP2354 Firmware — Progress & Resume Context

**Read this first when resuming work.** It records what is done, what is next, and the
decisions/measurements that must not be lost between sessions.

Last updated: **2026-10-20** (board 1 runs the readback-fix image `Oct 15 2026 22:09:11`, and the Phase 6 exit list is complete on it; the Phase 8 prep firmware failed its first independent review on two findings, both fixed in r2; r3 `Oct 20 2026 06:13:43` adds the Pi serial link and firmware updates from the Pi, and failed its independent review on two new update findings; r2's fixes are confirmed; r4 `Oct 20 2026 11:11:58` fixes the original reproductions, but its confirm review found R4-F1, a false cleanup success at a sector boundary. R3-F2 is confirmed; r5 `Oct 20 2026 14:59:52` passed the independent confirm review: R4-F1 closed. Reflash first with that exact image, then the no-Pi checks; Board 1 has not yet been reflashed). **Current status is in §0 and the TOP block of §10.**

**History moved 2026-10-05:** the 2026-08-24 overview, the Phase 2 bring-up fix table and the
old companion-docs table are in [`PROGRESS_ARCHIVE.md`](PROGRESS_ARCHIVE.md), verbatim. The
document map and reading order are in [`../README.md`](../README.md) and `HANDOFF.md`.

---

## 0. Where things stand

| | Status |
|---|---|
| **Handoff** | 🔵 **New to this work? Read `HANDOFF.md` first** (conventions, safety, tooling pitfalls, reading order; for a whole-repo developer overview see `DEVELOPER_GUIDE.md` at the repo root), then §10 for the live resume point. There is **no separate plan file** — the `~/.claude/plans/…` paths this table used to cite no longer exist. PROGRESS is the plan. |
| **Boards** | **Board 3** — UID `6d6fda754e367a40`, **reworked TIA (Rf 116 kΩ, Cf 0.99 pF)**, calibrated and saved (carrier 104166 Hz, phase 1311 ticks, slot A seq 5); runs the `Oct  5 2026 11:13:55` image; 6a.1 showed **no pulse at R61** (2026-10-05). **Board 2** — stock 470 kΩ TIA, calibrated; **a probe on R61 sparked 2026-10-05**, damage check pending. ⚠ One of boards 2/3 reads **10 Ω from R61 to GND even with R62 removed** (2026-10-06) — parked, R62 off; which board is to be confirmed (§6). 🟡 **Board 1** — UID `a764f5332ca5ac53`; **runs image 1.11 in partition A since 2026-11-12 (bought; 1.7 in B, the fallback); §8.10 PASS (§6)**; U11B destroyed 2026-08-17 by foil across D12 (§11): **out of service for optics**, **the strobe board** — fast-path bring-up and **6a/6b PASS 2026-10-06**, TP3 loop stability **PASS 2026-10-08**; before 1.2, 1.7 and 1.11 it ran **r5 `Oct 20 2026 14:59:52`** (flashed 2026-10-30; partition table installed 2026-11-02; since 2026-11-09 r5 runs in partition B, A erased, after §8.7; §6); before it the readback-fix image `Oct 15 2026 22:09:11` (flashed 2026-10-16), `Oct 12 2026 14:43:50` from 2026-10-12, and the `Oct  7` 6c build (§6); leads soldered to R33 (GPIO25), R61 pad 1 (U5 Q), TP3, TP4 and two on R66's GND pad; J3 pins 1 and 2 re-soldered (cold joints, 2026-10-13); LED leads twisted; **1 µF X5R at Q9's gate** (CR-23 stage-1 rework, verified 2026-10-15). |
| **Additional original-board rework batch** | **Three other original boards; 2 pF parts confirmed C0G. COM10 UID `6093584a7790419d` identified 2026-11-03**, running r5 flat; **it is Board 5, brought up through §12 on 2026-11-23, and powering board 1's Pi since that day, with R80 measured at 120 kΩ across its pads (§6, §10)**. Owner confirms original R80 470 kohm retained with 162 kohm added in parallel; intended Rf **120.475 kohm**, measured 120 kΩ. **ADC2 clipped at 8% and 25% duty on 2026-11-03**, with no baffle; with one, §8 passes to 25 % (2026-11-23). ADC5V scale **1.0688 saved, slot A seq 9**. Other batch UIDs and their checks remain pending; temporary RW-A/B/C labels have not been mapped to UIDs. [Rework plan](REWORK_ORIGINAL_BOARDS.md), [check protocol](BENCH_REWORK_CHECK.md). |
| Toolchain | Installed — VS Code Pico extension, private copies in `%USERPROFILE%\.pico-sdk`. SDK 2.3.0, toolchain 15_2_Rel1, ninja 1.13.2, cmake 4.3.4. ⚠ **Not on PATH** — see `HANDOFF.md` for the export line. |
| Bench equipment | Saleae **Logic Pro 8** (analog inputs **±10 V / 12 V max**, ~5 MHz analog bandwidth, up to 50 MS/s; **four channels in use**, owner 2026-11-18, so 7c's analyser plans use four); **Tek MSO54B** (500 MHz, 4 ch, TPP1000 probes — **available again 2026-10-08**); Tektronix **TDS 1002** (60 MHz, 2 ch, 8-bit; the fallback, 2026-10-07); DMM, current-limited PSU, FLIR thermal camera. |
| **Firmware** | 🟡 **Image 1.12, 2026-11-18: text only.** It retires the J4 printouts and comments after 7c, quotes 7c.1's measured `t_cam` (~0.2 µs) in comments, and makes the 1.11 review's V111-N1/N2 wording changes. Built; host tests 300/300; mutations 2/2; machine code identical to 1.11 apart from the version. **Independent software review PASS with nonblocking wording findings. On Board 5 since 2026-11-23 (A, bought; r5 in B); board 1's install pending** (§6, "Image 1.12" and 2026-11-23): `build_v1.12/`, Release `Nov 18 2026 15:57:13`, 247528 text / 0 data / 422684 bss, UF2 SHA256 `5ABB297C…0D39D377`, `.bin` `DC1B64BE…A56C04EB`. 🟡 **Image 1.11, 2026-11-12: 1.10 with V110-F1 fixed -- `reset` and `bootsel` check again after their 50 ms wait, immediately before the reboot, disarm the watchdog only then, and refuse while POWERING_ON. Built, host tests 299/299, 30/30 mutations; **on board 1 since 2026-11-12, from the Pi (A, bought; 1.7 in B the fallback); `BENCH_P8_PI.md` §8.10 PASS**; independent confirm review PASS (V110-F1 closed, sampled V19-F1 closure complete; V111-N1/N2 nonblocking wording notes)** (§6, "Image 1.11"): `build_v1.11/`, Release `Nov 12 2026 15:42:51`, 247536 text / 0 data / 422684 bss, UF2 SHA256 `E940AE6A…F9B72991`, `.bin` `883D4EFA…CA1CFC31`. ❌ **Image 1.10, 2026-11-12: 1.9 with V19-F1 fixed -- plain `reset`, `bootsel` and `cfg save` count a Pi's 3V3 sensed now as a Pi (`power_pi_maybe_present()`), the FSM unchanged -- and the 1.9 review's two text notes. Built, host tests 298/298, 25/25 mutations; NOT on a board; independent confirm review NOT PASSED (V110-F1: reset/bootsel permission becomes stale during their 50 ms wait)** (§6, "Image 1.10"): `build_v1.10/`, Release `Nov 12 2026 15:13:28`, 246560 text / 0 data / 422684 bss, UF2 SHA256 `F5BF95F2…CFF48060`, `.bin` `ECB0746B…2626029A`. ❌ **Image 1.9, 2026-11-12: 1.8 with the strobe's Pi observed fresh -- the 3V3 sensed now and the power FSM's classification, UNSETTLED when they disagree (V18-F1) -- and V18-N1's comments. Built, host tests 292/292, 17/17 mutations; NOT on a board; confirm review closes V18-F1/N1, but overall NOT PASSED (V19-F1: pre-existing reset/bootsel and config guards trust cached Pi presence)** (§6, "Image 1.9"): `build_v1.9/`, Release `Nov 12 2026 11:06:20`, 245512 text / 0 data / 422684 bss, UF2 SHA256 `0004839E…52247D16`, `.bin` `32409AE6…B57766B9`. ❌ **Image 1.8, 2026-11-12: the strobe may fire with a Pi powered; live mode then arms no watchdog (owner), and a live session ends if a Pi appears or goes away; `level` worded as an aiming aid (V17-N1). Built, host tests 280/280, 11/11 mutations; NOT on a board; independent review NOT PASSED (V18-F1: cached Pi presence defeats the live-session/watchdog rule)** (§6, "Image 1.8"): `build_v1.8/`, Release `Nov 12 2026 10:25:58`, 245072 text / 0 data / 422684 bss, UF2 SHA256 `14D5CBC6…88002DB6`, `.bin` `5E691DCB…4D530463`. The owner approved `BENCH_P8_PI.md` §8.10; the failed review blocks flashing and that test. 🟡 **Image 1.7, 2026-11-11: 1.6 without the all-phase `level` (back to 1.5's current-phase reading) and with the 1.6 review's comment note (V16-N1). It keeps the `cal demod` 30 % floor and phase restore, the saved threshold restored at boot, and `cfg save` naming its fix. Board 4's next image (it shipped on 1.5). Built, host tests PASS; independent review PASS 2026-11-12; **on board 1 since 2026-11-12, from the Pi (`BENCH_P8_PI.md` §8.9b PASS)** (V16-F1/N1 closed; nonblocking wording note V17-N1)** (§6, 2026-11-11, "Image 1.7"): `build_v1.7/`, Release `Nov 11 2026 16:41:14`, 244008 text / 0 data / 422684 bss, UF2 SHA256 `CCB7B931…BA15087B`, `.bin` `F7129CD4…84C94602`, 273/273 host tests, 9/9 mutations caught. ❌ **Image 1.6: independent review NOT PASSED (V16-F1, `level`); never released** (§6, "Image 1.6"). 🟡 **Image 1.5, 2026-11-10: 1.4 with the independent review's three findings fixed (H7-F1 one coherent sample of both strobes; H7-F2 the strobe gate read with the engine stopped, and CAM_TIMEOUT certified only by it; H7-F3 `gpio 10 1` refused with the rail down, a fault or a handshake). Built, host tests PASS; Board 4 shipped on it in partition A (2026-11-11); independent confirm review PASS (H7-F1/F2/F3 closed)** (§6, 2026-11-10, "Image 1.5"): `build_v1.5/`, Release `Nov 10 2026 16:48:47`, 241824 text / 0 data / 422684 bss, UF2 SHA256 `F82A6562…F85374B7`, `.bin` `6ABFDBE4…1716A9AA`, 265/265 host tests, 34/34 mutations caught. 🔴 **Image 1.4, 2026-11-10 (superseded by 1.5; never left this PC; `build_v1.4/` removed): Phase 7 -- the camera handshake in PIO1 (trigger, both strobes, `t_cam`, FIRE at max(first delay, t_cam), `CAM_TIMEOUT`) and a dry camera-gated strobe burst; `cam`, `cam shot`, `cam trig`. Built, host tests PASS; NOT on a board; independent review NOT PASSED (H7-F1/F2/F3; review report in section 6)** (§6, 2026-11-10, "Phase 7 firmware"): `build_v1.4/`, Release `Nov 10 2026 13:46:40`, 240896 text / 0 data / 422676 bss, UF2 SHA256 `96563EF9…48B54134`, `.bin` `3C894686…4F03BCB51`, 246/246 host tests (the PIO programs run in a cycle model), 20/20 mutations caught. 🟡 **Image 1.3, 2026-11-10: the 10 s shutdown holdoff (owner), the flash-update `this boot` label and the released-J8.37 label; built, host tests PASS, not on a board yet** (§6, 2026-11-10, "Image 1.3"): `build_v1.3/`, Release `Nov 10 2026 10:25:37`, 224872 text / 0 data / 422112 bss, UF2 SHA256 `0258B97E…07C3533`, `.bin` `76762B40…766971FD7`, 228/228 host tests, 23/23 Pi-tool, 21/21 shutdown-hook. Never ran on board 1: `BENCH_P8_PI.md` §8.9b runs 1.7 instead (2026-11-12). 🟡 **Image 1.2, 2026-11-10: r5's code with only the version changed; on board 1 since 2026-11-10, installed from the Pi (`BENCH_P8_PI.md` §8.9 PASS)** (§6, 2026-11-10): `build_v1.2/`, Release `Nov 10 2026 09:47:13`, 224728 text / 0 data / 422112 bss, UF2 SHA256 `7B7DBEE3…C24AD6FF`, `.bin` `3EFB1072…2E81CD18`. 🟡 **New image r5, 2026-10-20: r4 with R4-F1 fixed (cleanup counts only when sector 0 reads back all 0x00 or all 0xFF; the test oracle is the boot ROM's own scanner); independent confirm review PASS; flashed on board 1 2026-10-30**, §8.0b PASS, partition table installed 2026-11-02, §8.7 PASS 2026-11-09 (§6, 2026-10-30, 2026-11-02 and 2026-11-09; review: §6, 2026-10-20 r5): Release `Oct 20 2026 14:59:52`, 224720 text / 0 data / 422112 bss, SHA256 `AC5F4493…C272ACF7`, version 1.1, 228/228 host tests, 23/23 Python tests, 83/83 mutations caught. 🟡 **Image r4, 2026-10-20 (superseded by r5): r3 with the review's original update reproductions fixed; R3-F2 confirmed, but confirm review NOT PASSED on R4-F1 (cleanup can miss a surviving header across the first-sector boundary); NOT flashed** (§6, 2026-10-20 r4): Release `Oct 20 2026 11:11:58`, 224568 text / 0 data / 422112 bss, SHA256 `015205B5…10A7FE96`, version 1.1, 224/224 host tests, 23/23 Python tests, 79/79 mutations caught. 🟡 **Image r3, 2026-10-20 (superseded by r4): the Pi serial link and firmware updates from the Pi (A/B partitions, try-before-you-buy, rollback), on top of r2; independent review NOT PASSED, NOT flashed** (§6, 2026-10-20): Release `Oct 20 2026 06:13:43`, 222424 text / 0 data / 421752 bss, SHA256 `BD1E45D7…AD495AA4`, version 1.1, 204/204 host tests, 21/21 Python tests, 52/52 mutations caught. It contains r2 unchanged; both r2 fixes are now confirmed. Two new update findings (failed final header write and reset before Pi restoration), fixed in r4; see the 2026-10-20 independent review in §6. 🟡 **Image r2, 2026-10-19: the review's two findings confirmed fixed in the r3 source review; NOT flashed** (§6, "Review findings fixed"): Release `Oct 19 2026 11:44:38`, 207808 text / 0 data / 123508 bss, SHA256 `841E353E…34398ABF`, 161/161 host tests and 21/21 shutdown-hook tests, 49/49 mutations caught. Round 1 (`Oct 19 2026 06:23:15`) is superseded. It adds: J8.37 driven only while a Pi's 3V3 is up; the USB reboot paths closed; Phase 8.6 halt telemetry (`hist`); the strobe session cap from `strobe cal`; a config-store sequence fix; comment corrections. ✅ **Board 1 runs the live readback fix, flashed 2026-10-16** (Session A dry checks PASS, §6): `Oct 15 2026 22:09:11` (Release, 198840 text / 0 data / 123220 bss, SHA256 `FCC1AF66…18195B57`, 117/117 host tests, 28 mutations: 27 caught, 1 equivalent; independent review: round 1's four findings fixed, rounds 2 and 3 PASS; §6). Before it, board 1 ran fix image `Oct 12 2026 14:43:50` (Release, flashed 2026-10-12): **194344 text / 0 data / 122796 bss**, SHA256 `2D3BD0C1…4FB6471D`, **102/102 host tests**, 25 mutations all caught. Six review rounds (Sol passed round 6). Then the bench found **the ADC running at half rate (DIV 95)**, now fixed, along with first-pass snapshot brackets (§6, 2026-10-12 bench entry). It fixes the ADC0 stale-baseline readback found at 6c Step 2 (§6 2026-10-12). On `14:43:50`, board 1 passed `adc ringtest 100` (200/200 covered) and 6c Steps 1–2 (§6). It replaced `Oct  7 2026 08:07:33` (Release, 184440 text / 0 data / 122720 bss, SHA256 `DE0C6676…2DE80BC2`, 78/78), on which one live pulse at gate 0 latched a false `STROBE_CLAMP` (2026-10-12; no current). 🔴 **Never fire live on the `Oct  7` image.** Board 3 still runs `Oct  5 2026 11:13:55` (13/13 smoke PASS). The 6c build is the 2026-10-05 image plus **strobe 6c/6d live mode** (`strobe live on confirm`: pulses with a setpoint under the staircase / 70 % ceiling / pacing / charge guards, ADC0 readback and verdict per firing, `STROBE_OVERCURRENT` and `STROBE_CLAMP` faults, watchdog armed by the strobe, CLI allowlist, `strobe cal`, `strobe wave`), the "before 6c" §9 items, and two fixes from an independent review (§6 2026-10-07). Dry behaviour (6a/6b) is unchanged. Written: power FSM, safe state, beam carrier + demod, ADC engine (DMA ring, block capture, **triggered capture**, freeze-and-copy), detect (threshold DAC, gated HPF, comparator PIO timing, pass log + retained waveforms), cal (`cal demod`, `cal model`, `scan carrier`), dual-slot config store, service/yield layer, shot sequencer (**test shots from 1.4**: the camera handshake and a dry camera-gated burst; a detected pass ends "not implemented"), **strobe 6a–6d** (PIO0 burst engine + DMA, gate DAC, §15 schedule, charge interlock, A7 fix, live mode), CLI (every numeric argument strictly parsed). ❌ **Not written:** reported ball velocity, Phase 5 onset detector + mic veto, a fired shot from a detected pass (the ball speed, the first delay from the geometry), live current from the shot path, the strobe with a Pi present, persisting the strobe current LUT, the Phase 8 shot messages on the Pi link. Phase 8.6 halt telemetry and the Pi serial link/updates are written; independent review passed on r5 (2026-10-20); hardware acceptance under way on board 1 (§8.0b, 2026-10-30). Phase 8.0–8.5 power/shutdown firmware exists and passed against a **simulated** Pi (1b); no real Pi has been connected. **Committed 2026-10-09 as `a8e08b3`** (all Phase 6 work, tests and docs), at the owner's request. **Everything since `b467166` (2026-10-15), through image 1.11 and the 7c.0 procedure, committed 2026-11-16 at the owner's request; `CHANGELOG.md` at the repo root lists it. Everything since, through image 1.12, Board 5's bring-up, 7c.0–7c.3, the two-camera tools and the focus tools, committed 2026-11-24 at the owner's request (`CHANGELOG.md`, the 2026-11-24 section).** |
| **Bench work done** | ✅ Phases 0, 0.5, 1, 1b, 1c, 2 previously passed; **power-request regression found 2026-09-18**, fixed, and ✅ **BENCH.md Test 6 PASS (owner-reported 2026-09-28)**; see §6/§10. ✅ **Phase 3 §3.1–3.6 on board 3** — §3.6b is a *design finding* (Q8 ×7.43, CR-02), §3.6 Check 2 PASS. ✅ **Phase 5 mic bring-up** (2026-08-31). ✅ Board 3 detect control-path smoke and **first real optical ball waveform** (2026-09-18), **3.102 V peak / ~22.7 ms FWHM**, no observed clipping. Comparator edge timing and full §3.7/Phase 4 remain open. ✅ **Strobe 6a and 6b PASS on board 1 (2026-10-06)** — U5 clamp 135 µs, LA timing, schedule, A7, interlock, gate DAC (§6). |
| **Next** | 🔵 **2026-11-24: 7c.2 Steps 3–5 PASS on the firmware and Pi side** (12 handshakes, `t_cam` 0.207–0.240 µs; every frame paired; then a 50-shot soak, 50 of 50, all paired). Camera 0 is on bus 10 (CAM/DISP 0), the reverse of the table. **The first live session found no LED current; the owner fixed J3,** and 9 A solved at level 437 (42.68 %), 9.015 A. Flashes light both cameras. **New:** `tools/strobe_focus.ps1` and `focus-preview.sh --hold-flash`, for focusing by paced flashes. **Next:** focus (camera 0 is far out); then 7c.2's analyser parts (§6, §10). 🔵 **2026-11-23: Board 5 (the COM10 board) bring-up first (owner).** Steps 1.3–1.4 PASS: the partition table, and r5 in B. **1.12 in A, bought** (its first board). §2's readings at the first `on`; §3, §4 and §5 PASS; **R80 120 kΩ** (the rework check PASS). §6–§12 before the Pi (owner), with no soldered leads (§8, §10). **§8–§11 PASS:** phase 1320 ticks (400 ns), vref 3.270 V, Vos +6.2 mV; Step 6.4 assumed OK by the owner (no FLIR). **§12 PASS:** TP3 4.94 / 8.91 / 0 V; U5's clamp 134 µs; 9 A at level 436 (42.58 %). **Board 1's Pi on Board 5** (§6): `BENCH_P8_PI.md` "First seating" PASS as far as it runs from this PC, with one clean `on`/`off` and 1000 pings, 0 failed. **7c.2 Step 0 PASS:** the tests pass on the Pi, the second overlay is in, and both Mira220s (buses 11 and 10) are configured and stream. Camera 1 sees no light yet (lens or cap). **Next:** 7c.2 Steps 1–5 (module #1 and J4 wired with the power off; the analyser); the owner's call on repeating 7c.0/7c.1's module checks on Board 5 (§10); NIR light for the lenses. §13's rollback and §8.0b are skipped. 🔵 **2026-11-20: the owner decided the shot path's D1–D8** (§8; `SHOT_PATH_PLAN.md` §3a). D3 raises the live span limit to 150 ms with the first 30 ms watched, in WP5 under independent review. **Next:** 7c.2 on board 1, written 2026-11-20: a TXS0108E per camera (the owner's choice, §8), and the Pi tools for two Mira220s (§6). No reflash. Then WP0 and WP1. 🔵 **2026-11-20: 7c.3 PASS (desk; §6).** Four whole-ball freezes fit CAM0's view at 100 m/s if the first flash comes within 289 µs of the trailing edge (47.33 mm spacing). The live readback's baseline takes 198–200 µs, not ~0.4 ms as I had written (§6). **Next:** the owner's decisions D1–D8 in `SHOT_PATH_PLAN.md`, and WP0; 7c.2 when a second Mira220 is on hand. 🔵 **2026-11-18: 7c.0 and 7c.1 PASS on board 1** (§6). The Mira220 in slave mode exposes while the board's trigger is high, and its strobe follows about 0.17 µs late: `t_cam` 0.21–0.24 µs, not the assumed 100–300 µs. **Next:** at the next power-up, the fixed `trigger-capture.sh` onto the Pi and its frames pulled; prepare the Pi install instructions for the reviewed 1.12 image (§10). 7c.2 awaits a second Mira220. 🔵 **2026-11-13: 7c allowed by the owner** (a Pi on J8 and a camera on J4, the camera only through a TXS0108E module; §8). **Next: 7c.0 on board 1:** the module, the trigger's level at the camera, and the camera's strobe reaching GPIO8, with the camera free-running. 7c.1 (written 2026-11-16, runs after 7c.0) puts the Mira220 in slave mode with `trigger-capture.sh`, after each stream start; the register sequence is upstream PiTrac's (§6, 2026-11-16). The stray J4.3 edge at power-up was the Pico 2's (erratum E9), not the board's (§6). 🔵 **2026-11-13: Phase 7 on board 1 (owner), on 1.11 with no reflash. 7a and 7b PASS** (§6). 7b, with a Pico 2 running `tools/pulsegen`: t_cam reads 0.044–0.063 µs above the analyser at 50, 150, 200, 201 and 300 µs, and the burst starts 4.0–4.9 µs after FIRE in every shot. 7a: a 1000 µs timeout fires nothing, and every refusal holds (Step 3's 5000 µs repeat not run). 7b once a spare Pico runs `tools/pulsegen`. 7c is not yet allowed (§10). 🔵 **2026-11-10: Phase 7 image 1.5 fixes the review's H7-F1/F2/F3: independent confirm review PASS: reflash first with the exact 1.5 image, then `BENCH_P5_P7_MIC_CAMERA.md` 7a/7b with no Pi and no camera** (§6, §10). Image 1.4 failed its review and is not to be used. 🔵 **2026-11-10: board 1 runs 1.2 from partition A, installed from the Pi; `BENCH_P8_PI.md` §8.0b and §8.7 PASS** (§6, 2026-11-02 and 2026-11-09). **Phase 8 with the Pi seated on board 1: first seating, 8.1–8.6, 8.8 and 8.9 PASS** (2026-11-09 and 2026-11-10; §6). **6c complete on board 1 (2026-11-10)**: the session-cap check PASS and Step 6, with the Pi off the board; Phase 6 is complete on board 1 (§6). **Next: image 1.3 from the Pi** (`BENCH_P8_PI.md` §8.9b, the 10 s holdoff), if not yet done (§10). The owner relaxed the gate on 2026-11-09 (§8). Before that: 🔵 **2026-10-20: r5 confirm review PASS; reflash first** with `Oct 20 2026 14:59:52` (`captures/review_2026-10-19/SOL_REVIEW_r5.md`; R4-F1 closed), then **reflash and the no-Pi checks** (`BENCH_P8_PI.md` §8.0b and §8.7, the partition table) *(was: one review round for r2's confirm pass and r3, `REVIEW_BRIEF_r3.md`: r2 confirmed, r3 not passed, both findings fixed in r4)* and the session-cap check in 6c (§10). 6c Step 6 (`strobe live off`, `off`): done 2026-11-10. Earlier: **Strobe 6c on board 1** (`BENCH_P6_STROBE.md` 6c): ✅ Steps 0, A, 1, 2 (2026-10-12; Step 2 re-run on `14:43:50` after the readback fix). 🔴 **Step 3 stopped 2026-10-13: no current from gate 3 to 33 %. Cause: a cold joint on J3 pin 1, so the bank never saw VIR; re-soldered by the owner (§6). Step 2 re-run at gate 0 with the load: PASS. Then the staircase again reached 30 % with no current: **J3 pin 2 was also cold**, re-soldered. Step 2 then passed with the bank fully connected (VIR 36 V at J3). First current at gate 21 % (2026-10-13), but TP4 is not flat (~1 MHz ripple, decaying body) and ADC0's half-peak threshold mis-measures it. 🔴 **Step 3 stopped at 24 % on 2026-10-13 (3.3 A overshoot); **board-1 gate-drive rework verified 2026-10-15** (flat 0.70 A at 24 %, CR-23). ✅ **6d PASS at 24 % on 2026-10-15** (U5 ends 0.70 A at 135 µs of 1000 commanded, §6). Next: ✅ board 1 reflashed, and the dry and live re-checks passed (2026-10-16, §6); the readback fix is bench-verified. 6c Step 3 has reached 30 % (2.9 A), and ADC0 agrees with the Saleae within 0.5 %. Step 4 PASS: `strobe cal 9` solved level 439 (42.87 %, TP3 4.233 V on the DMM), confirmed at 9.02 A, and four runs agree within 0.3 %. Step 4b PASS (TDS photos and the firmware): ~12.4 V across Q9 at the start of a 9 A pulse. Step 5 PASS on the firmware's criteria: the boost goes into hiccup for ~125 ms after 50 µs and 100 µs pulses (not 20 µs), and the bank is ~600 µF (§6, CR-25). Step 5 complete: on the Saleae the gate node held within ±1 mV through the bursts, and TP4 agreed with ADC0 within ~1 % (§6).** (1) ✅ Step 0 done 2026-10-12: `id` confirmed `built : Oct  7 2026 08:07:33`, board in STANDBY; ✅ TP3 loop stability **PASS 2026-10-08** (MSO54B, §6); leads soldered per the 6c lead plan; (2) 6c Steps 1–6: refusals and arming, the first live pulse at gate 0, the 3 % staircase with ADC0 against TP4 (scope 20 MHz BW limit), **6d inside Step 3** (✅ 2026-10-15, at 0.70 A), `strobe cal 9`, bursts with the FLIR. Also: identify the parked 10 Ω board (Step −1 on boards 2 and 3). Optical acquisition-window constraint (~59.8 ms needed vs 32.768 ms ring at 500 ksps), §3.7 20-pass set and CR-18 mic impact remain open, deferred while strobe work proceeds. |

*Closure notes for Phases 1, 1b and 2 (the 2026-07-30 supply-monitor bug and fix, Q1/Q2) are
in [`PROGRESS_ARCHIVE.md`](PROGRESS_ARCHIVE.md) §0, verbatim.*

---

## 0.5 What is SETTLED vs what REPEATS — read this before planning any bench session

Work on this project splits into two kinds. Confusing them wastes bench time in both
directions: redoing settled physics, or trusting a "known" constant that is actually per-board.

| | **DESIGN VALIDATION** | **PER-BOARD** |
|---|---|---|
| asks | *how does this design behave?* | *does THIS board work, and what are ITS constants?* |
| done | **once, ever** | **every assembled board** |
| driven by | the `BENCH*.md` docs | 🔵 **`BRINGUP_NEW_BOARD.md` — that document is the driver** |
| recorded in | §6 and §8 of this file | the sign-off table in `BRINGUP_NEW_BOARD.md`, then copied to §6 |
| redo when | the design changes — a respin, or firmware that touches the mechanism | every board |

### ✅ SETTLED — established once, do not re-derive

| what | where established |
|---|---|
| RP2350 erratum **E9** does not bite on A4 silicon (Q5) | Phase 0, `pins` all read 0 |
| An RP2354 **reset is a hard power cut** to the Pi, not a reboot | Phase 1 + `PROGRESS` §2 |
| The full **Pi soft-shutdown FSM matrix**, both timeouts, RPI5_ON fallback, escape hatch | Phase 1b, simulated Pi |
| Adafruit 481 ring has **internal current limiting** — safe at 100 % | Phase 1c, 7.8 mA measured |
| **Q1 mechanism**: U9 clamp ≈ K·R·C with K ≈ 1.0, not 0.7 | Phase 2c |
| **Q2**: no duty-fidelity limit to 250 kHz — ~CLR resets C57 through a transistor, not R68 | Phase 2d |
| **Peak beam current is set by the rail/Vf/ballast, not by duty** | Phase 2b |
| **GPIO33 polarity: TRACK = 0** | TMUX1219 datasheet + 4 bench confirmations |
| **Ambient rejection ≈ 48 dB** — the synchronous-detection premise holds | Phase 3.3 |
| The "bursty noise" was **beam return off the room**, not a fault | Phase 3.3, D11 covered |
| **σ_noise is set by the optical background**, not the electronics | Phase 3.3 |
| **CR-15 mechanism is optical**, and largely the operator's hand | Phase 3.3 |
| The lock-in response is a **TRAPEZOID**; h2 ≡ 0; h3 has an intrinsic duty-dependent floor | Phase 3.4 |
| The chain is a **PURE DELAY** to ~1° across 80–200 kHz | Phase 3.4 |
| `phase_ticks` carries a **geometric duty/period term** — compare DELAY between boards, never ticks | Phase 3.4 |
| **Carrier = 104.1667 kHz, fixed for all boards** (§8) | decision 2026-08-28 |
| **Q8 cannot be measured with a beam step** — the optical term is 26× the electrical one | Phase 3.6b |

### ⚠ PER-BOARD — must be measured on every board

Driven by `BRINGUP_NEW_BOARD.md`; its sign-off table is the authoritative form.

| what | why it varies | §BRINGUP |
|---|---|---|
| Rail integrity, standby + idle current | assembly | 2 |
| **LM5157 boost SW frequency** | part tolerance; the carrier's ±8 % margin must cover it | 2 |
| E9 `pins` check | different die | 3 |
| 🔴 **ADC +5 V scale** | ADC gain + divider tolerance. **Safety gate** | 4 |
| 🔴 Latch guard + sustained supply monitor | what stops a Pi being fed through a 1 A diode | 5 |
| **U9 clamp width** | 109–136 µs band from 1 % R / 10 % C | 6 |
| Beam thermals | *this* heatsink mounting — 87.5 °C vs 98 °C across two boards | 6 |
| Static detect-chain health | assembly | 7 |
| `hpf test` polarity **and switch leakage** | leakage spans **52–250 pA** across boards | 7 |
| CR-15 baffle acceptance | optics and operator discipline | 8 |
| **`cal demod` + `cal model`** | chain delay spans **340–600 ns** board to board | 9 |
| **DAC vref + comparator Vos** | LM393 Vos is ±15 mV per part | 10 |
| **TIA variant (Rf, Cf)** if reworked | changes nearly every number downstream | 0 |

### 🔵 The rule for deciding which a new test is

**Ask what would have to change for the answer to change.** If it is *the design* — a value in
the netlist, a firmware mechanism, a physical law — it is settled once. If it is *this
assembly* — a tolerance, a mounting, an optical alignment — it repeats.

---

## 1. Hard safety rules (do not relax without a deliberate decision)

1. **A Pi 5 is seated on J8 only for Phase 8 and Phase 7c** (`BENCH_P8_PI.md`;
   `BENCH_P5_P7_MIC_CAMERA.md` 7c, owner, 2026-11-13). Relaxed from "not until
   Phase 7c" by the owner on 2026-11-09 (§8). The latch is the Pi's power switch; a firmware
   bug power-cycles it. With a Pi seated: SW2 taped over, no `force` resets, no SWD reset, the
   Pi's USB-C unplugged. Other phases: the Pi off the board unless their procedure says
   otherwise. Use flying-lead SWD only with the Pi off (§4).
2. **`PIN_PULSE_LIMIT_DIS` (GPIO27) is written 0 in exactly one place** — `safe_state()`.
   No CLI path, no config flag, until Phase 6d. It defeats the strobe hardware watchdog.
3. **Never run the +5V rail from USB power** — neither by latching on USB, nor by keeping the
   latch closed after the supply is pulled. Two independent checks now enforce this:
   `V5_MIN_FOR_LATCH` (5.05 V) at latch time, ✅ verified Phase 1 test 1; and a continuous
   `V5_MIN_SUSTAINED` (4.90 V, 500 ms debounce) monitor while latched, added 2026-07-30 after
   a bench find — see [`PROGRESS_ARCHIVE.md`](PROGRESS_ARCHIVE.md) §0. Without the second one, pulling the supply leaves a Pi 5 being fed
   through D8, a 1 A diode.
4. ~~**Phase 1b must pass before a real Pi is ever attached.**~~ ✅ **Passed 2026-07-31.**
   Superseded by: **an RP2354 reset is a hard power cut to the Pi** (§2). `reset` and
   `bootsel` are guarded in firmware; **SW2 is not, and cannot be** — that is operator
   discipline. Tape over SW2 whenever a Pi is seated.
5. ~~**Resolve the Mira220 1.8 V I/O question before Phase 7c.**~~ ✅ **ANSWERED 2026-08-14** — the
   sensor I/O is 1.8 V with no 3.3 V tolerance. Both directions fail; CR-09 is unblocked and
   red. **Still do not connect J4** until the camera board's own schematic confirms whether
   it level-shifts.
6. **No hand-held probe tips on a powered board in the fine-pitch areas** (added 2026-10-05,
   after a probe on R61 sparked on board 2 — R61's pads are 0.58–1.15 mm from +12 V and GND).
   Clips or soldered wires, attached with the board **unpowered** (PSU and USB off). See
   `HANDOFF.md` §3 rule 8 and `BENCH_P6_STROBE.md` 6a probes.

---

## 2. Verified hardware facts (already checked against the netlist — do not re-derive)

- The .md §10 pin map matches `The_One_Board_To_Rule_Them_All.net` **exactly**, all 30 signal GPIOs.
- Both netlist exports (`.net` and `(netlist).txt`) are byte-identical except the export timestamp.
- The updated `.csv` and the as-built `jlcpcb/production_files/BOM-*.csv` agree with the netlist
  and the .md. Every firmware-relevant R/C value was checked and matches. (An older stale CSV
  that disagreed has been replaced — ignore any earlier note about a BOM discrepancy.)
- QSPI SD0–3/SCLK unconnected (RP2354B internal stacked flash). `QSPI_SS` → R22 → SW1 (BOOTSEL).
- **The MCU runs whenever J1 *or* USB-C has power — the button does not power the MCU.**
  +5V_IN → NCP1117 → +3V3 is the always-on domain (RP2354, one-shot watchdogs, +3.3VA mic).
  The button asks the already-running firmware to close Q3 and raise the *switched* **+5V**
  rail (Pi, boost, beam LED, analog chain). Confirmed on hardware 2026-07-30: power on J1
  with no USB → yellow standby blink immediately, no press needed. ADC1 taps +5V_IN, which is
  upstream of the latch, so its reading is independent of latch state.
- **Silicon confirmed on-device** (`picotool info -a`, 2026-07-29): RP2350 rev **A4**, **QFN80**,
  flash **2048K**, chipid `0xa764f5332ca5ac53`. Running ARM (RISC-V also available on-die).
  SWD debug enabled — the Pi-as-probe path in §4 is not locked out.
- `SWD_34 → J8.18`, `SWCLK_33 → J8.22`, **no series resistors. There is no separate debug connector.**
- Y1 = ABM8-272-T3, C29/C30 = 15 pF → **12 MHz XOSC**.
- Both 74LVC1G123 one-shots (U5 strobe, U9 beam) are powered from **+3V3**, not 5 V.
- R45/R44 = 10K/100K → Pi 3V3 × 0.909 = 3.0 V at GPIO24 when the Pi is powered.
- **J8.17 has no copper** (DRC and fab gerbers, 2026-10-30): the netlist puts it on +3V3_Pi5 with
  J8.1, but only J8.1 is routed (to R45). Harmless with a Pi seated; not a bench 3.3 V point.
  `NEXT_BOARD_REV.md` CR-27.
- R46/R47 = 100K/100K → +5V_IN ÷ 2 at GPIO41/ADC1.
- **+2V5 is not 2.5 V.** R75/R76 = 10K/10K buffered by U11C makes it literally **+5VA / 2**.
  On the design's 5.2 V rail that is **2.59 V**, measured and confirmed at TP6/7/9/10.
  Ignore every "2.50 V" in the .md — it assumed a 5.00 V rail. See Q8 for the consequence.
- **Measured supply discriminator points:** USB-C only = **4.85 V** (host VBUS 5.2 V less
  ~0.35 V across D8/SS14); bench PSU = **5.20 V**. `V5_MIN_FOR_LATCH` = 5.05 V sits between them.
  Note USB VBUS varies by port — 4.59 V was seen on another port, so treat 4.6–4.85 V as the range.
- **The 12 V rail dominates idle current.** R15 (4K7) drops VIR 36 V → the D4 zener continuously,
  burning ~180 mW between resistor and zener to produce a ~5 mA rail. That is ~40 mA of the
  129 mA rail-up idle draw — roughly 40 %, purely to make the strobe gate-drive supply.
  Inherent to a shunt regulator and fine as designed; **first thing to revisit if idle power
  ever matters** in a future spin.
- **Adafruit 481 ring LED has internal current limiting.** Measured 7.8 mA through R48 (47 Ω)
  at 5.2 V, ring dropping ~4.84 V. Safe at 100 % duty indefinitely — no firmware cap needed.
- 🔴 **An RP2354 reset is a HARD POWER CUT to the Pi, not a reboot.** Netlist-confirmed
  (net 62 `LATCH_CONTROL` = **Q2 gate + R12 + GPIO15**, exactly three nodes): on any reset the
  GPIO15 pad reverts to high-Z, R12 pulls Q2's gate low, and the +5 V latch **opens**. The Pi
  loses power instantly — no shutdown request, no filesystem sync, no warning. This applies to
  **every** reset source: SW2, the `reset` and `bootsel` CLI commands, the hardware watchdog,
  and a brownout on +5V_IN. `main.c` already said this about the watchdog; it is true far more
  broadly.
  **This is a deliberate tradeoff, not a bug.** R12's pull-down is what makes the latch
  *fail-safe* — the rail opens if the MCU dies, which is the right call on a board that drives
  9 A strobe pulses. The cost is that a Pi can never be shut down gracefully by a reset, and
  you cannot have both without a supervisor or hold-up circuit that would weaken the fail-safe.
  **Consequence for Q10:** the spurious GPIO43 assertion during reset arrives at a Pi that is
  losing its rail in the same instant, which is why Q10 is defence-in-depth rather than a gate.
  Mitigation that exists today: the `reset`/`bootsel` CLI guard (§9).
- **RP2350 pads reset with the internal PULL-DOWN enabled, not floating.** Confirmed from the
  SDK register headers (SDK 2.3.0, `hardware/regs/pads_bank0.h`): `PADS_BANK0_GPIO43_RESET`
  = `0x116` → **PDE=1, PUE=0, OD=0, IE=0**. Every bank-0 pad is the same. So through a reset
  the *output driver* is high-Z but a ~50–80 kΩ pull-down is actively holding the pin **low**.
  This corrects the comments that used to be in `safe_state.c` and `board.h` §"Pi 5
  soft-shutdown", both of which claimed the pad simply reverts to high-Z. It matters because
  **GPIO43 = RPI5_SHUTDOWN is active-low**, so the reset default *is* the asserted level.
  See Q10 — the consequence is unmeasured, the pad behaviour is not.

---

## 3. Open questions and unverified numbers — **measure these, don't trust the .md**

| # | Item | Why it matters | Resolve in |
|---|---|---|---|
| ~~Q1~~ | ✅ **ANSWERED for U9 2026-08-13: 122.68 µs.** Neither estimate was right — K ≈ 1.0, not 0.7. **The concern was inverted:** the clamp is *above* the 100 µs software limit, so firmware controls pulse width and the .md §15 slow-ball rows need no re-derivation. `STROBE_SW_MAX_US` stays 100 µs. **Still open: U5**, same part and RC, expect 109–136 µs — verify in Phase 6a.1 and set the constant from it. *(original text)* One-shot clamp is probably ~86 µs, not 113 µs. SN74LVC1G123 t_w ≈ K·R·C, K ≈ 0.7 at 3.3 V; 56K × 2.2 nF = 86 µs. The .md's 113 µs implies K ≈ 0.92. | `STROBE_SW_MAX_US` (100 µs) may be **above** the real hardware limit → every slow-ball pulse silently truncated | Phase 2 step 4 (U9), Phase 6a (U5) |
| ~~Q2~~ | ✅ **ANSWERED 2026-08-13: no limit up to 250 kHz.** Exactly 30 % duty reproduced at TP5 at 250 kHz (4.0 µs period, 1.2 µs low). The premise was wrong: ~CLR resets C57 through an internal low-impedance transistor (~0.1–0.2 µs), not through R68's 123 µs charging RC. | — | ✅ Closed. **Carrier stays 104.167 kHz. Q3 may search freely up to 250 kHz** |
| ~~Q3~~ | 🔵 **CLOSED BY DECISION 2026-08-28, not by measurement. 104.1667 kHz is FIXED for every board and user.** The question was "which carrier is best on this board" and it is no longer being asked — uniformity is worth more than a few percent of per-board SNR. See §8 for the trade and the margin. ⚠ **What replaces it is narrower and still open:** does the fixed carrier have enough margin against the boost across the *population*? Computed margin is **−9.6 % / +7.1 %** of boost drift before folding; **the part-to-part spread of the LM5157 has never been measured on any board.** *(original text)* Best carrier frequency. 104.1667 kHz is a starting point, not an answer. Boost fsw tolerance makes paper analysis undefensible. | A folded boost harmonic looks exactly like a ball, and one bad choice is now bad on **every** board | **Measure the SW-node frequency on every board** (`BENCH.md` 0.5d), then §3.6 Check 2 |
| Q4 | **Does the Pi 5's header 3.3 V rail drop at halt?** | If not, `PI_3V3_SENSE` never fires and every shutdown hits the 60 s timeout. RPI5_ON fallback is implemented. Also decides whether `pi_down_unrequested` telemetry (§9) can rest on 3V3 or must use RPI5_ON alone. | **`BENCH_P8_PI.md` §8.2** — `sudo halt`, DMM on J8.1. 🟡 **Answered for a requested shutdown, 2026-11-09 (§6): it does NOT drop.** J8.1 held ~3.28 V for ~12 s after the Pi read down, until the latch opened. RPI5_ON does all the down detection. ✅ **Closed 2026-11-09 by §8.2:** also not after a `sudo halt` (25 s). |
| ~~Q5~~ | ~~**RP2350 erratum E9**~~ — **CLOSED 2026-07-29.** Empirical `pins` test: GPIO24/0/8/9 all read **0** floating. Silicon is revision **A4**, a later stepping than the A2 the erratum was documented against. | — | ✅ Resolved. No external pull-downs needed; R44's 100 kΩ holds GPIO24 down fine on this silicon. The `pins` reading is the authoritative evidence for this board. |
| ~~Q12~~ | ✅ **RESOLVED 2026-08-14.** The 53.6 °C D11 reading was an emissivity artifact off the **domed lens**, exactly as the physics predicted (it cannot be cooler than the 68 °C heatsink it feeds). Re-measured with the IR camera pointed **sideways at the LED base**, avoiding the lens: **104 °C at 30 % duty**. **D11 and the ballast resistors sit at the same temperature** — D11 carries 4× the power (3.23 W vs 0.81 W) through a ~4× better path (25 vs 100 K/W), and they are millimetres apart on shared copper. | — | ✅ **The 24 K/W figure in §6 is validated and does describe the LED**: it predicts 25 % → 89 °C (measured 87.5) and 30 % → 102 °C (measured 104) from one constant. **CR-12 confirmed 🔴** — junction at 30 % is **123–133 °C** against a 145 °C max. |
| ~~Q6~~ | ✅ **ANSWERED 2026-08-14 from the datasheet (DS000642 v9-00). YES, the I/O domain is 1.8 V** — VDD18 = 1.70/1.80/1.90 V, and **VIH max is specified as VDD18 itself, so there is no 3.3 V tolerance.** VOH min = 0.8·VDD18 = **1.44 V**, ceiling 1.80 V. | **BOTH directions fail and the 220 Ω resistors fix neither — they limit current, they shift no levels.** *Read:* RP2350 VIH ≈ 2.15 V > the sensor's 1.80 V ceiling → the strobe inputs can never read high → **the Phase 7 handshake cannot work.** *Write:* 3.3 V through 220 Ω injects **3.6 mA** into the sensor's clamp. Latch-up is not the risk (ISCR = ±100 mA, 27× margin), but the I/O rail draws only **0.6 mA max**, so 3.6 mA is 6× its own consumption and lifts VDD18 out of spec. **The destructive case is driving J4 with the camera unpowered** — that back-powers VDD18 through the ESD diode and violates the power-up sequence. | 🔧 **CR-09 is now unblocked and 🔴.** Needs a real translator on all three signals (TXB0104 or BSS138 discretes) plus a 1.8 V reference at J4, which the current pinout does not provide (pins 1/2/5/6 are all GND). **One unknown remains: whether the camera board already level-shifts.** J4 lands on its header, not on raw sensor pins — get that schematic. Until then **do not connect J4**, and never drive D_Cam_Trigger high with the camera unpowered. |
| ~~Q7~~ | ~~**RPI5_SHUTDOWN polarity**~~ — **RESOLVED on hardware 2026-07-31.** Active-low is correct and proven: a shutdown request produces a clean **200 ms low pulse at GPIO43**, measured during Phase 1b. | — | ✅ Firmware side closed. ✅ **Pi side closed 2026-11-09** (§8.1, §6): the overlay params are in `config.txt`, GPIO26 is held active-low by `shutdown`, and the board's 200 ms request (199.1 ms on the Saleae) halted the Pi. |
| Q10 | 🟢 **Measured 2026-07-31 — the level fails, but the impact is small. Demoted from blocker.** GPIO43's reset-default pull-down (§2: PDE=1) holds the *asserted* level from the reset edge until `safe_state_init()` runs. Measured with a 20 kΩ emulated pull-up: **2.07 V** at J8.37 against a **3.246 V** rail. `X = V·R/(3V3−V)` = **35.2 kΩ**, i.e. a **~34 kΩ pad pull-down** — stronger than the 50–80 kΩ assumed. A real Pi's ~50 kΩ pull-up would see **`V_pi` = 1.34 V**, below RP1's VIH, and even a typical 60 kΩ pull-down gives 1.81 V — so the *level* fails across the whole plausible range. | ⚠ **Corrects the 2026-07-31 write-up, which called this a blocker.** It is not. **Every reset also opens the +5 V latch** (§2), so the spurious request reaches a Pi that is losing its rail in the same instant. There is no case on this board where GPIO43 goes low but the latch holds — both pads reset together and the Pi has no other power source. The real hazard is the power cut; this is a footnote to it. | 🔧 **Optional defence-in-depth: 10 kΩ from J8.37 to +3V3** (or to **J8.1**, the Pi's own 3V3 — both are header pins, so no PCB work). Gives 2.67 V through reset and 0.35 V when firmware asserts. **Fit it if convenient; it does not gate Phase 8.** The mitigation that actually matters is the `reset`/`bootsel` CLI guard, already implemented. |
| ~~Q11~~ | **How long does a Pi 5 take to raise its header 3V3 after +5V is applied?** ✅ **Closed 2026-11-09 (§8.3, §6): 5.56 ms** to the 2.54 V crossing, two cold starts. `PI_DETECT_WINDOW_MS` (3000) stays, ~540× that. | `POWERING_ON` classifies the Pi as absent when this window expires, and in `BENCH_RUNNING` a button press is a hard `FORCE_OFF` — a power cut on a booting Pi. The late-detect promotion backstops it, but the window should be right on its own. | **`BENCH_P8_PI.md` §8.3** — scope J8.2 (+5V) against J8.1 (Pi 3V3), measure to the **2.54 V** crossing (that is 2.31 V VIH ÷ the R45/R44 0.909 divider, i.e. when firmware can actually see it). |
| ~~Q9~~ | **The +5V_IN read path is ~5.9% LOW — MECHANISM RESOLVED 2026-07-30.** True **5.200 V** at J1 reads back as **4.893 V** (code 3036). Three measurements localise it: J1 = 5.200 V, R46/R47 junction = **2.578 V**, ADC reports **2.447 V**. So the divider contributes only **−0.85%** (fine for two 1% parts) and the **ADC conversion itself contributes −5.1%**. <br>**Leakage ruled out:** µA into the pin would have dragged the junction to ~2.45 V; it sits at 2.578 V. **Reference ruled out by arithmetic:** explaining the error would need VREF = 3.477 V, above the +3V3 rail feeding ADC_AVDD — and R27's 33 Ω can only drop it *lower*, which pushes the error the other way. Residual cause is ADC gain error + incomplete S/H settling through the 50 kΩ source (RP2350 wants ≤10 kΩ). | Nearly broke Phase 1: uncalibrated, a good bench supply read as 4.89 V — below `V5_MIN_FOR_LATCH` (5.05 V) — so the firmware would refuse to latch and report `USB_POWER_ONLY`. | ✅ **Closed.** Compile-time default scale **1.063** folds in both terms; `adc5vcal` trims per-unit residue. Nothing to check on ADC ground/reference. **Next board spin:** R46/R47 = 10K/10K — see `NEXT_BOARD_REV.md` **CR-03**, which also flags that `ADC5V_SCALE_DEFAULT` (1.063) must drop to ~1.00 in the same commit or every reading goes ~6 % HIGH. |
| Q8 | 🔴 **CONFIRMED ON HARDWARE 2026-08-31 — ×7.43 measured against ×7.25 predicted. §3.6b FAILS.** 🔧 **Board fix required — `NEXT_BOARD_REV.md` CR-02** (regulate +2V5, or a unity diff amp taking TP9−TP6). **The virtual ground TRACKS the +5V rail** — R75/R76 = 10K/10K makes +2V5 literally +5VA/2, not a regulated 2.5 V. **Measured 2.59 V** at TP6/7/9/10 on a 5.2 V rail. Confirmed correct; the .md's "2.50 V" assumed a 5.00 V rail this board never runs at. | Any *step* on +5V while the HPF is in HOLD (i.e. armed) shifts the whole chain's virtual ground by ΔV/2, which passes through C81 and hits the comparator amplified **×14.5**. A 100 mV rail step → ~725 mV at ADC5, well over a typical 0.1–0.5 V threshold → **false trigger**. In TRACK mode the 0.66 s HPF removes it; in HOLD it does not. ✅ **MEASURED §3.6b Method A, board 3:** in HOLD a **+75.0 mV** rail step moves the comparator input **+556.9 mV** = **×7.43**, and it **stays**. In TRACK the same step peaks at ×3.84 and **decays away with τ ≈ 0.75 s** — the HPF works, and HOLD is the whole exposure. | ✅ **Measured. It is real and it is at the predicted magnitude.** 🔴 At a 300 mV threshold a **39 mV** rail step fires the detector; at 100 mV it takes **13 mV**. 🔴 **And a rail SAG blinds the detector outright** — U12B's output clamps **21 mV** below quiescent, so while the rail is down a ball cannot move it at all. Mitigations: keep the rail stiff while armed, shorten the armed window, stay in TRACK, or fix the board (CR-02). |
| Q13 | ✅ **Measured 2026-10-16** (Steps 4b and 5; §6): C ≈ 600 µF, a ~0.15 V step at the edge (~17 mΩ), and ~12.4 V across Q9 at the start of a 9 A pulse. **The LM5157 goes into hiccup for ~125 ms after 50 µs and 100 µs pulses at 9 A, but not after 20 µs ones** (CR-25). The 5.4 mC slow-ball burst sags ~8.8 V and leaves ~3.5 V across Q9. *Original question:* **What VIR can really deliver in a burst: the bulk capacitance, its ESR, the boost's refill current, and the headroom at 9 A (owner request, 2026-10-15).** `BURST_CHARGE_MAX_MC` (6 mC) is a sag budget, ~670 µF × ~9 V (design doc §15), and `board.h` marks it 🔴 UNVALIDATED. The bank is 2 × Rubycon 50TPV330M (330 µF, ±20 %) plus 8 × 2.2 µF 1206 X7R (small after DC bias); none of it has been measured. The model also leaves out: the ESR and harness drop at 9 A; the boost refilling during a burst (design 0.4 A, i.e. ~1.7 mC in a 4.27 ms slow-ball gap against 0.9 mC per 100 µs pulse at 9 A); and Q9's own saturation voltage. The design's "sink ≈ 1.5 V" is almost all sense resistors and Q10 (~1.4 V at 9 A). A rough square-law fit to 2026-10-15 (0.10 A at gate 21 %, 0.69 A at 24 %) gives V_th ≈ 1.95 V and ~1.2 V of overdrive at 9 A, so the sink may need ~2.5–3.5 V. The same fit predicts TP3 ≈ 4.5 V at 9 A, which `strobe cal 9` will check. | The limit decides how many pulses a slow-ball burst keeps, i.e. image coverage. The error can go either way: refill could remove the shedding at 10 m/s, and a bigger sink need shrinks the budget. **ADC0 cannot see it:** Q9 holds the current flat until the headroom is gone, so the plateau shows the failure, not the margin. There is no VIR tap for the firmware (design doc §17, item 2). Deep sag also starves +12 V (R15 from VIR), which drives Q10's gate. | **`BENCH_P6_STROBE.md` 6c Step 4b** (written 2026-10-16). 6c, after the readback fix and `strobe cal 9`, at 9 A, on the MSO54B. Channels: TP4, VIR (J3 pin 1), VIR_RTN (J3 pin 2) and U5 Q, all probes attached unpowered. 🔴 Probe grounds on the R66 GND wire only, **never on J3 pin 2**: the scope's ground is earth, and a ground there bypasses Q9 and Q10. Single 20/50/100 µs pulses: the step at the edge gives the ESR; C = I / (sag slope + recovery slope), with the boost current-limited in both; V(VIR_RTN) during the pulse, minus the sink's need, gives the margin. Then predict each Step 5 burst's sag before firing it, and compare. A new limit would come from the model with derating (±20 %, cold, ageing) and review, and may come out **lower** as well as higher. It is not a thermal limit: Q9 (FLIR, SOA) and the LED pulse ratings are separate. |

**When you measure any of these, record the number in §6 below and update `board.h`.**

---

## 4. Flashing / debugging

**USB BOOTSEL — ✅ proven working.** Hold SW1, tap SW2 (RUN), release SW1 → `RP2350` drive →
drag the UF2, or `picotool load -x build/pitrac.uf2` while it is in BOOTSEL. UF2 family is
`rp2350-arm-s`. From the 2026-10-19 image on, `picotool … -f` cannot reboot a running board (no
USB reset path; §9). *(Drive name corrected 2026-10-20: the RP2350 datasheet §5.5 names it
`RP2350`; this line said `RPI-RP2`, the RP2040's name. Owner: confirm at the next BOOTSEL.)*
From image 1.1 (2026-10-20) every reflash is a trial, and a board with the A/B partition table
puts each drag into the partition that is not running (HANDOFF §4; `BENCH_P8_PI.md` §8.7).

**SWD from a Pi 5 — not yet attempted.** Flying leads, Pi NOT seated on J8:

| Pi 5 (self-powered, off-board) | Board |
|---|---|
| GPIO24 | J8 pin 18 (SWDIO) |
| GPIO25 | J8 pin 22 (SWCLK) |
| GND | J8 pin 20 / 25 / 30 / 34 / 39 / 6 / 9 / 14 |

Because J8.2/4 (+5V) and J8.1/17 (Pi 3V3) stay unconnected, the board never powers the Pi and
the FSM correctly sees "no Pi." Gotchas: `bcm2835gpio` does **not** work on Pi 5 (RP1
southbridge) — use OpenOCD's `linuxgpiod`; gpiochip is `gpiochip4` on older kernels,
`gpiochip0` on rpi kernel ≥ 6.6.47 (check `gpioinfo`); needs OpenOCD with `target/rp2350.cfg`.
See `tools/openocd_pi5.cfg`.

**From a seated Pi, SWD defeats itself:** OpenOCD's `program` resets the target, and every
reset opens the latch under the Pi that is running it. **The boot ROM's UART loader is out of
reach:** it is a BOOTSEL mode, and BOOTSEL and RUN are not on J8 (§6, 2026-10-16). **But from
image 1.1 the firmware itself takes a new image over the UART** and installs it into the other
A/B partition with one reset, after it has shut the Pi down (§6, 2026-10-20; `pi/README.md`).
And from 1.1 `tools/flash_swd.sh` refuses by default: SWD writes over the partition table.

---

## 5. Phase checklist

- [x] **0** Toolchain, board header, blink, safe_state, CLI, `capture`, E9 check — all passed.
      *(Still open: SWD flashing via the Pi — optional, a convenience only.)*
- [x] **0.5** Dead-board rail smoke test (J2 jumper) — *before any firmware writes GPIO15*
- [x] **1** Power button + latch, ADC1 discriminator, fail-safe through reset — **all 5 tests pass**,
      including test 5 (supply pulled while latched → `SUPPLY_LOST`, latch drops). §1.3 (E9
      fallback) not needed: GPIO24 reads low correctly, `BENCH_RUNNING` as designed.
- [x] **1b** Full FSM + Pi soft-shutdown against a **simulated** Pi — **complete 2026-07-31.**
      All original rows pass (200 ms pulse, both timeouts, RPI5_ON fallback, escape hatch,
      reset invariant), **plus** the four added after the FSM changed: detect window,
      late-detect promotion, its debounce, and `fault clear` as a full acknowledgement.
      Q10's level fails but is defence-in-depth, not a gate (see §3).
- [x] **1c** Panel indicators on J7 — **complete.** §1c.2 function test passes, `panel demo`
      confirms all six ring patterns, ring current measured at 7.8 mA (safe, internally
      limited). Automatic state→pattern mapping **confirmed during 1b** (2026-07-31),
      including POWERING_ON, PI_BOOTING, SHUTTING_DOWN and the FAULT double-blink — all of
      which need a simulated Pi to reach.
- [x] **2** Beam carrier + demod phase lock — **COMPLETE 2026-08-13.** 2a all 7 checks
      (scatter 0.15 ticks); 2b ramp + thermals; **2c Q1 = 122.68 µs**; **2d Q2 = no limit
      to 250 kHz**. Carrier stays 104.167 kHz. `board.h` strobe constants 86/73 → 122/100.
- [~] **3** Photodiode: **§3.1–3.6 COMPLETE on board 3** (§3.1–3.5 also on board 2).
      **§3.6b is a design finding** (Q8 ×7.43 armed, CR-02); **§3.6 Check 2 PASS**.
      **§3.7:** detect control-path smoke test PASS and **one pilot ball** captured on board 3
      (2026-09-18, 3.102 V peak, ~22.7 ms FWHM, 198 mV ADC headroom); comparator edge timing,
      the 20-pass set and `cal gain` remain open, behind the acquisition-window constraint.
      🔵 **Per-board parts (§3.2–3.5) are driven by `BRINGUP_NEW_BOARD.md` §7–§10.**
      Results per section, with dates and links: the **Phase 3 results index** below.
      ⚠ **CR-15 stays 🟡** — the mitigation is mechanical and depends on operator discipline.
      ⚠ **The chopped calibration is over-driven by crosstalk alone** and needs optical
      attenuation over D12; it will need re-tuning on any board or geometry change.
- [~] **4** Trigger source experiment — **firmware complete** (`detect log` / `detect stats`,
      including the bias-vs-1/peak regression). Bench work not started. **No longer gated on
      CR-15** — the front end is linear to 25 % duty — but it needs §3.5 and `detect path`.
- [~] **5** Microphone. ✅ **Bring-up done on board 3 (2026-08-31)**: quiescent
      2053 codes / 1.654 V, noise σ 0.50–0.52 mV (sub-LSB), block and **triggered** `capture`.
      🔴 **No mic firmware beyond capture** — no onset detection, no veto logic.
      **CR-18 open:** a real ball impact must be captured to decide whether the 2.41 kHz
      high-pass corner discards signal.
- [x] **6** Strobe: 6a dry → 6b gate DAC → 6c LED bank ramp → 6d clamp-with-current.
      ✅ **COMPLETE on board 1 (2026-11-10):** 6c's last steps, the session-cap check and Step 6,
      passed (§6). Another board needs its own 6a–6d (U5's clamp varies ~10 %).
      ✅ **6a and 6b PASS on board 1 (2026-10-06)** — U5 clamp **135 µs**, PIO pulse/burst
      timing matches on the LA, §15 schedule and shedding confirmed, A7 independent, interlock
      both ways, gate chain 3 × 3.29 V × duty to 9.88 V, TP2 12.31 V (§6). Firmware: dry path
      written 2026-10-02 (in the 2026-10-05 build); **6c/6d live mode written 2026-10-07**
      (`Oct  7 2026 08:07:33`, 78/78 host tests — §6 2026-10-07). ✅ TP3 loop stability
      **PASS 2026-10-08** (MSO54B, §6). 6c on board 1: Steps 1-2 PASS 2026-10-12; first current
      2026-10-13; a gate-drive overshoot fixed by the CR-23 rework (verified 2026-10-15);
      ✅ **6d PASS 2026-10-15** (135 µs at 0.70 A). Then the readback fix, 6c to 9 A, 4b and 5
      (2026-10-16), and the session-cap check and Step 6 (2026-11-10).
      *(Until 2026-10-02 this line read "NO STROBE FIRMWARE EXISTS AT ALL" — see the 8/25
      audit row in the archived §6 table.)*
- [ ] **7** Cameras: 7a loopback → 7b delayed sim → 7c real (needs Pi).
      🟡 **Firmware written 2026-11-10; image 1.5 passed its review, and 1.11 carries it
      unchanged** (§6): the handshake in PIO1, `t_cam`, FIRE at max(first delay, t_cam) into a
      dry camera-gated burst, `CAM_TIMEOUT`; `cam shot` and `cam trig`. **On board 1** (owner,
      2026-11-13; no Pi, no camera): ✅ **7a PASS** (Step 3's 5000 µs repeat not run) and
      ✅ **7b PASS** (a Pico 2 running `tools/pulsegen`). The stray J4.3 edge at power-up was the Pico 2's
      (E9), not the board's (§6). 🟡 **7c:** allowed by the owner (2026-11-13; §8), the camera
      only through a TXS0108E module (CR-09). **7c.0** (the module and the strobe path, the
      camera free-running) **PASS 2026-11-18**. **7c.1 PASS 2026-11-18:** the Mira220 in slave
      mode, one frame per trigger, and the strobe following REQ EXP; `t_cam` 0.21–0.24 µs (§6).
- [ ] **8** Real Pi integration + clean-shutdown acceptance — `BENCH_P8_PI.md`.
      ✅ **The firmware for 8.0–8.5 exists and is proven** (full Phase 1b matrix). ✅ 8.6 halt
      telemetry, the serial link and updates from the Pi are in r5 (flashed 2026-10-30);
      §8.0b and §8.7 PASS (2026-11-02, 2026-11-09). ⚠ **NOT gated on the Q10 rework** — that
      was demoted to optional on 2026-07-31. **The gate "phases 2–7 first" was relaxed by the
      owner on 2026-11-09** for 8.0–8.5, 8.8 and 8.9 (§8). Closes Q4, Q7 (Pi side) and Q11.
      ✅ Board 1: first seating and 8.1–8.6 (2026-11-09), 8.8 and 8.9 (2026-11-10), 8.9b and
      8.10 (2026-11-12).

**Phase 3 results index.** Procedures are in `BENCH_P3_DETECT.md`; the dated narratives that used
to sit inside them are in `PROGRESS_ARCHIVE.md` → "Results moved from BENCH_P3_DETECT.md" (moved
verbatim 2026-10-05), and board 3's values in force are in `cfg` and §0.

| § | what | board | date | verdict | detail |
|---|---|---|---|---|---|
| 3.3 | beam on, CR-15 baffle sweep | 2 | 2026-08-19 | ✅ linear to 25 % duty | archive §3.3 |
| 3.3 | ambient rejection, 120 Hz | 2 | 2026-08-21 | ✅ ≈ 250× (≈ 48 dB) | archive §3.3 |
| 3.3 | "bursty noise" | 2 | 2026-08-21 | ✅ resolved — beam returning off the room | archive §3.3 |
| 3.4 | demod phase | 2 | 2026-08-24 | ✅ 1348 ticks at 104166 Hz | archive §3.4 |
| 3.4 | demod phase | 3 | — | ✅ 1311 ticks (saved, slot A seq 5) | `cfg` |
| 3.5 | threshold DAC ↔ comparator | 2 / 3 | 2026-08-25 / — | ✅ vref 3.268 / 3.256 V, Vos +11.0 / +5.4 mV | archive §3.5 |
| 3.6b | Q8 rail step | 3 | 2026-08-31 | 🔴 design finding: ×7.43 armed (CR-02) | archive §3.6b |
| 3.6 | carrier robustness, Check 2 | 3 | 2026-08-31 | ✅ PASS, σ_noise flat 4.57–5.08 | archive §3.6 |
| 3.7 | detect control path | 3 | 2026-09-18 | ✅ arm/disarm only | §6 2026-09-18 |
| 3.7 | pilot ball | 3 | 2026-09-18 | ✅ 3.102 V peak, ~22.7 ms FWHM, 0.198 V headroom | §6 2026-09-18 |
| 3.7 | 20-pass set, comparator timing, `cal gain` | 3 | — | ⏸ pending (acquisition window) | — |

---

## 6. Measurement log — **fill this in as you go**

**Entries 2026-07-29 → 2026-08-31 are in [`PROGRESS_ARCHIVE.md`](PROGRESS_ARCHIVE.md) §6**
(moved verbatim 2026-10-05): the measurement table — die revision, E9, rails, the 32 mA /
129 mA current baselines, +5V_IN 4.85 V USB / 5.20 V PSU, board-3 leakage, Q8 ×7.43, HPF τ,
switcher 801 kHz, mic quiescent/noise, the Phase 3 calibration rows — and the GPIO43
reset-test correction. **Those measurements are still valid**; they moved only to keep this
file short. New entries go below, newest last.

### 2026-09-18 - Board 3 detect control-path smoke test

**Owner confirmed no intervening bench work since 2026-08-31.** This result supersedes
the 2026-09-17 handoff's "detect never run on board 3"; it does not close §3.7.

| Evidence | Result |
|---|---|
| Commanded state | `detect disarm`, `beam off`, `on`, `hpf track`, `adcmode idle`; no ball stimulus |
| Power/status readback | `BENCH_RUNNING`, fault `none`, **5.207 V** firmware reading, latch `1`, railsready `1`, Pi absent, watchdog `off` |
| ADC | Mode **1 (idle)**, ring `RUNNING`; the held +5V_IN sample was only **1 ms** old, not a stopped ring |
| Config readback | **slot A, seq 5, v1**; carrier **104166 Hz**, phase **1311 ticks**, ADC5V scale **1.0620**, gain **14.5**, coalesce **2000 us**, path **0.00 mm**, threshold **level 0** |
| Calibration readback | HPF **TRACK = 0** confirmed; phase model fitted/pure delay **80-200 kHz**, calibration duty **25.00 %** |
| Firmware evidence | `help` includes `capture trig`; `stat` shows watchdog `off`. Consistent with the expected image, but no build ID or binary identity was captured |
| Arm/disarm | `detect arm` acknowledged, status **ARMED**; `detect disarm` acknowledged, status **disarmed**. **PIO2, GPIOBASE 16, SM0** reported throughout |
| Comparator/data | GPIO46 **1 (ABOVE threshold)** throughout; **0 passes, 0 raw FIFO words**, no last pass |

**PASS: command/control-path smoke test only.** Bare `detect` is a status read, not an
arm command. At a zero threshold, a high comparator reading is consistent with the
quiescent signal/offset. `detect.pio` first waits for LOW, then a complete HIGH pulse:
this transcript contains no evidence of that sequence. It proves neither edge timing
nor optical detection. The board was left **disarmed, beam OFF, HPF TRACK, ADC idle**;
no shutdown or config save was commanded.

**Next gate:** establish the final ball-path geometry and a usable threshold above the
quiescent background but below the measured ball signal. Confirm GPIO46 is LOW at rest.
Do not use the saved zero threshold or an unmeasured `detect path 20` as a transit setup.
Keep the rail/load steady while in HOLD (CR-02); the scope-ground issue is still open.

**Related text audit:** the transcript exposed stale `help` guidance (the old 30 % duty
example, "sense is unverified" despite saved HPF confirmation, literal `25%%`, and
carrier-ranking language). Corrected those, documented arm-only counter reset and
unchanged HPF, updated `id` for existing mic capture, removed a board-specific rail
measurement from `threshold` output, and corrected the phase-model persistence comment.
Only text/comments changed; detection, safety, sampling and config logic are unchanged.
**Initial build blocker (historical; recovery and flash confirmation below):** CMake Tools twice returned **"Unable to configure the
project"**, with no CMake diagnostics or available targets. The private CMake/Ninja/ARM
tools exist; adding their paths and the existing build directory to the ignored local
`.vscode/settings.json` did not resolve the failure. **At that point the UF2 on disk still
dated from 2026-08-31 and did not contain these corrections.** Source diagnostics, help-text
assertions and `git diff --check` pass. Encoding audit found no BOMs; existing non-ASCII
in firmware source is confined to deliberate comment emoji, and the netlist-report tool
has intentional Unicode input/output. No new non-ASCII source text was added.

The §3.7 audit also corrected "HOLD is safe for as long as you need": the leakage budget
does not bound rail-step errors, and board 2's drift measurement must not be presented as
board 3's. This is a documentation correction, not a new measurement.

**Build-blocker follow-up, same day:** the owner's CMake Tools log reports `EEXIST` while
creating `build/.cmake/api`. That path is a normal directory with owner full control,
not a stray file; a complete file-API reply and Ninja build files were generated at
10:51. Selecting `[Unspecified]` also raised the same error, so kit selection did not
resolve it. With no CMake/Ninja process running, temporarily renamed **only** `.cmake`
to preserve it and retried: the extension returned the same exception without recreating
the directory. Restored the original metadata; no build data was deleted and no temporary
backup remains. This suggests retained extension/driver error state, not a demonstrated
firmware or compiler failure. **The next recovery step was to reload VS Code and retry.**
The precise initiating cause is still unproven.

**Recovery, intermediate Debug build (superseded by Release below):** the owner reported
a successful build after **Developer: Reload Window**.
Host verification found fresh UF2/ELF/BIN at **11:07:46-47**, embedded build stamp
**Sep 18 2026 11:07:41**, all corrected CLI strings, and none of the four superseded help
strings. The UF2 has valid block magic and family **0xE48BFF59**; ELF is ARM.
However, CMake Tools selected **Debug (`-Og -g`)**, replacing the previous **Release
(`-g -O3 -DNDEBUG`)** configuration. Debug size is **137600 text / 0 data / 88304 bss**.
**The intermediate Debug build was not approved for flashing; Release was required to
preserve the established bench configuration.** The editor tool connection disconnected on reload; host artifact
checks remain available. Build success is owner-reported, not an independently captured
full build log.

**Host-check error corrected:** my first UF2 check falsely reported an invalid first block
because PowerShell interpreted `0x9E5D5157` as signed while the file read was unsigned.
Explicit unsigned conversion makes every block pass. That was a validation-script error,
not a corrupt UF2.

**Final Release verification - PASS (flash was pending here; confirmed below):** selecting Release first regenerated
the cache/Ninja files but left the old Debug UF2 in place. A subsequent **CMake: Build**
performed all **106** build steps and finished with **exit code 0** in **15.771 s**, with
no warnings in the owner's supplied log. Verified the **artifact**, not just the cache:
`picotool info -a build/pitrac.uf2` reports **Release**, SDK **2.3.0**, board
**pitrac_ltb_v1**, family **rp2350-arm-s**, target **RP2350**, image **ARM Secure**.
UF2 timestamp **2026-09-18 11:12:05**; embedded CLI `built` stamp
**Sep 18 2026 11:11:51**. Size **144768 text / 0 data / 88308 bss**: text +632 bytes,
BSS unchanged from the previous Release build. Corrected help/identity strings are
present in the linked image; the four checked stale help strings are absent.

UF2 SHA-256: `64E9632B3AF97508CF0B6ACD66F6E49E3D74C902DF5CB3AC6BC192633E14BDE0`.
The preserved file-API metadata was restored; no temporary backup remains.
**At this checkpoint flash/readback was still pending.** The planned checks were `id` for the stamp above and
board UID **6d6fda754e367a40**, and `cfg` for **slot A seq 5**, carrier **104166 Hz**,
phase **1311 ticks**, **TRACK = 0**, cal duty **25.00 %**.

### 2026-09-18 - Post-flash PASS; preparing the first physical ball setup

**Owner serial transcript confirms the new image is running on board 3:**

| Evidence | Readback |
|---|---|
| Identity | `pitrac phases 0-4 + mic capture (block/triggered)`, built **Sep 18 2026 11:11:51**, UID **6d6fda754e367a40**, sysclk **150000000 Hz** |
| Saved config | **slot A seq 5 v1**, carrier **104166 Hz**, phase **1311 ticks**, cal duty **25.00 %**, pure-delay model **80-200 kHz**, saved **TRACK = 0** |
| Other saved values | Threshold **0**, coalesce **2000 us**, ADC5V scale **1.0620**, gain **14.5**, path **0.00 mm** |
| Power | **BENCH_RUNNING**, firmware supply reading **5.207 V**, latch **1**, railsready **1**, fault **none**, Pi absent, watchdog **off** |
| ADC | **idle (mode 1)**, ring RUNNING, supply sample **1 ms** old |
| Last commands | `detect disarm`, `beam off`; both acknowledged |
| Physical optics | Owner confirmed **lenses fitted, no enclosure** |

**PASS: post-flash identity, retained configuration and powered idle health.** Saved HPF
polarity is not a live-mode readback; this transcript did not include bare `hpf`, so the
next powered procedure explicitly sets TRACK. No new calibration/config save, comparator
edge proof or ball transit is implied by this result.

**Initial proposed procedure (physical layout superseded by the extrusion choice below):**
power down for fixture work;
rigid lens/board mount; perpendicular flat rolling lane; marked crossing C; initial gentle
100 mm release height; smooth transition and remote catch; three unpowered dry rolls.
Measure the lens-to-ball range, ball-centre height/drop, lane/guide geometry and lighting,
and take top/side photos. About 400 mm range is only a provisional layout based on the
previous calibration-card plane, **not a proven ball-detection range**. Keep lenses/focus
unchanged. Then take a no-ball **ADC5 100 ksps / 100 ms** raw record in the final scene,
with beam **104.1667 kHz / 25 %**, warmed at least **5 min**, HPF TRACK, detect disarmed.
Do not arm at the saved zero threshold or enter an invented `detect path`.

**Corrections to my earlier guidance and the bench document:**

- I called the ramp a known/independent speed reference. That was too strong. The
  `sqrt(2*g*h*5/7)` values assume a lossless solid sphere rolling without slip; a golf ball,
  real ramp/transition and guide contacts do not establish few-percent speed accuracy.
  Use height for repeatable release, and an independently timed local speed with uncertainty
  if validating accuracy. The old few-percent ramp-speed exit criterion is superseded.
- I implied setting `detect path` would enable a speed readout. **The current CLI does
  not report velocity.** Code-intelligence references for `detect_path_mm` lead only to
  config snapshot/status uses, and the transit CLI emits durations. The field is stored;
  the sequencer's analysis stage is still a stub. This is not a gate to recording a
  waveform. Board-to-ball range is not the travelled distance between waveform crossings.
- The ordinary ADC ring holds **32.768 ms**, but that is not the available pulse-width
  budget. In armed mode `refine()` needs approximately **age + 2.5*T + 1.024 ms** of
  history for one fragment. At the saved **2 ms** coalesce delay, **T < about 11.9 ms**
  is needed before allowing for service latency; chatter needs more guard. The older
  source comments' "about 1.4 m/s" shortcut does not include this indexing budget or a
  measured optical response width. Slow ramp passes can set **WINCLIP (0x04)**. Measure
  a long pilot waveform first: single-channel **100 ksps** triggered capture can hold
  **163.84 ms** (25 % pre = **40.96 ms**, post = **122.88 ms**), or use the external scope.
  Do not raise the ramp to an unsafe height just to fit the normal ring.

**Printed-output/comment audit, no firmware edits:** current `capture trig` always says
"Make the sound", even for ADC5; the revised optical procedure explains that this means
release the ball, not clap. `level`'s 50-70 % target is a calibration-target criterion,
not a required ball amplitude. The path/speed and ring-speed comments above are known
overstatements, now explicitly qualified in the bench instructions; do not change code
or demand another reflash just to build the physical fixture. First review deliverables
are dimensions/photos and a raw no-ball baseline; real-transit measurements remain pending.

### 2026-09-18 - Extrusion ramp selected; serial logging setup

The owner will use a **grooved aluminum extrusion** rather than the proposed flat-lane
cardboard ramp. The ball crosses the optics **on the slope**:

| Owner-provided dimension | Meaning / status |
|---|---|
| **295 mm** above mat | Release location, **top surface of extrusion / bottom of ball**, clarified by owner |
| **85 mm** above mat | Approximate beam-crossing location, same surface/bottom-of-ball reference, **not ball centre** |
| **740 mm downrange** | Release to where extrusion touches mat; horizontal versus along-ramp reference not yet established |
| **210 mm inferred drop to crossing** | Difference of surface heights; also centre drop only if straight uniform groove and unchanged seating offset |

No incline angle, lens-to-ball distance, actual seated centre height, groove contact
geometry, arrival speed, dry-roll result or baseline capture has yet been supplied.
The 1.715 m/s lossless flat-surface solid-sphere value for 210 mm is **not a prediction
to trust for this groove**. Grooves change the rotation/contact geometry; use the fixture
for repeatability and measure speed independently if needed. Aim at the actual ball
centre, not the 85 mm-high aluminum surface. Secure the metal away from the PCB/D12 and
retain it in the no-ball scene so its reflected background is represented in the baseline.
Updated the §3.7 procedure to distinguish this adopted fixture from the earlier alternative.

**Serial truncation:** the owner reports the first output disappearing before copying.
Inspected the installed **Microsoft Serial Monitor 0.13.1**: its default scrollback is
**9000 lines**, below the requested 10000 ADC samples plus header. Verified built-in
**Toggle File Logging**, **Open the last used log file**, and
`vscode-serial-monitor.logFileDirectory`. The logging toggle is disabled while monitoring
or without a directory. Added the existing, gitignored `captures/` directory to the
ignored local workspace setting. **This configures a destination; it does not enable
logging or prove a file was captured.** Owner should stop monitoring, enable file logging,
restart, test with `id`, stop/open the log, then restart for the baseline.
Do not rely on scrollback or assume already-discarded output can be recovered.

The existing `scope.py --csv --no-plot` path was inspected as an alternative; a `--help`
check failed because **pyserial is absent in the selected Python 3.14.6 repo venv**.
No package installed or Python code changed: the built-in serial logger needs neither.
No firmware changes/reflash. At this checkpoint serial logging and all physical-transit
results were still unverified; the short logging test is confirmed below.

**Serial file-logging test - PASS:** inspected
`captures/COM11_2026_09_18.14.43.36.091.txt` (14 lines). It contains the complete
`detect disarm` / `beam off` acknowledgements and `id` response through the final prompt:
build **Sep 18 2026 11:11:51**, UID **6d6fda754e367a40**, sysclk **150000000 Hz**.
The owner stopped monitoring after saving the log. This proves the short file-logging
path, **not yet a 10000-sample capture**. No `stat` or ADC data in this file; it does not
establish a new rail/HPF state or a physical-transit result. Last commanded beam is OFF
and detection disarmed; stopping the host monitor is not an `off` command.
**Next at that checkpoint (superseded by the later capture below):** restart monitoring with file logging enabled, complete the safe fixture/optical
setup, warm at 25 % with HPF TRACK, and save the final-scene no-ball ADC5 baseline.

### 2026-09-18 - Complete no-ball ADC5 capture; serial logging proven at full length

**Correction to my response:** I inspected only the earlier 14:43 short logging-test file,
called the baseline pending, and sent the owner the procedure again. The later
`captures/COM11_2026_09_18.14.44.19.918.txt` already held the completed procedure and
capture. **That was my file-selection/status error, not missing bench work.** Check the
latest supplied file and adjacent newer captures before asking for a repeated measurement.

**Validated the entire later file:** exactly **10000 integer ADC5 samples**, all in
0-4095, one capture header, `# columns: ch5`, and the closing `# end` plus prompt.
Header: `# capture mask=0x20 n=10000 rate=100000 overran=0`.
**PASS: full-length logging/capture integrity**, 100 ksps, **100 ms**, 10 us/sample.

| Measurement | Result |
|---|---|
| Commanded/read-back optics | **104166 Hz**, TOP **1439**, **25.00 %** commanded/effective, phase **1311 ticks**, PWM hardware checks OK |
| HPF and ADC before capture | Live **TRACK (GPIO33 = 0)**, ADC **idle**, ring RUNNING |
| Chopped `level` | Rose while settling, ended at **2254 codes / 55 %**, GOOD. This is chopped-scene response, not the steady baseline voltage |
| Comparator | Threshold **level 0**, GPIO46 **HIGH**; no working ball threshold established |
| Pre-capture power | **5.178 V**, BENCH_RUNNING, latch/railsready **1**, fault none, Pi absent, watchdog off |
| Mean baseline | **16.6441 codes = 13.4128 mV** |
| Population standard deviation | **4.5435 codes = 3.6615 mV** |
| Minimum / maximum | **9 / 31 codes = 7.2527 / 24.9817 mV** |
| Peak-to-peak | **22 codes = 17.7289 mV** |
| Clipping checks | **0 samples <= 1**, **0 samples >= 4080**; no clipping observed in this window |
| Mean over each 10 ms block | **12.94-14.07 mV**; no large baseline step in the 100 ms record |

Voltages use nominal **3.3 V / 4095** ADC conversion, not the separate ADC5V supply scale.
This is measured variation in this scene, not an isolated electronics-noise specification
or proof against rare false triggers.

**Thermal qualification:** the first status (before beam setup) reports BENCH_RUNNING
**785367 ms**, and the pre-capture status reports **869396 ms**, an interval of **84.029 s**.
Beam ramp and `level` are between them. The record therefore does **not establish the
required 300 s steady-beam warm-up**. Keep this useful baseline; do not describe it as a
thermally qualified final measurement or repeat the whole procedure merely because I
opened the wrong file. Establish the steady-beam warm-up before the next pilot record.

**Last recorded state:** beam **ON at 25 %**, rail **UP**, HPF TRACK, detector last commanded
disarmed. Capture restores ADC idle in firmware. There is **no post-capture `stat`, `beam off`
or `off` in this file**; stopping the monitor is not evidence of shutdown. No ball
transit, pilot width, or comparator-edge proof yet. No firmware changes.

**Next prescribed experiment at that checkpoint (completed below):** one no-ball HOLD trigger control, then
one ball pilot if the control stays quiet. **`capture trig 5 64 100000 25 10`**:
ADC5, **64 codes = 51.6 mV deviation** from a newly measured baseline, **100 ksps**,
**25 % pre-trigger**, **10 s timeout**. About **14.1 times measured TRACK sigma**, with
**163.84 ms** total history (**40.96 ms pre / 122.88 ms post**). This is a conservative
starting trigger, not the eventual comparator threshold or a validated detection limit.
The trigger is either-polarity; require a ball-correlated positive waveform in review.
HOLD gets a separate quiet control because the measured 100 ms TRACK baseline does not
bound rail disturbances or long-term drift while armed.

Use the existing steady beam if it has remained on; establish **at least 5 min** total
warm-up without needlessly restarting it. Detector stays disarmed. TRACK for **at least
5 s** before each trial; HOLD immediately before the capture. No-ball **NO TRIGGER**
after 10 s is the desired control result; an unsolicited capture means stop and review.
For the ball trial, stage it at the existing **295 mm** release-surface mark outside the
view before settling, wait about **1 s** after `armed`, then release once without pushing.
After prompt/#end return to TRACK/idle, save `stat`, and power down if pausing for review.
Exact commands and output checks are in BENCH_P3_DETECT §3.7 step 1d. No new firmware,
config save or physical result is implied by these instructions.

### 2026-09-18 - First real optical ball capture PASS; power-request bug exposed

Newest file: `captures/COM11_2026_09_18.14.54.08.428.txt`, not the still-tagged 14:44
baseline. The owner confirmed **one clean ball crossing, no hand in view**, with the
capture starting after release. Same grooved aluminum extrusion fixture.

**Control:** initial typo `capture triv ...` returned `ERR: capture failed`; corrected
`capture trig 5 64 100000 25 10` then returned **NO TRIGGER in 10 s**, baseline
**136 codes / 109.60 mV**. This passes the prescribed no-ball HOLD control, not a
long-duration false-alarm qualification. The typo was not a successful acquisition.

**Ball capture integrity PASS:** exactly **16384 samples**, one header and `# end`,
`mask=0x20`, `rate=100000`, `overran=0`, `trig=4096`, `base=96`.
That is **163.84 ms**, **40.96 ms pre-trigger / 122.88 ms post-trigger**.
Beam **104166 Hz / 25 % / phase 1311**, HPF **HOLD**, detector disarmed. Initial supply
**5.179 V**; after capture/restoring TRACK/idle, **5.191 V**, no fault, watchdog off.

| Pilot measurement | Result / method |
|---|---|
| Waveform | Single smooth positive bump, both edges and return toward baseline captured; owner confirms a clean ball transit |
| Raw peak | **3849 codes = 3.10176 V**, at **+30.25 ms** relative to ADC trigger |
| Local pre-event baseline | **115.332 codes = 92.942 mV**, mean over **-40 to -10 ms** |
| Peak above that baseline | **3.00882 V** |
| Tail baseline | **109.4265 codes = 88.183 mV**, mean over **+100 to +120 ms** |
| Half-height duration | **~22.7 ms**; 0.2 ms box-averaged waveform, linearly interpolated crossings at **+19.754 / +42.463 ms**, using the local pre-event baseline |
| ADC headroom | **246 codes = 198.24 mV**, **6.01 %** of nominal full scale remains |
| Clipping checks | **0 samples >= 4080**, **0 samples <= 1**; no ADC rail clipping observed, not a proof of every upstream stage's linearity |

Voltages use nominal **3.3 V / 4095**. The arm-time `base=96` (**77.36 mV**) is not the
same as the later pre-event mean. Nor are the HOLD baselines interchangeable with the
earlier **13.41 mV TRACK** mean. Do not attribute those differences to one cause without
elapsed HOLD time and rail/optical evidence. A plot and reproducible standard-library
analysis are retained as session artifacts; no Python packages were installed.

**Pilot PASS is not full §3.7 closure.** There is no comparator timing/chatter result
because the PIO detector stayed disarmed, no velocity reference, and no repeatability
distribution. The baseline-to-pilot status ages differ by **483.281 s**, consistent with
the requested warm-up if the beam stayed on; uninterrupted beam-on time is not directly
timestamped in the log.

**Normal refinement does not fit this pulse at half-height:** with comparator width
T approximately **22.709 ms**, the current single-fragment indexing needs
**2 + 2.5*T + 1.024 = 59.80 ms**, against **32.768 ms** available. Lower comparator
thresholds would normally lengthen the pulse further. Do not proceed to a normal
20-pass `detect`/ADC-bias dataset and mistake WINCLIP for an optical failure.
Continue with long captures for amplitude/repeatability, or change the acquisition
approach deliberately after resolving the power issue. Do not move the ramp or narrow
the pulse by threshold alone merely to hide the buffer limit.

**Shutdown anomaly and confirmed present state:**

1. The log ends with `beam off`, `off` -> `requested shutdown`, but then
   **POWERING_ON (1486 ms), latch 1, railsready 0**. This was not successful shutdown.
2. Asked the owner to inspect current status before touching the fixture. The board
   had returned to **BENCH_RUNNING (99516 ms), latch 1**.
3. A second `off` then produced **STANDBY (11766 ms), latch 0, railsready 0,
   fault none, watchdog off**. Supply input still reads **5.209 V**; that upstream
   voltage does not mean the switched rail is enabled. Beam was explicitly turned off.
   **This is the current board state.** D12/VIR discharge has not been measured.

**Source-confirmed mechanism matching the bench sequence:** `power_request_on()` sets
`s_req_on = true` regardless of current state. `power_fsm_step()` consumes/clears it only
in **STANDBY**. The earlier 14:44 log issued `on` while already BENCH_RUNNING, so the
request remained pending until `off` took the FSM through FORCE_OFF back to STANDBY.
The stale request then closed the latch again; the second `off` succeeded because that
one pending request had been consumed. Code-intelligence references confirm the CLI is
the only caller besides the declaration/definition. **This is not a debounce or Pi
shutdown delay.** It is an untested stale-command sequence despite the older Phase 1
matrix passing.

**My procedural error:** I used `on` as though it were idempotent in the setup recipes;
that exposed a pre-existing firmware defect. Until fixed, send `on` **only from confirmed
STANDBY**, never to reconfirm an already-running rail, and verify actual latch 0 after
shutdown. The CLI's "requested shutdown" is an acknowledgement, not proof of rail-down.
The source header's request-state contract was not met for stale `on`. No source change
or reflash was made during that analysis; the subsequent fix/host verification is below.

### 2026-09-18 - Stale power requests fixed in source; reflash/bench verification pending

**User requested the fix. Implemented in `power_fsm.c/.h` and the CLI:**

- `power_request_on()` now returns acceptance status and queues only from **STANDBY**,
  with no shutdown/force-off pending. Repeated `on` while already powered, booting,
  shutting down or faulted is **refused**, not remembered for the next cycle.
- `power_request_shutdown()` cancels a pending start. In STANDBY/FORCE_OFF it leaves
  no stale shutdown that could stop the next intentional start. **Shutdown requested
  during startup/PI_BOOTING still waits for the existing normal shutdown path.**
- Force-off requests cancel pending starts/shutdowns; **all request flags are cleared
  in FORCE_OFF**, including teardown reached by button, supply failure or fault recovery.
- CLI prints **`REFUSED: 'on' needs STANDBY with no pending stop (state ...).`** and
  **`No power-on request was queued.`** for rejected starts. Its help text describes the
  new admission rule. Valid STANDBY starts still print `requested on` and obey the USB guard.

**Unchanged:** GPIO27 policy, all supply thresholds/debounce periods, Pi detect/boot/
shutdown timing, beam/detect/ADC behavior, phase/calibration/config record and flash layout.
Updated the related power header contract and corrected teardown comments referring to
30 % beam operation and `hpf track` rather than the actual high-level `hpf hold` case.

**Regression proof against the actual C producer:** added standalone native
`tests/CMakeLists.txt` and `tests/power_fsm_test.c`, compiling **`src/power_fsm.c`** with
real board constants and small mocked GPIO/time/ADC/beam/fault interfaces. No test
framework, package or SDK dependency was added to the host test project; it is not
linked into the firmware.

1. Built/ran the initial **17 tests before changing production code**: **13 FAIL / 4 PASS**.
   Failures reproduced the observed restart to POWERING_ON/latch 1, stale standby `off`,
   pending-request collisions, and restart after button/supply/fault teardown.
   Existing startup-shutdown handling, USB guard and stale fault-ack checks passed.
2. Applied the fix and added API acceptance plus Pi timeout coverage: **20/20 PASS** via
   CMake Tools/CTest, native **Release**, warnings treated as errors. Tests verify no
   new latch rise during 6 s after shutdown, fresh starts still work, 200 ms Pi shutdown
   pulse and 15 s floor remain, startup shutdown remains pending correctly, 90 s boot/
   60 s shutdown timeout behavior is unchanged, supply guards/debounce remain, and no
   GPIO27 writes occur. These are host logic tests, **not physical rail measurements**.
3. Restored the local CMake Tools source/build directory and Ninja generator to the Pico
   firmware after the native test run. **Release ARM build passed, no warnings/errors.**
   Editor diagnostics are clear for changed C/header files.

**Verified artifact:** `build/pitrac.uf2`, timestamp **2026-09-18 15:21:20**, CLI build
stamp **Sep 18 2026 15:21:18**. `picotool` confirms **Release**, SDK **2.3.0**,
**pitrac_ltb_v1**, family **rp2350-arm-s**, target **RP2350**, **ARM Secure**.
Size **145152 text / 0 data / 88308 bss** (text +384 bytes, BSS unchanged from 11:11:51).
The linked image contains the new refusal/help strings. UF2 SHA-256:
`B05C1624BB4186E7E52DB7DB3DFE80C97DF4976BB438E92E203631E4F2962745`.

**Reflash first.** Nothing has been flashed by the assistant. The board's last confirmed
state remains **STANDBY / latch 0**, on the **11:11:51** image. After flash, verify `id`,
UID **6d6fda754e367a40**, and `cfg` **slot A seq 5**, carrier **104166 Hz**,
phase **1311 ticks**, cal duty **25.00 %**, HPF polarity **TRACK = 0**.
Then perform **BENCH.md Test 6**: beam OFF, ADC idle, HPF TRACK whenever rail up,
no Pi/camera, supply 5.20 V. Do not mark the hardware regression passed until one `off`
after redundant `on` stays down and subsequent intentional starts still work.
No commits; unrelated Software changes left untouched.

### 2026-09-28 - Power-request fix flashed; BENCH.md Test 6 PASS (owner-reported)

The owner reported on 2026-09-28 that the power-request fix above was flashed and that
**BENCH.md Test 6 passed**. The console log and readings were not added to this repo; if
they turn up, record the `id` stamp (expected **Sep 18 2026 15:21:18**) and the Test 6
rows here. **The stale-`on` regression is closed.** The old-image workaround (`on` only
from confirmed STANDBY) is no longer needed on a reflashed board, but checking `stat` for
**STANDBY / latch 0** after a shutdown is still good practice.

Same day, documentation and comment-only corrections (no firmware behaviour change, no
reflash needed):
- `DEVELOPER_GUIDE.md` added at the repo root as the entry point for new developers.
- Stale comments fixed in `beam.h`, `panel.h`, `detect.h`, `safe_state.c`, `board.h`
  and `boards/pitrac_ltb_v1.h` (the last also lost a BOM and mojibake).
- `tools/netlist_report.py` now works on case-sensitive filesystems and dates the report
  from the netlist's own export stamp, so `--check` passes on a fresh clone.
- `LICENSES/CERN-OHL-S-2.0.txt` added (LICENSING.md referred to it; it was missing).

The full list is in `DEVELOPER_GUIDE.md` Appendix D.

### 2026-10-02 - Strobe 6a/6b dry-test firmware written; reflash + bench pending

**Owner request:** "Is the firmware ready for 6a and 6b? If not, build it." It was not:
`strobe_burst.pio` was assembled but never loaded, and there was no pulse command, gate
DAC, schedule, charge interlock or A7 fix.

**What was built** (`src/strobe.[ch]`, `src/strobe_plan.[ch]`, CLI `strobe`):

| Command | What it does |
|---|---|
| `strobe` | Status: rail, GPIO27 (read only), A7, gate DAC commanded + hardware readback, engine, limits, last run, whether a pulse / a gate raise would be admitted |
| `strobe pulse <us>` | One pulse, 5–100 µs, PIO0 SM0, DMA-fed, completion on IRQ0 |
| `strobe burst <w> <gap> <n>` | Uniform burst; gap 150–50000 µs; ≤ 16 pulses; burst charge ≤ 6.0 mC at the 9 A design current |
| `strobe clamptest <us>` | One pulse 101–2000 µs, past the software limit, for the 6a.1 U5 measurement |
| `strobe sched <m/s> [fire]` | The design's §13.4 schedule; `fire` runs it dry |
| `strobe gate <pct>` | Gate DAC (GPIO28, PWM 6A, 146.5 kHz, 20 ms settle); prints nominal TP3 |
| `strobe off` | GPIO25 SIO low, engine and DMA stopped, gate to zero; pulses refused for the next 20 ms while Q9's gate decays |

**The safety design, stated so it is not re-derived:**
- **Mutual exclusion — this build cannot command strobe current.** A pulse is admitted
  only while the gate DAC is provably zero (commanded level, PWM compare readback and pin
  function) **and at least 20 ms after it was last lowered** (Q9's gate decays through the
  RC ladder; see the review finding below). The gate rises only while no pulse can start
  (engine idle, GPIO25 low). Lowering to zero is always admitted. 6c must relax this
  deliberately, with tests.
- Every pulse also needs: rail ready (U8 and the gate amp run from VIR-derived +12 V), no
  latched fault, no Pi, **GPIO27 provably SIO output driven 0 (read only; still written in
  exactly one place)**, GPIO12 not on PWM.
- **GPIO25 stays SIO driven low between bursts.** It is handed to PIO only for an admitted
  burst, after the SM's output latch is set low and its direction set; every exit
  (completion, timeout, abort, rail-down) returns it to SIO low first. The PIO init
  helper used to set the pin direction before the level and claim the pad at boot; fixed.
- `power_fsm.c` calls `strobe_safe_off()` at the start of an orderly shutdown and in
  `FORCE_OFF` before the latch opens, because U5 is on always-on +3V3 while U8 loses its
  supply. The gate is therefore zero on every power-up.
- **A7 fixed:** ready LED is SIO on/off (≥ 50 % lights it); `strobe.c` owns slice 6;
  `strobe_init()` panics if GPIO12 is found on PWM, and every gate raise re-checks it.
- The MCU watchdog is **not** armed by this build: it cannot command current. 6c arms it.

**Netlist facts used** (re-extracted 2026-10-02): Gate_PWM → R55 10K → C52 0.1 µF →
R58 10K → C53 0.1 µF → U6A(+); U6A gain 1 + R60 20K / R59 10K = **3**; U7 (MMDT2227
complementary follower) drives TP3, inside the loop; R59 + R60 = 30 kΩ to GND from TP3.
U6, U7 and U8 run from **+12 V = R15 4K7 from VIR + D4 12 V Zener**. U5 runs from **+3V3**.
The gate-DAC filter is identical to the threshold DAC's, so `DAC_TOP` / `DAC_SETTLE_MS` are
shared.

**Schedule (design §13.4):** width = floor(1000 µm / v) clamped 5–100 µs; period =
round(42670 µm / v); gap = period − width ≥ 150 µs; 10 pulses, **shed one at a time with
the spacing kept** while charge > 6.0 mC, not below 3. Integer micrometres, because a
float 42.67 rounds the 20 m/s period (exactly 2133.5 µs) to 2133 instead of 2134. Charge
is evaluated on min(width, U5 clamp) at 9 A. First-pulse delay is not computed (needs
t_cam and FOV geometry; Phase 7).

**Corrections made on the way, in the record:**
- `board.h` said shedding "costs sample density, not coverage". **The design sheds and
  keeps the spacing**, so the burst covers fewer freeze positions. Withdrawn.
- `board.h` called `BURST_CHARGE_MAX_MC` a thermal budget. The design defines it as a
  **VIR sag budget** (~670 µF × ~9 V). Corrected.
- `BENCH_P6` asked where the ~3 mm in the 45.72 mm transit came from: it is
  `config.beam_width_mm = 3.0f` in design §13.1, a placeholder marked "calibrate!".

**Derived, not measured — 6a measures them:** PIO overhead (width word N → N + 2 µs; gap
word M → M + 8 µs; IRQ0 8 µs after the last falling edge), counted from the listing; and
U5's clamp (assumed 122 µs from U9).

**Verification:**
- **Independent code review** (read-only subagent) of the whole change found **one real
  interlock gap, now fixed:** the "gate is provably zero" check looked only at digital state
  (commanded level, PWM compare, pin function). `strobe off` zeroes those instantly, but
  Q9's gate decays through the 10K/0.1 µF ladder (dominant pole 2.62 ms), so
  `strobe off` + `strobe pulse` sent in one write could pulse into a gate still several
  volts high — breaking this build's "cannot command current" property. **My first version
  of the interlock was wrong in exactly the way this file warns about: it trusted firmware
  state for an analog node.** Fix: `gate_write()` is the only writer of the gate compare
  and stamps every non-zero → zero transition; the gate counts as non-zero for
  `DAC_SETTLE_MS` (20 ms) afterwards (`strobe_gate_settling()`, pure and host-tested).
  The reviewer re-checked the fix: no other writer of slice 6; TP3 from 9.9 V is 1.0 V at
  6.4 ms and 5.6 mV at 20 ms; no path admits a pulse inside the window. It confirmed the
  other invariants: GPIO25 handover and every exit, PIO cycle counts against the generated
  listing, schedule math, every latch drop via `FORCE_OFF`, GPIO27/GPIO12, CLI parsing.
- **32/32 native host tests PASS** (MSVC Release, /W4 /WX): 21 power FSM (20 existing +
  `strobe_teardown`) and 11 strobe plan — the §15 table at 90/50/20/10/2/100 m/s, shedding,
  bad speeds, burst limits, clamp-test bounds, PIO encoding, duration, the admission
  policy over **all 256 input combinations**, and the gate-decay window.
- **Mutation check:** removing `strobe_safe_off()` from `FORCE_OFF` made
  `power.strobe_teardown` fail ("Unsafe GPIO write ... pin 15, value 0"); restored; all pass.
- **Pico Release build clean, no warnings.** `picotool`: Release, SDK 2.3.0,
  pitrac_ltb_v1, RP2350, ARM Secure. **157744 text / 0 data / 88496 bss** (+12592 text,
  +188 bss vs 15:21:18). Built stamp **Oct 2 2026 09:27:16**. New help/refusal strings
  present in the image; strobe symbols linked.
- `HARDWARE_REFERENCE.md` regenerated from the netlist: one line changed (the A7 row);
  `--check` passes.

**Not done:** nothing flashed or run on hardware. No 6c/6d firmware. "TP3 during bursts"
moved to 6c (this build never pulses with a setpoint). No commit.

### 2026-10-05 - Full code + documentation audit; CLI bug fixes; PROGRESS split into live + archive

**Owner request:** a full code audit before 6a with refactor / tech-debt suggestions, and every
document brought up to date. **Owner decisions:** code — **fix bugs only**; refactors and
hardening go to a written backlog (§9), not scheduled. Docs — fix everything stale **and
restructure now** (this file → live + `PROGRESS_ARCHIVE.md`, dated results out of
`BENCH_P3_DETECT.md`, dedupe `BRINGUP_NEW_BOARD.md`).

**Method:** seven read-only audit passes (cli.c/main.c; ADC/beam/cal/detect/config; power/
safety/strobe/panel/service/shot plus the host tests; four document sets), plus direct checks
of the build, tools and repo hygiene. Every reported bug was confirmed in the code before it
was changed; three were not real (below). Already fine: `-Wall -Wextra` clean, forced board,
gitignore covers build/captures/roll CSVs, no TODO/FIXME, `tools/scope.py` framing matches the
CLI exactly, no PIO/DMA/PWM double claim, GPIO27 written only in `safe_state.c`.

**Fixed in firmware (build `Oct  5 2026 11:13:55`):**
- **Strict argument parsing everywhere** (`cli.c`). Only `strobe` used the strict parsers; every
  other command used `strtoul/strtof(..., NULL)`, which take junk and prefixes: `gpio xyz` read
  GPIO0, `gpio 2 o` drove a pin high, `cal gain 3100 67` (a percent typed for a fraction)
  solved for 67× full scale and wrote that gain into the config, and the `capture trig`
  timeout could wrap ×1000 into a short one. Every numeric argument must now be a whole number
  (decimal or `0x` hex) or a finite float inside an explicit range; anything else prints
  `ERR: ... Nothing was changed.` and does nothing. Valid input behaves as before, except
  leading-zero octal (`010` is now ten, not eight). `parse_u32` also gained an `errno` check:
  on this target `unsigned long` is 32 bits, so an overflowing number used to saturate to
  4294967295 and pass.
- **`threshold sweep` with 65535 steps never ended** — the sweep counts
  `for (uint16_t i = 0; i <= steps; i++)` — and 0 steps returned before filling the result.
  Steps are now 1–1000 (~20 s at 20 ms each).
- **`capture trig` pre-trigger is limited to 0–90 %**, matching the 90 % clamp inside
  `adc_capture_triggered()`; the `armed:` line used to report a split the capture did not use.
- **Over-long lines and more than 8 words** were silently truncated and the rest dispatched;
  both are now refused before anything runs.
- **The config loader now checks the record version** (`config_store.c`). Magic, size and CRC
  prove a record is intact, not that its fields mean what this build thinks. Board 3's record
  is v1, so it must still load — **confirm with `cfg` after the reflash.** Consequence: bumping
  `CFG_VERSION` will now make an old record fall back to defaults (see the §9 backlog).
- **`beam_plan()` divider capped at 255** (`beam.c`). Below ~9 Hz it reached 256, which the
  `(uint8_t)` cast wrote as 0, so the hardware ran at a divider the duty-ceiling check never
  saw. Only reachable with `beam freq` under ~9 Hz; 150 MHz / (65536 × 255) = 8.98 Hz is now
  the floor and the actual frequency is reported.
- **`strobe_run(NULL)` guard** (`strobe.c`) — the plan helpers were NULL-safe, the result copy
  was not. No caller passes NULL today.
- **`detect` now shows RX-FIFO overflow** (`detect.c`, `cli.c`). `detect_dropped()` existed but
  nothing printed it, so chatter loss in a §3.7 run was invisible. It counts polls that saw the
  sticky RXSTALL latch (≥ 1 word lost each), not words — comments corrected — and `detect arm`
  now clears the latch, which `pio_sm_restart()` does not. Printed only when non-zero, so the
  normal `detect` output is unchanged.
- **Stale printed text:** `id` (strobe 6a/6b is in the image; what is pending is strobe
  *current*); `help` and `panel` usage (`rdy` is on/off); `beam clamp` (prints the measured
  122.68 µs U9 clamp and the real effective duty instead of the pre-Q1 86 µs estimate, and no
  longer says its result sets the strobe limit — U5 is measured in 6a.1); `scan carrier` (no
  longer says "put the winner in board.h" — the carrier is fixed); `adc5vcal` (the scale IS
  persisted, by `cfg save`); `capture trig` (said "Make the sound" for every channel; now
  "roll the ball" on ch5, "make the sound" on ch7 — the `  window` prefix `scope.py` keys on
  is unchanged).

**Audit findings checked and NOT changed, because they were wrong:**
- "`capture 0 …` hangs the CLI" — `adc_capture()` returns 0 for an empty mask and the CLI
  prints `ERR: capture failed` before the loop. (The new mask range now refuses it earlier.)
- "A pending `on` survives a shutdown request in STANDBY" — `power_request_shutdown()` clears
  `s_req_on` unconditionally, and `pending_on_shutdown` already tests on → shutdown → step →
  latch never rose. **The 2026-09-18 power fix stands.**
- "`help` prints `25%%`" — the owner's 2026-09-17 transcript showed it, but the string had
  already been reworded; the current `k_help` prints a single `%`.

**Verification:** 32/32 host tests PASS (they do not cover cli.c, beam.c or config_store.c; the
§10 smoke list does). Release build clean, `picotool info -a` **Release**, `id` stamp
**Oct  5 2026 11:13:55**, **164232 text / 0 data / 88496 bss** (+6488 text vs Oct 2), UF2 SHA256
`171675B4309EC79BA1DA2199720800566A1E181EC3D389D577030E5D1B9108D6`. **Independent code review**
of the changes: one real issue — `threshold vref` accepted exactly 0.5 and 6.0, which
`detect_threshold_set_vref()` silently ignores (it excludes both endpoints), the very no-op the
check was added to prevent. Fixed (exclusive range) and rebuilt; a last rebuild added the `capture trig` wording; that is the stamp above.
Everything else confirmed: no uninitialised use, no skipped cleanup, format casts correct, the
FDEBUG write clears only PIO2 SM0's bit, the version check cannot reject a record `cfg_save` wrote.

**Documentation:** 862 lines of closed history moved **verbatim** from this file to
`PROGRESS_ARCHIVE.md` (checked by script: every original line is present, every moved block is
contiguous), keeping section numbers so existing "PROGRESS §n" references still land.
The 5.2 V supply setpoint is now explicit in `BRINGUP_NEW_BOARD.md` and `START_HERE.md`
(owner request). **Restructure, per the owner's choice:** dated results moved verbatim out of
`BENCH_P3_DETECT.md` (259 lines, 9 blocks) into the archive, with a verdict-plus-live-guidance
pointer at each spot, a §3.7 status box, and a Phase 3 results index in §5;
`BRINGUP_NEW_BOARD.md` now names the canonical BENCH section per step and drops repeated
prose (its §11 board-2 table had a chain delay of 570–590 ns against §9's 553–600 ns — the
duplicate is gone). **Doc errors fixed:** BRINGUP §6 told you to expect "slices ENABLED" from
`beam` *before* `beam on` (the readback is checked against the beam state, so off reads
`off ok`, SIO); BRINGUP §11 sent a new board to §3.6b/§3.6, which are done once (board 3);
the Phase 4 design note claimed the 32.8 ms ring holds "the whole bump down to ~1.4 m/s",
which the 22.7 ms pilot contradicts; `NEXT_BOARD_REV.md` had two CR-15 headings, CR-01 still
🔴 in its heading, CR-09 labelled "Unblocked" while translation is still required, and no
summary rows for CR-13/CR-14; ARCHITECTURE listed planned PIO/I²S work as in use and core 1 as
the hot path (it is unused); `Hardware/README.md` said strobe had no firmware; `START_HERE.md`'s
dated status block is now a pointer to PROGRESS; DEVELOPER_GUIDE said the host tests cover
only `power_fsm.c` and that every blocking command yields and aborts (unverified for five —
now flagged); BENCH_P6 status/prereq, refusal text and per-width `clamptest` lines;
BENCH_P5_P7 7a now says it is not runnable on current firmware. **Doc-audit findings that
were wrong, not changed:** the ADC ring is 32.768 ms per channel in every mode (500 ksps
total split across *n* channels), so the "only at one channel" rewrite was not applied; P8's
"8.0–8.5 firmware exists" is right (simulated Pi); BENCH_P6's CR-01 wording was accurate.
**Final checks:** every relative link and `#anchor` in 17 docs resolves (146 links, 52 with
anchors; script-checked), no stale stamp or placeholder remains, `netlist_report.py --check`
passes.

**Not done:** nothing flashed or run on hardware. No refactors (backlog in §9). No commit.

### 2026-10-05 - Audit-fix build flashed on board 3; post-flash smoke check 13/13 PASS

**Owner flashed** the `Oct  5 2026 11:13:55` image and sent the full log,
`captures/COM11_2026_10_05.12.32.34.965.txt` (2188 lines). **Checked line by line against the
§10 table, not taken on "looks good":**

| # | check | result |
|---|---|---|
| 1 | `id` | `built    : Oct  5 2026 11:13:55`, uid `6d6fda754e367a40`, new `fw`/`pending` lines ✅ |
| 2 | `cfg` | **slot A seq 5 v1**, 104166 Hz / 1311 ticks, scale 1.0620, TRACK = 0, model fitted ✅ — the new version check accepts board 3's record |
| 3 | `help` | no `%%`; `panel pwr` / `panel rdy` split ✅ |
| 4–10 | malformed input | all seven refused with the exact `ERR: … Nothing was changed.` / `NOTHING was run.` text ✅; `cfg` after `cal gain 3100 67` still `u12b gain: 14.5` ✅ |
| 11 | `capture 0x20 2000 250000` | header `mask=0x20 n=2000 rate=250000 overran=0`, exactly 2000 rows, `# end` ✅ |
| 12 | `beam freq 5` (beam off) | `8 Hz actual (TOP=65535)`, `div 0x0ff0` on slices 7 and 11 ✅ — divider 255, not the old wrap to 0 |
| 13 | `beam freq 104166` | TOP 1439, phase **1311 ticks**, `div 0x0010` ✅ |

**Two things in the log, neither a failure:**
- `cfg` warns `BEAM IS AT 2.00 %` against the 25 % `cal duty`. That is the boot default duty, not
  a change: set `beam duty 25` before any optical measurement, as always. Irrelevant to 6a/6b,
  which run with the beam off.
- **Printed-output bug found by step 12 (cosmetic, pre-existing):** `beam freq 5` printed
  "frequency step here is ~0 Hz". The step is computed as `SYSCLK/(TOP+1) − SYSCLK/(TOP+2)`
  and ignores the clock divider, so below ~2289 Hz (divider > 1) it is wrong — at 8 Hz the
  true step is ~0.0001 Hz per count, and the number printed is meaningless rather than
  dangerous. Not fixed now (a reflash for a print line is not worth interrupting the bench);
  added to the §9 backlog, to go into the next firmware change.

**Instrument constraint, 2026-10-05:** the owner currently has the **logic analyser only, no
scope**. 6a proceeds on the LA: U5's Q output at **R61** is the clamped pulse in 3.3 V logic,
and U8 only buffers it for Q10 (tens of ns against a ~122 µs clamp and a 22 µs margin to the
100 µs software limit), so the 6a.1 decision does not need the scope. **Deferred to the scope,
required before 6c:** Q10's gate level (~12 V), edges/ringing and width after U8.
`BENCH_P6_STROBE.md` updated with the LA-only route. 6b needs the DMM for TP3/TP2 (TP2 is
above the LA's 12 V analog limit).

### 2026-10-05 - 6a.1 first attempt: GPIO25 pulse at R57, nothing at R61 (U5 Q) — diagnosing

**Owner report:** the strobe pulse shows on the LA at **R57** (GPIO25 / `Strobe_Pulse`), but
**no signal at R61** (U5's Q).

**Serial log** `captures/COM11_2026_10_05.13.15.45.895.txt` (written when monitoring stopped,
13:39) — **the firmware side is clean:**
- **Step 0 PASS:** `id` stamp correct; `pins` → `STROBE_PULSE(25) 0`, `PULSE_LIMIT_DIS(27) 0`;
  `strobe` → `GPIO27 : SIO output, driven 0, pad 0 -> U5 pulse limiter armed`, A7 SIO, gate 0,
  engine idle, `may fire : +5V rail not ready`; `strobe pulse 20` → `REFUSED -- nothing fired`.
- **Step 1 PASS:** STANDBY / latch 0 / 5.203 V → `on` → `strobe` shows `rail : up (state
  BENCH_RUNNING)`, `may fire : yes`, GPIO27 still driven 0 / pad 0.
- **6a.1:** `strobe clamptest 200` ×5, every one `completed, IRQ0 seen`, enable→IRQ0 polled
  **213–216 µs** against 211 µs nominal, `GPIO25 after: low, SIO`. The `clamptest 100` refusal
  check was not run — a typo `clampteest` was correctly refused as an unknown subcommand. No
  `clamptest 1000` yet.

So five 200 µs pulses reached `Strobe_Pulse` exactly as the LA shows, and **GPIO27 was 0 at the
pad**, which leaves Q8 off unless Q8 is itself faulty. The fault is between U5's inputs and the
probe on R61.

**Netlist trace (re-extracted 2026-10-05):** `Strobe_Pulse` → U5 pin 2 (B) and pin 3 (CLR),
pin 1 (A) = GND. Pin 5 (Q) → R61 0 Ω → `GATE-DRV` = **R62 1 kΩ to GND** + U8 IN (pin 3);
U8 VDD = +12 V. Pin 7 (RCext) = R56 56K to +3V3 + C51 2.2 nF + **Q8 drain** (AO3400A, gate =
`Pulse_Limit_Disable` = GPIO27 with R54 1K to GND). U9 is identical minus Q8 and works at
122.68 µs, so the trigger wiring is proven. Q8 on would *defeat* the clamp (Q follows the
input), not silence Q, so GPIO27 cannot explain a flat Q on its own.

**Hypotheses, most likely first** (GPIO27 excluded by the log): the LA lead or channel on R61;
R61 open or missing; a U5 assembly fault (pins 2/3/4/5/8 joints, orientation, wrong part) —
board 3's strobe chain has never run before; +12 V absent, so U8's input ESD clamp drags
`GATE-DRV` to ~0.6 V, below the LA's logic threshold (TP2 has never been recorded on board 3;
`may fire : yes` checks only the latch state, not +12 V). The
7-step triage is in `BENCH_P6_STROBE.md` 6a.1, "If GPIO25 pulses at R57 but U5 Q (R61) shows
nothing".

The 13:51 log (`COM11_2026_10_05.13.51.09.029.txt`) shows three more `strobe clamptest 200`, all
`completed, IRQ0 seen`, 213–216 µs. It has no `id` line, so **which board it came from is not
recorded** — run `id` at the start of every log.

### 2026-10-05 (evening) - Board 2: a probe on R61 sparked. Incident, layout facts, my omission

**Owner report:** on **board 2**, putting a probe on R61 produced **a small spark**. The owner
plans to probe other points to check for damage, and proposes **board 1** (out of service for
optics since the 2026-08-17 foil incident, §11) as the strobe test board.

**What R61 sits next to** (computed from the KiCad layout, pad edge to pad edge): R61 is an
0805, pads 2.0 mm apart, between U8 and its decoupling.

| R61 pad | net | nearest exposed copper |
|---|---|---|
| 1 (U5 side) | `Net-(U5A-Q)` | R65.1 GND **0.68 mm**, C54.2 GND **0.75 mm**, C54.1 **+12 V 1.15 mm** |
| 2 (U8 side) | `GATE-DRV` | R62.1 GND **0.58 mm**, C54.1 **+12 V 0.75 mm**, C55.1 +12 V 1.20 mm, U8.2 +12 V 1.70 mm |

**Energy available:** the +12 V net holds C54 4.7 µF + C24 1 µF + C50/C55/C56 0.1 µF each
≈ **6.0 µF**, i.e. ½·C·V² ≈ **0.43 mJ** at 12 V — enough for a visible spark. The rail itself is
fed only through **R15 4K7** from VIR, so a sustained short draws ~36 V / 4.7 kΩ ≈ 7.7 mA. That
is current-limited, not a power-supply short.

**Likely cases, from the layout:**
- **+12 V ↔ GND** (e.g. across C54, or C54.1 to R62.1/R65.1): the decoupling discharges in a
  spark; nothing downstream sees more than 12 V. **Probably no lasting damage** beyond possible
  pad pitting.
- **+12 V onto R61** (C54.1 → either R61 pad): 12 V forced onto U5's Q output, which runs from
  +3V3. The 6 µF discharges through U5's output clamp diode into +3V3 → **U5 possibly damaged**.
  U8's input is rated for its 12 V supply, so it should be fine.
- **R61 ↔ GND:** harmless (U5 output briefly shorted, LVC current-limited).

**My error, recorded:** the bench doc I wrote told the owner to probe "R61 (0 Ω, either pad)"
with "rail down while connecting", and never said what R61 sits next to. "Rail down" also
leaves +3V3 live from USB. That is the same omission as the 2026-08-17 foil instruction (§11
lesson 1). **Fixed:** `BENCH_P6_STROBE.md` now says to solder a thin wire to **R61 pad 1** with
the board unpowered (PSU and USB off), and quotes the clearances. New hard rule in §1 (rule 6)
and in `HANDOFF.md` §3 (rule 8). Test points requested as `NEXT_BOARD_REV.md` **CR-21**.

**Board 1 for strobe work:** suitable. Its fault is U11B in the TIA chain (§11), which the strobe
chain (U5, U8, Q10, U6, U7, Q9) does not use, and the foil current was limited to ~3.3 mA by
R77. Run the `BRINGUP_NEW_BOARD.md` fast path (§1–§5 — it last ran in July, and has had an
incident since), flash the current image, then 6a.

**Board 2 damage check** (owner, next): board unpowered → magnified inspection and IPA clean around
R61/R62/C54/C55/R65/U8 → unpowered DMM comparison against another board (TP2→GND, R61 pad 1→GND
≈ 1 kΩ, +3V3→GND) → USB-only `id`/`stat`/`strobe` → PSU 5.20 V, `on`, TP2 ≈ 12 V, idle current
near the 129 mA reference, nothing warm.

### 2026-10-06 - Fault found: `GATE-DRV` / U5 Q shorted to GND at R62

**Owner report:** with the board unpowered, **both sides of R61 and of R62 have continuity to
GND**. The report doesn't say which board; it matches board 3's flat-R61 symptom, and a
continuity beep would also surface on board 2 during the damage check. **Record the board when
confirmed.**

**What it means:** R61 (0 Ω) joins U5 Q (pad 1) to `GATE-DRV` (pad 2), and R62 is the **1 kΩ**
pull-down from `GATE-DRV` to GND. A healthy board reads **≈ 1 kΩ** from either R61 pad to GND —
too high to beep. A beep means the node is shorted to GND, so **U5's output was driving into a
short, and R61 could never show a pulse.** That fully explains the 6a.1 symptom.

**Not a BOM/reel error:** R62 is an 0402 1 kΩ, LCSC C11702, on the same BOM line as 18 other
parts including **R57**. If the reel were wrong, R57 would short GPIO25 to GND, yet GPIO25's
pulse is clean at R57. So the fault is **local to R62's site**: a solder bridge across R62 (0402
pads, ~0.5 mm gap), a bridge from R61 pad 2 to R62 pad 1 (GND, **0.58 mm**), a misplaced or
tombstoned part, or debris. Less likely: a failed U8 input or U5 output.

**Consequences:** fail-safe — with `GATE-DRV` at GND, Q10 cannot turn on. U5 drove 3.3 V into
the short for at most the ~122 µs clamp on each of ~8 `clamptest` fires. LVC outputs normally
survive short, low-duty shorts, but U5's clamp is **re-measured** after the fix rather than
assumed good.

**Next (owner), board unpowered:**
1. DMM in **Ω**, not the beeper: R61 pad 1 → GND. ~0–2 Ω is a metallic short; tens to hundreds
   of Ω points at a damaged IC input or output. The beeper threshold is meter-dependent.
2. Magnify R62 and the R61-pad-2/R62-pad-1 gap. Wick any bridge, or reflow R62.
3. If nothing is visible: lift R62 and re-measure. If the short clears, R62 was it — fit a new
   0402 1 kΩ (required before 6c; it holds U8's input low if U5 is not driving). If it remains,
   lift R61 and measure each pad to GND separately: pad 1 shorted points at U5, pad 2 at U8.
4. After the fix: R61 pad 1 → GND ≈ 1 kΩ. Then solder the probe wire to R61 pad 1, power up,
   `id`, `strobe clamptest 200`.

**Added to the procedure:** this unpowered check (R61 pad 1 → GND ≈ 1 kΩ) is now a **gate
before 6a.1 on every board** in `BENCH_P6_STROBE.md` — it takes seconds and would have found this
before any pulse was fired.

### 2026-10-06 - R62 removed: still 10 Ω to GND. That board is parked; strobe work moves to board 1

**Owner:** with **R62 removed**, R61 → GND still reads **10 Ω**. **So R62 was not the short, and
my leading explanation above (a bridge across R62, or a bad R62) was wrong.** What remains on
the node is U5 pin 5 (output), U8 pin 3 (input), R61, and the gap from R61 pad 2 to R62's
now-empty GND pad (0.58 mm). A solder bridge would read under 1 Ω; **10 Ω looks more like a
damaged IC pin**. If this is board 2, +12 V driven into U5's output by the spark is a plausible
cause.

**Which board:** still not stated — board 2 (after the spark) or board 3 (the original flat-R61
symptom). **To confirm:** run Step −1 (unpowered R61 pad 1 → GND, Ω range) on both. It takes
seconds and tells us whether one or two boards are affected.

**State of that board:** **R62 is off the board**. It must be refitted (0402 1 kΩ) before 6c on
that board — it holds U8's input low when U5 is not driving. To localise later: lift R61 and
measure each side to GND (`BENCH_P6_STROBE.md` 6a.1 triage, check 3). Parked.

**Decision (owner):** move to **board 1** (U11B damage is in the TIA chain; the strobe chain is
untouched) via the `BRINGUP_NEW_BOARD.md` fast path, with leads **soldered** to R33 (GPIO25) and
R61 pad 1 (U5 Q) while unpowered and the LA ground on TP1. **Changed from the owner's plan:**
GPIO25 at **R33**, not R57. R57 is an 0402 whose GND pad is 0.38 mm from its signal pad and
0.46 mm from U5's pins; R33 is a 0 Ω 0805 carrying the same pulse with ≥ 1.2 mm to GND.
**Layout facts found while planning:** J3 pin 2 is `VIR_RTN` (LED return through Q9), not GND.
So VIR must be measured J3 pin 1 → GND (corrected in `BENCH.md` 0.5 and `BRINGUP_NEW_BOARD.md`
§2). Board 1's earlier trim of 1.0627 is within 0.03 % of the 1.063 default, so expect no
`adc5vcal`.

### 2026-10-06 - Board 1: fast-path bring-up done; 6a.1 PASS — U5 clamp 135 µs

**Owner report:** board 1 brought up via the fast path with leads soldered to R33 (GPIO25) and
R61 (U5 Q), LA ground on TP1. **6a.1: U5 Q (CH1) gives 135 µs consistently; GPIO25 (CH0)
matches the commanded 200 µs and 1000 µs.** The bring-up readings (currents, VIR, TP2, TP6,
`adc5v`) and the serial log were not sent. Ask for the log so the board-1 sign-off column can be
filled in.

**Interpretation:**
- **U5 clamps correctly** and is **inside the 109–136 µs band**, near its top. Margin to
  `STROBE_SW_MAX_US` (100 µs): **35 µs** → per the 6a.1 decision table, **keep 100**. The §15
  slow-ball rows (100 µs at 10 m/s) are unaffected.
- **The clamp is per-board.** U9 on the same board 1 measured 122.68 µs (2026-08-13): the same
  part and nominal 56 kΩ / 2.2 nF, 10 % apart, consistent with a ±10 % capacitor. So the
  "expect ~122 µs, U5 is the same circuit as U9" guidance was right about the band and too
  specific about the value.
- **What still runs on the old value:** `STROBE_HW_LIMIT_US_ASSUMED` = 122 is used only by
  `strobe_burst_charge_mc()` to cap a pulse's charge at the clamp. That matters only for pulses
  longer than the clamp — in this build, `clamptest`: 1 × 9 A × 122 µs = 1.10 mC vs 1.22 mC at
  135, both far under 6.0 mC. **No effect on 6a.2–6b, so no reflash now.**
- **Pending firmware change, batched after 6a.2** (with any PIO-overhead correction, so there
  is one reflash): set `STROBE_HW_LIMIT_US_ASSUMED` to the **worst case across boards**, not
  board 1's value. Proposed **137 µs** (nominal 123 µs × 1.11 for ±10 % C, ±1 % R). Update
  `board.h`, the `strobe` status line (today it prints `U5 clamp ASSUMED 122 us (U9's value --
  6a.1 measures U5)`, now stale), `clamptest`'s "expect 109-136 us (U9 … 122.68)" text, and the
  host test that pins the clamp charge.

**Next:** `BENCH_P6_STROBE.md` 6a.2 → 6a.3 on the LA, then 6b with the DMM on TP3 and TP2.

### 2026-10-06 - Board 1: 6a.2, 6a.3 and 6b PASS — the 6a/6b dry tests are complete

**Owner:** "Everything looks good." Measured: gate 50 % → **TP3 4.936 V**; gate 100 % → **TP3
9.88 V**; **TP2 ≈ 12.31 V** throughout; `off` returned TP3 to 0. Serial logs
`captures/COM5_2026_10_06.10.34.02.308.txt` (Step 1 / bring-up tail), `…10.41.35.840.txt`
(Step 0, Step 1, 6a.1), `…10.59.43.753.txt` (6a.2 → 6b), all with `id` = board 1
(`a764f5332ca5ac53`), image `Oct  5 2026 11:13:55`. **Checked line by line:**

| step | log | verdict |
|---|---|---|
| bring-up | `adc5v` 5.203 V, latch permitted; USB-only 4.885 V in STANDBY; `cfg` **slot A seq 1 v1** loads — the second real record the new version check has accepted | ✅ |
| 6a Step 0 / 1 | `pins` 25 = 0, 27 = 0; `strobe pulse 20` refused rail-down; after `on`, GPIO27 SIO / driven 0 / pad 0, `may fire : yes` | ✅ |
| 6a.1 | `clamptest 100` refused with the exact text; 200 ×5, 1000 ×2 completed (the earlier 10:41 log); **U5 clamp 135 µs** (§6 above) | ✅ |
| 6a.2 | pulses 5/10/20/50/100 all `completed, IRQ0 seen`, `GPIO25 after: low, SIO`; bursts 5/150/10, 20/500/10, 50/1000/10, 100/150/6 completed, polled IRQ0 within 2–4 µs of nominal (1413 vs 1411, 4713 vs 4711, 9513 vs 9511, 1365 vs 1361); all five refusals print the exact `INVALID … nothing fired` / `usage … NOTHING FIRED` text | ✅; LA widths and periods match per owner, **no PIO overhead correction** |
| 6a.3 | `sched 90/50/20/10/2` print **exactly** the §15 table (11/474/463/10 · 20/853/833/10 · 50/2134/2084/10 · 100/4267/4167/6 SHED · 100/21335/21235/6 CLAMPED+SHED; spans and charges match); `sched 1` refused; `sched 20 fire` 10 pulses, `sched 10 fire` 6 pulses, both completed | ✅ |
| 6b Step 1 (A7) | `strobe gate 50` → TP3 4.936 V; `panel rdy 100/0/auto` ran; TP3 did not move (owner) | ✅ |
| 6b Step 2 (interlock) | pulse at gate 50 → `REFUSED … gate DAC is not at zero …`; gate 0 → pulse completed; gate 25 admitted | ✅ exactly as specified |
| 6b Step 3 (ramp) | 0/10/20/30/40/50/70/90/100 commanded; **TP3 recorded at 50 % (4.936 V) and 100 % (9.88 V)**; TP2 12.31 V throughout | ✅ on the two recorded points; TP2 moved < 0.2 V |
| 6b Step 4 (rail-down) | gate 50 → `off` → STANDBY / latch 0 → `strobe` shows **level 0, hw compare 0** | ✅ firmware; TP3 0 V after `off` (owner) |

**Numbers worth keeping:**
- **Gate-chain transfer on board 1:** 4.936 / (3 × 0.5) = 3.291 V and 9.88 / 3 = 3.293 V → one
  straight line through zero, **3 × 3.29 V × duty**, linear to full scale. The 6b table assumed
  board 3's threshold-DAC reference (3.256 V); board 1's gate DAC runs ≈ 1 % higher, still well
  inside the ±3 % + 30 mV band. **No flattening at 100 %:** 9.88 V is ~0.9 V below the LM358 +
  U7 ceiling on a 12.31 V rail.
- **TP2 = 12.31 V** steady under every gate setting: the 12 V Zener rail (R15 4K7 from VIR) has
  headroom for the gate amp.
- The polled enable→IRQ0 of 61 µs vs 31 µs nominal on the pulse right after `strobe gate 0` is
  the coarse polling loop (it services USB), not pulse timing — the LA is the truth there.

**Gaps, small, recorded rather than glossed:**
- 6b Step 3: seven of nine TP3 points were not written down. The two that were sit on one
  line, which is strong evidence; record the rest next time the board is up.
- 6b Step 4: the second half — `on` again, then TP3 ≤ 30 mV — is not in the log. With the rail
  down TP3 is 0 regardless (the gate amp runs from VIR), so the real check is the re-power.
  The firmware shows level 0 / compare 0, so low risk; do it at the next power-up (10 s).
- TP3 ripple and Q10's gate (level, edges, width after U8) need the **scope** — still owed before 6c.
- The LA captures were not archived; the "every width and period within ±0.1 µs" result is
  owner-reported.

**Firmware batch — change of plan, stated plainly:** I said I would reflash after 6a.2 with
`STROBE_HW_LIMIT_US_ASSUMED` → ~137 µs (worst case) and any PIO-overhead fix. No PIO fix is
needed, and that constant affects nothing until LED current flows, so **it moves into the 6c
firmware change instead of a reflash of its own**. Until then the `strobe` status line still
prints `U5 clamp ASSUMED 122 us (U9's value -- 6a.1 measures U5)` — now stale, recorded here.

**Before 6c (all open):** (1) scope: Q10's gate via a lead soldered to R64's Q10 side, unpowered;
TP3 ripple; (2) firmware: the 6c guarded-current mode (pulses with a setpoint under a ceiling,
single pulses first, ADC0 plateau per shot), `STROBE_HW_LIMIT_US_ASSUMED`, and the 🔴
"before 6c" items of the §9 code-audit backlog — every long command keeps feeding the
watchdog, a strobe run token; (3) watchdog armed for 6c. Then `BENCH_P6_STROBE.md` §6c with the
LED bank on J3, FLIR on Q9/HS1.

### 2026-10-06 - Board 1: scope on Q10's gate and TP3 — gate drive clean; TP3 coupling baselined

**Owner photos** (Tek MSO54B, TPP1000 probes, `strobe pulse 20`, board unpowered while leads
were attached): `C:\Users\ATTAYEKP\Downloads\Photos-1-001 (4)\` (HANDOFF §9: screenshots stay
in Downloads). CH1 = R61 (U5 Q) in all five. CH2 = Q10's gate (`…172049087` leading, `…172125357`
trailing) or TP3 (`…172331022` leading, `…172358453` and `…172416739` trailing). Read off the
graticules, so ±5 ns and ±0.1–0.2 V.

**Q10 gate (via R64's Q10-side pad) — PASS:**

| | rising | falling |
|---|---|---|
| U5 Q → Q10 gate delay (50 % to 50 %) | ≈ 75 ns | ≈ 80 ns |
| Q10 gate edge (10–90 %) | ≈ 55 ns | ≈ 45 ns |

- The R61 pulse is **20.000 µs** edge to edge. The gate pulse is that **+ ≈ 7 ns**. Gate high
  ≈ 12 V, monotonic, no double edges. Commanded widths reproduce at Q10's gate.
- **Levels creep across the pulse on both channels** (R61 ≈ 3.0 → 3.3 V, gate ≈ 11.9 → 12.8 V).
  Neither a CMOS output nor a capacitive gate does that over 20 µs, and 12.8 V is above U8's
  VDD (TP2 = 12.31 V, DMM). So it is most likely probe compensation not yet run on those
  channels. **DC levels come from the DMM**, not these photos.
- **R61 rings** ≈ 1 V at ≈ 100 MHz for ≈ 40 ns at both edges, dipping to ≈ 2.3 V once on the
  rising edge. Most likely the probe's ground lead plus the soldered wire. It produces nothing
  at the gate, so it is not functional.
- The ≈ 55 ns rise is fine for this design: 1 % of the shortest 5 µs pulse.

**TP3 (Q9 gate, at gate 0) — the baseline for 6c:**
- **±0.3–0.4 V for ≈ 150 ns** at each R61 edge, with a ≈ +0.4 V spike at ≈ 60 ns on the rising
  edge — when Q10's gate rises.
- After Q10 turns off, a **−0.3 to −0.4 V step** (at ≈ 20.11 µs, as Q10's gate finishes falling)
  that recovers to 0 V in **≈ 5 µs**.
- **Topology (netlist, re-extracted):** U6A (LM358, ×3 via R60/R59) drives U7 (MMDT2227, an
  **unbiased complementary push-pull follower**) inside the loop: feedback is taken from the
  emitters, which are TP3. TP3 → R63 47 Ω → Q9 (IRLR2905) gate. Q9's source is Q10's drain
  (`PWR_LowSideD`); Q10 (AO3400A) source → R65 ∥ R66 → GND.
- **Hypothesis, not verified:** Q10's edges couple through its Cgd into the shared node, then
  through Q9's large Cgs (~1.7 nF class) into TP3. At gate 0 the follower sits in its ±V_BE
  crossover dead-band, so only the slow LM358 loop (0.3–0.5 V/µs) pulls TP3 back — consistent
  with the ≈ 5 µs recovery. At a setpoint one transistor conducts and should hold TP3 far more
  stiffly. That is what 6c measures on TP3 and TP4 ("TP3 steady with no ringing during bursts").
- **6c relevance:** the −0.3 V dip happens **after** the pulse and is gone in ≈ 5 µs, well inside
  the 150 µs minimum gap. A turn-on transient at a setpoint would land at the **start** of each
  current pulse — watch TP4 for current overshoot in the first ~150 ns.

**Still owed before 6c:** TP3 at a **static setpoint** (`strobe gate 50`, no pulses), scope
AC-coupled at 10 mV/div, 20 µs/div and 2 ms/div, < 10 mV p-p. The DAC ripple is µV after two RC
poles at ≈ 61 Hz and ≈ 417 Hz, so this is a **stability check**: an LM358 + unbiased follower
driving ~1.7 nF through 47 Ω could oscillate. A slow sine or bursts would mean it does. Running
the scope's probe compensation on both channels first would also settle the level creep.

**Owner, same day:** the DMM showed TP3 steady to ±1 mV at the tested setpoints, and asked
whether the scope check can be skipped. **My recommendation: no.** A DMM on DC volts averages
over ~100 ms, so an oscillation centred on the setpoint reads as a steady number. The DMM
cannot see the failure this check exists to catch. Two weaker supporting facts: TP3 lies on one
line through zero (4.936 V at 50 %, 9.88 V at 100 %), which a gross oscillation would likely
distort; and feedback is taken at TP3, before R63, so Q9's Ciss is buffered from the loop.
Risk is low, the check takes ~2 minutes with the probe already on TP3, and the alternative is
finding out in 6c with current flowing. **Fallback if skipped:** DMM AC mV on TP3 at gate 50 and
100 (limited by the meter's AC bandwidth), and the scope on TP3 and TP4 at the first,
lowest-setpoint 6c pulse. Decision pending (owner).

**Follow-up, same day — LA analog instead of the scope?** Yes, as an equivalent for the likely
failure mode. The LM358 loop cannot sustain oscillation much above its ~1 MHz GBW, which is
inside the LA's ~5 MHz analog bandwidth. It is not equivalent for a > 5 MHz parasitic in the
follower (R63 exists to damp that); 6c's first pulse covers it on TP3/TP4. Method: a gate-0
reference capture plus a spectrum. The LA's 12-bit / ±10 V resolution (~4.9 mV/LSB) cannot
decide < 10 mV p-p alone; periodic content averages out of the noise, as the 0.173 mV switcher
tone on +5 V did with this instrument. Use gate 90 rather than 100 (9.88 V is at the edge of
the ±10 V range). Procedure in `BENCH_P6_STROBE.md` exit criteria; I analyse the exported CSVs.

### 2026-10-07 - 6c/6d firmware: LIVE strobe mode — pulses with a setpoint, under guards (not yet flashed or fired)

**Owner request:** "implement the firmware for the remainder of strobe testing". **Build:**
Release **`Oct  7 2026 08:07:33`**, **184440 text / 0 data / 122720 bss** (was 164232 / 88496:
+30 KB is the ADC0 record buffer), `pitrac.uf2` SHA256
`DE0C66760E8364CDF02650DCCC591F1081D06D15FE477405D05F1B5F2DE80BC2`, no warnings. **Host tests
78/78** (32 before: +5 strobe-plan, +33 live, +8 service). **Not flashed, not run on hardware.**

**What it adds** (procedure and full guard table: `BENCH_P6_STROBE.md` 6c):
- **Live mode**, `strobe live on confirm`, from gate 0 only (`strobe live on` alone prints the
  checklist). It relaxes exactly one dry rule — a pulse may fire with the gate non-zero. The dry
  path is unchanged and is still the power-up default.
- **Guards** (constants in `board.h`, policy in `strobe_plan.c`, math in the new pure
  `strobe_live.c`):
  - gate ceiling **717 levels = 70.0 %** (TP3 ≈ 6.9 V);
  - **staircase:** each raise is at most **31 levels (3 %, 0.30 V at TP3, ≤ ~1.8 A)** above the
    highest level already fired **and judged OK**; re-arming restarts it from 0;
  - **ADC0 readback on every firing:** BURST at 500 ksps, 160 µs baseline, 200 µs after, frozen
    and copied, then verdicts:
    - plateau > **10.8 A** or a sample > **13 A** → `FAULT_STROBE_OVERCURRENT`;
    - current before the pulse, still on at the end, or on > **200 µs** → `FAULT_STROBE_CLAMP`;
    - more current pulses than fired, or no usable record → live mode ends, no fault;
    - every one of these ends live mode with the gate at 0;
  - **pacing:** ≥ 100 ms between firings; ≤ 30 mC per 10 s, booked before firing at 9 A × the
    clamp-limited width. Neither resets on re-arm;
  - live firing span ≤ 30 ms; idle exit after 300 s;
  - the **watchdog is armed by live mode**, `wdog off` is refused while live, and the CLI is
    limited to `help id stat pins adc5v fault off forceoff wdog strobe`.
- **6d:** `strobe clamptest` while live is admitted only at the level of the last live firing,
  and only if it measured 0.5–2.5 A.
- **`strobe cal [A]`** (2–9 A, default 9): from gate 0 in 20-level steps until current shows,
  then 5-level steps, aborting above 1.2 × target. It interpolates, sets the level and fires 3
  confirmation pulses (pass ±5 %). RAM only.
- **`strobe wave`:** the last record as `# capture` CSV. `adc_ring_freeze_copy()` was added for
  the readback.
- **Constants:** `STROBE_HW_LIMIT_US_ASSUMED` 122 → **`STROBE_HW_LIMIT_US` 137** (worst case;
  board 1's U5 135 µs), so a clamp test now books 1.23 mC instead of 1.10. The §15 schedules are
  unchanged. `STROBE_TIMEOUT_MARGIN_US` moved to `board.h`.
- **"Before 6c" backlog (§9) closed:**
  - fault detection by latch generation (SVC-02) and an abort on the final yield slice (SVC-03),
    both host-tested;
  - run token `s_run_gen` (STR-04/05); snapshot reads the GPIO25 pad (STR-07); STR-02 checked,
    no change needed;
  - `capture` CSV dumps now service the FSM and watchdog;
  - `level`, `hpf test`, `cal demod` and `cal model` checked: they already yield;
  - `shot.c` deadlines reclassified to "before Phase 7".
- **Printed text and comments audited:** `strobe` status (DRY / LIVE header, `U5 clamp <= 137 us
  (board 1 measured 135)`, PIO overhead "confirmed"), `id`, `help`, clamptest text, `wdog`;
  `board.h`, `strobe_plan.h`, `strobe_burst.pio`, `main.c`, `shot.c`, `power_fsm.c`,
  `service.h/.c`.

**Decisions I made; the owner may override any of them.**
- All the numbers above.
- Same command words in both modes rather than a `strobe live pulse` family. Dry procedures run
  at gate 0, where Q9 is off, so a dry procedure typed while live still produces no current.
- More current pulses than fired ends live mode **without** a fault. It may be a split plateau
  rather than a hardware failure; `strobe wave` shows which.
- **6d runs inside 6c Step 3 at the first ~2 A step**, so the clamp is proven with current
  before anything higher is fired. This is a change of order from the original doc.

**Corrections, recorded plainly:**
- **An independent review found two bugs in my first version, both fixed:**
  1. **A single commanded pulse that conducted 2–16 times was judged OK and raised the
     staircase.** The NOISY check compared against 16, never against the pulses fired.
  2. **The baseline/pulse split used a clock read that an interrupt could separate from the
     ADC stop.** A ~15 µs USB interrupt would have put a healthy pulse into the baseline and
     latched a false `STROBE_CLAMP`. The split is now counted from the stop instant, read with
     interrupts off; ≥ 8 µs margin to the first edge for any delay.
  - Each fix has a host test, and reverting either one makes its test fail. The reviewer
    re-checked both.
- **Caught myself before the build was final:**
  - a 716-level ceiling would have refused the documented `strobe gate 70` (rounds to 717);
  - a 1 A cal target could abort on its first detected coarse step (up to ~1.23 A > 1.2 A), so
    the minimum is now 2 A;
  - `STROBE_CAL_OVERSHOOT` was both a macro and an enum value;
  - `strobe live on` printed "NOT ARMED" while armed;
  - `service.h` said `pitrac_yield_ms()` returns as soon as an abort appears, but it honours the
    whole delay — the comment now matches the code and the test.
- **Dates:** this session crossed midnight. Firmware comments and docs first said 2026-10-06
  for this work; corrected to **2026-10-07**. The 6a/6b measurements are 2026-10-06.

**Unverified until 6c runs:**
- the gate-to-current slope (the guards assume ≤ ~6 A/V at TP3);
- ADC0 accuracy against TP4 (no cap on the ADC0 net — R32 4.7 kΩ only, netlist-checked — so
  2 µs point samples);
- TP3 at 9 A (design 4.5–6 V);
- the VIR droop across a burst;
- RP2350 ADC noise at baseline (the 12-code detection floor and 40-code baseline limit assume
  a few codes).

**Follow-up, 2026-10-07 afternoon — TP3 stability check: scope or LA?** The owner asked.
**Scope preferred**, with the LA as an acceptable fallback; do not wait days for a scope.
- **Resolution:** the scope at 10 mV/div AC reads < 10 mV p-p directly; the LA's ~4.9 mV/LSB
  cannot, and has to rely on the gate-0 comparison and the spectrum.
- **Bandwidth:** the scope also sees a > 5 MHz parasitic in the U7 follower, which is outside
  the LA's ~5 MHz analog bandwidth. The likely LM358-loop mode (< ~1 MHz) is inside it either
  way.
- **The rest of 6c:** the scope matters more there. TP4's first ~150 ns, where the 6b baseline
  predicts edge coupling, is invisible to the LA. For the ±5 % plateau comparison against ADC0
  the LA is adequate: 4.9 mV is 1.8 % of TP4 at 2 A and 0.4 % at 9 A.
- **Procedure:** the scope route (probe compensation, AC 10 mV/div, 2 ms / 20 µs / 100 ns per
  div, FFT, gate 0/50/90/100) is now in `BENCH_P6_STROBE.md` 6b exit criteria.
- **Owner, 16:20: the kit on hand is a Saleae Logic Pro 8 and a Tektronix TDS 1002** (the
  MSO54B is not available).
  - **Recommendation: the TDS 1002 for the stability check.** 60 MHz and 1 GS/s cover the
    follower-parasitic case that the Saleae's ~5 MHz cannot.
  - Its 8 bits at 20 mV/div are ~0.8 mV per level (20 mV/div is its minimum with a 10× probe),
    so a 10 mV criterion is still readable. Gate 0 sets the pickup floor.
  - **Peak Detect at 2 ms/div**, so a MHz oscillation cannot alias away at 2500 points.
  - FFT only if MATH has it. Otherwise a Saleae capture gives me the spectrum.
  - **For 6c, both instruments:** TDS CH1 TP4, CH2 TP3, EXT TRIG from the GPIO25 wire; Saleae
    digital on GPIO25 and U5 Q, optionally analog on TP4.
  - The scope's ground is mains earth: clip it to TP1 only, never TP5. **(Superseded the same
    day: the ground goes on R65's GND pad, not TP1 — see the next entry. Corrected again
    2026-10-08: R66's GND pad, not R65's.)**
  - FFT only if MATH has it. **(It does: MATH → Operation FFT on this TDS 1002.)**
  - `BENCH_P6_STROBE.md` (6b exit criteria; 6c board state and scales) is rewritten for this
    kit. The 6c scales are now 100 mV/div to ~5 A and 200 mV/div above, read with cursors.

**Correction (mine), found while re-reading 6c for that answer:** I wrote that the staircase
means "no typed command can jump from a measured current to an unmeasured one". That holds only
while the load stays connected.
- A firing that detects **no** current is "within limits" and raises the proven level, so with
  J3 open the staircase climbs with no load.
- The procedure itself is safe: re-seating J3 needs the board unpowered, which ends live mode
  and restarts the staircase from 0.
- An **intermittent** J3 or string connection is not covered. The first real current would land
  at a level never measured with load — bounded after the fact by the 10.8 A stop, one pulse
  ≤ 100 µs.
- **Mitigation now, docs only, no reflash:** 6c Step 3 stops if nothing is detected by gate 30 %
  (TP3 ≈ 3.0 V, well past Q9's 1.0–2.0 V V_GS(th)).
- **Firmware option** (§9, not done): refuse to raise the proven level above ~30 % on a firing
  that detected nothing, and end live mode with "no current — load open?". The same overstated
  sentence is in the `board.h` comment above `STROBE_LIVE_GATE_STEP_MAX`. It is left unchanged
  so the `Oct  7 2026 08:07:33` image stays as documented; fix it with the next firmware change.

### 2026-10-07 - Board 1: TP3 stability check on the TDS 1002 — no oscillation seen; boost pickup masks the floor; redo with a local ground

**Setup (owner):** board 1, dry mode, J3 disconnected, still on `Oct  5 2026 11:13:55` (the dry
path is the same in the new build). TDS 1002, 10× probe after PROBE CHECK, tip on TP3, ground
clip on **TP1**. CH1 AC, 20 mV/div, **BW limit on** (`Bw` in the readout). Trigger CH2 at
1.00000 kHz, so the CH2 probe was still on PROBE COMP. That is harmless: the spikes are not
synchronous with it. 14 photos in `C:\Users\ATTAYEKP\Downloads\Photos-1-001 (5)\`: three per
gate in time order (0, 50, 90, 100), plus `FFT_Gate_0.jpg` and `FFT_Gate_100.jpg`. The owner
reported "the measurements were jumping all over the place".

**Results (read from the photos):**

| gate | 2.5 ms/div Pk-Pk (min / max) | 25 µs/div Pk-Pk | 100 ns/div Pk-Pk |
|---|---|---|---|
| 0 | 143 mV (−68 / +75) | 35.2 mV (at **50 µs/div**) | 14.4 mV |
| 50 | 160 mV (−78 / +82) | 172 mV (−89 / +83) | photo blurred |
| 90 | 155 mV (−74 / +81) | ~33 mV (−73 / +83) | 20.8 mV |
| 100 | 155 mV (−79 / +76) | 36.0 mV (−18 / +18) | 25.6 mV (includes one slow dip) |

- **The same bursty spikes at all four gates.** They are ±70–85 mV, clustered in bursts tens of
  µs apart. Between them is a band of ~8–10 mV p-p at every gate. Pk-Pk is flat with setpoint
  (143–160 mV). Nothing that looks like a sine appears at any timebase.
- **Not the loop and not the DAC PWM.** At gate 0 the PWM is static low and the follower idles in
  its dead-band; at gate 100 the PWM is static high. Both look the same as 50 and 90.
- **Cause: pickup in the probe's ground loop.** TP1 is **72 mm** from TP3 (PCB: TP3 (33.3,
  121.1), TP1 (104.9, 110.8)), and the LM5157 boost (U1 (62.0, 105.5), L1 (56.4, 110.9), D2
  (54.3, 104.4)) sits between them. With J3 open the boost is at near-zero load, so it switches
  in bursts. That explains the jumping readings: Pk-Pk, Min and Max depend on whether a burst
  lands in the 2500-point record, and "Cyc RMS ?" means the scope found no cycle (the TDS 1000
  has no plain RMS).
- **Beaded pattern:** at 25 µs/div the band shows a regular ~12 µs pattern (~84 kHz apparent).
  It is clearest at gate 90 and faint at 50 and 100. Most likely it is the ~1.08 MHz boost beating
  against the display's 1 µs columns. Not proven, because gate 0 was captured at 50 µs/div.
- **FFTs:** both were taken at 1 GS/s, i.e. 0–500 MHz in ~0.5 MHz bins. The whole loop band falls
  in the first bin and everything above 60 MHz is beyond the scope, so they carry no information
  about the loop. They look the same.

**Reading:** no sustained oscillation of the U6A + U7 loop. An unstable loop grows until slew rate
or output swing limits it: an LM358 at ~0.3 V/µs allows ~1 V p-p at 100 kHz and ~0.2 V p-p at
500 kHz. That would stand far above this ~10 mV band and would be absent at gate 0. It is not
there. **The < 10 mV ripple criterion is not certified**, because the pickup floor is 15× higher.

**Why fix it rather than wave it through:** the same 72 mm loop would put the same ±80 mV on TP4
in 6c. That is ±0.6 A apparent at 135 mV/A, larger than the ±0.1 A ADC0-vs-TP4 band.

**Corrections (mine):**
- **TP1 as the ground was my instruction.** I did not check where TP1 is (it was fine for 6a's
  digital timing).
- I said to run with **BW limit off**. Leaving it on is better here: 20 MHz is far above the
  loop band, and it trims switching edges.
- I said the original TDS 1002 "may not" have FFT. This one does.
- I specified 2 ms and 20 µs/div; the TDS steps are 2.5 ms and 25 µs.

**New ground:** a short wire soldered, unpowered, to **R65's GND pad**, the pad of the 2512 sense
resistor farther from Q10 (PCB (45.3, 117.1); the pad toward Q10 is CurrentSense).
**(Corrected 2026-10-08: use R66's GND pad instead. R65's is 0.68 mm from R61 — see the next
entry.)**
- It is 12.6 mm from TP3, 9.4 mm from TP4 and 40 mm from D12.
- For TP4 it measures straight across the sense resistor.
- R67 pad 2 (0603, GND) is 2.4 mm from TP3, if a TP3-only ground is ever wanted.

**Redo** (procedure in `BENCH_P6_STROBE.md` 6b exit criteria):
1. A control capture with the tip on the ground wire.
2. Gates 0/50/90/100 at the same 2.5 ms (Peak Detect), 25 µs and 100 ns/div.
3. FFT at 25 µs/div with a Hanning window (0–5 MHz, ~5 kHz bins).
4. One 100 ns/div capture at gate 90 with BW limit off.

The 6c probe grounds now go to the same wire (6c board state).

### 2026-10-08 - Lead plan for the rest of Phase 6; correction: ground on R66, not R65

**Owner:** wants every lead for the rest of Phase 6 soldered in one session. I checked each candidate
point in the PCB file: nearest other-net copper (pad edge to pad edge) and parts on the bottom
side. The table, the do-not-solder list and the pre-power DMM checks are in `BENCH_P6_STROBE.md`
6c board state, "Leads to solder for 6c/6d".

**The plan:**
- **Keep** R33 (GPIO25) and R61 pad 1 (U5 Q).
- **Add two ground wires on R66's GND pad.** No other-net copper within 4.6 mm; 14.4 mm from TP3,
  13.2 mm from TP4.
- **Add TP3 and TP4.** Both are 1 mm through-hole pads with 0.5 mm holes, and nothing is on the
  bottom side within 17 mm, so feed the wire through from the top and solder on the bottom. TP3 is
  0.59 mm from C56's +12 V pad on top.
- **Twist each signal with its ground.** Every instrument ground goes to those two wires.

**Correction (mine):** yesterday I said to put the ground on **R65's GND pad**. That pad is
**0.68 mm from R61 pad 1** (U5 Q, already wired), the spot the 2026-10-05 board-2 spark entry
measured and recorded in this file. I had the fact and did not check it against the
recommendation. **R66's GND pad** is the same node — the low side of the paralleled sense
resistors — so the measurement is unchanged. The R65 references in `BENCH_P6_STROBE.md` and §10
are corrected; earlier §6 text keeps a correction note.

**Points not to solder, with reasons recorded:**
- **TP2:** 1.92 mm from R15's VIR (36 V) pad. A bridge puts 36 V on the 12 V rail.
- **R67:** a bridge across its pads shorts Q10 drain–source, so current flows with no pulse,
  bypassing U5's clamp and the sense resistor.
- **TP4 itself:** a bridge from TP4 to GND reads ≈ 0.135 Ω (indistinguishable on a DMM) and would
  blind the firmware's overcurrent check, so TP4 is inspected by eye.

### 2026-10-08 - Board 1: TP3 loop stability PASS (MSO54B, soldered leads, ground on R66)

**Setup (owner):** MSO54B, TPP1000 10× on the soldered TP3 lead, ground on the R66 lead; AC,
20 mV/div, full 500 MHz; trigger CH1 rising 3.2 mV, Auto; dry mode, J3 disconnected. Three
timebases per gate, 0/50/90/100 (200 µs, 2 µs, 10 ns/div), plus FFTs at gate 0 and 90. All are
running means over 140–2815 acquisitions. 14 photos in `C:\Users\ATTAYEKP\Downloads\Photos-1-001
(6)\`. The owner noted "still quite a bit of noise".

| gate | AC RMS 200 µs/div | AC RMS 2 µs/div | AC RMS 10 ns/div | Pk-Pk 200 µs/div |
|---|---|---|---|---|
| 0 | 3.795 mV | 3.737 mV | 4.473 mV | 193.8 mV |
| 50 | 3.604 mV | 3.780 mV | 6.127 mV | 107.9 mV |
| 90 | 3.717 mV | 3.358 mV | 4.907 mV | 200.9 mV |
| 100 | 3.818 mV | 3.659 mV | 3.688 mV | 116.5 mV |

**Reading: PASS — no oscillation of the U6A + U7 loop.**
- **The 2 ms AC RMS is flat with the setpoint.** Even allowing ±0.3 mV of scatter per reading,
  a setpoint-dependent component is **< 2.5 mV rms, ≈ 7 mV p-p as a sine**, anywhere in
  ~500 Hz–500 MHz. That is under the 10 mV p-p criterion.
- **Pk-Pk does not trend** with the setpoint (gate 0 is among the highest). Every capture carries
  the `Clipping` flag from occasional spikes past ±100 mV.
- **The 10 ns/div RMS** covers only 100 ns around a triggered ring, so it measures the ring, not
  the floor.

**What the noise is:**
- **The boost.** At 2 µs/div the spikes come on a regular **~1.0 µs spacing**, consistent with the
  LM5157's 1.055 MHz design frequency.
- **The probe's ground lead.** At 10 ns/div each spike is a damped **~450 MHz ring** (~2.2 ns
  period). That is the probe ground lead's inductance resonating with the probe's 3.9 pF,
  excited by the switching edges, and visible because the scope was at full bandwidth.
- **The same at gate 0.** So it is pickup or coupling, not the loop.
- Compared with the TDS on the TP1 ground: the 2026-10-07 ±80 mV bursts at 20 MHz are not
  directly comparable to full-bandwidth rings. With the 20 MHz limit these mostly disappear.

**FFTs:** taken at 2 µs/div, i.e. 0–3.1 GHz with 50 kHz bins. The loop band is in the leftmost
pixels and looks the same in both.
- The gate-90 FFT alone shows a narrow line at **~850 MHz**: the cellular uplink band (824–849
  MHz). It is most likely the phone taking the photos.
- It cannot be the board: it is above the scope's 500 MHz bandwidth and the MMDT2227's fT, and Q9
  carries no current with J3 open.
- Not verified. Repeating it with the phone away would confirm it, but the verdict does not rest
  on it.

**Not done:** the control capture (tip on the ground lead). The gate-0 reference carries the
verdict.

**Consequence for 6c:** read the plateaus with the **20 MHz bandwidth limit**. At full bandwidth
these rings are ±0.1–0.3 A apparent on TP4. Take one full-bandwidth capture of the first live
pulse, for turn-on overshoot only. Added to the `BENCH_P6_STROBE.md` 6c scope settings, with the
MSO54B channel plan: CH1 TP4, CH2 TP3, Aux Trig from GPIO25.

### 2026-10-09 - Handoff audit for a change of model/harness

**Owner asked** for the docs to be ready for a different model or harness. Findings and changes:

**Firmware on board 1 (inferred):**
- Board 1 runs the **6c build**. Its `captures/COM5_2026_10_07.16.42*` and
  `COM5_2026_10_08.15.50*` logs (the TP3 sessions) print `Dry pulses are now REFUSED until
  'strobe gate 0'`. That wording exists only in the 2026-10-07 builds; the 2026-10-05 image
  printed `Pulses are now REFUSED`.
- The only 2026-10-07 image handed over is `build/pitrac.uf2` = `Oct  7 2026 08:07:33`, SHA256
  `DE0C6676…`, unchanged since 2026-10-07 08:07.
- No `id` was logged, so confirm it before 6c. §0, §10 and `BENCH_P6_STROBE.md` updated.

**Session-only scripts moved into the repo.** The previous harness kept two scripts outside it:
- `tools/pilot_analysis.py` — the §3.7 pilot analysis that §9 asked to promote. Output paths are
  now derived from the input file. Re-run on `captures/COM11_2026_09_18.14.54.08.428.txt`, it
  reproduces the recorded **3101.8 mV peak, 22.71 ms FWHM, no rail samples**.
- `tools/check_doc_links.py` — the doc link and anchor checker used by every doc audit since
  2026-10-05. Repo-relative now; 17 files, 0 problems.

**`HANDOFF.md` updated** with conventions only:
- **Safety:** board 1 is the strobe board (§3 rule 1); the probe-point rule now says to check the
  PCB file (rule 8); new rule 9 on live strobe current, including "never raise a live limit to
  pass a test" and the open-load gap.
- **Build and tools (§4):** PowerShell build and host-test commands, how to identify and record
  the build stamp, the link checker, and COM5 = board 1.
- **Working rules (§5):** an independent review for safety-relevant firmware (rule 8), and read
  the `captures/` serial logs (rule 9).
- **Traps (§7):** PowerShell stdin and non-ASCII, `[IO.File]` relative paths, MSVC C4127,
  KiCad 10 `.kicad_pcb` pad syntax and rotation, stale views.
- **Lessons (§8):** a new "Probing" block (ground location, the ~450 MHz ground-lead ring and
  20 MHz BW limit, AC RMS against a reference state, the phone's 850 MHz line, FFT span), plus
  two interlock lessons (no-signal is not safe; time windows from the stop instant).
- **Data and tools (§9):** instruments, photo folders, serial logs.

**Open questions for the owner** are now listed in the §10 resume block.

**Root pointer files (owner's choice):** `AGENTS.md` and `CLAUDE.md` at the repo root, identical.
Harnesses read them automatically. They send the reader to `HANDOFF.md`, then §0/§10, then the
phase's bench doc, and repeat the few rules that must survive a skim: no commits, reflash-first,
the safety list, never raise a live limit.

**Committed at the owner's request:** `a8e08b3` "Phase 6 strobe: dry tests, live-current mode,
host tests, handoff docs" — 97 files, everything in the tree, including the strobe sources and
tests (never in git before), `PROGRESS_ARCHIVE.md`, the two tools, the root pointers, and the
owner's pending `Software/camera-comparison` and `Hardware/Calibration` work. Not pushed. A
follow-up commit corrects the docs that still said "uncommitted".

### 2026-10-12 - Board 1: 6c Step 0 (J3 disconnected) - image confirmed by `id`

Log: `captures/COM5_2026_10_12.08.41.04.756.txt` (preceded by a `bootsel` in
`COM5_2026_10_12.08.40.36.106.txt`; whether or not it was reflashed, the stamp is the right one).

| Check | Result |
|---|---|
| `id` | `built : Oct  7 2026 08:07:33`, `fw` ends `+ 6c/6d live current (guarded, ADC0 per firing)`, uid `a764f5332ca5ac53` - ✅ **board 1 runs the 6c image; no longer inferred** |
| `strobe` | `DRY.`; GPIO27 `SIO output, driven 0, pad 0 -> U5 pulse limiter armed`; A7 on SIO; `U5 clamp <= 137 us (board 1 measured 135)`; `live : OFF -- last ended: not armed since boot`; gate level 0, hw compare 0 - ✅ |
| `strobe pulse 20` (dry, gate 0) | `1 x 20 us -- completed, IRQ0 seen`, enable->IRQ0 polled 34 us (nominal 31) - ✅. LA widths: not in the log (owner to report; Step 2 records GPIO25 / U5 Q on the LA anyway) |
| `off`, `stat` | `STANDBY`, `latch 0`, fault none, 5V_IN 5.203 V - ✅ safe to work on the board |

**My omission:** the Step 0 instructions I gave (2026-10-09 chat) put `strobe` straight after
`on` with no wait, so it was read in `POWERING_ON`: `may fire` / `may gate` / `may arm` show
`+5V rail not ready` instead of `yes`. Harmless - the pulse that followed fired, so the rail came
up - and 6c Step 1 re-reads `strobe` with the rail up. Instructions that follow `on` must say
"wait ~3 s, `stat` shows the rail up".

**Log oddity, not yet explained:** in the saved file the `strobe` output is out of order -
`PIO overhead` (the third `limits` line) comes straight after `A7`, the `gate DAC` / `nominal` /
`engine` / first two `limits` lines come after the `> ` prompt, and `...design current;` runs
into `gate DAC` with no newline. Every line is present. `strobe_status()` in `cli.c` prints them
in one fixed order to one stdout stream, and a USB CDC stream can drop bytes but not reorder
them, so this points at the host (Serial Monitor's file logging), not the firmware. To check:
does the on-screen scrollback show the same order? It matters for 6c, where the per-firing
ADC0 block in the log is the evidence.
- ✅ **Answered the same day: host-side.** The owner's pasted scrollback of the same session has
  every line in firmware order. **Serial Monitor's file log reordered whole output chunks**
  (nothing lost). Rule (HANDOFF §4): the scrollback is the record where order matters; the file
  log is a backup. `strobe wave` of a 21.4 ms burst is ~11 000 lines, over the 9000-line
  default scrollback, so raise the scrollback limit before Step 5.

**Owner's decisions, 2026-10-12:**
- **Rely on the Step 3 stop rule** ("nothing by gate 30 % -> stop") for the open-load staircase
  gap; no firmware fix before 6c. The §9 item stays open.
- **Instruments for 6c today: TDS 1002 + Saleae** (no MSO54B) - the bench doc's TDS paragraph.
  With 2500 points the TDS cannot resolve 20 us plateaus across a 21 ms burst, so the Saleae
  analog channel on TP4 is no longer optional.

**6c Step A, unpowered DMM checks (owner, 2026-10-12) - all PASS:**

| From -> to | Read | Reading |
|---|---|---|
| G1 / G2 -> TP1 | 0 ohm | both ground wires on GND |
| R61 pad 1 -> GND | 1 kohm | no bridge near R61 / R65 |
| TP3 -> G1 | 30 kohm | exactly R60 (20k, TP3 -> U6A-) + R59 (10k, U6A- -> GND), from the netlist |
| TP3 -> TP2 (+12 V) | 30 kohm | not a bridge (that reads ~0 either polarity). Netlist: +12V is a shunt rail, R15 4k7 from VIR with D4 (12 V zener, anode GND) to GND, so with + on TP3 the meter sees R60 + R59 to GND, then D4 **forward** from GND to +12V. Hence about the same 30 k as TP3 -> GND |
| TP4 | inspected | no solder outside the pad |

**6c Step 1, rail up, J3 connected (scrollback pasted by the owner) - PASS.** TDS 1002 CH1 TP4 /
CH2 TP3 (DC), EXT TRIG from R33; Saleae **D1 = GPIO25 (R33), D3 = U5 Q (R61)**; TP4 not on the
Saleae yet. `stat` STANDBY -> `on` -> `BENCH_RUNNING`, `latch 1 railsready 1`, 5V_IN 5.201 V; `id`
stamp right; `strobe` `may fire : yes`, `may arm : yes`. `strobe gate 3` -> level 31, then
`live on confirm` REFUSED (gate not 0); `strobe gate 0`; `live on` checklist; `live on confirm`
-> `LIVE: ARMED ... Watchdog ARMED (1 s)`; `wdog off` and `beam on` REFUSED; `strobe gate 10` ->
REFUSED, `Asked for level 102. Highest allowed now: level 31 (3.03 %) -- proven to 0.` Every
response as the bench doc tables it.
- **Finding:** the `live on` checklist still says "ground on TP1" - stale text (§9).
- The port was closed with live mode armed. The firmware does not watch the CDC connection, so
  live mode stayed armed (gate 0, watchdog serviced) until the 300 s idle timeout ended it.
  Step 2 re-arms.

### 2026-10-12 - Board 1: 6c Step 2 STOPPED - `STROBE_CLAMP` at gate 0: ADC0 baseline 1277 codes (reads as 7.6 A)

**What ran (scrollback pasted by the owner):**
1. Reopened at 276 s idle: `strobe` -> `*** LIVE ***`, `idle exit in 24 s`.
2. The next `strobe pulse 20` came after the timeout, so it fired **DRY**. The output has no
   `LIVE` and no ADC0 block. Then `strobe` -> `live : OFF -- last ended: idle timeout`.
   - **Finding:** the background idle exit prints nothing, so the operator is not told until
     they read `strobe`. It fails safe (dry at gate 0), but should be announced (§9).
3. `strobe live on confirm` -> ARMED.
4. `strobe pulse 20` -> `LIVE 1 x 20 us -- completed`; `ADC0 : 208 samples at 500 ksps, 76
   before the edge; baseline 1277.3 codes (7.625 A); threshold 2158 codes. Verdict: CURRENT
   OUTSIDE THE PULSE`; `no current above the ~72 mA detection floor`; **`STROBE_CLAMP`
   latched**, live mode ended, gate 0.
5. `strobe live off`, then `off`.

**The guard did what it is for.** A baseline over 40 codes means "current before the pulse",
so the verdict is STUCK_ON.

**Why this is almost certainly not current:**
- **No step at the pulse.** The same ~1277 codes sit before, during and after it: `no current
  above the detection floor`.
- **Physically impossible.** A steady 7.6 A from VIR (36 V) is ~270 W. The source is a
  5.20 V / 3 A PSU (15.6 W) through the boost, and 5V_IN held 5.201 V.
- **Q9 cannot be conducting.** Its gate is at 0, so Q9 is off. Even with Q10 shorted, 7.6 A
  through Q9 in linear mode would be ~70 W, and Q9 would fail within seconds.
- **TP3** (TDS CH2, photo) stays at 0 V with only edge spikes: ~+0.6 / -0.5 V at the pulse end,
  with BW limit **off** on CH2.

**Measurement path (netlist):** ADC0 = GPIO40 <- **R32 4k7** <- `CurrentSense_ADC` (TP4,
R65 || R66 to GND, Q10 source). Nothing else is on either net. 1277 codes = **1.03 V at
GPIO40**, so GPIO40 does not see TP4's ~0 V.
- Candidates: R32 open, or GPIO40 not connected, leaving the ADC input floating; or a fault in
  the BURST readback path.
- The readback code (`adc_engine_set_mode(BURST)`: mask 0x01, round-robin of one channel;
  `adc_ring_freeze_copy`, window only from the BURST start) reads correctly.

**Procedure gap (mine):** the live interlock's baseline check assumes ADC0 reads ~0 at rest,
and nobody had ever read ADC0 on any board before arming. 6c Step 1 should have read `adc 0`
dry. Added to the bench doc (6c Step 1).

**LA (`Downloads\PulseTest`, Saleae Ch1 = GPIO25, Ch3 = U5 Q):** GPIO25 **20.002 us**; U5 Q
rises within ~6 ns and falls with it.
- My Step 2 text said "about 22 us": **wrong**. The +2 us PIO overhead has been compensated in
  firmware since 6a.2, so a 20 us command gives 20 us, as 6a recorded.
- The capture cannot say whether it is the dry or the live pulse. At gate 0 both are
  electrically the same: Q10 switched, Q9's gate at 0.
- ~~Analog Ch2 was recorded flat at about -0.05 V (2 GB CSV); it is not on TP4.~~ **Wrong
  (mine):** I misread the owner's earlier message. Saleae analog Ch2 **is on TP4** (ground on an
  R66 wire), 50 MS/s:

  | window | mean | std |
  |---|---|---|
  | 1 ms before the edge | -42.6 mV | 4.9 mV |
  | pulse 1-19 us | -40.3 mV | 2.9 mV |
  | 200 us after | -39.4 mV | 3.5 mV |

  - That is a constant Saleae offset with **no step**: 2 mV = ~15 mA at 135 mV/A.
  - Edge spikes: -13 mV at +0.1 us and -80 mV at +20.1 us (the edges).

**Owner, 2026-10-12, answering:**
- **FLIR:** no change on Q9 / HS1.
- **TDS CH1 (TP4, 20 mV/div):** on screen and at 0 V, overlaid on CH2's trace. The flat trace in
  the photo is both channels.
- **PSU current:** not readable today.
- **`fault clear`** had already been typed before the diagnosis.

**So: TP4 measured ~0 V on two instruments while ADC0 read 1.03 V, and Q9 did not warm.**
The `STROBE_CLAMP` was a false reading of a real 0 A. The open question is only why GPIO40 reads
1.03 V.

**Diagnosis (same day, rail down, no firing):** `adc 0 4096` = **9 codes (7.3 mV)**, `adc 1` =
3032 (2.443 V), `adc 7` = 2055 (1.656 V). The **hardware path is good**: GPIO40 sees TP4 through
R32. `strobe wave` (n=208, pre=76) shows:
- **samples 0-85:** a 4-periodic **10, 2055, 3038, 7**. These are the IDLE round-robin channels
  ch5 DETECT, ch7 MIC, ch1 5V_IN/2, ch2 TIA. Their mean, **1277.5**, is the reported baseline.
- **samples 86-207:** real ch0, **9-10 codes** (~55 mA equivalent, the ADC offset), flat through
  the pulse.

**Mechanism - a firmware bug (mine).** The stale samples are the *old IDLE ring contents*, not
conversions made during the window:
- The IDLE ring starts at index 0 with ch1, 4 channels, so index `i` holds slot `i % 4`.
- The copy ran back from the DMA write index **122** through index 0 and wrapped to 16298-16383.
  Index 16298 is slot 2 (ch5 = 10), 16299 ch7 (2055), 16300 ch1 (3038), 16301 ch2 (7), and the
  last stale sample, at 16383, is ch7 (2056).
- The phase matches exactly. So the BURST DMA had written only **122 samples (244 us)**, while
  the clock said ~440 us since `adc_engine_set_mode(BURST)` (PRE 200 + firing ~39 + POST 200).
- `adc_ring_freeze_copy()` sizes the copy from **elapsed time** (`(now - s_ring_t0_us) x 500
  ksps`), not from what the DMA actually wrote. When the ring starts late, it copies stale data.
  The 122 real samples begin about at the enable. So the ADC0 ring produced nothing for roughly
  the first ~200 us after the mode switch: its start is delayed by about PRE. **Root cause of the
  delay: not yet known.** Nothing in `engine_fire()` / `pitrac_service()` touches the ADC.

**What this does and does not threaten:**
- The guard failed **safe**: stale data read as current and ended live mode.
- It could equally fail **unsafe**. A stale baseline below 40 codes, e.g. if the IDLE channels
  were all low, would pass, and the 160 us pre-window would not be ADC0 at all.
- So **no live firing until it is fixed.**
- ~~Also check the detect path: it uses the same time-based freeze-and-copy after an IDLE->ARMED
  switch.~~ **Wrong (mine), found the same day while writing the fix:** only `strobe.c` calls
  `adc_ring_freeze_copy()`. The detect path reads through `adc_ring_view()` /
  `adc_ring_history()`, which do not bound by the mode start at all. That is harmless while
  ARMED, because it runs for seconds before a pass, but it is not safe straight after a switch
  (§9, `ARCHITECTURE.md` A9).

**Secondary, to verify:** `adc_fifo_setup(..., err_in_fifo=true, ...)` is commented as flagging
FIFO **overrun** in bit 15. My reading of the datasheet is that bit 15 is the per-conversion
**error** flag, and overflow is the sticky `FCS.OVER`. If so, `overran=0` has never proved "no
overrun".

**Fix plan (firmware; then host tests, independent review, reflash):**
1. Before firing, poll the ring's real write index until the PRE baseline is recorded (with a
   timeout -> refuse), instead of `busy_wait_us(200)`. Print the measured ring start latency.
2. `adc_ring_freeze_copy()` returns only what the DMA wrote since the mode start, from the
   write index, not the clock.
3. A pre-window shorter than required -> **readback unusable** (ends live, staircase unmoved),
   never a verdict.
4. Overrun from `FCS.OVER`.
5. Text: announce the idle exit; the checklist's TP1 ground.

Then on the bench: `adc 0` at rest, and a live pulse at gate 0 whose `strobe wave` must be ch0
(~10 codes) end to end.

**Procedure note:** `fault clear` was typed before the record was examined. It did no harm
here, because the record survives in RAM. The rule stays: keep a strobe fault latched until
its record is understood.

*(The next two paragraphs were stranded at the end of section 6 by an edit and were moved back here on 2026-10-12. Both have since been answered: the late start was the ADC running at half rate (the round-7 bench entry), and `adc 0` read 9 codes with the rail down.)*

**Still unknown: why the BURST ring started late.** Nothing in the PRE window touches the ADC or
the ring DMA. `adc ringtest` measures the start latency and per-firing `ring:` lines record it.
If the ring never starts until something else happens, every live firing will be refused
(`NOT FIRED`). That is safe, but it blocks 6c until understood.

**Next (no firing; the fault stays latched):** rail down, `strobe wave`, `adc 0 4096`, and
`adc 1` / `adc 7` as references. Then, if ADC0 reads high with the rail down, an unpowered DMM
check of R32.

### 2026-10-12 - Fix written for the stale-baseline readback; built, host-tested; awaiting independent review

**Owner approved the fix plan;** review by a separate model (ChatGPT 6.1 Sol). **Not flashed.**

**Image** *(superseded the same day by `Oct 12 2026 11:09:36`, after the review — next entry)*: Release **`Oct 12 2026 10:34:54`**, **187968 text / 0 data / 122752 bss**,
`pitrac.uf2` SHA256 `FF5E46F5D5CFE60D0763023BE3F0D012079FBC240EB3952B45EE94E3FDCBF308`. Release
confirmed by `picotool info -a` and `CMakeCache.txt`. No compiler warnings. `id` prints a new
line, `+ ADC0 ring wait + DMA-counted readback (2026-10-12 fix)`.

**Changes:**

| # | Where | What |
|---|---|---|
| 1 | `strobe.c` `run_live()` | After `adc_engine_set_mode(BURST)`, poll `adc_ring_written()` until **100 samples** (`STROBE_LIVE_RING_READY_SAMPLES`) are written, **instead of `busy_wait_us(200)`**. Give up after **5000 us** (`STROBE_LIVE_RING_TIMEOUT_US`): `STROBE_RUN_NO_ADC` "NOT FIRED", back to IDLE, live mode ends (`READBACK`). The budget is now booked after the wait, still before the firing. The first-sample and ready times are recorded per firing. |
| 2 | `adc_engine.c` `adc_ring_freeze_copy()` + new `adc_ring_math.h` | The copy is limited to `min(clock count, DMA write index)` in whole round-robin groups (`adc_ring_copy_avail()`), and returns the write index at the stop. New `adc_ring_written()`. |
| 3 | `strobe_live.c` | New `strobe_live_baseline_by_index()`, the split counted by the DMA write index read **immediately before the enable** (`engine_fire()`) and at the stop. `strobe_live_baseline_checked()` uses the **smaller** of the clock split and the DMA split. The minimum baseline to judge rises from 8 to **50 samples** (`STROBE_LIVE_PRE_MIN_SAMPLES`); below it, NO DATA. |
| 4 | `adc_engine.c` | Overrun is taken from **`FCS.OVER`**, cleared at every mode start. Bit 15 of a FIFO word is the conversion-**error** flag (SDK `adc.h`: "conversion error bit appears in the FIFO alongside the result"; `FCS.OVER` "1 if the FIFO has been overflowed. Write 1 to clear"). The freeze treats either as an unusable record. |
| 5 | `adc_engine.c`, `cli.c` | **`adc ringtest [n]`**: switch IDLE -> BURST, poll the write index for 1 ms (checkpoints 0-800 us), print the first-sample and 100-sample times and the first 8 samples, back to IDLE. Fires nothing; refused unless the engine is in IDLE, and while live (`adc` is not on the allowlist). |
| 6 | `strobe.c`, `cli.c` | A **background live exit** (idle timeout, hold condition) is counted, and the CLI prints `*** LIVE MODE ENDED: ... ***` before the next command. |
| 7 | `cli.c` | The live checklist says grounds on the R66 wires (not TP1/TP5) and `adc 0` < 10 codes. The result print gains a `ring:` line (start times, both splits). A `NOT FIRED` result does not print the previous firing's measurement. |
| 8 | `board.h` | Constants above. The staircase comment no longer claims more than it holds (the open-load gap, §9). |

**Tests: 83/83** (78 + 5 new: `ring_copy_avail`, `baseline_by_index`, `baseline_checked`,
`short_baseline`, `replay_2026_10_12`). `replay_2026_10_12` feeds the day's actual record (86 x
{10, 2055, 3038, 7} then 122 x 10):
- the old split gives STUCK_ON at 1277.5 codes;
- the new split gives NO DATA, both for the 208-sample copy and for the 122-sample copy the fixed
  freeze would make;
- a ring that was waited for gives a judged, clean record.

**Mutations (one per fix, each must fail a test):** all five caught.

| Mutation | Caught by |
|---|---|
| copy ignores the DMA count | `ring_copy_avail`, `replay_2026_10_12` |
| split by clock only | `baseline_checked`, `replay_2026_10_12` |
| minimum baseline back to 8 | `short_baseline` |
| drop the `got > w_stop` guard | `baseline_by_index` (needed a new case, `got = w_stop + 1`, which wraps the unsigned start index) |
| drop the 2-sample margin | `baseline_by_index`, `baseline_checked` |

**Not host-testable (hardware):**
- the ring wait loop;
- `FCS.OVER` clear and read;
- `adc ringtest`;
- the CLI background-exit notice.

These are for the review and the bench: `adc ringtest 5`, then 6c Step 2 again, whose `ring:` line
and `strobe wave` must show ADC0 throughout.

### 2026-10-12 - Independent review (ChatGPT 6.1 Sol): 4 findings, all fixed; image `Oct 12 2026 11:09:36`

The reviewer worked from `captures/review_2026-10-12/REVIEW_BRIEF.md` and the patch, and re-ran
83/83 and the link check. **"I would not clear this image for live bench use yet."** Findings,
and what was done:

1. 🔴 **Safety — a record with only pre-firing samples could pass.**
   - **Scenario:** the ring passes its readiness wait, then acquisition stops at the enable.
     The copy still returns 205 *older* ADC0 samples. The clock split calls 76 of them
     baseline and judges the other 129, also pre-enable, as "within limits, no current". The
     staircase would advance on a firing nobody observed.
   - The reviewer reproduced it with the real functions (`w_en = w_stop = 500`, 250 us,
     10 codes). It was an injected fault: not seen on board 1.
   - **Accepted — my omission:** nothing checked that ADC0 *kept* sampling after the wait.
   - **Fix:** `strobe_live_coverage_ok()`. From the enable to the stop, the DMA must have
     written `(t_stop - t_en) / 2 us` samples, ±4 (`STROBE_LIVE_COVERAGE_TOL_SAMPLES`). Fewer
     means a stall; more means the snapshots disagree. A wrap fails it too.
     `strobe_live_baseline_checked()` returns 0 (NO DATA) unless it passes.
   - To keep honest jitter inside ±4, `engine_fire()` now reads the write index, the clock
     and the PIO enable in **one interrupts-off snapshot**.
   - Each firing prints `enable->stop N samples written, clock allows M: covered`.
2. **Correctness — `adc ringtest` ran with the detector armed.** `detect arm` leaves the
   engine in IDLE, so the mode check passed, and BURST would destroy the armed detector's
   history. **Fixed:** refuses on `detect_armed()` before touching the ADC.
3. **Minor — exit notices duplicated or missed.**
   - **Duplicated:** a hold condition failing inside a blocking command (gate settling) was
     printed by the command and again before the next one.
   - **Missed:** a power-FSM rail-down while idle ends live mode via `strobe_safe_off()`,
     which was not counted.
   - **Fixed:** every live exit is counted in `live_drop()`. Every notice, and the own
     wording of `strobe live off` / `strobe off`, acknowledges it. `dispatch()` checks
     **before and after** every command.
4. **Minor — `NOT FIRED` printed "0.18 mC booked" then "nothing booked".**
   - Wider than reported: `REFUSED` live runs also said "booked".
   - **Fixed:** "booked" only when the firing was admitted and the ring was ready; otherwise
     "NOT booked (not fired)".

**Reviewer's other answers (no defect):**
- The copy bounds admit no stale IDLE data, and a wrap fails conservatively.
- No pulse sample can enter the baseline (`w_en` before the enable is conservative).
- `NO_ADC` cleanup is complete.
- The `FCS` write-back preserves the RW fields; reading OVER separately from bit 15 is right,
  per the RP2350 datasheet.
- The 5 ms poll is far below the 1 s watchdog. The SDK's abort/drain waits are unbounded,
  which predates this change.
- The root cause of the late start: unresolved; no code mechanism found.

**Image** *(superseded the same day by `Oct 12 2026 11:49:23` after review round 2 — next entry; its ±4-sample coverage tolerance was unsafe)*: Release **`Oct 12 2026 11:09:36`**, **188416 text / 0 data / 122764 bss**,
`pitrac.uf2` SHA256 `D08111FA205114F23B1B1A272B2B2C787F6662A02B1052106A1CDB877A6A808F`.
- Release confirmed by `picotool info -a`; no compiler warnings.
- `id` line: `+ ADC0 ring wait + DMA-counted, coverage-checked readback (2026-10-12)`.
- **85/85 host tests:** + `coverage` and `acquisition_stopped`, the latter being the reviewer's
  reproduction. One existing `baseline_checked` case (200 written in a 250 us window) was
  inconsistent under the new check and now expects 0. A case at the +4 tolerance edge was
  added.
- **Mutations, 8 of 8 caught:** the 5 earlier ones, plus coverage skipped (`baseline_checked`,
  `acquisition_stopped`), its upper bound dropped (`baseline_checked`, `coverage`), and its
  lower bound dropped (`coverage`, `acquisition_stopped`).
- **Not host-testable:** the interrupts-off snapshot, the `ringtest` refusal, the notice
  bookkeeping and the booking text. They are for the bench: `detect arm` then `adc ringtest`
  must be refused; an idle timeout must print one notice, once.

**Not flashed.** Whether to send the fixes back to the reviewer for a confirm pass is the
owner's call: `captures/review_2026-10-12/REVIEW_RESPONSE.md` and `fix_2026-10-12_r2.patch`.

### 2026-10-12 - Review round 2: the ±4-sample coverage tolerance could hide a gap; replaced by an exact SysTick check; image `Oct 12 2026 11:49:23`

**Reviewer (ChatGPT 6.1 Sol) on the round-1 fixes:**
- Three fixes are closed: the detector guard, the exit notices, the booking text.
- 🔴 **Safety: the coverage fix was not tight enough.** ±4 samples (8 us) tolerates a gap.
  The reviewer's fault injections, run against the real helpers:

  | Injected failure | Round-1 result |
  |---|---|
  | ADC stopped at the enable | NO DATA (correct) |
  | 6 us pause spanning a 5 us pulse | **covered, no current, within limits** |
  | 6 us pause inside 206 us of current | **covered, width 200 us, within limits**: the clamp violation was missed |

  These are injected faults, not events on board 1.
- The reviewer also noted that clk_adc and the timer share XOSC, so drift could not have
  justified the allowance.

**Accepted — my error.** The ±4 was not derived from anything. It was a guess at the
snapshot jitter, and a guess is no basis for a safety tolerance.

**Fix: exact coverage on the processor clock.**
- `adc_ring_snapshot()` (`adc_engine.c`), called with interrupts off, spins until the DMA write
  index ticks (a conversion has just landed). Then it reads **SysTick** (24-bit, free-running
  at clk_sys, set up in `adc_engine_init()`; unused elsewhere), the us timer, the index, and a
  ring generation (bumped by every `ring_start()`).
- `engine_fire()` takes one snapshot and the PIO enable in the same section. If the ring is
  not ticking, it enables **nothing** (`STROBE_RUN_NO_ADC`; the charge was booked first, and
  the output says so).
- `run_live()` takes the second snapshot after the post window.
- **ADC samples are exactly 96 clk_adc = 2 us = 300 clk_sys cycles at 150 MHz**, from one
  crystal. So `strobe_live_coverage_ok(cycles, samples, cps)` requires
  `|cycles - samples x 300| <= 300 / 5 = 60 cycles` (0.4 us). That bounds only the snapshots'
  latency spread (the poll loop and DMA, tens of cycles).
- **One missing sample is 300 cycles out**, so no gap of even one sample can pass.
  `_Static_assert`s in the tests check that the tolerance cannot hide a sample, and that the
  longest live window (15360 samples, ~4.6 M cycles) fits SysTick's 24 bits.
- `covered` also needs both snapshots and the same generation.
  `strobe_live_baseline_checked()` now takes `covered` and returns 0 without it.
- **Bench check before any live firing:** `adc ringtest` now takes the same two snapshots
  250 us apart and prints `N samples in C cycles, +r vs 300/sample (+-60): exact`.
- Each live firing prints the same line for its own window.

**Tests: 87/87.** New: `short_gap_pulse` and `short_gap_stuck`, the reviewer's two cases.
`coverage` is rewritten: exact; ±60 passes and ±61 fails; one sample missing or extra fails;
the round-1 4-sample hole fails; zero samples (even at zero cycles) fails; the ~30.4 ms window
is exact; SysTick elapsed across its wrap. `acquisition_stopped` and `baseline_checked` were
updated for the new signature.

**Mutations, 12 of 12 caught:**

| Mutation | Caught by |
|---|---|
| ignore `covered` | 3 tests |
| one-sided either way | `coverage`, plus both short-gap tests |
| tolerance widened to round 1's 4 samples | `coverage`, both short-gap tests |
| tolerance widened to 1 sample | `coverage` |
| zero samples accepted | `coverage`; it survived until the zero-cycles case was added |
| SysTick counted up instead of down | `coverage` |
| the earlier five | as before |

**Image** *(superseded the same day by `Oct 12 2026 12:23:23` after review round 3 — §6 below; its snapshot ran from flash and assumed its latency)*: Release **`Oct 12 2026 11:49:23`**, **190112 text / 0 data / 122772 bss**,
`pitrac.uf2` SHA256 `705232D481124D1F745C15D4CB9F3573D5476796AC581BB775ACE3CBC0910321`.
Release confirmed by `picotool info -a`; no compiler warnings. `id` line: `+ ADC0 ring wait +
DMA-counted readback, exact SysTick coverage (2026-10-12)`.

**Not host-testable:**
- that SysTick really runs at clk_sys;
- the latency spread being tens of cycles.

**`adc ringtest` measures both before any live firing:** the pair must read `exact`, with the
residual well inside ±60. If it does not, every live readback would be NO DATA. That is
safe, and it is the first bench step.

### 2026-10-12 - Review round 3: the ±60-cycle snapshot bound is assumed, not enforced (fixed — see the end of this entry)

**Reviewer (ChatGPT 6.1 Sol), safety.** `adc_ring_snapshot()` runs from **flash (XIP)**:
- The poll loop's DMA-index read is at `0x100004ba`; the SysTick read is at `0x100004cc`, on
  another 8-byte cache line (`0x100004c8`).
- A cold line fill is ~192 cycles (boot2_generic_03h, flash divider 2), and disabling
  interrupts does not stop fetch stalls.
- If the first snapshot's latency is ~300 cycles more than the second's, one missing sample is
  cancelled exactly: `126 x 300 + 10 - 310 = 125 x 300`. This was shown arithmetically, with the
  real helpers; the latencies were not measured on board 1.
- The opposite asymmetry would falsely reject healthy records.

I confirmed the addresses in our ELF. Whether that exit-line miss actually happens (the core's
sequential prefetch may fill the line on the first loop pass) is **unproven either way**, which
is the reviewer's point: the bound was never demonstrated.

**Accepted.** The "tens of cycles" was again an assumption standing in for a bound.

**Proposed fix (the owner approved "proceed"; written below):** make each snapshot **measure
its own uncertainty** instead of assuming it.
- Run the poll loop from SRAM (`__not_in_flash_func`, with direct register reads and no flash
  calls inside).
- Read SysTick on **every** pass, so the landing is bracketed between two SysTick readings.
- Reject any snapshot whose bracket is wider than a fixed bound, and require the two brackets
  together, plus a stated DMA-latency allowance, to stay within the 60-cycle budget.
- A fetch stall, an interrupt or contention then shows up as a wide bracket and is rejected, not
  absorbed.
- `adc ringtest` reports the brackets and residuals over many runs, including runs after an XIP
  cache flush.

**Written, built, tested (image `Oct 12 2026 12:23:23`):**
- **`adc_ring_snapshot()` is `__not_in_flash_func`.** Verified in the ELF: `T adc_ring_snapshot`
  at `0x20000110`, called through a veneer.
  - The loop at `0x20000142`-`0x20000150` is the SysTick read, the DMA index read, compare and
    branch: no call, no flash access.
  - The closing SysTick read is the next instruction after the exit branch (`0x20000152`).
  - `time_us_64()` is called only after the bracket closes.
- **The bracket:** `tick_lo` is the SysTick read before the last index read that saw no change;
  the closing read follows the index read that saw the change. So the landing lies inside
  `[tick_lo, tick_lo - bracket]` by construction.
- **`strobe_live_coverage_ok(span_lo, b1, b2, samples, cps)`:**
  - each bracket must be ≤ **40 cycles** (`STROBE_LIVE_SNAP_BRACKET_MAX`);
  - the window `b1 + b2 + 2 x 20` (`STROBE_LIVE_DMA_SPREAD_CYCLES`, the one *stated*, unmeasured
    bound) must be narrower than a sample period;
  - the landings' measured separation, widened by that window, must contain `samples x 300`.
  - The largest possible window is ±60, no wider than round 2's. One missing or extra sample is
    300 cycles out and cannot fit, by `_Static_assert` and by an exhaustive test.
- **`adc ringtest [1-200]`:** each run takes a warm-cache pair and a cold-cache pair
  (`xip_cache_invalidate_all()` before the first snapshot). Runs past 5 print a summary: covered
  count, widest bracket, residual range, PASS/FAIL. Each live firing prints its own
  `enable->post` line.
- **Tests: 88/88.**
  - New: `snapshot_latency_cancel`. The reviewer's 126-period / 125-sample case with a 310-cycle
    first latency: round 2's arithmetic cancels exactly; bracketed, the 320-cycle bracket is
    refused, and with narrow brackets the deficit shows as 300 cycles.
  - `coverage` is now exhaustive over landing position in each bracket (0..b), bracket widths
    {0, 1, 20, 40} and DMA spread {-20, 0, +20}: honest always covered; one missing or extra
    never.
  - Also covered: the window edges, asymmetric brackets, an over-wide bracket, the
    window-vs-period guard, and the round 1–2 cases.
- **Mutations: 15 of 15 caught**, including:
  - the bracket limit dropped;
  - the window guard dropped;
  - the centre made blind to bracket asymmetry;
  - the DMA spread doubled;
  - the brackets left out of the window.
- **The mutation script had a flaw:** it counted a failed build as "caught". One run hit a
  locked test `.exe` (LNK1104) and was falsely counted. Fixed (retry, then INCONCLUSIVE), and
  re-run: that mutation is genuinely caught by 4 tests.
- **Image:** Release **`Oct 12 2026 12:23:23`**, **191568 text / 0 data / 122780 bss**,
  `pitrac.uf2` SHA256 `7CCC6FC79AC960E072EE37C978F98CBFA84AC50A339FC3278EA93134EEF46E9D`, no
  warnings. `id` line: `+ ADC0 ring wait + DMA-counted readback, bracketed SRAM coverage
  (2026-10-12)`.
- **What remains assumed:** the DMA's conversion-to-visible-write latency spread, ≤ 20 cycles
  between the two snapshots. Everything else in the window is measured per snapshot. The bench
  (`adc ringtest 100`, warm and cold) and each live firing's `enable->post` line show the real
  brackets and residuals.

### 2026-10-12 - Review round 4: what the snapshot timestamps is a transfer *issue*, not a completed write; two new findings (analysis, no code change)

**Reviewer (ChatGPT 6.1 Sol), correctness:** the ring uses address wrapping, and RP2350 disables
the in-flight adjustment of `WRITE_ADDR` in that mode. So the index advancing does not mean an
SRAM write completed, and my "conversion landing" / "never an over-count" claims are wrong.
- The reviewer has not reproduced an unsafe verdict.
- Recommendation: establish the event and derive the bound before any further code.
- The SRAM placement and bracket arithmetic check out; cold-cache invalidation is appropriate.
- `ringtest 100` shows observed behaviour, not a worst case.

**Confirmed from the datasheet (my error):**
- §12.6.4: a transfer is "issued to the address FIFOs … in flight, but has not yet necessarily
  completed".
- §12.6 changes: the WRITE_ADDR in-flight adjustment is "disabled for address-wrapping …
  transfers".

**Analysis** (`captures/review_2026-10-12/REVIEW_RESPONSE_4.md`, sent for review before any code):
- **The event is a transfer issued for a conversion already in the ADC FIFO.** Each issue
  consumes one DREQ credit, so issued implies converted. That keeps the coverage check, the
  `by_index` split and the ring wait valid.
- **The derived spread of that event between the two snapshots is ≤ ~9 cycles:** ≤ 1 for the
  clock-domain crossing, plus ≤ 8 of arbitration against the strobe's PIO-feed initial fill at
  the first snapshot. D = 20 stands, now derived rather than assumed. Proposed: take the first
  snapshot after that fill, which removes the arbitration term.
- 🔴 **Real gap: the freeze copies before any completion barrier.** The newest transfer can
  still be in flight. Proposed: abort the ring correctly *before* the copy (in-flight transfers
  "cannot be revoked; BUSY stays high until they complete").
- 🔴 **New (mine): our DMA abort does not follow RP2350-E5.**
  - The SDK's `dma_channel_abort()` polls BUSY, does not clear EN, and does not poll ABORT.
  - `ring_start()` re-triggers at once. A trigger landing while ABORT is still in effect runs
    with a zero transfer count, so the ring writes nothing.
  - This is a **candidate** for the 2026-10-12 late start (nothing written for ~200 µs, then
    writing from index 0). What re-triggered it is unexplained.
  - Proposed: `dma_abort_safely()` per the datasheet sequence (clear EN, CHAN_ABORT, poll ABORT,
    poll BUSY) for all three channels.
- 🔴 **New (mine): a dead IDLE ring would silently blind the 5V_IN supply monitor.**
  `adc_read_5vin_volts()` never checks that the ring advances. Not observed (bench readings
  vary across switches). Proposed: a ring-liveness check in the 5V_IN path.

**Status:** reflash hold stays. No code change until the analysis is reviewed. Image
unchanged: `Oct 12 2026 12:23:23`. *(Superseded the same day by round 5 — next entry.)*

### 2026-10-12 - Review round 5: the analysis accepted with qualifications; abort, freeze barrier, PIO-feed wait and 5V_IN freshness implemented; image `Oct 12 2026 13:15:36`

**Reviewer (ChatGPT 6.1 Sol) on `REVIEW_RESPONSE_4.md`:** the copy barrier, the safer DMA stop and
the supply-freshness guard are justified. Proceed with the qualified fixes. **The 9-cycle timing
bound stays an OPEN review item**:
- "500 ksps means <= 1 outstanding transfer" assumes each transfer retires within 300 cycles,
  which is a service-time bound that needs proving;
- shared DMA queues and ADC-read bus latency can affect later issue timing through
  back-pressure;
- the late-start explanation (E5) is unproven: the compiled mode switch does ADC shutdown,
  drain and configuration between abort and restart, and the channels self-chain.

Qualifications, all adopted:
- the SDK documentation itself requires clearing EN before an abort;
- reject a drain failure or a DMA READ/WRITE_ERROR, and preserve those flags;
- the 5V guard must keep the true last-good age, handle mode changes, and make the FSM reject
  unavailable readings (neither the old value nor NaN works);
- wait for min(words, 8) feed words, with a timeout;
- CHAIN_TO = the channel itself (0 chains to channel 0).

**Implemented:**
1. **`dma_stop_safely()`** (new `dma_safe.c/.h`), per datasheet 12.6.8 and RP2350-E5:
   - EN off and CHAIN_TO = self, via the non-triggering CTRL alias, with the write-1-to-clear
     error flags masked to 0 so they survive;
   - CHAN_ABORT, then poll **CHAN_ABORT** (the register says "must be polled until it returns
     all-zero. Until this point, it is unsafe to restart the channel") and BUSY, bounded at
     100 us;
   - report READ_ERROR / WRITE_ERROR.

   Used for the ring (`ring_stop`), the strobe feed (`engine_reset`) and the triggered
   capture. A stop that does not complete marks that channel **never to be restarted**: the
   ring stays down and 5V_IN reads STALLED; the feed returns `STROBE_RUN_NO_FEED`.
2. **Freeze completion barrier** (`adc_ring_freeze_copy`):
   - stop the ADC, drain the FIFO (a failure is recorded);
   - `dma_stop_safely(ring)`; only then read the final index and copy;
   - `overran` is now: a drain failure, an incomplete stop, a READ/WRITE_ERROR, FCS.OVER, or a
     conversion-error bit.
3. **PIO-feed wait** (`engine_fire`): before the first snapshot, wait until the TX FIFO holds
   min(words, 8) (it is joined, per `strobe_burst.pio`), bounded at 50 us. Otherwise
   `STROBE_RUN_NO_FEED`: nothing is enabled. This removes feed arbitration at the first
   snapshot.
4. **Fresh-only 5V_IN:**
   - `adc_5vin_read()` returns FRESH only if all 128 ch1 samples were issued **during the
     call** (~1 ms wait, bounded at 3 ms), skipping the newest group because it may be in
     flight. Otherwise NOT_SAMPLED (ARMED, BURST, or a bench capture owning the ADC) or
     STALLED (the ring is down or not advancing).
   - Only FRESH resets the age.
   - The power FSM: latch and rail check require FRESH, else the new fault
     **`SUPPLY_UNREADABLE`**. The sustained monitor counts STALLED as low, so it trips after
     500 ms as `SUPPLY_UNREADABLE`; NOT_SAMPLED neither starts nor clears the debounce and is
     never judged.
   - `stat` prints the status.
   - **My regression, caught before the build:** `capture trig` (up to 30 s, with the FSM
     serviced through its poll callback) leaves the mode IDLE with the ring down. The first
     version would have read STALLED and dropped the rail. Bench captures now set a flag that
     reads NOT_SAMPLED.
   - Also: `adc_capture()`'s early return now runs *before* `ring_stop()`. Previously it could
     leave the ring down.
5. **Comments:** the event is a transfer **issued**, not a completed write, everywhere
   (adc_engine, strobe_live, board.h, adc_ring_math). DMA_SPREAD is marked derived and open.

**Tests: 93/93.** Five new power-FSM tests:
- STALLED and NOT_SAMPLED at the latch are refused (`SUPPLY_UNREADABLE`, no latch rise);
- STALLED at the rail check gives FORCE_OFF;
- STALLED while running trips after the debounce as `SUPPLY_UNREADABLE`;
- NOT_SAMPLED with a stale *low* value never trips, and the debounce restarts when sampling
  resumes.

**Mutations: 20 of 20 caught.** The script now runs the full suite. The five new ones: the
monitor judging NOT_SAMPLED; the monitor ignoring STALLED; the latch or the rail check accepting
non-fresh; unreadable reported as SUPPLY_LOST.

**Not host-testable (hardware):**
- `dma_stop_safely`'s register sequence;
- the freeze barrier;
- the feed wait;
- the bench-capture flag;
- `adc_5vin_read`'s ring wait.

**Image** *(superseded the same day by `Oct 12 2026 13:55:58` after review round 6 — next entry)*: Release **`Oct 12 2026 13:15:36`**, **193816 text / 0 data / 122784 bss**,
`pitrac.uf2` SHA256 `C8522736DBAB985C253C83C2A13AC7EC4EEBA535F60B0294739F3D0E93EB008E`.
- No warnings. `adc_ring_snapshot` is still in SRAM (`0x20000110`).
- `id` adds `+ E5-safe DMA stops, freeze completion barrier, fresh-only 5V_IN`.

**Open (review):**
- the conversion-to-issue spread bound D (service time and back-pressure);
- the late-start root cause;
- **5V_IN is not monitored while ARMED or during a bench capture.** NOT_SAMPLED is never judged,
  so a supply loss during a long detector arm goes unseen. This predates the change (the old
  code judged a stale cached value instead). Logged in section 9.

### 2026-10-12 - Review round 6: completeness proven from hardware flags; 5V_IN integrity; capture stop validation; image `Oct 12 2026 13:55:58`

**Reviewer (ChatGPT 6.1 Sol), two defects and a proof gap:**
1. 🔴 **Safety: `adc_5vin_read()` could certify invalid data as FRESH.**
   - It checked that the ring advanced, but ignored FIFO overflow and masked away
     conversion-error flags.
   - An overflow LOSES conversions (datasheet 12.4.3.4), so the round-robin can rotate and
     "ch1's slots" hold another channel.
   - Reproduced on a host copy of the body: a FRESH 6.853 V from a 4.600 V supply. Error-flagged
     samples also passed.
2. **Correctness: `adc_capture_triggered()` ignored its stop result.** After a stop timeout it
   could still return OK, process the buffer, and later restart the failed channel. Neither
   capture entry point guarded against restarting it, so "a channel whose stop does not
   complete is never restarted" was not true for the capture channel.
3. **The proposed timing checks (single step, empty FIFO, steady pace) do not close D.**
   - A regular interval proves frequency, not the conversion-to-issue delay.
   - The reviewer's counterexample passes the production coverage helper with one conversion
     missing: delays of 230 and 10 cycles, 40-cycle brackets, exact 300-cycle local intervals.
   - **Accepted.** The reviewer asked to bound the delay independently "or base coverage on an
     observed conversion event".
   - The supply gap need not block no-Pi 6c, but must be closed before 7c.

**Resolution of 3 — completeness from hardware flags, not timing.** The datasheet says:
- while CS.START_MANY is set, the ADC "automatically starts new conversions at regular
  intervals" (12.4.3.2);
- "If the FIFO is full when a conversion completes, the sticky error flag FCS.OVER is set …
  any conversions that complete during this time are lost" (12.4.3.4);
- a DMA read of an empty FIFO sets FCS.UNDER; a failed bus transfer sets READ/WRITE_ERROR.

So no conversion can be missing from the ring if, at the freeze:
- START_MANY and EN were still set at the stop instant;
- READY returned;
- the DMA drained the FIFO;
- the DMA stop completed, with no READ/WRITE_ERROR;
- there was no OVER and no UNDER;
- the ring was not restarted since the first snapshot (same generation).

`adc_ring_freeze_copy()` now checks every one of those, records which failed in
`adc_ring_freeze_flags()`, and any failure makes the readback unusable. Each firing prints a
`record:` line naming the failure. **The SysTick coverage check is kept as a cross-check that
can only reject.** Its bound D no longer carries the safety argument, so the round-4/5 open
item is closed by design rather than by argument. This is the "observed event": the ADC's own
loss flag.

**Fixes for 1:**
- New pure helper `adc_5vin_from_ring()` (`adc_ring_math.h`), now what `adc_5vin_read()` runs.
  It returns INVALID on FCS.OVER/UNDER since the ring started, on a ring READ/WRITE_ERROR, or on
  any averaged sample with the conversion-error bit.
- After an overflow or DMA error the reader **restarts the ring**, re-establishing the channel
  order and clearing the sticky flags, so a later reading can be trusted.
- The blocking mode-OFF read clears and checks CS.ERR_STICKY.
- The new status `ADC_5VIN_INVALID` is treated by the power FSM as unreadable:
  `SUPPLY_UNREADABLE` at the latch, the rail check, and in the monitor after 500 ms.
- `stat` names it.

**Fixes for 2:**
- The triggered capture samples FCS **before** stopping its DMA (after the stop, the ADC fills
  a FIFO nobody drains). It validates the stop: an incomplete stop marks the channel unsafe
  and returns ADC_TRIG_ERROR without touching the buffer; a bus error returns ADC_TRIG_ERROR.
- Both capture entry points refuse an unsafe channel and clear stale DMA error flags before
  use (`dma_clear_errors()`, new).
- `adc_capture()` waits by polling BUSY and FCS together with interrupts off, so a loss during
  the capture is distinguished from the FIFO filling after it.
- Both captures now treat OVER/UNDER/DMA errors as "overran". This closes the section 9 item
  where bit 15 was read as overrun.
- `ring_start()` also clears stale ring DMA errors.

**Tests: 101/101.**
- New executable `adc_ring_test`, 6 cases: fresh across the wrap; FIFO/DMA flags void it; a
  conversion-error bit inside the window voids it, while one outside or on another channel does
  not; the in-flight newest group is skipped; **the reviewer's rotated-phase case** (two lost
  conversions, a 4.6 V supply: unflagged it returns a confident wrong value, flagged it is
  INVALID); bad arguments.
- Two new power-FSM tests: INVALID at the latch is refused; INVALID while running trips as
  `SUPPLY_UNREADABLE`.

**Mutations: 23 of 23 caught**, including:
- the helper ignoring FIFO/DMA flags (`v5in_rejects_hw_flags`, `v5in_phase_rotation`);
- the helper ignoring conversion-error bits;
- the helper reading the in-flight group.

M21 first came back INCONCLUSIVE: removing the flag left an unused parameter, which `/W4 /WX`
refuses. That is the round-3 script fix doing its job. The mutation was corrected and re-run.

**Not host-testable (hardware):** the freeze's flag reads, the captures' stop handling, and the
ring restart after an overflow. They are for review and the bench.

**Image** *(superseded the same day by `Oct 12 2026 14:43:50` — the bench found the ADC at half rate, next entry)*: Release **`Oct 12 2026 13:55:58`**, **194712 text / 0 data / 122796 bss**,
`pitrac.uf2` SHA256 `239BB7230E6FDD2CD597318BBED2C0A9816B2C33B05F7471FD415AAB26167F7A`.
- No warnings. `adc_ring_snapshot` is still in SRAM.
- `id` adds `+ E5-safe DMA stops, record proven by ADC/DMA flags, fresh + intact 5V_IN`.

**Still open:**
- the late-start root cause (the bench's `adc ringtest` reports OVER and the start time);
- 5V_IN unmonitored while ARMED or during a bench capture, before Phase 7c (section 9).

### 2026-10-12 - Bench, image `13:55:58` (Sol passed round 6): `adc ringtest 5` FAIL — the ADC ran at HALF rate; root cause of the morning's "late start"; fixed in `Oct 12 2026 14:43:50`

**What the owner ran (board 1, dry, nothing fired):**
- `id`: `13:55:58`.
- `stat`: `5V_IN 5.205 V fresh`.
- `adc 0 4096`: 9 codes.
- `adc ringtest 5`: **0 of 10 pairs covered.** The owner correctly stopped there.

**What it showed:**
- **ADC0 arrived every 4 us, not 2 us.** `written at: +100:25 +200:50 +400:100 +800:200`, and
  `100 written +398 us` every run.
- Each 250 us pair: 64-66 samples in ~38,400 cycles, i.e. **600 cycles per sample**, against
  the 300 the check assumes. The residuals of about +19,200 cycles are 64 x 300. The cross-check
  rejected correctly.
- The first sample came at +2 to +9 us: **the ring starts on time.**

**Root cause (datasheet 12.4.3.2):** "By default (DIV = 0), new conversions start immediately
after the previous conversion finishes … 500 kS/s. Set DIV.INT to a positive value n to trigger
the ADC once per n + 1 cycles. **The ADC ignores this if a conversion is currently in progress,
so generally n will be >= 96.**"
- `set_rate()` set DIV = 95 for 500 ksps: a trigger every 96 cycles, landing as each 96-cycle
  conversion ends. Every other trigger was ignored.
- **Every ring mode has run at half its stated rate since the A1 ring was written:** IDLE
  62.5 ksps per channel, ARMED 125 ksps per channel, BURST 250 ksps.
- **That is the morning's "late ring start":** 122 samples where the clock said ~218, a ratio of
  0.56. The RP2350-E5 abort hypothesis (round 4) was **wrong**. The E5-safe abort stays: it is
  the datasheet sequence regardless.
- The round-1 DMA-counted copy and the round-6 flag proof both failed safe, as designed.

**The fix:** `adc_div_for_rate()` (`adc_ring_math.h`, host-tested) gives DIV = 0 at the 500 ksps
maximum, otherwise `clk / rate - 1`, never below 96.
- Test `adc.adc_rate_div`: 500 k gives 0; 250 k, 100 k and 1 k are exact; 499 k clamps to 96; a
  sweep shows no nonzero DIV below 96.
- Two new mutations (the old rule, and a 95 floor) are caught.

**Brackets.** Steady-state brackets were 15 cycles. The 60-226-cycle ones were caught on the
snapshot loop's **first pass**, so they spanned the function's entry and any instruction fetch the
flash-resident caller left outstanding (157 right after a cache flush).
- The second snapshot of each pair follows a timer-based `busy_wait_us(250)` on a grid locked to
  the same crystal, so it kept starting at the same phase, just as a sample landed.
- **Fix:** `adc_ring_snapshot()` now lets one change go by, then brackets the next, so the
  measured change is always caught in a steady SRAM pass. Disassembly checked: the loop is
  SysTick read, DMA index read, compare, branch, and the closing SysTick read follows one ALU op
  after exit.
- **`STROBE_LIVE_SNAP_BRACKET_MAX` stays at 40.** No limit was changed.

**Image:** Release **`Oct 12 2026 14:43:50`**, **194344 text / 0 data / 122796 bss**,
`pitrac.uf2` SHA256 `2D3BD0C1262AC402393C0349A9127FC21429528F832C0DD91E1B60904FB6471D`.
- 102/102 host tests; 25 of 25 mutations caught; no warnings.
- `id` adds `+ ADC at true 500 ksps (DIV 0), synchronised snapshots`.

**What the half rate may have affected (section 9):** any timing derived from a ring at its
*stated* rate.
- Detect pass logs and retained waveforms report `adc_ring_channel_rate_hz()`, which was 2x the
  real rate, so their time axes were compressed 2x.
- `capture` and `capture trig` were affected only at ~500 ksps requests (the default for
  `capture trig`). The `captures/` logs used 100 k and 250 k, and the section 3.7 pilot
  (3.102 V / 22.7 ms FWHM) was a 100 ksps triggered capture, **unaffected**.

**Bench re-test on `14:43:50` (board 1, dry), 2026-10-12: PASS.**
- `id` and `stat` correct; `adc 0` 9 codes.
- `adc ringtest 5`: `written at +400:200 +800:400` (**2 us per sample**), `100 written +199 us`,
  first samples 9-11 codes, and all 10 pairs covered with brackets 16/16.
- `adc ringtest 100`: **200 of 200 pairs covered** (warm and cold cache), widest bracket 16
  cycles, residual -7 .. +8 cycles against a +-36 window.
- The rate fix and the synchronised snapshot behave as derived.
- Side finding: `adc5vcal` calibrates the single-channel read, not the ring reading (section 9).

### 2026-10-12 - Board 1 on `14:43:50`: 6c Steps 1–2 PASS; Step 3 started (gate 3: no current; TP3's post-pulse kick reverses sign)

**6c Steps 1-2 on `14:43:50` (board 1), 2026-10-12: PASS.**
- **Step 1:** `on` gives `railsready 1`. Every refusal is as tabled; `live on confirm` armed with
  the watchdog; `strobe gate 10` was refused with `Highest allowed now: level 31`. The checklist
  now names the R66 grounds.
- **Step 2:** `strobe pulse 20` at gate 0 gave `LIVE 1 x 20 us -- completed`, `221 samples, 76
  before the edge; baseline 10.8 codes (0.064 A); threshold 22. Verdict: within limits`, and no
  current above the floor.
  - `ring: first sample +5 us, 100 ready +199 us`.
  - `enable->post: 134 samples, 40197 cycles, brackets 16/16, residual -3 … covered`.
  - Baseline split 76 by the clock, 77 by the DMA count. No `record:` line.
- `strobe wave`: 221 samples, all 10-11 codes.
- The owner reports the TDS and LA traces identical to the pre-fix gate-0 pulse.
- The 10.8-code baseline is the ADC zero offset; plateaus are measured from it.
- **Gate-0 full-bandwidth reference** (TDS: CH1 TP4 20 mV/div with BW off, CH2 TP3 1 V/div,
  250 ns/div, EXT trigger on GPIO25 at 1.19 V):
  - TP4 is flat apart from a ~30-40 ns burst of about +-25 mV, ~50 ns after GPIO25 rises. That
    is the U5 -> U8 -> Q10 turn-on edge coupling, as in 6b; no current.
  - TP3 shows a tiny blip at the same instant.
  - This is the reference for the first current step's turn-on.
- **Probes:** both are at **1X** (owner, 2026-10-12), so the `Probe 1X` menus were right and
  the amplitudes above stand. **Correction:** I told the owner that 10X with a matching menu was
  better and was what the procedure assumed. For 6c on the TDS that was wrong, and 1X stays:
  - TP4 is the sense node itself (netlist: Q10 source, R65 ∥ R66 = 2 × 0.27 Ω, R32, TP4), so a
    1X probe's 1 MΩ ∥ ~110 pF loads nothing.
  - A 1X probe has no compensation network, so a 20 µs plateau reads true. An uncompensated 10X
    probe crept ~7 % over 20 µs on 2026-10-06, more than the ±5 % ADC0-vs-TP4 band.
  - Cost: if these are the TDS's standard P2220s, ~6 MHz at 1X. Plateaus lose nothing. The
    full-bandwidth turn-on capture shows an overshoot lasting ≳ 100–150 ns but smears anything
    shorter; the gate-0 reference was taken the same way. Repeat on the MSO54B at 10X if the
    turn-on detail ever matters.

**6c Step 3, gate 3 % (board 1, `14:43:50`), 2026-10-12.** TDS: CH1 TP4 50 mV/div, BW limit
**off**; CH2 TP3 1 V/div, BW limit on; 10 µs/div; EXT trigger on GPIO25 at 1.19 V. Scope photo
only; the `strobe pulse 20` output comes with the step table.
- **No current:** TP4 is flat within ~±3 mV (< 0.03 A) through the pulse. That is as expected:
  TP3 is far below Q9's 1.0–2.0 V threshold.
- **TP3 ≈ 0.3 V** (3 × 3 % × 3.29 V = 0.30 V), flat through the pulse.
- **At the end of the pulse (≈ +20 µs), TP3 kicks up by ~0.1–0.2 V and recovers in ≈ 5 µs.**
  TP4 shows a tick of a few mV at the same instant. The 6b gate-0 baseline was a **−0.3 to
  −0.4 V** step at the same moment with the same ≈ 5 µs recovery. The owner flagged the reversed
  sign before climbing.
- **Likely reason (a schematic reading, not proven):** the kick is Q10's turn-off coupling
  through the shared Q9-source / Q10-drain node and Q9's ~1.7 nF Cgs into TP3, as in 6b. U7 is an
  unbiased complementary follower. Its NPN can only push TP3 up and its PNP only pull it down,
  with a ±V_BE dead band between them that only the slow LM358 (0.3–0.5 V/µs) crosses.
  - At gate 0 nothing conducts, so the kick shows unopposed.
  - At gate 3 the NPN already carries ~10 µA into the ~30 kΩ from TP3 to GND (DMM), so it
    clamps any downward kick within nanoseconds. It cannot pull TP3 down, though: the upward part
    of the coupled edge survives until the LM358 brings the PNP in, ≈ 5 µs later.
  - So the 6b prediction "at a setpoint U7 holds TP3 much more stiffly" is only half right:
    stiff downward, not upward.
- **Harmless as it stands:** it happens after the pulse, with Q10 off. Q10 is in series with the
  whole current path, so no current can flow, and TP4 confirms it. And it is gone in ≈ 5 µs,
  against ≥ 100 ms between staircase pulses and the 150 µs minimum burst gap.
- **What would matter:** an upward TP3 kick at the **start** of a pulse once Q9 conducts. It
  would last ≈ 5 µs and show as a hump over the first ~5 µs of the TP4 plateau. None is visible
  at gate 3. Step 3 now says to watch for it at every step.
- CH1's BW limit was off in this capture; plateau readings need it on (20 MHz).

**6c Step 3, gates 3-33 % (board 1, `14:43:50`), 2026-10-13: NO CURRENT at any step; stopped.**
- Twelve firings, one per 3 % step: levels 31, 61, 92, 123, 154, 184, 215, 246, 276, 307 and
  338, i.e. TP3 nominal 0.30 to **3.27 V**. Each was `LIVE 1 x 20 us -- completed, IRQ0 seen`
  with `Verdict: within limits` and `no current above the ~72 mA detection floor`.
- The readback was clean on every firing:
  - baseline 10.7-10.9 codes (the zero offset);
  - `covered`, brackets 16/16, residuals -7 .. +4;
  - splits 76/76-77;
  - `100 ready` +198 to +200 us.
- One oddity, harmless: at gate 18, `first sample +38 us` (others +2 to +5) but `100 ready
  +199 us` as usual. The ring started on time; the poll that notices it was late, most likely
  held off by an interrupt.
- **Reading:** TP3 at 3.0-3.3 V is well past Q9's 1.0-2.0 V V_GS(th), so the current path is
  open: most likely the load (J3 seating, the bank's wiring or polarity), less likely VIR or
  Q9/Q10.
  - This is the gap the Step 3 rule exists for: zero-current firings count as within limits,
    so the staircase climbs with no load.
  - The owner fired one step past the 30 % rule (33 %), but no harm was done: ending live mode
    restarts the staircase from 0.
- **Next:**
  - `strobe live off`, `off`, PSU and USB off.
  - The owner reports TP3 on the scope and U5 Q on the Saleae at the last steps.
  - Unpowered checks: J3 seating, polarity (J3.1 = VIR = anodes, J3.2 = VIR_RTN = cathodes to
    Q9's drain) and continuity to the bank.
  - VIR at J3 pin 1 -> GND (DMM clipped on while unpowered; expect ~36 V after `on`).
- **Owner, 2026-10-13:**
  - TP3 on the scope read **3.24 V** at gate 33 (nominal 3.268 V).
  - U5 Q (Saleae D3) pulsed at every firing.
  - **VIR to GND read 7 V.** The board state at that reading (rail up or decaying after `off`)
    is not yet known.
- **What it means:**
  - At 7 V no string can conduct. The strings need roughly 22-27 V (VIR 36 V leaves ~9-11 V of
    headroom at 9 A), so VIR this low explains zero current regardless of the gate.
  - But TP3 tracking to 3.24 V needs U6's supply (+12 V, fed from VIR through R15 4K7 and the
    D4 zener) at about 5.5 V or more. So VIR was likely higher than 7 V during the staircase,
    though not necessarily near 36 V.
  - **Correction (2026-10-13):** I wrote here that VIR had never been recorded on board 1. The
    `BRINGUP_NEW_BOARD.md` sign-off records **36.0 V** (§2, J2, no load), and TP2 = 12.31 V in
    6b agrees.
- **Next:** a clean reading with the rail up (`stat`: railsready 1), steady or not, plus PSU
  current and a FLIR sweep of U1/L1/D2 and the bank.
  - If VIR is ~36 V: the 7 V was a decaying rail; back to the bank wiring.
  - If VIR is low: repeat with J3 disconnected, to separate the bank loading VIR from a boost
    (U1, LM5157; 1.0 V x (1 + 150k/4k3) = 35.9 V) that is not regulating.
- **Owner, 2026-10-13, later:** **C26 reads 36 V, but the "VIR+" terminal reads 7 V.**
  - C26.1 and J3.1 are the same net (VIR), and no current flows (Q9/Q10 off). So a 29 V
    difference means an open or high-resistance joint between the VIR copper and the
    terminal: J3 pin 1's solder joint, the terminal's own contact, or a trace.
  - The 7 V is the floating side, set by leakage against the DMM's 10 MOhm. The boost works.
  - **This explains the zero current:** the bank never saw VIR.
- **Next (unpowered, VIR drained below 1 V at C26):** resistance C26 -> J3 pin 1 joint ->
  terminal clamp -> the bank's + lead, and J3 pin 2 joint -> Q9's drain tab. Each should be
  < 1 Ohm. Then inspect J3's joints.
- **Found and fixed by the owner, 2026-10-13: a cold solder joint on J3 pin 1** (Wurth
  691137710002 screw terminal). Re-soldered with the board unpowered.
  - Still to verify: VIR at the J3 screw = 36 V with the rail up.
- **Consequences:**
  - The first staircase (3-33 %) and the gate-0 Step 2 pulse ran with **no load**. They proved
    the readback and the gate drive, but nothing about current or about Q9 with VIR applied.
  - Step 2 is redone first (gate 0, expect no current), with a fresh gate-0 full-bandwidth
    reference, since the Q9/Q10 node now sits under the bank instead of floating. Then the
    staircase runs from gate 3.
  - Re-arming restarts the firmware staircase from 0.
- **Lesson (corrected 2026-10-13):** I first wrote that the bring-up VIR check at J3 pin 1 was
  never reported for board 1. **It was: 36.0 V** in the `BRINGUP_NEW_BOARD.md` sign-off.
  - So either it was read where a cold pin-to-pad joint does not show, or the joints failed
    later. Tightening a screw terminal and landing 16-18 AWG leads stresses the pins.
  - Every copper pour on this layout connects to its pads solid, with no thermal relief (PCB
    file), and the owner reports the pours are hard to solder.
  - Changes made:
    - `BRINGUP_NEW_BOARD.md` §1 now inspects every pour joint, and §2 reads VIR at the J3
      screw against C26.
    - `NEXT_BOARD_REV.md` CR-22 adds thermal reliefs.
    - The 6c pre-power table has J3-screw continuity rows.
    - `HANDOFF.md` §8 has an Assembly lesson.
    - The 6c board state makes "VIR at the J3 screw = C26" a required check before the first
      live pulse.

**6c Step 2 again, at gate 0, now with VIR at the bank (board 1, 2026-10-13).** Three TDS photos
in `C:\Users\ATTAYEKP\Downloads\Photos-1-001 (7)\`:
- Photo 1: 10 us/div, both channels BW-limited.
- Photos 2-3: 250 ns/div at the leading and trailing edges, with **CH1 (TP4) BW-limited and CH2
  (TP3) at full bandwidth.** That is the reverse of what was asked; harmless if the
  first-current-step captures repeat it.
- Both probes at 1X. CH1 TP4 20 mV/div, CH2 TP3 1 V/div, EXT trigger 1.19 V.

What the photos show:
- **No current through the pulse:** TP4 has no plateau at 10 us/div. Any step would be under
  ~2 mV, i.e. under ~0.02 A.
- **TP4 at turn-on:** about -30 mV, then a **+38 mV lobe** (~0.28 A apparent) that decays in
  ~250 ns. This morning, with J3 open, it was a ~90 ns +-25 mV burst. So the bank adds a small,
  slower lobe.
  - Likely: Q10 discharging the small charge on its drain node, which now sits under the bank
    (a few nC to ~20 nC), plus its own gate charge returning through R65/R66.
  - Harmless in size and duration; invisible to ADC0's 2 us samples.
- **TP4 at turn-off:** a ~-40 mV lobe over ~150 ns.
- **TP3 at turn-on:** a ~+0.3 V bump about 90 ns in. 6b saw +0.4 V at ~60 ns.
- **TP3 at turn-off:** a **-0.4 to -0.6 V step** that recovers in ~5 us. This matches the 6b
  gate-0 baseline (-0.3 to -0.4 V, ~5 us), slightly larger. At gate 0 a negative TP3 only holds
  Q9 further off.
- These three photos are the **gate-0 reference with load** for the first current step's edges.
- **Firmware, three gate-0 firings: PASS.** Each one was:
  - `LIVE 1 x 20 us -- completed`, `within limits`, `covered`;
  - `no current above the ~72 mA detection floor`;
  - baseline 10.7-10.9 codes, brackets 16/16, residuals -1 .. +3, splits 76/77.
- Q9 cold on the FLIR. **Step 2 passes with the load connected.** Next: the staircase from gate 3.
- Still to log: the owner's VIR reading at the J3 screw after the repair.

**6c Step 3 again, gates 3-30 % with the load (board 1, 2026-10-13): STILL NO CURRENT; stopped
by the 30 % rule.**
- Ten firings, levels 31 to 307: all `within limits`, `covered` and `no current above the
  ~72 mA detection floor`. Baseline 10.6-11.0 codes, residuals -8 .. +6.
- **TP3 on the scope tracks the setpoint**, ~0.05-0.09 V below nominal throughout, within the
  scope's accuracy at 1 V/div:

  | gate % | 0 | 3 | 6 | 9 | 12 | 15 | 18 | 21 | 24 | 27 | 30 |
  |---|---|---|---|---|---|---|---|---|---|---|---|
  | TP3 nominal (V) | 0 | 0.30 | 0.59 | 0.89 | 1.19 | 1.49 | 1.78 | 2.08 | 2.38 | 2.67 | 2.97 |
  | TP3 measured (V) | 0 | 0.24 | 0.52 | 0.84 | 1.12 | 1.40 | 1.72 | 2.04 | 2.32 | 2.60 | 2.92 |
- **Reading:** at 2.92 V on Q9's gate, with a bank of 2 x 5 VSMA1085400X02 (well under 36 V at
  low current), Q9 should conduct amps. So the series path is still open somewhere in VIR -> bank
  -> J3 pin 2 -> Q9's drain.
  - Q10 switches: TP4 shows its gate-charge lobes at both edges.
  - TP3 couples to Q9 through R63: the post-pulse kicks.
  - So the gate side is unlikely to be the problem.
  - Top suspect: a twin of the pin-1 cold joint, on **J3 pin 2** or on **Q9's tab**. Then the
    bank's wiring, clamping and polarity, then the LEDs.
- **VIR at the J3 screw after the repair was never reported.** Get it before the next power-up.
- **Next (unpowered, VIR < 1 V at C26):** resistance checks, each < 1 Ohm:
  - J3.1 screw -> C26;
  - J3.2 screw -> Q9's tab (this also covers the tab's solder joint);
  - each screw -> the far end of its wire at the bank.

  Then the bank's polarity, and a per-LED diode test with the bank off J3.

**Owner, 2026-10-13: J3 pin 2 also had a cold solder joint; re-soldered.** Both J3 joints were
cold, so every live firing before this had the bank open-circuit. Gate 0 re-run, three TDS photos
in `C:\Users\ATTAYEKP\Downloads\Photos-1-001 (8)\`:
- 10 us/div and 50 us/div, both channels BW-limited.
- 250 ns/div leading edge, with CH1 BW-limited and CH2 full (as before).
- Firmware output, VIR at the J3 screw and the post-repair J3.2 -> Q9-tab resistance: not yet sent.

What the photos show:
- **TP3:** the post-pulse dip is unchanged: ~-0.37 V, recovering in ~5-6 us.
- **TP4 leading edge:** about -11 mV then +22 mV, decaying in ~200 ns. That is smaller than the
  -30/+38 mV with only pin 1 fixed. These photos are now the gate-0 reference with the load fully
  connected.
- **New: an "AC" component on TP4.**
  - A continuous **~45-50 kHz triangle, ~5-7 mV p-p** (~0.04-0.05 A apparent p-p). It is
    present before the trigger, so the pulse does not cause it.
  - Plus intermittent bursts of fast spikes up to ~+-10-12 mV.
  - It appeared once both ends of the bank were connected, i.e. once the bank and its leads sit
    at VIR.
  - It cannot be current in R65/R66 between pulses (Q10 is off). Most likely measurement
    pickup (probe ground lead / common mode) from the bank wiring carrying VIR and the boost's
    light-load burst ripple. **Hypothesis, not verified.**
  - Below the +-0.1 A (13.5 mV) comparison band, and one cycle is ~20 us, so it averages out
    across a 20 us plateau.
- **Discriminating check:** `strobe wave` after a gate-0 firing.
  - A real +-3 mV wobble at ADC0 would swing ~+-4 codes with a ~10-sample period.
  - Still 10-11 codes means the scope alone sees it.
  - Optional: free-run the scope and type `off`. If the triangle stops, it is from the boost.
- **Result, same session: PASS.**
  - **VIR at the J3 screw = 36 V.** J3.2 -> Q9's tab < 1 Ohm.
  - Gate-0 firing: `within limits`, `covered`, `no current above the ~72 mA detection floor`,
    baseline 10.8 codes, residual -2.
  - **`strobe wave`: 214 samples, all 10-12 codes, no periodic swing.** ADC0's pin net is
    R32 4K7 and U3 only (no filter capacitor), so a real +-3 mV, 50 kHz wobble on TP4 would
    have shown as +-4 codes.
  - **The scope's triangle is measurement pickup, not on the sense node.** Read scope plateaus
    as the mean across the plateau; ADC0 is unaffected.
  - Live mode had idled out (300 s) by the time of `strobe wave`; re-arm before the staircase.
- **6c Step 2 PASS with the bank fully connected, 2026-10-13.** Next: the staircase from gate 3.

**6c Step 3, FIRST CURRENT at gate 21 % (level 215, TP3 ~2.04 V), board 1, 2026-10-13. The step
does NOT meet the Step 3 pass criteria; the climb is held at 21 %.**
- Gates 3-18 %: no current (scrollback not sent).
- Gate 21 %, three firings:
  - #1: threshold 36 codes, `no current above the ~72 mA detection floor`.
  - #2: `0.221 A plateau, peak 0.293 A, on 4 us (2 samples), short`, threshold 35.
  - #3: `0.181 A, peak 0.240 A, on 4 us (2 samples), short`, threshold 30.
  - All `within limits` and `covered`.
- Four TDS photos in `C:\Users\ATTAYEKP\Downloads\Photos-1-001 (9)\`:
  - 10 us/div and 2.5 us/div x2, both channels BW-limited;
  - 250 ns/div leading edge, CH1 BW-limited, CH2 full.
  - The owner had moved CH1's zero to ~0.3 div below CH2's.
- **TP4 (photos):**
  - **A turn-on peak of ~+45-54 mV (~0.35-0.40 A).** At 250 ns/div: -14 mV, a fast +19 mV,
    then a rise to +43 mV at ~0.5 us.
  - Then a **~1 MHz oscillation of ~+-12 mV (~+-0.09 A)**. Its envelope swells and fades over
    ~10 us; it is not a clean decay. Troughs reach below zero.
  - The pulse **settles at only ~7-11 mV (~0.05-0.08 A)** by its end.
  - At 10 us/div: a smooth decay from +41 mV to ~+4 mV by ~9 us, then a step to ~+12 mV
    holding to the end. Each photo is a different firing.
  - So TP4 is neither flat after 1-2 us nor free of ringing (Step 3 pass criteria), and Step 3's
    stop rule "TP4 rings or oscillates" applies.
- **The ~1 MHz is close to the LM5157's design 1.055 MHz (R8 20K).** The only switcher tone
  measured on this hardware so far is 801 kHz on +5 V (board 3), from L1 or L2, not settled
  (`BRINGUP_NEW_BOARD.md` §2). With the swelling envelope, the
  boost's switching is the leading suspect. Between pulses the boost's ripple on the scope was
  pickup (`strobe wave` flat), so this may be pickup too. **Unverified.** A real oscillation of
  the Q9 stage near threshold is the alternative, and it could grow with current.
- **TP3 after the pulse: +2.5 V for ~1-2 us**, back in ~5 us, one small undershoot. It was
  -0.4 V at gate 0 and +0.1-0.2 V at gate 3 with no load.
  - Likely: the current must stop when Q10 opens, so Q9's source flies up and drags the gate up
    through Cgs. U7's NPN cannot pull TP3 down, and the PNP needs the slow LM358.
  - Q10 is off, so it carries no current, and it recovers well inside the 150 us burst gap.
  - **Correction:** I told the owner to expect a post-pulse kick of about +0.1-0.2 V at a
    setpoint. With current flowing it is ~10x that.
- **Readback limitation (firmware, safety-relevant; section 9):** see "Half-peak threshold" in
  section 9. It explains all three ADC0 lines: the ~0.3 A turn-on peak put the threshold at
  ~0.15 A, above the ~0.07-0.1 A body of the pulse.
- **Next, at gate 21 % only (no climbing):**
  - `strobe wave` of the last firing. Real +-0.09 A ripple at 1 MHz would scatter the 2 us
    samples by ~+-15 codes; pickup would not.
  - The Saleae A2 file.
  - A 1 us/div capture to time the oscillation against 1.055 MHz.
  - If still unclear: a tip-on-ground test (CH1 tip moved to the G1 wire, unpowered).

**Gate 21 % again, after re-arming, 2026-10-13.**
- The staircase from 3 % was clean: gate 6 was fired twice, and 3-18 % showed no current.
- Gate 21 %: `0.240 A plateau, peak 0.317 A, on 4 us (2 samples), short`, threshold 37,
  `within limits`, `covered`.
- Instruments on this firing: `strobe wave`; Saleae A2 on TP4 at 50 MS/s
  (`C:\Users\ATTAYEKP\Downloads\strobegate21\analog.csv`, offset -74 mV removed); TDS at
  1 us/div, CH1 BW-limited, CH2 full.
- **ADC0 during the pulse** (codes, then amps above the 10.8-code baseline):

  | codes | 64 | 38 | 25 | 19 | 24 | 29 | 28 | 28 | 29 | 28 |
  |---|---|---|---|---|---|---|---|---|---|---|
  | A | 0.32 | 0.16 | 0.08 | 0.05 | 0.08 | 0.11 | 0.10 | 0.10 | 0.11 | 0.10 |
- **Saleae, the same firing:**
  - 0.36-0.39 A at 0.25-1 us, after an edge spike to 0.53 A at 0.05 us;
  - a decay to a **minimum of 0.055 A at 7-8 us**;
  - an abrupt rise at ~9 us to a **steady 0.124-0.139 A from 9 to 20 us**;
  - a ~-0.1 A mean (gate-charge lobe) at 20-20.5 us, then ~0.
  - **No 1.055 MHz content**: ~0.3 mV tone amplitude in the pulse, the same as before and after.
- **TDS, 1 us/div:** the same shape. TP4 peaks at ~+46 mV at ~0.7 us and decays smoothly, with
  no ripple. TP3 (CH2) steps **up ~0.3 V at turn-on**, then sags to **~0.3 V below its setpoint
  by ~8 us**, mirroring the current.
- **Conclusions:**
  - **The ~1 MHz ripple in the earlier photos was boost pickup.** It is intermittent, and absent
    on the scope, the Saleae and ADC0 in this firing. Without the half-peak artefact, ADC0 and
    the Saleae agree: the settled level is 0.10-0.11 A against 0.12-0.13 A, inside +-0.1 A.
  - **The real shape is a slow transient of the gate loop.** The turn-on kicks TP3 up, so the
    current overshoots to ~3x its settled level. U7 (an unbiased push-pull follower) cannot pull
    TP3 down until the LM358 slews through the crossover dead band (~0.3-0.5 V/us). TP3 then
    undershoots, so the current sags to ~0.05 A at ~7.5 us. The LM358 crosses back and the NPN
    re-engages at ~9 us, giving the abrupt step to the settled level.
    - This interpretation fits all three instruments. Why the turn-on kick on TP3 is upward is
      not fully explained.
  - Near threshold the current is very sensitive to Vgs, so 0.3 V on TP3 swings it from 0.35 A
    to 0.05 A. Higher up, Q9 approaches the 1/0.16 Ohm ~= 6 A/V degeneration limit, so the same
    0.3 V would be ~+-2 A. **How the transient scales with gate level decides whether the
    plateau can ever be flat**; one more step measures it.
  - Step 3's pass criterion (flat after <= 1-2 us) is not met. TP3's dip does recover within the
    pulse, and nothing rings, so no "stop altogether" rule is met.
  - **Candidate next-board change (pending data):** bias U7, or add a gate-charge reservoir at
    TP3 / Q9's gate, so Q10's edges cannot move the setpoint. This needs the loop-stability
    re-check that 6b did.

**LED-bank leads twisted tightly (owner), then gate 0 and gate 21 % again, 2026-10-13.**
- Before twisting, one more pass at 3-21 % (plus 6/9/12/15 us test pulses at gate 6) showed
  the same picture. Gate 21 % gave `short` 4 us (0.225 A) and once "no current" (threshold 39).
- After `off`, twisting, `on` and re-arming:
  - gates 0-18 %: no current;
  - gate 21 %, five firings: `0.071 A plateau, peak 0.229 A, 10 us (5 samples)`; "no current"
    (threshold 36); `0.072 A / 0.229 A, 5 samples`; `0.096 A / 0.216 A, 20 us (10 samples)`;
    `0.075 A / 0.240 A, 5 samples`.
  - All `within limits`, `covered`.
- Data: TDS `Photos-1-001 (10)` (gate 0) and `(11)` (gate 21); Saleae
  `C:\Users\ATTAYEKP\Downloads\strobegate21_twistedPair\analog.csv`.

**Saleae on TP4 at gate 21 %, before and after twisting:**

| | straight leads | twisted |
|---|---|---|
| edge spike, 0-0.25 us (max) | 72 mV | **35 mV** |
| overshoot, 0.25-1 us | 0.36-0.39 A | 0.26-0.34 A |
| minimum | 0.055 A at 7-8 us | 0.053 A at 6-8 us |
| step back up | ~9 us | ~8 us |
| settled, 9-20 us | 0.124-0.139 A | 0.10-0.13 A |
| turn-off lobe (min) | -74 mV | **-22 mV** |
| baseline sd / boost ripple | 3.0 mV, intermittent bursts | **2.4 mV**, none |

- **Twisting helped the fast edges and the pickup, as expected. The slow transient is
  unchanged:** overshoot, sag at 6-8 us, then a step to the settled level. That is consistent
  with the gate-loop cause, not the wiring.
- **The firmware's "better" lines are the half-peak artefact again.** A smaller spike lowered
  the threshold to 28-30 codes, which is right at the settled level (~28-31 codes). So
  firing-to-firing the run is 2, 5 or 10 samples, or none. The current did not change.
- **TP3:** the post-pulse kick is now ~+3 V for ~2 us (5 us/div, BW-limited), with a small
  -0.1 V dip at ~3 us. At gate 0 the -0.3 V turn-off dip is unchanged.
- ⚠ **The scope's CH1 now reads ~10x low.**
  - At gate 21 % it shows a ~+5 mV step and a ~+3 mV plateau, where the Saleae and ADC0 show
    a 46 mV overshoot and 13-17 mV.
  - CH1 also now shows ~20 MHz edge ringing, which the 1X probe's ~6 MHz bandwidth used to
    hide.
  - Both fit **the CH1 probe body switch moved to 10X while the menu still says 1X**, most
    likely while the leads were being handled.
  - Owner to check. Set it back to 1X, with live mode off and the rail down.
  - CH2 (TP3) reads correctly (2.08-2.2 V at gate 21 %).
  - **Correction, same day: wrong.** The owner checked, and the probe body and the CH1 menu are
    both at 1X.
    - The signature is that fast edges pass while the µs-scale levels (overshoot, plateau) do
      not. That fits a probe tip with **no galvanic contact with TP4**, seeing TP4 only through
      stray capacitance.
    - Candidates: the hook slipped onto G1 or onto insulation (S2 and G1 are twisted together);
      the S2 lead's joint at TP4 broke while the board was handled; or a damaged probe tip or
      cable.
    - Checks:
      - Scope and Saleae on the **same** firing.
      - Then, unpowered: PROBE COMP on CH1; S2's free end -> R66's CurrentSense pad < 1 Ohm;
        re-seat the hook on bare wire.
  - **Resolved (owner): the CH1 probe was not making good contact.** After re-seating it, the
    scope agrees with the Saleae.

**Gate 21 % with the probe fixed: 20 us and 50 us pulses, 2026-10-13.**
- Firings: four at 20 us and five at 50 us, all `within limits` and `covered`.
  - ADC0 reported either `short 4 us` (0.19-0.23 A, peak 0.25-0.30 A) or "no current"
    (thresholds 31-40 codes). That is the half-peak artefact (section 9): a ~0.1 A, 50 us pulse
    is reported as "no current".
- Data:
  - TDS `Photos-1-001 (12)`: 5 us/div and 10 us/div x2 BW-limited, 250 ns/div with CH1 full.
    CH2 is now at 2 V/div.
  - Saleae `C:\Users\ATTAYEKP\Downloads\strobegate21_twistedPair_50us\analog.csv` (offset
    -53.5 mV removed).
- **Saleae, 50 us pulse:**
  - an overshoot of 0.32-0.34 A at 0.25-1 us, then a decay;
  - a minimum of **0.037-0.040 A at 7-9 us**;
  - a step up at ~9 us;
  - then **steady at 0.09-0.115 A from 10 us to 50 us**, with no second sag;
  - ~1 MHz content <= 0.9 mV (~7 mA).
- **The scope now matches:**
  - TP4 peaks at +44-50 mV (0.33-0.37 A) at ~0.4-0.5 us after a fast edge spike;
  - it decays to ~0-4 mV by 6-7 us;
  - it steps to ~12-14 mV (0.09-0.10 A) at ~9-10 us and holds.
  - The scope still shows ~1 MHz bursts of +-10-20 mV, including **after** the pulse when Q10
    is off. That is boost pickup; the Saleae does not see it.
- **TP3:** ~2.1-2.2 V, with a +0.2 V bump at turn-on and a **+3 V kick for ~2 us after
  turn-off**. CH2 at 2 V/div cannot resolve the ~0.3 V sag seen earlier at 1 V/div.
- **Conclusion:** the gate loop recovers **once** and then holds steady, so it is not a limit
  cycle. What is wrong is the first ~10 us of every pulse: an overshoot to ~3x, a sag to
  ~0.4x, then the settled current.
  - **This matters for the product**, not only the test: §15's strobe pulses are 5-20 us, so
    they sit largely or entirely inside this transient.
  - How it scales with gate level is the next measurement: one step at 24 %.

**6c Step 3, gate 24 % (level 246, TP3 nominal 2.38 V), board 1, 2026-10-13: turn-on OVERSHOOT
3.3 A on a 0.72 A setting. Step 3 STOPPED.**
- Two 20 us firings, both `within limits`, `covered` and `no current above the ~72 mA detection
  floor`, at thresholds of **266 and 265 codes**.
  - A threshold of 266 means the window's peak sample was ~521 codes, i.e. **~3.05 A**.
- Data:
  - Saleae `C:\Users\ATTAYEKP\Downloads\strobegate24_twistedPair_20us\analog.csv` (offset
    -58.3 mV removed);
  - TDS `Photos-1-001 (13)`: CH1 TP4 at 100 mV/div, BW-limited; CH2 TP3 **AC-coupled at
    200 mV/div**, BW-limited; both at 2.5 us/div.
- **Saleae on TP4:**

  | time | current |
  |---|---|
  | 0.5-1 us | **3.30 A** (max sample 451 mV = 3.34 A) |
  | 1-2 us | 2.99 A |
  | 2-3 us | 2.12 A |
  | 3-4 us | 0.74 A |
  | 7-8 us | **0.34 A** (minimum) |
  | ~9 us | steps up |
  | 10-20 us | **0.70-0.74 A**, steady |
- **Scope:**
  - TP4 peaks at ~457 mV (3.4 A), drops steeply at ~3 us, sags to ~0.2-0.3 A by ~8.6 us, then
    steps to ~0.65 A and holds. That agrees with the Saleae.
  - **TP3 jumps up by more than 0.7 V at turn-on** (clipped at the top of the screen), holds
    ~1.5-2.5 us, falls ~0.2 V below its setpoint by ~7 us, and snaps back at ~7.5 us. That
    mirrors the current exactly.
  - The trace after turn-off is scope overload recovery from the +3 V kick; ignore it.
- **Scaling:**

  | gate | overshoot | settled | ratio |
  |---|---|---|---|
  | 21 % | 0.34 A | 0.10 A | 3.4x |
  | 24 % | 3.3 A | 0.72 A | 4.6x |

  0.3 V more on TP3 made the overshoot ~10x larger. Step 3's "stop above 2 A" rule fired, and
  the owner stopped.
- **Mechanism (consistent with every capture; not simulated):**
  - Q9 is a source follower. R65 || R66 (plus Q10) is meant to limit its current,
    I ~= (V_TP3 - Vgs) / R, but only while the gate is **held** as the source rises.
  - U7 (unbiased push-pull) holds the gate only against downward moves: the NPN conducts. Its
    PNP is off across the +-V_BE dead band, and only the LM358 (0.3-0.5 V/us) can bring it in.
  - At turn-on the source rises with the current (3.3 A x ~0.16 Ohm ~= 0.5 V), and Q9's
    ~1.7 nF Cgs carries the gate up with it. TP3's >0.7 V rise matches.
  - So Vgs barely falls, and the current runs to Q9's transfer-curve value at the full setpoint
    Vgs, until the LM358 brings the PNP in (~2-3 us).
  - That correction overshoots, giving the sag; the NPN re-engages at ~8-9 us.
  - Near threshold the transfer curve is steep, so the settled current is a fraction of the
    overshoot.
  - The +3 V TP3 kick after turn-off is the same one-sidedness.
  - This supersedes the gate-21 % "turn-on kick, not fully explained": the "kick" is the source
    rising with the current, not coupling from Q10's edge.
- **Why it is a hard stop:**
  - At the 9 A setting TP3 is ~4.5-6 V. At turn-on, the current would be limited only by Q9 at
    that Vgs and by the LED strings, ballast and wiring: plausibly **tens of amps for
    microseconds**.
  - The firmware's 13 A peak check acts after the pulse, and U5's clamp limits width, not
    amplitude.
  - §15's 5-20 us pulses sit inside the transient.
- **The readback failed the way section 9 predicted:**
  - A 0.72 A, 20 us pulse with a 3.3 A peak was printed as "no current".
  - The staircase counted both firings as passes, so the proven level is 24 % in RAM until
    re-arm.
  - The 13 A peak check would still have acted; the run-based checks (plateau, width,
    stuck-on) were blind.
- **Next:**
  - No live firing on any board until (1) the readback is fixed (section 9) and (2) the gate
    drive is fixed or a safe rework is shown (`NEXT_BOARD_REV.md` **CR-23**), with 6b's
    stability check repeated.
  - Q9's thermal reading for these two firings was not reported; the energy is negligible
    (~3 A x ~3 us).

**Gate-drive fix analysis, 2026-10-13 (owner asked for an in-depth review; full text in
`NEXT_BOARD_REV.md` CR-23).**
- **No rework hook exists:** there is no DNP footprint or 0 Ohm in the gate drive, and the only
  0 Ohm on the strobe sheet is R61 (the pulse path).
- **Recommended: a gate-hold capacitor**, 1 uF X7R from Q9's gate (behind R63) to the
  sense-resistor GND.
  - It cuts every gate disturbance by ~500x: ~20 mV worst case, about 1 % at 9 A, against
    >=0.7 V measured.
  - Small-signal model: phase margin 76-86 deg over TP3 = 1-9 V, against 31-69 deg as built;
    100 nF gives only 39-77 deg.
  - **At TP3, with no R63 in between, the margin is 1-4 deg: it would oscillate.** Never fit it
    there.
- **Board-1 rework:**
  - a wire from Q9's gate lead to a 0805 1 uF sitting on R66's GND pad;
  - not R67's pads (0.65 mm from the gate pad; a bridge bypasses Q10 and the sense);
  - then 6b's stability check, then 6c Steps 2-3 up to 24 % on the Saleae.
- **Next board:**
  - C_hold 1 uF (C28323, already on the BOM);
  - R_iso 100 Ohm;
  - a 0 Ohm gate-stopper footprint;
  - drop U7 (an extended part), with the LM358 driving R_iso and feedback from the op-amp pin
    (modelled PM 78-87 deg);
  - fallback footprints DNP.
  - Net BOM: cheaper.
- Rejected: biasing U7 (impossible on board 1; fights the 5 mA +12 V budget), a pull-down
  (same budget), a faster op-amp (the gate still floats), a current-mode loop (windup with Q10
  open), and firmware-only fixes.
- Firmware impact: the readback fix is still needed. `DAC_SETTLE_MS` and the dry-pulse
  interlock's "decayed to zero" model must be re-derived: the last ~0.6 V now decays with a
  ~30 ms tau, which still leaves it below V_GS(th) at 20 ms.
- **Proposed, owner to decide:** allow live firing **up to 24 %** (already reached) to verify
  the rework, with the Saleae as the truth instrument. Nothing higher until the readback fix.

**Second-model review of the CR-23 analysis (Astra, relayed by the owner, 2026-10-13).**
- **Agrees:** the first trial is 1 µF at Q9's gate, after R63, with U6 and U7 unchanged. Never at
  TP3. The next board is C_hold plus direct LM358 drive through an isolation resistor, with
  feedback before it.
- **Corrections I accept (CR-23 updated):**
  1. U7 *can* pull down. The problem is the transition into pulling down: U7's dead band plus
     the LM358's output-stage crossover, which TI documents as us-scale and which my linear
     model omits.
  2. The phase margins, the 470 nF-2.2 µF range and the ~1 % error are **predictions**.
  3. The disturbance budget is not a worst case. Charge can also arrive through R63 from TP3
     (0.7 V for 3 us -> ~71 mV on 0.6 µF). A collapsing source pulls the gate **down**.
  4. **My stage-2 rework was wrong.** 100 Ohm from U7's base to its emitter pads leaves R60
     sensing after that resistor, not the next-board circuit. Corrected to a 0 Ohm bridge, so
     that TP3 is the op-amp output and R63 is R_iso; deferred until stage 1 is understood.
  5. "The interlock still holds" was overstated. The tail starts only after the DAC ladder has
     fallen (gate maybe ~0.3-0.4 V at 20 ms), and V_GS(th) min is a 250 uA test point. Measure
     the gate voltage and current after a zero command.
  6. Pass criteria are judged on the **gate node and TP4**; TP3 is secondary.
  7. The edge spike is to be investigated, not excluded. The Saleae Logic Pro 8 analog channel
     and the TDS on 1X probes are both ~5-6 MHz, so fast edges need a compensated 10X probe
     or the MSO54B.
  8. 24 % is not a pre-cleared level. Re-climb from gate 3 with each step gated on the measured
     peak (> 2x settled or > 2 A stops). 24 % stays the ceiling until the readback fix.

**Next-board documentation consolidated (owner request, 2026-10-13).**
- `NEXT_BOARD_REV.md` CR-23 now states the full next-board gate drive:
  - C_hold 1 uF, R_iso 100 Ohm, and an R_stop 0 Ohm footprint;
  - U7 DNP with a fitted 0 Ohm (fit-one);
  - R60 sensing before R_iso, with the reason;
  - a DNP compensation footprint, TP3 kept plus a GATE test point, and twisted LED leads.
- **New CR-24, the test-point plan:**
  - the places every bench phase had to probe component pads, and TP2/TP3/TP9/TP10
    clearances;
  - rules: pad size, clearance, a local GND within 5 mm, named nets, silkscreen, clusters;
  - keep/move/rename for TP1-TP10, with **no removals**;
  - additions in P1-P3 priority;
  - a do-not-add list: no TP on `PWR_LowSideD`, no bare VIR TP, no SW-node TP.
- The design doc's strobe section has a known-issue pointer to CR-23 and CR-24.

**Board 1 gate-hold rework fitted, and dry checks on the MSO54B (2026-10-15).**
- **Rework (owner):** 1 µF 50 V X5R from Q9's gate (behind R63) to GND, per CR-23 stage 1. U6,
  U7 and the resistors are unchanged.
- **Data:** CSV exports `F:\strobe\Tek000-009_ALL.csv` (no screenshots, no scrollback).
  - Channels: CH1 TP4, CH2 the gate node, CH3 TP3 (labelled "tp4" on the scope, but it tracks
    the gate, so it is TP3), CH4 "trig".
  - Probes: TPP1000 10X; full bandwidth.
- **What the files are:**
  - Tek000-005: nothing. The gate is at 0 and CH4 shows no pulse; Tek000-003 are identical.
  - Tek006: part of a ramp, with CH3 over-range. Ignored.
  - Tek007: step 0 -> 30 %, 20 ms window.
  - Tek008: `strobe gate 0` from 30 %, 20 ms window.
  - Tek009: the same, 100 ms window.
  - **No pulse captures yet.**
- **Step 0 -> 30 % (Tek007): PASS.**
  - TP3 and the gate follow the DAC ladder together (10-90 % ~5.6 ms, as the 2.62 ms pole
    predicts).
  - Final 2.96 V (nominal 2.97 V). Overshoot +2.5 mV; no ringing.
- **The capacitor is connected and effective.** Regressing gate = TP3 - tau·d(gate)/dt over the
  ramps gives tau ~25-39 us, i.e. **C_eff ~0.5-0.8 µF** through R63 = 47 Ohm. The probe gains
  agree within 0.6 %.
- **Zero-command decay (Tek008, Tek009): measured.** It matches the prediction. Times are from
  the start of TP3's fall, which is within ~0.2 ms of the command.
  - The gate tracks TP3 within +-3 mV throughout.
  - Gate < 1.0 V at 2.8 ms; < 0.6 V at 4.2 ms; < 0.5 V at 6.6 ms.
  - **0.35 V at 20 ms** (the firmware's `DAC_SETTLE_MS` window); 0.27 V at 30 ms; 0.16 V at
    50 ms.
  - **< 0.1 V at ~69 ms.**
- **Static at 30 % (stability): PASS, no oscillation.**
  - Band-limited (~0.4 MHz) RMS is 3.4-4.5 mV on the gate and on TP3, comparable to 6b's
    pre-rework 3.6-3.8 mV.
  - A 47.7 kHz component of ~1.5 mV peak is the **same size on the gate as on TP3**.
    - The gate has ~1 µF to GND (~3 Ohm at 47.7 kHz), so a real millivolt there would need
      ~0.5 mA of AC.
    - And a disturbance driven from TP3 would be ~14x smaller at the gate (R63·C).
    - So it is probe pickup, most likely the boost's light-load burst rate (the ~45-50 kHz seen on
      TP4 before), not the loop.
  - 50 % and 90 % were not captured; 6b's check covers 0/50/90.
- **Still to do:**
  - Q9 current after a zero command (a gate-0 pulse after zeroing, on TP4).
  - 50 %/90 % static.
  - The loaded tests: Step 2, then the climb, with CSVs.

**Board 1 gate-hold rework, loaded verification on the MSO54B (2026-10-15): PASS.**
- **Data:** `F:\strobe\strobe\` — g0, g0zoom (200 ns/div), g21, g24, g24 50us (BW limit on CH4
  only), and g24 50us bwlimit (BW limit on all channels).
  - 100 us windows at 6.25 GS/s; TPP1000 10X.
  - Channels: CH1 TP4, CH2 the gate node, CH3 TP3, CH4 the pulse.
- **Firmware, the same session:**
  - Gate 0 x5: no current, `covered`.
  - Gates 3-18 %: no current.
  - **21 %: `0.100 A plateau, peak 0.103 A, on 20 us (10 samples)`.**
  - **24 %: `0.682 / 0.684 A plateau, peak 0.688-0.689 A, 20 us (10 samples)`.**
  - 24 %, 50 us x5: `0.686-0.690 A`, 25 samples.
  - All `within limits`. The half-peak artefact is gone at these levels, because there is no
    spike to set the threshold.

**Before and after the rework (TP4):**

| | before (2026-10-13) | after (2026-10-15) |
|---|---|---|
| 21 %, first us | 0.34 A overshoot, sag to 0.04 A at 7-9 us | 0.099-0.110 A from 0.1 us; 1-us peak 1.10x (the slow drift up is near-threshold heating) |
| 24 %, first us | **3.3 A for ~2.5 us**, sag to 0.34 A | **0.690-0.712 A from 0.1 us; 1-us peak 1.02x** |
| 24 %, settled | 0.72 A | 0.702 A (scope); firmware 0.682 A, -2.8 % |
| TP3 at the edges | > +0.7 V at turn-on, +3 V after turn-off | -1 to -5 mV during the pulse; nothing after turn-off |
| gate node during the pulse | (not probed) | +2 to +6 mV mean (prediction <= ~20 mV) |

- **Turn-on (g0zoom, g24):**
  - The current rises in **~20 ns** (40-60 ns after U5 Q) and is flat by ~100 ns.
  - At full bandwidth, the first 40 ns carry +-2 A of edge ringing. It is identical at gate 0,
    so it is pickup; with the 20 MHz limit it is gone.
  - At 40-80 ns there is a ~0.5 A, ~40 ns lobe (~19 nC). It is **also present at gate 0**, where
    no LED current can flow. It is Q10's gate charge returning through the sense resistors, not
    LED overshoot.
- **The "noise" on the 24 % plateau is boost pickup, not current:**
  - A ~1.03-1.08 MHz component, ~0.02 A apparent on TP4. That is the LM5157's switching
    frequency (1.055 MHz design).
  1. With the BW limit on all channels, the same component is there **before** the pulse, with
     Q10 off (0.021 A pre-pulse against 0.024 A on the plateau).
  2. The gate node shows ~3 mV of it. But the gate has 1 µF to GND (0.16 Ohm at 1 MHz), so a
     real 3 mV would need ~20 mA of AC into the capacitor. It is in the probes; TP4 correlates
     with it (r ~0.9 on g24).
  3. It grows through a long pulse and continues after it (the gate shows +-50 mV after the
     50 us pulse, with Q10 off), as the boost works to refill VIR.
  4. ADC0's samples on the plateau stay within ~1-2 codes. A real +-20 mA at 1.06 MHz would
     alias to ~+-3-4 codes.
  5. VIR ripple can reach the LED current only through Q9's output conductance and capacitance:
     well under 1 mA.
- **CR-23 stage-1 pass criteria, all met:**
  - the gate holds;
  - no overshoot;
  - settled by ~0.1 us;
  - the rise is faster than before;
  - no oscillation (the 1 MHz is pickup);
  - the edge spike was investigated.
- **Next:**
  - **6d at 24 %** (0.68 A measured, inside 0.5-2.5 A).
  - Then the firmware readback fix, with review, before anything above the 24 % ceiling.
  - Q9's temperature for this session was not reported.

**6d — the U5 clamp with current, board 1, gate 24 % (2026-10-15): PASS.**
- **Firmware** `14:43:50`, after the 24 % firings above. Two `strobe clamptest 1000` at gate
  level 246 (24.02 %, TP3 nominal 2.38 V):
  - `0.687 A plateau, peak 0.700 A, on 134 us (67 samples)` and
    `0.689 A plateau, peak 0.701 A, on 134 us (67 samples)`.
  - Both `completed, IRQ0 seen`, `within limits`, `covered` (brackets 16/16, residual -6 / +1).
  - Measured charge 0.092 mC (1.23 mC booked for the full 1000 µs).
  - enable->IRQ0 1018 µs: the PIO ran its full 1000 µs command. Only the current is clamped.
  - ADC0's record ran ~1250 µs past the edge (627 samples), beyond the 1000 µs command, and found
    one run.
- **Scope:** `F:\clamptest1000_ALL.csv`, one of the two firings (which one is not recorded).
  - MSO54B, -40 to +360 µs at 3.125 GS/s; TPP1000 10X.
  - Channels: CH1 TP4, CH2 the gate node, CH3 TP3, CH4 U5 Q (R61).
- **Results:**
  - **U5 Q high 134.98 µs.** That is the dry clamp (135 µs on the LA, 2026-10-06), unchanged
    with current.
  - **TP4's current is 135.0 µs wide** (50 % of the plateau, 1 µs average). It starts 55 ns after
    U5 Q rises and ends 82 ns after U5 Q falls; it falls from 0.72 A to ~0 in ~150 ns.
  - **No current after it:** from +136 µs to the end of the window (+360 µs), the 1 µs average
    stays within -30 / +6 mA.
    - The small negative offset (-5 to -17 mA in 10 µs blocks) cannot be current: Q10 is off,
      and nothing can drive current backwards through the sense resistors.
    - Most likely it is a ground shift while the boost refills VIR.
  - **Plateau 0.703 A (94.9 mV above the pre-pulse baseline), 20-100 µs.** The firmware's
    0.687/0.689 A is -2.2 %, inside +-5 %; the 24 % 20 µs pulses read -2.8 %. The firmware's
    "TP4 should read 93 mV" agrees within 2 %.
  - **Shape:**
    - 0.68 A over 1-5 µs, rising to 0.70 A by ~20 µs, then 0.70 +- 0.01 A to the end (1 µs
      averages 0.694-0.712 A).
    - The +3 % creep comes with the gate steady, so it fits Q9 warming (its threshold falls with
      temperature). The same creep was seen at 21 %.
    - Turn-on: the known ~0.85 A Q10 gate-charge lobe at +80 ns (also present at gate 0); 0.69-
      0.73 A on a 100 ns average from +120 ns. No overshoot.
    - 20 MHz dips of ~0.1 A every ~0.92 µs (~1.08 MHz): the boost pickup characterised above.
      The 1 µs average does not show them.
  - **The gate node and TP3 hold.**
    - Across the whole window, both edges included, the 1 µs average stays within -5 / +5 mV on
      the gate and -9 / +5 mV on TP3.
    - The 20 MHz extremes (gate -57 / +34 mV, TP3 -62 / +47 mV) are spikes. A real 57 mV in tens
      of ns on 1 µF would need amps.
    - Before the rework, TP3 kicked +3 V after turn-off.
- **Against the 6d pass criteria (`BENCH_P6_STROBE.md` 6d), all met:**
  - ADC0's on-time is 134 µs, against the U5 clamp's 135 µs +- 2;
  - TP4 is the same width (135.0 µs), not 1 ms;
  - the plateau matches the 20 µs pulse fired just before it, 0.678 A (+1.3 to +1.6 %; the long
    pulse includes more of the warm-up creep). *Corrected the same day: this first compared
    against the earlier 24 % pulses (0.682/0.684 A, +0.7 %); the serial log shows the
    `strobe pulse 20` just before 6d.*
  - no fault.
- **Not to be repeated:** 6d is a one-off (BENCH 6d). 0.70 A was the first level inside
  0.5-2.5 A. U5's clamp acts on Q10's gate drive and does not depend on the LED current.
- **Next:**
  - The firmware readback fix (§9 "Half-peak threshold"), with review, before anything above
    24 %.
  - No FLIR on 2026-10-15 (next entry).

**VIR measurement plan, and two corrections (2026-10-15).**
- **Owner's question:** after the firmware fix and the staircase, measure the charge the VIR caps
  really deliver instead of using the theoretical value? **Yes.** Planned as §3 Q13 (method
  there).
  - `BURST_CHARGE_MAX_MC` rests on ~670 µF × ~9 V, neither of them measured.
  - The useful result is a sag model (C, ESR, boost refill, headroom at 9 A), not C alone.
- **Correction 1: `BENCH_P6_STROBE.md` Step 5 was wrong.**
  - It said that the first -> last plateau change records "the VIR bulk-cap droop".
  - It cannot: Q9 regulates the current, so the plateau stays flat until the headroom is gone.
  - Step 5 now says so and points to Q13.
- **Correction 2: the FLIR was missing.**
  - The 6c board state requires the FLIR on Q9 and HS1 **before the first pulse**. The owner had
    none on 2026-10-15, so today's 21/24 % firings and 6d ran without it.
  - I asked for Q9's temperature afterwards, but did not check the board state before the
    firings.
  - Exposure was small. The worst case is 0.7 A × 36 V × 135 µs ≈ 3.4 mJ per pulse, at least
    100 ms apart (≤ 34 mW average).
  - **No live firing above 24 % without the FLIR.**

**Live readback fix written (2026-10-15): under review, not flashed.**
- **Why:** §9 "Half-peak threshold", the 2026-10-13 finding.
  - A turn-on spike hid the pulse under it: "no current" at 24 % under a 3.3 A spike.
  - The same rule hid a stuck-on current under any larger spike.
- **What changed** (`strobe_live.c`, `strobe.c`, `cli.c`, `board.h`):
  - **Pulses are runs above an absolute floor**, baseline + 12 codes (~72 mA). Every check uses
    them: stuck-on, width, overcurrent, the staircase, `strobe cal`.
  - **Per pulse**, besides the plateau (the mean, as before):
    - the **settled level**: the median, edges dropped;
    - the **held level**: the highest level held for two samples in a row.
  - **OVERCURRENT** adds "held over 10.8 A" to "plateau over 10.8 A" and "a sample over 13 A".
  - **A new verdict, SHAPE**, for pulses of ≥ 4 samples:
    - the trigger: a sample above settled × 1.25 + 0.14 A, or a sample other than the edges
      below settled × 0.75 − 0.14 A;
    - it ends live mode with no fault and prints `NOT CLEAN: pulse N OVERSHOOT|DIP -- ...`.
  - **NOISY and SHAPE exit as** "the current was not one clean pulse per pulse fired". NOISY used
    to print "ADC0 readback unusable".
  - **The clamp width and the held level are now taken over every run.** Found while writing the
    fix: only the first 16 runs are stored, and the width came from those alone. A long 17th
    conduction read as NOISY instead of latching STROBE_CLAMP.
  - **Printed wording:**
    - `floor N codes` replaces `threshold N codes`;
    - "no current pulse above the ~72 mA floor" replaces "no current above the ~72 mA detection
      floor", with the highest lone sample added when one was above it.
  - **Two small fixes slated for the next build:**
    - the `fresh/HELD` label on `stat`'s 5V_IN line comes from the read's status;
    - `adc5vcal` trims the IDLE ring's reading (the one the power FSM uses), and refuses
      outside IDLE.
- **Host tests: 114/114** (102 before). The new ones include:
  - a replay of the **real** 21 % `strobe wave` from 2026-10-13: 20 µs found, then SHAPE;
  - a reconstruction of the 24 % firing: found at 0.72 A settled, then SHAPE;
  - stuck-on currents under spikes: STUCK_ON;
  - a creeping pulse with its mean under 10.8 A and its held level over: OVERCURRENT;
  - the shape bounds to ±1 code;
  - records shaped like the 2026-10-15 clean pulses: OK, readings unchanged.
  - Three existing tests changed expectation: `single_pulse` (the floor), `dips_merge` (a
    2-sample dip to zero inside a 9 A pulse is now SHAPE) and `verdict_texts`. *Narrowed by
    review round 1: the dip bound bites only above a settled ~0.19 A. See the next entry.*
- **Mutations: 16 of 16 caught**, one per rule, each applied alone. The list is in the review
  brief.
- **Image (provisional; the final build follows the review):**
  - Release `Oct 15 2026 15:15:22`;
  - 196128 text / 0 data / 123212 bss;
  - SHA256 `7581DA0E…6B1EA0F6`;
  - a clean build with no warnings.
- **Review:** the brief and the patch are in `captures/review_2026-10-15/` for Sol. They also ask
  for the round-7 confirmation from 2026-10-12, which was never logged.
- **Correction (§9 `adc5vcal` item, 2026-10-12):** it called the blocking read the path "at
  boot". Wrong: `main.c` sets the ADC to IDLE before `power_fsm_init()`, so the power FSM always
  reads the ring. The blocking read is used only after `adcmode off`.

**Live readback fix, independent review round 1 (2026-10-15): four findings, all fixed in r2.**
- **Sol's review:** `captures/review_2026-10-15/SOL_REVIEW.md`. Its reproduction cases ran on the
  unmodified code. Decision: keep the image on hold.
  1. **Safety:** the plateau overcurrent still depended on the 16-run storage limit.
     - A 17th run alternating 10.5 / 11.7 A (mean 11.1 A, held 10.5 A) read NOISY, not
       OVERCURRENT, because its mean was computed after the storage return.
     - **Fixed:** the plateau maximum now covers every run.
  2. **Correctness (round-7 code):** the capture commands accepted any rate.
     - Below 733 Hz the divider wrapped: `capture 1 32 100` ran at 2259 Hz under a 100 Hz label.
     - **Fixed:** rates outside 733–500000 Hz are refused before the ADC is touched, and the
       header's `rate=` is the programmed rate.
     - Requests above 500 kHz were silently the maximum before; they are refused now too.
  3. **Correctness (round-7 code):** `adc_ring_snapshot()`'s first pass leaned on the
     synchronising read, which came before its lower timestamp.
     - A second issue between the two would have been accepted outside its bracket.
     - It is an ordering counterexample, not a board failure, and the hardware-flag proof was
       unaffected.
     - **Fixed:** a timestamp, then an unchanged index read, before any change is accepted.
       Checked in the disassembly.
  4. **Minor:** a NO_DATA record printed "no current pulse".
     - **Fixed:** it prints NO MEASUREMENT, and a record not proven complete never claims no
       current.
- **Round 7 of 2026-10-12, now logged:** confirmed (DIV = 0, the 96 floor, the SRAM snapshot),
  qualified by findings 2 and 3. Both are fixed in r2.
- **Policy points from the answers:**
  - **The overcurrent rules are SAMPLED, 2 µs apart.** Alternating 9 / 12 A passes them:
    - the mean is 10.5 A;
    - every sample is under 13 A;
    - the ripple is inside the shape bounds.

    A test pins this as the policy as written. **Owner's call** whether to tighten it, e.g. to
    2 of any 3 samples over 10.8 A.
  - **Correction:** in round 1, and to the owner, I said a dip to zero of up to 4 µs inside a
    pulse now ends live mode. Too broad.
    - It holds only above a settled level of ~0.19 A (32 codes). Below that the lower bound sits
      under the baseline.
    - Runs of fewer than 4 samples are not judged.
    - A test now pins this.
  - **Logged for later (§9):** a zero reference for the constant-current blind spot, and one
    +5V_IN path before Pi integration.
- **Host tests 117/117. Mutations: 21 of 21 caught** (the 16 from round 1 re-run, plus 5 new).
- **Image r2 (provisional):** Release `Oct 15 2026 21:23:39`, 198224 text / 0 data / 123212 bss, SHA256
  `3B93E2F0…E83E9B7B`. Clean build. Not flashed.
- **Round-2 package for Sol:** `captures/review_2026-10-15/REVIEW_RESPONSE_2.md` and
  `fix_2026-10-15_r2.patch`.

**Live readback fix: review round 2 PASS; two-of-three overcurrent added at the owner's choice (2026-10-15).**
- **Sol, round 2** (`captures/review_2026-10-15/SOL_REVIEW_2.md`): PASS under the documented
  policy.
  - All four round-1 findings are closed, and the round-7 qualifications with them.
  - Sol independently checked:
    - every integer rate from 733 to 500000 Hz;
    - the 17th-run case;
    - the snapshot's read order in the ELF;
    - the r2 image's stamp, size and hash.
- **Policy:** Sol recommended latching OVERCURRENT when two of any three consecutive samples
  exceed 10.8 A, keeping the existing checks. **The owner chose it.** I had first recommended
  keeping the rule as written, then agreed with Sol before the climb to 9 A.
- **Done in r3:**
  - the rule runs over the whole record, independent of runs and storage;
  - the CLI names the rule that fired;
  - `board.h` lists all four rules.
  - Alternating 9 / 12 A, which passed r2, now faults.
- **Host tests 117/117:** `policy_two_of_three` replaces the r2 policy test.
- **Mutations:** 7 new, all caught. Of the earlier 21, 20 were caught; 1 is now equivalent,
  because two of three implies the two-in-a-row check. That check is kept on purpose.
- **Image r3 (provisional):** Release `Oct 15 2026 22:09:11`, 198840 text / 0 data / 123220 bss, SHA256
  `FCC1AF66…18195B57`. Not flashed.
- **Round-3 confirm package for Sol:** `REVIEW_RESPONSE_3.md`, `fix_2026-10-15_r3.patch` and
  `r2_to_r3.diff`.

**Logic analyser or scope for the next 6c steps (owner's question, 2026-10-16).**
- **The Saleae alone covers Steps 1–4 on board 1:** the reflash checks, the climb above 24 %, and
  `strobe cal 9`. The 6c board state already allows its analog channels on TP4 and TP3.
  - Everything those steps judge is at µs scale, inside its ~5 MHz: turn-on overshoot (the
    2026-10-13 spike lasted ~2.5 µs), sag, the plateau against ADC0 (4.9 mV is 1.8 % of TP4 at
    2 A and 0.4 % at 9 A), and TP3 holding.
- **This supersedes the 2026-10-07 note** that "the scope matters more" for the rest of 6c:
  1. TP4's first ~150 ns is now characterised (2026-10-15). The ±2 A edge ringing and the 0.5 A,
     40 ns lobe are both present at gate 0, so neither is LED current.
  2. **On board 1, R63 (47 Ohm) and the 1 µF rework low-pass TP3 into Q9's gate** at ~4–7 kHz
     (C_eff 0.5–0.8 µF). A parasitic above the Saleae's bandwidth at TP3 is cut by ~60 dB before
     it reaches the gate, so it cannot appear in the current.
  - The firmware's SHAPE check also catches a spike or dip beyond ±25 % in ADC0's own samples.
  - Boards without the rework do not get argument 2.
- **The scope is required for VIR and VIR_RTN** (§3 Q13, and the Step 5 droop). They sit at
  36 V, while the Saleae's analog inputs are ±10 V (12 V absolute max). Use the MSO54B, or the
  TDS 1002 with 10X probes. **Never the logic analyser.**
- A scope look at the gate's static stability at 50 % stays optional.
- **Correction (`BENCH_P6_STROBE.md`, Step 3 STOPPED box):** the stop rule read "a turn-on peak
  above 2× settled, or above 2 A, on the scope".
  - The 2 A cap was written for the re-climb to 24 %, where currents were under 1 A. Read
    literally above 24 %, it would stop the climb at 2 A.
  - My 2026-10-15 edit had also narrowed "on the Saleae or the scope" to "on the scope".
  - Now: up to 24 %, 2× or 2 A; above 24 %, a peak more than ~20 % above settled (Step 3's
    ringing criterion); either instrument.
- **Follow-up, the same day:**
  - **The TDS 1002 on 36 V:** fine. 36 V DC is far inside its 300 V RMS input rating and its
    P2220 probes' (300 V RMS at 10X, 150 V at 1X).
    - Use 10X after PROBE CHECK.
    - **Grounds only on the R66 GND wire, never on J3 pin 2** (VIR_RTN). The scope's ground is
      mains earth, so a ground clip there joins the LED return to ground. That bypasses Q9 and
      Q10 and lights the bank from the bulk caps.
    - For a single pulse's sag, AC coupling at 100–200 mV/div with averaging. Its 8 bits give
      4–8 mV steps.
    - AC coupling's low corner (≤ 10 Hz at the BNC, ~1 Hz with 10X) tilts a 20–40 ms burst
      record, so prefer the MSO54B for burst-length droop.
  - **The gate node can replace TP3** on board 1.
    - Since the 1 µF rework the gate is the voltage that sets the current, and the CR-23 criteria
      already judge it.
    - TP3 is upstream of R63's ~5 kHz filter. It is now diagnostic: add it only if the gate
      moves, to tell a loop disturbance from one coming back through Q9.
    - On a board without the rework, TP3 and the gate are the same node at µs scale.
  - **R61 (U5 Q) can replace R33 (GPIO25).** It is the pulse Q10 actually receives, and the
    current follows it within ~75 ns.
    - 6a/6b/6d proved U5 passes pulses below its ~135 µs clamp unchanged.
    - R33 only distinguishes a commanded pulse from a U5 misfire. Add it back if a firing shows a
      pulse nobody commanded.
    - Use R61 as the scope's trigger.
  - `BENCH_P6_STROBE.md` 6c is updated to match: the Step 3 pass and stop rules, the probe row,
    the triggers, and the VIR ground warning.

**Live readback fix: review round 3 PASS; handover (2026-10-16).**
- **Sol, round 3** (`captures/review_2026-10-15/SOL_REVIEW_3.md`): PASS, no blocking finding.
  Sol confirmed:
  - the two-of-three loop bounds: every pair one or two positions apart, both record edges
    included;
  - its independence from runs and storage;
  - the priority: STUCK_ON outranks it, and it outranks NOISY and SHAPE;
  - the CLI reason line.
- **Sol's independent probes** covered 1,024 pulse patterns, 582 pair placements, the exact
  threshold (equality does not count) and the 17th pulse.
- Sol also confirmed the surviving two-in-a-row mutant is equivalent.
- **Handover image:** Release `Oct 15 2026 22:09:11`, 198840 text / 0 data / 123220 bss, SHA256
  `FCC1AF662923537ECEE27ACB697683E592E8795139277A68E526818518195B57`.
  - This is the reviewed r3, not rebuilt. The source still matches `fix_2026-10-15_r3.patch`
    byte for byte.
- **What is in it** (the 2026-10-12 image, plus):
  - the fixed 72 mA floor;
  - the pulse-shape check (SHAPE ends live mode);
  - overcurrent on a plateau, on two of any three samples, or on one sample over 13 A, over
    every run;
  - the clamp width over every run;
  - NO MEASUREMENT for an unusable record;
  - capture rates limited to 733–500000 Hz, with the programmed rate in the header;
  - the snapshot's first-pass bracket;
  - `stat`'s fresh label;
  - `adc5vcal` on the ring.
- **Bench re-check, reflash first** (§10):
  - `id`; `adc ringtest 100`; 6c Steps 1–2 at gate 0;
  - the staircase to 24 %, which must read as on 2026-10-15;
  - then Step 3 above 24 %.

**Readback-fix image on board 1, Session A (dry): PASS (2026-10-16).**
- The owner reflashed board 1 (`bootsel`, then the UF2).
  - `id`: `built    : Oct 15 2026 22:09:11`.
  - The last `fw` line: `+ live readback: fixed floor, pulse shape check, two-sample
    overcurrent (2026-10-15)`.
  - uid `a764f5332ca5ac53`, board 1.
- **`stat`:** STANDBY, no fault, `5V_IN 5.152 V fresh` (scale 1.0634, the saved one).
  - The `adc` line reads `5V_IN fresh`, with no "last sampled" line: the label fix works.
- **`adc ringtest 5`:**
  - 2 µs per sample (`+400:200 +800:400`);
  - first sample at +2 to +3 µs, 100 written by +199 µs;
  - first samples 9–12 codes (ADC0 at 0 A);
  - 10/10 pairs covered, brackets 14/14 cycles, residual −5 to +8 (window ±34).
- **`adc ringtest 100`:** **200/200 covered**, 0 snapshot failures.
  - Widest bracket 14 cycles, against 15–16 before the snapshot fix.
  - Residual −8 to +8; 100 written by ≤ +201 µs.
  - The changed first-pass bracket logic costs nothing measurable.
- **`capture 1 32 100`:** refused, `want 733-500000 Hz`. The rate fix, confirmed on the hardware.
- **Not run:**
  - `adc 0 4096`; the ring's first samples (9–12 codes) show ADC0 at rest as before.
  - The optional `adc5vcal`.
- **Next: Session B** (live, FLIR required): 6c Steps 1–2, then the staircase to 24 %, which must
  read as on 2026-10-15.

**Session B, step 8: the gate-0 live firing (2026-10-16): PASS, on the Saleae and in the firmware.**
- **Data:** `C:\Users\ATTAYEKP\Downloads\newFW_strobegate0\`, 50 MS/s, −1 to +2 ms around the
  edge.
  - A1: the gate node. A2: TP4. Digital: U5 Q on R61.
- **Firmware, `strobe pulse 20` at gate 0:**
  - `completed, IRQ0 seen`; `GPIO25 after: low, SIO`.
  - Baseline 10.8 codes (0.065 A), **`floor 22 codes`**, `within limits`.
  - `covered`: 135 samples, brackets 14/14, residual −1 of ±34; both baseline splits 76.
  - **`no current pulse above the ~72 mA floor`**, with no lone-sample line.
  - Arming read `LIVE: ARMED … Watchdog ARMED (1 s)`.
- Step 1's refusal checks were not repeated. They test arming code this build did not change,
  and they passed on 2026-10-12.
- **U5 Q** was high for **20.000 µs**.
- **Both analog channels sit at about −87 mV** (gate −89.2, TP4 −85.7).
  - That is the Saleae's DC offset; it showed −74 mV on 2026-10-13.
  - A real −86 mV at TP4 would be −0.64 A flowing backwards through the sense resistors, and
    nothing on the board can drive that.
  - Each channel is read against its own baseline.
- **They also share a common-mode signal.** 10.6 mV RMS appears on both channels with r = 0.996
  to 0.999 at unity gain (1.01), mostly 2–3 kHz, with a few −15 mV dips lasting a few µs.
  - This is the shared-ground signature HANDOFF §8 describes for this analyser.
  - With it removed, TP4's own noise is 0.4–0.9 mV RMS (1–20 µs averages).
- **No current at gate 0:**
  - inside the pulse TP4 moved +4.10 mV, and the gate +4.12 mV, the common mode. The difference
    is under ~1 mV, i.e. under ~10 mA;
  - the only TP4 features are at +94 ns after each U5 Q edge: +37 mV at turn-on and −31 mV at
    turn-off. These are Q10's gate charge through the sense resistors, smoothed by the Saleae's
    ~5 MHz. The scope saw the same at gate 0 on 2026-10-15.
- **How plateaus are read from here on:**
  - each channel against a LOCAL baseline taken just before and after the pulse, which takes
    out the slow common mode;
  - a firing with one of the −15 mV dips inside its pulse is flagged rather than read.
  - Optional: run the laptop on battery for these captures. If the 2–3 kHz goes away, it was a
    charger ground loop.

**Session B, step 9: the re-check staircase to 24 % (2026-10-16): PASS. The live readback fix
is bench-verified.**
- **Data:**
  - the scrollback;
  - Saleae captures `C:\Users\ATTAYEKP\Downloads\newFW_strobegate21\` and
    `...gate24\`, with A1 the gate node, A2 TP4, D3 U5 Q, at 50 MS/s.
- **Before the climb, live mode had ended on its idle timeout.** It had been more than 5 minutes
  since step 8.
  - The notice `*** LIVE MODE ENDED: idle timeout ***` printed before the next command, as the
    2026-10-12 fix intends.
  - The gate was then set to 3 % in dry mode, and `strobe live on confirm` was **REFUSED** (gate
    not at zero). That re-proves Step 1's arming refusal on this image.
  - After `strobe gate 0`, live mode re-armed, and a fresh gate-0 pulse read no current.
- **The firmware, one `strobe pulse 20` per step:**
  - **3–18 %:** `no current pulse above the ~72 mA floor` at every step (15 % was fired twice).
    Every firing was `within limits` and `covered`, with brackets 14/14 and residuals −8 to +3.
  - **21 %:** `0.107 A plateau, peak 0.108 A, on 20 us (10 samples)`. 2026-10-15 read 0.100 A.
  - **24 %:** `0.702 A plateau, peak 0.705 A, on 20 us (10 samples)`, "TP4 should read 95 mV".
    2026-10-15 read 0.678–0.684 A.
  - No `NOT CLEAN`, `OVERCURRENT` or `LIVE MODE ENDED` line during the climb.
- **The Saleae, each channel against a local baseline:**
  - **21 %:** TP4 15.3 mV = **0.114 A**, against the firmware's 0.107 A: −7 mA, inside ±0.1 A.
    No overshoot: the first 100 ns averages 0.18 A (the edge lobe), then 0.09–0.12 A.
  - **24 %:** TP4 95.1 mV = **0.704 A**, against the firmware's 0.702 A: **−0.3 %**. Flat from
    ~0.1 µs; 1 µs averages 0.69–0.72 A; no overshoot.
  - U5 Q high 20.000 µs at both levels. TP4 back to ~0 after the edge.
  - **The gate node held:** −2 to −3 mV on average during the pulse, at worst −6 mV. That is
    at the resolution the analyser's common mode allows; the scope saw +2 to +6 mV on 2026-10-15.
  - No shared dip inside either pulse; the worst was −6 mV.
- **Both levels read a little higher than on 2026-10-15** (+7 % at 21 %, +3 % at 24 %). Near Q9's
  threshold the current moves with temperature, ~0.02 A per °C at 24 %. Today's 0.70 A also
  matches what the scope read on 2026-10-15 (0.703 A).
- **FLIR after the climb (owner):**
  - **Q9 and HS1: 23 °C, room temperature.** A 20 µs pulse at 0.7 A puts ~0.3 mJ into Q9.
  - **D4, the 12 V zener: 40 °C**, the warmest part nearby. It is expected, and a good sign:
    - R15 (4.7 kOhm) carries ~5.0 mA from 36 V. The +12 V loads take ~1–2 mA, so the zener
      (BZT52B12, SOD-123) sinks ~3.5 mA, ~40–45 mW, all the time VIR is up.
    - That puts it ~15–18 °C over ambient, under a tenth of its rating.
    - A warm zener is regulating, so the +12 V rail is not overloaded.
    - Baseline for later: if D4 cools toward room temperature with VIR up, check TP2
      (12.31 V before).
  - R15 dissipates ~120 mW, half its ¼ W rating, and runs warm too.
- **Next, step 10:** 6c Step 3 above 24 %, to ~2–3 A, then stop and report before `strobe cal 9`.

**Session B, step 10: 6c Step 3 above 24 %, to 30 % (2.9 A) (2026-10-16): PASS.**
- **Data:**
  - the scrollback: a re-climb at 21 % and 24 % after re-arming, then 27 % and 30 %;
  - Saleae captures `...\Downloads\newFW_strobegate27\` and `...gate30\`.
- **Readings:**

  | gate (TP3 nominal) | firmware | Saleae TP4 | difference | step |
  |---|---|---|---|---|
  | 21 % (2.08 V) | 0.110 A, peak 0.115 | — | — | — |
  | 24 % (2.38 V) | 0.704 A, peak 0.710 | — | — | +0.59 A |
  | 27 % (2.67 V) | **1.685 A**, peak 1.702 | 227.5 mV = **1.685 A** | **0.0 %** | +0.98 A |
  | 30 % (2.97 V) | **2.915 A**, peak 2.937 | 391.7 mV = **2.901 A** | **+0.5 %** | +1.23 A |

  - Every firing was `within limits`, `covered` and 20 µs (10 samples), with no `NOT CLEAN` or
    `OVERCURRENT` line. U5 Q was high 20.000 µs.
- **Shape (Saleae):**
  - the current is up within ~200 ns, with no overshoot (the first µs never exceeds the plateau);
  - it is then flat, creeping +1 to +1.5 % over 20 µs, as Q9 warms;
  - it is back to ~0 after the edge.
- **The gate node held:** −3.7 mV (27 %) and −5.0 mV (30 %) during the pulse.
  - The small dip grows with current (−2 mV at 0.1 A, −5 mV at 2.9 A). The likely cause is the
    drain's fall coupling through Q9's Cgd into the 1 µF.
  - At 9 A, expect ~−10 mV, about 1–2 % of the current.
- **FLIR:** no change at any step. Q9 and HS1 stay at 23 °C; D4 at 40 °C.
- **Correction:** my rough prediction (~1.5 A at 27 %, ~2.5 A at 30 %) came in 12–19 % low.
  - It was a square-law fit through the two near-threshold points (0.1 and 0.7 A). Q9's curve
    steepens above them faster than a square law.
  - Refit on 27 and 30 %, with 0.155 Ohm in the source (R65||R66 and Q10): k ≈ 14 A/V²,
    V_th ≈ 2.06 V.
  - That puts **9 A near 43–45 % (TP3 ≈ 4.25–4.4 V)**, a little under the design's 4.5–6 V: a
    finding for Step 4, not a failure. The source resistance caps each 3 % step at
    0.30 V / 0.155 Ohm ≈ 1.9 A; expect ~1.3–1.6 A per step from here.
- **Next:** Step 4, `strobe cal 9`, with the FLIR. Afterwards, check the solved level with a few
  `strobe pulse 20` on the Saleae (TP4 = 1.215 V ± 5 %), and read the gate node on the DMM.

**Gate settling with the 1 µF (owner's question, 2026-10-16): rising steps settle; the falling
tail is slower but harmless.**
- **Every gate change blocks for `DAC_SETTLE_MS` (20 ms)** in `strobe_gate_set()`, for a typed
  `strobe gate` and for each `strobe cal` step alike.
  - In the cal, the next firing also waits for the 100 ms minimum spacing, so each step fires
    ~95 ms after its gate change.
- **Rising:** the DAC ladder's dominant pole is 2.62 ms.
  - After 20 ms, ~0.06 % of a step remains: 0.2 mV of a 0.30 V (3 %) step, ~1 mA at 30 %.
  - The 1 µF adds only R63 × C_eff ≈ 25–40 µs, ~1 % of the ladder's τ. U7 needs ~0.1 mA to slew
    it at the ladder's rate.
  - **Bench evidence:**
    - 2026-10-15 (Tek007): a 0 → 30 % step (≈ 3 V, 10× a staircase step) had the gate tracking
      TP3 within ±3 mV and landing at 2.96 V against 2.97 V nominal (+2.5 mV overshoot).
    - 2026-10-16: repeat firings at the same level agree (24 %: 0.702 and 0.704 A), and the
      Saleae read the gate at nominal before each pulse (27 %: 2.669 against 2.668 V).
- **Falling to zero is the slow direction.** The gate drops fast to ~0.6 V, where U7's PNP stops
  pulling, then decays with τ ≈ 38 ms: **0.35 V at 20 ms** from 30 % (measured), ~0.4 V from
  100 % (estimated).
  - That is safe for the dry-pulse interlock, which counts the gate as zero 20 ms after zeroing.
    The staircase shows Q9 passes nothing measurable with its gate up to 1.78 V (18 %), and the
    27/30 % fit puts V_th near 2.06 V.
  - It closes CR-23's open "current at that gate voltage" item.
  - A cal's first point fires ~100 ms after zeroing, at ~0.04 V.
- **Stale comment (§9):** `strobe_plan.h` says the 20 ms leaves "9.9 V decays to ~5 mV". That is
  true for TP3, not for Q9's gate on a board with the hold capacitor.

**6c Step 4, `strobe cal 9` (2026-10-16): PASS. 9 A at level 439 (42.87 %), TP3 4.233 V on the DMM.**
- **Data:**
  - the whole cal as one 8.5 s recording, `...\Downloads\newFW_strobegate_cal9\` (11.1 GB,
    read by seeking to each firing);
  - a separate 9 A pulse, `...\newFW_strobegate_9A\`.
  - The firmware's output for **four cal runs**, and the DMM at the solved level.
- **The firmware:**

  | run | solved | confirmations (A) | mean |
  |---|---|---|---|
  | 1 | 439 (42.87 %) | 9.017 / 9.017 / 9.021 | 9.018 A (+0.2 %) |
  | 2 | 439 | 9.019 / 9.021 / 9.020 | 9.020 A (+0.2 %) |
  | 3 | **438** (42.77 %) | 8.974 / 8.973 / 8.972 | 8.973 A (−0.3 %) |
  | 4 | 439 | 9.024 / 9.023 / 9.021 | 9.023 A (+0.3 %) |

  - Every run reports `every pulse within 5 % -- PASS`.
  - The crossing sits at the rounding boundary. The interpolation gives level 438.50–438.56
    between 435 (~8.83 A) and 440 (~9.07 A), so 438 once and 439 three times. One level is
    ~0.05 A.
  - **Repeatability:** each level reads within ±3 mA across the four runs (440: 9.070–9.074 A;
    300: 2.626–2.631 A). Fine steps rise from +0.07 to +0.25 A, all ≤ 0.3 A.
  - `strobe pulse 20` ×3 at 439 read 9.018–9.020 A (peaks 9.068–9.069), `within limits`,
    `covered`. The Saleae read the same pulses ~0.4 % lower (8.98 A); the band is ±5 %.
- **The DMM at level 439: 4.233 V.** That is 3 × the DAC's measured 3.29 V reference ×
  439/1024 = 4.231 V, within 2 mV. The firmware's "nominal 4.24 V" assumes 3.3 V.
  - **9 A needs 4.23 V on the gate, below the design's 4.5–6 V:** a finding, not a failure.
    This Q9 conducts more per volt of gate than the design assumed (V_th ≈ 2.06 V,
    ~4.9 A/V at 9 A).
- **The climb:**
  - 59 firings, 102.3–102.4 ms apart, each U5 Q 20.000 µs.
  - Coarse steps of ~0.19 V (TP3) found nothing up to ~1.94 V (0.02 A, under the 72 mA floor).
    First detection at ~2.14 V: 0.159 A.
  - Then fine steps of +0.07 A rising to +0.26 A each, all under the design's ≤ 0.3 A. It
    crossed 9 A between 8.785 and 9.027 A.
- **The three confirmation pulses:** 8.978, 8.975, 8.990 A. **Mean 8.981 A (−0.2 %)**, against
  ±5 %.
- **The solved level:** the steps imply level ~439 (**42.9 %, TP3 nominal 4.24 V**). The Saleae's
  gate reading, ~4.27 V with its offset removed, agrees.
  - This is the low edge of the 43–45 % estimate and **below the design's 4.5–6 V**: a finding,
    not a failure.
  - Slope near 9 A: ~4.9 A/V at TP3, as the source-degeneration model gives.
- **Shape:**
  - from ~1 A up, no overshoot: the first µs stays at 0.99–1.03× the plateau;
  - below 0.7 A the ratio reaches 1.28×, the Q10 gate-charge lobe (~0.05 A) against a small
    plateau.
  - The gate's dip grows to −6 to −7.6 mV at 9 A, as predicted.
- **The separate 9 A pulse** (`strobe pulse 20` at the solved level):
  - TP4 1212.6 mV = **8.98 A**, against 1.215 V ± 5 % (**−0.2 %**);
  - 10–90 % rise ~200 ns, no overshoot, +1.3 % creep over 20 µs;
  - gate −7 mV.
  - After the pulse TP4 and the gate both sit ~+6 to +11 mV high: the analyser's ground shifting
    while the boost refills VIR, not current (Q10 is off).
    - ⚠ **Corrected the same day** (Step 4b, below): the boost stops within ~0.2 ms of a 9 A
      pulse (hiccup), so "while the boost refills" fits only that first ~0.2 ms. It is not
      current either way; the cause of a longer offset is open.
- **FLIR:** no change during the cal or the pulses (owner).
- **Q9's transfer curve on board 1**, from the firmware's cal table (level → A, 20 µs pulses,
  23 °C):
  - 200: none (< 72 mA); 220: 0.163; 240: 0.544; 260: 1.130; 280: 1.836; 300: 2.628;
  - 320: 3.475; 340: 4.354; 360: 5.262; 380: 6.191; 400: 7.138; 420: 8.100; 440: 9.071.
  - TP3 = level × 3 × 3.29 V / 1024.
- **Next:** Step 4b, VIR and the headroom at 9 A (Q13), with a scope, then the Step 5 bursts.

**Pi 5 on J8: what it takes, the safety checks, and flashing from the Pi (owner's question,
2026-10-16). Analysis only; two firmware gaps found (§9).**
- **J8, re-read from the netlist:**
  - J8.2/4 carry the latched +5 V: the board is the Pi's only supply. J8.1/17 (the Pi's 3V3)
    feed only the R45/R44 sense divider.
  - Through 1 kΩ each: RPI5_ON (J8.15), RPI5_SHUTDOWN (J8.37), SYSTEM_READY (J8.13), IRQ_OUT
    (J8.11), and the two spare handshakes (J8.16, J8.36).
  - UART1 through 220 Ω: GPIO36 → J8.10 (Pi RXD); J8.8 (Pi TXD) → GPIO37.
  - **SWD, direct:** SWDIO on J8.18 (Pi GPIO24), SWCLK on J8.22 (Pi GPIO25).
  - **RUN goes only to SW2, and BOOTSEL (QSPI_SS through R22) only to SW1.** Neither reaches J8.
- **Firmware today:**
  - Written, and the Phase 1b matrix passed against a simulated Pi (`pisim`): the power FSM,
    the latch and its supply guards, the shutdown handshake with both timeouts, the RPI5_ON
    fallback, and the CLI `reset`/`bootsel` guard.
  - Not written: 8.6 halt detection and telemetry, the Phase 7 camera handshake, any UART link,
    and the SYSTEM_READY and IRQ_OUT drivers. UART1 is never initialised (stdio is USB only),
    so J8.8/10 are idle.
  - The Pi side needs the `gpio-shutdown` overlay (8.1) and an RPI5_ON unit, which is not
    written. Without the unit the board raises `PI_BOOT_TIMEOUT` at 90 s.
    - The unit should drop GPIO22 late in shutdown, after the filesystems are read-only. Once
      the 15 s holdoff has passed, the board cuts power as soon as GPIO22 reads low.
- **Gap 1: two USB reboot paths bypass the `reset`/`bootsel` guard.**
  - SDK 2.3.0 turns both on by default (`pico_usb_reset/usb_reset_config.h`), and
    `CMakeLists.txt` keeps the defaults. The image contains them: `tud_cdc_line_coding_cb` and
    `usb_reset_interface_*` are in `build/pitrac.elf.map`.
  - Any USB host that opens the board's serial port at **1200 baud**, or runs
    `picotool load -f` or `picotool reboot -f`, reboots the RP2354 into BOOTSEL. Neither path
    goes through `reset_guard_ok()`.
  - With a Pi powered, that is the hard power cut. The list of reset sources in
    `BENCH_P8_PI.md` §8.0 left both out; corrected today.
- **Gap 2: J8.37 back-feeds an unpowered Pi.**
  - `safe_state_init()` drives GPIO43 high (the inactive level) from boot, and nothing lowers
    it in STANDBY. With the latch open the Pi is unpowered, and the line can push up to ~2.7 mA
    ((3.25 V − ~0.6 V) / R39's 1 kΩ) into the Pi's GPIO26 clamp diode and its 3V3 rail.
  - That is an estimate: whether RP1's pads conduct that way is not known. The new
    first-seating check in `BENCH_P8_PI.md` measures it across R39.
  - Every other J8 line is driven low or is an input. The sense divider draws ~30 µA from the
    Pi's 3V3, not into it.
  - A UART link must not repeat this: UART TX idles high, ~12 mA through R30's 220 Ω.
- **Flashing from the Pi:**
  - **SWD: yes, off-board.** Flying leads (SWDIO, SWCLK, GND) with the Pi on its own supply are
    the documented path (§4, `tools/openocd_pi5.cfg`): OpenOCD with `linuxgpiod` and
    `target/rp2350.cfg`. It is safe because the board does not power that Pi. Not yet tried.
  - **SWD from a seated Pi: not with the stock commands.** OpenOCD's `program` resets the
    target before it writes, and the new image starts only after another reset. Each reset
    opens the latch under the Pi that is running OpenOCD. Attaching to debug without a reset
    is fine.
  - **UART: no.** The boot ROM's UART loader is a BOOTSEL mode (OTP flag
    `DISABLE_BOOTSEL_UART_BOOT`), and BOOTSEL is not on J8. A UART update would need a loader
    in the firmware, and it would still end in a reset.
  - **An update without a power cut** needs the firmware's help: stage the image, let the Pi
    halt cleanly, reboot the RP2354, and have the new image close the latch again by itself.
    Not designed.
- **Before a Pi is seated:** the §8.0 gate in `BENCH_P8_PI.md`, now with the two firmware
  lines, then its new first-seating check.

**Step 4b on the TDS 1002: setup notes added; one omission in my Step 4b (2026-10-16).** The
owner asked whether both channels should be 10X: yes.
- **My omission:** Step 4b's "CH2: DC, 5 V/div, no offset" was written for the MSO54B. On the
  TDS, which has no offset control, a centred ground marker puts everything above 20 V
  off-screen. CH2's ground marker goes on the bottom line (0–40 V on screen).
- **Added to Step 4b:**
  - the channel menus at 10X;
  - why CH1 must be 10X: the TDS's AC corner is up to 10 Hz at 1X, and ≤ 1 Hz with a 10X
    probe;
  - probe compensation: a mis-adjusted 10X probe's error fades over ~100 µs, so on CH2's
    ~23 V step, 5 % off is ~1 V;
  - the EXT TRIG lead stays a plain 1X clip lead.

**6c Step 4b on the TDS 1002, from photos (2026-10-16): the bank is as designed, but the boost
does not refill after a 9 A pulse. It goes into hiccup for ~125 ms.**
- **Data:** 12 photos in `...\Downloads\Photos-1-001 (14)\`.
  - CH1: VIR (J3 pin 1), AC, 10X. CH2: VIR_RTN (J3 pin 2), DC, 5 V/div, 10X. EXT TRIG on R61
    at 1.19 V.
  - `strobe pulse 20`, then `50`, then `100` at 25 µs, 500 µs, 10 ms and 100 ms/div.
  - Readings are ±0.1 div off a photo.
  - The firmware scrollback and the FLIR readings for these pulses are still to come.
- **The bank:**
  - VIR steps down **~0.15 V** at each edge: ~17 mΩ of ESR plus the path to J3.
  - It then falls at ~12–13 mV/µs: ~0.36 / 0.74 / 1.38 V in total by the end of the 20 / 50 /
    100 µs pulses.
  - Just after the pulse it sits ~0.28 / 0.65 / 1.2–1.3 V low. So **C ≈ 640–770 µF, best
    estimate ~680 µF**, against 677 µF nominal (2 × 330 µF + 8 × 2.2 µF).
    - ⚠ **Corrected by Step 5 (below): C ≈ 600 µF.** This single-pulse figure counted the
      boost's ~0.25 mC in the first ~0.3 ms as the bank's.
- **VIR_RTN (Q9's drain):**
  - ~25 V when idle. The strings pass only µA then, so it does not read VIR.
  - During a pulse it starts at **13.4–14.2 V** and falls with VIR (100 µs: 13.8 → 12.6 V). The
    strings, ballast and harness take ~22 V at 4.5 A per string (design: 21–23 V).
  - **Q9 has ~12.4 V across it** at the start of a 9 A pulse (VIR_RTN − TP4's 1.215 V − Q10's
    ~0.18 V), and ~11.2 V at the end of 100 µs.
  - Q9 needs ~1–1.5 V, so that leaves ~11 V of headroom: the top of the design's 9–11 V.
- **After the pulse the boost stops.** This is the owner's "droop, then overshoot":
  - At 500 µs/div, VIR recovers only ~0.2 V in the first ~0.15 ms, then stays flat.
  - At 100 ms/div it keeps sinking for **124 ± 5 ms**, then jumps back to ~36 V within a few
    ms.
  - **The sinking is real.** The scope's AC coupling hides most of it: with the 10X probe its τ
    is ≈ 0.2 s, read off the decay.
    - Corrected for that, VIR falls a further ~0.9 V at ~7 V/s.
    - That matches the boost delivering nothing: ~5.3 mA of load (R15 into D4, plus the FB
      divider) on ~680 µF.
  - **The overshoot is the AC coupling.** While the trace sat ~1.27 V low for 125 ms, the
    coupling capacitor charged by ~1.27 × 0.125 / 0.2 ≈ 0.8 V. The jump shows that as +0.77 V,
    which then decays with the same ~0.2 s. A DC-coupled shot should show no overshoot.
- **The cause: the LM5157's hiccup mode is enabled on this board** (TI SNVSBK7A, §8.3.11 and the
  MODE pin table):
  - MODE has R6 ∥ R7 = 75 k ∥ 75 k = 37.5 kΩ to AGND. That is the datasheet's 37.4 kΩ setting:
    "Hiccup mode protection is enabled and spread spectrum is enabled".
  - After 64 cycles in cycle-by-cycle current limit (ILIM 6.5–8.5 A), it stops for 32 768 cycles
    and pulls SS low. That is ~31 ms at ~1.05 MHz (RT = R8, 20 kΩ).
  - It then soft-starts. 10 µA into C6 (1 µF) ramps SS at ~10 V/s.
  - Switching resumes only when SS passes FB, which is ~0.94 V with VIR ~2 V low: ~94 ms
    later. 31 + 94 ≈ 125 ms, as measured.
  - A 9 A pulse holds the boost in current limit. Refilling 0.9 mC at its ~0.8 A output would
    take ~1 ms, far more than 64 cycles (~61 µs). Whether a 20 µs pulse also trips it is
    unconfirmed.
  - **Not fully excluded: a UVLO trip.**
    - In current limit the boost draws ~7 A from +5 V, and the PSU is limited to 3 A.
    - UVLO trips when +5 V falls below ~4.5 V (R3/R4 into the 1.45 V threshold).
    - A UVLO restart would come at ~94 ms rather than ~125 ms, and part tolerances only just
      separate the two. A +5 V shot during the pulse settles it (Step 4b, optional shots).
- **Consequences:**
  - **No boost help in any Step 5 burst:** each runs from the bank alone. The sag predictions
    are in `BENCH_P6_STROBE.md` Step 5. VIR is back ~130 ms after a burst's first pulse.
    - ⚠ **Wrong for 20 µs pulses** (Step 5, below): the boost keeps up with those. It holds
      for 50 µs and 100 µs pulses.
  - The design's refill (0.4 A, back within ~2 ms) does not happen on board 1.
  - Hiccup also protects the 3 A-limited +5 V rail. **Do not disable it on board 1.** Without
    it, the boost would pull ~7 A from +5 V for ~1 ms after every 9 A shot. The choice belongs
    to the next board (`NEXT_BOARD_REV.md` CR-25).
  - **Correction:** the 9 A Saleae note above blamed the post-pulse +6 to +11 mV on "the boost
    refilling VIR". The boost stops within ~0.2 ms of the pulse, so that fits only the first
    ~0.2 ms.
- **Next:**
  - Optional: one DC-coupled VIR shot, and one +5 V shot to rule out UVLO (Step 4b).
  - Send the scrollback and the FLIR readings for the Step 4b pulses: its pass criteria are in
    them.
  - Then Step 5, with the scope on VIR and VIR_RTN.

**6c Step 4b, the firmware side: PASS (2026-10-16).**
- **The session:** the board was powered down to attach the probes. Then `id` showed
  `Oct 15 2026 22:09:11`, followed by `on`, a re-arm and `strobe cal 9`.
- **The cal after the cold start:** SOLVED at level 439, confirmations 9.013–9.017 A (mean
  +0.2 %).
  - Each level read lower than in the warm runs: up to ~30 mA lower at 1–3 A, ~8 mA at 9 A.
    That is what a colder Q9 would do.
  - The solved level did not move.
- **Every firing was `within limits` and `covered`. No `NOT CLEAN`, no `OVERCURRENT`:**

  | Pulse | Firings | Plateau | Peak | Charge |
  |---|---|---|---|---|
  | 20 µs | 1 | 9.014 A | 9.063 A | 0.180 mC |
  | 50 µs | 1 | 9.041 A | 9.092 A | 0.452 mC |
  | 100 µs | 9 | 9.057–9.061 A | 9.088–9.123 A | 0.906 mC (once 0.924 mC, 51 samples) |

- **The current stays flat through 100 µs.** Peaks sit ≤ 0.7 % above the plateau, so Q9 never
  ran short of headroom, as VIR_RTN's ~12 V says.
  - The plateau rises ~0.5 % from 20 µs to 100 µs: the current creeps ~1 % from start to end as
    Q9 warms within the pulse. Watch first → last in Step 5's bursts.
- **TP4 was not compared this session;** the TDS was on VIR and VIR_RTN. The Step 4 Saleae check
  at the same level (−0.4 %) stands.
- **The photo sent as the DC-coupled VIR shot was the earlier CH2 (VIR_RTN) photo**
  (`PXL_20261016_191939810.jpg`, same SHA256).
  - The DC-coupled VIR shot came next (below).
  - That CH2 photo is DC-coupled, though, and it confirms the outage on its own: VIR_RTN drops
    ~1.2 V at the pulse, slides ~0.75 V more, and steps back at ~125 ms.
  - It cannot show whether VIR overshoots. VIR_RTN's idle level is set by the strings at µA, not
    by VIR: it ends ~2 V above where it started, and is still rising.
- **FLIR:** no change on any component (owner, reported with the DC shot below).

**6c Step 4b, the DC-coupled VIR shot (2026-10-16): no overshoot. The dip is 2.0 V, as the hiccup
model predicts.**
- **Setup:** CH1 DC, 5 V/div, 100 ms/div, `strobe pulse 100`.
- **The owner's cursors:** **36.2 V** before the pulse and again after the recovery; **34.2 V** at
  the lowest point, just before the snap-back. So the dip is 2.0 V.
- **The shape:**
  - a ~1.2–1.5 V step at the pulse;
  - a ~0.8 V slide over the next ~125–135 ms;
  - a snap back to 36.2 V, **with no overshoot** within one 0.2 V step of the display.
- **What it confirms:**
  - The AC-coupled "overshoot" was the coupling, as estimated.
  - The model predicted ~2.1 V: ~1.3 V for the pulse's 0.9 mC on ~680 µF, plus ~0.8–0.9 V of
    bleed (~7 V/s) while the boost is off.
- **VIR level:** 36.2 V on the scope (±3 % DC accuracy), against 35.9 V from the FB divider.
- **FLIR:** no change on any component across the Step 4b pulses (owner).
- **Step 4b is complete.** The +5 V shot (hiccup or UVLO) stays optional. It is worth taking
  before a Pi shares +5 V, because it also shows how far each shot pulls the rail.

**6c Step 5, bursts at 9 A, board 1 (2026-10-16): PASS on the firmware's criteria. The boost
keeps up with 20 µs pulses but goes into hiccup on 50 µs and 100 µs pulses, and the bank is
~600 µF, not ~680 µF.**
- **Data:**
  - the serial log `captures/COM5_2026_10_16.15.10.23.965.txt`, because the scrollback overran;
  - 8 TDS photos in `...\Downloads\Photos-1-001 (15)\`: CH1 VIR and CH2 VIR_RTN, both DC
    5 V/div;
  - the `strobe wave` CSV from the third `sched 10`, saved as
    `captures/strobe_wave_2026-10-16_sched10_board1.csv`.
- **The file log's order checks out:**
  - In one `sched 10` status the logger moved pulse 3's line two lines early. Nothing was lost.
  - The CSV is intact: 10 919 rows, as the header says, and six pulses of exactly 50 samples,
    4266–4268 µs apart.
  - Pulses 2–6 and the last 120 samples match the owner's scrollback value for value.
- **Every burst `within limits` and `covered`. No `NOT CLEAN`, no `OVERCURRENT`, no fault:**

  | Burst | Pulses | Plateaus | First → last | Charge |
  |---|---|---|---|---|
  | `strobe burst 20 500 4` | 4 × 20 µs | 9.022–9.029 A | +0.1 % | 0.722 mC |
  | `strobe sched 50 fire` | 10 × 20 µs | 9.019–9.032 A | +0.1 % | 1.805 mC |
  | `strobe sched 20 fire` | 10 × 50 µs | 9.044 → 9.016 A | −0.3 % | 4.515 mC |
  | `strobe sched 10 fire` ×3 | 6 × 100 µs (shed from 10) | 9.061–9.063 → 9.024–9.026 A | −0.4 % | 5.43–5.48 mC |

  - The 50 µs and 100 µs bursts fall steadily, ~0.4 % in all. That fits Q9's drain voltage
    falling ~9 V across the burst, through its finite output resistance. A headroom collapse
    would be abrupt.
  - **FLIR: no change on any component** (owner).
- **VIR on the photos.** Each level is read against the graticule lines in the same photo, so
  the camera angle drops out; about ±0.2 V.
  - **20 µs bursts: VIR stays flat**, within ~0.3 V. The boost refills each pulse's 0.18 mC
    between pulses and does not trip.
  - **50 µs (`sched 20`):** a staircase with no recovery between pulses. The steps are 0.39 V,
    then 0.73–0.82 V each: 7.4 V in all.
  - **100 µs (`sched 10`):** 1.10 V, then 1.49–1.59 V each: 8.8 V in all.
- **So hiccup trips on 50 µs and 100 µs pulses, not on 20 µs ones.**
  - The first step is smaller in both bursts: the boost adds ~0.25 mC in the first ~0.3 ms, then
    stops.
  - A pulse whose refill needs more than that trips it: somewhere above ~0.25 mC, i.e. ~28 µs at
    9 A.
- **C ≈ 590–600 µF.** It comes from the steps once the boost is off, bleed included: 0.46 mC per
  0.78 V, and 0.93 mC per 1.54 V. That is ~89 % of the 677 µF nominal, inside its ±20 %.
- **Two corrections of my own claims:**
  - Step 4b's "C ≈ 680 µF" was biased high. It came from single pulses, where the boost's
    ~0.25 mC in the first ~0.3 ms counted as the bank's.
  - I wrote that every 9 A shot trips the hiccup (Step 4b above, CR-25, Q13). That is wrong for
    20 µs pulses. All three are corrected.
- **Against the predictions** (which assumed hiccup in every burst and ~680 µF):
  - the 20 µs bursts sagged ~0 V, against 1.2 V and 2.9 V predicted;
  - `sched 20` sagged 7.4 V, against 6.9 V;
  - `sched 10` sagged 8.8 V, against 8.3 V.
  - With ~600 µF and the first-pulse help, the model gives 7.3 V and 8.9 V.
- **Q9's headroom at the end of the worst burst:** VIR_RTN is ≈ 13.75 − 8.8 ≈ 5.0 V at the end of
  the last pulse. That leaves ~3.5 V across Q9 against its ~1–1.5 V need, and the current held
  (−0.4 %).
- **`BURST_CHARGE_MAX_MC` (6 mC, 🔴 unvalidated), extrapolated on board 1:**
  - A 6 mC burst (e.g. 10 × 67 µs at 15 m/s) would sag ~9.8 V and leave ~2.5 V across Q9. It
    fits, with ~1 V to spare.
  - A bank at the −20 % end (~540 µF), or LEDs with a higher Vf, would use most of that margin.
  - A finding for the owner (§9). Nothing changed.
- **Still open for Step 5:** the gate drive during a burst (the TDS was on VIR), and ADC0
  against TP4 on the first and last pulse. One `sched 10` and one `sched 50` with the Saleae on
  the gate node and TP4 close both.
  - **Corrected the same day (owner's question):** I first said TP3. On board 1 the probe is
    the gate node, as `BENCH_P6_STROBE.md` 6c already says: since the 1 µF rework it is the
    voltage that sets the current.
    - TP3's own stability with the hold capacitor was checked on 2026-10-15 (MSO54B,
      6.25 GS/s): −1 to −5 mV at the edges, nothing after turn-off, no oscillation.
    - The gate node cannot show fast ringing at TP3: 47 Ω into ~1 µF filters anything faster
      than a few kHz.
    - **It does tell what the slow-ball burst's −0.4 % is.** If the gate stays flat within ~2 mV,
      the cause is Q9's falling drain voltage. A ~7 mV droop (at ~4.9 A/V) would mean the drive
      moved, e.g. +12 V sagging with VIR.

**6c Step 5, the gate node and TP4 during bursts on the Saleae (2026-10-16): PASS. Step 5 is
complete.**
- **Data:**
  - `...\Downloads\newFW_strobegate_sched50\`, `...sched20\` and `...sched10\`: 50 MS/s, −1 to
    +30 ms. A1 is the gate node, A2 is TP4, D3 is U5 Q.
  - The firmware output is in `captures/COM5_2026_10_16.16.39.37.576.txt` and in the owner's
    scrollback; the two agree.
- **The firmware:** all three bursts `within limits` and `covered`. First → last: +0.1 % (20 µs),
  −0.3 % (50 µs) and −0.4 % (100 µs), as in the earlier runs.
- **Timing (D3, U5 Q):** widths of 20.16, 50.16–50.22 and 100.16–100.23 µs; periods of 853.0,
  2134.0 and 4267.0 µs, as scheduled.
  - The digital channel shows ns-wide glitches at each falling edge: 70–112 sub-µs transitions per
    capture, pickup from the 9 A turn-off. A 1 µs filter removes them.
- **TP4 against the firmware, every pulse:**

  | Burst | First pulse | Last pulse | All pulses |
  |---|---|---|---|
  | `sched 50` | −0.10 % | +0.19 % | −0.19 to +0.19 % |
  | `sched 20` | +0.21 % | +1.12 % | +0.21 to +1.12 % |
  | `sched 10` | +0.87 % | +1.15 % | +0.73 to +1.15 % |

  - These use a pre-pulse baseline. With the pre/post average, the 50 µs and 100 µs pulses agree
    within −0.2 to +0.5 %. Either way they are far inside ±5 %.
  - The Saleae scatters about ±0.3 % from the shared-ground wander. That is too much to see the
    firmware's −0.3 % and −0.4 % trends; ADC0 is the better instrument for those.
- **The gate node:**
  - Each 9 A pulse dips it by 3.6–8.8 mV. It recovers smoothly within ~100 µs (47 Ω into the
    1 µF).
  - **Between pulses it stays within −0.7 to +1.1 mV of its pre-burst level, through every
    burst**, with the common mode removed against TP4. After each burst it is back within 1 mV.
  - **So the drive is steady.** Explaining the slow-ball bursts' −0.3 % and −0.4 % through the gate
    would need a ~7 mV droop. They come from Q9's drain voltage falling ~9 V as VIR sags. +12 V
    sagging along with VIR did not move the drive measurably.
- **No ringing:**
  - Wherever the boost is off, the gate's residual about a 10 µs average sits at the analyser's
    floor: ~3 mV RMS, ~24 mV peak-to-peak.
  - It doubles (~6.5 mV RMS, ~75 mV peak-to-peak) only while the boost switches: in every gap of
    the 20 µs burst, and in the first gap of the 50 µs and 100 µs bursts. That is the ~1 MHz
    boost picked up by the leads.
  - It also confirms the hiccup timing: the boost runs after the first pulse, then stops.
- **Step 5 is complete.** On the Phase 6 exit list only the thermal item is open. The FLIR saw
  no rise, and these loads cannot show the heatsink coupling; that call is the owner's.

**The +5 V shots on the logic analyser (2026-10-16): PASS. UVLO is ruled out, so the boost's
outage is hiccup. These are the dips a Pi on J8 would see.**
- **Data:** A2 on J8.2 (the latched +5 V) and D3 on R61. Each capture covers 500 ms and is read as
  a 1 µs min/mean/max envelope.
  - `...\Downloads\5Vtest\`: one `strobe pulse 100`, at 50 MS/s.
  - `...\5Vtest_sched50\` and `...\5Vtest_sched10\`: at 6.25 MS/s.
- **Before:** 5.10–5.12 V as read, ~5.18–5.21 V allowing for the analyser's ~87 mV offset.
- **Each time the boost refills at its current limit, +5 V dips ~0.30–0.33 V for ~0.28 ms.**
  - It falls within ~10 µs of the pulse's start.
  - The deepest point is ~180–210 µs after the rise, and the rail is back by ~280 µs.
  - That is ~7 A drawn from +5 V through roughly 0.05 Ω of source impedance.
  - **The lowest point in all three is ≈ 4.85–4.88 V (4.77–4.80 V as read), against the 4.75 V
    pass line.**
- **`strobe pulse 100`:** one dip, −0.32 V (−0.35 V on the deepest sample), then an overshoot of
  +40 to +50 mV for ~1 ms. The restart shows at ~126–137 ms as −20 to −30 mV, with spikes to
  −78 mV.
- **`sched 50` (10 × 20 µs): ten dips, one after every pulse,** each −0.30 to −0.33 V for
  ~0.28 ms. There is no restart afterwards: the boost never trips on 20 µs pulses.
- **`sched 10` (6 × 100 µs): one dip, at the first pulse (−0.31 V), then none.** The boost has
  tripped.
  - The restart comes earlier, at **~105 ms**, and lasts longer: −30 to −64 mV until ~128 ms.
  - With VIR ~8.8 V low, the soft start reaches FB sooner (~31 ms off plus ~77 ms of ramp), then
    refills along its ramp for ~23 ms. That is the model's timing.
- **Background:** the rail wanders ±30 mV on a slow, ms-scale pattern. All three captures also
  show a ~50 mV dip lasting ~2 ms about 24 ms after the firing starts. Its cause is unknown,
  possibly the bench supply recovering from its 3 A current limit; it is small next to the
  refill dips.
- **What it settles:**
  - **UVLO is ruled out.** +5 V never came near the 4.25–4.72 V trip, so the ~125 ms outage is
    the hiccup alone.
  - The ~0.27 ms current-limit phase matches the ~0.25 mC of first-pulse help (~0.8 A × ~0.27 ms).
- **For Phase 8:** without a Pi the margin to the Pi 5's ~4.63 V under-voltage flag is ~0.2 V.
  - The Pi's own 1–3 A through the same ~0.05 Ω lowers the start by ~0.05–0.15 V. Fast-ball
    bursts then dip toward ~4.7 V, ten times in 7.7 ms.
  - The bench repeat with the Pi running is now in `BENCH_P8_PI.md`.
  - A next-board note is in CR-25.

**Phase 8 prep firmware written (2026-10-19): J8.37, the USB reboot paths, halt telemetry, and the
strobe session cap. Under independent review; not flashed.** The owner asked for the queued
change and what Phase 8 needs.
- **Image:** Release `Oct 19 2026 06:23:15`, 208056 text / 0 data / 123416 bss, `pitrac.uf2` SHA256
  `EC46C94E9292FAEB9A6635F4304386AF5BF8CF0201F8230D6DE03AD0B5029708`. Board 1 still runs `Oct 15 2026 22:09:11`.
  *(Superseded: the review found two defects in it. See "Review findings fixed (2026-10-19,
  r2)", below.)*
- **Tests:** 155/155 host tests, 38 new:
  - power FSM +9, and every step of every power test now checks the J8.37 rule;
  - telemetry +13, config store +12 (a new test on a fake flash), strobe policy +1, session cap
    +2, service +1.
- **Mutations:** 34 deliberate mutations, 34 caught (`captures/review_2026-10-19/mutations.md`).
- **Review package:** `captures/review_2026-10-19/` (`REVIEW_BRIEF.md`, the patch against the
  flashed image's source, the mutation table and runner).
- **J8.37 (§9's Phase 8 gate):**
  - It now has three modes: released (input, no pulls, input buffer off), driven deasserted
    (high) and driven asserted (low). It is released at boot.
  - The power FSM sets the mode at the end of every step. It asserts the line for the 200 ms
    request. It drives it deasserted only while the latch is closed **and** PI_3V3_SENSE reads
    high, and releases it otherwise. FORCE_OFF also releases it as the latch drops.
  - `gpio 43` is no longer writable. `pins`, `pisim` and `stat` show the mode.
  - **This differs from §9's spec, which said "low in STANDBY".** Released puts nothing on the
    line into an unpowered Pi either way. A line the firmware failed to drive is held
    *deasserted* by the Pi's own pull-up, where a line held low would ask the Pi to shut down
    as it boots. The 3V3 condition also covers the moment before a Pi's 3V3 comes up, and the
    time after a halt that drops it.
- **USB reboot paths (§9's other gate):** built with `PICO_ENABLE_USB_RESET_VIA_BAUD_RATE=0` and
  `PICO_ENABLE_USB_RESET_VIA_VENDOR_INTERFACE=0`.
  - `tud_cdc_line_coding_cb` and `usb_reset_interface_*` are gone from the map.
  - The cost: `picotool info`, `load -f` and `reboot -f` no longer reach a running board. Put it
    in BOOTSEL first: `bootsel` at the CLI, or SW1 + SW2.
  - Without the vendor interface the USB descriptor changes, so Windows may give the board a new
    COM port number.
- **Halt telemetry (BENCH_P8_PI.md 8.6):** new `telemetry.c/.h`, and the `hist` command.
  - **Counters:** boots; boots after a watchdog hang; Pi shutdown requests, those where the Pi
    read down in time, and those that timed out; the Pi going down in RUNNING with no request,
    and coming back (a Pi reboot); power cuts to a powered Pi; faults. Also the last fault, its
    uptime and boot number, and the reason for this boot.
  - **"A powered Pi"** means the latch is closed, a Pi was found at power-on, and its 3V3 reads up.
  - **In RUNNING, the Pi reading down for 1 s with no request is counted and announced at the
    CLI, and the rail stays up** (8.6: "log it and stay up is the safe default").
  - **The counts survive a reset in a no-init RAM mirror** (CRC-checked), which also lets the next
    boot count a reset that cut a powered Pi.
  - **Flash:**
    - `cfg save` writes the counts too.
    - On its own, the board writes the **last saved** record with new counts after 10 s in
      STANDBY, at most once per change. A board that never had a `cfg save` gets a
      defaults-only record, labelled `telemetry only`.
    - A failed write is retried after 60 s, and the automatic writes stop after 3 failures in
      one boot.
  - Disarming the watchdog now clears the SDK's `watchdog_enable()` mark, so a `bootsel` or SW2
    after a live session is not counted as a hang.
- **The session cap (§8, option c):**
  - Once `strobe cal` solves, gate levels above
    `l1 + floor((10.8 A − a1) / 0.075 A per level)` are refused. (l1, a1) is the cal's first
    point at or above its target.
  - The cap clears on re-arm, on leaving live mode, and at the start of a cal. RAM only.
  - **Board 1 (the 2026-10-16 cal, 440 → 9.071 A): level 463, 45.2 %**, ~10.2 A on its own
    slope.
  - **My spec in §9 was wrong in one point.** "Use the slope of its last points" fails for a
    low-target cal. Board 1's slope near 2 A is ~0.040 A/level against 0.049 at 9 A, so a cap
    extrapolated from `strobe cal 2` would sit where board 1 draws ~12 A.
  - The firmware uses a physical bound instead. One level is at most 9.67 mV at TP3, and the
    sense voltage cannot rise faster than Q9's gate (a source follower). So the reported
    current rises at most 9.67 mV / 0.135 Ω = 0.0716 A per level, and 0.075 adds ~5 % of
    tolerance.
  - The cost is a cap ~12 levels lower than the spec's ~475 on board 1. It still leaves 24
    levels (~1.2 A) above 9 A.
- **Found on the way, and fixed: `cfg default` then `cfg save` was undone at the next boot.**
  - `cfg default` zeroed the RAM `seq`. The next save wrote seq 1 beside an older record with a
    higher seq, and `cfg_init()` loaded the older one.
  - The seq now comes from the newest record in flash. Host test `default_then_save_sticks`.
  - The bug dates from the dual-slot store, not from this change.
  - Also: the read-back now compares the whole record, and writes are refused if the image ever
    overlaps the config sectors (before, only the boot noticed).
- **Comments corrected** (queued in §9):
  - `BURST_CHARGE_MAX_MC`: ~600 µF measured on board 1, not "never measured";
  - the 70 % ceiling, which is now only the outer bound, and the 9 A point measured on board 1;
  - `strobe_gate_settling()`: 5 mV at TP3, but ~0.35 V at Q9's gate behind the 1 µF;
  - `service.c`, which claimed SDK 2.3.0 has no `watchdog_disable()`.
- **Pi-side files, new in `pi/`:**
  - a systemd unit that raises RPI5_ON (GPIO22) after `multi-user.target`;
  - a `system-shutdown` hook that drops it once the filesystems are read-only; *(wrong:
    systemd runs that hook even after a failed unmount. Fixed in r2: the hook now checks
    before it drops the line. See "Review findings fixed", below.)*
  - persistent journald, and an installer.

  None of it has run on a Pi yet. `pi/README.md` describes them.
- **Not host-testable, so checked only by reading:** the pad writes for J8.37, the boot-reason
  read, where `strobe.c` sets and clears the cap, and the CLI text. After the review, the
  bench checks are `BENCH_P8_PI.md` §8.0b (no Pi) and the session-cap check in
  `BENCH_P6_STROBE.md` (§10).

**Independent review, 2026-10-19 (Sol): NOT PASSED; the image remains unflashed.**
- Report: `captures/review_2026-10-19/SOL_REVIEW.md`.
- F1: the Pi shutdown hook unconditionally drops RPI5_ON even if a required filesystem
  remains writable; systemd can reach the hooks after a failed unmount/remount. It must not
  signal a clean power-removal acknowledgement without checking the storage conditions.
- F2: a reset during FORCE_OFF can count one physical power cut twice: the count is mirrored
  before the old Pi-powered flag is cleared. The independent native probe reports 2, expected 1.
- The clean host build passes 155/155 tests; doc links pass on 18 files. The supplied image's
  stamp, sizes and full hash match. No firmware source was changed or image rebuilt/flashed.
- Reviewer correction: I initially suspected a systemd ordering cycle from After=multi-user.target
  plus WantedBy=multi-user.target. That concern is ruled out: systemd suppresses the conflicting
  automatic ordering, and an isolated systemd verification passes. It is not a finding.
- Board 1 stays on `Oct 15 2026 22:09:11`. Return the findings for fixes and a confirm review
  before the pending image is handed over.

**Review findings fixed (2026-10-19, r2): F1 and F2. Waiting for the confirm pass; not flashed.**
- **Image r2:** Release `Oct 19 2026 11:44:38`, 207808 text / 0 data / 123508 bss, `pitrac.uf2` SHA256
  `841E353EB51F34440D2C7343F1EC6BA6EE107231CEE310417E22BE3134398ABF`. It replaces `Oct 19 2026 06:23:15`, which was never flashed.
- **Response for the reviewer:** `captures/review_2026-10-19/REVIEW_RESPONSE.md`, with the r2
  patch, `r1_to_r2.diff` and `mutations_r2.md`.
- **F2, a power cut counted twice:**
  - **My error:** I had seen this window while writing round 1 and judged it negligible. It is
    not: a reset there makes the next boot count one cut twice.
  - **The fix, the RAM mirror:** it now keeps two copies, each with a sequence number and a
    CRC, and every change is one commit into the older copy. A reset sees the state before a
    change or after it, never a mix, and history not yet in flash survives.
  - **The fix, the FSM:** `telemetry_release_pi()` counts the cut (or the clean shutdown) and
    clears the "Pi at risk" flag in that same commit. FORCE_OFF makes this call before the
    latch drops.
  - **Tests:** a reset injected at every point of a FORCE_OFF gives exactly one cut, with
    earlier unsaved counts kept; on the clean path, `clean + cut == 1`. The reviewer's probe
    now prints 1 (it printed 2).
- **F1, a premature shutdown acknowledgement:**
  - **My error:** the shutdown hook, the unit, `pi/README.md` and the guide all said the hook
    runs "after every filesystem is unmounted or read-only". systemd runs it even after a failed
    unmount, and syncs once more afterwards.
  - **The fix:** the hook now reads `/proc/self/mountinfo`. It drops RPI5_ON only if every disk
    filesystem is read-only at the superblock and the root is listed. Otherwise the line stays
    high, and the board's 60 s timeout ends the shutdown, as `PI_SHUTDOWN_TIMEOUT`, not as a
    clean one.
  - **Testing it:** `check` reports without touching GPIO22, and `install.sh` runs it.
    `pi/tests/test_shutdown_hook.sh` has 21 cases and passes under dash. The reviewer's probe
    passes.
- **Tests:** 161/161 native, 21/21 shell. 49 deliberate mutations, all caught:
  - 16 new ones reintroduce F1 or F2;
  - one of those, `f2_clean_still_at_risk`, replaces round 1's `tele_clean_is_cut`, whose line
    moved;
  - the other 33 from round 1 were updated to the new code.
- **For the owner, not implemented:** require the down indication to hold for ~2 s before the
  cut. That adds glitch immunity after the holdoff and settle time for the SD card. It changes
  reviewed FSM timing, so it is your call.

### 2026-10-20 - The Pi serial link and firmware updates from the Pi: written (r3), host-tested; not flashed, review pending

**Asked (2026-10-19):** flash the RP2354 from a Pi that the board powers, with no USB cable and no
other supply, and talk to it over serial. **Answer given:** possible on this board, in
firmware. A hardware change (a latch that holds through an RP2354 reset) is only needed if the
Pi must stay up during the flash, which was not asked. **Owner:** make the changes before the
code goes back to Sol.

**Image r3, NOT flashed:** Release `Oct 20 2026 06:13:43`, 222424 text / 0 data / 421752 bss, version 1.1,
flagged try-before-you-buy (TBYB).
- `pitrac.uf2` SHA256 `BD1E45D73597A9DD5356CFBCEA4A0B96D91BF083C5D6D0148770189BAD495AA4`.
- `pitrac.bin` (what the Pi sends) 218,332 B, SHA256 `BB48FC743114355455436CF36B048FDF87A2F45254C24A6EBC249F36190F14B1`.
- `pitrac_pt.uf2` (the partition table) SHA256 `368057302F84E41A33E099FD89C694846A885263AD5C46459C5AB035D6C2CB13`.
- It contains r2 unchanged. Board 1 still runs `Oct 15 2026 22:09:11`.

**What it adds:**
- **The serial link** (UART1, J8.8/J8.10, 921600 8N1; `link_frame.h`, `link_proto.h`,
  `pi_link.c`):
  - frames `0xA5 len type seq payload crc16`;
  - an allowlist of requests: `PING`, `INFO`, `STATUS`, `HIST` and the `UPD_*` set;
  - RX into a 2 KB DMA ring, TX polled from the superloop; `link` at the CLI;
  - GPIO36/37 connected only while the latch is closed, the Pi's 3V3 is up **and** RPI5_ON is
    high, and released before the latch drops.
- **Firmware updates from the Pi** (`fw_update.h`, `fw_layout.c`; the Pi's `pitrac-link`):
  - the image is staged in 288 KB of RAM, its SHA-256 and IMAGE_DEF checked;
  - `UPD_APPLY` starts the board's normal orderly shutdown of the Pi;
  - in STANDBY, latch open, the image goes into the other A/B partition, sector 0 last, every
    sector read back;
  - a FLASH_UPDATE reboot; the image buys itself at the end of init, or the boot ROM goes back
    to the previous image; either way the board powers the Pi again;
  - `upd` at the CLI; `pitrac-link update` / `upd-status` on the Pi.
- **The A/B partition table** (`partition_table.json` → `build/pitrac_pt.uf2`): A at 0x2000, B at
  0x100000, 1016 KB each; the config sectors stay unpartitioned. Installed once per board
  (`BENCH_P8_PI.md` §8.7).
- **The config store** reads through the untranslated flash alias, and its overlap check uses the
  image's real (physical) start.
- **Build:** version 1.1 (`pico_set_binary_version`), `PITRAC_TBYB` ON, a test-only
  `PITRAC_TEST_TRIAL_HANG` image for the rollback check; `tools/flash_swd.sh` refuses by default.
- `id` shows the version and partition; `hist` names an update reboot as a software reboot.

**What it rests on: the boot ROM** (RP2350 datasheet §5.1.16–5.1.19, §5.5, erratum E10; the
bootrom source, `varm_flash_boot.c`, `varm_launch_image.c`, `varm_boot_path.c`):
- an unbought TBYB image is launched only on a FLASH_UPDATE boot naming its partition, and never
  on a normal boot;
- that launch arms a 16.7 s watchdog for a normal reboot. The image must buy itself
  (`rom_explicit_buy`) first, and anything that disarms the watchdog before that defeats the
  rollback: `main()` buys before `pitrac_watchdog_enable(false)`;
- on a FLASH_UPDATE boot the updated partition wins whatever its version. At the buy, if its
  version is not higher than the other's, the other's first sector is erased. Equal versions
  work, but leave no fallback;
- the boot path keeps its workspace in USB RAM and its stack in boot RAM, so main SRAM, and the
  update's no-init note, survive the update reboot;
- the launched partition is mapped at 0x10000000 by QMI ATRANS, sized to the partition, and
  reads beyond it bus-fault. So the config sectors are read at 0x1C000000 (untranslated), and an
  image older than 1.1 crashes in a partition at its first config read;
- drag-and-drop onto a partitioned board needs an A3 or later boot ROM (E10 is A2's); the boards
  are A4 (Q5);
- `picotool load -x` ends in a flash-update reboot (picotool 2.3.0 `help load`).

**Hazards it is built around:**
- **No flash write while a Pi is powered** (the `cfg save` rule: an erase blinds the supply
  monitor). The image waits in RAM; the install needs STANDBY, the latch read back open, the
  config guard clear and 1 s in STANDBY. Leaving STANDBY stops it.
- **The one reset only after the Pi is off:** the install runs only after the board's own
  orderly shutdown has ended in STANDBY.
- **UART TX into an unpowered Pi:** ~12 mA through 220 Ω if left idling high. RPI5_ON is part of
  the rule because current fed into the Pi's 3V3 rail might hold the 3V3 sense up; it cannot
  hold up a line the Pi's SoC drives.
- **A partial install is never bootable:** sector 0 (the IMAGE_DEF) is erased first and written
  last. **Wrong as stated** (review R3-F1): a last write that *fails* can leave a valid image
  with its TBYB bit cleared, which a normal boot would launch. Fixed in r4.
- **A broken image rolls back:** TBYB; the trial checks that its config sectors are clear of it
  and that the ADC ring delivers fresh +5V_IN.
- **The Pi is powered again** once, from STANDBY, from the superloop, after the watchdog is
  disarmed. **Not across a reset** (review R3-F2): the request was a RAM flag, which a reset
  in the second before it was made lost. Fixed in r4.

**Tests:**
- 204/204 native: 161 + 43 new (power FSM +3 for the UART lines, link and SHA-256 13, update and
  requests 27). Python 21/21 (`pi/tests/test_pitrac_link.py`); the shutdown hook 21/21,
  unchanged.
- 52 deliberate mutations, all caught. The first run left one survivor, `install_any_state`: no
  test had STANDBY as the only thing blocking an install. A sweep over every other state was
  added; it catches it.
- Built clean, no warnings, both the bench image and the test image.

**Not verified on hardware: all of it.** `BENCH_P8_PI.md` §8.7 (no Pi, USB, ~30 min) checks the
partition table, a trial, a buy, a rollback and the calibration surviving; §8.8–8.9 the link and
an update with a Pi.

**Errors of mine:**
- **My research note of 2026-10-19 said "the higher image version wins the A/B choice, so builds
  need increasing versions".** Half right: a normal boot picks the higher version, but a
  FLASH_UPDATE boot prefers the updated partition whatever its version, and the buy erases the
  other image when the new one is not newer (bootrom source). So the firmware accepts any
  version; the Pi tool warns that no fallback will remain.
- **A test vector I computed by hand for the PING answer had a wrong length byte** (5 for a
  6-byte payload). The C test caught it on its first run.
- **Doc correction, not mine originally:** the BOOTSEL drive of an RP2350 is named `RP2350`
  (datasheet §5.5). HANDOFF, SETUP, START_HERE, BENCH, BRINGUP_NEW_BOARD, BENCH_P8_PI, the
  guide and §4 here said `RPI-RP2`, the RP2040's name. **Owner: confirm at the next BOOTSEL.**

**Open, for the owner:**
- **The framing** (DEVELOPER_GUIDE §9.3 flagged it as open): implemented as the binary frame,
  which an image transfer needs anyway. The Phase 8 shot messages can still follow upstream's
  vocabulary.
- **288 KB of RAM for staging**, leaving ~69 KB; it bounds the image at 288 KB (218 KB now).
- **Recovery:** an image that passes its trial but cannot talk on the link is only replaceable
  over USB. `NEXT_BOARD_REV.md` CR-26 is the hardware that would remove that limit.

---

### 2026-10-20 - independent review of r3: r2 confirmed; two new update findings

**NOT PASSED. Keep r3 unflashed; Board 1 stays on `Oct 15 2026 22:09:11`.**
- Report: `captures/review_2026-10-19/SOL_REVIEW_r3.md`; new independent reproduction:
  `captures/review_2026-10-19/sol_update_r3_probe.c`.
- **r2 confirmed:** the shutdown hook withholds acknowledgement on writable/unverifiable
  storage; the retained release transaction counts one power cut once across resets.
  Both original independent probes now pass, along with the current regression suites.
- **R3-F1 (P1):** a failed final header readback does not invalidate the partner. One cleared
  TBYB bit leaves a newer, valid non-trial image, eligible on the next normal boot even though
  the updater reports FLASH and says the old image stays. The probe uses the actual binary
  with only its declared version changed in RAM, then injects that one write fault.
- **R3-F2 (P2):** result classification seals NOTE_DONE before delayed Pi restoration.
  A second warm reset before that request loses the BSS pending flag; the healthy fallback
  image then never powers the Pi. The probe reports zero power-on requests, expected one.
- Independently rerun: 204/204 host tests, 21/21 Pi-tool tests, 21/21 shutdown-hook tests.
  The new real-image mock install passes in both A/B directions, then fails the two new
  assertions. The author's mutation table was inspected, not rerun. Doc links: 18 files,
  zero problems. Supplied stamp, sizes and full UF2/bin/partition-table hashes match.
- No production source or firmware image was changed, rebuilt or flashed; no commit.
  Return both findings for fixes, tests and a confirm review before a reflash handover.

---

### 2026-10-20 - r4: R3-F1 and R3-F2 fixed; host-tested; not flashed, confirm review pending

**Image r4, NOT flashed:** Release `Oct 20 2026 11:11:58`, 224568 text / 0 data / 422112 bss,
version 1.1, try-before-you-buy (`picotool info -a`: `Release`, `tbyb: not bought`).
- `pitrac.uf2` SHA256 `015205B535237FC92DC71337F84C59AFA0AB2E10C11A6C66EF079B0910A7FE96`.
- `pitrac.bin` (what the Pi sends) 220,476 B, SHA256 `7E6967F973D7B02422DC7014FD1EBD18B9F9FB68F237A1312FEBADFB0EC9A966`.
- `pitrac_pt.uf2` unchanged from r3: SHA256 `368057302F84E41A33E099FD89C694846A885263AD5C46459C5AB035D6C2CB13`.
- Test image `build_hang/pitrac.uf2` (§8.7 Step 3 only): `Oct 20 2026 11:12:03`, SHA256
  `4CB67FC42B81EDCFB5D91A7E847FFDA99620FB1043BEC3836C158B807B4F5C64`.
- Otherwise r3 unchanged. Board 1 still runs `Oct 15 2026 22:09:11`.

**R3-F1: a failed install could leave an image that a normal boot launches without a trial.**
- **Every failed install step now ends the same way** (`install_failed()` in `fw_update.c`):
  - sector 0 of the partner is zeroed: 16 page programs, ~6 ms at typical flash timing, no
    erase needed, and it works on whatever a faulty write left;
  - if that does not take, the sector is erased;
  - then it is parsed as the boot ROM would parse it. **Wrong** (review R4-F1): that parser wants
    the whole block inside the 4 KB, while the ROM finishes a block that only starts there. Fixed
    in r5: the sector must read back all 0x00, or all 0xFF after the erase.
  - Only if it holds no IMAGE_DEF does the board report `FLASH` (or `REBOOT`) and say the old
    image stays. Otherwise it reports `FWU_E_CLEANUP`, everywhere (CLI, link, Pi tool): reflash
    over USB before any other reset (BOOTSEL launches no image).
- **A reset during the last write is safe without this.** Programming only clears bits, and
  TBYB is a 1 in the image. Tested by every prefix across the block and by random subsets of
  the write's bit changes.
- **What is left:** a faulty write *and* a reset before the zeros reach the block: two page
  programs, ~1 ms at typical flash timing (not measured), since the IMAGE_DEF is in the first
  512 bytes (0x138 in this image). **Imprecise** (review R4-F1): ~1 ms is two cleanup pages, not
  the whole exposure, which starts at the fault inside the last write. r5 states it by events.
  The boot that then runs the new image reports `installed ... launched WITHOUT a trial`. A
  `CLEANUP` result whose image is found running is re-settled the same way.
- A refused update reboot clears the complete image too.

**R3-F2: a reset before the delayed power-on lost it.**
- **The obligation is now a field of the note**, committed at `UPD_APPLY`, not a RAM flag.
  - It is met when the power FSM leaves STANDBY: the board's request acted on, the USB guard's
    fault, or the button first.
  - `upd abort` cancels it, in one commit with the result. So does an arm timeout that never
    reached STANDBY.
  - Only a cold power-on loses it.
- A refused request is made again every pass. An accepted one is made once: the FSM acts on it
  at its next step, which comes first in the next superloop pass. After a reset an owed power-on
  is live at once, since the reset cut the Pi's power.
- **The note is two copies**, each with a sequence number and a CRC, the telemetry mirror's
  scheme. A reset partway through a commit leaves the commit before it.
- **Classification by the note's phase.** The target image running after `TRYING` is a bought
  trial, even on a normal boot after a reset that came before the result was settled. After
  `ARMED` it was launched without a trial.

**Also:** `tools/check_retained.cmake` runs after every link. It stops the build if the note
(`s_notes`, 0x200001b0, 88 B) or the telemetry mirror (`s_mirror`, 0x20000110, 160 B) moves or
changes size: the next image reads them at its own addresses. A moved, a resized and a missing
symbol were each shown to fail it. The CLEANUP warnings name `bootsel` as the safe way out.

**Tests:**
- **224/224 native**, 20 of them new update cases:
  - R3-F1: the review's TBYB fault; a stuck-low bit in each metadata byte, in sector 0's other
    bytes and in body sectors; partial programs of 0–16 pages; a reset at every prefix of the
    last write, and random subsets of its bit changes; cleanup by erase; cleanup failing; the
    two-fault window; a failed first erase; a refused reboot that cannot be cleared.
  - R3-F2: a reset in the delay, after a rollback and after an install; a reset between the
    buy and the settling; resets around the request's acceptance; refused requests; the USB
    guard's fault; `upd abort`; resets during the shutdown and the install; the arm timeout
    after STANDBY.
  - `note_commit_resets`: a reset at every store of every note commit in five scenarios. Owed
    either side means powered exactly once; owed neither side means never; a later reset
    repeats nothing.
  - 14 r3 cases updated. The fixture models the boot ROM's normal-boot choice and its buy, and
    the FSM acting on an accepted request.
- **23/23 Python** (+2: the results and their details, and the CLEANUP warning).
- **Shutdown hook: NOT rerun.** WSL would not start (`Wsl/Service/CreateInstance/CreateVm/HCS/
  0x80070569`, a logon failure). The hook, its unit, `install.sh` and its tests are
  byte-identical to r3, which passed 21/21 here and in Sol's rerun.
- **Mutations: 79, all caught** (`captures/review_2026-10-19/mutations_r4.md`). They are the
  52 from r3, re-aimed where the code changed, plus 27 new: 9 for R3-F1, 14 for R3-F2, 1 for
  the link's detail byte and 3 for the Pi tool. A SHA-256 check confirmed every mutated file was
  restored byte for byte. After the last run only comments changed (timing notes, an image
  size), and the image was built after them.
- **Sol's probe, unchanged, on r4:**
  - both real-image installs pass (the r4 `pitrac.bin`, 220,476 B);
  - R3-F2's case prints `pending=1` and `1 (expected 1)`;
  - it stops at its byte-equality assertion for R3-F1, which r4 makes false on purpose: sector 0
    is cleared.
  - The copy with that one check adapted (`sol_update_r3_probe_r4adapt.c`) prints `valid=0`
    and `eligible ... 0 (expected 0)`, and exits 0.

**Errors of mine:**
- **"A partial install is never bootable"** (r3's §6 entry, and `fw_update.h`'s "wherever a
  reset lands"). I reasoned about an install cut short and never about a last write that
  *fails*, which can clear the TBYB bit and leave an image a normal boot launches. Sol's R3-F1.
- **I made settling the result and powering the Pi one step.** At classification the
  obligation moved from the note into a RAM flag, so a reset in the next second lost it. Sol's
  R3-F2.
- Found before handover: my first r4 draft decided "bought on trial" by whether *this* boot
  bought. A reset between the buy and the settling would then have read as "launched WITHOUT a
  trial". The reset sweep's design showed it; the note's phase decides now.

**Policies the reviewer and the owner should see:**
- a fault after the power request counts as met (no retry into a fault);
- `upd abort` cancels a pending power-on;
- an arm timeout keeps the power-on only if STANDBY was reached.

**Not on hardware:** `CLEANUP` cannot be produced on a healthy part. It is host-tested only.

---

### 2026-10-20 - independent confirm review of r4: R3-F2 closed; cleanup finding R4-F1

**NOT PASSED. Do not flash r4 or install its partition table. Board 1 stays on
`Oct 15 2026 22:09:11`.**
- Report: `captures/review_2026-10-19/SOL_REVIEW_r4.md`.
- **R3-F2 confirmed fixed:** the original reset-before-restoration case now retains the
  obligation and asks for Pi power once. The adapted original real-image probe also passes
  the R3-F1 case, with successful mock installs in both directions.
- **R4-F1 (P1):** cleanup accepts a negative answer from a parser that requires a whole
  IMAGE_DEF inside the first 4096 bytes. The ROM searches there for its start and can finish
  reading it beyond the boundary. With a header at 0xff0, TBYB cleared by a write fault and
  the last cleanup page ineffective, r4 reports FLASH/cleared instead of CLEANUP and never
  tries its fallback erase. The valid, non-trial header survives into the next sector.
- Independent reproductions: `sol_update_r4_probe.c` links unchanged production code and
  fails the safety assertion; `sol_rom_scan_r4_probe.c` runs the unchanged official ROM
  scanner body on the resulting bytes and confirms the surviving block and valid loop.
  These are synthetic host cases, not claims about r4's own metadata placement or a bench boot.
- Required: prove conservative absence of ROM block starts (e.g. whole sector actually zero
  or erased), otherwise CLEANUP. Add boundary and partial-cleanup regressions with an
  independent oracle. Correct the comments that call the limited parser a ROM proof.
- Timing clarification for handover: ~1 ms is the quoted typical time for two cleanup pages,
  not the whole fault exposure or a maximum. Remaining final-write pages and readback can
  precede cleanup; nothing here was timed on the board.
- Independently rerun: 224/224 native tests, 23/23 Pi-tool tests. Shutdown-hook tests could
  not start: WSL 0x80070569; their files are unchanged in the delta and previously passed
  21/21. The retained-address check passes. The 79 mutations were inspected, not rerun.
- Supplied stamp, sizes and all three hashes match. Doc links: 18 files, zero problems.
  No production source or firmware image changed, rebuilt or flashed; no commit.

---

### 2026-10-20 - r5: R4-F1 fixed; host-tested; not flashed, confirm review pending

**Image r5, NOT flashed:** Release `Oct 20 2026 14:59:52`, 224720 text / 0 data / 422112 bss,
version 1.1, try-before-you-buy (`picotool info -a`: `Release`, `tbyb: not bought`).
- `pitrac.uf2` SHA256 `AC5F4493F5A4C7E8B9B1A387BE128F42055410AFADC9A5B58C092ADBC272ACF7`.
- `pitrac.bin` (what the Pi sends) 220,628 B, SHA256 `C2E820258C87C0C23C0CFBF298EC57142C4EA99E0E8BB734928A465B4F41CF6F`.
- `pitrac_pt.uf2` unchanged: SHA256 `368057302F84E41A33E099FD89C694846A885263AD5C46459C5AB035D6C2CB13`.
- Test image `build_hang/pitrac.uf2` (§8.7 Step 3 only): `Oct 20 2026 14:59:56`, SHA256
  `7366EC15F43218A55CD708F5F6DEF4916CD1B39584349E7BD89A9424C13AD943`.
- Otherwise r4 unchanged. Board 1 still runs `Oct 15 2026 22:09:11`.

**R4-F1: the cleanup's proof was my own parser.** Fix (`fw_update.c`: `partner_clear()`):
- after the zeros, sector 0 counts as cleared only if **every byte reads back 0x00**;
- otherwise it is erased, and counts only if **every byte then reads back 0xFF**;
- otherwise the result is `CLEANUP`.
- No parser is involved, and neither are the programs' or the erase's own return values.
- Why that is enough, from the boot ROM's source: a partition's first block is looked for only
  in its first 4 KB (`BLOCK_LIST_SEARCH_MAX`), and with no start marker there the scanner reads
  nothing further. 0x00000000 and 0xFFFFFFFF are not the start marker.
- `upd`'s `partner` line no longer says "no image" on the parser's word. It says `erased`,
  `cleared to zeros`, a parsed version, or `UNKNOWN` (BENCH_P8 §8.7 and §8.9 updated).
- `fw_update.h` states the residual window by its events, with no duration. It also says
  plainly that the incoming-image check (UPD_END) is the parser's view, not the ROM's, and
  assumes this repository's builds. Making that check ROM-exact is offered to the reviewer,
  not done.
- From the scanner's source: after an *invalid* candidate it searches on through its buffer,
  up to ~1 KB past the 4 KB. The interrupted-last-write claim now names that dependency: the
  image must have no other start marker there. This build has one start-marker word below
  0x1800, its IMAGE_DEF at 0x138.

**The test oracle is now the ROM's own scanner** (`tests/rom_scan.c`, `rom_scan.h`):
- `s_varm_crit_next_block()` is copied mechanically from pico-bootrom-rp2350 master (fetched
  2026-10-20), byte-identical to upstream and to the reviewer's harness. It keeps its
  BSD-3-Clause notice; it is test code only.
- `rom_pick_normal()`, `no_image_in_b()` and the buy model use it; `fwu_parse_image()` decides
  no test's boot outcome any more.
- The test file includes it at its end, so the reviewer's probes build unchanged.

**Tests:**
- **228/228 native** (+4):
  - `rom_oracle_semantics` (the boundary block the parser misses, a linked loop, the search
    past an invalid candidate);
  - `boundary_block_cleanup_erases` (the review's case; the last zero page failing, or
    "succeeding" with nothing written);
  - `boundary_block_cleanup_fails` (CLEANUP, and the scanner finds the 1.2 block);
  - `linked_block_cleared` (a first block linked to an end block, as the SDK lays images out).
- The 51 update cases also pass in Debug, with the scanner's asserts live.
- **23/23 Python**, unchanged.
- **Shutdown hook:** still not rerun (WSL 0x80070569); its files are unchanged since r3.
- **Mutations: 83, all caught** (`mutations_r5.md`: the r4 set re-aimed, plus 4 for R4-F1).
  `clear_trusts_parser` puts r4's check back and is caught by both boundary tests. A SHA-256
  check confirmed every mutated file was restored byte for byte.
- **The reviewer's probes on r5:**
  - `sol_update_r4_probe.c`, unchanged: two erases of sector 0, `whole sector zero/erased: 1`,
    no surviving block, exit 0.
  - `sol_rom_scan_r4_probe.c` on what is left: the official scanner finds no first block. Its
    exit 1 now means "no surviving block", which is the fix.
  - The r3 probes behave as on r4.

**Errors of mine:**
- **r4's cleanup proof** ("then it is parsed as the boot ROM would parse it"). The parser wants
  the whole block inside 4 KB; the ROM finishes a block that only starts there. I made the code
  under review the judge of its own result. The reviewer's R4-F1.
- **r4's "~1 ms" window** described two cleanup pages, not the exposure, which starts at the
  fault inside the last write. The reviewer's timing note.
- **While making r5, my derived mutation runner (`mutate_r5.py`) still wrote its table to
  `mutations_r4.md`, and overwrote the r4 record the reviewer had inspected.** I caught it from
  a run's output. The runner is fixed. `mutations_r4.md` was regenerated by running the
  unchanged r4 runner against an exact copy of the r4 sources (scratch copy, 224/224, never the
  repo's sources): 79/79 caught again. One row, `step0_failure_not_cleared`, hit a transient
  build failure in that run and was rerun alone; the file says so.
- **Transient build failures in mutation runs.** Once per long run, one mutation's build failed
  and was recorded "DOES NOT COMPILE" (r5's first run: `not_live_at_boot`; the r4 regeneration:
  `step0_failure_not_cleared`). Both compile and are caught when run alone. `mutate_r5.py` now
  prints the build's error line and retries once before recording a mutation as not compiling.
  In its final run the retry fired once and showed the cause: `LNK1104: cannot open file
  ...fw_update_test.exe`, the test executable still held open from the run before.
- The session's scratch area was wiped between rounds. The r4 baseline was rebuilt by
  reverse-applying the handed-over r4 patch, and checked by regenerating that patch
  byte-identical.

---

### 2026-10-20 - independent confirm review of r5: PASS; R4-F1 closed

**Reflash first with the exact r5 image, then the existing no-Pi bench checks.**
Board 1 remains on `Oct 15 2026 22:09:11` until the owner flashes it.
- Report: `captures/review_2026-10-19/SOL_REVIEW_r5.md`. No new blocking finding.
- R4-F1 confirmed: cleanup now proves every first-sector byte zero, or every byte erased
  after fallback, otherwise CLEANUP. No incoming-image parser decides cleanup success.
- The unchanged boundary reproduction now performs two erases and exits 0 with an erased
  sector. The unchanged scanner diagnostic finds no block; its exit 1 is expected because
  its success condition was finding the old surviving boundary header.
- The adapted original real-image probe passes in both A/B directions, including the
  failed-header and rollback/reset cases. R3-F2 remains confirmed.
- Independently rerun: 228/228 Release host tests, 51/51 Debug update cases, 23/23 Pi-tool
  tests. **WSL now works: all 21/21 shutdown-hook tests pass again.**
- Test scanner body equals the previous independent official-source harness after EOL
  normalization. The retained-address check passes. The 83 mutation records were inspected,
  not rerun. Doc links: 18 files, zero problems.
- Verified image: Release `Oct 20 2026 14:59:52`, 224720 text / 0 data / 422112 bss,
  version 1.1, TBYB not bought. UF2 SHA256:
  `AC5F4493F5A4C7E8B9B1A387BE128F42055410AFADC9A5B58C092ADBC272ACF7`.
  BIN 220628 bytes, SHA256 `C2E820258C87C0C23C0CFBF298EC57142C4EA99E0E8BB734928A465B4F41CF6F`.
  Partition table unchanged (`36805730...C2CB13`).
- Reviewer recommendation on incoming-image checks: defer stricter format validation to a
  separate change; this handover assumes trusted repository SDK builds, not arbitrary
  crafted metadata. Future validation should not preserve an acceptance gap for a test
  fixture: test cleanup separately at the flash boundary if needed.
- Next: `BENCH_P8_PI.md` 8.0b, then 8.7, with no Pi on J8 and no camera on J4; later Pi and
  strobe work retains its existing gates. No hardware acceptance was performed here.
- No production source or firmware image changed, rebuilt or flashed; no commit.

---

### 2026-10-26 - A new Pi 5 for Phase 8, prepared off the board (part 1)

**Owner's session, reviewed from the pasted terminal log.** No board involved: the Pi ran on its
own USB-C supply. Board 1 still runs `Oct 15 2026 22:09:11`.
- **Pi:** a new Raspberry Pi 5, hostname `pitrac2`. Raspberry Pi OS **Lite**, 64-bit, **Trixie**
  (Debian 13), kernel `6.18.50+rpt-rpi-2712`. Fully upgraded; bootloader EEPROM up to date
  (25 Sep 2026); `gpiod` 2.2.1 already present.
- `cmdline.txt` after removing `console=serial0,115200`: `console=tty1 root=PARTUUID=... rootwait
  ...`. Nothing names `serial0` or `ttyAMA0`.
- `pi/` copied with `scp`; `sudo bash ./install.sh` installed the unit (enabled), the shutdown hook,
  persistent journald and `pitrac-link`. The two `config.txt` lines were added by hand; the
  second run reported both `present`.
- **Checks after the reboot, all pass** (`pi/README.md`):
  - `pitrac-rpi5-on`: `active (exited)`, ran `pinctrl set 22 op dh` ~5 s into boot;
  - `pinctrl get 22`: `22: op dh pd | hi // GPIO22 = output`;
  - `gpioinfo`: `line 26: "GPIO26" input active-low consumer="shutdown"`: the overlay, with the
    right polarity;
  - `/dev/ttyAMA0` exists (it does only with `uart0=on` on a Pi 5), and `fuser` finds nothing
    holding it;
  - the shutdown-hook tests under the Pi's `/bin/sh`: **21 cases, 0 failed**;
  - the link tool's tests: **23 run, OK, 4 skipped**. The four compare the tool with the firmware
    sources and skip outside a repo checkout. `pi/README.md` said plain `OK`; corrected;
  - the hook's self-check on the running system: `NOT safe -- / (/dev/mmcblk0p2) is still
    writable`, the correct answer while running;
  - persistent journal: after a power-off and power-on, the previous boot is still listed.
    Both boots show 12:46:08 at the boundary: the Pi 5 has no RTC battery and restarts its
    clock from the last saved time.
- **First run of the Pi-side files on a real Pi.** They were written against Bookworm and
  passed unchanged on Trixie.
- **Later the same day (owner):**
  - **the key-press check passed:** a jumper from pin 37 to pin 39 for ~0.5 s started an orderly
    shutdown. Lite leaves KEY_POWER to `systemd-logind`, so the board's request will work;
  - `pinctrl get 14-15`: `14: a4 pn | hi // GPIO14 = TXD0` and `15: a4 pu | hi // GPIO15 =
    RXD0`. UART0 is on header pins 8 and 10, with the Pi's pull-up on its RX;
  - the card backup is in progress.
- **2026-10-30 (owner):** the card is backed up and the backup verified: the §8.0 gate item
  "SD card imaged and the image verified restorable" is met.
- `groups`: `pitrac2 adm dialout cdrom sudo ...`. `dialout` is there, so `pitrac-link` can open
  `/dev/ttyAMA0` without `sudo`.
- **2026-10-30, the capture before the r5 reflash (owner, COM5):** `id` `Oct 15 2026 22:09:11`;
  `cfg` slot A seq 3 v1, carrier 104166 Hz, phase 0 ticks, threshold level 0, coalesce
  2000 us, adc5v scale 1.0694, u12b gain 14.5, path unset, TRACK = 0, phase model not
  fitted. `hist` answered `? 'hist'`: that command arrived with r2 (2026-10-19).
  **Error of mine:** I told the owner to capture `cfg` and `hist` on the old image, and wrote
  the same into `BENCH_P8_PI.md` §8.0b (2026-10-20). §8.7 Step 0 already said it. Both are
  corrected: `id` and `cfg` on the old image, `hist` from the new one. Checked at the same
  time: the old record survives the reflash. r2 put the counters in the record's
  already-reserved `telemetry[16]` and took `telemetry_only` from `reserved`, so the size and
  `CFG_VERSION` are unchanged and r5 accepts it.
- **Part 1 complete: the Pi is ready to seat.** Optional, not done: the DMM on pin 15 during
  `sudo poweroff`. Seating waits for the no-Pi board checks (§8.0b, §8.7, after the r5
  reflash) and for the owner's decision on the Phase 8 gate.

---

### 2026-10-30 - Board 1 on r5; §8.0b in progress; J8.17 is not routed (an error of mine)

- **The reflash (owner, COM5):** board 1 runs r5.
  - `id`: `built : Oct 20 2026 14:59:52`, `version : 1.1 (no partition table: flat at
    0x000000)`, the link and update feature lines, uid `a764f5332ca5ac53`.
  - `hist`: `boots 1`, every other count 0, `loaded : none saved -- counting from zero`,
    `flash : up to date -- slot B`. The 10 s telemetry save wrote slot B from slot A seq 3.
  - §8.0b Step 1 passes on these. `upd` not reported yet.
- **§8.0b Step 4 setup.** The owner asked whether a jumper J8.1 → J5.2 plus one J8.17 → J8.37
  would do. No to J8.37: it is the board's output (GPIO43 through R39). I suggested a shunt
  across J8.15–J8.17 instead, because the netlist puts J8.17 on +3V3_Pi5 with J8.1, and added
  that to `BENCH_P8_PI.md` §8.0b Step 4. With 3.3 V on J8.1, the owner read no 3.3 V on J8.15.
- **Error of mine:** I checked the netlist, not the copper. The layout routes only J8.1, with
  one F.Cu track to R45 pin 1. J8.17 has no track, via or zone.
  - `kicad-cli pcb drc` reports it as the board's only unconnected item.
  - The fab gerbers in `jlcpcb/` (the same as the zip that was sent) show pad 17 with nothing
    touching it. Every board of this order has it.
  - With a Pi seated it does not matter: the Pi's 3V3 pins are one rail. On the bench, J8.17
    is not a 3.3 V point.
  - Corrected: the `BENCH_P8_PI.md` note now says not to use J8.17; Step 4 stays J5.2 → J8.1
    and J5.2 → J8.15. New `NEXT_BOARD_REV.md` CR-27, a fact in §2, the pin table in
    `DEVELOPER_GUIDE.md`, `BENCH.md` Phase 1b, and a `HANDOFF.md` §7 trap with the DRC
    command.
- **No soldering fault to look for here:** the DRC finds nothing else unconnected on the
  board, and J8.15 reading ~0 V through an open shunt is what an unrouted J8.17 gives.
- **§8.0b Step 4, the simulated Pi (owner's log, COM5; J5.2 → J8.1 and J5.2 → J8.15):** every
  count and state as expected. `boots` was 4 at the start: 1 after the reflash, plus the
  power-ups while the owner moved probes and fitted the jumpers ("a few times"). The `seq`
  count under Step 5 agrees.
  - `on`: `RUNNING`, `pi : present 1 3v3 1 userspace 1`, `J8.37 : driven, deasserted`; `link`
    `CONNECTED to UART1`, 921658 baud (asked 921600), rx 1 B = 1 junk byte (the floating RX
    pin as the UART took it; no Pi drives J8.8 here).
  - `off`, then J8.15 pulled: `link` `released`; after the holdoff `pins` shows `LATCH_CONTROL`
    0, `RPI5_ON` 0, J8.37 `released`. `hist`: requests 1, read down in time 1, cuts 0.
  - Unrequested halt (J8.15 out ~30 s, longer than the doc's ~3 s; no criterion depends on
    it): the `DOWN WITHOUT A REQUEST` banner and `stat` line, latch stayed 1; back in, the
    `Pi reboot` banner. `hist`: unrequested 1, came back 1.
  - `forceoff`: cuts 1. `on`, `reset force`: after the reboot `boots` 5, cuts 2, `this boot :
    software reboot`, `loaded : RAM mirror`, `flash : up to date -- slot B`. Slots alternate,
    so a save also went to slot A in STANDBY after `forceoff`.
  - Scope and DMM (owner): J8.37 low for **200 ms** at `off`; J8.37 and J8.10 ≈ 3.3 V while
    connected.
- **Error of mine:** my chat steps gave absolute `boots` counts (2 after Step 4's reset, 3 after
  Step 5) that ignored the power-offs my own Steps 3 and 4 call for. `BENCH_P8_PI.md` states
  them as "+1" and is right. With this log: Step 5 expects `boots` 6.
- **A wrong label, cosmetic:** `pins` printed `released (no Pi 3V3 sensed: ...)` while
  `PI_3V3_SENSE(24)` read 1. The line is released whenever the latch is open
  (`pi_lines_service()`); the label (`safe_state_pi_line_name()`) names only one of the two
  reasons. True with a real Pi (its 3V3 falls with the latch); wrong on the bench, where J5.2
  keeps the sense up. Not worth a rebuild of the reviewed r5: §9.
- **§8.0b Step 5 PASS (owner's log):**
  - before the power-off: `boots` 5, `flash : up to date -- slot B`;
  - after 10 s off, jumpers out: `boots` 6, `loaded : flash (power-on)`, every other count
    unchanged (requests 1, in time 1, timed out 0, unrequested 1, came back 1, cuts 2,
    faults 0), `flash : up to date -- slot A`;
  - `cfg`: `slot A seq 11 v1`, every calibration field as captured before the reflash.
  - `seq` 3 → 11 is eight writes: one per boot (six boots, each changes `boots`), plus the
    STANDBY saves after `off` and after `forceoff`. The slots alternated as `config_store.c`
    writes them. No boot or write is unaccounted for.
- **Still to record for §8.0b:** Step 2 (a 1200-baud open and `picotool reboot -f` change
  nothing, `boots` unchanged) and Step 3 (`pins`/`link` released, J8.37 and J8.10 ≈ 0 V in
  STANDBY with no jumpers). §8.7 Step 1 still needs `upd` (expect `normal boot`, flat). The
  trial boot right after the drag was not looked at; §8.7 Steps 2 and 4 show trial and buy
  again.
- The three UF2s for §8.7 hashed on 2026-10-30, all as recorded: `build/pitrac.uf2`
  `AC5F4493…C272ACF7`, `build/pitrac_pt.uf2` `36805730…D6C2CB13`, `build_hang/pitrac.uf2`
  `7366EC15…13AD943`.

---

### 2026-11-02 - §8.0b PASS; §8.7: the table is in, and the first image went into B, not A (an error of mine)

- **§8.0b Steps 2 and 3 (owner):** both as written. A 1200-baud open and `picotool reboot -f`
  changed nothing; in STANDBY with no jumpers, `pins` and `link` read released and J8.37 and
  J8.10 ≈ 0 V. **§8.0b PASS**, Steps 1–5, on r5.
- **§8.7 Step 1 (owner):** as written (`normal boot`, flat).
- **§8.7 Step 2:** `picotool load build\pitrac_pt.uf2`, then `reboot -u`. The first
  `partition info` answered `ERROR: The RP2350 device returned an error: rebooting` (asked
  too soon); the second:
  ```
  un-partitioned_space : S(rw) NSBOOT(rw) NS(rw), uf2 { 'absolute' }
  partitions:
    0(A)       00002000->00100000 S(rw) NSBOOT(rw) NS(rw), id=0000000000000000, "PiTrac A", uf2 { 'rp2350-arm-s' }, arm_boot 1, riscv_boot 1
    1(B w/ 0)  00100000->001fe000 S(rw) NSBOOT(rw) NS(rw), id=0000000000000001, "PiTrac B", uf2 { 'rp2350-arm-s' }, arm_boot 1, riscv_boot 1
  ```
  As `partition_table.json` and the doc expect. Then the drag of `build/pitrac.uf2`: `id` →
  `version  : 1.1  (partition B at 0x100000)`, `built Oct 20 2026 14:59:52`. The owner
  stopped there, because the doc said A.
- **Error of mine:** §8.7 Step 2 said the image goes into A, and Steps 3 and 4 followed from
  that. I wrote it from "a UF2 goes into the partition that is not running" without checking
  the case where nothing runs. The boot ROM source (`pico-bootrom-rp2350`, read 2026-11-02):
  - `varm_nsboot.c`, `s_varm_crit_ram_trash_find_uf2_target_partition()`: for an A/B pair it
    asks which partition would boot, then takes the other one ("Switching sense since we want
    the non-bootable partition").
  - `varm_flash_boot.c`, `s_varm_crit_choose_by_tbyb_flash_update_boot_and_version()`: with no
    image in B it returns A ("it may not have one either of course"). So the first image after
    the table always goes into B.
  - The reviews did not catch it either. Nothing is wrong with the board or the firmware:
    `fw_layout.c` takes its partition from where it runs, and the host tests cover A → B and
    B → A.
- **A second claim, corrected from the same source:** "when an image with a version no higher
  than the other's is bought, the ROM erases the other's first sector". It was in the
  `fw_update.h` comment, in §8.7 Step 4, and in the r3 entry above ("Equal versions work, but
  leave no fallback").
  - The version test is strict (`inline_is_version_greater`), and a normal boot with equal
    versions picks A.
  - So the erase happens only when the other image would otherwise win the next normal boot:
    it has a higher version, or an equal one and sits in A. An equal-version image bought in A
    leaves B bought, as a fallback.
  - Corrected in the `fw_update.h` comment. That is a comment only: nothing is rebuilt, and
    the image on the board and in `build/` is unchanged.
- **The board now (owner's `upd`, as predicted above):** `boot : flash update boot, ON TRIAL:
  bought itself at the end of init`, `layout : this image in partition B`, `partner :
  not erased, and no IMAGE_DEF this firmware can read: UNKNOWN`, `staged : idle`. A's first sector
  still holds r5's flat bytes from offset 0x2000. There is no block start marker there
  (checked on `build/pitrac.bin`), so the boot ROM finds no image in A, and `upd` will call the
  partner `UNKNOWN`. An update from the Pi erases that sector first (install step 0).
- **2026-11-09 (owner), the rest of Step 2:** `cfg` → `slot B seq 14 v1`, every calibration
  field unchanged. `hist` → `boots` 9, `this boot : power-on`, `loaded : flash (power-on)`,
  every other count unchanged, `flash : up to date -- slot B`. Seq 11 → 14 is three boots
  (Step 1's `reset`, the boot after the drag, a power-on), one write each, in alternating
  slots. **§8.7 Step 2 PASS.**
- **§8.7 Steps 2–4 rewritten** in the order the ROM uses:
  - Step 2: r5 into B, partner A `UNKNOWN`;
  - Step 3: the hang image into A, then back on B by itself;
  - Step 4: the real image into A, partner B `version 1.1, bought: the fallback`, and still A
    after a power cycle.
  - Also corrected: `HANDOFF.md` §4 and `DEVELOPER_GUIDE.md` (the first load after the table
    goes into B).

---

### 2026-11-03 - Original-board batch rework plan; correction to the old OPA4323 sizing assumptions

- **Owner request:** a step-by-step plan for three original boards using 0603 162 kohm
  resistors, 0402 2 pF capacitors and 0603 1 uF / 50 V capacitors described as XR5 (X5R to
  be confirmed). Prepared [REWORK_ORIGINAL_BOARDS.md](REWORK_ORIGINAL_BOARDS.md).
- **Proposed target, not completed work:** CR-19's approximately 120 kohm / 1.5 pF network:
  470 kohm || 162 kohm = **120.475 kohm**, and 2 pF added across **each** original 1 pF
  C68/C70 gives 3 pF series 3 pF = **1.5 pF**. Two new 2 pF parts per stock board; the
  original components remain fitted. Add CR-23 stage 1 at the actual Q9 gate, after R63,
  to R66 GND, leaving U6/U7 and the resistors unchanged. Board 1 already has a gate hold.
- The exact 0603 hold-capacitor part number/DC-bias curve and 2 pF dielectric are not yet
  provided. The plan requires C0G/NP0 for 2 pF, identifies the X5R bias uncertainty, and
  gates repetition on one board's inspection and acceptance. It explicitly preserves the
  board-1 optical damage and the unresolved boards-2/3 short/damage gates; which three
  physical boards the owner means has not yet been confirmed. **No board state changed.**
- **Error in the old model's part specifications:** CR-15 used **8 MHz GBW** and
  **1.5 V/us slew rate** for OPA4323. Those are wrong for the fitted OPA4323IPWR:
  [TI's datasheet](https://www.ti.com/lit/ds/symlink/opa4323.pdf), revision C, specifies
  **20 MHz GBW and 33 V/us typical**. Corrected the current sizing table and marked the
  older compensation-ratio/"maximally flat" argument and the slew-limiting explanation
  as superseded. With the same uncertain Cin = 20 pF, the formula gives about **1.17 pF at
  116 kohm** and **1.15 pF at 120.475 kohm**, rather than the old approximately 1.85 pF.
  This does not erase the measured clipping or ringing, certify 1.5 pF on these boards,
  or identify a new optimum. Archived entries are retained verbatim as history.
- Netlist and PCB pad identities checked. Read-only KiCad DRC: **26 violations, one
  unconnected item, J8.17**, as previously recorded. Pad locations/gaps checked for the
  actual solder/probe sites. No claim of clean DRC. The new plan states board power,
  connection safety, commands and acceptance references; the phase-shift advice now
  distinguishes the old board-3 rework from the proposed larger-Cf network.
- Audited relevant firmware output/comments for affected values; no R80/C68/C70 value
  update is needed in firmware. CLI's generic reflector/ND-filter advice remains less
  specific than the established no-reflector/insulating-aperture procedure; the new plan
  uses that procedure. **No firmware edit, build, flash, bench result or commit** in this
  task. The existing r5 UF2 SHA256 was rechecked and matches `AC5F4493...C272ACF7`.

---

### 2026-11-03 - Owner clarification: C0G confirmed; three additional original boards

- The owner confirmed the **0402 2 pF parts are C0G** and the rework batch consists of
  **three other original boards**, not bench boards 1-3. This resolves the two pending
  questions in the earlier entry. No damage or previous modifications have been reported
  for these additional boards; their inspection, UIDs and bring-up status remain pending.
- Updated [REWORK_ORIGINAL_BOARDS.md](REWORK_ORIGINAL_BOARDS.md) to remove the unrelated
  bench-board repair/rework branches from this batch's procedure, explicitly retain all
  three original TIA parts if stock, and add a separate pending RW-A/B/C tracker. Each
  stock board gets **one 162 kohm resistor, two 2 pF C0G capacitors, and one 1 uF gate-hold
  capacitor**: totals **3 resistors, 6 small capacitors, 3 hold capacitors**.
- The exact 1 uF part number/DC-bias data is still unspecified. No firmware change, build,
  flash, rework, powered test or commit. Bench boards 1-3 and the section 10 resume point
  remain unchanged.

---

### 2026-11-03 - Ordered verification protocol for the additional-board rework

- Owner asked for the easiest step-by-step way to check the work. Added
  [BENCH_REWORK_CHECK.md](BENCH_REWORK_CHECK.md), linked from the construction plan: magnified
  inspection, unpowered DMM connections, firmware/power prerequisites, TP7 optical response,
  recalibration, dry gate DC/decay and a guarded low-current pulse/clamp check. It does not
  claim an ordinary DMM or a successful phase calibration measures the 1.5 pF network.
- Added an explicit pitfall: **R66's sense-side pad is only 0.135 ohm from GND** through
  the parallel shunts. A normal continuity test cannot identify the correct ground end or
  reliably detect a solder bridge across the sense network. The capacitor must be on
  **R66 pad 1, farther from Q10**, established by pad identity and visual inspection.
- Checked the netlist and PCB for the new optical probe-ground location, **C72 pad 2 GND**,
  about 4 mm from TP7. Its own signal pad is 0.90 mm away, and R82's ADC pad about 0.85 mm:
  attach a secured wire fully unpowered. Optical grounds are disconnected from the scope
  before it moves to the strobe area, where all grounds use R66 GND. Read-only DRC again
  found the same **26 violations and one unconnected J8.17 item**; not a clean-DRC sign-off.
- The short loaded check stops at the first clean **0.5-2 A** point, with an additional
  **2 A peak stop** for this check; no forced gate percentage and no increase of firmware
  limits. Original 6a/6b and 6c prerequisites still apply. Full 9 A / burst acceptance is
  still the rest of 6c per board. Gate settling reference values are marked as board-1
  measurements, not guaranteed new-board targets; scope trigger timing is stated explicitly.
- Audited the relevant CLI output/comments and existing procedures against the new steps;
  no firmware change is needed. The reviewed r5 UF2 hash remains `AC5F4493...C272ACF7`.
  **No board results reported, firmware edit/build/flash or commit.** The RW-A/B/C tracker
  remains pending and bench boards 1-3 / section 10's resume point are unchanged.

---

### 2026-11-03 - Two additional boards: 280 kohm and 160 kohm measured across U11 pins 1-2

- **Owner report:** resistance across U11 pins 1-2 was **280 kohm on one board and 160 kohm
  on another**. Board labels/UIDs, meter/polarity/range, power/discharge state and whether
  the 162 kohm addition was present on both are not yet specified. Do not mark either
  value as a completed rework pass or diagnose a failed U11 from these readings alone.
- Netlist confirms **U11.1 = R80.2 (TIA output)** and **U11.2 = R80.1 (summing node)**.
  Stock resistance is 470 kohm; the intended added pair is 470 || 162 = **120.475 kohm**.
  Ordinary extra parallel resistance alone cannot explain a reading above the intact
  reworked pair's resistance. Check measurement conditions/contact, illumination and
  original/added resistor joints before assuming benign IC variation or damage.
- Added a targeted unpowered follow-up to [BENCH_REWORK_CHECK.md](BENCH_REWORK_CHECK.md):
  all power absent/discharged, insulating shade over D12, loose-part check, both-polarity
  readings across R80's actual pads, then pin-to-pad continuity if needed. No component
  removal or powered acceptance prescribed until the readings are understood.
- PCB identities checked; read-only DRC again reports **26 violations, one unconnected
  item (J8.17)**, not a clean-DRC sign-off. No firmware edit/build/flash or commit. The
  original bench-board resume point is unchanged; this finding concerns the additional batch.

---

### 2026-11-03 - COM10 UID 6093584a7790419d: optical captures clip; R80 parallel arrangement confirmed

- **Owner:** reports reaching step 6 and saving the log/captures, suspects saturation and
  wrong Rf. Then explicitly confirms the original **470 kohm R80 remains fitted with
  162 kohm added in parallel**. That is the intended topology, **120.475 kohm nominal**;
  it does not yet prove the solder connections/effective resistance. The earlier 280/160
  kohm readings remain unresolved and unmapped to UIDs.
- Read all three new Serial Monitor logs:
  `captures/COM10_2026_11_03.15.35.55.047.txt`, `.15.39.53.633.txt` and
  `.15.44.23.130.txt`. The four 400-sample blocks in the pasted attachment match the
  `.15.44.23.130` file **sample for sample**. Every block is `mask=0x04` (ADC2, raw TIA
  monitor), `rate=500000`, `overran=0`; no missing rows.
- **Identity:** UID `6093584a7790419d`, r5 `Oct 20 2026 14:59:52`, version 1.1, **flat,
  no partition table**. Initial config slot A seq 7, phase 1343 ticks, gain 14.5, fitted
  phase model, calibration duty 25%, ADC5V scale 1.0617. After `adc5vcal 5.18`, scale
  **1.0688**, `adc5v` **5.182 V**, saved **slot A seq 9**. Do not undo that trim merely
  because the optical capture clips. GPIO24/0/8/9 read 0; GPIO27 low, strobe DRY/gate 0
  in the recorded identity block.
- **Beam-off ADC2:** 3200 codes, **2.5788 V**, consistent with the approximately 2.59 V
  reference. **Capture statistics** (nominal ADC conversion 3.3 V / 4095; not an optical
  channel calibration):

  | Beam duty | ADC2 min-max codes | Nominal min-max V | Samples at 4095 | Samples below 20 |
  |---|---|---|---|---|
  | 2% | 1186-3512 | 0.956-2.830 | 0/400 | 0/400 |
  | 4% | 26-3767 | 0.021-3.036 | 0/400 | 0/400 (17 samples below 40) |
  | 8% | 8-4095 | 0.006-3.300 | **166/400, 41.5%** | 32/400, 8.0% |
  | 25% | 3-4095 | 0.002-3.300 | **198/400, 49.5%** | **117/400, 29.25%** |

- **Conclusion:** definite ADC2 clipping at 8% and 25%; 4% is already close to the
  negative rail. This is before the lock-in/demodulated gain stages, so the saved demod
  phase/duty warning cannot cause this raw-channel waveform. **4095 proves the 3.3 V ADC
  ceiling, not the TIA's positive supply rail**: the TIA runs on +5VA. The near-ground
  samples support front-end overload; a TP7 scope capture is still needed to establish
  actual TIA rail clipping versus monitor clipping and edge behavior. No scope data is
  present here. At 500 ksps / 104166.67 Hz, only 24 carrier phases are sampled; the 2%
  capture's nonzero minimum does not prove the true narrow-pulse minimum was captured.
- **Rf is not determined by these captures.** Gain and received photocurrent both affect
  swing. An open/wrong R80 joint could increase effective Rf, while the already established
  D11-to-D12 optical crosstalk/reflections can overload even a correctly valued network.
  The log supplies no baffle/target geometry, postwork resistance measurement or scope
  trace that separates them. Settling intervals/warm-up/thermal acceptance are not
  established by the command-only log; do not convert the owner's step-progress report
  into a full per-board sign-off.
- **Next:** leave the beam off; `off`, `stat` -> STANDBY/latch 0, then PSU and USB off,
  discharge. Shade D12 with insulating card; inspect and check both original/added R80
  joints to their PCB pads, then measure across R80's actual pads in both polarities by
  the check protocol. Resolve the earlier resistance discrepancy before deciding whether
  more solder work is needed. Optical acceptance/calibration is pending; do not save a
  new optical calibration from a clipped setup or infer a new resistor target from this log.
- **Last state evidence:** log ends with `beam off`; there is **no subsequent `off`/`stat`**,
  so the main rail's final state is not established. No live-strobe firing is logged.
  No firmware edit/build/flash or commit in this analysis. Added the ADC-monitor versus
  TIA-rail distinction to the check procedure; relevant CLI output/comments audited.

---

### 2026-11-09 - §8.7 Step 3: the real image reached A, not the hang image; plan from there

- **Owner's log after Step 3's drag:** `id` → `version  : 1.1  (partition A at 0x002000)`,
  `built Oct 20 2026 14:59:52`, no `TEST BUILD` line. `upd` → `boot : flash update boot, ON
  TRIAL: bought itself at the end of init`, `this image in partition A`, `partner  : version
  1.1, bought: the fallback`, `staged : idle`.
- **What reached A was `build/pitrac.uf2`.** The hang image's stamp is `14:59:56`, and it never
  buys. Both files were hashed and decoded the same day: `build/pitrac.uf2` `AC5F4493…C272ACF7`,
  stamp `14:59:52`, no `TEST BUILD` string; `build_hang/pitrac.uf2` `7366EC15…C13AD943`, stamp
  `14:59:56`, `TEST BUILD` present. Both are named `pitrac.uf2`. The owner confirmed it: the
  wrong copy.
  - §8.7 Step 3 now says to copy by path (`Copy-Item build_hang\pitrac.uf2 E:\`).
- **Seen on hardware, as the ROM source predicted (2026-11-02 entry):** an equal-version image
  bought in A leaves B bought, as the fallback.
- **The rollback from here is safe** (boot ROM source, `varm_launch_image.c`, read 2026-11-09):
  - The hang image goes into B, since a normal boot picks A (equal versions).
  - Its flash-update boot queues an erase of A (B's version is not higher than A's). For a
    TBYB image that erase waits for the explicit buy ("not erasing other slot as explicit
    TBYB flagged"), which the hang image never reaches.
  - After the trial watchdog, the normal boot never launches an unbought image ("NOT booting
    TBYB flagged image which isn't the flash update"), so it runs A.
- **Plan from this state:**
  - Step 3: the hang image into B; back on A by itself in 16–25 s; `id` partition A, no
    `TEST BUILD`; `upd` partner `version 1.1, NOT bought`.
  - Step 4: the real image into B; `upd` partition B, `ON TRIAL: bought itself`, partner
    `erased: no image` (the ROM erases A at the buy: an equal version in A would otherwise win).
    After 10 s off: still B, `normal boot`; `cfg` unchanged.
- **Step 3 with `build_hang/pitrac.uf2` (owner):** `id` → `version  : 1.1  (partition A at
  0x002000)`, `built Oct 20 2026 14:59:52`, no `TEST BUILD` line. `upd` → `boot : normal
  boot`, `this image in partition A`, `partner  : version 1.1, NOT bought (a trial that
  failed, or never ran)`, `staged : idle`. The rollback happened, and the image the ROM
  rejected is in B, unbought. **The time from the copy to the port's return (pass: 16–25 s)
  is not reported yet.** If it was not noted, repeating Step 3 from this state is safe: the
  hang image goes into B again.
  - The owner then reported **about 20 s** from the copy to the port's return. **Step 3 PASS.**
- **Step 4 (owner), `build/pitrac.uf2` copied by path:** `upd` → `boot : flash update boot,
  ON TRIAL: bought itself at the end of init`, `this image in partition B`, `partner  :
  erased: no image`, `staged : idle`. The boot ROM erased A's first sector at the buy, as
  predicted. After the power cycle: `boot : normal boot`, still partition B, partner erased.
  `cfg` after it → `slot B seq 18 v1`, every calibration field as captured before the r5
  reflash. Seq 14 → 18 is four writes in alternating slots, one per boot of the real image (the
  wrong-copy drag, the rollback, Step 4, the power cycle); the hang image writes nothing.
  **Step 4 PASS.**
- **§8.7 PASS on board 1.** Seen on hardware: the table, a trial and buy
  in each partition, a rollback in ~20 s, the ROM's buy-time erase (B bought over an equal A),
  and no erase for an equal-version buy in A.
- **Board 1 now:** r5 in partition B, bought; A erased. The next USB load goes into A. Never
  load an image older than 1.1.

---

### 2026-11-09 - Phase 8 begins: the Pi seated on board 1; first-seating checks PASS

- **The gate:** relaxed by the owner the same day (§8): a Pi on J8 for 8.0–8.5, 8.8 and 8.9.
- **Setup:** the new Pi 5 (`pitrac2`, §6 2026-10-26) seated directly in J8, under the board on
  M2.5 standoffs at MH1/MH2/MH4. SW2 taped over; the Pi's USB-C unplugged; nothing on J4.
- **Checked on the PCB first** (`.kicad_pcb`, Edge.Cuts gerber):
  - The board is 96 × 88 mm and ends at y = 174.65 mm, where the Pi 5's USB/Ethernet jacks
    begin on the MH1/MH2/MH4 hole pattern, so the tall jacks clear it.
  - Under the Pi's outline the underside exposes only J8, GND and the test points TP6–TP10
    (all low voltage); no 12 V or 36 V copper faces the Pi, and J3 is outside it.
  - R39 (1 kΩ, 0402, GPIO43 → J8.37 in series) sits 1.0 mm from R86 and 1.4 mm from C74, all
    3.3 V-level nets.
- **First seating, PSU on, Pi unpowered (owner's log):**
  - `id`: r5, `partition B at 0x100000`.
  - `stat`: `STANDBY`, latch 0, `5V_IN 5.180 V`, `pi : present 0  3v3 0  userspace 0  isdown 1`,
    J8.37 released, no fault.
  - `pins`: `PI_3V3_SENSE(24)` 0, `RPI5_ON(0)` 0, `RPI5_SHUTDOWN(43)` released.
  - **DMM across R39: 0 V.** No current into the unpowered Pi's GPIO26. PASS.
  - `hist` baseline: `boots` 15 (14 at the end of §8.7, plus this power-on), requests 1, in
    time 1, timed out 0, unrequested 1, came back 1, cuts 2, faults 0; `flash : up to date --
    slot A`.
  - J8.1 in STANDBY: not reported as a number yet. `PI_3V3_SENSE` reads 0.
- **Doc fixes from the owner's questions:** "First seating" step 1 now says the Pi's whole
  40-pin header goes straight into J8, under the board, not single jumper leads; step 3 says
  R39 is in series (one DMM lead on each pad), not a resistor to GND, and names its neighbours.
- **The Pi's first power-up from the board (owner's log):** `on` → `stat` 76 s later:
  `RUNNING`, latch 1, railsready 1, `pi : present 1  3v3 1  userspace 1  isdown 0`, J8.37
  `driven, deasserted`, no fault; `5V_IN 5.166 V` (5.180 V in STANDBY, so 14 mV lower with the Pi
  booted). `link`: `CONNECTED to UART1`, rx 0 B, **0 junk bytes**: with a Pi driving J8.8 the
  stray byte of the simulated-Pi test (§8.0b Step 4) does not appear. The DMM reading on J8.1
  is not reported yet. **First power-up PASS** on the board's states.
- **On the Pi (owner):** `vcgencmd get_throttled` → `throttled=0x0` while powered through the
  latch; `gpioinfo` → line 26 `input active-low consumer="shutdown"`; `pinctrl get 22` →
  `op dh pd | hi`.
- **§8.1 with a real Pi, by `off`** (no panel button is wired; `off` calls the same
  `begin_shutdown()` as a short press). Saleae: digital on J8.37, analog on J8.1, 312.5 kS/s,
  triggered on J8.37 falling (`captures/p8_2026-11-09_8.1_shutdown/`: `digital.csv`, `analog.csv`; analysed by
  streaming the 130 MB CSV):
  - **J8.37 low for 199.1 ms** (0 → 0.1991 s), then high. **Released at 14.9992 s**, when the
    latch opened: the **15 s holdoff** held, though the Pi had read down long before.
  - `stat` polled through it: `SHUTTING_DOWN`, and at 2.47 s already `userspace 0  isdown 1`,
    so the Pi's late hook dropped RPI5_ON within 2.5 s of the request. No fault. Then
    `STANDBY`, latch 0, `pi : present 0  3v3 0`, J8.37 released.
  - **J8.1 (the Pi's header 3V3) did not drop at halt.** Means per 0.5 s stayed 3.273–3.287 V
    from the request until the latch opened, ~12 s after the Pi read down. The ripple maximum
    fell from ~3.34 V to 3.29 V at 3.5–4.0 s, as the SoC went quiet. J8.1 then crossed
    2.54 V (the board's sense threshold) 3.8 ms after the latch opened, and sat at −0.03 V.
  - `hist`: requests 2, read down in time 2, timed out 0, cuts 2 (unchanged), boots 15;
    `flash : up to date -- slot B`.
  - **§8.1 PASS. Q7 closed on the Pi side:** the overlay's KEY_POWER halts the Pi on the
    board's 200 ms request.
  - **Q4, answered for a requested shutdown: the header 3V3 stays up at halt** on this Pi 5
    (default EEPROM). `PI_3V3_SENSE` never fires at halt; RPI5_ON does all the down
    detection. §8.2 (`sudo halt`, unrequested, rail held up) is still to run, and confirms
    it over a longer wait. Then `board.h` gets the result, as §8.2 asks, and §9's open question
    (should the board drop the rail after an unrequested halt?) can be decided.
  - Shutdown duration, request to "down": ≤ 2.5 s (resolution: the `stat` polling). The 15 s
    holdoff is about 6× that; §8.2 suggests ~2×. Noted, not changed: it is a firmware change.
- **§8.2, an unrequested `sudo halt` (owner's log; Saleae: J8.37 digital, J8.1 analog,
  312.5 kS/s, 43.8 s; `captures/p8_2026-11-09_8.2_sudo_halt/`):**
  - `on`: `PI_BOOTING` at 8.0 s (3V3 up, `userspace 0`), `RUNNING` shortly after, `userspace 1`.
  - After `sudo halt`: the banner `*** THE PI WENT DOWN WITHOUT A REQUEST…`; `stat` →
    `RUNNING`, latch 1, `pi : present 1  3v3 1  userspace 0  isdown 1`, "DOWN WITHOUT A
    REQUEST, 4 s ago". The rail stayed up, by design.
  - **J8.1 stayed at ~3.25 V** (1 s means 3.244–3.261 V) for ~25 s with the Pi halted. The
    ripple maximum fell from ~3.33 V to ~3.28 V at ~9.5 s into the capture, as the SoC stopped.
  - `off` at 20.02 s: J8.37 low **199.7 ms**; the latch opened at 35.02 s (**15.0 s**), and J8.1
    fell below 2.54 V 3.9 ms later.
  - `hist` at the end: requests 4, in time 4, timed out 0, unrequested 3, came back 1, cuts 2,
    boots 15.
  - **§8.2 PASS. Q4 closed: the Pi 5's header 3V3 does not drop at halt**, requested or not.
    `PI_3V3_SENSE` shows power, not "the Pi is down"; RPI5_ON carries the down detection
    alone. Comments only, recording it: `board.h` (`PIN_PI_3V3_SENSE`) and `power_fsm.c`
    (`power_pi_is_down()`), whose old comment claimed GPIO22 falls at halt without the hook.
    That is not measured, and the comment now says so. Nothing rebuilt; the image is
    unchanged.
  - **The counts, accounted for (owner):** one full §8.2 cycle ran before this log, and its
    capture was cut short, so it was re-run. That is the extra unrequested halt and request.
  - **RPI5_ON (J8.15, digital, re-exported):** high from the start, **fell at 7.84 s**, low to
    the end. The J8.1 ripple settled at ~8.3–9.3 s, so the hook drops the line at the very end
    of the shutdown, ~0.5–1.4 s before the SoC stops, as designed.
  - **A probe rule broken (owner's report):** the J8.15 lead was clipped on with PSU and USB
    on. No harm: the board was in STANDBY with the switched +5 V off, and J8.15's neighbours
    (J8.13, J8.16, the unrouted J8.17) are logic lines behind 1 kΩ. The rule stands for every
    lead (`HANDOFF.md` §3 rule 8), and §8.3's J8.4 lead sits on a live +5 V pin next to GND.
- **§8.3, Q11: the Pi 5's +5 V → header 3V3 latency (owner; Saleae: J8.4 digital as the trigger,
  J8.1 analog at 312.5 kS/s; `captures/p8_2026-11-09_8.3_run1/` and `_run2/`):**
  - The J8.4 lead went on with PSU and USB off (`boots` 15 → 16). Both starts cold: 64 s and
    66 s in STANDBY first.
  - **J8.1 crosses 2.54 V (the firmware's sense threshold) 5.56 ms after the +5 V edge**, in
    both runs, between the samples at 5.555 and 5.558 ms. The ramp: 0.3 V at 5.10 ms, 3.2 V at
    5.69 ms, peak 3.33 V. The two files are distinct recordings (different hashes and
    samples); the Pi's power-up sequence repeats to one 3.2 µs sample.
  - The reference is J8.4's logic-threshold crossing; J8.4's analog is not in the export. On
    a rising rail the 4.5 V crossing comes later, so 5.56 ms is an upper bound on the doc's
    4.5 V-referenced number.
  - **§8.3 PASS. Q11 closed:** under 1 s, so `PI_DETECT_WINDOW_MS` (3 s, ~540× the
    measurement) stays, as §8.3's table says.
  - **Boot to RPI5_ON high: ~16 s** (`PI_BOOTING` ran to 15.7 s, then `RUNNING`, in both
    runs). `PI_BOOT_TIMEOUT_MS` is 90 s, ~5.6× that.
  - Recorded as comments in `board.h` (the detect window, the holdoff's measured ≤ 2.5 s, and
    the boot time). Nothing rebuilt.
  - The board was left in `RUNNING` with the Pi up after run 2: §8.4's starting state.
- **§8.4, the reset guard with a real Pi (owner):** `reset` and `bootsel` both answered
  `REFUSED: a Pi is present and powered (state RUNNING)` with the latch explanation, naming
  the command; `stat` stayed `RUNNING`, latch 1; the Pi's `uptime` kept counting. After `off`
  and STANDBY, `reset` went through: `hist` → `boots` 17, `this boot : software reboot`,
  `loaded : RAM mirror`, requests 6, in time 6, cuts still 2. **§8.4 PASS.** Q10 is not
  fitted, so its part is skipped.
- **`tools/p8_cycles.ps1` written for §8.5** (Windows PowerShell 5.1). It drives the 20
  cycles over the USB CLI and sends only `id`, `stat`, `hist`, `on`, `off`; anything else is
  refused in code.
  - It stops on: a fault, `BENCH_RUNNING`, an unrequested halt, a latch opening inside 14.9 s
    of `off`, a timeout, or with `-PiHost` a repeated boot id, `throttled` ≠ 0x0 or kernel
    storage warnings.
  - It compares `hist` before and after: requests and in-time +N, every other count +0.
  - **Tested against a fake board** (the scratchpad harness reproduces `stat`/`hist`/`id`
    exactly): pass, a 5 s latch opening, `BENCH_RUNNING`, `throttled=0x50005` and SSH down
    all behave as intended. The test caught one bug of mine, fixed: the console line swapped
    the latch time and the 5V_IN minimum (the CSV was right).
  - §8.5 in `BENCH_P8_PI.md` now names it, and replaces `sudo touch /forcefsck` (possibly
    ignored by Trixie's systemd) with a forced check through `fsck.mode=force` plus `dumpe2fs`.
- **SSH key login for the script (owner's Windows PC, 2026-11-09):** a new key `~/.ssh/pitrac`
  and a `Host pitrac2.local` entry in `~/.ssh/config` (`IdentityFile ~/.ssh/pitrac`,
  `IdentitiesOnly yes`). OpenSSH had refused the key: it inherited "Modify" for
  `CodexSandboxUsers` and an unknown SID from the user profile. That one file is now limited
  to the owner, SYSTEM and Administrators; the profile itself is the owner's call.
- **§8.5, 20 boot/shutdown cycles, `tools/p8_cycles.ps1` with `-PiHost` (13:11–13:34;
  `captures/p8_8.5_cycles_2026-11-09_131148.log` and `.csv`): PASS 20/20.**
  - `on` → `RUNNING` with userspace up: **16.6–16.7 s**. `off` → latch open: **15.03–15.05 s**,
    never inside the holdoff. 5V_IN minimum while running: **5.130–5.150 V**.
  - The Pi on every boot: 20 distinct boot ids, `throttled=0x0`, 0 kernel ext4/mmc warnings.
  - `hist`: requests +20, in time +20; boots, watchdog, timed out, unrequested, came back,
    cuts and faults all +0. No fault, no `BENCH_RUNNING`, no unrequested halt.
- **The end-of-run filesystem check (owner):** `fsck.mode=force` added, `sudo reboot`, then
  removed. `dumpe2fs`: `Filesystem state: clean`, no error counts.
  - **Error of mine:** I said to read `journalctl -u systemd-fsck-root`. On Trixie the root is
    checked in the initramfs, so that unit reports "skipped ... ConditionPathExists=
    !/run/initramfs/fsck-root" (which shows the initramfs check ran). The result is in
    `/run/initramfs/fsck.log`. §8.5 is corrected.
  - **The forced check (owner):** `/run/initramfs/fsck.log`: `fsck.ext4 -f -y` at 13:37:35
    local (18:37:35 UTC), passes 1–5. The only finding: "Inode 30385 extent tree (at level 1)
    could be shorter. Optimize? yes" in pass 3A, an optimisation that e2fsck applies on any
    forced check and that alone prints `FILE SYSTEM WAS MODIFIED`. No orphan inodes, bitmap
    differences or wrong counts, the signs of an unclean power-off. `dumpe2fs`: `Last checked:
    Mon Nov 9 13:37:35 2026`, `Mount count: 1`, `clean`, no error counts. **§8.5 PASS,
    complete.** `fsck.mode=force` was removed from `cmdline.txt` afterwards.
  - **§8.6, first row with a real Pi:** the `sudo reboot` printed both banners (down without a
    request, then read up again: a Pi reboot); `hist`: unrequested 4 (from 3), came back 2
    (from 1), cuts 2, the rail up throughout (`RUNNING`). PASS.
  - **The counts, accounted for:** requests 28 = 6 after §8.4, +1 for the `off` after the SSH
    key setup, +1 for a one-cycle trial of the script (`captures/p8_8.5_cycles_2026-11-09_
    131019.log`, which starts at 7), +20 for the run.
  - **§8.6, `sudo poweroff` then `off` (owner):** the unrequested-halt banner printed at `off`,
    then `requested shutdown`; STANDBY, latch 0, no fault. `hist`: unrequested 5, came back 2,
    requests 29, in time 29, cuts 2, boots 17. `link`: `released`, `connected 24 time(s) this
    boot` (= the key setup, the trial, 20 cycles, the fsck boot and the Pi's reboot), 0
    frames: the Pi has not used the link yet. **§8.6 PASS with a real Pi**, all three rows.

### 2026-11-10 - §8.8 PASS: the serial link with a real Pi; version 1.2 built for §8.9

- **Board state:** the owner powered the board off to attach a DMM lead on J8.10 (PSU and USB
  off), then on again. `hist` gave boots 18, `this boot : power-on`, `loaded : flash
  (power-on)`. Every count came back from flash (requests 29, in time 29, unrequested 5, came
  back 2, cuts 2, faults 0). After 10 s in STANDBY it saved to slot A.
- **§8.8, board 1, with `off` for the button (owner):**
  - **Step 1:** STANDBY, `link` → `released`, `connected 0 time(s) this boot`; J8.10 at 0 V.
  - **Step 2:** `on`. At `PI_BOOTING` 10.2 s, `link` still read `released`: the lines wait for
    RPI5_ON. Then `CONNECTED to UART1`, `connected 1 time(s)`, and J8.10 at 3.3 V. `stat` →
    `RUNNING`, 5V_IN 5.161 V. `id` → version 1.1, partition B, `Oct 20 2026 14:59:52`.
  - **Step 3, on the Pi:**
    - `ping` → `board    : PiTrac RP2354, protocol 1, version 1.1`.
    - `info` → `version  : 1.1   built Oct 20 2026 14:59:52` (the same stamp as `id`),
      `normal boot`, `partition B at 0x100000 (1016 KB); partner at 0x002000 (1016 KB)`,
      `staging  : 288 KB of RAM`.
    - `status` → `RUNNING (98.0 s)`, fault none, 5V_IN 5.162 V (against `stat`'s 5.161 V;
      `adc5v` was not run, but `stat` shows the same calibrated value), Pi present, latch
      closed, rails ready.
    - `hist` → the same counts as the USB `hist`.
    - `link` after these four: 4 good frames, rx 24 B, tx 157 B, last request 0x04.
  - **Step 4, 1000 pings:** no `FAIL`. `link`: 1004 good (+1000), 0 CRC errors, 0 cut short,
    0 junk bytes, 1004 answered. rx +6000 B and tx +12000 B: 6 B per request, 12 B per reply.
  - **Step 5:** `off`, then `link` read `released` every time. `stat` meanwhile read
    `SHUTTING_DOWN` with latch 1 and `isdown 1` at 4.9, 8.6 and 12.2 s; then `STANDBY`, latch
    0. The counters stayed (`connected 1`, 1004 frames). The Pi's SSH session closed with the
    shutdown, after the loop had returned.
  - **Not seen:** `link` still `CONNECTED` early in the shutdown. The Pi drops GPIO22 ≤ 2.5 s
    after the request (§8.1), before the first `link` arrived. The lines follow `latched() &&
    pi_3v3_present() && pi_userspace_up()` in every state (`power_fsm.c`), not the FSM. To
    see it, send `link` within about a second of `off`.
  - **§8.8 PASS** on board 1, with that one sub-point not observed.
- **Version 1.2 for §8.9 (built; not on a board):** one line in `CMakeLists.txt`,
  `PITRAC_FW_VERSION_MINOR` 1 → 2, and its comment. Built in `build_v1.2/` and
  `build_hang_v1.2/` (gitignored). `build/` and `build_hang/` keep r5 for the other boards'
  §8.0b and §8.7.
  - `build_v1.2/pitrac.uf2`: Release `Nov 10 2026 09:47:13`, 224728 text / 0 data / 422112
    bss, UF2 SHA256 `7B7DBEE3CB9B123CD5F984CB116A92BB2DA544E482C73BBC3B8197A4C24AD6FF`.
    `pitrac.bin` (what the Pi sends): 220636 B, SHA256
    `3EFB1072ABE34CB4C8912DCB93E248C8BF1D1D9F3E080685A8A5AA552E81CD18`.
  - `build_hang_v1.2/pitrac.uf2` (§8.9 Step 4 only): `Nov 10 2026 09:48:59`, 224968 text,
    UF2 SHA256 `FF611CD962FBD0EB3B5F79DF553AB49C9941B6260EAF28E0D4CBA760173B6F42`.
    `pitrac.bin`: 220876 B, SHA256
    `93F780C0E86A1E4394EF130CBEA860FF36BFE8E5C0A1797FEA43B4DD9B389A6E`.
  - **Proof that it is r5's code:** the same tree, built at 1.1 in a scratch directory (the
    line then put back), matches r5's `build/pitrac.bin` byte for byte except 14 bytes, all of
    them date stamps (`__DATE__`/`__TIME__` and the binary-info build date). Between that
    build and 1.2, the only compile-flag difference is `-DPICO_CRT0_VERSION_MINOR=1` → `2`.
  - **Size:** +8 B of text. `fwu_apply` and `fwu_boot_check` grow 4 B each: 0x00010002 has no
    single-instruction Thumb-2 encoding, and 0x00010001 does.
  - **Checks:** the Pi tool's own `check_image()` on both `.bin` files gives version 1.2,
    try-before-you-buy, an Arm Secure executable, and a fit in the 288 KB staging buffer.
  - **Tests:** not re-run. No source changed, and host builds take their own 1.1 default from
    `fw_update.h`.
  - **Scope:** version-only on purpose. The J8.37 label rewording (§9) waits for the next real
    change. Whether 1.2 needs a review before §8.9 is the owner's call.
- **Error of mine, an incomplete correction:** on 2026-11-02 I corrected the buy-time erase
  rule in `fw_update.h` and §8.7 only.
  - These still said an image no newer than the running one always leaves no fallback:
    HANDOFF §4, `DEVELOPER_GUIDE.md`, `pi/README.md`, the `CMakeLists.txt` comment, and the Pi
    tool's `NOTE`.
  - The rule: the buy erases the other image only when that one would otherwise win the next
    normal boot.
  - Found while preparing 1.2. The docs and the CMake comment are corrected. The tool's text
    waits for its next change (§9), so §8.9 runs the reviewed tool.

### 2026-11-10 - §8.9 PASS: version 1.2 installed from the Pi, and a rollback with a Pi

- **The owner ran §8.9 with 1.2** without a separate review. The image changes only the
  version, as the entry above shows.
- **Step 1 (owner):** `pitrac-link update pitrac_v1.2.bin` printed what §8.9 now expects:
  - `image    : pitrac_v1.2.bin, 220636 B, version 1.2, try-before-you-buy, SHA-256
    3efb1072abe34cb4`;
  - `board    : version 1.1 (built Oct 20 2026 14:59:52) at 0x100000; installs into
    0x002000`;
  - `sent     : 220636 B in 3.2 s, 0 retries; the board checked the SHA-256 and the image`.
  - `y` → `applied  : the board is shutting this Pi down (J8.37)`.
- **Step 2, the board:** `update: the Pi is off: installing into the other partition`, then
  `update: installed and verified; rebooting into it on trial`. The USB port dropped at the
  reboot, so the next two lines (the buy, and powering the Pi back on) were not captured. The
  `upd` output and the Pi's return show that both happened.
- **Step 3:**
  - On the Pi, `upd-status` → `installed, 1.1 -> 1.2` / `it ran on trial and bought itself`.
    The next `update`'s `board` line shows what `info` gives: `version 1.2 (built Nov 10 2026
    09:47:13) at 0x002000`.
  - The board's `upd`: `version 1.2`, `flash update boot, ON TRIAL: bought itself at the end
    of init`, `this image in partition A`, `partner  : version 1.1, bought: the fallback`,
    `staged : idle`, `installed, 1.1 -> 1.2 (decided at this boot)`. **On hardware, then:** a
    higher version bought in A leaves the older image in B (the rule corrected 2026-11-02).
  - `hist`: boots 19, requests 31, in time 31, unrequested 5, came back 2, cuts 2, faults 0;
    `loaded : RAM mirror`. `stat`: `RUNNING`, 5V_IN 5.162 V, scale 1.0694 (the calibration is
    intact).
- **Error of mine:** I told the owner to expect requests and in time at 30. They read 31,
  which is right. The `off` in §8.8 Step 5 was request 30, and I left it out. The doc's "+1"
  was right.
- **Step 4 (rollback with a Pi):**
  - `pitrac-link update pitrac_hang_v1.2.bin`: 220876 B, `SHA-256 93f780c0e86a1e43`, `board :
    version 1.2 ... at 0x002000; installs into 0x100000`. The `NOTE ... no fallback
    afterwards` printed (overstated, §9). Sent in 3.3 s with 0 retries; `y`.
  - The board installed it and rebooted it on trial. The trial hung, the boot ROM's watchdog
    brought back 1.2 in A, and the board powered the Pi again.
  - `upd-status` on the Pi → `ROLLED BACK, 1.2 -> 1.2` / `the new image never bought itself
    (it crashed, hung or did not start)`. The board's `upd`: `normal boot`, partition A,
    `partner  : version 1.2, NOT bought (a trial that failed, or never ran)`, `ROLLED BACK,
    1.2 -> 1.2 (decided at this boot)`.
  - `hist`: boots 21 (+2: the test image's trial boot, then the rollback), `boots after a
    watchdog hang` 0, requests 32, in time 32, unrequested 5, cuts 2, faults 0.
- **Time:** about 40 s from `y` (owner). The code's timeline to the Pi being back up is
  ~38 s: the 15 s holdoff, 1 s quiet in STANDBY, a ~3 s install, the reboot and buy, 1 s, then
  the ~16.7 s boot from §8.5. Still to confirm: was the 40 s measured to the Pi's login?
- **A wrong label, found here:** after the update's reboot, `hist` said `this boot : power-on,
  brownout, RUN pin (SW2) or debugger`, while `loaded : RAM mirror (warm reset; nothing lost)`
  showed a warm reset.
  - The cause is in SDK 2.3.0, `hardware_watchdog/watchdog.c`: on the RP2350,
    `watchdog_caused_reboot()` also requires `rom_get_last_boot_type() == BOOT_TYPE_NORMAL`.
    An update's reboot is a FLASH_UPDATE boot, so `boot_reason()` (`main.c`) falls through to
    POWER_ON.
  - The rollback's boot is a NORMAL one after the ROM's watchdog, and read `software reboot`.
  - Only the label is wrong: every count is right. The `main.c` and `telemetry.h` comments said
    otherwise and are corrected. Line counts are unchanged, and a scratch rebuild matches
    `build_v1.2/pitrac.bin` except the 6-byte time stamp. The label waits for the next firmware
    change (§9).
- **Board 1 now:** 1.2 (`Nov 10 2026 09:47:13`) in partition A, bought. B holds the test
  image, never bought, which the boot ROM never launches. r5 is gone from B, so there is no
  bought fallback. The next USB load, or the next update from the Pi, goes into B.
- **§8.9 PASS on board 1.** Every Phase 8 procedure has now passed on board 1. Two exit
  criteria remain:
  - the shutdown holdoff, measured (≤ 2.5 s) but not retuned;
  - §8.7 on any other board that will carry a Pi.

### 2026-11-10 - Image 1.3: the 10 s holdoff and two label fixes; first-seating step 5 cannot run

- **The owner's decision:** `PI_SHUTDOWN_MIN_HOLDOFF_MS` 15 s → **10 s** (§8). It is ~4× the
  measured ≤ 2.5 s, against the exit criteria's ~2×.
  - The holdoff is a floor only. After it, the latch still waits for the Pi to read down, up
    to 60 s.
  - So a slower shutdown (a busier Pi) is still covered by RPI5_ON. The holdoff only bounds
    how early a glitch on a sense line could cut the power.
- **Image 1.3** carries the holdoff and the two label fixes queued in §9 "for the next firmware
  change":
  - `src/board.h`: `PI_SHUTDOWN_MIN_HOLDOFF_MS 10000`, and its comment.
  - `src/main.c`, `boot_reason()`: a FLASH_UPDATE boot type (`rom_get_last_boot_type()`) is
    tested before `watchdog_caused_reboot()`. It gets `TELE_BOOT_FLASH_UPDATE` (new, value 3,
    in `telemetry.h`), named `flash update boot (a UF2 load, or an update from the Pi)`.
  - `TELE_BOOT_SOFTWARE`'s label now reads `software reboot ('reset', a boot ROM reboot, or
    a trial's rollback)`.
  - `src/safe_state.c`: the released J8.37 reads `released (latch open or no Pi 3V3: nothing
    driven into J8.37)`.
  - `CMakeLists.txt`: version 1.3. `.gitignore`: `build_v*/` and `build_hang_v*/`.
  - Comments only: `power_fsm.c` and `tests/power_fsm_test.c` ("not 15 s later" → "not a
    holdoff later").
- **Tests:**
  - `tests/telemetry_test.c`: `boot_reasons` adds a flash update boot (boots +1, watchdog
    count +0, the reason word 3); `names` covers the new label.
  - Host 228/228 PASS. The case count is unchanged because both tests were extended, not
    added. The `power_fsm` tests use the constant by name, so they exercise 10 s.
  - Pi tool 23/23 and the shutdown hook 21/21 (WSL) PASS.
  - The Pi tool's own `check_image()`: version 1.3, try-before-you-buy.
- **`tools/p8_cycles.ps1`:** a `-HoldoffSeconds` parameter (default 10; 15 for an image before
  1.3). Tested on the scratchpad's fake board:
  - a 10 s board → PASS, latch at 10.12–10.14 s;
  - 9.5 s → STOP;
  - 5 s → STOP;
  - a 15 s board with `-HoldoffSeconds 15` → PASS.
- **Build:** `build_v1.3/pitrac.uf2`, Release (`picotool info -a`: `Release`, version 1.3,
  ARM Secure, TBYB), `Nov 10 2026 10:25:37`, 224872 text / 0 data / 422112 bss.
  - UF2 SHA256 `0258B97E15AF0E2ADE2F843BC61BF25823EADBC9E064F664E4678A73107C3533`.
  - `pitrac.bin`: 220780 B, SHA256
    `76762B40D5849DB79CC529A95F595E201AD3618489CB0FE5BD70DD1766971FD7`.
  - `build/` and `build_hang/` still hold r5.
- **Docs:** the holdoff and the labels are updated where they were quoted:
  - `BENCH.md` Phase 1b, `BENCH_P8_PI.md` (§8.0b Steps 3 and 5, §8.2, §8.5, §8.8, the exit
    criteria, and the new §8.9b that puts 1.3 on board 1);
  - `DEVELOPER_GUIDE.md`, `pi/README.md`, the hook script's comment, and HANDOFF §4.
  - The Pi's installed hook keeps its old comment until the next `install.sh`. Nothing else
    differs.
- **Review:** the owner's call, as for 1.2. The change is one constant on a host-tested path,
  plus labels.
- **Error of mine, first-seating step 5:** it cannot run on any image so far. Every strobe
  firing, dry or live, is refused while a Pi is present (`strobe_plan.c`, `common_checks()` and
  `strobe_live_may_continue()`: `a Pi is present -- Phase 6 runs with J8 empty`).
  - The step (written 2026-10-16) did not say so.
  - On 2026-11-10 I listed it as a decision against the rule "strobe dry with the Pi seated",
    without checking what the firmware allows.
  - `BENCH_P8_PI.md` now says it waits for Phase 7, the camera-triggered shot, which needs a
    deliberate firmware change to fire with a Pi present.
- **The owner's question about §8.7 "on any other board":** it means another physical board
  (2, 3, the reworked originals), once per board, and only for one that will carry a Pi. Board
  1 has passed it.

### 2026-11-10 - 6c complete on board 1: the session-cap check PASS, and Step 6

- **With the Pi still seated (owner):** `strobe gate 0`, then `strobe live on confirm` →
  `REFUSED: a Pi is present -- Phase 6 runs with J8 empty. Live mode NOT armed; nothing
  changed.` This is the interlock, seen with a real Pi.
- **The Pi off the board** (the USB port dropped and came back): `on`, `strobe gate 0`,
  `strobe live on confirm` → `LIVE: ARMED. Gate 0; ... Watchdog ARMED (1 s).`
- **`strobe cal 9`:**
  - Nothing above ~72 mA up to level 200 (19.53 %). The first current was at 220 (0.147 A).
    Then 5-level steps up to 440 (42.97 %, 9.060 A).
  - Confirmations at 439: 9.013, 9.016 and 9.012 A. `SOLVED: level 439 (42.87 %, TP3 nominal
    4.24 V)`, mean 9.013 A (+0.1 %), PASS.
  - On 2026-10-16 the cal solved the same level 439, and 440 read 9.071 A; today 9.060 A
    (−0.1 %).
- **The session cap:** level 463 (45.21 %, TP3 4.48 V), as predicted (~463).
  - `strobe live` → `cap : level 463`, staircase proven to 440, budget 0.00 of 30 mC.
  - The last shot: 9.012 A plateau, 9.063 A peak, baseline 10.7 codes (0.064 A), `within
    limits`, brackets 14/14, `covered`.
- `strobe gate 46` (level 471) → `REFUSED: over this session's cap from 'strobe cal' ... Gate
  unchanged at level 439`. **Session-cap check PASS.**
- **Step 6:** `strobe live off` → `live OFF: gate 0, engine stopped, GPIO25 low. Watchdog now
  off.`; `off` → `requested shutdown`. No fault at any point.
- **Not in this log**, and none of it part of the cap check:
  - `id`, so the image is not shown: 1.2 or 1.3, whose strobe code is the same;
  - TP4 on the scope (Step 4's TP4 agreement passed on 2026-10-16);
  - the FLIR.
- **6c is complete on board 1, and with it Phase 6.** Every exit criterion in
  `BENCH_P6_STROBE.md` was already ticked.

### 2026-11-10 - The owner's plan: a new board, then Phase 7; the bring-up doc's flashing gap

- **The plan (owner):** bring up a new board with everything fitted, through
  `BRINGUP_NEW_BOARD.md`, then Phase 7.
- **A gap, fixed:** `BRINGUP_NEW_BOARD.md` §1 still said "firmware flashed (`SETUP.md`)".
  - It said nothing about the A/B partition table (1.1 on), and its hand-off listed nothing a
    board needs before it carries a Pi.
  - §1 now installs the table first, then the image, which goes into B. Its hand-off now lists
    the per-board Pi prerequisites: the strobe 6a–6d with the CR-23 capacitor, §8.0b, §8.7
    Steps 3–4, and the first seating. The sign-off table has a partition row.
- **What Phase 7 needs** (`BENCH_P5_P7_MIC_CAMERA.md`):
  - **Firmware, not written yet:** the camera handshake (PIO: trigger, wait for both strobe
    monitors, `t_cam`), FIRING, and `CAM_TIMEOUT`. 7a and 7b need no Pi and no camera.
  - **CR-09, for 7c:** J4 stays off a camera until the camera board's schematic shows whether
    it level-shifts the Mira220's 1.8 V I/O.
  - **7c fires the strobe with a Pi running.** The firmware refuses that by design, so it is a
    deliberate safety change. Its first bench test is the +5 V dip under a 9 A burst with the
    Pi up (first-seating step 5), before any camera shot.

### 2026-11-10 - `BRINGUP_NEW_BOARD.md` rewritten as a step-by-step protocol (owner request)

- **The format:** every step has State / Do / Pass / Stop if / Record, and 📝 marks what goes
  into the sign-off table.
  - The section numbers §0–§11 are unchanged, so the references from `REWORK_ORIGINAL_BOARDS.md`,
    `BENCH_P3_DETECT.md`, `NEXT_BOARD_REV.md` and here still hold.
  - The long explanation boxes moved to a Background appendix at the end: the PSU voltage, the
    current limit, cold joints, the boost frequency, and the settled-against-per-board tables.
- **New sections:**
  - §0 Step 0.2: which reworks a board needs.
  - §12: the strobe on this board, in order: the CR-23 capacitor, 6a/6b, the capacitor's dry
    checks, then 6c/6d.
  - §13: before a Pi: §8.0b, §8.7 Steps 3–4, the first seating, and the Pi on this board with
    1000 pings and one clean `off`.
  - The sign-off table gains rows for both, filled in for board 1.
- **Corrections, found while rewriting:**
  - **§8, the CR-15 sweep:** its commands stopped at 8 % duty, while its pass criterion runs
    "all the way to 25 %" and is judged at 12 % and 25 % (`BENCH_P3_DETECT.md` §3.3 says the
    same). The sweep now steps to 12 % and 25 % with `beam ramp`.
  - **§6, the U9 clamp:** the step said "scope TP5 and note the clamp width" at the 104 kHz
    carrier. There the 2.4 µs high phase never reaches the ~123 µs clamp, so it cannot show.
    It now uses `beam clamp` (1 kHz / 50 %, `BENCH_P2_BEAM.md` Q1), checks `TOP=49999,
    clkdiv=3`, and restores the beam with `beam duty 2` before `beam freq 104166`.
  - **§4:** trim `adc5vcal` on every board rather than only "if it is off". The expected scale
    is ~1.069: board 1 has 1.0694 and the COM10 board 1.0688, both trimmed since the
    2026-10-15 change to the trim. Boards 2 and 3's 1.062 used the older reading, ~0.6 %
    higher. Board 1's sign-off cell now shows both.
  - **§9, `cal model`:** the residual "under ~0.1°" conflicted with board 3's accepted
    0.23–0.26°. The pass is now `PURE DELAY` with all five points, and a residual close to
    the earlier boards.
  - **§6, thermals:** a stop added. Hotter than board 2's 98 °C at 10 min, the hottest
    accepted so far, means checking the heatsink and the airflow first.
  - **§13 Step 13.2:** §8.7 Step 4 must copy the same image as the first load. With 1.3 in B,
    an older image in A would erase B at its buy, since the newer one would otherwise win.
- **`BENCH_P2_BEAM.md` 2c, a stale warning:** it said the 35 % duty ceiling lived only in
  `beam duty`, so that `beam freq` after `beam clamp` gave 50 % at 104 kHz with "nothing in
  hardware or firmware" to stop it.
  - Since commit 2ec1c0e, `beam_configure()` enforces the ceiling on the effective duty for
    every path (`beam.h`, "DUTY CEILING"; `beam.c`).
  - Corrected. `beam duty 2` first is still right: 35 % is above CR-12's 25 %.

### 2026-11-10 - Phase 7 firmware: the camera handshake in PIO, and a dry camera-gated burst (image 1.4)

- **The owner's request:** "If there is fw to be built for phase 7, can you go ahead and do that
  now?" Built to `BENCH_P5_P7_MIC_CAMERA.md` 7a/7b and `ARCHITECTURE.md` A3. Not on a board.
- **What it does.** One handshake, all of it in PIO1 (`src/cam_handshake.pio`, 30 instructions):
  - It raises D_Cam_Trigger (GPIO10), then polls both Cam_Strobe inputs (GPIO8/9) every 4 cycles
    (26.7 ns). The poll loop is 4 cycles on every path through it.
  - It FIREs at **max(first delay, t_cam)**, the .md's `delay_us(max(0, first_delay_us -
    t_cam))`. The first delay counts down while it polls. Strobes seen inside it: the rest is
    waited out (FIRE at 4D + 12 cycles). Seen after it: FIRE 11 cycles later. The delay
    clamps to zero; nothing can underflow.
  - **FIRE is `irq prev set 4`**: flag 4 of PIO0. A camera-gated burst starts on
    `src/strobe_gate.pio`, one `wait 1 irq 4` loaded in the word just below the burst program.
    So the burst starts with no CPU in between, 4-5 us after FIRE (the burst engine's 1 MHz tick).
  - The trigger is held for the burst plus 50 us, then dropped.
  - **Timeout in PIO:** no strobes by the timeout (default 5000 us), and the trigger drops. That
    path has no `irq`, so the gate cannot open. The CPU reads the strobe engine's PC still on
    the gate (the proof nothing fired), stops it, and latches `CAM_TIMEOUT`.
- **CLI:** `cam` (the handshake engine read back, the gate, the last shot); `cam shot [first_us]
  [m/s] [timeout_us]`; `cam trig [first_us] [timeout_us]` (the handshake alone). The test shot
  starts at the handshake: no beam, no detector. The burst is the S15 schedule at the speed
  given (default 70 m/s: 10 x 14 us, 610 us apart).
- **Design decisions:**
  - **The camera is on PIO1 SM0, not PIO0 SM1** as `ARCHITECTURE.md` had planned. The program
    is 30 instructions and PIO0 had 20 free. RP2350's `irq prev` reaches PIO0 from PIO1, and
    `pio_alloc_init()` asserts that relation. The planned I2S mic moves to PIO0 SM1, which has
    19 instructions free.
  - **The strobe burst program is unchanged**, and loaded where it always was (offset 20, now
    explicitly; the gate at 19). A burst that is not camera-gated starts at 20, as before.
  - **Dry only.** A camera-gated burst is refused with live mode armed (new refusal
    `STROBE_REFUSE_GATED_LIVE`). It is admitted by the dry rule (gate DAC provably zero), which
    also refuses it with a Pi present, the rail down or a fault latched.
  - **The trigger is driven only with the rail up**, and drops on every route to rail-down:
    `camera_safe_off()` sits next to `strobe_safe_off()` in the power FSM, at the start of a
    shutdown and before the latch opens. A camera on J4 is powered from the Pi, and a trigger
    into an unpowered camera back-powers its 1.8 V I/O (CR-09). New host test
    `power.camera_teardown`.
  - **Refused before anything is driven** if GPIO10 is already high (`gpio 10 1` left over), or if
    a strobe input reads high before the trigger (a camera already exposing, a generator idling
    high, or a wiring fault: the handshake would complete at once).
  - **`shot.c`** (audit SHOT-01/02, "before Phase 7"): every state that waits has a deadline, and
    an engine that misbehaves ends the shot with `FAULT_INTERNAL`. A detected pass now ends
    `SHOT_END_NOT_IMPLEMENTED` after ANALYSING, instead of falling through CAM_WAIT and FIRING
    claiming `cam_ok` with 0 pulses. Nothing calls `shot_arm()` yet.
- **Tests: 246/246 host** (228 + 18).
  - `camplan.*` (4): the request limits, the counts to within half a loop, the word decoding.
  - `campio.*` (13): `tests/pio_sim.c` is a cycle-counting model of one PIO state machine.
    - It runs **pioasm's own output** for `cam_handshake.pio`, `strobe_gate.pio` and
      `strobe_burst.pio`, generated by the test build from the same source files.
    - Cases: the 7a loopback, the 7b delays (50 / 150 / 300 us), and a cycle-by-cycle sweep across
      the first-delay boundary (FIRE at max(first, t_cam), one phase flip, no underflow). Also:
      staggered and never-simultaneous strobes, the 7a timeout, every small D/T for the exact
      constants, a zero first delay, and back-to-back handshakes.
    - With the strobe engine at 1 MHz: the first edge 4.0-5.0 us after FIRE at every clock phase,
      the burst unchanged, and on a timeout the gate never left, the schedule still queued, and
      no GPIO25 edge.
  - `power.camera_teardown` (1).
  - **20/20 deliberate mutations caught**: every loop's delay, the FIRE target block, the late
    path, the phase flag, the strobe-1 test, the timeout word, each cycle constant in
    `cam_plan.h`, both range checks, the timeout-vs-first-delay rule, the gate's flag, and the
    model's synchroniser.
  - All passed first time. The mutations are the evidence that the tests can fail.
- **Not host-testable** (SDK code): `camera.c`, the gated path in `strobe.c`, `shot.c`, `cli.c`.
  The bench steps 7a/7b exercise them, and `cam` reads the hardware back (A8). Two hardware
  assumptions are checked at run time, not assumed:
  - a stalled `wait` keeps the state machine's PC on the gate (the arm refuses, `NO_GATE`,
    nothing fired, if it is not holding just after the enable);
  - `irq prev` reaches PIO0 (if the camera finishes its hold with the gate still shut, the shot
    ends `ENGINE` with `FAULT_INTERNAL`).
- **A defect of mine, found in self-review before the build was final:** `prepare()` encodes a
  burst into `s_words` before the busy check, and `s_words` is what a running burst's feed DMA
  reads.
  - Unreachable before Phase 7: nothing could ask for a burst while one ran.
  - A camera-gated burst plays while the CLI is free. A `strobe pulse` typed during one would
    have rewritten the schedule under it.
  - Now `strobe_run()` and the gated arm refuse busy (`STROBE_REFUSE_BUSY`) before
    `prepare()`. The fix is in the build below.
- **Build** `build_v1.4/` (version 1.4, try-before-you-buy; `build/` still holds r5):
  - Release `Nov 10 2026 13:46:40`, 240896 text / 0 data / 422676 bss.
  - UF2 SHA256 `96563EF9835E987CBD9E76294119FD967E7795D480E2EF5EE98621B448B54134`.
  - `pitrac.bin` 236804 B, SHA256 `3C8946863C3FD7F5FF60EB2E787978A9D4F101A755A981E726F556B4F03BCB51`.
  - `picotool info -a`: Release, 1.4, ARM Secure, TBYB. The Pi tool's `check_image()` accepts it.
  - The changed files compile clean with `-Wall -Wextra`. The retained-RAM contract check passed
    (the update note and telemetry mirror did not move).
  - Rebuilt once after a comment-only change to `strobe_plan.h`, so the image matches the tree:
    the binary differs from the first 1.4 build (`13:32:29`, never handed out) only in the stamp.
- **Review: required before it goes on a board** (`HANDOFF.md` §5 rule 8): `strobe.c` changed
  (the gated path, the busy guard, the fixed load offsets), and a new engine drives a pin into
  J4. The brief is `captures/review_2026-11-10/REVIEW_BRIEF.md`.
- **Docs:**
  - `BENCH_P5_P7_MIC_CAMERA.md`: status, the image box, 7a/7b rewritten as steps with expected
    output, and a 7c note.
  - `ARCHITECTURE.md`: A3, the PIO allocation, A9.
  - `DEVELOPER_GUIDE.md`; HANDOFF §4; this file; `BENCH_P8_PI.md` (the Phase 7 mention);
    `BENCH_P6_STROBE.md` (the first-pulse delay); `pio_alloc.h`; the `id` and `help` text.
- **Not done, deliberately** (§9):
  - a shot from a detected pass;
  - live current from the shot path;
  - the strobe with a Pi present;
  - the first delay from the geometry;
  - a 7b signal source;
  - the Mira220's trigger semantics (the hold).


### 2026-11-10 - Phase 7 image 1.4 independent review NOT PASSED (H7-F1/F2/F3)

- **Do not flash 1.4.** Report: `captures/review_2026-11-10/INDEPENDENT_REVIEW.md`.
  The independent reviewer did not author the change. Review covered the supplied
  patch, current source and surrounding power/service/CLI paths, PIO model semantics,
  and 7a/7b's expected output. No production source or firmware bytes changed.
- **H7-F1:** `cam_handshake.pio` samples strobe 0 and strobe 1 on different cycles.
  A falling strobe 0 and rising strobe 1 between the reads are accepted despite no
  overlap. The new waveform in `independent_probes/nonoverlap_probe.c`, running the
  actual assembled program in the existing rig, gets FIRE at 200 us. The ambiguity
  is one 150 MHz cycle (6.67 ns). A single coherent sample of both pins is needed.
- **H7-F2:** `strobe_gated_cancel()` treats an already-finished abort with no IRQ0
  as proof the gate never opened. The new probe compiles the whole unchanged
  `strobe.c`: PC 24 / GPIO25 already high, then safe-off, then cancel returns
  `gate_held=true`. The same error follows open-gate timeout/fault polling. Preserve
  hardware evidence before teardown and report unknown honestly; also check the
  proof before certifying the CAM_TIMEOUT no-firing result in `shot.c`.
- **H7-F3 (existing bypass, not a new regression):** STANDBY `gpio 10 1` drives the
  trigger high without a rail guard. The exact CLI functions reproduce this on
  the host. Close the raw path to make the new global trigger safety claim true.
- **Correction to the preceding entry:** its claim that the CLI is free during
  `cam shot` was wrong. `run_test_shot()` waits and yields; service does not dispatch
  another typed CLI command. The early busy guard remains useful API protection.
  The earlier global rail-guard claim also overlooked the raw GPIO path above.
- **Independent validation:** 246/246 Release host tests PASS; all three added
  defect demonstrations reproduce. Existing 20-mutation record inspected, not
  rerun. The patch reverse-check passes against the source. The author's rebuilt
  1.3 binary-equivalence proof was not independently rebuilt during this review.
  The image stamp, size, Release/version/TBYB attributes and both full hashes
  match the preceding entry. Firmware was not rebuilt.
- **Next:** fixes with host regressions and a deliberate mutation per fix, a newly
  identified build, then a fresh confirm review before 7a/7b. The existing image
  1.4 identity and the pending bench procedure remain on hold. No board state was
  inferred or changed, and nothing was committed.

### 2026-11-10 - Image 1.5: the review's three findings fixed (H7-F1/F2/F3)

- **The owner's request:** fix the review's findings
  (`captures/review_2026-11-10/INDEPENDENT_REVIEW.md`). All three were reproduced, and all three
  were right.
- **H7-F1, both strobes from one sample** (`cam_handshake.pio`):
  - Each poll is now `mov isr, ~pins` (IN_BASE GPIO8, IN_COUNT 2), `in null, 30` (IN shift
    left), `mov y, isr`, then `jmp !y, both`. Y is zero exactly when both strobes read high in
    that one clock. The result is the same whether the masking comes before or after the
    inversion.
  - The poll loop is now 5 cycles (33.3 ns) on its one path, and the delay and hold loops
    match. The late window has one entry path, so its ±1 cycle is gone. 29 instructions.
  - Constants re-derived and proven against the program: FIRE at 5D + 14 (200 µs →
    199.993 µs, the nearest the loop makes), late FIRE 13 after the sample, hold release
    + 5H + 9, timeout 5D + 5T + 18.
  - Regressions (`cam_pio_test.c`):
    - `nonoverlap_handover`: the review's waveform, plus hand-overs at 10 consecutive
      cycles, both orders, both windows;
    - `coherent_property`: 4000 seeded waveforms against "FIRE exactly when one sample saw
      both" (1077 fired, 2923 timed out).
  - Restoring 1.4's split-sample program, with its constants and rig, fails both.
- **H7-F2, the gate as the stopped engine shows it** (`strobe.c`, `shot.c`):
  - `gated_stop_witness()` takes GPIO25 to SIO low, disables the state machine (the PC freezes),
    and only then reads the PC and IRQ0. PC on the gate and no IRQ0 → `STROBE_GATE_CLOSED`;
    anything else → OPENED. The burst program never returns to the gate.
  - It runs before every gated teardown (`strobe_safe_off()`, every poll outcome, cancel, the
    arm's NO_GATE path). The result is stored in the new `strobe_result_t.gate`.
  - `strobe_gated_cancel()` returns that record, never an inference: UNKNOWN if nothing ran.
  - `shot.c` changes:
    - it certifies `CAM_TIMEOUT` only with the gate CLOSED; a timeout with the gate OPENED or
      UNKNOWN is `ENGINE` + `FAULT_INTERNAL`;
    - a refusal with the gate seen OPENED is an engine fault, not "nothing driven";
    - the shot copies the gate on every end.
  - The CLI prints the record as it is, UNKNOWN included.
- **H7-F3, the manual trigger** (`camera.c`, `cli.c`):
  - `camera_manual_trigger()`: a low is always admitted (`camera_safe_off()`). A high needs the
    rail up, no fault, and no handshake running (and, from the CLI, no shot).
  - `gpio 10 <0|1>` goes through it. GPIO10 is no longer raw-writable. A manual high is dropped
    by every rail-down, as before.
- **New host test `engine_test`** (17 cases):
  - the UNCHANGED `strobe.c`, `camera.c` and `shot.c` against a register model of the SDK
    (`tests/sdk_standin/`);
  - the `gpio` command copied verbatim out of `cli.c` by `tests/extract_cli_gpio.py` at build
    time (needs Python).
  - It covers aborts before FIRE, during the first pulse, between pulses and after
    completion. It also covers cancel after a poll, repeated cancel, the read order (SM
    disabled, GPIO25 SIO low), and IRQ0 with the PC on the gate.
  - On the shot side: a certified timeout and a contradicted one, the review's power-abort
    scenario (OPENED) and the same abort before FIRE (CLOSED), a normal fire, and the
    review's `gpio 10 1` reproduction through the real command.
- **Found by the new test:** on a normal FIRED shot, `shot.c` left the shot's `gate` field
  UNKNOWN while the strobe record said OPENED. `end_shot()` now copies it on every path.
- **Host: 265/265** (246 + 2 PIO + 17 engine), MSVC `/W4 /WX`.
- **Mutations: 34/34 caught**, each by a named test (`mutations_r2.md`). The first full run
  caught 31:
  - the 1.4 restore failed at build (camera.c's size assert), so it now runs PIO-only;
  - two edits matched comments as well, and are now matched on the instruction line;
  - "IRQ0 ignored by the read-back" survived. The term is kept as conservative, and a test
    was added for it.
  - All four, rerun, are caught.
- **Version 1.5, not 1.4 again:**
  - 1.4 never left this PC. But `build_v1.4/` was reused while compiling the fixes, so it no
    longer held the reviewed bytes and held an unreviewed image labelled 1.4.
  - It is removed, so nothing flashable labelled 1.4 exists. The reviewed 1.4's identity
    stays recorded above.
- **Build** `build_v1.5/`:
  - Release `Nov 10 2026 16:48:47`, version 1.5, ARM Secure, try-before-you-buy;
    241824 text / 0 data / 422684 bss.
  - UF2 SHA256 `F82A6562C5F661D95197A2E582E54B11E9311DC5DB6196F49E95DC3AF85374B7`.
  - `pitrac.bin` 237732 B, SHA256 `6ABFDBE4C28965DC8BBAC48B73FBD4945FC6AD769903253E4E9CF20B1716A9AA`.
  - Changed files clean with `-Wall -Wextra`. The Pi tool's `check_image()` accepts it.
- **Errors of mine, logged:**
  - **A false claim:** the 1.4 entry above said "a camera-gated burst plays while the CLI is
    free" and that a typed `strobe pulse` would rewrite its schedule. The CLI is blocked in
    `run_test_shot()` for the whole shot, and `pitrac_service()` never dispatches a typed
    command. The scenario I described could not happen. The busy guard is kept, as protection
    of the API. Caught by the reviewer.
  - **The safety claim was wider than the code.** "The trigger is never driven with the rail
    down" was written while the old raw `gpio 10 1` path still did exactly that (H7-F3). I did
    not check the existing CLI paths to the pin before stating it.
  - **Two defects the self-review missed** (F1, F2): both were in timing or state at the edge of
    the tests I wrote. The new property test and the engine model cover that edge.
  - The heredoc trap (HANDOFF §7) bit twice while editing: `\n` collapsed into a real newline.
    Edits are now made from script files.
- **Docs:**
  - `BENCH_P5_P7_MIC_CAMERA.md`: the image box, and the new expected values (D 5997, FIRE
    199.993 µs, t_cam 0.040 µs, releases 5764.987 / 999.987 / 1199.987 µs, offset 3, the gate
    line). 7a Step 4 adds `gpio 10 1` with the rail down.
  - `ARCHITECTURE.md` A3, `DEVELOPER_GUIDE.md`, HANDOFF §4, this file.
- **Next:** a confirm review of 1.5: `captures/review_2026-11-10/REVIEW_BRIEF_r2.md`, with the
  patch `phase7_r2_2026-11-10.patch` (1.4 → 1.5).


### 2026-11-10 - Image 1.5 independent confirm review PASS: H7-F1/F2/F3 closed

- Report: `captures/review_2026-11-10/INDEPENDENT_REVIEW_r2.md`, by the reviewer
  who found H7-F1/F2/F3. No new blocking defect. The pass is for the exact 1.5
  identity in the preceding build entry, independently checked and unchanged.
- **H7-F1 confirmed:** one coherent input read, the left shift/inversion and
  IN_BASE/IN_COUNT correct; original non-overlap reproduction now times out.
  Independently counted the 5-cycle timing constants and checked the 7a/7b text.
- **H7-F2 confirmed:** GPIO25 to SIO low, SM disabled, then PC/IRQ witness; the
  burst cannot return to gate 19. Safe-off, poll, cancellation and NO_GATE paths
  preserve the witness; repeated cancellation does not infer a new result.
  Contradicted camera timeouts fault, and FIRED copies the same gate record.
- **H7-F3 confirmed:** the actual GPIO command goes through the camera helper;
  rail-down, fault and ownership refusals plus rail-up/manual-low cases pass.
- **Independent validation:** normal MSVC Release suite 265/265 PASS
  (`confirm_r2_ctest.log` in the review folder). Four independent mutations in an
  isolated source copy were caught by named tests after successful builds:
  - restore 1.4's whole split-sample program/constants/mapping: both new PIO regressions fail;
  - restore outcome-based cancellation inference: abort/cancel regressions fail;
  - read the witness before disabling: stopped-state/order regressions fail;
  - restore raw GPIO10 writes: rail-down/fault regressions fail.
  Restored copy: 36/36 camera-plan/PIO/engine tests PASS, 90 source/test files
  byte-identical to the working tree. Script and log: `confirm_r2/` in the review
  folder. Author's full 34-mutation record inspected, not all independently rerun.
- **Nonblocking comment note:** `camera.h` says a manual high ends on a handshake
  refusal, but refusal preserves it; the bench correctly follows with `gpio 10 0`.
  Source left unchanged. Stale documentation totals corrected from 264/16 to 265/17.
- **Next: reflash first, exact 1.5**, then 7a/7b with no Pi, no camera, J3 off,
  dry strobe, beam off, per `BENCH_P5_P7_MIC_CAMERA.md`. The owner runs hardware
  acceptance; no firmware rebuild, flash, board operation or commit in this review.

### 2026-11-11 - Board 4: bring-up started (owner); image 1.5 loaded flat

- **The board:** Board 4, one of the three additional original boards
  (`REWORK_ORIGINAL_BOARDS.md`), driven by `BRINGUP_NEW_BOARD.md`. The owner reports its TIA as
  the **original-board target** (162 kΩ ∥ R80; 2 pF stacked on each 1 pF of C68/C70:
  120.475 kΩ / 1.500 pF, calculated, not yet measured on any board). Step 0.1 and
  `BENCH_REWORK_CHECK.md` §1–§2 have not been reported yet.
- **UID `8fabac4bb8add852`.** USB only, COM9. The owner loaded image 1.5 (`build_v1.5`,
  `Nov 10 2026 16:48:47`) by `bootsel` and a UF2 copy, **flat, with no partition table**:
  - `id` → `version  : 1.5  (no partition table: flat at 0x000000)`, `sysclk 150000000 Hz`,
    `pitrac_ltb_v1 (RP2354B, 48 GPIO, 2MB internal flash)`;
  - `upd` → `ON TRIAL: bought itself at the end of init`, flat from 0x0, `staged : idle`.
- **Step 3.1 (E9), on USB: PASS.** GPIO24, 0, 8 and 9 all read 0. `D_COMPARATOR(46)` reads 1,
  as expected with the rail down (U15 unpowered, R103 pulls up). GPIO27 0, every output 0.
- **A deviation from the procedures, recommended to be undone:** Step 1.3 and
  `BENCH_REWORK_CHECK.md` §3 both call for **r5** on this board: board 1 has not yet passed 1.3
  (`BENCH_P8_PI.md` §8.9b), and 1.5 has not run on any board. Step 1.3 also puts the partition
  table in first. The recommended fix is Step 1.3 as written: the table, then r5 into B.
  `build\pitrac.uf2` is still r5 (SHA256 `AC5F4493…ADC272ACF7`, checked 2026-11-11).
  - **Checked for that conversion:** 1.5's `.bin` has no block start marker in 0x2000–0x2fff
    (its markers are at 0x138, 0xef50 and 0x3a090). So after the table goes in, partition A holds
    no image, as `BENCH_P8_PI.md` §8.7 Step 2.5 established for r5, and `upd` should read the
    partner as `UNKNOWN`.
- **A conflict between documents, not yet reconciled:** the PSU current limit is **2 A** in
  `BRINGUP_NEW_BOARD.md` (Step 1.1, Background C) and **3 A** in `REWORK_ORIGINAL_BOARDS.md`
  §3/§7 and `BENCH_REWORK_CHECK.md`. For §2–§5 of the bring-up, use 2 A, the value boards 1–3
  were brought up at.
- **The owner's decision (later, 2026-11-11): Board 4 is the board for 7a**, not board 1.
- **Step 1.3, first attempt: the table was not installed.** The owner ran
  `picotool load build\pitrac.uf2` (the image, not `build\pitrac_pt.uf2`), then `reboot -u`.
  `partition info` → `there is no partition table`. `info -a`:
  - **r5, flat, over 1.5:** version 1.1, Release, ARM Secure. Binary end `0x10035dd4`
    (220628 B) and blocks at 0x138 and 0x35dc0, both identical to `build\pitrac.uf2`'s.
  - `build date: Oct 19 2026`: that is the binary-info date inside r5's own file (picotool reads
    the same from the file). The CLI's `built` stamp, `Oct 20 2026 14:59:52`, is the one the
    docs use.
  - `tbyb: not bought`: a `picotool load` followed by `reboot -u` is not a flash-update boot, so
    r5 has not yet run.
  - **Silicon A4**, QFN80, chipid `0x8fabac4bb8add852` (= the UID).
- **Next for Board 4:**
  - finish Step 1.3: `pitrac_pt.uf2`, `reboot -u`, `partition info`, then r5 **dragged** onto the
    drive, which goes into B, on trial;
  - then §0 (unpowered), Steps 1.1–1.2, §2–§5, and §12 Step 12.2 (6a/6b dry, J3
    disconnected);
  - then 1.5 by `BENCH_P5_P7_MIC_CAMERA.md`, "Before 7a and 7b"; then 7a.
  - §6–§11 are not needed for 7a. The sign-off will say "fast path: §6–§11 not done".
- **Step 1.3, second attempt: the table is in.** `picotool load build\pitrac_pt.uf2`,
  `reboot -u`, then `partition info`: `0(A) 00002000->00100000 "PiTrac A"` and
  `1(B w/ 0) 00100000->001fe000 "PiTrac B"`, both `uf2 { 'rp2350-arm-s' }`. PASS.
- **This PC blocks writes to the `RP2350` USB drive** (owner, 2026-11-11: a policy restriction).
  So the UF2 drag cannot be used here. picotool still works, because it uses the PICOBOOT
  interface, not the drive.
  - The replacement is `picotool load -p 1 -v -x build\pitrac.uf2`: into partition B, verified,
    then (`-x`) a flash-update boot, which is what lets a try-before-you-buy image run on trial.
  - **Confirmed:** `id` → `version  : 1.1  (partition B at 0x100000)`, `built    : Oct 20 2026
    14:59:52`. `upd` → `flash update boot, ON TRIAL: bought itself at the end of init`, `this
    image in partition B`, partner `UNKNOWN`, `staged : idle`. `pins`: E9 still 0 on
    24/0/8/9. **Steps 1.3–1.4 PASS.**
  - It is now the alternative to the drag in `HANDOFF.md` §4, `BRINGUP_NEW_BOARD.md` Step 1.3,
    and `BENCH_P5_P7_MIC_CAMERA.md` "Before 7a and 7b" (Board 4: `-p 0` for 1.5).
- **§4, the +5 V scale: PASS, with a note.**
  - `adc5v` 5.208 V on the PSU before the trim, then `adc5vcal 5.206` (the DMM at J1) gave
    scale **1.0781** (raw 4.829 V).
  - `cfg save` → slot A, seq 9; `adc5v` 5.206 V, which matches the DMM; `cfg` → `source : slot A`.
  - 1.0781 is 0.85 % above board 1's 1.0694 and the COM10 board's 1.0688: this chip's ADC.
  - **Board 4 already carried a scale near 1.078 before today:** the default, 1.063
    (`V5IN_SCALE_DEFAULT`), would have read about 5.13 V, not 5.208. Seq 9 also means earlier
    saves. Asked the owner whether this board saw an earlier §4.
  - Safety: even if the DMM read 0.85 % high, USB's 4.85 V would read about 4.89 V, still
    0.16 V under the 5.05 V guard. Step 5.1 checks the guard directly.
- **The owner's decisions:**
  - **Skip §2 (the J2 smoke test).** Agreed, on condition that §2.1's readings are taken at the
    first `on` (Step 5.2) instead: PSU current watched as the latch closes; +5 V at J8.2; VIR at
    J3 pin 1's screw and at C26; TP2; TP6; the idle current. A short at `on` meets the 2 A limit
    and `FAULT_SUPPLY_LOST` (4.90 V for 500 ms), and with J2 it would meet only the limit.
    Step 2.2 (the boost frequency) is not done.
  - **§5 (corrected: the owner meant §5, not §6, then asked about both).** Agreed outcome:
    - **Steps 5.1 and 5.2 stay.** 5.1 takes one minute, and it is the direct check of the
      per-chip scale just set; this chip's is 0.85 % off the others. 5.2 is Board 4's first
      `on`, which 7a needs anyway, and it carries §2's readings.
    - **5.3–5.5 are deferred until before a Pi** (§13), as the protocol's strobe-only path
      allows. Their hazards are a Pi powered through a reset glitch, or fed through D8 (1 A),
      and no Pi goes on before then.
  - **§6 (beam): not needed for 7a**, with the beam off throughout. It is required before the
    beam is ever turned on on this board: 6.3 is the U9 hardware on-time clamp, and 6.4 the
    thermals. Step 6.5's read-only strobe checks are repeated in `BENCH_P6_STROBE.md` 6a Step 0
    (GPIO27 armed, the A7 line) and 6b Step 1 (the A7 independence).
- **Steps 5.1 and 5.2, the CLI half: PASS** (r5, slot A seq 9, scale 1.0781).
  - 5.1, USB only: `stat` STANDBY with 5V_IN 4.862 V; `adc5v` 4.862 V, latch INHIBITED; `on`
    → `FAULT` / `USB_POWER_ONLY`, 5V_IN 4.855 V, latch 0, railsready 0. With the PSU back on,
    5V_IN read 5.206 V, still in FAULT, as expected.
  - 5.2: `fault clear` → STANDBY; `on` → `BENCH_RUNNING` within 1.3 s, latch 1, railsready 1,
    5V_IN 5.204 V (5.206 V before the latch: no sag). Then `off`. `hist` counts the
    USB_POWER_ONLY as fault 1.
  - **Not in the paste:** the red LED's fast blink, and every DMM reading. §2's readings were
    moved here on condition that they are taken, so no rail on Board 4 has been measured yet.
- **The owner's decision: no current readings** (the PSU has no current readout at present).
  Agreed, with substitutes:
  - VIR 36 V at J3 pin 1's screw is the direct check of Background C's trap (the boost parked by
    the supply's limit). The current was only its second symptom.
  - A FLIR scan after ~5 minutes in `BENCH_RUNNING` stands in for "the current is high": a part
    drawing extra current shows as a hot spot.
  - The standby and idle currents stay "not measured". Take them before §13, with a supply that
    shows current or a DMM in series on its 10 A range. Not the mA range: its shunt would drop
    the 5.20 V supply below the 5.05 V latch guard.
- **I was wrong (2026-11-11): I gave live probe points that break `HANDOFF.md` §5 rule 8.** My
  §2/§5 instructions named +5 V at J8.2, VIR at C26 and TP6 without saying what each sits next
  to. Parsed from the PCB (pad edge to other-net pad edge):
  - **J8.2: 0.84 mm from J8.1 (+3V3_Pi5)** and 1.19 mm from J8.3 (GPIO2). Not a hand-held point
    on a powered board.
  - **TP6: 1.14 mm from C73 pad 1 (U11B's inverting input).** Not a hand-held point. TP9
    (0.78 mm from C79 pad 1) and TP10 (0.79 mm from R91 pad 1) are not either.
  - Fine for a tip:
    - TP7 (TIA_Out): 2.12 mm from R95 pad 2;
    - the J3 pin 1 screw: 3.2 mm from pin 2;
    - TP2: 1.92 mm from R15 pad 1. R15 pad 1 is VIR, so a slip puts 36 V on +12 V;
    - C26 pad 1: 1.80 mm from its own GND pad. A slip shorts VIR's bulk capacitors.
  - **VIR at the J3 screw stands in for J8.2.** The boost's input (U1 pin 3) is the switched
    +5 V. With the boost idle, VIR sits at that rail less D2's drop. So VIR below 1 V means the
    rail is off, and 36 V means it is up.
  - C26 is needed only if the J3 screw reads low. VIR comes from C26, so 36 V at the screw
    means 36 V at C26.
  - Fixed in `BRINGUP_NEW_BOARD.md` §2.1, §5.1 and §5.2. The other docs that name these points
    are listed in §9.
- **Steps 5.1–5.2, the DMM half: PASS. With the CLI half, Steps 5.1 and 5.2 PASS.** Black lead
  on J1's − terminal, fitted unpowered.
  - 5.1, USB only: VIR at the J3 pin 1 screw **0.022 V** before `on` and **0.022 V** after it.
  - 5.2, latched: J3 pin 1 screw **35.94 V** (board 1: 36.0); TP2 **12.28 V** (board 1:
    12.31); TP7 **2.601 V** (+5VA/2 on 5.20 V = 2.60). C26 was not needed.
  - FLIR after ~5 min in `BENCH_RUNNING`: no issues (owner).
  - TP7 at 2.601 V with the rail up and the beam off is also §7.1's record.
- **The owner's new plan (later, 2026-11-11), superseding "Board 4 is the board for 7a":**
  - Board 4: set up the calibration fully (§6–§11), then **ship it to a collaborator**;
  - **Board 5**, a new bring-up, takes Phase 7.
  - Asked the owner, because it decides what else Board 4 needs before it ships:
    - will the collaborator seat a Pi? If so: Steps 5.3–5.5, the currents, §12, §13;
    - will they fire the strobe? If so: §12;
    - which image ships?
  - `detect path` and the detect threshold (`BENCH_P3_DETECT.md` §3.7) depend on the beam path
    in the final housing, so §6–§11 leave them unset.
- **Steps 5.3–5.5 by jumper leads on J7** (the owner has no front panel). From the PCB, J7 is
  a 1×6 2.54 mm header:
  - pins 1 and 5 are the switched +5 V, the same net as J8.2;
  - pin 3 is `PWR_TOGGLE` (R42 1 kΩ → GPIO14, C40 to GND);
  - pin 4 is GND; pins 2 and 6 are the LED cathodes.
  
  The method:
  - **the press:** leads on J7.3 and J7.4, fitted unpowered, with their free ends touched
    together;
  - **the rail:** the scope on a lead at J7.5, its ground on J5's GND.
  
  The owner offered J5's 3V3 and J1's 5 V. Neither is needed:
  - the button is a switch to GND, so a supply must never go on J7.3;
  - J1 is the unswitched input.
  
  Recommended: scope Step 5.4 on the rail alone, without GPIO15. GPIO15 is reachable only at
  R12 (0402) or Q2 (SOT-23), which takes a soldered lead. Peak-detect on the rail catches any
  glitch that reaches a Pi. If the owner agrees, this is a deviation from `BENCH.md` Test 4.
  Written into `BRINGUP_NEW_BOARD.md` §5.
- **Step 5.3: PASS** (owner): the J7.3–J7.4 press latched from STANDBY, and the next press
  dropped the rail.
- **Step 5.4: PASS on the rail** (the owner ran it rail-only, without GPIO15).
  - The capture: a Saleae Logic Pro 8 on J7.5, analog at 5 MS/s plus digital, triggered on the
    digital falling edge, 1 s before and 3 s after. `C:\Users\ATTAYEKP\Downloads\rpi_SW2`
    (`analog.csv`, `digital.csv`; not copied into the repo).
  - The digital threshold is ~1.0 V, so the trigger fired ~260 ms after the press. The fall
    starts at −250 ms.
  - The rail:
    - steady at **5.264 V** mean before the press (raw 5.197–5.334 V);
    - 4.0 V at −228 ms, 2.0 V at −138 ms, **1.0 V at +11 ms**;
    - 0.95 V at 100 ms, 0.77 V at 1.0 s, **0.586 V at 3.0 s**.
  - **No rise.** The largest raw rise above the running minimum after the trigger is 116 mV,
    inside the 137 mV raw peak-to-peak of the PSU-held rail before the press. 1 ms means wander
    ±30–40 mV both before and after.
  - The rail's capacitance cannot move 40 mV in a millisecond without milliamps from something,
    so the wander is the analyser, not the rail.
  - Digital: chatter at the threshold until 61 ms, then 0 to the end of the capture. Nothing
    climbed back above ~1 V.
  - **The capture covers the release only if SW2 was let go within ~3 s of the press.** Asked
    the owner to confirm.
- **I was wrong (2026-11-11): Step 5.4's pass criterion.** I said "+5 V falls to 0 V and never
  rises above 0.5 V for the rest of the 2 s capture". With no Pi, the unloaded rail drains
  slowly below ~1 V and sat at 0.59 V after 3.3 s. The right test is the shape: no rise after
  the fall begins, beyond the instrument's noise on the steady rail. `BRINGUP_NEW_BOARD.md`
  §5.3–5.4 now say so.
- **Step 5.5: PASS** (the latch drops as designed; owner to confirm `stat` showed `SUPPLY_LOST`).
  - The capture: J7.5 on the Saleae, analog at 5 MS/s plus digital, 2 s before and 1 s after
    the digital falling edge. `C:\Users\ATTAYEKP\Downloads\Bringup_5p5`; not copied into the
    repo.
  - The rail:
    - **5.253 V** before (1 ms means 5.242–5.277);
    - the PSU off at about −850 ms, and the rail past 4.90 V at −829 ms;
    - the **USB plateau at 4.81 V** (1 ms means 4.80–4.86 V);
    - it leaves the plateau at −250 ms. **The plateau lasted 579 ms.**
  - The fall then matched 5.4's: 4.0 V 19 ms after the knee, 1.0 V at +25 ms, 0.767 V at
    +1.0 s. The largest rise afterwards was 40 mV in 1 ms means, the analyser's wander.
  - **579 ms is where the design puts it.** `supply_lost()` reads +5V_IN every 100 ms
    (`V5_MONITOR_INTERVAL_MS`) and trips after 500 ms of continuous low. So the trip lands
    500–600 ms after the crossing, plus loop latency. `BRINGUP_NEW_BOARD.md` §5.5 now gives
    this window.
  - **The Saleae's analog reads ~50 mV high.** 5.253 V at J7.5, after Q3, cannot exceed the
    5.206 V the DMM read at J1. Taking the true 4.90 V crossing ~3 ms earlier, the plateau is
    ~582 ms.
- **§5 is complete on Board 4** (5.1–5.5). The standby and idle currents are still not
  measured; take them before §13.
- **§6 (beam sanity), owner, 2026-11-11.** TP5 was on the Saleae (analog at 50 MS/s plus
  digital), with its ground on board GND.
  - **6.1 PASS:** `beam` with the beam on: `PWM_EN 0xce0`, slices 7 and 11 `ENABLED ok`;
    funcsel GPIO31 = GPIO39 = 4; pad ISO 0 / 0; `ctr(car)` counting. With the beam off: `off
    ok`, SIO (5), counter frozen.
  - **6.2 PASS** (`C:\Users\ATTAYEKP\Downloads\Bringup_6p2`):
    - period **9.600 µs** over 270 periods (sd 0.3 ns, 9.598–9.602), so 104,167 Hz;
    - LOW width 0.228 µs at 2 % commanded (0.192 µs): the ~36 ns extra is switching delay;
    - TP5 3.40 V off.
  - **6.3 PASS** (`C:\Users\ATTAYEKP\Downloads\Bringup_6p3`):
    - `beam clamp` → `TOP=49999, clkdiv=3`, 1000 Hz;
    - TP5's LOW width **120.96 µs** over 118 pulses (120.83–121.08 µs, sd 0.05). That is
      inside the 109–136 µs tolerance band (board 1: 122.68) and well short of the 500 µs
      commanded, so U9 clamps;
    - the effective duty is 12.10 % (the firmware printed 12.2 % from its compiled 122 µs);
    - TP5 0.11 V on, 3.66 V off (board 1: −0.007 / 3.637 V);
    - the recovery ran in order: `beam duty 2`, `beam off`, `beam freq 104166`, and `beam`
      read `104166 Hz (TOP=1439)` at 2 %.
  - **Doc correction, BRINGUP Step 6.3:** it said the U9 width "gates nothing". It does gate
    something: `beam.c`'s 35 % effective-duty ceiling assumes the compiled
    `BEAM_ONESHOT_CLAMP_US` = 122 µs.
    - That matters only below ~2.9 kHz, where a 35 % high phase reaches the clamp.
    - A board whose clamp is longer than 122 µs (the tolerance allows 136) would under-read by
      clamp/122 there.
    - Board 4's 120.96 µs keeps the ceiling conservative.
    - Step 6.3 now says this.
  - **6.4 PASS** (FLIR; `beam ramp 25 500`, held at 25 %):

    | min | LED base | heatsink | base − sink |
    |---|---|---|---|
    | 5 | 88.2 °C | 63.4 °C | 24.8 K |
    | 10 | **91.5 °C** | 65.1 °C | 26.4 K |
    | 15 | 92.1 °C | 67.2 °C | 24.9 K |
    | 20 | **92.9 °C** | 68.1 °C | 24.8 K |

    - The 10-minute gate (≤ 98 °C) passes. Extended to 20 min for a true plateau: +0.8 °C over
      the last five minutes, under the 1 °C criterion.
    - The junction is ~109–118 °C (base + 6–9 K/W × 2.74 W), against 145 °C: 27–36 K of margin.
  - **I was wrong (2026-11-11): the base-to-heatsink gradient.** I told the owner Board 4's
    ~25 K was ~7 K above boards 1 and 2 (19.4 / 20.0 K, from the `BRINGUP_NEW_BOARD.md`
    sign-off note), and suggested re-seating the LED.
    - `NEXT_BOARD_REV.md` CR-12 measured board 1's base from the side, the method it calls
      correct: 8.7 K/W, so ~24 K at 25 % (2.74 W). Board 4 matches that.
    - The two board-1 figures disagree with each other, probably by where the FLIR read the
      base. So there is no evidence of a weaker interface on Board 4. Re-seating withdrawn.
    - The disagreement is in §9.
  - **6.5 PASS** (`strobe`, J3 disconnected, BENCH_RUNNING):
    - `GPIO27 : SIO output, driven 0, pad 0 -> U5 pulse limiter armed`;
    - `A7 : GPIO12 ready LED on SIO (on/off); slice 6 belongs to the gate DAC`;
    - gate DAC `level 0 of 1024 (0.00 %) hw compare 0 GPIO28 PWM`;
    - `engine : idle ... GPIO25 SIO, pad 0`;
    - `may fire : yes`, live OFF.
    
    `panel rdy 100` / `0` / `auto` were accepted. There is no front panel, so there was no LED
    to watch.
  - **§7.1 PASS** (beam off): `adc 2 256` → **3207 codes, 2.5844 V at the pin** (expect ≈ 3212,
    2.59 V). The DMM read **2.601 V on TP6, TP7, TP9 and TP10**: the four agree, which is the
    criterion. ADC2 reads 17 mV under TP7, through R82 and the ADC's own error.
    - The owner read all four test points. TP6, TP9 and TP10 are rule-8 points: under 1.5 mm
      from other copper, `HANDOFF.md` §5 rule 8. Asked whether they were hand-held tips, and if
      so whether the owner wants to relax rule 8 for those signal-only test points.
  - **§7.2 PASS** (`hpf test`, beam off, after cooling from 6.4): **`CONFIRMED: GPIO33=0 is
    TRACK`**.

    | level | rep 1 mean | rep 2 mean | rep 1 p-p | rep 2 p-p |
    |---|---|---|---|---|
    | GPIO33=0 | +4.2 mV | +4.1 mV | 0.8 mV | 4.0 mV |
    | GPIO33=1 | +18.6 mV | +19.7 mV | 7.3 mV | 8.1 mV |

    - The repeats agree to within 0.1 mV (TRACK) and 1.1 mV (HOLD, which keeps integrating),
      against ~15 mV between the levels. Separation 4.6×.
    - HOLD drift: 2.31 mV/s (mean displacement) and 1.75 mV/s (p-p), so **53 pA / 40 pA into
      C81** (board 2: ~52 pA, board 3: 50–61 pA, board 1: ~250 pA).
    - `cfg save` → slot B, seq 14. The halt telemetry's automatic saves account for seq 10–13.
      `cfg` → `hpf sel : TRACK = 0`.
  - **The owner asked whether `hpf test` is needed on future boards, and whether it can default
    to TRACK.**
    - **It already defaults to TRACK.** `detect.c` drives GPIO33 from the compiled
      `HPF_SEL_TRACK` (0) whatever the config says. `hpf_sel_track` in the config is only the
      "confirmed on this board" record; it changes the `cfg` and `hpf` messages and nothing
      else.
    - The polarity is settled: boards 1–4 and the datasheet. What the test still checks per
      board is U14/R96/C81 and the leakage, in 32 s.
    - Recommended: keep it, but run §7 before §6, while the board is still cold, so there is no
      cool-down. `BRINGUP_NEW_BOARD.md` Step 7.2 now says so.
    - If the owner drops it instead, the firmware behaves the same. Only the two warnings would
      need softening, which is text-only, with the next firmware change.
  - **§8 PASS: linear to 25 %** (`hpf track`, baffle fitted, `capture 0x04 400 500000` at each
    duty; ADC2 codes):

    | duty | min | max | swing | board 2 swing |
    |---|---|---|---|---|
    | 2 % | 2621 | 3257 | 636 | 169 |
    | 3 % | 2140 | 3279 | 1139 | — |
    | 4 % | 1827 | 3295 | 1468 | 535 |
    | 6 % | 1736 | 3334 | 1598 | — |
    | 8 % | 1529 | 3374 | 1845 | 1001 |
    | 12 % | 1557 | 3450 | 1893 | 1527 |
    | 25 % | 1809 | 3679 | 1870 | 1530 |

    - No sample at 4095 and none near 0. The swing plateaus after 12 % (1893 → 1870).
    - At 25 %: 416 codes of headroom at the top, 1809 at the bottom.
  - **I was wrong (2026-11-11): the predicted swing.** I told the owner to expect roughly a
    quarter of board 2's swing, because the TIA is reported at 120 kΩ against board 2's 470 kΩ.
    It is **1.2× board 2's**.
    - Settled swing ÷ Rf gives the photocurrent: board 2 1.233 V / 470 kΩ = 2.6 µA; Board 4
      1.507 V / 120.5 kΩ = **12.5 µA, ~4.8×**.
    - Either far more beam light reaches D12 past this baffle than on board 2, or the TIA is not
      the reported 120 kΩ. A stock 470 kΩ TIA with a slightly weaker baffle would also give
      ~1870.
    - **§9's chain delay decides it:** stock boards read 553–600 ns, board 3 (116 kΩ) 340 ns.
      The original-board target should land near board 3's. If it reads stock, re-check
      Step 0.1: R80's parallel part, and which pads the added Cf bridges.
    - Expect §9.2 to need a card over D12.
  - **The owner asked whether §8 can go straight to 25 %.** Not quite.
    - The deepest sample came mid-sweep (1529 at 8 %, 1557 at 12 %, 1809 at 25 %), while the
      top peaks at 25 %.
    - So the short form is **12 % and 25 %**, with the full sweep only if one clips.
    - Added to `BRINGUP_NEW_BOARD.md` Step 8.2.
  - **§9 PASS** (beam 104166 Hz / 25 %, HPF TRACK, no card over D12).

    | run | aimed at | warm | peak | saturated | h3/h1 | phase |
    |---|---|---|---|---|---|---|
    | 1 | white wall, ~1.2 m (4 ft) | **249 s of 300** | **105 %, ABOVE FULL SCALE** | 69 % | 0.215 | not committed |
    | 2 | nothing within ~3 m (10 ft) | yes | **9 %** | 0 % | 0.118 | 1342 (delay 547 ns) |
    | 3 | the wall, angled | yes | 78 % | 0 % | 0.102 | 1332 (480 ns) |
    | 4 | the wall, angled more | yes | **69 %** | 0 % | 0.104 | **1333 (487 ns)** |

    - **Run 4 passes Step 9.2:** peak 69 % (50–70), saturated 0 %, quad null 0.2 codes, h3/h1
      0.104 against 0.111 intrinsic (bar 0.161), warm.
    - Run 1 failed safe: not committed, and the firmware named the cause.
    - **`cal model` PASS**, all five points:
      - `theta(f) = 5.3964 + 4.684e-06 f − 4.868e-12 f²`;
      - **residual 0.23°** (board 3: 0.23–0.26°, board 2: 0.07–0.08°);
      - amp spread 4 % (limit 20 %);
      - **chain delay 482 ns at 80 kHz → 510 ns at 200 kHz, PURE DELAY** to within 1.01°.
    - **`cfg save` was refused with the beam on** (the guard in `config_store.c`). The owner
      ran `off`; `beam off` is enough, since `BENCH_RUNNING` is allowed. Then slot A, seq 15.
      `cfg`:
      - carrier 104166 Hz, phase 1333 ticks;
      - `phase mdl: fitted, pure delay (80000..200000 Hz)`;
      - `cal duty : 25.00 %`;
      - hpf TRACK = 0, adc5v 1.0781.
  - **Run 2 explains §8's large swing, most likely.**
    - With nothing within 10 ft, the beam's own leak past the baffle gives only 9 % of full
      scale (amplitude 440 codes). The wall at 4 ft gave over 10× that (4936, clipped).
    - §8 ran just before run 1, so if the board faced the same wall (asked the owner), §8's
      swing was mostly the wall's reflection, and board 2's figures are not comparable.
    - §8 still passes: no clipping with a wall in view is the harder test.
    - **The baffle is good.**
  - **I was wrong (2026-11-11): "§9's chain delay decides the TIA variant".** It cannot.
    - Calculated TIA lag at 104 kHz: stock 8.74° = 233 ns; target 6.75° = 180 ns; board 3
      4.30° = 115 ns. Stock and target differ by only **53 ns**.
    - The rest of the chain differs between boards by 95 ns or more: board 2 320–367 ns,
      board 3 225 ns.
    - Board 4's 487 ns leaves 307 ns for the rest if it has the target TIA, or 254 ns if
      stock. Both are inside that spread.
    - No measurement now contradicts the reported variant, and the §8 swing has another
      explanation.
    - Only a visual check (Step 0.1, unpowered) would settle it. An in-circuit DMM across R80
      would not: R78, R81 and R83 tie its nodes to the servo and the next stages.
  - **The owner wants Step 9.2's level-setting more foolproof for users.**
    - **Docs, done** (`BRINGUP_NEW_BOARD.md` §9.1, 9.2, 9.5):
      - start with nothing in front; under 30 %, aim at a plain wall or white card, starting
        ~2 m away, and move in;
      - the light's source does not move the phase (69 % and 78 % runs: 1 tick apart; 9 %:
        9 ticks, 2°, under 0.1 % of signal);
      - `beam off` before `cfg save`.
    - **Firmware proposals, for the owner to decide** (§9).
  - **§10 PASS** (aimed as in §9's run 4; `on`, then `beam on` straight at 25 % from cold,
    with no ramp or warm-up; neither changes these results).
    - **10.1:** at duty 0, `D_Comparator(46) = 1`; at duty 5 (level 51), `= 0`.
    - **10.2, LOW**, three runs:

      | flip duty | TP8 nominal | ADC5 |
      |---|---|---|
      | 0.16 % | 5.2 mV | 3.2 mV |
      | 0.08 % | 2.6 mV | 4.0 mV |
      | 0.16 % | 5.2 mV | 4.0 mV |

      These are DAC levels 1–2, below the doc's 0.2–0.3 %. That is explained by a near-zero
      Vos (below).
    - **10.3, HIGH:** `beam phase 1053`, then `beam off`, `hpf hold`, `beam on`, `adc 5 100`
      → **2185 codes (1.761 V)**, inside 2000–2500 on the first try. The sweep flipped at
      **53.12 %**: TP8 nominal 1.7531 V, ADC5 1.7278 V. The ADC5 movement of 7.3 mV is HOLD's
      drift.
    - **10.5:** vref = (1.7278 − a₁)/(0.5312 − d₁) = **3.254 V** (3.250–3.256 over the three
      LOW runs; board 2 3.268, board 3 3.256). Vos = a₁ − vref·d₁ = **−0.6 mV** (−2.0 to
      +1.4; boards 2 and 3: +11.0 and +5.4). Both pass.
      - The LOW point sits on the DAC's first levels (3.2 mV each), so Vos is good to about
        ±2 mV.
      - `BRINGUP_NEW_BOARD.md` §10.2 now says so.
    - **10.6, beam off:** ADC5 movement across the sweep **2.4 mV**, under 3 (board 2 1.6,
      board 3 3.2).
    - **10.7:** slot B, seq 16. The saved `threshold: level 51` is §10.1's leftover, and it is
      not restored at boot: see §9, "never restored".
  - **§11 PASS:** `cfg` (slot B, seq 16):
    - carrier 104166 Hz, phase 1333 ticks;
    - `phase mdl: fitted, pure delay (80000..200000 Hz)`;
    - `hpf sel : TRACK = 0`, `adc5v : scale 1.0781`, `cal duty : 25.00 %`;
    - `path 0.00 mm (unset)`, which is set in `BENCH_P3_DETECT.md` §3.7.
    - **Base bring-up §0–§11 complete on Board 4**, with the gaps below.
  - **Board 4 sign-off** (`BRINGUP_NEW_BOARD.md` "Sign-off"; copied here as that section
    asks):

    | § | item | Board 4 (2026-11-11) |
    |---|---|---|
    | 0 | TIA variant | **reported** original-board target, 120.475 kΩ / 1.500 pF; not inspected here, and §9's delay cannot tell it from stock |
    | 0 | Reworks | TIA (reported). CR-23 1 µF: not checked (Step 12.1 not run) |
    | 1 | PSU at 5.20 V (DMM at J1) | 5.206 V |
    | 1 | PSU current limit | not recorded: this PSU has no current readout |
    | 1 | Chip UID; silicon | `8fabac4bb8add852`; A4 |
    | 1 | Partition table; image | ✅ 2026-11-11. **Shipped with 1.5 in A** (bought, `normal boot`), **r5 (1.1) in B** as the fallback |
    | 1 | Pour joints inspected (Step 1.2) | not recorded |
    | 2 | VIR; TP2; TP6 | skipped (owner). At Step 5.2 instead: J3 screw **35.94 V**; TP2 **12.28 V**; TP6/7/9/10 **2.601 V** |
    | 2 | Boost SW frequency | not done |
    | 3 | E9 `pins` | all 0 |
    | 4 | +5V_IN on USB only; on PSU; scale | 4.862 V; 5.206 V; **1.0781** |
    | 5 | Standby and idle currents | **not measured**: take before §13 |
    | 5 | Tests 1–5 | ✅. 5.4 on the rail only (J7.5, Saleae); 5.5's plateau 579 ms |
    | 6 | U9 clamp width | **120.96 µs** |
    | 6 | Beam temperature at 25 % | **92.9 °C plateau at 20 min** (heatsink 68.1 °C) |
    | 7 | TP7, beam off; ADC2 | 2.601 V; 3207 codes |
    | 7 | `hpf test`; HOLD leakage | CONFIRMED TRACK = 0; **53 pA** (2.31 mV/s) |
    | 8 | Max linear duty (CR-15) | ✅ ≥ 25 %, probably with a white wall in view |
    | 9 | `demod_phase_ticks`; chain delay | **1333**; **487 ns** (model 482–510 ns) |
    | 9 | `cal model` residual / verdict | **0.23°, PURE DELAY** |
    | 9 | D12 attenuation / target | none / white wall ~1.2 m, angled, **69 % FS** |
    | 10 | DAC vref; comparator Vos | **3.254 V**; **−0.6 mV** (±2) |
    | 10 | GPIO44 crosstalk, beam off | **2.4 mV** ✅ |
    | 11 | `cfg save` verified | ✅ slot B, seq 16 |
    | 12 | Gate-hold capacitor; gate DAC (12.2 part 5) | ✅ fitted; ✅ TP3 4.94 / 8.94 / 0 V |
    | 12, 13 | The rest: dry checks, live, `strobe cal 9`; before a Pi | the collaborator, without a scope or LA |
  - **Step 12.1 PASS** (owner): the 1 µF gate-hold capacitor is fitted. The DMM checks now
    listed in 12.1 (each end < 1 Ω to its pad, gate → TP3 ≈ 47 Ω, no hard short, R67
    ≈ 56 kΩ) were not reported; asked. They can be done in 12.2's unpowered session.
  - **The owner's decisions (2026-11-11):**
    - **The collaborator tests the Pi side, the camera and the strobe.**
    - **Board 4 ships with the image after 1.5**: 1.6, with the §9 firmware batch.
    - **`BRINGUP_NEW_BOARD.md` §12 and §13 rewritten to be self-contained**, with only what
      handing the board over requires:
      - **§12:** 12.1 the capacitor; 12.2 dry (the leads, the J3 joints, the refusals, the U5
        clamp, the gate DAC and its stability); 12.3 live to the first 0.5–2.5 A and U5 with
        current; 12.4 `strobe cal 9` once with the FLIR, then the cap's refusal.
      - **Dropped from §12:** 6a.2/6a.3 (the burst engine and the schedule math), 6b's A7 and
        interlock steps, the capacitor's decay capture, Step 4b (VIR sag) and Step 5
        (bursts). Each is proven on board 1, or is a design measurement.
      - **§13:** 13.1 a rollback, then the shipping image, a dry strobe re-check and a power
        cycle; 13.2 the USB reset paths and J8.37 released; 13.3 pack; 13.4 the sheet that
        travels with the board.
      - The first seating (the R39 check) and the Pi tests move to the collaborator, via the
        sheet.
    - **The camera** (owner): it takes a 1.8 V trigger and puts out a 1.8 V strobe, so it does
      not level-shift. The owner has TXS0108E modules for it. Fix on the next revision.
      - The camera board has 1.8 V and 3.3 V output pins.
      - The module's VA goes on the camera's 1.8 V; its VB on the board's 3.3 V (J5.2,
        +3.3VA), not the camera's 3.3 V. The board can be on with the camera off, and a
        trigger high would then back-power the camera's 3.3 V rail through the module.
      - Recorded in `NEXT_BOARD_REV.md` CR-09, `HANDOFF.md` rule 6, and Step 13.4's sheet.
  - **Every strobe firing, dry or live, is refused while a Pi is present.** That is
    `strobe_plan.c` `STROBE_REFUSE_PI`, in the current source. The collaborator's strobe work
    is therefore from the USB CLI with J8 empty. Firing with a Pi up needs the Phase 7 shot
    firmware. Stated in the sheet.
  - **I was wrong (2026-11-11): "a meter across R80 would not give a clean reading".**
    - `BENCH_REWORK_CHECK.md` §2, written for these three boards, specifies it: unpowered,
      both polarities, D12 shaded, expect 119–122 kΩ.
    - The rest of the circuit can only lower the reading. So a stable reading well above
      ~125 kΩ means the 162 kΩ is not connected.
    - It does settle Board 4's TIA question. Added to `BRINGUP_NEW_BOARD.md` Step 0.1.
  - **CR-28 added to `NEXT_BOARD_REV.md`** (owner's request): a temperature sensor at D11.
    - A digital I2C sensor on spare GPIOs, because all eight ADC inputs are in use.
    - Uses: a FLIR-free Step 6.4, a hard beam limit, a measured "warm" state for calibration,
      and telemetry.
  - **Step 12.2 part 5, the gate DAC: PASS** (owner, DMM on TP3):
    - gate 50 → **4.94 V** (band 4.71–5.06; board 1 4.936);
    - gate 90 → **8.94 V** (band 8.50–9.09);
    - gate 0 → **0 V**.
    - The owner first typed 8.94 V for gate 50 and corrected it to 4.94. Nothing was changed
      on the typo.
    - **Read with a hand-held tip on the powered board.** TP3 is 0.59 mm from C56's +12 V
      pad, and no point on that net is 1.5 mm clear (U7 0.23 mm, R60/R63 0.48 mm), so
      `HANDOFF.md` rule 8 calls for a lead. Noted in 12.2 part 5.
  - **The owner's plan (2026-11-11): no soldering on this board, so no logic-analyser or scope
    leads.**
    - The collaborator runs the rest of §12 and §13 without a scope or LA.
    - Board 5 stays with the owner for the work that needs a scope or LA.
    - **`BRINGUP_NEW_BOARD.md` §12.2–12.4 restructured to fit:** parts 1–5 of 12.2 need only a
      DMM and the CLI. Part 6 (the leads, the dry clamp on the LA, the loop on the scope) is an
      add-on, and every scope check in 12.3–12.4 is marked *with a scope*.
    - **U5's clamp is still measured on Board 4**, with current: 12.3 part 4's ADC0 on-time,
      2 µs resolution, against 118–137 µs. An unclamped pulse ends at the firmware's 200 µs
      limit, harmless at ~2 A.
    - **Residual risks, accepted with the no-instrument path:**
      - ADC0's current scale is not checked against TP4 on this board. R65 and R66 (0R27
        2512) are now inspected for value instead (12.2 part 1), and 12.4 treats a solved level
        above ~60 % as a finding;
      - the gate loop's stability is not seen on a scope. The firmware's shape check ends live
        mode on a split or dipping plateau;
      - a turn-on spike under 2 µs can fall between ADC0's samples. 12.1's capacitor removes
        the known one;
      - Q9's thermal coupling is not imaged. The firmware's budget, 30 mC per 10 s, keeps Q9
        near 30 mW average.
  - Step 13.4's sheet now opens with the collaborator's own list: 12.2 parts 1–4, 12.3, 12.4,
    then 13.1–13.2 before the first Pi.

### 2026-11-11 - Image 1.6: Board 4's shipping candidate (built; independent review NOT PASSED)

**The phase-independent `level` claim in item 4 below is superseded by V16-F1.**
The pure fit passes; acquisition through the HPF and ADC rails does not. See the
independent-review entry below. This candidate stays off every board.

The owner approved the §9 batch and chose to ship Board 4 with the image after 1.5.

- **Baseline:** `captures/review_2026-11-11/baseline_1.5/`, a copy of the source tree before
  any 1.6 edit.
  - Built Release, it is byte-identical to `build_v1.5/pitrac.bin` outside the build stamps
    (`compare_bins.py`: 4 bytes differ, all in them).
  - The SDK's binary-info build date (11 bytes, date only) also differs on another day. The
    comparison now masks it as well; 1.5's same-day baseline never met it.
- **What changed** (`v1_6_2026-11-11.patch`, 14 files):
  1. **The saved threshold is restored at boot.**
     - `cfg_apply_threshold()` is called after `detect_init()`, with
       `detect_threshold_set_level()` (no settle wait, clamped).
     - A level above `DAC_TOP` is ignored. A never-saved board stays at 0.
  2. **`cfg save` names its fix:** `cfg_save_unblock_hint()`, the same tests in the same order
     as the reason. Printed on the refusal and on `cfg`'s `save` line.
  3. **`cal demod` will not commit under 30 % of full scale** (`CAL_PEAK_MIN_FRAC`).
     - The verdict moved, unchanged, into the pure `cal_demod_judge()` (new `cal_math.c`).
     - `too_small` is kept out of `valid`, so `cal model`'s points are judged as before.
  4. **`level` is true at any phase.** 8 chopped readings, 1/8 of a period apart, fitted like
     the sweep: within 5.3 % of the true peak at every phase, 2–35 % duty, against a direct
     simulation. It puts the phase back afterwards.
  5. **Two pre-existing defects in `cmd_cal demod`, found while writing (3):**
     - a refused or aborted sweep left the beam on the last grid point (1417 ticks). It now
       puts back the phase it had;
     - `cal_duty`/`cal_warm` were written before the verdict, so a refused sweep relabelled
       the phase still held. They are now written only with a commit.
  6. **Text:**
     - the `camera.h` comment (the 1.5 confirm review's note);
     - `cal.h`'s claim that a reflector "is not needed, and on this board it hurts": true for
       board 2, the opposite on Board 4;
     - `help`, `id`, and the version, 1.6.
- **Build:** `build_v1.6/`, Release (picotool: `build attributes: Release`, version 1.6),
  `built : Nov 11 2026 15:57:11`, 244712 text / 0 data / 422684 bss.
  - UF2 SHA256 `AB4E4F1EC2D5EC66095F0003C3FF1513043082AB642C828245A0612082284507`.
  - `.bin` SHA256 `2DF1B3F33CE77A3D1DE6AAD20174C48070C048647195A27EA32EE394D7E412A0`.
  - No warnings, and the changed files are clean under `-Wall -Wextra`.
  - One warning in the first build was a real bug: the new `id` line had "30 % and" in a
    printf format. Fixed to `%%` before this build.
  - Rebuilt after the mutation run: the same hashes.
- **Host tests: 276/276** (`ctest_1.6.log`).
  - 1.5's 265, plus `cfg.threshold_restored_at_boot`, `_default_zero`,
    `_out_of_range_ignored`, `cfg.unblock_hint_names_the_command`, and `calm.*` (7).
  - The level test's reference is a direct simulation of the lock-in correlation, not
    `cal.h`'s Fourier series.
- **Mutations: 13 of 13 caught** (`mutations_1_6.md`). M6 was first caught by a segfault (a
  NULL hint into `strstr`). The test now checks for NULL, and the record is from the clean
  re-run.
- **Review:** `captures/review_2026-11-11/REVIEW_BRIEF.md`, for a fresh reviewer
  (`HANDOFF.md` §5 rule 8). **Not on any board until it passes.**
- **I was wrong (2026-11-11): "`cal level`, a new command".** The `level` command already
  existed (`BENCH_P3_DETECT.md` §3.4), reading the current phase only. I proposed a new one
  without checking. And `BRINGUP_NEW_BOARD.md` §9.2 never pointed at `level`, which is part of
  why Board 4 took four sweeps.
  - 1.6 makes the existing `level` phase-independent, rather than adding a command.
  - §9.2 now starts with it, and §3.4 describes 1.6's behaviour.
- **Docs:**
  - `BRINGUP_NEW_BOARD.md`: §9.2 (aim with `level`, the 30 % floor); §10.7 (`threshold duty 0`
    before `cfg save`, since 1.6 restores it); §13.1 (the image, the threshold back to 0, the
    restore seen across a power cycle); the 13.4 sheet (`cfg save` with a Pi, `level`);
  - `BENCH_P3_DETECT.md` §3.4;
  - `HANDOFF.md` §4 (1.6, `build_v1.6/`, 276 tests, `cal_math.c`).

### 2026-11-11 - Image 1.6 independent review NOT PASSED: V16-F1 in `level`

Report: `captures/review_2026-11-11/INDEPENDENT_REVIEW.md`; executable evidence is in
`independent_probes/` beside it. A fresh reviewer examined the exact 1.5-to-1.6 patch.

- **V16-F1 (blocker):** `cal_level_measure()` jumps 45° every 200 ms through the 0.66 s
  HPF and ground-referenced U12B/ADC5. The coarse scan creates sustained baseline-history
  errors and clipping, so it can print `SATURATED -- far less light` for a target accepted
  by `cal demod`. Ignoring the first 3.5 s does not fix this repeating transient.
  - Software model, not a hardware measurement: 25 % duty, 480 ns chain delay, documented
    HPF/gain/ADC rails. After 32 s, a stationary target reads 54.1406 % and SATURATED;
    the unchanged sweep from a freshly settled beam reads 55.7573 %, valid, sat_frac 0.
  - A stronger target reads 56.7174 % and SATURATED versus a fresh sweep's 65.0502 %,
    valid, sat_frac 0. Sensitivity checks cover HPF τ 0.60–0.82 s and 3.2–20.1 mV lower
    headroom. Full assumptions, acquisition histories and commands are in the report.
- **Correction to the earlier claim:** the 5.3 % result proves the pure eight-point fit
  of ideal correlation samples, not the real acquisition path. Calling the resulting
  command a true level at any phase was wrong; its test omitted HPF history and ADC rails.
- **V16-N1 (should-fix):** `cal.h` still calls `level` a current-phase reading and still
  says "No reflector" above the sweep. Correct both with the next firmware candidate.
- **Independent checks passed:** fresh Release host build 276/276; 810 acquisition/abort/
  restoration scenarios; 16 extracted CLI demod verdict/metadata combinations; 476640
  ideal-correlation fits (worst error 5.302930 %, rounded 5.3 %). Threshold restore, save
  hint ordering, the verdict move and the commit/metadata restore paths had no finding.
- **Provenance:** all 14 patch entries reconstruct the current source exactly; other
  baseline source inputs agree. The baseline binary matches reviewed 1.5 outside its
  stamps. A separate warning-free Release build matches supplied 1.6 outside its stamps.
  Original UF2/BIN hashes, size and build stamp remain as recorded above.
- **Next:** keep Board 4 on r5 for §12. Fix V16-F1 with an acquisition regression and a
  deliberate mutation, correct V16-N1, build with a new identity and obtain an independent
  confirm review before §13.1. Do not rebuild or flash the rejected 1.6 candidate.

No firmware source, reviewed image, production host-test binaries or hardware were changed;
nothing was committed. Documentation now marks the unaccepted `level` claims and the
shipping-image placeholders. Hardware acceptance is still pending.


### 2026-11-11 - Image 1.7: Board 4's next image (built; independent review PASS 2026-11-12)

**The owner, after Board 4 shipped on 1.5: "proceed to 1.7" = 1.6 without `level`, the
interlock kept.**

- **What changed from 1.6** (`captures/review_2026-11-11/v1_7_from_1_6.patch`, 7 files):
  - **`level` is back to 1.5's one-phase reading.** `cal_level_peak()` is byte-identical to
    1.5's (checked).
    - `cal_level_measure()`, `cal_level_t`, `CAL_LEVEL_PHASES` and `cal_level_from_points()`
      are removed.
    - So are the three `level_*` host tests.
    - `cmd_level`'s loop is 1.5's. Its banner says "the CURRENT phase", and its TOO LOW
      verdict uses the 30 % floor.
  - **V16-N1:** `cal.h`'s top note and `cal_demod_phase()`'s "No reflector" comment are
    corrected.
  - **Text:**
    - `help` and `id` no longer claim "at any phase";
    - `cal demod`'s TOO SMALL advice no longer points at `level`;
    - `CMakeLists.txt` is at version 1.7, and its history records that 1.6 never left the PC.
  - **Kept from 1.6, untouched:** the threshold restore, `cfg save`'s hint, the 30 % floor,
    the phase restore and the camera.h comment. The patch touches no line of
    `config_store.c/.h`, `detect.c/.h`, `main.c` or `camera.h`.
- **Baselines:**
  - `baseline_1.6/` is the 1.6 source before any 1.7 edit. Its 9 reviewed files match the
    1.6 reviewer's `reviewed_source_sha256.txt` exactly.
  - `v1_7_from_1_5.patch` (14 files) is everything since the last image that passed review.
- **Build:** `build_v1.7/`, Release (picotool: `build attributes: Release`, version 1.7),
  `built : Nov 11 2026 16:41:14`, 244008 text / 0 data / 422684 bss.
  - `.bin` 239916 B, SHA256 `F7129CD4FD07D6926C106F86B4B91593163A1D97CAD3F7565B5F12EF84C94602`.
  - UF2 SHA256 `CCB7B931CEDE9A9A384F06468AD1F128A29A4BE77FF04898E785141FBA15087B`.
  - No warnings. `check_retained`: `s_mirror` and `s_notes` unchanged, so an update from 1.5
    on the Pi reports and powers the Pi back as designed.
- **Host tests: 273/273** (`ctest_1.7.log`): 1.6's 276, less the three `level` cases.
- **Mutations: 9 of 9 caught** (`mutations_1_7.md`, runner `mutate_1_7.py`). These are 1.6's
  M1–M9, for the kept items; its M10–M13 tested the removed fit. Both mutated files were
  restored byte for byte (`sha256sum -c`), and the image was built from that tree.
- **The author's probe** (`v1_7_probes/`; not independent). It runs 1.7's real `cal.c`
  through the 1.6 review's own analog model (τ 0.66 s HPF, ADC5 clamped to [11, 4095]),
  from 16 starting phases: `cal demod`, a commit, then 40 `level` lines.

  | Stimulus | Sweep peak | 1.7 `level`, settled | ADC max | Verdict |
  |---|---|---|---|---|
  | 4000 | 46.5–50.9 % | 42.12 % | 2003 | low |
  | 4800 | 55.8–60.4 % | 50.54 % | 2401 | GOOD |
  | 5600 | 65.1–69.8 % | 58.96 % | 2799 | GOOD |

  - The phase is never moved, and the readings are steady line to line, far from the rail.
  - It reads 9–16 % below the sweep's peak. That is conservative, and never a saturation or
    "less light" remedy for an accepted target.
  - This is a model, not the bench.
- **Docs:**
  - `BRINGUP_NEW_BOARD.md`:
    - §9.2: `cal demod` sets the level; `level` only after a commit;
    - §10.7: "from 1.7";
    - §13.1: 1.7's identity; Board 4 loads into B with `-p 1`, and 1.5 becomes the fallback;
    - the 13.4 sheet: Board 4's copy says 1.5, there is no threshold restore on 1.5, the
      beam-at-2 % note is added, and the recalibration starts with `cal demod`.
  - `BENCH_P3_DETECT.md` §3.4: `level` rewritten for 1.7.
  - `HANDOFF.md` §4: 1.7, 273 tests, `build_v1.7/`.
- **Review:** `captures/review_2026-11-11/REVIEW_BRIEF_v1.7.md`, for a fresh reviewer
  (`HANDOFF.md` §5 rule 8). **Not on any board until it passes.**
- **For Board 4 after it passes:** by `pitrac-link update` from the Pi, or by Step 13.1 over USB
  (`-p 1`). 1.7 goes into B, replacing r5, and 1.5 in A becomes the fallback.

### 2026-11-12 - Image 1.7 independent review PASS, with nonblocking wording note V17-N1

Fresh report: `captures/review_2026-11-11/INDEPENDENT_REVIEW_v1.7.md`; new executable
probes and mutations are in `independent_v17_probes/`. The original 1.6 evidence is preserved.

- **V16-F1 closed:** the all-phase acquisition and its callers are removed. The actual
  `cal_level_peak()` body matches reviewed 1.5 exactly. Independent HPF/ADC acquisition
  tests confirm a held phase and no coarse-scan clipping for the accepted 4800/5600-code
  scenarios. **V16-N1 closed:** the current-phase and reflector comments are corrected.
- **V17-N1, nonblocking should-fix:** the inherited single-phase reading can understate
  the sweep even after a committed phase. The new refusal label overstates what it proves:
  a model scene gives a clean, committable 30.21–33.73 % sweep, but a settled 27.38 %
  `level` reading says `TOO LOW: cal demod refuses`. This is a model result, not a bench
  measurement. Use `level` relatively and `cal demod`'s peak/verdict for acceptance.
  - The earlier description "conservative" is not a general safety proof. In the tested
    accepted scenes the low reading does not recreate V16-F1's clipping/remedy failure.
  - BRINGUP §9.2 and BENCH_P3_DETECT §3.4 now qualify this explicitly. Correct the firmware's
    refusal/true-peak wording with a future candidate; reviewed 1.7 bytes stay unchanged.
- **Independent executable checks:** 176 acquisition model runs (11 cases × 16 starting
  phases), exact `cmd_level` output, 16 demod commit/restore/metadata combinations, all
  65536 saved uint16 threshold values, and 128 save blocker/hint combinations.
  Five source-copy mutations were caught, including a phase move restored before return;
  checking only the final phase would miss that acquisition transient.
- **Provenance/build:** both patches reconstruct current source; baseline 1.6 matches the
  prior review's nine source hashes. A separate warning-free Release build matches the
  supplied 1.7 binary outside six date/stamp bytes. Fresh host suite **273/273 PASS**.
  `s_mirror` and `s_notes` have identical addresses/sizes in 1.5 and 1.7. The update path
  and Pi-present strobe interlock are unchanged. Full evidence:
  `captures/review_2026-11-11/independent_v17_verification.md`.
- **Exact accepted candidate:** `build_v1.7/`, Release / version 1.7 / TBYB;
  `Nov 11 2026 16:41:14`, 244008 text / 0 data / 422684 bss, BIN 239916 B.
  - UF2 SHA256 `CCB7B931CEDE9A9A384F06468AD1F128A29A4BE77FF04898E785141FBA15087B`.
  - BIN SHA256 `F7129CD4FD07D6926C106F86B4B91593163A1D97CAD3F7565B5F12EF84C94602`.
- **Bench-doc correction:** Step 13.1's literal commands still showed `-p 0` below the
  Board 4 `-p 1` warning. They now show Board 4's actual A→B update and the installed
  picotool path. The r5-in-B case explicitly uses `-p 0`. The Board 4 handover sheet still
  describes its shipped 1.5, not an assumed completed upgrade.

**No flash, hardware operation, firmware-source/candidate-byte change or commit occurred.**
Board 4's last recorded image remains 1.5 in A, with r5 in B. When the owner starts 1.7
acceptance: reflash first with the exact candidate above, targeting B; 1.5 stays the fallback.
Hardware acceptance of 1.7 and the saved-threshold power-cycle check remain pending.


### 2026-11-12 - Board 1: image 1.7 from the Pi (`BENCH_P8_PI.md` §8.9b): PASS

**State:**
- the Pi seated (SW2 taped, its USB-C unplugged, J4 empty, beam off), and **J3 disconnected**;
- PSU 5.20 V;
- 1.2 in A, and 8.9's unbought test image in B.

Logs: `captures/COM5_2026_11_12.*`, `captures/p8_8.5_cycles_2026-11-12_095554.log` and its
`.csv`.

- **Step 1:**
  - `stat`: STANDBY, latch 0, no fault, 5V_IN 5.202 V.
  - `id`: 1.2 (partition A), `Nov 10 2026 09:47:13`, UID `a764f5332ca5ac53`.
  - `upd`: partner `version 1.2, NOT bought`.
  - `cfg`, slot B seq 52: threshold level 0, phase 0 ticks, model not fitted, TRACK = 0,
    scale 1.0694.
- **Step 2:** `on` → RUNNING, `pi : present 1`.
- **Step 3:**
  - `scp` sent 234 KB (239916 B).
  - The board printed `update: the Pi is off: installing into the other partition` and
    `update: installed and verified; rebooting into it on trial`.
  - The monitor reconnected after the reboot, so 1.7's first lines were not captured;
    `upd` below shows the result.
  - The Pi's `pitrac-link update` lines were not pasted.
- **Step 4:**
  - `id` → `version : 1.7 (partition B at 0x100000)`, `built : Nov 11 2026 16:41:14`, the
    same UID.
  - `upd` → `flash update boot, ON TRIAL: bought itself at the end of init`, and
    `partner : version 1.2, bought: the fallback`. Also `update : installed, 1.2 -> 1.7
    (decided at this boot)`, `it ran on trial and bought itself`.
  - `hist` → `this boot : flash update boot (...)`, `loaded : RAM mirror (warm reset; nothing
    lost)`. Boots 21; requests and in time 31; timed out 0; cuts 2; faults 0.
  - `cfg` unchanged (seq 52). Its `save` line read `refused now ... to save: send 'off', wait
    until 'stat' shows STANDBY (a powered Pi is shut down first), then 'cfg save'`. That is
    1.7's hint, seen on hardware for the first time.
  - **The interlock with a real Pi:** `pi : present 1`, and `strobe pulse 20` →
    `REFUSED -- nothing fired`, `a Pi is present -- Phase 6 runs with J8 empty`.
- **Step 5: `PASS: 3/3 cycles`.**
  - The latch opened 10.04, 10.03 and 10.04 s after `off`.
  - RUNNING after 16.6–16.7 s; 5V_IN min 5.176–5.186 V.
  - The Pi: `throttled=0x0`, 0 storage warnings.
  - `hist` deltas as expected: requests and in time +3, everything else 0.
- **Step 6:**
  - STANDBY: `J8.37 : released (latch open or no Pi 3V3: nothing driven into J8.37)`.
  - The threshold was already 0, so there was nothing to do.
  - 750 s later, `cfg` → slot B seq 56: four telemetry saves, one after the `off` and one per
    cycle.
- **Board 1 now:** 1.7 in B, bought; 1.2 in A, bought, the fallback.
- **Not exercised here:**
  - `cal demod`'s 30 % floor and phase restore;
  - a nonzero threshold restored at boot (board 1's is 0).

  Board 5's bring-up on 1.7 covers them.
- **I was wrong (2026-11-12): §8.9b Step 5 did not say to send `off` first.**
  - `p8_cycles.ps1` refuses to start unless the board is in STANDBY with latch 0. The first
    run stopped there, harmlessly (`STOP: start in STANDBY, latch 0, no fault`).
  - Fixed in §8.9b. The 1.3 text it replaced had the same gap.
- **`hist`'s requests read 31 after the update, below the 32 that 8.9 recorded on 2026-11-10.
  Explained, not a fault.**
  - The counts reach flash only after 10 s in STANDBY.
  - The 2026-11-10 10:04 log ends with the board RUNNING and the Pi up after the rollback.
    The next session (10:36, 6c, no Pi) started from flash.
  - So the board was switched off before 10 s in STANDBY, and 8.9's counts (requests 31–32,
    boots 19–21) never reached flash.
  - Today started from the last save: requests 30, and boots 19 + today's power-on = 20.
  - Added to `BENCH_P8_PI.md`'s reading notes: before switching off, `hist` → `flash : up to
    date`. 8.9's recorded deltas stand.
- **Owner decision, the same day:** live strobe mode does not arm the watchdog while a Pi is
  powered (§8). It goes into image 1.8.

### 2026-11-12 - Image 1.8: the strobe with a Pi present (built; independent review NOT PASSED)

**The claimed Pi-change binding below is not satisfied by this candidate.** V18-F1
reproduces against the actual power FSM and strobe code. Do not flash or bench 1.8;
see the independent-review entry below.

**The owner, 2026-11-12:** live strobe mode does not arm the watchdog while a Pi is powered
(§8). With that decided, 1.8 lifts the Pi refusal.

- **What changed from 1.7** (`captures/review_2026-11-12/v1_8_from_1_7.patch`, 11 files):
  - **A Pi powered is no longer a refusal,** dry or live.
    - `STROBE_REFUSE_PI` leaves `common_checks()` and `strobe_live_may_continue()`.
    - Its enum slot becomes `STROBE_REFUSE_PI_CHANGED`, so no later value moves.
  - **The watchdog:**
    - `strobe live on confirm` arms it only with no Pi powered
      (`strobe_live_arms_watchdog()`);
    - live mode requires it only in a session armed without a Pi
      (`strobe_live_needs_watchdog()`);
    - `wdog off` is refused only then.
  - **A live session is bound to the Pi state it was armed in.** This is the author's design;
    the brief asks the reviewer about it (Q1).
    - A Pi that appears or goes away ends the session, gate at 0: `a Pi appeared or went away
      since live mode was armed`.
    - So a session armed without a Pi never runs on with one under live mode's watchdog, and
      one armed with a Pi never runs on once it is gone.
  - **Text:**
    - the live checklist: a Pi only by `BENCH_P8_PI.md` §8.10 with the owner's go, and the
      PSU at ≥ 5 A;
    - the ARMED line and `strobe live`: the watchdog not armed with a Pi;
    - `wdog`, `help` and `id`.
  - **V17-N1:** `level` is an approximate aiming aid.
    - Its verdicts read `very low` under 30 % and `aim OK -- 'cal demod' decides` from 50
      to 70 %, and nothing predicts a refusal.
    - `cal_level_peak()` is unchanged.
- **Baseline:** `baseline_1.7/` matches the 1.7 reviewer's source manifest: 105 files and
  `CMakeLists.txt`, 0 mismatches.
- **Build:** `build_v1.8/`, Release, version 1.8, TBYB, `built : Nov 12 2026 10:25:58`,
  245072 text / 0 data / 422684 bss.
  - `.bin` 240980 B, SHA256 `5E691DCBA88A990C6A81FF0254D54B1D65E248BAF174284B076FF4A54D530463`.
  - UF2 SHA256 `14D5CBC6575A3C3E39A0C50AC97C65462A42B696B709F5F7628F1F1188002DB6`.
  - No warnings. `check_retained`: unchanged.
- **Host tests: 280/280** (`ctest_1.8.log`): 1.7's 273, plus 7 new.
  - Five run the real `strobe.c` in `engine_test`: live arming with and without a Pi, a Pi
    going away, a Pi appearing, and the operator's own watchdog.
  - `strobe.live_policy_exhaustive` now covers 2^17 input combinations.
- **Mutations: 11 of 11 caught by the tests** (`mutations_1_8.md`).
  - The first run's M2 and M11 failed to build (C4100 under `/WX`).
  - They were rewritten so that the tests, not the compiler, had to catch them.
- **Review:** `captures/review_2026-11-12/REVIEW_BRIEF_v1.8.md`. **Not on any board until it
  passes.**
- **Blocked candidate plan — do not run:** `BENCH_P8_PI.md` §8.10 on board 1. The
  owner approved the step (2026-11-12, §8), but this candidate did not pass review.
  - Reflash first, by an update from the Pi: 1.8 into A, and 1.7 in B becomes the fallback.
  - It measures the +5 V rail under 9 A pulses with a Pi on it, idle and loaded.
- **Then Board 4,** by `pitrac-link update`, after §8.10 passes.

### 2026-11-12 - Image 1.8 independent review NOT PASSED: V18-F1 cached Pi presence

Fresh report: `captures/review_2026-11-12/INDEPENDENT_REVIEW_v1.8.md`. Executable probes
and copied source are in `independent_v18_probes/`; supplied source/images remain unchanged.

- **V18-F1 (P1/blocker):** `strobe.c` feeds `power_pi_present()` into both session binding
  and watchdog admission. That is a deliberately sticky power-FSM cache, not fresh proof
  of the Pi's powered signal. Pure policy tests manually changing a Boolean miss this.
  - With a Pi session armed, drop the header 3V3 input and wait past the down debounce.
    The actual FSM remains RUNNING, cached Pi 1, live 1, watchdog 0, gate 1. Raising the
    gate to 2 is admitted, and the actual firing path starts the PIO. Synthetic ADC data
    intentionally ends the probe only after firing; this is not a physical current test.
  - In BENCH_RUNNING, raise header 3V3 before the late-presence debounce completes. The
    actual arming code still sees cached Pi 0 and arms its own watchdog while 3V3 is high.
    It ends live only when presence is confirmed later. No physical watchdog reset was
    measured or simulated; the reproduced defect is the enabled watchdog window.
  - A normal halt with RPI5_ON low and header 3V3 still high correctly keeps the powered-Pi
    session. Do not treat that as absence. Do not blindly clear the FSM's cached presence:
    its conservative meaning protects Pi power/reset paths. Use a fresh strobe-specific
    observation, handle unknown/mismatching state conservatively, and test the integration.
- **Correction to the candidate claim:** checking equality of the cached Pi Boolean does
  not prove that a live session ends on a header-powered-state change. The earlier claim
  that it always ends on appearance/disappearance was wrong for these actual FSM paths.
- **V18-N1 (nonblocking):** stale no-Pi/unconditional-watchdog comments in `strobe.h`,
  `service.h` and `main.c`. Correct these alongside V18-F1 in the next candidate.
- **Other focused checks:** actual `cmd_wdog` ownership/off behavior; normal halt; actual
  FSM shutdown and active camera-gated teardown; 20 ms dry gate-decay guard. The actual
  assembled PIO passes 2614 autonomous finite/truncated-feed executions, completing or
  stalling low with bounded pulse/count behavior. This is digital simulation, not U5 or
  analog safety acceptance. V17-N1's aiming-aid wording is corrected.
- **Routine checks:** fresh host suite **280/280 PASS**; all eleven patch entries reconstruct
  current source. Ninety-three baseline files match the prior review's 1.7 manifest and
  twelve omitted inputs remain unchanged (105 total). A separate warning-free Release
  build matches supplied 1.8 outside four stamp bytes; retained ELF addresses/sizes match
  1.7. Evidence: `independent_v18_verification.md` beside the report. Passing these checks
  did not expose the live-cache integration defect.
- **Rejected candidate preserved:** `build_v1.8/`, Release / version 1.8 / TBYB,
  `Nov 12 2026 10:25:58`, 245072 text / 0 data / 422684 bss, BIN 240980 B.
  - UF2 SHA256 `14D5CBC6575A3C3E39A0C50AC97C65462A42B696B709F5F7628F1F1188002DB6`.
  - BIN SHA256 `5E691DCBA88A990C6A81FF0254D54B1D65E248BAF174284B076FF4A54D530463`.
- **Next:** correct V18-F1/N1, add real-FSM regressions and a deliberate mutation per fix,
  build under a new recorded identity, and obtain an independent confirm review. Only
  then update §8.10's candidate identity and reflash first for hardware acceptance.

**No firmware source/image edits, flashing, hardware operation or commits.** §8.10's owner
approval remains recorded; its failed software-review gate blocks the test. Board 1's last
recorded image stays 1.7 in B. The latest COM5 file records STANDBY, but is not a live query.

### 2026-11-12 - Image 1.9: V18-F1/N1 confirmed closed; overall review NOT PASSED (V19-F1)

**The 1.8 review (NOT PASSED) found that live mode read the power FSM's debounced Pi flag.**
1.9 keeps 1.8's design and changes that input.

- **What changed from 1.8** (`captures/review_2026-11-12/v1_9/v1_9_from_1_8.patch`: 14 files
  changed, 1 new):
  - **`power_pi_3v3_sensed()`** (`power_fsm.c`): the Pi's header 3V3 as sensed now (GPIO24),
    unfiltered. The FSM itself is unchanged.
  - **`strobe_pi_t`** (`strobe_plan.c`, pure): ABSENT, POWERED, or **UNSETTLED** when the
    sensed 3V3 and the FSM's classification disagree. The strobe's snapshot carries it in
    place of 1.8's bool.
  - **UNSETTLED never arms live mode** (`STROBE_REFUSE_PI_UNSETTLED`).
    - It ends a live session at the next service pass, with the gate at 0, handing back a
      watchdog live mode armed.
    - A settled change still ends a session with `PI_CHANGED`.
    - The watchdog is armed only with the Pi observed ABSENT.
  - **V18-N1:** the comments in `strobe.h`, `service.h` and `main.c`.
    - The `strobe` status line prints the three states.
    - `id` and the live checklist say what the code does.
- **Tests: 292/292** (`v1_9/ctest_1.9.log`): 1.8's 280, plus 12.
  - **`strobe_fsm_test`, new:** 8 cases that link the REAL `power_fsm.c`, `service.c`,
    `telemetry.c` and `strobe.c`, stepped through `pitrac_service()`. They cover:
    - the 3V3 lost under RUNNING;
    - a late Pi in BENCH_RUNNING, before and after arming;
    - a halt, PI_BOOTING, and the settled cases;
    - an orderly shutdown, and the operator's own watchdog.

    The harness is adapted from the 1.8 review's probe. The stand-in `pico/stdlib.h` gains
    the four declarations that probe added.
  - **Three more in `engine_test`:** the sensed 3V3 lost, and risen, and arming refused
    while unsettled. Plus `strobe.pi_observe`.
  - `strobe.live_policy_exhaustive` now covers 2^18 combinations, UNSETTLED included.
- **The 1.8 reviewer's own probe, rerun on 1.9 source** (`v1_9/rp/`, a copy; their folder is
  untouched):
  - Its three defect witnesses now fail where 1.8 passed them:
    - `present_loss`: the session ended;
    - `late_presence`: arming was refused;
    - `late_after_arm`: the session ended.
  - The other seven pass, including the real `cmd_wdog` and 2614 PIO executions.
- **Mutations: 17 of 17 caught by the tests, none by the build** (`v1_9/mutations_1_9.md`).
  M12, M13 and M16 each put 1.8's input back at a different place, and `strobe_fsm_test`
  catches every one.
- **Build:** `build_v1.9/`, Release, version 1.9, TBYB, `built : Nov 12 2026 11:06:20`,
  245512 text / 0 data / 422684 bss.
  - `.bin` 241420 B, SHA256 `32409AE6F24E483282D44EA2E1CD13DE7C5E171785385C02D9EA0EE6B57766B9`.
  - UF2 SHA256 `0004839EE1F093812EC3D3CE3156E3BFD3723A8099AF41E4265B2AEB52247D16`.
  - No warnings. `check_retained`: unchanged.
- **Baselines:** `baseline_1.8/` holds all 105 inputs of the 1.8 reviewer's manifest, and
  matches it exactly.
- **I was wrong (2026-11-12), in 1.8's brief and §6 entry.**
  - "Its 105 `src/` and `tests/` files": `baseline_1.7/` holds 93 files. The manifest's 105
    include 12 tool, board and build inputs that I did not copy; they were unchanged. The
    reviewer accounted for both groups.
  - "A Pi that appears or goes away ends it": that held only for the FSM's flag, not for
    the Pi's actual 3V3, which is what V18-F1 showed.
- **Review NOT PASSED:** `captures/review_2026-11-12/v1_9/INDEPENDENT_REVIEW_v1.9.md`.
  V18-F1/N1 are closed. Explicit Q4 found V19-F1 in the pre-existing reset/bootsel and
  config-save guards. Keep 1.9 off boards; §8.10 stays blocked despite its recorded approval.

### 2026-11-12 - Image 1.9 Sol 6.1 confirm review: V18-F1/N1 closed; overall NOT PASSED on V19-F1

Fresh report: `captures/review_2026-11-12/v1_9/INDEPENDENT_REVIEW_v1.9.md`. Evidence is in
`independent_v19_probes/` beside it. The reviewer had no authorship of 1.8 or 1.9.

- **The requested strobe correction works:** fresh GPIO24 plus the FSM classification
  refuses UNSETTLED arming/gate/fire admission and ends an idle live session on its next
  service pass, zeroing the gate and releasing only its own watchdog. V18-F1 and the named
  V18-N1 comments are confirmed closed. The FSM's own power/shutdown behavior is unchanged.
- **V19-F1 (HIGH/blocker), explicit Q4:** the exact plain reset/bootsel guard and exact
  config-save guard still use the cached Pi classification. With the actual FSM in
  BENCH_RUNNING, latch high, and raw header 3V3 newly high, the cache stays false during
  the 100 ms late-detection debounce. Both plain reset/bootsel are admitted without force;
  their production callers reset after a 50 ms sleep, opening the latch. The config guard
  and unblock hint also return permitted/NULL in that interval. After debounce reset refuses.
  - The guard bodies match approved 1.7. **Earlier independent reviews missed this
    pre-existing defect. It is not a regression introduced by the 1.9 strobe fix.**
  - Actual-source control-flow/admission was reproduced; no reset, flash write, physical
    power cut, voltage disturbance or filesystem corruption was performed/measured.
  - Fix individual safety consumers to account conservatively for fresh sense and cached
    classification; do not replace the FSM's conservative flag globally. Add real-FSM
    boundary regressions and deliberate mutations, then review a new image.
- **The five questions:** sampled glitches should fail closed; persistent lost 3V3 blocks
  re-arm while the FSM still classifies a Pi, but a recovered sense can permit explicit
  re-arm without `off`. During an already admitted bounded firing/readback, live service
  defers the raw-only change; the next gate/fire admission is fresh. This is bounded
  conduction, not a guarantee of uninterrupted Pi power if a CPU hangs while a previously
  owned watchdog is still armed. There is no proved wall-clock cleanup bound through
  ADC processing/serial output. Q4 found V19-F1; Q5 also needs the nonblocking text
  qualifications about active firing and persistent UNSETTLED recovery.
- **Independent executable evidence:** 13 reviewer cases, including an explicit bad
  reset/config admission witness; 2614 actual assembled-PIO executions; one copy-only
  cached-input mutation detected by three separate tests. A green witness means the
  defect reproduces, not that its safety requirement passes. Copies were restored.
- **Parent verification:** fresh host suite **292/292 PASS**; both patches reconstruct
  current source. Full 1.8 baseline matches all 105 prior inputs; 1.7 has 93 copied plus
  12 unchanged omitted inputs. Separate warning-free Release build matches supplied 1.9
  outside four stamp bytes. Retained ELF symbols agree with 1.7. Full evidence:
  `v1_9/independent_v19_verification.md` in the review folder.
- **Preserved candidate:** `build_v1.9/`, Release / version 1.9 / TBYB,
  `Nov 12 2026 11:06:20`, 245512 text / 0 data / 422684 bss, BIN 241420 B.
  - UF2 SHA256 `0004839EE1F093812EC3D3CE3156E3BFD3723A8099AF41E4265B2AEB52247D16`.
  - BIN SHA256 `32409AE6F24E483282D44EA2E1CD13DE7C5E171785385C02D9EA0EE6B57766B9`.

**Flash neither 1.8 nor 1.9.** The owner's approval for §8.10 remains recorded; the software
gate is not met. No firmware source, supplied image, old review record or hardware was
changed by this review, and nothing was committed. Board 1's last recorded image remains
1.7 in B; the same pre-existing reset/config guard limitation applies there.

### 2026-11-12 - Image 1.10: V19-F1 entry-time guard fixed; confirm review NOT PASSED (V110-F1)

**The 1.9 review (NOT PASSED) found V19-F1.** Plain `reset`/`bootsel` and `cfg save`'s guard
read the power FSM's Pi flag alone. That flag is false while a Pi's 3V3 is already up in two
places:
- BENCH_RUNNING, for the 100 ms of late detection (the reviewer's case);
- POWERING_ON, before the rail settles (up to 250 ms; found in this round's audit).

1.10 changes those guards and nothing else in behaviour.

- **What changed from 1.9** (`captures/review_2026-11-12/v1_10/v1_10_from_1_9.patch`: 11 files
  changed, 1 new):
  - **`power_pi_maybe_present()`** (`power_fsm.c/.h`): the FSM's classification OR the 3V3
    sensed now. A disagreement counts as a Pi. The FSM's states, debounces and shutdown are
    unchanged.
  - **`reset` and `bootsel`** (`cli.c`, `reset_guard_ok()`): refused while a Pi may be present
    and the latch is closed. Before the Pi is classified, the refusal says so and says to
    wait for `stat` to show the Pi before `off`: in BENCH_RUNNING, an `off` inside the
    debounce is still a hard cut.
  - **`cfg save`'s guard** (`config_store.c`): the same input. The telemetry save and an
    update's install sit behind this guard, so a 3V3 sensed in STANDBY (a Pi powered some
    other way) holds them off too. The hint names the fix in each case.
  - **The 1.9 review's Q5 notes:** `strobe_plan.h` now states the during-firing exception, and
    the UNSETTLED refusal names the recovery from a lost 3V3 (`off`, then power up again).
  - Version 1.10. The minor is a number: the ROM, `id`/`upd` and `pitrac-link` all compare
    `(major << 16) | minor`, so 1.10 is above 1.9.
- **Tests: 298/298** (`v1_10/ctest_1.10.log`): 1.9's 292 plus 6.
  - **`strobe_fsm_test` gains `reset_late_pi`, `reset_powering_on` and `reset_settled`.**
    - They run the firmware's own `reset_guard_ok()`, `cmd_reset()`, `cmd_bootsel()`,
      `cfg_save_blocked_reason()` and `cfg_save_unblock_hint()` against the real FSM. The new
      `tests/extract_functions.py` copies them verbatim at build time.
    - They cover before, inside and after late detection; POWERING_ON; RUNNING after a lost
      3V3; STANDBY; and a glitch. They also check the refusal text.
  - **`power_fsm_test`** gains `maybe_present_power_up` and `maybe_present_late_pi`.
    **`config_store_test`** gains `sensed_pi_blocks_writes`.
- **The 1.9 reviewer's own probe, rerun on 1.10 source** (`v1_10/rp19/`, a copy; their folder
  is untouched). `reset_late_presence`, the V19-F1 witness, now fails at its first
  bad-admission check, `reset_guard_ok(1, reset, "reset")`. The other 12 pass, including 2614
  PIO runs.
- **Mutations: 25 of 25 caught by the tests, none by the build** (`v1_10/mutations_1_10.md`).
  M1–M17 are 1.9's. M18–M21 put 1.9's input back in the reset guard, the cfg guard, its hint
  and the helper itself. Each is caught by a test that runs the real FSM.
- **Build:** `build_v1.10/`, Release, version 1.10, TBYB, `built : Nov 12 2026 15:13:28`,
  246560 text / 0 data / 422684 bss.
  - `.bin` 242468 B, SHA256 `ECB0746B6179295BB79F000CCC4CAFCA211896049F56E17243A8ED392626029A`.
  - UF2 SHA256 `F5BF95F28435D541629C766CF9FB9D49F3B90957BA9EB012F5D78A58CFF48060`.
  - No warnings. `check_retained`: unchanged.
  - A second build of the tree after the mutation run differs only in the stamp's four time
    digits (`v1_10/fw_compare_1.10.log`).
- **Baselines:** I had not snapshotted 1.9 before editing. `baseline_1.9/` was rebuilt from
  `baseline_1.8/` and the 1.9 patch, and all 106 files equal the 1.9 reviewer's manifest
  (`v1_10/rebuild_baseline_1_9.log`).
- **I was wrong (2026-11-12), in the 1.9 brief's Q4.** I wrote that `cfg save`'s guard used the
  FSM's flag "on purpose, for power and shutdown semantics". It is a guard against acting
  under a powered Pi, and with the flag alone it was open for 0.1 s with a late Pi powered
  (V19-F1). I had not looked at the reset guard at all.
- **Not changed; asked of the reviewer (brief Q2).** The FSM's own button press or `off`
  inside the late-detect debounce is a FORCE_OFF: a hard cut to a Pi whose 3V3 rose under
  0.1 s earlier. Changing that would change the FSM's shutdown semantics, which the 1.9
  review asked to keep.
- **Review NOT PASSED:** `captures/review_2026-11-12/v1_10/INDEPENDENT_REVIEW_v1.10.md`.
  V19-F1's already-high-at-entry reproduction is closed, but V110-F1 leaves a 50 ms
  check-to-reboot window. Full V19-F1 closure is incomplete. Nothing flashed or committed.

### 2026-11-12 - Image 1.10 Sol 6.1 confirm review NOT PASSED: V110-F1 reset-delay window

Report: `captures/review_2026-11-12/v1_10/INDEPENDENT_REVIEW_v1.10.md`. New executable
evidence is in `independent_v110_probes/` beside it. Prior reports/probes remain untouched.

- **V110-F1 (HIGH/blocker):** the new helper fixes V19-F1 when 3V3 is already high at entry,
  but both actual reset commands check once, disarm the watchdog, print and sleep 50 ms,
  then reboot without resampling. The real FSM reaches POWERING_ON with the latch high
  and initial sense low. A modeled GPIO24 rise at 6 ms into that sleep leaves both plain
  `reset` and `bootsel` reaching their counting reboot stubs at 50 ms with raw3V3 high,
  cache false and latch asserted. Both commands also reproduce in BENCH_RUNNING.
  - The delay is deliberately software-created, not merely an asynchronous edge after a
    final sample. The reached production reset calls would open the latch; no physical
    reset, flash write, voltage disturbance or filesystem corruption was executed/measured.
  - Fix permission at the action point after the output wait. A late refusal must leave
    the operator's watchdog as it was: do not add a second guard after an irreversible
    early disarm without correcting that ordering. Preserve explicit force behavior.
  - Add both-command real-FSM tests for a rise during sleep in POWERING_ON and BENCH_RUNNING,
    plus a mutation proving the final check. Full V19-F1 closure remains incomplete.
- **Confirmed:** the entry-time helper and cfg guard/hints work; automatic telemetry and
  update installation use that exclusion. Prior strobe corrections and Q5 text fixes
  remain intact. The power FSM's own transition/shutdown behavior is unchanged.
- **Q1 accepted:** sensed 3V3 with the latch open conservatively blocks flash consumers;
  plain reset remains coherent with the latch already open. **Q2 nonblocking for the
  specified RUNNING-gated workflow:** an explicit early `off`/button request still cuts
  a sensed late Pi under existing BENCH semantics and may not count it in telemetry.
  Raw sense alone does not cause the cut. Do not claim universal graceful shutdown or
  complete cut accounting; a later FSM-policy change needs separate review/tests.
- **Test method accepted with limits:** the extracted command/guard bodies are verbatim
  current functions, generated with source dependencies and linked to the real FSM. A
  source-format contract and copying alone do not supply the missing timing scenario.
- **Independent probes:** 17 cases, including two explicit delay-defect witnesses and
  the late-off limitation witness, plus 2614 actual PIO executions. A green witness
  demonstrates the unsafe behavior, not that its safety requirement passed. The parent
  independently reproduced both commands in both FSM states, using counting stubs only.
- **Routine verification:** fresh host **298/298 PASS**; author and independent manifests
  agree on 107 current inputs; both patches reconstruct source (12 and 21 entries).
  Reconstructed 1.9 matches all 106 prior hashes; 1.7's 93 copied plus twelve omitted inputs
  are accounted for. A separate warning-free Release build differs only in four CLI time
  bytes. Retained ELF placement matches 1.7. Full record: the review folder's
  `independent_v110_verification.md`.
- **Preserved candidate:** `build_v1.10/`, Release / version 1.10 / TBYB,
  `Nov 12 2026 15:13:28`, 246560 text / 0 data / 422684 bss, BIN 242468 B.
  - UF2 SHA256 `F5BF95F28435D541629C766CF9FB9D49F3B90957BA9EB012F5D78A58CFF48060`.
  - BIN SHA256 `ECB0746B6179295BB79F000CCC4CAFCA211896049F56E17243A8ED392626029A`.

**Do not flash 1.8, 1.9 or 1.10.** §8.10's owner approval remains recorded, but its software
gate is unmet. No firmware source/image changes, hardware operation or commits occurred.
The previous claim that the entry-time fix fully closed V19-F1 was too strong; the command's
own delayed action still matters. Keep the startup workaround until a corrected image passes.

### 2026-11-12 - Image 1.11: independent confirm review PASS, V110-F1 closed

**The 1.10 review (NOT PASSED) found V110-F1.** `reset` and `bootsel` checked the guard,
disarmed the watchdog, then slept 50 ms and rebooted without checking again. A Pi raising
its 3V3 6 ms into that wait was cut, in POWERING_ON and in BENCH_RUNNING.

- **What changed from 1.10** (`captures/review_2026-11-12/v1_11/v1_11_from_1_10.patch`:
  4 files changed):
  - **Both commands now run in this order** (`cli.c`): the guard, the message and the 50 ms
    bare sleep, the guard again (`reset_still_ok()`), then the watchdog disarmed and the
    reboot. A refusal at the second check prints `Nothing was done: the Pi's 3V3 rose during
    the 50 ms wait` and leaves the watchdog as it was.
  - **No plain `reset`/`bootsel` in POWERING_ON**, with or without a 3V3 sensed: the FSM
    has not decided yet, and this is when a Pi raises its 3V3 (5.56 ms after +5 V, §8.3).
    The refusal says to wait for BENCH_RUNNING (up to 3 s after `on`).
  - **Why the wait stays a bare sleep:** with the FSM frozen, nothing can close the latch
    during it (a button press would, through a yield), so only the 3V3 can change before
    the second check. A 1 s watchdog fed just before the command cannot expire in 50 ms.
  - **What remains after the second check:** for `reset`, the instant before
    `watchdog_reboot(0, 0, 0)` triggers. For `bootsel`, also the boot ROM's own 10 ms
    inside `reset_usb_boot()` (SDK 2.3.0, RP2350), which nothing can check. The POWERING_ON
    refusal keeps that 10 ms out of power-up; in BENCH_RUNNING it needs a late Pi.
  - `help` and `id` say so. Version 1.11.
- **Tests: 299/299** (`v1_11/ctest_1.11.log`): 1.10's 298, plus `fsm.reset_rise_in_wait`.
  - A sleep hook raises the Pi's 3V3 6 ms into the commands' wait, as the 1.10 review's
    probe did.
  - Both commands are refused, with the watchdog kept armed or kept off.
  - Nothing rising: the reboot is reached, with the watchdog disarmed just before.
  - `force` still goes through.
  - `fsm.reset_powering_on` now expects a refusal in POWERING_ON with nothing sensed, and
    admission once BENCH_RUNNING.
- **The 1.10 reviewer's own probe, rerun on 1.11** (`v1_11/rp110/`).
  - Their files are copied unchanged, except the two commands embedded in `probe_body.c`,
    which `rp110_refresh_commands.py` replaces with 1.11's.
  - `sleep_rise_witness` now fails at its reboot count: nothing was reset.
  - `powering_sleep_witness` fails one check earlier: the command is refused at entry in
    POWERING_ON and never reaches its wait.
  - The other 15 pass, including `command_windows`, `late_off_witness` and 2614 PIO runs.
- **Mutations: 30 of 30 caught by the tests, none by the build** (`v1_11/mutations_1_11.md`).
  - M26 and M27 remove the second check from each command.
  - M28 and M29 move the watchdog disarm back before the wait.
  - M30 drops the POWERING_ON refusal.
- **Build:** `build_v1.11/`, Release, version 1.11, TBYB, `built : Nov 12 2026 15:42:51`,
  247536 text / 0 data / 422684 bss.
  - `.bin` 243444 B, SHA256 `883D4EFA56184782AEA7C8768121E556A9DE1D95CFE134871B682DD4CA1CFC31`.
  - UF2 SHA256 `E940AE6A21A0B86985075575E36C9EC894F04E129B52E183A5EC62A8F9B72991`.
  - No warnings. `check_retained`: unchanged. A second build after the mutation run
    differs only in two stamp digits (`v1_11/fw_compare_1.11.log`).
- **Baseline:** `baseline_1.10/` was snapshotted before any 1.11 edit: all 107 inputs, equal
  to my 1.10 manifest and to the 1.10 reviewer's.
- **I was wrong (2026-11-12), about 1.10.**
  - `BENCH_P8_PI.md` §8.10 said a plain reset could not catch a Pi in its first 0.1–0.3 s.
    The commands waited 50 ms after their check.
  - My tests copied the commands verbatim, but only ever put the 3V3 up before entry. I did
    not test it rising during the commands' own wait, which is the case the review found.
- **Bench check added to §8.10 Step 1:** `reset` and `bootsel` with `wdog on`, the Pi off in
  STANDBY. It confirms on silicon that `bootsel` still reaches BOOTSEL with the watchdog
  disarmed just before the ROM call, instead of 50 ms earlier.
- **Brief:** `captures/review_2026-11-12/v1_11/REVIEW_BRIEF_v1.11.md`. Nothing flashed, nothing
  committed.

### 2026-11-12 - Image 1.11 Sol 6.1 confirm review PASS: V110-F1 closed

Report: `captures/review_2026-11-12/v1_11/INDEPENDENT_REVIEW_v1.11.md`; independent source
copies, probes and mutations are in `independent_v111_probes/` beside it.

- **V110-F1 closed:** both commands perform entry guard, output/bare 50 ms sleep, final
  fresh guard, watchdog disarm and reboot, with no application wait/output/service after
  the admitted final check. Initial and late refusals retain watchdog state. POWERING_ON
  refuses with no Pi sensed as well as with one; force, open-latch STANDBY and settled
  no-Pi BENCH_RUNNING still work. V19-F1 closure is complete at the reviewed sampling points.
- **Independent confirmation:** 17 integration/PIO cases; a separate 20-combination
  matrix (both commands × watchdog on/off × five states/timing conditions), force after
  refusals, and five newly built copy-only mutations all caught. Restored copies pass.
  The actual assembled-PIO model runs 2614 complete/truncated finite-feed cases low at
  completion/stall. Host models do not prove physical reset, BOOTSEL or analog U5 behavior.
- **Scope/limits accepted:** the POWERING_ON refusal is conservative and appropriate.
  Installed SDK 2.3.0 confirms delay-zero reset's trigger path and BOOTSEL's 10 ms ROM
  request. Later asynchronous sense changes/interrupts are not excluded by a sampled
  guard. No continuous no-cut guarantee or silicon timing bound is claimed.
  - Manual/operator-owned watchdog output can exceed one second under USB trickle/stall;
    a feed before the command proves only that the bare 50 ms sleep fits, not that all
    printing/refusal is bounded. Do not arm the manual watchdog with a powered Pi based
    on this PASS. The approved physical regression deliberately uses STANDBY/Pi off.
  - The already documented explicit early-off/cut-accounting limitation is unchanged,
    nonblocking for the prescribed workflow that waits for classified RUNNING.
- **V111-N1/N2, LOW/nonblocking:** qualify watchdog timing prose as bare-sleep-only and
  describe the late refusal as presence changed after the first check. A rise during
  attempt-message output, before sleep, is correctly refused too, so its exact "during
  the 50 ms wait" attribution is too narrow. Correct wording in a future image; preserve
  the reviewed 1.11 source/image identity.
- **Parent corroboration:** fresh host suite **299/299 PASS**; both patches reconstruct
  current source (4 / 21 entries). All 107 baseline 1.10 input hashes match; 1.7's 93
  copied and twelve unchanged omitted inputs are accounted for. Author and independent
  manifests agree on all 107 current inputs. A separate warning-free Release BIN differs
  only in three CLI time bytes; retained ELF placement agrees with approved 1.7. Evidence:
  the review folder's `independent_v111_verification.md` and named logs.
- **Exact accepted software candidate:** `build_v1.11/`, Release / version 1.11 / TBYB,
  `Nov 12 2026 15:42:51`, 247536 text / 0 data / 422684 bss; BIN 243444 B.
  - UF2 SHA256 `E940AE6A21A0B86985075575E36C9EC894F04E129B52E183A5EC62A8F9B72991`.
  - BIN SHA256 `883D4EFA56184782AEA7C8768121E556A9DE1D95CFE134871B682DD4CA1CFC31`.
- **Owner next:** reflash first by §8.10 Step 1's Pi update, then its real-chip reset and
  BOOTSEL checks with the watchdog armed, in verified STANDBY/latch open/Pi off. This is
  sufficient for the narrow disarm-timing regression; still unrun. Then the approved
  live Pi/strobe rail measurements. Board 1's last recorded image remains 1.7 in B; 1.11
  will target A and keep 1.7 as fallback. 1.8–1.10 remain rejected.

No firmware source/image or prior evidence edits, hardware operations, flashing, actual
resets or commits occurred. Documentation now records software PASS and pending physical
acceptance. The owner's existing approval for §8.10 remains in effect.

### 2026-11-12 - Board 1: the strobe with a Pi present (`BENCH_P8_PI.md` §8.10), image 1.11: PASS

**State:** the Pi seated (SW2 taped, its USB-C unplugged, J4 empty, beam off); PSU 5.20 V,
limit ≥ 5 A; J3 off for Step 1, on from Step 2. Saleae Logic Pro 8, analog, 50 MS/s: Channel 2
on J7.5 (the +5 V the Pi runs from), Channel 3 on TP4 (135 mV/A).

**Logs:** `captures/p8_2026-11-12_8.10/`: the COM5 console, the Pi and PC terminal, and
`p810_analyse.py` with its output (`p810_idle.txt`, `p810_load.txt`). The Saleae exports stay
in `...\Downloads\PiIdle_*` and `PiLoad_*` (~150 MB).

- **Step 1, the update from the Pi: PASS.**
  - Before: `id` 1.7 (partition B), `Nov 11 2026 16:41:14`; `cfg` slot A seq 57, threshold 0.
  - `pitrac-link update pitrac_v1.11.bin`: `243444 B, version 1.11, try-before-you-buy,
    SHA-256 883d4efa56184782`; `board : version 1.7 ... installs into 0x002000`; no `NOTE`;
    sent in 3.5 s, 0 retries.
  - The board: `installing into the other partition`, then `installed and verified;
    rebooting into it on trial`.
  - After:
    - `id` → `version : 1.11 (partition A at 0x002000)`, `built : Nov 12 2026 15:42:51`, UID
      `a764f5332ca5ac53`;
    - `upd` → `flash update boot, ON TRIAL: bought itself`, `partner : version 1.7, bought:
      the fallback`, `installed, 1.7 -> 1.11`;
    - the Pi agrees (`upd-status`, `info`);
    - `hist`: boots 23, requests and in time 36, unrequested 5, cuts 2, faults 0;
    - `cfg` unchanged;
    - `strobe` → `rail : up (state RUNNING)   fault none   pi PRESENT`.
- **Step 1b, `reset` and `bootsel` with the watchdog armed (Pi off): PASS.**
  - `stat` first: STANDBY, `latch : 0`, `3v3 0`, watchdog off.
  - `wdog on`, `reset` → `resetting...`, the port dropped and came back; `wdog` → off; `id`
    1.11.
  - `wdog on`, `bootsel` → `rebooting to BOOTSEL...`. `picotool info` found the board in
    BOOTSEL twice, both partitions listed (1.11, 1.7). Then `picotool reboot` → `id` 1.11,
    `wdog` → off.
  - `hist` → boots 25, requests 37, cuts 2, faults 0; `this boot : software reboot`; `loaded :
    flash (power-on)` (the BOOTSEL session overwrote the RAM mirror); `flash : up to date --
    slot B`.
  - The gap between the two `picotool info` runs is not in the log (5 s asked for).
- **Step 2:** `on` → RUNNING: `pi : present 1  3v3 1  userspace 1`; 5V_IN 5.195 V;
  `throttled=0x0`.
- **Step 3, Pi idle (live):**
  - `strobe live on confirm` → `LIVE: ARMED ... Watchdog NOT armed: a Pi is powered, and a
    reset would cut its power.`
  - `strobe cal 9` → level 439, mean 9.006 A, `PASS`; session cap 463. The same as without a
    Pi.
  - ADC0:
    - `pulse 20`: 9.004 A;
    - `pulse 100`: 9.047 A;
    - `sched 50` (10 × 20 µs): 9.007–9.017 A;
    - `sched 10` (6 × 100 µs, shed from 10): 9.013–9.048 A.

    All `within limits`.
- **Step 4, the first loaded run fired dry.**
  - More than 300 s had passed since the last strobe command. So `strobe pulse 20` printed
    `*** LIVE MODE ENDED: idle timeout ***`, and the four shots fired dry, at gate 0, with no
    LED current. That is 1.8–1.11's dry rule with a Pi present, working as designed.
  - Re-armed: `strobe cal 9` → 439 again, mean 9.009 A, `PASS`.
  - Then **two sets** of the four live shots (console lines 717–811 and 815–911). ADC0 read
    9.006–9.055 A throughout, `within limits`.
- **The rail, J7.5, as read** (a 1 µs mean envelope, as on 2026-10-16; raw sample in
  brackets):

  | Capture | Before | Lowest | Under 5.000 V |
  |---|---|---|---|
  | Idle `pulse 20` | 5.124 V | 4.891 V (4.885) | 0.22 ms |
  | Idle `pulse 100` | 5.145 V | 4.919 V (4.911) | 0.24 ms |
  | Idle `sched 50` | 5.140 V | 4.888 V (4.880) | 0.18–0.22 ms after each of 10 pulses |
  | Idle `sched 10` | 5.140 V | 4.907 V (4.901) | 0.26 ms, first pulse only |
  | Load `pulse 20` | 5.130 V | 4.911 V (4.901) | 0.21 ms |
  | Load `pulse 100` | 5.140 V | 4.915 V (4.906) | 0.23 ms |
  | Load `sched 50` | 5.135 V | 4.897 V (4.890) | 0.19–0.22 ms after each of 10 pulses |
  | Load `sched 10` | 5.135 V | 4.909 V (4.901) | 0.24 ms, first pulse only |

  - **The lowest point anywhere is 4.880 V as read, ≥ 130 mV above the 4.75 V line.**
  - **The analyser reads low.** On 2026-10-16 it read ~87 mV under the DMM. Today its 5.12–5.15 V
    before the shots sits under the board's own 5V_IN, 5.195–5.205 V, which is measured
    upstream of the latch. So the true minimum is between 4.88 V and ~4.95 V. No DMM reading
    was taken on J7 this session.
  - **The same shape as without a Pi, and smaller.** Dips are 0.22–0.26 V, against
    0.30–0.33 V without a Pi on 2026-10-16. The deepest point is 155–190 µs after the
    pulse's start, and the rail is back above 5.0 V by ~0.25 ms. The PSU limit is ≥ 5 A now
    (3 A then), which is a likely reason; it is not established.
  - **`sched 10`:** one dip, at the first pulse only, then none: the boost's hiccup, as
    without a Pi. Its restart (~105–137 ms on 2026-10-16, −20 to −64 mV) falls outside every
    capture window; the longest is 43.8 ms.
  - **Load vs idle:** no difference in the dips. The load captures show 9 A on TP4, so they
    are live shots, not the dry run.
  - **TP4 on the Saleae:** plateaus 8.8–9.4 A against ADC0's 9.00–9.06 A, within ~4 %. On
    the ±10 V range one LSB is ~37 mA, and the ripple is visible; on 2026-10-16 the two
    agreed within ~1 %.
- **Step 5:**
  - Pi: `throttled=0x0` after the dry run and again after the first live set. The flag
    bits are sticky, so that covers everything up to then.
  - Pi: `dmesg` showed no voltage or brown-out lines. The 1000 pings printed no `FAIL`.
  - Board: `stat` → fault none, `pi : present 1`, cuts 2, faults 0; it was taken after the
    dry run.
  - Board: `link` → `CONNECTED`, `0 CRC errors`. It was taken before the pings (`rx 0 B`),
    so it does not count them: see "I was wrong" below.
- **Step 6:** `strobe live off` → `live OFF: gate 0, engine stopped, GPIO25 low. Watchdog now
  off.`; `off` → `requested shutdown`.
- **Against §8.10's pass list:**
  - CH1 ≥ 4.75 V: **PASS** in all eight captures.
  - `throttled=0x0` before and after: PASS through the first live set.
  - No fault, and the Pi up throughout: PASS as far as logged.
  - Live mode ended only by its idle timeout and `strobe live off`: PASS.
  - The 1000 pings clean: PASS.
- **The record items, closed the same day (owner):**
  1. **The `PiLoad_*` exports are the third run** (console lines 815–911), with the load on.
     The owner thinks `yes` was not running during the second run (717–811). The Pi's log
     fits that: its third `yes` loop was still running when `off` shut the Pi down. So the
     second run counts as one more idle set.
  2. **After the final run:**
     - Board, the same boot (boots still 25): `hist` → requests and in time 38 (the final
       `off`), timed out 0, unrequested 5, cuts 2, faults 0; `flash : up to date -- slot A`.
       So no power cut, no unrequested Pi halt and no fault in the whole session.
     - Pi: `journalctl -k -b -1 | grep -i -E "voltage|brown"` returned nothing, so there
       are no under-voltage lines in the session boot's kernel log. That covers the third
       run, which the last `throttled=0x0` did not.
  3. **The FLIR:** no change in any temperature (owner).
  4. **A DMM on J7.5: 5.201 V.**
     - The analyser read 5.124–5.145 V there before the shots, so it reads 56–77 mV low
       (~87 mV on 2026-10-16).
     - Each capture's dip on the analyser, taken from the DMM's 5.201 V, gives the true
       lowest points: **≈ 4.94 V idle** (`sched 50`, a 0.260 V raw dip) and **≈ 4.96 V
       loaded** (`sched 50`, 0.245 V).
     - That is ~190 mV above the 4.75 V line, and ~0.3 V above the Pi 5's ~4.63 V
       under-voltage flag.
- **§8.10: PASS.**
- **`link`, the same boot, after the pings:** `frames : 1000 good, 0 CRC errors, 0 cut short,
  0 junk bytes`; `requests : 1000 answered`; `traffic : rx 6000 B, tx 12000 B`.
- **I was wrong (2026-11-12):** §8.10 Step 5, and my step list for it, put the board's `link`
  check before the Pi's 1000 pings, so its CRC count could not include them. Step 5 now runs
  the pings first; Step 4 now confirms live mode before the loaded shots.
- **Board 1 now:** 1.11 in A, bought; 1.7 in B, bought, the fallback.

### 2026-11-13 - Phase 7 on board 1: the plan, the 7a/7b leads, and a wrong check in Step 13.4

- **The owner's decisions:**
  - Phase 7 runs on **board 1**, on 1.11 with no reflash. Its optical trigger is dead, and
    Phase 7 does not use it: every shot starts from the CLI. Board 5, the owner's last fully
    working board, stays free.
  - **7b's generator: a spare Pico running `tools/pulsegen`.** The owner asked about using the
    board's own chip through J5 or J7. Not chosen:
    - J7 cannot: pins 1 and 5 are the switched +5 V, 2 and 6 the panel LEDs' transistor
      sinks, and 3 the power button (GPIO14 behind 1 kΩ / 100 nF).
    - J5 could carry it electrically (GPIO4 on J5.5 and GPIO5 on J5.3, each through 220 Ω;
      the mic has no firmware yet). But no command makes the pulses. It would need a new
      image with a PIO program in PIO0, beside the strobe engine. That program would answer
      the camera handshake itself, the check 7a Step 3 proves, and the image would need its
      own review. It would also share the handshake's clock, so every shot would land at the
      same point in the 33 ns poll.
    - The Pi 5 was ruled out on 2026-11-12 (§10).
- **The 7a/7b leads changed (`BENCH_P5_P7_MIC_CAMERA.md`).** The trigger has two pins, J4.3
  and J4.4. 7a's two jumpers took both, and J4.7 too, where the analyser was also meant to go.
  A pin under a lead's housing cannot be probed.
  - 7a: J4.3, J4.7 and J4.8 into one breadboard row; the analyser on J4.4. J4.7 is no longer
    probed in 7a: there it is the trigger's own wire, and `pins` and t_cam prove GPIO8/9 see it.
  - 7b: the generator's trigger lead moves to J4.3 (`tools/pulsegen`'s comment and banner say
    so; both UF2s rebuilt). The analyser on J4.4, and on the strobe node at the generator's
    end of the J4.7 lead.
- **7c is not yet allowed.** It needs:
  - the owner to lift two standing rules for 7c: "a Pi on J8 only for Phase 8" and "no camera
    on J4";
  - a TXS0108E module wired as in `BRINGUP_NEW_BOARD.md` Step 13.4;
  - the Mira220 in external-trigger mode on the Pi, with its strobe output on. Nothing in this
    repo does that yet (`Software/camera-comparison` runs the sensor free-running).
- **I was wrong (2026-11-11):** `BRINGUP_NEW_BOARD.md` Step 13.4 asked for a 1.8 V high on the
  module's A1 "with the module wired and no camera". VA comes from the camera's 1.8 V, so with
  no camera that side is unpowered and A1 cannot go high. Fixed. **Board 4's shipped copy still
  has the old line**: its collaborator needs the correction.
- Also fixed: stale Phase 7 lines that called the camera schematic unknown and 1.9's review
  pending (`BENCH_P5_P7_MIC_CAMERA.md`, `DEVELOPER_GUIDE.md`, §5), and the pointer in
  `CLAUDE.md`/`AGENTS.md` that still sent Phase 7 to Board 5.

### 2026-11-13 - Board 1: Phase 7a Steps 1 and 2 PASS (image 1.11)

- **Setup:**
  - Board 1 on 1.11 (`built : Nov 12 2026 15:42:51`, partition A), with no reflash.
  - No Pi, no camera, beam off, J3 disconnected. J4.3, J4.7 and J4.8 are in one breadboard row.
  - The PSU is at 5.20 V; `stat`: `BENCH_RUNNING`, 5V_IN 5.200 V, fault none.
  - The Saleae runs at 500 MS/s, 3.3 V logic: Channel 3 on J4.4 (the trigger), Channel 2 on
    GPIO25 at R33 (the burst), ground on J4.5. The channels are told apart by their waveforms.
- **Step 1 PASS.**
  - `cam`: PIO1 SM0, program 3..31 (29 instructions), stopped, PC 3 (load D); gate PC 20
    (gate 19, burst 20..31); FIRE flag 0.
  - `gpio 10 1` → `CAM_STROBE_0(8) 1`, `CAM_STROBE_1(9) 1`, `CAM_TRIGGER(10) 1`;
    `gpio 10 0` → all three 0.
- **Step 2 PASS: three `cam shot`s** at the defaults: first delay 200 µs, timeout 5000 µs, and
  the dry S15 burst at 70 m/s.
  - The console, the same for all three: `FIRED: handshake complete, dry burst complete`,
    `t_cam = 0.040 us`, `FIRE at 199.993 us`, `trigger released at 5764.987 us`, and the
    trigger pad low afterwards.
  - The Saleae:

    | Shot | J4.4 high | First GPIO25 rise after J4.4 | Last GPIO25 fall to J4.4 falling |
    |---|---|---|---|
    | 1 | 5764.986 µs | 204.280 µs | 56.7 µs |
    | 2 | 5764.988 µs | 204.436 µs | 56.6 µs |
    | 3 | 5764.988 µs | 204.748 µs | 56.3 µs |

  - In every shot:
    - 10 pulses, 14.000–14.002 µs wide and 609.998–610.000 µs apart;
    - no GPIO25 edge before the trigger.
  - The first edge is FIRE + 4.29 to 4.76 µs: the burst engine's 1 MHz start, as designed
    (4–5 µs).
  - The analyser's trigger length agrees with the console's to one sample (2 ns).
- **Captures:** `captures/p7a_step2_shot1_2026-11-13.csv`, `_shot2_` and `_shot3_` (the
  Saleae's digital exports).
- **Still to run:** 7a Step 3 (a timeout fires nothing), 7a Step 4 (the refusals), and 7b.

### 2026-11-13 - Board 1: Phase 7a Steps 3 and 4 PASS; 7a complete but for one repeat

- **Step 3 PASS: the 1000 µs timeout, with the J4.8 lead out.**
  - `cam shot 200 70 1000` gave:
    - `CAM_TIMEOUT: trigger dropped, nothing fired, fault latched`;
    - `Trigger dropped at 999.987 us`, and the engine words `80000000 00000000`;
    - `strobe gate: NEVER OPENED -- ... Nothing reached GPIO25.`;
    - and `fault = CAM_TIMEOUT`.
  - The Saleae: J4.4 high 999.994 µs; GPIO25 low through the whole capture.
  - **Not run:** the repeat at the 5000 µs default timeout. The last `stat` agrees: `faults 1`,
    up from 0 at Step 2.
- **Step 4 PASS, with the J4.8 lead back.**
  - `cam trig`: `HANDSHAKE: complete, no burst asked for`, t_cam 0.040 µs, FIRE at 199.993 µs,
    released at 1199.987 µs. The Saleae: J4.4 high 1199.998 µs; no GPIO25 edge.
  - `cam`: `FIRE flag (PIO0 irq 4) 1`. The strobe engine is at PC 19 (the gate, where Step 3's
    timeout left it), with no burst armed.
  - With GPIO10 high: `cam shot` → `REFUSED: nothing was driven`, `D_Cam_Trigger (GPIO10) is
    already high`.
  - With the rail down (`stat` STANDBY):
    - `cam shot` → `REFUSED`, `+5V rail not ready`;
    - `gpio 10 1` → `REFUSED: the +5 V rail is not up`;
    - `pins` → `CAM_TRIGGER(10) 0`.

    No analyser capture was saved for these.
- **The analyser and the console agree** on every trigger length within one 33 ns loop, the
  console's own resolution:

  | Shot | Saleae | Console |
  |---|---|---|
  | Step 2 shots | 5764.986–5764.988 µs | 5764.987 µs |
  | Step 3 timeout | 999.994 µs | 999.987 µs |
  | Step 4 `cam trig` | 1199.998 µs | 1199.987 µs |
- **The J4.8 lead was moved with the chip powered.**
  - The evidence: the shot numbers ran on, #4 in Step 3 and #5 in Step 4, and they restart at #1
    only at a boot. Steps 3 and 4 asked for the PSU and USB off.
  - Step 3's lead came out after `off`. Step 4's went back with the rail up, as far as the log
    shows.
  - No harm was possible:
    - nothing on J4 is above 3.3 V;
    - GPIO9 was low when its lead came out (E9 needs a pin released from high);
    - a latched-high GPIO9 would have refused the shot, not passed it.

    The results stand.
  - 7b's wiring must be done unpowered, with the Pico's power order.
- **Captures:** `captures/p7a_step3_timeout1000_2026-11-13.csv` and
  `captures/p7a_step4_cam_trig_2026-11-13.csv`.

### 2026-11-13 - Board 1: Phase 7b PASS (a Pico 2 running `tools/pulsegen`)

- **Setup:**
  - Board 1 on 1.11, power-cycled for the wiring: its shot numbers restart at #1.
  - The 7a leads are off. A Pico 2 (150 MHz, 6.7 ns cycles) runs `tools/pulsegen`, wired as in
    7b: GP15 ← J4.3, GP16 → J4.7, GP17 → J4.8, GND → J4.2 and J4.6.
  - The Saleae runs at 500 MS/s: Channel 3 on J4.4, Channel 2 on GPIO25 (R33), and Channel 1 in
    GP16's row (the strobe node). The channels are told apart by their waveforms.
- **Steps 1 and 2 PASS.**
  - 17 shots on the board: 4 at 50 µs, 3 at 150, 3 at 300, 4 at 200 and 3 at 201. The analyser
    has 3 at each delay.

  | Pico delay | Console t_cam | Analyser J4.4 → strobe | t_cam − analyser | Branch | FIRE | First GPIO25 rise after J4.4 (console's window) |
  |---|---|---|---|---|---|---|
  | 50 µs | 50.073 µs | 50.010–50.016 µs | +0.057 to +0.063 µs | within | 199.993 µs | 204.58–204.84 µs (204.0–205.0) |
  | 150 µs | 150.073 µs | 150.012–150.018 µs | +0.055 to +0.061 µs | within | 199.993 µs | 204.61–204.74 µs (204.0–205.0) |
  | 300 µs | 300.060 µs | 300.010–300.016 µs | +0.044 to +0.050 µs | AFTER | 300.147 µs | 304.52–304.76 µs (304.1–305.1) |
  | 200 µs | 200.060 µs | 200.012–200.016 µs | +0.044 to +0.048 µs | AFTER (late) | 200.147 µs | 204.20–205.08 µs (204.1–205.1) |
  | 201 µs | 201.060 µs | 201.012–201.016 µs | +0.044 to +0.048 µs | AFTER | 201.147 µs | 205.64–205.68 µs (205.1–206.1) |

  - In every shot:
    - 10 pulses, 14.000–14.002 µs wide and 609.998–610.000 µs apart;
    - no GPIO25 rise before the strobe;
    - the strobe low at the trigger's rise;
    - the burst's first edge 4.0–4.9 µs after FIRE;
    - the trigger released at FIRE + 5565 µs, the analyser within 5 ns of the console.
  - In the AFTER shots:
    - FIRE = t_cam + 0.087 µs, with `(the delay clamped to zero -- no underflow)`;
    - the D word went negative (`fffdd93b`, `fffdcd83`, `fffdcda1`), and nothing wrapped.
  - **t_cam always reads 44–63 ns above the analyser.** That is the 2-cycle synchroniser plus
    where the strobes land in the 33 ns poll. 7a's loopback showed the same: 0.040 µs for a wire.
  - **Every shot at one delay gave the same t_cam.** Both PIO programs start from the same
    trigger edge, so the strobes land at the same point of the poll each time; pulsegen adds
    one 6.7 ns cycle of jitter. So the 200 µs boundary came out late in all four shots. Either
    is a pass.
- **Open: a stray rising edge on J4.3 while board 1 powered up.**
  - The Pico counted a shot (its "shot 1", at its default 200 µs) after `?` had shown
    `0 shots, trigger now 0`, and before `d 50`. The board's first `cam shot` came after `d 50`.
  - So J4.3 rose once while the board was powered or went `on`, unless a lead was moved with
    the Pico powered. There is no capture.
  - **The trigger has no external pull-down.** The netlist puts only U3 (GPIO10) and R18 on
    `D_Cam_Trigger`, and only J4.3/J4.4 on R18's other side. Until `safe_state_init()` drives
    GPIO10 low, the net is held only by the two chips' internal pull-downs, which erratum E9
    can weaken.
  - **It cannot fire the strobe:** no burst is loaded at boot, and the gate opens only on a
    handshake's FIRE. With a camera, it would start one exposure the Pi did not ask for.
  - **Next:** a power-up in steps, with the Pico counting and the analyser on J4.4 (§10). If the
    board makes the edge, a pull-down on J4.3's side goes to `NEXT_BOARD_REV.md`.
- **I was wrong, twice, about 7b's tooling and criteria (2026-11-12 and 2026-11-13):**
  - **`tools/pulsegen`'s accuracy.** I described its outputs rising "to one PIO cycle after the
    set delay". The analyser measured 10–18 ns after it, J4.4 → GP16 on a Pico 2: 1.5–2.7
    cycles, about one cycle of it jitter, the rest a fixed latency that the 5-cycle allowance
    does not cover. The analyser's delay is the reference for t_cam, so no result changes.
    Fixed in `pulsegen.c`'s comment (the UF2s are unchanged and not rebuilt), the 7b section
    and `DEVELOPER_GUIDE.md`.
  - **7b's exit criterion.** It put the burst at "max(first delay, t_cam) + 4–5 µs". In the
    AFTER branch, FIRE comes 0.087 µs after t_cam, so by that wording one shot (200 µs, shot
    2: t_cam + 5.016 µs) is 16 ns outside. The real criterion is the step's own: FIRE + 4–5 µs,
    which the firmware prints as its window. It came out 4.93 µs. The exit criterion now says
    FIRE + 4–5 µs.
- **Captures:** `captures/p7b_step1_d50_2026-11-13.csv`, `_d150_`, `_d300_`, and
  `captures/p7b_step2_d200_d201_2026-11-13.csv` (shots 1–3 at 200 µs, 4–6 at 201 µs).
- **7a Step 3's repeat at the 5000 µs default timeout is still not run.** pulsegen cannot
  answer later than 4000 µs, so it needs the 7a leads.

### 2026-11-13 - The stray power-up trigger is the Pico 2's, not the board's; the owner allows 7c; the Mira220 driver has no external trigger

- **Where the stray shot came from** (board 1, the 7b wiring, the Pico 2 powered and counting).
  - The owner powered the board in steps, with `?` on the Pico after each. The shot came at
    **PSU on**, with the board's USB still out.
  - The Saleae, digital and analog on J4.4:
    - 0 V before.
    - At plug-in, ~25 µs of ringing on every channel, with peaks to 4.3 V on J4.4: the PSU
      plug's transient on an undriven node.
    - Then **2.11 V, flat, for ~107 ms**, and **1.60 V** until 140.6 ms.
    - Then **0 V**: `safe_state_init()`, the first line of `main()`, driving GPIO10 low.
    - The Pico's strobes rose 201.0 µs after the plug-in and held 10 ms.
  - **The board never drove the trigger.** After the ringing the node never passed 2.55 V,
    nowhere near a driven 3.3 V.
  - **The 2.11 V is erratum E9, on the Pico 2's GP15.**
    - The RP2350 resets GPIO10's pad with its input buffer off, the pull-down on and the pad
      isolated (`PADS_BANK0_GPIO10_IE_RESET 0` in SDK 2.3.0; the RP2040 resets it on).
    - E9 needs the input buffer on, so the board's pad cannot hold the node up. pulsegen's
      GP15 is an input with its pull-down on: the E9 case.
    - The transient lifted the node, and GP15's leakage held it near 2.1 V against the two
      weak pull-downs.
    - Why it stepped to 1.60 V at ~107 ms, before `main()`, is not known.
  - **So the board's boot leaves J4.3 undriven for 140.6 ms after power-on,** held only by
    GPIO10's internal pull-down.
    - With a camera this does not matter: the camera, and the module's 1.8 V side, run from the
      Pi, and the Pi from the switched rail, which is down through every boot.
    - Noted in `NEXT_BOARD_REV.md` CR-09 for the on-board translator.
  - **For 7b:** expect one pulsegen shot at the board's PSU-on. It is harmless: the strobes are
    low again 10 ms later, 130 ms before the firmware starts. Added to the 7b section.
  - Captures: `captures/p7b_extra_shot_psu_2026-11-13_digital.csv`, and
    `captures/p7b_extra_shot_psu_2026-11-13_analog_summary.txt` (the 710 MB analog export,
    reduced to minimum, maximum and mean per window; the export stays in the owner's Downloads).
- **The owner lifts both rules for 7c (2026-11-13, §8):** a Pi on J8 and a camera on J4, the
  camera only through the TXS0108E module. Changed in `CLAUDE.md`/`AGENTS.md`, `HANDOFF.md`
  rules 3 and 6, §1 here, `DEVELOPER_GUIDE.md`, `ARCHITECTURE.md` and the 7c section.
- **The Mira220's external-trigger mode has not been tried on the Pi (owner).** The installed
  driver cannot do it. Read from its source on 2026-11-13: ams-OSRAM `mira220_v4l2_driver`, at
  the commit `install-mira220.sh` pins (`aeedd65`).
  - It runs the sensor free-running as master.
  - It defines `MIRA220_IMAGER_STATE_SLAVE_CONTROL` but never writes it.
  - ~~It writes `EXT_EVENT_SEL` and `EXT_EXP_PW_SEL` as 0 at init.~~ **Wrong** (found
    2026-11-16, see that entry): it writes `0x1001 = 0x41`, both bits set. The "(0)" in its
    comments is a field index, not the value.
  - It has no trigger control and no module parameter.
  - It does enable the illumination-trigger output at init (`0x10D7 = 0x1`, `0x10D8 = 0x2`).
    So the camera's strobe should pulse once per frame while it streams.
- **So 7c splits:**
  - **7c.0** (written today) runs now: the module, the trigger's level at the camera, and the
    strobe path, with the camera free-running.
  - **7c.1 on** needs the sensor in external-trigger mode: a driver change, or the owner's Mira
    register utility, from the datasheet's register descriptions (DS000642, kept outside this
    repo).
- **Also fixed:**
  - `HANDOFF.md` rule 3 still called 1.11's hardware acceptance pending (§8.10 passed
    2026-11-12).
  - The same stale line was in §8's 2026-11-12 entry.
- **Next image:** the firmware's `cam`, `cam shot` and `cam trig` still print
  `J4: NO CAMERA (CR-09)`. That is out of date from 7c; it changes with the 1.11 review's
  V111-N1/N2 notes.

### 2026-11-16 - Upstream PiTrac switches a streaming Mira220 to slave mode; what to port; I was wrong about 0x1001

- **The source** (the owner's pointer): `PiTracLM/PiTrac`, branch `feature/pico`, at `f198139`
  (2026-10-16).
  - The Mira220 work is commit `afe689e` (2026-10-10, "mira220 as camera 2…").
  - Read on 2026-11-16 into a scratch folder; nothing from it is in this repo.
  - The commit does not record a test on hardware. The owner believes it works.
- **What it does:** `Software/LMSourceCode/ImageProcessing/CameraTools/mira220_trigger <bus>
  <0|1>`, 31 lines of bash. It uses `i2ctransfer -f` at address 0x54, while the stream runs.
  - It reads 0x1001.
  - It stops the imager at a row boundary (`0x1003 = 0x02`, `0x10F0 = 0x00`), then waits 40 ms.
  - **Slave:** it writes 0x1001 with bits 6 and 0 cleared, then `0x1003 = 0x08`.
  - **Master:** it sets both bits, then writes `0x1003 = 0x10`.
  - Then it restarts the imager with `0x10F0 = 0x01`.
  - Its comment names the bits:
    - bit 6 is `EXT_EVENT_SEL`; when set, REQ_FRAME starts readout;
    - bit 0 is `EXT_EXP_PW_SEL`; when set, EXP_TIME sets the exposure length.
  - With both cleared, the sensor exposes while REQ EXP is high, and the falling edge starts
    readout.
  - **Upstream's C++** calls it once after each stream start, with 500 ms allowed for the switch.
    It never switches back: the next stream start does that.
  - **Polarity:** the Mira220's REQ EXP is active high; the IMX296's is active low. Our handshake
    already drives the trigger high.
- **Checked against the driver we pin (`aeedd65`):**
  - Stream start writes `0x1003 = 0x10` (master), `0x1002 = 0x04` (continuous) and
    `0x10F0 = 0x01`.
  - Stream stop writes `0x1003 = 0x02` and `0x10F0 = 0x00`.
  - Runtime PM powers the sensor down 1 s after a stream stops (autosuspend 1000 ms). Then it
    does not answer on I²C (measured 2026-11-18; this line first said "when it is not
    streaming").
  - The address is 0x54 (`mira220.dtsi`).
  - The init table writes **`0x1001 = 0x41`**.
  - So the switch has to follow every stream start, as upstream does.
- **I was wrong** (the 2026-11-13 entry above): I wrote that the driver writes `EXT_EVENT_SEL`
  and `EXT_EXP_PW_SEL` as 0.
  - It writes 0x41, which sets both bits. Its comments read `EXT_EVENT_SEL(0)`; the "(0)" is a
    field index, and I took it for the value.
  - **What stands:** the driver alone gives no trigger mode.
  - **What changes:** 7c.0 Step 2 expects REQ EXP to be ignored because the sensor is in master
    mode, not because of those bits.
  - Corrected in that entry, in `BENCH_P5_P7_MIC_CAMERA.md` (the 7c Pi software row, 7c.0 Step 2
    and 7c.1) and in §0, §5 and §10.
- **Worth porting:**
  - **The register sequence.** It is the whole of 7c.1's "external-trigger mode". Upstream is
    GPL-2.0-only and this repo is GPL-3.0-or-later, so we write our own tool from the register
    facts, checked against the driver, and credit upstream. Copying their file would need the
    author's permission.
  - **The lessons for operating it:**
    - switch after every stream start;
    - keep frames coming, or the Pi's capture may time out (upstream fires 12 priming triggers
      at 15 Hz);
    - allow time for the switch.
- **Not worth porting:**
  - **Their Pico firmware.** It fires the strobe a fixed time after the trigger
    (`CAM_XTR_SETUP_US`, 1000 µs by default) and never reads ILLUM TRIG. Our handshake waits for
    the sensor's strobe and measures `t_cam`.
  - **The Arducam module path** (`arducam_trigger`, a V4L2 `trigger_mode` control) and their
    1.5 kΩ / 1.8 kΩ divider. We use the ams board and the TXS0108E.
  - **`mira220_mono.json`,** a pisp tuning file for the mono sensor. Perhaps later, for the Pi
    software; not for 7c.
- **Still unknown; 7c.1 measures it:**
  - **Whether ILLUM TRIG pulses in slave mode, and when.** Our handshake waits for it. Upstream
    does not use it. The driver's illumination settings (`0x10D7 = 0x1`, `0x10D8 = 0x2`) are not
    touched by the switch. DS000642 would answer this in advance; the bench answers it anyway.
  - **The delay from trigger to exposure,** which is our `t_cam`.

### 2026-11-16 - 7c.1 written: the Pi tools for a triggered Mira220, and the procedure

- **Asked by the owner** while away from the bench: start everything possible for 7c.1 and
  beyond. No firmware changed; board 1 stays on 1.11.
- **Two Pi tools, in `Software/camera-comparison/scripts/`** (`TRIGGER.md`):
  - **`mira220-trigger.sh status|slave|master`:**
    - It makes the register switch (§6, the entry above) at 0x54, on the bus named in the
      configured entity (`mira220 <bus>-0054`).
    - It reads 0x1001 back, and fails if bits 6 and 0 are not as written.
    - If the imager may have been left stopped, it says so.
    - It is written for this repo; it is not upstream's GPL-2.0-only file.
  - **`trigger-capture.sh`:**
    - It streams the Mira220, switches it after the first frame, and prints `READY` with the
      CLOCK_MONOTONIC time.
    - It keeps each triggered frame: the raw data, a `.pgm`, pixel statistics and the device
      timestamp.
    - It stops and says so if frames keep coming at the free-running rate after `READY`.
    - Its `v4l2-ctl` runs **without `--stream-poll`**, which gives up after 2 s without a
      frame (read in the v4l-utils source). Blocking, it waits however long a trigger takes.
      `focus-preview.sh` and `record-dual.sh` use `--stream-poll`, so they cannot capture
      triggered frames.
  - **The report:** `conversion/triggered_frames.py`.
- **Tests, run on this PC only:**
  - `tests/test-trigger.sh`, 30 checks, against a fake `i2ctransfer`. 9 of 9 mutants of the
    tool are caught.
  - `tests/test-trigger-capture.sh`, 23 checks, end to end with a fake `v4l2-ctl` that
    free-runs until the fake sensor is switched, then emits one frame per trigger. 5 of 5
    mutants are caught.
  - `conversion.test_triggered_frames`, 8 tests.
  - Nothing has run on a Pi yet.
- **`BENCH_P5_P7_MIC_CAMERA.md` 7c.1 written:**
  - Step 1: switch to slave, then 10 s with no frames and the strobe idle low.
  - Step 2: `cam trig 0 50000` three times. FIRE as soon as both strobes are seen, and the
    longest timeout. It names three outcomes: the strobe answers; TIMEOUT, so ILLUM TRIG does
    not pulse in slave mode; or no frame at all.
  - Step 3: `cam shot` at 50, 200 and 300 µs, three each, with GPIO25 on the analyser.
  - **Exit:** one frame per trigger, and `t_cam` on 12 handshakes within ±0.1 µs of the
    analyser.
  - 7c.2 (the second camera) and 7c.3 (the geometry check) are outlined.
- **Checked for 7c.1's safety:**
  - A latched `CAM_TIMEOUT` does not move the power FSM.
  - In RUNNING, `fault clear` only clears the code: `power_request_fault_ack()` is dropped
    unless the FSM is in `PS_FAULT`.
  - So the procedure's `fault clear` after a camera timeout does not cut the Pi. The procedure
    still says to stop if the board prints `acknowledging FAULT`.

### 2026-11-18 - Board 1's Pi set up for 7c: the Mira220 probes on I²C bus 11 and streams at 88.96 fps

- **Asked by the owner:** the Pi had neither the repo nor the driver.
  - The model did the setup over SSH from the PC (`pitrac2.local`, key login from
    `~/.ssh/config`).
  - The Pi stayed seated on board 1 throughout, with the rail up and the board on 1.11.
- **The Pi:** Raspberry Pi OS Lite, Trixie, kernel `6.18.50+rpt-rpi-2712`. Its headers,
  `build-essential`, `dtc` and `initramfs-tools` were already installed.
- **What was done:**
  - **The camera kit:** `Software/camera-comparison` was copied from the PC to
    `~/PiTrac_V2/Software/camera-comparison`.
    - It is the PC's working tree, so it includes the uncommitted 7c.1 tools.
    - It is not a git clone: remove or replace it before cloning there.
  - **Packages:** `gawk`, `git`, `i2c-tools` and `python3-pil`, with their dependencies.
    - Installed with `--no-upgrade`, after a clean simulation: no removals, and nothing in the
      kernel, firmware or libcamera.
  - **The driver:** `install-mira220.sh --install` built and installed the pinned driver,
    srcversion `712AA4F74D072B09AA7CE8F`. The regenerated initramfs holds no mira220 module.
  - **`/boot/firmware/config.txt`:** `camera_auto_detect=1` became `0`, and
    `dtoverlay=mira220-nir` was added for CAM/DISP 1. Nothing else changed.
    - Backups: `/boot/firmware/config.txt.before-mira220.20261118-144454` and
      `~/claude-setup/config.txt.before`.
  - **I²C:** `i2c-dev` now loads at boot (`/etc/modules-load.d/i2c-dev.conf`). The user was
    already in the `i2c` group.
  - **Reboot:** `sudo reboot` on the seated Pi. It was back in 27 s, and the rail stayed up, as
    in 8.6.
  - **sudo:** the owner gave the password in chat.
    - It was used once, to add a temporary `NOPASSWD` rule (`/etc/sudoers.d/90-claude-temp`,
      checked with `visudo`).
    - The rule was removed at the end, and sudo asks for a password again.
    - The first attempt failed: PowerShell's pipe put a UTF-8 BOM in front of the password
      (`HANDOFF.md` §7).
  - **The remote scripts and logs** are in `~/claude-setup` on the Pi. They hold no password.
- **Results:**
  - **The driver:** after the reboot, `install-mira220.sh --check` reports matching checksums,
    and the loaded srcversion matches the build.
  - **The camera** was already on CAM/DISP 1.
    - The kernel log shows `rp1-cfe … Using sensor mira220 11-0054 for capture`.
    - Media entity `mira220 11-0054` is on `/dev/media0`, with video on `/dev/video0`.
    - **I²C bus 11**, and `/dev/i2c-11` is present.
  - **The profile:** `configure-cameras.sh --camera mira220` accepted the 8-bit mono profile:
    1600 × 1400, GREY, stride 1600.
  - **Streaming:** 270 frames at **88.96 fps**, against the model's 89.08.
  - **`mira220-trigger.sh status` while streaming,** which reads only:

    | Register | Value |
    |---|---|
    | `0x1001` | `0x41` |
    | `0x1002` | `0x04` |
    | `0x1003` | `0x10` |
    | `0x10F0` | `0x00` |
    | `0x10D7` | `0x01` |
    | `0x10D8` | `0x02` |

    - **The 2026-11-16 correction holds on hardware.**
    - `0x10F0` reads 0 while the sensor runs, so it is a self-clearing start bit. The tool does
      not interpret it.
- **I was imprecise (2026-11-16):** I wrote that the sensor does not answer on I²C between
  streams.
  - In fact the driver suspends it 1 s after a stream stops (runtime-PM autosuspend, 1000 ms).
  - It answered just after a stream ended, and gave `Remote I/O error` later.
  - Corrected in `mira220-trigger.sh`, `TRIGGER.md`, 7c.1 and the 2026-11-16 entry.
  - No effect on the tools: they switch only while the sensor streams.
- **Next: 7c.0,** once the owner has confirmed the module and the probes are wired as its board
  state says, with the board unpowered.

### 2026-11-18 - Board 1: 7c.0 Step 1 PASS; Step 3's strobe checks pass from the same capture

- **Setup:** image 1.11, the Pi seated, one Mira220 on CAM/DISP 1, and the TXS0108E module.
  - The analyser had four channels: ch0 "A1", ch1 the strobe row (digital and analog), ch2 "A2"
    and ch3 VA. Analog at 1 MS/s, 1 s per capture.
  - Captures: `captures/p7c0_step1_{idle,streaming}_{analog,digital}_2026-11-18.csv`.
- **Not streaming:** every channel within ±0.13 V of 0, VA included. The camera's 1.8 V is up
  only while it streams, which is the case 7c.0 Step 1 provides for.
- **Streaming** (`focus-preview.sh`, 1000 µs exposure):

  | Signal | Measured | Criterion |
  |---|---|---|
  | VA | **1.797 V**, 1.770–1.844 V | 1.70–1.90 V |
  | Strobe at the camera (ch0) | **0 ↔ 1.807 V**, 1.95 V overshoot at the edges | |
  | Strobe row (ch1) | **0.037 ↔ 2.754 V**, 3.58 V overshoot at the rising edges | ≥ 2.5 V high, ≤ 0.4 V low |
  | Trigger at the camera (ch2) | **0.03 V**; edge blips to 0.17 V | < 0.54 V |

  - The row follows the camera side within 1 µs, the analog resolution.
  - The strobe is **active high and idles low**. `pins` showed `CAM_STROBE_0(8) 0` and
    `CAM_STROBE_1(9) 0`.
  - **It comes every 11.2257 ms and is 981.7 µs wide.** That is exactly the 124 configured
    exposure lines × 7.917 µs (981.67 µs): the strobe width follows the exposure.
  - The row's 3.58 V overshoot comes from the module's one-shot edge accelerator. It sits
    behind R19/R24 (220 Ω), below the RP2350's 3.8 V limit.
- **The owner's "spikes on A1" are the strobe pulses.**
  - The ch0 probe labelled A1 carries the strobe, and the ch2 probe labelled A2 sits at the
    trigger level.
  - The row can only go high from the strobe channel's camera side. So either the A1 and A2
    probes are swapped, or the module's channels are used the other way round from the doc.
  - Both are electrically fine: the trigger reaches REQ EXP, and the strobe reaches the row.
  - The owner is checking which it is. It decides which probe stays for Step 2 and 7c.1: the
    one on the trigger channel's camera side, ch2 today.
- **Still open in 7c.0:** Step 2 (`gpio 10 1`/`0`, with a stream running), and Step 3's `pins`
  catching both strobes at 1.

### 2026-11-18 - Board 1: 7c.0 Step 2 PASS with 17 mV to spare; 7c.0 PASS; 7c.1 bridges channel 5 first

- **Captures:** `captures/p7c0_step2_{high,low}_{analog,digital}_2026-11-18.csv`. 1 s each, with
  `focus-preview.sh` streaming. The probes are as in Step 1: ch2 is the trigger at the camera.
- **`gpio 10 1`:** the trigger at the camera sat at **1.277 V**. That is 98 % of samples within
  1.277–1.282 V, against 1.26–1.90 V. **PASS, by 17 mV.**
  - About 1.35 V was predicted. 1.797 × 12 / (12 + R) = 1.277 gives R ≈ **4.9 kΩ** for the
    module's pull-up, against the 4 kΩ typical I used.
  - **Crosstalk from the strobe channel:**
    - 19 of the 89 strobe falls dipped the trigger line for one sample (≤ 1 µs), to as low as
      **1.038 V**, below VIH.
    - 11 strobe rises lifted it, to as high as 1.53 V.
    - 5 more tiny dips, to 1.19 V, were not at a strobe edge.
- **`gpio 10 0`:** 0.029 V, max 0.164 V, against < 0.54 V. **PASS.**
- **The strobe ignored REQ EXP,** as master mode should: 89 pulses each second, every 11.2257 ms
  and 981.7 µs wide, the same with the trigger high and low.
- **`pins`** read `CAM_STROBE_0(8) 0` and `CAM_STROBE_1(9) 0` every time; the paste shows at least
  four.
  - The strobe is high 8.7 % of the time, so four zeros happen 69 % of the time. That is
    inconclusive, not a fail.
  - 7c.1's handshake proves GPIO8/9 see the strobe: it cannot complete otherwise.
- **7c.0 PASS on its exit criteria:** VA 1.797 V; A1 1.277 V high and 0.03 V low; the strobe
  crosses once per frame, 2.754 V at the row.
- **For 7c.1, the bridge is required, not optional:** channel 5 goes onto the trigger channel.
  - **Why:** in 7c.1 REQ EXP must stay high while the strobe switches. A dip below VIH then
    could cut or restart an exposure.
  - **What it gives:** the bridge halves the pull-up, to about 1.49 V predicted, and halves the
    impedance the crosstalk works against.
  - **New 7c.1 Step 0** checks it: ≥ 1.40 V, and ≥ 1.26 V at every strobe fall.
  - **Also:** keep the trigger and strobe leads apart. `WIRING_DIAGRAM.md` §7.3 now recommends
    the bridge.

### 2026-11-18 - Board 1: 7c.1 Step 0 PASS: 1.490 V with channel 5 bridged, and no dips at strobe falls

- **Setup:** A5 is wired to A1 and B5 to B1, so the trigger is on channel 1, as in the doc. In
  7c.0 the A1 and A2 probes were swapped.
  - Probes: ch0 on J4.4, ch1 on B2 (the strobe row), ch2 on A1 (the trigger at the camera), and
    ch3 on R33 (GPIO25).
  - Captures: `captures/p7c1_step0_{high,low}_{analog,digital}_2026-11-18.csv`.
  - The analog ran at 250 kS/s (4 µs), and only ch1 was recorded as digital.
- **`gpio 10 1`:** J4.4 at 3.294 V. **A1 at 1.490 V**, between 1.448 V and 1.547 V, against
  ≥ 1.40 V; 1.49 V was predicted. **PASS.**
- **`gpio 10 0`:** A1 at 0.055 V.
- **The strobe** kept pulsing every 11.2257 ms, at 2.749 V on the row.
- **Crosstalk, compared at the same 4 µs sampling** (7c.0's 1 MS/s capture, every 4th sample):

  | | Before the bridge (7c.0) | With the bridge (Step 0) |
  |---|---|---|
  | Strobe falls moving A1 by more than 50 mV | 7 of 90 | **0 of 59** |
  | Deepest sample at a fall | 1.058 V | **1.448 V** |
  | Largest rise excursion | +0.20 V | +0.06 V |

  - With no change, about 5 of 59 falls would have dipped.
  - **The caveat:** at 4 µs a sub-µs dip can fall between samples. Steps 1–3 sample analog at
    ≥ 1 MS/s.
- **Analyser settings for Steps 1–3** (no probe moves):
  - digital on J4.4, the row and GPIO25, at ≥ 100 MS/s;
  - analog only on A1 and the row, at ≥ 1 MS/s.
- **Next: 7c.1 Step 1.**

### 2026-11-18 - Board 1: 7c.1 PASS: the Mira220 exposes while the board's trigger is high; t_cam ≈ 0.2 µs

- **Setup:** image 1.11, as Step 0, with channel 5 bridged onto the trigger channel.
  - The analyser had digital on J4.4, the B2 row and R33 (GPIO25), and analog on the row and A1
    at about 1.25 MS/s.
  - The board's scrollback covers shots #8–#19.
  - **The captures don't all match that scrollback:**
    - `trig2`, `trig3` and `shot50_1`–`_3` were exported at 15:31–15:34, from an earlier pass
      whose scrollback was not pasted.
    - `trig1` (#8) and `shot200_*`/`shot300_*` (#14–#19) match it.
    - `shot50_3` caught no trigger.
  - Captures: `captures/p7c1_*_2026-11-18.csv`.
- **Step 1:**
  - The switch printed `slave: 0x1001 0x41 -> 0x00, 0x1003 0x08, running.`, after 7 and then 6
    free-running frames, 11.227 ms apart.
  - The analyser saw **no edge at all** in 0.92 s. The row stayed at 0.027–0.037 V, and A1 at
    0.05–0.065 V.
- **Frames:** exactly **one per trigger:** 3 for `cam trig` and 9 for `cam shot`, each 12–74 s
  apart, with none in between (`free_running_after_ready: no`).
- **Slave mode, measured. The camera's strobe follows REQ EXP on both edges:**
  - It **rises 0.168–0.184 µs** after J4.4, in all 11 captures that caught a shot.
  - It **stays high as long as J4.4,** to within 12 ns: 1000.30 µs for `cam trig`, and
    5614.98 / 5764.98 / 5864.97 µs for `cam shot` 50 / 200 / 300.
  - It **falls 0.156–0.176 µs** after J4.4 does.
  - So in slave mode ILLUM TRIG marks the exposure, which lasts as long as REQ EXP is high.
  - A coupling glitch cannot hold 2.754 V for 5.8 ms, and 7c.0 Step 2 showed no DC path from
    the trigger to the row.
- **`t_cam`:**

  | Shot | Firmware | Analyser | Difference |
  |---|---|---|---|
  | `trig1` (#8) | 0.227 µs | 0.172 µs | +0.055 µs |
  | `shot200` ×3 (#14–16) | 0.207 / 0.240 / 0.240 µs | 0.168 / 0.184 / 0.180 µs | +0.039 to +0.060 µs |
  | `shot300` ×3 (#17–19) | 0.240 ×3 µs | 0.184 / 0.176 / 0.172 µs | +0.056 to +0.068 µs |

  - All within ±0.1 µs. It is the same +40–70 ns offset as 7b.
  - Over shots #8–#19 the firmware printed 0.207–0.240 µs: one or two 33.3 ns loops.
  - **So `t_cam` is about 0.2 µs, not the 100–300 µs assumed** in `board.h` and the design
    docs.
- **The release:** J4.4 fell within 0.005 µs of the printed `trigger released at`, in every
  capture.
- **The burst:**
  - The first GPIO25 edge came at FIRE + **4.05–4.99 µs**: +54.5 / +54.7 µs at 50, +204.5–205.0 µs
    at 200, and +304.0–304.7 µs at 300.
  - Then 10 pulses of 14.000 µs, 610.000 µs apart.
  - The last pulse ended about 56 µs before the trigger dropped.
  - The board printed `FIRE + 4-5 us` and `IRQ0 seen` for every shot.
- **A1 while J4.4 was high:** never below **1.464 V** (median 1.490 V), through the strobe edges
  and every burst.
- **7c.1 PASS on its exit criteria:**
  - no frames without a trigger, and one per trigger;
  - `t_cam` on 12 handshakes, 0.207–0.240 µs, with the analyser within ±0.1 µs where the two
    pair;
  - every burst 4–5 µs after FIRE;
  - the strobe recorded: active high, 0.17 µs after each trigger edge, and as wide as the
    trigger.
- **I was wrong (2026-11-16):** I said v4l2-ctl flushes its output file after every frame, so a
  TERM loses nothing.
  - Its `fflush(fout)` runs only when streaming to a network host.
  - To a named file, glibc holds up to 4 KB of each frame until the next frame or a normal
    exit.
  - So `trigger-capture.sh` lost the **last frame's tail** in both runs. The raw files ended at
    exactly 5468 × 4096 and 8203 × 4096 bytes: 3072 bytes short each time.
  - The report then rightly refused to convert those frames: the third `cam trig` and the
    ninth `cam shot`.
  - **Fixed:** the script runs `stdbuf -o0 v4l2-ctl … --stream-to=- > mira220.raw`. With `-`,
    v4l2-ctl's `--silent` stops only ioctl tracing; `--verbose` still logs each frame.
  - **The fake now holds back each frame's tail the same way,** and the end-to-end test checks
    that every logged frame is whole. 24 checks pass. The old line fails the test: 0 whole
    frames of 15.
  - Not yet run on the Pi, which was off. It gets the fixed script at the next power-up.
- **Not yet done:**
  - pulling the frames (`~/camera-trig-20261118-153552/{trig,shot}`) to compare brightness. The
    Pi was off.
- **For the next image:**
  - `board.h`'s 100–300 µs comment and `adc_engine.h`'s "100-300 us window": the refinement
    window now comes from the first delay, not the camera;
  - the `J4: NO CAMERA (CR-09)` printouts;
  - the V111-N1/N2 notes.
- **Geometry check (7c.3):** v·t_cam = 90 m/s × 0.24 µs ≈ 0.02 mm, which is negligible.

### 2026-11-18 - Image 1.12 built: text only (the J4 printouts, t_cam, V111-N1/N2). Independent review PASS with wording notes

- **What it is:** 1.11 with text and comments only, plus one host-test case and the version.
  The owner chose it after 7c.1. The patch, against 1.11's source in `e23aa65`, is
  `captures/review_2026-11-18/v1.11_to_v1.12.patch`.
  - **The J4 printouts:** `cli.c` no longer prints `NO CAMERA ON J4` or `J4: NO CAMERA (CR-09)`.
    `cam` says J4 is 3.3 V, and takes a Mira220 only through a TXS0108E. The `board.h` and
    `camera.h` 🔴 comments say the same.
  - **`t_cam`:** the 100–300 µs assumption in `board.h` and `adc_engine.h` is replaced by
    7c.1's measured ~0.2 µs. With `t_cam` that short, FIRE is at the first delay in practice.
  - **V111-N1:** the watchdog statements (above `cmd_reset`, and at `WATCHDOG_MS` in
    `service.c`) bound only the bare 50 ms sleep and the board's own work, not USB output.
  - **V111-N2:** the late refusal reads "the Pi's 3V3 rose after the first check (during the
    message or the 50 ms wait), before '<cmd>'".
  - **The test:** a new host case, `fsm.reset_rise_in_message`, raises the 3V3 inside the
    attempt message's `printf`, before the sleep.
- **Identity:** `build_v1.12/`, Release `Nov 18 2026 15:57:13`, 247528 text / 0 data / 422684 bss, UF2 SHA256 `5ABB297C…0D39D377`, `.bin` `DC1B64BE…A56C04EB`. `picotool`: Release, version 1.12, TBYB flagged. No warnings, and
  `check_retained` reports both retained blocks as before.
- **Host tests 300/300.** Mutations 2/2 caught: the old wording, and the message moved after
  the second check.
- **Machine code against 1.11** (`elf_code_diff.py`, with addresses and literal pools masked):
  - 748 labels in each .text disassembly, all at the same addresses.
  - Only `cmd_id_cmd` and `cmd_upd` differ, by one instruction each: `movs r2, #11` became
    `#12`, the version minor.
  - **Review correction.** Version metadata, pointers and image offsets also changed. The two version loads account for all changed Thumb instructions in flash and RAM.
- **Independent software review PASS, 2026-11-18.** An isolated build reproduced the BIN and UF2
  byte-for-byte. The independent host run passed 300/300, both mutations were caught, and four
  command/watchdog combinations refused a rise during output while preserving watchdog state.
  Both retained RAM records matched 1.11.
  - **V112-N1.** V111-N1 remains partly open. USB output can consume enough of the watchdog
    period for it to expire during the subsequent 50 ms sleep. The 5 ms yield slice is no
    general service deadline. The reset guard and Pi-present watchdog policy remain as reviewed.
  - **V112-N2/N3.** Shot 8 used zero first delay and reported FIRE at 0.313 µs. The first delay
    governs the longer-delay shots. The CLI could name 7c as the camera phase and the Pi
    exception in its short watchdog help. All three findings are nonblocking.
  - Report `captures/review_2026-11-18/INDEPENDENT_REVIEW_v1.12.md`. Board 1 still runs
    1.11 in A with 1.7 in B. The 1.12 Pi install instructions and on-board checks are pending.
- **I was wrong twice in the 1.12 package (author, 2026-11-18):**
  - **"Every other difference is string data" was too broad.** The IMAGE_DEF version word,
    relocated pointers and image-end offsets changed too: the review's correction above.
  - **My new comment above `cmd_reset` is false.** It says an armed watchdog "cannot expire
    in the bare 50 ms sleep itself".
    - Nothing feeds the watchdog between the superloop and the sleep, so the sleep starts
      with whatever the message's USB output left of the 1 s period. 970 ms of output leaves
      30 ms (V112-N1).
    - That comment replaced V111-N1's false assurance with another.
    - Nonblocking: no code depends on it, and the rule against arming the manual watchdog
      with a powered Pi stands.
- **Queued for the next image:**
  - **V112-N1:** describe the sleep as using 50 ms of whatever remains since the last feed.
  - **V112-N2:** say `t_cam` ~0.2 µs was measured with one Mira220 in slave mode, with its
    return on both inputs. FIRE follows at `t_cam` + 0.087 µs only with a zero first delay;
    otherwise the first delay governs (`board.h`, `adc_engine.h`). `ARCHITECTURE.md`'s
    schedule-window note is corrected now: docs need no image.
  - **V112-N3:** the short `help` should name 7c alongside the TXS0108E. The `wdog` help line
    should say live mode arms the watchdog only with no Pi present and the watchdog off.
- **The install:** `BENCH_P8_PI.md` §8.9c, written 2026-11-18.

### 2026-11-19 - The stand's geometry is in the repo (`Hardware/Stand/`); the mic is 455 mm from the ball; I was wrong about the impact sound

- **The owner asked** where the stand's parts sit, and why. The answer, with its sources, is
  [`../Stand/README.md`](../Stand/README.md).
  - **Where the layout comes from:** the Faceplate optimizer's weighted020 candidate
    (2026-09-15), with the strobe raised to clear the trigger lens (2026-10-01).
  - **Its `reference_003` export** (2026-10-02) is the frame the printed parts were drawn in.
  - **`Hardware/Stand/tools/stand_geometry.py`** reads `FullStand.STEP` (2026-11-18). Both
    camera pupils land on the design to 0.000 mm. The V2 board's M2 holes land on their
    standoffs to 0.00 mm.
- **Distances from the ball** (RH, a 36 mm mat assumed): CAM0 376.6 mm, CAM1 465.1 mm, mic U16
  454.6 mm. The light sheet is the plane 65 mm downrange of the ball centre.
- **I was wrong (2026-11-19) to say the impact sound reaches the mic "comfortably before FIRE
  at up to 100 m/s".**
  - That rested on a guessed 50 mm from tee to sheet and the design doc's
    `beam_to_fov_mm = 150`, and I did not say so.
  - With the real geometry the sound arrives 1.31–1.33 ms after contact. Under the 150 mm
    figure that is before FIRE at every speed.
  - The stand was optimised for a first flash just after the trailing edge, with the ball
    centre about 101 mm downrange. Under that model the sound comes after FIRE above about
    79 m/s: about 96 m/s once club contact is allowed for.
  - So copying the mic samples before BURST catches fast drivers only if the burst starts
    later, as it does with the 150 mm figure.
- **Two timing models disagree** (README §4.1).
  - The design doc: a 3 mm beam, 150 mm to the first freeze, 42.67 mm spacing.
  - The optimizer: a 30 mm sheet, the first flash 25 µs after the trailing edge, 47.33 mm
    spacing.
  - 7c.3 has to settle which. Nothing in the firmware changes.
- **The owner (2026-11-19): the sheet is about 3 cm thick.** The design doc's `beam_width_mm`
  of 3 mm should have been 3 cm.
  - The July optimizer written alongside the doc already used 25 mm ("2-3 cm").
  - No firmware uses the value: no speed is computed yet, and `detect path` is unset.
- **Where 150 mm came from:** a deliberately loose starting value.
  - §15 sized it for `t_cam` assumed at 100–300 µs. §16 step 8 says to tighten it from the
    images.
  - Neither optimizer used it.
- **A first freeze just after the trailing edge is possible** now that `t_cam` is about
  0.2 µs. The floor is about 40 mm past the sheet centre at 100 m/s. **(Wrong in part: see
  2026-11-20.)**
  - It needs no firmware change: the first delay is per shot.
  - Measure the detection latency and the trailing-edge-to-FIRE time first (README §4.1).
- **CAD storage is the owner's choice.**
  - The assembly STEP is 171.7 MB, over GitHub's 100 MB limit, and the repo has no Git LFS.
  - The repo holds only the text files and the tool so far.

### 2026-11-19 - The optimizer and the stand's CAD now live in `Faceplate\Faceplate_Design` (verified copy); a wording correction

- **The owner's choice:** the cleaned-up optimizer and the CAD go in
  `C:\Users\ATTAYEKP\Github-Personal\Faceplate\Faceplate_Design`, outside this repo.
- **The optimizer:** 526 files and 235 MB, from the original folder's 46,351 files and 36 GB.
  - The files were traced from real runs, then copied byte-for-byte with their relative paths.
  - It gets its own copy of the Python environment.
  - The engine's internals are unchanged: it hashes them.
- **The proof**, in `Faceplate_Design\VERIFICATION\`:
  - weighted020's five focus-condition capture maps match the record cell for cell, and the
    scores recompute exactly: 97.2005 % nominal, 79.2959 % worst-focus;
  - the same source identity, 593 files;
  - all 26 API jobs identical, apart from the STEP files' timestamps;
  - no reads into the original folder.
- **The CAD:** `Downloads\PitracStand` (235 files, a snapshot), `reference_003` and the
  2026-10-05 enclosure parts, each with a SHA-256 manifest. The originals are untouched.
- **`Hardware/Stand/README.md`** now points there. §5 gives the paths relative to that folder.
- **I was wrong (2026-11-19):** the Stand README said that any edit to the optimizer's sources
  "stops it", and it called `clock_model.py` hash-locked.
  - The engine hashes every module it loads into a source identity, which keys cached results.
  - It refuses to start only when the coaxial source folder differs from its snapshot, or a
    pinned input fails its hash.
  - It stops when a file changes mid-session.
  - Editing `clock_model.py` while stopped is allowed. Corrected in both READMEs.

### 2026-11-20 - The workbench's firmware handoff checked; the shot-path plan written; I was wrong twice

- **The owner asked** for a firmware plan from
  `Faceplate_Design\FIRMWARE_COMPATIBILITY_HANDOFF.md`, which the workbench side wrote that
  morning.
  - The plan is [`SHOT_PATH_PLAN.md`](SHOT_PATH_PLAN.md). Its §2 checks the handoff claim by
    claim against image 1.12 and `designs/weighted020/CONTROLLER.json`.
  - No firmware changed.
- **What the check found:**
  - **The live readback sets the first-flash latency.**
    - `STROBE_REFUSE_ADC` requires IDLE.
    - A live firing waits for 200 µs of ADC0 baseline after the switch to BURST. On board 1
      that was about 0.4 ms (2026-10-12). *(Wrong: 198–200 µs. See the 7c.3 entry below.)*
    - A9 puts the switch after the speed analysis.
    - So the handoff's 25–50 µs readiness, and its "BURST before admission", do not fit the
      reviewed code.
  - **A pass's fall time is the superloop's read time**, not the edge. The ±1 µs anchor needs
    a bounded hot loop or a hardware-timed edge.
  - **The LUT:**
    - 67 bins with gaps, not 89. A missing bin goes to the fallback, as in the workbench's own
      model;
    - bin 23 has no pulse, and eight bins drop the sharp pulses;
    - ten widths are under 5 µs;
    - RH and LH differ;
    - only bin 4 (71 ms) exceeds the 30 ms readback window;
    - the `flat15` × 10 fallback spans about 255 ms.
  - **The handoff's Option A** (6–8 µs ADC0 sampling, 90 ms bursts) would weaken the reviewed
    2 µs overcurrent rules and raise a live limit. Not recommended.
- **Decisions D1–D7** (plan §3) wait for the owner.
- **I was wrong (2026-11-19) twice.**
  - **`L_eff` for the ADC5 50 % width.** I changed `PITRAC_V2_TECHNICAL_GUIDE.html` to
    "`D_ball + w_beam` ≈ 72.7 mm" and set its simulator to 72.67 mm. 72.67 mm is the
    full-contact length.
    - The 50 %-of-peak width of the bump is about 36–41 mm in a simple backscatter model
      (plan §6). The old 45.67 mm was nearer, for the wrong reason.
    - Corrected to a modelled ~38 mm, still a per-board calibration.
  - **"A first freeze just after the trailing edge … needs no firmware change".** I said it
    in the Stand README §4.1, the stand HTML page, `DEVELOPER_GUIDE.md` §4.6, the CHANGELOG
    and the entry above.
    - Four things stand in the way: the 2 ms bench coalescer, the superloop's read-time
      anchor, the uniform burst encoder, and the live readback's baseline wait (~0.2 ms; first written
      as ~0.4 ms, corrected below).
    - All corrected.
- **The owner's direction:** the first flash comes as soon as reasonable, whatever the speed;
  the speed is needed only after it. Recorded in the plan's §2a, which revises D2 and adds D8.
  - The workbench already models pulse 0 that way.
  - It takes the speed off the critical path: a ch5 snapshot, then refinement while pulse 0
    fires; pulse 1 is at least 526 µs later.
  - It needs a fixed pulse-0 width (about 10 µs), a LUT with slot 0 always on, and a two-stage
    burst feed.
  - The live readback's baseline wait remains the floor (~0.2 ms; first written here as
    ~0.4 ms, corrected below). D2 now offers three ways around it.

### 2026-11-20 - 7c.3 PASS (desk); the live readback's baseline takes ~0.2 ms, not 0.4 ms: I was wrong

- **7c.3, the geometry check** (`BENCH_P5_P7_MIC_CAMERA.md`), at the owner's request.
  - The check: `beam_to_fov_mm > (D+w)/2 + v·(t_fixed + t_cam)`, from the sheet centre.
    (D+w)/2 = 36.3 mm with the owner's 3 cm sheet. `t_cam` is 0.24 µs (7c.1): 0.02 mm at
    100 m/s.
  - An upper limit the formula leaves out: the whole ball in CAM0's view, so at most 207 mm
    (first-order pinhole, the 1600-pixel side horizontal).
  - At 100 m/s the first freeze comes 39.6 mm past the sheet centre with the design's 32 µs,
    and 59.5 mm with the 199 µs baseline on top. The design doc's fixed value is 150 mm.
    **All PASS.**
  - Whole-ball freezes in view, at 42.67 / 47.33 mm spacing: 4 / 4, 4 / 4 and 2 / 2.
  - Four freezes at 100 m/s need the first flash within 289 µs of the trailing edge at
    47.33 mm, or 429 µs at 42.67 mm. After the baseline, that leaves 90 µs or 230 µs for
    detection and the CPU.
- **I was wrong about the live readback's start-up.**
  - I said about 0.4 ms in `SHOT_PATH_PLAN.md` (row 3a, §2a, D2), the Stand README §4.1,
    the stand page, `DEVELOPER_GUIDE.md` §4.6, the CHANGELOG and the entries above.
  - My first 7c.3 figures to the owner used it too: about 80 mm and 3 freezes.
  - The source was `board.h`'s comment: 122 ADC0 samples about 440 µs after the switch
    (2026-10-12). The same day's §6 entries explain both numbers:
    - 440 µs was the whole record (PRE 200 + firing ~39 + POST 200);
    - the ADC was running at half rate (DIV 95), so the ring was never late.
  - On the fixed image, every live firing in 6c printed `100 ready` at **+198 to +200 µs**,
    with the first sample at +2 to +5 µs (once +38 µs, a late poll). That answers the plan's
    WP0.2.
  - **Also wrong:** D2 (A) said PRE could go from 200 to 100 µs, "the review's floor of 50
    samples". 50 samples is the record's minimum baseline, and the 200 µs wait leaves the
    record only 76–77. How far PRE can shrink is a question for the review.
  - Corrected:
    - the plan: D2 now recommends (A), and WP0.2 is marked done;
    - the Stand README, the stand page, `DEVELOPER_GUIDE.md` and the CHANGELOG;
    - `ARCHITECTURE.md`, whose 2026-10-12 note still called the cause unknown.
- **Queued for the next image** (comments only). Three comments describe the 122 samples at
  ~440 µs as a late start:
  - `board.h`, above `STROBE_LIVE_RING_READY_SAMPLES`;
  - `strobe.c`, at the ring-ready wait;
  - `cli.c`, above `cmd_adc_ringtest`.
  They should say the ADC ran at half rate, and that the ring starts within about 5 µs. The
  index-counted wait stays: it is the right defence regardless.

### 2026-11-20 - The owner decides D1–D8 for the shot path; D3 changes a live-strobe limit

- **Recorded in [`SHOT_PATH_PLAN.md`](SHOT_PATH_PLAN.md) §3a, and in §8 here.** No firmware
  changed.
- **D1:** measure first (WP0.1). Core 0 if its armed worst case is under about 20 µs;
  otherwise core 1.
- **D2:** (A), switch to BURST at the trailing edge.
- **D3:** the owner asked why the 30 ms limit cannot become 100 ms.
  - The answer: 30 ms is how long ADC0 can watch the strobe current without a gap. The DMA
    ring can only wrap within 32 KB (16384 samples, 32.8 ms), and RAM has about 66 KB free
    (`build_v1.12` map) against the ~100 KB a 100 ms record needs.
  - The owner then asked whether every pulse needs watching, given calibration.
  - Calibration sets the current; the readback catches a pulse that departs from it (6c's
    3.3 A overshoot and cold J3 joints). Its verdict comes after the burst and stops the
    next firing.
  - **Decided:** watch the first 30 ms, and let later pulses fire unwatched. The span limit
    rises to 150 ms, in WP5, under independent review.
- **D4:** the live fallback is upstream PiTrac's driver pattern, 8 pulses over 21.8 ms
  (`kStrobePulseVectorDriver`, PiTracLM/PiTrac `main`, read 2026-11-20).
- **D5:** the workbench regenerates its table under the firmware's rules, to 100 m/s.
- **D6:** the analog mic is for after the shot only.
- **D7:** the table is compiled in first.
- **D8:** pulse 0 is fixed at the width that gives a 100 m/s shot a sharp image. About 9 µs
  is estimated; the workbench computes it.

### 2026-11-20 - 7c.2 written: a TXS0108E per camera (owner); the Pi tools take two Mira220s

- **The owner has a second Mira220,** and asked for 7c.2.
- **One module for both cameras would back-feed camera 1.**
  - The documented wiring fed a single module's VA and OE from camera 0's 1.8 V.
  - On board 1 a Mira220's 1.8 V is up only while it streams (7c.0 Step 1: VA 0 V with the
    stream stopped). The driver powers a camera only while it streams, and for 1 s after.
  - So whenever camera 0 streamed and camera 1 did not, the module would drive camera 1's
    REQ EXP and ILLUM TRIG pins with camera 1 unpowered: the CR-09 damage case.
  - **The owner chose a module per camera** (§8). Each module's VA and OE come from its own
    camera, so an unpowered camera faces a Hi-Z module. No start or stop order is needed.
- **No firmware change.** J4.3 and J4.4 carry the one trigger (GPIO10). J4.7 and J4.8 are the
  two strobe inputs, and the handshake already waits for both (`camera.c`).
- **[`BENCH_P5_P7_MIC_CAMERA.md`](BENCH_P5_P7_MIC_CAMERA.md) 7c.2:**
  - Step 0: the Pi's tests, both overlays, and each camera configured by its I²C bus.
  - Step 1: module #1 holds camera 1's trigger below 0.2 V while camera 1 is off.
  - Step 2: camera 1's trigger level and strobe, and the strobes not crossed.
  - Step 3: both cameras in slave mode, with no untriggered frames.
  - Steps 4 and 5: `cam trig` and `cam shot`. Each camera's delay and the skew come from the
    analyser; `t_cam` must be the later strobe; every trigger pairs a frame from each camera.
- **The Pi tools** (`Software/camera-comparison`; `TRIGGER.md`):
  - `configure-cameras.sh --bus N` chooses one of two Mira220s. Without it, two are refused,
    with both names listed. The search is a new helper, `find_sensor` in `common.sh`.
  - `trigger-capture.sh` takes `--config` twice. One process streams both, because the
    camera lock is per user. Each camera's files go into `bus<N>/`. One camera works as
    before.
  - `conversion/triggered_pairs.py` (new) pairs each trigger's frames: mutual nearest, within
    50 ms. A missed trigger leaves its frame unpaired and does not shift the rest.
  - **Tests, on this PC under Git Bash:**
    - `test-helpers.sh` 109 checks, `test-trigger.sh` 30, `test-trigger-capture.sh` 47 (24
      before);
    - the Python unit tests, 17;
    - the fakes now keep a sensor per bus, with one trigger file for both;
    - not yet run on a Pi. 7c.2 Step 0 runs them there.
  - **A test-host trap:** Git Bash rewrites `/dev/video11` into a Windows path for a Windows
    Python, so the fake reads only the device number.
- **The wiring docs follow the decision:**
  - `WIRING_DIAGRAM.md`: rules 3 and 4, the overview, §3, §6.2, §7.1–§7.3 (a new drawing), §8's
    J5.2 note, and §9;
  - `BRINGUP_NEW_BOARD.md` Step 13.4's module table. It had a second camera's strobe on the
    first module's A3;
  - `PITRAC_V2_TECHNICAL_GUIDE.html`'s rules, table and checklist. Its two wiring drawings
    still show one module, and their captions now say so.
- **Board 1's ports:** camera 0 stays on CAM/DISP 1, and camera 1 goes on CAM/DISP 0. The board
  does not care which port is which. 7c.2 records which bus is which camera.
- Not committed.

### 2026-11-21 - Two modules kept (owner); two alternatives considered; CR-09 corrected twice

- **A divider on the trigger, the strobe direct: rejected.**
  - **The strobe:** the Mira220 guarantees a high of only 1.44 V (1.80 V at most). The RP2350
    guarantees a high read only above ~2 V, so a direct strobe would rest on unspecified
    behaviour.
  - **The trigger:** a divider gives a good level. For example, 12 kΩ in series against the
    camera's 12 kΩ pull-down gives ~1.64 V. But it never isolates: every trigger high would
    reach a camera that is off.
    - A ~6 kΩ divider passes ~0.17 mA, with under 0.1 V of noise margin (7c.0's dips,
      scaled).
    - A ~0.6 kΩ divider passes ~1.7 mA, about 3× the camera's whole I/O supply current.
- **One module, VA from camera 0 and OE from camera 1 (100 kΩ, 470 kΩ to GND): considered,
  not adopted.**
  - OE low puts the TXS0108E's I/Os in Hi-Z and turns off its pull-ups (TI SCES642), so it
    would be safe in every steady state.
  - It leaves three things two modules don't have:
    - a window of a few ms while camera 1 powers down, with OE still high;
    - a check for a resistor on the module's OE;
    - neither camera working alone.
  - **The owner kept a module per camera** (§8).
- **CR-09 corrected in `NEXT_BOARD_REV.md`:**
  - **Its plan for the next layout allowed one on-board translator fed from a local 1.8 V
    regulator.** That would drive a camera that is off. It now asks for a translator per camera,
    each from its own camera's 1.8 V, with a VDDIO pin per camera on J4.
  - **"The strobe inputs never read high" was too strong.** "Never guaranteed to read high" is
    true; translation is still required.
- No firmware change. Not committed.

### 2026-11-23 - Board 5 bring-up started (owner): it is the COM10 board; Steps 1.3–1.4 PASS

- **The owner brings up Board 5 now, before 7c.2.**
  - USB to this PC and the PSU, no Pi, no camera.
  - DMM and logic analyser on test points and header pins; **no soldered leads on this board**
    (owner).
  - The owner reports its TIA as the original-board rework (162 kΩ ∥ R80, 1.5 pF).
- **Board 5 is the COM10 board** of the rework batch: UID `6093584a7790419d` (§6, 2026-11-03).
  Its history carries over:
  - ADC5V scale 1.0688, saved in slot A, seq 9. The config sectors survive the partition
    table.
  - ADC2 clipped at 8 % and 25 % beam duty, cause not found.
  - Its R80 network has never been measured. The unmapped 280 kΩ and 160 kΩ readings across
    U11 pins 1–2 (§6, 2026-11-03) may be this board's.
  - **Next for it:** `BENCH_REWORK_CHECK.md` §1–§2, all unpowered, before any optical work.
- **Before today it ran r5 flat.** `picotool info -a` in BOOTSEL:
  - version 1.1, `binary start 0x10000000`, `binary end 0x10035dd4`, blocks at 0x138 and
    0x35dc0: r5;
  - **silicon A4**, QFN80, chipid `0x6093584a7790419d`.
- **Step 1.3 PASS.**
  - `picotool load build\pitrac_pt.uf2`, `reboot -u`, then `partition info`:
    `0(A) 00002000->00100000 "PiTrac A"` and `1(B w/ 0) 00100000->001fe000 "PiTrac B"`.
  - Then `load -p 1 -v -x build\pitrac.uf2`, verified OK.
- **Step 1.4 PASS:**
  - `id` → `version : 1.1 (partition B at 0x100000)`, `built : Oct 20 2026 14:59:52`, the
    UID, `sysclk 150000000 Hz`;
  - `upd` → `flash update boot, ON TRIAL: bought itself at the end of init`, `this image in
    partition B`, `partner : ... UNKNOWN`.
  - The partner reads UNKNOWN because A holds the rest of the old flat r5, as `BENCH_P8_PI.md`
    §8.7 predicts.
- **I was wrong:** my Step 1.3 commands put `partition info` and the image load straight
  after `reboot -u`. The procedures had no wait either.
  - Right after the reboot, `partition info` answered `...returned an error: rebooting`, and
    the load `Communication with RP2350 device failed`.
  - Both worked when repeated, and the failed load wrote nothing.
  - A 5 s wait is now in `BRINGUP_NEW_BOARD.md` Step 1.3, `BENCH_P8_PI.md` §8.7 Step 2 and
    `HANDOFF.md` §7. Step 1.3 also covers a board that arrives flashed flat.
- **1.12 into A: PASS, its first board.** After `bootsel`, `picotool load -p 0 -v -x
  build_v1.12\pitrac.uf2`. COM10 came back.
  - `id` → `version : 1.12 (partition A at 0x002000)`, `built : Nov 18 2026 15:57:13`.
  - `upd` → `flash update boot, ON TRIAL: bought itself at the end of init`, `this image in
    partition A`, `partner : version 1.1, bought: the fallback`.
  - Why 1.12: it passed independent review, and its machine code is 1.11's apart from the
    version. Board 1 has run 1.11 since 2026-11-12, with §8.10 passed.
- **`cfg` → `source : slot B seq 14`**, not the slot A seq 9 recorded on 2026-11-03. Five later
  saves were never logged; asked the owner. The rest of `cfg`:
  - scale 1.0688; phase 1343 ticks; gain 14.5;
  - `hpf sel : TRACK = 0`; phase model fitted, pure delay; cal duty 25 %.
  - `stat`: STANDBY, no fault, `5V_IN 5.202 V fresh`, `halt : boots 7`.
- **§2 skipped (owner); its readings were taken at the first `on`,** as on Board 4. The owner
  sent `on` before §4 and reports every reading as expected:
  - the PSU at 5.202 V;
  - VIR 35.77 V; TP2 12.31 V; TP7 2.59 V.
  - Step 2.2 (the boost's frequency) was not done: no scope.
- **§3 (E9) PASS, with the latch closed** (`LATCH_CONTROL(15) 1`): GPIO24, 0, 8 and 9 all read
  0. Also: `D_COMPARATOR(46) 1`, GPIO27 0, `LED_YELLOW 1`.
- **§4 PASS.** `adc5vcal 5.202` → scale **1.0687** (raw 4.867 V). `cfg save` → slot A, seq 15.
  `adc5v` 5.202 V, which matches the DMM; `cfg` → `source : slot A seq 15`.
- **Step 5.1 PASS.** On USB only, `adc5v` 4.871 V, `USB-only, latch INHIBITED`. `on` →
  `FAULT`, `USB_POWER_ONLY`, latch 0.
- **Step 5.2 PASS.** PSU on: `fault clear`, `on` → `BENCH_RUNNING`, 5V_IN 5.200 V, latch 1.
- **Steps 5.3–5.5: PASS as reported by the owner.** The log ends at 5.2, so there are no
  captures or numbers for them.
- **Unpowered rework check (`BENCH_REWORK_CHECK.md` §1–§2): PASS as reported by the owner.**
  **R80 measures 120 kΩ across its pads,** inside 119–122 kΩ: the 162 kΩ is connected. So the
  2026-11-03 ADC2 clipping is not a wrong Rf. Optical crosstalk with no baffle remains the
  likely cause, and §8 checks it when this board does optics.
- The PSU current was not reported.
- **The owner wants to skip most of the rest (2026-11-23; §8).** The proposed minimum before
  a Pi:
  - `link` in STANDBY;
  - SW2 taped;
  - `BENCH_P8_PI.md` "First seating", with the R39 check;
  - one clean `on`/`off` cycle with the Pi;
  - the link (§8.8).
  - Deferred until this board needs them: §6–§11 (beam and optics), §12 (strobe current),
    §13's rollback and shipping steps, and §8.0b's simulated Pi.
  - **Later the same day, the owner chose to run §6–§12 before the Pi** (§8).
- **`link` in STANDBY:** `lines : released: no Pi up`.
- **Step 7.1 PASS:** `adc 2 256` → 3215 codes, 2.5908 V; TP7 2.59 V.
- **Step 7.2 PASS:** `CONFIRMED: GPIO33=0 is TRACK`.
  - GPIO33=0: 9.3 and 9.6 mV. GPIO33=1: 24.6 and 24.2 mV. Separation 2.6×.
  - HOLD p-p 7.3 mV; drift 2.29 mV/s; **leakage 52 pA** (board 2: 52 pA).
  - It ran right after `beam off`, without the 3-minute wait, and passed anyway.
  - `cfg save` → slot A, seq 17. `cfg`: `hpf sel : TRACK = 0`.
- **Step 6.1 PASS:** both slices `ENABLED ok`; funcsel 4 on GPIO31 and GPIO39; pad ISO 0;
  counting.
- **Steps 6.2 and 6.3: no analyser (owner).**
  - `beam clamp` ran, and the beam was set back by the doc's sequence: 104166 Hz, 2 %, off.
  - U9's width was not measured. 6.2 is covered by readback, then by §9.
  - **A DMM method for 6.3 is now in Step 6.3:** TP5 read from the back at three settings.
- **Step 6.4 not done.** The beam ramped to 25 %, but there was no 10-minute FLIR hold. It
  goes into §9's warm-up.
- **Step 6.5 PASS:**
  - `A7` on SIO; `GPIO27 : SIO output, driven 0, pad 0 -> U5 pulse limiter armed`;
  - gate DAC level 0; engine idle with GPIO25 SIO, pad 0.
  - `panel rdy` cannot be seen: there is no front panel on J7.6 (`panel rdy 100` was mistyped).
- **The test points are safe to probe from the back (owner's report; checked against the
  PCB):**
  - TP1–TP10 are through-hole, and on the back no other-net pad lies within 6 mm of any of
    them.
  - Every via is tented (board setup `tenting (front yes) (back yes)`, no per-via override).
    The nearest are mask-covered GND vias: TP8 0.45 mm, TP3 0.62 mm, TP10 1.24 mm.
  - The script that found this checks its front-side figures against the recorded ones:
    TP7/R95 2.13 mm (recorded 2.12), TP9/C79 0.78 (0.78), TP10/R91 0.79 (0.79). TP2, TP3 and TP6
    come out ~0.2 mm larger than recorded, so the recorded front figures stand as the
    conservative ones.
  - Written into `BRINGUP_NEW_BOARD.md` §2.1, Step 7.1, Step 12.2 part 5, and `HANDOFF.md` §3
    rule 8.
  - **A wrong reference fixed:** `BRINGUP_NEW_BOARD.md` cited the probe rule as `HANDOFF.md`
    "§5 rule 8"; it is §3 rule 8. §5 rule 8 is the independent review.
- **The owner has fitted a baffle at D12,** and has a wall to aim the beam at for §9.
- Not committed.

---

### 2026-11-23 - Board 5 §8–§11 PASS: phase 1320 ticks (400 ns), vref 3.270 V, Vos +6.2 mV

- **The owner's scrollback, one session.** The baffle is at D12, and there is a wall in front
  for §9.
  - No U9 readings: Step 6.3's DMM check is optional and was not done.
  - **No FLIR temperature was reported, so Step 6.4 is still open** (asked). The beam ran at
    25 % for about 7 minutes through §8–§9, and again through §10.
- **§8 PASS:** aimed at open space, HPF in TRACK.

  | duty | min | max | swing |
  |---|---|---|---|
  | 12 % | 3033 | 3245 | 212 codes = 171 mV |
  | 25 % | 3056 | 3263 | 207 codes = 167 mV |

  - Both captures sit far from the rails. The swing stops growing between 12 % and 25 %, the
    plateau §8 calls a good sign.
  - **Max linear duty ≥ 25 %.** The 2026-11-03 clipping is gone with the baffle fitted, and R80
    measures right. So that clipping was optical, not the TIA. Its geometry was not recorded.
- **§9 PASS.** `beam` first: 104166 Hz (TOP=1439), 25 %, phase 1343.
  - **Run 1, at 266 s** (`warm : NO`): peak 78 %, committed 1318. Superseded.
  - **Run 2, warm:**
    - peak **55 %**; saturated 0 %; quad null 0.1 codes;
    - h3/h1 0.104 (0.111 intrinsic, bar 0.161);
    - **committed 1320 ticks, 400 ns**.
  - The amplitude fell from 3675 to 2585 codes between the runs, faster than warm-up alone, so
    the aim probably changed (not reported). The phase barely moved (1318 against 1320), as on
    Board 4 (1333 at 69 %, 1332 at 78 %).
  - **`cal model` PASS**, all five points:
    - `theta(f) = 5.3832 + 4.351e-06 f − 5.393e-12 f²`;
    - **residual 0.26°**; amp spread 6 %;
    - **chain delay 396 ns at 80 kHz → 430 ns at 200 kHz, PURE DELAY** to within 1.21°.
  - **Step 9.4:** Board 4 carries the same reported 120 kΩ / 1.5 pF rework and measured
    487 ns. Board 5 measures 400 ns. They are 87 ns apart with the same TIA, inside the 95 ns
    non-TIA spread that 9.4 warns of.
    - No pre-rework `cal demod` exists for this board. The stored 1343 has no known source, so
      there is no before/after.
  - `beam off`, then `cfg save` → slot B, seq 18. `cfg`: phase 1320, `phase mdl: fitted, pure
    delay (80000..200000 Hz)`.
- **§10 PASS,** with the board unmoved.
  - `beam on` went straight to 25 % shortly after §9's `beam off`, with no ramp and no 5-minute
    re-warm. The LED was still warm, and Board 4 did the same with no effect on §10.
  - **10.1:** at duty 0, `D_Comparator(46) = 1`; at duty 5 (level 51), `= 0`.
  - **10.2, LOW,** two runs: both flipped at **0.16 %** (TP8 nominal 5.2 mV), with ADC5 at 12.9
    and 9.7 mV.
  - **10.3, HIGH:** `beam phase 1120`, which is §9's 1320 − 200 rather than − 280.
    - `adc 5 100` → **3686 (2.970 V)**, above 10.3's 2000–2500.
    - The point is still valid. The sweep flipped at **89.06 %** (TP8 nominal 2.9391 V), with
      ADC5 there at **2.9188 V**.
    - That is inside the DAC's range and inside U15's input range (an LM393 on +5 V, ~3.5 V).
    - ADC5 moved 12.1 mV in the sweep: HOLD's drift, with the LED still warming.
    - `BRINGUP_NEW_BOARD.md` 10.3 now says a reading up to ~3700 works.
  - **10.5:**
    - vref = (2.9188 − 0.0113)/(0.8906 − 0.0016) = **3.270 V** (3.268–3.272 over the two LOW
      runs);
    - Vos = 0.0113 − 3.270 × 0.0016 = **+6.2 mV** (+4.6 to +7.8, and ±2 from the DAC's first
      levels);
    - boards 2, 3 and 4: 3.268, 3.256 and 3.254 V; +11.0, +5.4 and −0.6 mV;
    - both pass.
  - **10.6, beam off:** ADC5 moved **2.4 mV** across the sweep, under 3 mV (Board 4 2.4).
  - **10.7:** `threshold duty 0`, then `cfg save` → slot A, seq 19.
- **§11 PASS:** `cfg` (slot A, seq 19):
  - carrier 104166 Hz, phase 1320 ticks; threshold level 0;
  - adc5v 1.0687; gain 14.5; path unset;
  - `hpf sel : TRACK = 0`; phase model fitted, pure delay; cal duty 25 %.
- **Docs:**
  - `BRINGUP_NEW_BOARD.md`:
    - 9.4 and 10.5 add Boards 4 and 5;
    - 10.3 as above;
    - Step 12.2 part 6 no longer names Board 5 as the board with leads. It has none (owner).
  - The same correction in `NEXT_BOARD_REV.md` ("Board 5's role", twice) and §9 below.
- **Open on Board 5:**
  - Step 6.4's 10-minute FLIR hold;
  - Step 6.3, which is optional;
  - §12;
  - then the Pi.
- Not committed.

---

### 2026-11-23 - Board 5 Step 12.2 parts 2–5 PASS (dry, J3 disconnected)

- **Part 2, rail down:**
  - `pins` → `PULSE_LIMIT_DIS(27) 0`;
  - `strobe` → `GPIO27 : SIO output, driven 0, pad 0 -> U5 pulse limiter armed`, `gate DAC :
    level 0`, `may fire : +5V rail not ready`;
  - `strobe pulse 20` → `REFUSED -- nothing fired`.
- **Part 3, rail up:** `BENCH_RUNNING`, latch 1; `strobe` → `rail : up`, `may fire : yes`,
  `may arm : yes`.
- **Part 4:**
  - `strobe pulse 20` → `completed, IRQ0 seen`, `GPIO25 after: low, SIO`;
  - `clamptest 100` → `ERR: clamptest is for widths over 100 us`;
  - `clamptest 200` → `completed, IRQ0 seen`.
- **Part 5, TP3 from the back** (owner's DMM):

  | command | TP3 | nominal | pass band |
  |---|---|---|---|
  | `strobe gate 50` | **4.94 V** | 4.950 V | 4.71–5.06 V |
  | `strobe gate 90` | **8.91 V** | 8.914 V | 8.50–9.09 V |
  | `strobe gate 0` | **0 V** | 0 | ≤ 30 mV |

  Board 1 read 4.936 V at 50; Board 4 read 4.94 V, 8.94 V and 0 V.
- **Step 12.1 PASS (owner): the gate-hold capacitor is fitted and measures 1 µF,** and every
  connection check passes (each end < 1 Ω to its pad, ≈ 47 Ω to TP3, no hard short, R67
  ≈ 56 kΩ).
- **The owner asked whether to seat the Pi and cameras before 12.3. Advised no:** J8 and J4 stay
  empty through 12.4, as §12 says.
  - With a Pi powered, live mode arms no watchdog (the owner's decision, 2026-11-12). First
    current on an unproven board is when the 1 s watchdog matters most.
  - A strobe fault on first current would brown out the +5 V rail under a running Pi. The
    ~3 A PSU limit has less headroom with a Pi drawing from it.
  - The Pi's first seating has its own checks. Doing both firsts at once makes a failure
    ambiguous.
  - A camera on J4 is for 7c only (`HANDOFF.md` §3).
- **Part 1 not reported yet** (R61 → GND, the two J3 joints, R65/R66's markings). It is
  required before live current, in the power-down before 12.3.
- **Step 6.4 assumed OK (owner):** no FLIR on hand (§8).
- `stat` shows `halt : ... faults 1`. That counts latched faults since the telemetry began, so it
  is most likely Step 5.1's deliberate `USB_POWER_ONLY`. `hist` names the last one.
- Step 6.4's temperature: still not reported.
- Not committed.

---

### 2026-11-23 - Board 5 Step 12.3 part 1: the ring PASS; ADC0 reads 10 codes, TP4 asked

- The LED bank is on J3, and the rail is up (`BENCH_RUNNING`).
- `strobe` shows `GPIO27 : ... U5 pulse limiter armed`, `live : OFF` and `may arm : yes`.
- **`adc ringtest 100` PASS:**
  - 200 of 200 pairs covered, 0 snapshot failures;
  - widest bracket 14 cycles (≤ 40), residual −7 to +7;
  - first sample ≤ +3 µs, 100 written ≤ +200 µs;
  - board 1, after its snapshot fix: 14 cycles, −8 to +8.
- **`adc 0 4096` → 10 codes (8.1 mV at the pin).** The pass line said "under 10".
- **I was wrong:** I passed on the doc's "under 10 codes" without checking it.
  - It is too tight. Board 1 read 9 codes at rest (2026-10-12), and its ring's first samples
    9–12. That is the ADC's own offset, not current.
  - The real gap: nothing checked a small standing current. 10 codes could also be 60 mA
    through the bank with the gate at 0. The firmware's baseline check
    (`STROBE_LIVE_BASELINE_MAX_CODES`, 40 codes, ~0.24 A) misses that, and so does the gate-0
    firing, which measures against the baseline.
  - **Fixed:** `BRINGUP_NEW_BOARD.md` 12.3 part 1, `BENCH_P6_STROBE.md` and
    `BENCH_REWORK_CHECK.md` now say "about 10 codes, 12 at most". 12.3 part 1 adds a DMM on
    TP4 from the back: under 2 mV, which is 15 mA.
  - 12.3's VIR line now compares against C26, not a flat 36 V: Board 5's VIR is 35.77 V.
- **Asked the owner for:**
  - TP4's DMM reading;
  - VIR at J3 pin 1's screw;
  - Step 12.2 part 1's readings (R61, the J3 joints, R65/R66).
- Not committed.

---

### 2026-11-23 - Board 5 §12.3–12.4 PASS: U5 clamp 134 µs; 9 A at level 436 (42.58 %)

- **The owner went on from 12.3 part 1 without the TP4, VIR or 12.2 part 1 readings** (below).
- **Part 2:** `LIVE: ARMED ... Watchdog ARMED (1 s)`. At gate 0: `within limits`, `no current
  pulse above the ~72 mA floor`, baseline 11.6 codes (0.069 A).
- **Part 3, the staircase:**
  - 3–18 %: nothing above the floor;
  - 21 %: 0.103 A; 24 %: 0.703 A; 27 %: 1.703 A, then 1.704 A.
  - Every step `within limits`, 20 µs (10 samples), no `NOT CLEAN`. Peak at most 1.008 ×
    plateau; the largest rise 1.0 A.
  - First current at 21 %, as on board 1.
  - The owner climbed one step past the first in-band step (24 %, 0.70 A), to 27 % (1.70 A).
    That is still inside 0.5–2.5 A.
- **Part 4,** at level 276 (26.95 %):
  - `strobe pulse 20`: 1.704 A;
  - `strobe clamptest 1000`: 1.722 A plateau (+1.1 %), **on 134 µs** (67 samples, ±2 µs),
    `within limits`, no fault.
  - **U5's clamp: 134 µs** (board 1 135 µs; band 118–137).
- **12.4, `strobe cal 9`:**
  - first current at level 220 (0.159 A);
  - **SOLVED: level 436 (42.58 %, TP3 nominal 4.22 V)**;
  - confirmation mean 8.975 A (−0.3 %), PASS;
  - **session cap: level 461 (45.02 %)**.
  - `strobe live`: the last shot read 8.973 A plateau, 9.026 A peak, 20 µs.
  - Board 1: level 439 (42.87 %), 9.02 A.
  - **The cap's refusal (`strobe gate 47`) was not run.** It tests the firmware, which board 1
    passed (§6, 2026-11-10), so it was not asked again.
- **Leave:**
  - `live OFF: gate 0, engine stopped, GPIO25 low. Watchdog now off.`;
  - `off` → `STANDBY`, latch 0;
  - `hist`: boots 8, one more for the power-down to connect J3; faults 1, unchanged.
- **What the live results cover from the unreported checks:**
  - **J3's joints, and VIR at the screw:** clean, flat 9 A pulses. A cold J3 joint on board 1
    gave no current at all.
  - **R61:** U5's output passes pulses, and clamps at 134 µs.
  - **R65/R66:** the solved level is within 0.3 points of board 1's. That is suggestive, not a
    substitute for the markings, which are asked.
  - **Not covered: TP4 at rest** (a small standing current at gate 0). Asked, for the next time
    J3 is connected with the rail up.
- **§12 PASS on Board 5,** with those two asks open. There was no FLIR on Q9/HS1.
- **The Pi next. The first seating's R39 check needs clips on Board 5.** From the PCB, edge to
  edge:
  - R39's pads are 0.48 mm apart;
  - R86 is 0.38 mm away and C74 0.57 mm. `BENCH_P8_PI.md` gave 1.0 and 1.4 mm, which were
    centre distances.
  - That is too tight for a hand-held tip (`HANDOFF.md` §3 rule 8).
  - J8.37's own joints are 0.84 mm from J8.35, J8.38 and J8.39, too tight as well.
  - **Added to "First seating": an equivalent check with J8 empty.**
    - Push jumper wires into J8's socket at pins 37, 10 and 39 with the power off. Then read
      J8.37 and J8.10 to J8.39 in `STANDBY`: ≈ 0 V.
    - Whatever would push current into a seated, unpowered Pi drives these pins here.
- `BRINGUP_NEW_BOARD.md` 12.3 part 4 and 12.4 add Board 5's clamp and solved level.
- **I was wrong (by omission):** my Pi steps for Board 5 left the PSU limit at §12's ~3 A.
  - Every Pi step in `BENCH_P8_PI.md` says **≥ 5 A** (8.2, 8.3, 8.5, 8.9b, 8.10).
  - A Pi 5 booting on the board's rail could trip a 3 A limit and brown out. Corrected in chat.
- **The owner asked why J3 comes off for the Pi.**
  - It is the owner's own rule (§8, 2026-11-09): with a Pi seated, the strobe stays dry and the
    beam off. §8.10 is the one approved exception.
  - The procedures make "dry" physical: with the bank off, no firmware or hardware fault can
    drive LED current from the rail the Pi runs on.
  - Nothing in the Pi steps or in 7c needs the bank.
- Not committed.

---

### 2026-11-23 - The owner hands over Board 5 and the Pi; neither is reachable yet

- **The owner (away for hours):** board 1's Pi is now on Board 5, with both Mira220s on the
  Pi's camera ports.
  - J4 is not wired: no trigger or strobe between the board and the cameras. The lenses are
    not set up yet.
  - "Do everything you can on the board (COM10) and the Pi."
  - The sudo password was given in chat. It is not written to any file.
- **Neither is reachable from this PC:**
  - **COM10 is not present.** Windows lists it as a remembered, absent device, and the only
    ports are Bluetooth ones. USB alone powers the RP2354, so the board's USB is not connected
    to this PC.
  - **`pitrac2.local` does not resolve**, and nothing on 192.168.0.0/24 answers as it. The one
    Raspberry Pi MAC there, 192.168.0.232 (`e4:5f:01`), has no SSH. Most likely the Pi is
    seated on an unpowered board.
- **Ready for when they appear,** with a watcher polling for COM10 or the Pi every 30 s:
  - **Board, read-only first:** `id`, `stat`, `pins`, `link`, `hist`, `cfg`, `upd`. Then `on`
    only from `STANDBY`, latch 0, no fault, with `pi : present 0  3v3 0`.
  - **Then, per `BENCH_P8_PI.md` and 7c.2 Step 0:**
    - the Pi's first power-up on Board 5;
    - a read-only Pi survey;
    - `Software/camera-comparison` copied again, with the old copy kept;
    - the tool tests;
    - `dtoverlay=mira220-nir,cam0` added after a backup, then a reboot. This is one sudo
      call, with the password on stdin only;
    - both sensors listed and configured;
    - a free-running stream check, and one snapshot per camera for the lenses;
    - the link (8.8 steps 3–4);
    - one clean `off`.
  - Nothing touches J3, J4, the beam or the strobe.
- **`captures/review_2026-11-20/REVIEW_BRIEF_7c2.md` refreshed:**
  - BRINGUP's hash, since Board 5's edits changed other sections, not Step 13.4;
  - its line numbers;
  - a note that 7c.2 now looks set for Board 5. The other pinned files are unchanged.
- Not committed.

---

### 2026-11-23 - Board 5 powers board 1's Pi; 7c.2 Step 0 on camera 0 only (sudo refused)

- **The owner connected the board's USB to this PC** and cleared the board to power the Pi.
  - The board's log: `captures/COM10_2026-11-23_claude.txt`.
  - The snapshots: `captures/p7c2_pi_2026-11-23/`.
- **The board, read-only first:**
  - `id`: 1.12 in A, `built Nov 18 2026 15:57:13`, UID `6093584a7790419d`.
  - `stat`: `STANDBY`, fault none, 5V_IN 5.205 V, latch 0.
  - **`pi : present 0  3v3 0` with the Pi seated and unpowered.** This is the guard left in place
    of the skipped R39 check (§8), and it holds.
  - `pins`: GPIO27 0, J8.37 released. `link`: lines released.
  - `cfg`: slot A seq 21, phase 1320 ticks, threshold level 0, scale 1.0687, cal duty 25 %.
    - It prints `BEAM IS AT 2.00 %`: the beam duty now set is 2 %, not the 25 % calibrated at.
    - That is harmless with the beam off. Set it back to 25 % before any beam work, as the
      warning says.
  - `hist`: boots 9, faults 1. The fault is the old `USB_POWER_ONLY` from boot 7.
- **`on`:**
  - `PI_BOOTING` at 3 s, with latch 1 and the Pi's 3V3 up.
  - **`RUNNING` with userspace 1 within 18 s.**
  - 5V_IN 5.19–5.20 V with the Pi and two idle cameras.
  - Watchdog off, as designed while a Pi is powered.
- **The Pi, read-only survey:**
  - Pi 5 Rev 1.1, kernel 6.18.50+rpt-rpi-2712, `throttled=0x0`.
  - `config.txt` holds `camera_auto_detect=0` and `dtoverlay=mira220-nir` only.
  - One sensor, `mira220 11-0054`. The driver check passes.
  - `pitrac-link` ping, info, status and hist answer from Board 5 (1.12).
  - No pipewire or wireplumber units.
- **The camera kit, copied again.** The Pi's old copy is kept as
  `camera-comparison.before-20261123-194527`. The four hashes pinned in `REVIEW_BRIEF_7c2.md`
  match on the Pi.
- **7c.2 Step 0.1 PASS, the first run on a Pi:**
  - `test-trigger.sh`: 30 checks passed;
  - `test-trigger-capture.sh`: 47;
  - the unit tests: 17, OK.
  - The Pi has the real `i2ctransfer`, `v4l2-ctl` and `media-ctl`, so I checked first that the
    tests shadow each one with a fake.
- **Step 0.2 not done: sudo refused.**
  - The installed overlay has the `cam0` override, read from its `.dtbo`.
  - The password given today was rejected, once. It reached the Pi intact: 6 bytes after
    stripping PowerShell's BOM and CR.
  - The password given on 2026-11-18, which worked then, was also rejected, once.
  - On 2026-11-18 I advised changing that password. So the Pi's password has probably changed
    since, and today's message probably has a typo.
  - I tried nothing more. `config.txt` is unchanged and the Pi did not reboot.
  - Camera 1 (CAM/DISP 0) stays invisible: Step 0.3 would list one sensor.
- **Camera 0 alone** (no root needed):
  - **Configured** on bus 11 into `~/camera-c0-20261123-195017`, `CONFIGURE_RC=0`:
    1600×1400 GREY, stride 1600, exposure 1000 µs (124 lines), 89.08 fps.
  - **Free-running:** 270 frames, rc 0, in 3.50 s including start-up. v4l2-ctl's running average
    reached 78.9 fps and was still climbing towards 89.08.
  - **The snapshots are too dark to set up the lenses:**
    - mean 14.2 of 255 at 1000 µs, and 14.5 at 11 000 µs, the full-rate maximum;
    - the maximum is 33–34.
  - Stretched over codes 11–33, the 11 ms frame shows the room.
  - The brightest area rises only from 14.2 to 15.2 codes between 1 and 11 ms. The room is
    nearly dark in NIR.
  - So this does not prove that the exposure control works. Setting up the lenses needs NIR
    light: daylight, an incandescent lamp, an IR lamp, or the strobe.
- **The link (`BENCH_P8_PI.md` 8.8 steps 3–4): 1000 pings, 0 failed.** The board's `link`:
  1008 good frames, 0 CRC errors, 0 junk bytes.
- **The Pi after streaming:** `throttled=0x0`, EXT5V 5.184 V, 38.9 °C. No under-voltage or
  camera errors in the kernel log.
- **`off`:**
  - `SHUTTING_DOWN`, then `STANDBY` about 10 s later, with latch 0, `pi : present 0  3v3 0` and
    J8.37 released.
  - `hist`: shutdown requests 1, read down in time 1, timed out 0, power cuts 0.
  - GPIO27 was 0 throughout.
- **On Board 5, `BENCH_P8_PI.md` "First seating" passes as far as it can run from here:** the
  guard, one clean `on`/`off`, and the link.
  - Not visible from here, so the owner's to confirm: SW2 taped, the Pi's USB-C unplugged, and
    the PSU limit ≥ 5 A.
- **Left:**
  - Board 5 in `STANDBY`, with the PSU and USB on and the Pi off.
  - J3, J4, the beam and the strobe untouched.
  - The password was sent on stdin only, and never written to a file.
- **Docs:**
  - `CLAUDE.md`/`AGENTS.md` say 7c.2 is now on Board 5.
  - `CHANGELOG.md` records the Pi tests and the Pi on Board 5.
  - **Stale text fixed:** `DEVELOPER_GUIDE.md` said three times that the Pi side of RPI5_ON
    (`Hardware/firmware/pi/`) was not yet run on a Pi. It has run on a seated Pi since Phase 8
    (2026-11-09).
  - **Left for the owner's decision:** the 7c.2 board-state table still says board 1. That file
    is pinned in `REVIEW_BRIEF_7c2.md`.
- Not committed.

---

### 2026-11-23 - 7c.2 Step 0 PASS on Board 5: both Mira220s listed and configured

- **The owner, back:**
  - gave the Pi's current sudo password. It is not written to any file;
  - confirmed SW2 is taped and the Pi's USB-C is unplugged. The PSU limit was not stated, but
    the Pi ran at 5.19–5.20 V throughout;
  - allowed `beam duty 25` from COM10 at any time.
- **Found:** the board `RUNNING` with the Pi up for 148 s (the owner's `on`). `cfg` was at slot B
  seq 22: a save since 19:57, not mine.
- **`beam duty 25`:** `duty <- 25.00 %  (level 360 of 1440, effective 25.00 % at the LED)`.
  - `beam`: off, both PWM slices off, the counter frozen. `cfg`'s warning is gone.
  - `beam_set_duty` writes only the compare level. Light needs `beam on`, which was not sent.
  - **2 % is the boot default** (`beam.c`: "start low; ramp up deliberately"). The live duty is
    not saved in `cfg`, so the warning returns after each power-up, until `beam duty 25`.
- **Step 0.2:**
  - Before the reboot: no camera in use, and `config.txt` unchanged. The owner's SSH session
    dropped at the reboot.
  - Backup: `/boot/firmware/config.txt.before-7c2.20261123-212037`, with a copy in
    `~/claude-setup`.
  - The diff is one line, `dtoverlay=mira220-nir,cam0`, right after `dtoverlay=mira220-nir`.
  - Then the reboot.
- **The Pi's reboot on Board 5, as in `BENCH_P8_PI.md` 8.6:**
  - The board stayed `RUNNING` with latch 1 and 5V_IN 5.19–5.20 V.
  - It printed `DOWN WITHOUT A REQUEST` about 8 s in, then `read up again ... a Pi reboot`
    by 20 s.
  - `hist`: down unrequested 1, came back 1, power cuts 0.
- **Step 0.3 PASS:** `mira220 10-0054` on `/dev/media1` and `mira220 11-0054` on `/dev/media3`.
  So B0 = 11 and B1 = 10.
- **Step 0.4 PASS:** both configurations end `Configured: …`.
  - Both: 1600×1400 GREY, stride 1600, 1000 µs (124 lines), 89.08 fps.
  - Camera 0 is `/dev/video0` and `/dev/v4l-subdev2`. Camera 1 is `/dev/video8` and
    `/dev/v4l-subdev5`.
  - The final configurations, made after the 11 ms snapshots so both sensors hold 1000 µs again:
    `~/camera-c0-20261123-212517` and `~/camera-c1-20261123-212517`, named in
    `~/claude-setup/7c2-configs.env`.
- **So 7c.2 Step 0 PASS.** The tool tests passed earlier today (the entry above).
- **Each camera, free-running:** 270 frames, rc 0, 3.50 s with start-up. `throttled=0x0`.
- **Snapshots** (`captures/p7c2_pi_2026-11-23/`, the `2124_` and `2125_` files):
  - **Camera 0 shows the room,** dark: mean 14.1–14.3, max 33–34.
  - **Camera 1 shows no scene.** Its maximum is 25 at both 1 ms and 11 ms. Stretched over codes
    12–25, the frame is column noise only.
  - So no light reaches camera 1: a cap, no lens, or the camera facing something dark. Its
    sensor streams normally.
  - Steps 1–5 count frames and measure levels, so they do not need an image. Setting up the
    lenses does, and it needs NIR light.
- **My slips, no harm:** two one-line `ssh` commands hit `HANDOFF.md`'s PowerShell quoting
  trap. Both only read. One waited on stdin and was stopped. Everything after went through
  scripts, run with `ssh -n`.
- **Left:**
  - the board `RUNNING` with the Pi up, as the owner left it;
  - the beam off, at 25 %;
  - J3 and J4 untouched.
- **Next: 7c.2 Steps 1–5.** They need module #1 wired and J4 connected, all with the power off,
  plus the analyser. The owner's call on repeating module #0's checks on Board 5 is still open
  (§10).
- Not committed.

---

### 2026-11-24 - The supply is the LRS-75-5; the PC stays on J6 for 7c.2

- **The owner:** the supply is a Mean Well LRS-75-5, the one `WIRING_DIAGRAM.md` §2 specifies.
  - It is rated 14 A at 5 V (70 W), and its overload protection trips above that.
  - So the procedures' "PSU limit ≥ 5 A" is met.
  - It has no settable current limit. Lines that ask for a lower limit assume a bench supply.
- **The owner is wiring both cameras, each through its own TXS0108E.**
  - Found at 09:35: J6 unplugged, but the board still `RUNNING` on the supply. The Pi had been up
    for 12 h; `pitrac-link status`: 5V_IN 5.192 V, fault none.
  - **Advised:** take the Pi down cleanly before the supply goes off. Either a short press of the
    panel button, under 5 s (the same as `off`; `power_fsm.c`), or J6 back on this PC for
    `off`. A press of 5 s or more forces the power off without the Pi's shutdown.
- **The owner asked whether the PC is still needed, or whether the Pi can do everything.** For
  7c.2, the PC is needed:
  - **`pitrac-link` has `ping`, `info`, `status`, `hist`, `upd-status`, `abort` and `update`**
    (`Hardware/firmware/pi/pitrac_link.py`), and nothing else. Steps 1–5 use the CLI on J6:
    `gpio 10 1`, `cam trig`, `cam shot`. Shot messages on the link are still pending (`id`).
  - The analyser for Steps 1–5 runs on the PC.
  - COM10 on this PC is how I reach the board's CLI.
- **J6 to one of the Pi's USB-A ports instead:** not covered by any procedure.
  - On paper, J6's VBUS reaches +5V_IN only through D8, which the 5.20 V supply holds
    reverse-biased (`The_One_Board_To_Rule_Them_All.md`).
  - 1.12 also refuses USB reboots, which would cut a seated Pi's power.
  - Not checked against the netlist and not tried. It needs that check, and the owner's go-ahead,
    before it is tried.
- **The owner asked whether A2 should also join A5, and B2 join B5.** No: channel 5 doubles only
  channel 1, the trigger.
  - Joining channel 2 to it would tie the camera's strobe output to its own REQ EXP input, and
    J4.7/J4.8 (GPIO8/9 inputs) to J4.3/J4.4 (GPIO10's output).
  - Channel 1 needs the help because REQ EXP has a 12 kΩ pull-down on the camera: 1.277 V with
    one channel, 1.490 V with two (7c.0, 7c.1).
  - Channel 2 drives an RP2354 input. It read 2.754 V at the row on one channel, and 7c.1's
    handshake completed on it.
- **`WIRING_DIAGRAM.md` §7.2's board side checked against the netlist:** it matches.
  - J4.1, J4.2, J4.5 and J4.6 are GND.
  - J4.3 and J4.4 are one net, to GPIO10 through R18.
  - J4.7 goes to GPIO8 through R19, and J4.8 to GPIO9 through R24.
  - J5.2 is +3.3VA.
  - This covers part of `REVIEW_BRIEF_7c2.md` §4 item 2. The camera side can only be checked
    against §7.1 B, the owner's transcription of the Mira220 schematic.
- **The owner was wiring from `PITRAC_V2_TECHNICAL_GUIDE.html` Figure 2.1 (`#fig-full-wiring`).**
  - That drawing is the old one-module wiring: channels 1–4, `B5..B8 & A5..A8 = NC`, and camera
    1's P1.5 marked "LEAVE NC".
  - Only its caption said it was superseded.
- **I was wrong:**
  - **On 2026-11-20 I left both drawings unchanged,** with only a caption note. That was not
    enough: the owner wired from Figure 2.1.
  - **My captions for Figures 2.1 and 2.2 gave the B5/A5 bridge for camera 1's module only.**
    Both modules need it.
- **Fixed in the guide:**
  - **Figures 2.1 and 2.2 now carry a red "superseded" panel on the drawing itself.** It lists
    the per-camera wiring, pin by pin. Figure 2.2's J4 pad drawing stays visible, because it is
    correct.
  - Both captions now give the bridge on both modules, and channel 2 unbridged.
  - Figure 2.3's caption, and the J5.2 label, name both modules' VB.
  - Both drawings render cleanly in headless Edge and parse as XML.
- **Then redrawn, at the owner's request** ("I prefer that graphical representation"). The
  panels are gone.
  - **Figure 2.1:** a module per camera, each beside its camera.
    - Each module's VB, B1+B5, B2 and GND rows face the camera's P1.5, P1.7, P1.6 and P1.1
      rows.
    - The J4 wires do not cross. J5.2's Y-splice reaches module #0 without a crossing.
    - Module #1's lead crosses three signal wires, drawn over them with a gap.
    - Camera 1's P1.5 now feeds module #1's VA + OE; it was "LEAVE NC". The common camera
      ground bus is gone: each module grounds its own camera.
  - **Figure 2.2:** redrawn as two chains, each J4 → module → P1.
    - Each 20-pin breakout pin by pin, VA…OE against VB…GND, as `WIRING_DIAGRAM.md` §7.1 C.
    - The B1–B5, A1–A5 and VA–OE jumpers are drawn on the breakout.
    - Each camera's P1, pins 9 to 1, is labelled with its own module's pins.
    - J4's pad map is kept, with a pin list per camera and the J4 nets from the netlist.
    - Generated by a script so the two halves are identical but for J4 and the labels.
  - The camera-to-port labels say they are 7c.2's bench assignment (camera 0 on CAM/DISP 1).
  - Both captions rewritten. Nothing in the guide still says "single" module, or uses
    channels 3/4.
  - Both drawings parse as XML and were checked by eye in headless Edge.
- **`REVIEW_BRIEF_7c2.md`:**
  - The guide's hash refreshed to `0C91491B…790B4B28`, with what changed. The other pinned
    files are unchanged.
  - Item 2 of §4 now includes both drawings.
- Not committed.

---

### 2026-11-24 - 7c.2 Steps 1–5 on Board 5, firmware and Pi side: PASS, with the buses swapped; no LED current in the first live session

- **The owner, before an hour away (§8):**
  - both modules wired, each module's VA and OE checked unpowered to its own camera's P1.5;
  - lenses with 850 nm filters on both cameras; the panel button wired;
  - the LED bank on J3, with live firing allowed up to 9 A;
  - no analyser.
- **Board state:** Board 5, image 1.12, the LRS-75-5 at 5.20 V. `on` at 12:15 → `RUNNING`, with the
  Pi up. Beam off, detect disarmed.
  - Logs: `captures/COM10_2026-11-24_claude.txt` and `captures/strobe_focus_2026-11-24_122511.log`.
- **A deviation from 7c.2's board state: J3 was connected** (7c.2 says disconnected).
  - The dry steps ran with the gate at 0 and live mode off. Then the firmware cannot command LED
    current (`strobe` → `DRY`).
- **Step 0 again (a new boot):** both sensors listed (`mira220 10-0054`, `mira220 11-0054`), and
  both configured at 1000 µs.
- **Steps 1–2, the firmware's side (no analyser).**
  - **How:** each camera streamed alone, free-running, for 600 frames (89.07–89.11 fps), while
    `pins` was sampled 40 times. GPIO10 stayed low throughout.
  - **With B0 = 11, as on board 1:** the bus 11 camera put GPIO9 high in 6 of 40 samples and
    GPIO8 in 0. The bus 10 camera put GPIO8 high in 7 and GPIO9 in 0.
    - **The buses are the other way round,** the case Step 1's note covers. I swapped them and
      configured into new directories.
  - **With B0 = 10, B1 = 11:**
    - camera 0 → GPIO8 high in 3 of 40, GPIO9 in 0;
    - camera 1 → GPIO9 high in 3 of 40, GPIO8 in 0.
    - **Not crossed.**
  - **On this bench, then, camera 0 (module #0, J4.7) is on bus 10 = CAM/DISP 0, and camera 1
    (module #1, J4.8) is on bus 11 = CAM/DISP 1.** That is the reverse of 7c.2's table and of the
    guide's figure labels, which say so are only the bench assignment. Record it in
    `WIRING_DIAGRAM.md` §6.2 when 7c.2 passes.
  - Each strobe passes only while its own camera streams. So each module's VA and A2 come from
    the same camera; the owner's continuity check puts VA on that camera's P1.5.
  - **Not done, because they need the analyser:**
    - Step 1's `gpio 10 1` isolation check;
    - Step 2's trigger levels: under 0.2 V with the camera off; 1.40–1.90 V on, with dips
      ≥ 1.26 V;
    - the strobe levels: ≥ 2.5 V high and ≤ 0.4 V low.
    - I did not raise GPIO10 with a camera off.
- **Step 3 PASS, firmware and Pi:**
  - both cameras printed `slave: 0x1001 0x41 -> 0x00, 0x1003 0x08, running.`, then `READY`;
  - no triggered frame for 26 s;
  - `pins` and `cam` show both strobes at 0.
- **Step 4 PASS, firmware and Pi:** three `cam trig 0 50000`.
  - Each time: `HANDSHAKE: complete`, `both strobes high at t_cam = 0.227 us`, FIRE at once, the
    trigger released at 1000.307 µs, and the pad low afterwards.
  - Three frames per camera, `free_running_after_ready: no`.
  - `pairs: 3`, unpaired 0, `status: complete`. `delta_us` 9–10 (bus 11's device time minus bus
    10's).
- **Step 5 PASS, firmware and Pi:** nine `cam shot`, at 50, 200 and 300 µs, three times each.
  - Every one `FIRED: handshake complete, dry burst complete`, with `t_cam` 0.207 or 0.240 µs.
  - FIRE at the first delay (49.993, 199.993 and 299.993 µs). The burst's first edge FIRE + 4–5
    µs; the trigger released at FIRE + 5565 µs.
  - `triggered_frames: 9` on each camera; `pairs: 9`, `status: complete`.
  - The frame means are 13.91–13.98 (camera 0) and 14.38–14.50 (camera 1). That is black level:
    nothing lights the scene.
- **Twelve handshakes in all, `t_cam` 0.207–0.240 µs,** as on board 1 (0.21–0.24 µs).
  - **Not recorded:** each camera's own delay, the skew between them, and `t_cam` against the
    analyser (±0.1 µs). **7c.2 has not passed** until the analyser parts run.
- **The ambient check:** each camera free-running at 11000 µs for 6 s (490 frames).
  - Means 13.99 and 14.60; the brightest pixels 25 and 32.
  - Behind 850 nm filters the room is black, so focusing needs the strobe.
- **Live: BRINGUP 12.3 part 1's firmware checks PASS:**
  - `adc 0 4096` → 9 codes;
  - `adc ringtest 100` → 200 of 200 covered;
  - `strobe` → U5 armed, `may arm : yes`, the Pi PRESENT.
- **Live session 1 (12:25, the first version of `tools/strobe_focus.ps1`): no LED current at all.**
  - `LIVE: ARMED`. At gate 0, 20 µs: `within limits`, no current.
  - `strobe cal 9` climbed in 20-level steps from level 0 to the 70 % ceiling (level 717, TP3
    nominal 6.93 V). **Nothing above ~72 mA at any step:** `CAL FAILED: CEILING`. The last
    shot's baseline was 11.1 codes.
  - `strobe live off` → live OFF, gate 0, no fault. Not re-armed.
  - On 11-23 this board gave its first current at 21 % and 9 A at 42.58 %. So this is **an open
    load:** J3 not seated, the bank's polarity, or a light that is not 11-23's bank. It needs
    hands, with the PSU and USB off.
- **I was wrong (my slip, no harm):** the first `strobe_focus.ps1` went from the gate-0 pulse
  straight to `strobe cal 9`.
  - BRINGUP 12.3 climbs by hand first, and **stops if nothing has flowed by 30 %**: an open
    load. 12.4's cal comes only after current has been seen.
  - So the cal climbed to 70 % into an open load. No current flowed. Had a loose contact closed
    above 30 %, the first pulse could have passed 9 A before the firmware's 10.8 A stop.
  - **Fixed:** the script now runs 12.3's staircase before any cal: 3 % steps, 20 µs each. It
    stops on a jump over 2 A, on a first current over 2.5 A, or on nothing by 30 %. **The fixed
    path has not run on a board.**
- **New, for focusing the lenses (the owner's next step):**
  - **`Hardware/firmware/tools/strobe_focus.ps1`:**
    - BRINGUP 12.3 part 1's firmware checks, then arm, then the gate-0 pulse;
    - the staircase, then `strobe cal <A>` (1–9 A, default 9), which must solve at ≤ 60 %;
    - then one pulse (default 100 µs) every 500 ms. Each is judged: `within limits`, one pulse,
      the plateau ≤ A × 1.06 + 0.1, the on-time ± 4 µs;
    - at most 24 mC per 10 s, 80 % of the firmware's budget;
    - `strobe live off` at every exit, including `q` and Ctrl-C.
    - It never raises a firmware limit.
  - **The preview:** `preview/server.py` and `focus-preview.sh --hold-flash`.
    - A free-running camera at 11000 µs (89 fps) catches almost every flash, but in one frame
      of ~90. The 10 fps preview would mostly miss it.
    - `--hold-flash` shows the latest frame whose sampled mean is ≥ 3 codes or 20 % above a
      baseline of the unlit frames, for up to 3 s.
    - Four new tests. All 66 preview tests pass on the PC and on the Pi.
    - On the Pi, camera 0 at 11000 µs: live, 89.08 fps, baseline 13.99, no flash frames (there
      is no light).
    - SHA256: `server.py` `c1165b2b…32bac30`, `focus-preview.sh` `a526d962…e6f9488`, the same on
      the Pi.
  - `FOCUS.md` gains the option and a section on focusing by the strobe. `DEVELOPER_GUIDE.md` and
    `HANDOFF.md` list the tool.
- **The preview through a tunnel from the PC works,** but on local port 8766: the PC's own port
  8765 is taken by the owner's design workbench (`tools.design_workbench.launcher --port 8765`,
  running since 11-20).
  - `ssh -4 -T -L 127.0.0.1:8766:127.0.0.1:8765 pitrac2 bash claude-setup/focus_cam.sh 1` →
    `http://127.0.0.1:8766/api/status`: live, 89.08 fps, 11000 µs, `hold_flash` on; the page
    HTTP 200.
  - `~/claude-setup/focus_cam.sh <0|1>` on the Pi configures a fresh 11000 µs config and starts
    the hold-flash preview. Tested on both cameras.
  - `FOCUS.md` now says to use `127.0.0.1` and another local port when 8765 is taken.
- **I was wrong (my slip): GPIO10 went high for 5 ms with both cameras off.**
  - A 100-shot soak's `trigger-capture.sh` refused `--frames 100` (its limit is 50). My command
    did not check for `READY` before sending the shots.
  - The first `cam shot 200` timed out at 5000 µs: `CAM_TIMEOUT: trigger dropped, nothing
    fired, fault latched`. The other 99 were refused.
  - Both cameras were idle, so both modules had VA at 0 V: the case of Step 1's isolation test,
    which I had said I would not run without the analyser. The modules isolate with VA at 0 V,
    so the risk was low, but it was not what the owner allowed ("while both cameras stream").
  - `fault clear` in `RUNNING` cleared only the code: `power_fsm.c` drops the acknowledgement
    outside PS_FAULT. Then `fault : none`, `RUNNING`, the Pi up, latch 1. `hist` faults 1 → 2.
  - **Fixed:** the soak script now sends nothing until the Pi's log shows `READY`, and stops at
    the first shot that is not `FIRED`. Its next run stopped correctly, with nothing sent,
    because the sensors still held the focus test's 11000 µs (`Sensor exposure changed`). Both
    were reconfigured at 1000 µs.
- **Soak, 7c.2 Step 5's dry shot, both cameras in slave mode: 50 of 50 `FIRED: handshake
  complete, dry burst complete`.**
  - `cam shot 200` once a second; `t_cam` 0.240 µs ×44 and 0.207 µs ×6.
  - The Pi: `triggered_frames: 50` on each camera, `free_running_after_ready: no`, `pairs: 50`,
    `status: complete`. `delta_us` 9–10.
  - With Steps 4–5, **62 handshakes on Board 5 with two cameras, every one complete and every
    frame paired.**
- **The Pi link, after all of it (`BENCH_P8_PI.md` 8.8 steps 3–4, from the Pi):**
  - `pitrac-link ping` → protocol 1, version 1.12; `status` → `RUNNING`, fault none;
  - `hist` → faults 2, the last `CAM_TIMEOUT` (the slip above);
  - **1000 pings, 0 failed.**
- **J3 fixed by the owner (13:00, boot 11). Live sessions 2 and 3 PASS, with `strobe_focus.ps1`'s
  fixed path** (logs `captures/strobe_focus_2026-11-24_130043.log` and `_130243.log`):
  - ADC0 at rest 9 codes; `adc ringtest 100` PASS; at gate 0, no current;
  - **the staircase:** nothing to 18 %; 21 % → 0.094 and 0.099 A; 24 % → 0.677 and 0.687 A,
    in band. On 11-23 the figures were 0.103 and 0.703 A;
  - **`strobe cal 9` → level 437 (42.68 %), confirmation mean 9.015 A, session cap 461,** both
    times (11-23: level 436, 8.975 A);
  - then 10 and 6 flashes, 100 µs every 500 ms: 9.053–9.058 A plateau, on 100 µs, every one
    `within limits`. The last pulse peaked at 9.096 A, 0.906 mC;
  - `strobe live off` each time. `fault : none`.
- **The flashes light the cameras** (each free-running at 11000 µs, 89 fps):
  - **camera 0:** 10 lit frames out of 6635, one per flash. Mean 27.0 against a 13.97 baseline;
    brightest pixel 122. A golf ball on the carpet, **far out of focus**;
  - **camera 1:** 6 lit frames, one per flash, mean 34.6; brightest pixel 230. The cal's 20 µs
    steps show too, at 17.6–18.7. The ball's dimples and logo are sharp: **nearly in focus**.
  - Frames: `captures/p7c2_pi_2026-11-24/` (`1301_cam0_*`, `1303_cam1_*`).
  - The browser preview shows raw values with no stretch (by design). Camera 0's ball is
    mid-grey on a darker floor.
- **The lenses (owner, 13:10):** two sets, labelled **A4** and **A5**. One has an 850 nm bandpass
  filter and the other a 940 nm one; which is which is not known. **A4 is fitted** to both
  cameras.
  - The flash reads up to 230 through A4, so A4 passes the strobe's wavelength. A mismatched
    bandpass would leave the flash near black level.
  - The design is 850 nm (D11; `Hardware/Calibration`'s "final 850 nm filter"). The strobe
    LEDs' part and wavelength are not recorded in the repo: asked.
  - Offered: an A/B with one A5 lens on camera 0, comparing the lit frame's mean (A4: +13.1
    codes over the baseline).
  - **The owner focuses with A4.**
- **`tools/focus_meter.py` (new), for the focusing:** a live sharpness score of each full
  flash-lit frame from the hold-flash preview, read through the tunnel. The score is the
  normalised gradient energy in a 500 × 500 box on the brightest area; within 95 % of the peak
  is marked, as in `The_One_Board_To_Rule_Them_All.md` §18.2.
  - **First run, camera 1** (10 flashes): it scored the cal's dim 20 µs flashes at 36–48 (noise)
    and the full flashes at 10.48–10.54. Fixed: only frames within 10 % of the brightest of the
    last 10 are scored, and the peak resets when much brighter frames arrive.
  - **Second run, camera 0** (8 flashes): full flashes 4.11–4.14. One flash lit a frame only
    partly (ROI mean 17.2 against 29.5) and scored 7.14: the case the 10 % rule now skips.
  - An offline check with synthetic frames (dim, full, partial, sharp, blurred) gives the
    expected scores, skips and reset.
  - **The starting points:** camera 0 at ~4.1 (far out of focus), camera 1 at ~10.5 (nearly in
    focus).
  - These runs re-solved 9 A at level 437 (9.022 A) and 436 (8.972 A). Every flash was within
    limits, with `strobe live off` each time.
- **Focused (owner, with the A4 lenses), then the flash width measured (13:51,
  `captures/strobe_focus_2026-11-24_135102.log`):**
  - **`strobe_focus.ps1 -WidthUs` now takes a list** (`"100,20,10"`, with `-Count` pulses at
    each), after one staircase and cal. It is a string, split on commas, because `powershell
    -File` passes a list as one string and an `[int[]]` read it as 1002010. That was refused
    (over 100), so nothing fired. Refusals tested: no `-Count`, a width under 5, a period over
    budget, a non-number.
  - **The session:** staircase 21 % → 0.107 A, 24 % → 0.706 A; `strobe cal 9` → level 436, 8.972
    A. Then six pulses each, every one `within limits`: 100 µs 9.010–9.014 A; 20 µs 8.972–8.978
    A, on 20 µs; 10 µs 8.950–8.954 A, on 10 µs.
  - **Both cameras free-running at 11000 µs** (`flash_widths.sh`, 5301 frames each). Every
    flash landed whole: 6 of 6 at each width on both cameras.
  - **Frame mean above black, 100 / 20 / 10 µs:** camera 0 +14.17 / +2.88 / +1.39; camera 1
    +19.70 / +3.99 / +1.98. That is 0.203× and 0.098–0.100× of 100 µs: linear in width.
  - **The ball** (median of its disk above half its peak, black subtracted; the noise from two
    frames' difference):

    | | 100 µs | 20 µs | 10 µs |
    |---|---|---|---|
    | camera 0 ball | 90.0 codes, noise 1.40, SNR 64 | 17.5, 0.77, SNR 23 | 9.0, 0.48, SNR 19 |
    | camera 1 ball | 89.5, 1.39, SNR 64 | 17.5, 0.74, SNR 24 | 9.0, 0.48, SNR 19 |

  - **Noise against signal** gives roughly 45–55 electrons per 8-bit code at gain 1. So the
    ball collects ~450 e⁻ per pixel at 10 µs and ~900 at 20 µs. SNR 19 at 10 µs is close to
    √450: the light, not the readout, sets it. More gain or 12-bit output would use more codes
    but would not raise the SNR; only more light would.
    **⚠ Wrong: see the 14:06 entry below.** The "noise" is the difference of two flashes, and
    most of it is a ~4 % flash-to-flash term, not photon noise. 12-bit raised the SNR by up to
    29 %.
  - **By eye** (`captures/p7c2_pi_2026-11-24/1351_widths/`, crops brightness-matched to 100 µs):
    - 20 µs: a clean outline and the logo, with faint dimples;
    - 10 µs: a clean outline and the logo just readable; the dimples are lost in the noise.
  - **Focus:** both cameras show the dimples and the carpet fibres across the frame.
    `focus_meter`'s score on a 500 × 500 box on the ball, at 100 µs: camera 0 45.5 (from about 4
    before focusing), camera 1 26.5.
  - **The geometry is not the design's.** The ball measures ~116 px across on camera 1 and
    ~98 px on camera 0. With the design lens (Spinel ND3630, 3.60 mm F/3.0), that puts it ~0.47
    and ~0.56 m from the cameras. The design (`Hardware/Stand/README.md`) has CAM0 376.6 mm
    and CAM1 465.1 mm from the resting ball, and the strobe ~0.42 m from it, aimed at −16°.
    The bench LED bank's position and aim are not recorded. **Illumination goes as 1/d²** from
    the bank to the ball, so these levels hold only for this bench.
- **Camera gain (owner's question, 14:00): there is none to raise.**
  - The driver on the Pi (`pitrac-mira220-nir`, `mira220.c`) fixes analogue gain at min = max
    = 1 (`MIRA220_ANALOG_GAIN_MIN/MAX`) and registers the control with no handler.
  - `v4l2-ctl --list-ctrls-menus` agrees: `analogue_gain min=1 max=1` on both cameras.
  - The Mira220 driver posted for the mainline kernel says the sensor has no analogue gain
    (LKML, 2026-03). Sister sensors (Mira016, Mira050) do have gain steps.
  - Digital gain would only scale values afterwards.
- **8-bit against 12-bit, same scene, 20 / 14 / 10 µs at 9 A, six flashes each (14:06 and
  14:07, both cameras at 11000 µs and 89.08 fps; 12-bit is Y12P, 2400 stride, 3.36 MB per
  frame, no frames dropped):**
  - Every flash `within limits`: 9.02 / 9.01 / 9.01 A, then 8.98 / 8.96 / 8.95 A. The 14 µs pulse
    read `on 16 us` once (2 µs samples; within ±4).
  - **The ball, in 8-bit-equivalent codes above black** (two full flashes per width; their
    brightness ratio is 0.995–1.000):

    | | 20 µs | 14 µs | 10 µs |
    |---|---|---|---|
    | camera 0, 8-bit | 18.0, SNR 21.8 | 12.0, SNR 19.9 | 9.0, SNR 18.2 |
    | camera 0, 12-bit | 17.6, SNR 24.6 | 11.9, SNR 23.1 | 8.75, SNR 22.7 |
    | camera 1, 8-bit | 17.5, SNR 22.7 | 11.5, SNR 19.3 | 8.5, SNR 17.6 |
    | camera 1, 12-bit | 17.3, SNR 24.3 | 11.7, SNR 22.6 | 8.6, SNR 22.8 |

  - **12-bit raises the SNR by ×1.07–1.13 at 20 µs, ×1.16–1.17 at 14 µs and ×1.25–1.29 at
    10 µs**, with no cost in frame rate. In 8-bit, the ball's median lands on whole codes.
  - **Dark noise (two unlit frames):** 8-bit 0.24 / 0.39 codes; 12-bit 0.18 / 0.18
    (8-bit-equivalent).
  - **A flash-to-flash term, unexplained:** on the ball, the two-flash difference grows in
    proportion to the signal, about 4 % of it at every width. That holds the SNR near 23–25 from
    10 to 20 µs.
    - It is **not** the flash's total (the scale-matched difference is unchanged).
    - It is **not** movement: phase correlation finds 0.001–0.06 px shifts, and realigning
      changes nothing.
    - Unresolved; the LED bank's illumination pattern varying from flash to flash is one
      candidate. A single frame's own noise on the ball is lower than these SNRs suggest.
  - **I was wrong** (13:51, above): "only more light would" raise the SNR rested on this
    two-flash noise, and 12-bit has now raised it.
  - Crops: `captures/p7c2_pi_2026-11-24/1406_depth/`.
  - **Today's scripts are kept in `captures/p7c2_pi_2026-11-24/scripts/`** (gitignored, local):
    - on the PC: `run_widths.ps1`, `soak_shots.ps1` (gated on `READY`), `sample_strobes.ps1`,
      `board_cli.ps1`, and the analyses (`depth_analyse.py`, `near_analyse.py`, `shift_test.py`,
      `focus_compare.py`);
    - for the Pi (`pi/`): `flash_widths.sh` (8/12-bit, keeps lit frames) and `focus_cam.sh`.
    - The Pi keeps its copies in `~/claude-setup/`.
- **The ball moved ~7 cm closer (owner), 12-bit, 6 flashes each at 100 / 20 / 14 / 10 µs (14:19,
  `captures/p7c2_pi_2026-11-24/1419_near/`):**
  - All 24 flashes `within limits`: 9.06 / 9.02 / 9.02 / 9.00 A.
  - Each camera missed one flash out of 24, a flash falling between two frames at 11.0 ms of
    11.23 ms. The analysis now places flashes by their measured period (~508 ms: Windows sleeps
    round up), so a missed one does not shift the groups.
  - **Focus held:** at native resolution, before (13:51) and after, the dimples, the printed
    text and the carpet fibres are as sharp on both cameras. No refocus.
  - **The ball is larger:** ×1.17 across on camera 0 (~98 → ~115 px) and ×1.20 on camera 1
    (~117 → ~140 px), measured on the crops. So it is ~15–17 % closer to each camera. With a
    7 cm move, that puts it at roughly 0.35–0.41 m now, near the design's 0.38–0.47 m. Both
    estimates are rough.
  - **The ball's brightness, 12-bit, in 8-bit-equivalent codes:** 100 µs ~115–117 (SNR ~73);
    20 µs 23.3 / 22.8 (SNR 29.7, from 25–26); 14 µs 15.4 / 15.1 (SNR ~23.5); 10 µs 10.9 / 10.8
    (SNR ~24). **×1.18–1.30 brighter than before the move.**
  - The automatic radius and limb-edge numbers from `near_analyse.py` are **not valid**: its 35 %
    threshold took in lit carpet. Not used.
- **Left:** Board 5 `RUNNING` (boot 11), the Pi up, live OFF, gate 0, beam off, `fault : none`.
  No preview or capture is running. Both cameras are focused (A4) and left configured at 12-bit,
  11000 µs: reconfigure before any 7c.2 step.
- Not committed.

---

## 7. Source layout

```
firmware/
  CMakeLists.txt              build; PICO_BOARD=pitrac_ltb_v1, PICO_PLATFORM=rp2350
  pico_sdk_import.cmake
  boards/pitrac_ltb_v1.h      RP2354B board header (48 GPIO, 2 MB internal flash, 12 MHz XOSC)
  src/board.h                 PIN MAP — single source of truth. Netlist-verified. Start here.
  src/main.c                  init order + core 0 superloop
  src/safe_state.[ch]         GPIO safe defaults + fault latch. Called first in main().
                              The ONLY writer of GPIO27 PULSE_LIMIT_DISABLE (held 0).
  src/service.[ch]            pitrac_service() / pitrac_yield_ms(): what every blocking
                              command must call; key-abort; watchdog (off by default)
  src/pio_alloc.[ch]          PIO block / state-machine / GPIOBASE assignments
  src/adc_engine.[ch]         ADC modes (IDLE/ARMED/BURST), 32 KB DMA ring (A1), block and
                              triggered capture, volts helpers, +5V_IN scale
  src/power_fsm.[ch]          Phase 1 latch + USB guard + Phase 1b Pi soft-shutdown FSM
  src/panel.[ch]              Phase 1c J7 indicators — ring PWM; ready LED SIO on/off (A7)
  src/beam.[ch]               Phase 2 carrier + phase-locked demod clock, effective-duty ceiling
  src/detect.[ch]             Phase 3/4: threshold DAC, gated HPF, comparator PIO timing,
                              pass coalescing, pass log + retained waveforms, `hpf test`
  src/detect.pio              comparator edge timer (PIO2 SM0, GPIOBASE 16)
  src/cal.[ch]                Phase 3: `level`, `cal demod`, `cal model`, `scan carrier`,
                              `cal gain`
  src/config_store.[ch]       dual-slot, CRC'd, versioned calibration record in flash
  src/telemetry.[ch]          Phase 8.6 halt telemetry: counters, a two-copy no-init RAM mirror
  src/link_frame.[ch]         the Pi link's framing (0xA5 len type seq payload crc16), the
                              byte decoder, the RX ring's settle rule. Pure -- host-tested
  src/link_proto.[ch]         the allowlist of Pi requests and their answers. Host-tested
  src/pi_link.[ch]            UART1 921600 8N1: RX DMA ring, polled TX, one request at a time
  src/fw_update.[ch]          firmware updates from the Pi: RAM staging, install into the
                              other A/B partition, the trial and buy at boot, rollback report.
                              Logic only -- host-tested on a fake flash
  src/fw_layout.[ch]          where this image runs (boot ROM partition table and boot info),
                              and the flash / buy / reboot calls behind fw_update.c
  src/sha256.[ch]             SHA-256 in software. Pure -- FIPS 180-4 vectors
  src/shot.[ch]               shot sequencer: test shots (camera handshake + dry gated burst)
                              since 1.4; a detected pass ends "not implemented"; deadlines
  src/camera.[ch]             Phase 7: the handshake engine on PIO1 -- start, poll, safe-off,
                              hardware readback for `cam`
  src/cam_plan.[ch]           Phase 7: the handshake's arithmetic (counts, words -> t_cam and
                              FIRE). Pure -- host-tested, and against the PIO program
  src/cam_handshake.pio       Phase 7: trigger, both strobes, t_cam, FIRE, timeout (PIO1 SM0)
  src/strobe_gate.pio         Phase 7: one `wait 1 irq 4` just below the burst program
  src/strobe.[ch]             Phase 6: PIO0 burst engine + DMA, gate DAC, safe-off, status;
                              LIVE mode (6c/6d): arm/disarm + watchdog, ADC0 readback per
                              firing, verdicts -> faults, hold checks, `strobe cal`
  src/strobe_plan.[ch]        Phase 6: §15 schedule, charge interlock, PIO encoding, DRY and
                              LIVE admission policy (staircase, ceiling). No hardware access --
                              compiled into the host tests too
  src/strobe_live.[ch]        Phase 6c: ADC0 plateau/verdict math, rolling charge budget,
                              interval, idle timeout, cal steps. Pure -- host-tested
  src/strobe_burst.pio        Phase 6 PIO burst engine, loaded by strobe.c
  src/cli.[ch]                USB-CDC line CLI, incl. `capture` (the bench instrument).
                              Every numeric argument goes through parse_u32/parse_float
  tests/                      NATIVE host tests (separate CMake project, not an RP2350
                              target): 265 cases on 2026-11-10 -- power FSM 44, strobe plan
                              17, strobe live 59, service 9, ADC ring 8, telemetry 16, config
                              store 12, link 13, update 51, camera plan 4, camera PIO 15,
                              engine 17; mocks/; pio_sim.c runs the PIO programs (pioasm
                              output) in a cycle model; sdk_standin/ lets engine_test run the
                              unchanged strobe.c/camera.c/shot.c. HANDOFF section 4 says how
  pi/                         the Pi's side: RPI5_ON unit, shutdown hook, journald, and
                              pitrac_link.py (the link and the updater), with tests
  partition_table.json        the A/B partition table (build/pitrac_pt.uf2), once per board
  tools/scope.py              plots a `capture` block from the CLI; `--roll`, `--trig`
  tools/pilot_analysis.py     one triggered ADC5 pass (`capture trig 5 ...`): windowed stats,
                              peak, 0.2 ms-box FWHM, rail check; JSON + SVG next to the input
                              (§3.7; reproduces the 2026-09-18 pilot: 3.102 V, 22.71 ms)
  tools/check_doc_links.py    every relative link and #anchor in the firmware docs; exit 1
                              on a break
  tools/la_phase.py           Phase 2a checks 1-7 from a 2-channel LA CSV. Auto-identifies
                              carrier vs demod by duty; circular statistics throughout
                              (sign convention and wrap both matter -- see its docstring);
                              `--compare` does the check-7 run-to-run spread
  tools/netlist_report.py     regenerates ../../HARDWARE_REFERENCE.md from the KiCad
                              netlist; `--check` fails if it is stale
  tools/openocd_pi5.cfg       SWD from a Pi 5 via linuxgpiod
  tools/flash_swd.sh          refuses from image 1.1 on unless told otherwise (its header)
```

`src/board.h` carries the load-bearing hardware comments. Read it before touching anything.

---

## 8. Decisions made (so they aren't re-litigated)

- **C / Pico SDK 2.x**, not Rust — matches the .md pseudocode, first-class PIO/DMA/dual-core.
- 🔵 **Carrier = 104.1667 kHz, FIXED for every board and every user** (sysclk 150 MHz,
  TOP=1439, level=432 → exactly 30.000 %). **Decided 2026-08-28.** It is *not* scanned per
  board, per user or per site.
  - **Cost:** possibly a few percent of SNR on any individual board.
  - **Buys:** one `board.h`, one calibration matrix, one set of expected numbers, field
    replaceable boards, and a support story that can be reasoned about. A per-board carrier
    would need a 10-minute scan, a `cal model` re-fit and a recorded frequency **per board**,
    repeated on every replacement.
  - **Margin, computed 2026-08-28 — ⚠ SUPERSEDED BY MEASUREMENT 2026-08-31:** a square-wave
    demodulator responds at odd harmonics, so interference folds to |f_i − n·f_c|. Against the
    LM5157's *assumed* nominal 1.055 MHz the nearest odd harmonics are **9·f_c = 937.5 kHz
    (117.5 kHz)** and **11·f_c = 1145.8 kHz (90.8 kHz)**, giving **−9.6 % / +7.1 %** of drift.
    ⚠ **Both the nominal and the margin were wrong.** The only switcher tone on the +5 V rail
    is at **801 kHz** (dithered, 762.9–833.1 kHz), which puts the **7th** harmonic 33.8 kHz away
    and cuts the paper margin to **−2.3 %**. ✅ **And then the paper stopped mattering:**
    `scan carrier` put the 7th harmonic *inside* the switcher band at three points and σ_noise
    stayed flat, so the coupling is too weak to fold at all. **The method here is right; the
    numbers are history. Trust the direct measurement.**
  - ⚠ **You cannot choose the collision away.** Odd harmonics are 2·f_c = 208 kHz apart, so
    *any* carrier near 100 kHz has one within ±104 kHz of the boost. The choice only decides
    how far, which is why §3.6 is now a verification and not an optimisation.
  ✅ **Validated on hardware 2026-08-13:** phase lock reproducible to 0.15 ticks, duty exact
  to 250 kHz, peak current 3.11 A.
- **Peak beam current is set by the rail, LED Vf and ballast — not by duty** (measured:
  2.92–3.02 A flat across an 8→30 % ramp). Duty controls only how often. This is why the
  ramp is safe by construction rather than by luck, and why `beam duty` is the wrong knob
  if peak current ever needs changing — that takes a different ballast.
- **The beam operating point is thermally stable but optically is not.** Cold → 85 °C
  plateau moves current +1.7 % and power +0.6 %, because at 3 A most of Vf is I·Rs and series
  resistance rises with temperature, opposing the bandgap term (measured tempco ≈ −0.5 mV/K
  for the stack, ~7× smaller than the low-current figure). **But radiant efficiency still
  falls ~0.3–0.6 %/K**, so ~25–45 % of the light is lost over a 76 K junction rise — and
  none of it is visible electrically. **Phase 3 must calibrate warm.**
- **ADC while armed = round-robin {ch5, ch7} at 250 ksps each**, resolving the .md's unstated
  conflict between the detect signal and the mic both wanting to free-run.
- **Trigger = comparator starts the timing, ADC refines during the camera-handshake dead
  time.** Gets the comparator's latency and the ADC's amplitude-independent accuracy for
  free. **Time the transit with a PIO state machine, not a GPIO ISR** (`ARCHITECTURE.md` A2)
  — one FIFO word gives the interval directly, with no handler and no jitter.
- **The CPU orchestrates; hardware executes.** Anything that must happen at a *specific
  time* goes to PWM/PIO/DMA/ADC. Anything that merely has to happen *soon* stays on a core.
  Full audit and allocation table in `ARCHITECTURE.md`.
- **RPI5_SHUTDOWN active-low**, matching the `gpio-shutdown` overlay default. ✅ Confirmed on
  hardware 2026-07-31 — a 200 ms low pulse. Note the *reason* originally given for this choice
  ("inherently safe through reset") was **wrong**; see §2 and Q10. The choice stands, the
  justification does not.
- **Pi presence is a detection WINDOW, not a single sample** (2026-07-31). It used to be one
  read of `PI_3V3_SENSE` at exactly `RAIL_SETTLE_MS` (250 ms) — which conflated the boost's
  soft-start time with how long a Pi 5 takes to raise its header 3V3, two unrelated things.
  A merely-slow Pi was classified as absent, landing in `BENCH_RUNNING`, where the next
  button press is a hard `FORCE_OFF` — a power cut on a booting Pi, i.e. the exact SD
  corruption this subsystem exists to prevent. Now: poll for `PI_DETECT_WINDOW_MS` (3 s),
  **plus** a debounced late-detect promotion from `BENCH_RUNNING` → `PI_BOOTING` so the
  window's exact value is not safety-critical. **`BENCH_RUNNING` stays** — it is the normal
  bench path for phases 2–6 and belongs in the shipping firmware.
- **`fault clear` is a full acknowledgement**, not just a code reset (2026-07-31). It clears
  the code *and* leaves `PS_FAULT` via `FORCE_OFF`, exactly as a button press does. Clearing
  only the code left the FSM latched in `PS_FAULT`, so the panel ring (which keys off state)
  kept double-blinking while the on-board red LED (which keys off the code) went dark.
- **Automatic panel patterns run on time-since-state-entry**, not free-running time, so a
  short-lived state always shows the *start* of its pattern instead of an arbitrary slice.
- **Watchdog reset action enabled only while the Pi is down**; software supervision while RUNNING,
  because a watchdog reset drops GPIO15 → R12 pulls low → Pi power yanked.
- 🔵 **Live gate ceiling: a session ceiling from the calibration (owner, 2026-10-16; option c of
  §9's finding).** Firmware change queued (§9); the 70 % compile-time ceiling stays as the outer
  bound.
- 🔵 **VIR boost on the next board: keep hiccup, and add a VIR monitor (owner, 2026-10-16;
  CR-25 options 1 and 5).** The monitor's divider must draw little current (`NEXT_BOARD_REV.md`
  CR-08).
- 🔵 **`BURST_CHARGE_MAX_MC` stays 6 mC (owner, 2026-10-16).** On board 1, the 5.4 mC slow-ball
  burst left ~3.5 V across Q9, and a 6 mC burst should leave ~2.5 V against its ~1–1.5 V need
  (§6, 6c Step 5). The readback's shape check is the guard if a weaker bank runs short.
- 🔵 **Phase 6 thermals closed on board 1 (owner, 2026-10-16).** No temperature rise on Q9, HS1
  or any other part, across every burst up to 5.4 mC. Heatsink coupling cannot be shown with
  these loads (~45 mJ into Q9 per burst). Thermals are sane.
- `cal_demod_phase()` in .md §13.8 is **buggy** (samples ADC5, which is after the 0.66 s HPF, with a
  *static* reflector → reads noise at every phase). Use the chopped-beam differential method instead.
- 🔵 **Firmware updates from a board-powered Pi are done in firmware, on this board (owner,
  2026-10-19).** The image goes into the other half of an A/B flash pair, try-before-you-buy,
  with one reset after the Pi is off; no latch hold-up hardware. Written 2026-10-20 (§6).
  Open within it: the framing, for the shot messages (§9).
- 🔵 **A Pi on J8 for Phase 8 before phases 2–7 are complete (owner, 2026-11-09).** Phase 8's
  gate said "phases 2–7 complete", and the safety rules said "no Pi on J8 until Phase 7c".
  Both are relaxed for `BENCH_P8_PI.md` 8.0–8.5, 8.8 and 8.9, which touch only power,
  shutdown and the serial link.
  - **Why it is safe enough:** everything that protects the Pi is verified on board 1: the
    full Phase 1b matrix against a simulated Pi, the `reset`/`bootsel` guard, the closed USB
    reboot paths and J8.37 released with no Pi up (§8.0b), and updates with rollback (§8.7).
  - **Kept:** no camera on J4 (CR-09); beam off and strobe dry with the Pi seated (except
    `BENCH_P8_PI.md` §8.10, approved 2026-11-12, below); SW2 taped
    over; the Pi's own USB-C unplugged; every other phase with the Pi off the board.
  - Changed in `CLAUDE.md`/`AGENTS.md`, `HANDOFF.md` §3, §1 here, `DEVELOPER_GUIDE.md` and
    `BENCH_P8_PI.md` §8.0.
- 🔵 **After an unrequested Pi halt, the rail stays up indefinitely (owner, 2026-11-09).** §8.2
  showed the Pi 5's header 3V3 stays up at halt, so the board sees a halt only on RPI5_ON. It
  counts it and announces it; `off` or the panel button ends the session in the usual way.
  - **Why not drop the rail after a timeout:** a `sudo reboot`, or a slow boot (an fsck, an
    update), also reads down for a while, and a timed cut would land on a booting Pi. Keeping
    the rail up costs only the idle power of a halted Pi and the board.
  - No code change: this is what the firmware already does (`PI_DOWN_DEBOUNCE_MS`,
    `watch_pi_down()`).
- 🔵 **`PI_SHUTDOWN_MIN_HOLDOFF_MS` = 10 s from image 1.3 (owner, 2026-11-10).** The Pi read
  down ≤ 2.5 s after a request (§8.1, §8.5). 10 s is ~4× that, more than the exit criteria's
  ~2×, as margin for a busier Pi.
  - It is a floor: past it the latch waits for RPI5_ON, up to 60 s.
  - Re-measure once the PiTrac software runs on the Pi.
- 🔵 **This revision's five prototype boards are brought up by hand; the guided calibration
  waits for the next revision (owner, 2026-11-12).**
  - By hand means `BRINGUP_NEW_BOARD.md`.
  - The script's plan, and the hardware that makes it instrument-free, are
    `NEXT_BOARD_REV.md` CR-29: an I2C housekeeping ADC with its own reference, two PIO
    timing pins and CR-28's temperature sensor.
  - **SW2 is a developer's button, never pressed in normal operation** (owner, the same
    day). The guided flow has no SW2 step; Step 5.4 stays a developer check.
- 🔵 **Live strobe mode does not arm the hardware watchdog while a Pi is powered (owner,
  2026-11-12).** Without a Pi it is armed as now.
  - A watchdog reset opens the latch: a hard power cut to the Pi.
  - U5 caps every pulse at ≤ 137 µs and the PIO caps every burst, so a hang cannot hold
    current on (§9, "The strobe with a Pi present").
  - For image 1.8.
- 🔵 **`BENCH_P8_PI.md` §8.10 approved (owner, 2026-11-12):** live strobe current with a Pi
  seated, on board 1, as written there.
  - Image 1.8's review did NOT PASS (V18-F1, 2026-11-12). The owner's approval remains
    recorded; the test stays blocked until a corrected candidate passes independent review.
    Image 1.9 closes V18-F1/N1 but its overall review did NOT PASS (V19-F1, 2026-11-12;
    §6, "Image 1.9"). Hold the step for a corrected independently reviewed candidate.
    Image 1.10 closes the entry-time case but did NOT PASS confirm review (V110-F1:
    reset/bootsel do not recheck after their 50 ms wait; §6, "Image 1.10").
    Image 1.11 confirm review PASS closes V110-F1 (§6, "Image 1.11"); software gate met.
    Its real-chip reset/BOOTSEL check and §8.10 hardware acceptance both passed on board 1,
    2026-11-12 (§6).
  - It extends the 2026-11-09 rule above ("strobe dry with the Pi seated") for that step
    only. The rest stands: SW2 taped over, the Pi's own USB-C unplugged, no camera on J4,
    beam off.
- 🔵 **A Pi on J8 and a camera on J4 for Phase 7c (owner, 2026-11-13).** The rules said "a Pi on
  J8 only for Phase 8" and "no camera on J4". Both are lifted for `BENCH_P5_P7_MIC_CAMERA.md` 7c.
  - **Kept:**
    - the camera only through a TXS0108E module (CR-09), wired as in `WIRING_DIAGRAM.md` §7
      and `BRINGUP_NEW_BOARD.md` Step 13.4;
    - SW2 taped over, and the Pi's own USB-C unplugged;
    - cables, the module and probes fitted with the PSU and USB off;
    - beam off, and the strobe dry (the handshake's burst is dry by design);
    - every other phase with J4 empty, and the Pi off the board unless its procedure says
      otherwise.
  - Changed in `CLAUDE.md`/`AGENTS.md`, `HANDOFF.md` rules 3 and 6, §1 here,
    `DEVELOPER_GUIDE.md`, `ARCHITECTURE.md` and the 7c section.
- 🔵 **The detected-shot path: D1–D8 (owner, 2026-11-20).** In full in `SHOT_PATH_PLAN.md` §3a.
  - **D3 changes a reviewed live-strobe limit, by the owner's decision.**
    - The live span limit rises from 30 ms to 150 ms.
    - The readback watches the first 30 ms. Later pulses fire unwatched, but are booked and
      counted against the 6 mC per burst and the pacing.
    - It is made in WP5, under independent review. Until then `STROBE_LIVE_SPAN_MAX_US` stays
      30 ms.
  - The rest:
    - D1: measure, then core 0 or 1;
    - D2: switch to BURST at the trailing edge;
    - D4: upstream PiTrac's driver pattern as the live fallback;
    - D5: the workbench regenerates its table under the firmware's rules;
    - D6: the analog mic for after the shot only;
    - D7: the table compiled in;
    - D8: pulse 0 fixed at a width that keeps a 100 m/s shot sharp.
- 🔵 **A TXS0108E per camera (owner, 2026-11-20).** The documented single module took its 1.8 V
  from camera 0 only. Because a Mira220's 1.8 V is up only while it streams, that module would
  drive camera 1's pins while camera 1 was unpowered.
  - Each module's VA and OE come from its own camera's P1.5. Each module bridges channel 5 onto
    its trigger channel.
  - Changed in `WIRING_DIAGRAM.md` §1 and §7, `BRINGUP_NEW_BOARD.md` Step 13.4, the technical
    guide and `BENCH_P5_P7_MIC_CAMERA.md` 7c.2.
  - **Kept, 2026-11-21 (owner),** after two alternatives were considered (§6, 2026-11-21):
    - a resistive divider on the trigger with the strobe direct;
    - one module, its OE driven from camera 1's 1.8 V.
- 🔵 **Board 5: most of the remaining bring-up skipped (owner, 2026-11-23).**
  - **Done:** §0–§5, every gate that protects a Pi, and the R80 check.
  - **Deferred until Board 5 needs them:**
    - §6–§11 (beam and optics);
    - §12 (strobe current);
    - §13's rollback and shipping steps;
    - `BENCH_P8_PI.md` §8.0b (the simulated Pi).
  - **This overrides two rules for this board only:**
    - `BRINGUP_NEW_BOARD.md` ("finish §6–§11 before any optical or Pi work");
    - `BENCH_P8_PI.md` §8.0's per-board §8.0b and rollback lines.
  - **Why the rollback is skipped:** a rollback test on Board 5 now would cost its fallback.
    Loading the test image into B overwrites r5. Buying anything not newer than 1.12 back into
    B then erases 1.12 in A. Board 1 has already shown the boot ROM's rollback (§8.7).
  - **Kept before a Pi:**
    - `link` in STANDBY;
    - SW2 taped;
    - the first seating's R39 check;
    - one clean `on`/`off` cycle with the Pi, and the link.
  - **Guards until the deferred sections run:**
    - the beam at its 2 % start or off;
    - J3 disconnected, and no `strobe live`.
  - **Changed the same day (owner): §6–§12 now run on Board 5 before the Pi,** with no
    soldered leads.
    - That is the beam and detection calibration (§6–§11) and the strobe (§12).
    - Still deferred: §13's rollback and shipping steps, and §8.0b's simulated Pi.
    - The Pi will be board 1's, with its overlays and `pitrac-link` already installed.
    - **No logic analyser either, unless it is essential (owner).** The test points are read
      with a hand-held DMM **from the back**. The PCB confirms that is safe (§6, 2026-11-23).
      - §6.2: by readback and §9;
      - §6.3: by the DMM method now in `BRINGUP_NEW_BOARD.md` Step 6.3;
      - Step 12.2 part 5: TP3 from the back;
      - Step 12.2 part 6 is skipped. U5's clamp is measured with current, in 12.3 part 4.
    - **The first seating's R39 check skipped (owner, 2026-11-23),** with its no-clip
      equivalent (J8.37 and J8.10 through jumper wires in the empty socket).
      - What it would have caught: a fault on this board driving J8.37 or J8.10 into the
        unpowered Pi. R39's 1 kΩ limits that to ~2.6 mA.
      - The firmware side is shown: `pins` reads `RPI5_SHUTDOWN(43)` `released`, and `link`
        reads `released`. 1.12's machine code is 1.11's, and board 1 read 0 V across R39.
      - The guard left: with the Pi seated and unpowered, `stat` in `STANDBY` must read
        `pi : present 0  3v3 0` before `on`. A Pi back-powered through its GPIOs would show
        `3v3 1`.
    - **Step 6.4 assumed OK (owner, 2026-11-23):** no FLIR on hand. The beam LED's temperature
      at 25 % is unmeasured on this board, though it ran at 25 % through §8–§10. Measure it
      when the FLIR is back, before long sessions at 25 % (Board 4: 92.9 °C; limit 98 °C).
- 🔵 **Board 5, 2026-11-24 (owner), before an hour away from the bench:**
  - **Both modules' VA (and OE) checked with the power off,** each to its own camera's P1.5
    only. Trigger tests (GPIO10 high) may run while both cameras stream.
  - **Live strobe allowed up to 9 A with nobody at the bench,** under the firmware's guards
    and the 25 % duty rule. The LED bank on J3 is rated **9 A pulsed, 1 A continuous** (owner).
  - **No analyser:** 7c.2's analog checks wait for the owner.

---

## 9. Not done / deliberately deferred

- **Direct-TIA ADC sampling / onboard digital demodulation — deferred by owner
  2026-10-02.** Discussed carrier-referenced ON/OFF sampling and approximately
  208.33 ksps acquisition after the TIA gain rework. The owner chose not to pursue it
  at present. Keep the existing analog demodulation chain; no sampling prototype,
  firmware change, hardware change or new measurement resulted from this discussion.
  This is a prioritization decision, not a finding that digital demodulation is impossible.
- **Closed or superseded items moved 2026-10-05** to [`PROGRESS_ARCHIVE.md`](PROGRESS_ARCHIVE.md)
  §9, verbatim: first build, git init, USB-only exercise, watchdog/`adc5vcal`/flash-persistence
  plans, the `reset`/`bootsel` guard, `adc_read_avg()` (A1), the beam ceiling fix, the A7 slice
  collision and the PIO-vs-CPU note. Their still-live consequences are restated below.
- **Watchdog:** `wdog on|off` exists and is **off by default**. On this board a watchdog reset
  is a hard power cut (GPIO15 high-Z → R12 opens the latch). **Since 2026-10-07 live strobe mode
  arms it itself** (`strobe live on confirm`) and disarms it on exit; `wdog off` is refused
  while live.
- **Before a Pi is seated (a Phase 8 gate; found 2026-10-16, §6):** two firmware changes,
  each through the usual independent review, host tests and a deliberate mutation.
  🟡 **Both written 2026-10-19** (§6, "Phase 8 prep firmware written"), in image
  `Oct 19 2026 11:44:38` (r2, after the review's two findings): waiting for the confirm pass, not flashed. The bench checks are `BENCH_P8_PI.md`
  §8.0b. J8.37 is **released**, not driven low, in STANDBY (§6 says why).
  - **Close the USB reboot paths.** Build with `PICO_ENABLE_USB_RESET_VIA_BAUD_RATE=0` and
    `PICO_ENABLE_USB_RESET_VIA_VENDOR_INTERFACE=0`. Today a 1200-baud open or `picotool … -f`
    reboots the RP2354 past the `reset`/`bootsel` guard: with a Pi up, the hard power cut.
    - Cost: `picotool -f` can no longer reboot a running board. Flash as now: `bootsel` at the
      CLI (guarded), or SW1/SW2.
    - Bench check afterwards: a 1200-baud open and `picotool reboot -f` change nothing.
  - **Stop back-feeding an unpowered Pi through J8.37.** Drive RPI5_SHUTDOWN to its inactive
    level only while the latch is closed, and low in STANDBY. Raise it with the latch, so the
    Pi never powers up into an asserted request.
    - Check: the first-seating step in `BENCH_P8_PI.md` (≈ 0 V across R39 in STANDBY).
    - UART1's TX follows the same rule, and also needs RPI5_ON high (written 2026-10-20, §6).
- **Left open in the 2026-10-19 firmware, on purpose (§6):**
  - The Pi going down unrequested is counted and shown, not acted on. Whether the board should
    then drop the rail waited on 8.2. **Answered 2026-11-09: the header 3V3 does not fall at
    halt**, so a halted Pi keeps the rail up indefinitely and only RPI5_ON shows it.
    **Decided 2026-11-09 (owner, §8): keep the rail up.** `off` or the button ends it.
  - The session cap is RAM only, like the cal; persisting it belongs with Phase 7.
  - A fault raised before `telemetry_init()` in `main()` is not counted.
  - **The released-J8.37 label** says `no Pi 3V3 sensed` even when the latch being open is
    the reason (seen on the bench 2026-10-30, §6). Reword it, e.g. `released (latch open or no
    Pi 3V3: nothing driven into J8.37)`, with the next firmware change, and update
    `BENCH_P8_PI.md` §8.0b Step 3's expected text with it. **Not in 1.2** (2026-11-10): that
    image changes only the version, on purpose (§6). **Fixed in 1.3** (built 2026-11-10;
    on board 1 once `BENCH_P8_PI.md` §8.9b passes).
  - **The Pi tool's `NOTE` for an image no newer than the running one** (`pi/pitrac_link.py`,
    `cmd_update`) says the buy erases the old image, leaving no fallback. That holds only when
    the old image would otherwise win the next normal boot: an older new image, or an equal
    one with the old image in A. Reword it with the next Pi-tool change. The docs were
    corrected on 2026-11-10 (§6).
  - **`this boot` after a flash update boot reads `power-on, brownout, RUN pin (SW2) or
    debugger`** (seen 2026-11-10, §8.9, beside `loaded : RAM mirror (warm reset)`).
    - The cause: `boot_reason()` uses the SDK's `watchdog_caused_reboot()`, which on the RP2350
      also requires a NORMAL boot type. An update from the Pi boots as FLASH_UPDATE, and so, by
      the same rule, does a UF2 load.
    - Only the label is affected: `boots` counts every boot, and `boots after a watchdog hang`
      uses `watchdog_enable_caused_reboot()`, which does not test the boot type.
    - Fix with the next firmware change:
      - read `rom_get_last_boot_type()` in `boot_reason()`, and give a flash update boot its
        own label;
      - reword `TELE_BOOT_SOFTWARE`'s label, which names "a firmware update";
      - update the quoted text in `BENCH_P8_PI.md` §8.0b Step 5 if that label changes.
    - **Fixed in 1.3** (built 2026-11-10; §6), all three, including the quoted text.
  - If an older firmware ever saves over a `telemetry only` record, the label survives and is
    wrong. It is only a label.
  - The board writes its config sector on its own about once per boot (after 10 s in
    STANDBY). That is ~100k boots per sector before wear matters, and the two sectors alternate.
- **`adc5v` scale:** compile-time default 1.063; `adc5vcal` trims it in RAM and **`cfg save`
  persists it** with the calibration (board 3: 1.0620).
- **`beam clamp` leaves the duty at 50 %.** `beam_configure()` clamps every caller to the
  **35 %** effective ceiling, but that is above the **25 %** operating point (CR-12): type
  `beam duty 2` straight after `beam clamp`.
- **PWM slices:** A7 is fixed in firmware (GPIO12 ready LED is SIO on/off; slice 6A belongs to
  the strobe gate DAC on GPIO28). Two other pairs are safe **only** while SIO: **GPIO15 (latch)
  shares 7B with the beam carrier** and **GPIO27 (strobe watchdog defeat) shares 5B with the
  panel button LED** — never put either on PWM.
- **Core 1 is unused.** When it is launched, `flash_safe_execute_core_init()` must run on it
  (`config_store.h`), or a `cfg save` hard-faults.
- **PIO:** the camera handshake is written (2026-11-10, image 1.4, PIO1; `ARCHITECTURE.md` A3).
  PIO1 is now 30 of 32 instructions; the I2S mic, if built, goes in PIO0 SM1 (19 free).
  No mic onset engine yet.
- **Phase 7, left for later (2026-11-10, §6):**
  - **A shot from a detected pass:** it needs the ball speed (reported velocity is not written),
    the S15 schedule from it, and the first delay from the beam-to-FOV geometry. Until then a
    pass ends `NOT_IMPLEMENTED` and `shot_arm()` has no caller.
  - **Live current from the shot path:** the live readback (ADC0 in BURST around a firing whose
    moment the CPU knows) does not fit a firing the camera times, and the A9 ordering applies. A
    deliberate change, with its own review.
  - **The strobe with a Pi present** (7c, and first-seating step 5): every firing is refused while
    a Pi is present. Lifting it is a safety decision for the owner. Its first bench test is the
    +5 V dip under a burst with the Pi up.
    - **The owner asked to unlock it for the Board 4 collaborator (2026-11-11).** What it
      takes:
      1. **Firmware:** drop `STROBE_REFUSE_PI` from the dry and live admission checks
         (`strobe_plan.c` `common_checks()` and `strobe_live_may_continue()`), with host tests,
         a deliberate mutation and an independent review.
         - **Built as image 1.8 (2026-11-12; §6, "Image 1.8")**, with the watchdog rule
           below and an intended live-session Pi binding. **Review NOT PASSED (V18-F1):**
           cached `power_pi_present()` misses header-3V3 changes. Add real FSM/strobe
           regressions and mutations, then review a corrected candidate before any flash.
           Also correct V18-N1: stale no-Pi/watchdog comments in `strobe.h`, `service.h`
           and `main.c`. Preserve rejected 1.8 bytes.
         - **Corrected as image 1.9 (2026-11-12; §6, "Image 1.9"):** the Pi observed
           fresh, UNSETTLED on disagreement; real-FSM tests; V18-N1. V18-F1/N1 confirmed
           closed, but overall review NOT PASSED on pre-existing V19-F1: plain reset,
           bootsel and config-save guards must also conservatively account for fresh
           3V3. Add real-FSM regressions/mutations before another confirm review.
           Qualify the source text about active-firing service deferral and persistent
           UNSETTLED recovery at the same time; preserve reviewed 1.9 bytes.
         - **Corrected as image 1.10 (2026-11-12; §6, "Image 1.10"):** `reset`, `bootsel`
           and `cfg save` count a Pi's 3V3 sensed now as a Pi (`power_pi_maybe_present()`);
           real-FSM regressions, 25 mutations, both text qualifications. **Confirm review
           NOT PASSED (V110-F1):** revalidate permission after the 50 ms output wait,
           immediately before disarming/rebooting; a newly refused command must preserve
           the prior watchdog state. Add both-command POWERING_ON and late-rise
           regressions plus a deliberate mutation before the next confirm review.
         - **Corrected as image 1.11 (2026-11-12; §6, "Image 1.11"):** checked again
           after the 50 ms wait, the watchdog disarmed only then, no plain reset in
           POWERING_ON; `fsm.reset_rise_in_wait`, 30 mutations. Confirm review PASS:
           V110-F1 closed; V19-F1 closed at the reviewed guard sampling points.
           The post-sample/ROM and manual-watchdog limits remain explicit in the report.
      2. **A watchdog decision for live mode with a Pi powered.**
         - Live mode arms a 1 s hardware watchdog (`strobe_live_arm()`). On this board a
           watchdog reset opens the latch: a hard power cut to the Pi (`service.h`).
         - With U5 capping every pulse at ≤ 137 µs and the PIO capping every burst, a hang
           cannot hold current on. So the watchdog buys little against that cost.
         - **Decided (owner, 2026-11-12):** not armed while a Pi is powered; armed as now
           without one (§8).
      3. **The bench test, on a board with leads** (board 1). Written as
         `BENCH_P8_PI.md` §8.10 (2026-11-12), approved by the owner the same day, to run
         on a corrected reviewed image (1.8 and 1.9 did not pass):
         - +5 V on J7.5 (a jumper lead; J8 is under the Pi), triggered from U5 Q;
         - at the solved 9 A level: `strobe pulse 20`, `strobe pulse 100`, `strobe sched 50
           fire` and `strobe sched 10 fire`, with the Pi idle, then under CPU load;
         - **Pass:** +5 V stays ≥ ~4.75 V; `vcgencmd get_throttled` → `0x0`; a `pitrac-link
           ping` loop through the shots → no FAIL and 0 CRC errors; no board fault.
         - Below that is a +5 V budget problem (CR-25), not a firmware one. Board 1's dips
           without a Pi already reached ≈ 4.85 V.
      4. **Delivery:** ship with the interlock in place, and send the unlocked image after the
         test passes. The collaborator installs it from the Pi (`pitrac-link update`, with
         trial and rollback). If the test is done before shipping, ship it directly. Not
         recommended: shipping it untested with a "do not use yet" note.
    - **Separate and larger:** live current from a camera-timed shot (`cam shot` is dry-only,
      Pi or not). Lifting the Pi refusal allows CLI-timed live firings, and dry `cam shot`,
      with a Pi up.
  - **A 7b signal source:** a generator with an external trigger and a programmable delay, or a
    spare Pico. A Pico program can be written on request.
  - **The Mira220's trigger semantics:** whether exposure follows the trigger's width (then the
    hold, now the burst + 50 us, is the exposure) or a register. 7c, with CR-09 settled.
  Comparator timing is already on PIO2 (A2 done). The Pi UART link is written (2026-10-20) on
  UART1 with an RX DMA ring, without PIO.
- **Left open in the 2026-10-20 link and update (§6):**
  - **The framing, for the shot messages:** the owner's call (DEVELOPER_GUIDE §9.3).
  - **288 KB of RAM** is reserved for staging an update.
  - **An image that passes its trial but breaks the link** needs USB to replace (CR-26).
  - The trial's self-check is deliberately small: init completes, the config sectors are clear,
    the ADC ring is fresh. A Pi-confirmed buy would need the Pi powered during the trial, and
    so a flash write (the buy) with a Pi powered.
  - The link is answered between CLI commands only, so a long bench command delays it.
- 📋 **Code-audit backlog (2026-10-05)** — owner decision: listed, **not scheduled**. Findings
  from the 2026-10-05 audit (§6) that are refactors or hardening rather than bugs. IDs are
  the audit's.
  - ✅ **"Before 6c" — closed 2026-10-07 with the 6c firmware** (§6 2026-10-07). 6c arms the
    watchdog, and an un-fed watchdog drops the latch.
    - Blocking-command service contract: **checked** — `level`, `hpf test`, `cal demod` and
      `cal model` already yield (longest unserviced stretch: `cal.c`'s 10 ms chopped-average
      window); the `capture` / `capture trig` CSV dumps now call `pitrac_service()` every 256
      lines. Fault detection by **generation counter** (SVC-02) and an abort on the final yield
      slice (SVC-03) are done and host-tested (`tests/service_test.c`). ⚠ **Still open, now
      backlog:** `adc_capture()` waits on its DMA for n / rate seconds without servicing, so with
      `wdog on` a block capture slower than ~1 s resets the board. Live mode refuses `capture`.
    - Strobe: run token `s_run_gen` (STR-04/05) ✅; the snapshot reads the GPIO25 pad directly
      (STR-07) ✅; per-resource teardown flags (STR-02) — **checked, no change**: every claim in
      `strobe_init()` panics on failure, so a partial init cannot return, and `strobe_safe_off()`
      drives GPIO25 low before it looks at `s_inited`; `STROBE_TIMEOUT_MARGIN_US` in `board.h`
      (STR-09) ✅.
    - `shot.c` per-state deadlines and an explicit "not implemented" result (SHOT-01/02):
      **reclassified "before Phase 7"** — 6c does not touch the shot sequencer; Phase 7 wires it.
      ✅ **Done 2026-11-10, image 1.4** (§6, "Phase 7 firmware").
  - **Live strobe, open-load staircase (found 2026-10-07, §6):** a zero-current firing raises
    the proven level. Option: refuse to raise it above ~30 % without detected current, and end
    live mode ("no current — load open?"). (The overstated `board.h` comment above
    `STROBE_LIVE_GATE_STEP_MAX` was corrected 2026-10-12.) Covered procedurally by the 6c Step 3
    stop rule; the owner chose to rely on it (2026-10-12).
  - ✅ **Silent idle exit (found 2026-10-12, 6c Step 2) — fixed in `Oct 12 2026 10:34:54`, reworked after review in `11:09:36`:**
    live mode's 300 s idle timeout ended it from the service loop and printed nothing; the next
    `strobe pulse` then fired DRY with no warning. Now announced before the next command.
  - ✅ **Fixed in `Oct 12 2026 13:55:58` (round 6).** **Capture paths read bit 15 as "overrun" (found 2026-10-12):** `adc_capture()` and
    `adc_capture_triggered()` raise `FAULT_ADC_OVERRUN` when a FIFO word's bit 15 is set. Bit 15
    is the conversion-**error** flag; overflow is `FCS.OVER`, which they never read. So a real
    overrun in a bench capture (round-robin phase rotated) goes unflagged. Same fix as the
    freeze: clear `FCS.OVER` at the start, check it at the end. Not in the strobe path, and not
    changed in the 2026-10-12 image.
  - 🔴 **+5V_IN is not monitored while ARMED or during a bench capture (logged 2026-10-12,
    review round 5).** Those modes do not convert ch1. Since the round-5 fix the monitor
    neither judges nor refreshes a value it did not sample (NOT_SAMPLED). Before it, it
    silently judged a stale cached value.
    - Either way, a supply loss during a long `detect arm` with a Pi on J8 goes unseen until
      the mode returns to IDLE.
    - Options: add ch1 to ARMED, which needs a 4-channel round-robin or a power-of-two layout;
      bound how long ARMED may run with the latch closed; or treat a long NOT_SAMPLED as
      unreadable.
    - Must be decided before Phase 7c, when a Pi is seated.
  - **+5V_IN reads ~0.8 % lower at the corrected ADC rate (board 1, 2026-10-12).** `stat` went
    from 5.205 V (`13:55:58`, half rate) to 5.162 V (`14:43:50`) with the same PSU. The scale is
    unchanged (1.0630, config slot A seq 1).
    - Mechanism: Q9's incomplete S/H settling through the 50 kOhm R46/R47 source. Back-to-back
      conversions leave ch1 less settling time than the half-rate idle gaps did.
    - It errs safe: the latch guard has 112 mV of margin, and the monitor trips early, not late.
    - Re-derive `V5IN_SCALE_DEFAULT` (1.063, measured under the old timing) at the corrected rate.
      Saved per-board scales (board 3: 1.062) read low the same way, so re-trim with `adc5vcal`
      when convenient. The real fix is CR-03 (10k/10k).
  - **`adc5vcal` calibrates a different reading from the one decisions use (found 2026-10-12).**
    - It takes its raw value from a blocking single-channel read (`adc_read_volts(ch1)`: the
      S/H stays on ch1 and settles fully). The monitor, latch guard and `stat` average ch1 from
      the IDLE round-robin, where ch1 follows ch7 (~1.65 V) and, at the corrected rate, settles
      less through 50 kOhm.
    - Board 1: `adc5vcal 5.195` gave `raw 4.885 V`, scale 1.0634, saved. Yet `stat` then read
      5.162 V, a ring raw of ~4.856 V (-0.6 %, ~20 codes; not discretisation).
    - Again on 2026-10-13: `adc5vcal 5.185` gave `raw 4.877 V`, scale 1.0630 -> 1.0631
      (unchanged in effect). `adc5v` then read 5.151 V, a ring raw of ~4.845 V (-0.65 %). The
      owner reasonably read this as "the cal does nothing". Do not work around it by typing an
      inflated voltage: that would make the blocking read (mode OFF, e.g. at boot) read high,
      the unsafe direction for the latch guard.
    - Fix: derive the raw value from a FRESH `adc_5vin_read()` divided by the current scale. Not
      urgent: errs safe, with 112 mV of latch-guard margin. Batch it with the next firmware change.
    - **Fixed in the 2026-10-15 build** (under review), as above, and refused unless the ADC is
      in IDLE.
    - **Correction:** "mode OFF, e.g. at boot" above is wrong about boot. `main.c` sets IDLE
      before `power_fsm_init()`, so the FSM always reads the ring. After a trim, the blocking
      read (used only after `adcmode off`) reads ~0.6 % high.
    - **Open (independent review 2026-10-15):** that mismatch is real if someone selects
      `adcmode off` and then requests power, because the 5.05 V guard boundary moves ~30 mV.
      Before Pi integration, do one of these:
      - refuse `adcmode off` where the latch can be decided;
      - calibrate the two paths separately.
  - ✅ **(Resolved 2026-10-16: fixed, reviewed in three rounds, bench-verified.)** **Half-peak
    threshold hides current that follows a larger spike (found 2026-10-13, 6c
    first current at gate 21 %).**
    - `strobe_live_measure()` sets the run threshold at baseline + max(12 codes, half the
      window's peak excursion), and a run needs >= 2 samples.
    - On board 1 a ~0.3 A turn-on peak hid a ~0.07-0.1 A, 20 us pulse. The first firing printed
      `no current above the ~72 mA detection floor` (cli.c prints that whenever no run is
      found, even with a 50-code peak). The next two reported a 4 us `short` pulse.
    - **Safety relevance:** `on_at_end` and `max_width_us` (STUCK_ON) use the same runs. A
      current that persists below half of a larger spike would not trip them. The plateau
      (I_STOP) and the `strobe cal` solve use those runs too.
    - Fixes to weigh, with independent review:
      - an absolute floor for the stuck-on and width checks;
      - a threshold relative to the settled level, not the peak;
      - accurate text when the excursion exceeds the floor but no run is found.
    - Must be settled before `strobe cal` relies on near-threshold readings, and before
      Phase 7.
    - 🔴 **Escalated 2026-10-13 (gate 24 %):**
      - A 0.72 A, 20 us pulse with a 3.3 A turn-on peak was printed as "no current"
        (threshold 266 codes).
      - The staircase counted it as a pass.
      - **Fix before any further live firing**, with independent review. Detect runs against
        an absolute floor, and stop on a turn-on peak far above the settled level.
      - **Narrowed 2026-10-15, board 1 only.** After the CR-23 rework there is no turn-on
        spike, so the threshold sits at half the plateau and the runs are right: 20, 50 and
        134 µs pulses were measured within one sample. Live firing at <= 24 % resumed for
        the rework check and 6d (§6). **Nothing above 24 %, and no `strobe cal`, until the
        fix.**
      - **Fixed in code 2026-10-15; under review, not flashed** (§6, "Live readback fix
        written"). The fix has three parts:
        - an absolute floor for every check;
        - a settled-level shape check (SHAPE ends live mode);
        - a two-sample overcurrent check.
        Close this item after the review passes and the bench re-check on the new image.
      - **Review round 1 (Sol, 2026-10-15):** four findings, all fixed in r2. Round 2: PASS.
        Two-of-three was added in r3 at the owner's choice; round 3 PASS (2026-10-16).
      - **CLOSED 2026-10-16:** bench-verified on board 1 (§6, Session B step 9). 21 % read
        0.107 A and 24 % 0.702 A, against the Saleae's 0.114 and 0.704 A. Both pulses were
        flat, with no SHAPE or OVERCURRENT.
  - **A steady current through the whole live window is invisible below `BASELINE_MAX`**
    (independent review 2026-10-15; open).
    - It lifts the baseline with it. One record cannot tell DC current from ADC offset.
    - On board 1's ~10.7-code offset, a steady current under ~0.17 A goes unseen.
    - A better check needs a separately established zero reference, ideally ADC0 read with
      the rail physically off, not another guessed threshold.
  - **Finding for the owner: `BURST_CHARGE_MAX_MC` (6 mC) against board 1's bank**
    (2026-10-16, §6 Step 5).
    - Board 1: C ≈ 600 µF; ~12.4 V across Q9 at the start of a burst; Q9 needs ~1–1.5 V.
    - A 6 mC burst (e.g. 10 × 67 µs at 15 m/s) would end with ~2.5 V across Q9. It fits, with
      ~1 V to spare.
    - A bank at −20 % (~540 µF), or LED strings with a higher Vf, would use most of that
      margin. The readback's shape check would flag a sagging plateau (a DIP), but only after
      the burst has fired.
    - Options: keep 6 mC with the readback as the guard; derate to ~5.4 mC (the slow-ball
      burst); or add bulk on the next board.
    - ✅ **Decided 2026-10-16 (owner): keep 6 mC** (§8). Nothing changed.
    - `board.h`'s comment on it says the ~670 µF "has never been measured". Correct it with
      the next firmware change: ~600 µF on board 1. ✅ **Corrected 2026-10-19** (§6).
  - **Finding for the owner: the 70 % live gate ceiling no longer bounds the current near 9 A
    on board 1** (2026-10-16).
    - `STROBE_LIVE_GATE_CEILING` (717, TP3 ≈ 6.9 V) was sized "above the 4.5–6 V the design
      expects at 9 A". Board 1 reaches 9 A at 4.23 V (42.9 %). At ~5 A/V, TP3 6.9 V would ask
      for far more than 10.8 A.
    - What bounds the current is the per-firing OVERCURRENT checks (10.8 A on the plateau or
      on two of three samples; 13 A on one sample), plus the ≤ 3 % staircase (~1.6 A per step
      here) and `strobe cal`'s 10.8 A abort.
    - A lower compile-time ceiling would tighten board 1, but could block a board whose Q9
      needs 6 V. A per-board ceiling would need a config field.
    - Nothing changed; no limit raised. Decide before production use, not for the bench.
    - ✅ **Decided 2026-10-16 (owner): option c, a session ceiling from the calibration** (§8).
      Queued for the next firmware change, with the usual review, host tests and mutation. Spec:
      - After `strobe cal` SOLVES, cap live gate levels at the level where the cal's own table
        reaches the 10.8 A stop. Use the slope of its last points (~+35 levels on board 1,
        ~46 %). Never above the 70 % ceiling.
      - `strobe gate` above the cap is refused, with the reason. The cap clears on re-arm and on
        leaving live mode. RAM only, like the cal table; persisting it belongs with Phase 7's
        stored calibration.
      - Before any cal, the 70 % ceiling and the staircase apply as now. The open-load gap
        (a zero-current firing counts as within limits) stays covered by the Step 3 procedure
        until then; option d was not taken up.
    - 🟡 **Written 2026-10-19** (§6), under review, not flashed. **One change from the spec
      above:** the cap uses a physical bound on the slope, 0.075 A per level, not the slope of
      the cal's last points, which put the cap at ~12 A after a 2 A cal. Board 1: level 463
      (45.2 %), not ~475.
  - **Comment to correct with the next firmware change (2026-10-16):** `strobe_plan.h`,
    `strobe_gate_settling()`, says the 20 ms leaves "9.9 V decays to ~5 mV". True for TP3, not
    for Q9's gate on a board with the 1 µF hold capacitor (board 1's rework; the next-board
    C_hold).
    - That gate is 0.35 V at 20 ms (measured from 30 %), ~0.4 V from 100 %, with τ ≈ 38 ms.
    - The interlock still holds: board 1 conducts nothing measurable with its gate up to 1.78 V
      (§6, gate settling). Only the comment's number is wrong.
    - Not edited now: any source change would move the reviewed, flashed image.
      ✅ **Corrected 2026-10-19** (§6), with the next firmware change.
  - **Comment to correct with the next firmware change (2026-11-11, confirm review of 1.5):**
    `camera.h`, the comment above `camera_manual_trigger()`, says a manual high lasts "until a
    low, a handshake refusal, or the next rail-down". A refusal does not end it: `camera_start()`
    refuses while the trigger is already high and leaves it high. It should say "until a low
    (`gpio 10 0`, `camera_safe_off()`) or the next rail-down".
    - The code and the bench procedure are right: 7a Step 4 follows the refusal with `gpio 10 0`.
    - ✅ **Corrected in image 1.6, carried into 1.7** (§6, "Image 1.7"; review PASS 2026-11-12). 1.6 itself failed review (V16-F1).
  - **V17-N1: qualify `level` wording with the next image** (independent review, 2026-11-12).
    The current-phase estimate is not the sweep's verdict, even after a commit. The model's
    clean 30.21–33.73 % sweep reads 27.38 % in `level`, which incorrectly says `cal demod
    refuses`. Describe it as an approximate relative aid and remove that categorical
    prediction from `cmd_level`; qualify the true-peak claims in help, the banner and comments.
    This is nonblocking for 1.7; BRINGUP §9.2 and BENCH_P3_DETECT §3.4 already make the final
    sweep authoritative. Preserve reviewed 1.7; record any future text change in a new image.
  - **V111-N1/N2 wording for the next image (2026-11-12):** `cli.c` and `service.c`
    should bound only the bare sleep, not USB output, by the watchdog timing statement.
    The late-refusal text should say presence changed after the initial check rather
    than attributing every rise to the 50 ms sleep. Nonblocking for reviewed 1.11;
    preserve its bytes and identify any future image separately.
  - **Probe-point notes still to add (2026-11-11, `HANDOFF.md` §5 rule 8):** these docs name
    J8.2, TP6, TP9 or TP10 as DMM points on a powered board without saying what each sits next
    to. Distances are in §6, "Board 4".
    - `BENCH.md` Phase 0.5, rows b and c, and the note under them;
    - `BENCH.md` Phase 1, Tests 1 and 5, and the note at "did the button work?";
    - `BENCH_P3_DETECT.md`, the idle-level table (TP6, TP9, TP10).
  - 🟡 **The saved detection threshold is never restored at boot (found 2026-11-11, Board 4).**
    - `cfg save` writes `threshold_level` (`cli.c` `cmd_cfg`), and `cfg` prints it. But nothing
      reads it back:
      - `cfg_init()` restores adc5v scale, coalesce and path;
      - `cfg_apply_beam()` restores carrier and phase;
      - `restore_phase_model()` restores the model;
      - `detect.c` starts the DAC at level 0.
    - So after any reset the comparator threshold is 0 V while `cfg` shows the saved level.
      This is the same defect class as 2026-08-14 (carrier/phase) and 2026-08-25 (phase
      model), both noted in `cli.c` above `restore_phase_model()`.
    - **It matters once anyone tunes the threshold** (`BENCH_P3_DETECT.md` §3.7, likely the
      collaborator with Board 4): their `cfg save` will not survive a power cycle.
    - **Fix:** in `cfg_init()`, apply a saved level ≤ `DAC_TOP` with a host regression and a
      deliberate mutation. Or, if boot-at-0 is intended, make `cfg` say so.
    - Board 4 currently has level 51 saved, left over from §10.1's `threshold duty 5`. Once the
      fix lands, that would become live at boot (164 mV at TP8), so set it deliberately first.
    - Goes in the same firmware batch as the items below.
    - ✅ **Fixed in image 1.6, carried into 1.7** (§6, "Image 1.7"; review PASS 2026-11-12): `cfg_apply_threshold()`
      after `detect_init()`, not in `cfg_init()`, which runs before `detect_init()` zeroes the
      DAC. Review PASS 2026-11-12; close after Board 4's §13.1 power-cycle check.
  - **Making `cal demod`'s level-setting foolproof (owner, 2026-11-11, Board 4).** The owner
    wants these built, after Board 4's bench work is done. Each is a firmware change and goes
    through the usual review.
    1. **A live level readout** (e.g. `cal level`).
       - The beam stays at its duty; demod at two phases 90° apart; print √(I² + Q²) as a
         percentage of full scale every ~0.5 s, with "move closer / OK / move away", until a
         key is pressed.
       - The I/Q magnitude needs no phase calibration, so it works before the first
         `cal demod`.
       - The user places the reflector in seconds, then runs one 26 s sweep. Board 4 took
         four.
    2. **`cal demod` refuses to commit under ~30 % of full scale.** Today it committed at 9 %
       (Board 4, run 2), with a "TOO SMALL: add light" hint to match the existing "TOO BIG"
       one.
    3. **`cfg save` with the beam on names the command:** "send `beam off`, then `cfg save`".
    4. Possibly widen Step 9.2's 50–70 % window. The firmware's own gates (saturated 0 %,
       h3/h1 under the bar) are what detect clipping, and the phase was within 1 tick at
       69 % and 78 %. Owner's call.
    - **Items 2 and 3 are in image 1.7** (§6, "Image 1.7"; review PASS 2026-11-12). **Item 1 is not:** 1.6's version of it failed review (V16-F1) and 1.7 dropped it. `level` reads the current phase again.
      - **Original claim, superseded by V16-F1:** Item 1 became the existing `level` made
        true at any phase. It uses 8 phases, not 2:
        two quadratures would be off by up to the 3rd harmonic, 11 % at 25 % duty.
      - Item 4 is still the owner's call.
  - **The guided calibration script (planned 2026-11-12) is deferred to the next board
    revision** (owner; §8). Its plan and the hardware it needs are `NEXT_BOARD_REV.md`
    CR-29. It would also take over the card placement above: it reads `cal demod`'s peak
    and says which way to move the card.
  - **The beam's base-to-heatsink gradient on board 1 has two values (2026-11-11).** They need
    reconciling, because Step 6.4 compares boards on it.
    - `BRINGUP_NEW_BOARD.md`'s sign-off note gives 19.4 K (87.5 − 68.1 °C).
    - `NEXT_BOARD_REV.md` CR-12 gives 8.7 K/W, measured from the side: ~24 K at 25 % (2.74 W),
      28 K at 30 %.
    - Board 4 read 24.8 K at its plateau.
    - Record where the FLIR reads the base and the heatsink, and use one method for every
      board.
    - Each needs the neighbour named, and either a lead fitted unpowered or a substitute point:
      VIR at the J3 screw for +5 V; TP7 for the mid-rail.
    - Not edited now: `cli.c` (the build stamp) includes `camera.h`, so any rebuild would move
      the reviewed image 1.5 (`INDEPENDENT_REVIEW_r2.md`, "Nonblocking documentation note").
  - **Cosmetic, from round 5:** `stat`'s `adc` line labels the age after a fresh read as `HELD`.
    The read itself takes ~1 ms, so the age is 1 ms, not 0. Label it by status, not by
    age == 0, with the next firmware change. **Fixed in the 2026-10-15 build** (under
    review).
  - 🔴 **Ring-derived timing taken before `Oct 12 2026 14:43:50` is at HALF the stated rate (2026-10-12).**
    Every ring mode ran at half rate (DIV 95, section 6). Detect pass logs and retained
    waveforms stamped `adc_ring_channel_rate_hz()` = 2x the real rate, so their time axes are
    compressed 2x. Re-derive any ring-based timing (transit times, FWHM from pass logs) before
    using it. Block captures at 100/250 ksps, the section 3.7 pilot included, are unaffected.
  - **`adc_ring_view()` / `adc_ring_history()` / `adc_ring_avg()` ignore `FCS.OVER` (2026-10-12,
    review round 6).** An overflow loses conversions and rotates the round-robin, so the channel
    labels they rely on can be wrong, and nothing tells them. The live readback and the 5V_IN
    reader check it; the detect path (ARMED pass analysis) does not. Fix before Phase 4/7 use
    the pass-log ADC column for decisions.
  - **`adc_ring_view()` / `adc_ring_history()` / `adc_ring_avg()` do not bound by the mode start
    (2026-10-12):** straight after a mode switch they return the previous mode's samples as this
    channel's. *(The 5V_IN path no longer uses `adc_ring_avg()`: since round 5 it takes only
    samples issued during the call.)*
    - `adc_ring_avg()` is the 5V_IN monitor's source. For ~2 ms after every switch back to IDLE
      (after each live firing, `adc <ch>`, `capture`, `adc ringtest`), its 256-sample mean
      includes BURST or blank slots.
    - Today the 500 ms `V5_MIN_SUSTAINED` debounce absorbs it. But a latch decision taken in that
      window would read a wrong 5V_IN. Check `on`'s latch-guard path.
    - Harmless for detect (ARMED runs for seconds before a pass).
    - Phase 7's `SHOT_FIRING`, and any reader right after a switch, must count what the DMA wrote
      (`adc_ring_written()`).
  - **Why the BURST ring started ~200 us late on board 1 (2026-10-12): unknown.** Measure with
    `adc ringtest` after reflashing.
  - ✅ **Stale live-mode checklist (found 2026-10-12, 6c Step 1) — fixed in `Oct 12 2026
    10:34:54` (kept in `11:09:36`),** with the `board.h` staircase comment: the checklist said "ground on TP1", which
    was disproven on 2026-10-07. It now says the R66 wires, not TP1/TP5.
  - **Structure:** split `cli.c` (2,275 lines) into cli_core / power / adc / panel / beam /
    detect / cal / strobe around one dispatcher and the shared parsers (F-23); a shared
    `require_rails()` guard with one refusal wording (F-25); table-driven subcommand help so
    syntax is machine-checked, not just top-level names (F-17/18); help defaults generated from
    the constants (F-19/20/21); split `board.h` into pins / adc / beam / strobe / power behind
    an umbrella header (BOARD-3); panel PWM constants into `board.h` (PNL-01); remove the dead
    `CARRIER_LEVEL_30PCT` / `DEMOD_LEVEL_50PCT` (BOARD-1).
  - **Testability, ranked:** (1) config record validation + slot selection incl. sequence wrap
    (CFG-1/3) — **also add a migration path before the first `CFG_VERSION` bump**, because the
    version check now makes an old record fall back to defaults (board calibration lost);
    (2) beam planner + duty ceiling; (3) detect waveform refinement; (4) cal phase/form-factor
    math; (5) +5 V ADC math; (6) detect coalescing. Before host-testing `strobe.c`,
    `safe_state.c` or `panel.c`, restructure `tests/mocks` into a small HAL state model (pin
    function/direction/level, PWM CC, µs clock) (TST-01..04). Add 32-bit ms wrap tests for
    `power_fsm`.
  - **Concurrency (latent — every caller today is the core-0 superloop):** write down the
    core-0-only ownership contract; ring-snapshot consistency and `adc_read_avg()` while the
    ring runs (ADC-1/2/3/5); beam multi-register updates (BEAM-2); detect FIFO latency budget,
    64-bit timestamps and returned-pointer lifetimes (DET-2/3/4).
  - **Config robustness:** reload both slots after a failed save (CFG-2); an explicit sequence-wrap
    policy (CFG-3); range-check every restored field (CFG-5).
  - **Cal:** report the effective chop frequency and point count instead of clamping silently;
    restore the beam exactly after `cal model` (CAL-1/2/3).
  - **Printed output:** `beam freq` below ~2289 Hz prints a wrong "frequency step" (ignores
    the clock divider; shows ~0 Hz at 8 Hz) — found by the 2026-10-05 smoke check. Cosmetic.
  - **Tools:** `tools/requirements.txt` (pyserial and matplotlib are not in the selected
    interpreter); unused locals in `scope.py` and `netlist_report.py`. *(Done 2026-10-09: the
    pilot-capture analysis script is now `tools/pilot_analysis.py`, for the §3.7 20-pass set.)*
  - **Checked, no action:** CFG-4 (a flash-resident callback under `flash_safe_execute()` is the
    SDK pattern); STR-08 (the PIO overheads are what 6a.2 measures); PNL-02 (the CLI never
    passes NULL).
- 📋 **Halt telemetry — record repeated or unrequested Pi shutdowns.** Requested 2026-07-31.
  If the Pi 5 halts repeatedly, and especially if it halts when nobody asked, that must leave
  a durable trace: it is the exact symptom of a Q10-class fault and the kind of intermittent
  problem that is invisible without a counter. **Full design note in `BENCH_P8_PI.md` §8.6.**
  The short version:
  - **The RP2354 is the authoritative witness** — it is the only party that knows *intent*
    (did we assert RPI5_SHUTDOWN?), and it survives the failure. If the SD card is the
    casualty, the Pi's own log is the least trustworthy record of what happened to it.
    Pi-side journald is corroborating detail, not the primary record.
  - **Ride it on the flash config block** (`config_store.c`, built in Phase 3) — the counters
    are a handful of words. Two constraints: `cfg_save()` refuses unless the machine is quiet,
    and adding fields means a `CFG_VERSION` bump, which needs the migration path in the
    code-audit backlog below.
  - Counters: `boot_count`, `pi_shutdown_requested`, `pi_shutdown_clean`,
    `pi_shutdown_timeout`, and the important one, **`pi_down_unrequested`**.
  - ⚠ **Prerequisite that does not exist yet:** `PS_RUNNING` never watches for the Pi going
    down — it only looks for a button press or a shutdown request. If the Pi halts on its
    own the FSM sits in `RUNNING` forever with the rail up and never notices. **That
    detection has to be added before `pi_down_unrequested` can mean anything**, and what the
    board should *do* about it interacts with Q4 (if the header 3V3 does not drop at halt,
    detection rests entirely on `RPI5_ON`).
- **`capture` is blocking and prints over CDC**, so a 16 k dump takes a few seconds. Fine for
  bench use; it is not a streaming telemetry path. Same for `beam ramp`, `beam sweep`,
  `panel demo` — all correctly blocking, because they are interactive bench tools. The rule
  that matters: **nothing in the armed or firing path may block.**

---

## 10. Next session — start here

> ### 2026-11-24: 7c.2 Steps 1–5 PASS on the firmware's side; no LED current; a focus tool
>
> - **Done** (§6, 2026-11-24; the owner's settings in §8):
>   - **7c.2 Steps 3–5 PASS on the firmware and Pi side,** both modules wired. Twelve
>     handshakes with `t_cam` 0.207–0.240 µs; 3 + 9 pairs, none unpaired; no untriggered
>     frames.
>   - **Steps 1–2's strobe mapping:** not crossed.
>     - **Camera 0 (J4.7) is on bus 10 = CAM/DISP 0, camera 1 (J4.8) on bus 11 = CAM/DISP 1.**
>       That is the reverse of 7c.2's table; the configs follow it.
>     - On the Pi, `~/claude-setup/7c2-configs.env` has B0=10, B1=11.
>   - **No LED current at first:** `strobe cal 9` reached the 70 % ceiling with nothing above
>     ~72 mA. **The owner then fixed J3.** The script's fixed path found first current at 21 %
>     and solved 9 A at level 437 (42.68 %), 9.015 A. Flashes of 9.05 A × 100 µs light both
>     cameras' frames. Camera 0 is far out of focus; camera 1 is nearly in focus.
>   - **For focusing:** `tools/strobe_focus.ps1` (live, paced flashes) and `focus-preview.sh
>     --hold-flash`. See `Software/camera-comparison/FOCUS.md`, "Focusing by the board's
>     strobe".
>   - **A soak of 50 dry `cam shot 200`:** 50 of 50 complete, all paired. That makes 62
>     two-camera handshakes on Board 5.
>   - **I was wrong, twice.**
>     - The first version of the script skipped 12.3's 30 % open-load stop. Fixed; the fixed
>       path has not yet run.
>     - A soak command that didn't wait for `READY` raised GPIO10 for 5 ms with both cameras
>       off: `CAM_TIMEOUT`, cleared with `fault clear`. Fixed. The risk was low, because the
>       modules isolate with VA at 0 V.
>   - **On the PC, use local port 8766 for the tunnel:** 8765 is the design workbench's.
> - **Left:** Board 5 `RUNNING`, the Pi up, live OFF, gate 0, beam off, `fault : none`.
> - **Later the same day (§6):** J3 fixed (13:00); both cameras focused with the A4 lenses; the
>   flash width measured, at 8- and 12-bit, and with the ball 7 cm closer. The Mira220 has no
>   analogue gain.
> - **Next, in order:**
>   1. **7c.2 with the analyser** (fitted unpowered): Step 1's isolation, Step 2's levels, and Steps
>      4–5's delays, skew and `t_cam` against the analyser.
>      - Then 7c.2 passes. The buses go into `WIRING_DIAGRAM.md` §6.2, and 7c.2's table and the
>        guide's figure labels change to this bench's camera 0 on CAM/DISP 0.
>      - The owner's open call (repeat 7c.0/7c.1's module checks on Board 5) can fold into the
>        same session.
>   2. **The independent 7c.2 review** (`captures/review_2026-11-20/REVIEW_BRIEF_7c2.md`; refresh
>      its pinned hashes first). Add `tools/strobe_focus.ps1` to a review: it commands live
>      current.
>   3. **The shot path** (`SHOT_PATH_PLAN.md`): WP0 (measure the superloop latency and the
>      comparator chatter), then WP1 (host-only strobe profiles, validator, LUT); WP2–WP6 after.
>   4. **Light:** the unexplained ~4 % flash-to-flash term (a flat white card at 20 µs would
>      show whether it is the LED pattern); the pulse width and bit depth; the bank at its
>      design spot (~0.42 m from the ball, −16°; 10 of 20 positions fitted).
>   5. **Mechanics:** lock the lenses; the cameras on the stand; then
>      `Hardware/Calibration`'s procedure at 850 nm.
>   6. **Housekeeping:** Board 5's asks (TP4 at rest, R65/R66, the 6.4 FLIR check); board 1's
>      1.12 install. Everything through 2026-11-24 is committed (the owner asked; `CHANGELOG.md`'s
>      2026-11-24 section lists it).

> ### 2026-11-23: Board 5 bring-up (owner), then board 1's Pi on it; 7c.2 Step 0 PASS
>
> - **Board 5 is the COM10 board** (UID `6093584a7790419d`; §6, 2026-11-03 and 2026-11-23).
>   Silicon A4. USB and PSU only. No soldered leads (owner).
> - **Done:**
>   - `BRINGUP_NEW_BOARD.md` Steps 1.3–1.4: the partition table, and r5 in B;
>   - **1.12 in A, bought**, its first board;
>   - §2's readings at the first `on` (owner skipped J2): VIR 35.77 V, TP2 12.31 V, TP7 2.59 V;
>   - §3 PASS;
>   - §4 PASS: scale 1.0687, slot A, seq 15;
>   - §5 PASS: 5.1 and 5.2 in the log, 5.3–5.5 reported by the owner;
>   - the unpowered rework check PASS: **R80 120 kΩ**.
> - **§6–§12 before the Pi (owner, 2026-11-23, §8),** with no soldered leads and no analyser.
>   Test points are read with a DMM from the back, which the PCB shows is safe.
>   - **Done:** §7.1, §7.2, §6.1 and §6.5.
>   - **§8–§11 PASS** (§6, 2026-11-23):
>     - the baffle holds to 25 %;
>     - phase 1320 ticks (400 ns), `cal model` PURE DELAY, residual 0.26°;
>     - vref 3.270 V, Vos +6.2 mV, crosstalk 2.4 mV;
>     - saved in slot A, seq 19.
>   - **Open:**
>     - Step 6.4's FLIR hold: assumed OK by the owner, with no FLIR on hand (§8). Measure it
>       when the FLIR is back.
>     - Step 6.3, the DMM U9 check, optional.
>   - **§12:** TP3 is read from the back, and part 6 is skipped.
>     - **Step 12.2 parts 2–5 PASS** (§6): TP3 4.94 / 8.91 / 0 V.
>     - **Step 12.1 PASS:** the capacitor is fitted, 1 µF, and every DMM check passes (owner).
>     - **J8 and J4 stay empty through 12.4** (§6): the Pi and cameras go on after §12.
>     - **12.3–12.4 PASS** (§6): U5's clamp 134 µs; 9 A at level 436 (42.58 %, TP3 4.22 V),
>       confirmed at 8.975 A; session cap 461.
>     - **Asked:** TP4 at rest (under 2 mV) the next time J3 is on with the rail up; R65/R66's
>       markings.
> - **2026-11-23, later: board 1's Pi on Board 5, with both Mira220s on the Pi** (§6, "Board 5
>   powers board 1's Pi").
>   - **"First seating" PASS as far as it runs from this PC:**
>     - `pi : present 0  3v3 0` before `on`, the guard left after the R39 check was skipped
>       (§8);
>     - one clean `on`/`off`;
>     - 1000 pings, 0 failed.
>     - SW2 taped and the Pi's USB-C unplugged: confirmed by the owner. The supply is the
>       design's Mean Well LRS-75-5, rated 14 A, so "limit ≥ 5 A" is met (§6, 2026-11-24).
>     - A Pi reboot keeps the rail up (8.6): down unrequested 1, came back 1, power cuts 0.
>   - **7c.2 Step 0 PASS** (§6, "7c.2 Step 0 PASS on Board 5"):
>     - the tests pass on the Pi;
>     - `dtoverlay=mira220-nir,cam0` is added, after a backup;
>     - `mira220 10-0054` and `mira220 11-0054` are listed, and both are configured at
>       1000 µs. B0 = 11, B1 = 10;
>     - each camera streams free-running.
>   - **Camera 1 sees no light** (a cap, no lens, or facing something dark). Camera 0 sees the
>     room, dark in NIR.
>   - **Left:** the board `RUNNING` with the Pi up (the owner's `on`), the beam off at 25 %, J3
>     and J4 empty.
> - **Next:**
>   - **7c.2 Steps 1–5:** module #1 wired and J4 connected, all with the power off, plus the
>     analyser.
>   - **7c.2 now runs on Board 5, not board 1** (the owner moved the Pi). Board 5's J4 and J5
>     have never carried the handshake, and 7c.2 takes module #0 as proven by 7c.0 and 7c.1 on
>     board 1. **For the owner to decide:** repeat 7c.0 Steps 1–3 and 7c.1 Step 0 on Board 5,
>     with module #0 alone, before 7c.2 Step 1.
>   - **NIR light for the lenses,** and camera 1's lens or cap checked.
> - §6 and §12 have passed, so the earlier beam and J3 guards are lifted. The owner accepts 6.4
>   untested (§8).
> - **The beam's duty boots at 2 %** (`beam.c`), so `cfg` warns after each power-up. Send
>   `beam duty 25` (owner: any time) before beam work. It sets the level only; the beam stays
>   off.

> ### 2026-11-20: 7c.3 PASS (desk); D1–D8 decided; 7c.2 written; next, 7c.2 on board 1
>
> - **7c.3 PASS** (§6, 2026-11-20). The first-flash latency now decides how many freezes fit:
>   four at 100 m/s within 289 µs of the trailing edge (47.33 mm spacing).
> - **Correction:** the live readback's baseline is ready 198–200 µs after the switch to
>   BURST, not ~0.4 ms (§6).
> - **Next:**
>   - **the owner decided D1–D8 the same day** (§8; `SHOT_PATH_PLAN.md` §3a). Next in the plan:
>     WP0.1 (a loop-latency counter, no behaviour change), WP0.3 from the logs, and WP1
>     (host-only). The workbench side regenerates its table (D5, D8);
>   - **7c.2, written 2026-11-20** (§6), is next on board 1, with no reflash.
>     - A TXS0108E per camera: the owner's choice (§8).
>     - **An independent review of its wiring and procedure is briefed** (the owner's request,
>       2026-11-20): `captures/review_2026-11-20/REVIEW_BRIEF_7c2.md`. Not required by
>       `HANDOFF.md` §5 rule 8, since no firmware changed. Recommended before module #1 is wired.
>     - Copy `Software/camera-comparison` to the Pi first: the two-camera tools, with the fixed
>       `trigger-capture.sh`.
>     - No firmware change: the handshake already waits for both strobes;
>   - at the next power-up, as before: the fixed `trigger-capture.sh` onto the Pi, its frames
>     pulled, and 1.12 installed (`BENCH_P8_PI.md` §8.9c).
> - Not committed.

> ### 2026-11-16: a route to 7c.1 — switch the Mira220 to slave mode after each stream start
>
> - **Upstream PiTrac does it** (`feature/pico`, commit `afe689e`; §6, 2026-11-16).
>   - It makes a few I²C register writes at 0x54 after the stream starts.
>   - Our driver puts the sensor back in master mode on every start, so the switch follows each
>     start.
> - **Correction:** the driver writes `0x1001 = 0x41`, not 0 (§6, 2026-11-16).
> - **Next, unchanged:** `BENCH_P5_P7_MIC_CAMERA.md` 7c.0 on board 1, image 1.11, no reflash.
>   - **The Pi is set up** (2026-11-18, §6): the driver, the overlay (CAM/DISP 1) and the camera
>     kit. The Mira220 probes on I²C bus 11 and streams at 88.96 fps.
>   - **7c.0 Step 1 PASS** (2026-11-18, §6). Step 3's strobe checks pass from the same
>     capture. The A1/A2 probe labels look swapped: ch2 carries the trigger at the camera.
>   - **7c.0 PASS** (2026-11-18, §6). Step 2 held 1.277 V, only 17 mV above VIH, and dipped to
>     1.04 V at strobe falls.
>   - **7c.1 Step 0 PASS** (§6): 1.490 V with channel 5 bridged, and no dips at strobe falls.
>   - **7c.1 PASS** (§6):
>     - one frame per trigger, and none without one;
>     - **t_cam 0.21–0.24 µs** (0.17–0.18 µs on the analyser), not the assumed 100–300 µs;
>     - the strobe follows REQ EXP on both edges.
>   - **Next:**
>     - at the next power-up: copy the fixed `trigger-capture.sh` to the Pi, and pull the
>       frames to compare brightness;
>     - 7c.2 when there is a second Mira220;
>     - **Image 1.12 passed independent software review** (§6, V112-N1 through N3).
>       Board 1 still runs 1.11 in A with 1.7 in B.
>     - **Its install is `BENCH_P8_PI.md` §8.9c** (written 2026-11-18). V112-N1–N3 wait for the
>       next image.
>     - **The stand's geometry** is in `Hardware/Stand/README.md` (2026-11-19, §6). It is the
>       input for 7c.3, and its two timing models disagree (§4.1 there).
>     - **The detected-shot path is planned** in `SHOT_PATH_PLAN.md` (2026-11-20, §6).
>       Decisions D1–D8 wait for the owner. WP0 (measurements only) can start without them.
> - **Then 7c.1, now written** (owner's go-ahead; §6, 2026-11-16, "7c.1 written"):
>   - the Pi tools `trigger-capture.sh` and `mira220-trigger.sh` (`Software/camera-comparison`,
>     `TRIGGER.md`), used on board 1's Pi on 2026-11-18;
>   - its first measurement: whether ILLUM TRIG pulses in slave mode (Step 2).
>   - On the Pi first: `i2c-tools`, the camera's `/dev/i2c-<bus>`, and membership of the
>     `i2c` group, not `sudo` (7c.1, "The Pi tools").
> - Not committed. `e23aa65` holds everything to 2026-11-16 before this block.

> ### 2026-11-13: 7c allowed (owner) — 7c.0 next on board 1; 7c.1 needs the Mira220's trigger mode
>
> - **The owner lifts both rules for 7c** (§8): a Pi on J8 and a camera on J4, the camera only
>   through a TXS0108E module.
> - **Next: `BENCH_P5_P7_MIC_CAMERA.md` 7c.0 on board 1,** image 1.11, no reflash.
>   - The setup: one Mira220, with the module wired as camera 0's rows of
>     `WIRING_DIAGRAM.md` §7.2, and J4.8 tied to J4.7.
>   - It checks the module's 1.8 V side, the trigger's high at the camera (≥ 1.26 V; ~1.35 V
>     predicted) and the camera's strobe reaching GPIO8, with the camera free-running.
>   - The Pi on J8 needs the Mira220 driver (`install-mira220.sh --check`).
> - **7c.1 on needs the Mira220 in external-trigger mode.** The installed driver cannot do it
>   (§6). Not started.
>   - Either a driver change or the owner's Mira register utility.
>   - Both need DS000642's register descriptions: master/slave control, `EXT_EVENT_SEL`,
>     `EXT_EXP_PW_SEL`, and the illumination trigger's polarity, delay and width.
> - **The stray J4.3 edge at power-up is explained** (§6). The Pico 2's GP15 (erratum E9) held
>   the undriven trigger near 2.1 V until the board's firmware drove it low, 140.6 ms after
>   power-on. The board never drove it. Nothing to do on board 1.
> - **Still open:**
>   - from 7a: Step 3's repeat at the 5000 µs default timeout;
>   - for the next image: the 1.11 review's V111-N1/N2 notes, and the firmware's
>     `J4: NO CAMERA` lines.
> - **Committed 2026-11-16** at the owner's request: everything since `b467166` (2026-10-15).
>   `CHANGELOG.md` at the repo root lists it.

> ### 2026-11-13: Phase 7 on board 1 (owner) — 7a and 7b PASS; a stray trigger at power-up to diagnose; 7c not yet allowed
>
> - **7a and 7b PASS on board 1, image 1.11** (§6, 2026-11-13). 7b used a Pico 2 running
>   `tools/pulsegen`: t_cam within 0.063 µs of the analyser at every delay, and the 200/201 µs
>   boundary clean. 7a Step 3's repeat at the 5000 µs default timeout was not run.
> - **Open: one stray rising edge on J4.3 while board 1 powered up** (§6). Proposed, with the
>   7b wiring still on and the Pico powered and counting:
>   - the analyser on J4.4, triggered on a rising edge;
>   - then PSU on, USB in, `on`, `off`, USB out, PSU off, with `?` on the Pico after each.
>
>   The step that adds a shot is the source.
> - **Optional, at the same sitting:** `d 1050` and `cam shot 200 70 1000`. The strobes then
>   arrive 50 µs after the timeout, before the CPU has tidied up. Expect `CAM_TIMEOUT` and
>   `NEVER OPENED`, with no GPIO25 edge.
> - **7c is not yet allowed:** it needs the owner to lift the Pi-on-J8 and no-camera-on-J4
>   rules for 7c, a TXS0108E module (`BRINGUP_NEW_BOARD.md` Step 13.4), and the Mira220's
>   trigger mode set up on the Pi (not in this repo). Its step list gets written once those
>   are settled.
> - **Pass on to Board 4's collaborator:** Step 13.4's no-camera A1 check was wrong (§6).

> ### 2026-11-12: board 1 on image 1.11; `BENCH_P8_PI.md` §8.10 PASS — next, Phase 7 (board choice open)
>
> - **Board 1 runs 1.11** from partition A (bought; 1.7 in B is the fallback), updated from the
>   Pi. §8.10 Step 1b (`reset`/`bootsel` with `wdog on`, Pi off): PASS on the chip.
> - **§8.10 PASS: 9 A strobe pulses with a Pi on the rail.**
>   - The +5 V rail's true lowest point is ≈ 4.94 V idle and ≈ 4.96 V loaded (DMM 5.201 V
>     less the analyser's dips), against 4.75 V.
>   - `cal 9` solved 439 twice, and ADC0 read 9.00–9.06 A.
>   - `throttled=0x0`, and the session's kernel log is clean. 1000 pings: 1000 good
>     frames, 0 CRC errors.
>   - `hist`: no cut, halt or fault. FLIR: no change.
>   - Details: §6, "Board 1: the strobe with a Pi present".
> - **Phase 7 next.** Board 1 can run `BENCH_P5_P7_MIC_CAMERA.md` 7a/7b on 1.11 with no
>   reflash: `camera.c`/`shot.c` are unchanged since 1.7, and the 1.9 and 1.11 reviews
>   confirmed the shots stay dry. Remove the Pi first (PSU and USB off). The optical trigger,
>   dead on board 1, is not used: every shot is started from the CLI. The owner chooses
>   board 1 or Board 5.
> - **7b's generator: `tools/pulsegen` on a spare Pico or Pico 2** (2026-11-12; built in
>   `tools/pulsegen/build_pico*/`; how to load and wire it is in 7b's board state).
>   - Not the Pi 5. From user space its timing jitters by several µs, with longer
>     outliers, which blurs 7b's 200/201 µs boundary.
>   - Its RP1 PIO could time it exactly, but that needs new tooling, and the Pi wired off
>     J8 to J4 by flying leads on its own supply.
> - The 1.11 review's V111-N1/N2 wording notes wait for the next image. Board 4 can take
>   1.11 from its Pi the same way.

> ### 2026-11-12: image 1.11 confirm review PASS — reflash first for §8.10 acceptance
>
> - **V110-F1 closed; sampled V19-F1 closure complete.** `reset` and `bootsel` check again after their
>   50 ms output wait, immediately before the reboot. The watchdog is disarmed only after
>   that check, so a refusal keeps it. Neither command runs while the board is POWERING_ON.
> - **Evidence:** 299/299 host tests. The 1.10 reviewer's probe on 1.11: both of its
>   V110-F1 witnesses now fail, and the other 15 pass. 30/30 mutations, none by the build.
> - **Build:** `build_v1.11/`, `Nov 12 2026 15:42:51`, 247536 / 0 / 422684. `.bin` 243444 B,
>   `883D4EFA…CA1CFC31`; UF2 `E940AE6A…F9B72991`.
> - **Report:** `captures/review_2026-11-12/v1_11/INDEPENDENT_REVIEW_v1.11.md`. PASS is
>   software acceptance for the sampled rules/workflow, not physical acceptance.
> - **Reflash first:** the exact 1.11 above, Release `Nov 12 2026 15:42:51`,
>   247536 text / 0 data / 422684 bss, UF2 SHA256
>   `E940AE6A21A0B86985075575E36C9EC894F04E129B52E183A5EC62A8F9B72991`.
>   The owner updates board 1 from the Pi, then runs §8.10 Step 1's real-chip
>   reset/BOOTSEL checks in verified STANDBY, latch open and Pi off, before any live
>   Pi/strobe measurements. Hardware acceptance is pending; no flash occurred in review.
> - **Limits:** BOOTSEL still has its disclosed 10 ms ROM delay after the final sample.
>   Operator-owned watchdog plus stalled USB output can still reset the board; do not
>   extrapolate the Pi-off watchdog bench check to a powered Pi. V111-N1/N2 wording
>   notes are nonblocking and stay for the next image. Keep 1.8–1.10 off every board.
> - **Until 1.11 is on a board** (board 1 runs 1.7; board 4 runs 1.5 or 1.7): don't type
>   `reset`, `bootsel` or `cfg save` while a Pi is powering up. Wait for `stat` to show
>   PI_BOOTING or RUNNING.

> ### 2026-11-12: image 1.10 confirm review NOT PASSED — do not flash
>
> - **V19-F1 is only partly closed.** The new helper correctly refuses an already sensed
>   Pi at command entry and protects config/telemetry/install guard sampling points.
>   **V110-F1:** plain reset/bootsel then wait 50 ms without rechecking. A Pi raising
>   3V3 at 6 ms into that wait still reaches reboot, including in normal POWERING_ON.
>   No actual reset was executed. The FSM itself and prior strobe fixes are unchanged.
> - **Evidence:** 298/298 host tests. The 1.9 reviewer's probe on 1.10: its V19-F1 witness
>   now fails at the plain reset, and the other 12 pass. 25/25 mutations, none by the build.
> - **Build:** `build_v1.10/`, `Nov 12 2026 15:13:28`, 246560 / 0 / 422684. `.bin` 242468 B,
>   `ECB0746B…2626029A`; UF2 `F5BF95F2…CFF48060`.
> - **Report:** `captures/review_2026-11-12/v1_10/INDEPENDENT_REVIEW_v1.10.md`. Keep
>   1.8, 1.9 and 1.10 off every board. Correct the final reset decision and watchdog
>   ordering, add regressions/mutations, and confirm a newly identified candidate.
>   Only then reflash first for §8.10; the owner's approval remains recorded.
> - **Until a corrected reviewed image is on a board** (board 1 runs 1.7; board 4
>   runs 1.5 or 1.7): don't type
>   `reset`, `bootsel` or `cfg save` while a Pi is powering up. Wait for `stat` to show
>   PI_BOOTING or RUNNING. This is the reviewer's note below.

> ### 2026-11-12: image 1.9 overall review NOT PASSED — flash neither 1.8 nor 1.9
>
> - **1.8 failed its review** (V18-F1: live mode read the power FSM's debounced Pi flag).
>   Keep it off every board. Report: `captures/review_2026-11-12/INDEPENDENT_REVIEW_v1.8.md`.
> - **1.9 closes V18-F1/N1, but Q4 found pre-existing V19-F1:** during late-Pi detection,
>   plain reset/bootsel and config saving still trust cached presence. Earlier reviews
>   missed this; approved 1.7 also has the same guard bodies. Avoid these actions during
>   Pi startup/late detection rather than trusting their cached-presence guard.
> - Rejected 1.9: `build_v1.9/`, `Nov 12 2026 11:06:20`, UF2 `0004839E…52247D16`,
>   BIN `32409AE6…B57766B9`; source and bytes preserved. Report:
>   `captures/review_2026-11-12/v1_9/INDEPENDENT_REVIEW_v1.9.md`.
> - Correct V19-F1, add real-FSM regressions and deliberate mutations, build under a new
>   identity, and obtain independent confirm review. Only then reflash first for §8.10.
>   Owner approval stands; its software gate remains unmet. Board 1's last recorded
>   image is 1.7 in B. This review operated no hardware.


> ### 2026-11-11: Board 4 bring-up in progress (owner) — §6, "Board 4"
>
> **Board 4: full calibration, then it ships to a collaborator. Board 5 (a new bring-up) takes
> Phase 7** (owner, superseding "Board 4 for 7a"). Board 4: UID `8fabac4bb8add852`, silicon A4;
> the TIA is reported as the original-board target.
>
> **Done:** the partition table; r5 in B, bought (loaded by picotool `-p 1 -v -x`, because this
> PC blocks the USB drive); E9; §4 (scale 1.0781, saved in slot A); Steps 5.1–5.2 PASS (VIR
> 35.94 V, TP2 12.28 V, TP7 2.601 V; FLIR clean); Steps 5.3–5.5 PASS (5.4 on the rail alone,
> by J7 leads; 5.5's plateau 579 ms). **§5 complete** (§6, "Board 4"). Steps 6.1–6.4 PASS
> (carrier 9.600 µs; U9 clamp 120.96 µs; LED base 92.9 °C at the 20-minute plateau). 6.5 and
> 7.1 PASS (ADC2 3207 codes; TP6/7/9/10 all 2.601 V). 7.2 PASS (TRACK = 0 confirmed, 53 pA;
> saved in slot B, seq 14). §8 PASS, linear to 25 % (its large swing was most likely the wall
> in view). §9 PASS: phase 1333 ticks, 69 % FS off a white wall ~1.2 m away; model PURE DELAY,
> 482–510 ns, residual 0.23°. §10 PASS: vref 3.254 V, Vos −0.6 mV, crosstalk 2.4 mV. §11
> PASS: slot B, seq 16. **Base bring-up §0–§11 complete**; the sign-off column is in §6,
> "Board 4".
>
> **Skipped (owner):**
> - §2, with its readings taken at Step 5.2;
> - the currents: the PSU has no readout. Take them before §13.
>
> **Next for Board 4.** It goes to a collaborator for the Pi side, the camera and the strobe;
> 12.1 PASS. `BRINGUP_NEW_BOARD.md` §12–§13 are now self-contained:
> 1. **The rest of §12, by the collaborator, without a scope or LA** (owner: no soldering on
>    Board 4; Board 5 keeps the scope/LA work):
>    - 12.2 parts 1–4, with 12.1's DMM checks and the R65/R66 inspection;
>    - 12.3, where U5's clamp is the ADC0 on-time;
>    - 12.4.
>
>    Part 5 (the gate DAC) PASS by the owner. Optional while unpowered: the R80 check (Step
>    0.1, 119–122 kΩ).
> 2. **Image 1.6 independent review NOT PASSED; do not flash it.** V16-F1: its
>    coarse `level` scan can create ADC clipping and advise reducing acceptable light.
>    - `build_v1.6/`, `Nov 11 2026 15:57:11`, UF2 `AB4E4F1E…82284507`; 276/276 host tests,
>      13/13 mutations caught.
>    - Report: `captures/review_2026-11-11/INDEPENDENT_REVIEW.md` (§6, "Image 1.6").
> 3. **Owner, 2026-11-11: Board 4 ships within 20 minutes, on image 1.5, with the Pi
>    interlock kept.** There was no time to build and review a new image.
>    - **Loading:** 1.5 into A by `picotool load -p 0 -v -x build_v1.5\pitrac.uf2`, with r5
>      kept in B as the fallback.
>    - **Before packing:** `threshold duty 0` and `cfg save`, then a power cycle.
>    - **The sheet says:**
>      - 1.5 in A, 1.1 in B;
>      - no threshold restore on 1.5;
>      - the camera handshake not yet bench-run;
>      - the strobe refused with a Pi present;
>      - 12.2 parts 1–4, 12.3, 12.4, 13.1's rollback and 13.2 for the collaborator.
>    - **Done (owner's paste):**
>      - `id` → `version : 1.5 (partition A at 0x002000)`, `built : Nov 10 2026 16:48:47`,
>        UID `8fabac4bb8add852`;
>      - `upd` → `flash update boot, ON TRIAL: bought itself`, and `partner : version 1.1,
>        bought: the fallback`;
>      - `cfg` intact: phase 1333, model fitted, TRACK = 0, scale 1.0781, cal duty 25 %;
>      - `strobe` → `U5 pulse limiter armed`, gate level 0, `pi absent`;
>      - `threshold duty 0`, then `cfg save` → slot A, seq 19;
>      - after the power cycle, `upd` → `normal boot`, partition A, the same partner.
>    - At power-up `cfg` prints `BEAM IS AT 2.00 % -- the stored phase is 166 ticks off`.
>      That is expected: the beam starts at 2 %, and the phase applies at 25 %. Added to
>      what the collaborator is told: ramp the beam to 25 % before detection.
> 4. **Image 1.7 independent review PASS, 2026-11-12** (§6, "Image 1.7").
>    V16-F1/N1 closed; nonblocking V17-N1: `level` is a relative aid, and its
>    `cal demod refuses` label can be false. Use the sweep for final acceptance.
>    - 1.6 without the all-phase `level`, plus V16-N1's comments.
>    - `build_v1.7/`, `Nov 11 2026 16:41:14`, UF2 `CCB7B931…BA15087B`; 273/273 host tests;
>      9/9 mutations.
>    - Report: `captures/review_2026-11-11/INDEPENDENT_REVIEW_v1.7.md`.
>    - **Reflash first when starting 1.7 acceptance:** the exact recorded 1.7 image, to
>      Board 4 by `pitrac-link update` from the Pi, or by §13.1 over USB with `-p 1`.
>      1.7 goes into B, and 1.5 in A becomes the fallback. No flash occurred in review.
>    - **Board 1 ran the same `pitrac-link update` path on 2026-11-12, from 1.2: PASS** (§6,
>      `BENCH_P8_PI.md` §8.9b).
>    - The Pi-present strobe unlock follows only after the owner's +5 V test on board 1 (§9,
>      "The strobe with a Pi present"), and the watchdog decision. It is not in 1.7.
> 5. Camera only through a TXS0108E: VA on the camera's 1.8 V, VB on the board's J5.2.
>
> **Phase 7 on Board 5:** the image-1.5 block below applies, with Board 5 in place of board 1.

> ### 2026-11-10: Phase 7 image 1.5 independent confirm review PASS -- reflash first, then 7a/7b
>
> **H7-F1/F2/F3 closed.** Report: `captures/review_2026-11-10/INDEPENDENT_REVIEW_r2.md`.
> The normal Release host suite independently passes 265/265. Four independent
> mutations in a source copy are caught; restored camera/engine tests pass 36/36.
>
> **Reflash first: the exact image 1.5** in `build_v1.5/`, Release
> `Nov 10 2026 16:48:47`, 241824 text / 0 data / 422684 bss,
> UF2 SHA256 `F82A6562C5F661D95197A2E582E54B11E9311DC5DB6196F49E95DC3AF85374B7`.
> BIN 237732 B, SHA256 `6ABFDBE4C28965DC8BBAC48B73FBD4945FC6AD769903253E4E9CF20B1716A9AA`.
> Both hashes and the image attributes are verified. Preserve this build; do not rebuild it.
>
> **Owner next:** `BENCH_P5_P7_MIC_CAMERA.md`, "Before 7a and 7b", then 7a and 7b.
> **No Pi on J8, no camera on J4**, strobe dry, J3 disconnected, beam off, in that
> procedure's board state. If removing a Pi or changing leads, PSU and USB both off.
> Hardware acceptance remains pending. The review did not flash, operate a board,
> change firmware bytes or commit anything.


> ### (superseded by the confirm PASS above) 2026-11-10: Phase 7 image 1.5 fixes H7-F1/F2/F3 — confirm review next, then 7a/7b
>
> **Image 1.5** (`build_v1.5/`, Release `Nov 10 2026 16:48:47`, `.bin` SHA256
> `6ABFDBE4…1716A9AA`): 1.4 with the review's three findings fixed (§6, "Image 1.5"). Host
> 265/265; mutations 34/34. **Not on any board.** Image 1.4 is superseded, and its build folder
> is removed.
>
> **Next, in order:**
> 1. **The confirm review** (`HANDOFF.md` §5 rule 8): `captures/review_2026-11-10/REVIEW_BRIEF_r2.md`,
>    with `phase7_r2_2026-11-10.patch` and `mutations_r2.md`.
> 2. **Then reflash first: 1.5** on board 1 by USB, with **no Pi on J8**
>    (`BENCH_P5_P7_MIC_CAMERA.md`, "Before 7a and 7b").
> 3. **7a**, then **7b**. **No camera on J4** (CR-09).


> ### (superseded by image 1.5 above) 2026-11-10: Phase 7 image 1.4 independent review NOT PASSED -- do not flash
>
> **1.4 remains off every board.** Its bytes are unchanged: `build_v1.4/`, Release
> `Nov 10 2026 13:46:40`, UF2 `96563EF9...48B54134`, BIN `3C894686...4F03BCB51`.
> The review is `captures/review_2026-11-10/INDEPENDENT_REVIEW.md`; reproductions are
> beside it in `independent_probes/` (all three reproduce; the existing 246/246 tests pass).
>
> - **H7-F1:** separate pin reads can accept two strobes that never overlap, then FIRE.
> - **H7-F2:** cancel after a power/fault/timeout teardown can falsely certify
>   `NEVER OPENED` after GPIO25 already pulsed.
> - **H7-F3:** the pre-existing `gpio 10 1` path still drives the trigger high with
>   the rail down; the camera engine's own rail check does not guard this path.
>
> **Next:** correct the findings, add a host regression and deliberate mutation per
> fix, build under a new recorded identity, and obtain an independent confirm review
> (`HANDOFF.md` section 5 rule 8). Only then update the 7a/7b image box and run the
> no-Pi/no-camera procedure. **Do not use the unchanged 1.4 candidate for 7a/7b.**
> No firmware rebuild, flashing, hardware operation or commit occurred in this review.


> ### (superseded by the independent review above) 2026-11-10: Phase 7 firmware written (image 1.4) — review first, then 7a/7b
>
> **Image 1.4** (`build_v1.4/`, Release `Nov 10 2026 13:46:40`, `.bin` SHA256
> `3C894686…4F03BCB51`): the camera handshake in PIO1 and a dry camera-gated burst; `cam`,
> `cam shot`, `cam trig` (§6). Host tests 246/246; mutations 20/20. **Not on any board.**
>
> **Next, in order:**
> 1. **The independent review** (`HANDOFF.md` §5 rule 8), from
>    `captures/review_2026-11-10/REVIEW_BRIEF.md`. Fix and re-test whatever it finds.
> 2. **Then reflash first: 1.4** on board 1 by USB, with **no Pi on J8**
>    (`BENCH_P5_P7_MIC_CAMERA.md`, "Before 7a and 7b"). 1.4 is newer than both images on board 1,
>    so its buy erases nothing.
> 3. **7a** (loopback leads on J4), then **7b** (a 3.3 V delayed-pulse source on J4.7/J4.8).
>    **No camera on J4** (CR-09).
>
> The owner's plan still holds: the new board's bring-up (`BRINGUP_NEW_BOARD.md`) can come first.
> 7a/7b need any board with a working strobe engine (6a) and the rail.

> ### 2026-11-03: additional-board optical rework check paused on ADC2 clipping (COM10)
>
> **UID `6093584a7790419d`**, r5 `Oct 20 2026 14:59:52`, version 1.1 **flat/no table**.
> Owner confirms **470 kohm R80 retained plus 162 kohm in parallel**, intended Rf
> **120.475 kohm**. Effective value/joints are unverified; the earlier 280/160 kohm
> readings have not been mapped or resolved. ADC5V trim **1.0688**, saved slot A seq 9.
>
> Raw TIA-monitor captures: 8% duty **166/400 at 4095**; 25% **198/400 at 4095** and
> **117/400 below 20**. Beam-off ADC2 **2.5788 V**. These prove ADC2 clipping, not a
> particular Rf or the TIA's positive rail; optical crosstalk remains a possible cause.
> The log ends `beam off`, **without a final rail-state check**.
>
> **Next:** `off`, `stat` -> STANDBY/latch 0, unplug PSU and USB, verify discharge; inspect
> and measure R80's original/added connections and PCB-pad resistance in both polarities,
> D12 shaded with insulating card. Follow [BENCH_REWORK_CHECK.md](BENCH_REWORK_CHECK.md)
> section 2. Do not infer a new Rf target or run/save optical calibration while clipped.
> UID-to-RW-A/B/C mapping and the other two additional boards remain pending.
>
> The **board-1 Phase 8 resume point below is separate and unchanged**.

> ### 2026-11-10: board 1 runs 1.2 from partition A with a Pi seated; Phase 8 §8.1–8.9 PASS
>
> **Board 1 now (2026-11-12):** version **1.7** (`Nov 11 2026 16:41:14`) in partition **B**,
> bought, installed from the Pi by `BENCH_P8_PI.md` §8.9b (PASS, §6).
> - Partition **A** holds 1.2, bought: the fallback. The next USB load or update goes into A.
> - Never load an image older than 1.1.
> - The calibration is intact (`stat`: adc5v scale 1.0694 after every update).
>
> **Phase 8 on board 1, all PASS:** §8.0b (2026-11-02); §8.7, first seating and §8.1–8.6
> (2026-11-09); §8.8 and §8.9 (2026-11-10). Q4, Q7 and Q11 are closed.
> - The owner relaxed the gate on 2026-11-09 (§8): a Pi on J8 for 8.0–8.5, 8.8 and 8.9, no
>   camera on J4, SW2 taped over, the Pi's USB-C unplugged. §8.10, live strobe current with
>   a Pi, approved 2026-11-12, to run on 1.9 after its review (1.8 failed).
> - Every other phase runs with the Pi off the board.
>
> **Next:**
> 1. ✅ **Image 1.7 from the Pi: PASS 2026-11-12** (§6), `BENCH_P8_PI.md` §8.9b, rewritten for 1.7 (1.3
>    never ran here). The Pi seated, **J3 disconnected**. It brings 1.3's 10 s holdoff and
>    label fixes, checks the Pi interlock with a real Pi, and rehearses Board 4's update.
>    `build_v1.7/`, `Nov 11 2026 16:41:14`, `.bin` SHA256 `F7129CD4…84C94602` (§6,
>    "Image 1.7").
>    - **Then, on board 1, before Board 5 can:** the strobe with a Pi present (§9, "The
>      strobe with a Pi present"). The watchdog is decided: not armed with a Pi powered
>      (owner, 2026-11-12; §8). **Image 1.8 review NOT PASSED** (§6, "Image 1.8").
>      Report: `captures/review_2026-11-12/INDEPENDENT_REVIEW_v1.8.md` (V18-F1).
>    - **Flash neither 1.8 nor 1.9.** V18-F1/N1 are confirmed closed in 1.9, but its
>      overall review did not pass on V19-F1 (§6, "Image 1.9"). Correct the remaining
>      guards, test/mutate the fixes and confirm a new candidate before §8.10.
>      The owner has already approved §8.10 (2026-11-12, §8); its software gate is unmet.
> 2. ✅ **6c's last steps, done 2026-11-10** with the Pi off the board: the session-cap check
>    PASS (level 439, cap 463) and Step 6 (§6). Phase 6 is complete on board 1.
>
> 3. **The owner's plan (2026-11-10): a new board, then Phase 7** (§6).
>    - The board: `BRINGUP_NEW_BOARD.md` §0–§11, then its "Before this board carries a Pi" list.
>    - Phase 7's firmware is not written: the handshake, `t_cam`, FIRING and `CAM_TIMEOUT`.
>    - 7c also needs the camera board's schematic (CR-09) and the strobe-with-a-Pi change.
>
> **Not now:**
> - First-seating step 5 cannot run up to image 1.7: the strobe refuses to fire with a Pi
>   present. From 1.8 it is `BENCH_P8_PI.md` §8.10.
> - §8.7 is needed on another board only before it carries a Pi.
> - The Pi tool's `NOTE` waits for its next change.
>
> **r5, still in `build/` for the other boards:** Release `Oct 20 2026 14:59:52`, 224720 text /
> 0 data / 422112 bss, version 1.1, TBYB. UF2 SHA256
> `AC5F4493F5A4C7E8B9B1A387BE128F42055410AFADC9A5B58C092ADBC272ACF7`.
>
> **Review:** `captures/review_2026-10-19/SOL_REVIEW_r5.md`. R4-F1 is closed; the prior
> fixes remain confirmed. 228/228 host, 51/51 Debug update, 23/23 Pi-tool and 21/21
> shutdown-hook tests independently pass. WSL is working again.
>
> **Done 2026-10-30 to 2026-11-09:** owner reflash, then `BENCH_P8_PI.md` 8.0b and 8.7, in their no-Pi/no-camera
> board state. **Before the reflash**, capture `id` and `cfg` on the old image (8.0b's
> intro): 8.0b Step 5 and 8.7 compare against them. The old image has no `hist`; its counts
> start with r5 (corrected 2026-10-30). **Done 2026-10-30:** `id` (`Oct 15 2026 22:09:11`) and
> `cfg` (slot A seq 3 v1, adc5v scale 1.0694) captured on COM5. **Do not
> rebuild `build/` or `build_hang/`:** a rebuild changes the stamp and the hashes, and the
> bench must run the reviewed bytes (`build_hang/pitrac.uf2`: `Oct 20 2026 14:59:56`, SHA256
> `7366EC15…13AD943`, checked 2026-10-20). `CMakeLists.txt` says 1.7 (2026-11-11), so a
> rebuild there would also change the version; later images are in `build_v1.2/`,
> `build_hang_v1.2/` and `build_v1.3/` to `build_v1.7/`. Check `id` against the exact stamp before continuing and record the trial,
> buy, rollback, calibration and telemetry results. Bench acceptance began 2026-10-30 (§8.0b).
> After those checks: the existing session-cap check in 6c, and 6c Step 6 (both done
> 2026-11-10).
>
> **2026-10-26, the new Pi 5 for Phase 8** (§6, "A new Pi 5 for Phase 8"): prepared off the
> board, every `pi/README.md` check passed, including the pin 37 key-press check and
> `pinctrl get 14-15` and `groups`. The card backup is made and verified (2026-10-30). **The Pi
> is ready to seat**: §8.0b and §8.7 passed, and the owner relaxed the Phase 8 gate on
> 2026-11-09 (§8).
>
> Incoming-image format hardening is a separate follow-up recommendation. Current scope
> trusts this repository's SDK builds; the general arbitrary-image case is not certified.
> No production-code or firmware-image edits, flashing or commits were made in the review.
>

> ### (superseded by the PASS above) RESUME POINT — 2026-10-20: r5 fixes R4-F1; next is Sol's confirm pass, then REFLASH FIRST and the no-Pi checks
>
> **Image r5, NOT flashed:** Release `Oct 20 2026 14:59:52`, 224720 text / 0 data / 422112 bss,
> SHA256 `AC5F4493…C272ACF7` (`Hardware/firmware/build/pitrac.uf2`), version 1.1,
> try-before-you-buy. Tests: 228/228 host, 23/23 Python; 83/83 mutations caught. The
> shutdown hook is unchanged since r3 (21/21 then); WSL still will not start. Board 1 still
> runs `Oct 15 2026 22:09:11`. **Do not flash r5 or install its partition table before the
> confirm pass.**
>
> **In it** (§6, "r5: R4-F1 fixed"): r4, plus a cleanup that counts as done only when sector 0
> reads back all 0x00 or all 0xFF, a test oracle that is the boot ROM's own block scanner, and
> an honest `partner` line in `upd`.
>
> **Next:**
> 1. **Owner:** send `captures/review_2026-10-19/REVIEW_RESPONSE_r4.md` and the files it lists
>    to Sol for a confirm pass. It also asks Sol whether the image check at UPD_END should be
>    made ROM-exact in this round. Return the reply.
> 2. **Model:** fix any finding, with a host test and a mutation each, rebuild, and hand over
>    with "reflash first".
> 3. **Bench, after the reflash, no Pi:** `BENCH_P8_PI.md` §8.0b and §8.7 (image
>    `Oct 20 2026 14:59:52` or later). Then, unchanged from r2: the session-cap check in 6c, and
>    6c Step 6.
>
> ⚠ **What changes for the operator after the reflash:** as in the r3 block below.
>

> ### (superseded 2026-10-20: r5 fixes R4-F1) RESUME POINT — 2026-10-20: r4 confirm review NOT PASSED; fix R4-F1 before any reflash
>
> **Board 1 stays on `Oct 15 2026 22:09:11`. Do not flash r4 or install its partition table.**
> r4 is unchanged: `Oct 20 2026 11:11:58`, version 1.1, SHA256 `015205B5…10A7FE96`.
>
> **Confirmed:** R3-F2's retained power-on obligation and both original reproductions.
> **Remaining R4-F1 (P1):** a header crossing the first 4 KiB boundary can survive partial
> cleanup while the limited parser reports no image, suppressing the fallback erase/CLEANUP.
> The independent production probe and the official ROM scanner reproduce the mismatch.
> Report and probes: `captures/review_2026-10-19/SOL_REVIEW_r4.md`.
>
> **Next:** fix cleanup and its regression tests, then obtain a confirm pass. After that
> review passes, hand over the exact image with "reflash first" and the existing no-Pi
> checks in `BENCH_P8_PI.md` §8.0b / §8.7. No hardware work was cleared by this review.
>

> ### (superseded by the r4 confirm review above) RESUME POINT — 2026-10-20: r4 fixes the review's R3-F1 and R3-F2; next is Sol's confirm pass, then REFLASH FIRST and the no-Pi checks
>
> **Image r4, NOT flashed:** Release `Oct 20 2026 11:11:58`, 224568 text / 0 data / 422112 bss,
> SHA256 `015205B5…10A7FE96` (`Hardware/firmware/build/pitrac.uf2`), version 1.1,
> try-before-you-buy. Tests: 224/224 host, 23/23 Python; 79/79 mutations caught. The shutdown
> hook is unchanged since r3, which passed 21/21; it was not rerun because WSL would not start.
> Board 1 still runs `Oct 15 2026 22:09:11`. **Do not flash r4 or install its partition table
> before the confirm pass.**
>
> **In it** (§6, "r4: R3-F1 and R3-F2 fixed"): r3, plus
> - a failed install clears what it wrote and proves it from flash, or reports `CLEANUP`;
> - the Pi's power-on is owed in the note from `UPD_APPLY` until it happens, across warm resets;
> - a build check that the note and the telemetry mirror stay where older images put them.
>
> **Next:**
> 1. **Owner:** send `captures/review_2026-10-19/REVIEW_RESPONSE_r3.md` and the files it lists
>    to Sol for a confirm pass. Return the reply.
> 2. **Model:** fix any finding, with a host test and a mutation each, rebuild, and hand over
>    with "reflash first".
> 3. **Bench, after the reflash, no Pi:** `BENCH_P8_PI.md` §8.0b and §8.7 (image
>    `Oct 20 2026 11:11:58` or later). Then, unchanged from r2: the session-cap check in 6c, and
>    6c Step 6.
>
> ⚠ **What changes for the operator after the reflash:** as in the r3 block below.
>

> ### (superseded 2026-10-20: r4 fixes both findings) RESUME POINT — 2026-10-20: independent review of r3 NOT PASSED; r2 confirmed; fix the two new update findings before any reflash
>
> **Board 1 stays on `Oct 15 2026 22:09:11`. Do not flash r3 or install its partition table.**
> The supplied r3 image is unchanged: `Oct 20 2026 06:13:43`, version 1.1,
> SHA256 `BD1E45D7…AD495AA4`.
>
> **Review result:** r2's two fixes are confirmed. New R3-F1 (P1) leaves a valid non-trial
> partner after a detected final-header write failure; R3-F2 (P2) loses Pi restoration if
> another warm reset occurs after result classification but before the delayed request.
> Report: `captures/review_2026-10-19/SOL_REVIEW_r3.md`, reproduction:
> `captures/review_2026-10-19/sol_update_r3_probe.c`.
>
> **Next:** correct both findings, add regressions and mutations, rebuild, and obtain a
> confirm review. Only after that review passes: hand over the exact image with "reflash
> first", then the existing no-Pi checks in §8.0b and §8.7 of `BENCH_P8_PI.md`.
> No hardware operations or production-code edits were performed in this review.
>

> ### (superseded by the independent review above) RESUME POINT — 2026-10-20: r3 adds the Pi serial link and firmware updates from the Pi; next is one review round (r2's confirm pass and r3), then REFLASH FIRST and the no-Pi checks
>
> **Image r3, NOT flashed:** Release `Oct 20 2026 06:13:43`, 222424 text / 0 data / 421752 bss, SHA256
> `BD1E45D7…AD495AA4` (`Hardware/firmware/build/pitrac.uf2`), version 1.1, try-before-you-buy.
> 204/204 host tests, 21/21 Python tests, 21/21 hook tests; 52/52 mutations caught. Board 1
> still runs `Oct 15 2026 22:09:11`.
>
> **In it** (§6, 2026-10-20): r2 unchanged (F1 and F2 fixed; its confirm pass is still owed),
> plus:
> - the serial link on UART1 (J8.8/J8.10): framed, CRC-checked requests from an allowlist;
>   `link` at the CLI; its pins connected only while a Pi is up;
> - firmware updates from the Pi: staged in RAM, installed into the other A/B partition after
>   the board shuts the Pi down, run on trial, bought or rolled back, the Pi powered again;
>   `upd` at the CLI, `pitrac-link` on the Pi;
> - the A/B partition table (`build/pitrac_pt.uf2`), installed once per board by
>   `BENCH_P8_PI.md` §8.7;
> - image version 1.1; `tools/flash_swd.sh` refuses by default.
>
> **Next:**
> 1. **Owner:** send `captures/review_2026-10-19/REVIEW_BRIEF_r3.md` and the files it lists to
>    the reviewer. One round covers r2's confirm pass and r3. Return the reply.
> 2. **Model:** fix any finding, with a host test and a mutation each, rebuild, and hand over
>    with "reflash first".
> 3. **Bench, after the reflash, no Pi:**
>    - `BENCH_P8_PI.md` §8.0b, about 30 min (now also checks the UART lines);
>    - `BENCH_P8_PI.md` §8.7, about 30 min: the partition table, a trial, a buy, a rollback,
>      the calibration surviving;
>    - then, unchanged from r2: the session-cap check in 6c, and 6c Step 6.
>
> ⚠ **What changes for the operator after the reflash:**
> - every reflash is a trial: `upd` shows `ON TRIAL: bought itself`;
> - after §8.7, reflashes alternate between partitions A and B, and **an image older than 1.1
>   must never be loaded** (HANDOFF §4);
> - bump `PITRAC_FW_VERSION_MINOR` in `CMakeLists.txt` for every image you will keep;
> - `tools/flash_swd.sh` refuses by default;
> - and r2's: `picotool` needs `bootsel` first; the COM port number may change; the board
>   writes its config sector by itself ~10 s into STANDBY.
>
> ### (superseded 2026-10-20: r3 adds the link and updates; one review round covers both) RESUME POINT — 2026-10-19: r2 fixes the review's two findings; next is the confirm pass, then REFLASH FIRST and the no-Pi checks
>
> **Image r2, NOT flashed:** Release `Oct 19 2026 11:44:38`, 207808 text / 0 data / 123508 bss, SHA256
> `841E353E…34398ABF` (`Hardware/firmware/build/pitrac.uf2`). 161/161 host tests, 21/21 hook tests;
> 49/49 mutations caught. Board 1 still runs `Oct 15 2026 22:09:11`.
>
> **Round 1 (`Oct 19 2026 06:23:15`) failed review on two findings, both fixed (§6, "Review findings fixed"):**
> - F2: a reset during FORCE_OFF could count one power cut twice;
> - F1: the Pi's shutdown hook acknowledged power removal without checking that the disks
>   were read-only.
>
> **In it** (§6, "Phase 8 prep firmware written"):
> - J8.37 driven only with the latch closed and the Pi's 3V3 up, released otherwise;
> - no USB path can reset the board (1200 baud, `picotool … -f`);
> - Phase 8.6 halt telemetry: `hist`, an unrequested-down watch in RUNNING, a RAM mirror and
>   flash;
> - the strobe session cap from `strobe cal` (board 1: level 463, 45.2 %);
> - a config-store fix (`cfg default` + `cfg save` was undone at the next boot);
> - comment corrections, and the Pi-side files in `pi/`.
>
> **Next:**
> 1. **Owner:** send `captures/review_2026-10-19/REVIEW_RESPONSE.md`,
>    `phase8_prep_2026-10-19_r2.patch`, `r1_to_r2.diff` and `mutations_r2.md` to the reviewer
>    for the confirm pass, and return the reply.
> 2. **Model:** fix any finding, with a host test and a mutation each, rebuild, and hand over
>    with "reflash first".
> 3. **Bench, after the reflash:**
>    - `BENCH_P8_PI.md` §8.0b: no Pi, about 30 min;
>    - then, in the 6c board state with the FLIR, `strobe cal 9` must print `Session cap:
>      level ~463`, and `strobe gate 46` must be refused (nothing fires);
>    - then 6c Step 6 (`strobe live off`, `off`).
>
> ⚠ **What changes for the operator after the reflash:**
> - `picotool` no longer reaches a running board: `bootsel` first;
> - the COM port number may change;
> - the board writes its config sector by itself, ~10 s into each STANDBY after a boot or a
>   change (`hist` shows it).
>
> ### (superseded 2026-10-19: Phase 8 prep firmware written) RESUME POINT — 2026-10-16 (Session A PASS): board 1 runs the readback-fix image; next is Session B, the live re-check (FLIR required)
>
> **Board 1:** `Oct 15 2026 22:09:11` since 2026-10-16. Dry checks PASS (§6): `id`; `stat` (`5V_IN
> fresh`); `adc ringtest 100` (200/200 covered, brackets 14 cycles); `capture 1 32 100` refused.
> **Step 8 (gate 0): PASS** (§6): no current on the Saleae or in the firmware.
> **Step 9 (staircase to 24 %): PASS** (§6): 21 % 0.107 A, 24 % 0.702 A, against the Saleae's
> 0.114 / 0.704 A. **The readback fix is bench-verified.**
> **Step 10 (Step 3 to 30 %): PASS** (§6). 27 % read 1.685 A and 30 % 2.915 A, within 0.5 % of
> the Saleae. No overshoot, and no temperature change.
> **Step 4 (`strobe cal 9`): PASS** (§6). Four runs solved level 439 (once 438), and the
> confirmations read 8.973–9.023 A. TP3 was 4.233 V on the DMM; the Saleae read the pulses at
> 8.98 A (−0.4 %).
> 🔵 **After any idle timeout or re-arm,** `strobe cal 9` is the quickest way back to 9 A: it
>   climbs from gate 0 by itself in ~6 s.
> 🔵 **Pi on J8 (owner's question, 2026-10-16):** answered in §6. Two firmware gaps must close
>   before a Pi is seated (§9). No bench change now.
> **Step 4b, measured on the TDS from photos** (§6): a ~0.15 V step at the edge, and ~12.4 V
> across Q9 at the start of a 9 A pulse. Step 5 corrected its C to ~600 µF.
> **Finding: the boost goes into hiccup after 50 µs and 100 µs pulses at 9 A** (not 20 µs),
> and VIR stays down for ~125 ms. The scope's AC coupling made the recovery look like an
> overshoot.
> **The firmware side: PASS** (§6). Every firing was `within limits`, and the 100 µs pulses held
> a flat 9.06 A.
> **The DC-coupled VIR shot:** 36.2 → 34.2 V and back, with no overshoot. FLIR: no change on any
> component. **Step 4b is complete;** the +5 V shot stays optional.
> **Step 5 (bursts at 9 A): PASS on the firmware's criteria** (§6). Every burst `within limits`,
> first → last within −0.4 %, FLIR flat.
> The 20 µs bursts keep VIR flat. The 50 µs and 100 µs bursts sag 7.4 V and 8.8 V from a
> ~600 µF bank, leaving ~3.5 V across Q9.
> **The gate node and TP4 during bursts (Saleae): PASS** (§6). The drive held within ±1 mV
> between pulses, and TP4 agreed with ADC0 within ~1 %. **Step 5 is complete.**
> **Owner, 2026-10-16:** thermals closed (sane, no rise), and the 6 mC burst limit stays (§8).
> **The Phase 6 exit list is complete on board 1.**
> **Decided:** the live gate ceiling becomes a session ceiling from the cal (firmware queued,
> §9); the next board keeps hiccup and adds a low-drain VIR monitor (CR-25, CR-08).
> **The +5 V shots (logic analyser): PASS** (§6). Every boost refill dips +5 V ~0.30–0.33 V for
> ~0.28 ms: once per slow-ball shot, and after every pulse of a fast-ball burst. The minimum is
> ≈ 4.85 V. UVLO is ruled out, so the outage is hiccup.
> **Next:** Step 6 (`strobe live off`, `off`); the queued firmware change (session ceiling, J8.37,
> USB reboot paths, comments) when the owner asks; then the next phase.
> Then Step 6 (`strobe live off`, `off`) and the next phase.
>
> **Session B — live, FLIR on Q9 and HS1 required:**
> - LA: TP4 and the gate node on analog, R61 on digital, grounds on the R66 wires.
> - 6c Steps 1–2 at gate 0: `floor ~22 codes`, `no current pulse above the ~72 mA floor`,
>   `covered`.
> - The staircase from 3 to 24 %:
>   - 3–18 %: no current;
>   - 21 %: ≈ 0.10 A, 20 µs;
>   - 24 %: ≈ 0.68 A, 20 µs, with TP4 ≈ 92–95 mV above its baseline;
>   - every firing `within limits`, with no `NOT CLEAN` or `OVERCURRENT` line.
> - **That bench-verifies the readback fix** (close the §9 item).
>
> **Then:** 6c Step 3 above 24 % in 3 % steps, with its stop rules, up to ~2–3 A. Stop there,
> and send the scrollback, the LA CSVs and the FLIR readings before `strobe cal 9`.
>
> ### (superseded 2026-10-16: Session A PASS) RESUME POINT — 2026-10-16: REFLASH FIRST — the live readback fix passed review (three rounds); bench re-check, then Step 3 above 24 %
>
> **Image:** Release `Oct 15 2026 22:09:11`, 198840 text / 0 data / 123220 bss, SHA256 `FCC1AF66…18195B57`
> (`Hardware/firmware/build/pitrac.uf2`). Board 1 runs `14:43:50` until reflashed.
>
> **Session A — dry, no FLIR needed:**
> - Reflash: `bootsel`, then drag the UF2 onto the drive.
> - `id`: the stamp above; the last `fw` line reads `+ live readback: ...`.
> - `stat`: `5V_IN fresh`, with no "last sampled" line.
> - `adc 0 4096`: < 10 codes.
> - `adc ringtest 5`: 2 µs per sample.
> - `adc ringtest 100`: `PASS: every pair covered.`, brackets ≤ 40.
> - Optional:
>   - `capture 1 32 100` must be refused (733–500000 Hz);
>   - `adc5vcal <DMM volts>`, then `stat` agrees within ~10 mV (RAM only unless `cfg save`).
>
> **Session B — live, FLIR on Q9 and HS1 required:**
> - LA: TP4 and the gate node on analog, R61 on digital, grounds on the R66 wires.
> - 6c Steps 1–2 at gate 0: `floor ~22 codes`, `no current pulse above the ~72 mA floor`,
>   `covered`.
> - The staircase from 3 to 24 %:
>   - 3–18 %: no current;
>   - 21 %: ≈ 0.10 A, 20 µs;
>   - 24 %: ≈ 0.68 A, 20 µs, with TP4 ≈ 92–95 mV above its baseline;
>   - every firing `within limits`, with no `NOT CLEAN` or `OVERCURRENT` line.
> - **That bench-verifies the readback fix** (close the §9 item).
>
> **Then:** 6c Step 3 above 24 % in 3 % steps, with its stop rules, up to ~2–3 A. Stop there,
> and send the scrollback, the LA CSVs and the FLIR readings before `strobe cal 9`.
>
> ### (superseded 2026-10-16: round 3 PASS, handover) RESUME POINT — 2026-10-15 (late): review round 2 PASS; two-of-three overcurrent added at the owner's choice (r3); next is Sol's round-3 confirm, then the final handover
>
> **Update 2026-10-15 (§6, "review round 2 PASS"):**
> - Sol passed r2.
> - The owner chose Sol's two-of-three rule. It is in r3: 117/117 tests, and 7/7 new mutations
>   caught.
> - Provisional image r3 `Oct 15 2026 22:09:11` (Release, SHA256 `FCC1AF66…18195B57`). **Not flashed.** Board 1 still
>   runs `14:43:50`.
> - **Next:**
>   1. **Owner:** send Sol `captures/review_2026-10-15/REVIEW_RESPONSE_3.md`,
>      `fix_2026-10-15_r3.patch` and `r2_to_r3.diff`, and return the reply.
>   2. **Model:** the final handover: stamp, size and SHA256, "reflash first", and the bench steps.
>   3. **Bench, reflash first:**
>      - `id`;
>      - `adc ringtest 100`: 200/200 covered, brackets ≤ 40;
>      - Steps 1–2 at gate 0;
>      - 21 % and 24 % pulses must read as on 2026-10-15: 0.10 A and 0.68 A, 20 µs, `within
>        limits`;
>      - **the FLIR is required before any live firing.**
>   4. Then Step 3 above 24 %, `strobe cal 9`, the VIR measurement (§3 Q13), and the Step 5
>      bursts.
> - **Instruments (2026-10-16, §6):**
>   - the Saleae alone covers items 3 and 4 up to `strobe cal 9`;
>   - the VIR measurement and the Step 5 droop need a scope (36 V);
>   - **never put the logic analyser on the J3 screws.**
> - Optional, dry, any time: the gate's static stability at 50 % and 90 % on the MSO54B.
>
> ### (superseded later 2026-10-15: round 2 PASS, two-of-three) RESUME POINT — 2026-10-15 (night): review round 1 found four issues, all fixed (r2: 117/117 host tests, 21/21 mutations); next is Sol's round-2 check, then reflash and the bench re-check
>
> **Update 2026-10-15 (§6, "independent review round 1"):**
> - **Fixed:**
>   - the plateau overcurrent now covers every run;
>   - capture rates outside 733–500000 Hz are refused;
>   - the snapshot's first-pass lower bound;
>   - an unusable record no longer prints "no current".
> - Round 7 of 2026-10-12 is now logged: confirmed, qualified by two of these, both fixed.
> - Provisional image r2 `Oct 15 2026 21:23:39` (Release, SHA256 `3B93E2F0…E83E9B7B`). **Not flashed.** Board 1 still
>   runs `14:43:50`.
> - **Owner's call:** keep the sampled overcurrent policy as written (alternating 9 / 12 A
>   passes), or tighten it.
> - **Next:**
>   1. **Owner:** send Sol `captures/review_2026-10-15/REVIEW_RESPONSE_2.md` and
>      `fix_2026-10-15_r2.patch`, and return the reply.
>   2. **Model:** fix anything it finds. Then the final build, with stamp, size and SHA256.
>   3. **Bench, reflash first:**
>      - `id`;
>      - `adc ringtest 100`: 200/200 covered, brackets ≤ 40;
>      - Steps 1–2 at gate 0;
>      - 21 % and 24 % pulses must read as on 2026-10-15: 0.10 A and 0.68 A, 20 µs, `within
>        limits`;
>      - **the FLIR is required before any live firing.**
>   4. Then Step 3 above 24 %, `strobe cal 9`, the VIR measurement (§3 Q13), and the Step 5
>      bursts.
> - Optional, dry, any time: the gate's static stability at 50 % and 90 % on the MSO54B.
>
> ### (superseded later 2026-10-15: review round 1 fixed) RESUME POINT — 2026-10-15 (evening): the live readback fix is written (114/114 host tests, 16/16 mutations); next is Sol's review, then reflash and the bench re-check
>
> **Update 2026-10-15 (§6, "Live readback fix written"):**
> - Pulses are found against a fixed ~72 mA floor.
> - A spike or dip beyond ±25 % (+0.14 A) of a pulse's settled level ends live mode (SHAPE).
> - A level held over 10.8 A for two samples is OVERCURRENT.
> - The clamp width is checked over every run.
> - Provisional image `Oct 15 2026 15:15:22` (Release, SHA256 `7581DA0E…6B1EA0F6`). **Not flashed.** Board 1 still runs
>   `14:43:50`.
> - **Next:**
>   1. **Owner:** send Sol `captures/review_2026-10-15/REVIEW_BRIEF.md` and
>      `fix_2026-10-15.patch`. For the outstanding round-7 confirm, also send
>      `captures/review_2026-10-12/REVIEW_RESPONSE_7.md` and `fix_2026-10-12_r7.patch`. Return
>      the reply.
>   2. **Model:** fix what the review finds, with tests and mutations. Then rebuild and give the
>      final stamp, size and SHA256.
>   3. **Bench, reflash first:**
>      - `id`, then `adc ringtest 100`, then Steps 1–2 at gate 0;
>      - 21 % and 24 % pulses must read as on 2026-10-15: 0.10 A and 0.68 A, 20 µs, `within
>        limits`;
>      - **the FLIR is required before any live firing.**
>   4. Then Step 3 above 24 %, `strobe cal 9`, the VIR measurement (§3 Q13), and the Step 5
>      bursts.
> - Optional, dry, any time: the gate's static stability at 50 % and 90 % on the MSO54B.
>
> ### (superseded later 2026-10-15: readback fix written) RESUME POINT — 2026-10-15 (later): 6d PASS on board 1 at 24 % (U5 ends 0.70 A at 135 µs of 1000 commanded); next is the readback fix, with review, before anything above 24 %
>
> **Update 2026-10-15 (§6):**
> - 6d: two `strobe clamptest 1000` at 24 %. Firmware: `0.687 / 0.689 A plateau, on 134 us`,
>   `within limits`.
> - Scope: U5 Q high 134.98 µs. TP4's current is 135.0 µs wide at 0.703 A, with nothing after
>   it to +360 µs. ADC0 covered ~1250 µs and found one run.
> - The gate node and TP3 hold (1 µs averages within +-5 / +-9 mV).
> - **Next:**
>   1. The firmware readback fix (§9 "Half-peak threshold"):
>      - an absolute floor for run detection and the stuck-on/width checks;
>      - a stop on a turn-on peak far above the settled level;
>      - accurate "no current" text.
>
>      Then host tests and Sol's review. **Reflash first** after it.
>   2. Then 6c Step 3 above 24 %, `strobe cal 9`, the VIR measurement (§3 Q13), and bursts
>      (Step 5).
>   3. **The FLIR on Q9 and HS1 before any live firing** (the 6c board state). There was none
>      on 2026-10-15, so the next live session waits for it.
> - Board 1 still runs `14:43:50`. The 1 µF rework is on board 1 only.
>
> ### (superseded later 2026-10-15: 6d PASS) RESUME POINT — 2026-10-15: board 1's gate-drive rework is verified (flat 0.70 A at 24 %); next is 6d at 24 %, then the readback fix before anything above 24 %
>
> **Update 2026-10-15 (§6):**
> - 1 µF X5R fitted at Q9's gate, behind R63 (CR-23 stage 1).
> - Dry checks pass; C_eff ~0.5-0.8 µF.
> - Loaded at 21/24 %:
>   - flat from 0.1 µs, with a ~20 ns rise;
>   - 1-us peak 1.02x settled (was 3.3 A on 0.72 A);
>   - the gate node moves +2 to +6 mV;
>   - ADC0 agrees within 3 %.
> - The plateau's ~1 MHz "noise" is boost pickup: it is present with Q10 off.
> - **Next:**
>   1. **6d at 24 %** (0.68 A).
>   2. The firmware readback fix (§9 "Half-peak threshold"), with review.
>   3. Then Step 3 above 24 %, `strobe cal 9`, and bursts.
> - Board 1 still runs `14:43:50`.
>
> ### (superseded 2026-10-15) RESUME POINT — 2026-10-13: 6c Step 3 STOPPED at gate 24 % (gate-drive overshoot, 3.3 A on 0.72 A; readback blind spot). No live firing until both are fixed.
>
> **Why (§6, 2026-10-13, gate 24 %):**
> - At turn-on the current overshoots to **3.3 A for ~2.5 us** before settling at 0.72 A.
> - TP3 rises >0.7 V at turn-on, because U7 can source but cannot sink quickly: Q9's source
>   drags the gate up, and the sense resistor's degeneration is defeated until the LM358
>   catches up.
> - At the 9 A setting this could be tens of amps for microseconds.
> - ADC0 printed the pulse as "no current" (half-peak threshold), and the staircase counted
>   it as a pass.
>
> **Next, in order:**
> 1. Firmware: fix the live readback (§9 "Half-peak threshold"), with Sol's review.
> 2. Hardware: the gate-drive fix, `NEXT_BOARD_REV.md` **CR-23**, analysed 2026-10-13.
>    - Board-1 rework candidate: 1 uF at Q9's gate, behind R63, to R66's GND pad.
>    - Then 6b's TP3 stability check, and 6c Steps 2-3 up to 24 % on the Saleae (proposed;
>      owner to decide).
> 3. Only then resume Step 3, from gate 3.
>
> **Board state:** live mode off and rail down (owner to confirm). LED leads twisted. Probes:
> CH1 TP4, CH2 TP3, both 1X; Saleae A2 on TP4.
>
> *Earlier on 2026-10-13 (history):* both J3 joints were cold (fixed); 6c Step 2 passed with the
> load; first current came at gate 21 %.
>
> 🔴 **Update 2026-10-13:** the staircase ran from 3 % to 33 % with **no current at any step**,
> although TP3 reached 3.27 V nominal and the readback was clean (§6). **Cause found:** a cold
> joint on J3 pin 1. C26 read 36 V and the J3 screw read 7 V. The owner has re-soldered it.
> **Next:**
> - Pre-power DMM checks.
> - VIR at the J3 screw = 36 V with the rail up.
> - ✅ Step 2 again at gate 0 with the load: PASS (3 firings, no current, Q9 cold), with a
>   fresh gate-0 edge reference (§6).
> - The staircase from gate 3 again reached 30 % with **no current**: **J3 pin 2 was also a
>   cold joint**, now re-soldered.
> - ✅ VIR at the J3 screw = 36 V; J3.2 -> Q9's tab < 1 Ohm. Step 2 at gate 0 PASS with the bank
>   fully connected. `strobe wave` was 10-12 codes, so a 50 kHz triangle on the scope's TP4 is
>   pickup (§6).
> - **First current at gate 21 % (2026-10-13), but the step fails Step 3's pass criteria.**
>   - TP4 shows a ~0.35 A turn-on peak, then a ~1 MHz ripple for ~10 us, settling at ~0.07 A.
>   - ADC0's half-peak threshold mis-measures that shape: it printed "no current" on the first
>     firing, then a 4 us "short" pulse (§6; firmware finding in §9).
>   - Resolved: the 1 MHz was boost pickup.
>   - The real shape is a slow gate-loop transient: ~0.35 A overshoot, a sag to 0.05 A at
>     ~7.5 us, then a steady ~0.11-0.13 A from 9 us (ADC0 and the Saleae agree) (§6).
>   - LED leads twisted (2026-10-13): the edge spikes and pickup are smaller; the slow
>     transient is unchanged.
>   - The scope's CH1 reads ~10x low. It is not the 10X switch (owner checked: 1X). Most likely
>     the tip has no contact with TP4.
>   - The CH1 probe contact is fixed, and the scope now agrees with the Saleae. A 50 us pulse
>     shows one recovery at ~9 us, then a steady 0.09-0.115 A to 50 us: no cycling.
>   - The first ~10 us (overshoot ~3x, sag ~0.4x) is the problem, and §15's pulses are 5-20 us.
>   - Next:
>     **one diagnostic step at 24 %**, then stop for review. No 6d until reviewed.
>
> **Firmware:** board 1 runs **`Oct 12 2026 14:43:50`** (Release, 194344 text / 122796 bss,
> SHA256 `2D3BD0C1…4FB6471D`, 102/102 host tests). Uncommitted; the owner commits. Live firing
> was gated on Sol's confirm of the round-7 changes (`REVIEW_RESPONSE_7.md`). The owner went
> ahead; the confirm itself is not logged here yet.
>
> **Done today (§6):**
> - `adc 0`: 9 codes. `adc ringtest 100`: 200/200 covered.
> - Steps 1–2 PASS: gate 0 gave `within limits` and `covered`; `strobe wave` read 10–11 codes.
> - The gate-0 full-bandwidth reference.
> - **Step 3 at gate 3:** no current, TP3 ≈ 0.3 V. A +0.1–0.2 V TP3 kick after Q10 turns off is
>   harmless (§6).
>
> **Scope today:** TDS 1002, both probes at **1X** (menus match): CH1 TP4, CH2 TP3, EXT
> trigger from the GPIO25 wire. Saleae: D1 GPIO25, D3 U5 Q, A2 TP4. Grounds on the R66 wires.
>
> **Next:**
> - Step 3 on up in 3 % steps, filling in the table at each step.
> - **6d at the first 0.5–2.5 A step.**
> - Stop at ~3–4 A for review.
> - Then `strobe cal 9` (Step 4) and bursts (Step 5).
>
> The stop rules are in Step 3 of `BENCH_P6_STROBE.md`.
>
> **Open:**
> - The §9 items, including 5V_IN unmonitored while ARMED or during a bench capture (fix before
>   Phase 7c).
> - Whether the R64 lead is still on board 1.
> - The 10 Ω board (2 or 3).
> - Board 2's post-spark check.

> ### (superseded later 2026-10-12: board 1 reflashed, Steps 1–2 PASS) RESUME POINT — 2026-10-12: 6c paused at Step 2; readback fix built, awaiting review, then reflash
>
> **What happened:** 6c Steps 0, A and 1 passed on board 1 (TDS 1002 + Saleae today, no MSO54B).
> The first live pulse at gate 0 latched a false **`STROBE_CLAMP`**. ADC0's "baseline" was 86
> leftover IDLE-ring samples (mean 1277.5 codes, "7.6 A"), because the BURST ring had written
> only 122 samples when the clock-sized copy was taken. TP4 was flat on the scope and the Saleae,
> Q9 stayed cold, and `adc 0` = 9 codes. §6 2026-10-12 has the diagnosis.
>
> **Firmware:** fix image **`Oct 12 2026 14:43:50`**, Release, 194344 text / 122796 bss, SHA256
> `2D3BD0C1…4FB6471D`, 102/102 host tests, 25 mutations all caught. Six review rounds (Sol
> passed round 6). Then the bench (`adc ringtest`) found the ADC at **half rate**, the real
> cause of the morning's "late start", and first-pass snapshot brackets. Both are fixed (§6). **Not flashed.** Board 1 still runs `Oct  7 2026 08:07:33`:
> **do not fire live on it.** Uncommitted; the owner commits.
>
> **Order:**
> 1. ✅ **Independent review** by ChatGPT 6.1 Sol (2026-10-12), six rounds, all fixed with
>    tests and mutations (§6).
>    - Round 1: 4 findings (1 safety).
>    - Round 2: the coverage tolerance was unsafe; replaced by an exact SysTick check.
>    - Round 3: the snapshot ran from flash with an assumed latency; an XIP fetch stall
>      could cancel one missing sample. **Fixed:** SRAM-resident, self-bracketing snapshots
>      that reject their own wide brackets (§6).
>    - Round 4: the index marks a transfer *issued*, not a completed write. Analysis
>      (`REVIEW_RESPONSE_4.md`, no code yet) finds:
>      - the coverage logic holds for the issue event, with D derived;
>      - a missing completion barrier before the freeze copy;
>      - our DMA abort not following RP2350-E5 (a candidate for the late start);
>      - a dead IDLE ring would blind the 5V_IN monitor.
>
>    - Round 5: the analysis accepted with qualifications. Implemented: E5-safe DMA stops,
>      the freeze completion barrier with error checks, the PIO-feed wait, and fresh-only
>      5V_IN with the new `SUPPLY_UNREADABLE` fault (§6).
>    - Round 6: the 5V reader could certify rotated or error data, and the triggered
>      capture ignored its stop result; both fixed. The timing bound was not closable by
>      argument. **Completeness is now proven from the ADC/DMA hardware flags** (no FIFO
>      overflow/underflow, ADC free-running to the stop, clean DMA); SysTick is a
>      cross-check (§6).
>      Sol passed round 6.
>    - Bench, image `13:55:58`: `adc ringtest` 0/10 covered. ADC0 arrived every 4 us
>      (DIV 95: the ADC ignored every other trigger). Fixed in `Oct 12 2026 14:43:50` (DIV 0). Sol to
>      confirm the fix; dry diagnostics can proceed meanwhile.
> 2. **Reflash board 1** (USB only is enough; BOOTSEL), then `id` → `built : Oct 12 2026
>    14:43:50` and the last `fw` line `+ ADC at true 500 ksps (DIV 0), synchronised
>    snapshots`. `adc ringtest 5`: `written at` must show **+400:200** (2 us per sample). Then `stat`: `5V_IN : ... fresh`.
> 3. Dry, rail down or up, gate 0: `adc 0 4096` (< 10 codes), **`adc ringtest 5`** (every pair,
>    warm and cold cache, `covered`) and **`adc ringtest 100`** (`PASS: every pair covered.`;
>    record the widest bracket and residual range). Send the output: it also measures the late
>    start.
> 4. 6c Steps 1–2 again (TDS 1002: CH1 TP4 DC with BW limit, CH2 TP3 DC; traces separated).
>    Step 2 passes with `within limits`, `covered`, the two splits agreeing, and `strobe wave`
>    ~10 codes end to end. Then the staircase. While there: `detect arm` then `adc ringtest`
>    must be refused; an idle timeout must print one notice, once.
>
> **Board state now:** board 1, rail down, J3 connected, leads and probes on. Fault cleared.
>
> **Still open for the owner:** whether the R64 lead is still on board 1; the 10 Ω board
> (boards 2/3); board 2's post-spark check.

> ### (superseded 2026-10-12: Step 2 stopped on a readback bug) RESUME POINT — 2026-10-09: board 1 on the 6c build, TP3 stability PASS; next is 6c's first live pulse
>
> 🔴 **Update 2026-10-12: 6c STOPPED at Step 2.** Steps 0, A and 1 passed. The first live pulse at
> gate 0 latched **`STROBE_CLAMP`**: ADC0 read a steady **1277 codes (1.03 V, "7.6 A")** before,
> during and after the pulse. This is almost certainly the ADC0 path (R32 / GPIO40) or the
> readback, not current (§6). **Do not `fault clear` or re-arm** until `adc 0` with the rail down
> and `strobe wave` explain it.
> **Explained the same day — a firmware readback bug, not current and not hardware.** `adc 0` =
> 9 codes. The record's first 86 samples were stale IDLE-ring data, because the copy is sized by
> time while the BURST ring had written only 122 samples. **6c is paused for a firmware fix**
> (§6, fix plan). No live firing on the `Oct  7` image.
>
> **New model or harness?** Read `HANDOFF.md` first (updated 2026-10-09 for this handoff), then
> this block, then `BENCH_P6_STROBE.md` 6c. Nothing below depends on the previous session.
>
> **Firmware:** `build/pitrac.uf2`, Release **`Oct  7 2026 08:07:33`**, 184440 text / 122720 bss,
> SHA256 `DE0C6676…2DE80BC2`, 78/78 host tests. **Board 1 is running it** — ✅ confirmed by `id`
> in 6c Step 0, 2026-10-12 (§6). In it: strobe **live mode** (6c/6d) with its guards; `strobe cal`, `strobe wave`; the
> "before 6c" §9 items; two review fixes (§6 2026-10-07). **No live pulse has been fired.**
> Everything through this handoff is committed (`a8e08b3`, 2026-10-09); later work is not, unless
> `git log` says otherwise. The owner commits.
>
> **Board 1** (`a764f5332ca5ac53`, COM5): leads on R33, R61 pad 1, TP3, TP4 and two on R66's GND
> pad. 6a/6b PASS 2026-10-06; **TP3 loop stability PASS 2026-10-08** (MSO54B: AC RMS 3.6–3.8 mV
> at every gate, flat; §6).
>
> **Order (all in `BENCH_P6_STROBE.md`):**
> 1. ✅ **Done 2026-10-12** (§6; LA widths still to be reported). J3 **disconnected**: `id` → `built : Oct  7 2026 08:07:33` and `fw` line `+ 6c/6d live current
>    (guarded, ADC0 per firing)` (reflash `build/pitrac.uf2` if not); `strobe` → `strobe   : DRY.`,
>    `U5 clamp <= 137 us (board 1 measured 135)`, `live     : OFF -- last ended: not armed since
>    boot`. A 10-second re-run of 6a's `strobe pulse 20` on the LA confirms the dry path.
> 2. ✅ TP3 at a static setpoint — done 2026-10-08.
> 3. PSU and USB off: the lead plan's DMM checks if not already done (6c board state, "Leads to
>    solder"); connect J3; probes on the TP4/TP3 leads, grounds on the R66 leads; MSO54B CH1 TP4,
>    CH2 TP3, **20 MHz BW limit** for plateau readings, Aux Trig from GPIO25; FLIR ready. 6c
>    Steps 1–6:
>    refusals and arming → first live pulse at **gate 0** (expect no current) → 3 % staircase,
>    ADC0 vs TP4 at every step (**stop if nothing is detected by gate 30 %** — open load) →
>    **6d at the first 0.5–2.5 A step** → `strobe cal 9` → bursts.
> 4. Bench tidy-ups (10 s each): 6b Step 4's `on` → TP3 ≤ 30 mV; the unrecorded TP3 ramp points;
>    Step −1 on boards 2 and 3 to identify the parked 10 Ω board.
>
> **Open questions for the owner** (unanswered as of 2026-10-09): which of boards 2/3 has the
> 10 Ω short on R61 (R62 is off it); board 2's post-spark damage check; board 1's fast-path
> bring-up currents/VIR were never sent; whether the R64 (Q10 gate) lead from the 2026-10-06 scope
> session is still on board 1; the firmware fix for the open-load staircase gap (§9) — before or
> after the first live pulses. **Answered 2026-10-12: after — 6c relies on the Step 3 stop rule
> (§6).** 6c runs on the **TDS 1002 + Saleae** (MSO54B unavailable), Saleae analog on TP4.
>
> **Open in parallel, deferred:** optical acquisition window, §3.7 20-pass set, CR-18.

> ### (superseded 2026-10-07: firmware written) RESUME POINT — 2026-10-06 (11:20): strobe 6a/6b PASS on board 1; next is 6c preparation
>
> **Board 1** (`a764f5332ca5ac53`) runs `Oct  5 2026 11:13:55`, with leads soldered to R33
> (GPIO25) and R61 pad 1 (U5 Q). **6a and 6b PASS** (§6): U5 clamp 135 µs; pulses and bursts
> exact on the LA; §15 schedule and shedding; A7 independent; interlock both ways; TP3 =
> 3 × 3.29 V × duty (4.936 V at 50 %, 9.88 V at 100 %); TP2 12.31 V.
>
> **Owed before 6c:**
> 1. **Scope:** ✅ Q10's gate done 2026-10-06 (≈ 12 V, +7 ns width, 55/45 ns edges; §6).
>    **Still:** TP3 at a static setpoint (`strobe gate 50`, no pulses), AC-coupled 10 mV/div,
>    20 µs/div and 2 ms/div, < 10 mV p-p — the U6A + U7 loop stability check. Run the scope's
>    probe compensation on both channels first.
> 2. **Firmware (my side, then reflash):** 6c guarded-current mode (setpoint ceiling, single
>    pulses first, ADC0 plateau per shot, watchdog armed), `STROBE_HW_LIMIT_US_ASSUMED` → worst
>    case ~137 µs plus the stale `strobe` status text, and the 🔴 §9 backlog items
>    (blocking-command service contract, strobe run token, `shot.c` deadlines).
> 3. **Bench tidy-ups (10 s each, next power-up):** 6b Step 4's `on` → TP3 ≤ 30 mV; the
>    unrecorded TP3 ramp points; Step −1 on boards 2 and 3 to identify the parked 10 Ω board.
>
> **Open in parallel, deferred:** optical acquisition window, §3.7 20-pass set, CR-18.

> ### (superseded 2026-10-06 11:20) RESUME POINT — 2026-10-06: board 1 → strobe-only bring-up, then 6a on the LA
>
> ✅ **Update 10:50: bring-up done, 6a.1 PASS — U5 clamp 135 µs** (§6). Next: 6a.2 → 6a.3 → 6b.
> Pending firmware batch after 6a.2: `STROBE_HW_LIMIT_US_ASSUMED` → worst case (~137 µs) and
> any PIO-overhead correction.
>
> **Board 1** (UID `a764f5332ca5ac53`, A4; U11B dead → TIA readings meaningless, strobe chain
> untouched). Steps, all in the owner's chat of 2026-10-06 and in the docs:
> **A** unpowered: inspect (nothing conductive near D12), Step −1 baseline (R61 → GND ≈ 1 kΩ,
> R33 → GND ≈ 1 kΩ), solder 30 AWG leads to **R33** and **R61 pad 1**, re-measure.
> **B** USB only: BOOTSEL, flash `pitrac.uf2`, `id` = `Oct  5 2026 11:13:55` + UID.
> **C** `BRINGUP_NEW_BOARD.md` §2 (USB out): 0.3 A no J2 → ~32 mA; J2 + 2 A → ~129 mA, +5 V
> 5.2, **VIR 36 (J3 pin 1 → GND)**, **TP2 11.4–12.7**, TP6 2.59; J2 off.
> **D** §3 `pins`, §4 `adc5v` ≈ 5.20. **E** §5 Test 1 (USB-only refusal) and Test 2 (latch).
> **F** LA: CH0 R33 lead, CH1 R61 lead, GND TP1 (attach unpowered); PSU 3 A; 6a Step 0 → Step 1
> → 6a.1 (`clamptest 100` refused; 200 ×3, 1000 ×3; record CH0/CH1 widths and delay).
>
> **Other boards:** the 10 Ω board is parked with R62 off (§6); identify it with Step −1 on
> boards 2 and 3. **Before 6c:** scope check of Q10's gate, 🔴 §9 backlog items.

> ### (superseded 2026-10-06) RESUME POINT — 2026-10-05 (afternoon): image flashed, smoke 13/13 PASS; 6a next, on the logic analyser
>
> **Board 3** runs `Oct  5 2026 11:13:55` (flashed and smoke-checked 2026-10-05, §6), calibration
> intact. **Instruments: logic analyser (Saleae, 50 MS/s) and DMM; no scope for now.**
>
> **Board state for 6a:** PSU 5.20 V / **3 A** limit on J1, USB on J6, **J3 disconnected**, J8/J4
> empty, beam off, `detect disarm`, file logging on. **Rail DOWN while attaching leads**, all LA
> grounds to board GND (not TP5): **CH0 GPIO25 at R57** (non-GND pad), **CH1 U5 Q at R61**.
> 3.3 V logic threshold, 50 MS/s, trigger GPIO25 rising, ≥ 2 ms capture. 🔴 Nothing on Q10's
> gate (R64) — up to 12.6 V, and it is the scope's job, due before 6c.
>
> Then `BENCH_P6_STROBE.md`: **Step 0** (rail down: `id`, `pins`, `strobe`, `strobe pulse 20`
> refused) → **Step 1** (`stat`, `on` only from STANDBY, 3 s, `strobe` → `may fire : yes`) →
> **6a.1** (`strobe clamptest 100` refused; `clamptest 200` ×3 and `clamptest 1000` ×3: record
> GPIO25 width, **U5 Q width = the clamp**, GPIO25↑→Q↑ delay; expect ~122 µs, band 109–136;
> **stop** if < 100 µs or ≈ the commanded width) → **6a.2** → **6a.3**. Then **6b** with the
> DMM on TP3 and TP2. Send the log, the LA captures (`.sal` or exported CSV) and the numbers.
>
> **Open in parallel, deferred:** the optical acquisition window, the §3.7 20-pass set, CR-18.
> **Before 6c:** the scope check of Q10's gate, and the 🔴 items of the §9 code-audit backlog.
>
> ⚠ **Update 13:40:** first 6a.1 attempt showed the pulse at R57 but **nothing at R61** —
> diagnosing (§6, last entry; triage table in `BENCH_P6_STROBE.md` 6a.1). Do not go on to
> 6a.2 until U5 Q shows the clamp.
>
> 🔴 **Update evening:** a hand-held probe on R61 **sparked on board 2** (§6). From now on:
> attach every lead with the board **unpowered** (PSU and USB off) and probe U5 Q through a wire
> soldered to **R61 pad 1** (§1 rule 6). Board 2 needs the damage check in §6 before further
> use. **Board 1** is acceptable for strobe work after the `BRINGUP_NEW_BOARD.md` fast path.
>
> ✅ **2026-10-06 — cause found:** both R61 pads and R62 have continuity to GND, so U5 Q /
> `GATE-DRV` is shorted to GND at R62's site (§6). Fix the short, confirm R61 pad 1 → GND
> ≈ 1 kΩ, then redo 6a.1. The ≈ 1 kΩ check is now a gate on every board before 6a.1.
>
> ⚠ **2026-10-06, later — superseded by the board-1 block below:** with R62 removed it still reads
> 10 Ω, so R62 was not it (§6). That board is parked with R62 off.

> ### (done 2026-10-05: flashed, smoke 13/13 PASS — §6) RESUME POINT — 2026-10-05: audit-fix build; reflash, smoke-check, then 6a
>
> 🔴 **Reflash first.** Release build **`Oct  5 2026 11:13:55`**, 164232 text / 88496 bss,
> 32/32 host tests, **never run on hardware**. It is the 2026-10-02 strobe 6a/6b dry-test
> firmware plus the 2026-10-05 audit fixes (§6): strict parsing of every numeric CLI
> argument, over-long/too-many-word lines refused, `threshold sweep` steps 1–1000, config
> version check, beam divider cap, `detect` FIFO-overflow warning, stale printed text. It
> replaces the unflashed `Oct 2 2026 09:27:16` image. Strobe current is still impossible.
>
> **Board state:** board 3, PSU **5.20 V / 2 A** on J1, USB on J6, **J3 (LED bank)
> disconnected**, no Pi, no camera. Rail may be up or down unless a step says otherwise.
>
> | # | Command | Expect | Pass |
> |---|---|---|---|
> | 1 | `id` | `built    : Oct  5 2026 11:13:55`; `fw` line ends `+ strobe 6a/6b dry tests (no LED current)` | stamp matches |
> | 2 | `cfg` | `source   : slot A   seq 5   v1`, carrier 104166 Hz, phase 1311 ticks, adc5v scale 1.0620, `hpf sel  : TRACK = 0`, phase mdl fitted | **all identical to 2026-09-18.** 🔴 If `source` says `defaults`: **STOP, do not `cfg save`**, send the output — the new version check rejected the record |
> | 3 | `help` | no `%%` anywhere; `panel pwr` and `panel rdy` on separate lines | as stated |
> | 4 | `gpio xyz` | `ERR: pin 'xyz' -- want 0-47. Nothing was changed.` | ERR, nothing else |
> | 5 | `capture 0 10 1000` | `ERR: mask '0' -- want a channel bitmask, e.g. 0x20 for ch5. Nothing was changed.` | ERR |
> | 6 | `capture trig 5 64 100000 95` | `ERR: pre-trigger '95' -- want 0-90 %. Nothing was changed.` | ERR, no `armed:` line |
> | 7 | `threshold sweep 0 100 65535` | `ERR: steps '65535' -- want 1-1000. Nothing was changed.` | ERR, returns at once |
> | 8 | `cal gain 3100 67`, then `cfg` | `ERR: fraction '67' -- want 0.05-1.0 of full scale (not %). Nothing was changed.` | ERR; `cfg` still shows `u12b gain: 14.5` |
> | 9 | `beam duty 2x` | `ERR: duty '2x' -- want 0-100 %. Nothing was changed.` | ERR; `beam` duty unchanged |
> | 10 | `detect a b c d e f g h` (9 words) | `ERR: more than 8 words -- NOTHING was run.` | ERR |
> | 11 | `capture 0x20 2000 250000` | `# capture mask=0x20 n=2000 rate=250000 overran=0`, 2000 values, `# end` | hex mask still accepted |
> | 12 | beam **off**: `beam freq 5`, then `beam` | `5 Hz requested, 8 Hz actual (TOP=65535, …)`; readback `div 0x0ff0` on both slices | divider 255, not `0x0000` |
> | 13 | `beam freq 104166`, then `beam` | `TOP=1439`, phase **1311 ticks**, `div 0x0010` | restored to the saved carrier |
>
> Then **`BENCH_P6_STROBE.md` 6a** (Step 0 → 6a.3) and **6b** (Steps 1–4). First number that
> matters: **U5's clamp** (`strobe clamptest 200` / `1000`). Send the logs; I analyse them and
> update `STROBE_HW_LIMIT_US_ASSUMED` and the PIO overhead constants if they differ.
>
> **Open in parallel, deferred:** the optical acquisition window (~59.8 ms needed vs 32.768 ms
> at 500 ksps), the §3.7 20-pass set, CR-18 (a real ball impact on the mic). **Before 6c:** the
> 🔴 items of the §9 code-audit backlog.

> **Older resume blocks** — 2026-10-02 (strobe build), 2026-09-28, 2026-08-24, 2026-08-21 and the
> 2026-08-14 firmware handover — are in [`PROGRESS_ARCHIVE.md`](PROGRESS_ARCHIVE.md) §10, verbatim.

---

## 11. Incident 2026-08-17 — foil short across D12, and the latched servo

**Recorded because the board is still in this state, and because the diagnostic that caused
it was a bad instruction rather than a bad execution.**

**Summary:** foil laid over D12 bridged VIR (36 V through R77 10 kΩ) into the TIA summing
node. **U11B was destroyed** — its output sits opposite to what its inputs demand — and
board 1 is out of service until U11 is replaced. The forensic narrative (what happened, the
current budget, the measurements that closed it, why it did not self-recover) is in
[`PROGRESS_ARCHIVE.md`](PROGRESS_ARCHIVE.md) §11, verbatim.

### The part

```
U11 = OPA4323IPWR    TSSOP-14 (PW0014A)    LCSC C22419728
```

**U12 is the same part and is undamaged** — it is the LPF and output amp and worked normally
throughout. Only U11 needs replacing. Three of U11's four sections still test good (U11A
saturates correctly, U11C makes a clean 2.59 V, U11D inverts linearly), but they share one die.

**After replacement, verify:** TP6 and TP7 both **2.59 V** (DMM, beam off), `adc 2 256` ≈ **3212**,
then `hpf test` should still report CONFIRMED / TRACK = 0.

⚠ **CR-15 is unrelated and unchanged.** The TIA saturation above ~3 % duty was measured on a
healthy board *before* this incident, and it is still the actual Phase 3 blocker.


### Lessons worth keeping

1. **Never put conductive material near this board without stating what it must not touch.**
   D12 has 36 V on one leg.
2. **A rail is not a null.** If a test is supposed to *remove* a signal, the pass condition is
   "returns to baseline", not "goes quiet" — a saturated output is also very quiet.
3. Foil over the **photodiode** was chosen over the LED specifically to keep metal away from
   the switching loop. That reasoning was right; the omission was the contact warning.
