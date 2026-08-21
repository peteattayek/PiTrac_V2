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
     → 4th-order LPF, f0 15.39 kHz, gain 2 ─────────────────────────────→ TP9
     → C81 330nF + gated HPF (τ 0.66 s, or HOLD) → U12B ×14.5 ─────────→ ADC5
     → U15 LM393 vs Threshold_DC (TP8) ────────────────────────────────→ GPIO46
```

**TP9 is the best single scope point for detection.** A ball transit there is a smooth
positive bump: amplitude = 2× the TP10 shift, width = the transit time, carrier absent
(−64 dB), edges shaped by the 15.39 kHz filter.

**Everything from TP6 through TP10 idles at the virtual ground, ≈2.59 V** — that is
+5VA/2, not a regulated 2.5 V. Ignore every "2.50 V" in the .md (see Q8).

**ADC5 is different: it idles near 0 V**, because it sits after the AC-coupling capacitor
and the ×14.5 stage is referenced to *ground*, not to the virtual ground. A 100 mV bump at
TP9 should appear as ~1.45 V at ADC5.

---

> ## ⚠ Corrections from the netlist, 2026-08-14 — read before starting
>
> Reading `THE_SECOND_BOARD_TO_RULE_THEM_ALL` against this document turned up several
> things it asserts that the board does not do. Two of them change what you do at the bench.
>
> | # | This doc said | The board actually does |
> |---|---|---|
> | 1 | **R100** is the unpopulated gain option | **R98** is the DNP part. R98 and R100 are a *parallel pair* from GND to U12B's inverting input, and **R100 (2 k) is fitted**. |
> | 2 | `gpio 33 1` = TRACK | ❌ **BACKWARDS — resolved 2026-08-17.** TMUX1219 truth table: SEL=0 → S1, SEL=1 → S2, and S1 is the R96/GND leg. So **`gpio 33 0` is TRACK.** Confirmed twice: `hpf test` on hardware and the datasheet. `HPF_SEL_TRACK` is now 0. Use `hpf track` / `hpf hold`, never the raw level. |
> | 3 | The R102/D14 clamp is on the threshold node | **It is on ADC5.** `Threshold_DC` has no clamp at all — its only nodes are C76.2, R89.2, TP8.1, U15.2. |
> | 4 | LPF f0 = 15.9 kHz | **15.39 kHz** from the fitted 4.7 k / 2.2 nF. The annotation was stale. |
> | 5 | "Settle 10 ms" after a threshold change | **~13 ms is 5τ, use 20 ms.** The two RC sections *load each other*, so the real poles are 2.62 ms and 0.382 ms, not two independent 1 ms poles. 10 ms is ~4τ, leaving ~2 % ≈ 66 mV ≈ 20 threshold codes. |
> | 6 | (not mentioned) | **U15 has NO HYSTERESIS.** There is no resistor from `D_Comparator` back to pin 3 anywhere on the board. Chatter on a slow edge is expected — see §3.7. |
> | 7 | (not mentioned) | **The LM393's input common-mode ceiling is ≈ V+ − 1.5 V ≈ 3.5 V**, and U15 runs from the *digital* +5 V. U12B is rail-to-rail on +5VA and can drive pin 3 to ~5.2 V — outside the comparator's valid range on large signals. |
> | 8 | (not mentioned) | **ADC5 saturates at code 4095 (3.3 V) BEFORE D14 conducts at ~3.6 V.** Clipping appears as ADC full scale, not a diode knee. |
>
> ### The gain option, correctly stated
>
> R98 was left unpopulated so its **value** could be chosen after measuring the real signal —
> so gain is a continuous knob, not an on/off choice:
>
> ```
> G = 1 + R101 / (R100 ∥ R98)        R101 = 27 k, R100 = 2 k
> ```
>
> | R98 | R100 ∥ R98 | Gain | ΔTP9 at clip |
> |---|---|---|---|
> | absent | 2.00 k | **14.5** | 228 mV |
> | 10 k | 1.67 k | 17.2 | 192 mV |
> | 4.7 k | 1.40 k | 20.2 | 163 mV |
> | 2 k | 1.00 k | 28.0 | 118 mV |
> | 1 k | 667 Ω | 41.5 | 80 mV |
>
> **Measure at the as-built ×14.5 first**, then `cal gain <peak>` solves backwards and prints
> the nearest E24 value. Set gain from the *weakest* target that must still trigger; the
> ceiling comes from the strongest, and is `min(ADC 3.3 V, LM393 CM ≈ 3.5 V)`.

---

> ## ✅ Pre-bench netlist verification, 2026-08-14
>
> Every schematic sheet was checked before starting. Nothing here blocks bench work, but
> four of these are numbers you would otherwise derive at the bench from a wrong premise.
>
> ### Verified clean
>
> - **All 30 `board.h` pin definitions and all 5 ADC channels** were cross-checked against
>   the netlist mechanically. Every one maps to a real, named net; none maps to an
>   unconnected or missing node.
> - **DNP inventory is complete across all 10 sheets.** Exactly three electrical parts are
>   unpopulated: **R98** (the detect-chain gain option), **C9** (100 pF on VIR) and **R11**
>   (5R1, Power). Everything else flagged is a mounting hole or an off-board part. There is
>   no second tuning option hiding anywhere.
> - **Test point map** (all are bare 1.0 mm pads, "No Solder" — probe holes, nothing fitted):
>
>   | | | | | |
>   |---|---|---|---|---|
>   | TP1 GND | TP2 +12V | TP3 U7-E1 | TP4 CurrentSense_ADC | **TP5 `Strobe_GND`** |
>   | **TP6 +2V5** | **TP7 TIA_Out** | **TP8 Threshold_DC** | **TP9 LPF out** | **TP10 Demod out** |
>
>   ⚠ **TP5 is `Strobe_GND`, the shared low-side return** — it is what Phase 2 measured the
>   U9 clamp on, and in Phase 6 the strobe pulses appear there too. Phase 3 does not use it.
> - `Modulation_PWM` and `Demodulation_PWM` each carry a **1 kΩ pull-down** (R69, R91), so
>   both sit at a defined level whenever the MCU pads are high-Z.
>
> ### Four numbers that change how you read a result
>
> **1. The +2V5 divider is filtered at 31.8 Hz — the Q8 test is valid, the Phase 6
> extrapolation is pessimistic.** R75/R76 (10K/10K) have **C66 = 1 µF** on the midpoint:
> `R75∥R76 = 5 kΩ × 1 µF → τ = 5 ms, f_c = 31.8 Hz`. Then a U11C unity buffer, then
> R79 10 Ω into C71 10 µF (a second pole at 1.6 kHz).
>
> So the virtual ground tracks the rail **only below ~32 Hz**. Consequences:
> - §3.6b uses `beam on`/`beam off` as the step, which is effectively DC against a 32 Hz
>   corner — so **it measures the fully-coupled worst case, which is exactly what you want.
>   The ×7.25 prediction stands for that test.**
> - But this doc's warning that *"a detector that false-triggers on a 100 mV step will
>   false-trigger on its own strobe"* is **too pessimistic**. A strobe transient is orders
>   of magnitude faster and is attenuated by this pole — a ~200 µs event by roughly 150×, a
>   ~5 ms burst envelope by only ~6×. **Measure it in Phase 6; do not assume either extreme.**
>
> **2. The DC servo corner is confirmed at 2.27 Hz, and that is why the chop runs at 20 Hz.**
> Derived independently from the fitted parts — R83 1M × C73 330 nF = 0.48 Hz, times
> R80/R78 = 470K/100K = 4.7 → **2.27 Hz**, which reproduces the schematic's own annotation
> exactly. Chopping near that corner costs signal:
>
> | chop | loss to the servo |
> |---|---|
> | 5 Hz (this doc's original suggestion) | **8.9 %** |
> | 20 Hz (what `cal demod` uses) | **0.6 %** |
>
> 20 Hz sits in a clean window: above the 2.27 Hz servo and the 0.24 Hz gated HPF, far
> below the 15.39 kHz LPF.
>
> **3. The TIA is dispersive, and by a measurable amount.** Feedback is R80 470K with
> **C68 in series with C70 (1 pF + 1 pF = 0.5 pF)** — two parts in series because sub-pF
> capacitors are unbuyable and PCB parasitics would dominate. That gives a **677 kHz** pole:
>
> | carrier | TIA phase lag |
> |---|---|
> | 104.2 kHz | **8.7°** |
> | 150 kHz | 12.5° |
> | 200 kHz | 16.5° |
> | 250 kHz | **20.3°** |
>
> **This is the concrete reason a single demod-phase number cannot cover the whole scan
> range**, and why `cal model` fits phase against frequency instead of assuming a pure
> delay. It is also a *testable prediction*: the fitted model should show roughly this much
> curvature. If `cal model` reports `pure_delay`, something is wrong with the measurement.
>
> **4. ADC1 has no filter capacitor** — `Net-(U3-GPIO41_ADC1)` is R46.1, R47.2 and the pin,
> nothing else. That is consistent with Q9 (50 kΩ source into an ADC wanting ≤10 kΩ, with no
> reservoir to help the sample-and-hold) and is what CR-03 fixes.

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
| **TP7 (TIA_Out)** | **2.59 V mean regardless of ambient light** — the DC servo nulls it. ⚠ **DC only:** the servo corner is 2.27 Hz, so mains flicker is *not* removed — see the note below |
| TP9, TP10 | 2.59 V |
| ADC2 | should agree with TP7 on the scope |

**If TP7 is pinned at a rail, stop.** Either ambient photocurrent exceeds the servo's
±25 µA null range (shade the photodiode and retry) or there is a bias fault — check R77,
VIR at J3, and C67.

All four agreeing is the pass criterion, not the absolute number. Divergence localises
the fault: TP7 off alone → servo or bias; TP9 ≠ TP10 → an LPF stage.

> ### ⚠ Ambient flicker is NOT rejected with the beam off — measured 2026-08-17
>
> The DC servo nulls **DC**. Its corner is 2.27 Hz, so 100/120 Hz room-light flicker passes
> straight through it. And with the beam off, `Demodulation_PWM` is low (R91 pulldown) → U13
> statically passes TIA_Out → **there is no lock-in rejection either.** Gain from TP7 to ADC5
> is then ×29 (U12A ±1 × LPF 2 × U12B 14.5), and the 0.24 Hz HPF passes flicker happily.
>
> **Measured on this board under ordinary LED room lighting:** `capture 0x20 4000 125000`
> gave **201 codes = 162 mV p-p at ADC5, at ~120 Hz** — a ramp-then-collapse shape, not a
> sinusoid, because LED lamps run off a rectified supply. That works back to ~5.6 mV at TP7
> and **~12 nA of photocurrent ripple**.
>
> **Consequences:**
> - **`hpf test` needs the photodiode shaded or the lights off.** It resolves 20–90 mV
>   baselines; 162 mV of flicker swamps it and the test correctly reports ORDER-DEPENDENT.
> - For scale: a 100 mV bump at TP9 is ~1.45 V at ADC5, so flicker is ~11 % of a nominal ball
>   — a real noise floor with the demod static, and the reason the carrier exists.
>
> ### 🟢 Use this as the lock-in proof
>
> Take that capture **lights on, beam off** (162 mV of 120 Hz), then repeat it **beam on with
> the demodulator running**. The flicker should collapse by ~64 dB and effectively vanish.
> That is a direct, quantitative demonstration that the synchronous demodulator works, and it
> is a stronger check than anything in §3.4 — do it before trusting any phase calibration.

---

## 3.3 Beam ON, no target

> ### ✅ CLEARED 2026-08-19 — was the CR-15 block, now passes to 25 %
>
> **This section blocked Phase 3 for two days and no longer does.** The TIA saturated on beam
> coupling above ~3 % on board 1 and ~2 % on board 2 — two independent boards within one duty
> step, which established it as a **design** property. **The coupling was optical**, and better
> baffling plus keeping hands out of the beam removed it:
>
> | duty | board 2, baffled | swing | |
> |---|---|---|---|
> | 2 % | 3084 … 3253 | 136 mV | linear |
> | 12 % | 1891 … 3418 | 1231 mV | linear |
> | **25 %** | **2076 … 3606** | **1233 mV** | **linear — operating point** |
>
> Worst case (overhead lights on, photodiode aimed at them): min 1993 / max 3573, still no
> saturation, and within 5 % of the dark result at 12 % and 25 %.
>
> ⚠ **The mitigation is mechanical and depends on operator discipline**, so `NEXT_BOARD_REV.md`
> **CR-15 stays open at 🟡** — the board fix is still wanted. Re-run the sweep on every build;
> it is now the baffle acceptance test (`BRINGUP_NEW_BOARD.md` §8).
>
> ⚠ **CR-16 now binds tighter than CR-15.** At 25 % there is **0.39 V of margin to the ADC's
> 3.3 V ceiling** against 1.67 V to the op-amp rail. The 5 V analog chain, not the TIA, is what
> limits usable signal from here on.
>
> ⚠ **Two confounds cost a bench session each — do not repeat them.** A hand in front of the
> board reflects 850 nm into D12 and moved a control reading 339 → 19 codes. And most black
> plastics are near-transparent at 850 nm, so an untested "opaque" baffle proves nothing.
>
> 🔴 **Before running any optical test here, read `PROGRESS.md` §11.** A foil-over-D12 test
> shorted 36 V into the TIA summing node and destroyed U11B. D12's cathode is at VIR through
> R77 — nothing conductive goes near it without knowing what it must not touch.


```
beam duty 25          # NOT 30 -- CR-12: 30 % puts T_j at 123-133 C vs 145 max
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

