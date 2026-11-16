# New-board bring-up: a step-by-step protocol

# 🔵 THIS DOCUMENT IS THE DRIVER FOR A NEW BOARD. Work top to bottom, one step at a time.

**Use it for a freshly assembled board, blank or flashed.** Step 1.3 flashes a blank one.
`START_HERE.md` is for setting up the build machine, not the board.

**What it covers: everything that varies from board to board.**
- Questions about the design were settled once, on boards 1–3, and are not repeated here
  (Background A).
- Each section names its canonical `BENCH*.md` procedure, where the reasoning and the
  troubleshooting live. If this protocol and that section ever disagree on a command or a pass
  criterion, the `BENCH` section is right and this file gets fixed.

**Time:**
- §0–§11 take about two hours, most of it waiting: two 5-minute beam warm-ups and a 128 s
  model fit.
- §12 (the strobe) and §13 (before a Pi) are separate sessions.

Rewritten as a numbered protocol on 2026-11-10. The section numbers §0–§11 are unchanged, so
references from other documents still hold.

**This revision's five prototype boards are brought up by hand, with this protocol** (owner,
2026-11-12). A guided version that needs no scope, logic analyser, DMM or FLIR waits for the
next board revision, which adds the measurements it needs: `NEXT_BOARD_REV.md` CR-29.

---

## How to run a step

Every step has the same parts:

| | |
|---|---|
| **State** | what must be true first: power, jumpers, probes, beam |
| **Do** | the exact actions; commands are typed at the board's CLI unless it says otherwise |
| **Pass** | what you should see, in physical units |
| **Stop if** | the failure that ends the session until it is understood |
| **Record** | what goes into the sign-off table at the end (📝) |

- **Logging:** start the serial monitor's log to `captures/` before §1 and keep it running.
- **Stop means stop.** Record what you saw and send it. Do not work around a failed check.
- **Probes and leads go on only with the PSU and USB both off.**

## Rules for the whole protocol

- 🔴 **Nothing conductive near D12.** Its cathode sits at VIR, 36 V, through R77. Bare foil
  over D12 destroyed U11B on board 1 (`PROGRESS.md` §11). Covers and attenuators are
  cardboard, or foil wrapped in tape.
- 🔴 **The PSU at 5.20 V, measured at J1 with a DMM**, with the current limit each section
  names. Never run the +5 V rail from USB.
- 🔴 **GPIO27 (`PULSE_LIMIT_DISABLE`) stays 0. No camera on J4. No Pi on J8 anywhere in this
  protocol:** §13 prepares the board for one, and its sheet covers the first seating.
- **The beam at 25 % duty at most** (CR-12).
- **Hands clear of the board during any optical reading:** 850 nm reflects off skin straight
  into D12.
- **§2, §4 and §5 are safety gates.** Never go past a failed one.

## Which sections you need

