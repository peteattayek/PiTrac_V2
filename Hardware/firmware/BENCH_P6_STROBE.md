# Phase 6 — High-power IR strobe

**This is the most dangerous phase on the board.** 9 A pulses from a 36 V rail through a
MOSFET deliberately operated in **linear mode**. Read the whole document before powering
anything.

**Prereq:** Phase 1, **plus A7 resolved** (see 6b). Otherwise independent of phases 2–5.
**Pi:** not connected. **Gear:** scope, logic analyzer, FLIR, PSU (limit 3 A).

---

> ## 🔴 FIRMWARE STATUS, audited 2026-08-25 — NONE OF THIS EXISTS YET
>
> **There is no strobe firmware.** Not partially written, not untested — absent. Before any
> of 6a–6d can be run, someone has to write it. The audit:
>
> | This document asks for | Reality in `src/` |
> |---|---|
> | Commanding a pulse width | **no `strobe` command**, no strobe module |
> | "Load the PIO burst program" | `src/strobe_burst.pio` **is assembled by `pioasm` on every build** (since commit 081c286, 2026-08-31, so it cannot rot unnoticed) — but **no C code loads it** and it emits nothing at link time |
> | "DMA-feed a schedule" | no DMA path, no schedule structure |
> | `compute_schedule()` unit test | **the function does not exist** |
> | `BURST_CHARGE_MAX_MC` interlock | the **constant** exists in `board.h` (6.0 mC, 🔴 unvalidated, since commit 081c286) — **no interlock uses it** |
> | Ramping Gate_PWM | **no `gate` command** |
> | "capture the ADC0 plateau in BURST mode" | `adcmode burst` exists and selects ch0; nothing fires a pulse to plateau |
>
> ✅ **What DOES exist and is usable today:** `adcmode burst` (ADC ring on ch0 at 500 ksps),
> `capture 0x01 ...` for ADC0, `pins`, and the `board.h` constants
> `STROBE_HW_LIMIT_US_ASSUMED` (122), `STROBE_SW_MAX_US` (100), `STROBE_MIN_GAP_US` (150),
> `STROBE_SENSE_V_PER_A` (0.135).
>
> **So the honest running order is: write the firmware, then 6a.** The sections below are the
> acceptance procedure for that firmware, not a procedure you can start today. Each step names
> the command it *will* need; if a command below is not in `help`, it has not been written.

---

## How to read the procedures below

Every sub-phase opens with a **board state** table. Set the board to exactly that state before
running the commands, and check it again after any fault or reset.

⚠ **`off` now also turns the beam off** (fixed 2026-08-24). Before that, `s_on` survived a
rail-down and the next `on` relit D11 with no `beam on`. If you are on an older build, type
`beam off` explicitly.

---

## 🔴 Two things measured in Phase 3 that land squarely on this phase

> ### 1. A rail sag BLINDS the detector, and its recovery can fake a ball.
>
> **Measured on board 3, 2026-08-31 (`BENCH_P3_DETECT.md` §3.6b).** `+2V5` is `+5VA/2`, so the
> whole detector chain's reference rides the rail. While the HPF is in **HOLD** — which is what
> *armed* means — a rail step reaches the comparator input amplified **×7.43**:
>
> | | |
> |---|---|
> | measured coupling, HOLD | **×7.43** (+75.0 mV rail → +556.9 mV at the comparator input) |
> | rail step that fires a 300 mV threshold | **39 mV** |
> | U12B's downward headroom before it saturates | **21 mV** |
>
> 🔴 **Bursts sag the rail deliberately, and that is exactly the mechanism.** Two distinct
> effects, and they happen in this order:
>
> 1. **During the sag the detector is BLIND.** U12B is single-supply with its gain taken to
>    ground, so its quiescent output *is* the bottom of its range. A falling rail saturates it
>    after 21 mV and it stops responding — measured through a further 120 mV of droop that
>    ×7.43 says should have moved it 870 mV.
> 2. **The RECOVERY is the false-trigger edge**, because the comparator triggers on a *rising*
>    input. A 200 mV sag recovering swings the comparator input ~1.5 V.
>
> ✅ **Why this is not a Phase 3 blocker:** the strobe fires in `SHOT_FIRING`, long after
> `SHOT_TRIGGERED`, so both effects land outside the armed window. 🔴 **What it constrains
> here:** do not re-arm while the rail is still recovering from a burst. The 500 ms supply-monitor
> debounce was sized for the sag; **nothing yet covers the recovery.** Budget a re-arm delay.
>
> 🔧 The board fix is `NEXT_BOARD_REV.md` **CR-02** (regulate +2V5), now a measured
> requirement rather than a proposal.