⚠ **Undersampling makes low duties untrustworthy.** 500 ksps ÷ 104.1667 kHz is exactly **4.8**,
so only **24 distinct carrier phases** are ever visited and the capture's start phase varies
run to run. Below ~12 % the pulse is shorter than the 2 µs sample interval and the recorded
minimum is largely luck. **Compare boards and settings at 12 % and 25 %, not at 2 %.**

### The lock-in proof — and the ADC5 noise floor

Two different measurements that people conflate. **Run both; they need different rates.**

**(a) σ_noise, the denominator of every `scan carrier` SNR figure.** Beam on, HPF in TRACK, no
target:

```
hpf track
capture 0x20 400 500000
```

Board 2, 2026-08-21: **mean 9.2 mV, σ 2.19 mV, p-p 12.1 mV.** The mean landing on the TRACK
settled offset (9.3 mV from `hpf test`) to within 0.1 mV is the useful part — it says the HPF
is removing the whole static beam-coupling DC with the beam running.

**(b) Ambient rejection.** This is what the lock-in exists for: with the beam **off** the demod
is static and mains flicker reaches ADC5 at **162 mV p-p**; with it running the flicker should
collapse by ~64 dB.

🔴 **Do NOT use the 500 ksps capture for this.** 400 samples at 500 ksps is **800 µs — one
tenth of a single mains half-cycle** — so it cannot resolve 120 Hz even in principle. This
mistake was made on 2026-08-21 and produced two captures differing 6.4× with no way to
attribute the difference. Use ~200 ms:

