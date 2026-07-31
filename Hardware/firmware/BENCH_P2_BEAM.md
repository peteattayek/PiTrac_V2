# Phase 2 — Beam carrier and demodulator clock

**Prereq:** Phases 1 and 1b passed.
**Power:** PSU 5.2 V, **current limit ≥ 2.5 A**. The beam alone averages ~0.95 A from
+5 V at 30 % duty.
**Pi:** not connected.
**Gear:** logic analyzer (2a), scope (2b), FLIR (2b), DMM.

Firmware for this phase is written and in the build. The beam cannot start at boot —
`beam_init()` configures the PWM slices but leaves GPIO31/39 as SIO outputs driven low,
so nothing happens until you type `beam on`.

---

## What you are actually bringing up

Two signals that must be *exactly* the same frequency with a controllable phase between
them, driving an optical chain whose whole noise immunity depends on that relationship:

```
GPIO31 Modulation_PWM  --> U9 74LVC1G123 --> U10 MCP1416 --> Q11 --> D11 (beam LED)
GPIO39 Demodulation_PWM --> U13 TMUX1219 --> U12A          (sign-switching demodulator)
```

**The MCU does not drive the LED.** U9 is a monostable wired A=GND, B=~CLR=Modulation_PWM.
A rising edge triggers it; a falling edge clears it immediately. So the LED follows the
input exactly for pulses shorter than the one-shot period, and is **hard clamped** above it.
There is no disable path on this watchdog, by design — a stuck-high Modulation_PWM gives
one clamped flash, not a cooked LED, and no DC beam mode can exist.

Two open questions get answered here, and both change downstream code:

| | |
|---|---|
| **Q1** | How wide is the one-shot clamp *really*? The .md says 113 µs; 0.7·R68·C57 = 0.7·56k·2.2n says **~86 µs**. This sets `STROBE_SW_MAX_US` for Phase 6, where the current software limit (100 µs) may be *above* the hardware limit. |
| **Q2** | Can the '123 recover fast enough to reproduce 30 % duty at 104 kHz? Its timing node must reset via ~CLR inside each 6.7 µs low phase. If it cannot, the carrier design shifts. |

---

## 2a — Phase lock, on the logic analyzer, **with the LED dark**

Do this first. It is the trickiest code in the project and it costs nothing to prove
before any current flows.

**Setup:** rails **down** (`off`, or never latch). LA on GPIO31 and GPIO39. Because
`beam on` refuses while the rail is open, you have two options:

- **Preferred:** probe the RP2354 pins directly (GPIO31 = pin 39, GPIO39 = pin 48) or the
  U9/U13 input side, latch the rail with `on`, and start at **2 % duty** — a 0.6 µs high
  phase at 104 kHz is nothing thermally.
- Or run the whole test at **1 kHz**, where U9 clamps everything anyway.

```
on                       # rail up
beam freq 104167
beam duty 2
beam on
beam                     # confirm: TOP=1439, level 28, actual 104166 Hz
```

### Checks

| # | What | Pass |
|---|---|---|
| 1 | Both signals present, same period | 9.6 µs at 104.167 kHz |
| 2 | GPIO39 is a clean 50 % square | level = (TOP+1)/2 = 720 |
| 3 | `beam phase 0` → measure the GPIO31↔GPIO39 edge offset | reference point, record it |
| 4 | `beam phase 360` → offset moves by 360 ticks = 2.4 µs | **monotonic and exact** |
| 5 | `beam phase 720` (180°) | offset = 4.8 µs |
| 6 | `beam phase 1439`, then `beam phase 0` | wraps cleanly, returns to the reference |
| 7 | Re-run `beam freq 104167` several times | **the phase relationship must be identical every time** |

**Check 7 is the important one.** It proves the atomic-enable trick works. If the offset
varies run-to-run, the two slices are not starting on the same clock edge and every
Phase 3 phase calibration will be built on sand.

> **Implementation note worth knowing:** the SDK's `pwm_set_mask_enabled()` assigns PWM_EN
> wholesale, which would switch off slices 5 and 6 — the panel LEDs. `beam.c` does a
> read-modify-write of just bits 3 and 7 instead, still in one store, so phase lock is
> preserved without collateral damage. If you ever see the panel ring die when the beam
> starts, that is the bug that came back.

