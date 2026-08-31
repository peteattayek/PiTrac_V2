# New-board bring-up — step by step

# 🔵 THIS DOCUMENT IS THE DRIVER FOR A NEW BOARD. Work top to bottom.

**Use this when you have a freshly assembled board and firmware already flashed.** Not the same
as `START_HERE.md`, which assumes you have never built or flashed anything.

**Budget about two hours**, most of it waiting: two 5-minute beam warm-ups and a 128 s model
fit. Do not skip ahead — **§4 and §5 are safety gates**, and one of them protects a Pi.

---

## The one distinction that organises everything

Work on this project splits into two kinds, and confusing them wastes bench time in both
directions — redoing settled physics, or trusting a constant that is actually per-board.

| | **DESIGN VALIDATION — done ONCE, ever** | **PER-BOARD — done EVERY time** |
|---|---|---|
| **asks** | *how does this design behave?* | *does THIS board work, and what are ITS constants?* |
| **lives in** | `BENCH*.md` + the record in `PROGRESS.md` §6 | **this document** |
| **redo when** | the design changes (a respin, a firmware change) | every assembled board |

**Everything in this document is per-board.** If a step here is ever found to be design-level,
it belongs in a `BENCH*.md` section and should be deleted from here.

### Already settled — do NOT redo these on a new board

| | established |
|---|---|
| GPIO33 polarity: **TRACK is LOW** | TMUX1219 truth table + 4 independent bench confirmations |
| **Carrier = 104.1667 kHz, fixed for every board** | design decision 2026-08-28, `PROGRESS.md` §8 |
| PWM slice collisions, PIO `GPIOBASE` allocation | `ARCHITECTURE.md` A2/A7 |
| Q1 mechanism (U9 clamp ≈ K·R·C), Q2 (no duty limit to 250 kHz) | Phase 2c / 2d |
| Ambient rejection ≈ **48 dB**; the lock-in premise | Phase 3.3, board 2 |
| The "bursty noise" was **beam return off the room**, not a fault | Phase 3.3, board 2 |
| Netlist-derived corner frequencies | `HARDWARE_REFERENCE.md` |
| The full Pi soft-shutdown FSM matrix | Phase 1b, simulated Pi |
| The firmware itself | — |

### Must be measured on every board

| | why it varies | § |
|---|---|---|
| Rail integrity, current draw | assembly | 2 |
| **LM5157 boost SW frequency** | part tolerance; the carrier's ±8 % margin has to cover it | 2 |
| E9 `pins` check | different die | 3 |
| **ADC +5 V scale** 🔴 safety gate | ADC gain + divider tolerance | 4 |
| Latch guard + sustained supply monitor 🔴 | it is what stops a Pi being fed through a 1 A diode | 5 |
| Beam thermals | *this* heatsink mounting — board 1 87.5 °C, board 2 98 °C | 6 |
| **U9 clamp width** | 109–136 µs band from 1 % R / 10 % C | 6 |
| Static detect-chain health | assembly | 7 |
| `hpf test` polarity + **switch leakage** | leakage spans 52–250 pA across boards | 7 |
| CR-15 baffle acceptance | optics and operator discipline | 8 |
| **`cal demod` + `cal model`** | chain delay differs 340–600 ns board to board | 9 |
| **Threshold DAC vref + comparator offset** | LM393 Vos is ±15 mV per part | 10 |

---

## 0. Which board is this? Record the TIA variant FIRST

🔴 **Do this before anything is energised, and write it in the sign-off table.** Nearly every
number in `BENCH_P3_DETECT.md` scales with the TIA feedback resistor, and a reworked board that
is bench-tested against stock numbers will look broken when it is fine — or fine when it is
broken.

- [ ] **Read R80 and its parallel neighbours under magnification.** Note any rework.
- [ ] **Read C68 / C70 and note any added capacitor, and WHICH LEG it bridges.**

| | Rf | Cf | TIA pole | lag at 104 kHz | servo corner |
|---|---|---|---|---|---|
| **stock** | 470 kΩ | 0.500 pF | 677.3 kHz | 8.74° | 2.267 Hz (τ 70 ms) |
| **board 3** (154 kΩ ∥ R80, 100 pF on one Cf leg) | **116.0 kΩ** | **0.990 pF** | **1.386 MHz** | **4.30°** | **0.559 Hz (τ 285 ms)** |

