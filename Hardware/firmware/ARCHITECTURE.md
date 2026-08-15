# Hardware Offload Architecture

**Design goal: the CPU orchestrates, hardware executes.** Every periodic or
timing-critical signal should be produced or consumed by PWM, PIO, DMA or the ADC
sequencer, with the cores only setting things up and reading results.

This document audits every function against that goal, records what is already
compliant, and flags the places where the current code or the .md pseudocode falls
short. **Three of the findings are real problems, not stylistic preferences** — see
A1, A2 and **A7**. A7 is a hardware collision found on 2026-07-31 and it will surface
in Phase 6b; board-level fix proposed as CR-01 in `NEXT_BOARD_REV.md`.

---

## Silicon budget

RP2350 gives us far more than this design needs, which means there is no reason to
economise by doing things in software:

| Resource | Total | Committed | Free |
|---|---|---|---|
| **PIO blocks** | **3** (PIO0/1/2), 4 SMs each = 12 SMs | 4 planned | **8 SMs** |
| PWM slices | 12 | **5** (5, 6, 7, 10, 11 — and 6 is double-booked, see **A7**) | **7** |
| DMA channels | 16 | 2 in use (capture + ring), ~6 planned | 10 |
| ADC | 1 SAR, 500 ksps, round-robin + DMA | **continuous 32 KB ring** (A1 ✅) | — |
| Cores | 2 | core 0 slow path, core 1 hot path | — |

**Three PIO blocks is the headline number.** The .md's pseudocode assumes two
(PIO0 for strobe, PIO1 for I²S) and therefore does several things on the CPU that
could simply have their own state machine. We are not short of state machines.

---

## Current allocation

### ✅ Already fully offloaded