### Exit criteria
Phase offset is exact, monotonic, wraps cleanly, and is **reproducible across
reconfiguration**. Record the tick↔time scale (should be 6.67 ns/tick).

---

## 2b — The beam LED, ramped

**Now current flows.** Set the PSU limit to 2.5 A and keep the FLIR pointed at
**R73/R74 (the 0R27 ballast pair) and D11** for the whole ramp.

### Scope points

| Point | What | Expect |
|---|---|---|
| **TP5** | Q11 drain, below the ballast chain | switching waveform; on-phase low ≤ **0.15 V** |
| D11 pad 1 → TP5 (differential) | across R73+R74 = 0.54 Ω | **0.54 V/A** → ~1.6 V at 3 A |

### The ramp

```
beam freq 104167
beam duty 2
beam on
beam ramp 30 500          # 1 % steps every 500 ms — ~14 s, watch it climb
```

**After every few percent, check:** TP5 on-phase level, +5 V rail sag, PSU current, and
the FLIR. Expected at 30 %: ~0.95 A average from +5 V, ~3.15 W in D11, ~0.73 W in each
ballast resistor (3 W parts, so ~25 % of rating).

**Abort the ramp** (`beam duty 2`) if anything climbs faster than linearly with duty, or
if the ballast resistors run away. `beam duty` refuses above 35 % as a backstop.

> **FLIR technique:** the ballast resistors and D11's package read reasonably (ε ≈ 0.9),
> but **any exposed metal reads falsely cool**. Put a scrap of electrical tape on shiny
> parts. Take a reference image at 2 % duty before you start so you have an A/B.

**Do not sit at 30 % for long** on an open bench without airflow. It is the design
operating point but nothing is heatsinked for continuous duty at this stage.

### Confirm it is actually emitting
850 nm is invisible. Most phone cameras see it — point one at D11. The real proof is
TP7 in Phase 3.

---

## 2c — Q1: measure the one-shot clamp

```
beam clamp        # sets 1 kHz / 50 %, i.e. a 500 us commanded high phase
```

Scope **TP5**. The pulse width you see **is** the clamp. Safe: even 113 µs at 1 kHz is
only 11 % duty.

- .md claims **113 µs**
- 0.7 · R68 · C57 = 0.7 · 56 kΩ · 2.2 nF = **~86 µs**

**Record the real number in `PROGRESS.md` §6.** Then update `board.h`:

```c
#define STROBE_HW_LIMIT_US_ASSUMED  <measured>
#define STROBE_SW_MAX_US            <0.85 × measured>
```

This matters more than it looks: if the true clamp is 86 µs, the current 100 µs software
limit is *above* the hardware limit, and every slow-ball strobe pulse would be silently
truncated by hardware instead of controlled by firmware. **Also re-derive the .md §15
slow-ball rows** — at 10 m/s the 1 mm blur budget already wants 100 µs, so a clamp-limited
case means the blur budget grows or the pulse count shrinks. Decide it explicitly.

U5 (the strobe one-shot) is the identical circuit, so this measurement predicts it — but
verify U5 independently in Phase 6a.

---

## 2d — Q2: duty-fidelity sweep

```
beam duty 30
beam sweep 5000 250000 25 3000
```

At each step, measure the **actual** duty at TP5 and compare to the commanded 30 %.
The command prints the requested/actual frequency, TOP and level for each step.

**You are looking for the frequency where TP5 stops tracking the commanded duty.** U9's
timing node (R68 56 kΩ / C57 2.2 nF, τ ≈ 123 µs) has to reset via ~CLR during each low
phase — only 6.7 µs at 104 kHz. If it cannot keep up, actual duty falls below commanded
and the carrier frequency choice has to move.

Record the breakdown frequency. If it is below ~104 kHz, **stop and tell me** — the
carrier design changes and Phase 3 needs rethinking.

---

## Exit criteria for Phase 2

- [ ] Phase offset exact, monotonic, wrapping, and reproducible across reconfiguration (2a check 7)
- [ ] Commanded duty reproduced at TP5 within a few percent at the operating carrier
- [ ] **U9 clamp measured** and `board.h` updated (Q1)
- [ ] **Duty-fidelity limit found** and recorded (Q2)
- [ ] Thermals sane at 30 % — nothing running away
- [ ] Peak LED current confirmed near 3 A via the 0.54 V/A ballast measurement

Record everything in `PROGRESS.md` §6.
