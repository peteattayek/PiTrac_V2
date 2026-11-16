# Original-board resistor and capacitor rework

Prepared 2026-11-03. **A procedure, not a record of completed rework.** Run one board through
inspection and acceptance before repeating on the other two. Board status remains in
[PROGRESS.md](PROGRESS.md), sections 0 and 10; record results there and in the per-board
sign-off table in [BRINGUP_NEW_BOARD.md](BRINGUP_NEW_BOARD.md).

**Owner confirmed:** these are **three additional original boards**, separate from bench
boards 1-3, and the available 2 pF capacitors are **C0G**. The old bench-board damage and
previous-rework exceptions do not apply to this batch. Their own condition and modifications
still need inspection; their UIDs and bring-up history have not yet been recorded here.

**To check completed rework:** use [BENCH_REWORK_CHECK.md](BENCH_REWORK_CHECK.md), the ordered
inspection, DMM, optical and low-current strobe protocol. It distinguishes assembly checks
from functional testing and subsequent full-current acceptance.

## 1. Target and parts

This implements the existing-board equivalent of **CR-19's 120 kohm / 1.5 pF TIA target**, plus
the **CR-23 stage-1 strobe gate-hold capacitor**. See [NEXT_BOARD_REV.md](NEXT_BOARD_REV.md).
These changes do not implement every next-revision request or repair damaged ICs.

| New part | Per stock board | For three stock boards | Installation |
|---|---:|---:|---|
| 162 kohm, 0603, preferably 1% | 1 | 3 | Parallel with the original R80 470 kohm |
| 2 pF, 0402, **C0G/NP0** | 2 | 6 | One parallel with C68 1 pF; one parallel with C70 1 pF |
| 1 uF, 50 V, 0603, **X5R** | 1 | 3 | Q9 gate, after R63, to R66 pad 1 (GND) |

Also needed: magnification, fine tweezers, flux and its specified cleaner, solder, a controlled
iron, short insulated 30 AWG wire, insulation/strain relief, DMM and the existing bench gear.
No additional resistor or capacitor values are needed for these two modifications on healthy,
stock boards. Previously modified or damaged boards need the inventory in section 2 first.