```
capture 0x20 2000 10000      # 200 ms = 24 cycles of 120 Hz
```

Run it **lights-on and lights-off, labelled**, and let the scene sit still for **≥3 s** before
each — the HPF's τ is 0.66 s, so any change within ~2 s leaves the node still settling and
fakes a large offset.

**A stronger version of this test** than the doc originally specified: point the photodiode
directly at the overhead lights. That is closer to the intended operating environment than a
shaded bench.

#### ✅ Result, board 2, 2026-08-21 — rejection confirmed

| | 120 Hz at ADC5 |
|---|---|
| beam **off** (no lock-in), lights on | **162 mV p-p** |
| beam **on**, lights off | 0.12 mV |
| beam **on**, lights on | **0.32 mV** |

**≈ 250×, about 48 dB.** The central premise of the synchronous-detection design is now
measured rather than assumed. **Quiet-baseline σ at ADC5 is 0.55–0.65 mV.**

> ### 🔴 The rejection is frequency-selective, and that is easy to forget
>
> A lock-in rejects what is **far from the carrier**. It does nothing about ambient energy
> **near 104 kHz** — and LED drivers, electronic ballasts and switch-mode supplies all have
> harmonics that reach up there. A source landing within a few Hz of the carrier demodulates
> to a slow beat that looks exactly like a real signal.
>
> Board 2 shows candidate evidence: **16–21 % of samples are bursts to 72 codes at 5–30 Hz,
> and they are 3× worse with the room lights on.** σ(all) is 6.16 mV dark / 9.15 mV lit —
> **10× the quiet baseline.** The alternative explanation is motion in the beam, which is the
> detector working correctly.
>
> **Discriminator — do this before `scan carrier`:** lights on, beam on, **scene genuinely
> static** (step away, nothing moving), three repeat captures.
> Reproducible burst statistics → the lighting. Wildly varying → it was motion.
>
> ⚠ **Consequence for §3.6:** `scan carrier` exists to find the quietest carrier frequency, so
> **run it under the lighting the machine will actually operate in.** A frequency chosen on a
> dark bench is not optimised against the interference that matters.

