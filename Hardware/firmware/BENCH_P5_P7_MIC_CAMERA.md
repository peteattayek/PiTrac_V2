# Phases 5 & 7 — Microphone and cameras

Two independent phases sharing a document because neither is large.

---

> ## 🔴 FIRMWARE STATUS, audited 2026-08-25
>
> | | Exists today? |
> |---|---|
> | **Phase 5 bring-up** (`adc 7`, `capture 0x80`, `tools/scope.py`) | ✅ **yes — runnable now** |
> | Mic onset detection (high-pass, energy window, trigger) | ❌ not written |
> | Mic-as-veto logic in the firing path | ❌ not written |
> | **Phase 7 handshake** (`wait_both`, `t_cam`, FIRING FSM, `CAM_TIMEOUT` fault) | ❌ not written |
> | PIO camera handshake (A3) | ❌ not written; `src/` has only `detect.pio` |
> | I²S digital mic (J5) | ❌ not written, and deliberately deferred |
>
> **Phase 5's bring-up section is the one part of this document you can run today**, and it
> needs nothing but USB power. Everything else is an acceptance procedure for firmware that
> has to be written first. If a command below is not in `help`, it does not exist yet.

## How to read the procedures below

Every procedure opens with a **board state** table. Set the board to exactly that state before
running the commands.

⚠ **`off` now also turns the beam off** (fixed 2026-08-24). On older builds `s_on` survived a
rail-down and the next `on` relit D11 unasked.

---

# Phase 5 — Impact microphone

**Prereq: none beyond Phase 0.** The mic front-end runs on the **always-on +3.3VA rail**,
so this phase needs no bench supply, no latch, and no Pi — **USB power alone**.

**Pull it forward whenever you are blocked on something else.** It is the one piece of
real signal work that can be done at a desk.

## The front-end

```
U16 CMM-2718AT MEMS mic → C84 2.2 nF → R106 30 kΩ → U17 LMV321 inverting amp
                                       (R107 200 kΩ ∥ C86 33 pF feedback)
                                    → AnalogMic_ADC, GPIO47 / ADC7
```

Measured against the design intent — these agree to three digits, which is a good sign
the analog section is as drawn:

| | Value |
|---|---|
| High-pass corner | 1/(2π · 30k · 2.2n) = **2.41 kHz** |
| Low-pass corner | 1/(2π · 200k · 33p) = **24.1 kHz** |
| Gain | 200k/30k = **6.67** (inverting) |
| Output bias | 1.65 V → **mid-scale ≈ 2048** |

## Bring-up — ✅ runnable today, on USB power alone

### Board state

| | |
|---|---|
| Power | **USB only.** No bench supply, no latch, no Pi. The mic front-end runs on the always-on +3.3VA rail. |
| Rail | **down** — do *not* type `on`; it is not needed and `adc5v` will refuse on USB anyway |
| Beam | **off** |
| ADC | `adcmode idle` (ch7 is in the idle set `{1,2,5,7}`) |
| Gear | none. A laptop with `tools/scope.py` makes it far easier to read. |

### Step 1 — the quiescent point

```
adc 7 256
```

**Expect ~2048 codes / ~1.65 V** in a quiet room. That is the LMV321's output bias, and it
should be stable to a few codes.

🔴 **If it sits near 0 or near 4095**, the amplifier is railed — check U17's bias network
before going further. A railed amp will still produce plausible-looking transients.

### Step 2 — a window you can look at

```
capture 0x80 8000 250000
```

8000 samples at 250 ksps = a **32 ms** window on ch7. Plot it:

```
python tools\scope.py --port COM7 --channel 7 --samples 8000 --volts
```

*(`--channel 7` sets the mask to 0x80 for you. Substitute your COM port.)*

### Step 3 — the four stimuli

Run the capture for each and keep the CSV (`--csv`):

| Test | Expect | What it tells you |
|---|---|---|
| Quiet room | flat at mid-scale, small noise band | the noise floor you will threshold against |
| Clap | sharp transient, decaying ring | the amp responds and does not rail |
| Tap on the enclosure | strong, structure-borne | mechanical coupling — this is also the **false-trigger** path |
| **Ball into a net near the board** | the real signal | **capture several; this is the reference set the onset detector gets tuned against** |

