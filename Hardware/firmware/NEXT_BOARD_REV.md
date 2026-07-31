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
| **CR-09** | Mira220 1.8 V I/O translation | ⏸ Blocked | TBD | **Conditional on Q6** — may damage the sensor |

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

## CR-08 — 🟢 No PGOOD, no VIR sense

### Why

**All eight ADC-capable pins are committed**, so there is no way to measure the 36 V boost.
Boost readiness is therefore timed **open-loop** in `power_fsm.c` — `RAIL_SETTLE_MS` 250 ms,
covering an ~86 ms soft start plus margin. It works, but the firmware genuinely cannot tell
the difference between "boost came up" and "boost failed and we waited 250 ms."

### The change

Either free an ADC channel, or bring the LM5157's PGOOD (if available in the chosen variant)
to a spare digital input. A VIR tap through a large divider would also let the strobe code
verify headroom before firing rather than assuming it.

Note this competes for the same scarce resource as everything else analog. Listed so the
tradeoff is visible at layout time, not as a recommendation.

---

## CR-09 — ⏸ Mira220 1.8 V I/O translation — **blocked on Q6**

**Do not act on this yet.** J4 drives 3.3 V logic through only 220 Ω into what may be a 1.8 V
sensor domain. Until the specific sensor-board schematic is obtained and Q6 is answered, the
required change is unknown — it could be nothing, a divider, or a full level shifter.

Both directions need answering, and the input direction has a hard constraint: **RP2350 VIH
is 2.0–2.31 V on a 3.3 V rail, so a 1.8 V sensor output will not register without
translation.** See `BENCH_P5_P7_MIC_CAMERA.md` §7.0.

Carried here so it is not forgotten at layout time.

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
