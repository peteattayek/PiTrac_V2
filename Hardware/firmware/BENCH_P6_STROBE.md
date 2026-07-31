# Phase 6 — High-power IR strobe

**This is the most dangerous phase on the board.** 9 A pulses from a 36 V rail through a
MOSFET deliberately operated in **linear mode**. Read the whole document before powering
anything.

**Prereq:** Phase 1. Independent of phases 2–5 — it can be done any time after the latch
works. **Pi:** not connected. **Gear:** scope, logic analyzer, FLIR, PSU (limit 3 A).

---

## The one rule

**`PULSE_LIMIT_DISABLE` (GPIO27) stays 0 through 6a–6c.**

It defeats the U5 hardware pulse-width watchdog. It is written 0 in exactly one place in
the entire firmware (`safe_state()`), the CLI actively refuses to touch it, and nothing
in phases 0–6c has any reason to change that. A stuck-high strobe with the watchdog
defeated destroys the LED bank and probably Q9.

---

## The circuit, in the order it fails

```
VIR 36 V → external LED strings (J3) → VIR_RTN → Q9 IRLR2905 (LINEAR) → Q10 AO3400A
                                                      → R65‖R66 = 0.135 Ω → GND
```

**Two separate controls, and confusing them is the classic mistake:**

| | Sets | Path |
|---|---|---|
| **"how much"** | current amplitude | Gate_PWM (GPIO28) → 2-pole RC → U6A LM358 ×3 → U7 buffer → R63 → **Q9 gate (TP3)** |
| **"when"** | pulse timing | Strobe_Pulse (GPIO25) → **U5 one-shot** → U8 MCP1416 → R64 → **Q10 gate** |

Q9 sits at a steady DC gate voltage the whole time; **Q10 does the switching.** So TP3 is
a DC level, not a pulse — if you see pulses at TP3 something is wrong.

**There is no analog current servo.** Absolute current depends on Q9's V_th, which drifts
with temperature. The design intent is a firmware loop: calibrate per session against
`CurrentSense_ADC`, read back per shot, trim between shots. Expect hundreds of mA of
cold-to-warm drift at 9 A, and that is normal.

---

## 6a — Timing chain dry: **J3 DISCONNECTED, no LED bank, Gate_PWM = 0**

Zero current flows. Completely safe, so be thorough here — everything you can prove now
is something you are not debugging at 9 A.

**Probe Q10's gate** (at R64). Note **TP3 is Q9's gate** — the DC setpoint — not the pulse.

### 6a.1 Measure the U5 clamp — before anything else

Command 200 µs and 1 ms pulses and record where U5 truncates.

- .md claims **113 µs**
- 0.7 · R56 · C51 = 0.7 · 56 kΩ · 2.2 nF = **~86 µs**
- Phase 2c already measured the identical U9 circuit — this should agree

**Set from the measurement, not from the .md:**
```c
#define STROBE_HW_LIMIT_US_ASSUMED  <measured>
#define STROBE_SW_MAX_US            <0.85 × measured>
```

**Why this is first:** the current 100 µs software limit may be *above* the real hardware
limit. If so, every slow-ball pulse is silently truncated by hardware rather than
controlled by firmware, and the blur budget you think you have is fiction.

**Then re-derive the .md §15 slow-ball rows.** At 10 m/s the 1 mm blur budget already
wants 100 µs. If the clamp is 86 µs, that case is clamp-limited: either the blur budget
grows or the pulse count shrinks. **Decide it explicitly rather than letting hardware
decide silently.**

### 6a.2 Pulse fidelity and the PIO burst engine

- Commanded 5 / 10 / 20 / 50 µs reproduce faithfully at Q10's gate
- Re-check the top of the range against the measured clamp
- Load the PIO burst program, DMA-feed a schedule, capture 10-pulse bursts on the LA at
  several (width, gap) pairs
- Verify 1 µs granularity, the width = 0 sentinel terminates, and IRQ0 fires on completion

### 6a.3 Schedule math, on the bench with no hardware at risk

Unit-test `compute_schedule()` against the .md §15 table:

| Ball speed | Transit | Pulse width | Period | Burst | Charge @ 9 A |
|---|---|---|---|---|---|
| 90 m/s | 508 µs | 11 µs | 474 µs | 4.3 ms | 1.0 mC |
| 50 m/s | 914 µs | 20 µs | 853 µs | 7.7 ms | 1.8 mC |
| 20 m/s | 2.29 ms | 50 µs | 2.13 ms | 19 ms | 4.5 mC |
| 10 m/s | 4.57 ms | 100 µs → **clamped** | 4.27 ms | 38 ms | 9 mC → **shed pulses** |

> **Two assumptions are baked into that table and neither is stated in the .md.** Without
> them the rows cannot be checked, so make them explicit before you unit-test against it:
>
> - **10 pulses per burst** (hence 9 inter-pulse periods). Check: 9 × 474 µs = 4.27 ms ✓,
>   9 × 853 µs = 7.68 ms ✓, 9 × 2.13 ms = 19.2 ms ✓, 9 × 4.27 ms = 38.4 ms ✓.
>   Charge follows: 10 × 11 µs × 9 A = 0.99 mC ✓, 10 × 20 × 9 = 1.8 ✓,
>   10 × 50 × 9 = 4.5 ✓, 10 × 100 × 9 = 9.0 ✓.
> - **Transit is over 45.72 mm, not the ball's 42.67 mm diameter.** Check: 45.72/90 = 508 µs ✓,
>   45.72/50 = 914 µs ✓, 45.72/20 = 2.29 ms ✓, 45.72/10 = 4.57 ms ✓. That is ball diameter
>   plus ~3 mm; **confirm where the 3 mm came from** before trusting the geometry downstream.
>
> The table is internally consistent to three digits on both, so these are almost certainly
> the intended assumptions rather than coincidence.

