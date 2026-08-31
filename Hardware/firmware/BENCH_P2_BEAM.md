# Phase 2 — Beam carrier and demodulator clock

**Prereq:** Phases 1 and 1b passed.
**Power:** PSU 5.2 V, **current limit ≥ 2.5 A**. The beam alone averages ~0.95 A from
+5 V at 30 % duty.
**Pi:** not connected.
**Gear:** logic analyzer (2a), scope (2b), FLIR (2b), DMM.

Firmware for this phase is written and in the build. The beam cannot start at boot —
`beam_init()` configures the PWM slices but leaves GPIO31/39 as SIO outputs driven low,
so nothing happens until you type `beam on`.

---

**Status: ✅ PHASE 2 COMPLETE — 2026-08-13 on board 1, re-run on board 2 2026-08-21.**
Q1 (U9 clamp) = **122.68 µs** and Q2 (duty fidelity) = **no limit to 250 kHz** are both closed.
Carrier stays **104.1667 kHz**. Full results at the end of this document.

⚠ **The operating duty changed after this phase.** Phase 2 ramped and characterised the beam
at **30 %**; from Phase 3 onward the operating point is **25 %** — CR-12 measured the junction
at 123–133 °C at 30 % against a 145 °C maximum. The 30 % numbers below are still the correct
record of what was measured; **do not adopt 30 % as an operating duty.**

⚠ **`off` now also turns the beam off** (fixed 2026-08-24). On older builds the beam state
survived a rail-down and the next `on` relit D11 unasked.

## How to read the procedures below

Every sub-phase opens with a **board state** table. Set the board to exactly that before
starting.

---

## What you are actually bringing up

Two signals that must be *exactly* the same frequency with a controllable phase between
them, driving an optical chain whose whole noise immunity depends on that relationship:

```
GPIO31 Modulation_PWM  --> U9 74LVC1G123 --> U10 MCP1416 --> Q11 --> D11 (beam LED)
GPIO39 Demodulation_PWM --> U13 TMUX1219 --> U12A          (sign-switching demodulator)
```

**The MCU does not drive the LED.** U9 is a monostable wired A=GND, B=~CLR=Modulation_PWM.
A rising edge triggers it; a falling edge clears it immediately. So the LED follows the
input exactly for pulses shorter than the one-shot period, and is **hard clamped** above it.
There is no disable path on this watchdog, by design — a stuck-high Modulation_PWM gives
one clamped flash, not a cooked LED, and no DC beam mode can exist.

Two open questions get answered here, and both change downstream code:

| | |
|---|---|
| **Q1** | How wide is the one-shot clamp *really*? The .md says 113 µs; 0.7·R68·C57 = 0.7·56k·2.2n says **~86 µs**. This sets `STROBE_SW_MAX_US` for Phase 6, where the current software limit (100 µs) may be *above* the hardware limit. |
| **Q2** | ✅ **ANSWERED — no limit up to 250 kHz.** The premise was wrong: ~CLR resets C57 through an internal low-impedance transistor (~0.1–0.2 µs), not through R68's 123 µs *charging* RC. Those are different mechanisms. Carrier design stands. |

---

## 2a — Phase lock, on the logic analyzer, **with the LED dark**

Do this first. It is the trickiest code in the project and it costs nothing to prove
before any current flows.

### Board state

| | |
|---|---|
| PSU | 5.2 V, limit **≥ 2.5 A** |
| **Rail** | 🔴 **UP.** `beam_enable()` hard-refuses unless `power_rails_ready()`, so this cannot be run with the latch open. *(An earlier draft said "rails down"; that was never possible.)* |
| **Beam duty** | 🔴 **2 %** — this is what makes 2a safe, not the rail state. 0.19 µs high at 104 kHz is nothing thermally. |
| Carrier | 104166 Hz (or 1 kHz — see the alternative below) |
| Pi | not connected |
| Probes | **LA on GPIO31 and GPIO39** |
| FLIR | not needed at 2 % |

**Setup:** LA on GPIO31 and GPIO39, **rails up** — `beam_enable()` hard-refuses unless
`power_rails_ready()`, so this test cannot be run with the latch open. (An earlier draft of
this line said "rails down"; that was never possible.) What keeps 2a safe is not the rail
being off, it is the **duty being tiny**. Two ways to get there:

- **Preferred:** latch the rail with `on` and start at **2 % duty** — a **0.19 µs** high phase
  at 104 kHz is nothing thermally. (An earlier draft said 0.6 µs; that is wrong —
  9.6 µs × 0.02 = 0.192 µs, and level 28 of 1440 confirms it.)
- Or run the whole test at **1 kHz**, where U9 clamps everything anyway. Phase ticks are
  6.67 ns **regardless of carrier frequency** (clkdiv = 1), so `beam phase 360` is still
  exactly 2.4 µs — the phase measurement is identical, and GPIO31's high phase becomes
  500 µs instead of 0.19 µs. Worth it if your LA is slow; unnecessary at 100 MS/s or better.

### Where to put the probes

**Do not probe the RP2354 pins.** The chip pins are GPIO31 = U3 pin 39 and GPIO39 = U3 pin 48
(netlist-verified), but that is 0.4 mm pitch on a QFN-80 and there is no reason to risk it.
Both signals pass through 0 Ω links and have 1 K pull-downs, which gives four better pads:

| Part | Package | Signal | Use it? |
|---|---|---|---|
| **R38** (0R) | **0805 HandSolder** | Modulation_PWM (GPIO31) — **either pad** | ✅ **Best.** Big pads, easy to land a clip on |
| **R40** (0R) | **0805 HandSolder** | Demodulation_PWM (GPIO39) — **either pad** | ✅ **Best.** Same |
| R69 (1K) | 0402 | pin 2 = Modulation_PWM, pin 1 = GND | Adjacent ground pad is nice in theory, but **0402 is realistically inaccessible** |
| R91 (1K) | 0402 | pin 1 = Demodulation_PWM, pin 2 = GND | Same |

**Use R38 and R40.** They are 0 Ω links in a hand-solder 0805 footprint, so both pads are the
same node and either end works. (An earlier revision of this table recommended R69/R91 for
their adjacent ground pad — correct electrically, useless in practice at 0402. Proven on the
bench 2026-07-31.)

**Ground separately**, since R38/R40 have no local ground pad: **J5 pin 4 or 6** is convenient
and already identified from the Phase 1b jumpering. **A missing ground is the single most
likely reason for "no signal"** — it looks identical to a dead pin.

> **If one pad of R38 toggles and the other does not, the 0 Ω link is open** (tombstoned or
> cold-jointed). "Both pads are the same node" holds only if the part is actually soldered.
> Worth 10 seconds with a DMM before suspecting anything else.

Loading is a non-issue — a few pF against a 3.3 V CMOS driver at 104 kHz changes nothing.

### If you see nothing at all

Type `beam` and read the **hardware readback** block, which reports the actual PWM registers
rather than firmware's opinion of them. Healthy looks like:

```
PWM_EN   : 0x8e0  slice 7(car) ENABLED   slice 11(dem) ENABLED
funcsel  : GPIO31=4 ok   GPIO39=4 ok   (4 = PWM)
pad ISO  : GPIO31=0  GPIO39=0   (1 = pad ISOLATED, no output)
slice 7  : top 1439  cc 0x001c0000  div 0x0010  csr 0x01
slice 11 : top 1439  cc 0x02d00000  div 0x0010  csr 0x01
ctr(car) : 1192 -> 685 -> 1122   counting (slice is live)
```

`cc` packs both channels — **bits 31:16 are channel B**, which is what both these pins use
(`PWM_B_7`, `PWM_B_11`). So `0x001c0000` is B=28 (2 % carrier) and `0x02d00000` is B=720
(50 % demod). `div 0x0010` is 8.4 fixed point = integer 1.

**If all of that reads healthy and `ctr` is counting, the firmware is definitively driving
the pins** and the fault is in the measurement setup — go back to ground.

Netlist topology, for reference:

```
U3 pin 39 (GPIO31) --[R38 0R]--> Modulation_PWM  --> U9 pin 2 (B) + pin 3 (~CLR)
                                       |                (both tied together --
                                   R69 1K                 the monostable wiring)
                                       v
                                      GND

U3 pin 48 (GPIO39) --[R40 0R]--> Demodulation_PWM --> U13 pin 1 (SEL)
                                       |
                                   R91 1K
                                       v
                                      GND
```