### What you are looking for

| | good | bad |
|---|---|---|
| quiet baseline | within a few codes of 2048 | offset or drifting = bias network |
| clap peak | a clear transient, **not clipped at 0 or 4095** | clipping means the ×6.67 gain is too high for your geometry |
| ring-down | decays within a few ms | a long ring is enclosure resonance, and it will widen your veto window |

⚠ **Keep the clipped/unclipped judgement.** The onset detector's threshold is meaningless if
the signal it is tuned on was already railed.

## Onset detection

1. One-pole software high-pass to strip the 1.65 V bias and slow drift
2. Short-term energy over a ~0.5 ms window
3. Trigger when it exceeds *k* × a slow-moving baseline
4. Store the onset timestamp plus a short pre/post snippet for the capture report

Tune *k* against the recorded clap/impact captures rather than guessing.

## ⚠ Set expectations correctly

Sound travels 343 m/s ≈ **2.9 µs/mm**, so mic-to-impact distance uncertainty dominates the
timestamp. Even a 100 mm placement error is 290 µs — an eternity next to the optical
path's ~32 µs.

**The mic is a confirmation gate and a coarse timestamp, never a precision trigger.** Its
highest-value use is as a **veto**: "was there an impact within the last N ms?" That one
test kills insect, hand and shadow false triggers on the optical path, which is worth far
more than any timing it could contribute.

## Optional
J5 I²S digital mic via PIO1 (GPIO4/5/6). Defer until the analog path is characterised —
it is a quality upgrade, not a prerequisite.

---

# Phase 7 — Cameras

**The first phase that genuinely needs the Pi 5** — the Mira220s hang off the Pi's CSI
ports and are configured over I²C by the ams driver. But a surprising amount is testable
without either.

## 🔴 7.0 — The 1.8 V I/O question. ANSWERED, and the answer is bad.

⚠ **This section used to say "get the schematic and establish what sits between the header and
the sensor pins." That was answered on 2026-08-14 from the datasheet** — see the Q6 block near
the end of this document for the full analysis. The short version:

**The Mira220's digital I/O is a 1.8 V domain with no 3.3 V tolerance** (DS000642 v9-00:
VDD18 = 1.70/1.80/1.90 V, VIH max specified as VDD18 itself, VOH min = 1.44 V). Both
directions fail, and **the 220 Ω resistors fix neither** — they limit current, they shift no
levels.

| Direction | Verdict |
|---|---|
| **In** (Cam_Strobe 0/1) | ❌ **Dead on arrival.** The sensor's output ceiling is 1.80 V against an RP2350 VIH of ~2.15 V. The strobes will never read high, so the 7b/7c handshake cannot work without translation. |
| **Out** (D_Cam_Trigger) | ❌ 3.3 V through 220 Ω injects **3.6 mA** into the sensor's ESD clamp — 6× the I/O rail's own 0.6 mA draw, lifting VDD18 out of spec. **The destructive case is driving J4 with the camera unpowered**, which back-powers VDD18 through the diode. |

**Fix: `NEXT_BOARD_REV.md` CR-09**, now unblocked and 🔴 — a real translator on all three
signals plus a 1.8 V reference at J4, which the current pinout does not provide (pins 1/2/5/6
are all GND).

### 🔴 The one thing still unknown, and it decides everything

**J4 lands on the camera board's header, not on raw sensor pins.** If that board already level
shifts, CR-09 may reduce to nothing.

**Get the camera board's schematic before connecting J4 to anything.** Until you have it:

- 🔴 **Do not connect J4 to a camera.**
- 🔴 **Never drive D_Cam_Trigger high with the camera unpowered.**
- ✅ **7a is unaffected** — it jumpers J4 to itself with no camera present, so it stays valid
  and is still the right first step.

## 7a — Loopback, no Pi, no cameras

✅ **Safe with no camera attached, and unaffected by the 1.8 V problem** — the jumpers connect
J4 to itself, so no 1.8 V domain is involved.

