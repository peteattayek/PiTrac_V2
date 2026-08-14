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

> ### ✅ A1 is fixed (2026-08-14) — the ring is already running
>
> `adc_read_avg()` used to stop the ADC on every call, and the power FSM called it 10× a
> second. That would have punched holes in the pre-trigger history this phase depends on.
>
> **Now:** IDLE, ARMED and BURST all free-run into a **32 KB DMA ring** that never stops —
> one channel in RP2350 ENDLESS mode with a hardware write-address wrap, zero CPU.
>
> | | |
> |---|---|
> | Pre-trigger history | **32.8 ms per channel**, every mode |
> | Covers a transit down to | **~1.4 m/s** (production `v_min` is 2.0 m/s) |
> | Slower bench balls | use `capture` instead — see 3.7 |
>
> **The API this phase wants:**
>
> ```c
> size_t adc_ring_history(unsigned chan, uint16_t *dst, size_t n);  // newest first
> bool   adc_ring_avg(unsigned chan, unsigned n, uint16_t *out);
> ```
>
> `adc_ring_history(ADC_CH_DETECT, ...)` after a comparator edge is how §4's
> amplitude-independent refinement gets its data — pull the bump back out of the ring and
> find its own 50 %-of-peak crossings.
>
> ⚠ **`adc <ch>` and `capture` still stop the ring** — they are bench commands and are
> documented as disruptive. Never call `adc_read_avg()` from the armed or firing path.
>
> ⚠ **ch1 (+5V_IN) is not in the ARMED set**, so while armed the supply reading is *held*,
> not refreshed. `stat` shows it as `HELD` with an age. This is deliberate: the flow
> ARMED → BURST → IDLE re-checks it after every shot, since IDLE is `{1,2,5,7}`.

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

> ### ⚠ Warm the beam up before calibrating anything
>
> **Measured 2026-08-13:** from cold to thermal plateau the LED's junction rises ~76 K, and
> radiant efficiency for 850 nm AlGaAs falls roughly **0.3–0.6 %/K** — so optical output drops
> on the order of **25–45 %** between switch-on and steady state.
>
> **None of that is visible electrically.** Over the same interval current moved +1.7 % and
> power +0.6 %, so voltage, current and duty all report "nothing happened." Only the light
> changes.
>
> **Consequence: let the beam reach thermal plateau (~5 min, τ ≈ 70 s) before running
> `scan carrier`, the threshold sweep, or the phase calibration.** A carrier and threshold
> chosen against a cold LED will be working with substantially less signal a few minutes into
> an armed session — and the discrepancy will look like drift or a detection fault rather than
> a calibration error.
>
> Record whether the chosen SNR figures were taken warm. See `PROGRESS.md` §6 and
> `NEXT_BOARD_REV.md` CR-12.

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

## 3.6b Q8 — prove a rail step cannot look like a ball

**Do this before you trust a single transit.** If the answer is bad, every measurement in
3.7 is contaminated by an artifact you have not yet characterised.

### The mechanism

`+2V5` is **not** a regulated reference — R75/R76 (10K/10K) buffered by U11C make it literally
**+5VA ÷ 2**, measured at 2.59 V on the 5.2 V rail. So the whole chain's reference **moves with
the rail**:

```
ΔV on +5V  →  ΔV/2 at virtual ground  →  through C81  →  U12B x14.5  →  ADC5
```

**Predicted coupling: ΔADC5 = ΔV_rail × 7.25.** A **100 mV** rail step becomes **725 mV** at
ADC5 — comfortably over a typical 0.1–0.5 V threshold, i.e. **a false trigger caused by a
power event.**

In **TRACK** mode the 0.66 s HPF removes it. In **HOLD** it does not. **Armed means HOLD**,
so the vulnerable state is exactly the operating state.

### The test

Use the beam as the load step — at 30 % duty it is ~0.95 A, the largest thing you can switch.
**No target present**, so the only optical change is crosstalk.

```
on
beam freq 104166
beam duty 30
adcmode idle
gpio 33 1          # HPF TRACK
```

Scope **+5 V** and **ADC5** together, then toggle `beam on` / `beam off` and capture both.

| Step | HPF | What to record |
|---|---|---|
| 1 | `gpio 33 1` (**TRACK**) | ΔV at +5 V, and the ADC5 excursion. The HPF should remove most of it. |
| 2 | `gpio 33 0` (**HOLD**) | Same step. **This is the armed case.** |
| 3 | — | Coupling ratio = ΔADC5 / ΔV_rail. **Compare against the predicted 7.25.** |

**The TRACK-vs-HOLD difference is the Q8 effect isolated** — same stimulus, same optical
conditions, only the baseline freeze changes.

### Pass criteria

- Coupling in **TRACK** is small — the HPF is doing its job.
- Coupling in **HOLD** is close to the predicted **×7.25**. If it is far off, the mechanism is
  not what the analysis says and that is worth understanding before proceeding.
- **The ADC5 excursion from a realistic rail step stays well under the comparator threshold**
  you set in 3.5. A useful bar: under **20 %** of the threshold.

### If it fails

Record the coupling ratio and the rail step that causes a trigger, then pick a mitigation:

| Mitigation | Cost |
|---|---|
| Keep the rail stiff while armed — no load switching during an armed window | Constrains what the firmware may do while armed |
| Shorten the armed window | Fewer opportunities, does not remove the mechanism |
| Do not freeze the baseline (stay in TRACK) | Loses the frozen reference the detector wants |
| **Board fix — `NEXT_BOARD_REV.md` CR-02** | Regulate +2V5, or a unity diff amp taking TP9 − TP6 |

⚠ **This gets worse in Phase 6, not better.** The strobe bursts sag the rail *deliberately* —
that is why the supply monitor carries a 500 ms debounce. A detector that false-triggers on a
100 mV step will false-trigger on its own strobe.

Record the coupling ratio in `PROGRESS.md` §6 either way. It is the number that decides
whether CR-02 is required or optional.

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
   ```c
   uint16_t hist[4096];
   size_t n = adc_ring_history(ADC_CH_DETECT, hist, 4096);  // newest first
   ```
   The ring holds **32.8 ms per channel** (A1), so the whole bump is already there for any
   transit down to ~1.4 m/s — no extra acquisition, and nothing to arm in advance.

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