### LA setup

You need **two channels only**, which on most instruments is exactly the case where the top
sample rate is available — use it. Capture length is trivial: a few carrier periods is
plenty, so **100 µs is generous**. Do not fill memory with a long capture; you will only
have more data to scroll through.

| Sample rate | Resolution | In ticks (1 tick = 6.67 ns) | Verdict |
|---|---|---|---|
| 500 MS/s | 2 ns | **0.3** | Sub-tick. Everything below is easy. |
| 100 MS/s | 10 ns | 1.5 | Fine |
| 24 MS/s | 41.7 ns | 6.3 | Checks 1–6 OK; check 7 needs the caveat below |

```
on                       # rail up
beam freq 104166
beam duty 2
beam on
beam                     # confirm: TOP=1439, level 28, actual 104166 Hz
```

> **Why 104166 and not 104167?** Either works on firmware built after 2026-07-31, which
> rounds to the nearest achievable period. On **earlier builds** `beam freq 104167` gave
> **TOP=1438** and 104239 Hz — 72 Hz high — because `150e6/104167 = 1439.995` truncated to
> 1439 before the code subtracted 1. **If you see TOP=1438, you are on an old build.**
> Reflash; TOP must be **1439** or level 432 is not exactly 30 %.

### Checks

| # | What | Pass |
|---|---|---|
| 1 | Both signals present, same period | 9.6 µs at 104.167 kHz |
| 2 | GPIO39 is a clean 50 % square | level = (TOP+1)/2 = 720 |
| 3 | `beam phase 0` → measure the GPIO31↔GPIO39 edge offset | reference point, record it |
| 4 | `beam phase 360` → offset moves by 360 ticks = 2.4 µs | **monotonic and exact** |
| 5 | `beam phase 720` (180°) | offset = 4.8 µs |
| 6 | `beam phase 1439`, then `beam phase 0` | wraps cleanly, returns to the reference |
| 7 | Re-run `beam freq 104166` several times | **the phase relationship must be identical every time** |

> **Analyse the CSV with `tools/la_phase.py`** rather than by eye — you will run this at
> least five times (checks 3–7) and again in Phase 3.
>
> ```
> python tools/la_phase.py digital.csv --ticks 0        # one capture
> python tools/la_phase.py run*.csv --ticks 0 --compare # check 7
> ```
>
> It auto-identifies which channel is which by duty cycle (the ~50 % one is the demod), so
> LA channel order does not matter, and it reports period, duty in ticks, the phase-offset
> histogram, and the pass/fail against the one-tick criterion.

**Check 7 is the important one.** It proves the atomic-enable trick works. If the offset
varies run-to-run, the two slices are not starting on the same clock edge and every
Phase 3 phase calibration will be built on sand. **A single capture at one setting cannot
demonstrate this** — you need several captures with a `beam freq` reconfiguration between
them, which is what `--compare` is for.

### ✅ Result, 2026-07-31 — **2a COMPLETE, all seven checks pass**

Analysed with `tools/la_phase.py` at 500 MS/s (2 ns sample period, 0.30 ticks).

| Check | | Measured | Error |
|---|---|---|---|
| 1 | Both signals, same period | **9600.00 ns** both channels → 104 166.7 Hz | — |
| 2 | Demod is a 50 % square | **4800.00 ns high = 50.000 %** (720.0 ticks) | — |
| 3 | `phase 0` reference | **0.00 ns** | 0.00 ticks |
| 4 | `phase 360` = 2400.0 ns | **2399.00 ns** | −0.15 ticks |
| 5 | `phase 720` = 4800.0 ns | **4800.00 ns** | **0.00 ticks** |
| 6 | `phase 1439` = 9593.3 ns | **9592.50 ns** | −0.12 ticks |
| 6 | back to `phase 0` | **−0.79 ns** | −0.12 ticks |
| **7** | **6 runs, `beam freq 104166` between each** | **spread 1.02 ns** | **0.15 ticks** |

Carrier high measured **186.51 ns = 28.0 ticks** (2 % of 1440); demod **720.0 ticks**. Every
per-capture spread was ≤ 2.0 ns = one LA sample.