⚠ **At 10 ksps the carrier aliases to 4166 Hz** (and 2f to 1668 Hz), clearly visible at
~2.6 mV. Harmless, but it means **10 ksps is the right rate for flicker and the wrong rate for
anything carrier-related.** Use the 500 ksps capture for that.

---

## 3.4 Demod phase calibration

> **⚠ The .md's `cal_demod_phase()` (§13.8) does not work as written.** It sweeps phase
> while sampling ADC5 with a *static* reflector. ADC5 sits after the 0.66 s gated HPF, so
> a static reflector produces **no signal there** in track mode — the sweep reads noise at
> every phase. Use one of the methods below instead.

**Setup:** a static reflector — a white card or a golf ball on a stand — at the operating
distance.

### Method A (preferred): chopped beam

HPF in **track** mode — type **`hpf track`**, which drives GPIO33 **LOW**. ⚠ An earlier
revision of this line said `gpio 33 1`; that is **HOLD** and is backwards. Polarity was
settled three ways (TMUX1219 truth table, `hpf test` on two boards): **GPIO33 = 0 is TRACK.**
Use the `hpf` command rather than raw `gpio` — it is the one place the constant lives.

Chop the carrier on/off at ~5 Hz, well inside the HPF
passband. Sample ADC5 synchronously and compute `mean(beam on) − mean(beam off)` over ~10
cycles. That differential ∝ cos(phase error), and it rejects ambient drift for free.