> ### 2. This is where the MCU watchdog earns its place — arm it deliberately.
>
> The watchdog **defaults OFF** as of 2026-08-31. On the bench a watchdog reset just drops the
> latch in the middle of a measurement, and a hung firmware is obvious to an operator sitting in
> front of the board. 🔴 **A hang with 9 A running through a linear-mode FET is a different
> risk entirely**, so arm it here — in the strobe code, deliberately — rather than relying on an
> ambient default nobody remembers is on. `wdog on` arms it for a session; `stat` shows the state.

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

### Board state

| | |
|---|---|
| **J3 (LED bank)** | 🔴 **DISCONNECTED.** This is what makes 6a safe. |
| PSU | 5.2 V, **limit 3 A** |
| Rail | **up** (`on`) |
| **Gate_PWM (GPIO28)** | 🔴 **0** — no current setpoint, so even a wiring error drives nothing |
| **GPIO27 `PULSE_LIMIT_DIS`** | 🔴 **0**, and stays 0. Verify with `pins` before every session. |
| Beam | **off** (`beam off`) — unrelated to this phase and one less variable |
| Pi | **not connected** |
| Probes | **Q10's gate, at R64.** ⚠ **TP3 is Q9's gate** — a DC level, not the pulse. If you see pulses at TP3, something is wrong. |

### Step 0 — confirm the watchdog is not defeated

```
pins
```

**Expect GPIO27 = 0.** 🔴 **If it is 1, stop.** Nothing in phases 0–6c has any reason to set
it, the CLI refuses to touch it, and it is written 0 in exactly one place (`safe_state()`).
A 1 here means something is very wrong.

### 6a.1 Measure the U5 clamp — before anything else

Command 200 µs and 1 ms pulses and record where U5 truncates.

- .md claims **113 µs**
- 0.7 · R56 · C51 = 0.7 · 56 kΩ · 2.2 nF = **~86 µs**
- **Phase 2c measured U9 — the identical circuit — at 122.68 µs (2026-08-13).** Both estimates
  above were low; the real coefficient is K ≈ **1.0**, not 0.7. U5 is the same part
  (74LVC1G123) with the same 56K/2.2nF, BOM-confirmed, so **expect ~122 µs**. With 1 %
  resistors and ±10 % capacitors the band is **109–136 µs**. Measure it; do not assume U9's
  exact value.

**`board.h` already carries values derived from U9 — confirm or correct them against U5:**
```c
#define STROBE_HW_LIMIT_US_ASSUMED 122    // U9 measured 2026-08-13; VERIFY ON U5 HERE
#define STROBE_SW_MAX_US           100    // meets .md S15 at 10 m/s; 0.82 x the HW limit
```
⚠ **These are not placeholders any more.** An earlier revision of this document showed them as
`<measured>`, which is stale — Phase 2c set them. **What is still open is whether U5 agrees
with U9.** If U5 comes in below ~118 µs the margin tightens; below 100 µs, `STROBE_SW_MAX_US`
must come down and the §15 slow-ball rows *do* need re-deriving.

**Why this is first:** if the 100 µs software limit were *above* the hardware limit, every
slow-ball pulse would be silently truncated by hardware rather than controlled by firmware,
and the blur budget you think you have would be fiction.