⚠ **C68 and C70 are in SERIES.** A cap across *one* leg shorts that leg and leaves the other
1 pF (Cf ≈ 0.99 pF). A cap across *the pair* gives 100.5 pF and a **13.7 kHz** pole — below the
carrier. Those two cases differ by 100× and only one of them works, so identify the pads rather
than assuming.

⚠ **The reworked figures above are CALCULATED.** §9 has the three commands that check them.

---

## 1. Before anything is energised

- [ ] 🔴 **SET THE PSU CURRENT LIMIT TO 2 A BEFORE ANYTHING ELSE.** See the box below.
- [ ] USB-C to **J6** only. **No bench supply. No Pi. J2 jumper OFF.**
- [ ] Firmware flashed (`SETUP.md`). A blank RP2354 enumerates as `RPI-RP2` on its own —
      the SW1/SW2 dance is only needed once there is an image to interrupt.
- [ ] Yellow LED **D5** blinking slowly, ~once per 2 s = standby, healthy.
- [ ] CLI responds. Press Enter once if the banner has scrolled past.

```
id
```

- [ ] `sysclk` = 150000000 Hz, board reads `pitrac_ltb_v1`.
- [ ] Note the **UID** — write it in the sign-off table at the bottom.

---

> ### 🔴 PSU current limit — 2 A, and check it every session
>
> **Cost a full bench session on 2026-08-18.** VIR's bulk capacitance needs **amps** for a few
> milliseconds at startup (~100 uF charged to 36 V is ~65 mJ). Against a low limit the supply
> drops into CC, **the boost never finishes its soft-start**, and it parks at a lower voltage.
>
> **What makes it vicious is that everything looks fine afterwards.** Parked at 27 V the steady
> draw is tiny — the 12 V shunt pulls only (27-12)/4K7 = 3.2 mA — so the supply sits far below
> its limit, **+5V_IN reads a healthy 5.2 V**, the CLI works, the LED works and the firmware's
> supply monitor is perfectly happy. The fault exists only during a startup transient nobody
> observes.
>
> **Symptom to recognise: VIR low (e.g. 27 V instead of 36 V) AND idle current low
> (e.g. 80 mA instead of ~129 mA) at the same time.** Those two travel together, because a
> lower VIR means the shunt draws less. If you see both, **suspect the supply before the board.**
>
> ⚠ The firmware cannot detect this — **there is no VIR sense and PGOOD is unconnected**
> (`NEXT_BOARD_REV.md` **CR-08**). This incident is the concrete argument for that change.

## 2. Dead-board rail smoke test (Phase 0.5)

**Do this before the latch is ever closed.** `safe_state_init()` holds GPIO15 low so the
latch is open and nothing is energised until you ask — that is exactly the window this test
lives in.

- [ ] Fit the **J2 jumper**, apply the bench supply, and confirm the rails come up without
      the firmware being involved: **+5 V**, **VIR 36 V**, **TP2 +12 V**, **+5VA**, **TP6 +2V5**.
- [ ] Current draw sane, nothing warm, nothing smells.
- [ ] **Scope the LM5157 SW node.** Decide the R11/C9 snubber (DNP by default — fit only if it
      rings), **and record the switching frequency**.
- [ ] Remove J2 again before continuing.

> ### 🟡 Record the boost SW frequency while the scope is on the node.
>
> ⚠ **Downgraded from 🔴 on 2026-08-31.** The concern was that an odd harmonic of the fixed
> 104.1667 kHz carrier could fold the boost into the 15.39 kHz passband and look like a ball.
> **That has now been tested directly and it does not happen:** `scan carrier 95000 115000 9`
> on board 3 put the 7th harmonic **inside** the measured switcher band at three separate
> points and σ_noise stayed flat (4.57–5.08, 10.8 % spread). The coupling is too weak to matter.
>
> ⚠ **The design-time margin quoted here was also wrong.** It assumed a 1.055 MHz nominal; the
> only switcher tone found on the +5 V rail is at **801 kHz**, which would have made the margin
> **−2.3 %**, not −9.6 %. Both numbers are now superseded by the direct measurement above.
>
> 🔵 **Still worth writing down**, because it is free while the scope is there, and it is the
> only way to settle whether that 801 kHz is the LM5157 (L1) or the RP2350's own core buck
> (L2 → U3.63 `VREG_LX`). See `BENCH_P3_DETECT.md` §3.6 Check 1 for the exact pad.
>
> 🔵 If the spread across boards approaches ±7 %, that is a **design** finding, not a board
> finding — see `BENCH_P3_DETECT.md` §3.6.

