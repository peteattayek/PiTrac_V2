# Original-board rework check, one board at a time

Applies to the three additional original boards in [REWORK_ORIGINAL_BOARDS.md](REWORK_ORIGINAL_BOARDS.md):
162 kohm across R80, 2 pF C0G across each original 1 pF C68/C70, and a 1 uF hold capacitor
from the actual Q9 gate to R66 GND. Read the construction plan if any connection is uncertain.

**Use magnification, DMM and scope.** Visual inspection establishes the intended parts and
topology; resistance checks find wrong connections/shorts; waveforms check operation. A
normal DMM cannot certify 1.5 pF on this assembled board. A successful `cal demod` alone
does not prove the feedback capacitor values or the strobe gate transient.

Work through one board, then repeat for the other two. Save a separate log and photos per UID.
This is a rework check, not a report of passing results or a substitute for full board bring-up.

## 1. Make the board safe and inspect the four additions

**State:** PSU and USB unplugged, J2 removed, J8/J4 empty. If previously running, use
`beam off`, `detect disarm`, `strobe live off`, `strobe gate 0`, `off`, then `stat` first;
expect STANDBY/latch 0 before unplugging. With power absent, verify **VIR < 1 V** at both
C26 pad 1 to GND and J3 pin 1 screw to GND, then disconnect J3. C26's other pad is GND,
1.80 mm away edge-to-edge. Do not short stored charge with a tool. Nothing conductive near D12.

Under magnification, confirm:

| Position | Must be present | Must be absent |
|---|---|---|
| R80 | Original 470 kohm plus the added 162 kohm, ends aligned and both ends wetted | Any bridge from one end to the other, extra parallel resistor, lifted original |
| C68 | Original 1 pF plus one added 2 pF C0G, both corresponding ends joined | End-to-end bridge, displaced original |
| C70 | Original 1 pF plus one added 2 pF C0G, both corresponding ends joined | End-to-end bridge, displaced original |
| New 1 uF | One end on R66 **pad 1 GND, farther from Q10**; other end wired to **Q9 gate pin 1**, behind R63 | Connection to TP3, Q9 source/tab, TP5 or R67; exposed wire touching copper |

Keep U6/U7 and all strobe resistors unchanged. Inspect for cracked MLCCs, disturbed nearby
parts, solder balls and residue. **Pass:** every intended joint is visible and clean; no
unidentified or bridged joint. Check the underside too. Do not infer the pF values from
unmarked ceramic bodies: retain the packaging/parts record.

## 2. Do the unpowered DMM checks

**State:** unchanged, fully unpowered/discharged, J3 off. Meter in ohms, not just beeper.
Use the pad map in [REWORK_ORIGINAL_BOARDS.md](REWORK_ORIGINAL_BOARDS.md) for identification.
Shade D12 with an insulating card while measuring the TIA; no foil or metal near D12.

| Measurement | Expected / decision |
|---|---|
| Each added R80/C68/C70 terminal to its corresponding original PCB pad | Approximately the shorted-lead resistance, normally **< 1 ohm**; prove both ends individually |
| Across R80, both meter polarities | The intended isolated network is **120.475 kohm**; with 1% parts approximately **119-122 kohm**. Record the in-circuit reading; semiconductor paths/light can make it differ. A stable near-zero reading is a fault. An ambiguous reading needs inspection, not a forced pass or immediate part removal |
| Across each C68/C70 stack | No stable end-to-end hard short. This checks for a solder bridge, not capacitance |
| New capacitor ground end to R66 pad 1 / TP1 | Approximately shorted-lead resistance, normally **< 1 ohm** |
| New capacitor gate end to Q9 pin 1 | Approximately shorted-lead resistance, normally **< 1 ohm** |
| New capacitor gate end to TP3 | Path through **R63, 47 ohm**, not a direct short. IC paths may affect the reading; resolve ambiguity before power |
| Q9 gate to GND, and gate to source | No stable hard short; the meter may initially charge the new capacitor |
| Across R67 | Approximately **56 kohm**, rather than a new short |
| R61 pad 1 to GND | Approximately **1 kohm**, by 6a Step -1. **20 ohm or less: stop**; an open/missing pull-down also needs repair |

