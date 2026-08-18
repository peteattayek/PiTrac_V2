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

```
hpf test
```

⚠ **Shade the photodiode first**, or kill the room lights. With the beam off the demodulator
is static, so there is no lock-in rejection, and mains flicker reaches ADC5 at ~162 mV p-p —
which swamps the 20–90 mV this test resolves. Unshaded it will report ORDER-DEPENDENT.

- [ ] Reports **CONFIRMED, GPIO33 = 0 is TRACK.**

```
cfg save
```

- [ ] `cfg` shows `hpf sel: TRACK = 0`.

🔴 If it reports **INVERTED**, do **not** simply flip the constant — the TMUX1219 truth table
says SEL = 0 selects S1 (the R96/GND leg). Two sources disagreeing means one is being
misread. See `PROGRESS.md` §6.

---

## 8. The CR-15 comparison — the reason a second board is valuable

Board 1's TIA saturates on beam coupling above **~3 % duty**, which blocks Phase 3
(`NEXT_BOARD_REV.md` CR-15). The mechanism — optical crosstalk vs beam-current coupling — is
**still open**. A second board is a controlled comparison, and that is worth more than any
further probing of one board.

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

Look for the highest duty with **no sample at 4095 and none below ~20**. Board 1:

| duty | board 1 TIA_Out | |
|---|---|---|
| 2 % | 597 … 3709 | linear |
| **3 %** | 98 … 3791 | **linear, marginal** |
| 4 % | 5 … 3798 | bottom railed |
| 6 %, 8 % | 4 … 4095 | both rails |

- **Same ceiling (~3 %)** → CR-15 is a **design** property. The fix is real and needed.
- **Materially higher** → something about board 1's assembly. The design may be fine.

⚠ **If you test optically, use an insulated opaque cover — cardboard, or foil wrapped in
tape.** Bare foil over D12 shorted 36 V into the TIA summing node on board 1 and destroyed
U11B. See `PROGRESS.md` §11. Most black plastics are near-transparent at 850 nm, which is why
this is fiddly; that does not make bare metal acceptable.

---

## Sign-off

| item | board 1 reference | this board |
|---|---|---|
| **PSU current limit set to 2 A** | — | |
| Chip UID | `a764f5332ca5ac53` | |
| Silicon revision | A4 | |
| E9 `pins` check | all 0 | |
| +5V_IN on USB only | 4.85 V | |
| +5V_IN on PSU | 5.20 V | |
| `adc5vcal` scale | 1.0627 | |
| VIR (J2, no load) | 36.0 V | |
| TP2 +12 V | 12.3 V | |
| TP6 +2V5 | 2.59 V | |
| TP7 TIA_Out, beam off | 2.59 V | |
| Standby current | 32 mA | |
| Rail-up idle current | 129 mA @ 5.2 V | |
| U9 clamp width | 122.68 µs | |
| Beam temp at 25 % duty | 87.5 °C plateau | |
| `hpf test` | CONFIRMED, TRACK = 0 | |
| **Max linear duty (CR-15)** | **~3 %** | |

Copy the filled table into `PROGRESS.md` §6 when done.
