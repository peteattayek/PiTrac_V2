# Phase 8 — Real Pi 5 integration and clean-shutdown acceptance

**Prereq:** Phase 1b passed. "Phases 2–7 done" was relaxed by the owner on 2026-11-09 for 8.0–8.5,
8.8 and 8.9, which touch only power, shutdown and the link (`PROGRESS.md` §8). Still no camera
on J4; beam off; strobe dry, except §8.10 (approved by the owner 2026-11-12, to run after
a reviewed image: 1.11, software confirm PASS; 1.8, 1.9 and 1.10 failed theirs). **Power:** PSU 5.2 V, **current limit
≥ 5 A** — a Pi 5 alone can pull 3 A in bursts. **Gear:** scope (2 channels minimum), DMM, a
Pi 5 with a **backed-up** SD card.

⚠ **Corrected 2026-08-25:** this line used to read "Phases 1b (including the **Q10 rework**)".
**The Q10 rework is not a gate** — `PROGRESS.md` Q10 measured it on 2026-07-31 and demoted it
to optional defence-in-depth. Every reset also opens the +5 V latch, so the spurious request
reaches a Pi that is losing its rail in the same instant; the level failure is a footnote to
the power cut, not an independent hazard. The mitigation that matters is the `reset`/`bootsel`
CLI guard, which is already implemented.
**2026-10-16:** that guard covers the CLI only. Two USB reboot paths and SWD bypass it; see
the reset-source table in §8.0. **2026-10-19:** the firmware closes both USB paths (image
`Oct 20 2026 14:59:52`, r5, independent confirm review passed, flashed on board 1 2026-10-30); SWD still bypasses the guard.

This is the first time a Pi 5 is ever seated on J8. Everything before this point
exists to make this step boring.

---

> ## FIRMWARE STATUS, audited 2026-08-25, updated 2026-11-02
>
> | | Exists today? |
> |---|---|
> | Power FSM, latch, USB guard, sustained-supply monitor | ✅ **yes, and proven** — full Phase 1b matrix passed against a simulated Pi |
> | Pi soft-shutdown handshake, both timeouts, RPI5_ON fallback | ✅ yes |
> | `reset` / `bootsel` guard while a Pi is powered | ✅ yes, **at the CLI only**: the USB reboot paths and SWD bypass it |
> | `pisim` for simulating a Pi | ✅ yes |
> | **8.6 halt telemetry** (`PS_RUNNING` watching for an unrequested Pi drop, a `hist` command) | ✅ **flashed on board 1 2026-10-30** (r5); §8.0b Steps 4–5 PASS against a simulated Pi — 8.6 below |
> | **USB reboot paths closed** (a 1200-baud open; `picotool … -f`) | ✅ **flashed on board 1 2026-10-30** (r5); §8.0b Step 2 PASS (2026-11-02) |
> | **J8.37 released unless a Pi's 3V3 is up** (it used to be driven high into an unpowered Pi) | ✅ **flashed on board 1 2026-10-30** (r5); §8.0b Steps 3–4 PASS: released in STANDBY and when the latch opens |
> | Pi-side unit, shutdown hook and persistent journald | 🟡 **written 2026-10-19** in [`pi/`](pi/README.md); **installed and self-checked on a new Pi 5 off the board, 2026-10-26** (Lite, Trixie); not yet run on the board |
> | **The serial link** (UART1 on J8.8/J8.10: status requests; `link` at the CLI, `pitrac-link` on the Pi) | 🟡 **flashed on board 1 2026-10-30** (r5) — the lines connect and release with a simulated Pi (§8.0b Step 4); the protocol needs a Pi: 8.8 below |
> | **Firmware updates from the Pi** (A/B partitions, try-before-you-buy, rollback; `upd`, `pitrac-link update`) | 🟡 **flashed on board 1 2026-10-30** (r5); partition table installed 2026-11-02, §8.7 PASS 2026-11-09 (trial, buy and rollback over USB); 8.9 needs a Pi — 8.7 (no Pi) and 8.9 below |
>
> **8.0 through 8.5, 8.8 and 8.9 are runnable now** (the gate relaxed 2026-11-09; §8.0b and §8.7
> passed on board 1). They do not need Phase 7's camera handshake (written 2026-11-10; in images from 1.5, whose
> confirm review passed 2026-11-10).

## How to read the procedures below

Every section opens with a **board state** table. Set the board to exactly that state first.
🔴 **On this phase, "board state" includes a Pi with a backed-up card and tape over SW2.**

**Before switching the PSU off:** `hist` → `flash : up to date`. The halt counts reach flash
only after 10 s in STANDBY. Switching off sooner, or with the Pi up, loses every count since
the last save. Board 1 lost 8.9's counts this way (2026-11-10, found 2026-11-12).

**No panel button wired?** `on` and `off` at the CLI do what a short press does
(`power_fsm.c`: in RUNNING, `s_req_shutdown || take_short_press()` → `begin_shutdown()`), so
use them wherever a step says "press the panel button". To fit a button: a momentary switch
across **J7.3 (PWR_TOGGLE)** and **J7.4 (GND)**, wired with PSU and USB off. Never a loose shunt
or a hand-held wire on a powered board: J7.4 sits next to **J7.5, the +5 V rail that powers the
Pi**. And a press held **5 s** is the escape hatch, a hard power-off from any state.

---

## 8.0 — The gate. Do not seat a Pi until every line is ticked.

| | Check | Where |
|---|---|---|
| ✅ | Phase 1b full test matrix passed against a simulated Pi (2026-07-31) | `BENCH.md` Phase 1b |
| ✅ | Firmware built **after 2026-07-31** — you need the `reset`/`bootsel` guard (r5) | `PROGRESS.md` §9 |
| ✅ | **Q6 resolved** — Mira220 1.8 V I/O question, if cameras are connected (none: no camera on J4) | `PROGRESS.md` Q6 |
| ✅ | SD card imaged and the image verified restorable (2026-10-30) | — |
| ✅ | `PI_SHUTDOWN_ACTIVE_LOW` still 1, `gpio-shutdown` overlay params written to the Pi (`gpioinfo`: GPIO26 active-low, consumer `shutdown`, 2026-10-26) | 8.1 below |
| ✅ | ~~**Phases 2–7 complete.**~~ **Relaxed by the owner, 2026-11-09**, for 8.0–8.5, 8.8 and 8.9: they touch only power, shutdown and the link. Still: no camera on J4, beam off, strobe dry, except §8.10 (live strobe with a Pi: approved 2026-11-12, on reviewed image 1.11, software confirm PASS; 1.8–1.10 did not pass) | `PROGRESS.md` §8 |
| ✅ | **Camera board schematic in hand** if J4 is connected (it is not) — the Mira220 1.8 V problem is real and unresolved | `BENCH_P5_P7` §7.0 |
| ☐ | **Firmware with the USB reboot paths closed** (written 2026-10-19, flashed on board 1 2026-10-30), checked in §8.0b Step 2 (PASS 2026-11-02). Until it is flashed: no `picotool -f`, and nothing that opens the board's serial port at 1200 baud, while a Pi is up | §8.0b |
| ☐ | **Firmware that releases J8.37 whenever no Pi's 3V3 is up** (written 2026-10-19, flashed on board 1 2026-10-30; older images drive it high into an unpowered Pi through R39), checked in §8.0b (PASS 2026-11-02) | §8.0b |
| ☐ | **§8.0b passed** on the image you will run with the Pi (board 1, r5: PASS 2026-11-02) | §8.0b |
| ☐ | **§8.7 passed** on this board: the partition table installed, and a trial, a buy and a rollback seen over USB (board 1: PASS 2026-11-09) | §8.7 |
| ☐ | **SW2 taped over** | below |
| ○ | *Optional:* Q10 10 kΩ pull-up fitted. Defence-in-depth — **not a gate** | `BENCH.md` Q10 |

> The latch **is** the Pi's power switch. A firmware bug here corrupts a
> filesystem. That is the entire reason phases 1b and 8 are separate.

### 8.0b — Firmware checks before seating (no Pi; added 2026-10-19)

For image `Oct 20 2026 14:59:52` (r5) or later. **Before the reflash**, on the image now on the
board: `id` and `cfg`, saved to `captures/` (this is §8.7 Step 0). Step 5 below and §8.7
compare `cfg` against it, so it must come from the old image. An image older than 2026-10-19
has no `hist` (it answers `? 'hist'`); the counts start with the new image, and Step 1 notes
the first ones. **Then reflash:** `bootsel` at the CLI, then drag `build/pitrac.uf2` onto the
drive. Its first boot is a trial (§8.7): `upd` then
reads `ON TRIAL: bought itself at the end of init`. About 30 minutes.

| | |
|---|---|
| PSU | 5.2 V, ~3 A limit, on J1. USB to the PC for the CLI |
| Pi / J4 | **none** on J8, nothing on J4 |
| Beam, strobe | off. No LED-bank work in this section |
| Gear | DMM; scope or logic analyser on J8.37 (pin 37) for step 4; the Phase 1b jumpers (`BENCH.md` Phase 1b: J5.2 → J8.1 and J5.2 → J8.15) |
| Probes | 🔴 attach the DMM and scope leads to J8.37, J8.10 (pin 10) and GND **with PSU and USB off**, before the reflash's power-up; move the DMM between J8.37 and J8.10 only with PSU and USB off again. Nothing conductive near D12 (36 V) |