**Important:** R66's other pad is only 0.135 ohm from ground through the sense network. A
normal DMM cannot establish that the capacitor is on the correct end of R66, or reliably
detect a bridge that shorts the sense network. **The pad identity and visual inspection are
mandatory.** Use the large ground pad farther from Q10, not whichever end beeps to GND.

Pass only after the connections and absence of bridges are established. Do not attempt an
in-circuit pF measurement as a substitute for inspection.

### If U11 pins 1-2 read 280 kohm, 160 kohm, or another unexpected value

U11 **pin 1 is the output** and **pin 2 the inverting input**. They connect directly to
R80 pad 2 and pad 1 respectively. Thus the intended resistor pair is directly across those
pins: **470 kohm before the resistor addition, 120.475 kohm after**. The rest of the board
is also connected, so this is not an isolated resistance measurement.

Do not label a high reading as normal IC variation. **Ordinary additional parallel resistance
can lower the reading, not raise it above the intact resistor pair's value.** A higher
apparent reading requires investigating probe contact/pin identification, residual/applied
voltage or light-generated photodiode current, meter behavior, wrong parts or open joints.
A reading near 162 kohm could be consistent with only the added resistor being connected,
but does not by itself establish that fault. An unexpected reading does not establish IC damage.

1. Confirm whether the added 162 kohm resistor is actually fitted on each measured board.
2. Unplug **both PSU and USB**, verify discharge as in step 1, and use an insulating card
   to shade D12. Never perform ohms measurements on a powered board or put metal near D12.
3. Check a loose spare 162 kohm resistor to establish the part value and meter behavior.
4. Measure directly across the two R80 PCB end pads, wait for a stable reading and repeat
   with the meter leads reversed. Record both values, whether they drift, the meter model
   and range. R80 is about 0.42-0.46 mm from nearby capacitor/IC pads: unpowered probes only.
5. If the R80-pad result and U11-pin result differ, check contacts and continuity with all
   power absent: **U11.1 to R80.2** and **U11.2 to R80.1**, normally < 1 ohm each. U11 is
   0.65 mm pitch, so keep the tips from touching adjacent pins; no live pin probing.
6. Inspect both ends of the original and added resistors. Report the results before
   unsoldering anything or proceeding to powered acceptance. Definitive isolation, if
   required later, is a separate controlled rework step rather than the first response.

## 3. Check firmware and the board's power prerequisites

**State:** J2 off, J3 disconnected, J8/J4 empty; PSU disconnected initially; USB on J6 only;
main rail down, beam off, detector disarmed. Start serial file logging.

```text
id
cfg
stat
pins
strobe
```

Record label and UID. For powered testing use the existing reviewed **r5**, version 1.1,
Release **`Oct 20 2026 14:59:52`**, 224720 text / 0 data / 422112 bss; UF2 SHA256:
`AC5F4493F5A4C7E8B9B1A387BE128F42055410AFADC9A5B58C092ADBC272ACF7`.
If different, **reflash first** by [HANDOFF.md](HANDOFF.md) section 4, after saving old `id`
and `cfg`; then repeat these checks. Do not rebuild or use `build_hang`. A partition-table
installation is not part of this rework check; never load pre-1.1 firmware onto a partitioned board.

**Pass:** STANDBY/latch 0, strobe DRY/gate level 0, GPIO27 SIO output driven 0/pad 0 and
`U5 pulse limiter armed`; no fault. The unconnected E9 inputs read 0 as in the bring-up guide.
If a previously calibrated board reports defaults unexpectedly, stop before saving config.
Defaults on a genuinely never-calibrated board mean it still needs its own calibration.

**An uncommissioned board must first complete [BRINGUP_NEW_BOARD.md](BRINGUP_NEW_BOARD.md)
sections 1-5**, including rail smoke test, its own ADC5V calibration and latch/supply guards,
and the beam sanity/clamp/thermal checks in section 6 before sustained optical work. These
boards do not inherit another board's calibration or passes. Skip completed checks only when
their results for this particular board are recorded and no new fault is present.

## 4. Check the optical rework with the strobe load disconnected

