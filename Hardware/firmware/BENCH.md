# Bench Procedures — Phases 0 through 1c

Covers **Phase 0** (toolchain/CLI), **0.5** (rails), **1** (latch), **1b** (Pi
shutdown), **1c** (panel), plus a thermal-imaging section at the end.
Later phases have their own documents — see `PROGRESS.md` for the index.

Step-by-step, with the exact CLI commands. **Record every measurement in
`PROGRESS.md` §6 as you go.**

**Standing rules, all phases:**
- **No Pi 5 seated on J8 until Phase 7c.** (Phase 1b, the firmware gate for this, ✅ passed
  2026-07-31.)
- **An RP2354 reset is a hard power cut to the Pi, not a reboot** — the pads reset, GPIO15
  goes high-Z, R12 pulls the latch open. `reset`/`bootsel` are guarded in firmware; **SW2
  is not.** Tape over it once a Pi is seated. See `BENCH_P8_PI.md` §8.0.
- **`PULSE_LIMIT_DISABLE` (GPIO27) stays 0.** It defeats the strobe hardware
  watchdog. There is deliberately no CLI path to it.

**Status: every phase in this document — 0, 0.5, 1, 1b and 1c — is ✅ complete on the first
board (2026-07-31).** Results are recorded inline below. The procedures stay here as-written
so a second board can be brought up the same way. **Next: `BENCH_P2_BEAM.md`.**

**LED reference** (used throughout):

| On-board D5/D6 — always-on +3V3, work in standby | Meaning |
|---|---|
| yellow slow blink | STANDBY, healthy |
| yellow solid | rails up |
| yellow fast blink | shutting down |
| red fast blink | fault latched (read the code with `stat`) |

---

## Phase 0 — prove the toolchain and the CLI

**Power:** USB-C only. **No J1, no bench supply. J2 jumper off. No Pi.**

1. Build (`SETUP.md`), then flash: hold SW1, tap SW2, release SW1 → a `RPI-RP2`
   drive appears → drag `build/pitrac.uf2` onto it. The drive vanishing is success.
2. Open the COM port (Device Manager → Ports). You should get the help banner and
   a `> ` prompt after pressing Enter.
3. `id` — expect `sysclk : 150000000`.
4. **`picotool info -a` on the host, with the board plugged in.** Note this reads
   the **connected board**, not the `.uf2` file — if you pass a filename you get
   the file's build info and *no* `Device Information` section. **Record the die
   revision** from that section.
5. `stat` — expect `STANDBY`, `fault none`, and `5V_IN` in the range
   **4.6–4.85 V**. USB VBUS legitimately varies by port; both ends of that range
   have been seen on the same board. Anything ≥5.0 V means J1 is powered.
6. `adc5v` — **must say "USB-only, latch INHIBITED"**. If it says "permitted",
   stop and work out why before anything else; this guard is what protects a Pi.
7. `capture 0x02 1000 100000` — 1000 samples of the +5V_IN monitor. Expect a flat
   run of near-identical codes. This proves the ADC + DMA + CSV path end to end.
8. Host side: `python tools\scope.py --port COM7 --channel 1 --samples 1000 --volts`
   (substitute your COM number).

**Exit criteria:** LEDs blink · `id` responds · `adc5v` reports INHIBITED ·
`capture` returns a flat trace · die revision recorded.

### 0.1 The erratum E9 test — free, 10 seconds, do it before Phase 1

**✅ Result on the first board: all four read 0. Silicon is revision A4. No external
pull-downs needed.** Re-run this on any new board — the procedure is below.

```
pins
```
With **nothing** connected to J4 and **no Pi** on J8, all four of these must read **0**:

```
PI_3V3_SENSE(24)   0
RPI5_ON(0)         0
CAM_STROBE_0(8)    0
CAM_STROBE_1(9)    0
```

**If any reads 1, that board has erratum E9** — a floating input whose pull-down is
too weak to hold it down against pad leakage. Record which pins in `PROGRESS.md` §6
and read the consequences below before going further.

Why each is at risk:
- GPIO0/8/9 use the **internal** pull-down (~50–80 kΩ), the exact configuration E9 describes.
- GPIO24 has no internal pull — it relies on **R44 = 100 kΩ** to GND. The documented
  E9 workaround is an external pull-down of **≤8.2 kΩ**, so 100 kΩ is over 10× too weak.

**Consequences if affected:**
| Pin reads high | Breaks |
|---|---|
| GPIO24 | Phase 1 goes to `PI_BOOTING` instead of `BENCH_RUNNING`, then faults at 90 s |
| GPIO0 | Phase 1b "Pi is down" detection — shutdown may never complete |
| GPIO8/9 | Phase 7 camera handshake fires without a camera |