**Step 1 — identity.** `id` → `built : Oct 20 2026 14:59:52`; the `fw` lines end with
`+ the Pi serial link ('link') and firmware updates from the Pi into` / `the other A/B
partition, try-before-you-buy ('upd') (2026-10-20)`; and `version  : 1.1`, followed by `(no
partition table: flat at 0x000000)` before §8.7 or `(partition A at 0x002000)` (or B) after it.
Note `hist`'s `boots` count.

**Step 2 — no USB path resets the board.** Pass: the board keeps running and `hist`'s `boots`
does not change.
1. On the PC: `picotool reboot -f`. Expect picotool to report that it found no device it can
   reboot. No `RP2350` drive appears.
2. Close the serial monitor. In PowerShell, with your COM number:
   ```powershell
   $p = New-Object System.IO.Ports.SerialPort COM5,1200; $p.Open(); Start-Sleep -Milliseconds 300; $p.Close()
   ```
   No `RP2350` drive appears.
3. Reopen the serial monitor (its usual settings). `hist` → `boots` unchanged.
- ⚠ If the COM port number changed after the reflash, that is the new USB descriptor, not a
  fault.

**Step 3 — J8.37 and the UART in STANDBY, no jumpers.**
- `pins` → the `RPI5_SHUTDOWN(43)` line reads `released (latch open or no Pi 3V3: nothing
  driven into J8.37)` (image 1.3 on; up to 1.2, `released (no Pi 3V3 sensed: ...)`).
- DMM, J8.37 to GND: ~0 V. The node floats now; a 10 MΩ meter reads near 0 V. The old image
  read ~3.3 V here.
- `link` → `lines    : released: no Pi up, so nothing is driven into J8.10 ...`. DMM, J8.10
  (pin 10) to GND: ~0 V.