> ✅ **Phase 2c resolved this favourably — and found a second problem.** U9 measured
> **122.68 µs**, well above the software limit. But `STROBE_SW_MAX_US` was **73 µs**, not the
> 100 µs often quoted: *below* the 100 µs that §15 needs at 10 m/s, so slow-ball pulses would
> have been firmware-truncated by 27 %. It has been raised to **100 µs**, which meets §15 and
> sits 18 % under the measured clamp. **The §15 slow-ball rows need no re-derivation.**
>
> This still has to be confirmed on **U5**, since that is the part the constant actually
> governs. If U5 comes in below ~118 µs the margin tightens; below 100 µs the original
> concern returns and the §15 rows *do* need re-deriving.

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
| 10 m/s | 4.57 ms | 100 µs — **NOT clamped** ✅ | 4.27 ms | 38 ms | 9 mC → **shed pulses** |

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

Verify the `BURST_CHARGE_MAX_MC` (6.0) interlock actually sheds pulses at 10 m/s — and note
it is the only row that exceeds it, at 9 mC. The 20 m/s row (4.5 mC) passes with 25 % margin.

---

## 6b — Gate DAC only, still no LED bank

### Board state

| | |
|---|---|
| **J3 (LED bank)** | 🔴 **STILL DISCONNECTED** |
| **A7** | 🔴 **MUST BE RESOLVED FIRST — see the block below. This is a gate, not a warning.** |
| Rail | **up** |
| GPIO27 | **0** |
| Beam | **off** |
| Probes | **TP3** (Q9 gate, the DC setpoint) and **TP2** (+12 V) |

### Step 1 — ramp the setpoint

Ramp Gate_PWM 0 → full while scoping **TP3**.

| Expect | |
|---|---|
| TP3 | **3 × the filtered DAC voltage, 0 → ~9.9 V** |
| TP2 (+12 V) | **holds** — the budget is only ~5 mA from R15 |

### What you are looking for

| Symptom | Meaning |
|---|---|
| TP3 tracks 3× the DAC, smooth | ✅ the gate chain is healthy |
| **Ringing at TP3 on a pulse edge** | 🔴 gate-loop stability problem — look at R63 and layout **before going further** |
| Steady DC at TP3 during bursts | ✅ correct — Q9 does not switch, Q10 does |
| TP2 sagging | the +12 V budget is exceeded; check R15 |

> ### 🔴 A7 — resolve this BEFORE configuring the gate DAC. It is a gate, not a note.
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

⚠ **A7 status, audited 2026-08-25: still open.** `ARCHITECTURE.md` lists it as the only one
of A1/A2/A7 not fixed, and `panel.c` still configures slice 6A. **Nothing in the current
firmware would stop the collision**, because the gate DAC that would collide has not been
written yet — which means the fix has to land in the same change that adds it.

---

## 6c — LED bank connected, current ramp

🔴 **This is the first step where real current flows. Everything before it exists to make this
boring.** Do not start it at the end of a session.

### Board state

| | |
|---|---|
| **J3 (LED bank)** | **CONNECTED** — for the first time |
| PSU | 5.2 V, **limit ~3 A.** The pulses come from the VIR bulk caps; the PSU only sees the average. |
| Rail | **up** |
| **Gate setpoint** | 🔴 **start at ZERO and ramp up.** Never begin at a guessed setpoint. |
| GPIO27 | **0** |
| Beam | **off** |
| Pi | **not connected** |
| Probes | **TP4** (current sense) on the scope, **and** read **ADC0** — each cross-checks the other |
| FLIR | **powered on and pointed at Q9 and HS1 before the first pulse** |

**TP4 = 135 mV/A** → 1.215 V at 9 A (0.61 V at 4.5 A single string, 2.43 V at 18 A).
That is `STROBE_SENSE_V_PER_A` in `board.h`.