**First, attach leads with PSU and USB OFF and VIR < 1 V.** Scope 10X probe on a secured
TP7 lead; use a short local ground lead on **C72 pad 2, GND**, verified to TP1 while
unpowered. C72 pad 2 is at (82.450, 152.050) mm, near TP7. Its own signal pad is 0.90 mm
away and R82's ADC pad is about 0.85 mm away: use a wire soldered unpowered, no live tip.
TP7's nearest different-net pad is R95.2, about 2.12 mm away. Do not probe R80/C68/C70 or
the photodiode summing node. Use only board GND for probe ground.

**Powered state:** J2 off, J3 disconnected, J8/J4 empty; PSU **5.20 V / 3 A** on J1 plus
USB J6; strobe DRY/gate 0; detector disarmed; no target, insulating baffle/aperture fixed,
hands clear. Read `stat`; issue `on` only from STANDBY and wait **3 s**. Then:

```text
beam off
detect disarm
adcmode idle
hpf track
adc 2 256
```

Wait **5 s** after the beam/HPF change before taking the final ADC reading. Expect ADC2
about **3212 codes / 2.59 V** and TP7 near **+5VA/2**, approximately 2.59 V at this supply.
If grossly off-center, railed or oscillating, stop and inspect before enabling the beam.

Set the carrier and start low:

```text
beam freq 104166
beam duty 2
beam on
```

At each duty **2, 4, 8, 12, 25%**, issue `beam duty <percent>`, wait **at least 5 s**, then:

```text
capture 0x04 400 500000
```

Scope: start **2 us/div, DC coupled, 20 MHz bandwidth**, at least 100 MS/s; trigger on the
TP7 waveform. At 25% expect a **9.6 us period and approximately 2.4 us illuminated interval**.
Zoom an edge to **200-500 ns/div** and record overshoot as percent of the optical swing and
ring settling time in us. Also take a full-bandwidth view and compare suspect pickup with a
beam-off reference using the same leads/settings. Probe menu and physical attenuation must agree.

**Pass:** a real beam-synchronous response with neither clipping nor sustained oscillation;
at 25% the ADC2 capture has no sample at 4095 or below approximately 20. The pulse has a
settled body rather than ringing throughout its illuminated interval. Record any decaying
edge peaking for review; there is no measured universal ringing-amplitude acceptance limit
for this exact 1.5 pF assembly yet. The nominal feedback RC is **180.7 ns**, not a guarantee
of the measured rise/settling time. **Do not use the 500 ksps CSV to judge sub-us edges.**

**Distinguish the ADC monitor from the TIA itself:** ADC2 reaches 4095 at nominal **3.3 V**,
while the TIA runs on **+5VA**, approximately 5.2 V. An upper-clipped ADC2 capture alone does
not prove that the TIA reached its positive supply rail; inspect **TP7 on the scope**.
Conversely, samples around 20-40 codes (about 16-32 mV) can already be close to the op-amp's
negative limit even without a literal zero ADC code. At this fixed carrier and 500 ksps,
there are only **24 sampled carrier phases**; the 2-8% pulses can have minima between them.
An unclipped low-duty CSV is therefore not sufficient proof of linear operation. Excess
photocurrent and an incorrect effective Rf can both cause clipping; resolve suspect resistor
connections using section 2 before changing values based on optical amplitude alone.

If the captured signal is too large, use the established insulating aperture/baffle setup;
do not change component values to make this check pass. Nothing conductive near D12.

## 5. Recalibrate the optical chain and save it

**State:** same as step 4, beam at **104166 Hz / 25%**, HPF TRACK, detector disarmed, J3 off,
no target, fixed aperture if needed. Warm at 25% for **at least 5 minutes**. Then:

```text
cal demod
cal model
beam
```

**Pass:** `warm: yes`, no `*** FAIL ***`/`NOT COMMITTED`, a committed phase, aim for
**50-70% ADC full scale and 0% saturation**, and all five model points accepted. Record
delay in ns, phase, duty, residual and model verdict. Use this board's results, not 1311 or
another board's phase. `beam` must show the restored **104166 Hz / TOP 1439**.
If quality fails or ringing/model results are unresolved, keep the board pending for review.

```text
beam off
detect disarm
cfg save
cfg
off
stat
```

Expect saved config readback, then **STANDBY/latch 0**. Unplug PSU and USB, check VIR < 1 V
again, then move probes for the strobe checks. Disconnect the optical probe and its C72 ground
connection from the scope; all scope grounds move to R66 GND for the strobe measurements.
Allow the beam/heatsink to cool before handling.