🔴 **STOP** if any rail is wrong or current is high. A short found here is cheap; the same
short found by closing the latch is not.

---

## 3. E9 check — free, and it is a different die

```
pins
```

- [ ] **GPIO24, GPIO0, GPIO8, GPIO9 all read 0** with nothing connected.

Board 1 was silicon revision **A4**, a later stepping than the A2 that erratum E9 was written
against. A new chip could differ, and E9 (input + pull-down latching around 2.2 V) is exactly
the failure that would make `PI_3V3_SENSE` lie about whether a Pi is present.

🔴 **STOP** if any reads 1 — external pull-downs would be needed and `PROGRESS.md` Q5 reopens.

---

## 4. 🔴 ADC +5 V scale — THE SAFETY GATE

**Do this before typing `on` with anything valuable attached.**

The +5 V read path is ~5.9 % low, and the error is **inside the ADC** — gain error plus
sample-and-hold settling through the 50 kΩ R46/R47 divider (`PROGRESS.md` Q9). **That varies
chip to chip.** The compile-time scale is 1.063; board 1 trimmed to 1.0627, but that is one
sample.

With the **bench supply connected**:

```
adc5v
```

- [ ] Reports **≈ 5.20 V** and says the latch is permitted.

**Both directions of error are dangerous, for different reasons:**

| symptom | consequence |
|---|---|
| Reads **low** | A good 5.2 V supply looks < 5.05 V, the board refuses to latch. Obvious, harmless. |
| Reads **high** | USB's actual 4.85 V could read above the 5.05 V guard and **the board latches on USB power** — the +5 V rail back-fed through **D8, a 1 A SS14**, with a Pi on the header. This is the hazard the guard exists to prevent. |

If it is off, trim against a DMM at J1 and persist it:

```
adc5vcal <volts measured with the DMM>
cfg save
```

- [ ] `cfg` now reports `source: slot A` rather than `defaults`.

⚠ The new board's config block is **empty**. Nothing persists until `cfg save`.

---

## 5. Latch guard and supply monitor (Phase 1)

Both of these exist because of live bench finds. Do not take them on trust from board 1.

**Test 1 — refuses to latch on USB.** Drop J1 power, keep USB connected, press the button or
type `on`.

- [ ] Red LED fast-blink; `stat` shows `FAULT` / `USB_POWER_ONLY`; **+5 V stays 0 V at J8.2**;
      VIR stays 0 V.

**Test 2 — latches on real power.** Restore the PSU, `fault clear`, then `on`.

- [ ] +5 V up within 250 ms; VIR reaches 36 V; TP2 = 12 V; `stat` = `BENCH_RUNNING`.

**Test 3 — unlatches.** Press again.

- [ ] +5 V → 0 V; CLI stays responsive (we live on +3V3).

**Test 4 — fails safe through reset.** While latched, press **SW2 (RUN)**.

- [ ] **+5 V drops and stays down.** Scope GPIO15 *and* +5 V through the reset — you are
      looking for the **absence** of any glitch high.

**Test 5 — supply pulled *while* latched.** Added 2026-07-30 after a bench find; the other
four only check the guard at the *moment* of latching.

- [ ] Pull the PSU with the latch closed → `FAULT_SUPPLY_LOST` within ~500 ms, latch drops.

🔴 **STOP** on any failure. Without test 1 and test 5 a Pi can be fed through a 1 A diode.

---

## 6. Beam sanity (abbreviated Phase 2)

You do not need the full carrier characterisation again — phase lock, Q1 and Q2 are design
properties. You do need to know **this** board's assembly is sound.

```
beam
```

- [ ] Hardware readback: both slices **ENABLED**, funcsel = **4 (PWM)** on GPIO31/39,
      **pad ISO = 0** on both.

```
beam duty 2
beam on
```

- [ ] Carrier visible at **TP5**, period 9.6 µs.
- [ ] Ramp gently — `beam ramp 25 500` — and **watch the temperature the whole way**.

⚠ **Do not inherit board 1's thermal numbers.** CR-12 measured the junction at 123–133 °C at
30 % duty against a 145 °C maximum, and that depends on *this* board's heatsink mounting,
paste and airflow. **25 % is the sustainable operating point** (~87.5 °C on board 1).