| Goal | Sections |
|---|---|
| **A fully working board**: optical, strobe, then a Pi for Phases 7 and 8 | all of them, in order: §0–§11, then §12, then §13 |
| **A board handed to someone else** for Pi, camera and strobe work | the same: §0–§11, §12, §13. §12 and §13 are self-contained, and Step 13.4 is the sheet that goes with the board |
| **Strobe dry tests only** (6a/6b, e.g. to compare one board's strobe chain with another's) | §0 (the TIA variant: record it if convenient); §1–§5 (§5's Steps 5.3–5.5 may wait until before a Pi); then §12, Step 12.2 |

**About the strobe-only path:** §2 is in it because it measures VIR and TP2 (+12 V, U8's
supply). §3 is in it because GPIO24 latching high would make the firmware think a Pi is present.
§4 is in it because the latch guard decides whether `on` latches on USB. §6–§10 are
beam-only. Mark a board done this way "fast path: §6–§11 not done" in the sign-off, and finish
those sections before any optical or Pi work.

---

## 0. Identify the board (unpowered)

### Step 0.1 — Record the TIA variant

Nearly every number in `BENCH_P3_DETECT.md` scales with the TIA's feedback resistor. A reworked
board tested against stock numbers looks broken when it is fine, or fine when it is broken.

**State:** PSU and USB off. A magnifier.

**Do:**
- Read **R80** and any part fitted in parallel with it.
- Read **C68** and **C70**. Note any added capacitor and **which pads it bridges**.
- Photograph both areas.
- **DMM across R80's two pads**, in Ω, both polarities, with D12 shaded by an insulating card.
  - Unpowered only: R80 is 0.42–0.46 mm from nearby pads.
  - Reworked (162 kΩ ∥ R80): **119–122 kΩ**. Stock: up to 470 kΩ.
  - The rest of the circuit can only pull the reading **lower**. So a stable reading well
    above ~125 kΩ means the 162 kΩ is not connected.
  - A near-zero reading is a bridge.

**Pass:** the board matches one row:

| | Rf | Cf | TIA pole | lag at 104 kHz | servo corner |
|---|---|---|---|---|---|
| **stock** | 470 kΩ | 0.500 pF | 677.3 kHz | 8.74° | 2.267 Hz (τ 70 ms) |
| **board 3** (154 kΩ ∥ R80, 100 pF on one Cf leg) | **116.0 kΩ** | **0.990 pF** | **1.386 MHz** | **4.30°** | **0.559 Hz (τ 285 ms)** |
| **original-board target** (162 kΩ ∥ R80; 2 pF across each original 1 pF leg) | **120.475 kΩ** | **1.500 pF** | **880.7 kHz** | **6.75°** | **0.581 Hz (τ 274 ms)** |

- ⚠ **C68 and C70 are in series.** A capacitor across *one* leg shorts that leg and leaves the
  other 1 pF (Cf ≈ 0.99 pF). A capacitor across *the pair* gives 100.5 pF and a **13.7 kHz**
  pole, below the carrier. The two cases differ by 100×, and only one works: identify the pads
  rather than assume.
- ⚠ **The reworked figures are calculated.** §9 checks them on the board. The original-board
  target is a plan (`REWORK_ORIGINAL_BOARDS.md`), not a reworked board that has passed.

**Stop if:** the parts match no row. Identify the modification first
(`REWORK_ORIGINAL_BOARDS.md` §2).

**Record:** 📝 Rf and Cf, and which leg any added capacitor bridges.

### Step 0.2 — Decide the reworks

**State:** as Step 0.1.

**Do:** pick the row that fits this board.

| The board is | Then |
|---|---|
| **Stock**, and it will see strobe current (§12) | Fit the **CR-23 gate-hold capacitor** (1 µF at Q9's gate, after R63, to R66 pad 1) before any current: `REWORK_ORIGINAL_BOARDS.md` §6. Without it every pulse overshoots; board 1 showed 3.3 A for ~2.5 µs at a 0.7 A setting (`NEXT_BOARD_REV.md` CR-23). It can be fitted at any point before §12 |
| **Stock**, and its TIA is to be reworked to the 120 kΩ / 1.5 pF target | Run §1–§9 stock first: §9's `cal demod` is the "before". Then `REWORK_ORIGINAL_BOARDS.md` §4–§6 and `BENCH_REWORK_CHECK.md`, then §7–§10 again |
| **Already reworked** | Inspect the work against `REWORK_ORIGINAL_BOARDS.md` §2 before power |

A stock TIA is usable: board 2 is stock, and with good baffles it was linear to 25 % duty.

**Record:** 📝 the reworks done or planned.

---

## 1. Before anything is energised (USB only)

### Step 1.1 — Set the bench supply

**State:** PSU output off; its leads **not** on J1.

**Do:** set **5.20 V** and a **2 A** current limit. Check both on a DMM or the PSU's display.

**Pass:** 5.20 V and 2 A.

**Why:**
- Below 5.05 V the board will not latch (Background B).
- A low current limit parks VIR at a lower voltage, and nothing else looks wrong
  (Background C).

### Step 1.2 — Inspect the joints on the copper pours

Every pour on this layout connects to its pads solid, with no thermal relief, so a joint can
look good on top and never have wetted the pin. Two cold J3 joints cost board 1 a bench day
in 6c (2026-10-13). Background D has the detail and the rework method.

**State:** unpowered.

**Do:** inspect both sides of the board under magnification, joint by joint:

| Group | Joints | On |
|---|---|---|
| Strobe current path (~9 A pulses) | **J3 pins 1 and 2**; **Q9's tab** (a 37 mm² pad); D10; R65, R66; L1, D2 (boost); C13, C15 (VIR bulk) | VIR, VIR_RTN, CurrentSense, GND pours |
| Power input and latch | **J1, both pins**; J2; Q1, Q3 (pads forced solid) | +VDC pour, GND plane, +5V |
| Through-hole pins on the inner planes | J8's GND and +5V pins (2, 4, 6, 9, 14, 20, 25, 30, 34, 39); J4; J5; J6 shell; J7; J9; TP1 | GND (In1), +5V (In2) |

Then, with a DMM:
- J3 pin 1's **screw** → C26's VIR pad;
- J3 pin 2's **screw** → Q9's tab;
- each J1 screw → its pour.

**Pass:**
- Every joint wets its pad all the way round, with a concave fillet and solder showing on
  the far side.
- Every DMM reading is under 1 Ω. The DMM finds open joints; only the visual check finds
  marginal ones.

**Stop if:** you find a dull or grainy joint, a ball sitting on a pin, or a ring of gap round
one. Reflow it first (Background D).

**Record:** 📝 joints inspected, and any reflowed.

Re-check J1 and J3 after the wires are landed and the screws tightened: that is when a marginal
joint cracks.

### Step 1.3 — Flash: the partition table, then the image

The A/B partition table is what lets a seated Pi update this board later (`pitrac-link
update`). A board flashed flat, without it, works for everything else and can be converted
later (`BENCH_P8_PI.md` §8.7).

**State:**
- USB-C to **J6** only. No bench supply, no Pi, **J2 off**.
- A blank RP2354 shows up as the `RP2350` drive by itself. The SW1/SW2 dance (hold SW1, tap
  SW2) is needed only once there is an image to interrupt.

**Do:** in PowerShell, in `Hardware\firmware`. picotool is
`%USERPROFILE%\.pico-sdk\picotool\2.3.0\picotool\picotool.exe`.
1. `picotool load build\pitrac_pt.uf2`
2. `picotool reboot -u`. This returns to BOOTSEL, so the boot ROM reads the table.
3. `picotool partition info`. If the first try answers `ERROR: The RP2350 device returned an
   error: rebooting`, run it again. Optional while the board is in BOOTSEL: `picotool info -a`,
   for the die revision.
4. Copy the image onto the `RP2350` drive **by path**: `Copy-Item <image> E:\`, with that
   drive's letter.
   - The image: r5 (`build\pitrac.uf2`, `Oct 20 2026 14:59:52`, version 1.1).
   - Which image a board then runs its bring-up on is settled per board (`PROGRESS.md`
     §10). 1.7 is the candidate: board 1 passed `BENCH_P8_PI.md` §8.9b with it on
     2026-11-12.
   - It goes into **partition B**, the boot ROM's rule for the first image after the table.
   - **If this PC blocks writes to the drive,** use `picotool load -p 1 -v -x <image>` instead
     (`HANDOFF.md` §4). Do not use `picotool load` then `reboot -u`: that leaves the image
     `not bought`, and it never runs (Board 4, 2026-11-11).

**Pass:**
- Step 3 shows two partitions: 0 from 0x2000 to 0x100000, and 1 from 0x100000 to 0x1fe000,
  linked to 0 as its B.
- After step 4 the board restarts: the yellow LED **D5** blinks about once every 2 s
  (standby), and the CLI answers. Press Enter once if the banner has scrolled past.

**Stop if:** the partitions read anything else, or the CLI never answers.

**Never** load an image older than 1.1 onto a board with the table. Such an image does not know
about partitions, bus-faults at its first config read, and erases the good image's first sector.

**Record:** 📝 the table installed; the die revision if you read it.

### Step 1.4 — Identity

**State:** as Step 1.3, after it.

**Do:**
```
id
upd
```

**Pass:**
- `id`:
  - `sysclk : 150000000 Hz`;
  - `board : pitrac_ltb_v1 (RP2354B, 48 GPIO, 2MB internal flash)`;
  - `version : 1.x  (partition B at 0x100000)`;
  - `built :` the stamp of the image you loaded.
- `upd` → `layout : this image in partition B`.

**Record:** 📝 the UID, and the image version and partition.

---

## 2. Dead-board rail smoke test (Phase 0.5): J2 fitted

**Canonical:** `BENCH.md` Phase 0.5.

J2 shorts the latch gate to GND, so the rails come up with no firmware involved. This finds a
short before the firmware ever closes the latch, and that is the cheap time to find one.

### Step 2.1 — The rails with J2

**State:**
- PSU off. **Fit J2.** USB may stay connected.
- **Do not press the power button during §2.** It drives GPIO15 high on top of the J2 short:
  harmless, but it muddies the readings.

**Do:** PSU on (5.20 V, 2 A limit, into J1). Measure with the DMM, to GND:

| Point | Expect |
|---|---|
| +5 V at **J8.2** | 5.2 V |
| VIR at **J3 pin 1's screw**, and at **C26** | 36 V, the same at both (not across J3: its pin 2 is `VIR_RTN`) |
| **TP2** (+12 V) | 12 V (11.4–12.7) |
| **TP6, TP7, TP9, TP10** | ≈ 2.59 V each, +5VA / 2 on a 5.2 V rail |
| PSU current, with the rail up | ~129 mA (board 1) |

⚠ **Where a hand-held tip may go** (`HANDOFF.md` §5 rule 8; pad gaps from the PCB, 2026-11-11):
- **Not on a powered board:**
  - J8.2: 0.84 mm from J8.1 (+3V3_Pi5);
  - TP6: 1.14 mm from C73 pad 1 (U11B's inverting input);
  - TP9: 0.78 mm from C79 pad 1;
  - TP10: 0.79 mm from R91 pad 1.
  
  Fit a lead there with the PSU and USB off, or use the substitutes below.
- **Fine for a tip:**
  - the J3 pin 1 screw: 3.2 mm from pin 2;
  - TP7: 2.12 mm from R95 pad 2;
  - TP2: 1.92 mm from R15 pad 1. That pad is VIR, and a slip puts 36 V on the +12 V rail;
  - C26 pad 1: 1.80 mm from its own GND pad. A slip shorts VIR's bulk capacitors.
- **Substitutes:**
  - **For +5 V: VIR at the J3 screw.** The boost's input (U1 pin 3) is the switched +5 V, so
    VIR is 36 V only when that rail is up. When the rail is off, VIR is near 0 V.
  - **For the mid-rail: TP7.**
  - **C26 only if the screw reads low.** VIR comes from C26, so 36 V at the screw means 36 V
    there too.

**Pass:** every reading as in the table. Nothing warm, and no smell.

**Stop if:**
- Any rail is wrong, or the current is high.
- VIR differs between the J3 screw and C26: a cold J3 joint (Step 1.2).
- VIR is low **and** the current is low together (e.g. 27 V and 80 mA): suspect the PSU's
  current limit before the board (Background C).

**Record:** 📝 VIR, TP2, TP6.

### Step 2.2 — The boost's switching frequency

**State:**
- PSU off to attach the scope probe at the LM5157 SW node. `BENCH_P3_DETECT.md` §3.6 Check 1
  names the pad.
- Then PSU on, as in Step 2.1.

**Do:** scope the SW node.

**Pass:** a clean switching waveform. The R11/C9 snubber is DNP by default: fit it only if the
node rings.

**Record:** 📝 the switching frequency (Background E).

### Step 2.3 — Remove J2

**Do:** PSU off. **Remove J2.**

**Pass:** J2 is off before §3, and stays off.

---

## 3. E9 check

**Canonical:** `PROGRESS.md` Q5.

Erratum E9 is an input with a pull-down latching near 2.2 V. Board 1 is silicon revision A4, a
later stepping than the A2 that E9 was written against, but each chip can differ. On GPIO24 it
would make `PI_3V3_SENSE` report a Pi that is not there.

### Step 3.1 — `pins`

**State:** J2 off; USB connected; nothing on J8 or J4.

**Do:**
```
pins
```

**Pass:** GPIO24, GPIO0, GPIO8 and GPIO9 all read **0**.

**Stop if:** any reads 1. That board needs external pull-downs, and Q5 reopens.

**Record:** 📝 all 0.

---

## 4. 🔴 ADC +5 V scale: THE SAFETY GATE

**Canonical:** `PROGRESS.md` Q9.

The +5 V reading is ~5.9 % low, and the error is inside the ADC: gain error, plus sample-and-
hold settling through the 50 kΩ R46/R47 divider. It varies from chip to chip.
- **Reading low:** a good 5.2 V supply looks like < 5.05 V, and the board refuses to latch.
  Obvious and harmless.
- **Reading high:** USB's real 4.85 V could read above the 5.05 V guard, and **the board would
  latch on USB power.** It would then feed the +5 V rail, and any Pi on it, through **D8, a 1 A
  SS14**. This is the hazard the guard exists for.

### Step 4.1 — Read it

**State:** PSU on at 5.20 V into J1, J2 off; USB connected; DMM across J1.

**Do:**
```
adc5v
```

**Pass:** about 5.20 V, and `external supply, latch permitted`.

### Step 4.2 — Trim it to the DMM, and save it

**Do:** every board, even when Step 4.1 reads close:
```
adc5vcal <the DMM reading at J1, in volts, e.g. 5.203>
cfg save
adc5v
cfg
```

**Pass:**
- `adc5v` matches the DMM.
- `cfg`'s `source` line reads `slot A` (or `slot B`), not `defaults`.
- The scale comes out near **1.069**: board 1 has 1.0694 and the COM10 board 1.0688, both
  trimmed on current firmware.
- Boards 2 and 3's 1.062 in the sign-off were trimmed before 2026-10-15, against a reading
  ~0.6 % higher.

**Stop if:** `adc5vcal` answers `REFUSED`. It works only with the ADC in its idle mode
(`adcmode idle` first).

⚠ **A new board's config block is empty.** Nothing persists until `cfg save`.

**Record:** 📝 +5V_IN on the PSU (the DMM), and the scale.

---

## 5. Latch guard and supply monitor (Phase 1)

**Canonical:** `BENCH.md` Phase 1, Tests 1–5, and Test 6 for the `on` guard.

These are here because of real bench finds: do not take them on trust from another board.
`on` is accepted only from `STANDBY` and is never queued for later. Read `stat` first, and if
the board is already running, skip `on`.

**Without a front panel: J7** (a 1×6 header, 2.54 mm; pinout from the PCB, 2026-11-11).

| J7 pin | Net | Use |
|---|---|---|
| 1, 5 | switched **+5 V**: the same net as J8.2 | read the rail here instead of J8.2 |
| 3 | `PWR_TOGGLE`, through R42 (1 kΩ) to GPIO14, active low | the button |
| 4 | GND | the button's other side |
| 2, 6 | the panel LEDs' cathodes | unused |

- Its pads are 0.84 mm apart (`HANDOFF.md` §5 rule 8), so fit female jumper leads with the
  PSU and USB off.
- **A press:** touch the free ends of the pin 3 and pin 4 leads together for under a second.
  The firmware debounces for 25 ms and acts on the release. A 5 s hold is the force-off escape
  hatch.
- **Never put a supply on pin 3:** it goes through 1 kΩ straight to GPIO14.
- **J1 cannot stand in for J7.** J1 is the unswitched input, and it reads 5.2 V whatever the
  latch does.

### Step 5.1 — Test 1: no latching on USB

**State:** PSU **off** (J1 unpowered); USB connected.

**Do:**
```
stat
adc5v
on
stat
```

**Pass:**
- `adc5v` reads about 4.85 V, latch INHIBITED.
- After `on`: the red LED fast-blinks, and `stat` shows `FAULT` / `USB_POWER_ONLY`.
- +5 V at J8.2 stays at 0 V, and so does VIR. On a powered board, read VIR at J3 pin 1's
  screw; it stands in for J8.2, which takes no hand-held tip (§2.1's probe note). If the board
  was just running, wait for VIR to drain below 1 V before `on`.

**Record:** 📝 +5V_IN on USB only.

### Step 5.2 — Test 2: latching on real power

**State:** PSU on, 5.20 V. Read the standby current on the PSU first.

**Do:**
```
fault clear
on
stat
```

**Pass:**
- +5 V at J8.2 within 250 ms. That needs a lead fitted with the board unpowered (§2.1's probe
  note); a DMM reading of VIR 36 V at the J3 screw proves the rail is up.
- VIR 36 V, TP2 12 V.
- `stat` → `BENCH_RUNNING`.

**Record:** 📝 the standby current (board 1: 32 mA), and the rail-up idle current (board 1:
129 mA at 5.2 V). These baselines make a future leak visible.
- **If the PSU shows no current**, record "not measured".
- Instead, scan the board with the FLIR after ~5 minutes in `BENCH_RUNNING`: no part should
  stand out from the rest.
- Take the currents before §13:
  - with a supply that shows current; or
  - with a DMM in series on its 10 A range, connected with the board unpowered. The mA range's
    shunt would drop 5.20 V below the 5.05 V latch guard.

### Step 5.3 — Test 3: unlatching

**Do:** press the power button.

**Pass:** +5 V falls to 0 V, and the CLI keeps answering (it lives on +3V3). With no Pi, the
fall ends in a slow tail below ~1 V (Step 5.4).

### Step 5.4 — Test 4: fail-safe through a reset

**State:**
- PSU and USB off to attach two scope channels: GPIO15 (the latch control) and +5 V at J8.2.
- Then power back on, and `on` → `BENCH_RUNNING`.

**Do:** press **SW2 (RUN)**.

**Pass:** +5 V drops and stays down, and GPIO15 shows **no** glitch high through the reset.
- **With no Pi on the rail, "down" is not 0 V within seconds.** Below ~1 V the remaining loads
  stop drawing, and the rail's capacitance drains slowly.
  - Board 4 (2026-11-11): 5.26 V → 1.0 V in 260 ms, 0.59 V at 3.3 s.
  - Judge the shape: **no rise after the fall begins**, beyond the noise the same instrument
    shows on the steady rail before the press.
- **The capture must include SW2's release:** the firmware takes GPIO15 back at the reboot.
- A logic analyser works too (Board 4: a Saleae on J7.5, analog plus digital).
  - Its digital trigger fires near 1 V, about 260 ms after the press, so keep at least 0.5 s
    before the trigger.
  - After the threshold chatter ends, a digital channel that stays 0 is a fast glitch detector
    for anything that climbs back above ~1 V.

### Step 5.5 — Test 5: the supply pulled while latched

**State:** PSU on; `fault clear` if needed; `on` → `BENCH_RUNNING`.

**Do:** switch the PSU's output off.

**Pass:** `FAULT_SUPPLY_LOST` within ~500 ms, and the latch drops.
- **On a scope or analyser, the timing is 500–600 ms**, measured from where +5 V crosses
  4.90 V to where it leaves the USB plateau.
  - The firmware reads +5V_IN every 100 ms (`V5_MONITOR_INTERVAL_MS`) and trips after 500 ms
    of continuous low (`V5_LOW_DEBOUNCE_MS`). So the first low reading lands 0–100 ms after the
    crossing.
  - Add a few ms of loop latency.
  - Board 4 (2026-11-11): **579 ms**, on a plateau of 4.81 V. The analyser read ~50 mV high
    against the DMM.

**Stop if:** any of Steps 5.1–5.5 fails. Without 5.1 and 5.5, a Pi can be fed through a 1 A
diode.

Afterwards: PSU on, then `fault clear`.

---

## 6. Beam sanity (an abbreviated Phase 2)

**Canonical:** `BENCH_P2_BEAM.md`. Phase lock, Q1 and Q2 are properties of the design. This
section checks that **this** board's assembly is sound.

**State for the whole section:**
- PSU 5.20 V, **limit 2.5 A**; rail up (`on` → `BENCH_RUNNING`).
- Scope on **TP5** (`Strobe_GND`, the shared low-side return), attached with the power off.
- The FLIR on the beam LED and its heatsink.

### Step 6.1 — The beam's PWM readback

**Do:**
```
beam duty 2
beam on
beam
```

**Pass:** `beam` (read after `beam on`) shows:
- both slices `ENABLED ok`;
- funcsel **4 (PWM)** on GPIO31 and GPIO39;
- **pad ISO = 0** on both;
- `ctr(car)` **counting**.

With the beam off, the same command reports `off ok` and SIO (5). That is not a fault.

### Step 6.2 — The carrier at TP5

**Pass:** a period of **9.6 µs** on TP5.

### Step 6.3 — The U9 clamp width

U9 is a one-shot that caps the LED's on-time at t_w = K·R68·C57. The tolerance of R68 and C57
puts it anywhere from 109 to 136 µs, so measure it.

**Do:**
```
beam clamp
beam
```
`beam clamp` sets 1 kHz / 50 % **and turns the beam on at once**. U9 holds the real duty to
~12 %, which is why this is safe.

**Pass:**
- `beam` reads `TOP=49999, clkdiv=3, 1000 Hz`.
- On TP5, the LED's on-time is the clamp width, shorter than the commanded 500 µs.

**Then, at once, before anything else:**
```
beam duty 2
beam off
beam freq 104166
beam
```
🔴 `beam clamp` leaves the duty at 50 %. A bare `beam freq 104166` after it would be cut back
only to the firmware's 35 % ceiling, which is still above the 25 % CR-12 allows, and U9
cannot help at 104 kHz. So `beam duty 2` comes first.

**Pass:** `beam` reads `freq 104166 Hz (TOP=1439)`, at 2 %.

**Record:** 📝 the U9 clamp width. Board 1: 122.68 µs; Board 4: 120.96 µs.
- **The firmware's beam ceiling uses this number.** The 35 % effective-duty ceiling (`beam.c`)
  assumes U9 clamps at the compiled `BEAM_ONESHOT_CLAMP_US`, 122 µs.
  - That only matters below ~2.9 kHz, where a 35 % high phase reaches the clamp. The carrier
    is 104 kHz, and `cal model` sweeps 80–200 kHz.
  - At or under 122 µs, the ceiling is conservative.
  - **Longer than 122 µs:** the ceiling under-reads by clamp/122 at those low frequencies. Note
    it, and run this board's beam below ~3 kHz only for `beam clamp`'s 1 kHz.
- The strobe's limit is U5's clamp, not U9's: §12 measures it.

### Step 6.4 — Thermals at 25 %

**Do:**
```
beam on
beam ramp 25 500
```
Watch the FLIR all the way up, then hold 25 % for **10 minutes**.

**Pass:**
- The temperature levels off.
- At 10 min the LED's base is at or under board 2's **98 °C** (board 1: 87.5 °C).
  - Board 2's junction works out at 114–122 °C, against a 145 °C maximum.
  - Never run above 25 %: CR-12 measured the junction at 123–133 °C at 30 %.

**Stop if:** the base is hotter than board 2 at 10 min, or still climbing fast. Then `beam off`
and check the heatsink's mounting and the airflow (CR-12) before any optical work.

**Record:** 📝 the base temperature at 10 min (and the heatsink's, if you can read it).

```
beam off
```

### Step 6.5 — The strobe's idle state, and the A7 fix (read-only)

**State:** J3 (the LED bank) disconnected. Nothing fires.

**Do:**
```
strobe
panel rdy 100
panel rdy 0
panel rdy auto
```

**Pass:**
- `A7       : GPIO12 ready LED on SIO (on/off); slice 6 belongs to the gate DAC`
- `GPIO27   : SIO output, driven 0, pad 0  -> U5 pulse limiter armed`
- `gate DAC : level 0 of 1024 (0.00 %)` … `GPIO28 PWM`
- `engine   : idle` … `GPIO25 SIO, pad 0`
- `panel rdy 100` / `panel rdy 0` switch the ready LED fully on and off, with no dimming in
  between (by design). `panel rdy auto` hands it back.

---

## 7. Detect-chain sanity

**Canonical:** `BENCH_P3_DETECT.md` §3.2.

### Step 7.1 — The analog chain's DC level

**State:** rail up; beam off.

**Do:**
```
adc 2 256
```
Then the DMM on TP6, TP7, TP9 and TP10.

⚠ **TP6, TP9 and TP10 take no hand-held tip on a powered board** (§2.1's probe note: 1.14,
0.78 and 0.79 mm from other copper). Either fit leads to them with the board unpowered, or read
TP7 alone, which is 2.12 mm clear. TP7 alone leaves TP9 and TP10's DC levels unchecked; §9's
`cal demod` then shows a clipping stage as `saturated` above 0 %.

**Pass:**
- ADC2 ≈ **3212 codes (2.59 V)**.
- TP6, TP7, TP9 and TP10 all ≈ **2.59 V**. The four agreeing is the criterion, not the
  absolute number. With TP7 alone, the criterion is TP7 ≈ 2.59 V and ADC2 agreeing with it.

**Record:** 📝 TP7 (TIA_Out) with the beam off.

### Step 7.2 — `hpf test`: the TRACK/HOLD polarity and the switch leakage

**What it still checks, per board.**
- **The polarity is settled.** It was confirmed on boards 1–4 and matches the TMUX1219
  datasheet. The firmware drives GPIO33 from the compiled `HPF_SEL_TRACK` (0 = TRACK) whether
  or not this test has run.
- The `cfg` line `hpf sel` is a record that this board confirmed it. Without the record,
  `cfg` and `hpf` print warnings; nothing else changes.
- **The per-board part is the HPF switch path** (U14, R96, C81) and its leakage. Board 1:
  ~250 pA; boards 2–4: 50–61 pA. A rework's flux residue would show here.

**Run §7 before §6 on a new board** if you can. Both of §7's steps are beam-off, and before the
beam has ever run the board is already at a steady temperature, so there is no cool-down to
wait for.

**State:** all three of these, which every failed run violated:

| Condition | Why |
|---|---|
| **Beam OFF, and left off** | The test measures U14's switch leakage integrating on C81, which is purely electrical. With the beam on, C81 holds the demodulated DC in HOLD, the LED's 25–45 % warm-up falloff drifts through U12B's ×14.5 over the 32 s, and the test reports `order_effect` every time |
| **The board at a steady temperature** | Leakage roughly doubles per 10 °C, so a board cooling from beam work gives repeats that disagree. Wait a few minutes after §6 |
| **The photodiode shaded (or the lights out), and hands clear for the full ~35 s** | What matters is a scene that does not change between windows, not darkness: steady 120 Hz flicker averages out. On 2026-08-17 the culprit was the operator's own hand reflecting 850 nm into D12 |

**Do:**
```
hpf test
```
It blocks for **~32 s**: 4 × (5 s settle + 3 s sample). Then:
```
cfg save
cfg
```

**Pass:**
- **`CONFIRMED, GPIO33 = 0 is TRACK`**.
- **Judge it on the repeats, not on the printed separation:** the two visits to each level
  agree closely, while the levels differ a lot. Board 2, 2026-08-21:
  ```
    level     rep1 mean   rep2 mean    rep1 p-p   rep2 p-p
    GPIO33=0    +0.0093 V   +0.0092 V     0.0024     0.0008
    GPIO33=1    +0.0271 V   +0.0269 V     0.0089     0.0089
  ```
  That is 0.1 and 0.2 mV within a level, against 17.8 mV between them. The separation is not a
  board property: HOLD is an integrator, and two good boards gave 6× and 2.9×. The failed runs
  gave 1.2× and 1.4×, both `ORDER-DEPENDENT`.
- `cfg` → `hpf sel  : TRACK = 0`.

**Stop if:**
- `INVERTED`. **Do not simply flip the constant.** The TMUX1219 truth table says SEL = 0
  selects S1 (the R96/GND leg), so two sources disagreeing means one is being misread
  (`PROGRESS.md` §6).
- `ORDER-DEPENDENT`. Re-check the three conditions, then repeat.

**Record:** 📝 the `hpf test` verdict, and HOLD's p-p over its 3 s window. That gives the
switch leakage:
- board 2: 8.9 mV → ≤ 2.3 mV/s at ADC5 → ~52 pA into C81;
- board 1: ~250 pA.

It says how long ARMED stays usable: ~43 s to walk 100 mV at board 2's rate, against a 1–10 ms
transit, so it never binds in practice.

---

## 8. CR-15: the baffle acceptance test

**Canonical:** `BENCH_P3_DETECT.md` §3.3.

CR-15 itself is settled: the coupling was optical, and baffling removes it (the record:
`PROGRESS_ARCHIVE.md`, "Results moved from BENCH_P3_DETECT.md", §3.3). The baffle is
mechanical, though, so **this is an acceptance test of this board's baffling**, re-run on every
build.

### Step 8.1 — Set up

**State:**
- Rail up; PSU limit 2.5 A; HPF in TRACK (`hpf track`).
- **The baffles fitted.** Carbon-black paint and tape-wrapped foil both worked. Most black
  plastics are near-transparent at 850 nm, so "it looks black" proves nothing.
- **Stand clear of the board.** A hand in front of it moved the no-baffle control from 339 to
  19 codes between sessions.

### Step 8.2 — The duty sweep

**Short form: capture at 12 % and at 25 % only.** Run the full sweep below only if either
capture clips, to find where clipping starts.
```
hpf track
beam duty 2
beam on
beam ramp 12 500
capture 0x04 400 500000
beam ramp 25 500
capture 0x04 400 500000
```
- **Why not 25 % alone:** the two edges peak at different duties.
  - The top (toward 4095) rises with duty, so it is highest at 25 %.
  - The deepest sample comes mid-sweep. On Board 4 (2026-11-11) it was 1529 at 8 % and 1557
    at 12 %, against 1809 at 25 %.
- A board that saturates fails at 12 % and 25 % as well (board 1's failing shape, below), so
  the two captures catch it.

**Full sweep:**

**Do:**
```
beam duty 2
beam on
capture 0x04 400 500000
beam duty 3
capture 0x04 400 500000
beam duty 4
capture 0x04 400 500000
beam ramp 6 500
capture 0x04 400 500000
beam ramp 8 500
capture 0x04 400 500000
beam ramp 12 500
capture 0x04 400 500000
beam ramp 25 500
capture 0x04 400 500000
beam off
```

**Pass:** **no sample at 4095, and none below ~20, at every duty up to 25 %.**
- **Judge it at 12 % and 25 %.** At 500 ksps only 24 distinct carrier phases are ever sampled
  (500 k ÷ 104.1667 k = 4.8). Below ~12 % the pulse is shorter than the 2 µs sample interval,
  so the minimum recorded at low duty is largely luck.
- The target is board 2, with good baffles and hands clear (2026-08-19):

  | duty | min | max | swing | |
  |---|---|---|---|---|
  | 2 % | 3084 | 3253 | 169 codes = 136 mV | linear |
  | 4 % | 2750 | 3285 | 535 = 431 mV | linear |
  | 8 % | 2373 | 3374 | 1001 = 807 mV | linear |
  | 12 % | 1891 | 3418 | 1527 = 1231 mV | linear |
  | **25 %** | **2076** | **3606** | **1530 = 1233 mV** | **linear: the operating point** |

  The swing stops growing after 12 % (1527 → 1530 codes): the pulse has settled past the
  235 ns TIA τ, so its amplitude is set by the peak photocurrent, which is flat with duty. Seeing
  that plateau is a good sign the sweep is real.

**Stop if:** the board saturates. The failing shape (board 1, and board 2 before baffling)
looks like this:

| duty | board 1 TIA_Out | |
|---|---|---|
| 2 % | 597 … 3709 | linear |
| **3 %** | 98 … 3791 | **linear, marginal** |
| 4 % | 5 … 3798 | bottom railed |
| 6 %, 8 % | 4 … 4095 | both rails |

Improve the baffling, then repeat.

**Record:** 📝 the maximum linear duty.

**Optional worst case:** repeat with the overhead lights on and the photodiode aimed at them.
Board 2 gave min 1993 / max 3573: no saturation, and within 5 % of the dark result at 12 % and
25 %. Its DC operating point barely moved (2.869 V against 2.876 V), which shows the servo
absorbing the ambient with range to spare.

---

## 9. Demod phase: `cal demod` and `cal model`

**Canonical:** `BENCH_P3_DETECT.md` §3.4. The two commands take about 7 minutes, 5 of them
warm-up.

### Step 9.1 — Set up and warm up

**State:**

| | |
|---|---|
| PSU | 5.20 V, limit ≥ 2.5 A |
| Rail | up |
| HPF | **`hpf track`** |
| Target | **Start with none: nothing reflective within ~3 m**, then set the level in Step 9.2. 🔴 On a stock board, D11 → D12 crosstalk plus the floor's return alone drove board 2 1.9× past the ADC's ceiling. A well-baffled 116–120 kΩ board can be the opposite: Board 4 read **9 %** with nothing in front, and needed a wall |
| Attenuation over D12 | stock board: probably needed; a 116–120 kΩ variant: probably not, and it may need a reflector instead (Step 9.2) |

**Do:**
```
beam freq 104166
beam duty 2
beam on
beam ramp 25 500
beam
```
Wait **at least 5 minutes** at 25 %.

**Pass:** `beam` reads `freq 104166 Hz (TOP=1439)` at 25 %.
- `cal model` sweeps 80–200 kHz and restores the carrier on builds after 2026-08-24, but a
  reset mid-sweep leaves it wherever it stopped.
- The tell is in the sweep listing: phases step by (TOP+1)/64, so 22/45/67 means 104 kHz,
  and 11/23/35 means 200 kHz.

### Step 9.2 — `cal demod`, and get the level in range

**Do:**
```
cal demod
```
Read `peak`. **The target is 50–70 % of full scale.** Set the light by the table below, from
`cal demod`'s own verdict, and repeat until it passes.
- **From image 1.7, `cal demod` will not commit under 30 %.** It prints `*** TOO SMALL ***`
  and a `floor ... *** FAIL ***` line, and puts the phase back where it was. Up to 1.5 it
  committed at any level: Board 4 committed at 9 %.
- **`level` reads the current phase only** (every image; 1.6's all-phase `level` failed its
  review, V16-F1, and 1.6 was never released). Use it as a relative check while clamping the
  target after a passing sweep; it cannot find the level on a fresh board. Even at a committed
  phase it can read below the sweep's peak (9–16 % in the review's model, not a bench result).
  Its `cal demod refuses` label (1.7; from 1.8 `very low`) is not proof that a sweep would refuse. **Use `cal demod`'s
  own verdict and peak for the final 50–70 % acceptance**, including after any light adjustment.

| `peak` | Do |
|---|---|
| `*** ABOVE FULL SCALE ***`, or `saturated` > 10 % | **Attenuate the light into D12** and repeat. A card with a hole in it is best: the attenuation is geometric, and does not depend on a material behaving at 850 nm |
| 50–70 % of full scale | go on |
| under ~30 % | too little light. Take any card off D12. If there is none, **aim the board at a plain wall or a white card**, starting ~2 m away, and move it closer or angle it until 50–70 %. Board 4: a white wall ~1.2 m away, angled, gave 69 % |

🔴 **Nothing conductive near D12** (its cathode is at 36 V through R77).

**Where the light comes from does not matter; only its level does.** The phase is the
electronics' delay, and light's own travel time is nanoseconds.
- Board 4's runs off the wall at 69 % and 78 % committed 1333 and 1332 ticks.
- Its 9 % run, on crosstalk alone, committed 1342: 9 ticks, or 2°, which is worth under 0.1 %
  of the signal.
- So aim for the range; there is no need to chase a particular target.
- **Keep the board where it is through §10:** §10 uses the same light.

**Pass:** all of these:

| Line | Want |
|---|---|
| `saturated` | **0 %** |
| `peak` | 50–70 % of full scale |
| `quad null` | near 0 |
| `h3/h1` | **≈ the printed intrinsic value**: 0.111 at 25 % duty. Not ≈ 0 |
| `warm` | yes |
| committed | `phase <- NNNN ticks` |

### Step 9.3 — `cal model`

**Do:**
```
cal model
```
It takes ~128 s.

**Pass:**
- all five points accepted, and the verdict **`PURE DELAY`**. That is the expected verdict,
  not a warning sign: an earlier revision said to distrust it, and that was wrong;
- the residual close to the earlier boards: board 2 read 0.07–0.08°, board 3 0.23–0.26°.

**Stop if:** a point is rejected, the verdict is not `PURE DELAY`, or the residual is far
above those.

### Step 9.4 — Record, and compare the right quantity

**Record:** 📝
- `demod_phase_ticks`;
- the **chain delay in ns**: this is the number that transfers between boards;
- the model's residual and verdict;
- the attenuation used over D12, and the target.

⚠ **Compare the delay between boards, never `phase_ticks`.** The tick count carries a
geometric term, `phase_ticks = t_chain_ticks − (duty/2)·(TOP+1)`, so it changes with duty and
carrier even on an identical board.

| | board 2 (stock 470 kΩ) | **board 3 (116 kΩ), measured** |
|---|---|---|
| `demod_phase_ticks` at 104166 Hz | 1343–1350 | **1311** |
| chain delay | 553–600 ns | **340 ns** |
| model residual | 0.07–0.08° | **0.23°** |
| model vs `cal demod` | 3.9–4.9 ticks | **3.5 ticks** |
| TIA lag at 104 kHz (calculated) | 8.74° | 4.30° |

🔴 **If you rework a board's TIA, run `cal demod` BEFORE and AFTER on that same board.**
- It costs 26 s, and it is the only way to separate the change you made from board-to-board
  variation.
- The non-TIA part of the delay differed by 95 ns between boards 2 and 3, more than most of
  what was being measured.
- **Do not expect a fixed sign.** Board 3's lower Rf shifted −32 ticks (−17.8 predicted).
  The 1.5 pF original-board target raises the feedback RC relative to board 3, so compare the
  measured delay on the same board, and record the actual Rf/Cf, duty and carrier
  (`REWORK_ORIGINAL_BOARDS.md`).
- **If the phase does not move at all**, the feedback change did not take effect: check
  which pads the added capacitor bridges before trusting anything downstream.

### Step 9.5 — Save

**Do:**
```
beam off
cfg save
cfg
```
- `cfg save` refuses while the beam is on: the flash erase stops the supply monitor for tens
  of ms.
- Use `beam off`, not `off`, so the rail stays up. §10 then starts with §9.1's beam commands
  and another 5 minutes' warm-up.

**Pass:** `cfg` reads back the carrier, the phase, and
`phase mdl: fitted, pure delay  (80000..200000 Hz)`.

---

## 10. Threshold DAC and comparator cross-calibration

**Canonical:** `BENCH_P3_DETECT.md` §3.5. Two constants that are genuinely per part: the DAC's
reference and the LM393's offset.

**State for the whole section:** rail up; the beam ON at 104166 Hz / 25 %, warm; HPF in TRACK;
the target and attenuation as §9 left them.

⚠ **The subcommand word is required.** `threshold 5` does not mean "set 5 %".

### Step 10.1 — Polarity

**Do:**
```
threshold duty 0
threshold
threshold duty 5
threshold
```

**Pass:**
- at duty 0: `D_Comparator(46) = 1` (ABOVE), a threshold of 0 V against a ~10 mV quiescent;
- at duty 5: `D_Comparator(46) = 0` (below).

### Step 10.2 — The LOW point

**Do:**
```
threshold sweep 0 5 64
```

**Pass:** the flip comes near **0.2–0.3 % duty**, or lower on a comparator whose offset is near
zero.
- Board 4 (2026-11-11) flipped at 0.08–0.16 % over three runs; Vos came out at −0.6 mV.
- The flip sits on the DAC's first one or two levels, at 3.2 mV each, so Vos is known only to
  about ±2 mV. The real criterion is Step 10.5's.
- Run it two or three times and use the mean.

**Record:** the flip's duty, TP8's nominal volts and ADC5's volts.

### Step 10.3 — The HIGH point

🔴 **At the calibrated phase this rails.** Turn it down with `beam phase`, then freeze it in
HOLD.

**Do:**
```
beam on
beam phase <the §9 phase - ~280>
```
Board 2 used 1000, board 3 1035. Wait 3 s, then:
```
beam off
```
Wait 3 s, then:
```
hpf hold
beam on
adc 5 100
```

**Pass:** ADC5 reads **2000–2500**. If it reads 4095, lower `beam phase` further and repeat
Step 10.3 from the start.

**Then:**
```
threshold sweep 0 100 64
```

**Pass:** the flip comes somewhere mid-range.

**Record:** the flip's duty, TP8's volts and ADC5's volts.

### Step 10.4 — Put it back

**Do:**
```
hpf track
beam phase <the phase from §9>
```

### Step 10.5 — Solve for vref and Vos

At a flip, ADC5 = vref·duty + Vos, with duty as a fraction. From the LOW point (d₁, a₁) and the
HIGH point (d₂, a₂):
- **vref** = (a₂ − a₁) / (d₂ − d₁)
- **Vos** = a₁ − vref·d₁

**Pass:**
- **vref** near **3.25–3.27 V**. The compiled default is a nominal 3.300.
- **Vos** anywhere inside the LM393's **±15 mV**: a per-part offset, and boards differ.

| | board 2 | board 3 |
|---|---|---|
| DAC **vref** | 3.268 V | 3.256 V |
| comparator **Vos** | +11.0 mV | +5.4 mV |

`threshold vref <volts>` applies vref if you want it. It is RAM only, and worth ~1 % on a
threshold that §3.7 tunes empirically anyway.

**Record:** 📝 vref and Vos.

### Step 10.6 — The crosstalk check, beam OFF

**Do:**
```
beam off
hpf track
```
Wait 3 s, then:
```
threshold sweep 0 100 64
```

**Pass:** `ADC5 movement across the sweep` **< ~3 mV**. Ignore the flip line: with the beam
off there is nothing meaningful to cross.

⚠ **It must be beam-off.** With the beam on, the number is dominated by the optical
background: board 3 read 12.9 mV lit and 3.2 mV dark.

**Record:** 📝 the ADC5 movement.

### Step 10.7 — Save

**Do:**
```
threshold duty 0
cfg save
```

⚠ **`cfg save` also saves whatever threshold the DAC holds.** Without the `threshold duty 0`,
that is Step 10.1's `threshold duty 5`, level 51: the sweeps put back the level they started
from.
- **From image 1.7 the saved level is restored at boot,** so a leftover 51 would become a live
  164 mV threshold.
- Up to 1.5, `cfg` printed it but the DAC started at 0 after every reset.
- 0 is the default for a board whose detection has not been tuned. The real threshold is set
  in `BENCH_P3_DETECT.md` §3.7, and saved there.

---

## 11. Hand-off: base bring-up complete

**Do:**
```
cfg
```

**Pass:** `cfg` reads back the carrier, the phase, the phase model, `hpf sel`, the `adc5v`
scale and `cal duty`.

**Next, by goal:**
- **The photodiode (Phase 3):** `BENCH_P3_DETECT.md` §3.7 is the only per-board part of Phase
  3 left: `detect path <mm>` (nothing reports a velocity until the beam path's width is set),
  the transits, and the `cal gain` decision. §3.6b and §3.6 were design checks, done once on
  board 3 (2026-08-31). Repeat them only if this board misbehaves.
- **The strobe:** §12.
- **A Pi** (Phases 7 and 8): §12, then §13.

⚠ **Carry the 5-minute warm-up habit into Phase 3.** Optical output falls 25–45 % from cold to
plateau while the current moves only +1.7 % and the power +0.6 %, so every electrical reading
says nothing is happening. `cal demod` only warns; `scan carrier` refuses below 5 minutes.

---

## 12. The strobe on this board

**For a board that someone else will fire the strobe on.** Everything needed is in this section.
It is condensed from `BENCH_P6_STROBE.md` 6a–6d and `BENCH_REWORK_CHECK.md` §6–§8; open those
only to troubleshoot.

**What varies from board to board, and so is checked here:**
- U5's clamp, the hardware limit on every pulse's length. Its RC varies ~10 %.
- The gate loop, with this board's parts and its 1 µF gate-hold capacitor.
- The gate level that gives 9 A: Q9's threshold varies.
- The joints and Q9's heatsink in the 9 A path.

The firmware's guards and the burst engine were proven on board 1 and are not repeated.

**Rules for all of §12:**
- 🔴 **GPIO27 (`PULSE_LIMIT_DISABLE`) stays 0.** It defeats U5. `strobe` must always show
  `GPIO27 : SIO output, driven 0, pad 0 -> U5 pulse limiter armed`. If it does not, stop.
- 🔴 **Never raise a firmware limit to make a step pass.** A refusal or a failed step is a
  result: stop and record it.
- **Connect or disconnect J3 (the LED bank), and attach any lead, only with the PSU and USB
  both off.**
- **Instrument grounds go on the R66 GND leads only** (Step 12.2 part 6).
  - Never on TP5, which is a switched node.
  - Never on **J3 pin 2**. It is the LED return, and a scope's mains-earthed ground there lights
    the LED bank with nothing in the path.
- **The logic analyser's analog inputs (±10 V) never go on TP2 (12.3 V), Q10's gate (~12 V)
  or the J3 screws (36 V).** TP3 and TP4 are fine.
- Nothing conductive near D12 (36 V).

**Time:** about an hour with a DMM and the CLI; add an hour for Step 12.2 part 6's leads. Do not
start Step 12.3 at the end of a session.

### Step 12.1 — The CR-23 gate-hold capacitor (unpowered)

**State:** PSU and USB off.

**Do:** confirm a 1 µF capacitor is fitted:
- one end on **Q9's gate pin 1**, behind R63;
- the other on **R66 pad 1**, the GND pad farther from Q10;
- never to TP3, Q9's source or tab, TP5 or R67.

If it is missing, fit it first (`REWORK_ORIGINAL_BOARDS.md` §6).

**Pass:** fitted, and with the DMM in Ω:
- each end to its pad: < 1 Ω;
- the gate end to TP3: ≈ 47 Ω (R63), not a direct short;
- gate to GND, and gate to source: no stable hard short. The meter may charge the capacitor
  at first;
- across R67: ≈ 56 kΩ.

**Record:** 📝 the capacitor fitted.

### Step 12.2 — Dry, J3 disconnected

**Two levels.**
- **Parts 1–5 need only a DMM and the CLI.** They are the minimum before live current, and
  what a collaborator without a scope or logic analyser runs.
- **Part 6** adds soldered leads, a logic analyser and a scope. It measures U5's clamp dry,
  the gate loop's stability and, in 12.3, TP4 against the firmware. Use it where you can
  solder (a lab board such as Board 5).
- Without part 6, U5's clamp is still measured, with current, in 12.3 part 4.

**State:** J3 disconnected; J2 off; J8 and J4 empty.

**1. Unpowered checks** (PSU and USB off; a hand-held DMM is fine on an unpowered board):

| From → to (DMM in Ω) | Expect | Stop if |
|---|---|---|
| R61 pad 1 → GND | ≈ 1 kΩ (R62; lower through the unpowered ICs) | ≤ 20 Ω: U5's output is shorted, and no pulse can pass. Open: R62 is missing |
| J3 pin 1's screw → C26 pad 1 | < 1 Ω | a cold J3 joint (board 1, 2026-10-13): reflow it |
| J3 pin 2's screw → Q9's tab | < 1 Ω | a cold J3 or Q9-tab joint |

**Under magnification: R65 and R66**, the two large 2512 resistors beside Q10.
- They must both be fitted and both marked 0.27 Ω (`R270`, `0R27` or `R27`).
- In parallel they are the 0.135 Ω the firmware's current reading rests on (135 mV/A). Without
  a scope, nothing else checks it.
- One missing reads double: the safe direction. A lower value reads low, and `strobe cal 9`
  would then deliver more than 9 A.

**2. Rail down: the refusals.** PSU at 5.20 V on J1, and USB on. `stat` → `STANDBY`.
```
pins
strobe
strobe pulse 20
```
**Pass:**
- `pins` → `PULSE_LIMIT_DIS(27)  0`;
- `strobe` → the GPIO27 `armed` line, `gate DAC : level 0`, and `may fire : +5V rail not
  ready ...`;
- `strobe pulse 20` → `REFUSED -- nothing fired`, `+5V rail not ready`.

**3. Rail up.** Send `on`, wait 3 s, then:
```
stat
beam off
detect disarm
strobe
```
**Pass:** `BENCH_RUNNING`, latch 1; `strobe` → `rail : up`, and `may fire : yes`.

**4. Dry pulses: the firmware's path.**
```
strobe pulse 20
strobe clamptest 100
strobe clamptest 200
```
**Pass:**
- `pulse 20` → `completed, IRQ0 seen` and `GPIO25 after: low, SIO`;
- `clamptest 100` → `ERR: clamptest is for widths over 100 us. Use 'strobe pulse 100'.`;
- `clamptest 200` → `CLAMP TEST: commanding 200 us ...` and `completed, IRQ0 seen`.

This proves the firmware's path. U5's clamp itself is measured in part 6 dry, or in 12.3
part 4 with current.

**5. The gate DAC: TP3 at three setpoints.**
- **TP3 sits 0.59 mm from C56's +12 V pad.** Read it through a lead attached with the board
  unpowered (part 6's S1), not with a hand-held tip on a powered board.
- **Do:** `strobe gate 50`, `strobe gate 90`, then `strobe gate 0`, reading TP3 to GND after
  each.
- **Pass:**
  - gate 50 → **4.71–5.06 V** (board 1: 4.936 V);
  - gate 90 → **8.50–9.09 V**;
  - gate 0 → **≤ 30 mV**.
- **Stop if** TP3 reads the same at 50 and 90, or near the top at 50. The gain stage
  (U6A × 3, R60 20 kΩ / R59 10 kΩ) is then not controlling. The first live step would jump
  straight to full current.
- 🔴 **This part is required before live current.** Live mode climbs in 3 % steps on the
  assumption that TP3 = 3 × the DAC.
- ✅ **Board 4, 2026-11-11 (owner):** 4.94 V, 8.94 V and 0 V. Read by hand, with the rule-8 risk
  above.

**6. With soldered leads, a logic analyser and a scope.**

*The leads* (30 AWG, 5–8 cm, all at once, PSU and USB off):

| Lead | Point | Signal | Instrument | Nearest other copper |
|---|---|---|---|---|
| L1 | **R33**, the end farther from R57 | GPIO25: the pulse the firmware commands | LA D0 | R57's GND pad, 0.52 mm |
| L2 | **R61 pad 1**, the end farther from U8 | U5 Q: the pulse after U5's clamp | LA D1; the scope's Aux/EXT trigger | R65 GND 0.68 mm; C54 **+12 V** 1.15 mm |
| G1, G2 | **R66 pad 1**, the pad farther from Q10: two wires on the one pad | GND, at the low side of the sense resistors | every instrument ground | 4.6 mm |
| S1 | **TP3** (through-hole): feed the wire down from the top, solder it on the bottom, keep the top fillet inside the pad | Q9's gate setpoint | DMM; scope CH2 | 🔴 C56's **+12 V** pad, 0.59 mm (top) |
| S2 | **TP4** (through-hole), the same way | CurrentSense, **135 mV/A** | scope CH1, or LA analog | C54 GND, 2.03 mm |

- Twist S1 with G2, and S2 with G1.
- **Do not solder** to:
  - TP2 (1.92 mm from R15's 36 V pad);
  - R65's GND pad;
  - R67: a bridge there bypasses Q10, U5 and the sense resistor;
  - anything near D12.
- **Before power** (DMM in Ω):
  - G1 and G2 → TP1 ≈ 0 Ω;
  - S1 → G1 in the kΩ range (≈ 30 kΩ), not ≈ 0;
  - S1 → TP2 not ≈ 0: that would be TP3 bridged to +12 V;
  - S2 inspected on both sides. A TP4-to-GND bridge reads 0.135 Ω, too close to 0 to see, and
    it blinds the overcurrent check.

*The dry clamp:*
- **LA:** D0 on L1, D1 on L2, ground on G1. 3.3 V logic, at least 50 MS/s, trigger on D0
  rising, at least 2 ms of capture.
- **Do:** `strobe clamptest 200` three times, then `strobe clamptest 1000` three times.
- **Pass:**
  - D0 (GPIO25) is high for the commanded 200 or 1000 µs.
  - D1 (U5 Q) is high for the clamp: the same on all six, and **118–137 µs** (board 1:
    135 µs).
- **Stop if:**
  - the clamp is under 118 or over 137 µs. The firmware's 100 µs and 137 µs limits assume
    this band;
  - D1 is as long as the commanded width: U5 is not clamping. Check GPIO27.

*The gate loop:*
- Scope CH2 on S1: DC, 20 MHz limit, ground G2.
- **Do:** `strobe gate 50`, then `strobe gate 90`, then `strobe gate 0`.
- **Pass:** a clean DC level at 50 and 90: ripple well under 10 mV p-p, and no oscillation.
- **Stop if** it oscillates, or rings when the setpoint steps.

**Record:** 📝 parts 1, 4 and 5; with part 6, the dry clamp and the loop verdict.

### Step 12.3 — Live, J3 connected: the first current, and U5 with current

**State:**
- `off`, then `stat` → `STANDBY`, latch 0. PSU and USB off. **Connect J3**, the LED bank.
- PSU 5.20 V, with its current limit ~3 A; USB on. J8 and J4 empty; beam off.
- **What carries the safety without a scope: the firmware.** Every live firing is read back
  on ADC0 (one sample per 2 µs). Live mode then ends, with the gate at 0, on any of these:
  - overcurrent (over 10.8 A);
  - current outside the pulse, or on for more than 200 µs;
  - a pulse that is not one clean plateau;
  - an unusable record.

  Each step is also limited to 3 % above the last one measured.
- **With a scope (part 6's leads):**
  - CH1 on S2 (TP4): DC, **20 MHz limit**, 100 mV/div, 10 µs/div, single sequence;
  - CH2 on S1;
  - grounds on G1 and G2;
  - trigger from L2 into Aux/EXT trigger at ~1.6 V.
- A FLIR on Q9 and HS1 is welcome, not required. The firmware's budget, 30 mC per 10 s, keeps
  Q9's average dissipation around 30 mW.

**1. Before any current.**
- Send `on` and wait 3 s.
- DMM: VIR at J3 pin 1's screw to GND → **36 V**, the same as at C26. The screw is a
  hand-held point: 3.2 mm clear.
- Then send:
  ```
  beam off
  detect disarm
  strobe
  adc 0 4096
  adc ringtest 100
  ```
- **Pass:**
  - `strobe` → the GPIO27 `armed` line, `live : OFF`, `may arm : yes`;
  - `adc 0 4096` → **under 10 codes**;
  - `adc ringtest 100` → `PASS: every pair covered`.
- **Stop if:**
  - VIR at the screw is under 36 V: a cold J3 joint;
  - any `NOT COVERED` or `FAIL`. Every live readback would then be unusable.

**2. Arm, and fire once at gate 0.**
```
strobe gate 0
strobe live on confirm
strobe pulse 20
```
- **Expect:** `LIVE: ARMED. Gate 0; the staircase starts from level 0. Watchdog ARMED (1 s).`;
  then `Verdict: within limits`, and `no current pulse above the ~72 mA floor`.
- **Stop if:** any current at gate 0. Send `strobe live off` and `off`: Q9 is conducting with
  its gate at 0.
- **While armed:** only `help id stat pins adc5v fault off forceoff wdog strobe` are accepted.
  Live mode ends itself after 300 s with no `strobe` command; re-arming restarts from gate 0.

**3. The staircase, 3 % per step.**
```
strobe gate 3
strobe pulse 20
strobe gate 6
strobe pulse 20
   ... and on, in 3 % steps
```
- Each `strobe pulse 20` prints its plateau, e.g. `pulse  1:   1.234 A plateau ... on   20
  us`.
- Nothing flows until about 10–20 %, Q9's threshold.
- **Pass, at every step:**
  - `within limits`, and no `NOT CLEAN` line;
  - the on-time is 20 ± 2 µs;
  - the plateau rises by at most ~1.8 A from the step before.
  - *With a scope:* TP4 (135 mV/A) agrees with the plateau within ±5 % or ±0.1 A, whichever
    is larger, and is flat after at most ~2 µs.
- **Stop climbing at the first step that measures 0.5–2.5 A,** and go to 4.
- **Stop altogether** (`strobe live off`, then `off`) on any of these:
  - any `*** LIVE MODE ENDED ***`;
  - a jump of more than ~2 A in one step;
  - *with a scope:* TP4 disagrees, or rings;
  - **nothing has flowed by 30 %.** That is an open load: PSU and USB off, then check J3 and
    the strings.
- Without a scope, a turn-on spike shorter than 2 µs can fall between ADC0's samples. The
  known one is what 12.1's capacitor removes.

**4. U5 with current: once, at that level, without touching the gate.**
```
strobe pulse 20
strobe clamptest 1000
```
- **Expect:** `6d CLAMP TEST WITH CURRENT: commanding 1000 us ...`, `completed, IRQ0 seen`, and
  a pulse line ending `on <the clamp> us (<n> samples)`.
- **Pass:**
  - the on-time is **118–137 µs**: U5's clamp, measured on this board. With part 6, it also
    matches the dry clamp within ±2 µs;
  - the plateau is within ~5 % of the 20 µs pulse's;
  - no fault;
  - *with a scope:* TP4 shows the same width, not 1 ms.
- **Stop at once** (`off`, then power down) on an on-time near 1000 µs, or `*** LIVE MODE
  ENDED: CURRENT OUTSIDE THE PULSE ***`. The hardware clamp is not working. At ~2 A a 1 ms
  pulse is harmless, and the firmware cuts it at 200 µs.
- **Record:** 📝 the plateau in A, and the on-time: this board's U5 clamp.

### Step 12.4 — 9 A once, then leave

**State:** as 12.3, with live mode still armed. *With a scope:* CH1 at 200 mV/div.
```
strobe cal 9
strobe live
```
`strobe cal 9` climbs from gate 0 with one 20 µs pulse per step. It solves the level for 9 A,
then fires 3 confirmation pulses. It takes about 10–20 s; any key aborts it, with the gate at 0.

**Expect:**
- `SOLVED: level N (x %, TP3 nominal y V) for 9.00 A`;
- `confirmation mean ... -- PASS`;
- `Session cap: level M ...`;
- `strobe live` → `cap : level M`.

**Pass:**
- **`SOLVED ... PASS`.** *With a scope:* TP4 = 1.215 V ± 5 % on the confirmation pulses.
- **The solved level as a sanity check on the current reading:**
  - board 1 solved at 42.87 % (TP3 nominal 4.24 V), and the design expects 4.5–6 V;
  - a solved level well above ~60 % (TP3 nominal over ~6 V) is a finding. Stop and check
    R65/R66 (part 1) before using 9 A: the reading may be low.
- **The cap's refusal:** `strobe gate <P>`, with P = M × 100 / 1024 + 1, rounded up. Expect
  `REFUSED: over this session's cap`. The refusal is the check; never fire above the solved
  level.
- *With a FLIR:* Q9 and HS1 warm together.

**Stop if:** `CAL FAILED: CEILING` (9 A not reached by 70 %), or `OVERSHOOT`. Record it and stop.
Do not raise a limit.

**Leave:**
```
strobe live off
off
stat
```
- **Expect:** `live OFF: gate 0, engine stopped, GPIO25 low. Watchdog now off.`, then
  `STANDBY`, latch 0.
- Then PSU and USB off, and disconnect J3.

**Record:** 📝 12.3's plateau and U5 clamp; the solved level, its % and the TP3 nominal; the
session cap.

---

## 13. Before this board carries a Pi, and the handover

**For a board that someone else will run a Pi on.** Steps 13.1–13.3 are done here, with no Pi.
Step 13.4 is the sheet that travels with the board. Condensed from `BENCH_P8_PI.md` §8.0b and
§8.7.

**Already done by this point:**
- §5: the latch and supply guards, including Steps 5.3–5.5;
- Step 1.3: the partition table.

### Step 13.1 — A rollback, then the shipping image (no Pi)

**What it does:**
- It proves that this board falls back to its old image when a new one never finishes
  starting.
- Then it installs the image the board ships with.

**State:**
- PSU off is fine; USB on. J8 and J4 empty; J3 disconnected.
- PowerShell in `Hardware\firmware`, with picotool at
  `%USERPROFILE%\.pico-sdk\picotool\2.3.0\picotool\picotool.exe`.
- **Files:**
  - `build_hang\pitrac.uf2`, a test image that never buys itself, on purpose;
  - the shipping image: **1.7**, `build_v1.7\pitrac.uf2`, built 2026-11-11 (`built : Nov 11
    2026 16:41:14`, UF2 SHA256 `CCB7B931…BA15087B`). **Independent review PASS**
    (`PROGRESS.md` §10; nonblocking wording note V17-N1). On board 1 since
    2026-11-12, by an update from the Pi (`BENCH_P8_PI.md` §8.9b PASS). 1.6 failed its review (V16-F1) and is never loaded.
- **Board 4 shipped on 1.5** (partition A, with r5 in B). It takes 1.7 by this step, or as an
  update from the Pi (`pitrac-link update`).

**0. Reference.** Run `id`, `upd`, `cfg` and `hist`, and save the output.
- `upd`'s `layout` line names the partition that runs. Load into the **other** one: A is `-p 0`,
  B is `-p 1`.
- **The examples below are for Board 4: 1.5 runs in A, so load into B with `-p 1` in
  parts 1 and 2.** On a board fresh from Step 1.3 with r5 in B, use `-p 0` instead.

**1. Rollback.**
- Send `bootsel`. Then:
  ```
  & "$env:USERPROFILE\.pico-sdk\picotool\2.3.0\picotool\picotool.exe" load -p 1 -v -x build_hang\pitrac.uf2
  ```
  Or copy the UF2 onto the `RP2350` drive, if the PC allows it. Note the time.
- **Expect:** the serial monitor may show `TEST BUILD (PITRAC_TEST_TRIAL_HANG): on trial,
  hanging before the buy.`. The CLI stops answering. Untouched, the COM port then drops and
  comes back.
- **Pass:** **16–25 s** after the load:
  - `id` shows the old image again, with no `TEST BUILD` line. Board 4: `version : 1.5
    (partition A ...)`; a board fresh from Step 1.3: `version : 1.1 (partition B ...)`.
  - `upd` → `partner : version 1.1, NOT bought (a trial that failed, or never ran)`. The hang
    image calls itself 1.1.

**2. The shipping image, into the same partition.**
- Send `bootsel`. Then:
  ```
  & "$env:USERPROFILE\.pico-sdk\picotool\2.3.0\picotool\picotool.exe" load -p 1 -v -x build_v1.7\pitrac.uf2
  ```
- **Pass:**
  - `id` → `version : 1.7` and `built : Nov 11 2026 16:41:14`, in the partition you loaded;
  - `upd` → `boot : flash update boot, ON TRIAL: bought itself at the end of init`, and
    `partner : version <the old image>, bought: the fallback`. Board 4: `version 1.5`;
  - `cfg` → the same calibration as in 0. Only `seq` may move;
  - `threshold` → the level `cfg` shows, now live: 1.7 restores it at boot. Board 4 was saved
    at 0 before shipping. A board still holding §10.1's leftover shows `level 51 of 1024`.
- **Then, if the threshold is not 0 and detection has not been tuned (§10.7):**
  ```
  threshold duty 0
  cfg save
  ```
  Expect `saved: slot ...`. If `cfg save` refuses, 1.7 prints the command to send first.
- ⚠ **Never load an image older than 1.1 onto a partitioned board.** It erases the good image's
  first sector.
- ⚠ **`load` without `-x`, followed by `reboot -u`, leaves the image `not bought`**, and it never
  runs.

**3. A dry strobe check on the shipping image.** J3 still disconnected. Send `on`, wait 3 s,
then:
```
strobe
strobe pulse 20
strobe clamptest 200
off
```
**Pass:**
- the GPIO27 `armed` line;
- `pulse 20` and `clamptest 200` → `completed, IRQ0 seen`;
- *with Step 12.2 part 6's logic analyser:* 20 µs on D0 and D1, and D1 ends `clamptest 200`
  at the clamp.

**4. A power cycle.** PSU and USB off for 10 s, then on.
- **Pass:**
  - `upd` → `normal boot`, still in the partition you loaded;
  - `cfg` as in 2, with `threshold: level 0`, and `threshold` reading level 0 too: the saved
    value, restored;
  - `hist` → `loaded : flash (power-on)`.

### Step 13.2 — Firmware checks before a Pi (no Pi)

On the shipping image. These are what keep a seated Pi powered: on this board **any reset of
the RP2354 cuts the Pi's power instantly.**

**1. No USB path resets the board.**
- `hist`: note `boots`.
- On the PC: `& "$env:USERPROFILE\.pico-sdk\picotool\2.3.0\picotool\picotool.exe" reboot -f`.
  Expect picotool to find no device it can reboot. No
  `RP2350` drive appears.
- Close the serial monitor. Then in PowerShell, with the board's COM port (COM9 for Board 4):
  ```powershell
  $p = New-Object System.IO.Ports.SerialPort COM9,1200; $p.Open(); Start-Sleep -Milliseconds 300; $p.Close()
  ```
  No `RP2350` drive appears.
- Reopen the serial monitor.
- **Pass:** `hist` → `boots` unchanged.

**2. Nothing is driven into the Pi's pins without a Pi.** In `STANDBY`:
- `pins` → the `RPI5_SHUTDOWN(43)` line reads `released (... nothing driven into J8.37)`;
- `link` → `lines : released: no Pi up ...`.

**Pass:** both.

### Step 13.3 — Pack

1. Send `off`; `stat` → `STANDBY`, latch 0. Then PSU and USB off.
2. **If Step 12.2 part 6's leads were fitted** (L1, L2, G1, G2, S1, S2), remove them, or tape
   each one down so that it cannot bend onto a neighbouring pad. L2 on R61 sits 0.68 mm from
   GND and 1.15 mm from +12 V. Check for solder balls and flux where they were.
3. J3 disconnected, J2 off, J4 and J8 empty. The baffle stays as it was for §9's calibration.
4. Fill in Step 13.4's sheet, and send it with the board.

### Step 13.4 — The sheet that goes with the board

Fill in the brackets. Board 4 shipped on 1.5 (2026-11-11), so its copy says 1.5. The note on
the threshold applies from 1.7.

> **PiTrac board [4], UID [8fabac4bb8add852]. Read this before powering it.**
>
> **Before you use it.** The steps are in `Hardware/firmware/BRINGUP_NEW_BOARD.md`. They need
> a DMM, the board's USB CLI and picotool; no scope, logic analyser or soldering.
> - **Done before shipping:** §0–§11; Step 12.1; Step 12.2 part 5 (the gate DAC).
> - **Before any live strobe:**
>   1. Step 12.2 parts 1–4;
>   2. then 12.3 and 12.4;
>   3. note the U5 clamp (12.3 part 4) and the 9 A level (12.4) under "Strobe" below.
> - **Before the first Pi:**
>   1. Step 13.1: the rollback, and image 1.7 (review PASS; `PROGRESS.md` §10);
>   2. Step 13.2;
>   3. then the first-seating check under "With a Pi".
>
> **What is on it**
> - Firmware [1.5] in partition A, with [1.1] in B as the fallback.
> - Saved calibration:
>   - carrier 104166 Hz, demod phase [1333] ticks at 25 % beam duty, phase model fitted;
>   - +5 V scale [1.0781].
> - Not set yet: `detect path` (the beam width) and the detection threshold (saved at 0). Set
>   them in your housing, then `cfg save`. **On 1.5 a saved threshold is not restored after a
>   reset:** set it again after each power-up. From 1.7 it is restored.
> - After every power-up, `cfg` prints `*** BEAM IS AT 2.00 % -- the stored phase is 166 ticks
>   off ***`. That is expected: the beam starts at 2 %, and the phase applies at 25 %. Before
>   detection work: `beam duty 2`, `beam on`, `beam ramp 25 500`.
> - **`cfg save` is refused while the beam is on, the detector armed or a Pi powered.** The
>   flash write would blind the supply monitor that protects the Pi. It prints what to send
>   first: with a Pi, `off`, wait for `STANDBY`, then `cfg save`.
> - The Pi-side software and its `config.txt` lines: `Hardware/firmware/pi/README.md`.
>
> **Power**
> - **5.20 V on J1.** A supply set to 5.0 V will not latch: the board takes it for USB.
> - Current limit: **at least 5 A with a Pi** (a Pi 5 bursts to 3 A); ~3 A without one.
> - The CLI is on the board's USB-C (J6). The +5 V rail never runs from USB: the firmware
>   refuses, by design.
> - `on` and `off` at the CLI do what the panel button does.
>   - To fit a button: a momentary switch across J7.3 and J7.4, wired with the power off.
>   - J7.5, next to them, is the +5 V rail.
>   - A 5 s hold is a hard power-off.
>
> **With a Pi**
> - **Seat or remove it only with the PSU and USB both off.** The whole 40-pin header goes into
>   J8 (the socket under the board), pin 1 to pin 1. Never single jumper leads.
> - **The Pi's own USB-C stays unplugged.** It would feed the board's +5 V rail whatever the
>   board's switch says.
> - **Tape over SW2 (RUN).** Any reset of the board's microcontroller cuts the Pi's power
>   instantly: SW2, an SWD debugger, `reset force`, `bootsel force`. Plain `reset` and
>   `bootsel` have guards, but keep both commands out of Pi startup until a corrected
>   reviewed image is installed (`PROGRESS.md` §10). Rejected 1.10 still has a 50 ms
>   check-to-reset window; 1.11 (software confirm PASS; hardware acceptance pending) checks again just before the reset. Wait for PI_BOOTING or RUNNING before requesting normal `off`.
> - **The first time only, before the Pi is ever powered:**
>   - clip a DMM across R39, the 1 kΩ 0402 about 25 mm from J8 toward the microcontroller,
>     with the power off;
>   - PSU on; `stat` → `STANDBY`;
>   - expect **≈ 0 V**. More than a few mV is current into the Pi's GPIO26: stop and report.
> - The board halts the Pi through J8.37 (GPIO26 on the Pi, active low), and cuts its power
>   10 s later.
> - Firmware updates from the Pi: `pitrac-link update <file.bin>`. A new image runs on trial
>   and rolls back by itself if it never finishes starting. Never load an image older than
>   1.1.
>
> **Camera (J4)** 🔴
> - **Never connect J4 straight to the camera. Always go through a TXS0108E level-shifter
>   module.**
>   - The camera takes a 1.8 V trigger and puts out a 1.8 V strobe, with no 3.3 V tolerance.
>   - J4's trigger (J4.3 and J4.4, the same net) is 3.3 V through 220 Ω.
>   - Its strobe inputs (J4.7 for GPIO8, J4.8 for GPIO9, each through 220 Ω) need ~2.15 V to
>     read high, which 1.8 V never reaches.
>   - J4.1, .2, .5 and .6 are GND. J4 has no supply pin.
> - **Wiring the module**, with everything unpowered. The A side must be the lower voltage:
>
>   | Module | Connects to |
>   |---|---|
>   | VA (VCCA) | the camera board's **1.8 V** output pin |
>   | VB (VCCB) | **the board's 3.3 V: J5.2** (+3.3VA, the only 3.3 V on one of the board's headers; it also feeds the mic). **Not** the camera board's 3.3 V pin: leave that unconnected |
>   | GND | J4.1 (or .2, .5, .6), and the camera board's GND |
>   | OE | VA, so the module is enabled. Many modules already pull it up: check |
>   | B1 → A1 | J4.3 (the board's trigger) → the camera's trigger input |
>   | A2 → B2 | the camera's strobe output → J4.7 |
>   | A3 → B3 | a second camera's strobe output, if any → J4.8 |
>
> - **Why VB from the board, not the camera.** Each side of the module must be powered by the
>   side it talks to.
>   - The board can be on while the camera is off. The reverse cannot happen: the camera runs
>     from the Pi, and the Pi from the board.
>   - With VB on the board, an unpowered camera takes VA to 0, and the module's pins go
>     high-impedance: nothing reaches the camera.
>   - With VB on the camera's 3.3 V, a trigger high from the board (3.3 V through 220 Ω, up to
>     ~12 mA) would push current through the module's B side into the unpowered camera's 3.3 V
>     rail. That is the back-powering CR-09 warns about.
> - Keep the wires short. The TXS0108E's edge accelerators do not like long, capacitive
>   lines.
> - **There is no check of the module without a camera.** VA comes from the camera's 1.8 V,
>   so with the camera unpowered that side is dead and the module passes nothing. (An earlier
>   copy of this sheet asked for a 1.8 V high on A1 with no camera: that cannot happen.) The
>   first check needs the camera powered by its Pi; it goes with `BENCH_P5_P7_MIC_CAMERA.md` 7c.
> - The camera handshake (image 1.5 on) [has / has not] run on a bench yet.
>
> **Strobe**
> - **Every strobe firing is refused while a Pi is present** (this firmware). Strobe work is
>   done from the USB CLI, with J8 empty.
>   - A corrected image lifts this only after independent review and board 1's
>     `BENCH_P8_PI.md` §8.10 pass (`PROGRESS.md` §10). It comes as an update with
>     its own instructions; rejected 1.8, 1.9 and 1.10 are not that update.
> - J3 (the LED bank, 36 V): connect and disconnect it only with the PSU and USB off.
> - Probe grounds go on GND, never on J3 pin 2 (the LED return) or TP5.
> - **Live current:**
>   - from gate 0, `strobe live on confirm`;
>   - then, each session, `strobe cal 9`. It sets the 9 A level (RAM only) and a cap above
>     it.
>   - This board: U5 clamp [__] µs; 9 A at level [__] ([__] %).
> - `strobe` must show `U5 pulse limiter armed`. Never raise a firmware limit to get a shot
>   through.
> - The firmware ends live mode on overcurrent, on current outside the pulse, or on an
>   unusable readback. Do not re-arm until you understand why.
> - Watch Q9 and its heatsink.
>
> **Beam (the detection light)**
> - **25 % duty at most.** This board's LED base plateaus at [92.9] °C at 25 %.
> - **Nothing conductive near D12**: its cathode is at 36 V. A strip of bare foil there
>   destroyed an op-amp on board 1.
> - If the optics change (the baffle, the housing), recalibrate, with `hpf track` and the beam
>   warm (5 minutes at 25 %):
>   1. `cal demod`. Aim for a `peak` of 50–70 % of full scale. Point the board at a wall or
>      card to raise it, away to lower it, and repeat until it passes. From 1.7 it will not
>      commit under 30 %.
>   2. Clamp everything in place. `level` then reads the level live at the committed phase.
>      It is not valid before a phase has been committed.
>   3. `cal model`. Nothing may move for its ~128 s.
>   4. `beam off`, then `cfg save`.
>
> **Not done on this board:** [the standby and idle current baselines; any other gap].

---

## Sign-off

| § | item | board 1 | **board 2** (08-21) | **board 3** (08-28) | next board |
|---|---|---|---|---|---|
| 0 | 🔴 **TIA variant: Rf, and Cf incl. WHICH LEG** | stock 470 kΩ / 0.500 pF | stock 470 kΩ / 0.500 pF | **116.0 kΩ / 0.990 pF** (154 k∥R80, 100 pF on one Cf leg) | |
| 0 | Reworks (Step 0.2) | CR-23 1 µF (2026-10-15) | — | TIA (above) | |
| 1 | **PSU set to 5.20 V** (DMM at J1) | — | *see §4 row* | *see §4 row* | |
| 1 | **PSU current limit set to 2 A** | — | ✅ | ✅ | |
| 1 | Chip UID | `a764f5332ca5ac53` | *not recorded* | `6d6fda754e367a40` | |
| 1 | Partition table; image version and partition | ✅ (2026-11-02); 1.2 in A (2026-11-10) | — | — | |
| 1 | Silicon revision | A4 | *not recorded* | *not recorded* | |
| 1 | 🔴 **Pour joints inspected** (Step 1.2; J1 and J3 re-checked after landing wires) | ❌ **J3 pins 1 and 2 were cold**, found 2026-10-13 and re-soldered | *not recorded* | *not recorded* | |
| 2 | VIR (J2, no load) | 36.0 V | **36.0 V** ✅ | *not recorded* | |
| 2 | TP2 +12 V | 12.3 V | *not recorded* | *not recorded* | |
| 2 | TP6 +2V5 | 2.59 V | *not recorded* | *not recorded* | |
| 2 | 🟡 **LM5157 boost SW frequency** | *not recorded* | *not recorded* | ⚠ **801 kHz on the RAIL** (LA, not scope; could be L1 or L2) | |
| 3 | E9 `pins` check | all 0 | *not recorded* | *not recorded* | |
| 4 | +5V_IN on USB only | 4.85 V | *not recorded* | *not recorded* | |
| 4 | +5V_IN on PSU | 5.20 V | **5.207 V** ✅ | **5.210 V** ✅ | |
| 4 | 🔴 **`adc5vcal` scale** | 1.0627; **1.0694** since the 2026-10-15 trim method | **1.0617** ✅ (old method) | **1.0620** ✅ (old method) | |
| 5 | Standby current | 32 mA | *not recorded* | *not recorded* | |
| 5 | Rail-up idle current | 129 mA @ 5.2 V | *not recorded* | *not recorded* | |
| 6 | **U9 clamp width** | 122.68 µs | *not recorded* | *not recorded* | |
| 6 | Beam temp at 25 % duty | 87.5 °C plateau | ⚠ **98 °C at 10 min** (sink 78 °C) | *not recorded* | |
| 7 | TP7 TIA_Out, beam off | 2.59 V | **2.589 V**, 2.4 mV p-p ✅ | *not recorded* | |
| 7 | ADC5 σ, beam on 25 %, TRACK | — | **2.19 mV** ✅ | *not recorded* | |
| 3.6b | **Q8 coupling, HOLD (armed)** | — | *not recorded* | 🔴 **×7.43**: FAILS, see PROGRESS Q8 | |
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
| 12 | CR-23 gate-hold capacitor | ✅ 1 µF (2026-10-15) | — | — | |
| 12 | **U5 clamp width** | **135 µs** (2026-10-06) | — | — | |
| 12 | `strobe cal 9`: level, TP3 | **439** (42.87 %), TP3 4.233 V (2026-10-16; 439 again 2026-11-10) | — | — | |
| 12 | 6d with current | ✅ 135 µs at 0.70 A (2026-10-15) | — | — | |
| 13 | §8.0b; §8.7 | ✅ 2026-11-02; ✅ 2026-11-09 | — | — | |
| 13 | First seating: V across R39 | **0 V** (2026-11-09) | — | — | |
| 13 | 1000 pings; a clean `off` | ✅ 2026-11-10 | — | — | |

⚠ **The board 2 gaps above are real, not clerical.** Two of them matter:

- ⚠ **Beam temperature: board 2 runs hotter than board 1.**
  - 98 °C at the base after 10 min at 25 % (board 1: 87.5 °C), and the heatsink at 78 °C
    (board 1: 68.1 °C).
  - Its junction works out at **114–122 °C against the 145 °C maximum**: 23–31 °C of margin,
    where board 1 had 33–41. **Do not run board 2 above 25 %**, and re-read at 20–30 min for
    a real plateau: CR-12 saw the temperature still climbing +4 °C over the second five
    minutes.
  - **The mounting is not the problem.** The base-to-heatsink gradient is 20.0 K, against
    board 1's 19.4 K, so the paste and clamping are as good. The difference is all
    heatsink-to-ambient, so **airflow is the lever**: CR-12 puts 2–3× airflow at 24.5 →
    14.7 K/W total.
- **The standby and rail-up idle currents** are the baselines that make a future leak visible.
  The 80 mA reading from the PSU-limit incident is *not* a valid baseline: it was taken with
  VIR parked at 27 V.

Copy the filled column into `PROGRESS.md` §6 when done.

---

## Background

### A. Design validation and per-board work

| | **DESIGN VALIDATION: done ONCE, ever** | **PER-BOARD: done EVERY time** |
|---|---|---|
| **asks** | *how does this design behave?* | *does THIS board work, and what are ITS constants?* |
| **lives in** | `BENCH*.md` + the record in `PROGRESS.md` §6 | **this protocol** |
| **redo when** | the design changes (a respin, a firmware change) | every assembled board |

If a step here is ever found to be design-level, it belongs in a `BENCH*.md` section and should
be deleted from here.

**Already settled: do NOT redo these on a new board.**

| | established |
|---|---|
| GPIO33 polarity: **TRACK is LOW** | the TMUX1219 truth table, plus 4 independent bench confirmations |
| **Carrier = 104.1667 kHz, fixed for every board** | design decision 2026-08-28, `PROGRESS.md` §8 |
| PWM slice collisions, PIO `GPIOBASE` allocation | `ARCHITECTURE.md` A2/A7; A7 fixed in firmware 2026-10-02 (the GPIO12 ready LED is SIO on/off) |
| Q1's mechanism (U9 clamp ≈ K·R·C), Q2 (no duty limit to 250 kHz) | Phase 2c / 2d |
| Ambient rejection ≈ **48 dB**; the lock-in premise | Phase 3.3, board 2 |
| The "bursty noise" was **beam return off the room**, not a fault | Phase 3.3, board 2 |
| Netlist-derived corner frequencies | `HARDWARE_REFERENCE.md` |
| The full Pi soft-shutdown FSM matrix | Phase 1b, simulated Pi |
| The Pi-side behaviour: Q4, Q7, Q11, 20 clean cycles, the link, updates from the Pi | Phase 8, board 1 (2026-11-09/10) |
| The firmware itself | — |

**Measured on every board:**

| | why it varies | § |
|---|---|---|
| Rail integrity, current draw | assembly | 2, 5 |
| **LM5157 boost SW frequency** | part tolerance; the carrier's ±8 % margin has to cover it | 2 |
| E9 `pins` check | a different die | 3 |
| **ADC +5 V scale** 🔴 safety gate | ADC gain and divider tolerance | 4 |
| Latch guard + sustained supply monitor 🔴 | it is what stops a Pi being fed through a 1 A diode | 5 |
| Beam thermals | *this* heatsink's mounting: board 1 87.5 °C, board 2 98 °C | 6 |
| **U9 clamp width** | the 109–136 µs band from 1 % R / 10 % C | 6 |
| Static detect-chain health | assembly | 7 |
| `hpf test` polarity + **switch leakage** | leakage spans 52–250 pA across boards | 7 |
| CR-15 baffle acceptance | optics and operator discipline | 8 |
| **`cal demod` + `cal model`** | the chain delay differs 340–600 ns board to board | 9 |
| **Threshold DAC vref + comparator offset** | the LM393's Vos is ±15 mV per part | 10 |
| U5 clamp, the 9 A gate level, the 9 A joints | part tolerance and assembly | 12 |
| J8.37 released, R39, the link's joints | assembly | 13 |

### B. 🔴 The PSU at 5.20 V, every session

The design supply is a Meanwell LRS-75-5 **set to 5.2 V**, and the firmware is built around
that number. Set the bench supply to **5.20 V**, not 5.0 V.

- **Below ~5.05 V the board will not power up.** `V5_MIN_FOR_LATCH` (`src/board.h`) is
  **5.05 V**: the midpoint between USB-only power (**4.85 V** through D8) and the 5.2 V design
  supply. It is how the firmware tells a real supply from USB. A 5.0 V supply sits on the USB
  side, so `on` is refused and `adc5v` reports the latch **INHIBITED**. Nothing is damaged; it
  just will not latch.
- **While latched, `V5_MIN_SUSTAINED` = 4.90 V** (debounced 500 ms) drops the latch with
  `FAULT_SUPPLY_LOST`. A supply set low, or long thin leads that sag under load, will trip it.
- **Every expected reading in this protocol assumes 5.2 V.** For example TP6/TP7/TP9/TP10 =
  +5VA / 2 ≈ **2.59 V**, and `adc5v` ≈ **5.20 V** in §4.

Measure at **J1** with a DMM, not only on the PSU's display: §4's trim (`adc5vcal`) is made
against the DMM reading at J1.

### C. 🔴 The PSU current limit: 2 A, checked every session

**This cost a full bench session on 2026-08-18.**
- VIR's bulk capacitance needs **amps** for a few milliseconds at startup (~100 µF charged to
  36 V is ~65 mJ).