**Tick scale: 6.6667 ns/tick** from the `phase 720` capture (4800.00 / 720), exactly nominal.
The 360 and 1439 captures gave 6.6639 and 6.6661 — agreement to 0.04 %.

**Check 7 is the one that matters and it is emphatic:** half an LA sample of drift across six
full `beam_configure()` teardown-and-reload cycles. The atomic enable is working.

> **A consistent ~−0.9 ns bias** shows up across every capture (offsets cluster near 9599
> rather than 0). It is **sub-tick**, so it cannot be a counter or firmware effect — the PWM
> can only place edges on 6.67 ns boundaries. It is LA inter-channel skew or a small pad-delay
> difference between GPIO31 and GPIO39. Harmless: Phase 3 calibrates `demod_phase_ticks`
> against the real optical signal, which absorbs any fixed skew in the chain.

> ⚠ **On the analysis tool.** The first version of `la_phase.py` reported checks 4, 5 and 6 as
> failures when the hardware was correct. Two bugs: it measured `carrier − demod` (the
> *negative* of the commanded phase, since `beam.c` preloads the demod counter to
> `period − phase`), and it used a nearest-edge search with no modular arithmetic — which
> breaks exactly at `phase 720` (both edges equidistant) and `phase 1439` (wraps to 6.67 ns).
> Both fixed with circular statistics. **If a phase reads as an exact negative or as
> `period − expected`, suspect the analysis before the board.**

### Exit criteria
Phase offset is exact, monotonic, wraps cleanly, and is **reproducible across
reconfiguration** — check-7 scatter under one tick (6.67 ns).

Record in `PROGRESS.md` §6:
- the **`phase 720` offset** (expect 4.800 µs) and the tick scale derived from it, 4800/720
- the **check-7 scatter** across ~10 reconfigurations, in ns

---

## 2b — The beam LED, ramped

🔴 **Now current flows.**

### Board state

| | |
|---|---|
| PSU | 5.2 V, **limit 2.5 A**. The beam alone averages ~0.95 A from +5 V at 30 % duty. |
| Rail | **up** |
| **Beam duty** | 🔴 **starts at 2 % and ramps.** Never jump straight to the target. |
| Carrier | 104166 Hz |
| Pi | not connected |
| **FLIR** | 🔴 **powered on and pointed at R73/R74 (the 0R27 ballast pair) and D11 for the whole ramp** |
| Probes | scope — see "Where the scope ground goes" below **before clipping anything** |

⚠ **Ceiling:** 35 % is a *destruction* limit, not an operating point. The Phase 3 operating
duty is **25 %** (CR-12).

### The drive chain, verified against the netlist and BOM

```
+5V ──▶ D11 anode (pads 2 AND 3, tied)          D11 = VSMA1085250x02, 850 nm
        D11 cathode (pad 1)                     "double stack emitter chip"
              │                                  = TWO DIES IN SERIES, Vf ~ 3.4 V
              ▼
            R73 (0R27, 2512)
              ▼
            R74 (0R27, 2512)        R73 + R74 in SERIES = 0.54 Ω = 0.54 V/A
              ▼
       Q11 drain  ◀── TP5           Q11 = AO3400A, low-side N-channel
              │
       Q11 source ──▶ GND
```

The series stack is what makes the ballast value correct:
`5.2 V = 3.4 (LED) + 0.54·I + 0.15 (Q11 on)` → **I ≈ 3.05 A**. Two dies in *parallel*
(Vf ~1.7 V) would pass 6.2 A through the same resistors.

### 🔴 Where the scope ground goes — read before clipping anything

**Never put a probe ground clip on TP5.** A standard probe's ground lead is bonded to the
scope chassis and to earth. TP5 is Q11's **drain**, swinging 0.15 V to 5.2 V. Clipping ground
there shorts the drain to earth, so:

- The LED conducts **continuously, DC**, at (5.2 − 3.4)/0.54 ≈ **3.3 A through your scope's
  ground lead**.
- It **bypasses U9's clamp entirely.** The one-shot protects by limiting *gate* drive; a clip
  across the drain makes the probe the switch, and that switch never opens.

> ⚠ **The net is named `Strobe_GND` in the schematic and it is NOT ground.** It is the
> switched drain. That name is precisely the trap that leads someone to clip a ground lead
> to it. (Renaming it is `NEXT_BOARD_REV.md` CR-11.)