- [ ] Scope **TP5** and note the U9 one-shot clamp width. Board 1 measured **122.68 µs**;
      R68/C57 tolerance means yours will differ. It gates nothing — `STROBE_SW_MAX_US` is
      100 µs, below any plausible value — but record it.

```
beam off
```

---

## 7. Detect chain sanity

Beam off, rail up:

```
adc 2 256
```

- [ ] ≈ **3212 codes (2.59 V)**. Also check **TP6, TP7, TP9, TP10 all at 2.59 V** on a DMM.
      All four agreeing is the pass criterion, not the absolute number.

**Beam OFF, and leave it off.** The mechanism under test is purely electrical — U14's switch
leakage integrating on C81 — and the beam contributes nothing to it. Turning the beam on
*breaks* the test: in HOLD, C81 freezes the demodulated DC, and the LED's 25–45 % warm-up
falloff then appears at ADC5 through U12B's ×14.5 as a monotonic drift across the 32 s run.
That trips `order_effect` every time.

Three conditions, all of which the failed runs violated:

| condition | why |
|---|---|
| **Photodiode shaded** (or lights out) | see the note below |
| **Board at a steady temperature** | switch leakage roughly doubles per 10 °C. A board *cooling* from a beam run has monotonically falling leakage, so repeat 2 disagrees with repeat 1 → `order_effect`. Any steady temperature is fine; a changing one is not. Wait a few minutes after any beam work |
| **Hands clear for the full ~35 s** | this is the one that actually bit us — see below |

⚠ **The requirement is stationarity, not darkness.** The test means-averages over a 3 s
window containing ~360 cycles of 120 Hz, so *steady* mains flicker largely averages out. What
it cannot reject is ambient that **changes between windows** — and on 2026-08-17 the culprit
was the operator's own hand reflecting 850 nm back into D12, the same confound that later
poisoned the CR-15 baffle comparisons. Shading is simply the most reliable way to guarantee a
scene that does not move.

```
hpf test
```

Blocking for **~32 s** — 4 × (5 s settle + 3 s sample).

- [ ] Reports **CONFIRMED, GPIO33 = 0 is TRACK.**

⚠ **Judge it on the repeats, not the ratio.** The printed separation is *not* a board
property: two correct boards gave **6×** and **2.9×**, because HOLD is an integrator whose
displacement depends on dwell time and temperature. What a good result looks like is the two
visits to one level agreeing closely while the levels differ a lot. Board 2, 2026-08-21:

```
  level     rep1 mean   rep2 mean    rep1 p-p   rep2 p-p
  GPIO33=0    +0.0093 V   +0.0092 V     0.0024     0.0008
  GPIO33=1    +0.0271 V   +0.0269 V     0.0089     0.0089
```

**0.1 and 0.2 mV within a level against 17.8 mV between them.** Compare the runs that failed:
1.2× and 1.4×, both ORDER-DEPENDENT.

```
cfg save
```

- [ ] `cfg` shows `hpf sel: TRACK = 0`.

**Free data point while you are here:** HOLD's p-p over the 3 s window gives the switch
leakage. Board 2's 8.9 mV → ≤2.3 mV/s at ADC5 → ~52 pA into C81; board 1 was ~250 pA. Record
it — it is the number that says how long ARMED stays usable (~43 s to walk 100 mV at board 2's
rate, against a 1–10 ms transit, so it never binds in practice).

🔴 If it reports **INVERTED**, do **not** simply flip the constant — the TMUX1219 truth table
says SEL = 0 selects S1 (the R96/GND leg). Two sources disagreeing means one is being
misread. See `PROGRESS.md` §6.

---

## 8. CR-15 — the baffle acceptance test

**The question this section used to ask is now answered.** Both boards saturated within one
duty step of each other (board 1 at ~3 %, board 2 at ~2 %), which established CR-15 as a
**design** property rather than an assembly fault; and better baffling plus keeping hands out
of the beam then removed it entirely. **The coupling was optical.**

So this is no longer a comparison between boards — **it is an acceptance test for *this*
board's baffling**, and it must be re-run on every build because the baffle is mechanical.

⚠ **Two confounds will invalidate the result if you let them.** Both cost a bench session:

- **Your hand in front of the board.** 850 nm reflects off skin straight back into D12. The
  no-baffle control moved 339 → 19 codes between sessions on nothing but this. Stand clear.
