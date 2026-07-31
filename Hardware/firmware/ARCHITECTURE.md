# Hardware Offload Architecture

**Design goal: the CPU orchestrates, hardware executes.** Every periodic or
timing-critical signal should be produced or consumed by PWM, PIO, DMA or the ADC
sequencer, with the cores only setting things up and reading results.

This document audits every function against that goal, records what is already
compliant, and flags the places where the current code or the .md pseudocode falls
short. **Two of the findings are real problems, not stylistic preferences** — see
A1 and A2.

---

## Silicon budget

RP2350 gives us far more than this design needs, which means there is no reason to
economise by doing things in software:

| Resource | Total | Committed | Free |
|---|---|---|---|
| **PIO blocks** | **3** (PIO0/1/2), 4 SMs each = 12 SMs | 4 planned | **8 SMs** |
| PWM slices | 12 | 6 | 6 |
| DMA channels | 16 | ~6 planned | 10 |
| ADC | 1 SAR, 500 ksps, round-robin + DMA | shared, see A1 | — |
| Cores | 2 | core 0 slow path, core 1 hot path | — |

**Three PIO blocks is the headline number.** The .md's pseudocode assumes two
(PIO0 for strobe, PIO1 for I²S) and therefore does several things on the CPU that
could simply have their own state machine. We are not short of state machines.

---

## Current allocation

### ✅ Already fully offloaded

| Function | Hardware | CPU cost |
|---|---|---|
| Beam carrier, GPIO31 | PWM slice 3B | zero after setup |
| Demod clock, GPIO39 | PWM slice 7B, phase-locked | zero after setup |
| Strobe current DAC, GPIO28 | PWM slice 2A + 2-pole RC | zero |
| Comparator threshold DAC, GPIO44 | PWM slice 10A + 2-pole RC | zero |
| Panel LED brightness, GPIO11/12 | PWM slices 5B/6A | see A4 |
| ADC block capture | DMA, `DREQ_ADC` | zero during transfer |
| Strobe burst train, GPIO25 | **PIO0 SM0** + DMA-fed schedule | zero during burst |
| I²S mic, GPIO4/5/6 | **PIO1 SM0** + DMA | zero (Phase 5, optional) |

The carrier/demod phase lock is worth calling out as the model for the rest: two
counters preloaded while disabled, then enabled in a **single register write**, so
the relationship is exact and needs no CPU maintenance ever again.

---

## Findings

### 🔴 A1 — The ADC monitor tears down free-running modes 10× per second

**This is a real bug that will surface in Phase 3.**

`adc_read_avg()` calls `adc_quiesce()` — which stops the ADC, drains the FIFO and
clears round-robin — then polls 256 conversions in a **blocking loop**, then
restarts the previous mode. The power FSM's supply monitor calls it every 100 ms.

Two consequences:

1. **~0.5 ms of spinning CPU, 10× per second.** Minor on its own.
2. **It destroys whatever free-running acquisition was in progress.** In Phase 3,
   ADC5 must free-run continuously into a DMA ring so that a comparator edge has
   pre-trigger history to look back at. A monitor that stops and restarts the ADC
   ten times a second will punch holes in that ring and rotate the round-robin
   channel phase.

**Fix:** make IDLE and ARMED modes free-run into a **continuous DMA ring buffer**,
and have the monitor *read from the ring* instead of commanding its own
conversions. `adc_read_5vin_volts()` becomes "average the ch1 samples already in
the ring" — zero ADC disruption, near-zero CPU, and it can never disturb detection.

Keep the stop-and-poll path only for the CLI's one-shot `adc <ch>` command, and
document that it is disruptive.

**Do this before Phase 3**, not during. It is much easier to fix now than to debug
as intermittent missing samples later.

---

### 🟡 A2 — Comparator edge timing should be PIO, not an ISR

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

---

### 🟢 A5 — UART to the Pi must be DMA on both directions

Not yet written, so this is a specification rather than a fix. At 921600 baud with
framed binary packets, **both TX and RX go through DMA**, with RX into a ring and
frame parsing done from the ring on core 0.

Never `uart_putc()` in a loop — that is exactly the kind of blocking that must not
exist on the core also running the power FSM.

---

### 🟢 A6 — Blocking bench commands are fine, and should stay labelled

`beam ramp`, `beam sweep`, `panel demo` and `capture` all block for seconds.

**That is correct for bench tooling** — they are interactive commands where the
operator is watching a scope, and non-blocking versions would add state machines
for no benefit. They are explicitly *not* part of the run-time path.

The rule to hold: **nothing in the armed or firing path may block.** Bench commands
are allowed to; production paths are not.

---

## Target allocation

| Function | Where it goes | Status |
|---|---|---|
| Beam carrier | PWM 3B | ✅ done |
| Demod clock | PWM 7B, phase-locked | ✅ done |
| Gate DAC | PWM 2A | ✅ done |
| Threshold DAC | PWM 10A | ✅ done |
| Panel LEDs | PWM 5B/6A + 50 Hz timer | 🟢 A4 |
| Strobe burst | **PIO0 SM0** + DMA | planned, `.pio` written |
| **Comparator transit timing** | **PIO0 SM1** | 🟡 A2 — new |
| **Camera trigger/strobe handshake** | **PIO0 SM2** | 🟡 A3 — new |
| I²S mic | **PIO1 SM0** + DMA | planned |
| Detect + mic acquisition | ADC round-robin → **continuous DMA ring** | 🔴 A1 |
| Strobe current capture | ADC ch0 → DMA, burst window | planned |
| Pi UART | DMA both directions | 🟢 A5 |
| Power FSM, CLI, calibration, reporting | **CPU — correctly** | ✅ |

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