**Ground goes to board GND only** — J5 pin 4 or 6, the same points used for the 2a jumpers.

### Scope points

| Measurement | How | Expect |
|---|---|---|
| **TP5 switching waveform** (2c) | **Single-ended:** tip on TP5, **ground clip on board GND** | ~5.2 V off, **≤ 0.15 V** on-phase |
| **LED current** (2b) | **Differential across R73+R74:** ch1 tip on **D11 pad 1 (cathode)**, ch2 tip on **TP5**, *both* grounds to board GND, display **ch1 − ch2**. Or a true differential probe. | **0.54 V/A** → ~1.6 V at 3 A |

> **TP5 is inverted relative to the LED.** When TP5 is **LOW**, Q11 is on, current flows and
> the LED is lit. So in 2c you measure the **LOW** pulse width at TP5 — that is the LED-on
> time and therefore the U9 clamp. Do not measure the high period.

R73/R74 are **2512** parts, physically large and easy to land a probe on.

### D11 ratings, and how much margin the design point has

| | |
|---|---|
| Max **DC** forward current | **1.5 A** |
| Max **pulsed** forward current | **5 A** |
| Thermal resistance R<sub>thJSP</sub> | **6–9 K/W** |
| Wavelength / half-intensity angle | 850 nm / ±28° |

At the 30 % design point: **peak 3 A** (60 % of the pulsed rating) and **average 0.9 A**
(60 % of the DC rating). At 104 kHz the 9.6 µs period is far shorter than the die's thermal
time constant, so **thermally it behaves as DC at the average current** — the 0.9 A figure is
the one that matters.

Dissipation ≈ 3.4 V × 0.9 A = **3.06 W**, so junction rise over the solder point is at most
3.06 × 9 = **~28 °C**. **FLIR check:** if D11's package reads much more than ~30 °C above the
surrounding board, either the ballast is passing more than 3 A or the thermal path to the pad
is poor.

### The ramp

```
beam freq 104167
beam duty 2
beam on
beam ramp 30 500          # 1 % steps every 500 ms — ~14 s, watch it climb
```

**After every few percent, check:** TP5 on-phase level, +5 V rail sag, PSU current, and
the FLIR. Expected at 30 %: ~0.95 A average from +5 V, ~3.15 W in D11, ~0.73 W in each
ballast resistor (3 W parts, so ~25 % of rating).

**Abort the ramp** (`beam duty 2`) if anything climbs faster than linearly with duty, or
if the ballast resistors run away. `beam duty` refuses above 35 % as a backstop.

> **FLIR technique:** the ballast resistors and D11's package read reasonably (ε ≈ 0.9),
> but **any exposed metal reads falsely cool**. Put a scrap of electrical tape on shiny
> parts. Take a reference image at 2 % duty before you start so you have an A/B.

### ✅ Thermal result, 2026-08-13 — measured, with heatsink, still air, 23 °C ambient

| Duty | LED base | Junction (base + 6…9 K/W) | Verdict |
|---|---|---|---|
| off (rails latched) | 37.4 °C | — | boost / R15-D4 floor, +14 °C over ambient |
| 15 % | ~60 °C *(predicted)* | 70–75 °C | comfortable |
| 20 % | ~72 °C *(predicted)* | 85–91 °C | good |
| **25 %** | **87.5 °C (measured)** | **104–112 °C** | **OK for bench** |
| **30 %** | **104 °C (measured, 10 min)** | **123–133 °C** | ❌ **only 12–22 °C from T_j max 145** |

> 🔬 **Measure the LED at its BASE, from the side — never through the lens.** A reading taken
> off D11's domed top gave **53.6 °C** while the true base temperature was ~104 °C. A curved
> specular surface loses effective emissivity with viewing angle, and the error was over 50 °C.
> **Sanity rule that catches it instantly: D11 must read hotter than the heatsink it feeds.**
>
> **D11 and the ballast resistors run at the same temperature.** Not a contradiction — D11
> carries 4× the power (3.23 W vs 0.81 W) through a ~4× better path (25 vs 100 K/W), and they
> are millimetres apart on shared copper.
>
> ⚠ **The 30 % figure was still rising at 10 min** (+4 °C over the second five). There is a
> slow board/heatsink time constant well beyond the ~70 s local one, so treat 104 °C as a
> **lower bound**; a true plateau needs 20–30 min.