| Function | Hardware | CPU cost |
|---|---|---|
| Beam carrier, GPIO31 | PWM slice **7B** | zero after setup |
| Demod clock, GPIO39 | PWM slice **11B**, phase-locked | zero after setup |
| Strobe current DAC, GPIO28 | PWM slice **6A** + 2-pole RC | zero |
| Comparator threshold DAC, GPIO44 | PWM slice 10A + 2-pole RC | zero |
| Panel LED brightness, GPIO11/12 | PWM slices 5B/**6A** | see A4, **and A7** |
| ADC block capture | DMA, `DREQ_ADC` | zero during transfer |
| Strobe burst train, GPIO25 | **PIO0 SM0** + DMA-fed schedule | zero during burst |
| I²S mic, GPIO4/5/6 | **PIO1 SM0** + DMA | zero (Phase 5, optional) |

> **Slice numbers corrected 2026-07-31.** This table previously said 3B / 7B / 2A. RP2350B
> has 12 slices and the mapping is not the RP2040 formula — for GPIO ≥ 32 it is
> `8 + ((gpio >> 1) & 3)`. The code was always right (it resolves at runtime via
> `pwm_gpio_to_slice_num()`); the documentation was not. Full map in `board.h`.

The carrier/demod phase lock is worth calling out as the model for the rest: two
counters preloaded while disabled, then enabled in a **single register write**, so
the relationship is exact and needs no CPU maintenance ever again.

> ✅ **Measured on hardware 2026-08-13, and it works exactly as designed.** Across **six**
> full `beam_configure()` teardown-and-reload cycles the carrier↔demod offset moved by
> **1.02 ns = 0.15 ticks** — half a logic-analyser sample at 500 MS/s. Commanded phase
> offsets of 0, 360, 720 and 1439 ticks all reproduced within 0.15 ticks, wrapped cleanly,
> and returned to the reference.
>
> The read-modify-write in `beam_slices_enable()` was confirmed too: with the beam running,
> `PWM_EN` read **0x8e0** — bits 7 and 11 (beam) set *alongside* bits 5 and 6 (panel LEDs),
> which the SDK's `pwm_set_mask_enabled()` would have cleared. **This is the pattern to copy
> for any future multi-peripheral start that must be simultaneous.**

---

## Findings

### ✅ A1 — FIXED 2026-08-14. Continuous DMA ring; nothing stops the ADC.

**Was:** `adc_read_avg()` called `adc_quiesce()` — stopping the ADC, draining the FIFO and
clearing round-robin — then polled 256 conversions in a blocking loop and restarted the
previous mode. The power FSM's supply monitor called it every 100 ms. Harmless while nothing
else used the ADC; it would have punched holes in the Phase 3 pre-trigger history and rotated
the round-robin channel phase ten times a second.

**Now:** IDLE, ARMED and BURST all free-run into a **32 KB DMA ring** that never stops.

| | |
|---|---|
| Ring | `ADC_RING_SAMPLES` = 16384 samples = 32 KB, aligned to its own size |
| DMA | **one** channel, RP2350 **ENDLESS** transfer mode (`TRANS_COUNT MODE = 0xf`) + hardware write-address ring wrap |
| History | **32.8 ms per channel in every mode** — covers a ball transit down to ~1.4 m/s (production `v_min` is 2.0 m/s) |
| CPU cost | **zero** |

**New API:**
- `adc_ring_avg(chan, n, &code)` — average the newest `n` samples of a channel already in the
  ring. Commands no conversions.
- `adc_ring_history(chan, dst, n)` — copy the newest `n` samples out, newest first. **This is
  the Phase 3 pre-trigger read**: after a comparator edge, pull the bump back out and find its
  own 50 % crossings.
- `adc_5vin_age_ms()` — staleness of the last +5V reading; `stat` displays it.

`adc_read_5vin_volts()` now averages ch1 out of the ring and disturbs nothing.

#### Two constraints that are load-bearing, not stylistic

**1. The round-robin channel count must be 1, 2 or 4.** The ring size has to be a whole
multiple of the channel count, or the channel phase rotates on every wrap and every
de-interleaved sample after that is mislabelled. A power-of-two ring can never be a multiple
of 3, so a 3-channel mode would need a self-chaining DMA and software phase tracking. This is
why ARMED stayed `{5,7}` rather than gaining ch1.

**2. Do not "fix" this with an A↔B DMA chain.** That was the first attempt and it is wrong: a
channel's transfer count does not reload itself, so after each channel has run its one lap
both counts sit at zero and the pair stalls silently. ENDLESS mode is the correct mechanism
and is RP2350-only — the RP2040 equivalent needs a control channel rewriting the count.

#### The one gap, accepted deliberately

**ch1 is not in the ARMED set**, so while armed `adc_read_5vin_volts()` **holds its last good
value** rather than stopping the ADC to fetch a fresh one. `adc_5vin_age_ms()` makes that
visible instead of silent, and `stat` prints `HELD` with the age.

This is safe because of the state flow: **`ARMED` → `BURST` (during the shot) → `IDLE`**, and
`IDLE` is `{1,2,5,7}` — so the supply is re-checked automatically after every shot without a
dedicated mode. The residual exposure is a PSU pulled *during* an armed window, which is a
deliberate trade against sampling the detect channel at half rate.

`adc_read_avg()` still exists and still stops the ring — it is now documented as disruptive
and belongs to the CLI's one-shot `adc <ch>` only. `adc_capture()` likewise; it is a bench
instrument and restores IDLE (and therefore the ring) when it finishes.

---

### ✅ A2 — DONE 2026-08-14. Comparator edge timing is a PIO state machine.

`src/detect.pio` + `src/pio_alloc.[ch]`. One SM waits for the rising edge on GPIO46, counts
at 1 MHz, and pushes the width as a single FIFO word on the fall. The count loop is exactly
2 cycles per tick, so the SM is clocked at 2 MHz — 150 MHz / 2 MHz = 75, an exact integer
divider with no fractional-divider jitter on the timebase. Asserted at init rather than
assumed.

#### The PIO block allocation is forced, not chosen

Each RP2350 PIO block has **one** `GPIOBASE` register holding only 0 or 16
(`PIO_GPIOBASE_BITS = 0x10`), giving a 32-pin window of GPIO 0–31 or 16–47. Every pin field
in that block — in/out/set/sideset bases and `jmp pin` — lives inside it.

| Pin | Function | Phase | Compatible base |
|---|---|---|---|
| GPIO 4/5/6 | I²S mic | 5 | **0 only** |
| GPIO 8/9/10 | camera strobe + trigger | 7 | **0 only** |
| GPIO 25 | Strobe_Pulse | 6 | either |
| **GPIO 46** | **D_Comparator** | **3/4** | **16 only** |

So the detector **cannot share a block with the cameras or the mic**. This is not a
performance trade — an allocation that violates it fails to configure, and the SDK returns
`PICO_ERROR_BAD_ALIGNMENT` from `pio_sm_set_config()`.

| Block | GPIOBASE | SM0 | SM1 |
|---|---|---|---|
| PIO0 | 0 | strobe (GPIO25), Ph6 | camera handshake (GPIO8/9/10), Ph7 |
| PIO1 | 0 | I²S mic (GPIO4/5/6), Ph5 | free |
| PIO2 | **16** | **detect (GPIO46)** | free |

Two constraints that are easy to violate silently:

1. **`pio_set_gpio_base()` refuses once a block has instructions loaded** — it checks the
   used-instruction-space mask. Hence one `pio_alloc_init()` from `main()` before every
   other init. Getting this wrong compiles fine and fails at run time.
2. **Pin numbers passed to `sm_config_set_in_pins()` / `set_jmp_pin()` are ABSOLUTE.**
   `pio_sm_set_config()` translates them against the base via the `pinhi` mechanism. Pass
   46, not 46−16. Verified against SDK 2.3.0.

`pio_alloc.c` carries `_Static_assert`s that each committed pin falls inside its block's
window, so a future pin move is a compile error rather than a bench mystery.

#### No glitch filter in the PIO, deliberately

U15 has no hysteresis (`NEXT_BOARD_REV.md` CR-13), so slow edges chatter. Filtering in PIO
would bake a policy into hardware before the chatter had ever been measured, and would hide
exactly what Phase 4 exists to characterise. Every fragment is pushed; `detect_service()`
coalesces and **counts** them, and the count is the measurement.

---

### 🟡 A2 (original analysis, retained)

The .md (§13.6) puts a GPIO IRQ on GPIO46 with a RAM-resident handler, ~1–2 µs.
That works, but a PIO state machine does it strictly better:

| | GPIO IRQ | PIO SM |
|---|---|---|
| Latency | ~1–2 µs | deterministic, sub-µs |
| Jitter | varies with what else is running | **none** |
| CPU cost | an ISR per edge, plus flash-XIP stall risk | zero |
| Measures transit directly | no — two timestamps, subtract in software | **yes, one FIFO word** |

A PIO program that waits for a rising edge, counts at 1 MHz until the falling edge,
and pushes the count gives you the **transit time directly** with no interrupt, no
handler, and no possibility of being delayed by a USB or UART interrupt at the
wrong moment.

Core 1 then blocks on the FIFO and does the arithmetic at its leisure. Absolute
time, if wanted, is one `time_us_64()` read when the word arrives — precision there
does not matter, only the *interval* does.

**Cost:** one state machine out of eight free. **Recommended for Phase 4.**

Keep a GPIO IRQ as well if you want an immediate wake, but take the *timing* from
the PIO word.

---

### 🔴 A9 — The ADC ring is ONE resource with MODE-SCOPED contents

`adc_engine_set_mode()` calls `ring_stop()` then `ring_start()`, which resets the DMA
write address and changes the round-robin stride. **Every sample already in the ring
becomes unreadable at that instant** — not stale, unreadable, because the de-interleave
depends on a stride that just changed.

| Mode | Channels | What is readable |
|---|---|---|
| IDLE | 1, 2, 5, 7 | supply, TIA, detect, mic |
| ARMED | 5, 7 | detect, mic |
| **BURST** | **0 only** | **strobe current — detect and mic history are GONE** |

Three phases want this resource in the same instant, and the conflict is not obvious
from any single one of them:

- **Phase 3/4** pulls the detect bump out of the ring *after* the comparator edge, to
  get an amplitude-independent transit.
- **Phase 5** pulls the shot audio out of the ring for the same reason.
- **Phase 6** needs BURST for the per-pulse strobe current, and entering it destroys
  both of the above.

**The ordering constraint, which belongs in the firing path and not in a comment in one
module:**

```
comparator edge  →  ADC refinement (ch5) AND mic analysis (ch7)  →  THEN BURST
                    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
                    all of this must COMPLETE before the mode changes
```

The Phase 4 design already puts the refinement inside the camera-handshake wait, which
is before the strobe fires — so the natural sequence is correct. The hazard is that
nothing *enforces* it, and the failure is silent: `adc_ring_view()` simply returns false
once ch5 leaves the round-robin, the pass is flagged `DQ_NO_ADC`, and the experiment
quietly loses its ADC column.

**When the firing path is written in Phase 6, `adc_engine_set_mode(ADC_MODE_BURST)` must
be the last thing it does before the burst, not the first.**

---

### 🟡 A3 — The camera handshake should not be a busy-wait

.md §13.6 does:

```c
if (!wait_both(PIN_CAM_STROBE_0, PIN_CAM_STROBE_1, HIGH, 5ms)) { ... }
```

That is a spin loop burning a core for 100–300 µs on every shot, in the most
timing-sensitive window in the whole system.

**Better:** a PIO SM that asserts D_Cam_Trigger, waits for both Cam_Strobe inputs
high, and pushes the elapsed count. That produces `t_cam` as a measured number for
free — the same value the .md wants — with no spinning and no timeout bookkeeping
in software.

Two inputs and one output on one SM is straightforward: `wait 1 pin` twice, or read
both into ISR and mask.

**Cost:** one more state machine. **Recommended for Phase 7b**, where the
delayed-response simulation gives an easy way to verify it.

---

### 🟢 A4 — Panel LED patterns run at superloop rate

`panel_update()` recomputes a pattern and writes two PWM levels on **every**
superloop iteration — thousands of times per second for effects that change at
1–50 Hz.

Harmless today. Worth changing anyway, for one reason: when core 0 gets busy with
the UART protocol and USB CDC in Phase 8, LED timing becomes hostage to loop
latency and the patterns will visibly stutter.

**Fix:** drive `panel_update()` from a `repeating_timer` at 50 Hz. Bounded, off the
superloop, immune to loop jitter. Ten-line change; do it whenever convenient.

> **Partly addressed 2026-07-31.** Automatic patterns now run on `power_state_elapsed_ms()`
> rather than free-running time, so every state starts its waveform at the beginning. That
> fixed a real symptom — `POWERING_ON` was catching an arbitrary slice of its breath cycle
> and could land near zero brightness, looking as though the state never happened. **The
> call rate is still superloop, so the A4 fix itself is still outstanding.**

---

### 🔴 A7 — The ready LED and the strobe current DAC are on the same PWM channel

**Found 2026-07-31. This will surface in Phase 6b and it is not a documentation problem.**

`GPIO12` (READY_LED) and `GPIO28` (GATE_PWM) both map to **slice 6, channel A** — the same
*channel*, not merely the same slice. That distinction is the entire problem:

| | Shares | Result |
|---|---|---|
| Same slice, **different** channel (6A vs 6B) | `TOP` and `DIV` | Common frequency, **independent duty** — each channel has its own compare register. Perfectly usable. |
| Same slice, **same** channel (6A and 6A) | `TOP`, `DIV` **and the compare register** | **Identical waveform on both pins.** One output, routed by the GPIO mux to two places. |

So this is not "same frequency, different duty," and letting whichever function needs a
specific frequency win does not help — **the duty is shared too**, and the duty *is* the
current setpoint.

Any two GPIOs **16 apart** collide this way: `slice = (gpio>>1)&7` wraps while the channel
bit `gpio&1` is unchanged. All three pairs on this board (12/28, 15/31, 11/27) are exactly
16 apart. **Design rule for the next spin: never put two PWM functions on GPIOs 16 apart.**
Slice 6 channel B (GPIO13/GPIO29) is unassigned — had the ready LED been routed to GPIO13,
it and the gate DAC would have coexisted. This is a layout accident, not a firmware one.

`panel.c` configures 6A today (wrap 999, div 150, for ~1 kHz). Phase 6b needs that same
channel for the strobe current setpoint DAC. **Whichever is configured second silently takes
over both**, so the ready-LED brightness becomes the 9 A current setpoint, or the current
setpoint becomes the LED brightness. Neither failure announces itself.

**Fix: the ready LED gives up the PWM block.** Both GPIO numbers are fixed by the PCB, so
the slice collision cannot be routed around — one function has to yield, and a status
indicator is obviously it. Options, cheapest first:

1. **Plain on/off** via SIO. D7 is a single ready indicator; brightness control is a luxury.
2. **Software PWM from the 50 Hz timer** proposed in A4, if dimming is wanted.

Do it in **6b**, before the gate DAC is first configured — not after, because the symptom
(LED brightness moving the strobe setpoint) is exactly the kind of thing that reads as an
analog fault.

**Two more pairs collide but are currently safe**, and both are on safety-critical pins:

| Pair | Slice | Why it is safe | What breaks it |
|---|---|---|---|
| GPIO15 LATCH_CONTROL / GPIO31 MOD_PWM | 7B | GPIO15 stays SIO | Putting GPIO15 on PWM would switch the **+5 V rail — the Pi's power** — at the beam carrier frequency |
| GPIO11 PWR_BTN_LED / GPIO27 PULSE_LIMIT_DIS | 5B | GPIO27 stays SIO | Putting GPIO27 on PWM would toggle the **strobe watchdog defeat line** at the panel LED's ~1 kHz |

Neither is a bug today. Both are landmines for anyone who adds a PWM without checking the
map in `board.h`.

**Board fix proposed:** `NEXT_BOARD_REV.md` **CR-01** — move READY_LED to GPIO13 (slice 6B,
unconnected today). Same slice as the gate DAC so a shared frequency, but a *different
channel*, hence its own compare register and independent duty. One trace.

---

### 🟢 A8 — Status commands should read hardware, not firmware's opinion of it

**Added 2026-08-13 after it cost a bench session.**

`beam` originally reported `s_on`, `s_top`, `s_duty`, `s_phase` — all **software** state. When
the beam appeared dead on the logic analyser, that output said "ON, 104166 Hz, 2 % duty" and
proved nothing: it could not distinguish *firmware is not driving the pin* from *the probe is
wrong*. (It was the probe — a missing ground.)

`beam` now also dumps the silicon: `PWM_EN`, both slices' `CSR`/`TOP`/`CC`/`DIV`, the GPIO
`funcsel`, the **pad ISO bit** (RP2350-specific — pads reset isolated, and a set ISO bit means
no output regardless of what the peripheral is doing), and the **live counter sampled three
times** so a frozen slice is obvious at a glance.

**The general rule for anything driving hardware we then measure externally:** the status
command should read back registers, not mirror the variables that were written. Everything
offloaded to PWM/PIO/DMA has this property — the CPU sets it up and then has no idea whether
it is still working. Worth applying to the strobe burst engine (Phase 6) and the camera
handshake (Phase 7), where a silently-stopped state machine would otherwise look identical to
a wiring fault.

---

### 🟢 A5 — UART to the Pi must be DMA on both directions

Not yet written, so this is a specification rather than a fix. At 921600 baud with
framed binary packets, **both TX and RX go through DMA**, with RX into a ring and
frame parsing done from the ring on core 0.

Never `uart_putc()` in a loop — that is exactly the kind of blocking that must not
exist on the core also running the power FSM.

---

### 🟢 A6 — Blocking bench commands are fine, and should stay labelled

`beam ramp`, `beam sweep`, `panel demo` and `capture` all block for seconds. So does the
hardware-readback block in `beam`, which busy-waits ~4 µs to sample the PWM counter twice.

**That is correct for bench tooling** — they are interactive commands where the
operator is watching a scope, and non-blocking versions would add state machines
for no benefit. They are explicitly *not* part of the run-time path.

The rule to hold: **nothing in the armed or firing path may block.** Bench commands
are allowed to; production paths are not.

---

## Target allocation

| Function | Where it goes | Status |
|---|---|---|
| Beam carrier | PWM **7B** | ✅ done — **validated on hardware**, see above |
| Demod clock | PWM **11B**, phase-locked | ✅ done — **0.15 ticks across 6 reconfigurations** |
| Gate DAC | PWM **6A** | planned — ⚠ **collides with the ready LED, CR-01/A7** |
| Threshold DAC | PWM 10A | ✅ done |
| Panel LEDs | PWR 5B + 50 Hz timer; **RDY off PWM entirely** | 🟢 A4, 🔴 **A7** |
| Strobe burst | **PIO0 SM0** (base 0) + DMA | planned, `.pio` written |
| **Comparator transit timing** | **PIO2 SM0 (base 16)** | ✅ **A2 done** — *not* PIO0; see below |
| **Camera trigger/strobe handshake** | **PIO0 SM1** (base 0) | 🟡 A3 |
| I²S mic | **PIO1 SM0** (base 0) + DMA | planned |
| Detect + mic acquisition | ADC round-robin → **continuous DMA ring** | ✅ **A1 done** |
| Strobe current capture | ADC ch0 → DMA, burst window | planned |
| Pi UART | DMA both directions | 🟢 A5 |
| Power FSM, CLI, calibration, reporting | **CPU — correctly** | ✅ |

⚠ **This table previously said comparator timing goes on PIO0 SM1. That is not
implementable.** PIO0 must be `GPIOBASE = 0` to reach the strobe (GPIO25) and camera
(GPIO8/9/10) pins, and GPIO46 is only reachable at base 16. One block, one base. The
detector therefore gets its own block — see A2.

After A1–A3, the entire hot path is hardware: a ball transit produces a PIO FIFO
word, the camera handshake produces another, the burst plays out of DMA, and the
current waveform lands in a buffer. **Core 1 does arithmetic between events and
nothing during them.**

---

## What genuinely belongs on the CPU

Offloading is not free, and these are correctly software:

- **Power FSM** — millisecond timescales, complex branching, no timing pressure.
- **Burst schedule computation** — a few hundred µs of float math in a window the
  camera handshake donates anyway.
- **Calibration routines** — sweeps, statistics, argmax. Inherently sequential.
- **CLI, UART protocol, config** — slow path by definition.
- **Fault policy** — needs judgement, not determinism.

The test to apply: *does this have to happen at a specific time, or merely soon?*
Specific time → hardware. Soon → CPU.
