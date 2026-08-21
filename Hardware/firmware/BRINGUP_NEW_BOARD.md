# New-board bring-up — step by step

**Use this when you have a freshly assembled board and firmware already flashed.**
Not the same as `START_HERE.md`, which assumes you have never built or flashed anything.
This is the condensed, ordered re-run of the parts of Phases 0–2 that are **specific to a
board** rather than to the design.

Budget about an hour. Do not skip to Phase 3 — two of these steps are safety gates, and one
of them protects a Pi.

---

## What carries over, and what does not

| ✅ Transferable — do NOT redo | ⚠ Per-board — MUST redo |
|---|---|
| GPIO33 polarity: **TRACK is LOW** (TMUX1219 truth table) | Rail integrity |
| PWM slice collisions, PIO `GPIOBASE` allocation | **ADC +5 V scale — safety gate, see §4** |
| Q1 mechanism (U9 clamp ≈ K·R·C), Q2 (no duty limit to 250 kHz) | Latch guard + sustained supply monitor |
| Netlist-derived corner frequencies (`HARDWARE_REFERENCE.md`) | E9 check — different die |
| CR-15's *analysis* | CR-15's *measurement* — see §8, this is the payoff |
| The firmware itself | Thermal figures — depend on **this** heatsink mounting |

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
- [ ] Remove J2 again before continuing.

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

## 9. Hand-off to Phase 3

At this point the board is bring-up complete. Before `BENCH_P3_DETECT.md` §3.4 will produce a
number you still need:

- [ ] **`detect path <mm>`** — nothing reports velocity until the beam path width is set.
- [ ] **`cal model`** — `cfg` shows `phase mdl: not fitted` until it runs.
- [ ] **A static reflector**, for `cal demod`.
- [ ] **5 minutes of beam warm-up at 25 %.** Optical output falls **25–45 %** cold→plateau
      while current moves +1.7 % and power +0.6 % — so every *electrical* reading says nothing
      is happening. A carrier or threshold chosen cold is wrong warm, and it presents as drift
      rather than as a calibration error. `scan carrier` refuses to run below 5 minutes.

⚠ **`cal model` has a prediction to check against:** the netlist says the TIA contributes
**8.7° of lag at 104 kHz rising to 20.3° at 250 kHz**, so the fit should find that much
curvature. **If it reports `pure_delay`, distrust the measurement**, not the netlist.

---

## Sign-off

| item | board 1 reference | **board 2** (2026-08-21) | next board |
|---|---|---|---|
| **PSU current limit set to 2 A** | — | ✅ | |
| Chip UID | `a764f5332ca5ac53` | *not recorded* | |
| Silicon revision | A4 | *not recorded* | |
| E9 `pins` check | all 0 | *not recorded* | |
| +5V_IN on USB only | 4.85 V | *not recorded* | |
| +5V_IN on PSU | 5.20 V | **5.207 V** ✅ | |
| `adc5vcal` scale | 1.0627 | **1.0617** ✅ | |
| VIR (J2, no load) | 36.0 V | **36.0 V** ✅ | |
| TP2 +12 V | 12.3 V | *not recorded* | |
| TP6 +2V5 | 2.59 V | *not recorded* | |
| TP7 TIA_Out, beam off | 2.59 V | **2.589 V**, 2.4 mV p-p ✅ | |
| Standby current | 32 mA | *not recorded* | |
| Rail-up idle current | 129 mA @ 5.2 V | *not recorded* | |
| U9 clamp width | 122.68 µs | *not recorded* | |
| Beam temp at 25 % duty | 87.5 °C plateau | ⚠ **98 °C at 10 min** (sink 78 °C) | |
| `hpf test` | CONFIRMED, TRACK = 0 | **CONFIRMED, TRACK = 0**, 2.9× ✅ | |
| HOLD leakage into C81 | ~250 pA (11 mV/s) | **~52 pA** (2.3 mV/s) | |
| ADC5 σ, beam on 25 %, TRACK | — | **2.19 mV** ✅ | |
| **Max linear duty (CR-15)** | **~3 %** | **≥ 25 % with baffles** ✅ | |

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