Sweep `beam phase` in TOP/64 ≈ 22-tick steps across 0..1439, take the argmax.

✅ **`cal demod` now does this.** It chops, sweeps 64 phase points, and fits a cosine via the
fundamental DFT bin rather than taking the grid argmax — using all 64 points instead of one,
immune to a single noisy sample, and sub-step in resolution. It also computes |H2|/|H1| and
**refuses to commit a phase when that exceeds 0.25**, because a response that is not a clean
cosine is not a lock-in response.

⚠ **The chop uses `beam_set_duty(0)`, never `beam_enable(false)`.** Disabling the slices
stops the *demodulator clock* too, so U13's mux freezes at one sign and the "off" half-cycle
is a different circuit rather than a dark reference. If you chop by hand, chop the duty.

⚠ **Chop at 20 Hz, not 5.** With τ = 0.66 s a 100 ms half-cycle droops 14 %; 25 ms droops
3.7 % and is still 80× above the HPF corner.

### Method B (quick): frozen HPF

**`hpf hold`** (GPIO33 **HIGH**) with the reflector present, then sweep phase. ⚠ Same
correction as above — an earlier revision had this as `gpio 33 0`, which is TRACK. Held DC shifts do reach
ADC5. Faster, but easy to rail — the ×14.5 stage clips at ΔTP9 ≈ 228 mV.

**Sanity check either way:** the response should fall to ~0 at +90° from the peak
(quadrature null). If it does not, you are not seeing the real lock-in response.