### Board state

| | |
|---|---|
| **Cameras** | 🔴 **NOT connected.** That is what makes this safe. |
| Pi | not connected |
| Rail | **up** (`on`) — GPIO10 is a 3V3 output but the FIRING path needs the rail |
| Beam | **off** |
| **Jumpers** | **J4.3 (D_Cam_Trigger) → J4.7 (Cam_Strobe_0)** and **→ J4.8 (Cam_Strobe_1)** |
| Probes | scope on **J4.3** |
| Pins | GPIO10 = trigger out, GPIO8 = strobe_0 in, GPIO9 = strobe_1 in |

This simulates a camera whose shutter opens instantly, and exercises the entire
`wait_both(...)` handshake, the `t_cam` measurement, the timeout/abort path, and the FIRING
state machine — with zero Pi and zero cameras.

### Step 1 — static check before any firing

```
pins
```

**Expect GPIO8 and GPIO9 to follow GPIO10.** With the jumpers in and the trigger idle, all
three read the same level. Toggle the trigger and they should move together.

🔴 **If GPIO8/9 do not follow, stop** — the jumpers are wrong or R19/R24 are open, and every
test below would report a timeout for the wrong reason.

### Step 2 — the three tests

| Test | Method | Pass |
|---|---|---|
| Handshake completes | both jumpers fitted | FIRING proceeds, `t_cam` ≈ 0 |
| Timeout path | **remove one jumper** | `CAM_TIMEOUT` fault, trigger drops, **no burst** |
| Trigger polarity | scope J4.3 | rising edge on request, falling on release |

⚠ **The timeout test is the important one.** A handshake that completes proves the happy path;
only the timeout proves the abort path drops the trigger *without* firing the strobe.

## 7b — Delayed-response simulation

### Board state

| | |
|---|---|
| **Cameras** | 🔴 **still NOT connected** |
| Rail | up |
| Beam | off |
| **Stimulus** | a function generator or a spare Pico driving Cam_Strobe_0/1, **at 3.3 V logic** |
| Jumpers | **removed** — the generator drives J4.7/J4.8 instead |

⚠ **Drive the strobe inputs at 3.3 V from the generator, not 1.8 V.** This phase tests the
firmware's timing, not the level problem; using 1.8 V here just reproduces 7.0's failure and
tells you nothing new.

Drive Cam_Strobe_0/1 with a programmable **50 / 150 / 300 µs** delay after the trigger edge.

- Verify `t_cam` is measured accurately at each delay
- Verify `delay_us(max(0, first_delay_us - t_cam))` behaves at the boundary
- **Specifically test `t_cam > first_delay_us`** — that must clamp to zero, not underflow.
  At high ball speeds `first_delay_us` gets small and this case is reachable in normal
  operation, not just in testing.

> **Implement the handshake in PIO, not as a busy-wait** (`ARCHITECTURE.md` A3).
>
> The .md pseudocode uses `wait_both(PIN_CAM_STROBE_0, PIN_CAM_STROBE_1, HIGH, 5ms)`,
> which spins a core for 100–300 µs in the single most timing-sensitive window in the
> system.
>
> One PIO state machine can assert D_Cam_Trigger, wait for both strobe inputs high, and
> push the elapsed count — producing **`t_cam` as a measured number for free**, which is
> exactly the value the .md wants, with no spinning and no software timeout bookkeeping.
>
> This phase is the natural place to build it: the programmable-delay rig in 7b gives you
> a way to verify the PIO measurement against a known delay before real cameras are
> involved. Budget: one SM out of eight free.

## 7c — Real cameras, Pi seated

### Board state

| | |
|---|---|
| **Gate** | 🔴 **`BENCH_P8_PI.md` §8.0 must be fully ticked first** |
| **Q6 / CR-09** | 🔴 **the camera board's schematic must be in hand.** If it does not level shift, do not connect J4 — fit CR-09 first. |
| PSU | 5.2 V, **limit ≥ 5 A** — a Pi 5 alone pulls 3 A in bursts |
| Pi | seated on J8 |
| **SW2** | 🔴 **taped over** |
| Cameras | connected to the Pi's CSI ports and to J4 |

