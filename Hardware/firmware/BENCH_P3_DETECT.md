# Phases 3 & 4 — Photodiode detection and trigger selection

> ## 🔵 Which of these sections repeat on a new board, and which do not
>
> | § | kind | driven by |
> |---|---|---|
> | 3.1 ADC allocation | design reference | — |
> | **3.2 static health** | ⚠ **per-board** | `BRINGUP_NEW_BOARD.md` §7 |
> | **3.3 beam on / CR-15 duty sweep** | ⚠ **per-board** (the baffle acceptance test) | `BRINGUP_NEW_BOARD.md` §8 |
> | 3.3 ambient rejection, burst source | ✅ settled once — see `PROGRESS.md` §0.5 | — |
> | **3.4 `cal demod` + `cal model`** | ⚠ **per-board** | `BRINGUP_NEW_BOARD.md` §9 |
> | **3.5 threshold + comparator** | ⚠ **per-board** | `BRINGUP_NEW_BOARD.md` §10 |
> | 3.6b Q8 rail step | ✅ **design validation — once** | this document |
> | 3.6 carrier margin | ✅ **design validation — once**, plus a per-board SW-frequency record | this document |
> | **3.7 transits + `cal gain`** | ⚠ **per-board** | this document |
>
> **The per-board rows are here for the reasoning; the procedure lives in the bring-up doc.**
> If you are bringing up a board, work `BRINGUP_NEW_BOARD.md` top to bottom and come back here
> only when a number does not match.

**Prereq:** Phase 2 complete, carrier frequency and clamp both measured.
**Power:** PSU 5.2 V / 2.5 A. **Pi:** not connected.
**Gear:** scope, DMM, plus a golf ball and a piece of cardboard (see §3.7).

Phases 3 and 4 are one continuous piece of work — 4 is an experiment run on the
apparatus 3 builds — so they share a document.

---

## The signal chain, and where to look

```
D12 (36 V bias) → U11A TIA (Rf, inverting -- SEE THE VARIANT NOTE) ────→ TP7
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
> **2. The DC servo corner is confirmed at 2.27 Hz ON A STOCK BOARD, and that is why the chop
> runs at 20 Hz.** ⚠ **It scales linearly with Rf** — `f = [1/(2π·R83·C73)] × (Rf/R78)` — so the
> 116 kΩ variant sits at **0.559 Hz, τ 285 ms**. The 20 Hz chop clears both comfortably; what
> changes is the settling wait after any beam change (5τ is 0.35 s stock, **1.4 s** reworked,
> so the 3 s the procedures ask for still covers it).
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
> ### 🔴 THE TIA FEEDBACK IS A PER-BOARD VARIANT. Check yours before using any number below.
>
> Boards may carry a reworked feedback network. **Record which variant your board is, in the
> bring-up sign-off table, before running any calibration** — nearly every number in this
> document scales with Rf.
>
> | | **stock** | **board 3, reworked 2026-08-25** |
> |---|---|---|
> | Rf | R80 **470 kΩ** | R80 ∥ 154 kΩ = **116.0 kΩ** |
> | transimpedance | 0.470 V/µA | **0.116 V/µA** — 4.05× less |
> | Cf | C68 series C70, 1 pF + 1 pF = **0.500 pF** | 100 pF C0G across **one** leg → **0.990 pF** |
> | TIA pole | **677.3 kHz** | **1.386 MHz** |
> | TIA lag at 104.1667 kHz | **8.74°** | **4.30°** |
> | DC servo corner | **2.267 Hz** (τ 70 ms) | **0.559 Hz** (τ 285 ms) |
>
> **Component values are calculated; the two phase rows below are MEASURED (2026-08-25).**
>
> | at 104166 Hz | board 2 (stock) | **board 3 (reworked)** | predicted |
> |---|---|---|---|
> | `demod_phase_ticks` | 1343 | **1311** | 1325 |
> | chain delay | 553 ns | **340 ns** | 435 ns |
>
> 🔴 **The phase moved 32 ticks where the TIA pole change predicts 17.8 — 1.80× too far.**
> Direction and order are right, so the rework did what it was for. But 95 ns of the drop is
> unattributed, and **this experiment cannot say why**, because board 2 and board 3 are
> different boards and the drive path (GPIO31 → U10 → U9 → FET → D11) has never been measured
> on either.
>
> Splitting the budget with the TIA's own contribution taken out:
>
> | | total | TIA share | everything else |
> |---|---|---|---|
> | board 2 | 553 ns | 233 ns (8.74°) | **320 ns** |
> | board 3 | 340 ns | 115 ns (4.30°) | **225 ns** |
>
> ✅ **Both parts of the rework took.** The Rf change is confirmed by TP7's swing: **0.76 V**,
> and 0.76 V / 116 kΩ = 6.55 µA against board 2's 1.795 V / 470 kΩ = 3.82 µA — a **1.72×**
> optical difference that independently matches the 80 %-vs-47 % full-scale figure from
> `cal demod`. The Cf change is confirmed by the operator: **the oscillation was significantly
> worse before the 100 pF went on.**
>
> > 🔴 **RETRACTED 2026-08-28 — an argument that the Cf change had NOT taken.** It rested on
> > board 3's TP7 overshoot (~12 % of swing) being 3–4× worse than board 2's (2.9 %). **Those
> > two numbers were taken on different instruments and are not comparable:** board 2's came
> > from a coherent average of a **logic-analyser analog capture at 50 MS/s**, whose front end
> > is bandwidth-limited in exactly the region where the ring lives (~1 MHz); board 3's came
> > from a **500 MHz scope**. The LA under-reports the overshoot, so the comparison was
> > measuring the instruments, not the boards.
> >
> > 🔵 **The rule: never compare a ringing amplitude across instruments of different
> > bandwidth.** Compare frequency, or re-take both on the same instrument.
>
> ⚠ **It is still under-compensated, and `NEXT_BOARD_REV.md` predicted that** — its
> compensation table says 0.5 pF at ~100 kΩ is *"badly under-compensated"*, and 0.99 pF is only
> halfway to where it should be. **See the sizing note below.**
>
> 🔵 **The lesson is procedural: a rework acceptance test needs a BEFORE measurement on the
> SAME board.** Board 3 was never calibrated stock, so the TIA change and the board-to-board
> difference are confounded and cannot be separated after the fact. **Do the before-run next
> time** — it costs 26 s.
>
> ✅ **The measurement that would settle it:** two LA channels on **GPIO31 and TP7**, and read
> the delay directly. That splits drive path from TIA on one board with no inference at all.
>
> ⚠ **C68 and C70 are in SERIES.** 100 pF across *one* leg effectively shorts that leg, leaving
> the other 1 pF, so Cf ≈ 0.99 pF. **Across the pair** it would be 100.5 pF and the pole would
> land at **13.7 kHz — below the carrier, with 82.5° of lag.** That would gut the signal.
> If a reworked board behaves nothing like the table, check which pads the cap bridges first.
>
> **3. The TIA is dispersive, and by a measurable amount.** On a **stock** board the feedback
> is R80 470K with C68 in series with C70 (1 pF + 1 pF = 0.5 pF) — two parts in series because
> sub-pF capacitors are unbuyable and PCB parasitics would dominate. That gives a **677 kHz**
> pole:
>
> | carrier | TIA phase lag, **stock** | TIA phase lag, **116 kΩ variant** |
> |---|---|---|
> | 104.2 kHz | **8.7°** | **4.3°** |
> | 150 kHz | 12.5° | 6.2° |
> | 200 kHz | 16.5° | 8.2° |
> | 250 kHz | **20.3°** | **10.2°** |
>
> ✅ **The 8.7° was confirmed exactly on 2026-08-24**: the measured total chain lag at 104 kHz
> is 23.2°, and the TIA's share of it is 8.74°.
>
> 🔴 **But do not distrust a `pure_delay` verdict.** An earlier revision of this line said
> "if `cal model` reports `pure_delay`, something is wrong with the measurement." That was
> wrong, it was written into a firmware test, and the test then reported the opposite of the
> truth for two sessions.
>
> The measured chain delay is **about 570–620 ns and nearly flat across the band**. Holding one
> *delay* number across 80–200 kHz costs well under a degree. The chain is a pure delay for
> every practical purpose.
>
> ⚠ **Do not quote a droop figure.** Two fits on this board, both with residuals under 0.09°,
> gave **−3.5 %** (8/24) and **−0.05 %** (8/25). The span is a difference of two large numbers,
> so a few-tick shift in the fit moves it enormously while barely touching either endpoint. An
> earlier revision of this section quoted the 3.5 % as a property of the hardware and gave the
> TIA pole credit for a quarter of it — **that was over-reading a single measurement.** The
> absolute delay and the pure-delay verdict are the reproducible parts.
>
> **A single phase number still fails across the scan range** — see §3.4 — but for a geometric
> reason that has nothing to do with these poles.
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
> Board 2 showed exactly this shape and it turned out **not** to be the lighting.

#### ✅ RESOLVED 2026-08-21 — the "bursty noise" was the beam returning off the room

Covering **D11's output aperture** so no light leaves the board, everything else identical:

| | D11 open | **D11 covered** |
|---|---|---|
| σ (three captures) | 6.49 / 10.37 / 14.03 mV | **2.62 / 3.39 / 2.91 mV** |
| peak | 56 / 66 / 77 codes | **28 / 29 / 33 codes** |
| excess kurtosis | −0.3 / −0.3 / +3.0 | **+0.1 / +0.5 / +0.1** |
| reproducible? | no, 2× spread | **yes, 30 %** |

**Stop the light leaving and the bursts stop.** The lock-in was passing genuine
carrier-modulated light returning off the room — the detector working correctly, not a fault.

🔵 **The kurtosis was the tell.** Impulsive *noise* has **positive** excess kurtosis (heavy
tails). Two captures ran at **−0.3** — a flattened, near-bimodal distribution, which is the
signature of **a modulated signal filling the range**. Compute the fourth moment before
calling something noise.

> ### 🔴 σ_noise is not a constant, and §3.6 depends on it
>
> Three values on one board in one session:
>
> | condition | σ at ADC5 |
> |---|---|
> | nothing returning (quiet population) | **0.65 mV** |
> | D11 covered — the cover itself reflects back at close range | **2.91 mV** |
> | open to the room | **6.5–14 mV** |
>
> **It is set by the optical background, not by the electronics.** Treat 2.91 mV as a bench
> reference for repeatability checks only — never as *the* noise floor.
> ⚠ **This used to say "so run `scan carrier` in the final geometry, or its ranking is against
> the wrong denominator".** That mattered when the carrier was ranked per board. Since
> 2026-08-28 it is **fixed by design** and §3.6 compares nine rows taken inside one run under
> identical optics, where a common background cancels. The absolute σ_noise still is not an
> operational number.

⚠ **At 10 ksps the carrier aliases to 4166 Hz** (and 2f to 1668 Hz), clearly visible at
~2.6 mV. Harmless, but it means **10 ksps is the right rate for flicker and the wrong rate for
anything carrier-related.** Use the 500 ksps capture for that.

---

## 3.4 Demod phase calibration

> **⚠ The .md's `cal_demod_phase()` (§13.8) does not work as written.** It sweeps phase
> while sampling ADC5 with a *static* reflector. ADC5 sits after the 0.66 s gated HPF, so
> a static reflector produces **no signal there** in track mode — the sweep reads noise at
> every phase. Use one of the methods below instead.

### 🔴 Setup: get the LEVEL right. Whether that means adding or removing light is per-board.

⚠ **An earlier revision of this section said flatly "no reflector". That is right for some
boards and wrong for others, and stating it unconditionally cost a bench run.** The rule is
**target a `cal demod` peak of 50–70 % of full scale**; which direction you move to get there
depends on the board and where it is sitting.

| situation | what it needs |
|---|---|
| **Board 2, stock 470 kΩ TIA, on the bench** | D11 → D12 crosstalk plus the floor return alone gave **6.30 V at ADC5 against a 3.3 V ceiling — 1.91× over** with nothing in front of the board. **Attenuate.** A reflector only makes it worse. |
| **Board 3, 116 kΩ TIA, in its final location** | 4× less gain and far less return. Bare, it is **too low to calibrate**. **Add a static target.** |

🔴 **Whatever you add, it must not move.** See the block below.

### ✅ Use `level` to aim it — do not guess and then spend 26 s finding out

```
level
```

Prints the live peak as a percentage of full scale, ~5×/s, until you press a key. It chops at
20 Hz and reports **the same `peak` number `cal demod` will report**, so you can position the
target and watch the figure instead of running a sweep to find out.

```
live level: chopping at 20 Hz, reading the peak at the CURRENT phase
  (1311 ticks, duty 25.00 %). Only the TRUE peak if the phase is already
  near it -- run 'cal demod' once first.
