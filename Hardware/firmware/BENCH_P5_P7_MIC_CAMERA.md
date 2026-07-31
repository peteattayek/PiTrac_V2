# Phases 5 & 7 — Microphone and cameras

Two independent phases sharing a document because neither is large.

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

## Bring-up

```
adc 7 256                      # expect ~2048 / ~1.65 V, quiet room
capture 0x80 8000 250000       # 32 ms window
python tools\scope.py --port COM7 --channel 7 --samples 8000 --volts
```

| Test | Expect |
|---|---|
| Quiet room | flat at mid-scale, small noise band |
| Clap | sharp transient, decaying ring |
| Tap on the enclosure | strong, structure-borne |
| Ball into a net near the board | the real signal — capture several for reference |

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

## ⚠ 7.0 — Resolve the 1.8 V I/O question FIRST

**This can damage hardware and must not be discovered empirically.**

J4 drives 3.3 V logic through only 220 Ω into what may be a 1.8 V sensor domain. Get the
specific sensor-board schematic and establish what sits between the module header and the
sensor pins — level translation, series protection, or nothing.

Two directions, both need answering:

| Direction | Question |
|---|---|
| **Out** (D_Cam_Trigger) | Will 3.3 V through 220 Ω damage a 1.8 V input? Budget for a divider or level shifter. |
| **In** (Cam_Strobe 0/1) | Does the sensor's strobe output swing high enough? **A 1.8 V output will not register without translation.** RP2350 V_IH is **2.0–2.31 V** on a 3.3 V rail depending on which spec line you take (a flat 2.0 V, or 0.7 × VDD = 2.31 V). Use **2.31 V** as the design floor — it is the conservative reading and it is what `PROGRESS.md` Q10 uses, so the two docs agree. 1.8 V fails against either. |

Do not connect J4 to a camera until both are answered.

## 7a — Loopback, no Pi, no cameras

**Jumper J4.3 (D_Cam_Trigger) → J4.7 (Cam_Strobe_0) and → J4.8 (Cam_Strobe_1).**

This simulates a camera whose shutter opens instantly, and exercises the entire
`wait_both(...)` handshake, the `t_cam` measurement, the timeout/abort path, and the
FIRING state machine — with zero Pi and zero cameras.

| Test | Method | Pass |
|---|---|---|
| Handshake completes | both jumpers fitted | FIRING proceeds, `t_cam` ≈ 0 |
| Timeout path | remove one jumper | `CAM_TIMEOUT` fault, trigger drops, no burst |
| Trigger polarity | scope J4.3 | rising edge on request, falling on release |

## 7b — Delayed-response simulation

Drive Cam_Strobe_0/1 from a function generator or a spare Pico with a programmable
**50 / 150 / 300 µs** delay after the trigger edge.

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