- **Baffle material.** Most black plastics are near-transparent at 850 nm. Carbon-black paint
  and tape-wrapped foil both worked; "it looks black" proves nothing.

Run the identical sweep:

```
beam on
beam duty 2   → capture 0x04 400 500000
beam duty 3   → capture 0x04 400 500000
beam duty 4   → capture 0x04 400 500000
beam duty 6   → capture 0x04 400 500000
beam duty 8   → capture 0x04 400 500000
beam off
```

**Pass criterion: no sample at 4095 and none below ~20, all the way to 25 % duty.**

Board 2 with good baffles and hands clear, 2026-08-19 — this is the target:

| duty | min | max | swing | |
|---|---|---|---|---|
| 2 % | 3084 | 3253 | 169 codes = 136 mV | linear |
| 4 % | 2750 | 3285 | 535 = 431 mV | linear |
| 8 % | 2373 | 3374 | 1001 = 807 mV | linear |
| 12 % | 1891 | 3418 | 1527 = 1231 mV | linear |
| **25 %** | **2076** | **3606** | **1530 = 1233 mV** | **linear — this is the operating point** |

**The excursion stops growing after 12 %** (1527 → 1530 codes) because the pulse has settled
past the 235 ns TIA τ and amplitude is then set by peak photocurrent, which Phase 2 showed is
flat vs duty. Seeing that plateau is a good sign the sweep is real.

For contrast, the **failing** shape — board 1, and board 2 before baffling:

| duty | board 1 TIA_Out | |
|---|---|---|
| 2 % | 597 … 3709 | linear |
| **3 %** | 98 … 3791 | **linear, marginal** |
| 4 % | 5 … 3798 | bottom railed |
| 6 %, 8 % | 4 … 4095 | both rails |

⚠ **Undersampling makes the low-duty rows untrustworthy.** 500 ksps ÷ 104.1667 kHz is exactly
4.8, so only **24 distinct carrier phases** are ever sampled, and the capture's start phase
varies run to run. At 2–8 % the pulse (192–768 ns) is far shorter than the 2 µs sample
interval, so the recorded minimum is largely luck. **Judge the board at 12 % and 25 %**, where
the settled pulse exceeds the sample interval.

**Worst case, if you want it:** repeat with the overhead lights on and the photodiode aimed at
them. Board 2 gave min 1993 / max 3573 — **no saturation, and within 5 % of the dark result at
12 % and 25 %.** The DC operating point barely moved (2.869 V vs 2.876 V), which is the real
tell: the servo is absorbing that ambient with range to spare.

⚠ **If you test optically, use an insulated opaque cover — cardboard, or foil wrapped in
tape.** Bare foil over D12 shorted 36 V into the TIA summing node on board 1 and destroyed
U11B. See `PROGRESS.md` §11. Most black plastics are near-transparent at 850 nm, which is why
this is fiddly; that does not make bare metal acceptable.

---

## 9. Demod phase — `cal demod` and `cal model`

**This is the last thing bring-up owes Phase 3**, and the two commands together take about
7 minutes of which 5 is warm-up.

### Board state

| | |
|---|---|
| PSU | 5.2 V, limit ≥ 2.5 A |
| Rail | **up** |
| Beam | **ON**, `beam freq 104166`, `beam duty 25`, **warm ≥ 5 min** |
| HPF | **`hpf track`** |
| Target | 🔴 **none. Do not put a reflector in front of the board.** |
| Attenuation over D12 | **stock board: probably needed. 116 kΩ variant: probably not.** See below. |

🔴 **Check the carrier before you start.** `beam` must read `freq 104166 Hz (TOP=1439)`.
`cal model` sweeps 80–200 kHz; it restores the carrier on builds after 2026-08-24, but an
older build or a reset mid-sweep leaves it wherever it stopped. The tell is in the sweep
listing: phases step by (TOP+1)/64, so **22/45/67 = 104 kHz** and **11/23/35 = 200 kHz**.

### Step 1 — get the level in range

**No reflector.** On board 2, D11 → D12 crosstalk plus the floor return alone drove the
calibration **1.9× past the ADC ceiling** with nothing in front of the board — a reflector only
makes it worse, and moving or darkening one cannot fix something that is not the reflector.

```
cal demod
```

Read `peak` in the output. **Target 50–70 % of full scale.**