TARGET 50-70 % of full scale. Position the target, then CLAMP IT --
  a hand cannot hold still for the 128 s 'cal model' takes.

  peak  1613 =  39 %  |########            |  low  -- more light
  peak  2120 =  52 %  |##########          |  GOOD
  peak  2650 =  65 %  |#############       |  GOOD
```

⚠ **It reads the peak only if the demod phase is already near the peak** — it samples one
phase rather than sweeping. Run `cal demod` once first, then use `level` for adjustment.

⚠ **Ignore the first ~3.5 s.** Starting the chop is a step into the 0.66 s gated HPF, so the
first readings climb toward the true value rather than sitting at it — board 3 read **9 %**
on its first line and settled at **66 %**. Builds after 2026-08-28 label those readings
`settling`. 🔵 That climb is itself a free measurement of the HPF: its deficit from the final
value fell by 0.736 per 200 ms reading, giving **τ = 0.653 s** against the 2 MΩ × 330 nF
design value of 0.66 s.

🔵 **Getting the level right is the single most repeated failure in Phase 3.** Too much light
saturates the sweep; too little and there is nothing to fit; and the obvious way to add signal
by hand is the one thing that ruins `cal model`. `level` closes that loop in seconds instead
of half-minutes.

> ### 🔴 NEVER USE YOUR HAND AS THE TARGET
>
> Measured 2026-08-28, same board, back to back — peak-to-peak per `cal model` point:
>
> | | spread across the five points |
> |---|---|
> | static scene | **2.7 %** |
> | operator holding a hand up, not perfectly still | **27.1 %** |
>
> A 10× difference. The hand run also **clipped two of its five points**, got one rejected, and
> produced a **0.37° residual against 0.08°** for a good run.
>
> 🔵 **Nothing in the per-point checks catches this, and that is the point:** every point is
> individually valid — clean h3, clean null, no saturation — because each one *is* a correct
> measurement. They just describe **different scenes**. The fit then interprets a change in the
> optics as a change in phase.
>
> ✅ Builds after 2026-08-28 print **`amp spread`** after the model fit and flag it above 20 %.
>
> **Use a card taped to something.** Frequency changes the phase, not how much light comes
> back — so if the amplitude moves between points, the scene moved.

Crosstalk calibrates the phase perfectly well: it traverses the same TIA → demod → LPF chain,
and the path-length difference is ~2 m = **1 tick** at 104 kHz.

**What you need instead is ATTENUATION INTO D12** — target a `cal demod` peak of 50–70 % of
full scale. 🔴 **Nothing conductive near D12**: its cathode is at 36 V through R77, and that
is what destroyed board 1 (`PROGRESS.md` §11). A card with a hole in it is the best
attenuator, because the attenuation is geometric and does not depend on the material behaving
at 850 nm — most "opaque" black plastics are near-transparent there.

### Method A (preferred): chopped beam

HPF in **track** mode — type **`hpf track`**, which drives GPIO33 **LOW**. ⚠ An earlier
revision of this line said `gpio 33 1`; that is **HOLD** and is backwards. Polarity was
settled three ways (TMUX1219 truth table, `hpf test` on two boards): **GPIO33 = 0 is TRACK.**
Use the `hpf` command rather than raw `gpio` — it is the one place the constant lives.

Chop the carrier on/off at **20 Hz** (see the droop note below), well inside the HPF
passband. Sample ADC5 synchronously and compute `mean(beam on) − mean(beam off)` over several
cycles. That differential ∝ cos(phase error), and it rejects ambient drift for free.

Sweep `beam phase` in TOP/64 ≈ 22-tick steps across 0..1439, take the argmax.

✅ **`cal demod` does this.** It chops, sweeps 64 phase points, and fits the fundamental DFT
bin rather than taking the grid argmax — all 64 points instead of one, immune to a single
noisy sample, sub-step in resolution.

> ### ⚠ The response is a TRAPEZOID, not a cosine — and the guards were rebuilt around that
>
> A duty-D pulse correlated with a 50 % ±1 square gives harmonic n (odd) equal to
> `4·D·ΔV·sinc(nD)/(nπ)`, with a true peak of `D·ΔV`. Three consequences:
>
> | quantity | what a CLEAN 25 %-duty response gives |
> |---|---|
> | h2/h1 | **0 exactly** — a 50 % square emits only ODD harmonics, whatever the optics do |
> | h3/h1 | **0.111** = 1/9, because sin(0.75π) = sin(0.25π) |
> | fitted fundamental ÷ true peak | **1.146** = 4·sinc(D)/π |
>
> 🔴 **h2 is a DIAGNOSTIC, not a gate.** It cannot see the optical path at all. What it does
> track is sweep rate against the 0.66 s HPF: **0.124–0.148 at 400 ms/point, 0.261–0.381 at
> 300 ms/point, and unchanged by a 2× amplitude change.** An earlier revision of this section
> said `cal demod` "refuses to commit a phase when |H2|/|H1| exceeds 0.25" — it no longer
> does, and it never should have.
>
> 🔴 **The h3 bar is duty-aware:** `h3_expected(duty) + 0.05`. The old fixed 0.15 sat only
> 1.35× above a clean 25 % signal and **rejected a clean signal below ~20 % duty** — a trap,
> since the saturation message tells you to reduce the light and lowering duty is the obvious
> way to do it.
>
> | duty | 25 % | 20 % | 15 % | 12.5 % | 10 % | 5 % |
> |---|---|---|---|---|---|---|
> | intrinsic h3/h1 | 0.111 | 0.180 | 0.242 | 0.268 | 0.291 | 0.323 |
>
> ⚠ **Below ~15 % duty h3 tells you nothing** — clean and clipped both approach 0.333.
> `sat_frac` is the guard there, and the CLI says so itself.
>
> ⚠ **The fitted `amplitude` is not the peak.** It is the fundamental, 1.146× the peak at
> 25 % duty, so the rail check runs on the recovered `peak` instead. `cal demod` prints both.

⚠ **The chop uses `beam_set_duty(0)`, never `beam_enable(false)`.** Disabling the slices
stops the *demodulator clock* too, so U13's mux freezes at one sign and the "off" half-cycle
is a different circuit rather than a dark reference. If you chop by hand, chop the duty.

⚠ **Chop at 20 Hz, not 5.** With τ = 0.66 s a 100 ms half-cycle droops 14 %; 25 ms droops
3.7 % and is still 80× above the HPF corner.

### Method B (quick): frozen HPF

**`hpf hold`** (GPIO33 **HIGH**), then sweep phase. ⚠ Same
correction as above — an earlier revision had this as `gpio 33 0`, which is TRACK. Held DC shifts do reach
ADC5. Faster, but easy to rail — the ×14.5 stage clips at ΔTP9 ≈ 228 mV.

**Sanity check either way:** the response should fall to ~0 at +90° from the peak
(quadrature null). If it does not, you are not seeing the real lock-in response.

> ### 🔴 The first run, 2026-08-21, saturated — and the guards let it through
>
> **84 % of the 64-point sweep sat at ±4086 codes** (full scale 4095). The response was a
> **square wave, not a cosine**: fitted amplitude **5128 codes, above full scale**, which is
> impossible for a real signal and is exactly what a square gives (fundamental = 4A/π = 1.27A).
>
> **Why the purity check missed it — worth understanding, not just fixing.**
> `h2_ratio` scored **0.012** against a 0.25 bar and passed. **Symmetric clipping produces only
> ODD harmonics** — an ideal square wave has h2/h1 = **0 exactly**. The even-harmonic test was
> looking in the one place where clipping is guaranteed to leave no trace. Measured
> **h3/h1 = 0.306** against a square's 0.333.
>
> And the **quadrature null reported −4086 where ~0 was required, printed it, and the phase
> committed anyway** — `valid` never referenced it.
>
> ✅ Both fixed in firmware: `h3_ratio`, `sat_frac` (limit 10 %), an amplitude sanity check,
> and the null now enforced at 15 % of amplitude and anchored to the *fitted* peak rather than
> the grid argmax, which is degenerate when the top is flat.
>
> ⚠ **Two of those numbers were themselves superseded on 2026-08-24.** `h3_ratio`'s limit was
> a fixed **0.15**, which turned out to be *below* what a clean signal gives at any duty under
> 20 % — it is now `h3_expected(duty) + 0.05`. And the amplitude check compared the fitted
> **fundamental** against full scale, when a trapezoid's fundamental is 1.146× its own peak;
> it now checks the recovered peak. See the RESOLVED block at the top of §3.4.
>
> 🔴 **If it says the signal is too big, reduce the LIGHT, not the gain.** U12B is already at
> its **minimum** 14.5 — R98 is DNP and fitting it only *raises* gain. In order of preference:
> **move the reflector further away**, use a **grey card rather than white**, or add an **ND
> filter over D12**.
>
> ✅ **Clipping preserves zero crossings**, so a saturated sweep can still give the right phase
> — the 8/21 run's 66 ticks was confirmed by its own crossings (427 / 1147 vs 426 / 1146
> predicted) and by the independent `cal model` (65.4 ticks at 104166 Hz). **Treat that as a
> lucky escape and re-measure**, because the amplitude, the null and the SNR are all worthless
> from a clipped sweep even when the phase survives.

> ### ✅ RESOLVED 2026-08-24 — 1348 ticks at 104166 Hz, and three guards were wrong
>
> **`demod_phase_ticks = 1348`**, from a clean sweep: peak 2030 codes (50 % of full scale),
> sat 0 %, quad null 0.4, h3 0.119. Confirmed by the saturated run (1347), an offline fit of
> the rejected model sweeps (1355), and `cal model` itself (1353) — four numbers inside 8 ticks.
> **The 8/21 value of 66 ticks is superseded.**
>
> Three things had to be fixed first, and all three are worth knowing before you re-run this.
>
> **1. `cal model` left the carrier at 200 kHz.** It swept 80→200 kHz and never restored the
> frequency, so every `cal demod` afterwards silently calibrated 200 kHz and committed the
> answer. Two bench runs were lost to it. ✅ Fixed, and `cal demod` now prints its frequency.
> ⚠ **The tell is in the sweep listing:** phases step by (TOP+1)/64, so steps of 11/23/35 mean
> a 750-tick period = 200 kHz; 22/45/67 means 1440 = 104 kHz. **Read the step size.**
>
> **2. The response is a TRAPEZOID, not a cosine, and h2 cannot see the optical path.**
> A 50 % square demodulator emits **only odd harmonics** of the phase sweep, so h2 ≡ 0 for any
> optical input — reconstructing the sweep from a 50 MS/s TP7 capture gives 0.0000. It was
> gating `cal model` at 0.26–0.38 and rejecting all five points, and what it actually tracked
> was sweep rate: **0.124–0.148 at 400 ms/point, 0.261–0.381 at 300 ms/point, and unchanged by
> a 2× amplitude change.** ✅ h2 is now a printed diagnostic only, and `cal model` sweeps at the
> same 64 points / 8 cycles as `cal demod` (~128 s for five points).
>
> **3. 🔴 The h3 bar rejected clean signals below ~20 % duty.** A clean trapezoid has intrinsic
> `h3/h1 = |sinc(3D)/(3 sinc(D))|` — **0.111 at 25 % duty (exactly 1/9), 0.180 at 20 %, 0.291 at
> 10 %** — against a fixed bar of 0.15. So the advice "reduce the light" combined with the
> obvious way to do it (drop the duty) walked straight into a false failure. ✅ The bar is now
> `h3_expected(duty) + 0.05`, and the fitted amplitude is divided by the trapezoid form factor
> `4·sinc(D)/π` (1.146 at 25 %) before being compared to full scale.
>
> ⚠ **Below ~15 % duty h3 tells you nothing** — a clean response and a clipped one both give
> ≈0.333. `sat_frac` is the guard there. The CLI says so itself when duty < 15 %.
>
> ### 🔴 You do not need a reflector, and the reflector is not what saturates you
>
> A 50 MS/s capture of TP7 with **no reflector at all** — 562 periods coherently averaged —
> gives 25.42 % duty, top 2.914 V, bottom 1.172 V, **swing 1.795 V**. Correlated against a 50 %
> square that is a **434 mV peak at TP10 → 6.30 V at ADC5 against a 3.3 V ceiling, 1.91× over**.
> **D11→D12 crosstalk plus floor return alone over-drives the chopped calibration by ~2×.**
>
> So moving, greying or removing a reflector cannot fix it — **only attenuating light into D12
> does**. And a reflector is not needed: crosstalk traverses the same TIA → demod → LPF chain,
> and the path-length difference is ~2 m = **1 tick** at 104 kHz.
>
> 🔴 **Nothing conductive near D12** — its cathode is at 36 V through R77, and that is what
> killed board 1. See `PROGRESS.md` §11. A card with a hole in it is the best attenuator,
> because the attenuation is geometric and does not depend on the material behaving at 850 nm.
> **Target `cal demod` peak at 50–70 % of full scale.**
>

Record `demod_phase_ticks` in `PROGRESS.md` §6.

---

## Running order for 3.5 → 3.7

**The section numbers are not the run order.** Do them in this order:

| | Section | Why here |
|---|---|---|
| 1 | **3.5** threshold DAC + comparator | Everything after it needs a known threshold |
| 2 | **3.6b** Q8 rail step | Do it *before* any transit — if a rail step looks like a ball, every number in 3.7 is contaminated |
| 3 | **3.6** carrier verification | **No longer a per-board step.** The carrier is fixed at 104.1667 kHz by design; §3.6 is now a one-off robustness check (~5 min) |
| 4 | **3.7** ball transit + `cal gain` | Needs all of the above |

**Common state for every section below**, unless a section says otherwise:

```
on                     # close the latch -- expect state RUNNING or BENCH_RUNNING
beam freq 104166       # confirm with 'beam': freq 104166 Hz, TOP=1439
beam duty 25           # NOT 30 -- CR-12 puts T_j at 123-133 C against a 145 C max
beam on
hpf track
```

then **wait 5 minutes** before any calibration. ⚠ **The warm-up is not optional and nothing
electrical shows it** — see the callout at the end of 3.5.

> ### ⚠ Check the carrier before every calibration command
>
> `cal model` and `scan carrier` both walk the carrier across 80–200 kHz. Both now restore it
> when they finish, but a reset, a crash or an older build will leave the beam wherever the
> sweep ended. **Type `beam` and read the first two lines** — `freq 104166 Hz (TOP=1439)`.
>
> The other tell is in any sweep listing: phases step by `(TOP+1)/64`, so **steps of 22/45/67
> mean 1440 ticks = 104 kHz**, and **steps of 11/23/35 mean 750 ticks = 200 kHz**. Two bench
> sessions were lost to this on 2026-08-24.

---

## 3.5 Threshold DAC and comparator cross-calibration

**What this proves:** that the threshold DAC, the ADC5 path and the comparator all agree with
each other, using no external gear. It also measures GPIO44 crosstalk into the detect node.

Threshold_PWM (GPIO44) → two RC poles → Threshold_DC at **TP8** = 3.3 V × duty, TOP = 1023 →
146.5 kHz, 3.2 mV steps. **Settle 20 ms after any change, not 10** — the two RC sections load
each other, so the real poles are 2.62 ms and 0.382 ms rather than two independent 1 ms poles.
10 ms is only ~4τ and leaves ~2 % of a step, about 20 codes. `threshold sweep` uses
`DAC_SETTLE_MS` and gets this right for you.

### Board state

| | |
|---|---|
| Rail | **up** (`on`) |
| Beam | **ON**, 104166 Hz, 25 % duty, **warm ≥ 5 min** |
| HPF | **`hpf track`** |
| ADC | `adcmode idle` (ch5 is in the idle set; the sweep reads the ring) |
| Target | **none** |
| Gear | none required. A DMM on TP8 is a useful cross-check. |

> ### 🔴 First, the thing that makes this test awkward: ADC5 has no DC to compare against
>
> With no target and the HPF in **TRACK**, ADC5 sits at the settled TRACK offset — **~9 mV on
> board 2**. The threshold DAC's smallest step is **3.2 mV** (TOP = 1023 → 3.3 V / 1024). So a
> naive `threshold sweep 0 100 64` puts the flip somewhere between its first two points and
> resolves nothing.
>
> **That is not a fault, it is the design working:** the 0.66 s HPF exists to remove exactly
> the DC you would want here. So the sweep is run **twice**, at two very different signal
> levels, and the pair gives you a scale *and* an offset instead of one weak point.

### Step 1 — confirm the comparator reads both ways

⚠ **The subcommand word is required.** `threshold 5` is not "set 5 %" — older builds printed
the status line and silently changed nothing, which reads exactly like it worked. Builds after
2026-08-25 reject it with `ERR: '5' is not a threshold subcommand -- NOTHING WAS SET.`

```
threshold duty 0
threshold
```

**Expect:** `D_Comparator(46) = 1   (ABOVE threshold ...)`. The threshold is 0 V and the signal
is ~9 mV, so the comparator is above it.

```
threshold duty 5
threshold
```

**Expect:** `D_Comparator(46) = 0   (below threshold ...)`. 5 % duty is ~165 mV, far above the
quiescent signal.

🔴 **If it never changes**, stop — the sweeps below will report NO FLIP and tell you nothing
about why. Check GPIO46 with `pins`, and check TP8 with a DMM against the `-> TP8 x.xxxx V
nominal` line that `threshold` prints.

⚠ **The `duty 0` reading is close to the LM393's own input offset** (a few mV typical, up to
~15 mV). If it reads 0 rather than 1, that is a plausible offset, not necessarily a fault — go
straight to step 2 and see whether a flip appears at all.

### Step 2 — LOW point: sweep against the quiescent baseline

Narrow range, because the flip is near the bottom:

```
threshold sweep 0 5 64
```

64 steps across 0–5 % is 0.078 % each ≈ 2.6 mV — finer than the DAC's own 3.2 mV, so the
resolution is DAC-limited, which is as good as this gets. Takes ~1.3 s.

**Expect roughly:**

```
FLIP at duty 0.28%  = 0.0092 V nominal at TP8
  ADC5 there: code 11 = 0.0089 V
