# Phases 3 & 4 — Photodiode detection and trigger selection

**Prereq:** Phase 2 complete, carrier frequency and clamp both measured.
**Power:** PSU 5.2 V / 2.5 A. **Pi:** not connected.
**Gear:** scope, DMM, plus a golf ball and a piece of cardboard (see §3.7).

Phases 3 and 4 are one continuous piece of work — 4 is an experiment run on the
apparatus 3 builds — so they share a document.

---

## The signal chain, and where to look

```
D12 (36 V bias) → U11A TIA (Rf 470k, 0.47 V/µA, inverting) ────────────→ TP7
     → U13/U12A sign-switching demod (phase-locked to carrier) ────────→ TP10
     → 4th-order LPF, f0 15.9 kHz, gain 2 ──────────────────────────────→ TP9
     → C81 + gated HPF (τ 0.66 s, or HOLD) → U12B ×14.5 ───────────────→ ADC5
     → LM393 vs Threshold_DC (TP8) ────────────────────────────────────→ GPIO46
```

**TP9 is the best single scope point for detection.** A ball transit there is a smooth
positive bump: amplitude = 2× the TP10 shift, width = the transit time, carrier absent
(−64 dB), edges shaped by the 15.9 kHz filter.

**Everything from TP6 through TP10 idles at the virtual ground, ≈2.59 V** — that is
+5VA/2, not a regulated 2.5 V. Ignore every "2.50 V" in the .md (see Q8).

**ADC5 is different: it idles near 0 V**, because it sits after the AC-coupling capacitor
and the ×14.5 stage is referenced to *ground*, not to the virtual ground. A 100 mV bump at
TP9 should appear as ~1.45 V at ADC5.

---

## 3.1 ADC allocation

One SAR, 500 ksps aggregate. `adcmode` switches:

| Mode | Channels | Rate each | When |
|---|---|---|---|
| IDLE | 1, 2, 5, 7 | 125 ksps | disarmed, health monitoring |
| **ARMED** | **5, 7** | **250 ksps** | detect + mic, both free-running |
| BURST | 0 | 500 ksps | strobe window only |

Never sample ch3 (GPIO43, RPI5_SHUTDOWN) or ch4 (GPIO44, Threshold_PWM) — both are
digital outputs on this board. The firmware rejects them.

> ### 🔴 Do this before starting Phase 3 — finding A1
>
> **The supply monitor currently tears down the ADC ten times a second.**
> `adc_read_avg()` stops the ADC, drains the FIFO, clears round-robin, polls 256
> conversions in a blocking loop, then restarts the previous mode — and the power
> FSM calls it every 100 ms.
>
> That is survivable today. It is **not** survivable in Phase 3, where ADC5 must
> free-run continuously into a DMA ring so a comparator edge has pre-trigger
> history to look back at. A monitor that restarts the ADC ten times a second will
> punch holes in that ring and rotate the round-robin channel phase, and the
> symptom will be intermittent missing samples that look like an analog fault.
>
> **Fix first:** make IDLE and ARMED free-run into a continuous DMA ring, and have
> the monitor average ch1 samples already in the ring rather than commanding its
> own conversions. Zero disruption, near-zero CPU. Keep stop-and-poll only for the
> CLI's one-shot `adc <ch>`.
>
> See `ARCHITECTURE.md` A1. Much cheaper to fix now than to debug later.

---

## 3.2 Static health, beam OFF — do this before anything else

```
on
adcmode idle
adc 2 256          # TIA_Out
```

| Point | Expect |
|---|---|
| TP6 (+2V5) | 2.59 V ±0.05 |
| **TP7 (TIA_Out)** | **2.59 V regardless of ambient light** — the 2.27 Hz DC servo nulls it |
| TP9, TP10 | 2.59 V |
| ADC2 | should agree with TP7 on the scope |

**If TP7 is pinned at a rail, stop.** Either ambient photocurrent exceeds the servo's
±25 µA null range (shade the photodiode and retry) or there is a bias fault — check R77,
VIR at J3, and C67.

