# Next Board Revision — Change List

**Status: open, accumulating.** Nothing here is committed to a layout yet.

This is the running list of hardware changes for the next spin of *The Second Board To Rule
Them All*, written as bring-up finds them. Every entry states **why** (with the measurement
or the mechanism, not just the request), **what to change**, **how to verify it worked**, and
**what firmware has to change with it** — because several of these break firmware silently if
the code is not updated in the same commit.

Cross-references to `Q<n>` are open questions in `PROGRESS.md` §3. `A<n>` are findings in
`ARCHITECTURE.md`.

---

## Summary

| # | Change | Priority | Effort | If skipped |
|---|---|---|---|---|
| **CR-01** | Ready LED and strobe gate DAC share PWM slice 6A | 🟢 Low — **fixed in firmware 2026-10-02** | 1 trace | Only cost now: no PWM dimming on the ready LED (SIO on/off) |
| **CR-02** | Virtual ground tracks the +5 V rail → false triggers | 🔴 **High — MEASURED ×7.43** | 1 part *or* 1 stage | ✅ **Confirmed on board 3 2026-08-31, at the predicted magnitude.** A 39 mV rail step reads as a ball at a 300 mV threshold; a rail sag *blinds* the detector. `PROGRESS.md` Q8, `BENCH_P3_DETECT.md` §3.6b |
| **CR-03** | R46/R47 100K → 10K | 🟡 Med | 2 parts | ADC read path stays ~5 % low, needs a firmware fudge factor. Q9 |
| **CR-04** | 10 kΩ pull-up on J8.37 | 🟡 Med | 1 part | Reset presents an ambiguous level to the Pi. Q10 |
| **CR-05** | Panel ring LED on always-on power | 🟡 Med | Rework J7 feed | No fault indication whenever the rail is down |
| **CR-06** | Larger terminals for +5 V power input and strobe output | 🟡 Med | 2 parts + footprints | Cannot land the intended power and strobe wiring |
| **CR-07** | 12 V shunt regulator burns ~40 mA at idle | 🟢 Low | Redesign | ~31 % of idle power, forever |
| **CR-08** | No PGOOD or VIR sense | 🟢 Low | Needs a free ADC | Boost readiness stays open-loop timed |
| **CR-09** | Mira220 1.8 V I/O translation | 🔴 **High — Q6 answered; translation still required** before J4 meets a camera | High | **Q6 ANSWERED**: I/O is 1.8 V, no 3.3 V tolerance. Both directions fail; 220 Ω fixes neither |
| **CR-11** | Net `Strobe_GND` is not ground — rename it | 🟢 Low | Rename | A name that invites clipping a scope ground to Q11's drain |
| **CR-13** | U15 comparator has no hysteresis → chatters on every slow edge | 🔴 High (respin) | 1 resistor | One ball gives several edge pairs; the comparator transit becomes a lower bound and the ADC path has to carry those passes (firmware coalesces and flags them) |
| **CR-14** | U15's input common-mode ceiling (~3.5 V) is below what U12B drives (~5.2 V) | 🟡 Med | Supply/clamp change *or* part swap | Large returns can push the LM393 outside its valid input range, where some parts invert; ADC saturation is the early warning |
| **CR-16** | **Analog chain runs 0-5.2 V into a 3.3 V ADC** | 🔴 High | Rail or scale | **36 % of every signal is invisible to firmware**, and clipping corrupts the ADC reference |
| **CR-17** | No test point on the comparator input node | 🟡 Med | 1 pad | The detection decision node cannot be probed; TP8 gives the threshold but not the signal |
| **CR-18** | Mic high-pass corner may be too high at 2.41 kHz | 🟡 **Open — needs a ball** | 1 part (C84) | **45 % of a clap's energy sits below the corner** (measured 2026-08-31). If a ball impact looks like a clap rather than a snap, the front end is discarding the band the signal lives in. `BENCH_P5_P7_MIC_CAMERA.md` Phase 5 |
| **CR-15** | LED→PD crosstalk saturates the TIA | 🟡 **Mitigated** | Baffle (mech) | ✅ **Fixed on the bench 2026-08-19** — linear to 25 % duty with good baffles and hands clear. Board fix still wanted; the workaround relies on operator discipline |
| **CR-12** | Beam LED thermal path caps sustained duty at ~20 % | 🔴 High | **Vias + bigger sink** (fanless target) | Beam runs at 2/3 optical power while armed → worse Phase 3 SNR |
| **CR-19** | TIA feedback: 120 kΩ resistor and 1.5 pF capacitor | Requested 2026-09-30 | Feedback component values / BOM | Requested TIA feedback values are not incorporated |
| **CR-20** | Reduce the on-board green LED brightness | Requested 2026-09-30 | LED current-setting component | Green indicator remains too bright |
| **CR-21** | Test points on the strobe drive chain (U5 Q, Q10 gate), and a GND test point by TP3/TP4 | 🟡 Med — **a probe sparked at R61, 2026-10-05**; the only GND TP is 72 mm from TP3 (2026-10-07) | 3–4 test points | The clamped pulse can only be probed at R61, 0.58–1.15 mm from +12 V and GND pads; a TP1 ground picks up ±80 mV of boost |
| **CR-22** | Thermal relief on pads in copper pours | 🟡 Med — **both J3 joints were cold on board 1, 2026-10-13** | Zone settings + a DRC pass | Hand-soldered joints on pours stay suspect; on J3 the strobe ran open-circuit for a bench day |
| **CR-23** | Q9's gate floats up for microseconds at each turn-on (the gate drive pulls down only after a dead-band delay), so every pulse overshoots | 🔴 **High — measured 2026-10-13: 3.3 A peak on a 0.72 A setting** | Rework: 1 µF at Q9's gate. Next board: + R_iso, − U7 (net cheaper) | The first ~10 µs of every strobe pulse is wrong (overshoot, then sag); at 9 A the overshoot could reach tens of amps |
| **CR-24** | Test-point plan for the next board (consolidates CR-17, CR-21, CR-23's test points) | 🟡 Med — every bench phase has probed component pads; a probe sparked at R61 (board 2); TP2/TP3 sit 1.9/0.6 mm from 36 V/12 V copper | Layout only (test points cost no parts) | Soldered leads and pad-probing on every new board; the same spark and pickup risks |

*CR-10 was never assigned.*

---

## CR-01 — 🟢 Ready LED and strobe gate DAC are the same PWM channel — optional since the firmware fix

> **Status 2026-10-02: resolved in firmware.** `panel.c` drives GPIO12 as SIO on/off and
> `strobe.c` owns slice 6, refusing to raise the gate if GPIO12 is ever found on PWM again
> (6b bench proof pending). The board change below is now **optional** — it only buys back
> ready-LED dimming. The analysis is the original finding, kept as written.

### Why

`GPIO12` (READY_LED) and `GPIO28` (GATE_PWM) both map to **slice 6, channel A** on RP2350B.
Same channel means a single compare register: the PWM block produces one output and the GPIO
mux routes it to both pins. They emit the **identical waveform** — frequency *and* duty.

`panel.c` configures 6A today. Phase 6b needs it for the 9 A strobe current setpoint.
Whichever is configured second silently takes over both, so the LED brightness becomes the
current setpoint or vice versa — presenting as an analog fault in the gate chain, which is
where you would waste the afternoon looking. Full detail in `ARCHITECTURE.md` A7.

**Root cause is a pin-numbering coincidence, not a design error.** With
`slice = (gpio>>1)&7` and `channel = gpio&1`, any two GPIOs **exactly 16 apart** land on the
same slice *and* channel. 12 and 28 are 16 apart. So are the other two colliding pairs.

### The change

**Move READY_LED from GPIO12 to GPIO13.** One trace. GPIO13 is unconnected today
(netlist: `unconnected-(U3-GPIO13-Pad12)`) and maps to **slice 6, channel B**.

That is the *good* kind of sharing: channels A and B of one slice share `TOP` and `DIV` —
hence a common frequency — but each has **its own compare register**, so duty cycles are
independent. The gate DAC dictates the frequency; the ready LED simply runs at whatever that
is, which it does not care about.

GPIO29 is the only other free pin and lands on the same 6B, so it is an equivalent
alternative if routing prefers it.

> **Check Q5's switching behaviour at the DAC's frequency.** The ready LED sink will now run
> at the gate DAC's PWM rate rather than ~1 kHz. A MOSFET is untroubled; a BJT's storage time
> could distort duty at low brightness. Confirm the part before committing.

### Do you actually need this?

**No — firmware resolves it with no board change at all**, by driving the ready LED as
plain on/off SIO and giving up PWM dimming on it. ✅ **That fix landed 2026-10-02**
(`panel.c`, `strobe.c`; `ARCHITECTURE.md` A7), bench check pending in 6b.

Make this change only if **PWM brightness control on the ready indicator is wanted.** It was
listed high priority because the *conflict* was high priority, not because the trace move is.

### Firmware impact

- `panel.c`: `PIN_READY_LED` → 13, and LED duty must be computed against the **shared** `TOP`
  that the gate DAC sets, not `PANEL_PWM_WRAP`.
- Slice 6's `wrap`/`clkdiv` become owned by the strobe code. `panel.c` must not reconfigure
  them.
- If the change is *not* made, `panel.c` must stop calling `gpio_set_function(PIN_READY_LED,
  GPIO_FUNC_PWM)` before Phase 6b. ✅ Done 2026-10-02.

### Verify

Scope GPIO13 and GPIO28 together with both active. Independent duty at a common frequency.

---

## CR-02 — 🔴 Virtual ground tracks the rail, so a rail step reads as a ball

### Why

`R75`/`R76` = 10K/10K buffered by U11C makes **+2V5 literally +5VA ÷ 2** — there is no 2.5 V
regulator. Measured **2.59 V** on the 5.2 V rail (`PROGRESS.md` §6, TP6/7/9/10 all agree).

So the entire detector chain's reference **moves with the rail**. A step ΔV on +5 V shifts
virtual ground by ΔV/2. While the baseline HPF is in **HOLD** — which is exactly when the
system is armed — that shift is not removed, passes through C81, and hits the comparator
amplified **×14.5**:

```
100 mV rail step  →  50 mV at virtual ground  →  725 mV at ADC5
```

against a threshold of typically 0.1–0.5 V. **That is a false trigger from a power event.**
And from Phase 6 the rail is *expected* to move: the strobe bursts sag it deliberately, which
is why the supply monitor carries a 500 ms debounce. This is `PROGRESS.md` **Q8**.

In TRACK mode the 0.66 s HPF removes it. In HOLD it does not. Armed means HOLD.

### Option A — unity differential amplifier (as requested)

Insert a **gain-1 difference amplifier taking TP9 − TP6, ahead of the HPF**:

```
TP9 ──┐
      ├── diff amp, gain 1 ── C81 / gated HPF ── U12B ×14.5 ── ADC5
TP6 ──┘   (subtracts the virtual ground)
```

Both inputs shift together on a rail step, so the difference rejects it. The ball signal,
which rides *on top of* virtual ground, passes through referenced to 0.

**Matching is what sets the rejection.** For a 4-resistor diff amp at gain 1, worst-case
CMRR ≈ 1/(2·tolerance):

| Resistors | CMRR | 100 mV CM → at ADC5 |
|---|---|---|
| 1 % | ~34 dB | ~29 mV |
| 0.1 % | ~54 dB | ~2.9 mV |
| Monolithic (AD8276 / INA-class) | ~86 dB | ~0.1 mV |

**Use a monolithic difference amplifier**, not discrete resistors — laser-trimmed matching,
one BOM line instead of five, and no risk of someone substituting a 1 % part later.

⚠ **Two details that will bite:**
1. The output is now **ground-referenced**, so a single-supply stage **clips any negative
   excursion**. Reference the diff amp output to a small positive offset (~0.3 V) rather than
   0 V, or confirm the signal is strictly unipolar in both directions.
2. The HPF's HOLD reference must move from +2V5 to whatever the new reference is. Do not
   leave it hanging off the old tracking node.

### Option B — regulate +2V5 instead ⭐ recommended

**Replace the R75/R76 divider with a real 2.5 V reference feeding U11C.** Keep U11C as the
buffer; it already sources and sinks the chain's bias current.

This kills the mechanism at source rather than cancelling it downstream:

| | Option A (diff amp) | Option B (reference) |
|---|---|---|
| Parts added | 1 diff amp (+ offset network) | 1 voltage reference |
| Removes the mechanism? | No — cancels it, limited by CMRR | **Yes — the reference stops moving** |
| New stage of noise/offset | Yes | No |
| Clipping/reference rework | Yes, see above | None — chain still centred |
| Residual on a 200 mV step | ~0.1–29 mV depending on matching | Op-amp PSRR only, ≲3 mV |

The chain re-centres from 2.59 V to 2.50 V, a 90 mV shift that the HPF removes as DC and that
costs negligible headroom on a 5.2 V rail.

**Recommendation: Option B**, unless there is a reason to keep the reference ratiometric.
It is fewer parts, no matching requirement, no clipping rework, and it makes every "2.50 V"
in the original .md correct after all.

### Firmware impact

Either option: the Q8 mitigation notes in `BENCH_P3_DETECT.md` can be dropped, and the armed
window no longer needs to be kept short for rail-stability reasons.

### Verify

While armed and holding, scope +5 V and ADC5 together and step the rail 200 mV. **Pass: no
visible step at ADC5.** This is the Phase 3 test Q8 already calls for — it becomes the
acceptance test for this change.

---

## CR-03 — 🟡 R46/R47 100K → 10K

### Why

The +5V_IN read path is **~5.9 % low**, localised on the bench (`PROGRESS.md` Q9):

| Measured | |
|---|---|
| J1 | 5.200 V |
| R46/R47 junction | 2.578 V (−0.85 %, fine for two 1 % parts) |
| ADC reports | 2.447 V (**−5.1 %** — the error is inside the ADC) |

Leakage was ruled out (the junction would have been dragged down; it was not) and a reference
error was ruled out by arithmetic. Residual cause is **ADC gain error plus incomplete
sample-and-hold settling through the 50 kΩ source**. The RP2350 wants **≤10 kΩ**.

This nearly broke Phase 1: uncalibrated, a good bench supply read as 4.89 V — below
`V5_MIN_FOR_LATCH` — so the firmware refused to latch and reported `USB_POWER_ONLY`.

### The change

**R46 = R47 = 10K.** Still ÷2, so the divider ratio is unchanged. Thevenin source impedance
drops 50 kΩ → **5 kΩ**, inside the ADC's requirement.

Cost: **260 µA** extra quiescent draw (5.2 V / 20 kΩ). Against a 32 mA standby budget that is
0.8 % — irrelevant.

### ⚠ Firmware impact — do not miss this

`ADC5V_SCALE_DEFAULT` is currently **1.063**, which exists *only* to compensate the error this
change removes. **Leave it at 1.063 and every reading is ~6 % HIGH**, which is worse than the
original bug: the board would happily latch on USB power.

**Same commit as the board change:**
1. Re-derive the scale on the new hardware (expect ≈ **1.00**, i.e. just the divider's −0.85 %).
2. Update the compile-time default in `board.h`.
3. Re-run `adc5vcal` and record in `PROGRESS.md` §6.
4. Re-verify Phase 1 test 1 (refuses to latch on USB) and test 5 (`SUPPLY_LOST`).

### Verify

DMM at J1 vs `adc5v`. Agreement within ~1 % with the scale at 1.000.

---

## CR-04 — 🟡 10 kΩ pull-up on J8.37

### Why

RP2350 pads reset with the **internal pull-down enabled** (`PADS_BANK0_GPIO43_RESET = 0x116`
→ PDE=1). GPIO43 is RPI5_SHUTDOWN and is **active-low**, so the reset default *is* the
asserted level, held from the reset edge until `safe_state_init()` runs.

Measured with a 20 kΩ emulated pull-up: 2.07 V against a 3.246 V rail → the pad's pull-down
is **~34 kΩ**, and a real Pi's ~50 kΩ pull-up would see **1.34 V** — below RP1's VIH.

### The change

**10 kΩ from J8.37 to the always-on +3V3.** With the measured 35.2 kΩ:

| | Level at J8.37 | Threshold | Margin |
|---|---|---|---|
| Through reset | 2.67 V | VIH 2.31 V | +0.36 V ✅ |
| Firmware asserting | 0.35 V | VIL 0.99 V | −0.63 V ✅ |

6.8 kΩ balances the margins (+0.51 / −0.52) if preferred. **Must go to +3V3, not the switched
+5 V** — the entire point is holding the line while the MCU is not running.

### Honest priority note

This is **defence-in-depth, not a fix for a live hazard.** Every RP2354 reset *also* opens the
+5 V latch (R12 pulls Q2's gate low when the pad goes high-Z), so the Pi loses power in the
same instant the spurious request arrives. The GPIO43 level is a footnote to that.

Cheap and correct, so worth doing on a respin — but it does not gate anything, and it is not
what protects the Pi from a reset. Nothing on this list is; see "Not solved here" below.

### Verify

`BENCH_P8_PI.md` §8.4 — with a Pi up, J8.37 stays above 2.3 V and `journalctl` shows no
KEY_POWER event.

---

## CR-05 — 🟡 Panel ring LED on always-on power

### Why

J7.1/J7.5 feed the panel LED anodes from the **switched +5 V**, so both panel LEDs are
physically dark whenever the latch is open — including **STANDBY and every fault that drops
the rail**. Pre-latch feedback currently comes only from on-board D5/D6, which are not visible
from across a room and not on the enclosure.

The motivating case is exactly right: **if the Pi fails to shut down, `PI_SHUTDOWN_TIMEOUT`
fires and the FSM goes `FORCE_OFF` → `STANDBY` with the rail down** — so today the fault is
recorded and completely invisible on the panel.

> **Firmware is already correct for this.** `panel_pattern_for_state()` returns
> `PANEL_PAT_FAULT` whenever `fault_current() != FAULT_NONE` **regardless of state**, and
> `FORCE_OFF` does not clear the fault code. The double-blink is already being driven in
> STANDBY — the rail is the only thing stopping it reaching the LED. **This change makes an
> existing behaviour visible; it needs no new firmware logic.**

### The change

**Feed J7.1 (and J7.5) from +5V_IN — the always-on rail upstream of the latch — instead of
the switched +5 V.** Q4/Q5 and R48/R49 are unchanged; only the anode feed moves.

⚠ **+3V3 will not work.** The Adafruit 481 ring drops **~4.84 V** (measured: 0.365 V across
R48's 47 Ω = 7.8 mA). It cannot light from 3.3 V. +5V_IN is the only always-on rail high
enough.

### Consequences to accept

| | |
|---|---|
| **Standby current** | +7.8 mA whenever the ring is lit with the rail down. Standby is **32 mA** today, so a lit ring is ~40 mA — a **24 % increase**. Fine on a mains supply; note it if battery operation is ever considered. |
| **Connector pinout changes** | J7.1/J7.5 change rails. **Any existing harness must be reworked**, and a v1 harness on a v2 board would feed the LEDs from the wrong domain. Consider a mechanical key or a silkscreen revision marker. |
| **D7 (ready) too?** | Only the ring was requested. Moving **both** is more flexible and lets firmware decide; leaving D7 switched keeps "ready" meaning "rails up" for free. **Decide explicitly.** |

### Firmware impact

Not required, but now worth doing:
- `PANEL_PAT_OFF` for STANDBY becomes a real choice rather than a description of the wiring.
  A slow breath would read as "asleep but alive" instead of "dead".
- `BENCH.md` §1c.4 ("why the automatic patterns only show dark and solid in bench mode") is
  substantially obsoleted — most states become visible without a simulated Pi.

### Verify

Rail down, provoke a fault, confirm the ring double-blinks. Then `stat` to confirm the fault
code matches what the ring is showing.

---

## CR-06 — 🟡 Larger terminals for +5 V power input and strobe output

### Why

J1 is currently a **Würth 691137710002** (`CONN2_710002_WRE`, 2-position screw terminal). The
3.5 mm-pitch WR-TBL family tops out around **1.5 mm² / 16 AWG** — confirm against the
datasheet, but that is the family limit and it is why 14 AWG stranded will not land.

**Scope expanded 2026-09-30:** larger terminals are requested for both the **+5 V power
input** and the **strobe output**, not just J1.

### The change

A larger block for J1. Specify on **rating**, not only on wire gauge:

| Requirement | Value |
|---|---|
| Wire | **2.5 mm² (14 AWG) stranded**, with ferrules |
| Pitch | **5.00 or 5.08 mm** |
| Current | **≥15 A** (see below) |
| Positions | 2 |

**Size the current properly.** J1 carries +5V_IN, which feeds *everything*: a Pi 5 at up to
~5 A peak, the boost driving the strobe, the beam LED at ~0.95 A average, and the analog
chain. Realistic peak is **6–8 A**, so a 10 A part has little margin and 15 A is the sensible
choice. **Widen the PCB pours and pads to match** — a 15 A terminal on a trace sized for 3 A
achieves nothing.

Ferrules on stranded wire are worth specifying in the build docs: bare stranded under a screw
clamp relaxes over thermal cycles, and this is the connector where a loose joint browns out a
Pi.

**Also enlarge the strobe output terminal and its PCB footprint.** Select its wire capacity
and current rating for the intended strobe harness and pulse load; the exact replacement
part is still to be selected.

### Firmware impact

None expected from the terminal changes.

### Verify

Land 14 AWG stranded with a ferrule, torque to spec, then a thermal image at full load. The
input terminal should not be a hot spot relative to the pour. Verify that the intended
strobe wiring fits the new output terminal securely, and check it under the normal strobe
pulse load.

---

## CR-07 — 🟢 The 12 V shunt regulator dominates idle current

### Why

**R15 (4K7) drops VIR 36 V into the D4 zener continuously**, burning ~180 mW between resistor
and zener to produce a ~5 mA rail. That is **~40 mA of the 129 mA rail-up idle draw —
roughly 31 %**, purely to make the strobe gate-drive supply.

Inherent to a shunt regulator and fine as designed. Recorded in `PROGRESS.md` §2 as "the
first thing to revisit if idle power ever matters."

### The change

Only worth doing if idle power becomes a requirement. A small buck or a series regulator from
VIR would recover most of it. Not free: the 12 V rail feeds gate drive for the strobe, so
anything replacing it needs to hold up under the burst load the shunt handles trivially.

**Deliberately low priority** — do not spend layout risk on this unless there is a reason.

---

## CR-08 — 🟡 No PGOOD, no VIR sense — **cost a bench session 2026-08-18**

### Why

**All eight ADC-capable pins are committed**, so there is no way to measure the 36 V boost.
Boost readiness is therefore timed **open-loop** in `power_fsm.c` — `RAIL_SETTLE_MS` 250 ms,
covering an ~86 ms soft start plus margin. It works, but the firmware genuinely cannot tell
the difference between "boost came up" and "boost failed and we waited 250 ms."

> ### This stopped being theoretical on 2026-08-18
>
> A bench PSU left at a **0.3 A** current limit could not supply the startup inrush into VIR's
> bulk capacitance (~100 uF to 36 V is ~65 mJ, needing amps for a few milliseconds). The
> supply dropped into CC, **the boost never completed soft-start, and VIR parked at 27 V.**
>
> **Every downstream symptom pointed at the board rather than the supply**, because once
> parked the steady draw is tiny — the 12 V shunt pulls (27-12)/4K7 = 3.2 mA — so:
>
> - `+5V_IN` read a healthy **5.207 V** and the latch guard was satisfied
> - the sustained supply monitor never tripped, correctly
> - the CLI, the LED and the detect chain all worked
> - idle current was **80 mA** instead of ~129 mA, which reads as "less load", not "fault"
>
> Two boards showed it simultaneously, which made it look like a systematic assembly or BOM
> error. Time went into measuring the feedback divider, questioning the LM5157 reference
> voltage, and considering whether firmware could be involved — **none of which it could,
> since no MCU pin touches U1 at all.**
>
> **A single VIR sense would have made this instant.** The firmware knows the boost is
> supposed to be at 36 V and had no way to look.
>
> This also has a safety dimension for Phase 6: **the strobe runs from VIR.** A silently low
> rail means every strobe measurement is taken against the wrong supply, and the firmware
> would fire regardless because it cannot see the difference.

### Priority raised

From 🟢 to 🟡. Not because the mechanism changed, but because the failure mode is now
demonstrated to be **silent, plausible and expensive** rather than hypothetical.

### The change

Either free an ADC channel, or bring the LM5157's PGOOD (if available in the chosen variant)
to a spare digital input. A VIR tap through a large divider would also let the strobe code
verify headroom before firing rather than assuming it.

Note this competes for the same scarce resource as everything else analog. Listed so the
tradeoff is visible at layout time, not as a recommendation.

---

## CR-09 — 🔴 Mira220 1.8 V I/O translation — **Q6 ANSWERED 2026-08-14; translation still required**

### The sensor spec, from the datasheet

ams-OSRAM Mira220, **DS000642 v9-00 (2025-Sep-26)**, Tables 2 and 3:

| Symbol | Parameter | Min | Typ | Max |
|---|---|---|---|---|
| VDD18 | **I/O supply voltage** | 1.70 | **1.80** | 1.90 V |
| VDD18 | I/O supply, *absolute max* | | | 4.125 V |
| IVDD18 | **I/O supply current** | | | **0.6 mA** |
| VIH | High-level input | 0.7·VDD18 = **1.26 V** | | **VDD18 = 1.80 V** |
| VIL | Low-level input | VSS | | 0.3·VDD18 = 0.54 V |
| VOH | **High-level output** | 0.8·VDD18 = **1.44 V** | | VDD18 = 1.80 V |
| VOL | Low-level output | VSSIO | | 0.2·VDD18 = 0.36 V |
| ISCR | Input current, latch-up immunity | | ±100 mA | (JESD78D) |

**So Q6's question is answered: yes, the digital I/O is a 1.8 V domain, and there is no
3.3 V tolerance — VIH max is specified as VDD18 itself.**

### Both directions fail, and 220 Ω fixes neither

**Read direction — a guaranteed functional failure, not a risk.** `Cam_Strobe_0/1` (GPIO8/9)
are RP2350 *inputs*. The sensor's guaranteed VOH minimum is **1.44 V** and its absolute
ceiling is 1.80 V, against an RP2350 VIH of **~2.15 V** (0.65 × IOVDD). **1.80 < 2.15**, so
the strobe inputs never read high and the entire Phase 7 camera handshake cannot work. This
half was already flagged in the previous revision of this CR; the datasheet now makes it
certain rather than suspected.

**Write direction — the damage path, quantified.** `D_Cam_Trigger` (GPIO10) drives 3.3 V
through R18 220 Ω into the sensor input, which clamps to VDD18 through its ESD diode:

```
I = (3.3 − (1.8 + 0.7)) / 220 Ω ≈ 3.6 mA
```

- **Latch-up is NOT the concern.** 3.6 mA against ISCR = ±100 mA is ~27× margin.
- **Rail injection IS.** The sensor's entire I/O supply draws **0.6 mA max**, so 3.6 mA of
  injection is **six times the rail's own consumption**. An LDO cannot sink, so VDD18 gets
  pulled up — out of its 1.70–1.90 V operating window, though still under the 4.125 V
  absolute max. Out-of-spec operation rather than instant destruction.
- **The genuinely destructive case is driving J4 while the camera is unpowered.** That
  back-powers VDD18 through the ESD diode and partially wakes the sensor through an I/O pin,
  violating the datasheet's power-up sequence. This is the one to avoid absolutely.

The 220 Ω resistors limit current, which helps the damage case a little. **They shift no
levels at all and fix neither direction.**

### The fix

Three signals need translating: one output (`D_Cam_Trigger`) and two inputs
(`Cam_Strobe_0/1`).

| Direction | Minimum viable | Preferred |
|---|---|---|
| 3.3 V → 1.8 V (trigger) | resistive divider — one extra resistor | part of the translator below |
| 1.8 V → 3.3 V (strobe ×2) | **a divider cannot step up** — translation is mandatory | |

Use a proper translator for all three rather than mixing approaches: a **TXB0104**-class
auto-direction part, or discrete **BSS138** MOSFET translators. Either needs a 1.8 V
reference available at J4 — **which the current J4 pinout does not provide** (pins 1, 2, 5, 6
are all GND). So J4 must gain a VDDIO sense pin, or the translator's low side must be fed
from a local 1.8 V regulator.

⚠ **Running the RP2350's bank 0 at 1.8 V is not an option** — it is a single IOVDD domain and
the rest of the board is 3.3 V.

### The one thing still unknown

**Whether the camera board mates raw sensor pins to J4, or already level-shifts.** The
analysis above is definitive for a *bare* Mira220. `BENCH_P5_P7_MIC_CAMERA.md` records that
the sensors hang off the Pi's CSI and J4 carries only trigger/strobe — so J4 lands on the
camera board's own header, and that board's schematic governs. **Get it before connecting
anything**, because if the module already translates, this CR may reduce to nothing.

**Until then the rule stands: do not connect J4 to a camera**, and in particular never drive
`D_Cam_Trigger` high with the camera unpowered.

---

## CR-11 — 🟢 Rename the net `Strobe_GND`

It is **not ground.** `Strobe_GND` (netlist code from the beam chain) is Q11's **drain** — the
switched node between the ballast pair and the low-side FET, swinging 0.15 V to 5.2 V. TP5
sits on it.

The name actively invites someone to clip a scope ground lead there, which shorts the drain to
earth and puts ~3.3 A DC through the probe lead while bypassing U9's pulse clamp. It is the
one net on this board whose name could cause damage.

**Rename to something like `BEAM_LED_K` or `Q11_DRAIN`.** Costs nothing, prevents a class of
mistake that no amount of documentation reliably fixes. Same review pass should check for any
other net named `*_GND` that is not actually at ground potential.

---

## CR-12 — 🔴 The beam LED's thermal path limits sustained duty, and therefore SNR

### Why — measured 2026-08-13, not estimated

With the heatsink fitted, still air, 23 °C ambient:

| Duty | LED base | Junction (+6…9 K/W) | Margin to 145 °C |
|---|---|---|---|
| 25 % | 87.5 °C | 104–112 °C | 33–41 °C |
| **30 %** | **104 °C** (measured at the base, 10 min) | **123–133 °C** | **12–22 °C** ❌ |

**R_th LED→ambient ≈ 24.5 K/W**, confirmed at two independent duty points — it predicts 25 %
→ 89 °C and 30 % → 102 °C against measurements of 87.5 and 104.

⚠ **The 30 % figure is a lower bound.** It was still climbing at 10 minutes (+4 °C over the
second five), so there is a slow board/heatsink time constant far beyond the ~70 s local one.
A true plateau needs 20–30 minutes. It does not change the verdict — 12–22 °C of margin to an
absolute-maximum rating is already not an operating point.

**D11 and the ballast resistors run at the same temperature**, which is not the contradiction
it looks like: D11 carries 4× the power through a ~4× better path, and they sit millimetres
apart on shared copper.

**Where the resistance lives — remeasured 2026-08-14 with the IR camera pointed sideways at
the LED base** (an earlier reading off the domed lens was a 50 °C emissivity artifact and gave
a misleading split):

| Stage | R_th | Share |
|---|---|---|
| Junction → solder point | 6–9 K/W | datasheet, fixed |
| **Base → heatsink** | **~8.7 K/W** | **35 %** — 28 °C gradient at 3.23 W |
| **Heatsink → ambient** | **~16.4 K/W** | **65 %** |

⚠ **This corrects an earlier claim in this document that the interface was ~2 K/W and that
"better thermal compound buys nothing."** It is 35 % of the total, so the mounting path is
worth attention alongside airflow — though airflow is still the bigger single lever.

| Fix | Total R_th | Sustainable duty @ 23 °C |
|---|---|---|
| As-is | 24.5 K/W | ~24 % |
| **Airflow only** (2–3× on the sink) | ~14.7 | **~34 %** ✅ |
| Interface only (8.7 → 3) | ~19.6 | ~28 % |
| Both | ~9 | ~45 % |

### Why it matters beyond thermals

The beam is the *detection* beam — it must be on the entire time the system is armed waiting
for a ball, which is minutes, not milliseconds. So this is a **continuous** rating, not a
burst one. At 40 °C enclosure ambient every figure above shifts **+17 °C**, capping sustained
duty at **~18–20 %** against a 30 % design point.

**That is a ~⅓ reduction in optical power, which lands directly on Phase 3 detection SNR.**
It is a system-level constraint discovered thermally, and it should be resolved before the
carrier and threshold work in Phase 3 is tuned against an optical power the product cannot
sustain.

### Options, best value first

1. **Airflow.** Forced convection typically improves a heatsink 2–3×; 22 → ~9 K/W takes the
   total to ~12 K/W and makes **30 % viable even at 40 °C ambient**. Cheapest fix by far,
   but it adds a fan (noise, power, a moving part, an air path through the enclosure).
2. **A larger heatsink / more board copper.** No moving parts. Needs layout area and an
   honest airflow assumption for the enclosure.
3. **Accept ~20 % and re-plan the optical budget.** Legitimate, but decide it deliberately
   and feed it into Phase 3 rather than discovering it during `scan carrier`.
4. **Re-examine the operating point.** LED efficiency droops at high current density, so
   *lower peak current at higher duty* may give more optical output for the same heat. Same
   average current, better photons per watt. Whether the demodulator tolerates the duty
   change is a Phase 3 question — worth testing during `scan carrier` rather than assuming
   30 % / 3.1 A is optimal.
5. **Replace the resistive ballast with a current regulator.** R73/R74 burn **1.38 W at 25 %
   duty** — a third of D11's own 2.74 W, measured. It does *not* reduce D11's junction
   temperature (different parts), so it is not a fix for the core problem, but it removes a
   third of the beam's total thermal load from the board. Weigh against losing the ballast's
   simplicity and its role in making the current inherently stable.

### Measured temperature behaviour — better than assumed

Cold → hot (85 °C plateau) at 25 % duty: **current +1.7 %, power +0.6 %.** The LED's Vf
tempco at 3 A is only ~−0.5 mV/K for the stack, because most of Vf is I·Rs and series
resistance rises with temperature, opposing the bandgap term. **So there is no meaningful
thermal feedback and the operating point is very stable.**

⚠ **But optical output is not.** Radiant efficiency falls ~0.3–0.6 %/K, so the same ~76 K
junction rise costs roughly **25–45 % of the light** — invisible in every electrical
measurement. That is the real cost of running hot, and it is what makes this a Phase 3 SNR
issue rather than a reliability one.

### How to actually run 30 % indefinitely — the options, costed

**The target.** For a junction of **110 °C** (35 °C below the 145 °C absolute max, a normal
lifetime derating) at a **40 °C enclosure ambient**, with D11 dissipating 3.23 W:

```
required junction->ambient = (110 - 40) / 3.23 = 21.7 K/W
minus R_thJSP (6-9 K/W, fixed by the part)
=> required BASE->AMBIENT <= ~12.7 K/W        (currently 24.5)
```

**So roughly halve it.** No single easy change quite does that alone, which is why the table
below is about combinations.

#### A — Get heat out of the board (base→heatsink, currently 8.7 K/W, 35 %)

| Option | Est. effect | Notes |
|---|---|---|
| **Thermal vias under D11's pad** | 8.7 → **4–5 K/W** | The standard fix, and the cheapest. **Check whether the current layout has any** — if the heatsink is on the opposite side, every watt crosses the PCB through whatever vias exist. |
| **Copper coin / embedded slug** | 8.7 → **2–3 K/W** | Best-in-class, meaningful fab cost. Worth it only if vias prove insufficient. |
| **2 oz copper, larger pour** | ~15–25 % better | Cheap if the stackup is being revised anyway. |
| **Heatsink on D11's own side** | bypasses the board entirely | ⚠ D11 emits from the top, so a sink cannot cover it — would need a clamp onto the package sides/base. Mechanically awkward. |

#### B — Get heat out of the heatsink (→ambient, currently 16.4 K/W, 65 %)

| Option | Est. effect | Notes |
|---|---|---|
| **Fan / forced air** | 16.4 → **5.5–8 K/W** | 2–3× is typical. Biggest single lever. Costs a moving part, noise, power, and an enclosure air path. |
| **Larger heatsink** | roughly ∝ area; 2× fins ≈ 16.4 → **8 K/W** | No moving parts. Needs volume and layout area. |
| **Enclosure airflow path** | varies, can be large | Free if designed in; worthless if bolted on later. **Whatever the enclosure does is what actually sets ambient** — the bench figure of 23 °C will not hold. |

#### C — Make less heat in the first place ⭐ the option not usually considered

| Option | Est. effect | Notes |
|---|---|---|
| **Lower peak current, higher duty** | ~10 % less heat **and more light** | LED efficiency droops at high current density, and 3.1 A is 2× the part's 1.5 A DC rating. E.g. **1.55 A at 60 % duty** is the same average current but lower Vf (~3.1 V) → **2.88 W instead of 3.23 W**, with better photons per watt. **Needs a new ballast (0.54 → ~1.32 Ω) and Phase 3 must confirm the demodulator tolerates the duty change.** |
| **Two LEDs at half current each** | halves per-package heat, better efficiency | Same total optical output spread over two packages, each at lower current density. Costs board area, alignment work and a second part. |
| **Replace the resistive ballast with a current regulator** | removes **1.62 W** from the immediate area | It does *not* lower D11's junction directly — but R73/R74 sit millimetres away at the **same 104 °C**, so they raise D11's local ambient. The measured 24.5 K/W **includes that mutual heating**. |

#### D — Layout

**Separate the ballast resistors from D11's copper island.** Costs nothing but routing. Today
they share a thermal environment and heat each other; the whole beam section dissipates
**4.85 W** (3.23 LED + 1.62 ballast) into one small region.

#### Combinations that hit the target

| Combination | Base→ambient | 30 % @ 40 °C ambient |
|---|---|---|
| As-is | 24.5 | ❌ junction 133–162 °C |
| Fan only | ~14.7 | ⚠ ~115–124 °C — marginal |
| **Better TIM + larger heatsink (no fan)** | **~12–14** | ✅ **~104–118 °C — fanless, tight** |
| **Better TIM + ENIG + larger sink** | **~12** | ✅ **~104–113 °C — fanless** |
| **Better TIM + fan** | **~10–12** | ✅ **~98–110 °C, comfortable** |
| Better TIM + fan + lower peak current | ~10 @ 2.88 W | ✅ ~93–101 °C, with *more* light |

**Recommendation, revised after inspecting the layout: start with the TIM, not the board.**
The vias are already there and the board is thin, so the interface is where the 8.7 K/W lives.
A thinner, higher-conductivity insulating pad is a **materials change needing no board
revision at all**, worth 2–2.5 K/W on its own. **Try it on the existing hardware first** — it
also measures how much of that stage really is the TIM, which nothing else will tell you.

Then **ENIG + a larger bottom-side pour** on the next spin, plus a **larger heatsink**. That
combination reaches the target **fanless**, which keeps a moving part out of a device that
otherwise has none. Add the fan only if the enclosure proves worse than assumed.

**Measure after each change.** The base→heatsink gradient is directly observable: LED base
minus heatsink temperature, divided by 3.23 W. That is the number to watch, and it is the one
this document previously had wrong by 4×.

### Verify

Repeat the plateau test at the **worst-case enclosure ambient**, not on an open bench:

```
beam duty 30
# FLIR every minute until it stops rising -- allow 20-30 min, not 5
```

⚠ **Allow 20–30 minutes.** The 2026-08-14 run was still climbing at 10 min (+4 °C over the
second five), so there is a slow board/heatsink time constant well beyond the ~70 s local one.
A 5-minute reading will flatter the result.

**Measure the LED at its base, from the side.** A reading through the domed lens gave 53.6 °C
against a true ~104 °C. **Sanity rule: D11 must read hotter than the heatsink it feeds.**

**Pass:** junction, computed as `base + 9 K/W × P`, stays under **110 °C**.

Also record the **base → heatsink gradient** — that is the number that tells you whether the
vias/interface work paid off, and it is the stage this document previously got wrong by 4×.

---

## Design rules for the next layout

1. **Never place two PWM functions on GPIOs 16 apart.** With `slice = (gpio>>1)&7` and
   `channel = gpio&1`, they land on the identical slice *and* channel and become one output
   on two pins. This board has **three** such pairs (12/28, 15/31, 11/27); only one is a live
   conflict, but the other two are landmines on the +5 V latch and the strobe watchdog defeat.
   Check the full map in `board.h` before assigning any new PWM.
2. **Any ADC input needs ≤10 kΩ source impedance.** CR-03 exists because a 50 kΩ divider
   cost 5 % of accuracy and nearly blocked Phase 1.
3. **Anything that must indicate a fault belongs on an always-on rail.** CR-05 exists because
   the fault indicator is fed from the rail that faults drop.
4. **Do not reference a signal chain to a divider off the rail it is trying to measure
   against.** CR-02 exists because the virtual ground tracks the supply.
5. **Bring out a ground pad next to every signal worth probing.** R69/R91 accidentally did
   this for the beam signals and it made Phase 2a straightforward; nothing else on the board
   is as convenient.

---

## Not solved by anything on this list

**An RP2354 reset is a hard power cut to the Pi.** The pads reset, GPIO15 goes high-Z, R12
pulls Q2's gate low, and the +5 V rail opens — no shutdown request, no sync, no warning. This
is true of *every* reset source: SW2, the CLI, the watchdog, a +5V_IN brownout.

**It is a deliberate tradeoff, not a defect.** R12's pull-down is what makes the latch
*fail-safe*: the rail opens if the MCU dies, which is the right behaviour on a board that
drives 9 A pulses. You cannot also have "the Pi survives an MCU reset" without a supervisor or
hold-up that would weaken exactly that property.

Current mitigations are firmware (`reset`/`bootsel` refuse while a Pi is powered) and
procedure (tape over SW2). **If a future revision wants to solve it properly it needs a
deliberate design decision** — a supervisor holding the latch through short MCU resets, with
its own watchdog so a genuinely dead MCU still drops the rail. That is a real design task, not
a component swap, and it should not be bolted on casually.

---

## When this list is acted on

Update `PROGRESS.md` — close Q8 (CR-02), Q9 (CR-03) and Q10 (CR-04), and move their
measurement rows in §6 (most are now in `PROGRESS_ARCHIVE.md` §6) to reference the revision
they were fixed in. A7 / CR-01 is already resolved in firmware; the trace move only restores
ready-LED dimming. Several bench
procedures change too; the ones that name specific voltages (2.59 V virtual ground, the 1.063
ADC scale) are written against **this** board and will be wrong on the next one.

---

## CR-13 — 🔴 U15 has no hysteresis, so the comparator chatters on every slow edge

### Why

`D_Comparator` (U15 pin 1) goes to R103 and GPIO46 and **nowhere else**. There is no
resistor from the output back to pin 3 anywhere on the board — I enumerated every net that
touches U15: `D_Comparator`, `Threshold_DC`, `Net-(U12B-OUT2)`, `+5V`, `GND`.

An open-loop comparator with no hysteresis reproduces its input noise as output chatter
whenever the input crosses the threshold slowly. A ball transit is *milliseconds* wide and
the signal is band-limited to 15.39 kHz by the LPF, so the edges are about as slow as edges
get — this is the worst case, not an edge case.

The consequence is not a missed ball. It is that **one ball produces several rising/falling
pairs**, and the transit between the first rise and the last fall is no longer measurable
from the edges alone: the notches between fragments are real time that the counter did not
count, and their widths are not recoverable after the fact.

### Firmware mitigation, already in place

`detect.c` coalesces fragments arriving within a tunable window and reports the **fragment
count** per pass. A chattered pass reports its comparator transit as an explicit **lower
bound** rather than pretending to a number it does not have, and the ADC-derived transit —
which works from the bump's own shape — carries that pass instead.

That is a correct answer, not a workaround: Phase 4 shows the ADC path is the more accurate
one anyway. But it costs the comparator path's whole reason for existing on those passes,
which is a ~1 µs deterministic answer with zero CPU.

### The fix

**One resistor from `D_Comparator` back to U15 pin 3.** With `Net-(U12B-OUT2)` driven from
U12B's output through R101's feedback network, positive feedback of a few tens of mV is
plenty:

```
V_hyst ≈ (V_OH − V_OL) × R_src / R_fb
```

R102 is already 1 k in series to ADC5; sizing R_fb around 1 M against U12B's output
impedance gives tens of millivolts of hysteresis, comfortably above the noise and far below
the smallest bump worth detecting. **Size it from the σ that `threshold sweep` measures** —
the 10–90 % width of the comparator's S-curve is exactly the input-referred noise this has
to exceed.

⚠ Note U15's output is **open collector** pulled to +3V3 by R103, while pin 3 swings toward
+5VA. The feedback network sees a 3.3 V swing, not 5 V — size against that.

### Priority

🔴 for a board respin. The firmware answer works and Phase 4 may well show the ADC path
should be primary regardless — but a detector whose fast path is unusable on slow targets is
carrying a defect, and this is one resistor.

---

## CR-14 — 🟡 U15's input common-mode ceiling is below what U12B can drive

### Why

**U15 (LM393) is powered from `+5V`, the digital rail** — not `+5VA`. Its input common-mode
range on a 5 V supply reaches only about **V+ − 1.5 V ≈ 3.5 V**.

**U12B (OPA4323) is rail-to-rail on +5VA** and will drive U15 pin 3 to ~5.2 V on a large
signal. Above ~3.5 V the comparator's inputs are outside their valid range, where LM393
behaviour is undefined and some parts **invert**.

An inverting comparator mid-transit produces a spurious falling edge, which the firmware
would see as the ball leaving the beam early — a short transit and an over-estimated speed,
on exactly the brightest, closest targets where confidence would otherwise be highest.

### What bounds it today

ADC5 saturates at code 4095 (3.3 V) before D14 conducts at ~3.6 V, so **ADC saturation is a
conservative early warning** that the comparator is approaching its limit — it fires below
the CM ceiling, from the same sample. `detect_refine()` flags it as `DQ_SATURATED`.

That is a detector, not a fix. It says the measurement is suspect; it does not keep the
comparator in range.

### The fix, in preference order

1. **Run U15 from +5VA and add a divider or clamp on pin 3** so the input cannot exceed
   V+ − 1.5 V. Also removes a digital-rail-to-analog-input coupling path that nothing else
   in the chain has.
2. **Swap U15 for a rail-to-rail-input comparator** (e.g. TLV3201/TLV7011 class). Cleaner,
   and would let CR-13's hysteresis resistor be sized without worrying about the input range
   at the same time.
3. **Keep the signal small.** Choose the U12B gain so the largest expected return stays under
   ~3.3 V — which the `cal gain` command already recommends, but it makes the gain choice a
   safety constraint rather than an SNR optimisation.

### Priority

🟡 — real, but it only bites on large signals, and option 3 avoids it at the cost of
dynamic range. **Measure the actual ceiling first:** sweep the threshold against signal
amplitude and find where GPIO46 stops tracking ADC5. That number decides whether this is
urgent or academic.

---

## CR-15 — 🟡 Beam coupling saturates the TIA — **MITIGATED ON THE BENCH 2026-08-19**

> ### ✅ FIXED with baffling — linear to 25 % duty
>
> Duty sweep with good baffles and hands clear of the optical path. `capture 0x04 400 500000`:
>
> | duty | min | max | swing | rails? |
> |---|---|---|---|---|
> | 2 % | 3084 | 3253 | 169 codes = 136 mV | no |
> | 4 % | 2750 | 3285 | 535 = 431 mV | no |
> | 8 % | 2373 | 3374 | 1001 = 807 mV | no |
> | 12 % | 1891 | 3418 | 1527 = 1231 mV | no |
> | **25 %** | **2076** | **3606** | 1530 = 1233 mV | **no** |
>
> **No saturation at any duty.** Pulse depth below the off-level fell from **2.58 V to
> 0.132 V at 2 % — about 20x.**
>
> Three independent checks that the measurement is sound:
>
> - **The servo signature tracks the model.** V_off = 2.59 + f x dV predicts the rise, and the
>   measured off-levels are 2.617 / 2.641 / 2.688 / 2.733 / 2.876 V for 2/4/8/12/25 %, against
>   2.589 V with the beam off.
> - **The excursion stops growing after 12 %** (1527 vs 1530 codes at 12 % and 25 %). Once the
>   pulse exceeds the TIA's 235 ns time constant it settles fully, and amplitude is then set by
>   *peak* photocurrent - which Phase 2 independently showed is flat against duty.
> - Captures are stable to a code or two, where the pre-baffle "control" moved 320 codes
>   between sessions.
>
> ⚠ **Low-duty numbers understate the excursion.** 500 ksps against 104.1667 kHz is exactly
> 4.8 samples per period, so the sampling is coherent and only **24 distinct phase points** are
> ever visited. At 2-4 % the pulse (192-384 ns) is far shorter than the 2 us sample interval,
> so the true bottom is rarely caught. **At 25 % it does not apply** - the settled 2.4 us pulse
> is wider than the interval - so the 2076 figure is trustworthy, which is the one that matters.
>
> ### CR-16 is now the binding constraint, not this one
>
> | | margin at 25 % duty |
> |---|---|
> | min 2076 -> op-amp rail (0 V) | 1.67 V |
> | max 3606 -> **ADC ceiling (3.3 V)** | **0.39 V** <- tightest |
> | max 3606 -> op-amp rail (5.2 V) | 2.30 V |
>
> Exactly as predicted: the ADC clips before the op-amp rails. 0.39 V is not much, so a
> brighter target or more ambient could still clip **ADC2**. That is the health monitor rather
> than the detection path, so it is a nuisance rather than a blocker - but read future TIA
> captures with it in mind.
>
> **Priority dropped from red to yellow.** The bench is unblocked and Phase 3 can proceed. The
> board change is still worth making, because the fix currently depends on baffling and on
> operator discipline about hands.

### Original finding (retained): beam coupling saturates the TIA above ~2-3 % duty — CONFIRMED ON TWO BOARDS

> ### ⚠ CORRECTION 2026-08-19 — only the BOTTOM clipping is real
>
> Every beam-on ADC2 capture was read as the op-amp railing at **both** extremes. That is wrong.
> **`4095` is the ADC's 3.3 V ceiling, not the op-amp's 5.2 V rail** (CR-16). Confirmed on a
> logic analyser in analog mode at 50 MS/s: TP7's top is clean, with well over a volt in hand.
>
> **The bottom clipping at ~0 V is real**, and here is why the asymmetry is inherent rather
> than surprising:
>
> 1. The TIA is **inverting**, so the beam pulse is always a **downward** excursion.
> 2. The DC servo forces the **mean** of TIA_Out to 2.59 V, not its resting level. With the
>    output slammed low during each pulse, the off-level must sit **above** 2.59 V to average
>    out: `(1 - f) x V_off = 2.59`, where f is the fraction of the period spent depressed.
> 3. The crosstalk swing (~2.5-3.8 V) exceeds the room below V_off (~2.6-2.9 V), so the bottom
>    rails while the top has ~2 V spare.
>
> **Checked against measurement.** At 2 % duty the pulse is 192 ns plus a ~700 ns recovery tail
> (3 x the TIA's 235 ns time constant), giving f = 0.093 and `V_off = 2.59/0.907 = 2.86 V`.
> **Measured off-level: 3540 codes = 2.85 V.**
>
> V_off rises with duty — roughly 3.2-3.4 V at 12 % and ~3.8 V at 25 % — which is why the top
> eventually reads 4095. **That is the ADC clipping, with the op-amp still ~1.4 V from its rail.**
>
> ### The bottom clipping still matters, and this is the reason
>
> It is tempting to dismiss it since the useful signal sits higher up. It cannot be dismissed:
> U13/U12A is a **synchronous demodulator**, correlating the signal against the carrier across
> the whole period, pulse included. **A ball reflects extra light during the LED's on-time — and
> if the TIA is already railed at 0 V through that interval, the extra light produces no extra
> output.** The detector is blind at exactly the moment the ball signal exists.
>
> So CR-15 survives, at half its original size: the ceiling numbers below are correct, the
> mechanism is correct, and only the "both rails" characterisation was instrument artifact.

> ### ✅ Confirmed as a DESIGN property, 2026-08-18
>
> Board 2, independently assembled, run through the identical duty sweep with no optical baffle:
>
> | duty | board 2 TIA_Out | board 1 TIA_Out |
> |---|---|---|
> | 2 % | **339 - 3672 linear** | 597 - 3709 linear |
> | 3 % | **8** - 3680 railed | 98 - 3791 linear |
> | 4 % | 5 - 3775 railed | 5 - 3798 railed |
> | 6 % | 4 - 3981 railed | 4 - 4095 both rails |
> | 8 % / 10 % / 12 % / 25 % | 3 - **4095** bottom railed; top is the ADC ceiling, not the op-amp | - |
>
> **Highest linear duty: board 1 = 3 %, board 2 = 2 %.** Same ceiling within one measurement
> step, and the crosstalk swing at 2 % agrees closely (2.51 V vs 2.69 V).
>
> **Two independent boards saturating at the same duty rules out an assembly artifact.** The
> front end has no meaningful headroom for beam coupling by design, and the fix is required
> rather than optional. Board 2 being marginally worse is ordinary unit-to-unit variation in
> LED output, photodiode responsivity and mechanical spacing.
>
> ⚠ This confirms the **magnitude**, not the **mechanism**. The run was without a baffle, so
> optical crosstalk and beam-current coupling both remain in play. See the correction below,
> and the resistor-substitution test that settles it.

### The measurement, 2026-08-17

`capture 0x04 400 500000` (ADC2 = TIA_Out) with the beam running, no target:

| beam duty | TIA_Out codes | state |
|---|---|---|
| **2 %** | 600 … 3709 | linear |
| **10 %** | 2 … 4095 | **saturated, both rails** |
| **25 %** | 3 … 4095 | **saturated, both rails** |

> ### ⚠ CORRECTION 2026-08-17 — the mechanism is OPEN, not settled
>
> This CR originally asserted the coupling was optical, on the grounds that saturation scales
> with duty while edge-coupled pickup would be duty-independent. **That reasoning is wrong.**
>
> The duty scaling is fully explained by **TIA settling** — the light pulse is shorter than
> the 235 ns feedback time constant (R80 470 k × 0.5 pF, a 677 kHz pole) at low duty, so the
> output never reaches full amplitude:
>
> | duty | pulse width | settling |
> |---|---|---|
> | 2 % | 192 ns | 56 % |
> | 3 % | 288 ns | 71 % |
> | 4 % | 384 ns | 80 % |
> | 8 % | 768 ns | 96 % |
>
> **An electrically coupled current pulse at the summing node would be exactly as wide as the
> LED on-time and would settle identically.** So the duty sweep shows only that the
> disturbance is a pulse synchronous with the drive — which both mechanisms produce.
>
> **Two direct optical tests came back negative and were discounted at the time:** shading the
> photodiode changed nothing (explained away as retroreflection), and a baffle changed nothing.
> On the evidence as it stands, **electrical coupling is now the more likely explanation.**
>
> Candidate electrical paths, none yet excluded:
> - **+5VA is fed from +5V through FB2**, and the beam puts 3 A pulses on +5V. U11's PSRR at
>   104 kHz is poor, and because C66 filters +2V5 at 31.8 Hz the virtual ground does *not*
>   follow — so the op-amp sees the full rail step relative to its own reference.
> - **D12's cathode sits on VIR through R77 10 kΩ.** VIR ripple couples through the
>   photodiode's junction capacitance (~10–20 pF at 36 V ≈ 100 kΩ at 104 kHz) directly into
>   the summing node; 100 mV of ripple injects ~1 µA.
> - Stray capacitance from the switching node into the 470 kΩ summing node. ~0.1 pF against a
>   5 V/50 ns edge is enough to inject 10 µA.
>
> **Discriminating test:** drop the carrier to ~10 kHz and capture ADC2 at 500 ksps, giving 50
> samples per period and no aliasing. Optical crosstalk is a pulse as wide as the LED on-time
> (~12 samples at 25 %); edge-coupled pickup is a 1–2 sample spike at each transition with a
> flat level between. **Do this before committing to any fix** — a baffle is worthless if the
> path is electrical, and the two fixes share nothing.
>
> ⚠ Note also that 850 nm passes through many materials that look opaque, including most black
> plastics. A negative baffle result is only trustworthy with foil, thick card or anodised metal.

At 2 % the crosstalk swing is 3109 codes = **2.50 V**, so the leakage photocurrent is
2.50 V / R80 470 kΩ = **~5.3 µA peak**.

### Why it saturates

The TIA idles at 2.59 V with about ±2.6 V of headroom. **A 2.50 V crosstalk swing uses 96 %
of it at the lowest usable duty.** There is essentially no headroom for crosstalk at all.

The DC servo then makes it worse as duty rises. It nulls the *mean*, so wider pulses push the
baseline up to compensate — driving the off level toward the positive rail while the on level
already sits near the negative one. Both extremes rail, which is the `4095 / 2` pattern above.

### Why it matters far more than a saturated front end usually would

Saturation does not stay contained in the TIA:

1. The demodulator receives a **full-scale square wave** instead of a small signal.
2. The 4th-order LPF is built from OPA4323 sections whose slew rate is ~1.5 V/µs. A 5 V step
   needs ~3.3 µs against a 9.6 µs carrier period, so **the filter op-amps slew-limit**.
3. **A slew-limited filter stops filtering.** It passes large signals through as triangles
   rather than attenuating them.
4. Measured result: **313 mV p-p of carrier on ADC5**, where the small-signal prediction
   (66 dB of rejection at 104 kHz) says ~18 mV. The 18× discrepancy *is* the slew limiting.

Consequences if not fixed:

- **Any comparator threshold below ~250 mV chatters at 104 kHz continuously.** That is not a
  detector.
- 313 mV is ~4× the measured ambient-flicker floor, so it would dominate every `scan carrier`
  SNR figure — the exact quantity that scan exists to compare.
- It would not announce itself. The numbers would simply be wrong.

### The fix

⚠ **Conditional on the mechanism — see the correction above. Establish that first.**

*If optical:* **a baffle between D11 and D12** — a mechanical change, not a PCB one. Crosstalk and signal are the same physical quantity
(reflected 850 nm light), so it cannot be filtered electrically; it has to be stopped
optically before it reaches the photodiode.

Sizing: the isolation needed is the ratio between the current crosstalk and the amount that
leaves adequate headroom at the intended operating duty. Take the max-linear-duty measurement
first and size from that rather than guessing.

Secondary options, both with real costs:

| Option | Cost |
|---|---|
| Reduce R80 below 470 kΩ | Lowers crosstalk swing *and* ball sensitivity by the same factor — no net SNR gain |
| Run at 2 % duty | Linear, but ~12× less optical power and therefore ~12× less signal |
| Faster op-amps in the LPF | Treats the symptom; the TIA is still saturated and its output is meaningless |

### Verify

`capture 0x04 400 500000` at the intended operating duty: no sample at 4095 or near 0.
Then `capture 0x20 400 500000` should show the carrier gone from ADC5, not merely reduced.

### ✅ MECHANISM CONFIRMED OPTICAL, 2026-08-19

Established with improved baffles: **the vast majority of the coupling is optical.** The
resistor substitution was not needed. The mitigation ranking below therefore applies as
written, and the aperture option is the one that matters.

> ### 🔴 A large part of it was the OPERATOR'S HAND
>
> A hand held in front of the board reflects the LED straight back into D12. **This is what
> made the earlier baffle comparisons uninterpretable** - the no-baffle control moved 320
> codes between sessions (339 then 19 at 2 % duty) with nothing nominally changed, and a hand
> at a varying distance is precisely a variable reflector.
>
> **Consequences for every optical measurement on this board:**
>
> - **Keep hands, arms and torso clear of the optical path while capturing.** Trigger captures
>   from the keyboard and stay back.
> - Anything reflective in front of the board is part of the experiment: bench surfaces, the
>   scope, a coffee cup.
> - This is a strong argument for the **aperture / field stop** mitigation, which rejects
>   off-axis returns by construction rather than relying on the operator's discipline.
>
> It also explains, in hindsight, the drift that was blamed on thermal state and ambient light.
> Neither was wrong as a mechanism; this one was simply larger.

### Mitigation options - ranked

#### The framing that ranks them

**Crosstalk and ball signal are the same light** - 850 nm, same emitter, same detector. So
nothing that scales *both* improves detection; it only moves where the chain clips. Options
fall into two categories, and only one of them is a fix:

| category | what it does |
|---|---|
| **Change the ratio** (geometry) | the only real fix |
| **Restore linearity** (gain, duty, attenuation) | makes the chain usable again, ratio unchanged |

#### 1. Aperture or field stop over D12 - the only ratio fix ⭐

Crosstalk and signal differ in **direction**, not wavelength or amplitude. Crosstalk is
near-field leakage from an emitter millimetres away; the ball is at a specific distance and
angle. **An aperture restricting D12's acceptance angle to where the ball actually is
attenuates off-axis crosstalk far more than on-axis signal.**

This is the only intervention that improves the quantity that matters. Everything else below
just buys headroom.

#### 2. Lower R80 (Rf) - the best linearity fix, and close to free

> ### TRIED ON HARDWARE 2026-08-25 (board 3) - CONFIRMED, and it over-delivered
>
> **154 kOhm soldered in parallel with R80, and 100 pF C0G across ONE leg of the C68/C70
> series pair.** Calculated result:
>
> | | stock | board 3 |
> |---|---|---|
> | Rf | 470 kOhm | **116.0 kOhm** (4.05x less) |
> | Cf | 0.500 pF | **0.990 pF** |
> | TIA pole | 677.3 kHz | **1.386 MHz** |
> | lag at 104.1667 kHz | 8.74 deg | **4.30 deg** |
> | DC servo corner | 2.267 Hz | **0.559 Hz** (tau 285 ms) |
>
> **The compensation ratio is essentially UNCHANGED, which is worth knowing before anyone
> reads this as a stability fix.** Against the table below (Cin ~20 pF, OPA4323 8 MHz GBW):
> Cf/Cf_required was **0.5/0.92 = 0.54** stock and is **0.99/1.85 = 0.53** now. Expect
> **similar overshoot at roughly twice the frequency** (the ~700 kHz ring measured on board 2
> should move toward ~1.4 MHz), not less ringing.
>
> **To actually damp it**, add ~1 pF across the OTHER leg as well: legs of 101 pF and 2 pF give
> Cf = **1.96 pF** against the 1.85 pF wanted, i.e. maximally flat. That is the change to make
> if the TP7 edge still peaks.
>
> **MEASURED 2026-08-25**, and every quality gate passed (sat 0 %, quad null 0.0,
> h3 0.101 against an intrinsic 0.111, PURE DELAY, model within 3.5 ticks of `cal demod`):
>
> | at 104166 Hz | board 2 (stock) | **board 3** | predicted |
> |---|---|---|---|
> | `demod_phase_ticks` | 1343 | **1311** | 1325 |
> | chain delay | 553 ns | **340 ns** | 435 ns |
>
> 🔴 **The phase moved 1.80x further than the pole change alone predicts** - 32 ticks against
> 17.8. Splitting the TIA's own contribution out leaves a **non-TIA delay of 320 ns on board 2
> and 225 ns on board 3**, i.e. a 95 ns board-to-board difference in a path nobody has measured
> (GPIO31 -> U10 -> U9 -> FET -> D11).
>
> 🔵 **The experiment could not separate the two**, because board 3 was never calibrated
> before the rework. **A rework acceptance test needs a BEFORE run on the SAME board** - it
> costs 26 s. The measurement that would settle it outright is two LA channels on GPIO31 and
> TP7, read directly.
>
> ⚠ **Model quality did drop:** residual 0.23 deg on board 3 against 0.07-0.08 deg on board 2,
> and the delay now trends +8.4 % across 80-200 kHz where board 2 gave -3.5 % and -0.05 %.
> Both are inside what the span figure can resolve (see the 8/25 rows in `PROGRESS.md`), but
> the 3x worse residual is worth watching if more boards get reworked.

Currently 470 kOhm. Reducing it scales crosstalk and signal down together, restoring
linearity without touching the ratio.

**It costs almost nothing in SNR, because shot noise dominates.** With the measured ~8 uA of
crosstalk photocurrent:

```
shot noise (8 uA)      sqrt(2qI)      = 1.60 pA/rtHz
R80 thermal (470 kOhm) sqrt(4kT/Rf)   = 0.19 pA/rtHz     <- 8.5x smaller
```

Signal and shot noise both scale with Rf at the output, so **shot-limited SNR is unchanged**.
Only Rf's own thermal contribution degrades, and it is 8.5x below the floor.

**Bonus: bandwidth.** Rf x Cf sets the TIA time constant. 470 kOhm x 0.5 pF = 235 ns, which is
longer than the light pulse at low duty - so at 2 % only ~56 % of the amplitude ever appears.
Drop to 100 kOhm and tau falls to ~50 ns, so even a 192 ns pulse settles fully.

⚠ **But it changes the compensation, and C68/C70 must be revisited in the same change.**
Stability wants roughly `Cf >= sqrt(Cin / (2*pi*Rf*GBW))`. With ~20 pF of input capacitance and
the OPA4323's 8 MHz GBW:

| Rf | Cf required | Cf fitted |
|---|---|---|
| 470 kOhm | ~0.9 pF | **0.5 pF** - already marginal |
| 100 kOhm | ~2.0 pF | 0.5 pF - **badly under-compensated** |

The board may already be under-compensated at 470 kOhm. **Scope TP7 for ringing on the pulse
edges before changing anything** - if it peaks today, that is worth knowing independently.

*(Cin is an estimate. Confirm D12's junction capacitance from the VBPW34FAS datasheet at the
actual reverse bias, which is 36 V when the supply is set correctly.)*

#### 3. Lower duty - diagnostic tool, not an operating point

Restores linearity, but costs optical power **and** makes settling worse: at 2 % the 192 ns
pulse is shorter than the 235 ns time constant, so amplitude is lost as well as light. Useful
for characterisation. Not somewhere to operate.

#### 4. ND filter on the photodiode - worse than lowering Rf, twice over

**First problem: most ND filters are not neutral at 850 nm.** "Neutral density" means neutral
across the **visible** spectrum. Absorptive glass, gelatin and most photographic ND filters
become substantially **transparent in the near-IR** - an ND8 giving 3 stops at 550 nm can be
close to ND1 at 850 nm. Without transmission data at 850 nm specifically, you may be
attenuating nothing. **This is the same trap as black plastic that is not opaque in the IR.**

**Second problem: it costs SNR where lowering Rf does not.** An ND filter reduces the
*photocurrent*, so it reduces shot noise too - and shot-limited SNR scales as **sqrt(T)**. Cut
the light 4x and lose 2x in SNR. Lowering Rf leaves the photocurrent untouched and is
SNR-neutral for the same linearity gain.

⚠ Also: a **reflective** (metallic-coated) ND could bounce light back toward the LED and create
new coupling paths.

#### Recommended combination

**Aperture for the ratio, plus a modest R80 reduction (with C68/C70 revisited) for headroom** -
keeping duty at 25 % so the optical power is retained.

⚠ **Whatever is fitted - aperture, baffle or filter - mount it clear of the board.** D12's
cathode sits at 36 V through R77, and bare foil laid across it destroyed U11B on board 1
(`PROGRESS.md` section 11).


---

## CR-16 — 🔴 The analog chain runs 0-5.2 V into a 3.3 V ADC

### Why

Every op-amp in the detect chain (U11, U12) is an **OPA4323 on +5VA**, rail-to-rail, so each
output can swing **0 to 5.2 V**. The RP2350's ADC full scale is **3.3 V**.

| node | signal range | ADC sees |
|---|---|---|
| `TIA_Out` (ADC2) | 0 - 5.2 V, idles 2.59 V | 0 - 3.3 V |
| `Net-(U12B-OUT2)` (ADC5 via R102) | 0 - 5.2 V, idles near 0 V | 0 - 3.3 V |

**Roughly 36 % of the available range is invisible to the firmware.** On the TIA monitor it is
worse than that: the node idles at 2.59 V with 2.61 V of headroom above, and the ADC can see
only 0.71 V of it.

### It is not a passive limit — clipping corrupts the reference

Above ~3.6 V the protection diode conducts into +3V3 through the 1 kOhm series resistor:

```
(5.2 - 3.6) / 1 kOhm = 1.6 mA  ->  through R27 (33 Ohm to ADC_AVDD)  ->  ~53 mV of droop
```

**The ADC's own reference sags whenever a monitored signal clips.** At a 104 kHz carrier that
happens every 9.6 us while the ADC samples every 2 us, so during any beam-on capture the
reference is being disturbed continuously. Readings taken near a clipping event are not
trustworthy in either direction.

### What this cost

**It invalidated a chunk of the CR-15 investigation.** Beam-on ADC2 captures were read as
showing the op-amp railing at BOTH extremes. The upper "rail" was the ADC at 3.3 V; the op-amp
was linear with ~1.4 V still in hand. Confirmed on a logic analyser in analog mode at 50 MS/s,
where TP7's top looks clean and only the bottom rails.

It also caps the detect path: `ADC_CH_DETECT` can only measure up to 3.3 V of a signal that
reaches 5.2 V, so **the usable dynamic range is dTP9 <= 228 mV at the as-built x14.5 gain**,
set by the ADC rather than by anything in the analog design.

And the comparator inherits the same asymmetry: `Threshold_DC` is generated by a GPIO PWM DAC
and therefore reaches only **0 - 3.3 V**, while the signal it is compared against swings to
5.2 V. **The upper 1.9 V of the signal cannot be thresholded at all.**

### Options

| Option | Cost |
|---|---|
| **Run the detect chain from +3V3** instead of +5VA | Cleanest match to the ADC, but reduces headroom for the crosstalk that CR-15 is already fighting |
| **Scale the ADC taps** - a divider ahead of ADC2 and ADC5 | Two resistors each. Costs signal-to-noise and needs the firmware scale factors updated in the same commit |
| **Reference the ADC to a higher rail** | Not available; ADC_AVDD is tied to +3V3 |
| **Accept it and document the ceiling** | What we do today, but it must then be stated everywhere a measurement is taken |

⚠ **Whichever is chosen, `detect.h`'s DETECT_SAT_CODE and the dTP9 clip table in
BENCH_P3_DETECT.md must change in the same commit**, or the firmware will keep flagging
saturation at the wrong level.

### Verify

With the beam at 25 % duty, scope `TIA_Out` and compare against `capture 0x04`. The scope
should show the off-level around 3.8 V while the ADC reads 4095. If they agree, the fix worked.

---

## CR-17 — 🟡 The detection decision node has no test point

### Why

`Net-(U12B-OUT2)` is the signal the comparator actually thresholds and the node the ADC
refinement measures. It is the single most important node in the detection chain, and it is
**the only one in that chain with no test point**:

```
U15.3 (+ input) -- Net-(U12B-OUT2) -- U12.7
                                   |- R101.2  (27K feedback)
                                   |- R102.2  (1K to ADC5)

U15.2 (- input) -- Threshold_DC -- TP8        <- has a test point
```

So the **threshold** can be probed but the **signal it is compared against** cannot. Today the
only access is a resistor pad (R102 pad 2 or R101 pad 2) identified by continuity to U15 pin 3,
or U12 pin 7 on a TSSOP-14.

For comparison, TP3 exists for `Net-(U7-E1)` and TP4 for `CurrentSense_ADC` - both less
central to the board's purpose.

### The change

**One test point on `Net-(U12B-OUT2)`.** Same 1.0 mm THT pad as TP1-TP10, placed near TP8 so
the two comparator inputs can be probed together with a two-channel scope. (Placement and pad
rules: CR-24.)

That single addition makes the detection decision directly observable: signal, threshold, and
the margin between them, live.

### Why it matters more than a convenience

ADC5 is this node **through R102 with a D14 clamp**, i.e. a copy truncated at 3.3 V (CR-16). So
without this test point there is **no way to observe the true comparator input at all** - not
by firmware, not on the bench. Every conclusion about detection margin currently rests on a
clipped copy.

---

## CR-19 — TIA feedback resistor and capacitor values

### Why

Requested for the next board revision on **2026-09-30**.

### The change

- Change the **TIA feedback resistor R80 to 120 kΩ**.
- Change the **TIA feedback capacitor to 1.5 pF**.

The current schematic uses C68 and C70 in series for the feedback capacitance. Confirm the
updated capacitor topology and component allocation when implementing the 1.5 pF target;
do not apply that value to both existing capacitors without checking the resulting
feedback capacitance.

### Firmware impact

Recheck detector gain, demodulator phase calibration, and detection thresholds against the
revised feedback network. Update revision-specific hardware references and bench expectations
when the schematic changes.

### Verify

Check the revised schematic and BOM for the 120 kΩ / 1.5 pF targets, then measure TIA
stability, carrier response, and detection margin on the new board.

---

## CR-20 — Reduce the on-board green LED brightness

### Why

The on-board green LED is too bright; brightness reduction was requested on **2026-09-30**.
This request concerns the board indicator, not the external panel ring LED.

### The change

Reduce the green LED's drive current while keeping it clearly visible as an indicator.
Select the revised current-setting component value during the board revision design;
no specific brightness or resistor value has been requested.

### Firmware impact

None expected for a hardware current reduction. This does not replace the separate ready
LED / PWM-channel change in CR-01.

### Verify

Compare the indicator with the current board under the same supply and lighting conditions.
Confirm reduced brightness and clear visibility.

---

## CR-21 — 🟡 Test points on the strobe drive chain, away from +12 V

### Why

The clamped strobe pulse (U5 Q, net `Net-(U5A-Q)`) has **no test point**. Its only exposed
copper is U5 pin 5 (0.35 mm pad, 0.5 mm pitch) and **R61**, an 0805 0 Ω link that sits in U8's
decoupling: R61 pad 2 is **0.58 mm** from GND (R62.1) and **0.75 mm** from +12 V (C54.1, 4.7 µF);
pad 1 is 0.68 mm from GND (R65.1) and 1.15 mm from +12 V. On **2026-10-05 a hand-held probe on
R61 sparked on board 2** (see `PROGRESS.md` §6). A slip from R61 onto C54.1 puts 12 V on U5's
3.3 V output. The Q10 gate (after U8, up to 12.6 V) has the same problem, at R64. This is the
same class of finding as CR-17 (no test point on the comparator node).

### The change

Add test points, each with **≥ 1.5 mm clearance to any other exposed copper** and a nearby GND
point for a probe ground (the consolidated plan for the whole board is CR-24):

- **TP on `Net-(U5A-Q)`** (U5 Q, 3.3 V logic) — the U5 clamp measurement.
- **TP on `Net-(Q10-G)`** (Q10 gate, 0–12 V) — U8 drive level and edges.
- Optionally **TP on `Strobe_Pulse`**, which today is probed at R57.
- **A GND test point within ~5 mm of TP3 and TP4** (added 2026-10-08, data below), and move
  **TP3 away from C56's +12 V pad** (0.59 mm today). The board's only GND test point, TP1, is
  **72 mm** from TP3 with the LM5157 boost between them. Grounding the scope there put ±70–85 mV
  of boost bursts on TP3 (`PROGRESS.md` §6 2026-10-07); on TP4 that reads as ±0.6 A of apparent
  strobe current. Today the workaround is soldered leads on R66's GND pad
  (`BENCH_P6_STROBE.md` 6c, "Leads to solder").

### Firmware impact

None.

### Verify

Each test point is reachable with a 10× scope probe and a ground spring without touching
another pad. 6a.1 can be run with only clip leads on test points.

---

## CR-22 — 🟡 Thermal relief on pads in copper pours

### Why

Every copper pour on this layout connects to its pads **solid** (zone pad connection "yes"),
including the inner GND (In1) and +5V (In2) planes. A solid connection to a large pour sinks
the iron's heat, so a hand-soldered joint can look acceptable and still not wet the pin.

On **board 1, 2026-10-13, both J3 pins were cold** (`PROGRESS.md` §6):
- C26 read 36 V while the J3 screw read 7 V.
- Two 6c staircases ran with the LED bank open-circuit.

The owner reports that the pours make these joints hard to solder. `BRINGUP_NEW_BOARD.md` §1
now inspects them on every board.

### The change

- **Thermal relief for through-hole pads** in every pour (zone properties → pad connections →
  thermal reliefs, or per footprint).
  - Size the spokes for the current: J3 carries the ~9 A strobe pulses (2 strings × 4.5 A), and
    J1 the board's input current.
  - The fill's default spoke (0.508 mm) is a thermal-pad default, not a current rating.
- **SMD power pads that are reflowed may stay solid** where the heat path matters: Q9's tab,
  the LM5157 and U3 exposed pads. Note that on the assembly drawing, so nobody hand-solders them
  without preheat.
- Do it with **CR-06** (larger terminals for the +5 V input and the strobe output): both touch
  J1 and J3.

### Firmware impact

None.

### Verify

- The zone fill shows spokes on every through-hole pad in a pour.
- A hand-soldered J3 wets through to the far side with an ordinary iron.
- The §1 joint inspection passes on the first board built.

---

## CR-23 — 🔴 Q9's gate floats up for microseconds at each turn-on, so every pulse overshoots

### Why

Measured on board 1, 2026-10-13 (`PROGRESS.md` §6), with single 20 µs pulses and the Saleae on
TP4:

| gate | TP3 | turn-on peak | sag | settled |
|---|---|---|---|---|
| 21 % | 2.08 V | 0.34 A at 0.5–1 µs | 0.04 A at 7–9 µs | 0.10 A from ~9 µs |
| 24 % | 2.38 V | **3.3 A for ~2.5 µs** | 0.34 A at 7–8 µs | 0.72 A from ~9 µs |

- TP3 (AC-coupled, 200 mV/div) rises by **more than 0.7 V** at turn-on and holds ~2 µs. It falls
  ~0.2 V below its setpoint by ~7 µs, then snaps back, mirroring the current.
- A 50 µs pulse showed one recovery, then steady current. This is a transient, not an
  oscillation.

**Mechanism** (consistent with every capture; not yet simulated):
- Q9 is a source follower. The 0.135 Ω sense resistor (plus Q10) is meant to limit its current:
  I ≈ (V_TP3 − Vgs)/R. That works only while the gate is held as the source rises.
- U7 (MMDT2227, an unbiased complementary follower inside the LM358 loop; feedback from TP3)
  holds the gate against downward moves, because the NPN conducts. It does not hold it against
  upward ones: the PNP is off across a ±V_BE dead band, and only the LM358 (0.3–0.5 V/µs) can
  bring it in.
  - U7 **can** pull down. The problem is the **transition** into pulling down: U7's dead band,
    plus the LM358's own output-stage crossover when its load current reverses direction.
  - TI documents microsecond-scale recovery for the LM358 in that case, and notes that older
    simulation models omit it (Astra's review, 2026-10-13).
- At turn-on the source rises with the current (3.3 A × ~0.16 Ω ≈ 0.5 V). Q9's ~1.7 nF Cgs
  carries the gate up with it, so Vgs barely falls, and the current runs to Q9's transfer-curve
  value at the full setpoint Vgs.
- After ~2–3 µs the LM358 brings the PNP in and pulls TP3 down. It overshoots low (the sag), and
  the NPN re-engages at ~8–9 µs.
- The ~+3 V TP3 kick after turn-off is the same one-sidedness.

**Consequences:**
- §15's 5–20 µs strobe pulses sit inside the transient.
- At the 9 A setting (TP3 ≈ 4.5–6 V) the turn-on current would be limited only by Q9 at that
  Vgs and by the LED strings, ballast and wiring: plausibly tens of amps for microseconds.
- The firmware's 13 A peak check acts after the pulse, and U5 limits width, not amplitude.
- Board 1's 6c Step 3 stopped at gate 24 % for this.

### No rework hook on the board

- Every part in the gate drive is fitted. There is **no DNP footprint and no 0 Ω link** between
  the LM358 and Q9's gate.
  - The only 0 Ω on the strobe sheet is R61, in the pulse path (U5 Q → U8).
  - The board's DNP options (C9, R11, R98) are elsewhere.
- U6B, the LM358's second half, is unused but tied off in copper (+ to GND, − to its output).
- Usable points:
  - Q9's gate lead and its 2.2 × 1.2 mm pad;
  - R63 (0402, 47 Ω);
  - R66's GND pad, where the G1/G2 leads already are;
  - TP3, but it is the follower output, on the **loop side** of R63.
- Neighbours to respect:
  - **R67 sits 0.65 mm from Q9's gate pad.** Its pads are GND and `PWR_LowSideD`, and a bridge
    across R67 bypasses Q10 and the sense resistors.
  - Three VIR (36 V) vias are ~1.8 mm below-left of the gate pad.

### Options considered

| Option | Fixes the root cause? | Parts (JLC) | Risk | Verdict |
|---|---|---|---|---|
| **A. Gate-hold capacitor** at Q9's gate, behind R63 | **Yes**: divides every charge the gate receives by ~500 | +1 MLCC (1 µF 0805 X7R, C28323, already on the BOM) | Loop stability (modelled below); setpoint changes slow from µs to ~0.1 ms (firmware already waits 20 ms) | **Recommended**, for the rework and the next board |
| B. Class-AB bias for U7 | Partly: removes the dead band | +2–4 basic parts; impossible on board 1 (U7's two bases are one copper node) | Bias current must fit the ~5 mA +12 V shunt (CR-07); the gate can still move at edges | Not needed with A |
| C. Passive gate pull-down | Only at a few mA (to sink the bootstrap charge in < 1 µs) | +1 resistor | The +12 V rail has ~5 mA in total (R15 4K7 from VIR) | Rejected |
| D. Faster op-amp, same footprint | No: it shortens the dead-band crossing, but the gate still floats | Extended part | A capacitive load inside a faster loop | Rejected |
| E. Current-mode loop (feedback from TP4) | Changes the problem | Fast op-amp (extended), compensation | **Windup**: with Q10 open between pulses the loop saturates, and the gate starts each pulse at the rail | Rejected |
| F. Firmware only | No | — | The gate is a DC node; firmware cannot act within a pulse | Rejected (the readback fix is still needed) |
| G. Cascode MOSFET above Q9 (gate at a fixed DC) | No: it stops the drain swing coupling into Q9's gate (Miller), but the dominant push is the **source** rising with current. At turn-on the drain swing actually pulls the gate the other way. | +1 power MOSFET (D-PAK) and a bias network | Its own gate would need holding too | Rejected for this problem (it would share Q9's dissipation, a separate topic) |
| H. Discrete transistor amplifier replacing U6A/U7 | Only with >5-10 MHz loop bandwidth around a 1.7 nF gate and class-A/AB bias | Several transistors and resistors | Stability; bias current against the ~5 mA +12 V budget; Vbe drift on DC accuracy | Rejected: the gate's correct value is DC, so this is a charge-storage problem, not a gain or speed problem |

### What U7 is for, and why it can go

- U7 is a unity-gain current booster between the LM358 and Q9's gate. Its NPN pushes up from
  +12 V and its PNP pulls down to GND. The loop's feedback from TP3 corrects its ~0.6 V drop at
  DC.
- The design note calls it "a complementary emitter-follower inside the loop". The usual reasons
  for such a stage are more gate current than the op-amp gives, and keeping the op-amp from
  driving ~1.7 nF directly.
- **But this gate never needs to move fast.** The setpoint is filtered by the 2.62 ms DAC ladder
  and changes only between bursts.
- And with no bias, only one of its transistors conducts at a time, with a ~1.2 V dead zone
  between them. That dead zone is what lets the gate float during the pulse edge.
- With C_hold, the capacitor supplies every fast charge, and U7 has nothing left to do.
  - Removing it saves an extended part, removes the dead zone, and lets the gate reach a true
    0 V (U7's PNP leaves ~0.6 V).
  - The risk is the LM358's unspecified output resistance. Hence R_iso >= 100 Ohm, the
    "fit exactly one" footprints, and the board-1 stage-2 test before committing.
  - Keeping U7 is also fine (modelled 76-86 deg with C_hold): a cost and simplicity choice, not
    a safety one.

### Why option A works

With C_hold from the gate to the sense-resistor ground, any charge pushed into the gate moves it
by ΔQ / C_hold instead of ΔQ / ~1.8 nF. That covers the bootstrap through Cgs as the source rises,
the Miller charge through Cgd as the drain moves, Q10's edge, and L·di/dt pumping.

Residual errors at 9 A, with ~0.6 µF effective (X7R at bias). 1 mV on the gate is ≈ 4 mA at 9 A,
with gm ≈ 10 S and 0.16 Ω of source resistance.

| Mechanism | Charge | Gate moves | Current error at 9 A |
|---|---|---|---|
| Source rises with current (1.44 V × 1.5 nF) | 2.2 nC | ~4 mV | ~15 mA |
| Source idle level (~3 V) collapses at turn-on | ~4.5 nC | ~8 mV **down**; recovers through R63 (τ ≈ 30–50 µs) | ~30 mA |
| Drain swing (~8 V × ~0.2 nF) | ~1.6 nC | ~3 mV | ~10 mA |
| L·di/dt pumping | a few nC | ~5 mV | ~20 mA |

- The total is ≲ 20 mV, about 1 % at 9 A. **These are predictions, not a demonstrated worst
  case.** (Astra's review, 2026-10-13.)
  - The budget covers charge coupled through the MOSFET only. The driver can also charge C_hold
    through R63 while TP3 is disturbed: 0.7 V at TP3 for 3 µs would move 0.6 µF by ~71 mV.
  - With the gate held, TP3 should no longer swing like that, but this must be measured, not
    assumed.
  - Signs differ by row: the source rising pushes the gate up, its collapse pulls it down.
- Measured today: **≥ 0.7 V** at 24 %. U7's PNP only clamps the gate at about +1.15 V (its dead
  band).
- The fix does not depend on which injection path dominates.

**Effect on pulse edges: none.** (Owner's question, 2026-10-13.)
- Q10 makes the edges. Q9's gate only sets the pulse height, and it is meant to sit still at a
  DC level between bursts. C_hold just makes it sit still.
- **Rise:** Q10 switches in ~50 ns. With Q9's gate held, Q9 is briefly over-driven at turn-on
  (its source starts at its idle level), so the current climbs as fast as the LED loop allows,
  di/dt ≈ headroom / L. At 9 A, ~10 V of headroom over ~0.3-1 µH of loop gives an estimated
  0.3-0.9 µs. Twisted, short leads lower L.
- **Fall:** when Q10 opens, the bank current freewheels through D10 against the LEDs' own
  forward voltage, so it falls in ~0.1-0.3 µs. Q9's gate is not involved.
- **What it does change:** the flat top. Today it carries the 3.3 A spike and the sag to ~9 µs;
  with C_hold it should be flat from the end of the rise. Pulse-to-pulse repeatability in bursts
  improves too, since each pulse moves the gate a few mV and it recovers in ~50 µs.
- **The only thing it slows is a setpoint change:** from µs to ~50-100 µs. The firmware changes
  the gate only between bursts, and waits 20 ms.
- **Minor:** Q10 now also discharges Q9's source node through Cgs at turn-on (~3 V × 1.5 nF ≈
  4.5 nC). That is a ~0.1 A blip lasting tens of ns, invisible to ADC0.

### Loop stability (small-signal model — predictions only)

A linear model. **It omits U7's dead band and the LM358's output-stage crossover, which is the
behaviour being fixed.** Its numbers rank the options; only the bench decides.

**Model (assumptions, not measurements):**
- LM358: GBW 0.7–1.1 MHz, with a second pole set for ~60° unity-gain margin.
- U7's NPN: r_e = 26 mV / (V_TP3 / 30 kΩ).
- Load: the 30 kΩ divider ∥ (R63 + C_hold + ~1.7 nF of gate).
- Feedback: β = 1/3.

| Capacitor | Where | Phase margin over TP3 = 1–9 V |
|---|---|---|
| none (as built) | — | 31–69° (lowest at low TP3); consistent with 6b's pass |
| 100 nF | Q9 gate (behind R63) | 39–77° |
| 470 nF | Q9 gate | 65–84° |
| **1 µF** | **Q9 gate** | **76–86°** |
| 2.2 µF | Q9 gate | 83–87° |
| 1 µF | **TP3, with no R63 in between** | **1–4°: it would oscillate** |

- **Predicted:** bigger is better, as long as R63 sits between the loop and the capacitor. The
  R63·C zero then falls below the loop crossover.
- **Never fit it at TP3.**
- A 1X probe on TP3 (~130 pF) changes nothing.

### Board-1 rework (a bench test of option A)

- **Part:** 1 µF X7R 0805, ≥ 25 V. C24's part (Samsung CL21B105KBFNNNE, 50 V) is ideal.
  470 nF–2.2 µF X7R is **predicted** to work (unverified); do not go below 470 nF, and do not
  use Y5V.
- **Board state:** unpowered (PSU and USB off), VIR < 1 V at C26.
- **Keep U6, U7 and every resistor unchanged** for this first experiment.
- **Gate end:** a short 30 AWG wire soldered to the top of Q9's **gate lead** (the D-PAK lead
  nearest TP3 and R63), close to the body and away from R67.
- **Ground end:** the capacitor's other terminal soldered to **R66's GND pad** (the pad with
  G1/G2, farther from Q10). Wire ≤ 20 mm.
- **Do not use R67's pads**, and keep clear of the VIR vias below-left of the gate pad.
- **DMM before power:**
  - Q9 gate → G1 reads like a capacitor (low, then climbing), not a short.
  - R67, pad to pad, still reads ≈ 56 kΩ.
  - Q9 gate → Q9 source does not read ~0.
- **Verification (revised after Astra's review):**
  1. **Dry, J3 disconnected:** 6b's TP3 stability check, plus **command settling**. With DC
     coupling on the **gate node** (the capacitor junction), record:
     - its response to a setpoint step;
     - its decay after `strobe gate 0`: the voltage at 20 ms, and the time to < 0.1 V.
  2. **Loaded:** 6c Step 2, then re-climb from gate 3 in 3 % steps.
     - **Gate each step on the measured peak current:** stop at a peak > 2x the settled level,
       or > 2 A.
     - 24 % is **not** a pre-cleared level; the modified circuit's transient is unknown.
     - 24 % stays the ceiling until the readback fix.
  - **Probes** (attach with the board unpowered; short grounds on the R66 GND pad):
    - first the **gate node and TP4** together;
    - then **TP3 and the gate node** together;
    - DC coupling for absolute gate voltage and shutdown.
  - **Instruments:** the Saleae Logic Pro 8 analog channel and the TDS on 1X probes are both
    only ~5–6 MHz. For anything faster (the edge spike), use a compensated 10X probe after
    PROBE CHECK, or the MSO54B.
  - **Pass, judged on the gate node and TP4:**
    - the gate holds steady through the pulse;
    - no turn-on overshoot;
    - the current settles within ≤ 2 µs;
    - the 10-90 % rise is no slower than before;
    - no HF oscillation.
  - **The edge spike is investigated, not excluded.** Compare it with the pre-rework captures.
  - **TP3 is secondary:** R63 deliberately lets TP3 and the gate behave differently.
- **Optional second stage, only after stage 1 is understood** (tests the next-board circuit):
  - Remove U7, and bridge a base pad to an emitter pad with **0 Ω** (pads 2 and 1 are adjacent;
    do not touch pad 3, GND, or pad 6, +12 V).
  - TP3 then *is* the op-amp output. R60 senses before R63, and R63 acts as R_iso (47 Ω,
    against the proposed 100 Ω). That is the next-board circuit.
  - **Correction:** I first wrote 100 Ω here. That would put R60's sense point after the new
    resistor, which is a different circuit (Astra's review).
  - Recheck the zero-command decay: without U7, the LM358 sinks only ~50 µA near 0 V.

### Recommended change for the next board

1. **C_hold:** 1 µF X7R 0805 (C28323, already on the BOM) from Q9's gate to the sense-resistor
   ground, ≤ 3 mm from the gate pin, with its own via to the R65/R66 GND copper.
2. **R_iso = 100 Ω** (basic 0402) between the driver and C_hold, taking over R63's role. Add a
   **0 Ω gate-stopper footprint** between C_hold and Q9's gate; fit 2.2–4.7 Ω only if HF
   oscillation appears.
3. **Drop U7** (MMDT2227, an extended part). Drive R_iso from the LM358's output, and take the
   R60/R59 feedback from the op-amp side of R_iso.
   - **Why R60 senses before R_iso:**
     - Sensing after it would put the 1 µF directly at the feedback point, with nothing to
       isolate it. That is the same failure as fitting the capacitor at TP3: predicted to
       oscillate.
     - No DC current flows through R_iso (the gate and C_hold draw none), so the gate still
       sits at exactly the regulated voltage.
     - The two nodes differ only for microseconds during a pulse, and holding the gate through
       those microseconds is C_hold's job.
   - Modelled phase margin: 78–87° for an LM358 output resistance of 100 Ω–3 kΩ, with R_iso
     100–220 Ω. A prediction; the LM358's crossover is not in the model.
   - Fallbacks, in order: keep U7, then a faster op-amp. A true current-feedback loop stays an
     option, but it needs deliberate handling of the interval when Q10 is off.
   - **Fit-one footprints:** U7's footprint is DNP, and a 0 Ω from its base net to its emitter
     net is **fitted** by default. To fall back, fit U7 and remove the 0 Ω.
4. A DNP footprint for a compensation capacitor across R60 (op-amp output → inverting input).
5. **Test points** (the full plan for the board is CR-24):
   - **Keep TP3 on the feedback node** (the op-amp output, before R_iso). Its meaning, 3 × the DAC
     voltage, stays the same, so the firmware's "TP3 ... V" messages and every bench procedure
     stay valid.
   - **Add a new test point on the held gate node** (after R_iso, at C_hold). That is the node
     that sets the current, and the one the acceptance checks use.
   - Add a GND test point within ~5 mm of both, and keep TP3 away from +12 V pads (CR-21; today
     it is 0.59 mm from C56).
   - Topology:
     ```
     LM358 out -> [U7 DNP | 0R fitted] -> TP3 (R60 feedback taps here) -> R_iso 100R
         -> gate node (C_hold 1 uF to sense GND; new gate TP) -> R_stop 0R -> Q9 gate
     ```
6. **LED-bank leads:** a tightly twisted pair, as short as practical. On 2026-10-13, twisting
   cut the turn-on edge spike from 72 to 35 mV and the turn-off lobe from −74 to −22 mV
   (`PROGRESS.md` §6). Specify it in the harness drawing alongside CR-06.

**Net BOM against today's board:**
- −1 extended part (MMDT2227, C41375117).
- +1 placement of C28323, already on the BOM.
- R63's value changes from 47 Ω to 100 Ω: one new basic line.
- +2 0 Ω placements (R_stop and the U7 bypass). Reuse R61's 0805 0 Ω line (C17477) if the
  footprints allow.
- All basic parts, so no new setup fee, and the extended-part fee is saved.

**Trade-off accepted:** the settled current still depends on Q9's Vgs, which drifts with
temperature. The firmware's per-pulse readback and `strobe cal` correct for that. Only a
current-mode loop would remove it, and that brings option E's problems.

### Firmware impact

- The readback fix (`PROGRESS.md` §9, "Half-peak threshold") is still required. It is
  independent of this change.
- **`DAC_SETTLE_MS` (20 ms, sized for the 2.62 ms ladder):**
  - With C_hold behind R63, the gate follows the setpoint with an extra ~50 µs (rework) to
    ~100 µs (R_iso 100 Ω), well inside 20 ms.
  - On zeroing, U7's PNP (or the op-amp) pulls the gate to ~0.6 V within ~0.3 ms. The last
    ~0.6 V then decays through R59 + R60 (30 kΩ × 1 µF ≈ 30 ms).
  - **Predicted, not established:** the final tail starts only once the DAC ladder has fallen
    (~3–7 ms after the command, depending on the starting level). So at 20 ms the gate may still
    be ~0.3–0.4 V.
  - Q9's V_GS(th) minimum (1.0 V) is a test point at 250 µA, not an on/off boundary.
  - **Measure the gate voltage and the resulting current after commanding zero** before the
    dry-pulse interlock's "decayed to zero" model is re-derived and reviewed. Recheck if U7 is
    removed.
  - **Measured on board 1, 2026-10-15** (1 µF 50 V X5R, C_eff ~0.5-0.8 µF; U7 fitted; from 30 %):
    - the gate tracks TP3 within ±3 mV;
    - < 1.0 V at 2.8 ms, **0.35 V at 20 ms**, < 0.1 V at ~69 ms.
    - The current at that gate voltage is still to be confirmed on TP4.
- **Board-1 stage 1 verified, 2026-10-15** (`PROGRESS.md` §6):
  - at 24 %, 0.690-0.712 A from 0.1 µs (before: 3.3 A for ~2.5 µs);
  - a ~20 ns rise;
  - the gate node moves +2 to +6 mV;
  - TP3's kicks are gone;
  - the residual ~1 MHz on TP4 is boost pickup (present with Q10 off).
- Large setpoint steps draw C_hold's charge from the +12 V shunt rail (C54, C24 and the rest).
  - A 5 V step costs ~5 µC, a ~1 V sag that recovers in ~1 ms.
  - Live-mode steps are ≤ 0.3 V.
- Dropping U7 does not change the DAC-to-gate relation.
- Re-run `strobe cal` after any change.

### Verify

- Saleae and scope on TP4, at ~1 A and at the 9 A setting: the current is within ±5 % of its
  settled value from ≤ 1–2 µs after the edge (6c Step 3's pass criterion).
- The held gate node moves < ~20–50 mV at turn-on (TP3 is secondary).
- 6b's TP3 stability check passes: no oscillation at static setpoints, no ringing at pulse edges,
  and no HF oscillation at full bandwidth.

---

## CR-24 — 🟡 Test-point plan for the next board

This consolidates the test-point items of CR-17, CR-21 and CR-23, and adds what the bench
phases have needed since. Those CRs keep their reasons; the placement plan lives here.

### Why

Every bench phase so far has had to probe component pads or solder leads where a test point
should have been:

| Where we probed instead | For | Problem | Source |
|---|---|---|---|
| R33 / R57 | GPIO25 `Strobe_Pulse`: the scope EXT trigger and LA timing in every Phase 6 session | a soldered lead; R57's GND pad is 0.38 mm from its signal pad | `BENCH_P6_STROBE.md` 6a |
| R61 pad 1 | U5 Q, the clamped pulse | **a probe sparked on board 2 (2026-10-05)**; the pads are 0.58–1.15 mm from +12 V and GND | CR-21 |
| R64 pad 2 | Q10 gate, 0–12 V | a soldered lead; Q10's gate pin is 0.53 mm from its drain | 6a |
| R66's GND pad (the G1/G2 leads) | a probe ground near TP3/TP4 | the only GND test point, TP1, is 72 mm away; grounding there put ±70–85 mV of boost pickup on TP3 | `PROGRESS.md` §6 2026-10-07; `HANDOFF.md` §8 |
| Q9's gate lead | the held gate (CR-23) | no access at all | CR-23 |
| R102 pad 2 | the comparator + input, the detection decision node | no test point at all | CR-17; `BENCH_P3_DETECT.md` §3.6 |
| D11 pad 1, with TP5 | beam LED current, differential across R73+R74 | a two-probe differential on component pads | `BENCH_P2_BEAM.md` 2b |
| J5 pins 4/6 | a probe ground for the beam area | no local GND | `BENCH_P2_BEAM.md` |
| J3 pin 1 screw, C26 | VIR (36 V) | the screw is the only safe point, and both J3 joints were cold on board 1 (2026-10-13) | `BRINGUP_NEW_BOARD.md` §2; CR-22 |
| J8.2, J8.1, J8.37 | the latched +5 V rail, the Pi's 3V3, the shutdown handshake | covered once a Pi is fitted | `BENCH.md` Phase 1; `BENCH_P8_PI.md` |
| J4.3, J4.7, J4.8 | the camera trigger and strobe lines | covered once cameras are fitted | `BENCH_P5_P7_MIC_CAMERA.md` Phase 7 |
| R11 pad 1 (empty) | the LM5157 SW node | **fine as it is**: the empty snubber pad is a good probe point | `BENCH_P3_DETECT.md` §3.6 |

And some existing test points sit too close to other nets (edge to edge, from the PCB file):

| TP | Net | Problem |
|---|---|---|
| TP2 | +12 V | **1.92 mm from R15's VIR (36 V) pad.** A bridge puts 36 V on the 12 V rail. |
| TP3 | gate setpoint | **0.59 mm from C56's +12 V pad** |
| TP9 | LPF output | ≈ 0.4 mm from U12 pin 9, an op-amp input |
| TP10 | demodulator output | ≈ 0.8 mm from R91 (`Demodulation_PWM`) |
| TP8 | threshold | ≈ 0.45 mm from a GND via |
| TP5 | `Strobe_GND` | the **name** invites a scope ground clip on a switched drain (CR-11) |

### Rules for every test point

1. **Pad:** THT, ~1.6 mm pad and ~0.8 mm drill, so it takes a hook clip or a soldered 30 AWG
   lead. Use **thermal relief** (CR-22).
2. **Clearance:**
   - ≥ 1.5 mm edge to edge to any other exposed copper;
   - ≥ 3 mm from VIR, +12 V and +5 V copper, unless the test point is that rail.
3. **A GND test point within ~5 mm of every analog or fast test point.** Ideally use
   signal/GND pairs 2.54–5 mm apart, so a probe's ground spring reaches.
4. **Named nets.** No `Net-(U7-E1)`-style auto-names.
5. **Silkscreen** the TP number, net name and nominal voltage, e.g. `TP3 GATE_SET 0-10V`.
6. **Cluster by function** (strobe, power, detect, beam, camera/Pi), so a two-channel scope sees
   related nodes together.
7. **No test point where a probe slip is dangerous** (see "Do not add" below).

### Keep, move, rename: TP1–TP10

**Remove: none.** Every existing test point is used in a procedure: bring-up, 6b/6c, and
Phases 2–3. The hazards are fixed by moving TP2 and TP3 and renaming TP5's net, not by deleting
anything.

| TP | Net | Action |
|---|---|---|
| TP1 | GND | **Keep**, as the DMM and clip ground. It must not be used as a scope ground for the strobe or detect areas; the new local GND test points replace it there. |
| TP2 | +12 V | **Keep, and move** it ≥ 3 mm from VIR copper (R15). Add a GND test point beside it. Used in bring-up and 6b. |
| TP3 | gate setpoint (the op-amp output, before R_iso) | **Keep, and move** it ≥ 1.5 mm from +12 V (C56). Rename the net `GATE_SET`. Its meaning (3 × the DAC) is unchanged. CR-21, CR-23. |
| TP4 | `CurrentSense_ADC` | **Keep.** Add the sense-return pair (below). |
| TP5 | `Strobe_GND` (Q11 drain, the beam LED's low side) | **Keep, and rename** the net (`BEAM_LED_K`, CR-11) and its silkscreen. Add a GND test point nearby: today the nearest GND copper is ~4.4 mm away, and procedures borrow J5's pins. |
| TP6 | +2V5 | **Keep.** It shares the detect-area GND test point. |
| TP7 | `TIA_Out` | **Keep.** |
| TP8 | `Threshold_DC` | **Keep**, ≥ 1.5 mm from other copper, with the comparator + input test point beside it (CR-17). |
| TP9 | LPF output (U12C) | **Keep, and move** it ≥ 1.5 mm from U12's pins. Name the net. |
| TP10 | `Demodulated_Signal` | **Keep, and move** it ≥ 1.5 mm from R91. |

### Add

| Priority | New test point | Net | Why | Source |
|---|---|---|---|---|
| **P1** | STROBE_PULSE | `Strobe_Pulse` (GPIO25, between R33 and U5) | the scope EXT trigger and LA timing reference in every Phase 6 session | CR-21 |
| **P1** | U5_Q | `Net-(U5A-Q)` | the U5 clamp measurement; replaces probing R61 (the board-2 spark) | CR-21 |
| **P1** | Q10_GATE | `Net-(Q10-G)` (0–12 V) | U8's drive level and edges; replaces R64 pad 2 | CR-21 |
| **P1** | GATE | Q9's gate node (after R_iso, at C_hold) | the node that sets the current, and the one CR-23's checks use | CR-23 |
| **P1** | SENSE_RTN | GND at R65/R66's low side | a Kelvin pair with TP4, and the local probe ground for TP3, TP4 and GATE; replaces the R66 solder leads | CR-21 |
| **P1** | COMP_IN | `Net-(U12B-OUT2)`, the comparator + input | the detection decision node, beside TP8 | CR-17 |
| **P1** | GND, one per cluster | GND | strobe, detect, beam and power clusters | `HANDOFF.md` §8 |
| P2 | VIR_MON | VIR **through a 10 kΩ series resistor** (basic 0402/0603) | safe access to 36 V for bring-up and burst droop: a slip cannot short VIR. A 10X probe through 10 kΩ still passes µs-scale droop (~150 ns with ~15 pF). | `BRINGUP_NEW_BOARD.md` §2; 6c Step 5 |
| P2 | BEAM_LED_A | `Net-(D11-Pad1)` (the top of the ballast pair) | with TP5, the differential beam-LED current (0.54 V/A) | `BENCH_P2_BEAM.md` 2b |
| P2 | +5V | the latched +5 V rail | the latch and supply-pull tests with a Pi fitted (today J8.2) | `BENCH.md` Phase 1 |
| P2 | CAM_TRIG, CAM_STROBE_0, CAM_STROBE_1 | `D_Cam_Trigger`, `Cam_Strobe_0/1`, after CR-09's level translation | Phase 7 timing with the cameras fitted (today the J4 pins) | `BENCH_P5_P7_MIC_CAMERA.md` |
| P3 | +3V3, +5VA, +1V1 | the rails | bring-up rail checks without hunting for pads | `BENCH.md` 0.5 |
| P3 | COMP_OUT | `D_Comparator` | pairs with COMP_IN and TP8 for comparator timing | Phase 3 |
| P3 | 5V_IN_ADC | the R46/R47 junction (ADC1) | `adc5vcal` and CR-03 checks, by DMM | `BRINGUP_NEW_BOARD.md` §4 |
| P3 | RPI5_SHUTDOWN | the J8.37 net | the Pi handshake with a Pi fitted | `BENCH_P8_PI.md` |

### Do not add

- **No test point on `PWR_LowSideD`** (Q9's source and Q10's drain). A probe or clip slipping to
  GND there bypasses Q10, U5's clamp and the sense resistors; this is the R67 hazard. Diagnose
  that node from TP4 and the GATE test point.
- **No bare test point on VIR.** Use VIR_MON through its series resistor, and the J3 screw for a
  DMM.
- **No test point on the LM5157 SW node.** Keep using R11's empty snubber pad, with a 10X probe
  and a short ground spring.

### Firmware and docs impact

- **Firmware:** none, except that messages naming test points (`strobe gate` prints "TP3 …")
  should use the new net names.
- **Docs to rewrite for the new test points:**
  - the "Leads to solder" table in `BENCH_P6_STROBE.md`;
  - the probe tables in `BENCH_P2_BEAM.md`, `BENCH_P3_DETECT.md`, `BENCH_P5_P7_MIC_CAMERA.md`
    and `BENCH_P8_PI.md`;
  - `BRINGUP_NEW_BOARD.md` §2.

### Verify

- **DRC:** every test point meets the clearance rules, and every analog or fast test point has a
  GND test point within 5 mm.
- **On the first board:** a 10X probe with a ground spring reaches every signal/GND pair, and
  strobe bring-up (6a–6c) needs no soldered leads.