ADC5 movement across the sweep: 0.0018 V
```

### Step 3 — HIGH point: give it a real signal, at a level you choose

Freeze a DC at ADC5 using the gated HPF, then sweep against *that*.

> ### 🔴 At the calibrated phase this RAILS. Dial it down first — read this before typing.
>
> **An earlier revision of this step said to expect 50–70 % of full scale at the operating
> phase. That was wrong, and it was a prediction rather than a measurement.** Run on
> 2026-08-25 it pinned ADC5 at **4095** and `threshold sweep 0 100 64` reported **NO FLIP** —
> correctly, because the comparator input was above the DAC's own 3.3 V maximum, so there was
> no crossing to find.
>
> **Why the chop misleads you here.** `cal demod`'s number is the *chopped differential*, and
> the 0.66 s HPF centres a chopped square — each half sits at roughly ±half the swing about the
> baseline. A **held** step is not centred: the whole excursion appears on one side. So a chop
> that peaks at 81 % of full scale corresponds to a held step well past the rail.
>
> **The knob is `beam phase`.** The response is a trapezoid in phase, so moving away from the
> calibrated peak scales the step down predictably and reversibly — no optics, no attenuator,
> no re-tuning. From the board-2 §3.4 sweep (peak ≈ 3690 codes at 25 % duty):
>
> | `beam phase` | fraction of peak |
> |---|---|
> | 1343 (calibrated peak) | 100 % |
> | 1102 | ~75 % |
> | 1080 | ~64 % |
> | **1057** | **~51 %** |
> | **1035** | **~40 %** ← start here |
> | 1012 | ~20 % |

**The sequence. Every step matters, including the waits.**

```
hpf track
beam on
beam phase 1035
```
…wait **3 s** — the HPF must settle at the new level before you freeze it.
```
beam off
```
…wait **3 s** — the node must return to baseline before you freeze it.
```
hpf hold
beam on
adc 5 100
```

**Read that number before sweeping.** You want **2000–2500 codes**.

| `adc 5` reads | do |
|---|---|
| **4095** | still railed — lower `beam phase` (1012, then 990) and repeat the whole sequence |
| **2000–2500** | ✅ go on to the sweep |
| **< 500** | too small to test the scale — raise `beam phase` toward 1102 |

Then:
```
threshold sweep 0 100 64
```

⚠ **Each retry needs the whole sequence**, not just a new `beam phase` — changing the phase
while in HOLD does not change the frozen value. Back to `hpf track` first, every time.

🔵 **Drift is not your problem here.** HOLD leaks ≤ 2.3 mV/s at ADC5 (board 2) and the sweep
takes ~1.3 s, so ~3 mV of walk against a ~2 V signal.

### Step 4 — put it back

```
hpf track
beam phase 1343
```

🔴 **Do not skip this.** `beam phase` is the live demod phase; leaving it at 1035 leaves the
detector 300 ticks off its peak, and nothing downstream will tell you.

### What you are looking for

| line | good | bad |
|---|---|---|
| a flip in **both** sweeps | yes | **NO FLIP in the high sweep almost always means ADC5 is railed** — check with `adc 5 100` before suspecting anything else. 4095 means the signal is above the DAC's 3.3 V ceiling and there is no crossing to find. |
| TP8 nominal **vs** ADC5 at flip, **low point** | agree to **~±15 mV** | a large gap means one scale is wrong |
| TP8 nominal **vs** ADC5 at flip, **high point** | agree to **a few %** | this is the one that tests the *scale*; the low point cannot, because it is swamped by offsets |
| `ADC5 movement across the sweep` | see the crosstalk note below — **the low sweep cannot measure this** | |

> ### 🔵 Board 2, 2026-08-25 — what the low point actually gave
>
> `FLIP at duty 0.08 % = 0.0026 V at TP8`, with `ADC5 there: code 15 = 0.0121 V`. The two
> disagree by **9.5 mV**.
>
> **That is a pass, and it is why the high point exists.** The flip landed between DAC level 0
> and level 1, so the threshold resolution there is the DAC's whole **3.2 mV** step; add the
> LM393's input offset (typ. a few mV, **max ±15 mV**) and 9.5 mV is comfortably inside what
> this measurement can resolve. **The low point tests the offset and proves both paths respond.
> It cannot test the scale** — for that you need a signal large enough that a 10 mV offset is
> negligible, which is the whole point of step 3.

**The agreement between the two voltages is the whole point.** TP8's number comes from the DAC
duty and an assumed 3.3 V reference; ADC5's comes from the ADC. They are independent paths, and
if they agree at both a 9 mV and a 2 V flip point then both are calibrated against each other
across the whole usable range.

⚠ **Put the HPF back afterwards:**

```
hpf track
```

> ### 🔵 Step 3 is also a free HPF polarity proof
>
> **The high point only works if HOLD actually holds.** You freeze the node, step the beam,
> and read a level seconds later. If the polarity were inverted, `hpf hold` would put the mux
> in TRACK and the step would decay with **τ = 0.66 s** — the `adc 5` read and the 1.3 s sweep
> both happen well after that, so there would be nothing to find and the sweep would report
> NO FLIP.
>
> **A flip at a large threshold is therefore proof that GPIO33 = 1 is HOLD**, independent of
> `hpf test`, the datasheet, and the compiled constant.
>
> ⚠ **It is not a substitute for `hpf test`**, which also records the polarity in the config
> and measures the switch leakage. But if you have a working step here and `hpf` still says
> HYPOTHESIS, the polarity is not what is wrong.

⚠ **The R102/D14 clamp is on ADC5, not on the threshold node.** `Threshold_DC` has no clamp at
all — its only nodes are C76.2, R89.2, TP8.1 and U15.2. So this sweep exercises the ADC path's
protection, not the threshold's.

### Step 5 — the GPIO44 crosstalk check, and why it needs the beam OFF

The 146.5 kHz DAC carrier is only **42 kHz** from the 104.17 kHz optical carrier. The two RC
poles should give ~120 dB, so anything visible is PCB crosstalk from the GPIO44 trace.

🔴 **Run this with the beam OFF.** `ADC5 movement across the sweep` is printed by every sweep,
but with the beam **on** that number is dominated by the optical background — the room return
that §3.3 measured at **2.9–14 mV** depending on geometry. Board 2 read **12.9 mV** on a
beam-on low sweep, which looks like a crosstalk failure and is almost entirely light.

```
beam off
hpf track
```
…wait **3 s**, then:
```
threshold sweep 0 100 64
```

🔵 **Ignore the FLIP line entirely here — it may or may not flip, and neither means anything.**
With the beam off ADC5 does not sit at zero; it sits at its TRACK quiescent, ~9–10 mV on
board 2. That is a real level and the comparator does cross it. But a 0–100 % sweep in 64 steps
has **51.6 mV granularity**, so a flip in the first step only tells you "somewhere below 51.6 mV"
— it cannot resolve where. (Board 2 reported `FLIP at duty 1.56 % = 0.0516 V` against an ADC5
reading of 9.7 mV. Consistent, and uninformative.)

**The only line that matters is `ADC5 movement across the sweep`.**

| movement, beam OFF | verdict |
|---|---|
| **< ~3 mV** | ✅ no meaningful crosstalk. Expect roughly the §3.3 quiet σ of 0.55–0.65 mV. |
| tens of mV | 🔴 real GPIO44 crosstalk — **move the DAC away in frequency**: `DAC_TOP` 2047 → 73 kHz, or 511 → 293 kHz, then re-run |

### What the two points actually give you

Two flips at very different levels solve a two-term model with no extra measurement:

```
ADC5 = vref * duty + Vos
```

where `vref` is the DAC's **effective** reference (the compiled value is a nominal 3.300 V) and
`Vos` is the offset between the comparator's decision point and what ADC5 reads — dominated by
the LM393's input offset voltage.

> ### ✅ Boards 2 and 3 — §3.5 COMPLETE
>
> | | board 2 (stock 470 kΩ) | board 3 (116 kΩ) |
> |---|---|---|
> | low flip | 0.23 %, TP8 7.7 mV, ADC5 18.5 mV | 0.23 %, TP8 7.7 mV, **ADC5 12.9 mV** |
> | high flip | 62.50 %, TP8 2062.5 mV, ADC5 2053.3 mV | 53.12 %, TP8 1753.1 mV, **ADC5 1735.0 mV** |
> | **DAC vref** | **3.268 V** | **3.256 V** |
> | **comparator Vos** | **+11.0 mV** | **+5.4 mV** |
> | crosstalk, beam off | 1.6 mV | **3.2 mV** |
> | `beam phase` used for the high point | 1000 → 2645 codes | **1035 → 2225 codes** |
>
> 🔵 **`vref` agrees to 0.36 % across two boards**, and both sit between the compiled 3.300 V
> and the 3.246 V measured on board 2's +3V3 rail — a consistent picture, and good evidence the
> two-point method measures what it claims to.
>
> ⚠ **`Vos` is a per-part LM393 parameter** and is expected to differ; ±15 mV is the datasheet
> limit and both boards are inside it.
>
> ⚠ **Board 3's beam-off crosstalk is 3.2 mV — right at the bar and 2× board 2's.** Not a
> problem against a working threshold of hundreds of mV, but worth watching. It will not scale
> with Rf: GPIO44 couples in **after** the ×14.5, so this is a board/layout difference and not
> a consequence of the TIA rework.
>
> ### ✅ Board 2, 2026-08-25 — the original two-point run
>
> | | duty | TP8 nominal | ADC5 at flip |
> |---|---|---|---|
> | low point | 0.23 % | 7.7 mV | 18.5 mV |
> | high point (`beam phase 1000`, ADC5 held at 2645) | 62.50 % | 2062.5 mV | 2053.3 mV |
>
> Solving both:
>
> | | |
> |---|---|
> | **DAC effective reference** | **3.268 V** (compiled 3.300; measured +3V3 was 3.246) |
> | **Comparator offset Vos** | **+11.0 mV** — inside the LM393's ±15 mV spec |
> | model vs each point | **exact to 0.1 mV at both** |
> | GPIO44 crosstalk, beam off | **1.6 mV** ✅ |
>
> 🔵 **Both paths agree once those two terms are accounted for**, across a **110×** range of
> level. That is the cross-calibration this section exists to produce, and it is a stronger
> result than either point alone: the low point is dominated by `Vos`, the high point by `vref`.
>
> ⚠ **Every printed threshold voltage therefore reads ~1 % high.** Apply it if you want with
> `threshold vref 3.268` — **RAM only, lost on reset.** It is deliberately not in the config
> record: 1 % on a threshold that gets tuned empirically in §3.7 does not justify another field
> in a fixed-size record (see `cal_duty` in `config_store.h` for why that record cannot simply
> grow). If absolute threshold accuracy ever matters, this is the number to persist, next to
> `adc5v_scale`.
>
> ⚠ **The 0.23 % low-point duty carries the sweep's 0.078 % granularity**, which is the
> dominant uncertainty: `vref` lands in **3.266–3.270 V** across that band. `Vos` is good to
> about ±2 mV.

### Record

`PROGRESS.md` §6: **both** flip points — duty, TP8 nominal volts and ADC5 volts for each — the
solved `vref` and `Vos`, and the **beam-off** crosstalk figure. Note the working threshold you
will use in §3.7; a sensible starting point is **~20 % below** the high-point flip, so a nominal
transit clears it but noise does not.

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
> `scan carrier` enforces this and refuses below 5 minutes; `cal demod` warns but proceeds.
> Record whether the chosen SNR figures were taken warm. See `PROGRESS.md` §6 and
> `NEXT_BOARD_REV.md` CR-12.

---

## 3.6b Q8 — prove a rail step cannot look like a ball

**Do this before you trust a single transit.** If the answer is bad, every measurement in 3.7
is contaminated by an artifact you have not yet characterised.

### The mechanism

`+2V5` is **not** a regulated reference — R75/R76 (10K/10K) buffered by U11C make it literally
**+5VA ÷ 2**, measured at 2.59 V on the 5.2 V rail. So the whole chain's reference **moves with
the rail**:

```
ΔV on +5V → ΔV/2 at virtual ground → through C81 → U12B x14.5 → Net-(U12B-OUT2)
                                                                  |
                                              U15.3, the comparator input  <- the trigger
                                                                  |
                                                    R102 1K + D14 -> ADC5  <- a copy, clipped
