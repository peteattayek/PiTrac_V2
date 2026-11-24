# Phases 5 & 7 — Microphone and cameras

Two independent phases sharing a document because neither is large.

---

> ## 🟡 FIRMWARE STATUS, audited 2026-08-25, updated 2026-11-10
>
> | | Exists today? |
> |---|---|
> | **Phase 5 bring-up** (`adc 7`, `capture 0x80`, `tools/scope.py`) | ✅ **yes — runnable now** |
> | **`capture trig`** — hardware trigger with pre-trigger history | ✅ **added 2026-08-31** |
> | `tools/scope.py --roll` / `--trig` | ✅ added 2026-08-31 |
> | Mic onset detection (high-pass, energy window, trigger) | ❌ not written |
> | Mic-as-veto logic in the firing path | ❌ not written |
> | **Phase 7 handshake** (trigger, both strobes, `t_cam`, FIRE at max(first delay, t_cam), `CAM_TIMEOUT`) | 🟡 **written 2026-11-10; image 1.5** (1.4 failed its review, H7-F1/F2/F3; 1.5 fixes them): `cam`, `cam shot`, `cam trig` (7a/7b below). Host-tested; independent confirm review PASS. **On board 1 (image 1.11): 7a and 7b PASS, 2026-11-13** (7a Step 3's 5000 µs repeat not run); **7c.0 and 7c.1 PASS, 2026-11-18**, with the Mira220 (`t_cam` about 0.2 µs) (`PROGRESS.md` §6, §10) |
> | PIO camera handshake (A3) | 🟡 `src/cam_handshake.pio` on **PIO1** (not PIO0 SM1 as planned: `ARCHITECTURE.md` A3), plus `src/strobe_gate.pio`, which holds the strobe burst until the handshake's FIRE |
> | A shot from a detected pass (ball speed → schedule → first delay → fire) | ❌ not written. The test shots start at the handshake, with the first delay typed |
> | The strobe fired with a Pi present, or with live current from the shot path | ❌ refused by design: camera-gated bursts are **dry only**. Every firing was also refused with a Pi present up to 1.7; image 1.11 lifts that (review PASS; `BENCH_P8_PI.md` §8.10 PASS on board 1, 2026-11-12; 1.8–1.10 failed their reviews) |
> | I²S digital mic (J5) | ❌ not written, and deliberately deferred |
>
> **Phase 5's bring-up section runs on any image**, on USB power alone. ✅ **Bring-up is COMPLETE
> on board 3 (2026-08-31)**: the measured results are below. **7a and 7b run on image 1.5 or later**
> (board 1 runs 1.11 and needs no reflash: "Before 7a and 7b" below). If a command below is not in `help`, it does not exist
> on the image you are running.

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

> ### 🔵 USE `capture trig`. A hand-started capture cannot catch a clap.
>
> Added 2026-08-31. The window here is tens of milliseconds; timing a clap into one you
> started by pressing Enter is not realistic, and rolling captures are blind during each
> serial dump. **The board triggers itself:**
>
> ```
> capture trig 7 80
> ```
> or, plotted and aligned for you:
> ```
> python tools/scope.py --port COM11 --channel 7 --trig 80 --volts --csv captures/clap.csv
> ```
>
> 🔵 **It runs the DMA continuously and stops it AFTER the trigger**, so the buffer holds
> the baseline and the leading edge -- the part an onset detector has to be tuned on, and the
> part a start-on-command capture can never contain. 25 % of the window is pre-trigger by
> default: **8.2 ms at the 500 ksps default, 16.4 ms at 250 ksps.** `t = 0` is the trigger in
> both the plot and the CSV.
>
> Starting thresholds: **60-80** codes for a snap, **150** for a clap, **400+** for a ball.
>
> ⚠ There is also `--roll` for a live rolling view, with `--pause-on-event`. Good for "is the
> mic alive"; it is **not** continuous, and it is not the tool for capturing a transient.

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

## ✅ MEASURED -- board 3, 2026-08-31. The front end is healthy.

| | clap | snap |
|---|---|---|
| baseline | **2053 codes / 1.6544 V** | 2053 codes |
| noise sigma | **0.64 codes (0.52 mV)** | 0.62 codes (0.50 mV) |
| peak | **+240 codes (193 mV)** | **+297 codes (239 mV)** |
| SNR | **51.5 dB** | **53.6 dB** |
| clipped? | no (1854-2293) | no (1960-2350) |
| decay to 2 % of peak | 2.51 ms | 1.12 ms |

✅ **Noise is SUB-LSB** -- 1 LSB is 0.806 mV and sigma is 0.50 mV, so the analog section is as
quiet as this ADC can resolve. ✅ **Headroom is ample**: the loudest stimulus used 14.5 % of the
available swing, leaving about **7x** before clipping.

✅ **The pre-trigger buffer is genuine.** 16.38 ms of history sitting at exactly the baseline,
sigma 0.62, p-p 5-7 codes -- which is also the proof that the ring-mode DMA and its
"do not arm until the history exists" guard both work.

### 🔴 45 % of a clap's energy is BELOW the 2.41 kHz high-pass corner

Energy by band, over the 6 ms after onset:

| band | clap | snap |
|---|---|---|
| 1.0 - 2.4 kHz (**below the corner**) | 🔴 **44.7 %** | 5.7 % |
| 2.4 - 5.0 kHz | 25.6 % | **42.3 %** |
| 5.0 - 10.0 kHz | 11.9 % | 16.3 % |
| 10.0 - 24.1 kHz | 10.4 % | 20.8 % |
| spectral peak | **1501 Hz** | 2502 Hz |

The corner is a **single pole**, so sub-corner content still gets through, attenuated -- which
is why it is visible at all. But **C84/R106 is rejecting the band where a clap's energy lives.**

⚠ **This only matters if a ball impact resembles a clap rather than a snap.** An impact is
typically a low-frequency thud plus a high-frequency click. **Capture a real ball before
deciding**: if its energy sits at 1-2 kHz like the clap, the front end is discarding signal and
a larger C84 is the fix (see `NEXT_BOARD_REV.md` CR-18). If it looks like the snap, the corner
is right as drawn.

### 🔵 The ring-down is REAL ACOUSTIC CONTENT, not the filter

Predicted before measuring: an impulse into a 2.41-24.1 kHz bandpass should ring at roughly the
geometric mean, ~7.6 kHz, identically for every stimulus. **That was wrong.** Zero-crossing
rates across the 0.2-2.0 ms decay:

| clap | **9.4 kHz** |
|---|---|
| snap | **24.2 kHz** |

Two stimuli, two rates -- so this is the *sound*, not the amplifier's impulse response.
✅ **The mic discriminates between stimuli.** ✅ And both decay fully within 2.5 ms, so there is
**no long enclosure resonance** to widen a veto window.

## Onset detection

1. One-pole software high-pass to strip the 1.65 V bias and slow drift
2. Short-term energy over a ~0.5 ms window
3. Trigger when it exceeds *k* x a slow-moving baseline
4. Store the onset timestamp plus a short pre/post snippet for the capture report

Tune *k* against the recorded clap/impact captures rather than guessing.

> ### 🔵 TIMESTAMP THE THRESHOLD CROSSING, NOT THE PEAK. Measured 2026-08-31.
>
> | | crossing -> peak |
> |---|---|
> | clap | **88 us** (10-90 % rise 44 us) |
> | snap | **300 us** |
>
> The peak is whichever oscillation cycle happens to be largest, so it moves with the stimulus
> and with where the samples land. **The crossing is both earlier and far more repeatable.**
> ✅ Both events are **positive-going first**, so a signed detector is viable and needs no
> rectifier.
>
> ⚠ Keep it in proportion: at 2.9 us/mm a 30 mm placement uncertainty already swamps the 88 us.
> This choice buys REPEATABILITY, not accuracy.

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

### ✅ The camera board does not level-shift (owner, 2026-11-11)

**It takes a 1.8 V trigger and puts out a 1.8 V strobe**, so the translation is needed. The fix
is on the next board revision (CR-09). Until then:

- 🔴 **Never connect J4 straight to a camera.** Only through a TXS0108E module, wired as in
  `BRINGUP_NEW_BOARD.md` Step 13.4: its A side on the camera's 1.8 V, its B side on the
  board's 3.3 V (J5.2).
- 🔴 **Never drive D_Cam_Trigger high with the camera unpowered.**
- ✅ **7a and 7b are unaffected**: they use J4 with no camera on it.

## Before 7a and 7b: the image (reflash first)

> **A board already on image 1.11 (board 1 since 2026-11-12) needs no reflash.**
> - 1.11 carries this Phase 7 code unchanged: `camera.c` and `shot.c` are identical to
>   1.7's. The 1.9 and 1.11 reviews confirmed the camera-gated shots stay dry.
> - Every expected output below is 1.11's too.
> - The one difference: up to 1.7 `cam shot` was refused with a Pi present. 7a and 7b run
>   with no Pi, so it does not arise.
> - `id` → `version : 1.11`; then start at 7a.

> 🔴 **Reflash first: image 1.5. Independent confirm review PASS** (`PROGRESS.md` section 6). Image 1.4 failed its
> independent review (H7-F1/F2/F3, `captures/review_2026-11-10/INDEPENDENT_REVIEW.md`). 1.5 fixes
> all three (`PROGRESS.md` §6, "Image 1.5"). **Never use 1.4**; its build folder is removed.
>
> | | |
> |---|---|
> | Folder | `build_v1.5\` (Release `Nov 10 2026 16:48:47`, version 1.5, try-before-you-buy) |
> | Size | 241824 text / 0 data / 422684 bss |
> | `pitrac.uf2` SHA256 | `F82A6562C5F661D95197A2E582E54B11E9311DC5DB6196F49E95DC3AF85374B7` |
> | `pitrac.bin` SHA256 | `6ABFDBE4C28965DC8BBAC48B73FBD4945FC6AD769903253E4E9CF20B1716A9AA` (237732 B) |
> | Status | built and host-tested 2026-11-10; **independent confirm review PASS** (`HANDOFF.md` §5 rule 8) |
>
> **The confirm review passed.** Load this exact image by USB, with **no Pi on J8**:
> 1. At the CLI: `bootsel`. The `RP2350` drive appears.
> 2. In `Hardware\firmware`: `Copy-Item build_v1.5\pitrac.uf2 E:\` (your drive letter). It goes
>    into the partition that is not running, and runs on trial.
>    - **If the PC blocks writes to the drive:** use
>      `picotool load -p <n> -v -x build_v1.5\pitrac.uf2` instead. `<n>` is the partition that
>      `upd` does **not** name as running: A = 0, B = 1. On a board fresh from
>      `BRINGUP_NEW_BOARD.md` Step 1.3, with r5 in B, that is `-p 0`. See `HANDOFF.md` §4.
> 3. Reconnect the serial port. `upd` → `ON TRIAL: bought itself`. `id` → `version  : 1.5`,
>    `built    : Nov 10 2026 16:48:47`, and a `+ Phase 7: the camera handshake` line.
>
> 1.5 is newer than every image on board 1 (1.2, and 1.3 if `BENCH_P8_PI.md` §8.9b ran) and on
> a board fresh from bring-up (r5, 1.1), so buying it erases nothing: the older image stays as
> the fallback.

**What the firmware does.** One handshake, all of it in PIO (`ARCHITECTURE.md` A3):

| | |
|---|---|
| t = 0 | D_Cam_Trigger (GPIO10, J4.3/J4.4) goes high |
| wait | for both Cam_Strobe inputs (GPIO8 J4.7, GPIO9 J4.8) high in the same sample, polling every 33.3 ns |
| FIRE | at **max(first delay, t_cam)**: the handshake releases the strobe engine directly (PIO1 → PIO0), with no CPU in between |
| burst | the first strobe rising edge on GPIO25 **4–5 µs after FIRE** (the burst engine's 1 MHz start), then the burst as in 6a |
| hold | the trigger stays high for the burst plus 50 µs, then drops |
| timeout | no strobes by the timeout (default 5000 µs): the trigger drops, **nothing fires**, `CAM_TIMEOUT` latches |

`cam shot` fires the burst **dry**: the gate DAC at zero, so no LED-bank current is possible.
Refused with live mode armed, a Pi present, the rail down, a fault latched, GPIO10 already high,
or a strobe input already high.

## 7a — Loopback, no Pi, no cameras

✅ **Safe with no camera attached, and unaffected by the 1.8 V problem**: the jumpers connect J4
to itself, so no 1.8 V domain is involved.

### Board state

| | |
|---|---|
| **Image** | **1.5 or later** (above). Board 1: 1.11 |
| **Cameras** | 🔴 **NOT connected.** That is what makes this safe |
| Pi | **not on J8** |
| PSU | 5.20 V on J1, limit 2 A; USB on J6 |
| Rail | **up** (`on`; `stat` → `BENCH_RUNNING`). The trigger is never driven with the rail down |
| Beam | **off** |
| Strobe | dry: `strobe` → live OFF, gate level 0. **J3 disconnected**, as in 6a: a dry burst cannot drive current, and nothing here needs the LED bank |
| **Leads** | a small solderless breadboard and three female–male 2.54 mm leads: **J4.3**, **J4.7** and **J4.8** into **one row**. That ties both strobe inputs to the trigger and leaves **J4.4**, the trigger's other pin, free for the analyser: a pin under a lead's housing cannot be probed (changed 2026-11-13; it was J4.3 → J4.7 and J4.4 → J4.8) |
| Probes | logic analyser (or scope), 3.3 V logic, ≥ 100 MS/s: **J4.4** (the trigger) and **GPIO25 at R33** (the 6a lead, on the end farther from R57). Ground on **J4.5** (J4.1, .2, .5 and .6 are all GND; 7b's generator takes J4.2 and J4.6). J4.7 is not probed: in 7a it is the trigger's own wire, and `pins` (Step 1) and t_cam (Step 2) prove GPIO8/9 see it |

**Probe-point check (`HANDOFF.md` §3 rule 8).** J4 is a 2×4 header at 2.54 mm pitch. Pins 1, 2, 5
and 6 are GND, 3 and 4 the trigger, 7 and 8 the strobes: nothing on it is above 3.3 V. A slip from
J4.3 onto J4.1 or J4.5 shorts GPIO10's output to GND through R18 (220 Ω, 15 mA), which the pin
survives. Fit the leads and clips with the PSU **and** USB off, then power up.

### Step 1 — the pins, before any handshake

**Do:**
```
cam
gpio 10 1
pins
gpio 10 0
pins
```
**Pass:**
- `cam` → `engine   : PIO1 SM0, program 3..31 (29 instructions), stopped, PC 3 (load D)`;
  `D_Cam_Trigger(10) on SIO, pad 0`; both strobes `0`; `gate     : strobe engine PC 20 (gate 19,
  burst 20..31)`; `FIRE flag (PIO0 irq 4) 0`.
- After `gpio 10 1`: `pins` shows `CAM_STROBE_0(8) 1` and `CAM_STROBE_1(9) 1`.
- After `gpio 10 0`: both `0`.

**Stop if** GPIO8 or GPIO9 does not follow: a lead is wrong or R19/R24 is open, and every test
below would time out for the wrong reason. **Leave GPIO10 at 0**: `cam shot` refuses while it is high.

### Step 2 — the handshake completes, and fires at the first delay

**Do:** arm the analyser on J4.4 rising (capture ≥ 10 ms), then:
```
cam shot
```
**Expect** (the defaults: first delay 200 µs, the S15 burst at 70 m/s, timeout 5000 µs). The second line is 1.11’s; from image 1.12 it ends at “every 610 us.”):
```
cam shot : first delay 200 us, timeout 5000 us; DRY burst = S15 at 70.0 m/s:
           10 x 14 us, every 610 us. J4: NO CAMERA (CR-09).
shot     : #<n> (test) -- FIRED: handshake complete, dry burst complete
plan     : first delay 200 us, timeout 5000 us, hold 5565 us -> D 5997, T 143999, H 166948
           (5-cycle loops at 150000000 Hz = 33.3 ns)
handshake: both strobes high at t_cam = 0.040 us (to one loop, 33.3 ns)
           within the first delay: FIRE at 199.993 us (the first delay)
           trigger released at 5764.987 us (FIRE + the hold)
           engine: done: trigger released after the hold. Words 0000176c ffffffff ...
strobe   : DRY burst 10 x 14 us, gap 596 us -- completed, IRQ0 seen
           10 pulses; the first rising edge 204.0-205.0 us after the trigger
```
**Pass:**
- `FIRED`, with t_cam **≤ 0.08 µs**: the loopback is a wire, so the strobes follow the trigger
  within the PIO's 2-cycle synchroniser and one poll.
- On the analyser, J4.4 high for **5765 µs ± 2 µs**.
- On GPIO25, **10 pulses of 14 µs, 610 µs apart**, the first rising edge **204.0–205.0 µs** after
  J4.4 rises. The 4–5 µs after FIRE is the burst engine's 1 MHz start, and it varies by up to 1 µs
  from shot to shot.
- The burst ends before J4.4 falls.

**Repeat** 3 times; the edge time stays within 204.0–205.0 µs. **Record** t_cam, and the J4.4 →
first GPIO25 edge time for each shot.

### Step 3 — the timeout fires nothing (the important one)

**Board state change:** PSU **and** USB off; remove the **J4.8** lead, both ends; power up, `on`.
Strobe 1 now has nothing on it but its pull-down.

**Do:** arm the analyser on J4.4 rising, then:
```
cam shot 200 70 1000
fault
```
**Expect:**
```
shot     : #<n> (test) -- CAM_TIMEOUT: trigger dropped, nothing fired, fault latched
handshake: TIMEOUT -- both strobes not seen. Trigger dropped at 999.987 us
strobe   : DRY burst 10 x 14 us, gap 596 us -- NOT FIRED -- stopped with the camera gate still closed
           strobe gate: NEVER OPENED -- read with the engine stopped: PC on the gate, no IRQ0. Nothing reached GPIO25.
fault    : CAM_TIMEOUT latched -- 'fault clear' before the next shot
```
and `fault = CAM_TIMEOUT`.

**Pass:**
- J4.4 high for **1000 µs ± 1 µs**, then low.
- **No edge at all on GPIO25.**
- `NEVER OPENED`: the firmware stopped the strobe engine (GPIO25 to SIO low, the state machine
  disabled) and then read its PC still on the gate instruction. That read-back, and nothing
  else, is what lets it report `CAM_TIMEOUT`. A shot that times out with the gate not proven
  closed ends `ENGINE FAULT` instead (independent review H7-F2).

**Then:** `fault clear`. Run `cam shot` (5000 µs default): J4.4 high for **5000 µs**, same result.
`fault clear` again.

**Stop if** GPIO25 shows any edge, or the trigger stays high past the timeout: report it, do not
continue.

### Step 4 — the handshake alone, and the refusals

**Board state:** the J4.8 lead back into the row (PSU and USB off to refit), rail up.

**Do:**
```
cam trig
cam
gpio 10 1
cam shot
gpio 10 0
off
cam shot
gpio 10 1
pins
on
```
**Pass:**
- `cam trig` → `HANDSHAKE: complete, no burst asked for`, `FIRE at 199.993 us`, `trigger
  released at 1199.987 us`. On the analyser, J4.4 high for **1200 µs**, and no GPIO25 edge.
- `cam` then shows `FIRE flag (PIO0 irq 4) 1`: the FIRE that nothing was waiting for. The next
  `cam shot` clears it before it arms.
- With GPIO10 high, `cam shot` → `REFUSED: nothing was driven`, `camera: D_Cam_Trigger (GPIO10)
  is already high -- 'gpio 10 0' first`.
- With the rail down (`off`, wait for `stat` → STANDBY), `cam shot` → `REFUSED`,
  `strobe: REFUSED -- nothing fired`, `+5V rail not ready`. On the analyser, J4.4 never rises.
- With the rail down, `gpio 10 1` → `REFUSED: the +5 V rail is not up ('on' first): the trigger is
  never driven without it. gpio10 unchanged at 0.`, and `pins` shows `CAM_TRIGGER(10) 0`. Image
  1.4 drove it high here (independent review H7-F3).

**Exit criteria for 7a:** Steps 2–4 pass; the timeout fires nothing; the trigger rises on the
request and falls at the release or the timeout.

## 7b — Delayed-response simulation

### Board state

| | |
|---|---|
| As 7a | the same image, rail up, no Pi, no camera, beam off, strobe dry, J3 disconnected |
| **Leads** | the 7a leads **removed**. The generator output on **J4.7 and J4.8 together** (one lead to each), its ground on **J4.6**. Its trigger input on **J4.3**, with that lead's ground on **J4.2**. J4.4 stays free for the analyser |
| **Generator** | a function generator, or a spare Pico or Pico 2 running **`tools/pulsegen`** (below). **0 V / 3.3 V** pulses, started by the trigger's rising edge (J4.3), after a programmable delay (50, 150, 300 µs), and lasting until after the shot (≥ 6 ms) |
| Probes | **J4.4** (the trigger) and **GPIO25** (R33), as in 7a, plus **the strobe node**: J4.7 carries the generator's lead, so probe that lead's other end (with `tools/pulsegen`, GP16's breadboard row) |

⚠ **Drive the strobe inputs at 3.3 V, not 1.8 V and not 5 V.** This phase tests the firmware's
timing, not the level problem: 1.8 V just reproduces 7.0's failure. 5 V through R19/R24 220 Ω
pushes ~6 mA into the RP2354's clamp diodes. On a generator, set the output for a **high-impedance
load** (a 50 Ω setting doubles the level into J4) and check 0 / 3.3 V on the scope **before**
connecting it. Fit the leads with the PSU and USB off.

The generator must idle **low**: `cam shot` refuses if a strobe input reads high before the trigger.

**With `tools/pulsegen`, on a spare Pico** (written 2026-11-12; `pulsegen.c` describes it):
- **Load it once.** Hold the Pico's BOOTSEL button while plugging it into the PC. Then, in
  `Hardware\firmware`:
  - a Pico: `picotool load -x tools\pulsegen\build_pico\pulsegen.uf2`;
  - a Pico 2: `picotool load -x tools\pulsegen\build_pico2\pulsegen.uf2`.

  picotool is `%USERPROFILE%\.pico-sdk\picotool\2.3.0\picotool\picotool.exe`. It only
  talks to a board in BOOTSEL, so the PiTrac board can stay connected.
- **Open the Pico's new COM port** in a second terminal. After ~2.5 s it prints its pin map
  and `delay 200.00 us`; `?` prints them again.
- **Wire it** with the PiTrac board's PSU and USB off and the Pico unplugged. Put the Pico
  in a solderless breadboard (it needs header pins), and run female–male 2.54 mm leads from
  J4 to its rows:
  - Pico pin 20 (GP15) ← **J4.3**;
  - pin 21 (GP16) → **J4.7**;
  - pin 22 (GP17) → **J4.8**;
  - pin 18 (GND) → **J4.2**, and pin 23 (GND) → **J4.6**.
  - The analyser's strobe channel goes in GP16's row (a male–male lead into its socket); its
    trigger channel on J4.4 and its ground on J4.5, as in 7a.

  All of it is 3.3 V logic, so the level warning above is met without setting anything.
- **Power order:** the Pico's USB first, then the board's PSU and USB, and the reverse at the
  end. An unpowered Pico would take the trigger's current through GP15's protection diode.
- **Expect one pulsegen shot when the board's PSU goes on.** J4.3 is undriven until the
  board's firmware starts, 140 ms after power-on.
  - The plug-in transient lifts it, and a Pico 2's GP15 (erratum E9) then holds it near
    2.1 V.
  - pulsegen counts a shot and raises the strobes for 10 ms, long before `on`.
  - It is harmless (`PROGRESS.md` §6, 2026-11-13).
- **Set the delay at its prompt:** `d 50`, `d 150` and `d 300` for Step 1; `d 200` and
  `d 201` for Step 2. Each shot prints a line.
- **How exact it is:** both outputs rise together, a little after the set delay.
  - On board 1 with a Pico 2 (2026-11-13), the analyser measured 10–18 ns late, J4.4 → GP16:
    about one PIO cycle of jitter (6.7 ns; 8 ns on a Pico), plus a fixed latency that its
    5-cycle allowance does not cover.
  - They hold for 10 ms and idle low.
  - The analyser's J4.4 → strobe-node delay, not the set delay, is the reference that t_cam
    is judged against.

### Step 1 — t_cam at 50, 150 and 300 µs, around a 200 µs first delay

**Do**, for each generator delay of **50, 150, 300 µs**, three shots each:
```
cam shot 200
```
**Expect:**
- **50 and 150 µs:** `t_cam = 50.0xx` / `150.0xx us`, `within the first delay: FIRE at 199.993 us`.
  The first GPIO25 edge **204.0–205.0 µs** after J4.4.
- **300 µs:** `t_cam = 300.0xx us`, then `AFTER the first delay (200 us): FIRE at once`, at
  **t_cam + 0.087 µs** (13 cycles), and `(the delay clamped to zero -- no underflow)`. The first
  GPIO25 edge at **t_cam + 4.1 to 5.1 µs**.

**Pass:**
- t_cam agrees with the analyser's J4.4 → strobe-node rising-edge delay within **±0.1 µs** at
  every delay.
  That is one 33.3 ns loop, the 2-cycle synchroniser, the RC through 220 Ω, and the scope.
- The first GPIO25 edge lands where the line above says.
- **At 300 µs, nothing fires before the strobes arrive**, and nothing waits after them.

### Step 2 — the boundary (t_cam just past the first delay)

**Do:** generator delay **200 µs**, then **201 µs**; `cam shot 200` three times at each.

**Pass:**
- 200 µs is reported early or late, either is right.
- 201 µs is `AFTER the first delay`, and its FIRE is ≤ 0.1 µs after t_cam.
- Neither ever fires before **199.993 µs**, and none shows a huge or wrapped time: that would be
  an underflow.

**Exit criteria for 7b:** t_cam within ±0.1 µs of the analyser at 50/150/300 µs, and the first
pulse 4–5 µs after FIRE in every shot. FIRE is at the first delay (199.993 µs) or at t_cam +
0.087 µs, whichever is later. (An earlier copy said "max(first delay, t_cam) + 4–5 µs", which
leaves out that 0.087 µs: `PROGRESS.md` §6, 2026-11-13.)

> **The handshake is in PIO** (`ARCHITECTURE.md` A3; written 2026-11-10, image 1.5). The .md's
> `wait_both(PIN_CAM_STROBE_0, PIN_CAM_STROBE_1, HIGH, 5ms)` would have spun a core for
> 100–300 µs (the assumption; 7c.1 measured about 0.2 µs) in the most timing-sensitive window in the system. Instead, one state machine raises
> the trigger and counts while it waits for both strobes, read in one sample each poll. It FIREs the strobe engine at max(first
> delay, t_cam) and drops the trigger: t_cam comes back as a measured count, and no core waits.
> `tests/cam_pio_test.c` runs the assembled programs in a cycle model and checks every time
> above, the 7a timeout and the 7b boundary included.

## 7c — Real cameras, Pi seated

### Board state

| | |
|---|---|
| **Gate** | 🔴 **`BENCH_P8_PI.md` §8.0 must be fully ticked first** |
| **Q6 / CR-09** | 🔴 **The camera does not level-shift** (owner, 2026-11-11). J4 to a camera **only through a TXS0108E module**, wired as in `BRINGUP_NEW_BOARD.md` Step 13.4 |
| **Rules** | ✅ **Allowed by the owner, 2026-11-13** (`PROGRESS.md` §8): a Pi on J8 and a camera on J4 for 7c, the camera only through the module. SW2 taped over and the Pi's own USB-C unplugged, as in Phase 8 |
| **Pi software** | the Mira220 driver (`Software/camera-comparison/scripts/install-mira220.sh`). From 7c.1, the sensor in external-trigger mode with its strobe output on: **the driver starts every stream free-running**, and a register switch after each start puts it in slave mode (`PROGRESS.md` §6, 2026-11-16): `scripts/trigger-capture.sh` and `scripts/mira220-trigger.sh` in `Software/camera-comparison`, written 2026-11-16 (7c.1). 7c.0 needs only the stream |
| PSU | 5.2 V, **limit ≥ 5 A** — a Pi 5 alone pulls 3 A in bursts |
| Pi | seated on J8 |
| **SW2** | 🔴 **taped over** |
| Cameras | on the Pi's CSI ports, and on J4 through a TXS0108E module each (one camera in 7c.0 and 7c.1; two from 7c.2) |

**Phase 1b passed 2026-07-31**, so that gate is cleared. Two things now govern instead:

- **Work through `BENCH_P8_PI.md` §8.0 before seating a Pi** — it is the current gate list.
- ⚠ **An RP2354 reset is a hard power cut to the Pi, not a reboot.** The pads reset, GPIO15
  goes high-Z, R12 pulls the latch open. `reset` and `bootsel` are guarded in firmware;
  **SW2 is not.** Tape over it while a Pi is seated — and note that this phase involves a
  lot of reflashing, which is exactly when a reflex reach for SW2 happens.

**Firmware for 7c:** `cam trig` runs the handshake alone, with no strobe burst. `cam shot` was
refused with a Pi present up to image 1.7; image 1.11 lifts that (review PASS; `BENCH_P8_PI.md`
§8.10 PASS on board 1, 2026-11-12). Its burst stays dry. Nothing here changes CR-09: every one
of these commands drives the 3.3 V trigger, so a camera goes on J4 only through the module. Up
to 1.11 their printouts say `J4: NO CAMERA (CR-09)`: ignore that. Image 1.12 drops that text. Its software review passed on 2026-11-18
(`PROGRESS.md` §6, "Image 1.12").

### 7c.0 — The module and the strobe path, the camera free-running (PASS 2026-11-18)

Written 2026-11-13.

**What it proves:**
- the TXS0108E module takes its 1.8 V side from the camera, and translates both ways;
- the trigger's high at the camera clears the Mira220's VIH;
- the camera's strobe output reaches GPIO8 at 3.3 V.

**There is no handshake.** The installed driver runs the sensor free-running, and turns on its
illumination-trigger output at init, so the strobe pulses once per frame by itself
(`PROGRESS.md` §6, 2026-11-13).

**Board state.** Fit every lead, cable and probe with the PSU and USB off.

| | |
|---|---|
| Image | 1.11 (board 1), no reflash |
| Pi | seated on J8, **SW2 taped over**, the Pi's own USB-C unplugged. It needs the Mira220 driver: on the Pi, `bash Software/camera-comparison/scripts/install-mira220.sh --check`. **A Pi without it** (board 1's had neither the driver nor the repo until 2026-11-18, when it was set up: `PROGRESS.md` §6): copy `Software/camera-comparison` to it, then follow that folder's `README.md` from "Install only the required packages" to "Check what actually loaded", with one camera: `camera_auto_detect=0` and `dtoverlay=mira220-nir` only (`,cam0` if it is on CAM/DISP 0). Add `i2c-tools`, the `i2c-dev` module and the `i2c` group for 7c.1. Never `apt upgrade`: a new kernel needs the driver rebuilt. A `sudo reboot` on the seated Pi keeps the rail up (`BENCH_P8_PI.md` 8.6) |
| Camera | **one** Mira220, on a Pi camera port, with that port's overlay in `config.txt` (`install-mira220.sh --help`) |
| Module | TXS0108E, as camera 0's rows of `WIRING_DIAGRAM.md` §7.2. VB ← **J5.2**; VA and OE ← the camera's **P1.5** (1V8); GND ← **J4.1** and the camera's **P1.1**. B1 ← **J4.3**; A1 → **P1.7** (REQ EXP); A2 ← **P1.6** (ILLUM TRIG); B2 → **J4.7**. The camera's P1.2 (3V3) stays unconnected |
| J4.8 | **in one breadboard row with B2 and J4.7**, so both strobe inputs see the one camera. The handshake waits for both; 7c.1 needs this |
| PSU | 5.20 V, limit **≥ 5 A** |
| Beam, strobe | beam off; strobe dry; **J3 disconnected** |
| Analyser | Four channels (the owner's analyser, 2026-11-18). ch0 analog on the module's **A1**; ch1 digital **and** analog in the **B2/J4.7 row**; ch2 analog on **A2**; ch3 analog on **VA**. The 1.8 V levels sit below a 3.3 V logic threshold, hence analog. Ground on **J4.5**. J4.4 is not probed in 7c.0: `gpio 10` holds the trigger still. Name the channels in Logic 2 (A1, strobe, A2, VA): the names go into the exports |

**Power up:** PSU on, then USB, then `on`. Wait for `stat` → `RUNNING` with `pi : present 1`.
Start nothing on the Pi yet.

**Step 1 — the module's supply and the idle levels** (the camera powered, not streaming).

**Read:** VA, A1 and A2 on the analyser; then `pins` on the board.

**Pass:**
- VA **1.70–1.90 V**. If it reads 0 V, the camera's 1.8 V is up only while it streams: run
  Step 3 first, then Steps 1 and 2 with the stream running.
- A1 **below 0.54 V** (the Mira220's VIL max), with GPIO10 low.
- **Record** the strobe's idle level: A2, and `CAM_STROBE_0(8)` / `CAM_STROBE_1(9)` in `pins`.
  - Both 0 is what the handshake needs.
  - Both 1 means the sensor's strobe idles high. Report it: it sets the polarity in 7c.1.

**Step 2 — the trigger's level at the camera.**

**Do:** `gpio 10 1`, then read A1 for 1 s; `gpio 10 0`, then read A1.

The driver starts every stream in master mode, where the sensor times its own frames, so
REQ EXP should be ignored. If it is not, the high starts an exposure, which is harmless.

**Pass:**
- With GPIO10 high, A1 settles **between 1.26 V and VA + 0.1 V**. 1.26 V is the sensor's VIH,
  0.7·VDD18.
  - **~1.35 V is predicted:** the module's 4 kΩ pull-up against the camera's 12 kΩ pull-down on
    REQ EXP (`WIRING_DIAGRAM.md` §7.3).
  - **Board 1 measured 1.277 V** (2026-11-18): the module's pull-up is nearer 4.9 kΩ. The
    strobe's falling edges also dipped it to 1.04 V for ≤ 1 µs. 7c.1 bridges channel 5 onto
    the trigger channel, whatever this step reads.
- With GPIO10 low, A1 **below 0.54 V**.
- **Stop if** A1 rises above VA + 0.3 V, which would be the 3.3 V side leaking through. Send
  `gpio 10 0`, then `off`.
- **If the high is under 1.26 V:** power down and bridge channel 5 onto the trigger channel: B5
  to its B pin, A5 to its A pin. The trigger channel is the one whose B pin goes to J4.3 (B1/A1
  in the doc's wiring). That halves the pull-up (~1.5 V, §7.3). Then repeat.

**Step 3 — the strobe crosses the module.**

**Do:**
1. On the Pi, pause the desktop camera clients as `Software/camera-comparison/QUICKSTART.md`
   says.
2. Stream the Mira220 alone, from `Software/camera-comparison`:
   ```
   CONFIG="$HOME/camera-focus-$(date +%Y%m%d-%H%M%S)"
   bash scripts/configure-cameras.sh --output "$CONFIG" --camera mira220 --exposure-us 1000 --gain 1 --apply &&
   bash scripts/focus-preview.sh --config "$CONFIG"
   ```
3. Capture 100 ms on the analyser.
4. Run `pins` a few times on the board.

**Pass:**
- A2 pulses **0 ↔ VA** once per frame: ~11.2 ms apart at the ~89 fps that
  `QUICKSTART.md` measured.
- The B2/J4.7 row follows each A2 edge within **1 µs**.
  - Its high is **≥ 2.5 V**. ~2.8 V is predicted: the module's pull-up against GPIO8's and
    GPIO9's pull-downs.
  - Its low is **≤ 0.4 V**.
- `pins` sometimes catches `CAM_STROBE_0(8) 1` and `CAM_STROBE_1(9) 1` together.
- **Record** the strobe's width and polarity, and whether the width follows the exposure (repeat
  at 2000 µs if the preview allows it).

**Do not run `cam shot` or `cam trig` in 7c.0.** The strobe pulses by itself, so a handshake
would complete on whatever frame came next, or be refused with a strobe already high.

**Finish:**
1. Stop the stream.
2. `off`: the Pi halts, and the rail drops ~10 s later.
3. `stat` → `STANDBY`.
4. PSU and USB off before anything is unwired.

**Exit criteria for 7c.0:**
- VA 1.70–1.90 V;
- A1 ≥ 1.26 V high and < 0.54 V low;
- the strobe crosses the module once per frame, ≥ 2.5 V high at J4.7.

### 7c.1 — The Mira220 triggered by the board (slave mode)

Written 2026-11-16. **PASS on board 1, 2026-11-18** (`PROGRESS.md` §6): one frame per trigger, and
the strobe following REQ EXP about 0.17 µs late on both edges. `t_cam` read 0.207–0.240 µs on
the firmware and 0.168–0.184 µs on the analyser.

**What it proves:**
- **Slave mode works:** the sensor waits for REQ EXP. No frames come without a trigger, and one
  comes per trigger.
- **The strobe in slave mode:** whether ILLUM TRIG pulses, and when. Its rise after the trigger
  is the real `t_cam`.
- **The firmware's `t_cam`** agrees with the analyser, as in 7b.
- **The burst:** `cam shot`'s dry burst starts 4–5 µs after FIRE, while the sensor exposes.

**Before it:** 7c.0 PASS. Its Step 1 must have found the strobe idling **low**. If it idled
high, every `cam` command is refused with a strobe input reading high: stop and report.

**How the switch works** (`PROGRESS.md` §6, 2026-11-16):
- **When:** the driver starts every stream as master and powers the sensor down 1 s after the
  stream stops. So the switch is a few I²C writes made after *each* stream start.
- **The sequence,** to address 0x54 on the bus in the entity name (`mira220 <bus>-0054`):
  1. Stop the imager: `0x1003 = 0x02`, `0x10F0 = 0x00`, then wait 40 ms.
  2. Clear bits 6 and 0 of `0x1001`. The driver sets both: they are `EXT_EVENT_SEL` and
     `EXT_EXP_PW_SEL`.
  3. Restart as slave: `0x1003 = 0x08`, `0x10F0 = 0x01`.
- **The result:** the sensor exposes while REQ EXP is high, and the fall starts readout. The
  illumination output keeps the driver's settings (`0x10D7 = 0x1`, `0x10D8 = 0x2`).
- **Credit:** this is upstream PiTrac's sequence (`feature/pico`, `CameraTools/mira220_trigger`,
  commit `afe689e`, GPL-2.0-only). Our tools are written for this repo, not copied.

**The Pi tools,** in `Software/camera-comparison/scripts/`, written 2026-11-16
([`TRIGGER.md`](../../Software/camera-comparison/TRIGGER.md)):
- **`trigger-capture.sh`** streams the Mira220, switches it to slave after the first frame and
  prints `READY`. It then keeps each triggered frame: the raw data, a viewable `.pgm`, pixel
  statistics and the device timestamp.
  - It waits as long as it takes, because its capture runs without `--stream-poll`, which gives
    up after 2 s.
  - For the same reason `focus-preview.sh` cannot show triggered frames.
- **`mira220-trigger.sh status|slave|master`** does the switch by hand, and only while a stream
  runs. `trigger-capture.sh` calls it.
- **On the Pi, first:**
  - `command -v i2ctransfer`. If it prints nothing: `sudo apt install i2c-tools`.
  - `ls /dev/i2c-*` must list the camera's bus, the number in `mira220 <bus>-0054`. If it does
    not: `sudo modprobe i2c-dev`.
  - `id` must list `i2c`. If it does not: `sudo usermod -aG i2c "$USER"`, then log out and
    in again. `sudo` will not do for `trigger-capture.sh`: its camera lock needs your own
    runtime directory.

**Board state.** As 7c.0, with two analyser probes moved. Move them with the PSU and USB off,
after 7c.0's Finish.

| | |
|---|---|
| Image | 1.11 (board 1), no reflash |
| Pi, camera, module, J4.8 | as 7c.0: one Mira220; J4.8 in the B2/J4.7 row |
| **Trigger channel** | **Channel 5 bridged onto it:** B5 to its B pin, A5 to its A pin. It is the channel whose B pin goes to J4.3 (B1/A1 in the doc's wiring). Required since board 1's 7c.0 (2026-11-18): alone it held 1.277 V, 17 mV above VIH, and the strobe's falling edges dipped it to 1.04 V. Keep the trigger and strobe leads apart, not bundled together |
| PSU | 5.20 V, limit **≥ 5 A** |
| Beam, strobe | beam off; strobe dry; **J3 disconnected** |
| Analyser | Four channels. **Keep** two probes from 7c.0: the one on the trigger channel's camera side (A1 in the doc's wiring; analog), and the one in the **B2/J4.7 row** (digital and analog). **Move** the probe on the strobe channel's camera side (A2) to **J4.4** (digital, the trigger). **Move** the VA probe to **GPIO25 at R33** (digital; the 6a lead, the end farther from R57). Ground on **J4.5**. Name each channel in Logic 2 by what it carries. Digital on J4.4, the row and GPIO25, at ≥ 100 MS/s. Analog only on the trigger at the camera and the row, at ≥ 1 MS/s: at 250 kS/s a sub-µs dip can fall between samples (Step 0's first run, 2026-11-18). Trigger on J4.4 rising, 1 ms before and 100 ms after. A2 and VA are not probed: 7c.0 showed the strobe row follows A2 |

**Power up:** as 7c.0. PSU on, then USB, then `on`. Wait for `RUNNING` with `pi : present 1`.

**Step 0 — the trigger's level with the bridge** (added 2026-11-18).

**Do:**
1. On the Pi, from `Software/camera-comparison`, stream with the preview. After a power cycle the
   configuration must be new:
   ```
   CONFIG="$HOME/camera-focus-$(date +%Y%m%d-%H%M%S)"
   bash scripts/configure-cameras.sh --output "$CONFIG" --camera mira220 --exposure-us 1000 --gain 1 --apply &&
   bash scripts/focus-preview.sh --config "$CONFIG"
   ```
2. Run `gpio 10 1`, then take a 1 s untriggered capture.
3. Run `gpio 10 0`, then Ctrl-C the preview.

**Pass:**
- **The level:** the trigger at the camera sits at **≥ 1.40 V**, and below VA + 0.1 V.
  - About 1.49 V is predicted: 1.797 V × 12 / (12 + 2.45) kΩ. That is the two channels'
    pull-ups in parallel, each the 4.9 kΩ that 7c.0 implied.
- **The dips:** at every strobe fall it stays **≥ 1.26 V**.
- **The strobe** keeps pulsing every 11.2 ms.

**Stop if** the level is still under 1.40 V, or the dips still reach below 1.26 V. Send the
capture.

**Step 1 — switch to slave, trigger nothing.**

**Do:**
1. On the Pi, pause the desktop camera clients, as in 7c.0.
2. From `Software/camera-comparison`:
   ```
   CONFIG="$HOME/camera-trig-$(date +%Y%m%d-%H%M%S)"
   bash scripts/configure-cameras.sh --output "$CONFIG" --camera mira220 --exposure-us 1000 --gain 1 --apply &&
   bash scripts/trigger-capture.sh --config "$CONFIG" --output "$CONFIG/trig" --frames 3 --wait-seconds 1800
   ```
3. Leave it running.
4. On the board, run `pins` a few times.
5. Take a 1 s analyser capture, untriggered.

**Pass:**
- **The script switches:** it prints `slave: 0x1001 0x41 -> 0x00, 0x1003 0x08, running.`, then
  `READY at …`.
  - The first value may differ in bits other than 6 and 0.
- **No frames:** no `triggered frame` line for **10 s**.
- **The strobe idles low:**
  - `pins` shows `CAM_STROBE_0(8) 0` and `CAM_STROBE_1(9) 0`;
  - on the analyser, the B2/J4.7 row stays low for the whole second, at ≤ 0.4 V.

**Stop if:**
- **`STOPPED: frames keep coming at a free-running rate after READY`:** the switch did not take.
  Send the script's output.
- **`No answer from the Mira220`:** the message names the checks: the stream, `i2c-dev` and
  access.
- **The strobe idles high:** every `cam` command would be refused. Send `pins` and the capture.

**Step 2 — the handshake alone, three times: `cam trig 0 50000`.**

**Why these values:**
- First delay 0: FIRE comes as soon as both strobes are seen.
- Timeout 50 ms, the longest the firmware allows, so a slow strobe is still measured.
- The trigger stays high until FIRE + 1 ms, so each exposure is about `t_cam` + 1 ms.

**Do:** three times, a few seconds apart:
1. Arm the analyser on J4.4 rising.
2. Run `cam trig 0 50000`.
3. Save the capture.
4. Wait for the Pi's `triggered frame N arrived`.

**Expect one of three:**
1. **The strobe answers.**
   - The board prints `handshake: both strobes high at t_cam = … us`, then `AFTER the first
     delay (0 us): FIRE at once`.
   - `trigger released at …` is FIRE + 1000 µs.
   - One triggered frame arrives on the Pi.
2. **`handshake: TIMEOUT`.** The trigger dropped at 50 ms, and `CAM_TIMEOUT` latched.
   - The sensor probably still exposed for those 50 ms, so a frame may still arrive.
   - Send `fault clear`. In RUNNING it prints `cleared` and changes nothing else.
   - **This means ILLUM TRIG does not pulse in slave mode, or not within 50 ms.**
   - Save the analyser capture: does the strobe row move at all, for example when the trigger
     falls?
   - Then stop 7c.1. The illumination output needs DS000642's register descriptions.
3. **No frame on the Pi at all,** whatever the board printed. The sensor did not take REQ EXP.
   Check A1's high on the analyser (≥ 1.26 V, 7c.0 Step 2), and send everything.

**Stop if `fault clear` prints `acknowledging FAULT: the rail drops`.** The board was in a power
fault, not a camera timeout, and acknowledging it cuts the Pi. Report it.

**Pass:** case 1, all three times, with:
- **Frames:** one `triggered frame` per `cam trig`, and no others.
- **`t_cam`:** the analyser's J4.4 rise to B2/J4.7 rise agrees with the printed `t_cam` within
  **±0.1 µs**. In 7b, `t_cam` read 44–63 ns above the analyser.
- **The module:** A1 rises with J4.4, to ≥ 1.26 V on its analog trace. The module adds
  nanoseconds.
- **The release:** J4.4 falls at the printed `trigger released at`, within ±0.1 µs.
- **No burst:** no GPIO25 edge. `cam trig` fires no burst.

**Record,** for each shot:
- `t_cam`;
- the strobe's width, and whether it ends when J4.4 falls or by itself;
- the frame's `mean` from the script.

The first frame after the switch may look different from the rest, for example dark or
striped. Upstream discards it. Record it; it is not a fail.

**When the third frame arrives,** the script ends by itself. Its summary shows
`triggered_frames: 3`, `free_running_after_ready: no` and `status: complete`. Copy
`$CONFIG/trig/trigger-0*.pgm` to the PC to look at them: GIMP and IrfanView read PGM.

**Step 3 — `cam shot` with the dry burst, at 50, 200 and 300 µs.**

**Do:**
1. On the Pi, start a new capture for nine frames:
   ```
   bash scripts/trigger-capture.sh --config "$CONFIG" --output "$CONFIG/shot" --frames 9 --wait-seconds 1800
   ```
2. Wait for `READY`.
3. With the analyser armed each time, run `cam shot 50` three times, `cam shot 200` three
   times, then `cam shot 300` three times.

**Pass, every shot:**
- **`t_cam`:** printed, and within ±0.1 µs of the analyser, as in Step 2.
- **FIRE:** at whichever is later, the first delay − 0.007 µs or `t_cam` + 0.087 µs (as in 7b).
- **The burst:** the first GPIO25 rising edge comes **4–5 µs after FIRE**. Then 10 pulses of
  14 µs, 610 µs apart, as in 7b.
- **The trigger:** J4.4 stays high until after the burst's last pulse.
- **Frames:** one triggered frame per shot. The summary shows `triggered_frames: 9`.

**Record** each frame's `mean` against Step 2's.
- The exposure here is about 5.6–5.9 ms, against about `t_cam` + 1 ms in Step 2.
- If the exposure follows REQ EXP, the means differ by about that ratio. It is less if the
  frames saturate: see `saturated_fraction`.

**Finish:** as 7c.0. If a capture is still waiting, Ctrl-C ends it and keeps what arrived. The
stream stops with it, and the next stream starts free-running.

**Exit criteria for 7c.1:**
- no frames without a trigger, and one frame per trigger;
- `t_cam` on 12 handshakes, with its range recorded, and the firmware within ±0.1 µs of the
  analyser;
- every burst 4–5 µs after FIRE;
- the strobe's polarity, delay and width in slave mode recorded.

**Then:** the measured `t_cam`, about 0.2 µs on board 1, replaces the 100–300 µs assumption in
`board.h` (the next image). In the geometry check (7c.3) it is now negligible: 90 m/s × 0.24 µs ≈
0.02 mm.

### 7c.2 — The second camera, through its own module

Written 2026-11-20, after the owner chose a TXS0108E per camera (`PROGRESS.md` §6, 2026-11-20).

**What it proves:**
- **The second module isolates its camera.** With camera 1 off, a trigger high reaches nothing
  on its side. That is why each camera has its own module.
- **Camera 1's levels:** its trigger clears VIH through its own bridged channel, and its strobe
  reaches GPIO9 at 3.3 V. The two strobes are not crossed.
- **Two cameras in slave mode:** no frames without a trigger, and one frame on each per trigger.
- **The handshake with two strobes:** it waits for both, so `t_cam` is the later of the two.
  Each camera's delay, and the skew between them, are new numbers.

**No firmware change.** J4.3 and J4.4 carry the same trigger (GPIO10), J4.7 and J4.8 the two strobe
inputs (GPIO8, GPIO9), and the handshake already waits for both (`camera.c`). Until now, J4.8 shared
the J4.7 row so both inputs saw camera 0.

**Board state.** Fit every lead, cable, module and probe with the PSU and USB off.

| | |
|---|---|
| Image | 1.11 or 1.12 (board 1), no reflash. 1.12 only drops the `J4: NO CAMERA` printouts |
| Pi | as 7c.1, with `Software/camera-comparison` copied to it again: the two-camera tools (2026-11-20) and the fixed `trigger-capture.sh`. Its tests run without hardware; run them first (Step 0) |
| Camera 0 | as 7c.1: on the Pi's **CAM/DISP 1**, behind module #0 with channel 5 bridged |
| Camera 1 | the second Mira220, on the Pi's **CAM/DISP 0** |
| Module #0 | as 7c.1, except **J4.8 leaves the B2/J4.7 row**: it goes to module #1's B2 |
| Module #1 | a second TXS0108E, wired as module #0 but for camera 1 (`WIRING_DIAGRAM.md` §7.2). VB ← **J5.2** (a second lead from that pin); VA and OE ← camera 1's **P1.5**; GND ← **J4.6** and camera 1's **P1.1**. B1 ← **J4.4**, A1 → camera 1's **P1.7**, with **B5 bridged to B1 and A5 to A1**. A2 ← camera 1's **P1.6**, B2 → **J4.8**. Camera 1's P1.2 stays unconnected |
| PSU | 5.20 V, limit **≥ 5 A** |
| Beam, strobe | beam off; strobe dry; **J3 disconnected** |
| Analyser | Four channels. ch0 digital on **J4.4** (the trigger). ch1 digital in the **B2/J4.7 row** (camera 0's strobe). ch2 digital **and** analog on **J4.8** (camera 1's strobe). ch3 analog on module #1's **A1** (camera 1's trigger at the camera). Ground on **J4.5**. Digital at ≥ 100 MS/s, analog at ≥ 1 MS/s. Name each channel in Logic 2 by what it carries |

**Power up:** PSU on, then USB, then `on`. Wait for `RUNNING` with `pi : present 1`.

**Step 0 — the Pi: both cameras, configured.**

**Do:**
1. On the Pi, from `Software/camera-comparison`, run the tool tests:
   ```
   bash tests/test-trigger.sh && bash tests/test-trigger-capture.sh &&
   python3 -m unittest conversion.test_triggered_pairs conversion.test_triggered_frames
   ```
2. In `/boot/firmware/config.txt`, the Pi 5 section must hold both overlays: `camera_auto_detect=0`,
   `dtoverlay=mira220-nir,cam0` and `dtoverlay=mira220-nir`. If you add one, back the file up
   first, then `sudo reboot` (on a seated Pi it keeps the rail up, `BENCH_P8_PI.md` 8.6).
3. List the sensors:
   ```
   for m in /dev/media*; do media-ctl -d "$m" -p 2>/dev/null | grep -o 'mira220 [0-9]*-0054'; done
   ```
4. Pause the desktop camera clients (`QUICKSTART.md`). Configure each camera into its own
   directory. `B0` and `B1` are the buses from step 3: camera 0's is the one it has used since 7c.0
   (`mira220 11-0054` on board 1, the CAM/DISP 1 bus):
   ```
   B0=11 B1=<the other bus>
   CONF0="$HOME/camera-c0-$(date +%Y%m%d-%H%M%S)" CONF1="$HOME/camera-c1-$(date +%Y%m%d-%H%M%S)"
   bash scripts/configure-cameras.sh --output "$CONF0" --camera mira220 --bus $B0 --exposure-us 1000 --gain 1 --apply &&
   bash scripts/configure-cameras.sh --output "$CONF1" --camera mira220 --bus $B1 --exposure-us 1000 --gain 1 --apply
   ```

**Pass:**
- the tests end `checks passed` (twice) and `OK`;
- two sensors listed, `mira220 <bus>-0054` each;
- both configurations end `Configured: …`.

**Stop if** only one sensor is listed. Check the second overlay line, then the ribbon (power off).

**Step 1 — module #1 isolates camera 1 while it is off.**

**Do:**
1. Stream camera 0 alone: `bash scripts/focus-preview.sh --config "$CONF0"`. Camera 1 stays idle:
   the driver powers a camera only while it streams.
2. On the board, `gpio 10 1`. Take a 1 s capture. Then `gpio 10 0`.
3. Ctrl-C the preview.

**Pass:**
- **Isolation:** with J4.4 high (ch0), camera 1's trigger (ch3) stays **below 0.2 V**.
- **J4.8** (ch2) stays low, **≤ 0.4 V**: camera 1 is off.
- **J4.7** (ch1) pulses every 11.2 ms: camera 0's strobe, free-running.

**If J4.8 pulses and J4.7 does not,** the buses are the other way round: `$CONF0` is camera 1.
Send `gpio 10 0`, Ctrl-C, swap `B0` and `B1`, configure into new directories, and repeat Step 1.

**Stop if** ch3 rises with J4.4 while J4.8 stays low. Module #1 is passing the trigger to an
unpowered camera: send `gpio 10 0`, then `off`, and check that its VA and OE go to camera 1's
P1.5 only.

**Step 2 — camera 1 alone: its trigger level, and its strobe on J4.8.**

**Do:**
1. Stream camera 1 alone: `bash scripts/focus-preview.sh --config "$CONF1"`.
2. `gpio 10 1`, then a 1 s capture. `gpio 10 0`, then a 1 s capture.
3. Ctrl-C the preview.

**Pass:**
- **Camera 1's trigger** (ch3), with GPIO10 high: **≥ 1.40 V**, and no higher than 1.90 V. At every
  strobe fall it stays **≥ 1.26 V**. Camera 0's bridged channel measured 1.490 V (7c.1 Step 0).
- With GPIO10 low: **below 0.54 V**.
- **Camera 1's strobe:** J4.8 (ch2) pulses every 11.2 ms, **≥ 2.5 V** high and **≤ 0.4 V** low.
- **Not crossed:** J4.7 (ch1) stays low throughout, with camera 0 off.

**Stop if** the trigger is under 1.40 V or a dip goes below 1.26 V. Check module #1's channel 5
bridge.

**Step 3 — both cameras in slave mode, nothing triggered.**

**Do:**
1. Stream both and switch both:
   ```
   bash scripts/trigger-capture.sh --config "$CONF0" --config "$CONF1" --output "$CONF0/trig2" --frames 3 --wait-seconds 1800
   ```
2. Leave it running. On the board, run `pins` a few times. Take a 1 s capture, untriggered.

**Pass:**
- **Both switch:** two `slave: 0x1001 0x41 -> 0x00, 0x1003 0x08, running.` lines, then
  `READY at …`.
- **No frames:** no `triggered frame` line for **10 s**.
- **Both strobes idle low:** `pins` shows `CAM_STROBE_0(8) 0` and `CAM_STROBE_1(9) 0`, and ch1 and
  ch2 stay low for the whole second.

**Stop if:**
- **`STOPPED: bus<N>: frames keep coming at a free-running rate`:** that camera's switch did not
  take. Send the output.
- **`The switch to slave mode failed for mira220 <N>-0054`:** see that camera's `switch.log`.

**Step 4 — the handshake alone, three times: `cam trig 0 50000`.**

**Do:** three times, a few seconds apart: arm the analyser on J4.4 rising; `cam trig 0 50000`; save
the capture; wait for a `triggered frame` line from **each** camera.

**Pass, every time:**
- **The board** prints `handshake: both strobes high at t_cam = … us`, then `FIRE at once`.
- **Each camera's delay:** from J4.4's rise to J4.7's rise (camera 0), and to J4.8's rise (camera 1).
  Both about 0.2 µs, as in 7c.1.
- **`t_cam` is the later of the two,** within **±0.1 µs** of the analyser.
- **Camera 1's trigger** (ch3) rises with J4.4 and stays **≥ 1.26 V** until it falls.
- **Frames:** one per camera per `cam trig`, and no others.

**Record,** each time: both delays, the skew between them, and the printed `t_cam`.

**When the third pair arrives,** the script ends by itself.
- Each camera's summary shows `triggered_frames: 3` and `free_running_after_ready: no`.
- The pair summary shows `pairs: 3`, `unpaired_bus<N>: 0` for both, and `status: complete`.
- `pairs.tsv` gives each pair's frame times and means.

**Step 5 — `cam shot` with the dry burst: 50, 200 and 300 µs, three times each.**

**Do:**
1. Start a capture for nine frames:
   ```
   bash scripts/trigger-capture.sh --config "$CONF0" --config "$CONF1" --output "$CONF0/shot2" --frames 9 --wait-seconds 1800
   ```
2. Wait for `READY`. With the analyser armed each time, run `cam shot 50` three times, `cam shot 200`
   three times, then `cam shot 300` three times.

**Pass, every shot:**
- **`t_cam`:** printed, the later strobe, within ±0.1 µs of the analyser.
- **The trigger** (ch0) stays high until the printed `trigger released at`, after the burst.
- **Frames:** one per camera per shot. The pair summary shows `pairs: 9` and no unpaired frames.

**Record** each pair's `mean` from `pairs.tsv`. The two cameras see the same scene from the same
spot, so their means differ only by the sensors and lenses.

**Finish:** as 7c.0. Ctrl-C ends a capture that is still waiting, and keeps what arrived.

**Exit criteria for 7c.2:**
- module #1 keeps camera 1's trigger below 0.2 V while camera 1 is off (Step 1);
- camera 1's trigger ≥ 1.40 V high, dips ≥ 1.26 V and < 0.54 V low; its strobe ≥ 2.5 V on J4.8,
  and the strobes not crossed (Step 2);
- no frames without a trigger on either camera, and every trigger paired: 12 pairs (Steps 3–5);
- `t_cam` the later strobe, within ±0.1 µs, on 12 handshakes; each camera's delay and the skew
  recorded.

**Then:** record which I²C bus is camera 0 and which is camera 1 (`WIRING_DIAGRAM.md` §6.2).

### 7c.3 — The geometry check (desk work, after 7c.1): PASS 2026-11-20

Check `beam_to_fov_mm > (D+w)/2 + v·(t_fixed + t_cam)` with 7c.1's largest `t_cam`, at the
fastest ball speed. Nothing is wired or powered: it is arithmetic on 7c.1's `t_cam` and the
stand's geometry ([`../Stand/README.md`](../Stand/README.md) §1 and §4).

**Inputs:**
- `beam_to_fov_mm` is measured from the light sheet's centre, X = 65 mm.
- D = 42.67 mm and w = 30 mm (the owner: about 3 cm), so (D+w)/2 = 36.3 mm.
- `t_cam` = 0.24 µs, 7c.1's largest on the firmware: 0.02 mm at 100 m/s.
- v = 100 m/s, the design's `v_max_mps`.
- `t_fixed` has two parts on the planned shot path (`SHOT_PATH_PLAN.md` §2a, D2 (A)):
  - detection: the design's 32 µs, not yet measured on this firmware (WP0.1);
  - the live readback's baseline: ADC0 has written its 100 samples **198–200 µs** after the
    switch to BURST, on every live firing on board 1 (6c, 2026-10-12 and 13).
- **An upper limit the formula leaves out:** the whole ball must be in CAM0's view. Its centre
  must be at X ≤ 272.3 mm, so `beam_to_fov_mm` ≤ 207 mm.

**Result at 100 m/s:**

| `t_fixed`, trailing edge to the first flash | First freeze, past the sheet centre | Whole-ball freezes in CAM0's view, at 42.67 / 47.33 mm spacing |
|---|---|---|
| The design doc's 32 µs | 39.6 mm (the floor) | 4 / 4 |
| 32 µs + the 199 µs baseline = 231 µs | 59.5 mm (the floor) | 4 / 4 |
| The design doc's fixed 150 mm | 150 mm | 2 / 2 |

- **PASS in every case:** each floor is well under the 207 mm limit.
- **What is left open is how many freezes fit.** That is set by the first-flash latency
  ([`SHOT_PATH_PLAN.md`](SHOT_PATH_PLAN.md) D1, D2) and the spacing:
  - 4 freezes at 100 m/s need the first flash within **289 µs** of the trailing edge at
    47.33 mm spacing, or within 429 µs at 42.67 mm;
  - after the 199 µs baseline, that leaves 90 µs or 230 µs for detection and the CPU;
  - 3 freezes need it within 763 µs (47.33 mm) or 856 µs (42.67 mm).
- **Still estimates:**
  - The field of view is a first-order pinhole, for CAM0 alone, with the 1600-pixel side
    horizontal. If that side is vertical, the view is 12 % narrower and about one freeze is
    lost. A frame of a ruler at the ball line, from a camera in the stand, would replace the
    estimate.
  - Detection's 32 µs is the design's figure. WP0.1 measures this firmware's.

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

✅ **The last unknown is answered (owner, 2026-11-11): the camera board does not
level-shift.** Until the next revision, J4 reaches a camera only through a TXS0108E module
(CR-09; `BRINGUP_NEW_BOARD.md` Step 13.4).

**The loopback test in §7a is unaffected** — it jumpers J4 to itself with no camera present,
so it stays valid and is still the right first step.

---

### (original wording, retained)

Q6 (Mira220 digital I/O possibly 1.8 V, while J4 drives 3.3 V through 220 Ω) is **the only
damage-class open question left in the whole plan** — every other outstanding item is a
measurement whose worst case is a wrong number. It costs nothing to settle from the
datasheet now, and the failure mode is a dead sensor. Do it before Phase 5 rather than at
Phase 7c.