Verify the `BURST_CHARGE_MAX_mC` (6.0) interlock actually sheds pulses at 10 m/s — and note
it is the only row that exceeds it, at 9 mC. The 20 m/s row (4.5 mC) passes with 25 % margin.

---

## 6b — Gate DAC only, still no LED bank

> ### 🔴 Do this before configuring the gate DAC — finding A7
>
> **`GPIO28` (Gate_PWM) and `GPIO12` (the panel ready LED) are the same PWM channel** —
> slice 6A on RP2350B. Note that is the same **channel**, not just the same slice. Two
> channels of one slice (6A and 6B) would have been fine: they share `TOP` and `DIV`, so a
> common frequency, but each has its own compare register and so its own duty. **The same
> channel shares the compare register as well**, so both pins emit the identical waveform.
> There is no second register to write, and letting the gate DAC "win" on frequency does not
> help — it would inherit the LED's duty cycle, which *is* the current setpoint.
>
> `panel.c` configures 6A today (wrap 999, div 150). **Whichever is configured second
> silently takes over both.** So either the ready-LED brightness becomes your 9 A current
> setpoint, or your current setpoint becomes the LED brightness — and neither announces
> itself. You would be chasing it as an analog fault in the gate chain.
>
> **Fix first: take the ready LED off the PWM block** — plain SIO on/off, or software PWM
> from the 50 Hz timer in `ARCHITECTURE.md` A4. Both GPIO numbers are fixed by the PCB, so
> the collision cannot be routed around; the indicator is the one that yields.
>
> See the PWM SLICE MAP in `board.h`. Two other pairs collide but are safe as long as
> **GPIO15 (the latch) and GPIO27 (the watchdog defeat) never go on PWM.**

Ramp Gate_PWM 0 → full, scope **TP3**.

| Expect | |
|---|---|
| TP3 | 3 × the filtered DAC voltage, 0 → ~9.9 V |
| TP2 (+12 V) | holds — the budget is only ~5 mA from R15 |

Steady DC at TP3 even during bursts is correct. **Ringing at TP3 on a pulse edge** means a
gate-loop stability problem — look at R63 and layout before going further.

---

## 6c — LED bank connected, current ramp

**PSU limit ~3 A.** The pulses come from the VIR bulk caps; the PSU only sees the average.

**Scope TP4 *and* read ADC0** so each cross-checks the other:
- TP4 = **135 mV/A** → 1.215 V at 9 A (0.61 V at 4.5 A single string, 2.43 V at 18 A)

### Procedure

Single **20 µs** pulses, gate setpoint ramping upward from zero:

1. Fire one pulse, capture the ADC0 plateau in BURST mode
2. Build the duty → amps LUT
3. **Stop early if any plateau exceeds 1.2 × target**
4. Verify with 3 confirmation pulses at the solved setpoint

### What to watch for

| Symptom | Meaning |
|---|---|
| Plateau **sags within a pulse** | VIR headroom exhausted — only ~9–11 V above the string Vf |
| Plateau **sags across a burst** | bulk caps depleting; shorten pulses or shed count |
| Rise slower than ~1–2 µs | harness inductance higher than expected |
| ADC0 and TP4 disagree | trust TP4; suspect the R32 path or ADC scaling |

### ⚠ Thermal — the FLIR's most important job on this board

**Q9 is dissipating real power in linear mode.** Image it and HS1 after *every* burst
sequence during the ramp.

**Also check HS1 is actually coupling.** A hot Q9 with a cold heatsink means the thermal
interface (TIM1) is not doing its job — and **no electrical measurement will ever tell you
that.** This is the one failure mode where the thermal camera is not a convenience but the
only instrument that works.

Emissivity: Q9's tab and the heatsink are metal and read **falsely cool**. Tape or paint a
patch on both before trusting any number.

Never run repeated bursts faster than the energy interlock allows.

---

## 6d — Clamp verification *with* current, once

At a **low setpoint only (~2 A)**, command a 1 ms pulse and scope TP4 to confirm the
hardware clamp truncates it. Record it.

**Then never do this again** outside a guarded CLI test mode.

---

## Exit criteria

- [ ] **U5 clamp measured**, `STROBE_SW_MAX_US` set from it, §15 table re-derived
- [ ] Commanded widths reproduce at Q10's gate
- [ ] PIO burst patterns verified on the LA, 1 µs granularity, IRQ on completion
- [ ] Energy interlock sheds pulses at 10 m/s
- [ ] **A7 resolved** — ready LED off the PWM block before the gate DAC was configured
- [ ] TP3 = 3 × DAC, no ringing
- [ ] Strobe current LUT built, ADC0 agrees with TP4
- [ ] Q9 and HS1 thermals sane, heatsink demonstrably coupling
- [ ] `PULSE_LIMIT_DISABLE` still 0, still with no CLI path