**It plateaus** — τ ≈ **70 s**, settled within ~5 min. The thermal path works; heat leaves as
fast as it arrives. **R_th package→ambient ≈ 24 K/W**, and that single number predicts both
measured duty points, so use it to plan any other duty/ambient combination:
`T_pkg = T_amb + 24 × P`, where `P ≈ 3.4 V × 3.1 A × duty`.

**Where the resistance lives:**

| Stage | R_th | Share |
|---|---|---|
| Junction → solder point | 6–9 K/W | datasheet, fixed |
| **Base → heatsink** | **~8.7 K/W** | **35 %** — 104 → 76 °C at 3.23 W |
| **Heatsink → ambient** | **~16.4 K/W** | **65 %** |

**Airflow is the bigger lever, but the interface is no longer negligible** — an earlier
estimate of ~2 K/W came from the bad lens reading and was wrong by 4×.

| Fix | Total R_th | Sustainable duty @ 23 °C |
|---|---|---|
| As-is | 24.5 K/W | ~24 % |
| **Airflow only** | ~14.7 | **~34 %** ✅ |
| Interface only | ~19.6 | ~28 % |
| Both | ~9 | ~45 % |

⚠ **Ambient does a lot of work in this table.** It was taken at 23 °C in open air. An
enclosure at 40 °C shifts every figure **+17 °C**, which drops the sustainable duty to
**~18–20 %**. Since the beam must stay on the whole time the system is armed, this is a
design constraint on optical power and therefore on Phase 3 SNR — not just a bench note.
See `NEXT_BOARD_REV.md` CR-12.

**Do not sit at 30 % for long** on an open bench without airflow. It is the design
operating point but nothing is heatsinked for continuous duty at this stage.

### Confirm it is actually emitting
850 nm is invisible. Most phone cameras see it — point one at D11. The real proof is
TP7 in Phase 3.

---

## 2c — Q1: measure the one-shot clamp

### Board state

| | |
|---|---|
| PSU | 5.2 V, limit 2.5 A |
| Rail | **up** |
| Beam | **`beam clamp` sets it for you** — 1 kHz / 50 %, and it *enables* the beam as it does so |
| Pi | not connected |
| Probes | **TP5** (`Strobe_GND`, the shared low-side return) |

⚠ **`beam clamp` turns the LED on immediately** — it reconfigures *and* enables, so you have a
signal to scope straight away. That is intended, but it means the beam goes live the moment you
type it. At 1 kHz / 50 % the U9 clamp holds the real LED duty to ~12 %, which is why this is
safe.

```
beam clamp        # sets 1 kHz / 50 %, i.e. a 500 us commanded high phase
```

**`beam clamp` turns the LED on immediately** — it reconfigures *and* enables, so you have a
signal to scope straight away. That is intended.

### What "commanded high phase" means

The MCU never drives the LED. GPIO31 drives **U9**, a monostable with **B and ~CLR tied to the
same signal** (netlist: U9 pins 2 and 3 both on `Modulation_PWM`). A rising edge triggers it;
a falling edge clears it immediately. So:

```
LED on-time = min( commanded high phase , t_w )        t_w = K x R68 x C57
```

The commanded high phase is simply the MCU's high time. **Make it far longer than t_w and the
one-shot becomes what ends the pulse — so the width at TP5 is t_w.** That is the measurement.
It also needs a period much longer than t_w, which is why this drops from 104 kHz (9.6 µs
period, shorter than the clamp) to 1 kHz.

Scope **TP5** and measure the **LOW** width — remember TP5 is inverted, low = LED conducting.

| | |
|---|---|
| Commanded high phase | **500 µs** (1 kHz, 50 %) |
| Expected clamp | **~86 µs** (0.7 x 56k x 2.2n); .md claims 113 µs |
| LED duty at 86 µs | **8.6 %** — well under the 25 % you have already run |

> ⚠ **If the LOW width reads ~500 µs, that is NOT the clamp** — it means the one-shot is not
> terminating the pulse and you are seeing the input width. Do not record it as t_w.