## 6. Check gate DC and decay, with J3 still disconnected

**Attach probes unpowered:** CH1 TP3, CH2 the **actual Q9 gate at the new capacitor's free
terminal**. Both grounds on short leads from **R66 pad 1 GND**. TP3 is **0.59 mm from C56
+12 V**. Q9 gate is **0.58 mm from R67's switched-source pad** and 0.68 mm from R67 GND.
Attach to secured wires, not hand-held live tips. R66 GND has about 4.6 mm to the nearest
different-net pad. Never ground Q9's source or TP5.

**Powered state:** J3 disconnected, J8/J4 empty, J2 off; PSU 5.20 V / 3 A; USB J6; rail up
after `on` from STANDBY and a 3 s wait; beam off, detector disarmed, ADC IDLE, strobe DRY.
Use DC coupling and the 20 MHz bandwidth limit. Send individually, allowing 1 s between:

```text
strobe gate 0
strobe gate 30
strobe gate 50
```

**Pass at steady state:** TP3 and held gate reach approximately **2.8-3.05 V at 30%**,
**4.7-5.1 V at 50%**, without sustained oscillation. They should closely agree after settling;
record their difference. Save both traces, plus a full-bandwidth check for oscillation.
Compare long-window ripple against gate 0, as in 6b, rather than judging a rare pickup spike.

For decay, first return to 30%, wait 1 s, then capture the falling edge:

```text
strobe gate 30
strobe gate 0
```

Use approximately **10 ms/div**, triggered on CH1 falling through about **1.5 V**, and keep
at least 100 ms after the edge. The serial command cannot serve as an exact scope trigger.
Record the held gate **20 ms after TP3's falling-edge trigger** and its time to **< 0.1 V**;
state this time reference in the saved capture. Board 1's measurements, timed from zero
command, were about **0.35 V at 20 ms** and **69 ms to < 0.1 V**: they are comparison values,
not exact targets for a different part/board or for the scope's later trigger reference.

**Pass:** gate returns below **0.1 V** and stays there; no unexpected rise/oscillation. If it
has not done so within the captured 100 ms, or differs materially from the expected decay,
stop for review before live testing. TP3 itself should return to **<= 30 mV**. Wait **1 s**
after zeroing before any dry pulse in this check; a zero register alone does not establish
that the added capacitor has discharged.

## 7. Clear the existing dry-strobe prerequisites before connecting J3

**State:** same powered setup, J3 off, gate 0, actual gate discharged, beam off, detector
disarmed. Complete this board's **6a and 6b** in [BENCH_P6_STROBE.md](BENCH_P6_STROBE.md)
if not already recorded. Their tests establish the limiter, pulse path, interlocks, A7 and
DC setpoint stability; this procedure does not waive them.

Use the 6a lead plan, attaching all leads with both power sources off: R33 for GPIO25 and
R61 pad 1 for U5 Q. R33's nearest GND pad is about 0.52 mm away; R61 pad 1 is 0.68 mm from
R65 GND and 1.15 mm from C54 +12 V. Q10 gate measurement uses the **R64 pad 2 secured wire**,
not a live probe on Q10: nearby other nets are 1.32-1.33 mm away. Use a **10X scope probe**
there; it is about 12 V and must never go to the Saleae analog input. All strobe grounds
remain on R66 GND. The existing 6a/6b procedure contains the exact commands and decisions.

For U5 dry-clamp checks, command **200 us and 1000 us**, three times each, per 6a.1.
**Pass to proceed under the current firmware:** the measured clamp is consistent for both
commands and within the procedure's ready-to-proceed band, **118-137 us**. Report a shorter
or longer clamp and stop before live use; do not alter a safety limit to admit it.

## 8. Prove the hold capacitor works during a low-current pulse

This is **6c Steps 1-3**, following that section's complete board-state table. Only after
steps 1-7 pass on this board: `off`, verify STANDBY/latch 0, unplug **PSU and USB**, check
VIR < 1 V, then connect J3. Never reseat a load or move a probe with either power source on.