All four agreeing is the pass criterion, not the absolute number. Divergence localises
the fault: TP7 off alone → servo or bias; TP9 ≠ TP10 → an LPF stage.

---

## 3.3 Beam ON, no target

```
beam duty 30
beam on
```

TP7 should now show the carrier riding on 2.59 V from direct optical crosstalk (LED →
photodiode near-field leakage). Light pulls the node **down** — the TIA is inverting,
0.47 V/µA.

**Reading the carrier on ADC2 is limited by sample rate.** At 104 kHz and 500 ksps you get
~4.8 samples per period; you cannot digitize the waveform. What you *can* do:

```
capture 0x04 4000 500000     # ch2, free-running
```
and take min/max/RMS over many periods — that gives carrier amplitude without needing
coherence, which is enough for a health check.

---

## 3.4 Demod phase calibration

> **⚠ The .md's `cal_demod_phase()` (§13.8) does not work as written.** It sweeps phase
> while sampling ADC5 with a *static* reflector. ADC5 sits after the 0.66 s gated HPF, so
> a static reflector produces **no signal there** in track mode — the sweep reads noise at
> every phase. Use one of the methods below instead.

**Setup:** a static reflector — a white card or a golf ball on a stand — at the operating
distance.

### Method A (preferred): chopped beam

HPF in **track** mode (`gpio 33 1`). Chop the carrier on/off at ~5 Hz, well inside the HPF
passband. Sample ADC5 synchronously and compute `mean(beam on) − mean(beam off)` over ~10
cycles. That differential ∝ cos(phase error), and it rejects ambient drift for free.

Sweep `beam phase` in TOP/64 ≈ 22-tick steps across 0..1439, take the argmax.

*(Firmware helper `cal demod` is not written yet — it lands with the Phase 3 code. Until
then this is doable by hand: `beam off` / `beam on` with `capture 0x20 …` either side.)*

### Method B (quick): frozen HPF

`gpio 33 0` (HOLD) with the reflector present, then sweep phase. Held DC shifts do reach
ADC5. Faster, but easy to rail — the ×14.5 stage clips at ΔTP9 ≈ 228 mV.

**Sanity check either way:** the response should fall to ~0 at +90° from the peak
(quadrature null). If it does not, you are not seeing the real lock-in response.

Record `demod_phase_ticks` in `PROGRESS.md` §6.

---

## 3.5 Threshold DAC and comparator cross-calibration

Threshold_PWM (GPIO44) → two 1 ms RC poles → Threshold_DC at **TP8** = 3.3 V × duty.
Use TOP = 1023 → 146.5 kHz, 3.2 mV steps. Settle 10 ms after any change.

**Self-test needing no external gear:** sweep the threshold duty while watching ADC5 and
GPIO46 together, and find the duty where D_Comparator flips. That single measurement
cross-calibrates the threshold DAC against the ADC5 scale and proves both paths including
the R102/D14 clamp.

**Watch for:** the 146.5 kHz DAC carrier is only 42 kHz from the 104.17 kHz optical
carrier. The two 1 ms RC poles attenuate it by ~120 dB so nothing should escape the node,
but PCB crosstalk from the GPIO44 trace is possible. **Empirical check:** with the beam
running and no target, step the threshold across its range and confirm TP9/ADC5 does not
move. If it does, change the DAC frequency (TOP 2047 → 73 kHz, or 511 → 293 kHz).

---

## 3.6 `scan carrier` — the real answer to the frequency question

104.1667 kHz is a **starting point**, not an answer. The LM5157 boost runs at a nominal
1.055 MHz with real tolerance, and a square-wave demodulator folds interference near every
**odd** harmonic n·f_c down to |f_i − n·f_c|. Anything under ~16 kHz lands in the LPF
passband and looks exactly like a ball.

For each candidate frequency, measure three things:

1. **Noise:** beam ON, no target, HPF tracking → σ of ADC5 over 100 ms
2. **Floor:** beam OFF → σ of ADC5 (separates ambient/boost noise from beam-induced)
3. **Signal:** chopped beam with a static reflector → differential amplitude

Report **SNR = signal / σ_noise** per frequency and **pick the carrier by maximum measured
SNR.** Run the scan twice — boost loaded and unloaded — and once in daylight, once dark.

