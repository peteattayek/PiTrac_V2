# Phase 6 — High-power IR strobe

**This is the most dangerous phase on the board.** 9 A pulses from a 36 V rail through a
MOSFET deliberately operated in **linear mode**. Read the whole document before powering
anything.

**Prereq:** Phase 1. A7 is resolved in firmware (2026-10-02) and proven on the bench in 6b
(board 1, 2026-10-06). Otherwise independent of phases 2–5.
**Pi:** not connected. **Gear:** scope, logic analyzer, FLIR, PSU (limit 3 A).

---

> ## FIRMWARE STATUS, 2026-10-09 — 6a/6b dry tests PASS; 6c/6d live-current firmware on board 1, not yet fired
>
> **Image for 6c:** Release build **`Oct  7 2026 08:07:33`** (`id` → `built`), **184440 text /
> 0 data / 122720 bss**, `pitrac.uf2` SHA256 `DE0C6676…2DE80BC2`. **78/78 native host tests pass**
> (`tests/`, see `BENCH.md` "Host regression tests"). It is the 2026-10-05 image plus the 6c/6d
> live mode, the "before 6c" backlog items and two fixes from an independent review
> (`PROGRESS.md` §6 2026-10-07). **Board 1 runs it** (inferred from its 2026-10-07/08 logs; confirm
> with `id` before 6c Step 1 — `PROGRESS.md` §6 2026-10-09). Any other board: reflash first.
> **6a and 6b PASS on board 1 (2026-10-06**, LA + DMM + scope, on
> the 2026-10-05 image; see the exit criteria) — the dry path is unchanged in the new build.
> ✅ **TP3 loop stability PASS on board 1 (2026-10-08**, MSO54B: AC RMS 3.6–3.8 mV at gates
> 0/50/90/100, flat with the setpoint — 6b exit criteria). Nothing else gates 6c on board 1.
>
> | This document asks for | Status |
> |---|---|
> | Commanding a pulse width | ✅ `strobe pulse <us>`, 5–100 µs |
> | Measuring the U5 clamp (6a.1) | ✅ `strobe clamptest <us>`, one pulse 101–2000 µs — **135 µs on board 1** |
> | Loading the PIO burst program, DMA-feeding a schedule | ✅ `strobe.c` loads `strobe_burst.pio` into PIO0 SM0; every pulse and burst is DMA-fed and completion is IRQ0 |
> | `compute_schedule()` and its unit test | ✅ `strobe_compute_schedule()` in `strobe_plan.c`; `strobe sched <m/s> [fire]`; host-tested against §15 |
> | `BURST_CHARGE_MAX_MC` interlock | ✅ sheds pulses (spacing kept) in the schedule; refuses over-limit manual bursts |
> | A7 — ready LED off the gate DAC's PWM channel | ✅ ready LED is SIO on/off; `strobe_init()` panics if GPIO12 is ever on PWM |
> | Ramping Gate_PWM | ✅ `strobe gate <pct>` |
> | Firing with current (6c), ADC0 plateau per shot, watchdog arming | ✅ **written 2026-10-07** — live mode, `strobe live on confirm` (6c below) |
> | Current LUT | ✅ `strobe cal [A]` — solves the level for 2–9 A, 3 confirmation pulses; RAM only (`cfg` has no field for it yet) |
> | Clamp verification with current (6d) | ✅ `strobe clamptest <us>` in live mode, only at a level that measured 0.5–2.5 A |
>
> **Two modes.** LED-bank current needs Q9's gate (setpoint) **and** Q10's gate (pulse).
>
> - **DRY (the default, and every power-up):** a pulse is admitted only while the gate DAC is
>   **provably zero** — commanded level, hardware compare register and pin function all
>   checked, **and at least 20 ms since it was last lowered to zero**, because Q9's gate decays
>   through the RC ladder (dominant pole 2.62 ms) long after the register reads zero — and the
>   gate rises only while **no pulse can start**. So with live mode off, nothing can command
>   strobe current, whatever is on J3. **J3 stays disconnected for 6a and 6b.**
> - **LIVE (`strobe live on confirm`, from gate 0 only):** relaxes exactly that one rule, under
>   the guards tabled at the top of 6c — staircase, 70 % ceiling, ADC0 readback and verdict on
>   every firing, overcurrent and stuck-on faults, pacing and charge budget, watchdog, CLI lock,
>   idle timeout.
>
> Every pulse in either mode also requires: rail ready, no latched fault, no Pi, GPIO27 provably
> SIO output driven 0 (read only — it is still written in exactly one place), GPIO12 off PWM.
> Every route to rail-down calls `strobe_safe_off()` before the latch opens: GPIO25 to SIO low,
> engine and DMA stopped, gate DAC to zero, live mode off.
>
> **Measured on board 1 (6a, 2026-10-06), so no longer assumptions:** the PIO overhead (a width
> word *N* gives *N* + 2 µs, a gap word *M* gives *M* + 8 µs) and U5's clamp (135 µs; `board.h`
> carries the worst case, 137 µs). **Not yet measured — 6c measures them:** the gate-to-current
> slope (the guards assume ≤ ~6 A per volt at TP3), ADC0 against TP4, TP3 at 9 A (design
> 4.5–6 V), and the VIR droop across a burst.

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
>
> **2026-10-02:** the 6a/6b build did **not** arm it, because it could not command strobe
> current. **2026-10-07:** live mode arms it itself (`strobe live on confirm`) and disarms it
> again when live mode ends — unless it was already armed or you typed `wdog on` while live.
> `wdog off` is refused while live. Every path inside live mode services it (≤ 5 ms gaps).

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

With J3 disconnected there is no LED-bank current. **This is not a voltage-free setup:**
the board still contains 36 V and stored charge. Keep conductive objects clear of D12,
connect probes only with the rail down, and use true board GND, not TP5 (`Strobe_GND`,
which is a switched node). Everything proved here is something not debugged at 9 A.

The current build is **`Oct  7 2026 08:07:33`** (2026-10-07: adds 6c/6d live mode; the dry path
used here is unchanged). Boards 1 and 3 run the earlier `Oct  5 2026 11:13:55`, on which 6a/6b
passed. On any board, **reflash first** with the current build. Everything below needs it.

### Board state

| | |
|---|---|
| Firmware | `id` → `built    : Oct  7 2026 08:07:33` |
| **J3 (LED bank)** | 🔴 **DISCONNECTED.** This is what makes 6a safe. |
| PSU | 5.2 V, **limit 3 A** |
| Rail | **up** for the measurements; **down** while connecting probes |
| **Gate_PWM (GPIO28)** | 🔴 **0** — and the firmware refuses every pulse unless it is |
| **GPIO27 `PULSE_LIMIT_DIS`** | 🔴 **0**, and stays 0. Checked in Step 0. |
| Beam / detector | **`beam off`**, **`detect disarm`** — unrelated, one less variable |
| Pi / camera | **J8 and J4 empty** |
| Logging | Serial Monitor **file logging ON** before Step 0 |

**Probes** — attach with the board **unpowered** (PSU and USB off; "rail down" still leaves
+3V3 live from USB), clips or soldered wires only, all grounds to board GND:

| Signal | Where | Level | Instrument |
|---|---|---|---|
| **Strobe_Pulse (GPIO25)** — what firmware commanded | **R33** (0805, 0 Ω, MCU → R57/U5), soldered wire on the end **farther from R57** — *not* R57 itself (0402: its GND pad is **0.38 mm** from the signal pad and 0.46 mm from U5's pins) | 0–3.3 V | LA digital, or scope CH2 |
| **U5 Q** — after the clamp, 3.3 V logic | **R61 pad 1** (the end farther from U8) — via a **wire soldered on with the board unpowered**, never a hand-held probe | 0–3.3 V | **LA digital** — the clamp measurement when there is no scope |
| **LA / probe ground** | **TP1** (the GND loop test point) — a grabber clip | 0 V | not TP5 (`Strobe_GND`, switched) |
| **Q10 gate** — after U8 | **R64 pad 2** (the Q10 side; 0805) — **soldered wire, board unpowered**. Nearest other nets 1.32 mm (CurrentSense) and 1.33 mm (GND). Not Q10's gate pin: it is 0.53 mm from Q10's drain | 0 → **~12 V** | **Scope CH1, 10× probe** on the wire — not the LA |

After soldering, **still unpowered**, re-measure each lead → GND: it must read the same as
before soldering (≈ 1 kΩ via R57 / R62 — some meters read lower through the unpowered ICs'
protection diodes); **≤ ~20 Ω is a short** — find it before powering up.

🔴 **R61 sits between +12 V and GND pads (2026-10-05, a probe sparked here on board 2).** R61 is
an 0805 (pads 2.0 mm apart) beside U8's decoupling. Edge-to-edge from the KiCad layout: pad 2
(U8 side) is **0.58 mm from GND** (R62.1) and **0.75 mm from +12 V** (C54.1, 4.7 µF); pad 1
(U5 side) is 0.68 mm from GND (R65.1), 0.75 mm from GND (C54.2) and 1.15 mm from +12 V (C54.1).
A probe tip that slips bridges them. **+12 V to GND** dumps ~0.4 mJ from the ~6 µF on +12 V
(a visible spark; the rail itself is only R15 4K7 from VIR, ~8 mA). **+12 V onto R61** forces
12 V into U5's 3.3 V output through its clamp diode, which can kill U5. So: board fully
unpowered (PSU **and** USB off), solder ~30 mm of thin wire (30 AWG) to **R61 pad 1**, clip
the LA to the wire, then power up. No test point exists for this node — see
`NEXT_BOARD_REV.md` CR-21.

⚠ **Do not put the logic analyser's analog input on Q10's gate.** U8 runs from +12 V, which
is a 12 V Zener (±5 %, up to ~12.6 V), and the LA's analog inputs are rated **12 V max**.
⚠ **TP3 is Q9's gate** — a DC level, not the pulse. In 6a it should sit at ~0 V throughout.

> 🔵 **Logic analyser only, no scope (added 2026-10-05)? 6a can still run.** U5's Q output
> (R61) **is** the clamped pulse in 3.3 V logic; U8 only buffers it to ~12 V for Q10, adding
> tens of ns of delay and edge time against a ~122 µs clamp and a 22 µs margin to the 100 µs
> software limit. So every 6a timing number and the 6a.1 go/no-go decision come from two LA
> digital channels: **GPIO25 at R33** and **U5 Q at R61 pad 1 (soldered wires — see the 🔴 note
> above)**, 3.3 V logic threshold, **50 MS/s**
> (20 ns), triggered on GPIO25 rising, capture ≥ 2 ms.
>
> **What waits for the scope:** Q10's gate high level (~12 V), its rise/fall times and ringing,
> and its width after U8 — the "Commanded widths reproduce at Q10's gate" exit item. Those
> describe whether U8 drives Q10 hard enough, which first matters when current flows, so they
> are **required before 6c**, not before 6a.2, 6a.3 or 6b. Do not improvise a divider or clip
> extra leads onto the Q10/R64 node to get them sooner.

### Step −1 — board UNPOWERED: one resistance check (added 2026-10-06)

**State:** PSU **and** USB disconnected. DMM in **Ω** (not the continuity beeper).

| Measure | Expect | If not |
|---|---|---|
| **R61 pad 1 → GND** | **≈ 1 kΩ** (R62, the `GATE-DRV` pull-down; some meters read lower through the unpowered ICs' protection diodes) | **≤ ~20 Ω: `GATE-DRV`/U5 Q shorted to GND** — U5's pulse can never appear (found 2026-10-06; on that board it stayed at **10 Ω with R62 removed**, so R62 was not the cause — see the triage block in 6a.1). **Open / ≫ 1 kΩ:** R62 missing or open — U8's input is not held low if U5 stops driving; fit it before 6c |

**Pass:** ≈ 1 kΩ. This takes seconds and finds an assembly short before any pulse is fired.

### Step 0 — rail DOWN: identity, the limiter, and a refusal

**State:** STANDBY, latch 0, beam off, gate 0, J3 disconnected.

```
id
pins
strobe
strobe pulse 20
```

**Expect:**

- `id`: `built    : Oct  7 2026 08:07:33`.
- `pins`: **`PULSE_LIMIT_DIS(27)  0`** and **`STROBE_PULSE(25)  0`**.
  🔴 **If GPIO27 is 1, stop.** Nothing in phases 0–6c has any reason to set it.
- `strobe` shows:
  ```
  strobe   : DRY. Pulses need the gate at 0 and the gate rises only while no
  ...
  rail     : DOWN (state STANDBY) ...
  GPIO27   : SIO output, driven 0, pad 0  -> U5 pulse limiter armed
  A7       : GPIO12 ready LED on SIO (on/off); slice 6 belongs to the gate DAC
  gate DAC : level 0 of 1024 (0.00 %)   hw compare 0   GPIO28 PWM
  engine   : idle   PIO0 SM0 @ 1 MHz, offset <n>, DMA ch <n>   GPIO25 SIO, pad 0
  ...
  may fire : +5V rail not ready (U8 and the gate amp run from VIR)
  ```
- `strobe pulse 20` →
  ```
  last     : 1 x 20 us -- REFUSED -- nothing fired
             +5V rail not ready (U8 and the gate amp run from VIR)
  ```

**Pass:** all four. A refusal with the rail down is the point of this step.

### Step 1 — rail UP, ready to fire

**State:** board as above. Read `stat` first; send `on` **only from STANDBY**.

```
stat
on
```

Wait **at least 3 s** (no-Pi detection window), then:

```
stat
beam off
detect disarm
strobe
```

**Expect:** `BENCH_RUNNING`, latch 1, railsready 1, fault none; `strobe` shows `rail : up`,
gate level 0, engine idle, GPIO25 SIO pad 0, **`may fire : yes`**.

### 6a.1 Measure the U5 clamp — before anything else

**State:** rail up, gate 0, beam off. Scope single-shot, triggered on GPIO25 rising,
horizontal ~200 µs/div for 1 ms.

```
strobe clamptest 100
strobe clamptest 200
strobe clamptest 1000
```

`clamptest 100` must be **refused** (`ERR: clamptest is for widths over 100 us. Use 'strobe
pulse 100'.`) — it only accepts widths the software limit would otherwise block. Then fire each
of 200 and 1000 **three times**, recording each capture.

**Expect, each fire** (shown for 1000; `<n>` varies run to run):
```
CLAMP TEST: commanding 1000 us. U5 should end Q10's gate pulse at its
  clamp -- board 1 measured 135 us (U9, the same circuit, 122.68 us), so
  expect ~110-137 us. Record GPIO25's width AND Q10's gate width.
last     : 1 x 1000 us -- completed, IRQ0 seen
           3 PIO words; IRQ0 seen; enable->IRQ0 polled at <n> us,
           nominal 1011 us (coarse -- the logic analyser is the timing truth)
           GPIO25 after: low, SIO
```
For `clamptest 200` the same block reads `commanding 200 us`, `last     : 1 x 200 us`, and
`nominal 211 us` (3 µs PIO startup + 200 + 8 µs to IRQ0).

**Record:** GPIO25 high time (should be the commanded 200 / 1000 µs); Q10 gate high time at
50 % of its amplitude (~6 V); Q10 gate high level; rise and fall times. Six numbers → the
spread.

**LA-only (no scope):** record, for each of the six fires, the **GPIO25 high time**, the
**U5 Q (R61) high time** — this is the clamp — and the **GPIO25↑ → U5 Q↑ delay**. The
decision table below uses the U5 Q width. Q10's level, edges and width are the scope item
listed under the probes, due before 6c.

- .md claims **113 µs**
- 0.7 · R56 · C51 = 0.7 · 56 kΩ · 2.2 nF = **~86 µs**
- **Phase 2c measured U9 — the identical circuit — at 122.68 µs (2026-08-13).** Both estimates
  above were low; the real coefficient is K ≈ **1.0**, not 0.7. U5 is the same part
  (74LVC1G123) with the same 56K/2.2nF, BOM-confirmed, so **expect ~122 µs**. With 1 %
  resistors and ±10 % capacitors the band is **109–136 µs**. Measure it; do not assume U9's
  exact value.

**Pass / decision:**

| U5 clamp | Meaning |
|---|---|
| **≥ 118 µs** and both widths agree | ✅ keep `STROBE_SW_MAX_US` 100; check U5 against `STROBE_HW_LIMIT_US` (worst case across boards) |
| 100–118 µs | ⚠ margin tight — report it; the software limit comes down before 6c |
| **< 100 µs** | 🔴 **stop** — the software limit is above the hardware clamp; §15 slow-ball rows need re-deriving |
| ≈ the commanded 200 / 1000 µs | 🔴 **stop** — U5 is not clamping. Check GPIO27 and Q8 before anything else |

**`board.h` since 2026-10-07** (U5 measured on board 1; before that the clamp constant was
`STROBE_HW_LIMIT_US_ASSUMED` = 122, U9's value):
```c
#define STROBE_HW_LIMIT_US         137    // U5 worst case; board 1 measured 135 us (6a.1)
#define STROBE_SW_MAX_US           100    // meets .md S15 at 10 m/s; 35 us under board 1's U5
```
`STROBE_HW_LIMIT_US` is what the charge interlock uses as the longest deliverable pulse.
**A board whose U5 measures above 137 µs raises it** (one constant, one reflash); one below
~110 µs needs a look at the 100 µs software limit.

> ✅ **Measured, board 1, 2026-10-06: U5 clamp = 135 µs** (both widths, 6 fires). That is 10 %
> longer than U9 on the same board (122.68 µs): same part and nominal RC, so it is component
> tolerance (2.2 nF ±10 %), and **the clamp is per-board**. Because the constant is global and
> bounds charge, it holds the **worst case** across boards, not one board's value: the nominal
> 123 µs × 1.11 tolerance gives ≈ 137 µs, which is what `board.h` now carries.

**Why this is first:** if the 100 µs software limit were *above* the hardware limit, every
slow-ball pulse would be silently truncated by hardware rather than controlled by firmware,
and the blur budget you think you have would be fiction.

> ### If GPIO25 pulses at R57 but U5 Q (R61) shows nothing (added 2026-10-05)
>
> **The chain, from the netlist:** `Strobe_Pulse` (R57's top pad) drives U5 **pin 2 (B) and
> pin 3 (CLR)**; pin 1 (A) is GND, so a rising edge triggers. **Pin 5 (Q) → R61 (0 Ω) →
> `GATE-DRV`**, which has **R62 1 kΩ to GND** and U8's input (pin 3). **Pin 7 (RCext)** has R56
> 56K to +3V3, C51 2.2 nF, **and Q8's drain**; Q8 (AO3400A, gate = GPIO27 through R54 1K to GND)
> shorts RCext to GND when GPIO27 = 1, which **defeats** the clamp (Q then follows the input).
> U9 is wired the same way minus Q8, and works — so the trigger arrangement is proven.
>
> Cheapest first; **rail down while moving any lead**:
>
> | # | Check | Expect | If not |
> |---|---|---|---|
> | 1 | Move LA CH1 onto the **R33** lead beside CH0 (board unpowered while moving it), fire `strobe pulse 20` | both channels show the pulse | the LA channel/lead/setting is the problem |
> | 2 | DMM, board **unpowered** (PSU and USB off): across **R61** | ≈ 0 Ω | R61 open or missing — U5 Q never reaches the U8-side pad (the U5-side pad would still show it) |
> | 3 | DMM, board **unpowered**: **each R61 pad → GND** | ≈ 1 kΩ (R62) | **≤ ~20 Ω on both pads = `GATE-DRV`/U5 Q shorted to GND** (found 2026-10-06). Lift **R62** first: if the short stays (it did — **10 Ω**), R62 is not it. Then lift **R61** (0805, 0 Ω) and measure each pad side to GND: pad-1 side low → U5 pin 5 or a bridge there; pad-2 side low → U8 pin 3 or a bridge to R62's GND pad (0.58 mm). ~10 Ω is more like a damaged IC pin than a solder bridge (< 1 Ω) |
> | 4 | DMM: **U5 pin 8** → GND | 3.3 V (always on, rail up or down) | U5 is unpowered |
> | 5 | DMM: **Q8 pin 3 (drain)** → GND — the lone pin on its side of the SOT-23 | ≈ 3.3 V idle (RCext pulled up through R56) | ≈ 0 V: Q8 conducting (check `strobe` → GPIO27 line) or C51 shorted |
> | 6 | DMM, rail **up**: **TP2** → GND | ≈ 12 V | +12 V missing: U8 unpowered, and its input clamp drags R61 to ~0.6 V |
> | 7 | LA **analog** on the **wire soldered to R61 pad 1** **and** on Q8 drain, trigger on GPIO25 rising, `strobe clamptest 200` once | Q8 drain dips and recharges over ~120 µs; R61 is a ~3.3 V pulse of the same length | see below |
>
> Reading check 7: **RCext moves but R61 is flat** → U5's pin 5 joint, R61, or the probe.
> **R61 pulses at only ~0.6–0.8 V** → U8 unpowered (check 6). **RCext never moves** → U5 is not
> triggering: pins 2/3/4/8 joints, orientation, or the part. Remove the analog probe from Q8's
> drain afterwards — its 1 MΩ load pulls RCext down a few percent and would lengthen the clamp
> you are trying to measure. Keep the clip on Q8 pin 3 only: shorting it to pin 2 (source, GND)
> also defeats the clamp, and pin 1 is GPIO27's gate.

> ✅ **Phase 2c resolved this favourably for U9 — and found a second problem.** U9 measured
> **122.68 µs**, well above the software limit. But `STROBE_SW_MAX_US` was **73 µs**, not the
> 100 µs often quoted: *below* the 100 µs that §15 needs at 10 m/s, so slow-ball pulses would
> have been firmware-truncated by 27 %. It has been raised to **100 µs**, which meets §15 and
> sits 18 % under the measured clamp. This still has to be confirmed on **U5**.

### 6a.2 Pulse fidelity and the PIO burst engine

**State:** as 6a.1. LA on GPIO25 (and U5 Q), 50 MS/s if available (20 ns resolution); scope
on Q10's gate for the single pulses if you have one (without a scope, U5 Q stands in — see the
LA-only note under the 6a probes).

**Single pulses:**
```
strobe pulse 5
strobe pulse 10
strobe pulse 20
strobe pulse 50
strobe pulse 100
```

**Expect:** each `completed, IRQ0 seen`, `GPIO25 after: low, SIO`. GPIO25 high for exactly the
commanded width; Q10's gate the same width, delayed by U5 + U8 propagation. On the LA, U5 Q the
same width as GPIO25 (every one of these is under the clamp), delayed by U5's propagation only.

**Bursts** — one at a time, LA single-shot on GPIO25's first rising edge:

| Command | Pulses | Width | Period (rise to rise) | Nominal enable→IRQ0 |
|---|---|---|---|---|
| `strobe burst 5 150 10` | 10 | 5 µs | 155 µs | 1411 µs |
| `strobe burst 20 500 10` | 10 | 20 µs | 520 µs | 4711 µs |
| `strobe burst 50 1000 10` | 10 | 50 µs | 1050 µs | 9511 µs |
| `strobe burst 100 150 6` | 6 | 100 µs | 250 µs | 1361 µs |

The last one is **5.4 mC at the 9 A design current** — just under the 6.0 mC limit — so it is
admitted. These must all be **refused**, and fire nothing:

| Command | Expect |
|---|---|
| `strobe burst 100 4167 10` | `INVALID burst -- nothing fired` / `burst charge over the limit` (9.0 mC) |
| `strobe burst 20 100 10` | `INVALID` / `gap out of range` (minimum 150 µs) |
| `strobe burst 20 500 17` | `INVALID` / `pulse count out of range` (maximum 16) |
| `strobe pulse 101` | `INVALID` / `pulse width out of range` |
| `strobe pulse 2O` (letter O) | `usage: strobe pulse <us>   (NOTHING FIRED)` |

**Pass:**
- Exact pulse count; every width and every period within **±0.1 µs** of the table. The PIO
  counts at exactly 1 MHz from the crystal, so anything else is systematic.
- 🔵 **If every width is off by the same whole microsecond, or every period is, the
  instruction-overhead constants are wrong** (`STROBE_PIO_WIDTH_OVERHEAD_US` = 2,
  `STROBE_PIO_GAP_OVERHEAD_US` = 8, in `strobe_plan.h`). They were counted from the program
  listing, not measured. **Record the offset; it is a one-line fix**, not a failure of the engine.
- `IRQ0 seen` and `GPIO25 after: low, SIO` on every burst. The polled enable→IRQ0 time is
  coarse; it should be at or a little above the nominal, never wildly different.
- Every refusal row fires nothing — the LA shows no edge.
- TP3 stays at ~0 V throughout.

### 6a.3 Schedule math — host-tested, then shown and fired dry on the bench

`strobe_compute_schedule()` implements the design's `compute_schedule()` (.md §13.4): width
= 1 mm blur ÷ v, rounded **down** and clamped to 5–100 µs; period = 42.67 mm spacing ÷ v,
rounded to nearest; gap = period − width, at least 150 µs; 10 pulses, **shed one at a time
with the spacing kept** while the burst charge at 9 A exceeds 6.0 mC, never below 3.
The design's first-pulse delay is **not** computed: it needs t_cam and the beam-to-FOV
geometry, which belong to Phase 7.

✅ **Already verified on the host** (`tests/strobe_plan_test.c`; 11 tests when 6a ran, 16 since
the 2026-10-06 live-policy tests): every row below, the shedding, bad-speed rejection, the PIO
encoding and all 256 combinations of the dry admission policy.

**On the bench, state as 6a.1:**
```
strobe sched 90
strobe sched 50
strobe sched 20
strobe sched 10
strobe sched 2
strobe sched 1
```

| Speed | Width | Period | Gap | Pulses | Span | Charge @ 9 A | Flags |
|---|---|---|---|---|---|---|---|
| 90 m/s | 11 µs | 474 µs | 463 µs | 10 | 4277 µs | 0.99 mC | — |
| 50 m/s | 20 µs | 853 µs | 833 µs | 10 | 7697 µs | 1.80 mC | — |
| 20 m/s | 50 µs | 2134 µs | 2084 µs | 10 | 19256 µs | 4.50 mC | — |
| 10 m/s | 100 µs | 4267 µs | 4167 µs | **6 of 10** | 21435 µs | 5.40 mC | **SHED** |
| 2 m/s | 100 µs | 21335 µs | 21235 µs | **6 of 10** | 106775 µs | 5.40 mC | **CLAMPED**, **SHED** |

`strobe sched 1` → `ERR: speed must be 2.0-100.0 m/s`.

Then fire two of them dry and capture on the LA:
```
strobe sched 20 fire
strobe sched 10 fire
```

**Pass:** printed values match the table; the LA shows **10 × 50 µs at 2134 µs** and
**6 × 100 µs at 4267 µs**; both `completed, IRQ0 seen`.

> **Two assumptions are baked into the design's §15 table and neither was stated there.**
>
> - **10 pulses per burst** (hence 9 inter-pulse periods). Charge follows:
>   10 × 11 µs × 9 A = 0.99 mC ✓, 10 × 20 × 9 = 1.8 ✓, 10 × 50 × 9 = 4.5 ✓, 10 × 100 × 9 = 9.0 ✓.
> - **Transit is over 45.72 mm, not the ball's 42.67 mm diameter** (45.72/90 = 508 µs ✓ …).
>   ✅ **Source found 2026-10-02:** the extra ~3 mm is `config.beam_width_mm = 3.0f` in the
>   design's §13.1 — a placeholder optical line width marked "calibrate!", not a measurement.
>
> The design's "38 ms" burst at 10 m/s is the **unshed** 10-pulse span. After shedding,
> the burst is 6 pulses over 21.4 ms. The design keeps the spacing when it sheds, so the
> burst covers fewer freeze positions — not the same window more sparsely.

---

## 6b — Gate DAC only, still no LED bank

### Board state

| | |
|---|---|
| Firmware | same build as 6a |
| **J3 (LED bank)** | 🔴 **STILL DISCONNECTED** |
| **A7** | ✅ resolved in firmware 2026-10-02 — **verified on the bench in Step 1** |
| Rail | **up**; down while connecting probes |
| GPIO27 | **0** |
| Beam / detector | off / disarmed |
| Probes | **DMM on TP3** (Q9 gate, the DC setpoint) and **TP2** (+12 V), both to board GND. Scope on TP3 optional, for ripple. |

⚠ TP2 can exceed the logic analyser's 12 V analog limit — use the DMM or the scope there.

### Step 1 — A7: prove the ready LED and the gate DAC are independent

**State:** rail up, gate 0.

```
strobe
strobe gate 50
panel rdy 100
panel rdy 0
panel rdy auto
```

**Expect:** `strobe` → `A7 : GPIO12 ready LED on SIO (on/off) ...`. After `strobe gate 50`,
TP3 ≈ **4.88 V**. Read TP3 after each `panel rdy`.

**Pass:** TP3 **does not move** when the ready LED is switched (within DMM noise, < 5 mV). The
ready LED itself, if the panel is fitted on J7, goes on at `100` and off at `0`.
Before 2026-10-02 these two shared one compare register; this step is the bench proof that
they no longer do.

### Step 2 — the interlock, both directions

**State:** gate still at 50 %.

```
strobe pulse 20
strobe gate 0
strobe pulse 20
strobe gate 25
```

**Expect, in order:**
1. `REFUSED -- nothing fired` / `gate DAC is not at zero, or was zeroed < 20 ms ago and
   Q9's gate is still decaying (dry mode never pulses with a setpoint)` (the 2026-10-05 image
   said "this build" instead of "dry mode").
2. `gate <- level 0 of 1024 (0.00 %)`; TP3 back to ~0 V. The command waits 20 ms, so the
   decay window has already passed when it returns.
3. `completed, IRQ0 seen` — a dry pulse, gate at zero.
4. `gate <- level 256 ...` — admitted, because the engine is idle again.

**Pass:** exactly that. Then `strobe gate 0`.

### Step 3 — ramp the setpoint

**State:** rail up, starting from gate 0. One command at a time; after each, wait for the
`settled 20 ms` line and **read TP3 and TP2 on the DMM**.

```
strobe gate 0
strobe gate 10
strobe gate 20
strobe gate 30
strobe gate 50
strobe gate 70
strobe gate 90
strobe gate 100
strobe gate 0
```

The CLI prints `nominal ... -> TP3` against an assumed 3.300 V. Board 3's threshold DAC
measured an **effective reference of 3.256 V** (§3.5) on the same +3V3 rail, so expect TP3
nearer 3 × 3.256 V × duty:

| `strobe gate` | DAC level | CLI nominal TP3 | Expected TP3 (3.256 V) | Pass band (±3 % + 30 mV) |
|---|---|---|---|---|
| 0 | 0 | 0.000 V | 0.000 V | ≤ 0.030 V |
| 10 | 102 | 0.986 V | 0.973 V | 0.914–1.032 V |
| 20 | 205 | 1.982 V | 1.956 V | 1.867–2.044 V |
| 30 | 307 | 2.968 V | 2.928 V | 2.811–3.046 V |
| 50 | 512 | 4.950 V | 4.884 V | 4.707–5.061 V |
| 70 | 717 | 6.932 V | 6.840 V | 6.604–7.075 V |
| 90 | 922 | 8.914 V | 8.795 V | 8.501–9.089 V |
| 100 | 1024 | 9.900 V | 9.768 V | **record** — may flatten |

The ±3 % is gain (R59/R60, 1 %) plus reference; the 30 mV is LM358 offset (±7 mV) × 3.
At 100 % the LM358 on +12 V plus U7's V_BE may not reach 9.8 V: **record where TP3 tops
out** rather than calling it a failure.

> ✅ **Board 1, 2026-10-06:** 50 % → **4.936 V**, 100 % → **9.88 V**, TP2 **12.31 V** throughout.
> Both points give 3 × **3.29 V** × duty, so board 1's gate DAC has an effective reference of
> ≈ 3.29 V (board 3's threshold DAC measured 3.256 V — a different board and DAC). **No
> flattening at 100 %.** The other ramp points were not recorded; the two that were sit inside
> their bands and on one straight line.

**Pass:**
- TP3 within the band from 0 % to 90 %, and monotonic.
- **TP2 moves by < 0.2 V** across the whole ramp (the +12 V budget is only ~5 mA from R15).
- TP3 is a clean DC level: on the scope, ripple well under 10 mV p-p, no oscillation.
- Back at `strobe gate 0`, TP3 ≤ 30 mV.

### Step 4 — rail-down zeroes the setpoint

```
strobe gate 50
off
stat
strobe
```

Repeat `stat` until **STANDBY, latch 0**. **Expect** `strobe` → `gate DAC : level 0 ...`. Then
read `stat` (STANDBY), send `on` once, wait 3 s, and read TP3: **≤ 30 mV**. The next power-up
never starts with a setpoint.

### What you are looking for

| Symptom | Meaning |
|---|---|
| TP3 tracks 3× the DAC, smooth | ✅ the gate chain is healthy |
| TP3 moves when `panel rdy` changes | 🔴 **A7 is back** — stop; GPIO12 is driving slice 6 again |
| TP3 oscillates, or rings when the setpoint steps | 🔴 gate-loop stability problem — look at R63 and the U6A/U7 loop **before going further** |
| TP2 sagging as TP3 rises | the +12 V budget is exceeded; check R15 |
| TP3 stuck near 0 V at every setpoint | check GPIO28 is PWM (`strobe` → `GPIO28 PWM`), then R55/R58 |

**Deferred to 6c — "TP3 during bursts".** Q9's gate should stay a steady DC level while Q10
switches; ringing there on a pulse edge would be a gate-loop problem. **This build cannot run
that check**: it never pulses with a setpoint. 6c's firmware change has to add it.

> ### ✅ A7 — resolved in firmware 2026-10-02
>
> **`GPIO28` (Gate_PWM) and `GPIO12` (the panel ready LED) are the same PWM channel** —
> slice 6A on RP2350B: the same **channel**, not just the same slice, so one compare register
> drove both pins. Whichever was configured second silently took over both, so the ready-LED
> brightness could have become the 9 A current setpoint, or the reverse — presenting as an
> analog fault in the gate chain.
>
> **The fix taken:** the ready LED gave up the PWM block. `panel.c` drives GPIO12 as plain SIO
> on/off (any brightness ≥ 50 % is on), `strobe.c` owns slice 6, and `strobe_init()`
> **panics** if GPIO12 is ever found on PWM again; every gate raise re-checks it. The board
> fix, `NEXT_BOARD_REV.md` CR-01, is now optional: it buys back ready-LED dimming only.
>
> See the PWM SLICE MAP in `board.h`. Two other pairs collide but are safe as long as
> **GPIO15 (the latch) and GPIO27 (the watchdog defeat) never go on PWM.**

---

## 6c — LED bank connected, current ramp

🔴 **This is the first step where real current flows. Everything before it exists to make this
boring.** Do not start it at the end of a session.

> **Firmware (2026-10-07): LIVE mode.** `strobe live on confirm` relaxes exactly one dry rule —
> a pulse may fire with the gate setpoint non-zero — and adds its own guards (all host-tested,
> numbers in `board.h`):
>
> | Guard | Value | What it does |
> |---|---|---|
> | Arming | from **gate 0** only; rail up, no fault, no Pi, GPIO27 SIO-low, A7, beam off, detector disarmed, ADC idle | `strobe live on` alone prints the checklist and arms nothing |
> | Watchdog | 1 s, **armed by live mode**, disarmed again on exit (unless you had armed it) | `wdog off` is refused while live |
> | CLI | only `help id stat pins adc5v fault off forceoff wdog strobe` | everything else that touches the ADC, the beam, the detector, flash or a reset is refused |
> | Ceiling | gate ≤ **level 717 = 70.0 %** → TP3 ≈ 6.9 V | the design expects 4.5–6 V at 9 A |
> | **Staircase** | each raise ≤ **31 levels (3.0 %, 0.30 V at TP3, ≤ ~1.8 A)** above the highest level already **fired and measured within limits** | no typed command can jump from a measured current to an unmeasured one **while the load stays connected**; re-arming restarts it from 0. Typed 3 % steps always fit. ⚠ A firing that measures **zero** current also counts as "within limits", so with J3 open the staircase climbs with no load — see the Step 3 stop rule (gap found 2026-10-07) |
> | Readback | **every** live firing: ADC0 alone at 500 ksps (2 µs/sample), 160 µs of baseline, then the pulse(s), 200 µs after | per-pulse plateau (edge samples dropped), peak, on-time; `strobe wave` dumps the record |
> | Overcurrent | plateau **> 10.8 A** (1.2 × 9 A) or any sample **> 13.0 A** | latches **`STROBE_OVERCURRENT`**, ends live mode, gate 0 |
> | Clamp / stuck-on | current **before** the pulse (baseline > 40 codes ≈ 0.24 A), **still on** at the end of the window, or **on > 200 µs** | latches **`STROBE_CLAMP`**, ends live mode, gate 0 |
> | Pacing | ≥ **100 ms** between firings; ≤ **30 mC per 10 s**, booked before firing at 9 A × the clamp-limited width | refused, not delayed (`strobe cal` waits instead) |
> | Live burst length | enable → IRQ0 ≤ **30 ms** (the readback window) | §15 at 10 m/s and faster fits (≤ 21.4 ms); 2 m/s does not |
> | Idle | **300 s** with no `strobe` command | live mode ends itself, gate 0 |
>
> Live mode also ends on `strobe live off`, `strobe off`, `off`/`forceoff`, the button, any
> fault, a Pi appearing, a firing that times out or is aborted, or an unusable ADC0 record.
> Every command that ends it prints `*** LIVE MODE ENDED: <why> ***`.

### Board state

| | |
|---|---|
| **Firmware** | the 2026-10-07 6c build — `id` → `fw` line ends `+ 6c/6d live current (guarded, ADC0 per firing)` |
| **Prerequisites on this board** | 6a and 6b **passed on this board**, and **TP3 at a static setpoint passed** (6b exit criteria: scope or LA analog, gate 0/50/90, no oscillation) — 🔴 not optional: an oscillating gate loop with current flowing is exactly what this step must never meet first |
| **J3 (LED bank)** | **CONNECTED** — for the first time. Connect it with the **PSU and USB both off** |
| PSU | **5.20 V, current limit ~3 A.** The pulses come from the VIR bulk caps; the PSU only sees the average |
| Rail | **up** (`on`) |
| GPIO27 | **0** (`strobe` → `U5 pulse limiter armed`) |
| Beam / detector / Pi | **off** / **disarmed** / **not connected** |
| Probes — attach every one with the board **unpowered** (rule 6) | **TP4** (CurrentSense, **135 mV/A**: 0.27 V at 2 A, 1.215 V at 9 A) on the scope, 10× probe, ground on a **wire soldered to R66's GND pad** (the pad farther from Q10) — that measures straight across the sense resistors. **Not TP1**: it is 72 mm away across the boost and picked up ±70–85 mV of spikes (≈ ±0.6 A apparent on TP4) on 2026-10-07. **TP3** (Q9 gate setpoint) on a second channel, ground on the second R66 wire. All leads: the table below. The LA analog channel (±10 V) is an acceptable substitute on TP4 and TP3 — **never** on TP2 (12.3 V) or Q10's gate |
| FLIR | **on and pointed at Q9 and HS1 before the first pulse**, with tape or paint patches on Q9's tab and HS1 (bare metal reads falsely cool) |
| Nothing conductive near **D12 (36 V)** | |

#### Leads to solder for 6c/6d — all at once, PSU **and** USB off (added 2026-10-08)

Gaps are edge-to-edge copper to the nearest pad on another net, from the PCB file. Wire: 30 AWG
Kynar like the existing leads, 5–8 cm; strip ~3 mm at the board end, ~5 mm at the free end.

| # | Point | Net / use | Instrument | How to solder | Nearest other-net copper |
|---|---|---|---|---|---|
| — | **R33**, end farther from R57 | GPIO25 (commanded pulse) | Saleae D0; scope **EXT TRIG** (BNC-to-clip lead) | ✅ already on board 1 — keep | R57 GND pad 0.52 mm |
| — | **R61 pad 1** | U5 Q (after the clamp) | Saleae D1 | ✅ already on board 1 — keep; make sure it is not bent onto R65 | R65 GND 0.68 mm |
| G1, G2 | **R66 pad 1** — the pad **farther from Q10** (silkscreen R66: the 2512 beside R65, farther from R61) | **GND** = the low side of R65 ∥ R66 | G1: scope CH1 ground; G2: scope CH2 ground; Saleae and EXT TRIG grounds clip onto G1/G2 | two wires on the same pad | **4.6 mm** — the cleanest spot near the strobe |
| S1 | **TP3** (TH pad, 0.5 mm hole) | Q9 gate setpoint (U7 emitters) | scope CH2; DMM | feed the bare end **down through the hole from the top and solder on the bottom** (no parts on the bottom within 17 mm); keep the top fillet inside the 1 mm pad | 🔴 **C56 +12 V pad 0.59 mm** (top) |
| S2 | **TP4** (TH pad, 0.5 mm hole) | CurrentSense, 135 mV/A | scope CH1; optional Saleae analog | same: through the hole, soldered on the bottom | C54 GND pad 2.03 mm (top) |

Twist **S1 with G2** and **S2 with G1** from where they leave the board, so each probe's tip and
ground clip sit side by side (the residual loop is the ~14 mm across the board).

**Do NOT solder these:**
- **TP2 (+12 V)** — 1.92 mm from R15's **VIR (36 V)** pad; a bridge puts 36 V on the 12 V rail
  (U6, U7, U8). 6c/6d do not need it.
- **R65's GND pad** — 0.68 mm from R61 (U5 Q) and its existing wire.
- **R67** (the GND pad nearest TP3) — 0.85 mm from its own other pad, `PWR_LowSideD`; a bridge
  shorts Q10 drain-to-source, so current would flow whenever Q9's gate is up, **bypassing Q10,
  U5's clamp and the sense resistor**.
- Anything near **D12**. And no instrument ground on **TP1** or **TP5** — one ground point only.

**If a lead from the 6a scope session is still on R64 pad 2 (Q10 gate):** 6c/6d do not use it
(both scope channels are taken). Leave it, insulate the free end, dress it away from the board.

**Check before powering, DMM in Ω, everything still off:**

| From → to | Expect | If not |
|---|---|---|
| G1 / G2 → TP1 | ≈ 0 Ω | the ground wire is not on the GND pad |
| R61 pad 1 → GND | ≈ 1 kΩ (6a Step −1) | something bridged near R61 / R65 — stop and inspect |
| S1 (TP3) → G1 | kΩ range (R60 + R59 = 30 kΩ, lower through unpowered ICs) — **not ≈ 0** | TP3 bridged to C56's GND pad |
| S1 (TP3) → TP2 | **not ≈ 0** | 🔴 TP3 bridged to C56's +12 V pad — fix before power |
| S2 (TP4) | inspect both sides with magnification: no solder outside the pad | a TP4-to-GND bridge reads ≈ 0.135 Ω, too close to a short for a DMM, and it would **blind the firmware's overcurrent check**, which reads this node |

Not soldered: **VIR** (if needed) by DMM on J3 pin 1's screw; FLIR patches on Q9's tab and HS1
(tape or paint) — do them in the same unpowered session.

**TP4 = 135 mV/A** → 1.215 V at 9 A (0.61 V at 4.5 A single string, 2.43 V at 18 A). That is
`STROBE_SENSE_V_PER_A`. Scope: DC, 10 µs/div, **Single Seq** per firing. **100 mV/div up to ~5 A,
200 mV/div above** (on an 8-bit scope that is 4 / 8 mV per level — under 2 % of TP4 at 2 A and
< 1 % at 9 A). Read the plateau with cursors, not the Mean measurement (Mean averages the whole
record, including the off-time). Trigger on TP4 rising at ~50 mV — or on the LA's GPIO25 wire if
it is still on R33.

**Bandwidth limit: 20 MHz for every plateau reading** (measured 2026-10-08). At full bandwidth
each ~1 MHz boost edge rings the probe ground lead at ~450 MHz, ±15–40 mV on TP3 — about
±0.1–0.3 A apparent on TP4. A 20 µs pulse with ~1 µs edges loses nothing at 20 MHz. For the
**first live pulse only**, take one extra capture at full bandwidth to look at TP4's first
~150 ns for turn-on overshoot (the 6b gate-0 baseline predicts edge coupling there); pickup
rings that are also there with no pulse are not overshoot.

**With the MSO54B (available again 2026-10-08):** CH1 TP4, CH2 TP3 on the soldered leads,
grounds on the R66 leads; trigger from the GPIO25 wire on R33 into **Aux Trig** (BNC-to-clip
lead, ~1.6 V), or on CH1 rising ~50 mV with the 20 MHz limit on. The Saleae keeps GPIO25 and
U5 Q as digital timing, ground on an R66 lead.

**With a TDS 1002 + Saleae Logic Pro 8 (2026-10-07 kit):** the scope's two channels are **CH1 TP4**
and **CH2 TP3**; take the trigger from the GPIO25 wire on R33 into the scope's **EXT TRIG**
(3.3 V logic, trigger level ~1.6 V). That keeps both channels for analog and triggers before the
first edge. The Saleae keeps **GPIO25 (R33) and U5 Q (R61)** as digital timing. Optionally put a
third Saleae channel, in analog, on **TP4** too: a 12-bit record of every firing to set against
ADC0 and `strobe wave` — with that channel's ground lead on an R66 ground wire as well, or it sees
the same boost pickup. Attach everything **unpowered**; every instrument ground goes to the **R66
GND pad wires** (never TP5), with the ground leads wrapped back along the probe bodies.

### Step 1 — identity and refusals, before any current (rail up, J3 connected)

```
id
strobe
```

| Expect | Pass |
|---|---|
| `id` → the 6c `fw` line and the new `built` stamp | ✅ right image |
| `strobe` → `strobe   : DRY.` … `GPIO27 … U5 pulse limiter armed`, `A7 … on SIO`, `limits   : … U5 clamp <= 137 us (board 1 measured 135)`, `live     : OFF -- last ended: not armed since boot`, `may arm  : yes …` | ✅ all present; ❌ any `***` line — stop |

Now prove the arming guard and the CLI lock **before** anything can conduct:

```
strobe gate 3
strobe live on confirm
strobe gate 0
strobe live on
strobe live on confirm
wdog off
beam on
strobe gate 10
```

| Command | Expect | Pass |
|---|---|---|
| `strobe gate 3` | `gate <- level 31 of 1024 (3.03 %)` … `Dry pulses are now REFUSED` | dry rule unchanged |
| `strobe live on confirm` | `REFUSED: gate DAC is not at zero … Live mode NOT armed` | arming needs gate 0 |
| `strobe gate 0` | `gate <- level 0` | |
| `strobe live on` | the checklist, starting `LIVE MODE IS NOT ARMED` | nothing armed |
| `strobe live on confirm` | `LIVE: ARMED. Gate 0; the staircase starts from level 0. Watchdog ARMED (1 s).` | ✅ armed |
| `wdog off` | `REFUSED: live strobe mode is armed and requires the watchdog.` | |
| `beam on` | `REFUSED: 'beam' is not allowed while live strobe mode is armed.` | |
| `strobe gate 10` | `REFUSED: over the staircase limit …` and `Asked for level 102. Highest allowed now: level 31 (3.03 %) -- proven to 0.` | ✅ staircase works |

### Step 2 — the first live pulse, at gate 0: expect no current

```
strobe pulse 20
```

Expect (numbers vary by a few codes):
```
last     : LIVE 1 x 20 us -- completed, IRQ0 seen
           at gate level 0 (0.00 %, TP3 nominal 0.00 V); 0.18 mC booked
           ...
ADC0     : ~200 samples at 500 ksps, 76 before the edge; baseline <10 codes
           (<0.06 A); threshold ... Verdict: within limits
           no current above the ~72 mA detection floor
```
**Pass:** verdict `within limits`, `no current above`; **TP4 flat (< 10 mV)** on the scope. TP3 shows
only the ±0.3–0.4 V edge coupling already recorded in 6b. ❌ Any current at gate 0 → `strobe live
off`, `off`, and stop: Q9 is conducting with its gate at 0 V.

### Step 3 — the staircase: one 20 µs pulse per 3 % step

```
strobe gate 3
strobe pulse 20
strobe gate 6
strobe pulse 20
strobe gate 9
strobe pulse 20
   ... in 3 % steps ...
```

Each `strobe pulse 20` prints the plateau and the TP4 voltage to expect for it:
```
  pulse  1:   1.234 A plateau   peak   1.260 A   on   20 us (10 samples)   +164 us
  measured charge 0.025 mC. TP4 should read 167 mV for 1.234 A (135 mV/A):
  compare it on the scope -- the two must agree.
```

**Expect:** nothing until TP3 passes Q9's threshold — IRLR2905 V_GS(th) is 1.0–2.0 V, so gate
~10–20 %, plus the source drop. After that, current rises by **at most ~1.8 A per 3 % step** (the
guards assume ≤ ~6 A per volt at TP3; less is normal). On-time **20 µs ± 2 µs** (2 µs samples).

Record every step:

| gate % | TP3 (V) | ADC0 plateau (A) | TP4 plateau (mV) | TP4 → A (÷ 135 mV) | on-time (µs) | notes |
|---|---|---|---|---|---|---|

**Pass, per step:** verdict `within limits`; ADC0 and TP4 agree within **±5 % or ±0.1 A**, whichever
is larger; TP4's plateau flat after a rise of ≲ 1–2 µs, no ringing over ~20 % of the plateau; TP3
holds its DC level through the pulse (any dip recovers within the pulse).

🔴 **Stop the ramp at the first step that measures 0.5–2.5 A and run 6d there** (below), then
come back. The clamp is proven with current at ~2 A before anything higher is fired.

🔴 **Stop altogether** — `strobe live off`, then think — if: any `*** LIVE MODE ENDED ***`; ADC0
and TP4 disagree outside the band above; the current **jumps by more than ~2 A** in one 3 % step;
TP4 rings or oscillates; TP3 does not hold. The firmware stops at **> 10.8 A** on its own; do not
let it be the thing that stops you.

🔴 **Stop if nothing is detected by gate 30 %** (TP3 ≈ 3.0 V, well past Q9's 1.0–2.0 V threshold):
`strobe live off`, `off`, **PSU and USB off**, then check J3 and the strings. The firmware counts a
zero-current firing as "within limits", so with an open load the staircase keeps climbing, and a
connection that closes later would see the first real current at that higher level. Re-seating J3
needs the board unpowered, which ends live mode and restarts the staircase from 0 — so the
procedure is safe; an **intermittent** connection is the case this rule exists for.

### Step 4 — calibrate to 9 A

Once the staircase has reached a few amps cleanly (and 6d has passed), let the firmware solve
the level. FLIR on Q9 and HS1 throughout.

```
strobe cal 9
```

It returns the gate to 0 and climbs again with one 20 µs pulse per step: 20 levels (~2 %) while
nothing is detected, then 5 levels (~0.5 %, ≤ ~0.3 A). It aborts above **10.8 A** (1.2 × target)
and fails at the ceiling. Then it interpolates the level, sets it, and fires **3 confirmation
pulses**. It paces itself (≥ 100 ms apart) and takes ~10–20 s. Any key aborts with the gate at 0.

Expect:
```
CAL: solving the gate level for 9.00 A -- one 20 us pulse per step from gate 0,
  ...
  step     ...   48.83 %   4.82 V    8.861 A
  step     ...   49.32 %   4.87 V    9.140 A
  confirm  ...   49.12 %   4.85 V    9.012 A
  ...
SOLVED: level 503 (49.12 %, TP3 nominal 4.85 V) for 9.00 A.
  confirmation mean 9.010 A (+0.1 %) over 3 pulses: every pulse within 5 % -- PASS
  Gate left at level 503. RAM only -- nothing is saved.
```
(The level and TP3 above are illustrative; the measurement is the point.)

**Pass:** `SOLVED … PASS`; on the confirmation pulses **TP4 = 1.215 V ± 5 %**; TP3 on the DMM at
the solved level — record it, and compare it with the design's **4.5–6 V** at 9 A (outside that is a
finding, not a failure). `strobe` → `cal      : 9.00 A -> level …` keeps it until a reset.

`CAL FAILED: CEILING` means 9 A was not reached by 70 % (TP3 ≈ 6.9 V): record the highest current
and **stop** — do not raise the ceiling to get a pass. `OVERSHOOT` means one 0.5 % step jumped
past 10.8 A, far steeper than assumed: record it and stop.

### Step 5 — bursts at the solved level

The gate is at the solved level after Step 4. FLIR after **every** burst.

```
strobe burst 20 500 4
strobe sched 50 fire
strobe sched 20 fire
strobe sched 10 fire
strobe wave
```

| Command | Fires | Booked |
|---|---|---|
| `strobe burst 20 500 4` | 4 × 20 µs, 520 µs period | 0.72 mC |
| `strobe sched 50 fire` | 10 × 20 µs at 853 µs (7.7 ms) | 1.80 mC |
| `strobe sched 20 fire` | 10 × 50 µs at 2134 µs (19.3 ms) | 4.50 mC |
| `strobe sched 10 fire` | 6 × 100 µs at 4267 µs (21.4 ms) — shed from 10 | 5.40 mC |

Each prints every pulse's plateau and a `burst   : min … max … first -> last …` line. `strobe wave`
dumps the last record (the 10 m/s burst) as `# capture … # end` CSV — keep the log; it is the
plateau-across-a-burst evidence.

**Pass:** no fault; every plateau within limits; ADC0 and TP4 agree on the first and last pulse;
**record** the first → last change (the VIR bulk-cap droop — there is no design number yet, which
is why it is measured here) and Q9's and HS1's temperature after each burst. HS1 must warm with
Q9: a hot Q9 over a cold heatsink means TIM1 is not coupling.

### Step 6 — leave

```
strobe live off
off
```
Expect `live OFF: gate 0, engine stopped, GPIO25 low. Watchdog now off.` Disconnect J3 only with the
PSU and USB off.

### What to watch for

| Symptom | Meaning |
|---|---|
| Plateau **sags within a pulse** | VIR headroom exhausted — only ~9–11 V above the string Vf |
| Plateau **sags across a burst** | bulk caps depleting; shorten pulses or shed count |
| Rise slower than ~1–2 µs | harness inductance higher than expected |
| ADC0 and TP4 disagree | trust TP4; suspect the R32 path or ADC scaling |
| `*** LIVE MODE ENDED: OVERCURRENT -- fault latched ***` | a plateau over 10.8 A (or a sample over 13 A). Keep the log, `strobe wave` for the record, then `fault clear`. Do not re-arm until it is understood |
| `*** LIVE MODE ENDED: CURRENT OUTSIDE THE PULSE -- fault latched ***` | current before the edge, after the window, or on > 200 µs: **Q10 or U5 is not ending the pulse.** `off`, power down, and check GPIO27/Q8/U5/Q10 before anything else |
| `*** LIVE MODE ENDED: ADC0 readback unusable ***` | no usable record, or **more current pulses than were fired** (a U5 retrigger, ringing, or an oscillating gate loop splitting the plateau). Nothing is assumed safe without a reading, and the staircase does not move. `strobe wave` shows which |
| `REFUSED: too soon after the last live firing` / `rolling charge budget is used up` | pacing, by design: wait 0.1 s / up to 10 s |

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

**Run it inside 6c Step 3, at the first step that measures 0.5–2.5 A** — before the ramp goes
any higher.

### Board state

| | |
|---|---|
| **Live mode** | armed; gate at the **first level whose live pulse measured 0.5–2.5 A** (ideally ~2 A) |
| J3 | connected |
| GPIO27 | **0** — the point of the test is that the *hardware* clamp works |
| Probes | TP4 on the scope at **50 µs/div** (the pulse should be ~135 µs, not 1 ms) |

### Step

The firmware admits a live clamp test **only at the gate level of the most recent live firing,
and only if that firing measured 0.5–2.5 A** — so fire a 20 µs pulse at this level first (Step 3
already did) and then the clamp test, without touching the gate in between:

```
strobe pulse 20
strobe clamptest 1000
```

Expect:
```
6d CLAMP TEST WITH CURRENT: commanding 1000 us at gate level ... (... %).
  TP4 must show a pulse of about the U5 clamp (135 us on board 1), NOT
  1000 us. An on-time over 200 us latches STROBE_CLAMP and ends live mode.
last     : LIVE 1 x 1000 us -- completed, IRQ0 seen
  ...
  pulse  1:   2.0xx A plateau   ...   on  134-136 us (67-68 samples)   ...
```

**Pass:** ADC0 on-time **≈ the U5 clamp** (135 µs on board 1, ± 2 µs sampling) and **TP4 on the
scope shows the same width, not 1 ms**; the plateau matches the 20 µs pulse's; no fault.

🔴 **If TP4 shows ~1 ms, or the firmware prints `*** LIVE MODE ENDED: CURRENT OUTSIDE THE PULSE ***`,
stop immediately: `off`, power down.** The hardware clamp is not working, and every later step
assumes it is. (The firmware's 200 µs limit is a backstop for exactly this; at ~2 A a 1 ms pulse is
~2 mC and harmless.)

**Record the truncated width. Then never do this again** outside this guarded mode: a live
clamp test anywhere but a measured 0.5–2.5 A level is refused (`REFUSED: clamptest needs a live
firing at THIS gate level that measured 0.5-2.5 A`).

---

## Exit criteria

- [x] **6a/6b firmware written** (2026-10-02) — pulse command, PIO burst engine loaded and
      DMA-fed, schedule computation, `BURST_CHARGE_MAX_MC` interlock, gate DAC; host-tested.
      **Bench verification is the rest of this list.**
- [x] **U5 clamp measured**, `STROBE_SW_MAX_US` confirmed or corrected against it, §15 table
      re-derived only if U5 comes in below 100 µs — **board 1, 2026-10-06: 135 µs**, the same at
      200 and 1000 µs commanded, 6 fires, on the LA at U5 Q (R61). Margin to `STROBE_SW_MAX_US`
      100: **35 µs** → 100 kept; §15 table unaffected. Constant update pending (PROGRESS §6
      2026-10-06). Other boards: re-measure — this RC varies ~10 % board to board (U9 on
      board 1: 122.68 µs).
- [x] Commanded widths reproduce at Q10's gate, with a ~12 V level and clean edges —
      **board 1, 2026-10-06 (scope, `strobe pulse 20`):** U5 Q (R61) high for **20.000 µs**;
      Q10's gate follows it with **≈ 75 ns** rise delay and **≈ 80 ns** fall delay (50 % points),
      **10–90 % rise ≈ 55 ns, fall ≈ 45 ns**, so the gate pulse is the commanded width **+ ≈ 7 ns**.
      Gate high **≈ 12 V** (scope 11.9 → 12.8 V across the pulse — an uncompensated-probe creep;
      the DC truth is U8's VDD, TP2 = 12.31 V on the DMM). Monotonic, no double edges. R61 itself
      rings ~1 V at ~100 MHz for ~40 ns at both edges — most likely the probe's ground lead and the
      soldered wire; it does not reach the gate.
- [x] TP3 = 3 × DAC, smooth DC, TP2 holds — **board 1:** 50 % → **4.936 V**, 100 % → **9.88 V**
      (3 × an effective 3.29 V reference, linear to full scale, no flattening); TP2 **12.31 V**
      throughout. ✅ **Loop stability PASS, board 1, 2026-10-08** (MSO54B, soldered leads,
      ground on R66; result paragraph below). **The check (kept for other boards):** TP3 at a
      static setpoint — the loop stability check. **Scope
      preferred** (owner asked 2026-10-07): it reads < 10 mV p-p directly (the LA's ~4.9 mV/LSB
      cannot) and it sees a > 5 MHz parasitic in the follower, which the LA's ~5 MHz analog
      bandwidth cannot. **Scope route** (written for the **TDS 1002**, the scope on hand
      2026-10-07: 60 MHz, 1 GS/s, 8-bit, 2500 points, 2 channels, FFT under MATH): dry mode,
      J3 disconnected. Probe switch on **10×** and CH1 → Probe **10×**; run **PROBE CHECK** on
      the PROBE COMP terminal first and trim for a flat square wave; then take the CH2 probe
      **off** PROBE COMP. 🔴 **Ground: short wires soldered to R66's GND pad** — R66 is the 2512
      sense resistor paralleled with R65 and farther from R61; use its pad **farther from Q10** (the
      pad toward Q10 is CurrentSense) — **not TP1**, and **not R65** (R65's GND pad is 0.68 mm
      from R61, U5 Q; corrected 2026-10-08). TP1 is **72 mm** from TP3 with the LM5157 boost (U1,
      L1, D2) between them, and that loop picked up ±70–85 mV of boost spikes on the first attempt
      (below). R66's GND pad has no other-net copper within 4.6 mm; it is 14.4 mm from TP3,
      13.2 mm from TP4 and 40 mm from D12, and it is the right reference for TP4 in 6c (the low
      side of the sense resistors). Leads: see "Leads to solder for 6c/6d" in 6c's board state.
      Solder the wire and attach the probe with the board **unpowered**; wrap the probe's ground
      lead back along the probe body so the loop is as small as possible; ground **never on TP5**
      (`Strobe_GND`) — the scope's ground is mains earth. Rail up. CH1 **AC-coupled, 20 mV/div**
      (the minimum with a 10× probe: ~0.8 mV per level), **BW limit on** (the readout shows
      `Bw`; 20 MHz, far above the LM358 loop's < ~1 MHz, and it trims switching edges); trigger
      **Auto**, source CH1. **First a control capture:** probe tip **on the ground wire too**, at
      gate 0, 2.5 ms/div and 25 µs/div — whatever shows there is pickup, not TP3. Then tip on TP3
      at `strobe gate 0` (the reference), **50**, **90** and **100**, using **the same timebases
      at every gate**: **2.5 ms/div in Peak Detect** (so a MHz signal cannot alias away), then
      **25 µs/div** and **100 ns/div in Sample**. Read **Pk-Pk, Min, Max**; ignore **Cyc RMS**
      (the TDS 1000 has no plain RMS, and `?` means it found no cycle). **FFT at 25 µs/div**
      (MATH → Operation FFT, Source CH1, Window **Hanning**): 10 MS/s → 0–5 MHz, ~5 kHz bins,
      which covers the loop. Not at 1 GS/s: that spans 0–500 MHz in ~0.5 MHz bins and puts the
      whole loop band in the first bin. One extra 100 ns/div capture at gate 90 with BW limit
      **off** closes the > 20 MHz follower-parasitic case. Photograph each screen. **Pass:** the
      control capture shows the pickup floor; at 50/90/100 the band between any spikes, and the
      Pk-Pk, match gate 0 within a few mV; no sine and no FFT line that appears or grows with the
      setpoint. An oscillation of this loop would not be subtle: it grows until slew rate or
      output swing limits it (an LM358 at 0.3 V/µs allows ~1 V p-p at 100 kHz), so a sustained
      sine of tens to hundreds of mV that is absent at gate 0 → stop and send the photos. (A
      higher-end scope works the same way at 10 mV/div.)

      **First attempt, board 1, 2026-10-07 (TDS 1002, ground on TP1, BW limit on, FFT at
      1 GS/s):** the same bursty spikes at **all four** gates — Pk-Pk at 2.5 ms/div **143 / 160 /
      155 / 155 mV** (gate 0 / 50 / 90 / 100), ±70–85 mV peaks; between spikes a band of
      ~8–10 mV p-p at every gate; at 100 ns/div 14–26 mV p-p. Nothing grows with the setpoint;
      no sine at any timebase. Gate 0 (PWM static low, follower idle) and gate 100 (PWM static
      high) look the same as 50 and 90, so the spikes are neither the loop nor the DAC PWM —
      they are boost pickup in the 72 mm ground loop, and they are why the readings jumped
      (Pk-Pk depends on whether a burst lands in the record). **Reading: no sustained
      oscillation, but < 10 mV is not certified** — redo with the R66 ground above. A beaded
      ~12 µs pattern in the band at 25 µs/div (clearest at gate 90, also faintly at 50 and 100)
      is most likely the ~1.08 MHz boost beating against the display's 1 µs columns; the gate-0
      reference was taken at 50 µs/div, so the redo compares like with like. The ground on TP1
      was my instruction — I did not check where TP1 is; logged in `PROGRESS.md` §6.

      ✅ **Redo, board 1, 2026-10-08 — PASS** (MSO54B, TPP1000 10× on the soldered TP3 lead,
      ground on the R66 lead, AC, 20 mV/div, **full 500 MHz**, trigger CH1 rising 3.2 mV Auto;
      mean over 140–2815 acquisitions each; 14 photos in the owner's Downloads
      `Photos-1-001 (6)`):

      | gate | AC RMS, 200 µs/div (2 ms) | AC RMS, 2 µs/div | Pk-Pk, 200 µs/div |
      |---|---|---|---|
      | 0 | **3.80 mV** | 3.74 mV | 194 mV |
      | 50 | **3.60 mV** | 3.78 mV | 108 mV |
      | 90 | **3.72 mV** | 3.36 mV | 201 mV |
      | 100 | **3.82 mV** | 3.66 mV | 117 mV |

      - **The RMS is flat with the setpoint.** Even allowing ±0.3 mV of scatter per reading, a
        setpoint-dependent component is **< 2.5 mV rms (≈ 7 mV p-p as a sine)** anywhere in
        ~500 Hz–500 MHz, and none is visible. An oscillation of this loop would be ~0.1–1 V p-p
        (slew- or swing-limited) and absent at gate 0.
      - **Pk-Pk is all pickup.** It does not trend with the setpoint (gate 0 is among the highest),
        and the `Clipping` flag on every capture is the occasional spike past ±100 mV.
      - **What the noise is:** at 2 µs/div, spikes on a regular **~1.0 µs spacing**, the LM5157
        boost's ~1.05 MHz switching. At 10 ns/div each spike is a damped **~450 MHz ring**, the
        probe ground lead's resonance with the probe's 3.9 pF, excited by switching edges and
        visible because the scope ran at full bandwidth. Same at every gate.
      - **FFTs** (gate 0 and 90): taken at 2 µs/div (0–3.1 GHz, 50 kHz bins), so the loop band is
        in the leftmost pixels; it looks the same in both. The gate-90 FFT alone has a narrow
        line at **~850 MHz**: the cellular uplink band. It is most likely the phone taking the
        photos — beyond the scope's bandwidth, beyond anything U6/U7 can oscillate at, and Q9
        carries no current with J3 open.
      - **No control capture** (tip on the ground lead) was taken; gate 0 served as the
        reference, which is what the verdict rests on.

      The DAC ripple itself is
      µV after the two RC poles. What this checks is that the U6A + U7 loop is **stable** driving
      Q9's ~1.7 nF gate through R63 47 Ω. The owner's DMM showed TP3 steady to ±1 mV at the
      tested setpoints (2026-10-06). That is consistent with a stable loop, but it is not proof:
      a DMM in DC volts averages over ~100 ms, so a kHz–MHz oscillation centred on the setpoint
      reads as a steady number. **Logic-analyser route (accepted 2026-10-06; the fallback):**
      analog channel on TP3,
      ground on the R66 GND pad wire (not TP1 — see above), analog-only captures at gate **0** (reference: loop idle), **50** and **90**
      (not 100: 9.88 V is at the edge of the LA's ±10 V range). Take 50 MS/s × 20 ms and
      ~1–2 MS/s × 1 s at each. Pass: Vpp/RMS within a few mV of the gate-0 reference, and no
      spectral line above it (12-bit over ±10 V is ~4.9 mV/LSB, so it is the comparison and the
      spectrum that decide, not the raw Vpp). It covers the LM358-loop mode (< ~1 MHz, inside
      the ~5 MHz analog bandwidth) but not a > 5 MHz parasitic, which 6c's first pulse covers on
      TP3/TP4. **Never put the LA on TP2 (12.31 V) or Q10's gate (up to 12.6 V).**
- [x] PIO burst patterns verified on the LA, 1 µs granularity, IRQ on completion; PIO
      overhead constants confirmed or corrected — **board 1, 2026-10-06:** 5 single pulses and 4
      bursts all `completed, IRQ0 seen`; the owner reports the LA matched every width and period,
      so **no overhead correction** (2 / 8 µs kept). The LA files were not archived.
- [x] Energy interlock sheds pulses at 10 m/s — ✅ host-tested; `strobe sched 10 fire` →
      **6 × 100 µs at 4267 µs**, `SHED 10 -> 6` (board 1, 2026-10-06)
- [x] **A7 resolved** — ✅ in firmware; TP3 held at gate 50 % through `panel rdy 100 / 0 / auto`
      (board 1, 2026-10-06, owner-reported)
- [x] **6c firmware change** — pulses with a setpoint, under guards; watchdog armed; ADC0 per
      firing — **written 2026-10-07** (LIVE mode: `strobe live on confirm`, staircase, ceiling,
      overcurrent and stuck-on faults, pacing, `strobe cal`, `strobe wave`; 78/78 host tests).
      **Bench verification is the items below.**
- [ ] First live pulses: gate 0 → no current; 3 % staircase with ADC0 and TP4 agreeing (6c Steps 1–3)
- [ ] **6d: U5 clamps with current** — `strobe clamptest 1000` at ~2 A ends at the clamp on TP4
- [ ] Strobe current LUT built (`strobe cal 9` SOLVED, confirmation within 5 %), ADC0 agrees with TP4
- [ ] Bursts at 9 A: plateau first → last recorded; no fault (6c Step 5)
- [ ] TP3 steady with no ringing during bursts (6c Step 5). **Baseline at gate 0
      (board 1, 2026-10-06):** Q10's edges couple into TP3 — ±0.3–0.4 V for ~150 ns at each
      edge, and a **−0.3 V step after turn-off that recovers in ≈ 5 µs**. Most likely path: Q10's
      Cgd → the shared Q9-source / Q10-drain node → Q9's large Cgs → TP3, with U7 (an unbiased
      push-pull follower) sitting in its crossover dead-band at 0 V, inside a slow LM358 loop. At
      a setpoint U7 conducts and should hold TP3 much more stiffly. **Compare against this in 6c,
      on TP3 and TP4.**
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

**6c's live mode (2026-10-07)** selects BURST around every live firing (160 µs before the edge
to 200 µs after), freezes the ring, copies ch0 and returns to IDLE. It is a bench path, not
the firing sequencer: arming it **requires the detector disarmed**, so there is no detect or
mic history to lose. Phase 7 wires the strobe into `shot.c`'s `SHOT_FIRING`, which is still
the only place the production path may select BURST.

### 2. Launching core 1 changes what `cfg_save()` has to do

`pico_multicore` is deliberately not linked today, so `flash_safe_execute()` takes its
single-core path. **The moment Phase 6 launches core 1, `flash_safe_execute_core_init()`
must be called on it.** Without that, core 1 executes XIP during a flash erase, which is a
hard fault rather than a corrupted write.

Also note `cfg_save()` refuses while the beam is on, the detector is armed, or the state is
not STANDBY/BENCH_RUNNING — a 4 KB erase blinds the `V5_MIN_SUSTAINED` monitor for tens of
milliseconds. **Consequence: calibration cannot be persisted between shots**, only at the
end of a session. That is the right trade, but plan the session around it.