### Procedure — single 20 µs pulses, setpoint ramping from zero

1. Set the gate setpoint to **0**.
2. Fire **one** 20 µs pulse. Capture the ADC0 plateau in BURST mode
   (`adcmode burst`, then `capture 0x01 ...`).
3. Read TP4 on the scope for the same pulse. **The two must agree.**
4. Raise the setpoint one step. Repeat from 2.
5. 🔴 **Stop early if any plateau exceeds 1.2 × target.**
6. **FLIR Q9 and HS1 after every burst sequence** — not at the end.
7. Once the LUT is built, verify with **3 confirmation pulses** at the solved setpoint.

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

### Board state

| | |
|---|---|
| **Gate setpoint** | 🔴 **LOW ONLY — ~2 A.** This is the whole safety margin for this step. |
| J3 | connected |
| GPIO27 | **0** — the point of the test is that the *hardware* clamp works |
| Probes | TP4 |

### Step

Command a **1 ms** pulse — deliberately far longer than the clamp — and scope TP4 to confirm
the hardware truncates it at ~122 µs.

**Expect:** TP4 shows a pulse of roughly the measured U5 clamp width, **not** 1 ms.

🔴 **If it shows 1 ms, stop immediately and power down.** The hardware watchdog is not
working, and every later step assumes it is.

**Record the truncated width. Then never do this again** outside a guarded CLI test mode.

---

## Exit criteria

- [ ] **Strobe firmware written** — pulse command, PIO burst engine (`strobe_burst.pio` is
      already assembled; it needs LOADING), schedule computation, `BURST_CHARGE_MAX_MC` interlock, gate DAC
- [ ] **U5 clamp measured**, `STROBE_SW_MAX_US` confirmed or corrected against it, §15 table
      re-derived only if U5 comes in below 100 µs
- [ ] Commanded widths reproduce at Q10's gate
- [ ] PIO burst patterns verified on the LA, 1 µs granularity, IRQ on completion
- [ ] Energy interlock sheds pulses at 10 m/s
- [ ] **A7 resolved** — ready LED off the PWM block before the gate DAC was configured
- [ ] TP3 = 3 × DAC, no ringing
- [ ] Strobe current LUT built, ADC0 agrees with TP4
- [ ] Q9 and HS1 thermals sane, heatsink demonstrably coupling
- [ ] `PULSE_LIMIT_DISABLE` still 0, still with no CLI path

---

## ⚠ Two constraints Phase 3 established that bite here

### 1. Entering BURST destroys the detect and mic history

`adc_engine_set_mode()` restarts the ring, so the moment BURST is selected every ch5 and
ch7 sample already captured becomes unreadable — the stride changes and the de-interleave
no longer maps. BURST is `{0}` only.

So the firing path must be ordered:

```
comparator edge -> ADC refinement (ch5) + mic analysis (ch7) -> THEN adcmode burst
```

The Phase 4 design already runs the refinement inside the camera-handshake wait, which is
before the strobe, so the natural sequence is right. **The hazard is that nothing enforces
it and the failure is silent** — `adc_ring_view()` just returns false, the pass is flagged
`DQ_NO_ADC`, and the ADC column quietly disappears from the results. Full detail in
`ARCHITECTURE.md` **A9**.

### 2. Launching core 1 changes what `cfg_save()` has to do

`pico_multicore` is deliberately not linked today, so `flash_safe_execute()` takes its
single-core path. **The moment Phase 6 launches core 1, `flash_safe_execute_core_init()`
must be called on it.** Without that, core 1 executes XIP during a flash erase, which is a
hard fault rather than a corrupted write.

Also note `cfg_save()` refuses while the beam is on, the detector is armed, or the state is
not STANDBY/BENCH_RUNNING — a 4 KB erase blinds the `V5_MIN_SUSTAINED` monitor for tens of
milliseconds. **Consequence: calibration cannot be persisted between shots**, only at the
end of a session. That is the right trade, but plan the session around it.