**Fix:** fit an external pull-down of 4.7–8.2 kΩ to GND on the affected net.
For GPIO24 that means paralleling R44. Do this before Phase 1 if GPIO24 is affected;
the others can wait until their phase.

*(This is why step 4 records the die revision — but the `pins` reading is the
definitive answer for your board, regardless of what the errata list says.)*

### 0.2 Optional: SWD flashing

`tools/flash_swd.sh` from a Pi 5, flying leads only. Not required to proceed, but
the BOOTSEL button dance gets old around the twentieth cycle. Come back to it
whenever reflashing starts to annoy you.

Quicker win in the meantime: **`bootsel` at the CLI** reboots straight into flash
mode with no button press. Works whenever the firmware is running.

---

## Phase 0.5 — dead-board rail smoke test

**Do this before any firmware writes GPIO15.** This is the first time the board
sees real power. All measurements are DMM/scope — the CLI plays no part.

**Two things to know before you start:**

1. **J2 bypasses the firmware entirely.** It shorts the latch gate to GND, so the
   +5 V rail comes up regardless of what GPIO15 is doing. The firmware still
   believes the latch is open — if USB happens to be connected, `stat` will
   report `latch: 0` and `railsready 0` while +5 V is live on the board. That
   disagreement is expected in this phase and only in this phase.
2. **Don't press the power button during 0.5.** It would drive GPIO15 high on top
   of the J2 short. Harmless, but it muddies what you're measuring.

| Step | Setup | Expect |
|---|---|---|
| a | USB-C only, J1 unpowered, **J2 off** | +3V3 = **3.30 V**. Draw < 100 mA. TP2 = **0 V**. TP6/TP7 ≈ **0 V — up to a few hundred mV is normal**, see below. |
| b | Unplug USB. PSU **5.2 V, limit 0.3 A** into J1. J2 off. | Only the +3V3 domain draws. Verify +5V is genuinely **0 V at J8.2**. Yellow LED still blinks — the MCU runs on +3V3 with no USB. |
| c | Raise limit to **2 A**. Fit **J2** (forces latch on). | +5V = 5.2 V · VIR = **36 V** at J3 (open circuit is fine) · TP2 = **12 V** (11.4–12.7) · TP6/TP7/TP9/TP10 all = **+5VA / 2**, i.e. **≈2.59 V** on a 5.2 V rail — see note |
| d | Scope the LM5157 SW node | Decide the R11/C9 snubber (DNP by default — fit only if it rings) |
| e | Remove J2 | +5V drops, board returns to standby cleanly |

Soft-start takes ~86 ms and there is ~670 µF of VIR bulk, so a 1 A limit may
trip on inrush. Hence 2 A at step c.

*(+1V1, the RP2354 core rail, has no test point and only 0402-scale access on
C31/C33/L2. Skip it unless something else looks wrong — if the MCU is running
and the LED blinks, the internal regulator is fine.)*

> **The virtual ground is not 2.50 V — ignore the .md on this.** R75/R76 = 10K/10K
> buffered by U11C makes +2V5 literally **+5VA ÷ 2**. There is no 2.5 V regulator.
> On the design's 5.2 V rail that is **2.59 V**, and TP6/TP7/TP9/TP10 should all
> agree with each other to within a few mV.
>
> **All four matching is the pass criterion, not the absolute number.** It means
> the DC servo, the demodulator and both LPF stages are each sitting at virtual
> ground. Divergence is the interesting signal: TP7 far from the others means the
> servo is fighting ambient light or the photodiode bias is wrong; TP9 ≠ TP10
> isolates an LPF stage fault.
>
> Consequence recorded as **Q8** in `PROGRESS.md`: because the reference tracks the
> rail, any *step* on +5V while the baseline HPF is frozen gets amplified ×14.5 at
> the comparator. Revisit in Phase 3.

**Gate:** every rail reads its §8.1 quiescent value with J2 fitted, and 0 V with
J2 removed. Only now does firmware get to touch the latch.

---

## Phase 1 — power button and latch

**Power:** PSU 5.2 V / 2 A into J1. **J2 removed. No Pi.**

If the Adafruit 481 panel button isn't wired yet, test by shorting
**J7.3 → J7.4 (GND)** with a jumper.

> **The MCU is already running before you press anything — that is by design.**
> Apply power to J1 and the yellow LED starts its slow standby blink immediately,
> with no button press. This surprises people; it is correct.
>
> The board has two power domains. **+5V_IN → NCP1117 → +3V3 is always on** and
> feeds the RP2354, the one-shot watchdogs, and the mic front-end — so the MCU is
> alive whenever J1 *or* USB-C has power. Something has to be listening for the
> button press, and that something is the MCU.
>
> **The +5 V rail is what the button switches**, via Q3: the Pi 5, the 36 V boost,
> the beam LED, and the entire analog chain. That rail is still dark in STANDBY.
>
> So "did the button work?" is answered by +5 V at J8.2 and VIR at J3 — never by
> whether the MCU is running.
>
> **LED code:** yellow slow blink (100 ms / 2 s) = STANDBY healthy · yellow solid =
> rails up · yellow fast = shutting down · red fast = fault latched.