**Leads, attached unpowered:** CH1 TP4, CH2 the actual held gate; both probe grounds on
R66 GND. TP4's nearest other-net pad is C54 GND, 2.03 mm away. FLIR on Q9/HS1, with the
existing emissivity-patch practice. Trigger from the existing **R61 U5 Q wire at about 1.6 V**,
on a third scope channel or Aux/EXT trigger, so a gate-0/no-current firing is captured too.
Start around **5 us/div** and **50-100 mV/div on TP4**; offset/zoom the DC-coupled gate
channel around its actual level to measure mV changes. Use **20 MHz bandwidth for current
plateaus**. Take an extra full-bandwidth capture for the first pulse
and compare fast pickup with a gate-0/no-current reference. Use the 6c lead plan.

**Powered state:** PSU 5.20 V / 3 A, USB J6, J2 off, J8/J4 empty, beam off, detector disarmed,
ADC IDLE, rail up/BENCH_RUNNING, gate 0. Verify **36 V at J3 pin 1 screw to GND**, not across
J3; check against C26 by the existing lead plan. J3's return is not board ground.

```text
strobe
adc 0 4096
adc ringtest 100
```

**Pass:** GPIO27 low/limiter armed, no fault; ADC0 **< 10 codes / < 8 mV** at rest;
`adc ringtest 100` prints **PASS: every pair covered**. Then complete 6c Step 1's arming and
refusal checks if not already done on this board. With gate returned to zero:

```text
strobe live on confirm
strobe pulse 20
```

**Pass at gate 0:** a covered/usable record, `within limits`, no current pulse on the scope,
and no current above the approximately **72 mA** detection floor. A fault or unusable record
is a stop, not permission to clear/retry blindly.

Increase individually, checking each shot before the next command; wait at least **1 s**
between manual firings for this check:

```text
strobe gate 3
strobe pulse 20
strobe gate 6
strobe pulse 20
```

Continue in **3% increments**, using the scope and ADC0 at every shot. **Stop increasing at
the first clean 0.5-2 A plateau** and save the waveforms and `strobe wave`. Do not jump to
another board's percentage. For this short rework check, stop for review if a shot's peak
exceeds **2 A** before reaching the test level. If no suitable point is reached by **30%**,
stop and review the load/current record; never raise the gate to force a test point.

**Low-current pass:**

- The 20 us pulse is **20 us +/- 2 us** on the firmware readback, with a usable/covered record
  and `within limits`.
- TP4 = **135 mV/A**: 0.5 A -> 67.5 mV; 1 A -> 135 mV; 2 A -> 270 mV. Scope current and
  ADC0 agree within **5% or 0.1 A**, whichever is larger, per 6c.
- Current settles within **2 us**, with a flat body and no ringing/turn-on overshoot over
  approximately **20% of the plateau**. Investigate fast peaks against the reference;
  the 2 us ADC samples cannot certify absence of a sub-us spike.
- The **actual gate**, not just TP3, stays steady. CR-23's verification expects turn-on
  movement below approximately **20-50 mV**; record the measured excursion.
- Current stops at pulse end and stays off; no fault or unexpected heating. Stop on any
  live-mode exit, unreadable record, disagreement, ringing or unexplained behavior.

If a low-current loaded clamp check has not been recorded on this board, perform **6d at
this same measured level** before declaring the low-current test complete: without changing
the gate, `strobe pulse 20`, then `strobe clamptest 1000`, by the complete 6d procedure.
Current must end at this board's previously measured **U5 clamp**, not 1000 us; the ADC
on-time should agree within **2 us** and the plateau should match, with no fault.

## 9. Shut down and record the scope of the pass

**State:** loaded setup, beam off, detector disarmed. Issue:

```text
strobe live off
off
stat
```

**Pass:** STANDBY/latch 0. Unplug PSU and USB, verify discharge before touching anything.

Record per UID: inspection photos, R80 reading/polarity, 47 ohm gate-path check, saved optical
calibration and TP7 edge capture, gate DC/decay traces, dry-clamp width, low-current scope/ADC0
agreement and actual gate excursion. Mark the batch tracker with what actually passed.

These checks establish assembly and low-current function; they do not independently measure
the exact feedback capacitance or certify behavior at 9 A. **Normal full-current strobe use
still requires the remainder of 6c on each board**, including its own calibration and burst
checks. Higher-current operation is not needed merely to find the usual rework mistakes.
GPIO27 stays 0 throughout; never raise a live-strobe limit to make a test pass.