> **Fixed 2026-08-13:** this command used to print "1 kHz / 500 µs" while actually producing
> **2289 Hz / 218 µs**. `beam_configure()` hardcoded clkdiv = 1, and 1 kHz needs 150 000 counts
> against a 16-bit counter, so TOP silently clamped to 65535. The measurement still worked
> (218 µs > 86 µs) but the LED ran at ~20 % duty, not the ~9 % the procedure assumes, and a
> reading of 218 µs could have been mistaken for the clamp. `beam_configure()` now engages a
> clock divider below ~2289 Hz. **Type `beam` after `beam clamp` and confirm `TOP=49999,
> clkdiv=3, 1000 Hz`** — if you see TOP=65535, you are on an old build.
>
> Note that with clkdiv > 1 a phase tick is `clkdiv x 6.67 ns`. Only the clamp command goes
> there; 2a runs at 104 kHz where the divider is always 1.

> 🔴 **When you are done here, type `beam duty 2` before anything else.**
>
> `beam clamp` writes the duty to **50 %** and it *persists*. The 35 % ceiling is checked
> only inside the `beam duty` command — `beam freq`, `beam clamp` and `beam sweep` all call
> `beam_configure()` directly and skip it. So a bare `beam freq 104167` after this step
> gives you **104 kHz at 50 % duty**: ~1.6 A average against a 0.95 A design point, and
> ~5.25 W in D11 against 3.15 W.
>
> **U9 will not save you.** Its one-shot clamps pulse *width* (~86 µs); a 50 % high phase
> at 104 kHz is 4.8 µs, nowhere near the clamp. Nothing in hardware or firmware stops this.
>
> Step 2d below happens to be safe because it opens with `beam duty 30`. Any other path out
> of 2c is not. The permanent fix is to move the ceiling into `beam_configure()` — see
> `PROGRESS.md` §9.

- .md claims **113 µs**
- 0.7 · R68 · C57 = 0.7 · 56 kΩ · 2.2 nF = **~86 µs**

**Record the real number in `PROGRESS.md` §6.** Then update `board.h`:

```c
#define STROBE_HW_LIMIT_US_ASSUMED  <measured>
#define STROBE_SW_MAX_US            <0.85 × measured>
```

This matters more than it looks: if the true clamp is 86 µs, the current 100 µs software
limit is *above* the hardware limit, and every slow-ball strobe pulse would be silently
truncated by hardware instead of controlled by firmware. **Also re-derive the .md §15
slow-ball rows** — at 10 m/s the 1 mm blur budget already wants 100 µs, so a clamp-limited
case means the blur budget grows or the pulse count shrinks. Decide it explicitly.

U5 (the strobe one-shot) is the identical circuit, so this measurement predicts it — but
verify U5 independently in Phase 6a.

---

### ✅ Result, 2026-08-13 — **Q1 answered: t_w = 122.68 µs**

| | Measured |
|---|---|
| **LOW width (LED on) = the clamp** | **122.680 µs**, spread **0.22 µs** over 1291 pulses (0.18 %) |
| HIGH width | 314.220 µs |
| Period | 436.900 µs |
| TP5 low / high level | −0.007 V / 3.637 V |

**Neither prior estimate was right, and both were low:**

| | t_w | implied K in t_w = K·R·C |
|---|---|---|
| 0.7 · R68 · C57 | 86 µs | 0.70 |
| .md's claim | 113 µs | 0.92 |
| **Measured** | **122.7 µs** | **0.996** |

K is essentially **1.0**. The 0.7 coefficient is a TI datasheet condition; the fitted part is
LCSC C26159250 and its coefficient differs.

**The Phase 6 concern is inverted, favourably.** 122.7 µs is *above* the 100 µs
`STROBE_SW_MAX_US`, so firmware controls the pulse width, hardware never truncates, and the
.md §15 slow-ball rows need no re-derivation. **Keep `STROBE_SW_MAX_US` at 100 µs.**
Still to confirm on **U5** — same part and RC, expect 109–136 µs with tolerance — in Phase 6a.1.