### 1.1 Calibrate the discriminator first

This decides whether a Pi 5 gets powered. On this board the two supplies are
**4.85 V (USB-only)** and **5.20 V (bench/Meanwell)**, so at the ADC pin you are
separating 2.43 V from 2.60 V. `V5_MIN_FOR_LATCH` sits between them at 5.05 V.
Worth calibrating properly.

**Setup:** USB **and** the 5.2 V bench supply on J1, both connected. **No J2 jumper,
and do not press the power button** — the +5 V rail stays off for this; we are only
reading +5V_IN, which is always-on.

With both connected the PSU wins: USB delivers 4.85 V through D8 against the PSU's
5.2 V, so D8 is reverse-biased and +5V_IN follows the PSU.

**Where to put the DMM:** +5V_IN has no test point. Measure at the **J1 screw
terminal**. Strictly +5V_IN is after Q1, but Q1 is ≤3 mΩ and at standby current
that is a ~0.1 mV drop — ignore it.

```
adc 1 256              # sanity check only — see below
adc5vcal 5.203         # <-- what YOUR DMM reads at J1, not this number
adc5v                  # should now agree with the DMM
```

**What the `adc 1 256` reading is for: nothing computational.** `adc5vcal` takes its
own 256-sample reading internally and ignores anything you ran before it. The point
of running it first is to catch a bad calibration *before* baking it in.

**Expect roughly `ch1 = 3036 (2.4466 V at pin)` — NOT 2.60 V.** The raw read path is
about 5.9% low, and that is normal on this board:

> **Why it reads low (Q9).** R46/R47 = 100K/100K presents a **50 kΩ** source
> impedance to an ADC that wants **≤10 kΩ**. The sample-and-hold cap cannot fully
> charge through that in the sampling window, and a few µA of input leakage across
> 50 kΩ is hundreds of mV on its own. Measured: a true 5.200 V input reads back as
> 4.893 V. This is a property of the divider design, so expect it on any board built
> from these files.
>
> The firmware compensates with a **1.063 default scale**, so `adc5v` and `stat`
> already report correctly before you calibrate anything. `adc5vcal` trims the
> residual per-unit error on top.
>
> This default is load-bearing, not cosmetic: with a 1.0 scale the firmware reads a
> good 5.2 V supply as 4.89 V — below `V5_MIN_FOR_LATCH` — and refuses to latch at all.

So: **2.4 V region → normal, proceed.** 0.1 V or 3.2 V → something is genuinely wrong
with the divider or the connection; stop rather than calibrate. Record the number in
`PROGRESS.md` §6 as the "before" value.

**Optional, one probe, worth knowing:** DMM the **R46/R47 junction** (same node as
GPIO41). 2.60 V there means the divider is fine and the ADC reads low; ~2.45 V means
input leakage is actually dragging the divider down. Either way `adc5vcal` fixes it.

> **⚠ The scale is RAM-only — any reset loses it.** That matters for the test
> order below: test 1 needs USB-only power, but you just calibrated on the PSU.
> **Remove power from J1 while leaving the USB cable connected.** The board keeps
> running on USB through D8, so the MCU never resets and the calibration
> survives. If you unplug USB instead, you reset and have to recalibrate.
>
> **Why it isn't in flash yet, deliberately:** the latch guard is designed to work
> *uncalibrated*. `V5_MIN_FOR_LATCH` = 5.05 V sits ~170 mV from both measured
> supplies (4.85 / 5.20 V) while the divider tolerance is only ~±50 mV, so a cold
> boot still discriminates correctly. The calibration improves reported accuracy;
> it is not load-bearing for safety.
>
> Flash persistence lands in **Phase 3**, when there is more than one value worth
> storing — `demod_phase_ticks` especially, since nobody wants to re-run a 64-point
> phase sweep every boot. One versioned, CRC'd config block, written once and
> properly: flash writes on RP2350 must execute from RAM with interrupts off and
> core 1 parked, because the chip runs XIP from that same flash. Worth doing once,
> not worth doing for a single float now.

### 1.2 The four tests — all must pass

| # | Test | Method | Pass |
|---|---|---|---|
| 1 | **Refuses to latch on USB** | Drop J1 power, keep USB connected (see warning above). Press the button (or `on`). | Red LED fast-blink; `stat` shows `FAULT` / `USB_POWER_ONLY`; **+5V stays 0 V at J8.2**; VIR stays 0 V. |
| 2 | **Latches on real power** | Restore PSU 5.2 V. Clear the fault (below), then press. | +5V up within 250 ms; VIR reaches 36 V; TP2 = 12 V; `stat` shows `BENCH_RUNNING` (no Pi → bench branch). |
| 3 | **Unlatches** | Press again. | +5V → 0 V; VIR decays; CLI stays responsive over USB (we live on +3V3). |
| 4 | **Fails safe through reset** | While latched, press **SW2 (RUN)**. | **+5V must drop and stay down.** Scope GPIO15 *and* +5V through the reset — you are looking for the absence of any glitch high. |

