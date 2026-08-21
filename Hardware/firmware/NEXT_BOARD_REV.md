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
| **CR-01** | Ready LED and strobe gate DAC share PWM slice 6A | 🔴 High | 1 trace | Firmware workaround exists, but no PWM dimming on the ready LED |
| **CR-02** | Virtual ground tracks the +5 V rail → false triggers | 🔴 High | 1 part *or* 1 stage | A rail step while armed reads as a ball. `PROGRESS.md` Q8 |
| **CR-03** | R46/R47 100K → 10K | 🟡 Med | 2 parts | ADC read path stays ~5 % low, needs a firmware fudge factor. Q9 |
| **CR-04** | 10 kΩ pull-up on J8.37 | 🟡 Med | 1 part | Reset presents an ambiguous level to the Pi. Q10 |
| **CR-05** | Panel ring LED on always-on power | 🟡 Med | Rework J7 feed | No fault indication whenever the rail is down |
| **CR-06** | J1 terminal block for 14 AWG stranded | 🟡 Med | 1 part + footprint | Cannot land the supply wire you want to use |
| **CR-07** | 12 V shunt regulator burns ~40 mA at idle | 🟢 Low | Redesign | ~31 % of idle power, forever |
| **CR-08** | No PGOOD or VIR sense | 🟢 Low | Needs a free ADC | Boost readiness stays open-loop timed |
| **CR-09** | Mira220 1.8 V I/O translation | 🔴 **Unblocked** | High | **Q6 ANSWERED**: I/O is 1.8 V, no 3.3 V tolerance. Both directions fail; 220 Ω fixes neither |
| **CR-11** | Net `Strobe_GND` is not ground — rename it | 🟢 Low | Rename | A name that invites clipping a scope ground to Q11's drain |
| **CR-16** | **Analog chain runs 0-5.2 V into a 3.3 V ADC** | 🔴 High | Rail or scale | **36 % of every signal is invisible to firmware**, and clipping corrupts the ADC reference |
| **CR-17** | No test point on the comparator input node | 🟡 Med | 1 pad | The detection decision node cannot be probed; TP8 gives the threshold but not the signal |
| **CR-15** | LED→PD crosstalk saturates the TIA | 🟡 **Mitigated** | Baffle (mech) | ✅ **Fixed on the bench 2026-08-19** — linear to 25 % duty with good baffles and hands clear. Board fix still wanted; the workaround relies on operator discipline |
| **CR-12** | Beam LED thermal path caps sustained duty at ~20 % | 🔴 High | **Vias + bigger sink** (fanless target) | Beam runs at 2/3 optical power while armed → worse Phase 3 SNR |

---

## CR-01 — 🔴 Ready LED and strobe gate DAC are the same PWM channel

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

**No — firmware can resolve it with no board change at all**, by driving the ready LED as
plain on/off SIO and giving up PWM dimming on it. That is the planned Phase 6b fix and it
costs nothing.

Make this change only if **PWM brightness control on the ready indicator is wanted.** It is
listed high priority because the *conflict* is high priority, not because the trace move is.

### Firmware impact

- `panel.c`: `PIN_READY_LED` → 13, and LED duty must be computed against the **shared** `TOP`
  that the gate DAC sets, not `PANEL_PWM_WRAP`.
- Slice 6's `wrap`/`clkdiv` become owned by the strobe code. `panel.c` must not reconfigure
  them.
- If the change is *not* made, `panel.c` must stop calling `gpio_set_function(PIN_READY_LED,
  GPIO_FUNC_PWM)` before Phase 6b.

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

## CR-06 — 🟡 J1 terminal block for 14 AWG stranded

### Why

J1 is currently a **Würth 691137710002** (`CONN2_710002_WRE`, 2-position screw terminal). The
3.5 mm-pitch WR-TBL family tops out around **1.5 mm² / 16 AWG** — confirm against the
datasheet, but that is the family limit and it is why 14 AWG stranded will not land.

### The change

A larger block. Specify on **rating**, not only on wire gauge:

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

### Verify

Land 14 AWG stranded with a ferrule, torque to spec, then a thermal image at full load. The
terminal should not be a hot spot relative to the pour.

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

## CR-09 — 🔴 Mira220 1.8 V I/O translation — **Q6 ANSWERED 2026-08-14, no longer blocked**

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

Update `PROGRESS.md` — close Q8 (CR-02), Q9 (CR-03), Q10 (CR-04) and A7 (CR-01), and move
their measurement rows in §6 to reference the revision they were fixed in. Several bench
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

### (original finding, retained)

## CR-15 (original) — Beam coupling saturates the TIA above ~2-3 % duty — CONFIRMED ON TWO BOARDS

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
the two comparator inputs can be probed together with a two-channel scope.

That single addition makes the detection decision directly observable: signal, threshold, and
the margin between them, live.

### Why it matters more than a convenience

ADC5 is this node **through R102 with a D14 clamp**, i.e. a copy truncated at 3.3 V (CR-16). So
without this test point there is **no way to observe the true comparator input at all** - not
by firmware, not on the bench. Every conclusion about detection margin currently rests on a
clipped copy.