| `peak` | do |
|---|---|
| `*** ABOVE FULL SCALE ***`, or `saturated` > 10 % | **attenuate light into D12** — a card with a hole in it is best, because the attenuation is geometric and does not depend on the material behaving at 850 nm. Most "opaque" black plastic is near-transparent there. |
| 50–70 % of full scale | ✅ go on |
| under ~30 % | you have over-attenuated; take some off |

🔴 **Nothing conductive near D12.** Its cathode is at 36 V through R77, and that is what
destroyed board 1 (`PROGRESS.md` §11).

### Step 2 — what a good result looks like

| line | want |
|---|---|
| `saturated` | **0 %** |
| `peak` | 50–70 % of full scale |
| `quad null` | near 0 |
| `h3/h1` | **≈ the printed intrinsic value** — 0.111 at 25 % duty. Not ≈ 0. |
| `warm` | yes |
| committed | `phase <- NNNN ticks` |

Then:

```
cal model
```

~128 s. All five points should be accepted, residual **under ~0.1°**, and it should report
**PURE DELAY**.

### Step 3 — record it, and compare the right quantity

- [ ] `demod_phase_ticks` — board 2 landed **1343–1350** across four runs at 104166 Hz
- [ ] **chain delay in ns** — this is the number that transfers
- [ ] model residual, and the pure-delay verdict

⚠ **Compare the DELAY between boards, never `phase_ticks`.** The tick count carries a
geometric term — `phase_ticks = t_chain_ticks − (duty/2)·(TOP+1)` — so it changes with duty and
carrier even on an identical board.

| | board 2 (stock 470 kΩ) | **board 3 (116 kΩ) — MEASURED** |
|---|---|---|
| `demod_phase_ticks` at 104166 Hz | 1343–1350 | **1311** |
| chain delay | 553–600 ns | **340 ns** |
| model residual | 0.07–0.08° | **0.23°** |
| model vs `cal demod` | 3.9–4.9 ticks | **3.5 ticks** |
| TIA lag at 104 kHz (calculated) | 8.74° | 4.30° |

🔵 **A large negative tick shift is the acceptance test for a lower-Rf rework**, because the TIA
pole moves up and the chain needs less offset. Board 3 shifted **−32 ticks** against a predicted
−17.8. **Right sign, right order, 1.8× too far** — and the excess could not be attributed,
because board 3 was never calibrated *before* the rework.

🔴 **So: if you rework a board, run `cal demod` BEFORE and AFTER on that same board.** It costs
26 s and it is the only way to separate the change you made from board-to-board variation. The
non-TIA part of the delay differed by **95 ns** between boards 2 and 3, which is larger than
most of what was being measured.

⚠ **If the phase does NOT move at all**, the feedback change did not take effect the way the
arithmetic says — check which pads the added capacitor bridges before trusting anything
downstream.

```
cfg save
```

- [ ] `cfg` reads back the carrier, the phase and `phase mdl: fitted ... (80000..200000 Hz)`

---

## 10. Threshold DAC and comparator cross-calibration

**Two constants that are genuinely per-part**, and the full reasoning is in
`BENCH_P3_DETECT.md` §3.5. This is the condensed run.

### Board state

| | |
|---|---|
| Rail | up · Beam **ON** 104166 Hz / 25 %, warm · HPF **track** · target as §9 left it |

### Step 1 — polarity check

⚠ **The subcommand word is required.** `threshold 5` is not "set 5 %".

```
threshold duty 0
threshold
```
- [ ] `D_Comparator(46) = 1` (ABOVE) — threshold 0 V against a ~10 mV quiescent
```
threshold duty 5
threshold
```
- [ ] `D_Comparator(46) = 0` (below)

### Step 2 — LOW point

```
threshold sweep 0 5 64
```
- [ ] flip near **0.2–0.3 % duty**. Record duty, TP8 nominal volts, and ADC5 volts.

### Step 3 — HIGH point

🔴 **At the calibrated phase this rails.** Dial it down with `beam phase`, then freeze it:

```
beam on
beam phase <peak - ~280>      # board 2 used 1000, board 3 used 1035
```
…wait 3 s…
```
beam off
```
…wait 3 s…
```
hpf hold
beam on
adc 5 100
```
- [ ] reads **2000–2500**. If 4095, lower `beam phase` further and repeat the whole sequence.
```
threshold sweep 0 100 64
```
- [ ] flip somewhere mid-range. Record duty, TP8 volts, ADC5 volts.