- Against a low limit, the supply drops into CC, **the boost never finishes its soft-start**,
  and it parks at a lower voltage.

**What makes it vicious is that everything looks fine afterwards.**
- Parked at 27 V, the steady draw is tiny: the 12 V shunt pulls only (27 − 12) / 4K7 = 3.2 mA.
- So the supply sits far below its limit, **+5V_IN reads a healthy 5.2 V**, the CLI works, the
  LED works, and the firmware's supply monitor is perfectly happy.
- The fault exists only during a startup transient that nobody observes.

**The symptom to recognise: VIR low (e.g. 27 V instead of 36 V) AND the idle current low (e.g.
80 mA instead of ~129 mA), at the same time.** The two travel together, because a lower VIR
means the shunt draws less. If you see both, **suspect the supply before the board.**

⚠ The firmware cannot detect this: **there is no VIR sense, and PGOOD is unconnected**
(`NEXT_BOARD_REV.md` **CR-08**). This incident is the concrete argument for that change.

### D. 🔴 Cold joints on the copper pours

**Why this board is prone to them:**
- Every copper pour on this layout connects to its pads **solid, with no thermal relief**. That
  includes the inner **GND (In1)** and **+5V (In2)** planes, the two largest areas of copper on
  the board.
- A pad tied to a pour soaks up the iron's heat, so the solder can look acceptable on top
  without ever wetting the pin or the barrel.