```

**Predicted coupling: Δ(comparator input) = ΔV_rail × 7.25.** A **100 mV** rail step becomes
**725 mV** — comfortably over a typical 0.1–0.5 V threshold, i.e. **a false trigger caused by a
power event.**

> ### 🔴 The ×7.25 lands on the COMPARATOR INPUT, not on ADC5. Probe the right pad.
>
> ⚠ **An earlier revision of this section said "scope +5 V and ADC5 … probe R102 pad 2", which
> is self-contradictory** — pad 2 is not ADC5. The two pads are different nets:
>
> | | net | range | why it matters |
> |---|---|---|---|
> | **R102 pad 2** | `Net-(U12B-OUT2)` — also U15.3, U12.7, R101.2 | **0 – 5.2 V** | **What the comparator actually thresholds. Probe THIS.** |
> | R102 pad 1 | `Comparator_ADC` — GPIO45 / ADC5 | 0 – 3.3 V, **D14-clamped** | what the firmware sees; a copy **truncated** at the ADC ceiling |
>
> 🔴 **Probing ADC5 saturates exactly when the answer starts to matter.** U12B is rail-to-rail
> on +5VA and can drive the comparator input to 5.2 V, while D14 holds ADC5 at 3.3 V. A rail
> step big enough to be interesting is a step ADC5 cannot show you.
>
> 🔵 **You have four channels — use three.** +5 V, R102 pad 2, and R102 pad 1 together give the
> coupling *and* a direct measurement of how much the D14 clamp truncates, which is CR-16's
> whole subject and has never been measured.
>
> ⚠ **Neither pad has a test point** — that is **CR-17**, and this test is the reason it is
> raised. Identify pad 2 by continuity to **U15 pin 3** (or U12 pin 7 on the TSSOP-14).

In **TRACK** mode the 0.66 s HPF removes it. In **HOLD** it does not. **Armed means HOLD**, so
the vulnerable state is exactly the operating state.

> ### 🔴 DO NOT use a plain beam on/off step. It changes the LIGHT far more than the RAIL.
>
> **Measured on board 3, 2026-08-28**, 50 MS/s on the comparator input and +5 V together:
>
> | | |
> |---|---|
> | rail step when the beam switches | **−20 mV** |
> | Q8 would predict at ×7.25 | **145 mV** at the comparator input |
> | **actually measured** | **+3801 mV** |
>
> **The optical term is 26× the electrical one**, so Q8 is buried and the apparent "coupling"
> comes out at **190×**. That is not a coupling ratio — it is the detector doing its job.
>
> ⚠ **Removing the target does not fix it.** Bare crosstalk is smaller but still larger than
> 145 mV on any board that can be calibrated at all, and a board with too little return to
> calibrate cannot run §3.4 either.
>
> 🔵 **The stimulus must change the rail WITHOUT changing the light.** Two ways, both free.
>
> **Method A (cleanest) — step the PSU, beam OFF.** Settle in TRACK, `beam off`, `hpf hold`,
> then turn the bench supply **5.20 V → 5.10 V by hand** and capture. Zero optical change, so
> whatever moves is Q8. 🔵 **A slow hand-turned ramp is exactly right, not a compromise:** the
> +2V5 divider is filtered at **31.8 Hz** (R75∥R76 = 5 kΩ with C66 1 µF), so a slow change is
> the **fully-coupled worst case** — the number this test wants. A fast transient is attenuated
> by that pole and would flatter the result.
>
> **Method B (cross-check) — beam step at the DEMOD NULL.** Set `beam phase` to a quadrature
> null from your §3.4 sweep, i.e. where the response crossed zero (**board 3: ~225 or ~973**),
> then run the steps below. 🔵 **Why it works:** the optical crosstalk is carrier-modulated, so
> the demodulator nulls it there — board 3's sweep read **±1 code**. The rail coupling is *not*
> carrier-modulated: it is a shift of the virtual ground that moves the whole analog section
> together, identically in both demodulator sign states. **The phase kills the optical term and
> leaves the electrical one untouched**, with the same ~0.79 A load transient.
> ⚠ **Put `beam phase` back afterwards.**

### Board state

| | |
|---|---|
| Rail | **up** |
| Beam | 104166 Hz / 25 % (~0.79 A load step). 🔴 **Method A: beam OFF throughout. Method B: `beam phase` at a NULL, not at the calibrated peak.** |
| HPF | **varies by step — that is the experiment** |
| ADC | `adcmode idle` |
| Target | leave it where §3.4 set it — with Method A or B the optical term is removed by the *method*, not by taking the target away |
| Gear | **scope, 3 channels: +5 V, R102 pad 2 (comparator input — the one that decides), R102 pad 1 (ADC5, the clipped copy).** Neither pad has a test point; see the block above and CR-17. |

### Step 1 — TRACK (the HPF should remove it)

```
hpf track
beam off
```
…wait 3 s, arm the scope, then:
```
beam on
```

**Record:** the ΔV step on +5 V, and the excursion at **R102 pad 2**. Note pad 1 (ADC5) too if
you have the channel — the difference between them is the D14 truncation.

### Step 2 — HOLD (this is the armed case)

```
hpf hold
beam off
```
…wait 3 s, then:
```
beam on
```

**Record the same numbers.** ⚠ **Do not linger in HOLD** — the node floats and drifts at
~2.3 mV/s (board 2), so take the step within a few seconds of switching to HOLD.

### Step 3 — the number

```
coupling = Δ(R102 pad 2) / ΔV_rail
```

**Compare against the predicted ×7.25.** ⚠ **Use pad 2, not ADC5.** If you compute it from ADC5
and the step was large, you will get a number that is too small purely because D14 clipped it —
and the error is in the safe direction, which is the worst kind.

### Pass criteria

- Coupling in **TRACK** is small — the HPF is doing its job.
- Coupling in **HOLD** is close to **×7.25**. If it is far off, the mechanism is not what the
  analysis says, and that is worth understanding before proceeding.
- **The excursion at the comparator input from a realistic rail step stays well under the
  threshold you found in 3.5.** A useful bar: under **20 %** of it.
  🔵 **Compare like with like:** §3.5 gives the threshold as a voltage at the comparator input
  (that is what `vref × duty + Vos` solves for), so pad 2 is the node both numbers live on.

**The TRACK-vs-HOLD difference is the Q8 effect isolated** — same stimulus, same optical
conditions, only the baseline freeze changes.

### If it fails

| Mitigation | Cost |
|---|---|
| Keep the rail stiff while armed — no load switching during an armed window | Constrains what the firmware may do while armed |
| Shorten the armed window | Fewer opportunities, does not remove the mechanism |
| Do not freeze the baseline (stay in TRACK) | Loses the frozen reference the detector wants |
| **Board fix — `NEXT_BOARD_REV.md` CR-02** | Regulate +2V5, or a unity diff amp taking TP9 − TP6 |

⚠ **This gets worse in Phase 6, not better.** The strobe bursts sag the rail *deliberately* —
that is why the supply monitor carries a 500 ms debounce.

> ### ⚠ Watch for common-mode pickup in the measurement itself
>
> Board 3’s capture showed **12 mV RMS of 60 Hz on BOTH channels, ratio 0.99, r = 0.998**.
> That is not circuit coupling — real coupling would be ~7.25× (or ~3.6× at 60 Hz once the
> 31.8 Hz pole is allowed for), never 1.00. It is **mains pickup on a shared measurement
> ground**, and it puts a ~12 mV floor under everything else in the capture.
>
> 🔵 **A ratio of exactly 1.00 between two different nets is the signature.** Shorten the
> ground lead and reference it at the board before trusting any small number here.
>
> 🔵 **But do not over-extrapolate from this test to the strobe.** The +2V5 divider is filtered
> at **31.8 Hz** (R75∥R76 = 5 kΩ with C66 1 µF), so `beam on`/`beam off` is effectively a DC
> step and this test measures the **fully-coupled worst case** — which is what you want here.
> A strobe transient is orders of magnitude faster and is attenuated by that pole: roughly
> **150×** for a ~200 µs event, only ~6× for a ~5 ms burst envelope. **Measure it in Phase 6;
> do not assume either extreme.**

Record the coupling ratio in `PROGRESS.md` §6 either way. It is the number that decides whether
CR-02 is required or optional.

---

## 3.6 Carrier frequency — a FIXED DESIGN CONSTANT, verified once

> ### 🔵 DECISION 2026-08-28: 104.1667 kHz is fixed for every board and every user.
>
> **Not scanned per board, not per user, not per site.** An earlier revision of this section
> treated the carrier as something each board picks for itself by maximum measured SNR. That is
> now explicitly rejected.
>
> | | |
> |---|---|
> | **What it costs** | possibly a few percent of SNR on any individual board |
> | **What it buys** | one `board.h`, one calibration matrix, one set of expected numbers, field-replaceable boards, and a support story that can be reasoned about |
>
> A per-board carrier would mean every board needing its own 10-minute scan, its own `cal model`
> re-fit, and its own recorded frequency — and any replacement repeating all of it. For a
> product that trade is not close.

### What that does NOT remove

`scan carrier` was doing two jobs, and only one of them goes away:

| job | status |
|---|---|
| **Optimisation** — "which carrier is best on *this* board?" | ❌ **dropped**, deliberately |
| **Robustness** — "does the chosen carrier sit near something that folds into the passband?" | ✅ **more important than before** |

The second one does not go away by fixing the frequency — **it gets sharper**, because one bad
choice is now bad on every board forever.

### The mechanism, and the margin 104.1667 kHz actually has

A square-wave demodulator responds at **odd** harmonics of the carrier, so interference at
`f_i` folds down to `|f_i − n·f_c|` for odd n. Anything under the **15.39 kHz** LPF corner lands
in the passband and looks exactly like a ball. The obvious candidate is the LM5157 boost at a
nominal **1.055 MHz**.

| n | n·f_c | distance from 1.055 MHz | folds? |
|---|---|---|---|
| 7 | 729.2 kHz | 325.8 kHz | no |
| **9** | **937.5 kHz** | **117.5 kHz** | no |
| **11** | **1145.8 kHz** | **90.8 kHz** | no |
| 13 | 1354.2 kHz | 299.2 kHz | no |

**The nearest odd harmonic is 91 kHz away from the boost, against a 15.4 kHz passband.** The
boost can drift **−9.6 % or +7.1 %** before anything reaches the LPF.

🔵 **Note what this does *not* let you do.** Odd harmonics are `2·f_c` = **208 kHz** apart, so
*any* carrier near 100 kHz has one within ±104 kHz of the boost. **You cannot design the
collision away by choosing a different frequency in this band** — you can only know where you
sit. That is why this is a verification, not an optimisation.

### Check 1 — measure the actual boost frequency. On every board.

`BENCH.md` Phase 0.5 step d already puts a scope on the LM5157 SW node to decide the snubber.
**Record the frequency while you are there** — it has never been written down on any board, and
it is the number the ±9.6 % margin above has to cover.

- [ ] SW-node frequency, board 1 / 2 / 3 → `PROGRESS.md` §6

🔴 **If the part-to-part spread approaches ±7 %, the margin is not comfortable and this becomes
a real design issue** — worth a snubber, a spread-spectrum option, or a deliberate carrier move
made once, for all boards.

### Check 2 — confirm σ_noise is flat near the carrier

**Not a ranking. A flatness check**, and a much better use of the command:

```
scan carrier 95000 115000 9
```

Nine points across ±10 kHz of the operating carrier, ~5 minutes.

| what you want | what it means |
|---|---|
| `sigma_noise` **flat** across the nine rows | nothing is folding nearby |
| `beam_noise_ratio` **near 1.0 and flat** | the beam is not adding noise over ambient |
| a **spike** in either | something folds close to the carrier — investigate before shipping |

🔵 **`beam_noise_ratio` is the column that matters here**, not SNR. It is σ_noise ÷ σ_floor,
i.e. how much noise the *beam* adds over ambient — which is exactly the folding this check
exists to find. Ignore `BEST by SNR`; it is answering a question you have stopped asking.

⚠ **Run it warm** (the command refuses below 5 minutes) and **with the phase model fitted**, or
every point is measured at a different phase error and the comparison is meaningless.

### If Check 2 shows a spike

That is a design finding, not a board finding. Repeat it on a second board before acting — a
spike on one board and not another points at that board's boost, not at the carrier choice.
Then decide **once, for all boards**, and record the reasoning here.

### ⚠ σ_noise still depends on the optical background

Unchanged and still true: **σ_noise is set by the optical background, not the electronics** —
0.65 mV (nothing returning), 2.91 mV (D11 covered), 6.5–14 mV (open to the room) on one board
in one session.

For the flatness check this matters less than it did for ranking: you are comparing nine rows
taken **within one run under identical optics**, so a common background cancels out of the
comparison. But 🔴 **a calibration target left in the beam is part of that background**, and it
is not what a ball looks like — so do not read the absolute σ_noise or SNR values as
operational numbers.

### Record

`PROGRESS.md` §6: the boost SW frequency per board, and the σ_noise / `beam_noise_ratio` spread
across the nine points, with the beam temperature and the lighting. **Not a "winning
frequency"** — there is no longer such a thing.

---

## 3.7 Ball transit — use a ramp, not your hand

### Board state

| | |
|---|---|
| Rail | **up** |
| Beam | **ON**, at the carrier you settled on, 25 % duty, **warm** |
| HPF | **`hpf hold`** — armed means HOLD, and that is the state you are characterising |
| ADC | `adcmode armed` |
| Threshold | set from 3.5 — start at the flip duty you measured, then back off ~20 % |
| Gear | **a cardboard ramp**, a golf ball, a ruler |

⚠ **`detect arm` does NOT set the HPF.** You must type `hpf hold` yourself. Arming with the HPF
in TRACK means the 0.66 s servo is fighting your transit.

🔵 **HOLD is safe for as long as you need.** Drift is ≤ 2.3 mV/s at ADC5 (board 2), so a 1–10 ms
transit walks ≤ 23 µV. It takes ~43 s to walk 100 mV. Arming well before a shot is fine; only
an arm-and-forget of many seconds needs thought. *(F6, closed 2026-08-21.)*

### Step 1 — the ramp gives you an independent speed reference

A ball released from height *h* on a ramp arrives at

```
v = sqrt(2 · g · h · 5/7)          (5/7 accounts for rolling inertia)
```

| drop height h | arrival speed v |
|---|---|
| 50 mm | 0.84 m/s |
| 100 mm | 1.18 m/s |
| 200 mm | 1.67 m/s |
| 400 mm | 2.37 m/s |

**That is a speed reference costing one piece of cardboard**, and it is how you validate the
firmware's speed math rather than eyeballing it. Hand-waving gives you repeatability you cannot
measure.

### Step 2 — set the geometry, or nothing reports a speed

```
detect path 20          # beam path width in mm -- MEASURE IT, do not guess
```

🔴 **No velocity is reported until this is set**, and deliberately so: a speed derived from a
guessed geometry looks authoritative and is wrong.

### Step 3 — arm and roll

```
hpf hold
adcmode armed
detect cond 0           # 0 nominal, 1 far 1.5x, 2 low-reflectance
detect clear
detect arm
```

Roll **20 balls** from the same height. Then:

```
detect
```

**Expect:** `passes` climbing by one per ball, and a `last` line per pass with a transit in µs.

### Step 4 — read the data out

```
detect log
```

CSV, one row per pass:
`seq,t_ms,cond,cmp_us,adc_us,peak,baseline,asym_q8,frag,thr,rate_khz,qual`

```
detect wave
```
The retained waveform for the most recent pass (only the **last 4** are kept, decimated ×8 —
full waveforms for 60 passes would need 960 KB against 520 KB of SRAM).

### What you are looking for

| | good | what it means if not |
|---|---|---|
| TP9 on a scope | a **smooth positive bump** per pass | if it is not smooth, the transit is not what you think |
| `cmp_us` vs `adc_us` | within a few % | the comparator and ADC disagree — that is Phase 4's whole subject |
| computed speed | within a few % of the ramp prediction | check `detect path` first, it is the usual culprit |
| `frag` | 1 | see below — **more than 1 is expected, not a bug** |
| `qual` | 0x00 | bits: `01` SAT `02` NOCROSS `04` WINCLIP `08` LAPPED `10` CHATTER `20` NOADC |

### ⚠ Chatter is expected. U15 has no hysteresis.

There is no resistor from `D_Comparator` back to U15 pin 3 anywhere on the board, so a
slow-slewing edge can cross the threshold several times. **`detect` reports a fragment count
per pass — that count *is* the chatter measurement.**

A chattered pass reports its comparator transit as a **lower bound** (the sum of fragment
widths, excluding the notches) and says so, because the gaps cannot be recovered from FIFO
arrival times. **Use the ADC-derived transit for those passes.**

If chatter is bad enough to matter: `detect coalesce <us>` (software merge), a PIO change to
measure the notch widths, or the board fix in `NEXT_BOARD_REV.md` CR-13.

### Step 5 — repeat under three conditions, for Phase 4

```
detect cond 1           # ball 1.5x further away
```
…20 more passes, then
```
detect cond 2           # low-reflectance ball
```
…20 more. Then:

```
detect stats
```

**This is the Phase 4 deliverable**: it prints per-condition means and standard deviations for
both timing methods, and fits **bias against 1/peak**. A fixed threshold crosses a smaller bump
later going up and earlier coming down, so if the comparator's error really is the amplitude
effect, bias is linear in 1/peak and that slope measures it. **A slope indistinguishable from
zero at your geometry means the ADC refinement can be dropped and the design simplifies.**

### Step 6 — pick R98

```
cal gain <peak>
```

using the `peak` your strongest condition produced. It solves the gain backwards and prints the
nearest E24 value.

🔵 **Set the gain from the WEAKEST target that must still trigger**; the ceiling comes from the
strongest, and is `min(ADC 3.3 V, LM393 common mode ≈ 3.5 V)`. U12B is rail-to-rail on +5VA and
can drive 5.2 V into a comparator that stops being valid at 3.5 V.

### Step 7 — persist

```
detect disarm
cfg save
```

`cfg save` refuses unless the machine is quiet — a 4 KB flash erase blinds the supply monitor
for tens of ms.

### Exit criteria

- TP9 shows a clean smooth positive bump per pass
- Computed speed matches the ramp prediction within a few percent
- `detect stats` has ≥ 2 conditions with data and prints the bias-vs-1/peak slope
- A gain decision is recorded (fit R98, or leave it out)
- `cfg save` succeeded and `cfg` reads back the carrier, phase, model, path and gain

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