This is worth more than any amount of arithmetic about harmonics. Record the winner and
put it in `board.h`.

---

## 3.7 Ball transit — use a ramp, not your hand

```
adcmode armed
capture 0x20 8000 250000     # ADC5 at 250 ksps ≈ 32 ms window
```

**A ball rolling from height *h* on a cardboard ramp arrives at**

```
v = sqrt(2 · g · h · 5/7)        (5/7 accounts for rolling inertia)
```

That is an independent speed reference costing one piece of cardboard, and it is how you
validate the firmware's speed math rather than eyeballing it. Hand-waving gives you
repeatability you cannot measure.

Set `v_min` to 0.3 m/s for bench work — the production 2.0 m/s rejects everything you can
do by hand or ramp.

**Log per pass:** comparator rise/fall timestamps, transit µs, computed v, and the ADC5
waveform.

### Exit criteria
- TP9 shows a clean smooth positive bump per pass
- D_Comparator gives one clean pulse pair per pass, no chatter
- Computed speed matches the ramp prediction within a few percent

---

# Phase 4 — Trigger source: comparator, ADC, or both

**The recommendation is both, and it costs nothing.** Here is the reasoning.

### Why the comparator alone is biased

A fixed threshold crosses the signal's rising slope at a point that depends on the
signal's **amplitude**. A dimmer or more distant ball makes a smaller bump, so the
threshold is crossed later going up and earlier coming down → **measured transit is short
→ speed is overestimated, and the error scales with reflectance.** That is a systematic
bias, not noise; averaging will not remove it.

### Why the ADC alone is too slow

At 250 ksps you have 4 µs granularity and need several samples past the peak before you
can compute "50 % of peak" — ~10 µs of added latency versus the comparator's ~1 µs, plus
a core spent polling.

### The arrangement that gets both

1. Comparator rising edge → transit timing starts. ~1 µs, deterministic, zero CPU.
2. Comparator falling edge → provisional transit and v; go to FIRING immediately.
3. Raise D_Cam_Trigger, wait for both Cam_Strobe edges. **That wait is 100–300 µs of
   measured latency you are spending anyway.**
4. *During that wait*, post-process the ADC5 ring: find the bump peak, compute the
   50 %-of-own-peak crossings on both edges, recompute v. Amplitude-independent. Commit
   the burst schedule from the refined value.

> **Implement steps 1–2 as a PIO state machine, not a GPIO ISR** (`ARCHITECTURE.md` A2).
> One SM that waits for the rising edge, counts at 1 MHz to the falling edge, and pushes
> the count gives you the transit **directly as one FIFO word** — no interrupt handler, no
> jitter from a USB or UART IRQ landing at the wrong moment, no risk of a flash-XIP stall
> in the handler. Core 1 blocks on the FIFO and does the arithmetic afterwards.
>
> We have 8 free state machines across three PIO blocks. There is no reason to spend a
> core on edge timing.
>
> Same argument applies to step 3 — see `BENCH_P5_P7_MIC_CAMERA.md` §7b and A3. The
> `wait_both()` busy-wait in the .md pseudocode should be a PIO handshake that returns
> `t_cam` as a measured count.

You get the comparator's latency and the ADC's accuracy for free, because the camera
handshake donates the time. The ADC path also gives pulse-shape validation (reject
insects, hands and noise by width and symmetry) and the data to auto-tune the threshold
between shots.

### The experiment that proves it

At a **fixed ramp height** (so v is known and repeatable), record 20 passes under each of
three conditions:

| Condition | Purpose |
|---|---|
| Ball at nominal distance | baseline |
| Ball at ~1.5× distance | weaker return |
| Lower-reflectance target (grey card, scuffed ball) | weaker return, different spectrum |

For each pass log **both** the comparator-derived and ADC-derived transit.

**Deliverable:** a table of mean and σ per method per condition, plus the measured
amplitude-dependent bias of the comparator method. That table decides the final design —
and if the bias turns out negligible at your geometry, you get to simplify and drop the
ADC refinement.