**Put it back:**
```
hpf track
beam phase <the value from §9>
```

### Step 4 — solve, and the crosstalk check

Two flips solve `ADC5 = vref·duty + Vos`:

| | board 2 | board 3 |
|---|---|---|
| DAC **vref** | 3.268 V | 3.256 V |
| comparator **Vos** | +11.0 mV | +5.4 mV |

- [ ] **vref** should land near **3.25–3.27 V** (compiled default is a nominal 3.300)
- [ ] **Vos** anywhere inside the LM393's **±15 mV** — it is a per-part offset and boards differ

🔵 Apply vref with `threshold vref <volts>` if you want; **RAM only**, and it is worth ~1 % on a
threshold that §3.7 tunes empirically anyway.

**Then the crosstalk check — beam OFF:**
```
beam off
hpf track
```
…wait 3 s…
```
threshold sweep 0 100 64
```
- [ ] **`ADC5 movement across the sweep` < ~3 mV.** Ignore the flip line; with the beam off
      there is nothing meaningful to cross.

⚠ **This must be beam-off.** With the beam on the number is dominated by the optical
background — board 3 read 12.9 mV lit and **3.2 mV** dark.

```
cfg save
```

---

## 11. Hand-off

The board is now bring-up complete and its calibration is persisted. **`cfg` should read back
the carrier, the phase, the phase model, `hpf sel`, `adc5v` scale and `cal duty`.**

**Next: `BENCH_P3_DETECT.md` §3.6b**, then §3.6's two checks, then §3.7.

Nothing in Phase 3 beyond this point is per-board except **§3.7** (`detect path`, transits and
the `cal gain` decision). §3.6b and §3.6 are **design verification** — do them once, on one
board, unless a later board misbehaves.

- [ ] **`detect path <mm>`** — nothing reports velocity until the beam path width is set. It is
      a §3.7 measurement, not a bring-up one.

⚠ **Carry the 5-minute warm-up habit into Phase 3.** Optical output falls **25–45 %**
cold→plateau while current moves +1.7 % and power +0.6 %, so every *electrical* reading says
nothing is happening. `cal demod` only warns; `scan carrier` refuses below 5 minutes.

✅ **Reference values to compare against, board 2 (stock 470 kΩ TIA, 2026-08-24):**

| | board 2 |
|---|---|
| **chain delay** at 104 kHz | **~570–590 ns** (`cal demod` prints it directly) |
| delay drift, 80 → 200 kHz | **small — do not expect a repeatable figure.** Two runs gave −3.5 % and −0.05 % |
| `pure_delay` verdict | **PURE DELAY**, by a wide margin both times (0.78° and 0.01° against a 5° bar) |
| `demod_phase_ticks` at 104166 Hz | **1343–1350** across four runs (7 ticks = 1.75°) |
| model residual | **0.07–0.08°** |

⚠ **Compare the DELAY between boards, never `phase_ticks`.** The tick count carries a
geometric term — `phase_ticks = t_chain_ticks − (duty/2)·(TOP+1)` — so it changes with duty
and carrier even on an identical board. The delay is the part that describes the hardware.

⚠ **`pure_delay` is the expected verdict, not a warning sign.** An earlier revision of this
line said to distrust it; that was wrong and it had been written into the firmware test.

---

## Sign-off

