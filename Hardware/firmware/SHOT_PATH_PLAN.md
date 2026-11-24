# The detected-shot path: plan

Written 2026-11-20. **A plan only: no firmware has changed.** The owner decided D1–D8 the same
day (§3a).

- **Source:** `Github-Personal\Faceplate\Faceplate_Design\FIRMWARE_COMPATIBILITY_HANDOFF.md`
  (2026-11-20), the workbench side's view of what the firmware needs.
- **Checked against:** image 1.12's source, and the controller the stand was scored with,
  `Faceplate_Design\designs\weighted020\CONTROLLER.json`.
- **Background:** the stand's geometry and timing models are in
  [`../Stand/README.md`](../Stand/README.md) §4.

---

## 1. Where things stand

Phases 3–4 (detection), 6 (strobe) and 7 (cameras) each work on their own. `shot.c` chains
them only for a test shot started from the CLI (`cam shot`):

- A **detected** pass ends `SHOT_END_NOT_IMPLEMENTED` after `SHOT_ANALYSING`.
- Nothing calls `shot_arm()`.
- The firmware computes no ball speed.
- Camera-gated bursts are **dry only** (`STROBE_REFUSE_GATED_LIVE`).

This plan is the work that makes a ball crossing the sheet produce a timed, current-checked,
logged burst that matches what the stand was optimized for.

---

## 2. The handoff, checked claim by claim