- Screw terminals make it worse: tightening the screw and landing heavy wire lever the pins
  against a marginal joint.

**Found on board 1, 2026-10-13:** both **J3** pins (VIR and VIR_RTN) were cold.
- Pin 1: C26 read 36 V, but the J3 screw read 7 V (floating).
- Two 6c staircases fired into an open LED bank, and every firing read "no current". It cost a
  bench day.

**Reworking one:**
- Preheat the board.
- Use a large chisel tip on a high-power iron, with plenty of flux.
- Feed the solder from the side opposite the iron, and hold until it shows on the far side.

`NEXT_BOARD_REV.md` **CR-22** adds thermal relief to the pours.

### E. 🟡 The boost's switching frequency

⚠ **Downgraded from 🔴 on 2026-08-31.**
- **The concern was** that an odd harmonic of the fixed 104.1667 kHz carrier could fold the
  boost into the 15.39 kHz passband and look like a ball.
- **That has been tested directly, and it does not happen.** `scan carrier 95000 115000 9` on
  board 3 put the 7th harmonic **inside** the measured switcher band at three separate points,
  and σ_noise stayed flat (4.57–5.08, a 10.8 % spread). The coupling is too weak to matter.

⚠ **The design-time margin quoted earlier was also wrong.** It assumed 1.055 MHz nominal, but
the only switcher tone found on the +5 V rail is at **801 kHz**. That would have made the
margin **−2.3 %**, not −9.6 %. The direct measurement above supersedes both numbers.

🔵 **Still worth writing down**, because it is free while the scope is there, and it is the only
way to settle whether that 801 kHz is the LM5157 (L1) or the RP2350's own core buck (L2 →
U3.63 `VREG_LX`). `BENCH_P3_DETECT.md` §3.6 Check 1 has the exact pad.

🔵 If the spread across boards approaches ±7 %, that is a **design** finding, not a board
finding (`BENCH_P3_DETECT.md` §3.6).