### Test 5 — supply pulled *while* latched (added 2026-07-30 after a bench find)

The first four tests all check the guard at the moment of latching. This one checks
what happens when the supply disappears **afterwards**.

| Method | Pass |
|---|---|
| USB **and** PSU connected. `on` to latch. Then **unplug the PSU**, leaving USB. | Within ~0.6 s the latch drops: +5 V → 0 V at J8.2, VIR decays, `stat` shows `STANDBY` / `fault SUPPLY_LOST`. |

**Why this matters more than it looks.** Before the fix the latch just stayed closed, and
the +5 V rail — Pi, boost, beam LED, analog — was quietly back-fed from USB VBUS through
D8, an SS14 rated **1 A**. With no Pi that looks completely stable, which is what makes it
dangerous: with a Pi 5 on the header the same sequence puts a multi-amp load through a 1 A
diode. Brownout, SD corruption, and a cooked D8.

Detection is a sustained-low test (below 4.90 V for 500 ms), not a bare threshold, because
from Phase 6 the rail is *expected* to dip during strobe bursts — the boost UVLO is
bracketed at 4.74/4.52 V for exactly that reason. Burst sag is milliseconds; a removed
supply is permanent.

**Clearing the latched fault after test 1:** the FSM sits in `FAULT` until you
acknowledge it. Either press the button once (clears the fault and returns to
`STANDBY`) or type `fault clear`. `stat` should then show `STANDBY` / `fault none`.

Test 4 is the important one. R12 pulls GPIO15 low through reset and
`safe_state_init()` sets the level before enabling the driver, but prove it
rather than trusting it.

Also record standby current (+3V3 domain only) with the rail down.

### 1.3 If test 2 shows `PI_BOOTING` instead of `BENCH_RUNNING`

That means `PI_3V3_SENSE` (GPIO24) read high with no Pi attached — go back to the
E9 test in §0.1. R44's 100 kΩ pull-down is over 10× weaker than the documented
≤8.2 kΩ workaround, so this is the most likely pin on the board to be affected.