Record `demod_phase_ticks` in `PROGRESS.md` §6.

---

## 3.5 Threshold DAC and comparator cross-calibration

Threshold_PWM (GPIO44) → two 1 ms RC poles → Threshold_DC at **TP8** = 3.3 V × duty.
Use TOP = 1023 → 146.5 kHz, 3.2 mV steps. **Settle 20 ms after any change, not 10.**
The two RC sections load each other, so the real poles are 2.62 ms and 0.382 ms rather than
two independent 1 ms poles; 10 ms is only ~4τ and leaves ~2 % of a step, about 20 codes.
`threshold sweep` uses `DAC_SETTLE_MS` and gets this right for you.

**Self-test needing no external gear:** sweep the threshold duty while watching ADC5 and
GPIO46 together, and find the duty where D_Comparator flips. That single measurement
cross-calibrates the threshold DAC against the ADC5 scale and proves both paths.
(The R102/D14 clamp is on **ADC5**, not on the threshold node — `Threshold_DC` has no clamp
at all. So the sweep exercises the ADC path's protection, not the threshold's.)

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

Use the beam as the load step — at 25 % duty it is ~0.79 A, the largest thing you can safely
switch for a sustained test. (30 % is a bigger step but is not a sustainable operating
point; see CR-12.)
**No target present**, so the only optical change is crosstalk.

```
on
beam freq 104166
beam duty 25          # NOT 30 -- see CR-12
adcmode idle
hpf track          # HPF TRACK -- drives GPIO33 LOW, not high
```

Scope **+5 V** and **ADC5** together, then toggle `beam on` / `beam off` and capture both.

| Step | HPF | What to record |
|---|---|---|
| 1 | `hpf track` (**TRACK** = GPIO33 **0**) | ΔV at +5 V, and the ADC5 excursion. The HPF should remove most of it. |
| 2 | `hpf hold` (**HOLD** = GPIO33 **1**) | Same step. **This is the armed case.** |
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

**Log per pass:** `detect log` gives CSV of every pass — comparator transit, ADC transit,
peak, baseline, asymmetry, fragment count, quality flags. `detect wave` dumps the retained
waveform for the last few passes (4 are kept, decimated ×8; full waveforms for 60 passes
would need 960 KB against 520 KB of SRAM).

```
detect path <mm>       # beam width. NO VELOCITY IS REPORTED UNTIL THIS IS SET --
                       # a speed from a guessed geometry looks authoritative and is wrong.
detect cond 0          # tag the condition before each block of 20 passes
detect arm
```

### Exit criteria
- TP9 shows a clean smooth positive bump per pass
- Computed speed matches the ramp prediction within a few percent
- ⚠ **"One clean pulse pair, no chatter" may not be achievable, and that is not a firmware
  bug.** U15 has no hysteresis, so a slow-slewing edge can cross the threshold several
  times. `detect` reports a **fragment count** per pass — that count *is* the chatter
  measurement. A chattered pass reports its comparator transit as a **lower bound** (the
  sum of fragment widths, excluding the notches) and says so, because the gaps cannot be
  recovered from FIFO arrival times. Use the ADC-derived transit for those passes.
- If chatter is bad enough to matter, the options are `detect coalesce` (software merge),
  a PIO change to measure the notch widths, or the board fix in `NEXT_BOARD_REV.md` CR-13.

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

**Deliverable:** `detect stats` computes this on the board — mean and σ per method per
condition, and then regresses bias against 1/peak across the three conditions. **That slope
is the amplitude-dependent bias**, stated as a number rather than eyeballed from a table.

⚠ **Saturated passes bias the ADC method the OPPOSITE way.** A clipped peak makes the 50 %
level too low, so the transit reads long and the speed low — while the comparator's bias
reads the transit short and the speed high. So a saturated pass brackets the truth rather
than being useless, but silently mixing them in would flatten the very slope being measured.
`detect stats` counts them separately; watch that column. That table decides the final design —
and if the bias turns out negligible at your geometry, you get to simplify and drop the
ADC refinement.