The owner has confirmed that the 2 pF parts are C0G. For the hold capacitor, X5R is
appropriate for this bench rework; board 1's successful trial also used 1 uF / 50 V X5R.
**The exact 0603 part has not been identified or verified under DC bias.** Its capacitance at
the gate's operating voltage, up to about 10 V in dry tests, matters. A 50 V rating alone does
not establish that capacitance. Check the manufacturer's bias curve and perform section 7;
do not assume a fixed effective capacitance from the package or voltage rating.
See [Murata's DC-bias explanation](https://www.murata.com/en-us/support/faqs/capacitor/ceramiccapacitor/char/0005).

The resulting nominal network is:

```text
Rf = 470 kohm || 162 kohm = 120.475 kohm

TIA summing node ----[ 1 pF || added 2 pF ]----[ 1 pF || added 2 pF ]---- TIA output
                                C68                       C70
Cf = 3 pF in series with 3 pF = 1.5 pF

U7 / TP3 ---- R63, 47 ohm ---- Q9 gate
                                  |
                              added 1 uF
                                  |
                             R66 pad 1, GND
```

**Leave the original R80, C68 and C70 fitted.** Replacing R80 with 162 kohm gives a different
gain. Replacing both original capacitors with 2 pF gives only 1 pF total. Adding 2 pF to only
one original capacitor gives 0.75 pF total. Do not bridge the two terminals of either capacitor.

Calculated target: Rf 120.475 kohm, Cf 1.500 pF, feedback RC 180.7 ns, feedback pole 880.7 kHz,
and nominal feedback-network lag 6.75 degrees at 104166.67 Hz. These are component-model
figures, excluding parasitics and the op-amp response, not measured bandwidth or acceptance.
The exact 162 kohm / added-2-pF assembly has not yet passed on a board.

## 2. Identify each board and its existing modifications

**Board state: PSU and USB disconnected; no soldering yet.** Use the shutdown/discharge steps
in section 4 before handling a board that has been powered. Label each board, photograph both
work areas and note the original components and any stacked parts.

Use temporary batch labels **RW-A, RW-B and RW-C**, or existing unique labels, and record
each MCU UID at its first USB identity check. Do not reuse bench-board numbers 1-3 or their
calibration records. This batch has no reported damage; that is not a substitute for inspection.

Verify on each board that R80 is still the original **470 kohm**, C68 and C70 are still the
original **1 pF each**, and no gate-hold capacitor is already installed. If confirmed stock,
**remove no components**: install all four additions (one resistor and three capacitors).
If unexpected prior rework is present, identify it before proceeding; do not stack this plan's
parts on top of an unidentified modification.

Do not put a second parallel resistor over an existing parallel rework. For example,
470 kohm || 154 kohm || 162 kohm would be about 67.6 kohm. If an original 1 pF capacitor is
missing or damaged, stop: the listed inventory cannot reproduce the 3 pF + 3 pF method without
restoring that original part. Do not infer a capacitor's value from its unmarked body.

## 3. Take a before measurement where the board is healthy

Skip powered baseline work on a damaged/unverified board; record why it is unavailable.
For a new/unverified assembly, [BRINGUP_NEW_BOARD.md](BRINGUP_NEW_BOARD.md) sections 1-5 are
the power-safety gates before any optical or strobe testing.

**State for a healthy board:** J2 off, J3 disconnected, J8/J4 empty; PSU 5.20 V / 3 A at J1;
USB J6; detector disarmed, strobe DRY, gate 0. Use the already verified rail/ADC calibration.
Start file logging before:

```text
id
cfg
stat
strobe
```

Save UID, firmware identity, config source/sequence and ADC calibration. Before powered
acceptance, use the existing reviewed r5 image if this board runs an older build: see section 7.
Capture the old `id`/`cfg` before reflashing. Do not overwrite defaults with `cfg save` if a
previously calibrated board unexpectedly reports defaults.

For the optical before-run, follow [BRINGUP_NEW_BOARD.md](BRINGUP_NEW_BOARD.md) section 9:
fixed 104166 Hz carrier, 25% duty, warmed at that duty for at least 5 minutes, HPF TRACK,
no reflector and hands out of the optical path. Use a fixed insulating aperture if needed to
keep the calibration peak at 50-70% of ADC full scale. Then:

```text
cal demod
```

Record delay in ns and the printed quality figures. A before/after comparison needs the same
board, carrier, duty, geometry, thermal state and instrument/probe bandwidth. If practical,
save a TP7 edge waveform on the same scope/probe setup for comparison after rework. Attach
all probe leads while unpowered as in section 7. No baseline justifies powering a known fault.

## 4. Shut down, disconnect and discharge

If the board is running, beam off and detector disarmed, issue individually:

```text
beam off
detect disarm
strobe live off
strobe gate 0
off
stat
```

**Pass before handling:** `stat` shows STANDBY and latch 0. Then switch off and disconnect
**both PSU and USB**. USB alone keeps +3V3 live. Remove J2 if fitted.

Wait and verify **VIR below 1 V at C26 pad 1 to board GND**, and at J3 pin 1's screw to GND;
then disconnect J3's LED-bank wiring. A cold J3 joint can make its screw read low while C26
still holds charge, so check C26 too. C26's other pad is GND, 1.80 mm away edge-to-edge; keep
the meter tip on the intended pad. Check any external capacitor bank separately before
handling its wiring. Do not short capacitors with tweezers or a screwdriver.

All remaining soldering, inspection and resistance measurements are with PSU/USB absent and
VIR below 1 V. **Nothing conductive near D12**: its bias comes from the 36 V rail.

## 5. Rework the TIA, then inspect it

1. Under magnification, identify **R80, C68 and C70**, not nearby parts. R80 is 0603; the
   original C68/C70 are 0402. Resolve any existing rework using section 2 first.
2. Measure the loose 162 kohm resistor. Flux R80's exposed end terminations lightly.
3. Place the 162 kohm resistor directly on top of R80, aligned end-for-end. Tack one end,
   check alignment, then solder the other. Each new end must join the corresponding old
   end. Keep solder clear of the body gap and adjacent pads; do not remove the original R80.
4. Flux **C68** lightly. Stack one 2 pF part directly over its original 1 pF part, with both
   corresponding ends joined. Use brief heat and minimal solder; do not let the lower part
   move or lift. Do not bridge one end to the other.
5. Repeat with **a second 2 pF part over C70**. Both capacitor positions must now be 1 pF ||
   2 pF. Do not run jumper wires around the TIA feedback path; at these values parasitics matter.
6. Inspect from above and from the side: two wet joints per added component, originals still
   on both pads, no bridges, loose solder, tombstones or damaged ceramic. If an original
   part shifts, stop and restore the documented topology before continuing.
7. Clean using the flux maker's process; let the board dry completely. Residue around the
   photodiode summing node can introduce leakage. Keep solvents/heat away from optical parts
   unless their manufacturers permit them.

KiCad top-view pad centers, in mm, are below. Use the designators and net identity as the
primary identification; board rotation at the bench changes left/right.

| Part | Pad 1 (x, y), net | Pad 2 (x, y), net | Nearby different-net pads |
|---|---|---|---|
| R80 | (76.700, 153.900), summing node | (78.350, 153.900), U11A output | U11 pin 1 about 0.46 mm from R80.1; C70.2 about 0.42 mm from R80.2 |
| C68 | (77.590, 152.700), series midpoint | (76.630, 152.700), summing node | Its own opposite pad is 0.40 mm away; R80's output pad is about 0.42 mm from C68.1 |
| C70 | (79.410, 152.700), U11A output | (78.450, 152.700), series midpoint | Its own opposite pad is 0.40 mm away; R80's output pad is about 0.42 mm from C70.2 |

These pad gaps are rounded from the PCB geometry; they are not clearances to every trace/via.
No live hand-held probes in this area. A basic DMM cannot verify 1.5 pF in circuit, and
unpowered semiconductor paths can make an R80 measurement differ from 120.5 kohm. A stable
near-zero reading is a fault; a plausible resistance reading alone does not prove the rework.

## 6. Add the strobe gate-hold capacitor

**State: still fully unpowered and discharged, J3 disconnected.** Keep **U6, U7, R63 and all
other strobe parts unchanged**. This is CR-23 stage 1 only.

1. Identify **R66 pad 1, GND**, the large pad farther from Q10. Verify continuity to TP1.
   R66's other pad is CurrentSense, not ground. Do not bridge across R66: that would bypass
   the current-sense resistance and blind overcurrent measurement.
2. Arrange the 0603 1 uF capacitor so one end can be soldered securely to **R66's GND pad**
   while its other end remains insulated from board copper. The capacitor is nonpolar.
   Use an insulating support if necessary; do not leave a fragile unsupported assembly.
3. Run a short insulated 30 AWG wire, **20 mm maximum**, from the free capacitor end to
   **Q9's gate lead, pin 1**, on top of the lead near the body. Keep stripped ends short and
   provide strain relief that does not load the ceramic or gate lead.
4. **Do not connect to TP3**, R63's U7 side, Q9's tab/drain, Q9's source, TP5, or either R67
   pad. Keep the wire clear of VIR vias below-left of the gate pad. Q9 gate is at
   (31.100, 117.360); R66 GND at (45.325, 113.140), a straight-line separation of 14.8 mm.
5. Inspect closely: Q9 gate is only about **0.58 mm from R67's PWR_LowSideD pad** and 0.68 mm
   from R67's GND pad. R66's GND pad is the much roomier ground landing; its nearest other-net
   pad is about 4.6 mm away. Do not use R67 as a convenient ground landing.
6. Clean, dry and recheck with the DMM: capacitor ground end to TP1 approximately 0 ohm;
   gate wire to Q9 pin 1 approximately 0 ohm; gate to TP3 approximately R63's **47 ohm**
   rather than a direct short (parallel IC paths can affect the reading); gate-to-GND may
   charge through the meter but must not remain a hard short. Compare with prework readings
   if IC paths make it ambiguous. Gate-to-source must not be shorted, and R67 must still
   read about **56 kohm** across its pads. Stop on an unexplained low resistance.

Do not remove U7 or change R63 in this batch. Those are separate next-revision experiments.

## 7. Accept the pilot board before copying the rework

### 7.1 Identity, power and dry strobe checks

For the complete ordered verification run, follow [BENCH_REWORK_CHECK.md](BENCH_REWORK_CHECK.md).
The sections here summarize the acceptance scope and the existing canonical bench procedures.

**State:** J2 off, J3 disconnected, J8/J4 empty; beam off, detector disarmed; PSU 5.20 V / 3 A;
all measurement leads attached with PSU/USB off. No rail power from USB.

**Firmware prerequisite:** use the existing reviewed r5 `build/pitrac.uf2` for powered
acceptance. If needed, reflash by [HANDOFF.md](HANDOFF.md) section 4 after capturing old `id`
and `cfg`. Do not rebuild it and do not use `build_hang` for this work. r5 is Release,
version 1.1, stamp **`Oct 20 2026 14:59:52`**, 224720 text / 0 data / 422112 bss;
UF2 SHA256 **`AC5F4493F5A4C7E8B9B1A387BE128F42055410AFADC9A5B58C092ADBC272ACF7`**.
Never load pre-1.1 firmware on a board with the partition table. Installing a partition table
is a separate procedure and is not required for this passive rework.

Run `id`, `cfg`, `stat`, `strobe`; verify the identity, saved ADC calibration, gate 0, DRY,
and **GPIO27 low / U5 pulse limiter armed**. An unverified board must finish
[BRINGUP_NEW_BOARD.md](BRINGUP_NEW_BOARD.md) sections 1-5 before advancing.

Follow [BENCH_P6_STROBE.md](BENCH_P6_STROBE.md) 6a/6b with J3 disconnected, including the
R61-to-GND check before power and this board's U5 clamp check. Then CR-23's dry stability and
settling checks: measure **both TP3 and the actual held gate** at static gate settings and
record the held gate at **20 ms after commanding zero**, plus its time to **below 0.1 V**.
Board 1's reference was about 0.35 V at 20 ms and below 0.1 V at about 69 ms; these are
reference measurements, not a guarantee for the new boards or the unknown 0603 part.
Unexpected ringing, slow decay or a materially different waveform needs review before live use.

For these checks, use the 6c lead table and its local grounds at R66. TP3 is only **0.59 mm
from C56's +12 V pad**; TP4's nearest other-net pad is C54 GND, 2.03 mm away. Attach leads
unpowered, use 20 MHz bandwidth for levels and take a full-bandwidth capture for oscillation.
Do not put a grounded probe clip on Q9's source or the switched node TP5.

### 7.2 Optical recheck and calibration

**State:** J3 disconnected, J8/J4 empty, strobe DRY/gate 0; PSU 5.20 V / 3 A; rail up;
HPF TRACK; detector disarmed. Attach any TP7 scope lead unpowered first. TP7's nearest
different-net pad is R95.2, about 2.12 mm away. Probe **TP7**, the TIA output after R81,
not R80/C68/C70 or the photodiode summing node. Use a short board-GND connection.

Use [BRINGUP_NEW_BOARD.md](BRINGUP_NEW_BOARD.md) sections 7-10 for static health, baffling
acceptance and recalibration. Set up individually:

```text
on
hpf track
beam freq 104166
beam duty 2
beam on
```

Allow at least **5 s** after each beam/HPF change before measuring. Check the TIA waveform
while increasing duty through **2, 4, 8, 12, 25%**, stopping if it clips. Do not exceed 25%.
Keep the aperture/geometry fixed and hands clear; no conductive baffle near D12. At each
duty, after settling, `capture 0x04 400 500000` records ADC2. Pass at the operating 25% duty:
no sample at 4095 or below approximately 20; verify edges and ringing on TP7 with the scope,
because the MCU's 2 us sample interval cannot resolve the sub-microsecond edge response.
No sustained oscillation; compare overshoot and settling on the same instrument/bandwidth
as the before-run and send unresolved peaking for review before calling the pilot accepted.

Warm at 25% for at least **5 minutes**, then run individually:

```text
cal demod
cal model
beam
```

Expect `cal demod` to commit a phase with `warm: yes`, no quality failure and 0% saturation;
aim for 50-70% ADC full scale. Record delay in ns, phase, duty and all model quality output.
All five model points should be accepted; investigate rejection or unusual residuals rather
than copying another board's phase. `beam` must show the restored **104166 Hz**, TOP 1439.
The new Cf is larger than board 3's old value, so a negative phase shift is not required for
that board. Save only after the measurements pass:

```text
beam off
detect disarm
cfg save
cfg
off
stat
```

Expect saved config readback, then STANDBY/latch 0. Recheck detection thresholds and ball
waveforms before operational use; a phase calibration alone does not certify the detector.

### 7.3 Live strobe acceptance is a separate controlled bench run

Only after dry checks pass on this board, run [BENCH_P6_STROBE.md](BENCH_P6_STROBE.md) 6c
from its complete board-state table: all power off to connect J3, FLIR on Q9/HS1, local
R66 grounds, TP4 current and the actual gate node monitored. Start at gate 0 and use the
3% staircase; **24% and board 1's 9 A setting are not preapproved values for another board**.
Rerun `strobe cal` on each board; do not transfer its solved level.

Required quantitative checks include TP4 **135 mV/A**, agreement with ADC0, a clean plateau
within the bench procedure's limits and settling within **2 us**, plus the U5 hardware
clamp and zero-current/off checks. Stop if there is no current by 30% gate, any unexplained
overshoot/ringing or a fault. **Never raise a live limit to pass a test. GPIO27 stays 0.**

## 8. Record and repeat

For each board, record UID/label, prework faults and component values, photographs, actual
added parts and their part numbers, inspection and DMM results, firmware identity, new
calibration, TP7 waveform, gate settling and strobe acceptance. Distinguish **reworked** from
**tested and accepted**. Leave bench-board 1-3 records unchanged by this batch.

Use separate copies of the full bring-up sign-off form for these additional boards. This
batch tracker starts pending and records no completed physical work:

| Batch label | MCU UID | Original values inspected | Four additions fitted | Dry checks | Optical acceptance | Live strobe acceptance |
|---|---|---|---|---|---|---|
| RW-A | pending | pending | pending | pending | pending | pending |
| RW-B | pending | pending | pending | pending | pending | pending |
| RW-C | pending | pending | pending | pending | pending | pending |

Repeat only after the pilot's results are understood. Check every board individually; assembly,
Q9 threshold, gate decay and optical calibration are per-board properties.

Design connections were checked against the netlist and PCB on 2026-11-03. Read-only KiCad DRC
reported 26 violations and the one already documented unconnected item, J8.17; this is not a
clean-DRC sign-off. No design connectivity change or firmware rebuild is part of this plan.