It will sit in `PI_BOOTING` for 90 s and then latch `PI_BOOT_TIMEOUT` (the rail
stays up — that's deliberate, so a real Pi could still be debugged). Confirm the
diagnosis with `pins`, then fit a 4.7–8.2 kΩ resistor in parallel with R44.

---

## Phase 1b — full FSM + Pi soft-shutdown, **simulated Pi**

**Power:** PSU 5.2 V / 2 A. **Still no Pi.** This is the gate before a real Pi
is ever attached.

### Simulating the Pi

**You need a 3.3 V source.** With no Pi seated, J8.1/J8.17 (the Pi's 3V3 output pins)
are *dead* — they are driven by the Pi, not by this board. Use the board's own
+3.3VA instead, which is brought out on the mic header:

| Source | Pin |
|---|---|
| **+3.3 V** | **J5 pin 2** |
| GND | J5 pin 4 or 6 |

| Signal | Pin | How to assert |
|---|---|---|
| PI_3V3_SENSE (GPIO24) | R45/R44 divider fed from J8.1/17 | jumper **J5.2 → J8.1** |
| RPI5_ON (GPIO0) | J8.15 via R29 1K | jumper **J5.2 → J8.15** |
| RPI5_SHUTDOWN (GPIO43) | J8.37 via R39 1K | output — scope it |

Both inputs are high-impedance (110 kΩ and 1 kΩ into a GPIO), so the total load on
+3.3VA is tens of µA. Deassert by pulling the jumper.

`pisim` prints all three plus the computed `pi_is_down` — use it to confirm each
jumper landed before running the matrix.

Better: a spare Pico running a "fake Pi" — wait for the RPI5_SHUTDOWN assertion,
hold a configurable delay, then drop both inputs. Lets you sweep shutdown
duration from 1 s to never, unattended.

### Confirm the polarity first

Firmware commits to **active-low** (`PI_SHUTDOWN_ACTIVE_LOW 1` in `board.h`),
matching the `gpio-shutdown` overlay default. Check with `pisim` that
RPI5_SHUTDOWN idles **1** (deasserted), then scope J8.37 during a shutdown
request and confirm it pulses **low** for 200 ms.

### ⚠ Before the "reset ≠ shutdown" test — read this or you will misread the scope

**GPIO43 goes low through reset, and that is not a firmware bug.** RP2350 pads reset
with the internal **pull-down enabled** — `PADS_BANK0_GPIO43_RESET` = `0x116`, i.e.
PDE=1, PUE=0, OD=0, IE=0 (SDK 2.3.0 `hardware/regs/pads_bank0.h`). The output driver
is high-Z, but a ~50–80 kΩ pull-down is actively holding the pin down until
`safe_state_init()` executes and drives it high. For an **active-low** signal, that
reset default *is* the asserted level.

So the pass criterion is **not** "no edge appears."

> ⚠ **And it is not the pulse width either.** An earlier revision of this document said
> "a few ms = the pad, 200 ms = a real assertion." **That is wrong.** SW2 holds the chip
> *in* reset, so GPIO43's low lasts as long as your thumb is on the button — the bench
> measurement on 2026-07-31 was **116 ms on the fastest possible press**, and a deliberate
> hold sails past 200 ms with nothing wrong at all. Width measures the operator.

**The correct discriminator is correlation.** Put RUN and GPIO43 on two channels:

| What you see | Means |
|---|---|
| GPIO43 low **only** while RUN is low, plus a short tail after release | ✅ The pad's reset pull-down. Expected. |
| GPIO43 low at any moment while RUN is **high** (chip running) | ❌ **Firmware asserted. This is the fail.** |

**The number worth recording is RUN rising edge → GPIO43 rising edge.** That is the true
"firmware not yet running" window — bootrom + XIP + the handful of instructions before
`safe_state_init()` — and it is independent of how long you held the button. Trigger on
RUN's **rising** edge. Record it in `PROGRESS.md` §6; Phase 8 will want it.

### The part this test cannot see without help (Q10)

With no Pi seated there is **no pull-up anywhere in the circuit**, so the above only
proves the firmware's behaviour, not the Pi's interpretation of it. With a real Pi,
the RP2354's reset pull-down fights `gpio-shutdown`'s ~50 kΩ pull-up through R39's
1 kΩ, and J8.37 lands around **1.7–2.0 V** during the reset window — at or below
RP1's VIH. If RP1 reads that low, **every RP2354 reset requests a Pi halt.**

You can measure it without a Pi. **Any pull-up from ~20 kΩ to 220 kΩ works** — you extract
the unknown and compute the real case, so you do not need to match the Pi's 50 kΩ:

1. Fit a resistor `R` from **J8.37 to +3.3 V (J5.2)**.
2. Scope **J8.37** (not GPIO43 — you want the node the Pi would see) through an SW2 press.
3. Read `V` during the reset window, and `3V3` with GPIO43 driven high.

```
X    = V·R / (3V3 − V)        # R39 + the pad's pull-down, lumped
V_pi = 3V3 · X / (50k + X)    # what a real Pi's ~50k pull-up would see
```

**Judge `V_pi`, not `V`.** A pull-up stronger than 50 kΩ biases the node high and will look
safer than reality. Below ~10 kΩ the arithmetic gets noise-sensitive — `3V3 − V` collapses.

| `V_pi` | Verdict |
|---|---|
| **> 2.3 V** | ✅ Safe. Q10 closes. |
| **2.0–2.3 V** | ⚠ Marginal. Do the rework anyway; it costs one resistor. |
| **< 2.0 V** | ❌ Level fails. Fit the pull-up if convenient — but see the correction below: the latch drop dominates either way. |

### 🟢 Result, 2026-07-31: the level fails, but it barely matters

> ⚠ **Read this before acting on the numbers below.** This section originally called the
> result a blocker. It is not, and the correction is important enough to lead with:
>
> **Every RP2354 reset already opens the +5 V latch.** Net 62 `LATCH_CONTROL` is Q2's gate
> + R12 + GPIO15, so when the pads reset, GPIO15 goes high-Z, R12 pulls the gate low, and
> **the Pi loses power outright** — no shutdown, no sync, no warning. The spurious GPIO43
> assertion arrives at a Pi that is losing its rail in the same instant.
>
> There is no case on this board where GPIO43 goes low but the latch holds: both pads reset
> together, and the Pi has no power source other than the latch. So the pull-up below is
> **defence-in-depth, not a gate.** The hazard worth your attention is the power cut, and
> the mitigation that matters is the `reset`/`bootsel` CLI guard, not a resistor.

The measurement itself, for the record:

Measured with **R = 20 kΩ** (two 10 kΩ in series): `3V3` = **3.246 V**, `V` = **2.07 V**.

```
X    = 2.07 × 20k / (3.246 − 2.07) = 35.2 kΩ    → pad pull-down ≈ 34 kΩ
V_pi = 3.246 × 35.2 / 85.2         = 1.34 V     → FAIL
```

Note the raw reading of 2.07 V looks fine. It is `V_pi` that fails, which is exactly why
the formula is not optional. The pad's pull-down is also **~34 kΩ** — considerably stronger
than the 50–80 kΩ assumed, which is what pushes the result under. **The verdict is robust:**
even at a datasheet-typical 60 kΩ pull-down it would be 1.81 V, still a fail. Do not
re-measure hoping for a better number.

### 🔧 The optional rework

**10 kΩ pull-up from J8.37 to +3V3.** With the measured 35.2 kΩ:

| | Level at J8.37 | Threshold | Margin |
|---|---|---|---|
| Through reset | **2.67 V** | VIH 2.31 V | +0.36 V ✅ |
| Firmware asserting | **0.35 V** | VIL 0.99 V | −0.63 V ✅ |

Correct in both directions. **6.8 kΩ** balances the margins better (+0.51 / −0.52); both work.

**No PCB work needed if you don't want it:** run the resistor between **J8.37 and J8.1**,
both header pins. J8.1 is the Pi's own 3.3 V output, so it parallels the Pi's internal
~50 kΩ pull-up and the arithmetic is identical. It can sit on the underside of the header
or on a Pi-side breakout.

If you do use the board's +3V3 instead, note it must be the **always-on** +3V3, not the
switched +5 V — the entire point is holding the line while the MCU is not running.

**Fit it if convenient. It does not gate Phase 8.** If you do, re-measure both directions,
record in `PROGRESS.md` §6, and verify against a real Pi in `BENCH_P8_PI.md` §8.4.

### Test matrix

| ✓ | Test | Method | Pass |
|---|---|---|---|
| ✅ | Normal boot | Assert PI_3V3_SENSE, then RPI5_ON, then press the button | `POWERING_ON` → `PI_BOOTING` → `RUNNING` |
| ✅ | Pi never boots | Assert PI_3V3_SENSE only | `PI_BOOT_TIMEOUT` at 90 s; **latch stays on** (so you could debug the Pi); CLI responsive |
| ✅ | Normal shutdown | From RUNNING press the button; drop both sim signals after 8 s | Request pulses low for 200 ms; **latch releases at the 15 s hold-off floor, not at 8 s** |
| ✅ | Pi never halts | Press; leave both sim signals asserted | Latch releases at 60 s, `PI_SHUTDOWN_TIMEOUT` logged |
| ✅ | 3V3 stays up | Drop RPI5_ON only, hold PI_3V3_SENSE | Fallback indicator fires; shutdown still completes |
| ✅ | Button during shutdown | Short press mid-`SHUTTING_DOWN` | No re-latch, no second request |
| ✅ | Escape hatch | 5 s hold mid-`SHUTTING_DOWN` | Immediate `FORCE_OFF` |
| ✅ | **Reset ≠ shutdown** | **Two channels: RUN and GPIO43.** SW2 while the "Pi" is up | GPIO43 low **only while RUN is low** — no assertion with the chip running. *Optional extra: `RUN rising → GPIO43 rising` is the boot-window number. Nice to have, no longer load-bearing now that Q10 is demoted.* |
| 🟢 | **Reset ≠ shutdown, Pi's view (Q10)** | Pull-up J8.37 → J5.2, then SW2. Compute `V_pi` | **2026-07-31: `V_pi` = 1.34 V — level fails, impact minor.** Every reset opens the latch anyway, so the Pi loses power regardless. Optional 10 kΩ; not a gate. |

**Added after the FSM changed on 2026-07-31.** Pi detection became a 3 s window with a
debounced late-detect promotion, and `fault clear` became a full acknowledgement. All three
of these are new code on the highest-risk path in the project, so they need their own rows:

| ✓ | Test | Method | Pass |
|---|---|---|---|
| ✅ | **POWERING_ON is visible** | Press with **no** sim jumpers fitted | Ring **breathes for 3 s**, then goes solid (`BENCH_RUNNING`). It used to jump straight to solid in 250 ms. |
| ✅ | **Late Pi detection** | Press with no jumpers; once solid, fit **J5.2 → J8.1** | Promotes `BENCH_RUNNING` → `PI_BOOTING` after ~100 ms; ring changes to the slow breath |
| ✅ | **Late detect is debounced** | Briefly tap the J8.1 jumper on and off | **No** promotion, **no** `NO_PI_DETECTED` fault, rail stays up |
| ✅ | **`fault clear` acknowledges** | Provoke `PI_BOOT_TIMEOUT`, then type `fault clear` | Ring leaves the fault pattern, rail drops, `stat` shows `STANDBY`. Previously the ring kept double-blinking. |

**On the two reset rows in the first table** (this paragraph refers to those, not to the
FSM table immediately above): the first proves the *firmware* never asserts a shutdown
request; the second measures what a real Pi's input *would* see while the firmware is not
yet running, which is only measurable while no Pi is on the header. Both were run on
2026-07-31. The second one's level fails, but the finding that came out of it is that
**every reset opens the +5 V latch anyway** — so the Pi loses power regardless and the
GPIO43 level is a footnote. See the Q10 correction above.

Watch state transitions live with `stat` and `pisim`.

**Be patient — several of these are deliberately slow.** The guard times are real:
the normal-shutdown test takes **≥15 s** after the button press (the hold-off floor,
which is the point of that test), "Pi never halts" runs the full **60 s** timeout, and
"Pi never boots" waits **90 s**. If a test seems hung, check the elapsed time in
`stat` before concluding anything is wrong.

---

## Phase 1c — Panel indicators (J7)

**Prereq:** Phase 1. **Power:** PSU 5.2 V. Requires the panel button wired to J7.

| J7 | Signal | Drive |
|---|---|---|
| 1, 5 | +5V (switched) | LED anodes |
| 2 | PWR_LED− | GPIO11, Q4 sink via **R48 47 Ω** → Adafruit 481 LED ring |
| 3 | PWR_TOGGLE | the button contacts |
| 4 | GND | |
| 6 | RDY_LED− | GPIO12, Q5 sink via **R49 220 Ω** → D7 (BL-BGE1V1) in holder S1 |

> **Both panel LEDs are fed from the SWITCHED +5 V rail.** They are physically dark
> in STANDBY no matter what firmware does — that is wiring, not a bug (.md §17 item 7).
> Pre-latch feedback comes from on-board D5/D6 on the always-on +3V3.

### 1c.1 Ring LED current — ✅ measured, safe

**R48 is only 47 Ω**, which implied the design assumed the Adafruit 481's ring had its
own internal current limiting. **Confirmed on the bench 2026-07-30:**

| | |
|---|---|
| Voltage across R48 | **0.365 V** |
| Ring current | 0.365 / 47 = **7.8 mA** |
| Ring forward drop | 5.2 − 0.365 ≈ **4.84 V** |
| R48 dissipation | 2.8 mW (nothing) |

The ring **is** internally limited — a bare LED at 4.84 V through 47 Ω would draw far
more. **Safe at 100 % duty indefinitely. No firmware cap, no thermal check needed.**

To re-measure on another build: `on`, then `panel pwr 100`, and put the DMM across R48.
If a different button ever draws materially more, don't rework the resistor — cap the
duty in firmware (`panel pwr 40`); the patterns in `panel.c` scale linearly.

### 1c.2 Function test

```
on                    # rail up — panel LEDs cannot work otherwise
panel test            # ramps both 0->100->0, then returns to automatic
panel pwr 50          # fixed brightness
panel rdy 100         # the separate ready indicator D7
panel pwr auto        # hand back to the state machine
```

Then watch the automatic patterns through a full power cycle:

| State | Power ring |
|---|---|
| STANDBY | dark (rail open — unavoidable) |
| POWERING_ON | fast breath, 600 ms |
| PI_BOOTING | slow breath, 2 s — "waiting on the Pi" |
| RUNNING / BENCH_RUNNING | solid |
| SHUTTING_DOWN | fast blink |
| FAULT | double-blink, distinct at a glance |

The breath patterns are the useful part: from across a room you can tell "booting" from
"ready" without a terminal.

### 1c.3 Testing every ring pattern without a Pi

The patterns can be driven directly, which is the only way to see most of them before
Phase 1b:

```
on                      # rail up first — the panel LEDs cannot work otherwise
panel demo              # plays all six in turn, ~4 s each, with labels (~24 s total)

panel pattern booting   # or: off powering booting running shutdown fault
panel pattern auto      # release, back to following the FSM
panel pattern           # shows what is forced, and what the FSM would be doing
```

`panel demo` is the quick one — it walks the whole set and prints what each is meant to
convey, so you can check the breath rates and the fault double-blink read the way you
want them to. Adjust the periods in `pattern_pct()` in `panel.c` if they don't.

Forcing a pattern releases any fixed brightness set with `panel pwr <n>`, since the two
would otherwise fight.

### 1c.4 Why the automatic patterns only show "dark" and "solid" in bench mode

Not a bug — **those are the only states reachable without a Pi.** With no Pi present the
FSM runs:

```
STANDBY --press--> POWERING_ON --250 ms--> BENCH_RUNNING --press--> FORCE_OFF --> STANDBY
```

- ~~**POWERING_ON lasts 250 ms**, so you see well under half a breath cycle.~~
  **Changed 2026-07-31.** POWERING_ON now lasts up to `PI_DETECT_WINDOW_MS` (**3 s**) while
  it looks for a Pi, so the fast breath is clearly visible — about five full cycles — before
  it drops to `BENCH_RUNNING` and goes solid. Automatic patterns are also phase-aligned to
  state entry now, so the breath always starts from zero instead of catching the waveform
  at an arbitrary point.
- **PI_BOOTING is never entered**, because it requires PI_3V3_SENSE asserted.
- **SHUTTING_DOWN is never entered.** It is reachable only from `PS_RUNNING`, which needs
  a Pi. In `BENCH_RUNNING` a press goes straight to FORCE_OFF.
- **FAULT is reachable** (press on USB-only power) — but that fault leaves the rail *open*,
  so the panel LED is dark regardless. On-board red D6 covers that case.

**You'll see all of them during Phase 1b.** In this order, it's a complete tour:

| Step | Shows |
|---|---|
| No jumpers, press | **POWERING_ON fast breath** for 3 s, then solid `BENCH_RUNNING`. Press again to drop the rail. |
| Jumper **J5.2 → J8.1** only (PI_3V3_SENSE), then press | **PI_BOOTING slow breath**, held for a full 90 s |
| Let it time out | **FAULT double-blink** — and `PI_BOOT_TIMEOUT` deliberately leaves the rail UP, so the panel LED can actually show it |
| Add **J5.2 → J8.15** (RPI5_ON), then clear the fault | `fault clear` (or a press) — **both drop the rail and return to STANDBY** |
| Press to power up | **POWERING_ON** briefly, then straight to **RUNNING, solid** |
| Press again | **SHUTTING_DOWN fast blink** for the full 15 s hold-off |

> **That is two presses, not one.** An earlier revision of this table said "clear fault,
> press → straight to RUNNING." It is not: acknowledging a fault always exits via
> `FORCE_OFF` → `STANDBY` with the rail down, so one press (or `fault clear`) acknowledges,
> and a *second* press powers back up.

---

## Thermal imaging — where the FLIR earns its keep

Four specific points, rather than ad hoc.

**Technique matters more than the camera: emissivity.** Solder mask and black plastic
read accurately (ε ≈ 0.9). **Bare copper, tinned pads and metal-can parts read falsely
cool** (ε ≈ 0.1–0.3) — a 100 °C metal tab can image as 40 °C. Put a dot of matte black
paint, electrical tape, or matte Kapton on any shiny part you care about. Trends and A/B
comparisons are far more trustworthy than absolute numbers.

| When | Target | Looking for |
|---|---|---|
| **Now (Phase 1)** | Whole board, rails up, no load | **Baseline image.** Capture it while everything is known-good, so later anomalies are obvious by comparison. Two minutes, and you only get one chance to record "healthy". |
| ~~Phase 1c~~ | ~~Button LED ring~~ | ✅ Not needed — measured at 7.8 mA, internally limited (§1c.1) |
| **Phase 2b** ⚠ | **R73 / R74 ballast (0R27, 3 W each) and D11** | At 30 % duty the pair dissipates ~0.73 W each and D11 ~3.15 W average. Watch live while stepping duty 2 → 30 % instead of poking with a finger. **Stop the ramp if anything climbs fast.** |
| **Phase 6c** ⚠⚠ | **Q9 (IRLR2905) + HS1 heatsink** | Highest-risk thermal part on the board — a MOSFET deliberately run in **linear mode** at 9 A. Image after every burst sequence during the current ramp. Also confirms HS1 is coupling: a hot Q9 with a cold heatsink means TIM1 isn't doing its job, which no electrical measurement will tell you. |

Secondary targets if something looks off: U2 (NCP1117 — only ~60 mW at standby, should be
barely warm), L1 and D2 under strobe load, and Q3 (SI7137DP latch, ~3 mΩ, negligible until
a Pi draws real current).

---

## Quick CLI reference

```
help                        command list
id                          firmware + chip identity, sysclk
stat                        power state, 5V_IN, fault, Pi signals
pins                        every signal pin at once
pisim                       simulated-Pi inputs + pi_is_down

adc <ch> [n]                oversampled read; ch 0,1,2,5,7 only
adc5v                       +5V_IN volts + latch permit/inhibit verdict
adc5vcal <dmm_volts>        trim the +5V_IN scale
capture <mask> <n> <rate>   block capture -> CSV (the bench instrument)
adcmode off|idle|armed|burst

on / off / forceoff         power requests (on obeys the USB guard)
gpio <n> [0|1]              read/drive (writes are allowlisted)
led r|y <0|1>               on-board D6/D5 — work in standby (+3V3)
panel pwr|rdy <0-100|auto>  J7 panel LEDs — need the +5V rail latched
panel test                  ramp both 0->100->0 for a current/thermal check
panel pattern <p|auto>      force a ring pattern (off powering booting running
                            shutdown fault) — testable without a Pi
panel demo                  play all six patterns in turn, ~4 s each
fault [clear]
reset / bootsel
```

Channel numbers: `0` strobe current · `1` +5V_IN/2 · `2` TIA raw · `5` detect ·
`7` mic. Channels 3 and 4 are digital outputs on this board (RPI5_SHUTDOWN and
Threshold_PWM) and the firmware rejects them.