**Step 4 — the simulated Pi.** Jumpers on J5.2 → J8.1 (3V3 sense) and J5.2 → J8.15 (RPI5_ON),
fitted with PSU and USB off; scope or LA on J8.37, trigger on the falling edge. Pulling and
refitting the J8.15 jumper with the board up is the Phase 1b method: it carries only +3.3VA
through 1 kΩ. **Not** a shunt across J8.15–J8.17: J8.17 is on +3V3_Pi5 in the netlist but has
no copper on these boards (`NEXT_BOARD_REV.md` CR-27), so it stays at 0 V. Jumper nothing to
J8.37: it is the board's output.
1. `on` → `stat` reads `RUNNING` within a second; `pisim` → `RPI5_SHUTDOWN(43)= driven,
   deasserted`. J8.37 ≈ 3.3 V. `link` → `lines    : CONNECTED to UART1`, and J8.10 ≈ 3.3 V (the
   UART's idle high; nothing is on the other end).
2. **The request:** `off`. Expect J8.37 low for **200 ms**, then high again. Then pull the
   J8.15 jumper (the simulated Pi "halts"). After the holdoff (10 s from image 1.3, 15 s
   before) the latch drops, and `pins`
   shows `released`.
   - `hist`: `Pi shutdown requests` +1, `...Pi read down in time` +1, `power cuts to a powered
     Pi` unchanged.
   - `link` read **right after pulling J8.15**, before the latch drops: `released`. The UART
     lets go with RPI5_ON, not with the latch.
3. **An unrequested halt:** jumper J8.15 back, `on`, `RUNNING`. Pull J8.15 for ~3 s. The next
   command prints `*** THE PI WENT DOWN WITHOUT A REQUEST from the board. ...`; `stat` shows the
   `DOWN WITHOUT A REQUEST` line, and the latch stays 1. Put the jumper back: the next command
   prints `*** The Pi read up again ... a Pi reboot.`
   - `hist`: `Pi went down unrequested` +1, `...and came back (Pi reboot)` +1.
4. **A power cut:** `forceoff` in RUNNING. `hist`: `power cuts to a powered Pi` +1.
5. **A reset under the simulated Pi:** `on`, `RUNNING`, then `reset force` (safe: no real Pi).
   After the reboot, `hist`: `power cuts to a powered Pi` +1; `this boot` reads `software
   reboot ('reset', a boot ROM reboot, or a trial's rollback)` (up to image 1.2: `('reset',
   BOOTSEL, or a firmware update)`); `loaded` reads `RAM mirror (warm reset;
   nothing lost)`.
6. Pull both jumpers.

**Step 5 — the counts reach flash.** Leave the board in STANDBY for 15 s. `hist`'s `flash`
line reads `up to date -- slot A` (or B). Power everything off (PSU and USB) for 10 s, then on:
`hist`'s `loaded` line reads `flash (power-on)`, `boots` is +1, and the other counts are as they
were. `cfg`: every calibration field as before the reflash; only `seq` has moved.

**Record** the results in `PROGRESS.md` §6. ✅ Pass when every step reads as above.

### First seating — before the Pi is ever powered (added 2026-10-16)

1. PSU **and** USB off. Plug the Pi's 40-pin header straight into J8, the 2×20 socket on the
   board's underside, so all 40 pins connect at once: not single jumper leads (J8.2/4 carry
   the Pi's +5 V, up to ~3 A, and one misplaced lead puts 5 V on a 3.3 V GPIO).
   - The Pi sits **under** the board, component side up, on M2.5 standoffs at MH1, MH2 and MH4
     (its 58 × 49 mm holes). Its USB/Ethernet end sticks out past the board's edge.
   - Pin 1 then meets pin 1. J8.1 is the square pad among J8's top-side solder joints; the
     Pi's pin 1 is at the header end away from its jacks, on the inner row.
   - Check that no pin sits outside the socket, and that the standoffs let the header seat
     fully without strain.
   - With a ribbon or an extender instead, check the orientation before power: rotated,
     J8.2/4's +5 V lands on Pi GND and GPIO pins.
2. **The Pi's own USB-C stays unplugged for the whole phase.** It would feed the board's
   switched +5 V rail through J8.2/4, whatever the latch says.
3. With everything still off, clip the DMM across **R39**: one lead on each of its two pads, DC
   volts, no GND lead. R39 is the 1 kΩ in series from GPIO43 to J8.37, not a resistor to GND.
   It is an 0402 about 25 mm from J8 toward the RP2354. Edge to edge (PCB, 2026-11-23), its
   pads are 0.48 mm apart, R86 (threshold PWM) is 0.38 mm away and C74 0.57 mm, all 3.3 V-level
   nets. Clips only, attached unpowered: never a hand-held tip (`HANDOFF.md` §3 rule 8). PSU on.
   `stat` reads `STANDBY` and latch 0.
   - **No clip that fits an 0402?** Check the same thing **before step 1, with J8 empty**
     (Board 5, 2026-11-23).
     - PSU and USB off. Push jumper wires into J8's socket at **pins 37, 10 and 39** (GND).
       Pin 1 is the square pad among J8's top-side joints; odd pins share its row.
     - Power up to `STANDBY`. `pins` shows `RPI5_SHUTDOWN(43)` `released`, and `link` shows
       `released`.
     - DMM, **J8.37 → J8.39** and **J8.10 → J8.39: ≈ 0 V.** A released node reads near 0 V on
       a 10 MΩ meter, while anything driving it reads ~3.3 V. Whatever would push current into a
       seated, unpowered Pi drives these pins here.
     - PSU and USB off, pull the wires, then go on with step 1. Skip steps 3–4's R39 reading.
     - Don't hand-probe J8.37's top-side joint instead: it is 0.84 mm from J8.35, J8.38 and
       J8.39.
4. **Expect ≈ 0 V across R39:** J8.37 released (firmware from 2026-10-19), no current into the
   unpowered Pi. Any reading above a few mV is current into the Pi's GPIO26 (V / 1 kΩ): stop
   and report. Record **J8.1** (the Pi's 3V3) in STANDBY too.
5. **Not possible up to image 1.7 (corrected 2026-11-10); from 1.8 it is §8.10.** Every strobe firing, dry or
   live, is refused while a Pi is present: `REFUSED: a Pi is present -- Phase 6 runs with J8
   empty` (`strobe_plan.c`). Firing with a Pi running belongs to Phase 7, the camera-triggered
   shot, and needs a deliberate firmware change first. That test will be the +5 V shots
   (`BENCH_P6_STROBE.md` Step 4b, logic analyser on J8.2) at 9 A, `strobe pulse 100` and
   `strobe sched 50 fire`, against these numbers:
   - Without a Pi, board 1 dipped ~0.30–0.33 V for ~0.28 ms per boost refill, to ≈ 4.85 V.
     A fast-ball burst gives ten such dips in 7.7 ms (`PROGRESS.md` §6, 2026-10-16).
   - The Pi 5 flags under-voltage at ~4.63 V, and its own load lowers the starting level. If
     a dip reaches ~4.7 V, or `vcgencmd get_throttled` shows an under-voltage bit afterwards,
     stop and report.

### 🔴 The one thing to internalise before you start

**An RP2354 reset is a hard power cut to the Pi, not a reboot.** Net 62 `LATCH_CONTROL` is
Q2's gate + R12 + GPIO15: on any reset the pad goes high-Z, R12 pulls the gate low, and the
+5 V rail opens. The Pi loses power instantly — no shutdown request, no sync, no warning.

This applies to **every** reset source:

| Source | Mitigated? |
|---|---|
| `reset` / `bootsel` over the CLI | ✅ Guarded — refuses while a Pi is powered. `force` overrides. |
| **SW2 (RUN button)** | ❌ **Operator discipline only. Do not press it with a Pi up.** |
| **USB: a host opens the serial port at 1200 baud** | 🟡 **Closed in the 2026-10-19 firmware** (r5 flashed on board 1 2026-10-30; §8.0b Step 2 checks it). Older images: ❌ not guarded, the SDK's reboot-to-BOOTSEL hook is on by default |
| **USB: `picotool load -f` or `picotool reboot -f`** | 🟡 **Closed in the 2026-10-19 firmware**: same cause, same fix |
| **SWD: a debugger reset**, from J8.18/22 or a probe (OpenOCD's `program` resets the target) | ❌ Operator discipline only. From image 1.1 on, an SWD load also overwrites the A/B partition table; `tools/flash_swd.sh` refuses unless told otherwise |
| **A firmware update from the Pi** (`pitrac-link update`) | ✅ By design: the board shuts the Pi down through its normal request (J8.37) and resets only once the latch is open (8.9) |
| Hardware watchdog | ✅ **OFF by default since 2026-08-31** — it is armed only when something deliberately arms it (`wdog on`, or Phase 6). The old "enabled while no Pi is powered" policy protected a *Pi* from a hung MCU; on the bench there is no Pi and a reset just drops the latch mid-measurement. `stat` shows the state. |
| Brownout on +5V_IN | ❌ Unavoidable, and it takes +3V3 down too |

It is a deliberate tradeoff, not a defect: R12's pull-down is what makes the latch
**fail-safe**, so the rail opens if the MCU dies — the right call on a board driving 9 A
strobe pulses. You cannot also have "the Pi survives an MCU reset" without a supervisor or
hold-up that would weaken exactly that property.

**Practical consequence: put a piece of tape over SW2 while a Pi is seated.**

---

## 8.1 — Overlay parameters and polarity (Q7)

On the Pi, in `/boot/firmware/config.txt`:

```
dtoverlay=gpio-shutdown,gpio_pin=26,active_low=1,gpio_pull=up
```

Firmware committed to **active-low** in `board.h` (`PI_SHUTDOWN_ACTIVE_LOW 1`)
because it matches this overlay default. The parameters above are written
explicitly rather than relying on defaults, so a future Raspberry Pi OS change
cannot silently invert the contract.

**Verify before trusting it:**

```bash
# On the Pi:
gpioinfo | grep -i 26          # should show the line consumed by gpio-shutdown
journalctl -b | grep -i shutdown
```

| Check | Pass |
|---|---|
| Overlay loaded, GPIO26 claimed | `gpioinfo` shows the line in use |
| From `RUNNING`, press the panel button | Pi begins an orderly halt within ~1 s |
| Scope J8.37 during the request | **200 ms low pulse**, already confirmed in Phase 1b, then high. Released (floating) once the latch opens |

**Q7 closes here.**

---

## 8.2 — Does the Pi's header 3V3 drop at halt? (Q4)

> **Answered 2026-11-09 on board 1: NO.** J8.1 held ~3.25 V for 25 s after a `sudo halt`, and for
> 12 s after a requested shutdown (8.1), until the latch opened (`PROGRESS.md` §6). RPI5_ON does
> all the down detection; `board.h` and `power_fsm.c` say so.

### Board state

| | |
|---|---|
| PSU | 5.2 V, limit **≥ 5 A** |
| Rail | **up and latched**, Pi fully booted to userspace |
| State | `stat` should read **`RUNNING`** |
| Beam | **off** — irrelevant here and one less load |
| SW2 | **taped over** |
| Gear | **DMM on J8.1** (Pi header 3V3) |

### Step 1 — confirm the starting state

```
stat
```

**Expect:** `state : RUNNING`, `pi : present 1  3v3 1  userspace 1  isdown 0`.

🔴 **If it reads `BENCH_RUNNING`, the board thinks there is no Pi** — the detect window
expired. That is Q11 (see 8.3) and you should measure it before continuing, because in
`BENCH_RUNNING` a button press is a hard power cut on a booting Pi.

### Step 2 — halt and measure

The FSM's "Pi is down" indicator is `!PI_3V3_SENSE || !RPI5_ON`. Which of the two
actually fires decides whether the design is resting on a real signal or on a
fallback.

```bash
sudo halt
```

Then, with the board still latched, DMM on **J8.1**:

| Reading after halt | Meaning |
|---|---|
| **~0 V** | The header 3V3 rail drops. `PI_3V3_SENSE` is a real primary indicator. |
| **~3.3 V** | The PMIC holds it up. `PI_3V3_SENSE` never fires; **RPI5_ON is doing all the work.** |

Record the answer in `PROGRESS.md` §6 and close Q4. If 3V3 stays up, say so
loudly in `board.h` — the OR in `power_pi_is_down()` stops being belt-and-braces
and becomes the only thing that works.

**Also record the shutdown duration**: time from the button press to the
down-indicator firing. `PI_SHUTDOWN_MIN_HOLDOFF_MS` should be about **2×** that.
Measured 2026-11-09: ≤ 2.5 s. Set to **10 s** in image 1.3 (owner, 2026-11-10; 15 s before).

---

## 8.3 — Pi 5 +5V → header 3V3 latency (Q11) — **new, and it sets a constant**

> **Measured 2026-11-09 on board 1: 5.56 ms**, two cold starts, the same to one 3.2 µs sample.
> Reference: the switched +5 V's logic-threshold crossing at J8.4, so this is an upper bound on
> the 4.5 V-referenced number below. Under 1 s: the 3 s window stays (`PROGRESS.md` §6, Q11).

This is the measurement that `PI_DETECT_WINDOW_MS` was invented for and has never
been taken. Until it is, that constant is a guess.

**Why it matters:** `POWERING_ON` looks for a Pi for `PI_DETECT_WINDOW_MS` (3 s)
after the latch closes. If a real Pi takes longer than that to raise its header
3V3, the board concludes "no Pi" and drops into `BENCH_RUNNING`, where a button
press is a hard `FORCE_OFF` — a power cut on a booting Pi. The late-detect
promotion in `BENCH_RUNNING` is a backstop for exactly this, but the window
should be right on its own.

### Board state

| | |
|---|---|
| PSU | 5.2 V, limit **≥ 5 A** |
| Rail | 🔴 **DOWN to start** — this measures the latch *closing*, so begin in STANDBY |
| Board temp | 🔴 **cold.** Rails down for a minute first; a warm restart is faster and would flatter the number. |
| Pi | seated, powered only through the latch |
| Beam | off |
| SW2 | taped over |

**Setup:**

| Scope ch | Point | Signal |
|---|---|---|
| 1 | **J8.2** | +5 V switched rail (the latch closing) |
| 2 | **J8.1** | Pi header 3V3 |

Trigger on **ch1 rising**. Press the panel button. Single-shot capture, ~5 s of
timebase.

**Measure:** time from ch1 crossing 4.5 V to **ch2 crossing 2.54 V**.

> The 2.54 V threshold is not arbitrary. `PI_3V3_SENSE` is GPIO24 behind the
> R45/R44 divider (×0.909), and the RP2350 needs ~2.31 V (0.7 × VDD) to read a
> reliable high. 2.31 / 0.909 = **2.54 V at J8.1**. That is the instant the
> firmware can actually see the Pi, which is later than the instant the rail
> starts moving.

| Measured latency | Action |
|---|---|
| **< 1 s** | 3 s window is comfortable. Leave it. |
| **1–2 s** | Leave it, but record the margin. |
| **> 2 s** | **Raise `PI_DETECT_WINDOW_MS` to ~3× the measurement.** |

Record in `PROGRESS.md` §6 and close Q11. Repeat from a **cold** board (rails
down for a minute) — a warm restart can be faster and would flatter the number.

---

## 8.4 — The reset guard, and Q10 if you fitted it

> ⚠ **Do not press SW2 as a test here.** It cuts the Pi's power (see the box in
> §8.0). There is nothing to learn from doing it deliberately that is worth an
> unclean shutdown. This section tests the *guard*, not the reset.

### The CLI guard (the mitigation that matters)

With a Pi up and running, from the board's CLI:

| Command | Pass |
|---|---|
| `reset` | **REFUSED**, with the latch explanation. Pi keeps running. |
| `bootsel` | **REFUSED** likewise |
| `stat` | still shows `RUNNING`, latch 1 |
| `off`, wait for the latch to drop, then `reset` | **Allowed** — no Pi powered, so no guard |

The `force` variants exist deliberately and are not tested here; just know they
bypass the guard and will cut the Pi.

### Q10, only if you fitted the pull-up

Skip this if you didn't — it is defence-in-depth and the latch drop dominates
regardless.

```bash
# On the Pi, in one terminal:
journalctl -f | grep -iE 'power|shutdown|halt|key'
```

With the Pi up, scope J8.37 and confirm it sits **above 2.3 V** at idle and
during a `reset force` (which you may not want to run at all). The meaningful
check is simply that the pull-up holds the line high whenever firmware is not
actively asserting — a static DMM reading answers it.

---

## 8.5 — Clean-shutdown acceptance

The actual acceptance test for the whole power subsystem. **Budget an hour** — 20 full boot
and shutdown cycles is not a quick check.

### Board state

| | |
|---|---|
| PSU | 5.2 V, limit **≥ 5 A**, and **left alone for the whole run** |
| Rail | starts **down** |
| Pi | seated, **card backed up and the backup verified restorable** |
| Beam | **off** for the whole run — it is 0.79 A of extra load and no part of this test |
| SW2 | 🔴 **taped over.** Twenty cycles is exactly when a reflex reach happens. |
| Gear | scope or `stat` for the latch timing |

⚠ **Do not reflash between cycles.** A reflash resets the RP2354, which is a hard power cut to
the Pi — that is the one thing this test exists to avoid, and doing it mid-run invalidates the
count.

Run **20 full cycles**: button on → wait for full boot → button off → wait for
the latch to drop → repeat.

**`tools/p8_cycles.ps1` runs them** (added 2026-11-09). Close the Serial Monitor first: the COM
port has one owner.
- It drives the cycles over the USB CLI: `on`, wait for `RUNNING` with the Pi's userspace up,
  settle 20 s, `off`, wait for `STANDBY` with the latch open, rest 15 s.
- It checks every `stat` (no fault, no `BENCH_RUNNING`, no unrequested halt), times the latch
  opening against the holdoff (`-HoldoffSeconds`: 10 by default; 15 for an image before 1.3),
  and compares `hist` before and after.
- It sends only `id`, `stat`, `hist`, `on` and `off`, and stops on anything unexpected.
- With `-PiHost` and an SSH key, it also reads each boot's id, `vcgencmd get_throttled` and the
  kernel's ext4/mmc warnings from the Pi.
- It writes `captures/p8_8.5_cycles_<time>.log` and `.csv`. Try `-Cycles 1` first.

```powershell
cd Hardware\firmware
powershell -ExecutionPolicy Bypass -File tools\p8_cycles.ps1 -Port COM5 -PiHost pitrac2@pitrac2.local
```

**At the end, the filesystem check.** This replaces the old `sudo touch /forcefsck`, which
the systemd in Raspberry Pi OS Trixie may not honour. Force one full check through the kernel
command line, then remove it:
```bash
sudo sed -i '1 s/$/ fsck.mode=force/' /boot/firmware/cmdline.txt
sudo reboot        # the board counts an unrequested halt and a Pi reboot (8.6), rail up
# once it is back. Trixie checks the root in its initramfs, so systemd-fsck-root only
# reports "skipped ... ConditionPathExists=!/run/initramfs/fsck-root"; the result is here:
sudo cat /run/initramfs/fsck.log
sudo dumpe2fs -h /dev/mmcblk0p2 2>/dev/null | grep -iE 'state|error|last checked|mount count'
sudo sed -i 's/ fsck.mode=force//' /boot/firmware/cmdline.txt
```

| Check | Pass |
|---|---|
| Every cycle reaches `RUNNING` | 20/20 |
| Every shutdown completes before `PI_SHUTDOWN_MAX_WAIT_MS` | no `PI_SHUTDOWN_TIMEOUT` faults; `hist`: `read down in time` +20. A timeout means the Pi's shutdown hook found a disk still writable, and its serial console shows `pitrac: ... left high`: stop and report |
| Latch never drops before `PI_SHUTDOWN_MIN_HOLDOFF_MS` | scope or `stat` |
| No filesystem errors after any cycle | the script's `-PiHost` check, or `journalctl -b -k -p warning` on the Pi |
| `fsck` clean at the end | the forced check above: `fsck.log` shows passes 1–5 with no errors; `dumpe2fs`: `clean`, `Last checked` at that boot, no error counts. An "extent tree ... could be shorter. Optimize? yes" line is an optimisation, not damage, though it alone prints `FILE SYSTEM WAS MODIFIED` |

Twenty is not arbitrary — a 1-in-20 corruption rate is the kind of thing that
looks fine in a three-cycle test and destroys a card in a week of real use.

---

## 8.6 — Recording repeated or unrequested Pi halts

**Requirement:** if the Pi 5 halts repeatedly — especially when nobody asked it
to — that must leave a durable trace. A spurious halt is the exact symptom of a
Q10-class fault, and it is precisely the kind of intermittent problem that is
invisible without a counter.

### Two witnesses, and neither is sufficient alone

| Witness | Knows | Blind to |
|---|---|---|
| **RP2354** | Whether *it* asserted RPI5_SHUTDOWN — i.e. **intent** | What userspace did about it |
| **Pi journald** | The halt, its cause, the systemd chain | Whether the board ever asked |

A halt the board did **not** request is the smoking gun, and only the RP2354 can
identify one. Correlating the two is what turns "the Pi keeps rebooting" into a
diagnosis.

### Where it should live: the RP2354

It is the authoritative witness, because it is the only one that knows intent —
and because it **survives the failure**. If the SD card is the casualty, the Pi's
own log is the least trustworthy record of what happened to it.

**Ride this on the flash config block already scheduled for Phase 3**
(`PROGRESS.md` §9: versioned, CRC'd, writer running from RAM with interrupts off
and core 1 parked, because the chip executes XIP from that same flash). The
counters are a handful of words; the hard part is the flash-write machinery,
which is being built anyway for `demod_phase_ticks`.

Proposed counters:

| Field | Meaning |
|---|---|
| `boot_count` | RP2354 resets. Correlates a rise here with the Q10 hazard. |
| `pi_shutdown_requested` | Times we asserted RPI5_SHUTDOWN |
| `pi_shutdown_clean` | Pi indicated down before `PI_SHUTDOWN_MAX_WAIT_MS` |
| `pi_shutdown_timeout` | `FAULT_PI_SHUTDOWN_TIMEOUT` raised |
| **`pi_down_unrequested`** | **Pi went down while in `PS_RUNNING` with no assertion from us. The one that matters.** |
| `last_fault`, `last_fault_uptime_ms` | Context for the above |

Expose over the CLI (extend `stat`, or a `hist` command) so a field unit can be
interrogated without a debugger.

### What the firmware does (written 2026-10-19; `PROGRESS.md` §6)

**`hist`** prints the counts; `stat` has a one-line summary. They are kept in RAM, in a RAM
mirror that survives a reset, and in the config record. The board writes that record by itself
after 10 s in STANDBY, so never with a Pi powered.

| Counter | Meaning |
|---|---|
| boots, boots after a watchdog hang | RP2354 resets, and the reason for this one |
| Pi shutdown requests / read down in time / timed out | the board's own requests and how they ended |
| **Pi went down unrequested** | **RUNNING, and the Pi read down (3V3 or RPI5_ON low) for 1 s with no request from the board. The one that matters.** |
| ...and came back (Pi reboot) | the same Pi read up again without a power cycle |
| power cuts to a powered Pi | the latch opened on a Pi whose 3V3 was up, other than after a clean shutdown. A reset counts at the next boot |
| faults latched, last fault | with its uptime and boot number |

**What the board does about an unrequested halt:** it counts it, announces it at the CLI and
**keeps the rail up**. Q4 decides whether it should do more: if the header 3V3 does not drop
at halt, detection rests entirely on `RPI5_ON`. Dropping the rail is arguably more correct
(the Pi is down, there is nothing to power), but it should not be implemented until 8.2 says
which signal is trustworthy. **8.2 answered (2026-11-09): the 3V3 does not drop, so RPI5_ON is
the signal.** **Decided 2026-11-09:** the rail stays up after an unrequested halt; `off` or the
button ends the session (`PROGRESS.md` §8).

### With a real Pi (after 8.5)

| Do | `hist` must show |
|---|---|
| `sudo reboot` on the Pi | `Pi went down unrequested` +1, then `...came back` +1. The rail stays up. ✅ board 1, 2026-11-09 |
| `sudo poweroff` on the Pi, then the panel button | `went down unrequested` +1, then `Pi shutdown requests` +1 and `read down in time` +1. No power cut. ✅ board 1, 2026-11-09 (`off` for the button) |
| A board-requested shutdown (button) | `requests` +1 and `read down in time` +1. ✅ board 1, 2026-11-09: §8.1 and §8.5's 20 cycles (`off`) |

Compare with the Pi's `journalctl --list-boots`: every boot on the Pi should match a board
boot, a Pi reboot, or a board power-on.

### Pi side

The files are in [`pi/`](pi/README.md): the RPI5_ON unit, the late shutdown hook and persistent
journald. `sudo ./install.sh` installs them, with the Pi off the board on its own supply. The
journal gives boot-to-boot history that survives power cycles: `journalctl --list-boots`.

The hook drops RPI5_ON only after checking that every disk filesystem is read-only (independent
review 2026-10-19). If a shutdown leaves one writable, the line stays high and the board counts
a timeout, not a clean shutdown.

## 8.7 — The A/B partition table, try-before-you-buy and rollback (no Pi; added 2026-10-20)

An update from the Pi (8.9) goes into the **other** half of an A/B pair of flash partitions,
and the boot ROM runs the new image **on trial** until it buys itself. This section installs
the partition table, once per board, and has the boot ROM show each of those steps on this
board over USB before anything depends on it. About 30 minutes.

| | |
|---|---|
| PSU | off is fine. USB to the PC. The latch stays open throughout |
| Pi / J4 | none on J8, nothing on J4 |
| Beam, strobe | off |
| Files | `build/pitrac.uf2` and `build/pitrac_pt.uf2`, image `Oct 20 2026 14:59:52` or later; for Step 3 the test image `build_hang/pitrac.uf2` |
| Tools | picotool: `%USERPROFILE%\.pico-sdk\picotool\2.3.0\picotool\picotool.exe` |

**What changes for good once the table is in** (HANDOFF §4):
- A UF2 dropped on the BOOTSEL drive goes into the partition the boot ROM would **not** boot,
  and runs on trial; the image buys itself at the end of its init. So reflashes alternate.
  **The first one after the table goes into B:** with no image in B the ROM would boot A, so it
  writes B (boot ROM source, `varm_nsboot.c`; found on board 1, 2026-11-02).
- An image older than 1.1 must **never** be loaded: it does not know about the partitions,
  bus-faults at its first config read, and its own boot erases the good image's first sector.
  Recovery is BOOTSEL and a 1.1+ UF2.
- `tools/flash_swd.sh` refuses: SWD writes at flash offset 0, over the table.

**Step 0 — the reference.** On the image now on the board: `id` and `cfg`, saved to
`captures/`, and `hist` too if that image has it (from 2026-10-19 on). `cfg` is what "the
calibration survived" is checked against. If §8.0b came first, this is the capture taken
before its reflash, and the `hist` reference is the one §8.0b Step 1 noted.

**Step 1 — the new image, still flat.** `bootsel`, drag `build/pitrac.uf2`. If §8.0b came first,
that was its reflash: do not repeat it. Its Step 1 saw the trial boot, and its later resets
make `upd` read `normal boot` now. Check `id` and `cfg` below.
- `id` → `version  : 1.1  (no partition table: flat at 0x000000)`, `built    : Oct 20 2026 14:59:52`.
- `upd` → `boot     : flash update boot, ON TRIAL: bought itself at the end of init` and
  `layout   : no partition table: the image runs flat from 0x0.`
- `reset`, then `upd` → `boot     : normal boot`: bought, it now runs on a plain boot.
- `cfg` → the same values and slot as Step 0.

Pass: all four.

**Step 2 — the table, then the image (into B).**
1. `bootsel` at the CLI. The `RP2350` drive appears.
2. PowerShell, in `Hardware\firmware`: `picotool load build\pitrac_pt.uf2`, then
   `picotool reboot -u` (back to BOOTSEL, so the boot ROM reads the new table). The table
   occupies flash sector 0; the config sectors are untouched. **Then wait 5 s**
   (`Start-Sleep -Seconds 5`) while the board re-enumerates. A picotool command sent sooner fails
   with `...returned an error: rebooting` or `Communication with RP2350 device failed` (Board 5,
   2026-11-23).
3. `picotool partition info` → two partitions: 0 from 0x2000 to 0x100000, 1 from 0x100000 to
   0x1fe000, the second linked to the first as its B. Save the output.
   If it still answers `ERROR: The RP2350 device returned an error: rebooting`, wait a few
   seconds and run it again.
4. Drag `build/pitrac.uf2` onto the drive. It goes into **B** (above).
5. `id` → `version  : 1.1  (partition B at 0x100000)`. `upd` →
   ```
   boot     : flash update boot, ON TRIAL: bought itself at the end of init
   layout   : this image in partition B, 0x100000-0x1fdfff
              partner A 0x002000-0x0fffff; config 0x1fe000-0x1fffff
   partner  : not erased, and no IMAGE_DEF this firmware can read: UNKNOWN
   staged   : idle
   ```
   `boot` reads `normal boot` instead if the board was reset after the drag. `UNKNOWN` is
   expected here: A's first sector still holds the flat image's bytes from offset 0x2000, which
   are neither erased nor an image (no block start marker there; checked on r5's `.bin`). The
   boot ROM finds no image in A, and Step 3 overwrites it.
6. `cfg` → the same calibration and slot as Step 0; only `seq` may have moved (the telemetry
   write). `hist` → the counts carried over, `boots` higher.

Pass: 5 and 6 as shown. Anything else in `upd`: stop and record it.

**Step 3 — rollback: an image that never buys itself.** The test image is already built:
`build_hang/pitrac.uf2`, `Oct 20 2026 14:59:56`, SHA256 `7366EC15…13AD943` (`PROGRESS.md` §6, r5).
Use it as it is. Only if it is missing, build it in its own directory (the PATH line is in
HANDOFF §4; `build_hang/` is gitignored):
```powershell
cd Hardware\firmware
& "$env:USERPROFILE\.pico-sdk\cmake\v4.3.4\bin\cmake.exe" -B build_hang -G Ninja -DPITRAC_TEST_TRIAL_HANG=ON -DCMAKE_BUILD_TYPE=Release
& "$env:USERPROFILE\.pico-sdk\cmake\v4.3.4\bin\cmake.exe" --build build_hang
```
1. `bootsel`, then copy `build_hang\pitrac.uf2` onto the drive. It goes into **A**, the
   partition the ROM would not boot. Note the time. Both images are named `pitrac.uf2`, so copy
   by path rather than dragging from a window: in `Hardware\firmware`,
   `Copy-Item build_hang\pitrac.uf2 E:\`, with the `RP2350` drive's letter.
2. The serial monitor, if it reconnects in time, shows `TEST BUILD (PITRAC_TEST_TRIAL_HANG): on
   trial, hanging before the buy.`, and the CLI does not answer.
3. Without anyone touching the board, the COM port drops and comes back. Note the time.
4. `id` → `version  : 1.1  (partition B at 0x100000)` and **no** `TEST BUILD` line.
5. `upd` → `partner  : version 1.1, NOT bought (a trial that failed, or never ran)`.

Pass: the board is back on B by itself, 16–25 s after the drag (the boot ROM's trial watchdog
is 16.7 s from launch).

**Step 4 — the real image into A, and a power cycle.** `bootsel`, drag `build/pitrac.uf2`. It
goes into A: the unbought test image there is never chosen on a normal boot, so the ROM would
boot B. `upd` → `this image in partition A`, `ON TRIAL: bought itself`, and `partner  : version
1.1, bought: the fallback`. B keeps r5: at a buy the boot ROM erases the other partition's first
sector only if the other would otherwise win the next normal boot, which takes a higher
version, or an equal version in A (equal versions boot A). Power everything off for 10 s, then
on: `upd` → still partition A, `normal boot`. `cfg` → as Step 0.

Pass: as stated.

**Record** the `upd` outputs, `picotool partition info` and the rollback time in `PROGRESS.md`
§6.

## 8.8 — The serial link (Pi seated; after 8.5)

| | |
|---|---|
| Board | as 8.5: the Pi seated, `RUNNING`; image r5 or later, §8.7 done |
| Pi | `pi/install.sh` run, `dtparam=uart0=on` and the gpio-shutdown line in `config.txt`, rebooted |
| Probes | DMM leads on J8.10 (pin 10) and GND, attached with **PSU and USB off** |

1. **Before `on`**, with the Pi unpowered: `link` → `released`; DMM J8.10 ≈ 0 V.
2. `on`, wait for `RUNNING`: `link` → `CONNECTED to UART1`; J8.10 ≈ 3.3 V.
3. On the Pi:
   - `pitrac-link ping` → `board    : PiTrac RP2354, protocol 1, version 1.1`;
   - `pitrac-link info` → the same `built` stamp as `id` on the USB CLI;
   - `pitrac-link status` → `power    : RUNNING`, and `5V_IN` within 0.05 V of `adc5v` (or of
     `5V_IN` in `stat`: the same calibrated reading);
   - `pitrac-link hist` → the same counts as `hist`.
4. **1000 requests:** `for i in $(seq 1000); do pitrac-link ping >/dev/null || echo FAIL; done`
   prints nothing; `link` → `frames` up by 1000 good, `CRC errors` 0.
5. **The lines go with the Pi:** the panel button. During the Pi's shutdown, `link` stays
   `CONNECTED` until the Pi's hook drops GPIO22, then reads `released` while `stat` still shows
   the latch closed (the holdoff).

Pass: every step as written.

**Board 1, 2026-11-10: PASS** (`PROGRESS.md` §6), with `off` for the button ("No panel button
wired?" above).
- Steps 1–2: J8.10 at 0 V with `link` `released`, then 3.3 V with `CONNECTED`. At `PI_BOOTING`
  10.2 s the lines were still released: they wait for RPI5_ON.
- Step 3 as written. `status` read 5.162 V, against 5.161 V from `stat`.
- Step 4: no `FAIL`. `frames` went from 4 to 1004 good, with 0 CRC errors, 0 cut short and
  0 junk bytes.
- Step 5: `released` at every `link` after `off`, while `stat` read `SHUTTING_DOWN` with the
  latch at 1 (4.9, 8.6 and 12.2 s); then `STANDBY`, latch 0.
- **Not seen:** `CONNECTED` early in the shutdown. The Pi drops GPIO22 ≤ 2.5 s after the request
  (§8.1), before the first `link` arrived. To see it, send `link` within about a second of `off`.

## 8.9 — Firmware update from the Pi (after 8.8)

| | |
|---|---|
| Board | as 8.8, `RUNNING` |
| Build | `PITRAC_FW_VERSION_MINOR` set to **2** in `CMakeLists.txt` (keep it: each new image needs a higher number). **Built 2026-11-10 in `build_v1.2/`** (`Nov 10 2026 09:47:13`, r5's code with only the version changed; `PROGRESS.md` §6). `build/` keeps r5 for the other boards' §8.0b and §8.7. Copy the image to the Pi under its own name, from `Hardware\firmware`: `scp build_v1.2\pitrac.bin pitrac2.local:pitrac_v1.2.bin` |
| USB | the PC on the board's CLI, logging to `captures/` |

1. On the Pi: `pitrac-link update pitrac_v1.2.bin`. Expect, in order:
   ```
   image    : pitrac_v1.2.bin, 220636 B, version 1.2, try-before-you-buy, SHA-256 3efb1072abe34cb4
   board    : version 1.1 (built Oct 20 2026 14:59:52) at 0x100000; installs into 0x002000
   sending  :   0% ... 100%
   sent     : ... B in ~3 s, 0 retries; the board checked the SHA-256 and the image
   ready    : version 1.2 is staged in the board's RAM
   Apply now? ... [y/N]
   ```
   (The partitions may read the other way round.) Answer `y`: `applied  : the board is shutting
   this Pi down (J8.37)...`
2. The Pi shuts down. The board's CLI then prints `update: the Pi is off: installing into the
   other partition` and, ~3 s later, `update: installed and verified; rebooting into it on
   trial`; after the reboot the new banner, `update: the new image runs and has bought itself`
   and `update: powering the Pi back on`.
3. When the Pi is back: `pitrac-link upd-status` → `update   : installed, 1.1 -> 1.2` and, on
   the next line, `it ran on trial and bought itself`. `pitrac-link info` → `version  : 1.2`,
   the other partition. On the board, `hist` → `Pi shutdown requests` +1, `...Pi read down in
   time` +1, `power cuts to a powered Pi` unchanged; `upd` → no `pending` line. `this boot`
   reads `power-on, brownout, ...` here up to image 1.2, a wrong label (`PROGRESS.md` §9;
   `loaded : RAM mirror` shows the warm reset); from 1.3, `flash update boot (...)`.
4. **Rollback with a Pi:** the test image (§8.7 Step 3's) built with this `CMakeLists.txt` is
   `build_hang_v1.2/pitrac.bin`, `Nov 10 2026 09:48:59` (built 2026-11-10). Copy it under its own
   name, `scp build_hang_v1.2\pitrac.bin pitrac2.local:pitrac_hang_v1.2.bin`, and run
   `pitrac-link update pitrac_hang_v1.2.bin` (`220876 B`, `SHA-256 93f780c0e86a1e43`). The tool
   says the version is not newer; answer `y`. Its note says the buy will erase the old image.
   That overstates the rule (`PROGRESS.md` §9), and this image never buys itself, so nothing is
   erased. Expect the shutdown and the install as
   above, then the trial hangs, the board runs 1.2 again ~17 s later and powers the Pi. On it,
   `pitrac-link upd-status` → `update   : ROLLED BACK, 1.2 -> 1.2` and, on the next line, `the
   new image never bought itself (it crashed, hung or did not start)`.

Pass: 1–4 as written. Record the time from `y` to the Pi's login prompt, and the board's log, in
`PROGRESS.md` §6.

**Board 1, 2026-11-10: PASS** (`PROGRESS.md` §6).
- **Step 1:** the lines as shown above. 220636 B were sent in 3.2 s, with 0 retries.
- **Steps 2–3:** `upd-status` → `installed, 1.1 -> 1.2`, `it ran on trial and bought itself`.
  On the board, `upd` showed partition A, `ON TRIAL: bought itself` and `partner  : version 1.1,
  bought: the fallback`: the higher version, bought in A, left B alone. `hist`: requests and in
  time 31 (+1), cuts 2.
- **Step 4:** 220876 B were sent in 3.3 s. Then `ROLLED BACK, 1.2 -> 1.2`, and the board was
  back on 1.2 in A (`normal boot`), with partner `version 1.2, NOT bought`; the Pi was powered
  again. `hist`: requests 32, boots +2 (the test image's trial, then the rollback), `boots after
  a watchdog hang` 0.
- **Time:** about 40 s from `y` (owner). The code's timeline to the Pi being back up is ~38 s:
  the 15 s holdoff, 1 s quiet, a ~3 s install, the reboot and buy, 1 s, then a ~16.7 s boot.
- **Afterwards:** the board runs 1.2 from A. B holds the test image, never bought: r5 is gone
  from B, and there is no bought fallback.

### 8.9b — Image 1.7 from the Pi (rewritten 2026-11-12; was image 1.3)

**1.7 replaces 1.3 here** (2026-11-12). 1.3 never ran on board 1, and 1.7 contains its changes.
1.7 passed its independent review on 2026-11-12, and it is the image Board 4's collaborator
will install the same way, from 1.5. So this step also rehearses their update.

`build_v1.7/pitrac.bin`: Release `Nov 11 2026 16:41:14`, 239916 B, SHA256
`F7129CD4FD07D6926C106F86B4B91593163A1D97CAD3F7565B5F12EF84C94602` (`PROGRESS.md` §6,
"Image 1.7").
- Its update handoff sits where 1.2's does (`s_mirror` 0x20000110, `s_notes` 0x200001b0, the
  same sizes; checked on both ELFs 2026-11-12). So 1.2 hands over, and 1.7 reports the update
  and powers the Pi back.

**What changes from 1.2, as seen here:**
- **From 1.3:**
  - `PI_SHUTDOWN_MIN_HOLDOFF_MS` goes from 15 s to **10 s** (owner, 2026-11-10);
  - `hist`'s `this boot` names a flash update boot;
  - the released J8.37 reads `released (latch open or no Pi 3V3: nothing driven into J8.37)`.
- **From 1.5:** the Phase 7 camera handshake. J4 stays empty, and nothing here exercises it.
- **From 1.6/1.7:**
  - the saved threshold is applied at boot;
  - `cfg save` names the command that clears its refusal;
  - `cal demod`'s 30 % floor, not exercised here.
- **Unchanged:** every strobe firing, dry or live, is refused while a Pi is present. Step 4
  checks it with the Pi up. Image 1.11's software confirm review passed; its physical
  Pi/strobe acceptance is §8.10 (1.8, 1.9 and 1.10 failed their reviews).

| | |
|---|---|
| Board | the Pi seated, as 8.5: PSU 5.20 V at J1, limit ≥ 5 A; SW2 taped over; the Pi's own USB-C unplugged; J4 empty; beam off. **J3, the LED bank, disconnected:** the strobe stays dry with a Pi seated (`PROGRESS.md` §8, 2026-11-09). 1.2 in partition A |
| USB | the PC on the board's CLI, logging to `captures/` |

1. **Before, with the rail down** (`stat` → `STANDBY`, latch 0), run `id`, `upd`, `cfg` and
   `hist`, and keep the output.
   - `id` → `version : 1.2 (partition A at 0x002000)`, `built : Nov 10 2026 09:47:13`, UID
     `a764f5332ca5ac53`.
   - `upd` → `partner : version 1.2, NOT bought`: 8.9 Step 4's test image.
   - `cfg`: note the saved threshold level. From 1.7 it is applied at boot.
2. `on`. Wait for `RUNNING` and the Pi's login. On the Pi, `pitrac-link ping` → `version 1.2`.
3. **The update.**
   - PC, in `Hardware\firmware`: `scp build_v1.7\pitrac.bin pitrac2.local:pitrac_v1.7.bin`.
   - Pi: `pitrac-link update pitrac_v1.7.bin`. Expect:
     - `image    : pitrac_v1.7.bin, 239916 B, version 1.7, try-before-you-buy, SHA-256
       f7129cd4fd07d692`;
     - `board    : version 1.2 (built Nov 10 2026 09:47:13) at 0x002000; installs into
       0x100000`;
     - no `NOTE` line.
   - Answer `y` only if the SHA-256 matches.
   - **Then, as 8.9 Steps 1–2:**
     - the Pi shuts down, after 1.2's 15 s holdoff;
     - `update: the Pi is off: installing into the other partition`, and ~3 s later
       `update: installed and verified; rebooting into it on trial`;
     - the USB port drops at the reboot: reconnect the serial monitor;
     - `update: the new image runs and has bought itself`, then `update: powering the Pi back
       on`.

     About 40 s from `y` to the Pi's login.
4. **When the Pi is back:**
   - Pi: `pitrac-link upd-status` → `installed, 1.2 -> 1.7` and `it ran on trial and bought
     itself`. `pitrac-link info` → `version  : 1.7`, built `Nov 11 2026 16:41:14`.
   - Board:
     - `id` → `version : 1.7 (partition B at 0x100000)`, the same UID;
     - `upd` → `this image in partition B`, `ON TRIAL: bought itself` and `partner  : version
       1.2, bought: the fallback`;
     - `hist` → `this boot : flash update boot (a UF2 load, or an update from the Pi)`. The
       Pi shutdown requests and `...read down in time` are each +1 from Step 1, and the power
       cuts to a powered Pi unchanged;
     - `cfg` → the same values as Step 1; `threshold` → the level noted in Step 1.
   - **The interlock, with a real Pi:**
     - `stat` → `pi       : present 1`;
     - `strobe pulse 20` → `REFUSED: a Pi is present -- Phase 6 runs with J8 empty`.

     J3 is disconnected, so nothing could flow even if it fired.
5. **The holdoff.** Send `off` and wait for `stat` → `STANDBY`, latch 0: the script refuses to
   start otherwise. Close the serial monitor, then run the cycle script, which times the latch
   from the host:
   `powershell -ExecutionPolicy Bypass -File tools\p8_cycles.ps1 -Port COM5 -Cycles 3 -PiHost pitrac2@pitrac2.local`
   (board 1's port). Expect `latch open 10.0x s after 'off'` on every cycle, then `PASS: 3/3
   cycles.`
6. Reopen the serial monitor. In STANDBY:
   - `stat` → `J8.37    : released (latch open or no Pi 3V3: nothing driven into J8.37)`.
   - **If Step 1's threshold was not 0 and nothing on board 1 uses it:** `threshold duty 0`,
     then `cfg save`. It saves in STANDBY, and is refused with the Pi powered.

Pass: 3–6 as written. Every latch opening is 9.9–10.5 s after `off`, and none comes earlier.

**Board 1, 2026-11-12: PASS** (`PROGRESS.md` §6).
- **Steps 3–4:** `update : installed, 1.2 -> 1.7`, `it ran on trial and bought itself`; `id`
  → `version : 1.7 (partition B at 0x100000)`, `built : Nov 11 2026 16:41:14`; `partner :
  version 1.2, bought: the fallback`. `hist`: `flash update boot`, requests and in time 31,
  cuts 2. `cfg` unchanged, and its `save` line named the fix with the Pi up.
- **The interlock:** `pi : present 1`; `strobe pulse 20` → `REFUSED -- nothing fired`, `a Pi
  is present -- Phase 6 runs with J8 empty`.
- **Step 5:** latch open 10.04, 10.03 and 10.04 s after `off`; RUNNING after 16.6–16.7 s;
  5V_IN min 5.176–5.186 V; `throttled=0x0`, 0 storage warnings; `PASS: 3/3 cycles`.
- **Step 6:** the 1.7 J8.37 label. The threshold was already 0.
- The first run of the script stopped at `STOP: start in STANDBY` because Step 5 did not yet
  say `off`. Fixed above.

**Afterwards:** 1.7 runs from B, bought, with 1.2 in A as the fallback. Board 4 can take 1.7 by
the same path (`BRINGUP_NEW_BOARD.md` Step 13.4's sheet).

**If `upd-status` reads `ROLLED BACK, 1.2 -> 1.7`,** 1.7 did not finish its trial on this
board. The board runs 1.2 from A again and powers the Pi. Stop, keep `upd`, `hist` and the log,
and do not send 1.7 to Board 4.

**If the result is `INSTALL FAILED`**, stop and record the board's `update:` lines and `upd`.
The old image still runs and the Pi is powered again. If the detail line names **`CLEANUP`**,
the partner partition may hold an image that a reset would launch without a trial.
- **Do not reset or power-cycle the board, except into BOOTSEL to reflash it.** Entering
  BOOTSEL launches no image: `bootsel` at the CLI or SW1+SW2, then drag `build/pitrac.uf2`,
  as in §8.7 Step 4.
- Then `upd` should show `partner  : erased: no image`, or an older version that is `bought`.

### 8.9c — Image 1.12 from the Pi (added 2026-11-18)

1.12 is 1.11 with text only (`PROGRESS.md` §6, "Image 1.12"). Its independent review passed on
2026-11-18, with three nonblocking wording notes, V112-N1–N3, queued for the next image.

`build_v1.12/pitrac.bin`: Release `Nov 18 2026 15:57:13`, 243436 B, SHA256
`DC1B64BE993A73A90986A337039912970FFB28A231909C54833C88C5A56C04EB`.
- Its update handoff sits where 1.11's does (`s_mirror` 0x20000110, `s_notes` 0x200001b0, the
  same sizes; confirmed by the review). So 1.11 hands over, and 1.12 reports the update and
  powers the Pi back.

| | |
|---|---|
| Board | Board 1 as it stands after 7c: the Pi seated, SW2 taped over, the Pi's own USB-C unplugged, PSU 5.20 V at J1 with the limit ≥ 5 A, beam off, **J3 disconnected**. 1.11 in partition A, 1.7 in B |
| J4 | 8.9b had J4 empty. The 7c camera wiring may stay **only if the owner allows it for this step**: the Pi, and with it the camera's 1.8 V, is off through the board's reboot, and the TXS0108E isolates while VA is 0. Otherwise power down and unplug the module's J4 and J5.2 leads first |
| USB | the PC on the board's CLI, logging to `captures/` |

1. **Before, with the rail down** (`stat` → `STANDBY`), run `id`, `upd` and `hist`, and keep
   the output.
   - `id` → `version : 1.11 (partition A at 0x002000)`, `built : Nov 12 2026 15:42:51`, UID
     `a764f5332ca5ac53`.
   - `upd` → `partner  : version 1.7, bought: the fallback`.
2. `on`. Wait for `RUNNING` and the Pi's login. On the Pi, `pitrac-link ping` → `version 1.11`.
3. **The update.**
   - PC, in `Hardware\firmware`: `scp build_v1.12\pitrac.bin pitrac2.local:pitrac_v1.12.bin`.
   - Pi: `pitrac-link update pitrac_v1.12.bin`. Expect:
     - `image    : pitrac_v1.12.bin, 243436 B, version 1.12, try-before-you-buy, SHA-256
       dc1b64be993a73a9`;
     - `board    : version 1.11 (built Nov 12 2026 15:42:51) at 0x002000; installs into
       0x100000`;
     - no `NOTE` line, since 1.12 is newer.
   - Answer `y` only if the SHA-256 matches.
   - **Then, as 8.9 Steps 1–2:**
     - the Pi shuts down, after 1.11's 10 s holdoff;
     - `update: the Pi is off: installing into the other partition`, and ~3 s later
       `update: installed and verified; rebooting into it on trial`;
     - the USB port drops at the reboot: reconnect the serial monitor;
     - `update: the new image runs and has bought itself`, then `update: powering the Pi back
       on`.

     About 35 s from `y` to the Pi's login.
4. **When the Pi is back:**
   - Pi: `pitrac-link upd-status` → `installed, 1.11 -> 1.12` and `it ran on trial and bought
     itself`. `pitrac-link info` → `version  : 1.12`, built `Nov 18 2026 15:57:13`.
   - Board:
     - `id` → `version : 1.12 (partition B at 0x100000)`, `built : Nov 18 2026 15:57:13`, the
       same UID;
     - `upd` → `this image in partition B`, `ON TRIAL: bought itself` and `partner  : version
       1.11, bought: the fallback`. 1.7 is gone from B;
     - `hist` → `this boot : flash update boot (a UF2 load, or an update from the Pi)`. The Pi
       shutdown requests and `...read down in time` are each +1 from Step 1, and the power cuts
       to a powered Pi are unchanged.
5. **The 1.12 text:**
   - `help`: the Phase 7 lines read `J4 is 3.3 V: a Mira220 only through a` / `TXS0108E module
     (CR-09)`.
   - `cam`: `J4       : 3.3 V. A Mira220 only through a TXS0108E module (CR-09);` then `t_cam
     ~0.2 us measured with one (7c.1, 2026-11-18).`
   - Neither prints `NO CAMERA`.

Pass: 3–5 as written.

**If `upd-status` reads `ROLLED BACK, 1.11 -> 1.12`,** or the result is `INSTALL FAILED`: stop,
and follow the last two paragraphs of 8.9b with 1.11 for 1.2. The board runs 1.11 from A again.

---

## 8.10 — The strobe with a Pi present (image 1.11; PASS on board 1, 2026-11-12)

✅ **Approved by the owner, 2026-11-12** (`PROGRESS.md` §8). That approval remains recorded.
**Image 1.11 independent confirm review PASS** (`PROGRESS.md` §10); V110-F1 closed.
Report: `captures/review_2026-11-12/v1_11/INDEPENDENT_REVIEW_v1.11.md`. This is software
acceptance only. Run the exact image and Step 1's Pi-off reset/BOOTSEL regression before
the live measurements. Never load rejected 1.8, 1.9 or 1.10.
- Image 1.8 failed its review (V18-F1: live mode read the power FSM's debounced Pi flag;
  `captures/review_2026-11-12/INDEPENDENT_REVIEW_v1.8.md`).
- Image 1.9 closed V18-F1/N1, but its review did not pass (V19-F1: plain `reset`,
  `bootsel` and `cfg save` trusted the FSM's Pi flag, which is false for 0.1 s with a late
  Pi powered; `captures/review_2026-11-12/v1_9/INDEPENDENT_REVIEW_v1.9.md`).
- Image 1.10 fixed those guards at command entry, but its review did not pass (V110-F1:
  `reset`/`bootsel` waited 50 ms after their check and rebooted without checking again;
  `captures/review_2026-11-12/v1_10/INDEPENDENT_REVIEW_v1.10.md`).
- 1.11 is 1.10 with that fixed (`PROGRESS.md` §6, "Image 1.11"). Its brief is
  `captures/review_2026-11-12/v1_11/REVIEW_BRIEF_v1.11.md`. The commands below name 1.11.

Up to 1.7 every strobe firing was refused with a Pi present, and the
Phase 8 Pi rule (`PROGRESS.md` §8, 2026-11-09) keeps the strobe dry with a Pi seated. This step
fires live current with a Pi seated, so it needs, in order:
1. ✅ independent confirm review PASS for the exact image 1.11 (1.8–1.10 did not pass);
2. ✅ the owner's go for this step (2026-11-12, `PROGRESS.md` §8);
3. a board whose strobe is already proven live, with its 9 A level solved: board 1 (level
   439, session cap 463, CR-23's 1 µF fitted).

**What 1.11 changes** (`PROGRESS.md` §6, "Image 1.8" to "Image 1.11"):
- **A Pi powered is no longer a refusal,** dry or live.
- **Live mode does not arm the watchdog with a Pi powered** (owner, 2026-11-12).
  - On this board a watchdog reset is a hard power cut to the Pi.
  - U5 (≤ 137 µs per pulse) and the PIO (every burst bounded) already stop a hang from holding
    current on.
- **A live session is bound to the Pi state it was armed in.** A Pi that appears or goes away
  ends it with the gate at 0: `a Pi appeared or went away since live mode was armed`.
- **"A Pi" is observed fresh (1.9):** its 3V3 sensed now and the power FSM's
  classification together.
  - While they disagree, `strobe` shows `pi UNSETTLED (its 3V3 and the power state
    disagree)`.
  - Live mode will not arm then. An idle live session ends at its next service pass:
    `the Pi's 3V3 and the power state disagree`. During an already admitted bounded
    firing/readback this check is deferred; subsequent gate/fire admission is fresh.
  - Persistent lost 3V3 keeps the pair UNSETTLED until recovery or an `off`/power
    sequence. If the sense recovers, explicit re-arm may succeed after gate settling.
    Bounded conduction is not a guarantee of uninterrupted Pi power after a CPU hang
    if a no-Pi-owned watchdog was already armed when the Pi appeared.
- **`reset`, `bootsel` and `cfg save` (1.10, 1.11):** a Pi's 3V3 sensed counts as a Pi
  before the power FSM has classified it. `reset` and `bootsel` also refuse while the board
  is POWERING_ON, and check again after their 50 ms output wait, immediately before the
  reboot (V110-F1). What remains is the instant before a `reset`, and the boot ROM's own
  10 ms inside `bootsel`. Waiting for `stat` to show the Pi is still the rule.

**What this step measures:** the +5 V rail under 9 A pulses, with a Pi on it.
- Without a Pi, board 1 dipped ~0.30–0.33 V for ~0.28 ms per boost refill, to ≈ 4.85 V
  (`PROGRESS.md` §6, 2026-10-16).
- The Pi 5 flags under-voltage at ~4.63 V, and its own load lowers the starting level.
- Pulses of 50 µs and longer put the VIR boost into hiccup for ~125 ms (CR-25), so watch what
  the restart draws from +5 V.

| | |
|---|---|
| Board | board 1, on 1.11 after Step 1; the Pi seated; SW2 taped over; the Pi's own USB-C unplugged; J4 empty; beam off. **J3 connected from Step 2** |
| PSU | 5.20 V at J1 (DMM), limit **≥ 5 A** |
| Scope | **CH1 on J7.5**, the +5 V rail the Pi runs from, by a jumper lead into J7 (J8 is under the Pi); its ground on J7.4. 50 mV/div, DC with a 5.0 V offset (or AC-coupled), 100 µs/div; trigger on CH1 falling through ~5.05 V, single sequence. CH2 on TP4 (135 mV/A) if board 1's Phase 6 lead S2 is still fitted. Leads on with the PSU and USB off |
| Or the logic analyser | its analog inputs instead of the scope (Saleae Logic Pro 8, 50 MS/s, ~5 MHz). Ample: the pulses are 20–100 µs with edges of microseconds, and 9 A is 1.215 V at TP4 (~5 mV, i.e. ~36 mA, per step on the ±10 V range). TP4 on lead S2, J7.5 on its jumper lead, grounds on the R66 GND-pad lead (G1/G2), never TP1 (boost pickup) or TP5. Start a few-second capture, then send the shot. **The analyser is not a calibrated voltmeter:** read the steady rail with the DMM on the same J7 lead before the first shot, and judge the 4.75 V line against that |
| Thermal | FLIR on Q9 and HS1 throughout the live steps (HANDOFF §3 rule 9) |
| Pi | card backed up; a second SSH session for the load |
| USB | the PC on the board's CLI, logging to `captures/` |

1. **Reflash first, by an update from the Pi**, with J3 still disconnected (as §8.9b).
   - PC, in `Hardware\firmware`: `scp build_v1.11\pitrac.bin pitrac2.local:pitrac_v1.11.bin`.
   - Pi: `pitrac-link update pitrac_v1.11.bin`. Expect:
     - `image    : pitrac_v1.11.bin, 243444 B, version 1.11, try-before-you-buy, SHA-256
       883d4efa56184782`;
     - `board    : version 1.7 (built Nov 11 2026 16:41:14) at 0x100000; installs into
       0x002000`;
     - no `NOTE`.

     Answer `y` only if the SHA-256 matches.
   - When the Pi is back:
     - `id` → `version : 1.11 (partition A at 0x002000)`, `built : Nov 12 2026 15:42:51`;
     - `upd` → `this image in partition A` and `partner  : version 1.7, bought: the
       fallback`.
   - `off`. Wait for STANDBY, and `hist` → `flash : up to date`.
   - **`reset` and `bootsel` with the watchdog armed.** 1.11 disarms it just before the
     reboot, no longer 50 ms earlier. Still in STANDBY: the latch open, the Pi off.
     - `wdog on`, then `reset` → `resetting...`. The CLI comes back within a few seconds;
       `wdog` → `watchdog : off`.
     - `wdog on`, then `bootsel` → `rebooting to BOOTSEL...`. On the PC, `picotool info`
       finds the board in BOOTSEL mode, and still does 5 s later. Then `picotool reboot`,
       and `id` → `version : 1.11`.
     - Stop if either command does nothing, or the board leaves BOOTSEL by itself.
   - The resets were counted: wait 10 s in STANDBY, `hist` → `flash : up to date`. Then PSU
     and USB off.
2. **Set up**, with PSU and USB off: connect J3, and attach the scope leads.
   - PSU on (≥ 5 A), then USB.
   - `on`; wait for `RUNNING` and the Pi's login. `stat` → `pi       : present 1`.
   - Note `hist`'s counts.
   - On the Pi: `vcgencmd get_throttled` → `throttled=0x0`.
3. **Live, with the Pi idle.**
   - `strobe live on`, and read the checklist. Then `strobe live on confirm` → `LIVE: ARMED.
     ... Watchdog NOT armed: a Pi is powered, and a reset would cut its power.`
   - `strobe live` → `watchdog : off: not armed with a Pi powered (a reset would cut its
     power)`.
   - `strobe cal 9` → `SOLVED: level N ... for 9.00 A` and `confirmation mean ... -- PASS`.
     Board 1 solved 439 twice. A level far from that is a finding: stop.
   - At the solved level, one at a time, with the scope re-armed for each: `strobe pulse 20`,
     `strobe pulse 100`, `strobe sched 50 fire`, `strobe sched 10 fire`. Record CH1's minimum,
     and how long it stays under 5.0 V.
4. **With the Pi under load.**
   - Second SSH session: `for i in 1 2 3 4; do (yes > /dev/null &); done`. Four busy cores.
   - `strobe` → `live     : *** ARMED *** for N s; idle exit in M s`. Live mode ends itself
     after 5 minutes without a strobe command, and setting up the load can take that long. If it has ended, the next shot
     prints `*** LIVE MODE ENDED: idle timeout ***` and fires DRY, at gate 0 (no current):
     `strobe live on confirm` and `strobe cal 9` again first. (Board 1, 2026-11-12: the first
     loaded run fired dry this way and was repeated.)
   - Repeat Step 3's four shots, then `pkill yes`.
5. **After**, straight after the last loaded shots, in this order:
   - Pi: `vcgencmd get_throttled` → `throttled=0x0`; `dmesg | grep -i -E "voltage|brown"` →
     nothing new.
   - Pi: `for i in $(seq 1000); do pitrac-link ping >/dev/null || echo FAIL; done` prints
     nothing.
   - Board, after the pings: `stat` → no fault, `pi : present 1`. `hist` → `power cuts to a
     Pi` and `Pi went down unrequested` as in Step 2. `link` → `frames   : N good, 0 CRC
     errors`, N at least 1000. (Until 2026-11-12 this step put `link` before the pings, so it
     could not count them.)
6. **Leave.**
   - `strobe live off` → `live OFF: gate 0, engine stopped, GPIO25 low. Watchdog now off.`
   - `off`. Wait for STANDBY, and `hist` → `flash : up to date`.
   - PSU and USB off; disconnect J3.

**Pass:**
- CH1 stays at or above **4.75 V** through every dip;
- `throttled=0x0` before and after;
- no board fault, and the Pi up throughout;
- live mode ended only by `strobe live off`, or its idle timeout;
- the 1000 pings clean.

**Stop if** CH1 goes under 4.75 V, a throttled bit appears, a fault latches or the Pi goes down.
- Send `strobe live off`, then `off`, and keep the scope capture.
- It is a +5 V budget finding (CR-25), not something to fix by raising a limit.

---

## Exit criteria for Phase 8

- [ ] 8.0 gate fully ticked before the Pi was seated
- [x] **Q7 closed** — overlay params verified, 200 ms pulse confirmed against a real Pi (board 1, 2026-11-09, 8.1)
- [x] **Q4 closed** — header 3V3 behaviour at halt recorded, primary indicator identified (2026-11-09: the 3V3 stays up; RPI5_ON is the indicator)
- [x] **Q11 closed** — +5V→3V3 latency measured, `PI_DETECT_WINDOW_MS` set from it (5.56 ms, 2026-11-09; 3000 ms kept)
- [x] **Reset guard verified** — `reset` and `bootsel` both refuse while the Pi is powered (board 1, 2026-11-09, 8.4)
- [x] Shutdown duration measured; `PI_SHUTDOWN_MIN_HOLDOFF_MS` retuned to ~2× (measured ≤ 2.5 s, 2026-11-09; the owner chose 10 s, ~4×, from image 1.3; 10.03–10.04 s on 1.7, board 1, 2026-11-12, 8.9b)
- [x] 20 clean power cycles, no filesystem errors (board 1, 2026-11-09, 8.5)
- [x] Halt-telemetry design decided and recorded (8.6; written 2026-10-19, `PROGRESS.md` §6)
- [x] §8.0b PASS on the image used with the Pi, and the real-Pi rows of 8.6 checked (board 1: 2026-11-02, 2026-11-09)
- [ ] §8.7 PASS on every board that will carry a Pi (partition table, trial, buy, rollback)
- [x] §8.8 and §8.9 PASS: the serial link, an update from the Pi, and a rollback with a Pi (board 1, 2026-11-10)
- [x] §8.10: the strobe with a Pi present, on image 1.11 (software confirm PASS; owner-approved; 1.8–1.10 rejected). **PASS on board 1, 2026-11-12: the +5 V rail's true lowest ≈ 4.94 V (idle) / 4.96 V (loaded) under 9 A pulses; `throttled=0x0`; no fault, cut or halt** (`PROGRESS.md` §6)

Record everything in `PROGRESS.md` §6.