> **Captured on the pre-fix firmware** (2289 Hz, 218 µs commanded). Still valid: 122.7 < 218,
> so the one-shot genuinely terminated the pulse, with 1.8× headroom. On the fixed build the
> headroom is 4× and the LED sits at **12.3 %** duty rather than the **28 %** this capture ran
> at. Re-running is optional confirmation and thermally lighter.

---

## 2d — Q2: duty-fidelity sweep

### Board state

| | |
|---|---|
| PSU | 5.2 V, limit 2.5 A |
| Rail | **up** |
| Beam | **on**, duty set by the procedure below |
| Pi | not connected |
| Probes | **TP5** |
| FLIR | worth keeping on R73/R74 — the sweep holds the beam on for 25 × 3 s |

⚠ **This sweep was run at 30 % duty**, which was the Phase 2 operating point. If you re-run it
on a new board, **use 25 %** and expect the same answer — Q2's conclusion (no duty-fidelity
limit up to 250 kHz) is about U9's reset path, not about the duty.

```
beam duty 30
beam sweep 5000 250000 25 3000
```

At each step, measure the **actual** duty at TP5 and compare to the commanded 30 %.
The command prints the requested/actual frequency, TOP and level for each step.

**You are looking for the frequency where TP5 stops tracking the commanded duty.** U9's
timing node (R68 56 kΩ / C57 2.2 nF, τ ≈ 123 µs) has to reset via ~CLR during each low
phase — only 6.7 µs at 104 kHz. If it cannot keep up, actual duty falls below commanded
and the carrier frequency choice has to move.

Record the breakdown frequency. If it is below ~104 kHz, **stop** — the carrier design
changes and Phase 3 needs rethinking.

### ✅ Result, 2026-08-13 — **no breakdown anywhere in the range**

Swept 5 → 250 kHz at 30 % commanded. At the top of the range:

| At 250 kHz | Measured |
|---|---|
| Period | **4.0 µs** |
| TP5 low (LED on) | **1.2 µs** |
| **Actual duty** | **exactly 30 %** |

**Why it passed, and why the question was mis-framed:** this section warned that a 123 µs
timing constant must reset inside a 6.7 µs low phase. Those are two different mechanisms.
The 123 µs is C57 **charging** through R68 — that is what sets t_w (122.7 µs, measured in 2c).
The **reset** does not go through R68 at all: ~CLR discharges C57 through an internal
low-impedance transistor, on the order of **0.1–0.2 µs**. Against a 2.8 µs low phase at
250 kHz that is an order of magnitude of headroom, so there was never a recovery problem.

**Consequences:** the carrier stays **104.167 kHz**, and **Q3's search space is unconstrained
up to at least 250 kHz** — `scan carrier` can optimise purely for SNR without a hardware
ceiling in the way.

---

## Exit criteria for Phase 2

- [x] Phase offset exact, monotonic, wrapping, and reproducible across reconfiguration (2a check 7)
      — **scatter 0.15 ticks over 6 reconfigurations**
- [x] Commanded duty reproduced at TP5 at the operating carrier — **exact 30 % to 250 kHz**
- [x] **U9 clamp measured** (**122.68 µs**) and `board.h` updated — `STROBE_HW_LIMIT_US_ASSUMED`
      86 → 122, `STROBE_SW_MAX_US` **73 → 100** (the old 73 was *below* the .md §15 slow-ball
      requirement). **Verify on U5 in Phase 6a.1** — that is the part the constants govern.
- [x] **Duty-fidelity limit found** — **none up to 250 kHz** (Q2)
- [x] Thermals sane at 30 % — plateaus, nothing running away
- [x] Peak LED current confirmed — **3.11 A cold / 3.165 A hot** via the 0.54 V/A ballast

## ✅ PHASE 2 COMPLETE — 2026-08-13

All exit criteria met. Q1 and Q2 both answered; Q3 (carrier choice) passes to Phase 3 with an
unconstrained search space.

**Outstanding but not blocking Phase 3:**
- **D11 emissivity check** — put matte tape on the LED package and re-read. The 53.6 °C
  reading came off a domed lens, which is the classic falsely-cool surface. It decides
  whether `NEXT_BOARD_REV.md` CR-12 is a real constraint or a misattribution.
- **U5 clamp** — Phase 6a.1, sets the strobe constants for real.

Record everything in `PROGRESS.md` §6.