| § | item | board 1 | **board 2** (08-21) | **board 3** (08-28) | next board |
|---|---|---|---|---|---|
| 0 | 🔴 **TIA variant — Rf, and Cf incl. WHICH LEG** | stock 470 kΩ / 0.500 pF | stock 470 kΩ / 0.500 pF | **116.0 kΩ / 0.990 pF** (154 k∥R80, 100 pF on one Cf leg) | |
| 1 | **PSU current limit set to 2 A** | — | ✅ | ✅ | |
| 1 | Chip UID | `a764f5332ca5ac53` | *not recorded* | `6d6fda754e367a40` | |
| 1 | Silicon revision | A4 | *not recorded* | *not recorded* | |
| 2 | VIR (J2, no load) | 36.0 V | **36.0 V** ✅ | *not recorded* | |
| 2 | TP2 +12 V | 12.3 V | *not recorded* | *not recorded* | |
| 2 | TP6 +2V5 | 2.59 V | *not recorded* | *not recorded* | |
| 2 | 🟡 **LM5157 boost SW frequency** | *not recorded* | *not recorded* | ⚠ **801 kHz on the RAIL** (LA, not scope — could be L1 or L2) | |
| 3 | E9 `pins` check | all 0 | *not recorded* | *not recorded* | |
| 4 | +5V_IN on USB only | 4.85 V | *not recorded* | *not recorded* | |
| 4 | +5V_IN on PSU | 5.20 V | **5.207 V** ✅ | **5.210 V** ✅ | |
| 4 | 🔴 **`adc5vcal` scale** | 1.0627 | **1.0617** ✅ | **1.0620** ✅ | |
| 5 | Standby current | 32 mA | *not recorded* | *not recorded* | |
| 5 | Rail-up idle current | 129 mA @ 5.2 V | *not recorded* | *not recorded* | |
| 6 | **U9 clamp width** | 122.68 µs | *not recorded* | *not recorded* | |
| 6 | Beam temp at 25 % duty | 87.5 °C plateau | ⚠ **98 °C at 10 min** (sink 78 °C) | *not recorded* | |
| 7 | TP7 TIA_Out, beam off | 2.59 V | **2.589 V**, 2.4 mV p-p ✅ | *not recorded* | |
| 7 | ADC5 σ, beam on 25 %, TRACK | — | **2.19 mV** ✅ | *not recorded* | |
| 3.6b | **Q8 coupling, HOLD (armed)** | — | *not recorded* | 🔴 **×7.43** — FAILS, see PROGRESS Q8 | |
| 3.6b | **Q8 coupling, TRACK** | — | *not recorded* | ✅ **×3.84 peak, decays τ≈0.75 s** | |
| 3.6b | **U12B negative clamp** | — | *not recorded* | **21 mV** below quiescent | |
| 3.6 C2 | **`scan carrier` flatness** | — | *not recorded* | ✅ **PASS** σ 4.57–5.08, 10.8 % spread | |
| 7 | `hpf test` polarity | CONFIRMED, TRACK = 0 | **CONFIRMED, TRACK = 0** ✅ | **CONFIRMED, TRACK = 0** ✅ | |
| 7 | **HOLD leakage into C81** | ~250 pA (11 mV/s) | **~52 pA** (2.3 mV/s) | **~50–61 pA** (2.2–2.7 mV/s) ✅ | |
| 8 | **Max linear duty (CR-15)** | **~3 %** | **≥ 25 % with baffles** ✅ | ✅ 25 % | |
| 9 | **`demod_phase_ticks`** | — | **1343–1350** (4 runs) | **1311** (2 runs, identical) | |
| 9 | **chain delay, ns** | — | **553–600 ns** | **340 ns** | |
| 9 | **`cal model` residual / verdict** | — | **0.07–0.08°, PURE DELAY** | **0.26°, PURE DELAY** | |
| 9 | **D12 attenuation / target** | — | card, ~50–80 % FS | card at ~400 mm, **66 % FS** | |
| 10 | **DAC vref** | — | **3.268 V** | **3.256 V** | |
| 10 | **comparator Vos** | — | **+11.0 mV** | **+5.4 mV** | |
| 10 | GPIO44 crosstalk, **beam off** | — | **1.6 mV** ✅ | **3.2 mV** ⚠ at the bar | |
| 11 | `cfg save` verified | — | ✅ | ✅ | |

⚠ **The board 2 gaps above are real, not clerical.** Two of them matter:

- ⚠ **Beam temp — now measured on board 2, and it is worse than board 1.** 98 °C at the base
  after 10 min at 25 % (board 1: 87.5 °C), heatsink 78 °C (board 1: 68.1 °C). Junction works
  out at **114–122 °C against the 145 °C maximum** — 23–31 °C of margin, where board 1 had
  33–41. **Do not run board 2 above 25 %**, and re-read at 20–30 min for a real plateau: CR-12
  saw the temperature still climbing +4 °C over the second five minutes.
  **The mounting is not the problem** — the base→heatsink gradient is 20.0 K against board 1's
  19.4 K, so paste and clamping are as good. It is all heatsink→ambient, which means **airflow
  is the lever**: CR-12 puts 2–3× airflow at 24.5 → 14.7 K/W total.
- **Standby and rail-up idle current** are the baselines that make a future leak visible. The
  80 mA reading from the PSU-limit incident is *not* a valid baseline — it was taken with VIR
  parked at 27 V.

Copy the filled column into `PROGRESS.md` §6 when done.