| # | The handoff says | Verdict | What the source and the controller file show |
|---|---|---|---|
| 1 | `detect_service()` waits 2 ms (`DETECT_COALESCE_US`) after the fall: a latency bug | **Real, but not a bug today** | It is the bench-logging coalescer, retunable from the CLI. The shot path is not written, so nothing is late yet. The shot path must not wait on it |
| 1a | Anchor on `time_us_64()` at the confirmed fall; FIRE to ±1 µs | **Not achievable as written** | `s_frag_last_end_us` is the time the superloop **read** the FIFO word; `detect.c` says so ("`now` dates the read, not the edge"). Core 0's superloop latency is unbounded: USB CLI, pi_link, power FSM, and yields inside blocking commands. A precise anchor needs either a bounded hot loop (ARCHITECTURE: core 1 "planned for the hot path") or a hardware-timed edge |
| 2 | `strobe_burst.pio` already takes per-pulse (width, gap) pairs; the C side is uniform | **Correct** | `width_0, gap_0, … , 0`; N + 2 µs high, M + 8 µs low. `strobe_burst_t` and `strobe_encode()` are uniform |
| 3 | `STROBE_REFUSE_BEAM`, `_DETECT` and `_GATED_LIVE` block live firing | **Correct** | They exist as named |
| 3a | Switch to BURST, and `STROBE_REFUSE_ADC` is "naturally satisfied"; admit when `adc == ADC_MODE_BURST` | **Wrong** | `STROBE_REFUSE_ADC` requires **IDLE**: the live path switches to BURST itself. It then waits for 200 µs of ADC0 baseline (`STROBE_LIVE_PRE_US`). On board 1 that baseline was written 198–200 µs after the switch, on every live firing (6c, 2026-10-12 and 13). **So with the reviewed live readback, the first flash comes about 0.2 ms after the switch, not 25–50 µs after the trailing edge.** (First written here as about 0.4 ms, from a half-rate ADC reading: PROGRESS §6, 2026-11-20) |
| 3b | Quench the beam with a new `beam_quench_for_shot()` | **Exists already** | `beam_enable(false)` drives GPIO31 low through SIO at once, without blocking, and clears the flag `STROBE_REFUSE_BEAM` checks. It also stops the demodulator, so re-arming needs a settle. It never touches GPIO15 |
| 4 | `72.67 mm` is the wrong length for the ADC5 50 % width; the right one is about 42–50 mm | **Right that 72.67 mm is wrong; the number is off** | 72.67 mm (ball + 30 mm sheet) is the full contact length. A backscatter model (the ball's chord inside the sheet) gives a **50 %-of-peak width of 36.2 mm** for a 30 mm top-hat sheet, and **41.1 mm** for a Gaussian of 30 mm FWHM (§6). Either way, calibrate it |
| 5 | Slow shots of 4–9.5 m/s span 38–71 ms, past the 30 ms readback window | **Only bin 4** | Only the 4–5 m/s bin (7 pulses) exceeds 30 ms: 71.1 ms at 4 m/s. Every other bin fits. D3 raises the span limit to 150 ms and watches the first 30 ms (§3a) |
| 5a | Option A: sample ADC0 every 6–8 µs and allow 90 ms bursts | **Not without an owner decision and independent review** | The live overcurrent stop rules are explicitly sampled every 2 µs (`board.h`). An 11 µs pulse would get one or two samples. It also raises a live-strobe limit |
| 6 | The LUT is 89 contiguous bins, 4–92 m/s, identical in form for both hands | **Wrong** | **67 bins**: 4 and 7–89, with gaps at 5–6, 33, 39–40, 48, 54–57, 65–71, 80 and 84. In the workbench's own model a missing bin goes to the fallback (`MISSING_STORED_LUT_BIN`). RH and LH differ by up to 9.8 µs |
| 6a | Every bin passes the firmware's limits | **Mostly** | Charge ≤ 3.77 mC (limit 6.0), widths ≤ 100 µs, gaps ≥ 508 µs (≥ 150). But ten widths are 4.08–4.99 µs, under the 5 µs floor. **Bin 23 has no active pulse.** Bins 9, 12, 14, 15, 18, 24, 30 and 35 drop the two sharp spin pulses |
| 6b | A fixed-period fallback | **Not the workbench's fallback** | weighted020 uses `flat15` × 10. The 2026-09-06 controller handoff describes it as 15 µs pulses with dark gaps doubling from 0.5 ms. That is **255.65 ms for 10 pulses**; 6 pulses take 15.59 ms. Only six or fewer fit 30 ms. Confirm against the workbench's `pattern_commands()` before encoding it. **Superseded by D4 (§3a): the live fallback is upstream PiTrac's driver pattern** |
| 7 | Use the external I²S mic on PIO1 | **Wrong block** | PIO1 is the camera handshake. ARCHITECTURE plans the I²S mic on PIO0 SM1 |
| 8 | The safety-invariants table | **Consistent** | It matches `board.h` and `HANDOFF.md`: GPIO27, A7, slices 7B and 10A, the HPF polarity, A9, the Pi-present watchdog policy, CR-09 |

**The workbench model the firmware must reproduce** (`clock_model.program()`):

- Pulse k's rising edge comes at trailing edge + readiness + delay + k · 47.331 mm / v.
- The trailing edge is the ball's geometric full exit from a 30 mm slab, with its centre
  36.3 mm past the sheet centre.
- Speed is 72.67 mm divided by the geometric contact time.
- Disabled slots keep their time positions.
- A speed outside the LUT's declared domain, or a missing bin, uses the fallback.

---

## 2a. The owner's direction (2026-11-20): the first flash as soon as reasonable, at any speed

The speed is not needed until after the first flash.

**The workbench already models it this way.** Pulse 0 comes at trailing edge + readiness,
whatever the speed. The speed sets only pulses 1–9: their times (k · 47.331 mm / v) and the
bin's widths.

**What it changes:**

- **Speed is off the critical path.**
  - Snapshot the ch5/ch7 window at the trailing edge, then switch to BURST at once.
  - Refine the speed on the copy while pulse 0 goes out.
  - Pulse 1 is due 526 µs after pulse 0 at 90 m/s, and later at any slower speed. That is
    ample time for `refine()`.
- **Pulse 0's width cannot come from the bin.** The LUT's slot 0 runs from 4.1 to 14.0 µs,
  and from 9.6 to 14.0 µs at 60 m/s and above.
  - A fixed width of about 10 µs blurs a 90 m/s ball by 0.9 mm.
  - Alternatively, take a coarse speed from the comparator's transit word. The PIO delivers
    it at the falling edge, with no analysis.
  - **Decided (D8):** fixed at the width that gives a 100 m/s shot a sharp image, about
    9 µs.
- **Every bin must start with pulse 0.** Slot 0 is off in bins 9, 12 (RH only), 14, 15, 18,
  23, 24, 30 and 35. The workbench has to regenerate the LUT with slot 0 always on at the fixed
  width, then re-score.
- **The burst is fed in two stages.**
  - Pulse 0 is loaded when the shot arms.
  - The gap after it, and pulses 1–9, are pushed once the speed is known.
  - The PIO waits LOW in `pull` meanwhile. The first gap word must subtract the time already
    spent waiting, so pulse 1 lands at its scheduled time.
  - This changes the reviewed strobe-arm path: independent review.
- **What is left on the critical path:**
  - the time for the CPU to see the trailing edge (D1);
  - the beam quench and the camera trigger, both under 1 µs once the CPU acts;
  - **the live current readback's start-up**: ADC0 has to write 200 µs of baseline after the
    switch to BURST (198–200 µs on board 1). D2 now chooses how to treat that.

---

## 3. Decisions the owner must make

Each changes what gets built. **All eight were decided on 2026-11-20: §3a.** The table keeps
the options as they were offered.

| | Decision | Options | Recommendation |
|---|---|---|---|
| D1 | Where the armed hot path runs | (a) Core 0, with a bounded armed loop: no CLI output and no blocking commands while armed. (b) Core 1, as ARCHITECTURE planned. (c) A hardware-timed edge | Measure first (WP0). Then (a) if the loop's worst case is a few µs, else (b) |
| D2 | How to meet "as soon as reasonable" (§2a), given the live readback's start-up | (A) Switch to BURST at the trailing edge, after the ch5 snapshot. The ring is not slow to start: the baseline is ready 198–200 µs after the switch. Shortening PRE changes reviewed code, and the record must keep its 50-sample baseline minimum (`STROBE_LIVE_PRE_MIN_SAMPLES`; the 200 µs wait gives 76–77). The first flash comes about 0.2 ms after the switch, plus the detection latency, and the ADC5 speed is kept. (B) Switch to BURST at the **leading** edge, so the baseline is ready by the trailing edge (the pass takes at least ~0.6 ms at 90 m/s). The first flash comes tens of µs after the edge, but the schedule uses the comparator's speed: amplitude-biased by about ±10 %, which can close the 47.3 mm spacing to one ball diameter (42.7 mm). The ADC5 shape check and the mic are also lost during the pass. (C) A new mode sampling ch0 and ch5 together at 250 ksps each, running while armed. This keeps ADC5 and gives the earliest flash, but samples current every 4 µs, against the reviewed 2 µs rules: independent review | **(A).** The baseline time is already measured (WP0.2). With the design's 32 µs of detection on top, four whole-ball freezes fit CAM0's view at 100 m/s (7c.3). That holds while detection and the CPU take under 90 µs at 47.33 mm spacing, or 230 µs at 42.67 mm. Keep (B) and (C) in reserve |
| D3 | Bin 4 (4–5 m/s) | (a) Shed pulses to fit 30 ms: 3 pulses at 4 m/s, 4 at 5 m/s. (b) The fallback below 7 m/s | (a). Keep the 30 ms limit |
| D4 | The live fallback | (a) `flat15` × 6 (15.59 ms). (b) Another pattern within 30 ms. Re-score either in the workbench | (a) |
| D5 | Odd LUT bins (23, and those without the sharp pulses) | Ask the workbench side whether they are intended. The firmware refuses any bin that breaks its rules and uses the fallback | Refuse to fallback, and report |
| D6 | The microphone | The analog mic (ch7) cannot run during BURST, and at fast speeds the impact sound arrives after FIRE (Stand README §4.2). So it cannot gate the burst | Post-shot use only, then the I²S mic on PIO0 SM1 later |
| D7 | How the LUT reaches the board | (a) A compile-time table generated from `CONTROLLER.json` by a tool. (b) An upload over pi_link (frames ≤ 255 B, chunked like `UPD_DATA`) into flash config | (a) first: reviewable and host-tested. (b) later |
| D8 | Pulse 0's width (§2a) | (a) Fixed at about 10 µs. (b) From the comparator's coarse speed | (a), with the workbench's LUT regenerated so slot 0 is always on at that width |

### 3a. Decided by the owner, 2026-11-20

| | Decision | What it means for the work |
|---|---|---|
| D1 | **Measure, then decide.** WP0.1 measures core 0's worst-case loop period while armed. Core 0 if it is under about 20 µs with CLI output and blocking commands held off; otherwise core 1 | The budget from 7c.3: detection and the CPU must take under 90 µs for four freezes at 100 m/s with 47.33 mm spacing |
| D2 | **(A): switch to BURST at the trailing edge**, after the ch5/ch7 snapshot | The first flash comes about 0.2 ms after the edge, plus detection. The ADC5 speed and the 2 µs current sampling are kept |
| D3 | **Watch the first 30 ms; later pulses fire unwatched.** The live span limit rises from 30 ms to 150 ms, which covers upstream's 145 ms putter pattern | **A change to a reviewed live-strobe limit, by the owner's decision: independent review in WP5.** The readback, its 2 µs sampling and its stop rules apply to the first 30 ms. The 6 mC per burst, the booking and the pacing count every pulse. A bad reading still ends live mode. The hardware still caps each pulse at 137 µs. No bin needs shedding: bin 4 spans 71.1 ms |
| D4 | **The live fallback is upstream PiTrac's driver pattern:** `kStrobePulseVectorDriver = [0.7, 1.8, 3.0, 2.2, 3.0, 7.1, 4.0, 0]` ms (PiTracLM/PiTrac `main`, `Software/web-server/configurations.json`) | Each entry is a pulse, then that wait, start to start (`pulse_strobe.cpp`, `BuildPulseTrain`). So 8 pulses at 0, 0.7, 2.5, 5.5, 7.7, 10.7, 17.8 and 21.8 ms: 21.8 ms in all. Upstream's width is 1 bit at 115200 baud, about 8.7 µs; here it is D8's width. Upstream's Pi software already decodes this spacing. The putter pattern, `[5.0, 30.0, 30.0, 30.0, 50.0, 0]` (145 ms), is allowed by D3. When to use it is open: upstream selects it by club, on the Pi |
| D5 | **The workbench regenerates the table under the firmware's rules** | Widths 5–100 µs; gaps ≥ 150 µs; ≤ 6 mC; span ≤ 150 ms (D3); slot 0 always on at D8's width; extended to 100 m/s (it stops at 90 m/s, so 90–100 m/s goes to the fallback today). Then re-score. The firmware still refuses any bin that breaks a rule, uses the fallback and reports it |
| D6 | **The analog mic is for after the shot only.** The I²S mic on PIO0 SM1 later | Under D2 (A) the ch7 record ends at the trailing edge, so it holds the impact sound only below roughly 75–90 m/s |
| D7 | **The table is compiled in first.** Upload over pi_link later | `tools/lut_from_controller.py` (WP1.3). The detected shot therefore uses the table's spacing, 47.331 mm. `STROBE_FREEZE_SPACING_UM` (42.67 mm) stays for `cam shot` test bursts |
| D8 | **Pulse 0's width is fixed at what gives a 100 m/s shot a sharp image** | The workbench's sharp rule for a driver is a spin blur of 3.5 px. At CAM0's ~3.5 px/mm, motion alone allows 10 µs. The workbench's own 89 m/s bin uses 10.1 µs, about 9 µs scaled to 100 m/s. The workbench computes the exact value when it regenerates the table (D5). Estimate: 9 µs |

---

## 4. Work packages, in order

Every image follows HANDOFF.md: "reflash first" with stamp, size and SHA256; host tests; and an
independent review wherever strobe, beam, power or detection behaviour changes.

### WP0 — Measure, and change no behaviour

1. **Superloop latency:**
   - add a counter for the longest and typical superloop period, printed by `stat`, and
     whether a blocking command was running;
   - measure it idle, with pi_link traffic, and during CLI output;
   - it decides D1: core 0 if its armed worst case is under about 20 µs.
2. **Live BURST start-up: done from the records.** Every live firing prints
   `ring: first sample +N us, 100 ready +M us`. On board 1 (6c, 2026-10-12 and 13): the first
   sample at +2 to +5 µs (once +38 µs, a late poll), and `100 ready` at **+198 to +200 µs**.
3. **Comparator chatter:**
   - from the Phase 3/4 pass logs (`fragments`, `DQ_CHATTER`), find the longest gap inside
     one ball's pass;
   - that sets the de-chatter window. The handoff's 12–15 µs is a guess.

### WP1 — Host-only plumbing, not wired to anything that fires

1. **`strobe_profile_t`:** per-pulse widths, gaps and a count. Add an encoder alongside
   `strobe_encode()`; the uniform burst becomes the special case.
   - Round each rising edge's absolute time to the µs, not each gap, so error does not
     accumulate.
   - Skipped slots keep their time positions.
   - Never encode a width of 0, which is the PIO's end sentinel.
2. **Profile validator:** width 5–100 µs, gap ≥ 150 µs, charge ≤ 6.0 mC at the clamp-limited
   width, span ≤ 150 ms (D3; `STROBE_LIVE_SPAN_MAX_US` stays 30 ms until WP5's review). The
   other live rules are unchanged.
3. **LUT:**
   - `tools/lut_from_controller.py` reads `CONTROLLER.json` and writes a C table: per hand,
     per bin, 10 widths in µs, plus spacing, readiness, delay and fallback, with a source
     SHA-256;
   - it rejects any bin the validator would reject, and lists them;
   - in firmware, lookup is bin = ⌊v⌋, missing to the fallback, as the workbench does.
4. **Schedule:** pulse 0 at a fixed latency from the reference event (§2a); then speed → bin →
   pulses 1–9, pushed as a second stage, with the PIO's wait subtracted from the first gap.
   - First delay = (36.3 mm − x_ref) / v + readiness + delay + the first active slot's offset.
     x_ref is where the ball centre is at the firmware's reference event (WP4).
   - No shedding: under D3 every bin fits 150 ms. Bin 4 spans 71.1 ms.
5. **Host tests:**
   - all 134 bins, through the encoder against the PIO cycle maths;
   - skipped-slot bins and the fallback;
   - that the first-delay arithmetic holds for any analysis time;
   - the A9 ordering, as `shot_timing` cases in the existing harness.

### WP2 — A fast trailing-edge reference for the shot path

1. **Keep the coalescer** for bench logging (`detect`, `detect stats`).
2. **Add a shot-mode reference event:** a comparator fall after a transit of at least
   `DETECT_MIN_TRANSIT_US`, followed by a quiet window sized from WP0.3.
3. **Anchor it** within the bound D1 sets:
   - (a) a bounded poll; or
   - (b) the PIO pushing the LOW interval or raising an IRQ on the fall, so the edge is
     measured rather than inferred. `detect.h` already describes this option.
4. **Snapshot the ch5 (and ch7) window** from the ring at the reference event. Run `refine()`
   on the copy, so the switch to BURST can start at once and the analysis overlaps the PRE fill
   and pulse 0. Under D2 (B) the switch happens at the leading edge instead, and there is no
   ch5 to copy.

### WP3 — A dry detected shot on board 1: beam, detect, cameras and a dry burst; no current

1. **`shot_arm()` gets a CLI command.**
2. **The sequence:**
   - ARMED → TRIGGERED: `beam_enable(false)`, `detect_arm(false)`, the ch5/ch7 snapshot;
   - ANALYSING: speed, bin, profile;
   - CAM_WAIT: `camera_start` with the computed first delay, and a **dry** gated multi-width
     burst;
   - LOGGING: the full record;
   - back to IDLE. Re-arming waits for the demodulator and HPF to settle after the beam
     comes back.
3. **Bench section to write:** "BENCH_P9" or a new 7d, with a logic analyser on GPIO46, 31, 10,
   8/9 and 25.
   - Pass criteria: the quench time, the trigger time, the multi-width train's widths and
     edges against the plan, and the first-edge time against the computed schedule.
   - Ball drops or ramp passes.

### WP4 — Calibrate the speed and the reference position

1. **The Phase 4 experiment:**
   - two or more known speeds with an independent reference (photogates, or calibrated video);
   - fit `l_eff_fwhm` for the ADC5 50 % width (modelled 36–41 mm);
   - fit x_ref, where the ball centre is at the reference event.
2. **Feed the measured readiness, fallback and spacing back into the workbench** and re-score
   weighted020 (D2, D4).
   - The September controller study's declared latencies were 0, 25, 50, 100, 250, 500 and
     1000 µs.

### WP5 — The live detected shot

**Safety-critical: independent review is mandatory.**

1. **A non-blocking live readback:** `strobe_run()`'s live sequence (ring-ready wait, enable,
   freeze, coverage proof, analysis) as states the shot path can step. `strobe_run()` yields,
   and the firing path may not.
   - **Under D3** it watches the first 30 ms of the burst. Later pulses are booked and
     counted, not watched.
   - `STROBE_LIVE_SPAN_MAX_US` rises to 150 ms only here, under the independent review.
   - The camera's exposure must span the burst, up to 150 ms, as upstream's does for
     putting.
2. **Replace `STROBE_REFUSE_GATED_LIVE`** with an admission for detected shots only.
   - It keeps every existing live check: the rail, faults, the pulse limit, A7, live mode and
     its gate, the session cap, the staircase, the interval and budget, the Pi-present policy,
     beam off and detect disarmed.
   - It adds a proved ARMED → BURST transition.
   - Owner decision first.
3. **The bench:**
   - board 1, J3 on, following the 6c staircase discipline;
   - per-pulse current checked at 2 µs sampling;
   - GPIO27 low throughout.

### WP6 — The shot report to the Pi

1. **A pi_link shot-report message:** under 255 B for 10 pulses.
   - Fields: sequence, hand, ADC transit, peak, the saturated flag, v, bin, fallback reason,
     `t_cam`, each pulse's scheduled rising edge and width, each pulse's measured peak current.
2. **A pulse on `PIN_IRQ_OUT` (GPIO3)**, and `pitrac-link` support on the Pi.
3. **The Pi must already have both cameras streaming in slave mode** before the board arms
   (the 7c tools).

---

## 5. What does not change

- **The live limits:**
  - 6.0 mC per burst;
  - 100 µs software maximum width;
  - 150 µs minimum gap;
  - the readback's **30 ms watched window**. The span limit itself rises to 150 ms under D3,
    by the owner's decision, in WP5 under independent review;
  - **2 µs readback sampling**;
  - the staircase, the pacing (100 ms, 30 mC per 10 s), the stop currents and the session
    cap.
- **The hardware rules:**
  - GPIO27 stays 0;
  - GPIO12 and GPIO15 stay SIO;
  - the A9 ordering;
  - the Pi-present watchdog policy.
- **Never raise a live-strobe limit to make a LUT bin fit.** D3 is the owner's decision, made
  for slow shots and the putter pattern, and it goes through review like any other change.

---

## 6. The numbers behind §2

**The weighted020 LUT** (`CONTROLLER.json`; RH shown; LH has the same bins):

| Bin, m/s | Active pulses | Widths, µs | Span at the bin's floor | Charge |
|---|---:|---|---:|---:|
| 4 | 7 | 10.9, 11.6, 53.0, 63.0, 78.2, 100, 100 | **71.1 ms** | 3.75 mC |
| 9 | 4 | 0, 0, 30.6, 38.4, 51.4, 100 | 15.9 ms | 1.98 mC |
| 10 | 6 | 13.1, 14.2, 47.6, 57.4, 74.0, 100 | 23.8 ms | 2.76 mC |
| 20 | 6 | 7.6, 7.4, 31.8, 40.1, 53.1, 55.2 | 11.9 ms | 1.76 mC |
| 75 | 6 | 11.4, 11.7, 7.3, 8.8, 10.8, 13.7 | 3.2 ms | 0.57 mC |

**The bump width.** Backscatter is modelled as proportional to the ball's projected chord inside
the sheet. This is the ball-sized bump the ADC5 50 % measurement sees:

| Sheet | Width at 50 % | at 20 % | at 5 % | Support |
|---|---:|---:|---:|---:|
| Top-hat, 30 mm | 36.2 mm | 53.9 mm | 65.5 mm | 72.6 mm |
| Gaussian, 30 mm FWHM | 41.1 mm | 60.9 mm | 80.9 mm | — |
| Top-hat, 3 mm | 36.8 mm | 41.5 mm | 44.0 mm | 45.6 mm |

- **The 50 % width barely depends on the sheet**, which is why ADC5 is the speed path.
- **The comparator's width depends on where its threshold sits**, which is why it is not.
- **The trailing reference moves with it:**
  - the ADC5 falling 50 % crossing comes about 18–21 mm past the sheet centre;
  - a comparator fall at 5–20 % of peak comes about 27–40 mm past it;
  - the workbench's trailing edge is at 36.3 mm.