**Phase 1b passed 2026-07-31**, so that gate is cleared. Two things now govern instead:

- **Work through `BENCH_P8_PI.md` §8.0 before seating a Pi** — it is the current gate list.
- ⚠ **An RP2354 reset is a hard power cut to the Pi, not a reboot.** The pads reset, GPIO15
  goes high-Z, R12 pulls the latch open. `reset` and `bootsel` are guarded in firmware;
  **SW2 is not.** Tape over it while a Pi is seated — and note that this phase involves a
  lot of reflashing, which is exactly when a reflex reach for SW2 happens.

- Verify trigger polarity against the real sensor
- Confirm both strobe-monitor edges arrive
- **Measure the real `t_cam`** and compare against the 100–300 µs assumption
- Check the geometry constraint holds: `beam_to_fov_mm > (D+w)/2 + v·(t_fixed + t_cam)`

## Alternative worth considering

The .md notes an option that avoids sensor mode-switching entirely: leave both sensors
permanently in triggered mode and have the RP2354 generate the preview cadence itself
(single triggers at 2–5 Hz), switching to precision bursts when armed.

Same hardware either way. If the Pi-side mode switching turns out to be slow or flaky,
this is the escape hatch — worth keeping in mind before investing heavily in the
libcamera reconfiguration path.

---

## ⚠ The mic shares the ADC ring, and BURST destroys it

`ADC_MODE_ARMED` is `{5, 7}` — detect and mic free-running at 250 ksps each — so the two
coexist by design. But `ADC_MODE_BURST` is `{0}` only, and `adc_engine_set_mode()` restarts
the ring: the write address resets and the stride changes, so **every mic sample already
captured becomes unreadable the instant BURST is entered.**

This is the same constraint the detect path hit in Phase 3/4, arriving from the other side.
Any mic analysis of a shot must complete before the strobe fires. See `ARCHITECTURE.md`
**A9** for the full ordering rule.

The practical consequence for this phase: while characterising the mic on its own, stay in
IDLE or ARMED and do not let anything switch modes underneath the measurement. `adc <ch>`
and `capture` are both documented as disruptive for exactly this reason.

## ✅ Q6 ANSWERED 2026-08-14 — and the answer is bad

**The Mira220's digital I/O is a 1.8 V domain with no 3.3 V tolerance** (datasheet DS000642
v9-00: VDD18 = 1.70/1.80/1.90 V, **VIH max specified as VDD18 itself**, VOH min = 1.44 V).

**Both directions fail, and the 220 Ω resistors fix neither** — they limit current; they
shift no levels:

- **Reading the strobes cannot work.** The sensor's output ceiling is 1.80 V against an
  RP2350 VIH of ~2.15 V. `Cam_Strobe_0/1` will never read high, so the §7b/7c handshake is
  dead on arrival without translation.
- **Driving the trigger injects 3.6 mA** into the sensor's ESD clamp. Latch-up is not the
  risk (±100 mA immunity), but the I/O rail draws only **0.6 mA max**, so that is 6× its own
  consumption and lifts VDD18 out of spec. **The destructive case is driving J4 with the
  camera unpowered**, which back-powers VDD18 through the diode and violates the power-up
  sequence.

**Full analysis and the fix in `NEXT_BOARD_REV.md` CR-09**, now unblocked and red.

⚠ **One unknown remains, and it decides everything:** J4 lands on the *camera board's*
header, not on raw sensor pins. If that board already level-shifts, CR-09 may reduce to
nothing. **Get its schematic before connecting anything.**

**The loopback test in §7a is unaffected** — it jumpers J4 to itself with no camera present,
so it stays valid and is still the right first step.

---

### (original wording, retained)

Q6 (Mira220 digital I/O possibly 1.8 V, while J4 drives 3.3 V through 220 Ω) is **the only
damage-class open question left in the whole plan** — every other outstanding item is a
measurement whose worst case is a wrong number. It costs nothing to settle from the
datasheet now, and the failure mode is a dead sensor. Do it before Phase 5 rather than at
Phase 7c.
