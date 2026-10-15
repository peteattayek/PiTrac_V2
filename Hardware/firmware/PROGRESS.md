# PiTrac RP2354 Firmware — Progress & Resume Context

**Read this first when resuming work.** It records what is done, what is next, and the
decisions/measurements that must not be lost between sessions.

Last updated: **2026-10-12** (board 1 runs the readback-fix image `Oct 12 2026 14:43:50`; both J3 joints were cold and are re-soldered; 6c Step 2 PASS with the load; Step 3, the first real current, next). **Current status is in §0 and the TOP block of §10.**

**History moved 2026-10-05:** the 2026-08-24 overview, the Phase 2 bring-up fix table and the
old companion-docs table are in [`PROGRESS_ARCHIVE.md`](PROGRESS_ARCHIVE.md), verbatim. The
document map and reading order are in [`../README.md`](../README.md) and `HANDOFF.md`.

---

## 0. Where things stand

| | Status |
|---|---|
| **Handoff** | 🔵 **New to this work? Read `HANDOFF.md` first** (conventions, safety, tooling pitfalls, reading order; for a whole-repo developer overview see `DEVELOPER_GUIDE.md` at the repo root), then §10 for the live resume point. There is **no separate plan file** — the `~/.claude/plans/…` paths this table used to cite no longer exist. PROGRESS is the plan. |
| **Boards** | **Board 3** — UID `6d6fda754e367a40`, **reworked TIA (Rf 116 kΩ, Cf 0.99 pF)**, calibrated and saved (carrier 104166 Hz, phase 1311 ticks, slot A seq 5); runs the `Oct  5 2026 11:13:55` image; 6a.1 showed **no pulse at R61** (2026-10-05). **Board 2** — stock 470 kΩ TIA, calibrated; **a probe on R61 sparked 2026-10-05**, damage check pending. ⚠ One of boards 2/3 reads **10 Ω from R61 to GND even with R62 removed** (2026-10-06) — parked, R62 off; which board is to be confirmed (§6). 🟡 **Board 1** — UID `a764f5332ca5ac53`; U11B destroyed 2026-08-17 by foil across D12 (§11): **out of service for optics**, **the strobe board** — fast-path bring-up and **6a/6b PASS 2026-10-06**, TP3 loop stability **PASS 2026-10-08**; runs the **fix image `Oct 12 2026 14:43:50`** (flashed 2026-10-12; before that the `Oct  7` 6c build, §6); leads soldered to R33 (GPIO25), R61 pad 1 (U5 Q), TP3, TP4 and two on R66's GND pad; J3 pins 1 and 2 re-soldered (cold joints, 2026-10-13); LED leads twisted; **1 µF X5R at Q9's gate** (CR-23 stage-1 rework, verified 2026-10-15). |
| Toolchain | Installed — VS Code Pico extension, private copies in `%USERPROFILE%\.pico-sdk`. SDK 2.3.0, toolchain 15_2_Rel1, ninja 1.13.2, cmake 4.3.4. ⚠ **Not on PATH** — see `HANDOFF.md` for the export line. |
| Bench equipment | Saleae **Logic Pro 8** (analog inputs **±10 V / 12 V max**, ~5 MHz analog bandwidth, up to 50 MS/s); **Tek MSO54B** (500 MHz, 4 ch, TPP1000 probes — **available again 2026-10-08**); Tektronix **TDS 1002** (60 MHz, 2 ch, 8-bit; the fallback, 2026-10-07); DMM, current-limited PSU, FLIR thermal camera. |
| **Firmware** | ✅ **Board 1 runs fix image `Oct 12 2026 14:43:50`** (Release, flashed 2026-10-12): **194344 text / 0 data / 122796 bss**, SHA256 `2D3BD0C1…4FB6471D`, **102/102 host tests**, 25 mutations all caught. Six review rounds (Sol passed round 6). Then the bench found **the ADC running at half rate (DIV 95)**, now fixed, along with first-pass snapshot brackets (§6, 2026-10-12 bench entry). It fixes the ADC0 stale-baseline readback found at 6c Step 2 (§6 2026-10-12). On `14:43:50`, board 1 passed `adc ringtest 100` (200/200 covered) and 6c Steps 1–2 (§6). It replaced `Oct  7 2026 08:07:33` (Release, 184440 text / 0 data / 122720 bss, SHA256 `DE0C6676…2DE80BC2`, 78/78), on which one live pulse at gate 0 latched a false `STROBE_CLAMP` (2026-10-12; no current). 🔴 **Never fire live on the `Oct  7` image.** Board 3 still runs `Oct  5 2026 11:13:55` (13/13 smoke PASS). The 6c build is the 2026-10-05 image plus **strobe 6c/6d live mode** (`strobe live on confirm`: pulses with a setpoint under the staircase / 70 % ceiling / pacing / charge guards, ADC0 readback and verdict per firing, `STROBE_OVERCURRENT` and `STROBE_CLAMP` faults, watchdog armed by the strobe, CLI allowlist, `strobe cal`, `strobe wave`), the "before 6c" §9 items, and two fixes from an independent review (§6 2026-10-07). Dry behaviour (6a/6b) is unchanged. Written: power FSM, safe state, beam carrier + demod, ADC engine (DMA ring, block capture, **triggered capture**, freeze-and-copy), detect (threshold DAC, gated HPF, comparator PIO timing, pass log + retained waveforms), cal (`cal demod`, `cal model`, `scan carrier`), dual-slot config store, service/yield layer, shot-sequencer **skeleton**, **strobe 6a–6d** (PIO0 burst engine + DMA, gate DAC, §15 schedule, charge interlock, A7 fix, live mode), CLI (every numeric argument strictly parsed). ❌ **Not written:** reported ball velocity, Phase 5 onset detector + mic veto, the strobe in the shot sequencer (Phase 7), persisting the strobe current LUT, Phase 7 camera handshake, Phase 8.6 halt telemetry. Phase 8.0–8.5 power/shutdown firmware exists and passed against a **simulated** Pi (1b); no real Pi has been connected. **Committed 2026-10-09 as `a8e08b3`** (all Phase 6 work, tests and docs), at the owner's request. |
| **Bench work done** | ✅ Phases 0, 0.5, 1, 1b, 1c, 2 previously passed; **power-request regression found 2026-09-18**, fixed, and ✅ **BENCH.md Test 6 PASS (owner-reported 2026-09-28)**; see §6/§10. ✅ **Phase 3 §3.1–3.6 on board 3** — §3.6b is a *design finding* (Q8 ×7.43, CR-02), §3.6 Check 2 PASS. ✅ **Phase 5 mic bring-up** (2026-08-31). ✅ Board 3 detect control-path smoke and **first real optical ball waveform** (2026-09-18), **3.102 V peak / ~22.7 ms FWHM**, no observed clipping. Comparator edge timing and full §3.7/Phase 4 remain open. ✅ **Strobe 6a and 6b PASS on board 1 (2026-10-06)** — U5 clamp 135 µs, LA timing, schedule, A7, interlock, gate DAC (§6). |
| **Next** | **Strobe 6c on board 1** (`BENCH_P6_STROBE.md` 6c): ✅ Steps 0, A, 1, 2 (2026-10-12; Step 2 re-run on `14:43:50` after the readback fix). 🔴 **Step 3 stopped 2026-10-13: no current from gate 3 to 33 %. Cause: a cold joint on J3 pin 1, so the bank never saw VIR; re-soldered by the owner (§6). Step 2 re-run at gate 0 with the load: PASS. Then the staircase again reached 30 % with no current: **J3 pin 2 was also cold**, re-soldered. Step 2 then passed with the bank fully connected (VIR 36 V at J3). First current at gate 21 % (2026-10-13), but TP4 is not flat (~1 MHz ripple, decaying body) and ADC0's half-peak threshold mis-measures it. 🔴 **Step 3 stopped at 24 % on 2026-10-13 (3.3 A overshoot); **board-1 gate-drive rework verified 2026-10-15** (flat 0.70 A at 24 %, CR-23). ✅ **6d PASS at 24 % on 2026-10-15** (U5 ends 0.70 A at 135 µs of 1000 commanded, §6). Next: the readback fix (§9), with review, before anything above 24 %.** (1) ✅ Step 0 done 2026-10-12: `id` confirmed `built : Oct  7 2026 08:07:33`, board in STANDBY; ✅ TP3 loop stability **PASS 2026-10-08** (MSO54B, §6); leads soldered per the 6c lead plan; (2) 6c Steps 1–6: refusals and arming, the first live pulse at gate 0, the 3 % staircase with ADC0 against TP4 (scope 20 MHz BW limit), **6d inside Step 3** (✅ 2026-10-15, at 0.70 A), `strobe cal 9`, bursts with the FLIR. Also: identify the parked 10 Ω board (Step −1 on boards 2 and 3). Optical acquisition-window constraint (~59.8 ms needed vs 32.768 ms ring at 500 ksps), §3.7 20-pass set and CR-18 mic impact remain open, deferred while strobe work proceeds. |

*Closure notes for Phases 1, 1b and 2 (the 2026-07-30 supply-monitor bug and fix, Q1/Q2) are
in [`PROGRESS_ARCHIVE.md`](PROGRESS_ARCHIVE.md) §0, verbatim.*

---

## 0.5 What is SETTLED vs what REPEATS — read this before planning any bench session

Work on this project splits into two kinds. Confusing them wastes bench time in both
directions: redoing settled physics, or trusting a "known" constant that is actually per-board.

| | **DESIGN VALIDATION** | **PER-BOARD** |
|---|---|---|
| asks | *how does this design behave?* | *does THIS board work, and what are ITS constants?* |
| done | **once, ever** | **every assembled board** |
| driven by | the `BENCH*.md` docs | 🔵 **`BRINGUP_NEW_BOARD.md` — that document is the driver** |
| recorded in | §6 and §8 of this file | the sign-off table in `BRINGUP_NEW_BOARD.md`, then copied to §6 |
| redo when | the design changes — a respin, or firmware that touches the mechanism | every board |

### ✅ SETTLED — established once, do not re-derive

| what | where established |
|---|---|
| RP2350 erratum **E9** does not bite on A4 silicon (Q5) | Phase 0, `pins` all read 0 |
| An RP2354 **reset is a hard power cut** to the Pi, not a reboot | Phase 1 + `PROGRESS` §2 |
| The full **Pi soft-shutdown FSM matrix**, both timeouts, RPI5_ON fallback, escape hatch | Phase 1b, simulated Pi |
| Adafruit 481 ring has **internal current limiting** — safe at 100 % | Phase 1c, 7.8 mA measured |
| **Q1 mechanism**: U9 clamp ≈ K·R·C with K ≈ 1.0, not 0.7 | Phase 2c |
| **Q2**: no duty-fidelity limit to 250 kHz — ~CLR resets C57 through a transistor, not R68 | Phase 2d |
| **Peak beam current is set by the rail/Vf/ballast, not by duty** | Phase 2b |
| **GPIO33 polarity: TRACK = 0** | TMUX1219 datasheet + 4 bench confirmations |
| **Ambient rejection ≈ 48 dB** — the synchronous-detection premise holds | Phase 3.3 |
| The "bursty noise" was **beam return off the room**, not a fault | Phase 3.3, D11 covered |
| **σ_noise is set by the optical background**, not the electronics | Phase 3.3 |
| **CR-15 mechanism is optical**, and largely the operator's hand | Phase 3.3 |
| The lock-in response is a **TRAPEZOID**; h2 ≡ 0; h3 has an intrinsic duty-dependent floor | Phase 3.4 |
| The chain is a **PURE DELAY** to ~1° across 80–200 kHz | Phase 3.4 |
| `phase_ticks` carries a **geometric duty/period term** — compare DELAY between boards, never ticks | Phase 3.4 |
| **Carrier = 104.1667 kHz, fixed for all boards** (§8) | decision 2026-08-28 |
| **Q8 cannot be measured with a beam step** — the optical term is 26× the electrical one | Phase 3.6b |

### ⚠ PER-BOARD — must be measured on every board

Driven by `BRINGUP_NEW_BOARD.md`; its sign-off table is the authoritative form.

| what | why it varies | §BRINGUP |
|---|---|---|
| Rail integrity, standby + idle current | assembly | 2 |
| **LM5157 boost SW frequency** | part tolerance; the carrier's ±8 % margin must cover it | 2 |
| E9 `pins` check | different die | 3 |
| 🔴 **ADC +5 V scale** | ADC gain + divider tolerance. **Safety gate** | 4 |
| 🔴 Latch guard + sustained supply monitor | what stops a Pi being fed through a 1 A diode | 5 |
| **U9 clamp width** | 109–136 µs band from 1 % R / 10 % C | 6 |
| Beam thermals | *this* heatsink mounting — 87.5 °C vs 98 °C across two boards | 6 |
| Static detect-chain health | assembly | 7 |
| `hpf test` polarity **and switch leakage** | leakage spans **52–250 pA** across boards | 7 |
| CR-15 baffle acceptance | optics and operator discipline | 8 |
| **`cal demod` + `cal model`** | chain delay spans **340–600 ns** board to board | 9 |
| **DAC vref + comparator Vos** | LM393 Vos is ±15 mV per part | 10 |
| **TIA variant (Rf, Cf)** if reworked | changes nearly every number downstream | 0 |

### 🔵 The rule for deciding which a new test is

**Ask what would have to change for the answer to change.** If it is *the design* — a value in
the netlist, a firmware mechanism, a physical law — it is settled once. If it is *this
assembly* — a tolerance, a mounting, an optical alignment — it repeats.

---

## 1. Hard safety rules (do not relax without a deliberate decision)

1. **The Pi 5 is not seated on J8 until Phase 7c.** The latch is the Pi's power switch; a
   firmware bug power-cycles it. Use flying-lead SWD instead (§4).
2. **`PIN_PULSE_LIMIT_DIS` (GPIO27) is written 0 in exactly one place** — `safe_state()`.
   No CLI path, no config flag, until Phase 6d. It defeats the strobe hardware watchdog.
3. **Never run the +5V rail from USB power** — neither by latching on USB, nor by keeping the
   latch closed after the supply is pulled. Two independent checks now enforce this:
   `V5_MIN_FOR_LATCH` (5.05 V) at latch time, ✅ verified Phase 1 test 1; and a continuous
   `V5_MIN_SUSTAINED` (4.90 V, 500 ms debounce) monitor while latched, added 2026-07-30 after
   a bench find — see [`PROGRESS_ARCHIVE.md`](PROGRESS_ARCHIVE.md) §0. Without the second one, pulling the supply leaves a Pi 5 being fed
   through D8, a 1 A diode.
4. ~~**Phase 1b must pass before a real Pi is ever attached.**~~ ✅ **Passed 2026-07-31.**
   Superseded by: **an RP2354 reset is a hard power cut to the Pi** (§2). `reset` and
   `bootsel` are guarded in firmware; **SW2 is not, and cannot be** — that is operator
   discipline. Tape over SW2 whenever a Pi is seated.
5. ~~**Resolve the Mira220 1.8 V I/O question before Phase 7c.**~~ ✅ **ANSWERED 2026-08-14** — the
   sensor I/O is 1.8 V with no 3.3 V tolerance. Both directions fail; CR-09 is unblocked and
   red. **Still do not connect J4** until the camera board's own schematic confirms whether
   it level-shifts.
6. **No hand-held probe tips on a powered board in the fine-pitch areas** (added 2026-10-05,
   after a probe on R61 sparked on board 2 — R61's pads are 0.58–1.15 mm from +12 V and GND).
   Clips or soldered wires, attached with the board **unpowered** (PSU and USB off). See
   `HANDOFF.md` §3 rule 8 and `BENCH_P6_STROBE.md` 6a probes.

---

## 2. Verified hardware facts (already checked against the netlist — do not re-derive)

- The .md §10 pin map matches `The_Second_Board_To_Rule_Them_All.net` **exactly**, all 30 signal GPIOs.
- Both netlist exports (`.net` and `(netlist).txt`) are byte-identical except the export timestamp.
- The updated `.csv` and the as-built `jlcpcb/production_files/BOM-*.csv` agree with the netlist
  and the .md. Every firmware-relevant R/C value was checked and matches. (An older stale CSV
  that disagreed has been replaced — ignore any earlier note about a BOM discrepancy.)
- QSPI SD0–3/SCLK unconnected (RP2354B internal stacked flash). `QSPI_SS` → R22 → SW1 (BOOTSEL).
- **The MCU runs whenever J1 *or* USB-C has power — the button does not power the MCU.**
  +5V_IN → NCP1117 → +3V3 is the always-on domain (RP2354, one-shot watchdogs, +3.3VA mic).
  The button asks the already-running firmware to close Q3 and raise the *switched* **+5V**
  rail (Pi, boost, beam LED, analog chain). Confirmed on hardware 2026-07-30: power on J1
  with no USB → yellow standby blink immediately, no press needed. ADC1 taps +5V_IN, which is
  upstream of the latch, so its reading is independent of latch state.
- **Silicon confirmed on-device** (`picotool info -a`, 2026-07-29): RP2350 rev **A4**, **QFN80**,
  flash **2048K**, chipid `0xa764f5332ca5ac53`. Running ARM (RISC-V also available on-die).
  SWD debug enabled — the Pi-as-probe path in §4 is not locked out.
- `SWD_34 → J8.18`, `SWCLK_33 → J8.22`, **no series resistors. There is no separate debug connector.**
- Y1 = ABM8-272-T3, C29/C30 = 15 pF → **12 MHz XOSC**.
- Both 74LVC1G123 one-shots (U5 strobe, U9 beam) are powered from **+3V3**, not 5 V.
- R45/R44 = 10K/100K → Pi 3V3 × 0.909 = 3.0 V at GPIO24 when the Pi is powered.
- R46/R47 = 100K/100K → +5V_IN ÷ 2 at GPIO41/ADC1.
- **+2V5 is not 2.5 V.** R75/R76 = 10K/10K buffered by U11C makes it literally **+5VA / 2**.
  On the design's 5.2 V rail that is **2.59 V**, measured and confirmed at TP6/7/9/10.
  Ignore every "2.50 V" in the .md — it assumed a 5.00 V rail. See Q8 for the consequence.
- **Measured supply discriminator points:** USB-C only = **4.85 V** (host VBUS 5.2 V less
  ~0.35 V across D8/SS14); bench PSU = **5.20 V**. `V5_MIN_FOR_LATCH` = 5.05 V sits between them.
  Note USB VBUS varies by port — 4.59 V was seen on another port, so treat 4.6–4.85 V as the range.
- **The 12 V rail dominates idle current.** R15 (4K7) drops VIR 36 V → the D4 zener continuously,
  burning ~180 mW between resistor and zener to produce a ~5 mA rail. That is ~40 mA of the
  129 mA rail-up idle draw — roughly 40 %, purely to make the strobe gate-drive supply.
  Inherent to a shunt regulator and fine as designed; **first thing to revisit if idle power
  ever matters** in a future spin.
- **Adafruit 481 ring LED has internal current limiting.** Measured 7.8 mA through R48 (47 Ω)
  at 5.2 V, ring dropping ~4.84 V. Safe at 100 % duty indefinitely — no firmware cap needed.
- 🔴 **An RP2354 reset is a HARD POWER CUT to the Pi, not a reboot.** Netlist-confirmed
  (net 62 `LATCH_CONTROL` = **Q2 gate + R12 + GPIO15**, exactly three nodes): on any reset the
  GPIO15 pad reverts to high-Z, R12 pulls Q2's gate low, and the +5 V latch **opens**. The Pi
  loses power instantly — no shutdown request, no filesystem sync, no warning. This applies to
  **every** reset source: SW2, the `reset` and `bootsel` CLI commands, the hardware watchdog,
  and a brownout on +5V_IN. `main.c` already said this about the watchdog; it is true far more
  broadly.
  **This is a deliberate tradeoff, not a bug.** R12's pull-down is what makes the latch
  *fail-safe* — the rail opens if the MCU dies, which is the right call on a board that drives
  9 A strobe pulses. The cost is that a Pi can never be shut down gracefully by a reset, and
  you cannot have both without a supervisor or hold-up circuit that would weaken the fail-safe.
  **Consequence for Q10:** the spurious GPIO43 assertion during reset arrives at a Pi that is
  losing its rail in the same instant, which is why Q10 is defence-in-depth rather than a gate.
  Mitigation that exists today: the `reset`/`bootsel` CLI guard (§9).
- **RP2350 pads reset with the internal PULL-DOWN enabled, not floating.** Confirmed from the
  SDK register headers (SDK 2.3.0, `hardware/regs/pads_bank0.h`): `PADS_BANK0_GPIO43_RESET`
  = `0x116` → **PDE=1, PUE=0, OD=0, IE=0**. Every bank-0 pad is the same. So through a reset
  the *output driver* is high-Z but a ~50–80 kΩ pull-down is actively holding the pin **low**.
  This corrects the comments that used to be in `safe_state.c` and `board.h` §"Pi 5
  soft-shutdown", both of which claimed the pad simply reverts to high-Z. It matters because
  **GPIO43 = RPI5_SHUTDOWN is active-low**, so the reset default *is* the asserted level.
  See Q10 — the consequence is unmeasured, the pad behaviour is not.

---

## 3. Open questions and unverified numbers — **measure these, don't trust the .md**

| # | Item | Why it matters | Resolve in |
|---|---|---|---|
| ~~Q1~~ | ✅ **ANSWERED for U9 2026-08-13: 122.68 µs.** Neither estimate was right — K ≈ 1.0, not 0.7. **The concern was inverted:** the clamp is *above* the 100 µs software limit, so firmware controls pulse width and the .md §15 slow-ball rows need no re-derivation. `STROBE_SW_MAX_US` stays 100 µs. **Still open: U5**, same part and RC, expect 109–136 µs — verify in Phase 6a.1 and set the constant from it. *(original text)* One-shot clamp is probably ~86 µs, not 113 µs. SN74LVC1G123 t_w ≈ K·R·C, K ≈ 0.7 at 3.3 V; 56K × 2.2 nF = 86 µs. The .md's 113 µs implies K ≈ 0.92. | `STROBE_SW_MAX_US` (100 µs) may be **above** the real hardware limit → every slow-ball pulse silently truncated | Phase 2 step 4 (U9), Phase 6a (U5) |
| ~~Q2~~ | ✅ **ANSWERED 2026-08-13: no limit up to 250 kHz.** Exactly 30 % duty reproduced at TP5 at 250 kHz (4.0 µs period, 1.2 µs low). The premise was wrong: ~CLR resets C57 through an internal low-impedance transistor (~0.1–0.2 µs), not through R68's 123 µs charging RC. | — | ✅ Closed. **Carrier stays 104.167 kHz. Q3 may search freely up to 250 kHz** |
| ~~Q3~~ | 🔵 **CLOSED BY DECISION 2026-08-28, not by measurement. 104.1667 kHz is FIXED for every board and user.** The question was "which carrier is best on this board" and it is no longer being asked — uniformity is worth more than a few percent of per-board SNR. See §8 for the trade and the margin. ⚠ **What replaces it is narrower and still open:** does the fixed carrier have enough margin against the boost across the *population*? Computed margin is **−9.6 % / +7.1 %** of boost drift before folding; **the part-to-part spread of the LM5157 has never been measured on any board.** *(original text)* Best carrier frequency. 104.1667 kHz is a starting point, not an answer. Boost fsw tolerance makes paper analysis undefensible. | A folded boost harmonic looks exactly like a ball, and one bad choice is now bad on **every** board | **Measure the SW-node frequency on every board** (`BENCH.md` 0.5d), then §3.6 Check 2 |
| Q4 | **Does the Pi 5's header 3.3 V rail drop at halt?** | If not, `PI_3V3_SENSE` never fires and every shutdown hits the 60 s timeout. RPI5_ON fallback is implemented. Also decides whether `pi_down_unrequested` telemetry (§9) can rest on 3V3 or must use RPI5_ON alone. | **`BENCH_P8_PI.md` §8.2** — `sudo halt`, DMM on J8.1 |
| ~~Q5~~ | ~~**RP2350 erratum E9**~~ — **CLOSED 2026-07-29.** Empirical `pins` test: GPIO24/0/8/9 all read **0** floating. Silicon is revision **A4**, a later stepping than the A2 the erratum was documented against. | — | ✅ Resolved. No external pull-downs needed; R44's 100 kΩ holds GPIO24 down fine on this silicon. The `pins` reading is the authoritative evidence for this board. |
| ~~Q12~~ | ✅ **RESOLVED 2026-08-14.** The 53.6 °C D11 reading was an emissivity artifact off the **domed lens**, exactly as the physics predicted (it cannot be cooler than the 68 °C heatsink it feeds). Re-measured with the IR camera pointed **sideways at the LED base**, avoiding the lens: **104 °C at 30 % duty**. **D11 and the ballast resistors sit at the same temperature** — D11 carries 4× the power (3.23 W vs 0.81 W) through a ~4× better path (25 vs 100 K/W), and they are millimetres apart on shared copper. | — | ✅ **The 24 K/W figure in §6 is validated and does describe the LED**: it predicts 25 % → 89 °C (measured 87.5) and 30 % → 102 °C (measured 104) from one constant. **CR-12 confirmed 🔴** — junction at 30 % is **123–133 °C** against a 145 °C max. |
| ~~Q6~~ | ✅ **ANSWERED 2026-08-14 from the datasheet (DS000642 v9-00). YES, the I/O domain is 1.8 V** — VDD18 = 1.70/1.80/1.90 V, and **VIH max is specified as VDD18 itself, so there is no 3.3 V tolerance.** VOH min = 0.8·VDD18 = **1.44 V**, ceiling 1.80 V. | **BOTH directions fail and the 220 Ω resistors fix neither — they limit current, they shift no levels.** *Read:* RP2350 VIH ≈ 2.15 V > the sensor's 1.80 V ceiling → the strobe inputs can never read high → **the Phase 7 handshake cannot work.** *Write:* 3.3 V through 220 Ω injects **3.6 mA** into the sensor's clamp. Latch-up is not the risk (ISCR = ±100 mA, 27× margin), but the I/O rail draws only **0.6 mA max**, so 3.6 mA is 6× its own consumption and lifts VDD18 out of spec. **The destructive case is driving J4 with the camera unpowered** — that back-powers VDD18 through the ESD diode and violates the power-up sequence. | 🔧 **CR-09 is now unblocked and 🔴.** Needs a real translator on all three signals (TXB0104 or BSS138 discretes) plus a 1.8 V reference at J4, which the current pinout does not provide (pins 1/2/5/6 are all GND). **One unknown remains: whether the camera board already level-shifts.** J4 lands on its header, not on raw sensor pins — get that schematic. Until then **do not connect J4**, and never drive D_Cam_Trigger high with the camera unpowered. |
| ~~Q7~~ | ~~**RPI5_SHUTDOWN polarity**~~ — **RESOLVED on hardware 2026-07-31.** Active-low is correct and proven: a shutdown request produces a clean **200 ms low pulse at GPIO43**, measured during Phase 1b. | — | ✅ Firmware side closed. Only the Pi-side overlay params remain to be written and verified — `BENCH_P8_PI.md` §8.1. |
| Q10 | 🟢 **Measured 2026-07-31 — the level fails, but the impact is small. Demoted from blocker.** GPIO43's reset-default pull-down (§2: PDE=1) holds the *asserted* level from the reset edge until `safe_state_init()` runs. Measured with a 20 kΩ emulated pull-up: **2.07 V** at J8.37 against a **3.246 V** rail. `X = V·R/(3V3−V)` = **35.2 kΩ**, i.e. a **~34 kΩ pad pull-down** — stronger than the 50–80 kΩ assumed. A real Pi's ~50 kΩ pull-up would see **`V_pi` = 1.34 V**, below RP1's VIH, and even a typical 60 kΩ pull-down gives 1.81 V — so the *level* fails across the whole plausible range. | ⚠ **Corrects the 2026-07-31 write-up, which called this a blocker.** It is not. **Every reset also opens the +5 V latch** (§2), so the spurious request reaches a Pi that is losing its rail in the same instant. There is no case on this board where GPIO43 goes low but the latch holds — both pads reset together and the Pi has no other power source. The real hazard is the power cut; this is a footnote to it. | 🔧 **Optional defence-in-depth: 10 kΩ from J8.37 to +3V3** (or to **J8.1**, the Pi's own 3V3 — both are header pins, so no PCB work). Gives 2.67 V through reset and 0.35 V when firmware asserts. **Fit it if convenient; it does not gate Phase 8.** The mitigation that actually matters is the `reset`/`bootsel` CLI guard, already implemented. |
| Q11 | **How long does a Pi 5 take to raise its header 3V3 after +5V is applied?** Never measured. `PI_DETECT_WINDOW_MS` (3000) is currently a guess. | `POWERING_ON` classifies the Pi as absent when this window expires, and in `BENCH_RUNNING` a button press is a hard `FORCE_OFF` — a power cut on a booting Pi. The late-detect promotion backstops it, but the window should be right on its own. | **`BENCH_P8_PI.md` §8.3** — scope J8.2 (+5V) against J8.1 (Pi 3V3), measure to the **2.54 V** crossing (that is 2.31 V VIH ÷ the R45/R44 0.909 divider, i.e. when firmware can actually see it). |
| ~~Q9~~ | **The +5V_IN read path is ~5.9% LOW — MECHANISM RESOLVED 2026-07-30.** True **5.200 V** at J1 reads back as **4.893 V** (code 3036). Three measurements localise it: J1 = 5.200 V, R46/R47 junction = **2.578 V**, ADC reports **2.447 V**. So the divider contributes only **−0.85%** (fine for two 1% parts) and the **ADC conversion itself contributes −5.1%**. <br>**Leakage ruled out:** µA into the pin would have dragged the junction to ~2.45 V; it sits at 2.578 V. **Reference ruled out by arithmetic:** explaining the error would need VREF = 3.477 V, above the +3V3 rail feeding ADC_AVDD — and R27's 33 Ω can only drop it *lower*, which pushes the error the other way. Residual cause is ADC gain error + incomplete S/H settling through the 50 kΩ source (RP2350 wants ≤10 kΩ). | Nearly broke Phase 1: uncalibrated, a good bench supply read as 4.89 V — below `V5_MIN_FOR_LATCH` (5.05 V) — so the firmware would refuse to latch and report `USB_POWER_ONLY`. | ✅ **Closed.** Compile-time default scale **1.063** folds in both terms; `adc5vcal` trims per-unit residue. Nothing to check on ADC ground/reference. **Next board spin:** R46/R47 = 10K/10K — see `NEXT_BOARD_REV.md` **CR-03**, which also flags that `ADC5V_SCALE_DEFAULT` (1.063) must drop to ~1.00 in the same commit or every reading goes ~6 % HIGH. |
| Q8 | 🔴 **CONFIRMED ON HARDWARE 2026-08-31 — ×7.43 measured against ×7.25 predicted. §3.6b FAILS.** 🔧 **Board fix required — `NEXT_BOARD_REV.md` CR-02** (regulate +2V5, or a unity diff amp taking TP9−TP6). **The virtual ground TRACKS the +5V rail** — R75/R76 = 10K/10K makes +2V5 literally +5VA/2, not a regulated 2.5 V. **Measured 2.59 V** at TP6/7/9/10 on a 5.2 V rail. Confirmed correct; the .md's "2.50 V" assumed a 5.00 V rail this board never runs at. | Any *step* on +5V while the HPF is in HOLD (i.e. armed) shifts the whole chain's virtual ground by ΔV/2, which passes through C81 and hits the comparator amplified **×14.5**. A 100 mV rail step → ~725 mV at ADC5, well over a typical 0.1–0.5 V threshold → **false trigger**. In TRACK mode the 0.66 s HPF removes it; in HOLD it does not. ✅ **MEASURED §3.6b Method A, board 3:** in HOLD a **+75.0 mV** rail step moves the comparator input **+556.9 mV** = **×7.43**, and it **stays**. In TRACK the same step peaks at ×3.84 and **decays away with τ ≈ 0.75 s** — the HPF works, and HOLD is the whole exposure. | ✅ **Measured. It is real and it is at the predicted magnitude.** 🔴 At a 300 mV threshold a **39 mV** rail step fires the detector; at 100 mV it takes **13 mV**. 🔴 **And a rail SAG blinds the detector outright** — U12B's output clamps **21 mV** below quiescent, so while the rail is down a ball cannot move it at all. Mitigations: keep the rail stiff while armed, shorten the armed window, stay in TRACK, or fix the board (CR-02). |
| Q13 | **What VIR can really deliver in a burst: the bulk capacitance, its ESR, the boost's refill current, and the headroom at 9 A (owner request, 2026-10-15).** `BURST_CHARGE_MAX_MC` (6 mC) is a sag budget, ~670 µF × ~9 V (design doc §15), and `board.h` marks it 🔴 UNVALIDATED. The bank is 2 × Rubycon 50TPV330M (330 µF, ±20 %) plus 8 × 2.2 µF 1206 X7R (small after DC bias); none of it has been measured. The model also leaves out: the ESR and harness drop at 9 A; the boost refilling during a burst (design 0.4 A, i.e. ~1.7 mC in a 4.27 ms slow-ball gap against 0.9 mC per 100 µs pulse at 9 A); and Q9's own saturation voltage. The design's "sink ≈ 1.5 V" is almost all sense resistors and Q10 (~1.4 V at 9 A). A rough square-law fit to 2026-10-15 (0.10 A at gate 21 %, 0.69 A at 24 %) gives V_th ≈ 1.95 V and ~1.2 V of overdrive at 9 A, so the sink may need ~2.5–3.5 V. The same fit predicts TP3 ≈ 4.5 V at 9 A, which `strobe cal 9` will check. | The limit decides how many pulses a slow-ball burst keeps, i.e. image coverage. The error can go either way: refill could remove the shedding at 10 m/s, and a bigger sink need shrinks the budget. **ADC0 cannot see it:** Q9 holds the current flat until the headroom is gone, so the plateau shows the failure, not the margin. There is no VIR tap for the firmware (design doc §17, item 2). Deep sag also starves +12 V (R15 from VIR), which drives Q10's gate. | 6c, after the readback fix and `strobe cal 9`, at 9 A, on the MSO54B. Channels: TP4, VIR (J3 pin 1), VIR_RTN (J3 pin 2) and U5 Q, all probes attached unpowered. Single 20/50/100 µs pulses: the step at the edge gives the ESR; C = I / (sag slope + recovery slope), with the boost current-limited in both; V(VIR_RTN) during the pulse, minus the sink's need, gives the margin. Then predict each Step 5 burst's sag before firing it, and compare. A new limit would come from the model with derating (±20 %, cold, ageing) and review, and may come out **lower** as well as higher. It is not a thermal limit: Q9 (FLIR, SOA) and the LED pulse ratings are separate. |

**When you measure any of these, record the number in §6 below and update `board.h`.**

---

## 4. Flashing / debugging

**USB BOOTSEL — ✅ proven working.** Hold SW1, tap SW2 (RUN), release SW1 → `RPI-RP2` drive →
drag the UF2, or `picotool load -f build/pitrac.uf2`. UF2 family is `rp2350-arm-s`.

**SWD from a Pi 5 — not yet attempted.** Flying leads, Pi NOT seated on J8:

| Pi 5 (self-powered, off-board) | Board |
|---|---|
| GPIO24 | J8 pin 18 (SWDIO) |
| GPIO25 | J8 pin 22 (SWCLK) |
| GND | J8 pin 20 / 25 / 30 / 34 / 39 / 6 / 9 / 14 |

Because J8.2/4 (+5V) and J8.1/17 (Pi 3V3) stay unconnected, the board never powers the Pi and
the FSM correctly sees "no Pi." Gotchas: `bcm2835gpio` does **not** work on Pi 5 (RP1
southbridge) — use OpenOCD's `linuxgpiod`; gpiochip is `gpiochip4` on older kernels,
`gpiochip0` on rpi kernel ≥ 6.6.47 (check `gpioinfo`); needs OpenOCD with `target/rp2350.cfg`.
See `tools/openocd_pi5.cfg`.

---

## 5. Phase checklist

- [x] **0** Toolchain, board header, blink, safe_state, CLI, `capture`, E9 check — all passed.
      *(Still open: SWD flashing via the Pi — optional, a convenience only.)*
- [x] **0.5** Dead-board rail smoke test (J2 jumper) — *before any firmware writes GPIO15*
- [x] **1** Power button + latch, ADC1 discriminator, fail-safe through reset — **all 5 tests pass**,
      including test 5 (supply pulled while latched → `SUPPLY_LOST`, latch drops). §1.3 (E9
      fallback) not needed: GPIO24 reads low correctly, `BENCH_RUNNING` as designed.
- [x] **1b** Full FSM + Pi soft-shutdown against a **simulated** Pi — **complete 2026-07-31.**
      All original rows pass (200 ms pulse, both timeouts, RPI5_ON fallback, escape hatch,
      reset invariant), **plus** the four added after the FSM changed: detect window,
      late-detect promotion, its debounce, and `fault clear` as a full acknowledgement.
      Q10's level fails but is defence-in-depth, not a gate (see §3).
- [x] **1c** Panel indicators on J7 — **complete.** §1c.2 function test passes, `panel demo`
      confirms all six ring patterns, ring current measured at 7.8 mA (safe, internally
      limited). Automatic state→pattern mapping **confirmed during 1b** (2026-07-31),
      including POWERING_ON, PI_BOOTING, SHUTTING_DOWN and the FAULT double-blink — all of
      which need a simulated Pi to reach.
- [x] **2** Beam carrier + demod phase lock — **COMPLETE 2026-08-13.** 2a all 7 checks
      (scatter 0.15 ticks); 2b ramp + thermals; **2c Q1 = 122.68 µs**; **2d Q2 = no limit
      to 250 kHz**. Carrier stays 104.167 kHz. `board.h` strobe constants 86/73 → 122/100.
- [~] **3** Photodiode: **§3.1–3.6 COMPLETE on board 3** (§3.1–3.5 also on board 2).
      **§3.6b is a design finding** (Q8 ×7.43 armed, CR-02); **§3.6 Check 2 PASS**.
      **§3.7:** detect control-path smoke test PASS and **one pilot ball** captured on board 3
      (2026-09-18, 3.102 V peak, ~22.7 ms FWHM, 198 mV ADC headroom); comparator edge timing,
      the 20-pass set and `cal gain` remain open, behind the acquisition-window constraint.
      🔵 **Per-board parts (§3.2–3.5) are driven by `BRINGUP_NEW_BOARD.md` §7–§10.**
      Results per section, with dates and links: the **Phase 3 results index** below.
      ⚠ **CR-15 stays 🟡** — the mitigation is mechanical and depends on operator discipline.
      ⚠ **The chopped calibration is over-driven by crosstalk alone** and needs optical
      attenuation over D12; it will need re-tuning on any board or geometry change.
- [~] **4** Trigger source experiment — **firmware complete** (`detect log` / `detect stats`,
      including the bias-vs-1/peak regression). Bench work not started. **No longer gated on
      CR-15** — the front end is linear to 25 % duty — but it needs §3.5 and `detect path`.
- [~] **5** Microphone. ✅ **Bring-up done on board 3 (2026-08-31)**: quiescent
      2053 codes / 1.654 V, noise σ 0.50–0.52 mV (sub-LSB), block and **triggered** `capture`.
      🔴 **No mic firmware beyond capture** — no onset detection, no veto logic.
      **CR-18 open:** a real ball impact must be captured to decide whether the 2.41 kHz
      high-pass corner discards signal.
- [~] **6** Strobe: 6a dry → 6b gate DAC → 6c LED bank ramp → 6d clamp-with-current.
      ✅ **6a and 6b PASS on board 1 (2026-10-06)** — U5 clamp **135 µs**, PIO pulse/burst
      timing matches on the LA, §15 schedule and shedding confirmed, A7 independent, interlock
      both ways, gate chain 3 × 3.29 V × duty to 9.88 V, TP2 12.31 V (§6). Firmware: dry path
      written 2026-10-02 (in the 2026-10-05 build); **6c/6d live mode written 2026-10-07**
      (`Oct  7 2026 08:07:33`, 78/78 host tests — §6 2026-10-07). ✅ TP3 loop stability
      **PASS 2026-10-08** (MSO54B, §6). 6c on board 1: Steps 1-2 PASS 2026-10-12; first current
      2026-10-13; a gate-drive overshoot fixed by the CR-23 rework (verified 2026-10-15);
      ✅ **6d PASS 2026-10-15** (135 µs at 0.70 A). Next: the readback fix (§9), then 6c above
      24 %.
      *(Until 2026-10-02 this line read "NO STROBE FIRMWARE EXISTS AT ALL" — see the 8/25
      audit row in the archived §6 table.)*
- [ ] **7** Cameras: 7a loopback → 7b delayed sim → 7c real (needs Pi).
      🔴 **No camera firmware exists** — no handshake, no `t_cam`, no FIRING FSM, no
      `CAM_TIMEOUT`. 🔴 **And CR-09 (Mira220 1.8 V I/O) is unresolved** — get the camera
      board's schematic before connecting J4 to anything.
- [ ] **8** Real Pi integration + clean-shutdown acceptance — `BENCH_P8_PI.md`.
      ✅ **The firmware for 8.0–8.5 exists and is proven** (full Phase 1b matrix). ❌ 8.6 halt
      telemetry is not written. ⚠ **NOT gated on the Q10 rework** — that was demoted to
      optional on 2026-07-31; the gate is phases 2–7. Closes Q4, Q7 (Pi side) and Q11.

**Phase 3 results index.** Procedures are in `BENCH_P3_DETECT.md`; the dated narratives that used
to sit inside them are in `PROGRESS_ARCHIVE.md` → "Results moved from BENCH_P3_DETECT.md" (moved
verbatim 2026-10-05), and board 3's values in force are in `cfg` and §0.

| § | what | board | date | verdict | detail |
|---|---|---|---|---|---|
| 3.3 | beam on, CR-15 baffle sweep | 2 | 2026-08-19 | ✅ linear to 25 % duty | archive §3.3 |
| 3.3 | ambient rejection, 120 Hz | 2 | 2026-08-21 | ✅ ≈ 250× (≈ 48 dB) | archive §3.3 |
| 3.3 | "bursty noise" | 2 | 2026-08-21 | ✅ resolved — beam returning off the room | archive §3.3 |
| 3.4 | demod phase | 2 | 2026-08-24 | ✅ 1348 ticks at 104166 Hz | archive §3.4 |
| 3.4 | demod phase | 3 | — | ✅ 1311 ticks (saved, slot A seq 5) | `cfg` |
| 3.5 | threshold DAC ↔ comparator | 2 / 3 | 2026-08-25 / — | ✅ vref 3.268 / 3.256 V, Vos +11.0 / +5.4 mV | archive §3.5 |
| 3.6b | Q8 rail step | 3 | 2026-08-31 | 🔴 design finding: ×7.43 armed (CR-02) | archive §3.6b |
| 3.6 | carrier robustness, Check 2 | 3 | 2026-08-31 | ✅ PASS, σ_noise flat 4.57–5.08 | archive §3.6 |
| 3.7 | detect control path | 3 | 2026-09-18 | ✅ arm/disarm only | §6 2026-09-18 |
| 3.7 | pilot ball | 3 | 2026-09-18 | ✅ 3.102 V peak, ~22.7 ms FWHM, 0.198 V headroom | §6 2026-09-18 |
| 3.7 | 20-pass set, comparator timing, `cal gain` | 3 | — | ⏸ pending (acquisition window) | — |

---

## 6. Measurement log — **fill this in as you go**

**Entries 2026-07-29 → 2026-08-31 are in [`PROGRESS_ARCHIVE.md`](PROGRESS_ARCHIVE.md) §6**
(moved verbatim 2026-10-05): the measurement table — die revision, E9, rails, the 32 mA /
129 mA current baselines, +5V_IN 4.85 V USB / 5.20 V PSU, board-3 leakage, Q8 ×7.43, HPF τ,
switcher 801 kHz, mic quiescent/noise, the Phase 3 calibration rows — and the GPIO43
reset-test correction. **Those measurements are still valid**; they moved only to keep this
file short. New entries go below, newest last.

### 2026-09-18 - Board 3 detect control-path smoke test

**Owner confirmed no intervening bench work since 2026-08-31.** This result supersedes
the 2026-09-17 handoff's "detect never run on board 3"; it does not close §3.7.

| Evidence | Result |
|---|---|
| Commanded state | `detect disarm`, `beam off`, `on`, `hpf track`, `adcmode idle`; no ball stimulus |
| Power/status readback | `BENCH_RUNNING`, fault `none`, **5.207 V** firmware reading, latch `1`, railsready `1`, Pi absent, watchdog `off` |
| ADC | Mode **1 (idle)**, ring `RUNNING`; the held +5V_IN sample was only **1 ms** old, not a stopped ring |
| Config readback | **slot A, seq 5, v1**; carrier **104166 Hz**, phase **1311 ticks**, ADC5V scale **1.0620**, gain **14.5**, coalesce **2000 us**, path **0.00 mm**, threshold **level 0** |
| Calibration readback | HPF **TRACK = 0** confirmed; phase model fitted/pure delay **80-200 kHz**, calibration duty **25.00 %** |
| Firmware evidence | `help` includes `capture trig`; `stat` shows watchdog `off`. Consistent with the expected image, but no build ID or binary identity was captured |
| Arm/disarm | `detect arm` acknowledged, status **ARMED**; `detect disarm` acknowledged, status **disarmed**. **PIO2, GPIOBASE 16, SM0** reported throughout |
| Comparator/data | GPIO46 **1 (ABOVE threshold)** throughout; **0 passes, 0 raw FIFO words**, no last pass |

**PASS: command/control-path smoke test only.** Bare `detect` is a status read, not an
arm command. At a zero threshold, a high comparator reading is consistent with the
quiescent signal/offset. `detect.pio` first waits for LOW, then a complete HIGH pulse:
this transcript contains no evidence of that sequence. It proves neither edge timing
nor optical detection. The board was left **disarmed, beam OFF, HPF TRACK, ADC idle**;
no shutdown or config save was commanded.

**Next gate:** establish the final ball-path geometry and a usable threshold above the
quiescent background but below the measured ball signal. Confirm GPIO46 is LOW at rest.
Do not use the saved zero threshold or an unmeasured `detect path 20` as a transit setup.
Keep the rail/load steady while in HOLD (CR-02); the scope-ground issue is still open.

**Related text audit:** the transcript exposed stale `help` guidance (the old 30 % duty
example, "sense is unverified" despite saved HPF confirmation, literal `25%%`, and
carrier-ranking language). Corrected those, documented arm-only counter reset and
unchanged HPF, updated `id` for existing mic capture, removed a board-specific rail
measurement from `threshold` output, and corrected the phase-model persistence comment.
Only text/comments changed; detection, safety, sampling and config logic are unchanged.
**Initial build blocker (historical; recovery and flash confirmation below):** CMake Tools twice returned **"Unable to configure the
project"**, with no CMake diagnostics or available targets. The private CMake/Ninja/ARM
tools exist; adding their paths and the existing build directory to the ignored local
`.vscode/settings.json` did not resolve the failure. **At that point the UF2 on disk still
dated from 2026-08-31 and did not contain these corrections.** Source diagnostics, help-text
assertions and `git diff --check` pass. Encoding audit found no BOMs; existing non-ASCII
in firmware source is confined to deliberate comment emoji, and the netlist-report tool
has intentional Unicode input/output. No new non-ASCII source text was added.

The §3.7 audit also corrected "HOLD is safe for as long as you need": the leakage budget
does not bound rail-step errors, and board 2's drift measurement must not be presented as
board 3's. This is a documentation correction, not a new measurement.

**Build-blocker follow-up, same day:** the owner's CMake Tools log reports `EEXIST` while
creating `build/.cmake/api`. That path is a normal directory with owner full control,
not a stray file; a complete file-API reply and Ninja build files were generated at
10:51. Selecting `[Unspecified]` also raised the same error, so kit selection did not
resolve it. With no CMake/Ninja process running, temporarily renamed **only** `.cmake`
to preserve it and retried: the extension returned the same exception without recreating
the directory. Restored the original metadata; no build data was deleted and no temporary
backup remains. This suggests retained extension/driver error state, not a demonstrated
firmware or compiler failure. **The next recovery step was to reload VS Code and retry.**
The precise initiating cause is still unproven.

**Recovery, intermediate Debug build (superseded by Release below):** the owner reported
a successful build after **Developer: Reload Window**.
Host verification found fresh UF2/ELF/BIN at **11:07:46-47**, embedded build stamp
**Sep 18 2026 11:07:41**, all corrected CLI strings, and none of the four superseded help
strings. The UF2 has valid block magic and family **0xE48BFF59**; ELF is ARM.
However, CMake Tools selected **Debug (`-Og -g`)**, replacing the previous **Release
(`-g -O3 -DNDEBUG`)** configuration. Debug size is **137600 text / 0 data / 88304 bss**.
**The intermediate Debug build was not approved for flashing; Release was required to
preserve the established bench configuration.** The editor tool connection disconnected on reload; host artifact
checks remain available. Build success is owner-reported, not an independently captured
full build log.

**Host-check error corrected:** my first UF2 check falsely reported an invalid first block
because PowerShell interpreted `0x9E5D5157` as signed while the file read was unsigned.
Explicit unsigned conversion makes every block pass. That was a validation-script error,
not a corrupt UF2.

**Final Release verification - PASS (flash was pending here; confirmed below):** selecting Release first regenerated
the cache/Ninja files but left the old Debug UF2 in place. A subsequent **CMake: Build**
performed all **106** build steps and finished with **exit code 0** in **15.771 s**, with
no warnings in the owner's supplied log. Verified the **artifact**, not just the cache:
`picotool info -a build/pitrac.uf2` reports **Release**, SDK **2.3.0**, board
**pitrac_ltb_v1**, family **rp2350-arm-s**, target **RP2350**, image **ARM Secure**.
UF2 timestamp **2026-09-18 11:12:05**; embedded CLI `built` stamp
**Sep 18 2026 11:11:51**. Size **144768 text / 0 data / 88308 bss**: text +632 bytes,
BSS unchanged from the previous Release build. Corrected help/identity strings are
present in the linked image; the four checked stale help strings are absent.

UF2 SHA-256: `64E9632B3AF97508CF0B6ACD66F6E49E3D74C902DF5CB3AC6BC192633E14BDE0`.
The preserved file-API metadata was restored; no temporary backup remains.
**At this checkpoint flash/readback was still pending.** The planned checks were `id` for the stamp above and
board UID **6d6fda754e367a40**, and `cfg` for **slot A seq 5**, carrier **104166 Hz**,
phase **1311 ticks**, **TRACK = 0**, cal duty **25.00 %**.

### 2026-09-18 - Post-flash PASS; preparing the first physical ball setup

**Owner serial transcript confirms the new image is running on board 3:**

| Evidence | Readback |
|---|---|
| Identity | `pitrac phases 0-4 + mic capture (block/triggered)`, built **Sep 18 2026 11:11:51**, UID **6d6fda754e367a40**, sysclk **150000000 Hz** |
| Saved config | **slot A seq 5 v1**, carrier **104166 Hz**, phase **1311 ticks**, cal duty **25.00 %**, pure-delay model **80-200 kHz**, saved **TRACK = 0** |
| Other saved values | Threshold **0**, coalesce **2000 us**, ADC5V scale **1.0620**, gain **14.5**, path **0.00 mm** |
| Power | **BENCH_RUNNING**, firmware supply reading **5.207 V**, latch **1**, railsready **1**, fault **none**, Pi absent, watchdog **off** |
| ADC | **idle (mode 1)**, ring RUNNING, supply sample **1 ms** old |
| Last commands | `detect disarm`, `beam off`; both acknowledged |
| Physical optics | Owner confirmed **lenses fitted, no enclosure** |

**PASS: post-flash identity, retained configuration and powered idle health.** Saved HPF
polarity is not a live-mode readback; this transcript did not include bare `hpf`, so the
next powered procedure explicitly sets TRACK. No new calibration/config save, comparator
edge proof or ball transit is implied by this result.

**Initial proposed procedure (physical layout superseded by the extrusion choice below):**
power down for fixture work;
rigid lens/board mount; perpendicular flat rolling lane; marked crossing C; initial gentle
100 mm release height; smooth transition and remote catch; three unpowered dry rolls.
Measure the lens-to-ball range, ball-centre height/drop, lane/guide geometry and lighting,
and take top/side photos. About 400 mm range is only a provisional layout based on the
previous calibration-card plane, **not a proven ball-detection range**. Keep lenses/focus
unchanged. Then take a no-ball **ADC5 100 ksps / 100 ms** raw record in the final scene,
with beam **104.1667 kHz / 25 %**, warmed at least **5 min**, HPF TRACK, detect disarmed.
Do not arm at the saved zero threshold or enter an invented `detect path`.

**Corrections to my earlier guidance and the bench document:**

- I called the ramp a known/independent speed reference. That was too strong. The
  `sqrt(2*g*h*5/7)` values assume a lossless solid sphere rolling without slip; a golf ball,
  real ramp/transition and guide contacts do not establish few-percent speed accuracy.
  Use height for repeatable release, and an independently timed local speed with uncertainty
  if validating accuracy. The old few-percent ramp-speed exit criterion is superseded.
- I implied setting `detect path` would enable a speed readout. **The current CLI does
  not report velocity.** Code-intelligence references for `detect_path_mm` lead only to
  config snapshot/status uses, and the transit CLI emits durations. The field is stored;
  the sequencer's analysis stage is still a stub. This is not a gate to recording a
  waveform. Board-to-ball range is not the travelled distance between waveform crossings.
- The ordinary ADC ring holds **32.768 ms**, but that is not the available pulse-width
  budget. In armed mode `refine()` needs approximately **age + 2.5*T + 1.024 ms** of
  history for one fragment. At the saved **2 ms** coalesce delay, **T < about 11.9 ms**
  is needed before allowing for service latency; chatter needs more guard. The older
  source comments' "about 1.4 m/s" shortcut does not include this indexing budget or a
  measured optical response width. Slow ramp passes can set **WINCLIP (0x04)**. Measure
  a long pilot waveform first: single-channel **100 ksps** triggered capture can hold
  **163.84 ms** (25 % pre = **40.96 ms**, post = **122.88 ms**), or use the external scope.
  Do not raise the ramp to an unsafe height just to fit the normal ring.

**Printed-output/comment audit, no firmware edits:** current `capture trig` always says
"Make the sound", even for ADC5; the revised optical procedure explains that this means
release the ball, not clap. `level`'s 50-70 % target is a calibration-target criterion,
not a required ball amplitude. The path/speed and ring-speed comments above are known
overstatements, now explicitly qualified in the bench instructions; do not change code
or demand another reflash just to build the physical fixture. First review deliverables
are dimensions/photos and a raw no-ball baseline; real-transit measurements remain pending.

### 2026-09-18 - Extrusion ramp selected; serial logging setup

The owner will use a **grooved aluminum extrusion** rather than the proposed flat-lane
cardboard ramp. The ball crosses the optics **on the slope**:

| Owner-provided dimension | Meaning / status |
|---|---|
| **295 mm** above mat | Release location, **top surface of extrusion / bottom of ball**, clarified by owner |
| **85 mm** above mat | Approximate beam-crossing location, same surface/bottom-of-ball reference, **not ball centre** |
| **740 mm downrange** | Release to where extrusion touches mat; horizontal versus along-ramp reference not yet established |
| **210 mm inferred drop to crossing** | Difference of surface heights; also centre drop only if straight uniform groove and unchanged seating offset |

No incline angle, lens-to-ball distance, actual seated centre height, groove contact
geometry, arrival speed, dry-roll result or baseline capture has yet been supplied.
The 1.715 m/s lossless flat-surface solid-sphere value for 210 mm is **not a prediction
to trust for this groove**. Grooves change the rotation/contact geometry; use the fixture
for repeatability and measure speed independently if needed. Aim at the actual ball
centre, not the 85 mm-high aluminum surface. Secure the metal away from the PCB/D12 and
retain it in the no-ball scene so its reflected background is represented in the baseline.
Updated the §3.7 procedure to distinguish this adopted fixture from the earlier alternative.

**Serial truncation:** the owner reports the first output disappearing before copying.
Inspected the installed **Microsoft Serial Monitor 0.13.1**: its default scrollback is
**9000 lines**, below the requested 10000 ADC samples plus header. Verified built-in
**Toggle File Logging**, **Open the last used log file**, and
`vscode-serial-monitor.logFileDirectory`. The logging toggle is disabled while monitoring
or without a directory. Added the existing, gitignored `captures/` directory to the
ignored local workspace setting. **This configures a destination; it does not enable
logging or prove a file was captured.** Owner should stop monitoring, enable file logging,
restart, test with `id`, stop/open the log, then restart for the baseline.
Do not rely on scrollback or assume already-discarded output can be recovered.

The existing `scope.py --csv --no-plot` path was inspected as an alternative; a `--help`
check failed because **pyserial is absent in the selected Python 3.14.6 repo venv**.
No package installed or Python code changed: the built-in serial logger needs neither.
No firmware changes/reflash. At this checkpoint serial logging and all physical-transit
results were still unverified; the short logging test is confirmed below.

**Serial file-logging test - PASS:** inspected
`captures/COM11_2026_09_18.14.43.36.091.txt` (14 lines). It contains the complete
`detect disarm` / `beam off` acknowledgements and `id` response through the final prompt:
build **Sep 18 2026 11:11:51**, UID **6d6fda754e367a40**, sysclk **150000000 Hz**.
The owner stopped monitoring after saving the log. This proves the short file-logging
path, **not yet a 10000-sample capture**. No `stat` or ADC data in this file; it does not
establish a new rail/HPF state or a physical-transit result. Last commanded beam is OFF
and detection disarmed; stopping the host monitor is not an `off` command.
**Next at that checkpoint (superseded by the later capture below):** restart monitoring with file logging enabled, complete the safe fixture/optical
setup, warm at 25 % with HPF TRACK, and save the final-scene no-ball ADC5 baseline.

### 2026-09-18 - Complete no-ball ADC5 capture; serial logging proven at full length

**Correction to my response:** I inspected only the earlier 14:43 short logging-test file,
called the baseline pending, and sent the owner the procedure again. The later
`captures/COM11_2026_09_18.14.44.19.918.txt` already held the completed procedure and
capture. **That was my file-selection/status error, not missing bench work.** Check the
latest supplied file and adjacent newer captures before asking for a repeated measurement.

**Validated the entire later file:** exactly **10000 integer ADC5 samples**, all in
0-4095, one capture header, `# columns: ch5`, and the closing `# end` plus prompt.
Header: `# capture mask=0x20 n=10000 rate=100000 overran=0`.
**PASS: full-length logging/capture integrity**, 100 ksps, **100 ms**, 10 us/sample.

| Measurement | Result |
|---|---|
| Commanded/read-back optics | **104166 Hz**, TOP **1439**, **25.00 %** commanded/effective, phase **1311 ticks**, PWM hardware checks OK |
| HPF and ADC before capture | Live **TRACK (GPIO33 = 0)**, ADC **idle**, ring RUNNING |
| Chopped `level` | Rose while settling, ended at **2254 codes / 55 %**, GOOD. This is chopped-scene response, not the steady baseline voltage |
| Comparator | Threshold **level 0**, GPIO46 **HIGH**; no working ball threshold established |
| Pre-capture power | **5.178 V**, BENCH_RUNNING, latch/railsready **1**, fault none, Pi absent, watchdog off |
| Mean baseline | **16.6441 codes = 13.4128 mV** |
| Population standard deviation | **4.5435 codes = 3.6615 mV** |
| Minimum / maximum | **9 / 31 codes = 7.2527 / 24.9817 mV** |
| Peak-to-peak | **22 codes = 17.7289 mV** |
| Clipping checks | **0 samples <= 1**, **0 samples >= 4080**; no clipping observed in this window |
| Mean over each 10 ms block | **12.94-14.07 mV**; no large baseline step in the 100 ms record |

Voltages use nominal **3.3 V / 4095** ADC conversion, not the separate ADC5V supply scale.
This is measured variation in this scene, not an isolated electronics-noise specification
or proof against rare false triggers.

**Thermal qualification:** the first status (before beam setup) reports BENCH_RUNNING
**785367 ms**, and the pre-capture status reports **869396 ms**, an interval of **84.029 s**.
Beam ramp and `level` are between them. The record therefore does **not establish the
required 300 s steady-beam warm-up**. Keep this useful baseline; do not describe it as a
thermally qualified final measurement or repeat the whole procedure merely because I
opened the wrong file. Establish the steady-beam warm-up before the next pilot record.

**Last recorded state:** beam **ON at 25 %**, rail **UP**, HPF TRACK, detector last commanded
disarmed. Capture restores ADC idle in firmware. There is **no post-capture `stat`, `beam off`
or `off` in this file**; stopping the monitor is not evidence of shutdown. No ball
transit, pilot width, or comparator-edge proof yet. No firmware changes.

**Next prescribed experiment at that checkpoint (completed below):** one no-ball HOLD trigger control, then
one ball pilot if the control stays quiet. **`capture trig 5 64 100000 25 10`**:
ADC5, **64 codes = 51.6 mV deviation** from a newly measured baseline, **100 ksps**,
**25 % pre-trigger**, **10 s timeout**. About **14.1 times measured TRACK sigma**, with
**163.84 ms** total history (**40.96 ms pre / 122.88 ms post**). This is a conservative
starting trigger, not the eventual comparator threshold or a validated detection limit.
The trigger is either-polarity; require a ball-correlated positive waveform in review.
HOLD gets a separate quiet control because the measured 100 ms TRACK baseline does not
bound rail disturbances or long-term drift while armed.

Use the existing steady beam if it has remained on; establish **at least 5 min** total
warm-up without needlessly restarting it. Detector stays disarmed. TRACK for **at least
5 s** before each trial; HOLD immediately before the capture. No-ball **NO TRIGGER**
after 10 s is the desired control result; an unsolicited capture means stop and review.
For the ball trial, stage it at the existing **295 mm** release-surface mark outside the
view before settling, wait about **1 s** after `armed`, then release once without pushing.
After prompt/#end return to TRACK/idle, save `stat`, and power down if pausing for review.
Exact commands and output checks are in BENCH_P3_DETECT §3.7 step 1d. No new firmware,
config save or physical result is implied by these instructions.

### 2026-09-18 - First real optical ball capture PASS; power-request bug exposed

Newest file: `captures/COM11_2026_09_18.14.54.08.428.txt`, not the still-tagged 14:44
baseline. The owner confirmed **one clean ball crossing, no hand in view**, with the
capture starting after release. Same grooved aluminum extrusion fixture.

**Control:** initial typo `capture triv ...` returned `ERR: capture failed`; corrected
`capture trig 5 64 100000 25 10` then returned **NO TRIGGER in 10 s**, baseline
**136 codes / 109.60 mV**. This passes the prescribed no-ball HOLD control, not a
long-duration false-alarm qualification. The typo was not a successful acquisition.

**Ball capture integrity PASS:** exactly **16384 samples**, one header and `# end`,
`mask=0x20`, `rate=100000`, `overran=0`, `trig=4096`, `base=96`.
That is **163.84 ms**, **40.96 ms pre-trigger / 122.88 ms post-trigger**.
Beam **104166 Hz / 25 % / phase 1311**, HPF **HOLD**, detector disarmed. Initial supply
**5.179 V**; after capture/restoring TRACK/idle, **5.191 V**, no fault, watchdog off.

| Pilot measurement | Result / method |
|---|---|
| Waveform | Single smooth positive bump, both edges and return toward baseline captured; owner confirms a clean ball transit |
| Raw peak | **3849 codes = 3.10176 V**, at **+30.25 ms** relative to ADC trigger |
| Local pre-event baseline | **115.332 codes = 92.942 mV**, mean over **-40 to -10 ms** |
| Peak above that baseline | **3.00882 V** |
| Tail baseline | **109.4265 codes = 88.183 mV**, mean over **+100 to +120 ms** |
| Half-height duration | **~22.7 ms**; 0.2 ms box-averaged waveform, linearly interpolated crossings at **+19.754 / +42.463 ms**, using the local pre-event baseline |
| ADC headroom | **246 codes = 198.24 mV**, **6.01 %** of nominal full scale remains |
| Clipping checks | **0 samples >= 4080**, **0 samples <= 1**; no ADC rail clipping observed, not a proof of every upstream stage's linearity |

Voltages use nominal **3.3 V / 4095**. The arm-time `base=96` (**77.36 mV**) is not the
same as the later pre-event mean. Nor are the HOLD baselines interchangeable with the
earlier **13.41 mV TRACK** mean. Do not attribute those differences to one cause without
elapsed HOLD time and rail/optical evidence. A plot and reproducible standard-library
analysis are retained as session artifacts; no Python packages were installed.

**Pilot PASS is not full §3.7 closure.** There is no comparator timing/chatter result
because the PIO detector stayed disarmed, no velocity reference, and no repeatability
distribution. The baseline-to-pilot status ages differ by **483.281 s**, consistent with
the requested warm-up if the beam stayed on; uninterrupted beam-on time is not directly
timestamped in the log.

**Normal refinement does not fit this pulse at half-height:** with comparator width
T approximately **22.709 ms**, the current single-fragment indexing needs
**2 + 2.5*T + 1.024 = 59.80 ms**, against **32.768 ms** available. Lower comparator
thresholds would normally lengthen the pulse further. Do not proceed to a normal
20-pass `detect`/ADC-bias dataset and mistake WINCLIP for an optical failure.
Continue with long captures for amplitude/repeatability, or change the acquisition
approach deliberately after resolving the power issue. Do not move the ramp or narrow
the pulse by threshold alone merely to hide the buffer limit.

**Shutdown anomaly and confirmed present state:**

1. The log ends with `beam off`, `off` -> `requested shutdown`, but then
   **POWERING_ON (1486 ms), latch 1, railsready 0**. This was not successful shutdown.
2. Asked the owner to inspect current status before touching the fixture. The board
   had returned to **BENCH_RUNNING (99516 ms), latch 1**.
3. A second `off` then produced **STANDBY (11766 ms), latch 0, railsready 0,
   fault none, watchdog off**. Supply input still reads **5.209 V**; that upstream
   voltage does not mean the switched rail is enabled. Beam was explicitly turned off.
   **This is the current board state.** D12/VIR discharge has not been measured.

**Source-confirmed mechanism matching the bench sequence:** `power_request_on()` sets
`s_req_on = true` regardless of current state. `power_fsm_step()` consumes/clears it only
in **STANDBY**. The earlier 14:44 log issued `on` while already BENCH_RUNNING, so the
request remained pending until `off` took the FSM through FORCE_OFF back to STANDBY.
The stale request then closed the latch again; the second `off` succeeded because that
one pending request had been consumed. Code-intelligence references confirm the CLI is
the only caller besides the declaration/definition. **This is not a debounce or Pi
shutdown delay.** It is an untested stale-command sequence despite the older Phase 1
matrix passing.

**My procedural error:** I used `on` as though it were idempotent in the setup recipes;
that exposed a pre-existing firmware defect. Until fixed, send `on` **only from confirmed
STANDBY**, never to reconfirm an already-running rail, and verify actual latch 0 after
shutdown. The CLI's "requested shutdown" is an acknowledgement, not proof of rail-down.
The source header's request-state contract was not met for stale `on`. No source change
or reflash was made during that analysis; the subsequent fix/host verification is below.

### 2026-09-18 - Stale power requests fixed in source; reflash/bench verification pending

**User requested the fix. Implemented in `power_fsm.c/.h` and the CLI:**

- `power_request_on()` now returns acceptance status and queues only from **STANDBY**,
  with no shutdown/force-off pending. Repeated `on` while already powered, booting,
  shutting down or faulted is **refused**, not remembered for the next cycle.
- `power_request_shutdown()` cancels a pending start. In STANDBY/FORCE_OFF it leaves
  no stale shutdown that could stop the next intentional start. **Shutdown requested
  during startup/PI_BOOTING still waits for the existing normal shutdown path.**
- Force-off requests cancel pending starts/shutdowns; **all request flags are cleared
  in FORCE_OFF**, including teardown reached by button, supply failure or fault recovery.
- CLI prints **`REFUSED: 'on' needs STANDBY with no pending stop (state ...).`** and
  **`No power-on request was queued.`** for rejected starts. Its help text describes the
  new admission rule. Valid STANDBY starts still print `requested on` and obey the USB guard.

**Unchanged:** GPIO27 policy, all supply thresholds/debounce periods, Pi detect/boot/
shutdown timing, beam/detect/ADC behavior, phase/calibration/config record and flash layout.
Updated the related power header contract and corrected teardown comments referring to
30 % beam operation and `hpf track` rather than the actual high-level `hpf hold` case.

**Regression proof against the actual C producer:** added standalone native
`tests/CMakeLists.txt` and `tests/power_fsm_test.c`, compiling **`src/power_fsm.c`** with
real board constants and small mocked GPIO/time/ADC/beam/fault interfaces. No test
framework, package or SDK dependency was added to the host test project; it is not
linked into the firmware.

1. Built/ran the initial **17 tests before changing production code**: **13 FAIL / 4 PASS**.
   Failures reproduced the observed restart to POWERING_ON/latch 1, stale standby `off`,
   pending-request collisions, and restart after button/supply/fault teardown.
   Existing startup-shutdown handling, USB guard and stale fault-ack checks passed.
2. Applied the fix and added API acceptance plus Pi timeout coverage: **20/20 PASS** via
   CMake Tools/CTest, native **Release**, warnings treated as errors. Tests verify no
   new latch rise during 6 s after shutdown, fresh starts still work, 200 ms Pi shutdown
   pulse and 15 s floor remain, startup shutdown remains pending correctly, 90 s boot/
   60 s shutdown timeout behavior is unchanged, supply guards/debounce remain, and no
   GPIO27 writes occur. These are host logic tests, **not physical rail measurements**.
3. Restored the local CMake Tools source/build directory and Ninja generator to the Pico
   firmware after the native test run. **Release ARM build passed, no warnings/errors.**
   Editor diagnostics are clear for changed C/header files.

**Verified artifact:** `build/pitrac.uf2`, timestamp **2026-09-18 15:21:20**, CLI build
stamp **Sep 18 2026 15:21:18**. `picotool` confirms **Release**, SDK **2.3.0**,
**pitrac_ltb_v1**, family **rp2350-arm-s**, target **RP2350**, **ARM Secure**.
Size **145152 text / 0 data / 88308 bss** (text +384 bytes, BSS unchanged from 11:11:51).
The linked image contains the new refusal/help strings. UF2 SHA-256:
`B05C1624BB4186E7E52DB7DB3DFE80C97DF4976BB438E92E203631E4F2962745`.

**Reflash first.** Nothing has been flashed by the assistant. The board's last confirmed
state remains **STANDBY / latch 0**, on the **11:11:51** image. After flash, verify `id`,
UID **6d6fda754e367a40**, and `cfg` **slot A seq 5**, carrier **104166 Hz**,
phase **1311 ticks**, cal duty **25.00 %**, HPF polarity **TRACK = 0**.
Then perform **BENCH.md Test 6**: beam OFF, ADC idle, HPF TRACK whenever rail up,
no Pi/camera, supply 5.20 V. Do not mark the hardware regression passed until one `off`
after redundant `on` stays down and subsequent intentional starts still work.
No commits; unrelated Software changes left untouched.

### 2026-09-28 - Power-request fix flashed; BENCH.md Test 6 PASS (owner-reported)

The owner reported on 2026-09-28 that the power-request fix above was flashed and that
**BENCH.md Test 6 passed**. The console log and readings were not added to this repo; if
they turn up, record the `id` stamp (expected **Sep 18 2026 15:21:18**) and the Test 6
rows here. **The stale-`on` regression is closed.** The old-image workaround (`on` only
from confirmed STANDBY) is no longer needed on a reflashed board, but checking `stat` for
**STANDBY / latch 0** after a shutdown is still good practice.

Same day, documentation and comment-only corrections (no firmware behaviour change, no
reflash needed):
- `DEVELOPER_GUIDE.md` added at the repo root as the entry point for new developers.
- Stale comments fixed in `beam.h`, `panel.h`, `detect.h`, `safe_state.c`, `board.h`
  and `boards/pitrac_ltb_v1.h` (the last also lost a BOM and mojibake).
- `tools/netlist_report.py` now works on case-sensitive filesystems and dates the report
  from the netlist's own export stamp, so `--check` passes on a fresh clone.
- `LICENSES/CERN-OHL-S-2.0.txt` added (LICENSING.md referred to it; it was missing).

The full list is in `DEVELOPER_GUIDE.md` Appendix D.

### 2026-10-02 - Strobe 6a/6b dry-test firmware written; reflash + bench pending

**Owner request:** "Is the firmware ready for 6a and 6b? If not, build it." It was not:
`strobe_burst.pio` was assembled but never loaded, and there was no pulse command, gate
DAC, schedule, charge interlock or A7 fix.

**What was built** (`src/strobe.[ch]`, `src/strobe_plan.[ch]`, CLI `strobe`):

| Command | What it does |
|---|---|
| `strobe` | Status: rail, GPIO27 (read only), A7, gate DAC commanded + hardware readback, engine, limits, last run, whether a pulse / a gate raise would be admitted |
| `strobe pulse <us>` | One pulse, 5–100 µs, PIO0 SM0, DMA-fed, completion on IRQ0 |
| `strobe burst <w> <gap> <n>` | Uniform burst; gap 150–50000 µs; ≤ 16 pulses; burst charge ≤ 6.0 mC at the 9 A design current |
| `strobe clamptest <us>` | One pulse 101–2000 µs, past the software limit, for the 6a.1 U5 measurement |
| `strobe sched <m/s> [fire]` | The design's §13.4 schedule; `fire` runs it dry |
| `strobe gate <pct>` | Gate DAC (GPIO28, PWM 6A, 146.5 kHz, 20 ms settle); prints nominal TP3 |
| `strobe off` | GPIO25 SIO low, engine and DMA stopped, gate to zero; pulses refused for the next 20 ms while Q9's gate decays |

**The safety design, stated so it is not re-derived:**
- **Mutual exclusion — this build cannot command strobe current.** A pulse is admitted
  only while the gate DAC is provably zero (commanded level, PWM compare readback and pin
  function) **and at least 20 ms after it was last lowered** (Q9's gate decays through the
  RC ladder; see the review finding below). The gate rises only while no pulse can start
  (engine idle, GPIO25 low). Lowering to zero is always admitted. 6c must relax this
  deliberately, with tests.
- Every pulse also needs: rail ready (U8 and the gate amp run from VIR-derived +12 V), no
  latched fault, no Pi, **GPIO27 provably SIO output driven 0 (read only; still written in
  exactly one place)**, GPIO12 not on PWM.
- **GPIO25 stays SIO driven low between bursts.** It is handed to PIO only for an admitted
  burst, after the SM's output latch is set low and its direction set; every exit
  (completion, timeout, abort, rail-down) returns it to SIO low first. The PIO init
  helper used to set the pin direction before the level and claim the pad at boot; fixed.
- `power_fsm.c` calls `strobe_safe_off()` at the start of an orderly shutdown and in
  `FORCE_OFF` before the latch opens, because U5 is on always-on +3V3 while U8 loses its
  supply. The gate is therefore zero on every power-up.
- **A7 fixed:** ready LED is SIO on/off (≥ 50 % lights it); `strobe.c` owns slice 6;
  `strobe_init()` panics if GPIO12 is found on PWM, and every gate raise re-checks it.
- The MCU watchdog is **not** armed by this build: it cannot command current. 6c arms it.

**Netlist facts used** (re-extracted 2026-10-02): Gate_PWM → R55 10K → C52 0.1 µF →
R58 10K → C53 0.1 µF → U6A(+); U6A gain 1 + R60 20K / R59 10K = **3**; U7 (MMDT2227
complementary follower) drives TP3, inside the loop; R59 + R60 = 30 kΩ to GND from TP3.
U6, U7 and U8 run from **+12 V = R15 4K7 from VIR + D4 12 V Zener**. U5 runs from **+3V3**.
The gate-DAC filter is identical to the threshold DAC's, so `DAC_TOP` / `DAC_SETTLE_MS` are
shared.

**Schedule (design §13.4):** width = floor(1000 µm / v) clamped 5–100 µs; period =
round(42670 µm / v); gap = period − width ≥ 150 µs; 10 pulses, **shed one at a time with
the spacing kept** while charge > 6.0 mC, not below 3. Integer micrometres, because a
float 42.67 rounds the 20 m/s period (exactly 2133.5 µs) to 2133 instead of 2134. Charge
is evaluated on min(width, U5 clamp) at 9 A. First-pulse delay is not computed (needs
t_cam and FOV geometry; Phase 7).

**Corrections made on the way, in the record:**
- `board.h` said shedding "costs sample density, not coverage". **The design sheds and
  keeps the spacing**, so the burst covers fewer freeze positions. Withdrawn.
- `board.h` called `BURST_CHARGE_MAX_MC` a thermal budget. The design defines it as a
  **VIR sag budget** (~670 µF × ~9 V). Corrected.
- `BENCH_P6` asked where the ~3 mm in the 45.72 mm transit came from: it is
  `config.beam_width_mm = 3.0f` in design §13.1, a placeholder marked "calibrate!".

**Derived, not measured — 6a measures them:** PIO overhead (width word N → N + 2 µs; gap
word M → M + 8 µs; IRQ0 8 µs after the last falling edge), counted from the listing; and
U5's clamp (assumed 122 µs from U9).

**Verification:**
- **Independent code review** (read-only subagent) of the whole change found **one real
  interlock gap, now fixed:** the "gate is provably zero" check looked only at digital state
  (commanded level, PWM compare, pin function). `strobe off` zeroes those instantly, but
  Q9's gate decays through the 10K/0.1 µF ladder (dominant pole 2.62 ms), so
  `strobe off` + `strobe pulse` sent in one write could pulse into a gate still several
  volts high — breaking this build's "cannot command current" property. **My first version
  of the interlock was wrong in exactly the way this file warns about: it trusted firmware
  state for an analog node.** Fix: `gate_write()` is the only writer of the gate compare
  and stamps every non-zero → zero transition; the gate counts as non-zero for
  `DAC_SETTLE_MS` (20 ms) afterwards (`strobe_gate_settling()`, pure and host-tested).
  The reviewer re-checked the fix: no other writer of slice 6; TP3 from 9.9 V is 1.0 V at
  6.4 ms and 5.6 mV at 20 ms; no path admits a pulse inside the window. It confirmed the
  other invariants: GPIO25 handover and every exit, PIO cycle counts against the generated
  listing, schedule math, every latch drop via `FORCE_OFF`, GPIO27/GPIO12, CLI parsing.
- **32/32 native host tests PASS** (MSVC Release, /W4 /WX): 21 power FSM (20 existing +
  `strobe_teardown`) and 11 strobe plan — the §15 table at 90/50/20/10/2/100 m/s, shedding,
  bad speeds, burst limits, clamp-test bounds, PIO encoding, duration, the admission
  policy over **all 256 input combinations**, and the gate-decay window.
- **Mutation check:** removing `strobe_safe_off()` from `FORCE_OFF` made
  `power.strobe_teardown` fail ("Unsafe GPIO write ... pin 15, value 0"); restored; all pass.
- **Pico Release build clean, no warnings.** `picotool`: Release, SDK 2.3.0,
  pitrac_ltb_v1, RP2350, ARM Secure. **157744 text / 0 data / 88496 bss** (+12592 text,
  +188 bss vs 15:21:18). Built stamp **Oct 2 2026 09:27:16**. New help/refusal strings
  present in the image; strobe symbols linked.
- `HARDWARE_REFERENCE.md` regenerated from the netlist: one line changed (the A7 row);
  `--check` passes.

**Not done:** nothing flashed or run on hardware. No 6c/6d firmware. "TP3 during bursts"
moved to 6c (this build never pulses with a setpoint). No commit.

### 2026-10-05 - Full code + documentation audit; CLI bug fixes; PROGRESS split into live + archive

**Owner request:** a full code audit before 6a with refactor / tech-debt suggestions, and every
document brought up to date. **Owner decisions:** code — **fix bugs only**; refactors and
hardening go to a written backlog (§9), not scheduled. Docs — fix everything stale **and
restructure now** (this file → live + `PROGRESS_ARCHIVE.md`, dated results out of
`BENCH_P3_DETECT.md`, dedupe `BRINGUP_NEW_BOARD.md`).

**Method:** seven read-only audit passes (cli.c/main.c; ADC/beam/cal/detect/config; power/
safety/strobe/panel/service/shot plus the host tests; four document sets), plus direct checks
of the build, tools and repo hygiene. Every reported bug was confirmed in the code before it
was changed; three were not real (below). Already fine: `-Wall -Wextra` clean, forced board,
gitignore covers build/captures/roll CSVs, no TODO/FIXME, `tools/scope.py` framing matches the
CLI exactly, no PIO/DMA/PWM double claim, GPIO27 written only in `safe_state.c`.

**Fixed in firmware (build `Oct  5 2026 11:13:55`):**
- **Strict argument parsing everywhere** (`cli.c`). Only `strobe` used the strict parsers; every
  other command used `strtoul/strtof(..., NULL)`, which take junk and prefixes: `gpio xyz` read
  GPIO0, `gpio 2 o` drove a pin high, `cal gain 3100 67` (a percent typed for a fraction)
  solved for 67× full scale and wrote that gain into the config, and the `capture trig`
  timeout could wrap ×1000 into a short one. Every numeric argument must now be a whole number
  (decimal or `0x` hex) or a finite float inside an explicit range; anything else prints
  `ERR: ... Nothing was changed.` and does nothing. Valid input behaves as before, except
  leading-zero octal (`010` is now ten, not eight). `parse_u32` also gained an `errno` check:
  on this target `unsigned long` is 32 bits, so an overflowing number used to saturate to
  4294967295 and pass.
- **`threshold sweep` with 65535 steps never ended** — the sweep counts
  `for (uint16_t i = 0; i <= steps; i++)` — and 0 steps returned before filling the result.
  Steps are now 1–1000 (~20 s at 20 ms each).
- **`capture trig` pre-trigger is limited to 0–90 %**, matching the 90 % clamp inside
  `adc_capture_triggered()`; the `armed:` line used to report a split the capture did not use.
- **Over-long lines and more than 8 words** were silently truncated and the rest dispatched;
  both are now refused before anything runs.
- **The config loader now checks the record version** (`config_store.c`). Magic, size and CRC
  prove a record is intact, not that its fields mean what this build thinks. Board 3's record
  is v1, so it must still load — **confirm with `cfg` after the reflash.** Consequence: bumping
  `CFG_VERSION` will now make an old record fall back to defaults (see the §9 backlog).
- **`beam_plan()` divider capped at 255** (`beam.c`). Below ~9 Hz it reached 256, which the
  `(uint8_t)` cast wrote as 0, so the hardware ran at a divider the duty-ceiling check never
  saw. Only reachable with `beam freq` under ~9 Hz; 150 MHz / (65536 × 255) = 8.98 Hz is now
  the floor and the actual frequency is reported.
- **`strobe_run(NULL)` guard** (`strobe.c`) — the plan helpers were NULL-safe, the result copy
  was not. No caller passes NULL today.
- **`detect` now shows RX-FIFO overflow** (`detect.c`, `cli.c`). `detect_dropped()` existed but
  nothing printed it, so chatter loss in a §3.7 run was invisible. It counts polls that saw the
  sticky RXSTALL latch (≥ 1 word lost each), not words — comments corrected — and `detect arm`
  now clears the latch, which `pio_sm_restart()` does not. Printed only when non-zero, so the
  normal `detect` output is unchanged.
- **Stale printed text:** `id` (strobe 6a/6b is in the image; what is pending is strobe
  *current*); `help` and `panel` usage (`rdy` is on/off); `beam clamp` (prints the measured
  122.68 µs U9 clamp and the real effective duty instead of the pre-Q1 86 µs estimate, and no
  longer says its result sets the strobe limit — U5 is measured in 6a.1); `scan carrier` (no
  longer says "put the winner in board.h" — the carrier is fixed); `adc5vcal` (the scale IS
  persisted, by `cfg save`); `capture trig` (said "Make the sound" for every channel; now
  "roll the ball" on ch5, "make the sound" on ch7 — the `  window` prefix `scope.py` keys on
  is unchanged).

**Audit findings checked and NOT changed, because they were wrong:**
- "`capture 0 …` hangs the CLI" — `adc_capture()` returns 0 for an empty mask and the CLI
  prints `ERR: capture failed` before the loop. (The new mask range now refuses it earlier.)
- "A pending `on` survives a shutdown request in STANDBY" — `power_request_shutdown()` clears
  `s_req_on` unconditionally, and `pending_on_shutdown` already tests on → shutdown → step →
  latch never rose. **The 2026-09-18 power fix stands.**
- "`help` prints `25%%`" — the owner's 2026-09-17 transcript showed it, but the string had
  already been reworded; the current `k_help` prints a single `%`.

**Verification:** 32/32 host tests PASS (they do not cover cli.c, beam.c or config_store.c; the
§10 smoke list does). Release build clean, `picotool info -a` **Release**, `id` stamp
**Oct  5 2026 11:13:55**, **164232 text / 0 data / 88496 bss** (+6488 text vs Oct 2), UF2 SHA256
`171675B4309EC79BA1DA2199720800566A1E181EC3D389D577030E5D1B9108D6`. **Independent code review**
of the changes: one real issue — `threshold vref` accepted exactly 0.5 and 6.0, which
`detect_threshold_set_vref()` silently ignores (it excludes both endpoints), the very no-op the
check was added to prevent. Fixed (exclusive range) and rebuilt; a last rebuild added the `capture trig` wording; that is the stamp above.
Everything else confirmed: no uninitialised use, no skipped cleanup, format casts correct, the
FDEBUG write clears only PIO2 SM0's bit, the version check cannot reject a record `cfg_save` wrote.

**Documentation:** 862 lines of closed history moved **verbatim** from this file to
`PROGRESS_ARCHIVE.md` (checked by script: every original line is present, every moved block is
contiguous), keeping section numbers so existing "PROGRESS §n" references still land.
The 5.2 V supply setpoint is now explicit in `BRINGUP_NEW_BOARD.md` and `START_HERE.md`
(owner request). **Restructure, per the owner's choice:** dated results moved verbatim out of
`BENCH_P3_DETECT.md` (259 lines, 9 blocks) into the archive, with a verdict-plus-live-guidance
pointer at each spot, a §3.7 status box, and a Phase 3 results index in §5;
`BRINGUP_NEW_BOARD.md` now names the canonical BENCH section per step and drops repeated
prose (its §11 board-2 table had a chain delay of 570–590 ns against §9's 553–600 ns — the
duplicate is gone). **Doc errors fixed:** BRINGUP §6 told you to expect "slices ENABLED" from
`beam` *before* `beam on` (the readback is checked against the beam state, so off reads
`off ok`, SIO); BRINGUP §11 sent a new board to §3.6b/§3.6, which are done once (board 3);
the Phase 4 design note claimed the 32.8 ms ring holds "the whole bump down to ~1.4 m/s",
which the 22.7 ms pilot contradicts; `NEXT_BOARD_REV.md` had two CR-15 headings, CR-01 still
🔴 in its heading, CR-09 labelled "Unblocked" while translation is still required, and no
summary rows for CR-13/CR-14; ARCHITECTURE listed planned PIO/I²S work as in use and core 1 as
the hot path (it is unused); `Hardware/README.md` said strobe had no firmware; `START_HERE.md`'s
dated status block is now a pointer to PROGRESS; DEVELOPER_GUIDE said the host tests cover
only `power_fsm.c` and that every blocking command yields and aborts (unverified for five —
now flagged); BENCH_P6 status/prereq, refusal text and per-width `clamptest` lines;
BENCH_P5_P7 7a now says it is not runnable on current firmware. **Doc-audit findings that
were wrong, not changed:** the ADC ring is 32.768 ms per channel in every mode (500 ksps
total split across *n* channels), so the "only at one channel" rewrite was not applied; P8's
"8.0–8.5 firmware exists" is right (simulated Pi); BENCH_P6's CR-01 wording was accurate.
**Final checks:** every relative link and `#anchor` in 17 docs resolves (146 links, 52 with
anchors; script-checked), no stale stamp or placeholder remains, `netlist_report.py --check`
passes.

**Not done:** nothing flashed or run on hardware. No refactors (backlog in §9). No commit.

### 2026-10-05 - Audit-fix build flashed on board 3; post-flash smoke check 13/13 PASS

**Owner flashed** the `Oct  5 2026 11:13:55` image and sent the full log,
`captures/COM11_2026_10_05.12.32.34.965.txt` (2188 lines). **Checked line by line against the
§10 table, not taken on "looks good":**

| # | check | result |
|---|---|---|
| 1 | `id` | `built    : Oct  5 2026 11:13:55`, uid `6d6fda754e367a40`, new `fw`/`pending` lines ✅ |
| 2 | `cfg` | **slot A seq 5 v1**, 104166 Hz / 1311 ticks, scale 1.0620, TRACK = 0, model fitted ✅ — the new version check accepts board 3's record |
| 3 | `help` | no `%%`; `panel pwr` / `panel rdy` split ✅ |
| 4–10 | malformed input | all seven refused with the exact `ERR: … Nothing was changed.` / `NOTHING was run.` text ✅; `cfg` after `cal gain 3100 67` still `u12b gain: 14.5` ✅ |
| 11 | `capture 0x20 2000 250000` | header `mask=0x20 n=2000 rate=250000 overran=0`, exactly 2000 rows, `# end` ✅ |
| 12 | `beam freq 5` (beam off) | `8 Hz actual (TOP=65535)`, `div 0x0ff0` on slices 7 and 11 ✅ — divider 255, not the old wrap to 0 |
| 13 | `beam freq 104166` | TOP 1439, phase **1311 ticks**, `div 0x0010` ✅ |

**Two things in the log, neither a failure:**
- `cfg` warns `BEAM IS AT 2.00 %` against the 25 % `cal duty`. That is the boot default duty, not
  a change: set `beam duty 25` before any optical measurement, as always. Irrelevant to 6a/6b,
  which run with the beam off.
- **Printed-output bug found by step 12 (cosmetic, pre-existing):** `beam freq 5` printed
  "frequency step here is ~0 Hz". The step is computed as `SYSCLK/(TOP+1) − SYSCLK/(TOP+2)`
  and ignores the clock divider, so below ~2289 Hz (divider > 1) it is wrong — at 8 Hz the
  true step is ~0.0001 Hz per count, and the number printed is meaningless rather than
  dangerous. Not fixed now (a reflash for a print line is not worth interrupting the bench);
  added to the §9 backlog, to go into the next firmware change.

**Instrument constraint, 2026-10-05:** the owner currently has the **logic analyser only, no
scope**. 6a proceeds on the LA: U5's Q output at **R61** is the clamped pulse in 3.3 V logic,
and U8 only buffers it for Q10 (tens of ns against a ~122 µs clamp and a 22 µs margin to the
100 µs software limit), so the 6a.1 decision does not need the scope. **Deferred to the scope,
required before 6c:** Q10's gate level (~12 V), edges/ringing and width after U8.
`BENCH_P6_STROBE.md` updated with the LA-only route. 6b needs the DMM for TP3/TP2 (TP2 is
above the LA's 12 V analog limit).

### 2026-10-05 - 6a.1 first attempt: GPIO25 pulse at R57, nothing at R61 (U5 Q) — diagnosing

**Owner report:** the strobe pulse shows on the LA at **R57** (GPIO25 / `Strobe_Pulse`), but
**no signal at R61** (U5's Q).

**Serial log** `captures/COM11_2026_10_05.13.15.45.895.txt` (written when monitoring stopped,
13:39) — **the firmware side is clean:**
- **Step 0 PASS:** `id` stamp correct; `pins` → `STROBE_PULSE(25) 0`, `PULSE_LIMIT_DIS(27) 0`;
  `strobe` → `GPIO27 : SIO output, driven 0, pad 0 -> U5 pulse limiter armed`, A7 SIO, gate 0,
  engine idle, `may fire : +5V rail not ready`; `strobe pulse 20` → `REFUSED -- nothing fired`.
- **Step 1 PASS:** STANDBY / latch 0 / 5.203 V → `on` → `strobe` shows `rail : up (state
  BENCH_RUNNING)`, `may fire : yes`, GPIO27 still driven 0 / pad 0.
- **6a.1:** `strobe clamptest 200` ×5, every one `completed, IRQ0 seen`, enable→IRQ0 polled
  **213–216 µs** against 211 µs nominal, `GPIO25 after: low, SIO`. The `clamptest 100` refusal
  check was not run — a typo `clampteest` was correctly refused as an unknown subcommand. No
  `clamptest 1000` yet.

So five 200 µs pulses reached `Strobe_Pulse` exactly as the LA shows, and **GPIO27 was 0 at the
pad**, which leaves Q8 off unless Q8 is itself faulty. The fault is between U5's inputs and the
probe on R61.

**Netlist trace (re-extracted 2026-10-05):** `Strobe_Pulse` → U5 pin 2 (B) and pin 3 (CLR),
pin 1 (A) = GND. Pin 5 (Q) → R61 0 Ω → `GATE-DRV` = **R62 1 kΩ to GND** + U8 IN (pin 3);
U8 VDD = +12 V. Pin 7 (RCext) = R56 56K to +3V3 + C51 2.2 nF + **Q8 drain** (AO3400A, gate =
`Pulse_Limit_Disable` = GPIO27 with R54 1K to GND). U9 is identical minus Q8 and works at
122.68 µs, so the trigger wiring is proven. Q8 on would *defeat* the clamp (Q follows the
input), not silence Q, so GPIO27 cannot explain a flat Q on its own.

**Hypotheses, most likely first** (GPIO27 excluded by the log): the LA lead or channel on R61;
R61 open or missing; a U5 assembly fault (pins 2/3/4/5/8 joints, orientation, wrong part) —
board 3's strobe chain has never run before; +12 V absent, so U8's input ESD clamp drags
`GATE-DRV` to ~0.6 V, below the LA's logic threshold (TP2 has never been recorded on board 3;
`may fire : yes` checks only the latch state, not +12 V). The
7-step triage is in `BENCH_P6_STROBE.md` 6a.1, "If GPIO25 pulses at R57 but U5 Q (R61) shows
nothing".

The 13:51 log (`COM11_2026_10_05.13.51.09.029.txt`) shows three more `strobe clamptest 200`, all
`completed, IRQ0 seen`, 213–216 µs. It has no `id` line, so **which board it came from is not
recorded** — run `id` at the start of every log.

### 2026-10-05 (evening) - Board 2: a probe on R61 sparked. Incident, layout facts, my omission

**Owner report:** on **board 2**, putting a probe on R61 produced **a small spark**. The owner
plans to probe other points to check for damage, and proposes **board 1** (out of service for
optics since the 2026-08-17 foil incident, §11) as the strobe test board.

**What R61 sits next to** (computed from the KiCad layout, pad edge to pad edge): R61 is an
0805, pads 2.0 mm apart, between U8 and its decoupling.

| R61 pad | net | nearest exposed copper |
|---|---|---|
| 1 (U5 side) | `Net-(U5A-Q)` | R65.1 GND **0.68 mm**, C54.2 GND **0.75 mm**, C54.1 **+12 V 1.15 mm** |
| 2 (U8 side) | `GATE-DRV` | R62.1 GND **0.58 mm**, C54.1 **+12 V 0.75 mm**, C55.1 +12 V 1.20 mm, U8.2 +12 V 1.70 mm |

**Energy available:** the +12 V net holds C54 4.7 µF + C24 1 µF + C50/C55/C56 0.1 µF each
≈ **6.0 µF**, i.e. ½·C·V² ≈ **0.43 mJ** at 12 V — enough for a visible spark. The rail itself is
fed only through **R15 4K7** from VIR, so a sustained short draws ~36 V / 4.7 kΩ ≈ 7.7 mA. That
is current-limited, not a power-supply short.

**Likely cases, from the layout:**
- **+12 V ↔ GND** (e.g. across C54, or C54.1 to R62.1/R65.1): the decoupling discharges in a
  spark; nothing downstream sees more than 12 V. **Probably no lasting damage** beyond possible
  pad pitting.
- **+12 V onto R61** (C54.1 → either R61 pad): 12 V forced onto U5's Q output, which runs from
  +3V3. The 6 µF discharges through U5's output clamp diode into +3V3 → **U5 possibly damaged**.
  U8's input is rated for its 12 V supply, so it should be fine.
- **R61 ↔ GND:** harmless (U5 output briefly shorted, LVC current-limited).

**My error, recorded:** the bench doc I wrote told the owner to probe "R61 (0 Ω, either pad)"
with "rail down while connecting", and never said what R61 sits next to. "Rail down" also
leaves +3V3 live from USB. That is the same omission as the 2026-08-17 foil instruction (§11
lesson 1). **Fixed:** `BENCH_P6_STROBE.md` now says to solder a thin wire to **R61 pad 1** with
the board unpowered (PSU and USB off), and quotes the clearances. New hard rule in §1 (rule 6)
and in `HANDOFF.md` §3 (rule 8). Test points requested as `NEXT_BOARD_REV.md` **CR-21**.

**Board 1 for strobe work:** suitable. Its fault is U11B in the TIA chain (§11), which the strobe
chain (U5, U8, Q10, U6, U7, Q9) does not use, and the foil current was limited to ~3.3 mA by
R77. Run the `BRINGUP_NEW_BOARD.md` fast path (§1–§5 — it last ran in July, and has had an
incident since), flash the current image, then 6a.

**Board 2 damage check** (owner, next): board unpowered → magnified inspection and IPA clean around
R61/R62/C54/C55/R65/U8 → unpowered DMM comparison against another board (TP2→GND, R61 pad 1→GND
≈ 1 kΩ, +3V3→GND) → USB-only `id`/`stat`/`strobe` → PSU 5.20 V, `on`, TP2 ≈ 12 V, idle current
near the 129 mA reference, nothing warm.

### 2026-10-06 - Fault found: `GATE-DRV` / U5 Q shorted to GND at R62

**Owner report:** with the board unpowered, **both sides of R61 and of R62 have continuity to
GND**. The report doesn't say which board; it matches board 3's flat-R61 symptom, and a
continuity beep would also surface on board 2 during the damage check. **Record the board when
confirmed.**

**What it means:** R61 (0 Ω) joins U5 Q (pad 1) to `GATE-DRV` (pad 2), and R62 is the **1 kΩ**
pull-down from `GATE-DRV` to GND. A healthy board reads **≈ 1 kΩ** from either R61 pad to GND —
too high to beep. A beep means the node is shorted to GND, so **U5's output was driving into a
short, and R61 could never show a pulse.** That fully explains the 6a.1 symptom.

**Not a BOM/reel error:** R62 is an 0402 1 kΩ, LCSC C11702, on the same BOM line as 18 other
parts including **R57**. If the reel were wrong, R57 would short GPIO25 to GND, yet GPIO25's
pulse is clean at R57. So the fault is **local to R62's site**: a solder bridge across R62 (0402
pads, ~0.5 mm gap), a bridge from R61 pad 2 to R62 pad 1 (GND, **0.58 mm**), a misplaced or
tombstoned part, or debris. Less likely: a failed U8 input or U5 output.

**Consequences:** fail-safe — with `GATE-DRV` at GND, Q10 cannot turn on. U5 drove 3.3 V into
the short for at most the ~122 µs clamp on each of ~8 `clamptest` fires. LVC outputs normally
survive short, low-duty shorts, but U5's clamp is **re-measured** after the fix rather than
assumed good.

**Next (owner), board unpowered:**
1. DMM in **Ω**, not the beeper: R61 pad 1 → GND. ~0–2 Ω is a metallic short; tens to hundreds
   of Ω points at a damaged IC input or output. The beeper threshold is meter-dependent.
2. Magnify R62 and the R61-pad-2/R62-pad-1 gap. Wick any bridge, or reflow R62.
3. If nothing is visible: lift R62 and re-measure. If the short clears, R62 was it — fit a new
   0402 1 kΩ (required before 6c; it holds U8's input low if U5 is not driving). If it remains,
   lift R61 and measure each pad to GND separately: pad 1 shorted points at U5, pad 2 at U8.
4. After the fix: R61 pad 1 → GND ≈ 1 kΩ. Then solder the probe wire to R61 pad 1, power up,
   `id`, `strobe clamptest 200`.

**Added to the procedure:** this unpowered check (R61 pad 1 → GND ≈ 1 kΩ) is now a **gate
before 6a.1 on every board** in `BENCH_P6_STROBE.md` — it takes seconds and would have found this
before any pulse was fired.

### 2026-10-06 - R62 removed: still 10 Ω to GND. That board is parked; strobe work moves to board 1

**Owner:** with **R62 removed**, R61 → GND still reads **10 Ω**. **So R62 was not the short, and
my leading explanation above (a bridge across R62, or a bad R62) was wrong.** What remains on
the node is U5 pin 5 (output), U8 pin 3 (input), R61, and the gap from R61 pad 2 to R62's
now-empty GND pad (0.58 mm). A solder bridge would read under 1 Ω; **10 Ω looks more like a
damaged IC pin**. If this is board 2, +12 V driven into U5's output by the spark is a plausible
cause.

**Which board:** still not stated — board 2 (after the spark) or board 3 (the original flat-R61
symptom). **To confirm:** run Step −1 (unpowered R61 pad 1 → GND, Ω range) on both. It takes
seconds and tells us whether one or two boards are affected.

**State of that board:** **R62 is off the board**. It must be refitted (0402 1 kΩ) before 6c on
that board — it holds U8's input low when U5 is not driving. To localise later: lift R61 and
measure each side to GND (`BENCH_P6_STROBE.md` 6a.1 triage, check 3). Parked.

**Decision (owner):** move to **board 1** (U11B damage is in the TIA chain; the strobe chain is
untouched) via the `BRINGUP_NEW_BOARD.md` fast path, with leads **soldered** to R33 (GPIO25) and
R61 pad 1 (U5 Q) while unpowered and the LA ground on TP1. **Changed from the owner's plan:**
GPIO25 at **R33**, not R57. R57 is an 0402 whose GND pad is 0.38 mm from its signal pad and
0.46 mm from U5's pins; R33 is a 0 Ω 0805 carrying the same pulse with ≥ 1.2 mm to GND.
**Layout facts found while planning:** J3 pin 2 is `VIR_RTN` (LED return through Q9), not GND.
So VIR must be measured J3 pin 1 → GND (corrected in `BENCH.md` 0.5 and `BRINGUP_NEW_BOARD.md`
§2). Board 1's earlier trim of 1.0627 is within 0.03 % of the 1.063 default, so expect no
`adc5vcal`.

### 2026-10-06 - Board 1: fast-path bring-up done; 6a.1 PASS — U5 clamp 135 µs

**Owner report:** board 1 brought up via the fast path with leads soldered to R33 (GPIO25) and
R61 (U5 Q), LA ground on TP1. **6a.1: U5 Q (CH1) gives 135 µs consistently; GPIO25 (CH0)
matches the commanded 200 µs and 1000 µs.** The bring-up readings (currents, VIR, TP2, TP6,
`adc5v`) and the serial log were not sent. Ask for the log so the board-1 sign-off column can be
filled in.

**Interpretation:**
- **U5 clamps correctly** and is **inside the 109–136 µs band**, near its top. Margin to
  `STROBE_SW_MAX_US` (100 µs): **35 µs** → per the 6a.1 decision table, **keep 100**. The §15
  slow-ball rows (100 µs at 10 m/s) are unaffected.
- **The clamp is per-board.** U9 on the same board 1 measured 122.68 µs (2026-08-13): the same
  part and nominal 56 kΩ / 2.2 nF, 10 % apart, consistent with a ±10 % capacitor. So the
  "expect ~122 µs, U5 is the same circuit as U9" guidance was right about the band and too
  specific about the value.
- **What still runs on the old value:** `STROBE_HW_LIMIT_US_ASSUMED` = 122 is used only by
  `strobe_burst_charge_mc()` to cap a pulse's charge at the clamp. That matters only for pulses
  longer than the clamp — in this build, `clamptest`: 1 × 9 A × 122 µs = 1.10 mC vs 1.22 mC at
  135, both far under 6.0 mC. **No effect on 6a.2–6b, so no reflash now.**
- **Pending firmware change, batched after 6a.2** (with any PIO-overhead correction, so there
  is one reflash): set `STROBE_HW_LIMIT_US_ASSUMED` to the **worst case across boards**, not
  board 1's value. Proposed **137 µs** (nominal 123 µs × 1.11 for ±10 % C, ±1 % R). Update
  `board.h`, the `strobe` status line (today it prints `U5 clamp ASSUMED 122 us (U9's value --
  6a.1 measures U5)`, now stale), `clamptest`'s "expect 109-136 us (U9 … 122.68)" text, and the
  host test that pins the clamp charge.

**Next:** `BENCH_P6_STROBE.md` 6a.2 → 6a.3 on the LA, then 6b with the DMM on TP3 and TP2.

### 2026-10-06 - Board 1: 6a.2, 6a.3 and 6b PASS — the 6a/6b dry tests are complete

**Owner:** "Everything looks good." Measured: gate 50 % → **TP3 4.936 V**; gate 100 % → **TP3
9.88 V**; **TP2 ≈ 12.31 V** throughout; `off` returned TP3 to 0. Serial logs
`captures/COM5_2026_10_06.10.34.02.308.txt` (Step 1 / bring-up tail), `…10.41.35.840.txt`
(Step 0, Step 1, 6a.1), `…10.59.43.753.txt` (6a.2 → 6b), all with `id` = board 1
(`a764f5332ca5ac53`), image `Oct  5 2026 11:13:55`. **Checked line by line:**

| step | log | verdict |
|---|---|---|
| bring-up | `adc5v` 5.203 V, latch permitted; USB-only 4.885 V in STANDBY; `cfg` **slot A seq 1 v1** loads — the second real record the new version check has accepted | ✅ |
| 6a Step 0 / 1 | `pins` 25 = 0, 27 = 0; `strobe pulse 20` refused rail-down; after `on`, GPIO27 SIO / driven 0 / pad 0, `may fire : yes` | ✅ |
| 6a.1 | `clamptest 100` refused with the exact text; 200 ×5, 1000 ×2 completed (the earlier 10:41 log); **U5 clamp 135 µs** (§6 above) | ✅ |
| 6a.2 | pulses 5/10/20/50/100 all `completed, IRQ0 seen`, `GPIO25 after: low, SIO`; bursts 5/150/10, 20/500/10, 50/1000/10, 100/150/6 completed, polled IRQ0 within 2–4 µs of nominal (1413 vs 1411, 4713 vs 4711, 9513 vs 9511, 1365 vs 1361); all five refusals print the exact `INVALID … nothing fired` / `usage … NOTHING FIRED` text | ✅; LA widths and periods match per owner, **no PIO overhead correction** |
| 6a.3 | `sched 90/50/20/10/2` print **exactly** the §15 table (11/474/463/10 · 20/853/833/10 · 50/2134/2084/10 · 100/4267/4167/6 SHED · 100/21335/21235/6 CLAMPED+SHED; spans and charges match); `sched 1` refused; `sched 20 fire` 10 pulses, `sched 10 fire` 6 pulses, both completed | ✅ |
| 6b Step 1 (A7) | `strobe gate 50` → TP3 4.936 V; `panel rdy 100/0/auto` ran; TP3 did not move (owner) | ✅ |
| 6b Step 2 (interlock) | pulse at gate 50 → `REFUSED … gate DAC is not at zero …`; gate 0 → pulse completed; gate 25 admitted | ✅ exactly as specified |
| 6b Step 3 (ramp) | 0/10/20/30/40/50/70/90/100 commanded; **TP3 recorded at 50 % (4.936 V) and 100 % (9.88 V)**; TP2 12.31 V throughout | ✅ on the two recorded points; TP2 moved < 0.2 V |
| 6b Step 4 (rail-down) | gate 50 → `off` → STANDBY / latch 0 → `strobe` shows **level 0, hw compare 0** | ✅ firmware; TP3 0 V after `off` (owner) |

**Numbers worth keeping:**
- **Gate-chain transfer on board 1:** 4.936 / (3 × 0.5) = 3.291 V and 9.88 / 3 = 3.293 V → one
  straight line through zero, **3 × 3.29 V × duty**, linear to full scale. The 6b table assumed
  board 3's threshold-DAC reference (3.256 V); board 1's gate DAC runs ≈ 1 % higher, still well
  inside the ±3 % + 30 mV band. **No flattening at 100 %:** 9.88 V is ~0.9 V below the LM358 +
  U7 ceiling on a 12.31 V rail.
- **TP2 = 12.31 V** steady under every gate setting: the 12 V Zener rail (R15 4K7 from VIR) has
  headroom for the gate amp.
- The polled enable→IRQ0 of 61 µs vs 31 µs nominal on the pulse right after `strobe gate 0` is
  the coarse polling loop (it services USB), not pulse timing — the LA is the truth there.

**Gaps, small, recorded rather than glossed:**
- 6b Step 3: seven of nine TP3 points were not written down. The two that were sit on one
  line, which is strong evidence; record the rest next time the board is up.
- 6b Step 4: the second half — `on` again, then TP3 ≤ 30 mV — is not in the log. With the rail
  down TP3 is 0 regardless (the gate amp runs from VIR), so the real check is the re-power.
  The firmware shows level 0 / compare 0, so low risk; do it at the next power-up (10 s).
- TP3 ripple and Q10's gate (level, edges, width after U8) need the **scope** — still owed before 6c.
- The LA captures were not archived; the "every width and period within ±0.1 µs" result is
  owner-reported.

**Firmware batch — change of plan, stated plainly:** I said I would reflash after 6a.2 with
`STROBE_HW_LIMIT_US_ASSUMED` → ~137 µs (worst case) and any PIO-overhead fix. No PIO fix is
needed, and that constant affects nothing until LED current flows, so **it moves into the 6c
firmware change instead of a reflash of its own**. Until then the `strobe` status line still
prints `U5 clamp ASSUMED 122 us (U9's value -- 6a.1 measures U5)` — now stale, recorded here.

**Before 6c (all open):** (1) scope: Q10's gate via a lead soldered to R64's Q10 side, unpowered;
TP3 ripple; (2) firmware: the 6c guarded-current mode (pulses with a setpoint under a ceiling,
single pulses first, ADC0 plateau per shot), `STROBE_HW_LIMIT_US_ASSUMED`, and the 🔴
"before 6c" items of the §9 code-audit backlog — every long command keeps feeding the
watchdog, a strobe run token; (3) watchdog armed for 6c. Then `BENCH_P6_STROBE.md` §6c with the
LED bank on J3, FLIR on Q9/HS1.

### 2026-10-06 - Board 1: scope on Q10's gate and TP3 — gate drive clean; TP3 coupling baselined

**Owner photos** (Tek MSO54B, TPP1000 probes, `strobe pulse 20`, board unpowered while leads
were attached): `C:\Users\ATTAYEKP\Downloads\Photos-1-001 (4)\` (HANDOFF §9: screenshots stay
in Downloads). CH1 = R61 (U5 Q) in all five. CH2 = Q10's gate (`…172049087` leading, `…172125357`
trailing) or TP3 (`…172331022` leading, `…172358453` and `…172416739` trailing). Read off the
graticules, so ±5 ns and ±0.1–0.2 V.

**Q10 gate (via R64's Q10-side pad) — PASS:**

| | rising | falling |
|---|---|---|
| U5 Q → Q10 gate delay (50 % to 50 %) | ≈ 75 ns | ≈ 80 ns |
| Q10 gate edge (10–90 %) | ≈ 55 ns | ≈ 45 ns |

- The R61 pulse is **20.000 µs** edge to edge. The gate pulse is that **+ ≈ 7 ns**. Gate high
  ≈ 12 V, monotonic, no double edges. Commanded widths reproduce at Q10's gate.
- **Levels creep across the pulse on both channels** (R61 ≈ 3.0 → 3.3 V, gate ≈ 11.9 → 12.8 V).
  Neither a CMOS output nor a capacitive gate does that over 20 µs, and 12.8 V is above U8's
  VDD (TP2 = 12.31 V, DMM). So it is most likely probe compensation not yet run on those
  channels. **DC levels come from the DMM**, not these photos.
- **R61 rings** ≈ 1 V at ≈ 100 MHz for ≈ 40 ns at both edges, dipping to ≈ 2.3 V once on the
  rising edge. Most likely the probe's ground lead plus the soldered wire. It produces nothing
  at the gate, so it is not functional.
- The ≈ 55 ns rise is fine for this design: 1 % of the shortest 5 µs pulse.

**TP3 (Q9 gate, at gate 0) — the baseline for 6c:**
- **±0.3–0.4 V for ≈ 150 ns** at each R61 edge, with a ≈ +0.4 V spike at ≈ 60 ns on the rising
  edge — when Q10's gate rises.
- After Q10 turns off, a **−0.3 to −0.4 V step** (at ≈ 20.11 µs, as Q10's gate finishes falling)
  that recovers to 0 V in **≈ 5 µs**.
- **Topology (netlist, re-extracted):** U6A (LM358, ×3 via R60/R59) drives U7 (MMDT2227, an
  **unbiased complementary push-pull follower**) inside the loop: feedback is taken from the
  emitters, which are TP3. TP3 → R63 47 Ω → Q9 (IRLR2905) gate. Q9's source is Q10's drain
  (`PWR_LowSideD`); Q10 (AO3400A) source → R65 ∥ R66 → GND.
- **Hypothesis, not verified:** Q10's edges couple through its Cgd into the shared node, then
  through Q9's large Cgs (~1.7 nF class) into TP3. At gate 0 the follower sits in its ±V_BE
  crossover dead-band, so only the slow LM358 loop (0.3–0.5 V/µs) pulls TP3 back — consistent
  with the ≈ 5 µs recovery. At a setpoint one transistor conducts and should hold TP3 far more
  stiffly. That is what 6c measures on TP3 and TP4 ("TP3 steady with no ringing during bursts").
- **6c relevance:** the −0.3 V dip happens **after** the pulse and is gone in ≈ 5 µs, well inside
  the 150 µs minimum gap. A turn-on transient at a setpoint would land at the **start** of each
  current pulse — watch TP4 for current overshoot in the first ~150 ns.

**Still owed before 6c:** TP3 at a **static setpoint** (`strobe gate 50`, no pulses), scope
AC-coupled at 10 mV/div, 20 µs/div and 2 ms/div, < 10 mV p-p. The DAC ripple is µV after two RC
poles at ≈ 61 Hz and ≈ 417 Hz, so this is a **stability check**: an LM358 + unbiased follower
driving ~1.7 nF through 47 Ω could oscillate. A slow sine or bursts would mean it does. Running
the scope's probe compensation on both channels first would also settle the level creep.

**Owner, same day:** the DMM showed TP3 steady to ±1 mV at the tested setpoints, and asked
whether the scope check can be skipped. **My recommendation: no.** A DMM on DC volts averages
over ~100 ms, so an oscillation centred on the setpoint reads as a steady number. The DMM
cannot see the failure this check exists to catch. Two weaker supporting facts: TP3 lies on one
line through zero (4.936 V at 50 %, 9.88 V at 100 %), which a gross oscillation would likely
distort; and feedback is taken at TP3, before R63, so Q9's Ciss is buffered from the loop.
Risk is low, the check takes ~2 minutes with the probe already on TP3, and the alternative is
finding out in 6c with current flowing. **Fallback if skipped:** DMM AC mV on TP3 at gate 50 and
100 (limited by the meter's AC bandwidth), and the scope on TP3 and TP4 at the first,
lowest-setpoint 6c pulse. Decision pending (owner).

**Follow-up, same day — LA analog instead of the scope?** Yes, as an equivalent for the likely
failure mode. The LM358 loop cannot sustain oscillation much above its ~1 MHz GBW, which is
inside the LA's ~5 MHz analog bandwidth. It is not equivalent for a > 5 MHz parasitic in the
follower (R63 exists to damp that); 6c's first pulse covers it on TP3/TP4. Method: a gate-0
reference capture plus a spectrum. The LA's 12-bit / ±10 V resolution (~4.9 mV/LSB) cannot
decide < 10 mV p-p alone; periodic content averages out of the noise, as the 0.173 mV switcher
tone on +5 V did with this instrument. Use gate 90 rather than 100 (9.88 V is at the edge of
the ±10 V range). Procedure in `BENCH_P6_STROBE.md` exit criteria; I analyse the exported CSVs.

### 2026-10-07 - 6c/6d firmware: LIVE strobe mode — pulses with a setpoint, under guards (not yet flashed or fired)

**Owner request:** "implement the firmware for the remainder of strobe testing". **Build:**
Release **`Oct  7 2026 08:07:33`**, **184440 text / 0 data / 122720 bss** (was 164232 / 88496:
+30 KB is the ADC0 record buffer), `pitrac.uf2` SHA256
`DE0C66760E8364CDF02650DCCC591F1081D06D15FE477405D05F1B5F2DE80BC2`, no warnings. **Host tests
78/78** (32 before: +5 strobe-plan, +33 live, +8 service). **Not flashed, not run on hardware.**

**What it adds** (procedure and full guard table: `BENCH_P6_STROBE.md` 6c):
- **Live mode**, `strobe live on confirm`, from gate 0 only (`strobe live on` alone prints the
  checklist). It relaxes exactly one dry rule — a pulse may fire with the gate non-zero. The dry
  path is unchanged and is still the power-up default.
- **Guards** (constants in `board.h`, policy in `strobe_plan.c`, math in the new pure
  `strobe_live.c`):
  - gate ceiling **717 levels = 70.0 %** (TP3 ≈ 6.9 V);
  - **staircase:** each raise is at most **31 levels (3 %, 0.30 V at TP3, ≤ ~1.8 A)** above the
    highest level already fired **and judged OK**; re-arming restarts it from 0;
  - **ADC0 readback on every firing:** BURST at 500 ksps, 160 µs baseline, 200 µs after, frozen
    and copied, then verdicts:
    - plateau > **10.8 A** or a sample > **13 A** → `FAULT_STROBE_OVERCURRENT`;
    - current before the pulse, still on at the end, or on > **200 µs** → `FAULT_STROBE_CLAMP`;
    - more current pulses than fired, or no usable record → live mode ends, no fault;
    - every one of these ends live mode with the gate at 0;
  - **pacing:** ≥ 100 ms between firings; ≤ 30 mC per 10 s, booked before firing at 9 A × the
    clamp-limited width. Neither resets on re-arm;
  - live firing span ≤ 30 ms; idle exit after 300 s;
  - the **watchdog is armed by live mode**, `wdog off` is refused while live, and the CLI is
    limited to `help id stat pins adc5v fault off forceoff wdog strobe`.
- **6d:** `strobe clamptest` while live is admitted only at the level of the last live firing,
  and only if it measured 0.5–2.5 A.
- **`strobe cal [A]`** (2–9 A, default 9): from gate 0 in 20-level steps until current shows,
  then 5-level steps, aborting above 1.2 × target. It interpolates, sets the level and fires 3
  confirmation pulses (pass ±5 %). RAM only.
- **`strobe wave`:** the last record as `# capture` CSV. `adc_ring_freeze_copy()` was added for
  the readback.
- **Constants:** `STROBE_HW_LIMIT_US_ASSUMED` 122 → **`STROBE_HW_LIMIT_US` 137** (worst case;
  board 1's U5 135 µs), so a clamp test now books 1.23 mC instead of 1.10. The §15 schedules are
  unchanged. `STROBE_TIMEOUT_MARGIN_US` moved to `board.h`.
- **"Before 6c" backlog (§9) closed:**
  - fault detection by latch generation (SVC-02) and an abort on the final yield slice (SVC-03),
    both host-tested;
  - run token `s_run_gen` (STR-04/05); snapshot reads the GPIO25 pad (STR-07); STR-02 checked,
    no change needed;
  - `capture` CSV dumps now service the FSM and watchdog;
  - `level`, `hpf test`, `cal demod` and `cal model` checked: they already yield;
  - `shot.c` deadlines reclassified to "before Phase 7".
- **Printed text and comments audited:** `strobe` status (DRY / LIVE header, `U5 clamp <= 137 us
  (board 1 measured 135)`, PIO overhead "confirmed"), `id`, `help`, clamptest text, `wdog`;
  `board.h`, `strobe_plan.h`, `strobe_burst.pio`, `main.c`, `shot.c`, `power_fsm.c`,
  `service.h/.c`.

**Decisions I made; the owner may override any of them.**
- All the numbers above.
- Same command words in both modes rather than a `strobe live pulse` family. Dry procedures run
  at gate 0, where Q9 is off, so a dry procedure typed while live still produces no current.
- More current pulses than fired ends live mode **without** a fault. It may be a split plateau
  rather than a hardware failure; `strobe wave` shows which.
- **6d runs inside 6c Step 3 at the first ~2 A step**, so the clamp is proven with current
  before anything higher is fired. This is a change of order from the original doc.

**Corrections, recorded plainly:**
- **An independent review found two bugs in my first version, both fixed:**
  1. **A single commanded pulse that conducted 2–16 times was judged OK and raised the
     staircase.** The NOISY check compared against 16, never against the pulses fired.
  2. **The baseline/pulse split used a clock read that an interrupt could separate from the
     ADC stop.** A ~15 µs USB interrupt would have put a healthy pulse into the baseline and
     latched a false `STROBE_CLAMP`. The split is now counted from the stop instant, read with
     interrupts off; ≥ 8 µs margin to the first edge for any delay.
  - Each fix has a host test, and reverting either one makes its test fail. The reviewer
    re-checked both.
- **Caught myself before the build was final:**
  - a 716-level ceiling would have refused the documented `strobe gate 70` (rounds to 717);
  - a 1 A cal target could abort on its first detected coarse step (up to ~1.23 A > 1.2 A), so
    the minimum is now 2 A;
  - `STROBE_CAL_OVERSHOOT` was both a macro and an enum value;
  - `strobe live on` printed "NOT ARMED" while armed;
  - `service.h` said `pitrac_yield_ms()` returns as soon as an abort appears, but it honours the
    whole delay — the comment now matches the code and the test.
- **Dates:** this session crossed midnight. Firmware comments and docs first said 2026-10-06
  for this work; corrected to **2026-10-07**. The 6a/6b measurements are 2026-10-06.

**Unverified until 6c runs:**
- the gate-to-current slope (the guards assume ≤ ~6 A/V at TP3);
- ADC0 accuracy against TP4 (no cap on the ADC0 net — R32 4.7 kΩ only, netlist-checked — so
  2 µs point samples);
- TP3 at 9 A (design 4.5–6 V);
- the VIR droop across a burst;
- RP2350 ADC noise at baseline (the 12-code detection floor and 40-code baseline limit assume
  a few codes).

**Follow-up, 2026-10-07 afternoon — TP3 stability check: scope or LA?** The owner asked.
**Scope preferred**, with the LA as an acceptable fallback; do not wait days for a scope.
- **Resolution:** the scope at 10 mV/div AC reads < 10 mV p-p directly; the LA's ~4.9 mV/LSB
  cannot, and has to rely on the gate-0 comparison and the spectrum.
- **Bandwidth:** the scope also sees a > 5 MHz parasitic in the U7 follower, which is outside
  the LA's ~5 MHz analog bandwidth. The likely LM358-loop mode (< ~1 MHz) is inside it either
  way.
- **The rest of 6c:** the scope matters more there. TP4's first ~150 ns, where the 6b baseline
  predicts edge coupling, is invisible to the LA. For the ±5 % plateau comparison against ADC0
  the LA is adequate: 4.9 mV is 1.8 % of TP4 at 2 A and 0.4 % at 9 A.
- **Procedure:** the scope route (probe compensation, AC 10 mV/div, 2 ms / 20 µs / 100 ns per
  div, FFT, gate 0/50/90/100) is now in `BENCH_P6_STROBE.md` 6b exit criteria.
- **Owner, 16:20: the kit on hand is a Saleae Logic Pro 8 and a Tektronix TDS 1002** (the
  MSO54B is not available).
  - **Recommendation: the TDS 1002 for the stability check.** 60 MHz and 1 GS/s cover the
    follower-parasitic case that the Saleae's ~5 MHz cannot.
  - Its 8 bits at 20 mV/div are ~0.8 mV per level (20 mV/div is its minimum with a 10× probe),
    so a 10 mV criterion is still readable. Gate 0 sets the pickup floor.
  - **Peak Detect at 2 ms/div**, so a MHz oscillation cannot alias away at 2500 points.
  - FFT only if MATH has it. Otherwise a Saleae capture gives me the spectrum.
  - **For 6c, both instruments:** TDS CH1 TP4, CH2 TP3, EXT TRIG from the GPIO25 wire; Saleae
    digital on GPIO25 and U5 Q, optionally analog on TP4.
  - The scope's ground is mains earth: clip it to TP1 only, never TP5. **(Superseded the same
    day: the ground goes on R65's GND pad, not TP1 — see the next entry. Corrected again
    2026-10-08: R66's GND pad, not R65's.)**
  - FFT only if MATH has it. **(It does: MATH → Operation FFT on this TDS 1002.)**
  - `BENCH_P6_STROBE.md` (6b exit criteria; 6c board state and scales) is rewritten for this
    kit. The 6c scales are now 100 mV/div to ~5 A and 200 mV/div above, read with cursors.

**Correction (mine), found while re-reading 6c for that answer:** I wrote that the staircase
means "no typed command can jump from a measured current to an unmeasured one". That holds only
while the load stays connected.
- A firing that detects **no** current is "within limits" and raises the proven level, so with
  J3 open the staircase climbs with no load.
- The procedure itself is safe: re-seating J3 needs the board unpowered, which ends live mode
  and restarts the staircase from 0.
- An **intermittent** J3 or string connection is not covered. The first real current would land
  at a level never measured with load — bounded after the fact by the 10.8 A stop, one pulse
  ≤ 100 µs.
- **Mitigation now, docs only, no reflash:** 6c Step 3 stops if nothing is detected by gate 30 %
  (TP3 ≈ 3.0 V, well past Q9's 1.0–2.0 V V_GS(th)).
- **Firmware option** (§9, not done): refuse to raise the proven level above ~30 % on a firing
  that detected nothing, and end live mode with "no current — load open?". The same overstated
  sentence is in the `board.h` comment above `STROBE_LIVE_GATE_STEP_MAX`. It is left unchanged
  so the `Oct  7 2026 08:07:33` image stays as documented; fix it with the next firmware change.

### 2026-10-07 - Board 1: TP3 stability check on the TDS 1002 — no oscillation seen; boost pickup masks the floor; redo with a local ground

**Setup (owner):** board 1, dry mode, J3 disconnected, still on `Oct  5 2026 11:13:55` (the dry
path is the same in the new build). TDS 1002, 10× probe after PROBE CHECK, tip on TP3, ground
clip on **TP1**. CH1 AC, 20 mV/div, **BW limit on** (`Bw` in the readout). Trigger CH2 at
1.00000 kHz, so the CH2 probe was still on PROBE COMP. That is harmless: the spikes are not
synchronous with it. 14 photos in `C:\Users\ATTAYEKP\Downloads\Photos-1-001 (5)\`: three per
gate in time order (0, 50, 90, 100), plus `FFT_Gate_0.jpg` and `FFT_Gate_100.jpg`. The owner
reported "the measurements were jumping all over the place".

**Results (read from the photos):**

| gate | 2.5 ms/div Pk-Pk (min / max) | 25 µs/div Pk-Pk | 100 ns/div Pk-Pk |
|---|---|---|---|
| 0 | 143 mV (−68 / +75) | 35.2 mV (at **50 µs/div**) | 14.4 mV |
| 50 | 160 mV (−78 / +82) | 172 mV (−89 / +83) | photo blurred |
| 90 | 155 mV (−74 / +81) | ~33 mV (−73 / +83) | 20.8 mV |
| 100 | 155 mV (−79 / +76) | 36.0 mV (−18 / +18) | 25.6 mV (includes one slow dip) |

- **The same bursty spikes at all four gates.** They are ±70–85 mV, clustered in bursts tens of
  µs apart. Between them is a band of ~8–10 mV p-p at every gate. Pk-Pk is flat with setpoint
  (143–160 mV). Nothing that looks like a sine appears at any timebase.
- **Not the loop and not the DAC PWM.** At gate 0 the PWM is static low and the follower idles in
  its dead-band; at gate 100 the PWM is static high. Both look the same as 50 and 90.
- **Cause: pickup in the probe's ground loop.** TP1 is **72 mm** from TP3 (PCB: TP3 (33.3,
  121.1), TP1 (104.9, 110.8)), and the LM5157 boost (U1 (62.0, 105.5), L1 (56.4, 110.9), D2
  (54.3, 104.4)) sits between them. With J3 open the boost is at near-zero load, so it switches
  in bursts. That explains the jumping readings: Pk-Pk, Min and Max depend on whether a burst
  lands in the 2500-point record, and "Cyc RMS ?" means the scope found no cycle (the TDS 1000
  has no plain RMS).
- **Beaded pattern:** at 25 µs/div the band shows a regular ~12 µs pattern (~84 kHz apparent).
  It is clearest at gate 90 and faint at 50 and 100. Most likely it is the ~1.08 MHz boost beating
  against the display's 1 µs columns. Not proven, because gate 0 was captured at 50 µs/div.
- **FFTs:** both were taken at 1 GS/s, i.e. 0–500 MHz in ~0.5 MHz bins. The whole loop band falls
  in the first bin and everything above 60 MHz is beyond the scope, so they carry no information
  about the loop. They look the same.

**Reading:** no sustained oscillation of the U6A + U7 loop. An unstable loop grows until slew rate
or output swing limits it: an LM358 at ~0.3 V/µs allows ~1 V p-p at 100 kHz and ~0.2 V p-p at
500 kHz. That would stand far above this ~10 mV band and would be absent at gate 0. It is not
there. **The < 10 mV ripple criterion is not certified**, because the pickup floor is 15× higher.

**Why fix it rather than wave it through:** the same 72 mm loop would put the same ±80 mV on TP4
in 6c. That is ±0.6 A apparent at 135 mV/A, larger than the ±0.1 A ADC0-vs-TP4 band.

**Corrections (mine):**
- **TP1 as the ground was my instruction.** I did not check where TP1 is (it was fine for 6a's
  digital timing).
- I said to run with **BW limit off**. Leaving it on is better here: 20 MHz is far above the
  loop band, and it trims switching edges.
- I said the original TDS 1002 "may not" have FFT. This one does.
- I specified 2 ms and 20 µs/div; the TDS steps are 2.5 ms and 25 µs.

**New ground:** a short wire soldered, unpowered, to **R65's GND pad**, the pad of the 2512 sense
resistor farther from Q10 (PCB (45.3, 117.1); the pad toward Q10 is CurrentSense).
**(Corrected 2026-10-08: use R66's GND pad instead. R65's is 0.68 mm from R61 — see the next
entry.)**
- It is 12.6 mm from TP3, 9.4 mm from TP4 and 40 mm from D12.
- For TP4 it measures straight across the sense resistor.
- R67 pad 2 (0603, GND) is 2.4 mm from TP3, if a TP3-only ground is ever wanted.

**Redo** (procedure in `BENCH_P6_STROBE.md` 6b exit criteria):
1. A control capture with the tip on the ground wire.
2. Gates 0/50/90/100 at the same 2.5 ms (Peak Detect), 25 µs and 100 ns/div.
3. FFT at 25 µs/div with a Hanning window (0–5 MHz, ~5 kHz bins).
4. One 100 ns/div capture at gate 90 with BW limit off.

The 6c probe grounds now go to the same wire (6c board state).

### 2026-10-08 - Lead plan for the rest of Phase 6; correction: ground on R66, not R65

**Owner:** wants every lead for the rest of Phase 6 soldered in one session. I checked each candidate
point in the PCB file: nearest other-net copper (pad edge to pad edge) and parts on the bottom
side. The table, the do-not-solder list and the pre-power DMM checks are in `BENCH_P6_STROBE.md`
6c board state, "Leads to solder for 6c/6d".

**The plan:**
- **Keep** R33 (GPIO25) and R61 pad 1 (U5 Q).
- **Add two ground wires on R66's GND pad.** No other-net copper within 4.6 mm; 14.4 mm from TP3,
  13.2 mm from TP4.
- **Add TP3 and TP4.** Both are 1 mm through-hole pads with 0.5 mm holes, and nothing is on the
  bottom side within 17 mm, so feed the wire through from the top and solder on the bottom. TP3 is
  0.59 mm from C56's +12 V pad on top.
- **Twist each signal with its ground.** Every instrument ground goes to those two wires.

**Correction (mine):** yesterday I said to put the ground on **R65's GND pad**. That pad is
**0.68 mm from R61 pad 1** (U5 Q, already wired), the spot the 2026-10-05 board-2 spark entry
measured and recorded in this file. I had the fact and did not check it against the
recommendation. **R66's GND pad** is the same node — the low side of the paralleled sense
resistors — so the measurement is unchanged. The R65 references in `BENCH_P6_STROBE.md` and §10
are corrected; earlier §6 text keeps a correction note.

**Points not to solder, with reasons recorded:**
- **TP2:** 1.92 mm from R15's VIR (36 V) pad. A bridge puts 36 V on the 12 V rail.
- **R67:** a bridge across its pads shorts Q10 drain–source, so current flows with no pulse,
  bypassing U5's clamp and the sense resistor.
- **TP4 itself:** a bridge from TP4 to GND reads ≈ 0.135 Ω (indistinguishable on a DMM) and would
  blind the firmware's overcurrent check, so TP4 is inspected by eye.

### 2026-10-08 - Board 1: TP3 loop stability PASS (MSO54B, soldered leads, ground on R66)

**Setup (owner):** MSO54B, TPP1000 10× on the soldered TP3 lead, ground on the R66 lead; AC,
20 mV/div, full 500 MHz; trigger CH1 rising 3.2 mV, Auto; dry mode, J3 disconnected. Three
timebases per gate, 0/50/90/100 (200 µs, 2 µs, 10 ns/div), plus FFTs at gate 0 and 90. All are
running means over 140–2815 acquisitions. 14 photos in `C:\Users\ATTAYEKP\Downloads\Photos-1-001
(6)\`. The owner noted "still quite a bit of noise".

| gate | AC RMS 200 µs/div | AC RMS 2 µs/div | AC RMS 10 ns/div | Pk-Pk 200 µs/div |
|---|---|---|---|---|
| 0 | 3.795 mV | 3.737 mV | 4.473 mV | 193.8 mV |
| 50 | 3.604 mV | 3.780 mV | 6.127 mV | 107.9 mV |
| 90 | 3.717 mV | 3.358 mV | 4.907 mV | 200.9 mV |
| 100 | 3.818 mV | 3.659 mV | 3.688 mV | 116.5 mV |

**Reading: PASS — no oscillation of the U6A + U7 loop.**
- **The 2 ms AC RMS is flat with the setpoint.** Even allowing ±0.3 mV of scatter per reading,
  a setpoint-dependent component is **< 2.5 mV rms, ≈ 7 mV p-p as a sine**, anywhere in
  ~500 Hz–500 MHz. That is under the 10 mV p-p criterion.
- **Pk-Pk does not trend** with the setpoint (gate 0 is among the highest). Every capture carries
  the `Clipping` flag from occasional spikes past ±100 mV.
- **The 10 ns/div RMS** covers only 100 ns around a triggered ring, so it measures the ring, not
  the floor.

**What the noise is:**
- **The boost.** At 2 µs/div the spikes come on a regular **~1.0 µs spacing**, consistent with the
  LM5157's 1.055 MHz design frequency.
- **The probe's ground lead.** At 10 ns/div each spike is a damped **~450 MHz ring** (~2.2 ns
  period). That is the probe ground lead's inductance resonating with the probe's 3.9 pF,
  excited by the switching edges, and visible because the scope was at full bandwidth.
- **The same at gate 0.** So it is pickup or coupling, not the loop.
- Compared with the TDS on the TP1 ground: the 2026-10-07 ±80 mV bursts at 20 MHz are not
  directly comparable to full-bandwidth rings. With the 20 MHz limit these mostly disappear.

**FFTs:** taken at 2 µs/div, i.e. 0–3.1 GHz with 50 kHz bins. The loop band is in the leftmost
pixels and looks the same in both.
- The gate-90 FFT alone shows a narrow line at **~850 MHz**: the cellular uplink band (824–849
  MHz). It is most likely the phone taking the photos.
- It cannot be the board: it is above the scope's 500 MHz bandwidth and the MMDT2227's fT, and Q9
  carries no current with J3 open.
- Not verified. Repeating it with the phone away would confirm it, but the verdict does not rest
  on it.

**Not done:** the control capture (tip on the ground lead). The gate-0 reference carries the
verdict.

**Consequence for 6c:** read the plateaus with the **20 MHz bandwidth limit**. At full bandwidth
these rings are ±0.1–0.3 A apparent on TP4. Take one full-bandwidth capture of the first live
pulse, for turn-on overshoot only. Added to the `BENCH_P6_STROBE.md` 6c scope settings, with the
MSO54B channel plan: CH1 TP4, CH2 TP3, Aux Trig from GPIO25.

### 2026-10-09 - Handoff audit for a change of model/harness

**Owner asked** for the docs to be ready for a different model or harness. Findings and changes:

**Firmware on board 1 (inferred):**
- Board 1 runs the **6c build**. Its `captures/COM5_2026_10_07.16.42*` and
  `COM5_2026_10_08.15.50*` logs (the TP3 sessions) print `Dry pulses are now REFUSED until
  'strobe gate 0'`. That wording exists only in the 2026-10-07 builds; the 2026-10-05 image
  printed `Pulses are now REFUSED`.
- The only 2026-10-07 image handed over is `build/pitrac.uf2` = `Oct  7 2026 08:07:33`, SHA256
  `DE0C6676…`, unchanged since 2026-10-07 08:07.
- No `id` was logged, so confirm it before 6c. §0, §10 and `BENCH_P6_STROBE.md` updated.

**Session-only scripts moved into the repo.** The previous harness kept two scripts outside it:
- `tools/pilot_analysis.py` — the §3.7 pilot analysis that §9 asked to promote. Output paths are
  now derived from the input file. Re-run on `captures/COM11_2026_09_18.14.54.08.428.txt`, it
  reproduces the recorded **3101.8 mV peak, 22.71 ms FWHM, no rail samples**.
- `tools/check_doc_links.py` — the doc link and anchor checker used by every doc audit since
  2026-10-05. Repo-relative now; 17 files, 0 problems.

**`HANDOFF.md` updated** with conventions only:
- **Safety:** board 1 is the strobe board (§3 rule 1); the probe-point rule now says to check the
  PCB file (rule 8); new rule 9 on live strobe current, including "never raise a live limit to
  pass a test" and the open-load gap.
- **Build and tools (§4):** PowerShell build and host-test commands, how to identify and record
  the build stamp, the link checker, and COM5 = board 1.
- **Working rules (§5):** an independent review for safety-relevant firmware (rule 8), and read
  the `captures/` serial logs (rule 9).
- **Traps (§7):** PowerShell stdin and non-ASCII, `[IO.File]` relative paths, MSVC C4127,
  KiCad 10 `.kicad_pcb` pad syntax and rotation, stale views.
- **Lessons (§8):** a new "Probing" block (ground location, the ~450 MHz ground-lead ring and
  20 MHz BW limit, AC RMS against a reference state, the phone's 850 MHz line, FFT span), plus
  two interlock lessons (no-signal is not safe; time windows from the stop instant).
- **Data and tools (§9):** instruments, photo folders, serial logs.

**Open questions for the owner** are now listed in the §10 resume block.

**Root pointer files (owner's choice):** `AGENTS.md` and `CLAUDE.md` at the repo root, identical.
Harnesses read them automatically. They send the reader to `HANDOFF.md`, then §0/§10, then the
phase's bench doc, and repeat the few rules that must survive a skim: no commits, reflash-first,
the safety list, never raise a live limit.

**Committed at the owner's request:** `a8e08b3` "Phase 6 strobe: dry tests, live-current mode,
host tests, handoff docs" — 97 files, everything in the tree, including the strobe sources and
tests (never in git before), `PROGRESS_ARCHIVE.md`, the two tools, the root pointers, and the
owner's pending `Software/camera-comparison` and `Hardware/Calibration` work. Not pushed. A
follow-up commit corrects the docs that still said "uncommitted".

### 2026-10-12 - Board 1: 6c Step 0 (J3 disconnected) - image confirmed by `id`

Log: `captures/COM5_2026_10_12.08.41.04.756.txt` (preceded by a `bootsel` in
`COM5_2026_10_12.08.40.36.106.txt`; whether or not it was reflashed, the stamp is the right one).

| Check | Result |
|---|---|
| `id` | `built : Oct  7 2026 08:07:33`, `fw` ends `+ 6c/6d live current (guarded, ADC0 per firing)`, uid `a764f5332ca5ac53` - ✅ **board 1 runs the 6c image; no longer inferred** |
| `strobe` | `DRY.`; GPIO27 `SIO output, driven 0, pad 0 -> U5 pulse limiter armed`; A7 on SIO; `U5 clamp <= 137 us (board 1 measured 135)`; `live : OFF -- last ended: not armed since boot`; gate level 0, hw compare 0 - ✅ |
| `strobe pulse 20` (dry, gate 0) | `1 x 20 us -- completed, IRQ0 seen`, enable->IRQ0 polled 34 us (nominal 31) - ✅. LA widths: not in the log (owner to report; Step 2 records GPIO25 / U5 Q on the LA anyway) |
| `off`, `stat` | `STANDBY`, `latch 0`, fault none, 5V_IN 5.203 V - ✅ safe to work on the board |

**My omission:** the Step 0 instructions I gave (2026-10-09 chat) put `strobe` straight after
`on` with no wait, so it was read in `POWERING_ON`: `may fire` / `may gate` / `may arm` show
`+5V rail not ready` instead of `yes`. Harmless - the pulse that followed fired, so the rail came
up - and 6c Step 1 re-reads `strobe` with the rail up. Instructions that follow `on` must say
"wait ~3 s, `stat` shows the rail up".

**Log oddity, not yet explained:** in the saved file the `strobe` output is out of order -
`PIO overhead` (the third `limits` line) comes straight after `A7`, the `gate DAC` / `nominal` /
`engine` / first two `limits` lines come after the `> ` prompt, and `...design current;` runs
into `gate DAC` with no newline. Every line is present. `strobe_status()` in `cli.c` prints them
in one fixed order to one stdout stream, and a USB CDC stream can drop bytes but not reorder
them, so this points at the host (Serial Monitor's file logging), not the firmware. To check:
does the on-screen scrollback show the same order? It matters for 6c, where the per-firing
ADC0 block in the log is the evidence.
- ✅ **Answered the same day: host-side.** The owner's pasted scrollback of the same session has
  every line in firmware order. **Serial Monitor's file log reordered whole output chunks**
  (nothing lost). Rule (HANDOFF §4): the scrollback is the record where order matters; the file
  log is a backup. `strobe wave` of a 21.4 ms burst is ~11 000 lines, over the 9000-line
  default scrollback, so raise the scrollback limit before Step 5.

**Owner's decisions, 2026-10-12:**
- **Rely on the Step 3 stop rule** ("nothing by gate 30 % -> stop") for the open-load staircase
  gap; no firmware fix before 6c. The §9 item stays open.
- **Instruments for 6c today: TDS 1002 + Saleae** (no MSO54B) - the bench doc's TDS paragraph.
  With 2500 points the TDS cannot resolve 20 us plateaus across a 21 ms burst, so the Saleae
  analog channel on TP4 is no longer optional.

**6c Step A, unpowered DMM checks (owner, 2026-10-12) - all PASS:**

| From -> to | Read | Reading |
|---|---|---|
| G1 / G2 -> TP1 | 0 ohm | both ground wires on GND |
| R61 pad 1 -> GND | 1 kohm | no bridge near R61 / R65 |
| TP3 -> G1 | 30 kohm | exactly R60 (20k, TP3 -> U6A-) + R59 (10k, U6A- -> GND), from the netlist |
| TP3 -> TP2 (+12 V) | 30 kohm | not a bridge (that reads ~0 either polarity). Netlist: +12V is a shunt rail, R15 4k7 from VIR with D4 (12 V zener, anode GND) to GND, so with + on TP3 the meter sees R60 + R59 to GND, then D4 **forward** from GND to +12V. Hence about the same 30 k as TP3 -> GND |
| TP4 | inspected | no solder outside the pad |

**6c Step 1, rail up, J3 connected (scrollback pasted by the owner) - PASS.** TDS 1002 CH1 TP4 /
CH2 TP3 (DC), EXT TRIG from R33; Saleae **D1 = GPIO25 (R33), D3 = U5 Q (R61)**; TP4 not on the
Saleae yet. `stat` STANDBY -> `on` -> `BENCH_RUNNING`, `latch 1 railsready 1`, 5V_IN 5.201 V; `id`
stamp right; `strobe` `may fire : yes`, `may arm : yes`. `strobe gate 3` -> level 31, then
`live on confirm` REFUSED (gate not 0); `strobe gate 0`; `live on` checklist; `live on confirm`
-> `LIVE: ARMED ... Watchdog ARMED (1 s)`; `wdog off` and `beam on` REFUSED; `strobe gate 10` ->
REFUSED, `Asked for level 102. Highest allowed now: level 31 (3.03 %) -- proven to 0.` Every
response as the bench doc tables it.
- **Finding:** the `live on` checklist still says "ground on TP1" - stale text (§9).
- The port was closed with live mode armed. The firmware does not watch the CDC connection, so
  live mode stayed armed (gate 0, watchdog serviced) until the 300 s idle timeout ended it.
  Step 2 re-arms.

### 2026-10-12 - Board 1: 6c Step 2 STOPPED - `STROBE_CLAMP` at gate 0: ADC0 baseline 1277 codes (reads as 7.6 A)

**What ran (scrollback pasted by the owner):**
1. Reopened at 276 s idle: `strobe` -> `*** LIVE ***`, `idle exit in 24 s`.
2. The next `strobe pulse 20` came after the timeout, so it fired **DRY**. The output has no
   `LIVE` and no ADC0 block. Then `strobe` -> `live : OFF -- last ended: idle timeout`.
   - **Finding:** the background idle exit prints nothing, so the operator is not told until
     they read `strobe`. It fails safe (dry at gate 0), but should be announced (§9).
3. `strobe live on confirm` -> ARMED.
4. `strobe pulse 20` -> `LIVE 1 x 20 us -- completed`; `ADC0 : 208 samples at 500 ksps, 76
   before the edge; baseline 1277.3 codes (7.625 A); threshold 2158 codes. Verdict: CURRENT
   OUTSIDE THE PULSE`; `no current above the ~72 mA detection floor`; **`STROBE_CLAMP`
   latched**, live mode ended, gate 0.
5. `strobe live off`, then `off`.

**The guard did what it is for.** A baseline over 40 codes means "current before the pulse",
so the verdict is STUCK_ON.

**Why this is almost certainly not current:**
- **No step at the pulse.** The same ~1277 codes sit before, during and after it: `no current
  above the detection floor`.
- **Physically impossible.** A steady 7.6 A from VIR (36 V) is ~270 W. The source is a
  5.20 V / 3 A PSU (15.6 W) through the boost, and 5V_IN held 5.201 V.
- **Q9 cannot be conducting.** Its gate is at 0, so Q9 is off. Even with Q10 shorted, 7.6 A
  through Q9 in linear mode would be ~70 W, and Q9 would fail within seconds.
- **TP3** (TDS CH2, photo) stays at 0 V with only edge spikes: ~+0.6 / -0.5 V at the pulse end,
  with BW limit **off** on CH2.

**Measurement path (netlist):** ADC0 = GPIO40 <- **R32 4k7** <- `CurrentSense_ADC` (TP4,
R65 || R66 to GND, Q10 source). Nothing else is on either net. 1277 codes = **1.03 V at
GPIO40**, so GPIO40 does not see TP4's ~0 V.
- Candidates: R32 open, or GPIO40 not connected, leaving the ADC input floating; or a fault in
  the BURST readback path.
- The readback code (`adc_engine_set_mode(BURST)`: mask 0x01, round-robin of one channel;
  `adc_ring_freeze_copy`, window only from the BURST start) reads correctly.

**Procedure gap (mine):** the live interlock's baseline check assumes ADC0 reads ~0 at rest,
and nobody had ever read ADC0 on any board before arming. 6c Step 1 should have read `adc 0`
dry. Added to the bench doc (6c Step 1).

**LA (`Downloads\PulseTest`, Saleae Ch1 = GPIO25, Ch3 = U5 Q):** GPIO25 **20.002 us**; U5 Q
rises within ~6 ns and falls with it.
- My Step 2 text said "about 22 us": **wrong**. The +2 us PIO overhead has been compensated in
  firmware since 6a.2, so a 20 us command gives 20 us, as 6a recorded.
- The capture cannot say whether it is the dry or the live pulse. At gate 0 both are
  electrically the same: Q10 switched, Q9's gate at 0.
- ~~Analog Ch2 was recorded flat at about -0.05 V (2 GB CSV); it is not on TP4.~~ **Wrong
  (mine):** I misread the owner's earlier message. Saleae analog Ch2 **is on TP4** (ground on an
  R66 wire), 50 MS/s:

  | window | mean | std |
  |---|---|---|
  | 1 ms before the edge | -42.6 mV | 4.9 mV |
  | pulse 1-19 us | -40.3 mV | 2.9 mV |
  | 200 us after | -39.4 mV | 3.5 mV |

  - That is a constant Saleae offset with **no step**: 2 mV = ~15 mA at 135 mV/A.
  - Edge spikes: -13 mV at +0.1 us and -80 mV at +20.1 us (the edges).

**Owner, 2026-10-12, answering:**
- **FLIR:** no change on Q9 / HS1.
- **TDS CH1 (TP4, 20 mV/div):** on screen and at 0 V, overlaid on CH2's trace. The flat trace in
  the photo is both channels.
- **PSU current:** not readable today.
- **`fault clear`** had already been typed before the diagnosis.

**So: TP4 measured ~0 V on two instruments while ADC0 read 1.03 V, and Q9 did not warm.**
The `STROBE_CLAMP` was a false reading of a real 0 A. The open question is only why GPIO40 reads
1.03 V.

**Diagnosis (same day, rail down, no firing):** `adc 0 4096` = **9 codes (7.3 mV)**, `adc 1` =
3032 (2.443 V), `adc 7` = 2055 (1.656 V). The **hardware path is good**: GPIO40 sees TP4 through
R32. `strobe wave` (n=208, pre=76) shows:
- **samples 0-85:** a 4-periodic **10, 2055, 3038, 7**. These are the IDLE round-robin channels
  ch5 DETECT, ch7 MIC, ch1 5V_IN/2, ch2 TIA. Their mean, **1277.5**, is the reported baseline.
- **samples 86-207:** real ch0, **9-10 codes** (~55 mA equivalent, the ADC offset), flat through
  the pulse.

**Mechanism - a firmware bug (mine).** The stale samples are the *old IDLE ring contents*, not
conversions made during the window:
- The IDLE ring starts at index 0 with ch1, 4 channels, so index `i` holds slot `i % 4`.
- The copy ran back from the DMA write index **122** through index 0 and wrapped to 16298-16383.
  Index 16298 is slot 2 (ch5 = 10), 16299 ch7 (2055), 16300 ch1 (3038), 16301 ch2 (7), and the
  last stale sample, at 16383, is ch7 (2056).
- The phase matches exactly. So the BURST DMA had written only **122 samples (244 us)**, while
  the clock said ~440 us since `adc_engine_set_mode(BURST)` (PRE 200 + firing ~39 + POST 200).
- `adc_ring_freeze_copy()` sizes the copy from **elapsed time** (`(now - s_ring_t0_us) x 500
  ksps`), not from what the DMA actually wrote. When the ring starts late, it copies stale data.
  The 122 real samples begin about at the enable. So the ADC0 ring produced nothing for roughly
  the first ~200 us after the mode switch: its start is delayed by about PRE. **Root cause of the
  delay: not yet known.** Nothing in `engine_fire()` / `pitrac_service()` touches the ADC.

**What this does and does not threaten:**
- The guard failed **safe**: stale data read as current and ended live mode.
- It could equally fail **unsafe**. A stale baseline below 40 codes, e.g. if the IDLE channels
  were all low, would pass, and the 160 us pre-window would not be ADC0 at all.
- So **no live firing until it is fixed.**
- ~~Also check the detect path: it uses the same time-based freeze-and-copy after an IDLE->ARMED
  switch.~~ **Wrong (mine), found the same day while writing the fix:** only `strobe.c` calls
  `adc_ring_freeze_copy()`. The detect path reads through `adc_ring_view()` /
  `adc_ring_history()`, which do not bound by the mode start at all. That is harmless while
  ARMED, because it runs for seconds before a pass, but it is not safe straight after a switch
  (§9, `ARCHITECTURE.md` A9).

**Secondary, to verify:** `adc_fifo_setup(..., err_in_fifo=true, ...)` is commented as flagging
FIFO **overrun** in bit 15. My reading of the datasheet is that bit 15 is the per-conversion
**error** flag, and overflow is the sticky `FCS.OVER`. If so, `overran=0` has never proved "no
overrun".

**Fix plan (firmware; then host tests, independent review, reflash):**
1. Before firing, poll the ring's real write index until the PRE baseline is recorded (with a
   timeout -> refuse), instead of `busy_wait_us(200)`. Print the measured ring start latency.
2. `adc_ring_freeze_copy()` returns only what the DMA wrote since the mode start, from the
   write index, not the clock.
3. A pre-window shorter than required -> **readback unusable** (ends live, staircase unmoved),
   never a verdict.
4. Overrun from `FCS.OVER`.
5. Text: announce the idle exit; the checklist's TP1 ground.

Then on the bench: `adc 0` at rest, and a live pulse at gate 0 whose `strobe wave` must be ch0
(~10 codes) end to end.

**Procedure note:** `fault clear` was typed before the record was examined. It did no harm
here, because the record survives in RAM. The rule stays: keep a strobe fault latched until
its record is understood.

*(The next two paragraphs were stranded at the end of section 6 by an edit and were moved back here on 2026-10-12. Both have since been answered: the late start was the ADC running at half rate (the round-7 bench entry), and `adc 0` read 9 codes with the rail down.)*

**Still unknown: why the BURST ring started late.** Nothing in the PRE window touches the ADC or
the ring DMA. `adc ringtest` measures the start latency and per-firing `ring:` lines record it.
If the ring never starts until something else happens, every live firing will be refused
(`NOT FIRED`). That is safe, but it blocks 6c until understood.

**Next (no firing; the fault stays latched):** rail down, `strobe wave`, `adc 0 4096`, and
`adc 1` / `adc 7` as references. Then, if ADC0 reads high with the rail down, an unpowered DMM
check of R32.

### 2026-10-12 - Fix written for the stale-baseline readback; built, host-tested; awaiting independent review

**Owner approved the fix plan;** review by a separate model (ChatGPT 6.1 Sol). **Not flashed.**

**Image** *(superseded the same day by `Oct 12 2026 11:09:36`, after the review — next entry)*: Release **`Oct 12 2026 10:34:54`**, **187968 text / 0 data / 122752 bss**,
`pitrac.uf2` SHA256 `FF5E46F5D5CFE60D0763023BE3F0D012079FBC240EB3952B45EE94E3FDCBF308`. Release
confirmed by `picotool info -a` and `CMakeCache.txt`. No compiler warnings. `id` prints a new
line, `+ ADC0 ring wait + DMA-counted readback (2026-10-12 fix)`.

**Changes:**

| # | Where | What |
|---|---|---|
| 1 | `strobe.c` `run_live()` | After `adc_engine_set_mode(BURST)`, poll `adc_ring_written()` until **100 samples** (`STROBE_LIVE_RING_READY_SAMPLES`) are written, **instead of `busy_wait_us(200)`**. Give up after **5000 us** (`STROBE_LIVE_RING_TIMEOUT_US`): `STROBE_RUN_NO_ADC` "NOT FIRED", back to IDLE, live mode ends (`READBACK`). The budget is now booked after the wait, still before the firing. The first-sample and ready times are recorded per firing. |
| 2 | `adc_engine.c` `adc_ring_freeze_copy()` + new `adc_ring_math.h` | The copy is limited to `min(clock count, DMA write index)` in whole round-robin groups (`adc_ring_copy_avail()`), and returns the write index at the stop. New `adc_ring_written()`. |
| 3 | `strobe_live.c` | New `strobe_live_baseline_by_index()`, the split counted by the DMA write index read **immediately before the enable** (`engine_fire()`) and at the stop. `strobe_live_baseline_checked()` uses the **smaller** of the clock split and the DMA split. The minimum baseline to judge rises from 8 to **50 samples** (`STROBE_LIVE_PRE_MIN_SAMPLES`); below it, NO DATA. |
| 4 | `adc_engine.c` | Overrun is taken from **`FCS.OVER`**, cleared at every mode start. Bit 15 of a FIFO word is the conversion-**error** flag (SDK `adc.h`: "conversion error bit appears in the FIFO alongside the result"; `FCS.OVER` "1 if the FIFO has been overflowed. Write 1 to clear"). The freeze treats either as an unusable record. |
| 5 | `adc_engine.c`, `cli.c` | **`adc ringtest [n]`**: switch IDLE -> BURST, poll the write index for 1 ms (checkpoints 0-800 us), print the first-sample and 100-sample times and the first 8 samples, back to IDLE. Fires nothing; refused unless the engine is in IDLE, and while live (`adc` is not on the allowlist). |
| 6 | `strobe.c`, `cli.c` | A **background live exit** (idle timeout, hold condition) is counted, and the CLI prints `*** LIVE MODE ENDED: ... ***` before the next command. |
| 7 | `cli.c` | The live checklist says grounds on the R66 wires (not TP1/TP5) and `adc 0` < 10 codes. The result print gains a `ring:` line (start times, both splits). A `NOT FIRED` result does not print the previous firing's measurement. |
| 8 | `board.h` | Constants above. The staircase comment no longer claims more than it holds (the open-load gap, §9). |

**Tests: 83/83** (78 + 5 new: `ring_copy_avail`, `baseline_by_index`, `baseline_checked`,
`short_baseline`, `replay_2026_10_12`). `replay_2026_10_12` feeds the day's actual record (86 x
{10, 2055, 3038, 7} then 122 x 10):
- the old split gives STUCK_ON at 1277.5 codes;
- the new split gives NO DATA, both for the 208-sample copy and for the 122-sample copy the fixed
  freeze would make;
- a ring that was waited for gives a judged, clean record.

**Mutations (one per fix, each must fail a test):** all five caught.

| Mutation | Caught by |
|---|---|
| copy ignores the DMA count | `ring_copy_avail`, `replay_2026_10_12` |
| split by clock only | `baseline_checked`, `replay_2026_10_12` |
| minimum baseline back to 8 | `short_baseline` |
| drop the `got > w_stop` guard | `baseline_by_index` (needed a new case, `got = w_stop + 1`, which wraps the unsigned start index) |
| drop the 2-sample margin | `baseline_by_index`, `baseline_checked` |

**Not host-testable (hardware):**
- the ring wait loop;
- `FCS.OVER` clear and read;
- `adc ringtest`;
- the CLI background-exit notice.

These are for the review and the bench: `adc ringtest 5`, then 6c Step 2 again, whose `ring:` line
and `strobe wave` must show ADC0 throughout.

### 2026-10-12 - Independent review (ChatGPT 6.1 Sol): 4 findings, all fixed; image `Oct 12 2026 11:09:36`

The reviewer worked from `captures/review_2026-10-12/REVIEW_BRIEF.md` and the patch, and re-ran
83/83 and the link check. **"I would not clear this image for live bench use yet."** Findings,
and what was done:

1. 🔴 **Safety — a record with only pre-firing samples could pass.**
   - **Scenario:** the ring passes its readiness wait, then acquisition stops at the enable.
     The copy still returns 205 *older* ADC0 samples. The clock split calls 76 of them
     baseline and judges the other 129, also pre-enable, as "within limits, no current". The
     staircase would advance on a firing nobody observed.
   - The reviewer reproduced it with the real functions (`w_en = w_stop = 500`, 250 us,
     10 codes). It was an injected fault: not seen on board 1.
   - **Accepted — my omission:** nothing checked that ADC0 *kept* sampling after the wait.
   - **Fix:** `strobe_live_coverage_ok()`. From the enable to the stop, the DMA must have
     written `(t_stop - t_en) / 2 us` samples, ±4 (`STROBE_LIVE_COVERAGE_TOL_SAMPLES`). Fewer
     means a stall; more means the snapshots disagree. A wrap fails it too.
     `strobe_live_baseline_checked()` returns 0 (NO DATA) unless it passes.
   - To keep honest jitter inside ±4, `engine_fire()` now reads the write index, the clock
     and the PIO enable in **one interrupts-off snapshot**.
   - Each firing prints `enable->stop N samples written, clock allows M: covered`.
2. **Correctness — `adc ringtest` ran with the detector armed.** `detect arm` leaves the
   engine in IDLE, so the mode check passed, and BURST would destroy the armed detector's
   history. **Fixed:** refuses on `detect_armed()` before touching the ADC.
3. **Minor — exit notices duplicated or missed.**
   - **Duplicated:** a hold condition failing inside a blocking command (gate settling) was
     printed by the command and again before the next one.
   - **Missed:** a power-FSM rail-down while idle ends live mode via `strobe_safe_off()`,
     which was not counted.
   - **Fixed:** every live exit is counted in `live_drop()`. Every notice, and the own
     wording of `strobe live off` / `strobe off`, acknowledges it. `dispatch()` checks
     **before and after** every command.
4. **Minor — `NOT FIRED` printed "0.18 mC booked" then "nothing booked".**
   - Wider than reported: `REFUSED` live runs also said "booked".
   - **Fixed:** "booked" only when the firing was admitted and the ring was ready; otherwise
     "NOT booked (not fired)".

**Reviewer's other answers (no defect):**
- The copy bounds admit no stale IDLE data, and a wrap fails conservatively.
- No pulse sample can enter the baseline (`w_en` before the enable is conservative).
- `NO_ADC` cleanup is complete.
- The `FCS` write-back preserves the RW fields; reading OVER separately from bit 15 is right,
  per the RP2350 datasheet.
- The 5 ms poll is far below the 1 s watchdog. The SDK's abort/drain waits are unbounded,
  which predates this change.
- The root cause of the late start: unresolved; no code mechanism found.

**Image** *(superseded the same day by `Oct 12 2026 11:49:23` after review round 2 — next entry; its ±4-sample coverage tolerance was unsafe)*: Release **`Oct 12 2026 11:09:36`**, **188416 text / 0 data / 122764 bss**,
`pitrac.uf2` SHA256 `D08111FA205114F23B1B1A272B2B2C787F6662A02B1052106A1CDB877A6A808F`.
- Release confirmed by `picotool info -a`; no compiler warnings.
- `id` line: `+ ADC0 ring wait + DMA-counted, coverage-checked readback (2026-10-12)`.
- **85/85 host tests:** + `coverage` and `acquisition_stopped`, the latter being the reviewer's
  reproduction. One existing `baseline_checked` case (200 written in a 250 us window) was
  inconsistent under the new check and now expects 0. A case at the +4 tolerance edge was
  added.
- **Mutations, 8 of 8 caught:** the 5 earlier ones, plus coverage skipped (`baseline_checked`,
  `acquisition_stopped`), its upper bound dropped (`baseline_checked`, `coverage`), and its
  lower bound dropped (`coverage`, `acquisition_stopped`).
- **Not host-testable:** the interrupts-off snapshot, the `ringtest` refusal, the notice
  bookkeeping and the booking text. They are for the bench: `detect arm` then `adc ringtest`
  must be refused; an idle timeout must print one notice, once.

**Not flashed.** Whether to send the fixes back to the reviewer for a confirm pass is the
owner's call: `captures/review_2026-10-12/REVIEW_RESPONSE.md` and `fix_2026-10-12_r2.patch`.

### 2026-10-12 - Review round 2: the ±4-sample coverage tolerance could hide a gap; replaced by an exact SysTick check; image `Oct 12 2026 11:49:23`

**Reviewer (ChatGPT 6.1 Sol) on the round-1 fixes:**
- Three fixes are closed: the detector guard, the exit notices, the booking text.
- 🔴 **Safety: the coverage fix was not tight enough.** ±4 samples (8 us) tolerates a gap.
  The reviewer's fault injections, run against the real helpers:

  | Injected failure | Round-1 result |
  |---|---|
  | ADC stopped at the enable | NO DATA (correct) |
  | 6 us pause spanning a 5 us pulse | **covered, no current, within limits** |
  | 6 us pause inside 206 us of current | **covered, width 200 us, within limits**: the clamp violation was missed |

  These are injected faults, not events on board 1.
- The reviewer also noted that clk_adc and the timer share XOSC, so drift could not have
  justified the allowance.

**Accepted — my error.** The ±4 was not derived from anything. It was a guess at the
snapshot jitter, and a guess is no basis for a safety tolerance.

**Fix: exact coverage on the processor clock.**
- `adc_ring_snapshot()` (`adc_engine.c`), called with interrupts off, spins until the DMA write
  index ticks (a conversion has just landed). Then it reads **SysTick** (24-bit, free-running
  at clk_sys, set up in `adc_engine_init()`; unused elsewhere), the us timer, the index, and a
  ring generation (bumped by every `ring_start()`).
- `engine_fire()` takes one snapshot and the PIO enable in the same section. If the ring is
  not ticking, it enables **nothing** (`STROBE_RUN_NO_ADC`; the charge was booked first, and
  the output says so).
- `run_live()` takes the second snapshot after the post window.
- **ADC samples are exactly 96 clk_adc = 2 us = 300 clk_sys cycles at 150 MHz**, from one
  crystal. So `strobe_live_coverage_ok(cycles, samples, cps)` requires
  `|cycles - samples x 300| <= 300 / 5 = 60 cycles` (0.4 us). That bounds only the snapshots'
  latency spread (the poll loop and DMA, tens of cycles).
- **One missing sample is 300 cycles out**, so no gap of even one sample can pass.
  `_Static_assert`s in the tests check that the tolerance cannot hide a sample, and that the
  longest live window (15360 samples, ~4.6 M cycles) fits SysTick's 24 bits.
- `covered` also needs both snapshots and the same generation.
  `strobe_live_baseline_checked()` now takes `covered` and returns 0 without it.
- **Bench check before any live firing:** `adc ringtest` now takes the same two snapshots
  250 us apart and prints `N samples in C cycles, +r vs 300/sample (+-60): exact`.
- Each live firing prints the same line for its own window.

**Tests: 87/87.** New: `short_gap_pulse` and `short_gap_stuck`, the reviewer's two cases.
`coverage` is rewritten: exact; ±60 passes and ±61 fails; one sample missing or extra fails;
the round-1 4-sample hole fails; zero samples (even at zero cycles) fails; the ~30.4 ms window
is exact; SysTick elapsed across its wrap. `acquisition_stopped` and `baseline_checked` were
updated for the new signature.

**Mutations, 12 of 12 caught:**

| Mutation | Caught by |
|---|---|
| ignore `covered` | 3 tests |
| one-sided either way | `coverage`, plus both short-gap tests |
| tolerance widened to round 1's 4 samples | `coverage`, both short-gap tests |
| tolerance widened to 1 sample | `coverage` |
| zero samples accepted | `coverage`; it survived until the zero-cycles case was added |
| SysTick counted up instead of down | `coverage` |
| the earlier five | as before |

**Image** *(superseded the same day by `Oct 12 2026 12:23:23` after review round 3 — §6 below; its snapshot ran from flash and assumed its latency)*: Release **`Oct 12 2026 11:49:23`**, **190112 text / 0 data / 122772 bss**,
`pitrac.uf2` SHA256 `705232D481124D1F745C15D4CB9F3573D5476796AC581BB775ACE3CBC0910321`.
Release confirmed by `picotool info -a`; no compiler warnings. `id` line: `+ ADC0 ring wait +
DMA-counted readback, exact SysTick coverage (2026-10-12)`.

**Not host-testable:**
- that SysTick really runs at clk_sys;
- the latency spread being tens of cycles.

**`adc ringtest` measures both before any live firing:** the pair must read `exact`, with the
residual well inside ±60. If it does not, every live readback would be NO DATA. That is
safe, and it is the first bench step.

### 2026-10-12 - Review round 3: the ±60-cycle snapshot bound is assumed, not enforced (fixed — see the end of this entry)

**Reviewer (ChatGPT 6.1 Sol), safety.** `adc_ring_snapshot()` runs from **flash (XIP)**:
- The poll loop's DMA-index read is at `0x100004ba`; the SysTick read is at `0x100004cc`, on
  another 8-byte cache line (`0x100004c8`).
- A cold line fill is ~192 cycles (boot2_generic_03h, flash divider 2), and disabling
  interrupts does not stop fetch stalls.
- If the first snapshot's latency is ~300 cycles more than the second's, one missing sample is
  cancelled exactly: `126 x 300 + 10 - 310 = 125 x 300`. This was shown arithmetically, with the
  real helpers; the latencies were not measured on board 1.
- The opposite asymmetry would falsely reject healthy records.

I confirmed the addresses in our ELF. Whether that exit-line miss actually happens (the core's
sequential prefetch may fill the line on the first loop pass) is **unproven either way**, which
is the reviewer's point: the bound was never demonstrated.

**Accepted.** The "tens of cycles" was again an assumption standing in for a bound.

**Proposed fix (the owner approved "proceed"; written below):** make each snapshot **measure
its own uncertainty** instead of assuming it.
- Run the poll loop from SRAM (`__not_in_flash_func`, with direct register reads and no flash
  calls inside).
- Read SysTick on **every** pass, so the landing is bracketed between two SysTick readings.
- Reject any snapshot whose bracket is wider than a fixed bound, and require the two brackets
  together, plus a stated DMA-latency allowance, to stay within the 60-cycle budget.
- A fetch stall, an interrupt or contention then shows up as a wide bracket and is rejected, not
  absorbed.
- `adc ringtest` reports the brackets and residuals over many runs, including runs after an XIP
  cache flush.

**Written, built, tested (image `Oct 12 2026 12:23:23`):**
- **`adc_ring_snapshot()` is `__not_in_flash_func`.** Verified in the ELF: `T adc_ring_snapshot`
  at `0x20000110`, called through a veneer.
  - The loop at `0x20000142`-`0x20000150` is the SysTick read, the DMA index read, compare and
    branch: no call, no flash access.
  - The closing SysTick read is the next instruction after the exit branch (`0x20000152`).
  - `time_us_64()` is called only after the bracket closes.
- **The bracket:** `tick_lo` is the SysTick read before the last index read that saw no change;
  the closing read follows the index read that saw the change. So the landing lies inside
  `[tick_lo, tick_lo - bracket]` by construction.
- **`strobe_live_coverage_ok(span_lo, b1, b2, samples, cps)`:**
  - each bracket must be ≤ **40 cycles** (`STROBE_LIVE_SNAP_BRACKET_MAX`);
  - the window `b1 + b2 + 2 x 20` (`STROBE_LIVE_DMA_SPREAD_CYCLES`, the one *stated*, unmeasured
    bound) must be narrower than a sample period;
  - the landings' measured separation, widened by that window, must contain `samples x 300`.
  - The largest possible window is ±60, no wider than round 2's. One missing or extra sample is
    300 cycles out and cannot fit, by `_Static_assert` and by an exhaustive test.
- **`adc ringtest [1-200]`:** each run takes a warm-cache pair and a cold-cache pair
  (`xip_cache_invalidate_all()` before the first snapshot). Runs past 5 print a summary: covered
  count, widest bracket, residual range, PASS/FAIL. Each live firing prints its own
  `enable->post` line.
- **Tests: 88/88.**
  - New: `snapshot_latency_cancel`. The reviewer's 126-period / 125-sample case with a 310-cycle
    first latency: round 2's arithmetic cancels exactly; bracketed, the 320-cycle bracket is
    refused, and with narrow brackets the deficit shows as 300 cycles.
  - `coverage` is now exhaustive over landing position in each bracket (0..b), bracket widths
    {0, 1, 20, 40} and DMA spread {-20, 0, +20}: honest always covered; one missing or extra
    never.
  - Also covered: the window edges, asymmetric brackets, an over-wide bracket, the
    window-vs-period guard, and the round 1–2 cases.
- **Mutations: 15 of 15 caught**, including:
  - the bracket limit dropped;
  - the window guard dropped;
  - the centre made blind to bracket asymmetry;
  - the DMA spread doubled;
  - the brackets left out of the window.
- **The mutation script had a flaw:** it counted a failed build as "caught". One run hit a
  locked test `.exe` (LNK1104) and was falsely counted. Fixed (retry, then INCONCLUSIVE), and
  re-run: that mutation is genuinely caught by 4 tests.
- **Image:** Release **`Oct 12 2026 12:23:23`**, **191568 text / 0 data / 122780 bss**,
  `pitrac.uf2` SHA256 `7CCC6FC79AC960E072EE37C978F98CBFA84AC50A339FC3278EA93134EEF46E9D`, no
  warnings. `id` line: `+ ADC0 ring wait + DMA-counted readback, bracketed SRAM coverage
  (2026-10-12)`.
- **What remains assumed:** the DMA's conversion-to-visible-write latency spread, ≤ 20 cycles
  between the two snapshots. Everything else in the window is measured per snapshot. The bench
  (`adc ringtest 100`, warm and cold) and each live firing's `enable->post` line show the real
  brackets and residuals.

### 2026-10-12 - Review round 4: what the snapshot timestamps is a transfer *issue*, not a completed write; two new findings (analysis, no code change)

**Reviewer (ChatGPT 6.1 Sol), correctness:** the ring uses address wrapping, and RP2350 disables
the in-flight adjustment of `WRITE_ADDR` in that mode. So the index advancing does not mean an
SRAM write completed, and my "conversion landing" / "never an over-count" claims are wrong.
- The reviewer has not reproduced an unsafe verdict.
- Recommendation: establish the event and derive the bound before any further code.
- The SRAM placement and bracket arithmetic check out; cold-cache invalidation is appropriate.
- `ringtest 100` shows observed behaviour, not a worst case.

**Confirmed from the datasheet (my error):**
- §12.6.4: a transfer is "issued to the address FIFOs … in flight, but has not yet necessarily
  completed".
- §12.6 changes: the WRITE_ADDR in-flight adjustment is "disabled for address-wrapping …
  transfers".

**Analysis** (`captures/review_2026-10-12/REVIEW_RESPONSE_4.md`, sent for review before any code):
- **The event is a transfer issued for a conversion already in the ADC FIFO.** Each issue
  consumes one DREQ credit, so issued implies converted. That keeps the coverage check, the
  `by_index` split and the ring wait valid.
- **The derived spread of that event between the two snapshots is ≤ ~9 cycles:** ≤ 1 for the
  clock-domain crossing, plus ≤ 8 of arbitration against the strobe's PIO-feed initial fill at
  the first snapshot. D = 20 stands, now derived rather than assumed. Proposed: take the first
  snapshot after that fill, which removes the arbitration term.
- 🔴 **Real gap: the freeze copies before any completion barrier.** The newest transfer can
  still be in flight. Proposed: abort the ring correctly *before* the copy (in-flight transfers
  "cannot be revoked; BUSY stays high until they complete").
- 🔴 **New (mine): our DMA abort does not follow RP2350-E5.**
  - The SDK's `dma_channel_abort()` polls BUSY, does not clear EN, and does not poll ABORT.
  - `ring_start()` re-triggers at once. A trigger landing while ABORT is still in effect runs
    with a zero transfer count, so the ring writes nothing.
  - This is a **candidate** for the 2026-10-12 late start (nothing written for ~200 µs, then
    writing from index 0). What re-triggered it is unexplained.
  - Proposed: `dma_abort_safely()` per the datasheet sequence (clear EN, CHAN_ABORT, poll ABORT,
    poll BUSY) for all three channels.
- 🔴 **New (mine): a dead IDLE ring would silently blind the 5V_IN supply monitor.**
  `adc_read_5vin_volts()` never checks that the ring advances. Not observed (bench readings
  vary across switches). Proposed: a ring-liveness check in the 5V_IN path.

**Status:** reflash hold stays. No code change until the analysis is reviewed. Image
unchanged: `Oct 12 2026 12:23:23`. *(Superseded the same day by round 5 — next entry.)*

### 2026-10-12 - Review round 5: the analysis accepted with qualifications; abort, freeze barrier, PIO-feed wait and 5V_IN freshness implemented; image `Oct 12 2026 13:15:36`

**Reviewer (ChatGPT 6.1 Sol) on `REVIEW_RESPONSE_4.md`:** the copy barrier, the safer DMA stop and
the supply-freshness guard are justified. Proceed with the qualified fixes. **The 9-cycle timing
bound stays an OPEN review item**:
- "500 ksps means <= 1 outstanding transfer" assumes each transfer retires within 300 cycles,
  which is a service-time bound that needs proving;
- shared DMA queues and ADC-read bus latency can affect later issue timing through
  back-pressure;
- the late-start explanation (E5) is unproven: the compiled mode switch does ADC shutdown,
  drain and configuration between abort and restart, and the channels self-chain.

Qualifications, all adopted:
- the SDK documentation itself requires clearing EN before an abort;
- reject a drain failure or a DMA READ/WRITE_ERROR, and preserve those flags;
- the 5V guard must keep the true last-good age, handle mode changes, and make the FSM reject
  unavailable readings (neither the old value nor NaN works);
- wait for min(words, 8) feed words, with a timeout;
- CHAIN_TO = the channel itself (0 chains to channel 0).

**Implemented:**
1. **`dma_stop_safely()`** (new `dma_safe.c/.h`), per datasheet 12.6.8 and RP2350-E5:
   - EN off and CHAIN_TO = self, via the non-triggering CTRL alias, with the write-1-to-clear
     error flags masked to 0 so they survive;
   - CHAN_ABORT, then poll **CHAN_ABORT** (the register says "must be polled until it returns
     all-zero. Until this point, it is unsafe to restart the channel") and BUSY, bounded at
     100 us;
   - report READ_ERROR / WRITE_ERROR.

   Used for the ring (`ring_stop`), the strobe feed (`engine_reset`) and the triggered
   capture. A stop that does not complete marks that channel **never to be restarted**: the
   ring stays down and 5V_IN reads STALLED; the feed returns `STROBE_RUN_NO_FEED`.
2. **Freeze completion barrier** (`adc_ring_freeze_copy`):
   - stop the ADC, drain the FIFO (a failure is recorded);
   - `dma_stop_safely(ring)`; only then read the final index and copy;
   - `overran` is now: a drain failure, an incomplete stop, a READ/WRITE_ERROR, FCS.OVER, or a
     conversion-error bit.
3. **PIO-feed wait** (`engine_fire`): before the first snapshot, wait until the TX FIFO holds
   min(words, 8) (it is joined, per `strobe_burst.pio`), bounded at 50 us. Otherwise
   `STROBE_RUN_NO_FEED`: nothing is enabled. This removes feed arbitration at the first
   snapshot.
4. **Fresh-only 5V_IN:**
   - `adc_5vin_read()` returns FRESH only if all 128 ch1 samples were issued **during the
     call** (~1 ms wait, bounded at 3 ms), skipping the newest group because it may be in
     flight. Otherwise NOT_SAMPLED (ARMED, BURST, or a bench capture owning the ADC) or
     STALLED (the ring is down or not advancing).
   - Only FRESH resets the age.
   - The power FSM: latch and rail check require FRESH, else the new fault
     **`SUPPLY_UNREADABLE`**. The sustained monitor counts STALLED as low, so it trips after
     500 ms as `SUPPLY_UNREADABLE`; NOT_SAMPLED neither starts nor clears the debounce and is
     never judged.
   - `stat` prints the status.
   - **My regression, caught before the build:** `capture trig` (up to 30 s, with the FSM
     serviced through its poll callback) leaves the mode IDLE with the ring down. The first
     version would have read STALLED and dropped the rail. Bench captures now set a flag that
     reads NOT_SAMPLED.
   - Also: `adc_capture()`'s early return now runs *before* `ring_stop()`. Previously it could
     leave the ring down.
5. **Comments:** the event is a transfer **issued**, not a completed write, everywhere
   (adc_engine, strobe_live, board.h, adc_ring_math). DMA_SPREAD is marked derived and open.

**Tests: 93/93.** Five new power-FSM tests:
- STALLED and NOT_SAMPLED at the latch are refused (`SUPPLY_UNREADABLE`, no latch rise);
- STALLED at the rail check gives FORCE_OFF;
- STALLED while running trips after the debounce as `SUPPLY_UNREADABLE`;
- NOT_SAMPLED with a stale *low* value never trips, and the debounce restarts when sampling
  resumes.

**Mutations: 20 of 20 caught.** The script now runs the full suite. The five new ones: the
monitor judging NOT_SAMPLED; the monitor ignoring STALLED; the latch or the rail check accepting
non-fresh; unreadable reported as SUPPLY_LOST.

**Not host-testable (hardware):**
- `dma_stop_safely`'s register sequence;
- the freeze barrier;
- the feed wait;
- the bench-capture flag;
- `adc_5vin_read`'s ring wait.

**Image** *(superseded the same day by `Oct 12 2026 13:55:58` after review round 6 — next entry)*: Release **`Oct 12 2026 13:15:36`**, **193816 text / 0 data / 122784 bss**,
`pitrac.uf2` SHA256 `C8522736DBAB985C253C83C2A13AC7EC4EEBA535F60B0294739F3D0E93EB008E`.
- No warnings. `adc_ring_snapshot` is still in SRAM (`0x20000110`).
- `id` adds `+ E5-safe DMA stops, freeze completion barrier, fresh-only 5V_IN`.

**Open (review):**
- the conversion-to-issue spread bound D (service time and back-pressure);
- the late-start root cause;
- **5V_IN is not monitored while ARMED or during a bench capture.** NOT_SAMPLED is never judged,
  so a supply loss during a long detector arm goes unseen. This predates the change (the old
  code judged a stale cached value instead). Logged in section 9.

### 2026-10-12 - Review round 6: completeness proven from hardware flags; 5V_IN integrity; capture stop validation; image `Oct 12 2026 13:55:58`

**Reviewer (ChatGPT 6.1 Sol), two defects and a proof gap:**
1. 🔴 **Safety: `adc_5vin_read()` could certify invalid data as FRESH.**
   - It checked that the ring advanced, but ignored FIFO overflow and masked away
     conversion-error flags.
   - An overflow LOSES conversions (datasheet 12.4.3.4), so the round-robin can rotate and
     "ch1's slots" hold another channel.
   - Reproduced on a host copy of the body: a FRESH 6.853 V from a 4.600 V supply. Error-flagged
     samples also passed.
2. **Correctness: `adc_capture_triggered()` ignored its stop result.** After a stop timeout it
   could still return OK, process the buffer, and later restart the failed channel. Neither
   capture entry point guarded against restarting it, so "a channel whose stop does not
   complete is never restarted" was not true for the capture channel.
3. **The proposed timing checks (single step, empty FIFO, steady pace) do not close D.**
   - A regular interval proves frequency, not the conversion-to-issue delay.
   - The reviewer's counterexample passes the production coverage helper with one conversion
     missing: delays of 230 and 10 cycles, 40-cycle brackets, exact 300-cycle local intervals.
   - **Accepted.** The reviewer asked to bound the delay independently "or base coverage on an
     observed conversion event".
   - The supply gap need not block no-Pi 6c, but must be closed before 7c.

**Resolution of 3 — completeness from hardware flags, not timing.** The datasheet says:
- while CS.START_MANY is set, the ADC "automatically starts new conversions at regular
  intervals" (12.4.3.2);
- "If the FIFO is full when a conversion completes, the sticky error flag FCS.OVER is set …
  any conversions that complete during this time are lost" (12.4.3.4);
- a DMA read of an empty FIFO sets FCS.UNDER; a failed bus transfer sets READ/WRITE_ERROR.

So no conversion can be missing from the ring if, at the freeze:
- START_MANY and EN were still set at the stop instant;
- READY returned;
- the DMA drained the FIFO;
- the DMA stop completed, with no READ/WRITE_ERROR;
- there was no OVER and no UNDER;
- the ring was not restarted since the first snapshot (same generation).

`adc_ring_freeze_copy()` now checks every one of those, records which failed in
`adc_ring_freeze_flags()`, and any failure makes the readback unusable. Each firing prints a
`record:` line naming the failure. **The SysTick coverage check is kept as a cross-check that
can only reject.** Its bound D no longer carries the safety argument, so the round-4/5 open
item is closed by design rather than by argument. This is the "observed event": the ADC's own
loss flag.

**Fixes for 1:**
- New pure helper `adc_5vin_from_ring()` (`adc_ring_math.h`), now what `adc_5vin_read()` runs.
  It returns INVALID on FCS.OVER/UNDER since the ring started, on a ring READ/WRITE_ERROR, or on
  any averaged sample with the conversion-error bit.
- After an overflow or DMA error the reader **restarts the ring**, re-establishing the channel
  order and clearing the sticky flags, so a later reading can be trusted.
- The blocking mode-OFF read clears and checks CS.ERR_STICKY.
- The new status `ADC_5VIN_INVALID` is treated by the power FSM as unreadable:
  `SUPPLY_UNREADABLE` at the latch, the rail check, and in the monitor after 500 ms.
- `stat` names it.

**Fixes for 2:**
- The triggered capture samples FCS **before** stopping its DMA (after the stop, the ADC fills
  a FIFO nobody drains). It validates the stop: an incomplete stop marks the channel unsafe
  and returns ADC_TRIG_ERROR without touching the buffer; a bus error returns ADC_TRIG_ERROR.
- Both capture entry points refuse an unsafe channel and clear stale DMA error flags before
  use (`dma_clear_errors()`, new).
- `adc_capture()` waits by polling BUSY and FCS together with interrupts off, so a loss during
  the capture is distinguished from the FIFO filling after it.
- Both captures now treat OVER/UNDER/DMA errors as "overran". This closes the section 9 item
  where bit 15 was read as overrun.
- `ring_start()` also clears stale ring DMA errors.

**Tests: 101/101.**
- New executable `adc_ring_test`, 6 cases: fresh across the wrap; FIFO/DMA flags void it; a
  conversion-error bit inside the window voids it, while one outside or on another channel does
  not; the in-flight newest group is skipped; **the reviewer's rotated-phase case** (two lost
  conversions, a 4.6 V supply: unflagged it returns a confident wrong value, flagged it is
  INVALID); bad arguments.
- Two new power-FSM tests: INVALID at the latch is refused; INVALID while running trips as
  `SUPPLY_UNREADABLE`.

**Mutations: 23 of 23 caught**, including:
- the helper ignoring FIFO/DMA flags (`v5in_rejects_hw_flags`, `v5in_phase_rotation`);
- the helper ignoring conversion-error bits;
- the helper reading the in-flight group.

M21 first came back INCONCLUSIVE: removing the flag left an unused parameter, which `/W4 /WX`
refuses. That is the round-3 script fix doing its job. The mutation was corrected and re-run.

**Not host-testable (hardware):** the freeze's flag reads, the captures' stop handling, and the
ring restart after an overflow. They are for review and the bench.

**Image** *(superseded the same day by `Oct 12 2026 14:43:50` — the bench found the ADC at half rate, next entry)*: Release **`Oct 12 2026 13:55:58`**, **194712 text / 0 data / 122796 bss**,
`pitrac.uf2` SHA256 `239BB7230E6FDD2CD597318BBED2C0A9816B2C33B05F7471FD415AAB26167F7A`.
- No warnings. `adc_ring_snapshot` is still in SRAM.
- `id` adds `+ E5-safe DMA stops, record proven by ADC/DMA flags, fresh + intact 5V_IN`.

**Still open:**
- the late-start root cause (the bench's `adc ringtest` reports OVER and the start time);
- 5V_IN unmonitored while ARMED or during a bench capture, before Phase 7c (section 9).

### 2026-10-12 - Bench, image `13:55:58` (Sol passed round 6): `adc ringtest 5` FAIL — the ADC ran at HALF rate; root cause of the morning's "late start"; fixed in `Oct 12 2026 14:43:50`

**What the owner ran (board 1, dry, nothing fired):**
- `id`: `13:55:58`.
- `stat`: `5V_IN 5.205 V fresh`.
- `adc 0 4096`: 9 codes.
- `adc ringtest 5`: **0 of 10 pairs covered.** The owner correctly stopped there.

**What it showed:**
- **ADC0 arrived every 4 us, not 2 us.** `written at: +100:25 +200:50 +400:100 +800:200`, and
  `100 written +398 us` every run.
- Each 250 us pair: 64-66 samples in ~38,400 cycles, i.e. **600 cycles per sample**, against
  the 300 the check assumes. The residuals of about +19,200 cycles are 64 x 300. The cross-check
  rejected correctly.
- The first sample came at +2 to +9 us: **the ring starts on time.**

**Root cause (datasheet 12.4.3.2):** "By default (DIV = 0), new conversions start immediately
after the previous conversion finishes … 500 kS/s. Set DIV.INT to a positive value n to trigger
the ADC once per n + 1 cycles. **The ADC ignores this if a conversion is currently in progress,
so generally n will be >= 96.**"
- `set_rate()` set DIV = 95 for 500 ksps: a trigger every 96 cycles, landing as each 96-cycle
  conversion ends. Every other trigger was ignored.
- **Every ring mode has run at half its stated rate since the A1 ring was written:** IDLE
  62.5 ksps per channel, ARMED 125 ksps per channel, BURST 250 ksps.
- **That is the morning's "late ring start":** 122 samples where the clock said ~218, a ratio of
  0.56. The RP2350-E5 abort hypothesis (round 4) was **wrong**. The E5-safe abort stays: it is
  the datasheet sequence regardless.
- The round-1 DMA-counted copy and the round-6 flag proof both failed safe, as designed.

**The fix:** `adc_div_for_rate()` (`adc_ring_math.h`, host-tested) gives DIV = 0 at the 500 ksps
maximum, otherwise `clk / rate - 1`, never below 96.
- Test `adc.adc_rate_div`: 500 k gives 0; 250 k, 100 k and 1 k are exact; 499 k clamps to 96; a
  sweep shows no nonzero DIV below 96.
- Two new mutations (the old rule, and a 95 floor) are caught.

**Brackets.** Steady-state brackets were 15 cycles. The 60-226-cycle ones were caught on the
snapshot loop's **first pass**, so they spanned the function's entry and any instruction fetch the
flash-resident caller left outstanding (157 right after a cache flush).
- The second snapshot of each pair follows a timer-based `busy_wait_us(250)` on a grid locked to
  the same crystal, so it kept starting at the same phase, just as a sample landed.
- **Fix:** `adc_ring_snapshot()` now lets one change go by, then brackets the next, so the
  measured change is always caught in a steady SRAM pass. Disassembly checked: the loop is
  SysTick read, DMA index read, compare, branch, and the closing SysTick read follows one ALU op
  after exit.
- **`STROBE_LIVE_SNAP_BRACKET_MAX` stays at 40.** No limit was changed.

**Image:** Release **`Oct 12 2026 14:43:50`**, **194344 text / 0 data / 122796 bss**,
`pitrac.uf2` SHA256 `2D3BD0C1262AC402393C0349A9127FC21429528F832C0DD91E1B60904FB6471D`.
- 102/102 host tests; 25 of 25 mutations caught; no warnings.
- `id` adds `+ ADC at true 500 ksps (DIV 0), synchronised snapshots`.

**What the half rate may have affected (section 9):** any timing derived from a ring at its
*stated* rate.
- Detect pass logs and retained waveforms report `adc_ring_channel_rate_hz()`, which was 2x the
  real rate, so their time axes were compressed 2x.
- `capture` and `capture trig` were affected only at ~500 ksps requests (the default for
  `capture trig`). The `captures/` logs used 100 k and 250 k, and the section 3.7 pilot
  (3.102 V / 22.7 ms FWHM) was a 100 ksps triggered capture, **unaffected**.

**Bench re-test on `14:43:50` (board 1, dry), 2026-10-12: PASS.**
- `id` and `stat` correct; `adc 0` 9 codes.
- `adc ringtest 5`: `written at +400:200 +800:400` (**2 us per sample**), `100 written +199 us`,
  first samples 9-11 codes, and all 10 pairs covered with brackets 16/16.
- `adc ringtest 100`: **200 of 200 pairs covered** (warm and cold cache), widest bracket 16
  cycles, residual -7 .. +8 cycles against a +-36 window.
- The rate fix and the synchronised snapshot behave as derived.
- Side finding: `adc5vcal` calibrates the single-channel read, not the ring reading (section 9).

### 2026-10-12 - Board 1 on `14:43:50`: 6c Steps 1–2 PASS; Step 3 started (gate 3: no current; TP3's post-pulse kick reverses sign)

**6c Steps 1-2 on `14:43:50` (board 1), 2026-10-12: PASS.**
- **Step 1:** `on` gives `railsready 1`. Every refusal is as tabled; `live on confirm` armed with
  the watchdog; `strobe gate 10` was refused with `Highest allowed now: level 31`. The checklist
  now names the R66 grounds.
- **Step 2:** `strobe pulse 20` at gate 0 gave `LIVE 1 x 20 us -- completed`, `221 samples, 76
  before the edge; baseline 10.8 codes (0.064 A); threshold 22. Verdict: within limits`, and no
  current above the floor.
  - `ring: first sample +5 us, 100 ready +199 us`.
  - `enable->post: 134 samples, 40197 cycles, brackets 16/16, residual -3 … covered`.
  - Baseline split 76 by the clock, 77 by the DMA count. No `record:` line.
- `strobe wave`: 221 samples, all 10-11 codes.
- The owner reports the TDS and LA traces identical to the pre-fix gate-0 pulse.
- The 10.8-code baseline is the ADC zero offset; plateaus are measured from it.
- **Gate-0 full-bandwidth reference** (TDS: CH1 TP4 20 mV/div with BW off, CH2 TP3 1 V/div,
  250 ns/div, EXT trigger on GPIO25 at 1.19 V):
  - TP4 is flat apart from a ~30-40 ns burst of about +-25 mV, ~50 ns after GPIO25 rises. That
    is the U5 -> U8 -> Q10 turn-on edge coupling, as in 6b; no current.
  - TP3 shows a tiny blip at the same instant.
  - This is the reference for the first current step's turn-on.
- **Probes:** both are at **1X** (owner, 2026-10-12), so the `Probe 1X` menus were right and
  the amplitudes above stand. **Correction:** I told the owner that 10X with a matching menu was
  better and was what the procedure assumed. For 6c on the TDS that was wrong, and 1X stays:
  - TP4 is the sense node itself (netlist: Q10 source, R65 ∥ R66 = 2 × 0.27 Ω, R32, TP4), so a
    1X probe's 1 MΩ ∥ ~110 pF loads nothing.
  - A 1X probe has no compensation network, so a 20 µs plateau reads true. An uncompensated 10X
    probe crept ~7 % over 20 µs on 2026-10-06, more than the ±5 % ADC0-vs-TP4 band.
  - Cost: if these are the TDS's standard P2220s, ~6 MHz at 1X. Plateaus lose nothing. The
    full-bandwidth turn-on capture shows an overshoot lasting ≳ 100–150 ns but smears anything
    shorter; the gate-0 reference was taken the same way. Repeat on the MSO54B at 10X if the
    turn-on detail ever matters.

**6c Step 3, gate 3 % (board 1, `14:43:50`), 2026-10-12.** TDS: CH1 TP4 50 mV/div, BW limit
**off**; CH2 TP3 1 V/div, BW limit on; 10 µs/div; EXT trigger on GPIO25 at 1.19 V. Scope photo
only; the `strobe pulse 20` output comes with the step table.
- **No current:** TP4 is flat within ~±3 mV (< 0.03 A) through the pulse. That is as expected:
  TP3 is far below Q9's 1.0–2.0 V threshold.
- **TP3 ≈ 0.3 V** (3 × 3 % × 3.29 V = 0.30 V), flat through the pulse.
- **At the end of the pulse (≈ +20 µs), TP3 kicks up by ~0.1–0.2 V and recovers in ≈ 5 µs.**
  TP4 shows a tick of a few mV at the same instant. The 6b gate-0 baseline was a **−0.3 to
  −0.4 V** step at the same moment with the same ≈ 5 µs recovery. The owner flagged the reversed
  sign before climbing.
- **Likely reason (a schematic reading, not proven):** the kick is Q10's turn-off coupling
  through the shared Q9-source / Q10-drain node and Q9's ~1.7 nF Cgs into TP3, as in 6b. U7 is an
  unbiased complementary follower. Its NPN can only push TP3 up and its PNP only pull it down,
  with a ±V_BE dead band between them that only the slow LM358 (0.3–0.5 V/µs) crosses.
  - At gate 0 nothing conducts, so the kick shows unopposed.
  - At gate 3 the NPN already carries ~10 µA into the ~30 kΩ from TP3 to GND (DMM), so it
    clamps any downward kick within nanoseconds. It cannot pull TP3 down, though: the upward part
    of the coupled edge survives until the LM358 brings the PNP in, ≈ 5 µs later.
  - So the 6b prediction "at a setpoint U7 holds TP3 much more stiffly" is only half right:
    stiff downward, not upward.
- **Harmless as it stands:** it happens after the pulse, with Q10 off. Q10 is in series with the
  whole current path, so no current can flow, and TP4 confirms it. And it is gone in ≈ 5 µs,
  against ≥ 100 ms between staircase pulses and the 150 µs minimum burst gap.
- **What would matter:** an upward TP3 kick at the **start** of a pulse once Q9 conducts. It
  would last ≈ 5 µs and show as a hump over the first ~5 µs of the TP4 plateau. None is visible
  at gate 3. Step 3 now says to watch for it at every step.
- CH1's BW limit was off in this capture; plateau readings need it on (20 MHz).

**6c Step 3, gates 3-33 % (board 1, `14:43:50`), 2026-10-13: NO CURRENT at any step; stopped.**
- Twelve firings, one per 3 % step: levels 31, 61, 92, 123, 154, 184, 215, 246, 276, 307 and
  338, i.e. TP3 nominal 0.30 to **3.27 V**. Each was `LIVE 1 x 20 us -- completed, IRQ0 seen`
  with `Verdict: within limits` and `no current above the ~72 mA detection floor`.
- The readback was clean on every firing:
  - baseline 10.7-10.9 codes (the zero offset);
  - `covered`, brackets 16/16, residuals -7 .. +4;
  - splits 76/76-77;
  - `100 ready` +198 to +200 us.
- One oddity, harmless: at gate 18, `first sample +38 us` (others +2 to +5) but `100 ready
  +199 us` as usual. The ring started on time; the poll that notices it was late, most likely
  held off by an interrupt.
- **Reading:** TP3 at 3.0-3.3 V is well past Q9's 1.0-2.0 V V_GS(th), so the current path is
  open: most likely the load (J3 seating, the bank's wiring or polarity), less likely VIR or
  Q9/Q10.
  - This is the gap the Step 3 rule exists for: zero-current firings count as within limits,
    so the staircase climbs with no load.
  - The owner fired one step past the 30 % rule (33 %), but no harm was done: ending live mode
    restarts the staircase from 0.
- **Next:**
  - `strobe live off`, `off`, PSU and USB off.
  - The owner reports TP3 on the scope and U5 Q on the Saleae at the last steps.
  - Unpowered checks: J3 seating, polarity (J3.1 = VIR = anodes, J3.2 = VIR_RTN = cathodes to
    Q9's drain) and continuity to the bank.
  - VIR at J3 pin 1 -> GND (DMM clipped on while unpowered; expect ~36 V after `on`).
- **Owner, 2026-10-13:**
  - TP3 on the scope read **3.24 V** at gate 33 (nominal 3.268 V).
  - U5 Q (Saleae D3) pulsed at every firing.
  - **VIR to GND read 7 V.** The board state at that reading (rail up or decaying after `off`)
    is not yet known.
- **What it means:**
  - At 7 V no string can conduct. The strings need roughly 22-27 V (VIR 36 V leaves ~9-11 V of
    headroom at 9 A), so VIR this low explains zero current regardless of the gate.
  - But TP3 tracking to 3.24 V needs U6's supply (+12 V, fed from VIR through R15 4K7 and the
    D4 zener) at about 5.5 V or more. So VIR was likely higher than 7 V during the staircase,
    though not necessarily near 36 V.
  - **Correction (2026-10-13):** I wrote here that VIR had never been recorded on board 1. The
    `BRINGUP_NEW_BOARD.md` sign-off records **36.0 V** (§2, J2, no load), and TP2 = 12.31 V in
    6b agrees.
- **Next:** a clean reading with the rail up (`stat`: railsready 1), steady or not, plus PSU
  current and a FLIR sweep of U1/L1/D2 and the bank.
  - If VIR is ~36 V: the 7 V was a decaying rail; back to the bank wiring.
  - If VIR is low: repeat with J3 disconnected, to separate the bank loading VIR from a boost
    (U1, LM5157; 1.0 V x (1 + 150k/4k3) = 35.9 V) that is not regulating.
- **Owner, 2026-10-13, later:** **C26 reads 36 V, but the "VIR+" terminal reads 7 V.**
  - C26.1 and J3.1 are the same net (VIR), and no current flows (Q9/Q10 off). So a 29 V
    difference means an open or high-resistance joint between the VIR copper and the
    terminal: J3 pin 1's solder joint, the terminal's own contact, or a trace.
  - The 7 V is the floating side, set by leakage against the DMM's 10 MOhm. The boost works.
  - **This explains the zero current:** the bank never saw VIR.
- **Next (unpowered, VIR drained below 1 V at C26):** resistance C26 -> J3 pin 1 joint ->
  terminal clamp -> the bank's + lead, and J3 pin 2 joint -> Q9's drain tab. Each should be
  < 1 Ohm. Then inspect J3's joints.
- **Found and fixed by the owner, 2026-10-13: a cold solder joint on J3 pin 1** (Wurth
  691137710002 screw terminal). Re-soldered with the board unpowered.
  - Still to verify: VIR at the J3 screw = 36 V with the rail up.
- **Consequences:**
  - The first staircase (3-33 %) and the gate-0 Step 2 pulse ran with **no load**. They proved
    the readback and the gate drive, but nothing about current or about Q9 with VIR applied.
  - Step 2 is redone first (gate 0, expect no current), with a fresh gate-0 full-bandwidth
    reference, since the Q9/Q10 node now sits under the bank instead of floating. Then the
    staircase runs from gate 3.
  - Re-arming restarts the firmware staircase from 0.
- **Lesson (corrected 2026-10-13):** I first wrote that the bring-up VIR check at J3 pin 1 was
  never reported for board 1. **It was: 36.0 V** in the `BRINGUP_NEW_BOARD.md` sign-off.
  - So either it was read where a cold pin-to-pad joint does not show, or the joints failed
    later. Tightening a screw terminal and landing 16-18 AWG leads stresses the pins.
  - Every copper pour on this layout connects to its pads solid, with no thermal relief (PCB
    file), and the owner reports the pours are hard to solder.
  - Changes made:
    - `BRINGUP_NEW_BOARD.md` §1 now inspects every pour joint, and §2 reads VIR at the J3
      screw against C26.
    - `NEXT_BOARD_REV.md` CR-22 adds thermal reliefs.
    - The 6c pre-power table has J3-screw continuity rows.
    - `HANDOFF.md` §8 has an Assembly lesson.
    - The 6c board state makes "VIR at the J3 screw = C26" a required check before the first
      live pulse.

**6c Step 2 again, at gate 0, now with VIR at the bank (board 1, 2026-10-13).** Three TDS photos
in `C:\Users\ATTAYEKP\Downloads\Photos-1-001 (7)\`:
- Photo 1: 10 us/div, both channels BW-limited.
- Photos 2-3: 250 ns/div at the leading and trailing edges, with **CH1 (TP4) BW-limited and CH2
  (TP3) at full bandwidth.** That is the reverse of what was asked; harmless if the
  first-current-step captures repeat it.
- Both probes at 1X. CH1 TP4 20 mV/div, CH2 TP3 1 V/div, EXT trigger 1.19 V.

What the photos show:
- **No current through the pulse:** TP4 has no plateau at 10 us/div. Any step would be under
  ~2 mV, i.e. under ~0.02 A.
- **TP4 at turn-on:** about -30 mV, then a **+38 mV lobe** (~0.28 A apparent) that decays in
  ~250 ns. This morning, with J3 open, it was a ~90 ns +-25 mV burst. So the bank adds a small,
  slower lobe.
  - Likely: Q10 discharging the small charge on its drain node, which now sits under the bank
    (a few nC to ~20 nC), plus its own gate charge returning through R65/R66.
  - Harmless in size and duration; invisible to ADC0's 2 us samples.
- **TP4 at turn-off:** a ~-40 mV lobe over ~150 ns.
- **TP3 at turn-on:** a ~+0.3 V bump about 90 ns in. 6b saw +0.4 V at ~60 ns.
- **TP3 at turn-off:** a **-0.4 to -0.6 V step** that recovers in ~5 us. This matches the 6b
  gate-0 baseline (-0.3 to -0.4 V, ~5 us), slightly larger. At gate 0 a negative TP3 only holds
  Q9 further off.
- These three photos are the **gate-0 reference with load** for the first current step's edges.
- **Firmware, three gate-0 firings: PASS.** Each one was:
  - `LIVE 1 x 20 us -- completed`, `within limits`, `covered`;
  - `no current above the ~72 mA detection floor`;
  - baseline 10.7-10.9 codes, brackets 16/16, residuals -1 .. +3, splits 76/77.
- Q9 cold on the FLIR. **Step 2 passes with the load connected.** Next: the staircase from gate 3.
- Still to log: the owner's VIR reading at the J3 screw after the repair.

**6c Step 3 again, gates 3-30 % with the load (board 1, 2026-10-13): STILL NO CURRENT; stopped
by the 30 % rule.**
- Ten firings, levels 31 to 307: all `within limits`, `covered` and `no current above the
  ~72 mA detection floor`. Baseline 10.6-11.0 codes, residuals -8 .. +6.
- **TP3 on the scope tracks the setpoint**, ~0.05-0.09 V below nominal throughout, within the
  scope's accuracy at 1 V/div:

  | gate % | 0 | 3 | 6 | 9 | 12 | 15 | 18 | 21 | 24 | 27 | 30 |
  |---|---|---|---|---|---|---|---|---|---|---|---|
  | TP3 nominal (V) | 0 | 0.30 | 0.59 | 0.89 | 1.19 | 1.49 | 1.78 | 2.08 | 2.38 | 2.67 | 2.97 |
  | TP3 measured (V) | 0 | 0.24 | 0.52 | 0.84 | 1.12 | 1.40 | 1.72 | 2.04 | 2.32 | 2.60 | 2.92 |
- **Reading:** at 2.92 V on Q9's gate, with a bank of 2 x 5 VSMA1085400X02 (well under 36 V at
  low current), Q9 should conduct amps. So the series path is still open somewhere in VIR -> bank
  -> J3 pin 2 -> Q9's drain.
  - Q10 switches: TP4 shows its gate-charge lobes at both edges.
  - TP3 couples to Q9 through R63: the post-pulse kicks.
  - So the gate side is unlikely to be the problem.
  - Top suspect: a twin of the pin-1 cold joint, on **J3 pin 2** or on **Q9's tab**. Then the
    bank's wiring, clamping and polarity, then the LEDs.
- **VIR at the J3 screw after the repair was never reported.** Get it before the next power-up.
- **Next (unpowered, VIR < 1 V at C26):** resistance checks, each < 1 Ohm:
  - J3.1 screw -> C26;
  - J3.2 screw -> Q9's tab (this also covers the tab's solder joint);
  - each screw -> the far end of its wire at the bank.

  Then the bank's polarity, and a per-LED diode test with the bank off J3.

**Owner, 2026-10-13: J3 pin 2 also had a cold solder joint; re-soldered.** Both J3 joints were
cold, so every live firing before this had the bank open-circuit. Gate 0 re-run, three TDS photos
in `C:\Users\ATTAYEKP\Downloads\Photos-1-001 (8)\`:
- 10 us/div and 50 us/div, both channels BW-limited.
- 250 ns/div leading edge, with CH1 BW-limited and CH2 full (as before).
- Firmware output, VIR at the J3 screw and the post-repair J3.2 -> Q9-tab resistance: not yet sent.

What the photos show:
- **TP3:** the post-pulse dip is unchanged: ~-0.37 V, recovering in ~5-6 us.
- **TP4 leading edge:** about -11 mV then +22 mV, decaying in ~200 ns. That is smaller than the
  -30/+38 mV with only pin 1 fixed. These photos are now the gate-0 reference with the load fully
  connected.
- **New: an "AC" component on TP4.**
  - A continuous **~45-50 kHz triangle, ~5-7 mV p-p** (~0.04-0.05 A apparent p-p). It is
    present before the trigger, so the pulse does not cause it.
  - Plus intermittent bursts of fast spikes up to ~+-10-12 mV.
  - It appeared once both ends of the bank were connected, i.e. once the bank and its leads sit
    at VIR.
  - It cannot be current in R65/R66 between pulses (Q10 is off). Most likely measurement
    pickup (probe ground lead / common mode) from the bank wiring carrying VIR and the boost's
    light-load burst ripple. **Hypothesis, not verified.**
  - Below the +-0.1 A (13.5 mV) comparison band, and one cycle is ~20 us, so it averages out
    across a 20 us plateau.
- **Discriminating check:** `strobe wave` after a gate-0 firing.
  - A real +-3 mV wobble at ADC0 would swing ~+-4 codes with a ~10-sample period.
  - Still 10-11 codes means the scope alone sees it.
  - Optional: free-run the scope and type `off`. If the triangle stops, it is from the boost.
- **Result, same session: PASS.**
  - **VIR at the J3 screw = 36 V.** J3.2 -> Q9's tab < 1 Ohm.
  - Gate-0 firing: `within limits`, `covered`, `no current above the ~72 mA detection floor`,
    baseline 10.8 codes, residual -2.
  - **`strobe wave`: 214 samples, all 10-12 codes, no periodic swing.** ADC0's pin net is
    R32 4K7 and U3 only (no filter capacitor), so a real +-3 mV, 50 kHz wobble on TP4 would
    have shown as +-4 codes.
  - **The scope's triangle is measurement pickup, not on the sense node.** Read scope plateaus
    as the mean across the plateau; ADC0 is unaffected.
  - Live mode had idled out (300 s) by the time of `strobe wave`; re-arm before the staircase.
- **6c Step 2 PASS with the bank fully connected, 2026-10-13.** Next: the staircase from gate 3.

**6c Step 3, FIRST CURRENT at gate 21 % (level 215, TP3 ~2.04 V), board 1, 2026-10-13. The step
does NOT meet the Step 3 pass criteria; the climb is held at 21 %.**
- Gates 3-18 %: no current (scrollback not sent).
- Gate 21 %, three firings:
  - #1: threshold 36 codes, `no current above the ~72 mA detection floor`.
  - #2: `0.221 A plateau, peak 0.293 A, on 4 us (2 samples), short`, threshold 35.
  - #3: `0.181 A, peak 0.240 A, on 4 us (2 samples), short`, threshold 30.
  - All `within limits` and `covered`.
- Four TDS photos in `C:\Users\ATTAYEKP\Downloads\Photos-1-001 (9)\`:
  - 10 us/div and 2.5 us/div x2, both channels BW-limited;
  - 250 ns/div leading edge, CH1 BW-limited, CH2 full.
  - The owner had moved CH1's zero to ~0.3 div below CH2's.
- **TP4 (photos):**
  - **A turn-on peak of ~+45-54 mV (~0.35-0.40 A).** At 250 ns/div: -14 mV, a fast +19 mV,
    then a rise to +43 mV at ~0.5 us.
  - Then a **~1 MHz oscillation of ~+-12 mV (~+-0.09 A)**. Its envelope swells and fades over
    ~10 us; it is not a clean decay. Troughs reach below zero.
  - The pulse **settles at only ~7-11 mV (~0.05-0.08 A)** by its end.
  - At 10 us/div: a smooth decay from +41 mV to ~+4 mV by ~9 us, then a step to ~+12 mV
    holding to the end. Each photo is a different firing.
  - So TP4 is neither flat after 1-2 us nor free of ringing (Step 3 pass criteria), and Step 3's
    stop rule "TP4 rings or oscillates" applies.
- **The ~1 MHz is close to the LM5157's design 1.055 MHz (R8 20K).** The only switcher tone
  measured on this hardware so far is 801 kHz on +5 V (board 3), from L1 or L2, not settled
  (`BRINGUP_NEW_BOARD.md` §2). With the swelling envelope, the
  boost's switching is the leading suspect. Between pulses the boost's ripple on the scope was
  pickup (`strobe wave` flat), so this may be pickup too. **Unverified.** A real oscillation of
  the Q9 stage near threshold is the alternative, and it could grow with current.
- **TP3 after the pulse: +2.5 V for ~1-2 us**, back in ~5 us, one small undershoot. It was
  -0.4 V at gate 0 and +0.1-0.2 V at gate 3 with no load.
  - Likely: the current must stop when Q10 opens, so Q9's source flies up and drags the gate up
    through Cgs. U7's NPN cannot pull TP3 down, and the PNP needs the slow LM358.
  - Q10 is off, so it carries no current, and it recovers well inside the 150 us burst gap.
  - **Correction:** I told the owner to expect a post-pulse kick of about +0.1-0.2 V at a
    setpoint. With current flowing it is ~10x that.
- **Readback limitation (firmware, safety-relevant; section 9):** see "Half-peak threshold" in
  section 9. It explains all three ADC0 lines: the ~0.3 A turn-on peak put the threshold at
  ~0.15 A, above the ~0.07-0.1 A body of the pulse.
- **Next, at gate 21 % only (no climbing):**
  - `strobe wave` of the last firing. Real +-0.09 A ripple at 1 MHz would scatter the 2 us
    samples by ~+-15 codes; pickup would not.
  - The Saleae A2 file.
  - A 1 us/div capture to time the oscillation against 1.055 MHz.
  - If still unclear: a tip-on-ground test (CH1 tip moved to the G1 wire, unpowered).

**Gate 21 % again, after re-arming, 2026-10-13.**
- The staircase from 3 % was clean: gate 6 was fired twice, and 3-18 % showed no current.
- Gate 21 %: `0.240 A plateau, peak 0.317 A, on 4 us (2 samples), short`, threshold 37,
  `within limits`, `covered`.
- Instruments on this firing: `strobe wave`; Saleae A2 on TP4 at 50 MS/s
  (`C:\Users\ATTAYEKP\Downloads\strobegate21\analog.csv`, offset -74 mV removed); TDS at
  1 us/div, CH1 BW-limited, CH2 full.
- **ADC0 during the pulse** (codes, then amps above the 10.8-code baseline):

  | codes | 64 | 38 | 25 | 19 | 24 | 29 | 28 | 28 | 29 | 28 |
  |---|---|---|---|---|---|---|---|---|---|---|
  | A | 0.32 | 0.16 | 0.08 | 0.05 | 0.08 | 0.11 | 0.10 | 0.10 | 0.11 | 0.10 |
- **Saleae, the same firing:**
  - 0.36-0.39 A at 0.25-1 us, after an edge spike to 0.53 A at 0.05 us;
  - a decay to a **minimum of 0.055 A at 7-8 us**;
  - an abrupt rise at ~9 us to a **steady 0.124-0.139 A from 9 to 20 us**;
  - a ~-0.1 A mean (gate-charge lobe) at 20-20.5 us, then ~0.
  - **No 1.055 MHz content**: ~0.3 mV tone amplitude in the pulse, the same as before and after.
- **TDS, 1 us/div:** the same shape. TP4 peaks at ~+46 mV at ~0.7 us and decays smoothly, with
  no ripple. TP3 (CH2) steps **up ~0.3 V at turn-on**, then sags to **~0.3 V below its setpoint
  by ~8 us**, mirroring the current.
- **Conclusions:**
  - **The ~1 MHz ripple in the earlier photos was boost pickup.** It is intermittent, and absent
    on the scope, the Saleae and ADC0 in this firing. Without the half-peak artefact, ADC0 and
    the Saleae agree: the settled level is 0.10-0.11 A against 0.12-0.13 A, inside +-0.1 A.
  - **The real shape is a slow transient of the gate loop.** The turn-on kicks TP3 up, so the
    current overshoots to ~3x its settled level. U7 (an unbiased push-pull follower) cannot pull
    TP3 down until the LM358 slews through the crossover dead band (~0.3-0.5 V/us). TP3 then
    undershoots, so the current sags to ~0.05 A at ~7.5 us. The LM358 crosses back and the NPN
    re-engages at ~9 us, giving the abrupt step to the settled level.
    - This interpretation fits all three instruments. Why the turn-on kick on TP3 is upward is
      not fully explained.
  - Near threshold the current is very sensitive to Vgs, so 0.3 V on TP3 swings it from 0.35 A
    to 0.05 A. Higher up, Q9 approaches the 1/0.16 Ohm ~= 6 A/V degeneration limit, so the same
    0.3 V would be ~+-2 A. **How the transient scales with gate level decides whether the
    plateau can ever be flat**; one more step measures it.
  - Step 3's pass criterion (flat after <= 1-2 us) is not met. TP3's dip does recover within the
    pulse, and nothing rings, so no "stop altogether" rule is met.
  - **Candidate next-board change (pending data):** bias U7, or add a gate-charge reservoir at
    TP3 / Q9's gate, so Q10's edges cannot move the setpoint. This needs the loop-stability
    re-check that 6b did.

**LED-bank leads twisted tightly (owner), then gate 0 and gate 21 % again, 2026-10-13.**
- Before twisting, one more pass at 3-21 % (plus 6/9/12/15 us test pulses at gate 6) showed
  the same picture. Gate 21 % gave `short` 4 us (0.225 A) and once "no current" (threshold 39).
- After `off`, twisting, `on` and re-arming:
  - gates 0-18 %: no current;
  - gate 21 %, five firings: `0.071 A plateau, peak 0.229 A, 10 us (5 samples)`; "no current"
    (threshold 36); `0.072 A / 0.229 A, 5 samples`; `0.096 A / 0.216 A, 20 us (10 samples)`;
    `0.075 A / 0.240 A, 5 samples`.
  - All `within limits`, `covered`.
- Data: TDS `Photos-1-001 (10)` (gate 0) and `(11)` (gate 21); Saleae
  `C:\Users\ATTAYEKP\Downloads\strobegate21_twistedPair\analog.csv`.

**Saleae on TP4 at gate 21 %, before and after twisting:**

| | straight leads | twisted |
|---|---|---|
| edge spike, 0-0.25 us (max) | 72 mV | **35 mV** |
| overshoot, 0.25-1 us | 0.36-0.39 A | 0.26-0.34 A |
| minimum | 0.055 A at 7-8 us | 0.053 A at 6-8 us |
| step back up | ~9 us | ~8 us |
| settled, 9-20 us | 0.124-0.139 A | 0.10-0.13 A |
| turn-off lobe (min) | -74 mV | **-22 mV** |
| baseline sd / boost ripple | 3.0 mV, intermittent bursts | **2.4 mV**, none |

- **Twisting helped the fast edges and the pickup, as expected. The slow transient is
  unchanged:** overshoot, sag at 6-8 us, then a step to the settled level. That is consistent
  with the gate-loop cause, not the wiring.
- **The firmware's "better" lines are the half-peak artefact again.** A smaller spike lowered
  the threshold to 28-30 codes, which is right at the settled level (~28-31 codes). So
  firing-to-firing the run is 2, 5 or 10 samples, or none. The current did not change.
- **TP3:** the post-pulse kick is now ~+3 V for ~2 us (5 us/div, BW-limited), with a small
  -0.1 V dip at ~3 us. At gate 0 the -0.3 V turn-off dip is unchanged.
- ⚠ **The scope's CH1 now reads ~10x low.**
  - At gate 21 % it shows a ~+5 mV step and a ~+3 mV plateau, where the Saleae and ADC0 show
    a 46 mV overshoot and 13-17 mV.
  - CH1 also now shows ~20 MHz edge ringing, which the 1X probe's ~6 MHz bandwidth used to
    hide.
  - Both fit **the CH1 probe body switch moved to 10X while the menu still says 1X**, most
    likely while the leads were being handled.
  - Owner to check. Set it back to 1X, with live mode off and the rail down.
  - CH2 (TP3) reads correctly (2.08-2.2 V at gate 21 %).
  - **Correction, same day: wrong.** The owner checked, and the probe body and the CH1 menu are
    both at 1X.
    - The signature is that fast edges pass while the µs-scale levels (overshoot, plateau) do
      not. That fits a probe tip with **no galvanic contact with TP4**, seeing TP4 only through
      stray capacitance.
    - Candidates: the hook slipped onto G1 or onto insulation (S2 and G1 are twisted together);
      the S2 lead's joint at TP4 broke while the board was handled; or a damaged probe tip or
      cable.
    - Checks:
      - Scope and Saleae on the **same** firing.
      - Then, unpowered: PROBE COMP on CH1; S2's free end -> R66's CurrentSense pad < 1 Ohm;
        re-seat the hook on bare wire.
  - **Resolved (owner): the CH1 probe was not making good contact.** After re-seating it, the
    scope agrees with the Saleae.

**Gate 21 % with the probe fixed: 20 us and 50 us pulses, 2026-10-13.**
- Firings: four at 20 us and five at 50 us, all `within limits` and `covered`.
  - ADC0 reported either `short 4 us` (0.19-0.23 A, peak 0.25-0.30 A) or "no current"
    (thresholds 31-40 codes). That is the half-peak artefact (section 9): a ~0.1 A, 50 us pulse
    is reported as "no current".
- Data:
  - TDS `Photos-1-001 (12)`: 5 us/div and 10 us/div x2 BW-limited, 250 ns/div with CH1 full.
    CH2 is now at 2 V/div.
  - Saleae `C:\Users\ATTAYEKP\Downloads\strobegate21_twistedPair_50us\analog.csv` (offset
    -53.5 mV removed).
- **Saleae, 50 us pulse:**
  - an overshoot of 0.32-0.34 A at 0.25-1 us, then a decay;
  - a minimum of **0.037-0.040 A at 7-9 us**;
  - a step up at ~9 us;
  - then **steady at 0.09-0.115 A from 10 us to 50 us**, with no second sag;
  - ~1 MHz content <= 0.9 mV (~7 mA).
- **The scope now matches:**
  - TP4 peaks at +44-50 mV (0.33-0.37 A) at ~0.4-0.5 us after a fast edge spike;
  - it decays to ~0-4 mV by 6-7 us;
  - it steps to ~12-14 mV (0.09-0.10 A) at ~9-10 us and holds.
  - The scope still shows ~1 MHz bursts of +-10-20 mV, including **after** the pulse when Q10
    is off. That is boost pickup; the Saleae does not see it.
- **TP3:** ~2.1-2.2 V, with a +0.2 V bump at turn-on and a **+3 V kick for ~2 us after
  turn-off**. CH2 at 2 V/div cannot resolve the ~0.3 V sag seen earlier at 1 V/div.
- **Conclusion:** the gate loop recovers **once** and then holds steady, so it is not a limit
  cycle. What is wrong is the first ~10 us of every pulse: an overshoot to ~3x, a sag to
  ~0.4x, then the settled current.
  - **This matters for the product**, not only the test: §15's strobe pulses are 5-20 us, so
    they sit largely or entirely inside this transient.
  - How it scales with gate level is the next measurement: one step at 24 %.

**6c Step 3, gate 24 % (level 246, TP3 nominal 2.38 V), board 1, 2026-10-13: turn-on OVERSHOOT
3.3 A on a 0.72 A setting. Step 3 STOPPED.**
- Two 20 us firings, both `within limits`, `covered` and `no current above the ~72 mA detection
  floor`, at thresholds of **266 and 265 codes**.
  - A threshold of 266 means the window's peak sample was ~521 codes, i.e. **~3.05 A**.
- Data:
  - Saleae `C:\Users\ATTAYEKP\Downloads\strobegate24_twistedPair_20us\analog.csv` (offset
    -58.3 mV removed);
  - TDS `Photos-1-001 (13)`: CH1 TP4 at 100 mV/div, BW-limited; CH2 TP3 **AC-coupled at
    200 mV/div**, BW-limited; both at 2.5 us/div.
- **Saleae on TP4:**

  | time | current |
  |---|---|
  | 0.5-1 us | **3.30 A** (max sample 451 mV = 3.34 A) |
  | 1-2 us | 2.99 A |
  | 2-3 us | 2.12 A |
  | 3-4 us | 0.74 A |
  | 7-8 us | **0.34 A** (minimum) |
  | ~9 us | steps up |
  | 10-20 us | **0.70-0.74 A**, steady |
- **Scope:**
  - TP4 peaks at ~457 mV (3.4 A), drops steeply at ~3 us, sags to ~0.2-0.3 A by ~8.6 us, then
    steps to ~0.65 A and holds. That agrees with the Saleae.
  - **TP3 jumps up by more than 0.7 V at turn-on** (clipped at the top of the screen), holds
    ~1.5-2.5 us, falls ~0.2 V below its setpoint by ~7 us, and snaps back at ~7.5 us. That
    mirrors the current exactly.
  - The trace after turn-off is scope overload recovery from the +3 V kick; ignore it.
- **Scaling:**

  | gate | overshoot | settled | ratio |
  |---|---|---|---|
  | 21 % | 0.34 A | 0.10 A | 3.4x |
  | 24 % | 3.3 A | 0.72 A | 4.6x |

  0.3 V more on TP3 made the overshoot ~10x larger. Step 3's "stop above 2 A" rule fired, and
  the owner stopped.
- **Mechanism (consistent with every capture; not simulated):**
  - Q9 is a source follower. R65 || R66 (plus Q10) is meant to limit its current,
    I ~= (V_TP3 - Vgs) / R, but only while the gate is **held** as the source rises.
  - U7 (unbiased push-pull) holds the gate only against downward moves: the NPN conducts. Its
    PNP is off across the +-V_BE dead band, and only the LM358 (0.3-0.5 V/us) can bring it in.
  - At turn-on the source rises with the current (3.3 A x ~0.16 Ohm ~= 0.5 V), and Q9's
    ~1.7 nF Cgs carries the gate up with it. TP3's >0.7 V rise matches.
  - So Vgs barely falls, and the current runs to Q9's transfer-curve value at the full setpoint
    Vgs, until the LM358 brings the PNP in (~2-3 us).
  - That correction overshoots, giving the sag; the NPN re-engages at ~8-9 us.
  - Near threshold the transfer curve is steep, so the settled current is a fraction of the
    overshoot.
  - The +3 V TP3 kick after turn-off is the same one-sidedness.
  - This supersedes the gate-21 % "turn-on kick, not fully explained": the "kick" is the source
    rising with the current, not coupling from Q10's edge.
- **Why it is a hard stop:**
  - At the 9 A setting TP3 is ~4.5-6 V. At turn-on, the current would be limited only by Q9 at
    that Vgs and by the LED strings, ballast and wiring: plausibly **tens of amps for
    microseconds**.
  - The firmware's 13 A peak check acts after the pulse, and U5's clamp limits width, not
    amplitude.
  - §15's 5-20 us pulses sit inside the transient.
- **The readback failed the way section 9 predicted:**
  - A 0.72 A, 20 us pulse with a 3.3 A peak was printed as "no current".
  - The staircase counted both firings as passes, so the proven level is 24 % in RAM until
    re-arm.
  - The 13 A peak check would still have acted; the run-based checks (plateau, width,
    stuck-on) were blind.
- **Next:**
  - No live firing on any board until (1) the readback is fixed (section 9) and (2) the gate
    drive is fixed or a safe rework is shown (`NEXT_BOARD_REV.md` **CR-23**), with 6b's
    stability check repeated.
  - Q9's thermal reading for these two firings was not reported; the energy is negligible
    (~3 A x ~3 us).

**Gate-drive fix analysis, 2026-10-13 (owner asked for an in-depth review; full text in
`NEXT_BOARD_REV.md` CR-23).**
- **No rework hook exists:** there is no DNP footprint or 0 Ohm in the gate drive, and the only
  0 Ohm on the strobe sheet is R61 (the pulse path).
- **Recommended: a gate-hold capacitor**, 1 uF X7R from Q9's gate (behind R63) to the
  sense-resistor GND.
  - It cuts every gate disturbance by ~500x: ~20 mV worst case, about 1 % at 9 A, against
    >=0.7 V measured.
  - Small-signal model: phase margin 76-86 deg over TP3 = 1-9 V, against 31-69 deg as built;
    100 nF gives only 39-77 deg.
  - **At TP3, with no R63 in between, the margin is 1-4 deg: it would oscillate.** Never fit it
    there.
- **Board-1 rework:**
  - a wire from Q9's gate lead to a 0805 1 uF sitting on R66's GND pad;
  - not R67's pads (0.65 mm from the gate pad; a bridge bypasses Q10 and the sense);
  - then 6b's stability check, then 6c Steps 2-3 up to 24 % on the Saleae.
- **Next board:**
  - C_hold 1 uF (C28323, already on the BOM);
  - R_iso 100 Ohm;
  - a 0 Ohm gate-stopper footprint;
  - drop U7 (an extended part), with the LM358 driving R_iso and feedback from the op-amp pin
    (modelled PM 78-87 deg);
  - fallback footprints DNP.
  - Net BOM: cheaper.
- Rejected: biasing U7 (impossible on board 1; fights the 5 mA +12 V budget), a pull-down
  (same budget), a faster op-amp (the gate still floats), a current-mode loop (windup with Q10
  open), and firmware-only fixes.
- Firmware impact: the readback fix is still needed. `DAC_SETTLE_MS` and the dry-pulse
  interlock's "decayed to zero" model must be re-derived: the last ~0.6 V now decays with a
  ~30 ms tau, which still leaves it below V_GS(th) at 20 ms.
- **Proposed, owner to decide:** allow live firing **up to 24 %** (already reached) to verify
  the rework, with the Saleae as the truth instrument. Nothing higher until the readback fix.

**Second-model review of the CR-23 analysis (Astra, relayed by the owner, 2026-10-13).**
- **Agrees:** the first trial is 1 µF at Q9's gate, after R63, with U6 and U7 unchanged. Never at
  TP3. The next board is C_hold plus direct LM358 drive through an isolation resistor, with
  feedback before it.
- **Corrections I accept (CR-23 updated):**
  1. U7 *can* pull down. The problem is the transition into pulling down: U7's dead band plus
     the LM358's output-stage crossover, which TI documents as us-scale and which my linear
     model omits.
  2. The phase margins, the 470 nF-2.2 µF range and the ~1 % error are **predictions**.
  3. The disturbance budget is not a worst case. Charge can also arrive through R63 from TP3
     (0.7 V for 3 us -> ~71 mV on 0.6 µF). A collapsing source pulls the gate **down**.
  4. **My stage-2 rework was wrong.** 100 Ohm from U7's base to its emitter pads leaves R60
     sensing after that resistor, not the next-board circuit. Corrected to a 0 Ohm bridge, so
     that TP3 is the op-amp output and R63 is R_iso; deferred until stage 1 is understood.
  5. "The interlock still holds" was overstated. The tail starts only after the DAC ladder has
     fallen (gate maybe ~0.3-0.4 V at 20 ms), and V_GS(th) min is a 250 uA test point. Measure
     the gate voltage and current after a zero command.
  6. Pass criteria are judged on the **gate node and TP4**; TP3 is secondary.
  7. The edge spike is to be investigated, not excluded. The Saleae Logic Pro 8 analog channel
     and the TDS on 1X probes are both ~5-6 MHz, so fast edges need a compensated 10X probe
     or the MSO54B.
  8. 24 % is not a pre-cleared level. Re-climb from gate 3 with each step gated on the measured
     peak (> 2x settled or > 2 A stops). 24 % stays the ceiling until the readback fix.

**Next-board documentation consolidated (owner request, 2026-10-13).**
- `NEXT_BOARD_REV.md` CR-23 now states the full next-board gate drive:
  - C_hold 1 uF, R_iso 100 Ohm, and an R_stop 0 Ohm footprint;
  - U7 DNP with a fitted 0 Ohm (fit-one);
  - R60 sensing before R_iso, with the reason;
  - a DNP compensation footprint, TP3 kept plus a GATE test point, and twisted LED leads.
- **New CR-24, the test-point plan:**
  - the places every bench phase had to probe component pads, and TP2/TP3/TP9/TP10
    clearances;
  - rules: pad size, clearance, a local GND within 5 mm, named nets, silkscreen, clusters;
  - keep/move/rename for TP1-TP10, with **no removals**;
  - additions in P1-P3 priority;
  - a do-not-add list: no TP on `PWR_LowSideD`, no bare VIR TP, no SW-node TP.
- The design doc's strobe section has a known-issue pointer to CR-23 and CR-24.

**Board 1 gate-hold rework fitted, and dry checks on the MSO54B (2026-10-15).**
- **Rework (owner):** 1 µF 50 V X5R from Q9's gate (behind R63) to GND, per CR-23 stage 1. U6,
  U7 and the resistors are unchanged.
- **Data:** CSV exports `F:\strobe\Tek000-009_ALL.csv` (no screenshots, no scrollback).
  - Channels: CH1 TP4, CH2 the gate node, CH3 TP3 (labelled "tp4" on the scope, but it tracks
    the gate, so it is TP3), CH4 "trig".
  - Probes: TPP1000 10X; full bandwidth.
- **What the files are:**
  - Tek000-005: nothing. The gate is at 0 and CH4 shows no pulse; Tek000-003 are identical.
  - Tek006: part of a ramp, with CH3 over-range. Ignored.
  - Tek007: step 0 -> 30 %, 20 ms window.
  - Tek008: `strobe gate 0` from 30 %, 20 ms window.
  - Tek009: the same, 100 ms window.
  - **No pulse captures yet.**
- **Step 0 -> 30 % (Tek007): PASS.**
  - TP3 and the gate follow the DAC ladder together (10-90 % ~5.6 ms, as the 2.62 ms pole
    predicts).
  - Final 2.96 V (nominal 2.97 V). Overshoot +2.5 mV; no ringing.
- **The capacitor is connected and effective.** Regressing gate = TP3 - tau·d(gate)/dt over the
  ramps gives tau ~25-39 us, i.e. **C_eff ~0.5-0.8 µF** through R63 = 47 Ohm. The probe gains
  agree within 0.6 %.
- **Zero-command decay (Tek008, Tek009): measured.** It matches the prediction. Times are from
  the start of TP3's fall, which is within ~0.2 ms of the command.
  - The gate tracks TP3 within +-3 mV throughout.
  - Gate < 1.0 V at 2.8 ms; < 0.6 V at 4.2 ms; < 0.5 V at 6.6 ms.
  - **0.35 V at 20 ms** (the firmware's `DAC_SETTLE_MS` window); 0.27 V at 30 ms; 0.16 V at
    50 ms.
  - **< 0.1 V at ~69 ms.**
- **Static at 30 % (stability): PASS, no oscillation.**
  - Band-limited (~0.4 MHz) RMS is 3.4-4.5 mV on the gate and on TP3, comparable to 6b's
    pre-rework 3.6-3.8 mV.
  - A 47.7 kHz component of ~1.5 mV peak is the **same size on the gate as on TP3**.
    - The gate has ~1 µF to GND (~3 Ohm at 47.7 kHz), so a real millivolt there would need
      ~0.5 mA of AC.
    - And a disturbance driven from TP3 would be ~14x smaller at the gate (R63·C).
    - So it is probe pickup, most likely the boost's light-load burst rate (the ~45-50 kHz seen on
      TP4 before), not the loop.
  - 50 % and 90 % were not captured; 6b's check covers 0/50/90.
- **Still to do:**
  - Q9 current after a zero command (a gate-0 pulse after zeroing, on TP4).
  - 50 %/90 % static.
  - The loaded tests: Step 2, then the climb, with CSVs.

**Board 1 gate-hold rework, loaded verification on the MSO54B (2026-10-15): PASS.**
- **Data:** `F:\strobe\strobe\` — g0, g0zoom (200 ns/div), g21, g24, g24 50us (BW limit on CH4
  only), and g24 50us bwlimit (BW limit on all channels).
  - 100 us windows at 6.25 GS/s; TPP1000 10X.
  - Channels: CH1 TP4, CH2 the gate node, CH3 TP3, CH4 the pulse.
- **Firmware, the same session:**
  - Gate 0 x5: no current, `covered`.
  - Gates 3-18 %: no current.
  - **21 %: `0.100 A plateau, peak 0.103 A, on 20 us (10 samples)`.**
  - **24 %: `0.682 / 0.684 A plateau, peak 0.688-0.689 A, 20 us (10 samples)`.**
  - 24 %, 50 us x5: `0.686-0.690 A`, 25 samples.
  - All `within limits`. The half-peak artefact is gone at these levels, because there is no
    spike to set the threshold.

**Before and after the rework (TP4):**

| | before (2026-10-13) | after (2026-10-15) |
|---|---|---|
| 21 %, first us | 0.34 A overshoot, sag to 0.04 A at 7-9 us | 0.099-0.110 A from 0.1 us; 1-us peak 1.10x (the slow drift up is near-threshold heating) |
| 24 %, first us | **3.3 A for ~2.5 us**, sag to 0.34 A | **0.690-0.712 A from 0.1 us; 1-us peak 1.02x** |
| 24 %, settled | 0.72 A | 0.702 A (scope); firmware 0.682 A, -2.8 % |
| TP3 at the edges | > +0.7 V at turn-on, +3 V after turn-off | -1 to -5 mV during the pulse; nothing after turn-off |
| gate node during the pulse | (not probed) | +2 to +6 mV mean (prediction <= ~20 mV) |

- **Turn-on (g0zoom, g24):**
  - The current rises in **~20 ns** (40-60 ns after U5 Q) and is flat by ~100 ns.
  - At full bandwidth, the first 40 ns carry +-2 A of edge ringing. It is identical at gate 0,
    so it is pickup; with the 20 MHz limit it is gone.
  - At 40-80 ns there is a ~0.5 A, ~40 ns lobe (~19 nC). It is **also present at gate 0**, where
    no LED current can flow. It is Q10's gate charge returning through the sense resistors, not
    LED overshoot.
- **The "noise" on the 24 % plateau is boost pickup, not current:**
  - A ~1.03-1.08 MHz component, ~0.02 A apparent on TP4. That is the LM5157's switching
    frequency (1.055 MHz design).
  1. With the BW limit on all channels, the same component is there **before** the pulse, with
     Q10 off (0.021 A pre-pulse against 0.024 A on the plateau).
  2. The gate node shows ~3 mV of it. But the gate has 1 µF to GND (0.16 Ohm at 1 MHz), so a
     real 3 mV would need ~20 mA of AC into the capacitor. It is in the probes; TP4 correlates
     with it (r ~0.9 on g24).
  3. It grows through a long pulse and continues after it (the gate shows +-50 mV after the
     50 us pulse, with Q10 off), as the boost works to refill VIR.
  4. ADC0's samples on the plateau stay within ~1-2 codes. A real +-20 mA at 1.06 MHz would
     alias to ~+-3-4 codes.
  5. VIR ripple can reach the LED current only through Q9's output conductance and capacitance:
     well under 1 mA.
- **CR-23 stage-1 pass criteria, all met:**
  - the gate holds;
  - no overshoot;
  - settled by ~0.1 us;
  - the rise is faster than before;
  - no oscillation (the 1 MHz is pickup);
  - the edge spike was investigated.
- **Next:**
  - **6d at 24 %** (0.68 A measured, inside 0.5-2.5 A).
  - Then the firmware readback fix, with review, before anything above the 24 % ceiling.
  - Q9's temperature for this session was not reported.

**6d — the U5 clamp with current, board 1, gate 24 % (2026-10-15): PASS.**
- **Firmware** `14:43:50`, after the 24 % firings above. Two `strobe clamptest 1000` at gate
  level 246 (24.02 %, TP3 nominal 2.38 V):
  - `0.687 A plateau, peak 0.700 A, on 134 us (67 samples)` and
    `0.689 A plateau, peak 0.701 A, on 134 us (67 samples)`.
  - Both `completed, IRQ0 seen`, `within limits`, `covered` (brackets 16/16, residual -6 / +1).
  - Measured charge 0.092 mC (1.23 mC booked for the full 1000 µs).
  - enable->IRQ0 1018 µs: the PIO ran its full 1000 µs command. Only the current is clamped.
  - ADC0's record ran ~1250 µs past the edge (627 samples), beyond the 1000 µs command, and found
    one run.
- **Scope:** `F:\clamptest1000_ALL.csv`, one of the two firings (which one is not recorded).
  - MSO54B, -40 to +360 µs at 3.125 GS/s; TPP1000 10X.
  - Channels: CH1 TP4, CH2 the gate node, CH3 TP3, CH4 U5 Q (R61).
- **Results:**
  - **U5 Q high 134.98 µs.** That is the dry clamp (135 µs on the LA, 2026-10-06), unchanged
    with current.
  - **TP4's current is 135.0 µs wide** (50 % of the plateau, 1 µs average). It starts 55 ns after
    U5 Q rises and ends 82 ns after U5 Q falls; it falls from 0.72 A to ~0 in ~150 ns.
  - **No current after it:** from +136 µs to the end of the window (+360 µs), the 1 µs average
    stays within -30 / +6 mA.
    - The small negative offset (-5 to -17 mA in 10 µs blocks) cannot be current: Q10 is off,
      and nothing can drive current backwards through the sense resistors.
    - Most likely it is a ground shift while the boost refills VIR.
  - **Plateau 0.703 A (94.9 mV above the pre-pulse baseline), 20-100 µs.** The firmware's
    0.687/0.689 A is -2.2 %, inside +-5 %; the 24 % 20 µs pulses read -2.8 %. The firmware's
    "TP4 should read 93 mV" agrees within 2 %.
  - **Shape:**
    - 0.68 A over 1-5 µs, rising to 0.70 A by ~20 µs, then 0.70 +- 0.01 A to the end (1 µs
      averages 0.694-0.712 A).
    - The +3 % creep comes with the gate steady, so it fits Q9 warming (its threshold falls with
      temperature). The same creep was seen at 21 %.
    - Turn-on: the known ~0.85 A Q10 gate-charge lobe at +80 ns (also present at gate 0); 0.69-
      0.73 A on a 100 ns average from +120 ns. No overshoot.
    - 20 MHz dips of ~0.1 A every ~0.92 µs (~1.08 MHz): the boost pickup characterised above.
      The 1 µs average does not show them.
  - **The gate node and TP3 hold.**
    - Across the whole window, both edges included, the 1 µs average stays within -5 / +5 mV on
      the gate and -9 / +5 mV on TP3.
    - The 20 MHz extremes (gate -57 / +34 mV, TP3 -62 / +47 mV) are spikes. A real 57 mV in tens
      of ns on 1 µF would need amps.
    - Before the rework, TP3 kicked +3 V after turn-off.
- **Against the 6d pass criteria (`BENCH_P6_STROBE.md` 6d), all met:**
  - ADC0's on-time is 134 µs, against the U5 clamp's 135 µs +- 2;
  - TP4 is the same width (135.0 µs), not 1 ms;
  - the plateau matches the 20 µs pulses' 0.682/0.684 A (+0.7 %; the long pulse includes more
    of the warm-up creep);
  - no fault.
- **Not to be repeated:** 6d is a one-off (BENCH 6d). 0.70 A was the first level inside
  0.5-2.5 A. U5's clamp acts on Q10's gate drive and does not depend on the LED current.
- **Next:**
  - The firmware readback fix (§9 "Half-peak threshold"), with review, before anything above
    24 %.
  - No FLIR on 2026-10-15 (next entry).

**VIR measurement plan, and two corrections (2026-10-15).**
- **Owner's question:** after the firmware fix and the staircase, measure the charge the VIR caps
  really deliver instead of using the theoretical value? **Yes.** Planned as §3 Q13 (method
  there).
  - `BURST_CHARGE_MAX_MC` rests on ~670 µF × ~9 V, neither of them measured.
  - The useful result is a sag model (C, ESR, boost refill, headroom at 9 A), not C alone.
- **Correction 1: `BENCH_P6_STROBE.md` Step 5 was wrong.**
  - It said that the first -> last plateau change records "the VIR bulk-cap droop".
  - It cannot: Q9 regulates the current, so the plateau stays flat until the headroom is gone.
  - Step 5 now says so and points to Q13.
- **Correction 2: the FLIR was missing.**
  - The 6c board state requires the FLIR on Q9 and HS1 **before the first pulse**. The owner had
    none on 2026-10-15, so today's 21/24 % firings and 6d ran without it.
  - I asked for Q9's temperature afterwards, but did not check the board state before the
    firings.
  - Exposure was small. The worst case is 0.7 A × 36 V × 135 µs ≈ 3.4 mJ per pulse, at least
    100 ms apart (≤ 34 mW average).
  - **No live firing above 24 % without the FLIR.**

---

## 7. Source layout

```
firmware/
  CMakeLists.txt              build; PICO_BOARD=pitrac_ltb_v1, PICO_PLATFORM=rp2350
  pico_sdk_import.cmake
  boards/pitrac_ltb_v1.h      RP2354B board header (48 GPIO, 2 MB internal flash, 12 MHz XOSC)
  src/board.h                 PIN MAP — single source of truth. Netlist-verified. Start here.
  src/main.c                  init order + core 0 superloop
  src/safe_state.[ch]         GPIO safe defaults + fault latch. Called first in main().
                              The ONLY writer of GPIO27 PULSE_LIMIT_DISABLE (held 0).
  src/service.[ch]            pitrac_service() / pitrac_yield_ms(): what every blocking
                              command must call; key-abort; watchdog (off by default)
  src/pio_alloc.[ch]          PIO block / state-machine / GPIOBASE assignments
  src/adc_engine.[ch]         ADC modes (IDLE/ARMED/BURST), 32 KB DMA ring (A1), block and
                              triggered capture, volts helpers, +5V_IN scale
  src/power_fsm.[ch]          Phase 1 latch + USB guard + Phase 1b Pi soft-shutdown FSM
  src/panel.[ch]              Phase 1c J7 indicators — ring PWM; ready LED SIO on/off (A7)
  src/beam.[ch]               Phase 2 carrier + phase-locked demod clock, effective-duty ceiling
  src/detect.[ch]             Phase 3/4: threshold DAC, gated HPF, comparator PIO timing,
                              pass coalescing, pass log + retained waveforms, `hpf test`
  src/detect.pio              comparator edge timer (PIO2 SM0, GPIOBASE 16)
  src/cal.[ch]                Phase 3: `level`, `cal demod`, `cal model`, `scan carrier`,
                              `cal gain`
  src/config_store.[ch]       dual-slot, CRC'd, versioned calibration record in flash
  src/shot.[ch]               shot sequencer SKELETON (Phase 7 states fall straight through)
  src/strobe.[ch]             Phase 6: PIO0 burst engine + DMA, gate DAC, safe-off, status;
                              LIVE mode (6c/6d): arm/disarm + watchdog, ADC0 readback per
                              firing, verdicts -> faults, hold checks, `strobe cal`
  src/strobe_plan.[ch]        Phase 6: §15 schedule, charge interlock, PIO encoding, DRY and
                              LIVE admission policy (staircase, ceiling). No hardware access --
                              compiled into the host tests too
  src/strobe_live.[ch]        Phase 6c: ADC0 plateau/verdict math, rolling charge budget,
                              interval, idle timeout, cal steps. Pure -- host-tested
  src/strobe_burst.pio        Phase 6 PIO burst engine, loaded by strobe.c
  src/cli.[ch]                USB-CDC line CLI, incl. `capture` (the bench instrument).
                              Every numeric argument goes through parse_u32/parse_float
  tests/                      NATIVE host tests (separate CMake project, not an RP2350
                              target): power_fsm_test.c (21), strobe_plan_test.c (16),
                              strobe_live_test.c (33), service_test.c (8) = 78, mocks/.
                              BENCH.md "Host regression tests" says how to run them
  tools/scope.py              plots a `capture` block from the CLI; `--roll`, `--trig`
  tools/pilot_analysis.py     one triggered ADC5 pass (`capture trig 5 ...`): windowed stats,
                              peak, 0.2 ms-box FWHM, rail check; JSON + SVG next to the input
                              (§3.7; reproduces the 2026-09-18 pilot: 3.102 V, 22.71 ms)
  tools/check_doc_links.py    every relative link and #anchor in the firmware docs; exit 1
                              on a break
  tools/la_phase.py           Phase 2a checks 1-7 from a 2-channel LA CSV. Auto-identifies
                              carrier vs demod by duty; circular statistics throughout
                              (sign convention and wrap both matter -- see its docstring);
                              `--compare` does the check-7 run-to-run spread
  tools/netlist_report.py     regenerates ../../HARDWARE_REFERENCE.md from the KiCad
                              netlist; `--check` fails if it is stale
  tools/openocd_pi5.cfg       SWD from a Pi 5 via linuxgpiod
  tools/flash_swd.sh
```

`src/board.h` carries the load-bearing hardware comments. Read it before touching anything.

---

## 8. Decisions made (so they aren't re-litigated)

- **C / Pico SDK 2.x**, not Rust — matches the .md pseudocode, first-class PIO/DMA/dual-core.
- 🔵 **Carrier = 104.1667 kHz, FIXED for every board and every user** (sysclk 150 MHz,
  TOP=1439, level=432 → exactly 30.000 %). **Decided 2026-08-28.** It is *not* scanned per
  board, per user or per site.
  - **Cost:** possibly a few percent of SNR on any individual board.
  - **Buys:** one `board.h`, one calibration matrix, one set of expected numbers, field
    replaceable boards, and a support story that can be reasoned about. A per-board carrier
    would need a 10-minute scan, a `cal model` re-fit and a recorded frequency **per board**,
    repeated on every replacement.
  - **Margin, computed 2026-08-28 — ⚠ SUPERSEDED BY MEASUREMENT 2026-08-31:** a square-wave
    demodulator responds at odd harmonics, so interference folds to |f_i − n·f_c|. Against the
    LM5157's *assumed* nominal 1.055 MHz the nearest odd harmonics are **9·f_c = 937.5 kHz
    (117.5 kHz)** and **11·f_c = 1145.8 kHz (90.8 kHz)**, giving **−9.6 % / +7.1 %** of drift.
    ⚠ **Both the nominal and the margin were wrong.** The only switcher tone on the +5 V rail
    is at **801 kHz** (dithered, 762.9–833.1 kHz), which puts the **7th** harmonic 33.8 kHz away
    and cuts the paper margin to **−2.3 %**. ✅ **And then the paper stopped mattering:**
    `scan carrier` put the 7th harmonic *inside* the switcher band at three points and σ_noise
    stayed flat, so the coupling is too weak to fold at all. **The method here is right; the
    numbers are history. Trust the direct measurement.**
  - ⚠ **You cannot choose the collision away.** Odd harmonics are 2·f_c = 208 kHz apart, so
    *any* carrier near 100 kHz has one within ±104 kHz of the boost. The choice only decides
    how far, which is why §3.6 is now a verification and not an optimisation.
  ✅ **Validated on hardware 2026-08-13:** phase lock reproducible to 0.15 ticks, duty exact
  to 250 kHz, peak current 3.11 A.
- **Peak beam current is set by the rail, LED Vf and ballast — not by duty** (measured:
  2.92–3.02 A flat across an 8→30 % ramp). Duty controls only how often. This is why the
  ramp is safe by construction rather than by luck, and why `beam duty` is the wrong knob
  if peak current ever needs changing — that takes a different ballast.
- **The beam operating point is thermally stable but optically is not.** Cold → 85 °C
  plateau moves current +1.7 % and power +0.6 %, because at 3 A most of Vf is I·Rs and series
  resistance rises with temperature, opposing the bandgap term (measured tempco ≈ −0.5 mV/K
  for the stack, ~7× smaller than the low-current figure). **But radiant efficiency still
  falls ~0.3–0.6 %/K**, so ~25–45 % of the light is lost over a 76 K junction rise — and
  none of it is visible electrically. **Phase 3 must calibrate warm.**
- **ADC while armed = round-robin {ch5, ch7} at 250 ksps each**, resolving the .md's unstated
  conflict between the detect signal and the mic both wanting to free-run.
- **Trigger = comparator starts the timing, ADC refines during the camera-handshake dead
  time.** Gets the comparator's latency and the ADC's amplitude-independent accuracy for
  free. **Time the transit with a PIO state machine, not a GPIO ISR** (`ARCHITECTURE.md` A2)
  — one FIFO word gives the interval directly, with no handler and no jitter.
- **The CPU orchestrates; hardware executes.** Anything that must happen at a *specific
  time* goes to PWM/PIO/DMA/ADC. Anything that merely has to happen *soon* stays on a core.
  Full audit and allocation table in `ARCHITECTURE.md`.
- **RPI5_SHUTDOWN active-low**, matching the `gpio-shutdown` overlay default. ✅ Confirmed on
  hardware 2026-07-31 — a 200 ms low pulse. Note the *reason* originally given for this choice
  ("inherently safe through reset") was **wrong**; see §2 and Q10. The choice stands, the
  justification does not.
- **Pi presence is a detection WINDOW, not a single sample** (2026-07-31). It used to be one
  read of `PI_3V3_SENSE` at exactly `RAIL_SETTLE_MS` (250 ms) — which conflated the boost's
  soft-start time with how long a Pi 5 takes to raise its header 3V3, two unrelated things.
  A merely-slow Pi was classified as absent, landing in `BENCH_RUNNING`, where the next
  button press is a hard `FORCE_OFF` — a power cut on a booting Pi, i.e. the exact SD
  corruption this subsystem exists to prevent. Now: poll for `PI_DETECT_WINDOW_MS` (3 s),
  **plus** a debounced late-detect promotion from `BENCH_RUNNING` → `PI_BOOTING` so the
  window's exact value is not safety-critical. **`BENCH_RUNNING` stays** — it is the normal
  bench path for phases 2–6 and belongs in the shipping firmware.
- **`fault clear` is a full acknowledgement**, not just a code reset (2026-07-31). It clears
  the code *and* leaves `PS_FAULT` via `FORCE_OFF`, exactly as a button press does. Clearing
  only the code left the FSM latched in `PS_FAULT`, so the panel ring (which keys off state)
  kept double-blinking while the on-board red LED (which keys off the code) went dark.
- **Automatic panel patterns run on time-since-state-entry**, not free-running time, so a
  short-lived state always shows the *start* of its pattern instead of an arbitrary slice.
- **Watchdog reset action enabled only while the Pi is down**; software supervision while RUNNING,
  because a watchdog reset drops GPIO15 → R12 pulls low → Pi power yanked.
- `cal_demod_phase()` in .md §13.8 is **buggy** (samples ADC5, which is after the 0.66 s HPF, with a
  *static* reflector → reads noise at every phase). Use the chopped-beam differential method instead.

---

## 9. Not done / deliberately deferred

- **Direct-TIA ADC sampling / onboard digital demodulation — deferred by owner
  2026-10-02.** Discussed carrier-referenced ON/OFF sampling and approximately
  208.33 ksps acquisition after the TIA gain rework. The owner chose not to pursue it
  at present. Keep the existing analog demodulation chain; no sampling prototype,
  firmware change, hardware change or new measurement resulted from this discussion.
  This is a prioritization decision, not a finding that digital demodulation is impossible.
- **Closed or superseded items moved 2026-10-05** to [`PROGRESS_ARCHIVE.md`](PROGRESS_ARCHIVE.md)
  §9, verbatim: first build, git init, USB-only exercise, watchdog/`adc5vcal`/flash-persistence
  plans, the `reset`/`bootsel` guard, `adc_read_avg()` (A1), the beam ceiling fix, the A7 slice
  collision and the PIO-vs-CPU note. Their still-live consequences are restated below.
- **Watchdog:** `wdog on|off` exists and is **off by default**. On this board a watchdog reset
  is a hard power cut (GPIO15 high-Z → R12 opens the latch). **Since 2026-10-07 live strobe mode
  arms it itself** (`strobe live on confirm`) and disarms it on exit; `wdog off` is refused
  while live.
- **`adc5v` scale:** compile-time default 1.063; `adc5vcal` trims it in RAM and **`cfg save`
  persists it** with the calibration (board 3: 1.0620).
- **`beam clamp` leaves the duty at 50 %.** `beam_configure()` clamps every caller to the
  **35 %** effective ceiling, but that is above the **25 %** operating point (CR-12): type
  `beam duty 2` straight after `beam clamp`.
- **PWM slices:** A7 is fixed in firmware (GPIO12 ready LED is SIO on/off; slice 6A belongs to
  the strobe gate DAC on GPIO28). Two other pairs are safe **only** while SIO: **GPIO15 (latch)
  shares 7B with the beam carrier** and **GPIO27 (strobe watchdog defeat) shares 5B with the
  panel button LED** — never put either on PWM.
- **Core 1 is unused.** When it is launched, `flash_safe_execute_core_init()` must run on it
  (`config_store.h`), or a `cfg save` hard-faults.
- **PIO work not started:** camera handshake (`ARCHITECTURE.md` A3), the Pi UART protocol and
  any mic onset engine. Comparator timing is already on PIO2 (A2 done).
- 📋 **Code-audit backlog (2026-10-05)** — owner decision: listed, **not scheduled**. Findings
  from the 2026-10-05 audit (§6) that are refactors or hardening rather than bugs. IDs are
  the audit's.
  - ✅ **"Before 6c" — closed 2026-10-07 with the 6c firmware** (§6 2026-10-07). 6c arms the
    watchdog, and an un-fed watchdog drops the latch.
    - Blocking-command service contract: **checked** — `level`, `hpf test`, `cal demod` and
      `cal model` already yield (longest unserviced stretch: `cal.c`'s 10 ms chopped-average
      window); the `capture` / `capture trig` CSV dumps now call `pitrac_service()` every 256
      lines. Fault detection by **generation counter** (SVC-02) and an abort on the final yield
      slice (SVC-03) are done and host-tested (`tests/service_test.c`). ⚠ **Still open, now
      backlog:** `adc_capture()` waits on its DMA for n / rate seconds without servicing, so with
      `wdog on` a block capture slower than ~1 s resets the board. Live mode refuses `capture`.
    - Strobe: run token `s_run_gen` (STR-04/05) ✅; the snapshot reads the GPIO25 pad directly
      (STR-07) ✅; per-resource teardown flags (STR-02) — **checked, no change**: every claim in
      `strobe_init()` panics on failure, so a partial init cannot return, and `strobe_safe_off()`
      drives GPIO25 low before it looks at `s_inited`; `STROBE_TIMEOUT_MARGIN_US` in `board.h`
      (STR-09) ✅.
    - `shot.c` per-state deadlines and an explicit "not implemented" result (SHOT-01/02):
      **reclassified "before Phase 7"** — 6c does not touch the shot sequencer; Phase 7 wires it.
  - **Live strobe, open-load staircase (found 2026-10-07, §6):** a zero-current firing raises
    the proven level. Option: refuse to raise it above ~30 % without detected current, and end
    live mode ("no current — load open?"). (The overstated `board.h` comment above
    `STROBE_LIVE_GATE_STEP_MAX` was corrected 2026-10-12.) Covered procedurally by the 6c Step 3
    stop rule; the owner chose to rely on it (2026-10-12).
  - ✅ **Silent idle exit (found 2026-10-12, 6c Step 2) — fixed in `Oct 12 2026 10:34:54`, reworked after review in `11:09:36`:**
    live mode's 300 s idle timeout ended it from the service loop and printed nothing; the next
    `strobe pulse` then fired DRY with no warning. Now announced before the next command.
  - ✅ **Fixed in `Oct 12 2026 13:55:58` (round 6).** **Capture paths read bit 15 as "overrun" (found 2026-10-12):** `adc_capture()` and
    `adc_capture_triggered()` raise `FAULT_ADC_OVERRUN` when a FIFO word's bit 15 is set. Bit 15
    is the conversion-**error** flag; overflow is `FCS.OVER`, which they never read. So a real
    overrun in a bench capture (round-robin phase rotated) goes unflagged. Same fix as the
    freeze: clear `FCS.OVER` at the start, check it at the end. Not in the strobe path, and not
    changed in the 2026-10-12 image.
  - 🔴 **+5V_IN is not monitored while ARMED or during a bench capture (logged 2026-10-12,
    review round 5).** Those modes do not convert ch1. Since the round-5 fix the monitor
    neither judges nor refreshes a value it did not sample (NOT_SAMPLED). Before it, it
    silently judged a stale cached value.
    - Either way, a supply loss during a long `detect arm` with a Pi on J8 goes unseen until
      the mode returns to IDLE.
    - Options: add ch1 to ARMED, which needs a 4-channel round-robin or a power-of-two layout;
      bound how long ARMED may run with the latch closed; or treat a long NOT_SAMPLED as
      unreadable.
    - Must be decided before Phase 7c, when a Pi is seated.
  - **+5V_IN reads ~0.8 % lower at the corrected ADC rate (board 1, 2026-10-12).** `stat` went
    from 5.205 V (`13:55:58`, half rate) to 5.162 V (`14:43:50`) with the same PSU. The scale is
    unchanged (1.0630, config slot A seq 1).
    - Mechanism: Q9's incomplete S/H settling through the 50 kOhm R46/R47 source. Back-to-back
      conversions leave ch1 less settling time than the half-rate idle gaps did.
    - It errs safe: the latch guard has 112 mV of margin, and the monitor trips early, not late.
    - Re-derive `V5IN_SCALE_DEFAULT` (1.063, measured under the old timing) at the corrected rate.
      Saved per-board scales (board 3: 1.062) read low the same way, so re-trim with `adc5vcal`
      when convenient. The real fix is CR-03 (10k/10k).
  - **`adc5vcal` calibrates a different reading from the one decisions use (found 2026-10-12).**
    - It takes its raw value from a blocking single-channel read (`adc_read_volts(ch1)`: the
      S/H stays on ch1 and settles fully). The monitor, latch guard and `stat` average ch1 from
      the IDLE round-robin, where ch1 follows ch7 (~1.65 V) and, at the corrected rate, settles
      less through 50 kOhm.
    - Board 1: `adc5vcal 5.195` gave `raw 4.885 V`, scale 1.0634, saved. Yet `stat` then read
      5.162 V, a ring raw of ~4.856 V (-0.6 %, ~20 codes; not discretisation).
    - Again on 2026-10-13: `adc5vcal 5.185` gave `raw 4.877 V`, scale 1.0630 -> 1.0631
      (unchanged in effect). `adc5v` then read 5.151 V, a ring raw of ~4.845 V (-0.65 %). The
      owner reasonably read this as "the cal does nothing". Do not work around it by typing an
      inflated voltage: that would make the blocking read (mode OFF, e.g. at boot) read high,
      the unsafe direction for the latch guard.
    - Fix: derive the raw value from a FRESH `adc_5vin_read()` divided by the current scale. Not
      urgent: errs safe, with 112 mV of latch-guard margin. Batch it with the next firmware change.
  - 🔴 **Half-peak threshold hides current that follows a larger spike (found 2026-10-13, 6c
    first current at gate 21 %).**
    - `strobe_live_measure()` sets the run threshold at baseline + max(12 codes, half the
      window's peak excursion), and a run needs >= 2 samples.
    - On board 1 a ~0.3 A turn-on peak hid a ~0.07-0.1 A, 20 us pulse. The first firing printed
      `no current above the ~72 mA detection floor` (cli.c prints that whenever no run is
      found, even with a 50-code peak). The next two reported a 4 us `short` pulse.
    - **Safety relevance:** `on_at_end` and `max_width_us` (STUCK_ON) use the same runs. A
      current that persists below half of a larger spike would not trip them. The plateau
      (I_STOP) and the `strobe cal` solve use those runs too.
    - Fixes to weigh, with independent review:
      - an absolute floor for the stuck-on and width checks;
      - a threshold relative to the settled level, not the peak;
      - accurate text when the excursion exceeds the floor but no run is found.
    - Must be settled before `strobe cal` relies on near-threshold readings, and before
      Phase 7.
    - 🔴 **Escalated 2026-10-13 (gate 24 %):**
      - A 0.72 A, 20 us pulse with a 3.3 A turn-on peak was printed as "no current"
        (threshold 266 codes).
      - The staircase counted it as a pass.
      - **Fix before any further live firing**, with independent review. Detect runs against
        an absolute floor, and stop on a turn-on peak far above the settled level.
      - **Narrowed 2026-10-15, board 1 only.** After the CR-23 rework there is no turn-on
        spike, so the threshold sits at half the plateau and the runs are right: 20, 50 and
        134 µs pulses were measured within one sample. Live firing at <= 24 % resumed for
        the rework check and 6d (§6). **Nothing above 24 %, and no `strobe cal`, until the
        fix.**
  - **Cosmetic, from round 5:** `stat`'s `adc` line labels the age after a fresh read as `HELD`.
    The read itself takes ~1 ms, so the age is 1 ms, not 0. Label it by status, not by
    age == 0, with the next firmware change.
  - 🔴 **Ring-derived timing taken before `Oct 12 2026 14:43:50` is at HALF the stated rate (2026-10-12).**
    Every ring mode ran at half rate (DIV 95, section 6). Detect pass logs and retained
    waveforms stamped `adc_ring_channel_rate_hz()` = 2x the real rate, so their time axes are
    compressed 2x. Re-derive any ring-based timing (transit times, FWHM from pass logs) before
    using it. Block captures at 100/250 ksps, the section 3.7 pilot included, are unaffected.
  - **`adc_ring_view()` / `adc_ring_history()` / `adc_ring_avg()` ignore `FCS.OVER` (2026-10-12,
    review round 6).** An overflow loses conversions and rotates the round-robin, so the channel
    labels they rely on can be wrong, and nothing tells them. The live readback and the 5V_IN
    reader check it; the detect path (ARMED pass analysis) does not. Fix before Phase 4/7 use
    the pass-log ADC column for decisions.
  - **`adc_ring_view()` / `adc_ring_history()` / `adc_ring_avg()` do not bound by the mode start
    (2026-10-12):** straight after a mode switch they return the previous mode's samples as this
    channel's. *(The 5V_IN path no longer uses `adc_ring_avg()`: since round 5 it takes only
    samples issued during the call.)*
    - `adc_ring_avg()` is the 5V_IN monitor's source. For ~2 ms after every switch back to IDLE
      (after each live firing, `adc <ch>`, `capture`, `adc ringtest`), its 256-sample mean
      includes BURST or blank slots.
    - Today the 500 ms `V5_MIN_SUSTAINED` debounce absorbs it. But a latch decision taken in that
      window would read a wrong 5V_IN. Check `on`'s latch-guard path.
    - Harmless for detect (ARMED runs for seconds before a pass).
    - Phase 7's `SHOT_FIRING`, and any reader right after a switch, must count what the DMA wrote
      (`adc_ring_written()`).
  - **Why the BURST ring started ~200 us late on board 1 (2026-10-12): unknown.** Measure with
    `adc ringtest` after reflashing.
  - ✅ **Stale live-mode checklist (found 2026-10-12, 6c Step 1) — fixed in `Oct 12 2026
    10:34:54` (kept in `11:09:36`),** with the `board.h` staircase comment: the checklist said "ground on TP1", which
    was disproven on 2026-10-07. It now says the R66 wires, not TP1/TP5.
  - **Structure:** split `cli.c` (2,275 lines) into cli_core / power / adc / panel / beam /
    detect / cal / strobe around one dispatcher and the shared parsers (F-23); a shared
    `require_rails()` guard with one refusal wording (F-25); table-driven subcommand help so
    syntax is machine-checked, not just top-level names (F-17/18); help defaults generated from
    the constants (F-19/20/21); split `board.h` into pins / adc / beam / strobe / power behind
    an umbrella header (BOARD-3); panel PWM constants into `board.h` (PNL-01); remove the dead
    `CARRIER_LEVEL_30PCT` / `DEMOD_LEVEL_50PCT` (BOARD-1).
  - **Testability, ranked:** (1) config record validation + slot selection incl. sequence wrap
    (CFG-1/3) — **also add a migration path before the first `CFG_VERSION` bump**, because the
    version check now makes an old record fall back to defaults (board calibration lost);
    (2) beam planner + duty ceiling; (3) detect waveform refinement; (4) cal phase/form-factor
    math; (5) +5 V ADC math; (6) detect coalescing. Before host-testing `strobe.c`,
    `safe_state.c` or `panel.c`, restructure `tests/mocks` into a small HAL state model (pin
    function/direction/level, PWM CC, µs clock) (TST-01..04). Add 32-bit ms wrap tests for
    `power_fsm`.
  - **Concurrency (latent — every caller today is the core-0 superloop):** write down the
    core-0-only ownership contract; ring-snapshot consistency and `adc_read_avg()` while the
    ring runs (ADC-1/2/3/5); beam multi-register updates (BEAM-2); detect FIFO latency budget,
    64-bit timestamps and returned-pointer lifetimes (DET-2/3/4).
  - **Config robustness:** reload both slots after a failed save (CFG-2); an explicit sequence-wrap
    policy (CFG-3); range-check every restored field (CFG-5).
  - **Cal:** report the effective chop frequency and point count instead of clamping silently;
    restore the beam exactly after `cal model` (CAL-1/2/3).
  - **Printed output:** `beam freq` below ~2289 Hz prints a wrong "frequency step" (ignores
    the clock divider; shows ~0 Hz at 8 Hz) — found by the 2026-10-05 smoke check. Cosmetic.
  - **Tools:** `tools/requirements.txt` (pyserial and matplotlib are not in the selected
    interpreter); unused locals in `scope.py` and `netlist_report.py`. *(Done 2026-10-09: the
    pilot-capture analysis script is now `tools/pilot_analysis.py`, for the §3.7 20-pass set.)*
  - **Checked, no action:** CFG-4 (a flash-resident callback under `flash_safe_execute()` is the
    SDK pattern); STR-08 (the PIO overheads are what 6a.2 measures); PNL-02 (the CLI never
    passes NULL).
- 📋 **Halt telemetry — record repeated or unrequested Pi shutdowns.** Requested 2026-07-31.
  If the Pi 5 halts repeatedly, and especially if it halts when nobody asked, that must leave
  a durable trace: it is the exact symptom of a Q10-class fault and the kind of intermittent
  problem that is invisible without a counter. **Full design note in `BENCH_P8_PI.md` §8.6.**
  The short version:
  - **The RP2354 is the authoritative witness** — it is the only party that knows *intent*
    (did we assert RPI5_SHUTDOWN?), and it survives the failure. If the SD card is the
    casualty, the Pi's own log is the least trustworthy record of what happened to it.
    Pi-side journald is corroborating detail, not the primary record.
  - **Ride it on the flash config block** (`config_store.c`, built in Phase 3) — the counters
    are a handful of words. Two constraints: `cfg_save()` refuses unless the machine is quiet,
    and adding fields means a `CFG_VERSION` bump, which needs the migration path in the
    code-audit backlog below.
  - Counters: `boot_count`, `pi_shutdown_requested`, `pi_shutdown_clean`,
    `pi_shutdown_timeout`, and the important one, **`pi_down_unrequested`**.
  - ⚠ **Prerequisite that does not exist yet:** `PS_RUNNING` never watches for the Pi going
    down — it only looks for a button press or a shutdown request. If the Pi halts on its
    own the FSM sits in `RUNNING` forever with the rail up and never notices. **That
    detection has to be added before `pi_down_unrequested` can mean anything**, and what the
    board should *do* about it interacts with Q4 (if the header 3V3 does not drop at halt,
    detection rests entirely on `RPI5_ON`).
- **`capture` is blocking and prints over CDC**, so a 16 k dump takes a few seconds. Fine for
  bench use; it is not a streaming telemetry path. Same for `beam ramp`, `beam sweep`,
  `panel demo` — all correctly blocking, because they are interactive bench tools. The rule
  that matters: **nothing in the armed or firing path may block.**

---

## 10. Next session — start here

> ### 🔴 RESUME POINT — 2026-10-15 (later): 6d PASS on board 1 at 24 % (U5 ends 0.70 A at 135 µs of 1000 commanded); next is the readback fix, with review, before anything above 24 %
>
> **Update 2026-10-15 (§6):**
> - 6d: two `strobe clamptest 1000` at 24 %. Firmware: `0.687 / 0.689 A plateau, on 134 us`,
>   `within limits`.
> - Scope: U5 Q high 134.98 µs. TP4's current is 135.0 µs wide at 0.703 A, with nothing after
>   it to +360 µs. ADC0 covered ~1250 µs and found one run.
> - The gate node and TP3 hold (1 µs averages within +-5 / +-9 mV).
> - **Next:**
>   1. The firmware readback fix (§9 "Half-peak threshold"):
>      - an absolute floor for run detection and the stuck-on/width checks;
>      - a stop on a turn-on peak far above the settled level;
>      - accurate "no current" text.
>
>      Then host tests and Sol's review. **Reflash first** after it.
>   2. Then 6c Step 3 above 24 %, `strobe cal 9`, the VIR measurement (§3 Q13), and bursts
>      (Step 5).
>   3. **The FLIR on Q9 and HS1 before any live firing** (the 6c board state). There was none
>      on 2026-10-15, so the next live session waits for it.
> - Board 1 still runs `14:43:50`. The 1 µF rework is on board 1 only.
>
> ### (superseded later 2026-10-15: 6d PASS) RESUME POINT — 2026-10-15: board 1's gate-drive rework is verified (flat 0.70 A at 24 %); next is 6d at 24 %, then the readback fix before anything above 24 %
>
> **Update 2026-10-15 (§6):**
> - 1 µF X5R fitted at Q9's gate, behind R63 (CR-23 stage 1).
> - Dry checks pass; C_eff ~0.5-0.8 µF.
> - Loaded at 21/24 %:
>   - flat from 0.1 µs, with a ~20 ns rise;
>   - 1-us peak 1.02x settled (was 3.3 A on 0.72 A);
>   - the gate node moves +2 to +6 mV;
>   - ADC0 agrees within 3 %.
> - The plateau's ~1 MHz "noise" is boost pickup: it is present with Q10 off.
> - **Next:**
>   1. **6d at 24 %** (0.68 A).
>   2. The firmware readback fix (§9 "Half-peak threshold"), with review.
>   3. Then Step 3 above 24 %, `strobe cal 9`, and bursts.
> - Board 1 still runs `14:43:50`.
>
> ### (superseded 2026-10-15) RESUME POINT — 2026-10-13: 6c Step 3 STOPPED at gate 24 % (gate-drive overshoot, 3.3 A on 0.72 A; readback blind spot). No live firing until both are fixed.
>
> **Why (§6, 2026-10-13, gate 24 %):**
> - At turn-on the current overshoots to **3.3 A for ~2.5 us** before settling at 0.72 A.
> - TP3 rises >0.7 V at turn-on, because U7 can source but cannot sink quickly: Q9's source
>   drags the gate up, and the sense resistor's degeneration is defeated until the LM358
>   catches up.
> - At the 9 A setting this could be tens of amps for microseconds.
> - ADC0 printed the pulse as "no current" (half-peak threshold), and the staircase counted
>   it as a pass.
>
> **Next, in order:**
> 1. Firmware: fix the live readback (§9 "Half-peak threshold"), with Sol's review.
> 2. Hardware: the gate-drive fix, `NEXT_BOARD_REV.md` **CR-23**, analysed 2026-10-13.
>    - Board-1 rework candidate: 1 uF at Q9's gate, behind R63, to R66's GND pad.
>    - Then 6b's TP3 stability check, and 6c Steps 2-3 up to 24 % on the Saleae (proposed;
>      owner to decide).
> 3. Only then resume Step 3, from gate 3.
>
> **Board state:** live mode off and rail down (owner to confirm). LED leads twisted. Probes:
> CH1 TP4, CH2 TP3, both 1X; Saleae A2 on TP4.
>
> *Earlier on 2026-10-13 (history):* both J3 joints were cold (fixed); 6c Step 2 passed with the
> load; first current came at gate 21 %.
>
> 🔴 **Update 2026-10-13:** the staircase ran from 3 % to 33 % with **no current at any step**,
> although TP3 reached 3.27 V nominal and the readback was clean (§6). **Cause found:** a cold
> joint on J3 pin 1. C26 read 36 V and the J3 screw read 7 V. The owner has re-soldered it.
> **Next:**
> - Pre-power DMM checks.
> - VIR at the J3 screw = 36 V with the rail up.
> - ✅ Step 2 again at gate 0 with the load: PASS (3 firings, no current, Q9 cold), with a
>   fresh gate-0 edge reference (§6).
> - The staircase from gate 3 again reached 30 % with **no current**: **J3 pin 2 was also a
>   cold joint**, now re-soldered.
> - ✅ VIR at the J3 screw = 36 V; J3.2 -> Q9's tab < 1 Ohm. Step 2 at gate 0 PASS with the bank
>   fully connected. `strobe wave` was 10-12 codes, so a 50 kHz triangle on the scope's TP4 is
>   pickup (§6).
> - **First current at gate 21 % (2026-10-13), but the step fails Step 3's pass criteria.**
>   - TP4 shows a ~0.35 A turn-on peak, then a ~1 MHz ripple for ~10 us, settling at ~0.07 A.
>   - ADC0's half-peak threshold mis-measures that shape: it printed "no current" on the first
>     firing, then a 4 us "short" pulse (§6; firmware finding in §9).
>   - Resolved: the 1 MHz was boost pickup.
>   - The real shape is a slow gate-loop transient: ~0.35 A overshoot, a sag to 0.05 A at
>     ~7.5 us, then a steady ~0.11-0.13 A from 9 us (ADC0 and the Saleae agree) (§6).
>   - LED leads twisted (2026-10-13): the edge spikes and pickup are smaller; the slow
>     transient is unchanged.
>   - The scope's CH1 reads ~10x low. It is not the 10X switch (owner checked: 1X). Most likely
>     the tip has no contact with TP4.
>   - The CH1 probe contact is fixed, and the scope now agrees with the Saleae. A 50 us pulse
>     shows one recovery at ~9 us, then a steady 0.09-0.115 A to 50 us: no cycling.
>   - The first ~10 us (overshoot ~3x, sag ~0.4x) is the problem, and §15's pulses are 5-20 us.
>   - Next:
>     **one diagnostic step at 24 %**, then stop for review. No 6d until reviewed.
>
> **Firmware:** board 1 runs **`Oct 12 2026 14:43:50`** (Release, 194344 text / 122796 bss,
> SHA256 `2D3BD0C1…4FB6471D`, 102/102 host tests). Uncommitted; the owner commits. Live firing
> was gated on Sol's confirm of the round-7 changes (`REVIEW_RESPONSE_7.md`). The owner went
> ahead; the confirm itself is not logged here yet.
>
> **Done today (§6):**
> - `adc 0`: 9 codes. `adc ringtest 100`: 200/200 covered.
> - Steps 1–2 PASS: gate 0 gave `within limits` and `covered`; `strobe wave` read 10–11 codes.
> - The gate-0 full-bandwidth reference.
> - **Step 3 at gate 3:** no current, TP3 ≈ 0.3 V. A +0.1–0.2 V TP3 kick after Q10 turns off is
>   harmless (§6).
>
> **Scope today:** TDS 1002, both probes at **1X** (menus match): CH1 TP4, CH2 TP3, EXT
> trigger from the GPIO25 wire. Saleae: D1 GPIO25, D3 U5 Q, A2 TP4. Grounds on the R66 wires.
>
> **Next:**
> - Step 3 on up in 3 % steps, filling in the table at each step.
> - **6d at the first 0.5–2.5 A step.**
> - Stop at ~3–4 A for review.
> - Then `strobe cal 9` (Step 4) and bursts (Step 5).
>
> The stop rules are in Step 3 of `BENCH_P6_STROBE.md`.
>
> **Open:**
> - The §9 items, including 5V_IN unmonitored while ARMED or during a bench capture (fix before
>   Phase 7c).
> - Whether the R64 lead is still on board 1.
> - The 10 Ω board (2 or 3).
> - Board 2's post-spark check.

> ### (superseded later 2026-10-12: board 1 reflashed, Steps 1–2 PASS) RESUME POINT — 2026-10-12: 6c paused at Step 2; readback fix built, awaiting review, then reflash
>
> **What happened:** 6c Steps 0, A and 1 passed on board 1 (TDS 1002 + Saleae today, no MSO54B).
> The first live pulse at gate 0 latched a false **`STROBE_CLAMP`**. ADC0's "baseline" was 86
> leftover IDLE-ring samples (mean 1277.5 codes, "7.6 A"), because the BURST ring had written
> only 122 samples when the clock-sized copy was taken. TP4 was flat on the scope and the Saleae,
> Q9 stayed cold, and `adc 0` = 9 codes. §6 2026-10-12 has the diagnosis.
>
> **Firmware:** fix image **`Oct 12 2026 14:43:50`**, Release, 194344 text / 122796 bss, SHA256
> `2D3BD0C1…4FB6471D`, 102/102 host tests, 25 mutations all caught. Six review rounds (Sol
> passed round 6). Then the bench (`adc ringtest`) found the ADC at **half rate**, the real
> cause of the morning's "late start", and first-pass snapshot brackets. Both are fixed (§6). **Not flashed.** Board 1 still runs `Oct  7 2026 08:07:33`:
> **do not fire live on it.** Uncommitted; the owner commits.
>
> **Order:**
> 1. ✅ **Independent review** by ChatGPT 6.1 Sol (2026-10-12), six rounds, all fixed with
>    tests and mutations (§6).
>    - Round 1: 4 findings (1 safety).
>    - Round 2: the coverage tolerance was unsafe; replaced by an exact SysTick check.
>    - Round 3: the snapshot ran from flash with an assumed latency; an XIP fetch stall
>      could cancel one missing sample. **Fixed:** SRAM-resident, self-bracketing snapshots
>      that reject their own wide brackets (§6).
>    - Round 4: the index marks a transfer *issued*, not a completed write. Analysis
>      (`REVIEW_RESPONSE_4.md`, no code yet) finds:
>      - the coverage logic holds for the issue event, with D derived;
>      - a missing completion barrier before the freeze copy;
>      - our DMA abort not following RP2350-E5 (a candidate for the late start);
>      - a dead IDLE ring would blind the 5V_IN monitor.
>
>    - Round 5: the analysis accepted with qualifications. Implemented: E5-safe DMA stops,
>      the freeze completion barrier with error checks, the PIO-feed wait, and fresh-only
>      5V_IN with the new `SUPPLY_UNREADABLE` fault (§6).
>    - Round 6: the 5V reader could certify rotated or error data, and the triggered
>      capture ignored its stop result; both fixed. The timing bound was not closable by
>      argument. **Completeness is now proven from the ADC/DMA hardware flags** (no FIFO
>      overflow/underflow, ADC free-running to the stop, clean DMA); SysTick is a
>      cross-check (§6).
>      Sol passed round 6.
>    - Bench, image `13:55:58`: `adc ringtest` 0/10 covered. ADC0 arrived every 4 us
>      (DIV 95: the ADC ignored every other trigger). Fixed in `Oct 12 2026 14:43:50` (DIV 0). Sol to
>      confirm the fix; dry diagnostics can proceed meanwhile.
> 2. **Reflash board 1** (USB only is enough; BOOTSEL), then `id` → `built : Oct 12 2026
>    14:43:50` and the last `fw` line `+ ADC at true 500 ksps (DIV 0), synchronised
>    snapshots`. `adc ringtest 5`: `written at` must show **+400:200** (2 us per sample). Then `stat`: `5V_IN : ... fresh`.
> 3. Dry, rail down or up, gate 0: `adc 0 4096` (< 10 codes), **`adc ringtest 5`** (every pair,
>    warm and cold cache, `covered`) and **`adc ringtest 100`** (`PASS: every pair covered.`;
>    record the widest bracket and residual range). Send the output: it also measures the late
>    start.
> 4. 6c Steps 1–2 again (TDS 1002: CH1 TP4 DC with BW limit, CH2 TP3 DC; traces separated).
>    Step 2 passes with `within limits`, `covered`, the two splits agreeing, and `strobe wave`
>    ~10 codes end to end. Then the staircase. While there: `detect arm` then `adc ringtest`
>    must be refused; an idle timeout must print one notice, once.
>
> **Board state now:** board 1, rail down, J3 connected, leads and probes on. Fault cleared.
>
> **Still open for the owner:** whether the R64 lead is still on board 1; the 10 Ω board
> (boards 2/3); board 2's post-spark check.

> ### (superseded 2026-10-12: Step 2 stopped on a readback bug) RESUME POINT — 2026-10-09: board 1 on the 6c build, TP3 stability PASS; next is 6c's first live pulse
>
> 🔴 **Update 2026-10-12: 6c STOPPED at Step 2.** Steps 0, A and 1 passed. The first live pulse at
> gate 0 latched **`STROBE_CLAMP`**: ADC0 read a steady **1277 codes (1.03 V, "7.6 A")** before,
> during and after the pulse. This is almost certainly the ADC0 path (R32 / GPIO40) or the
> readback, not current (§6). **Do not `fault clear` or re-arm** until `adc 0` with the rail down
> and `strobe wave` explain it.
> **Explained the same day — a firmware readback bug, not current and not hardware.** `adc 0` =
> 9 codes. The record's first 86 samples were stale IDLE-ring data, because the copy is sized by
> time while the BURST ring had written only 122 samples. **6c is paused for a firmware fix**
> (§6, fix plan). No live firing on the `Oct  7` image.
>
> **New model or harness?** Read `HANDOFF.md` first (updated 2026-10-09 for this handoff), then
> this block, then `BENCH_P6_STROBE.md` 6c. Nothing below depends on the previous session.
>
> **Firmware:** `build/pitrac.uf2`, Release **`Oct  7 2026 08:07:33`**, 184440 text / 122720 bss,
> SHA256 `DE0C6676…2DE80BC2`, 78/78 host tests. **Board 1 is running it** — ✅ confirmed by `id`
> in 6c Step 0, 2026-10-12 (§6). In it: strobe **live mode** (6c/6d) with its guards; `strobe cal`, `strobe wave`; the
> "before 6c" §9 items; two review fixes (§6 2026-10-07). **No live pulse has been fired.**
> Everything through this handoff is committed (`a8e08b3`, 2026-10-09); later work is not, unless
> `git log` says otherwise. The owner commits.
>
> **Board 1** (`a764f5332ca5ac53`, COM5): leads on R33, R61 pad 1, TP3, TP4 and two on R66's GND
> pad. 6a/6b PASS 2026-10-06; **TP3 loop stability PASS 2026-10-08** (MSO54B: AC RMS 3.6–3.8 mV
> at every gate, flat; §6).
>
> **Order (all in `BENCH_P6_STROBE.md`):**
> 1. ✅ **Done 2026-10-12** (§6; LA widths still to be reported). J3 **disconnected**: `id` → `built : Oct  7 2026 08:07:33` and `fw` line `+ 6c/6d live current
>    (guarded, ADC0 per firing)` (reflash `build/pitrac.uf2` if not); `strobe` → `strobe   : DRY.`,
>    `U5 clamp <= 137 us (board 1 measured 135)`, `live     : OFF -- last ended: not armed since
>    boot`. A 10-second re-run of 6a's `strobe pulse 20` on the LA confirms the dry path.
> 2. ✅ TP3 at a static setpoint — done 2026-10-08.
> 3. PSU and USB off: the lead plan's DMM checks if not already done (6c board state, "Leads to
>    solder"); connect J3; probes on the TP4/TP3 leads, grounds on the R66 leads; MSO54B CH1 TP4,
>    CH2 TP3, **20 MHz BW limit** for plateau readings, Aux Trig from GPIO25; FLIR ready. 6c
>    Steps 1–6:
>    refusals and arming → first live pulse at **gate 0** (expect no current) → 3 % staircase,
>    ADC0 vs TP4 at every step (**stop if nothing is detected by gate 30 %** — open load) →
>    **6d at the first 0.5–2.5 A step** → `strobe cal 9` → bursts.
> 4. Bench tidy-ups (10 s each): 6b Step 4's `on` → TP3 ≤ 30 mV; the unrecorded TP3 ramp points;
>    Step −1 on boards 2 and 3 to identify the parked 10 Ω board.
>
> **Open questions for the owner** (unanswered as of 2026-10-09): which of boards 2/3 has the
> 10 Ω short on R61 (R62 is off it); board 2's post-spark damage check; board 1's fast-path
> bring-up currents/VIR were never sent; whether the R64 (Q10 gate) lead from the 2026-10-06 scope
> session is still on board 1; the firmware fix for the open-load staircase gap (§9) — before or
> after the first live pulses. **Answered 2026-10-12: after — 6c relies on the Step 3 stop rule
> (§6).** 6c runs on the **TDS 1002 + Saleae** (MSO54B unavailable), Saleae analog on TP4.
>
> **Open in parallel, deferred:** optical acquisition window, §3.7 20-pass set, CR-18.

> ### (superseded 2026-10-07: firmware written) RESUME POINT — 2026-10-06 (11:20): strobe 6a/6b PASS on board 1; next is 6c preparation
>
> **Board 1** (`a764f5332ca5ac53`) runs `Oct  5 2026 11:13:55`, with leads soldered to R33
> (GPIO25) and R61 pad 1 (U5 Q). **6a and 6b PASS** (§6): U5 clamp 135 µs; pulses and bursts
> exact on the LA; §15 schedule and shedding; A7 independent; interlock both ways; TP3 =
> 3 × 3.29 V × duty (4.936 V at 50 %, 9.88 V at 100 %); TP2 12.31 V.
>
> **Owed before 6c:**
> 1. **Scope:** ✅ Q10's gate done 2026-10-06 (≈ 12 V, +7 ns width, 55/45 ns edges; §6).
>    **Still:** TP3 at a static setpoint (`strobe gate 50`, no pulses), AC-coupled 10 mV/div,
>    20 µs/div and 2 ms/div, < 10 mV p-p — the U6A + U7 loop stability check. Run the scope's
>    probe compensation on both channels first.
> 2. **Firmware (my side, then reflash):** 6c guarded-current mode (setpoint ceiling, single
>    pulses first, ADC0 plateau per shot, watchdog armed), `STROBE_HW_LIMIT_US_ASSUMED` → worst
>    case ~137 µs plus the stale `strobe` status text, and the 🔴 §9 backlog items
>    (blocking-command service contract, strobe run token, `shot.c` deadlines).
> 3. **Bench tidy-ups (10 s each, next power-up):** 6b Step 4's `on` → TP3 ≤ 30 mV; the
>    unrecorded TP3 ramp points; Step −1 on boards 2 and 3 to identify the parked 10 Ω board.
>
> **Open in parallel, deferred:** optical acquisition window, §3.7 20-pass set, CR-18.

> ### (superseded 2026-10-06 11:20) RESUME POINT — 2026-10-06: board 1 → strobe-only bring-up, then 6a on the LA
>
> ✅ **Update 10:50: bring-up done, 6a.1 PASS — U5 clamp 135 µs** (§6). Next: 6a.2 → 6a.3 → 6b.
> Pending firmware batch after 6a.2: `STROBE_HW_LIMIT_US_ASSUMED` → worst case (~137 µs) and
> any PIO-overhead correction.
>
> **Board 1** (UID `a764f5332ca5ac53`, A4; U11B dead → TIA readings meaningless, strobe chain
> untouched). Steps, all in the owner's chat of 2026-10-06 and in the docs:
> **A** unpowered: inspect (nothing conductive near D12), Step −1 baseline (R61 → GND ≈ 1 kΩ,
> R33 → GND ≈ 1 kΩ), solder 30 AWG leads to **R33** and **R61 pad 1**, re-measure.
> **B** USB only: BOOTSEL, flash `pitrac.uf2`, `id` = `Oct  5 2026 11:13:55` + UID.
> **C** `BRINGUP_NEW_BOARD.md` §2 (USB out): 0.3 A no J2 → ~32 mA; J2 + 2 A → ~129 mA, +5 V
> 5.2, **VIR 36 (J3 pin 1 → GND)**, **TP2 11.4–12.7**, TP6 2.59; J2 off.
> **D** §3 `pins`, §4 `adc5v` ≈ 5.20. **E** §5 Test 1 (USB-only refusal) and Test 2 (latch).
> **F** LA: CH0 R33 lead, CH1 R61 lead, GND TP1 (attach unpowered); PSU 3 A; 6a Step 0 → Step 1
> → 6a.1 (`clamptest 100` refused; 200 ×3, 1000 ×3; record CH0/CH1 widths and delay).
>
> **Other boards:** the 10 Ω board is parked with R62 off (§6); identify it with Step −1 on
> boards 2 and 3. **Before 6c:** scope check of Q10's gate, 🔴 §9 backlog items.

> ### (superseded 2026-10-06) RESUME POINT — 2026-10-05 (afternoon): image flashed, smoke 13/13 PASS; 6a next, on the logic analyser
>
> **Board 3** runs `Oct  5 2026 11:13:55` (flashed and smoke-checked 2026-10-05, §6), calibration
> intact. **Instruments: logic analyser (Saleae, 50 MS/s) and DMM; no scope for now.**
>
> **Board state for 6a:** PSU 5.20 V / **3 A** limit on J1, USB on J6, **J3 disconnected**, J8/J4
> empty, beam off, `detect disarm`, file logging on. **Rail DOWN while attaching leads**, all LA
> grounds to board GND (not TP5): **CH0 GPIO25 at R57** (non-GND pad), **CH1 U5 Q at R61**.
> 3.3 V logic threshold, 50 MS/s, trigger GPIO25 rising, ≥ 2 ms capture. 🔴 Nothing on Q10's
> gate (R64) — up to 12.6 V, and it is the scope's job, due before 6c.
>
> Then `BENCH_P6_STROBE.md`: **Step 0** (rail down: `id`, `pins`, `strobe`, `strobe pulse 20`
> refused) → **Step 1** (`stat`, `on` only from STANDBY, 3 s, `strobe` → `may fire : yes`) →
> **6a.1** (`strobe clamptest 100` refused; `clamptest 200` ×3 and `clamptest 1000` ×3: record
> GPIO25 width, **U5 Q width = the clamp**, GPIO25↑→Q↑ delay; expect ~122 µs, band 109–136;
> **stop** if < 100 µs or ≈ the commanded width) → **6a.2** → **6a.3**. Then **6b** with the
> DMM on TP3 and TP2. Send the log, the LA captures (`.sal` or exported CSV) and the numbers.
>
> **Open in parallel, deferred:** the optical acquisition window, the §3.7 20-pass set, CR-18.
> **Before 6c:** the scope check of Q10's gate, and the 🔴 items of the §9 code-audit backlog.
>
> ⚠ **Update 13:40:** first 6a.1 attempt showed the pulse at R57 but **nothing at R61** —
> diagnosing (§6, last entry; triage table in `BENCH_P6_STROBE.md` 6a.1). Do not go on to
> 6a.2 until U5 Q shows the clamp.
>
> 🔴 **Update evening:** a hand-held probe on R61 **sparked on board 2** (§6). From now on:
> attach every lead with the board **unpowered** (PSU and USB off) and probe U5 Q through a wire
> soldered to **R61 pad 1** (§1 rule 6). Board 2 needs the damage check in §6 before further
> use. **Board 1** is acceptable for strobe work after the `BRINGUP_NEW_BOARD.md` fast path.
>
> ✅ **2026-10-06 — cause found:** both R61 pads and R62 have continuity to GND, so U5 Q /
> `GATE-DRV` is shorted to GND at R62's site (§6). Fix the short, confirm R61 pad 1 → GND
> ≈ 1 kΩ, then redo 6a.1. The ≈ 1 kΩ check is now a gate on every board before 6a.1.
>
> ⚠ **2026-10-06, later — superseded by the board-1 block below:** with R62 removed it still reads
> 10 Ω, so R62 was not it (§6). That board is parked with R62 off.

> ### (done 2026-10-05: flashed, smoke 13/13 PASS — §6) RESUME POINT — 2026-10-05: audit-fix build; reflash, smoke-check, then 6a
>
> 🔴 **Reflash first.** Release build **`Oct  5 2026 11:13:55`**, 164232 text / 88496 bss,
> 32/32 host tests, **never run on hardware**. It is the 2026-10-02 strobe 6a/6b dry-test
> firmware plus the 2026-10-05 audit fixes (§6): strict parsing of every numeric CLI
> argument, over-long/too-many-word lines refused, `threshold sweep` steps 1–1000, config
> version check, beam divider cap, `detect` FIFO-overflow warning, stale printed text. It
> replaces the unflashed `Oct 2 2026 09:27:16` image. Strobe current is still impossible.
>
> **Board state:** board 3, PSU **5.20 V / 2 A** on J1, USB on J6, **J3 (LED bank)
> disconnected**, no Pi, no camera. Rail may be up or down unless a step says otherwise.
>
> | # | Command | Expect | Pass |
> |---|---|---|---|
> | 1 | `id` | `built    : Oct  5 2026 11:13:55`; `fw` line ends `+ strobe 6a/6b dry tests (no LED current)` | stamp matches |
> | 2 | `cfg` | `source   : slot A   seq 5   v1`, carrier 104166 Hz, phase 1311 ticks, adc5v scale 1.0620, `hpf sel  : TRACK = 0`, phase mdl fitted | **all identical to 2026-09-18.** 🔴 If `source` says `defaults`: **STOP, do not `cfg save`**, send the output — the new version check rejected the record |
> | 3 | `help` | no `%%` anywhere; `panel pwr` and `panel rdy` on separate lines | as stated |
> | 4 | `gpio xyz` | `ERR: pin 'xyz' -- want 0-47. Nothing was changed.` | ERR, nothing else |
> | 5 | `capture 0 10 1000` | `ERR: mask '0' -- want a channel bitmask, e.g. 0x20 for ch5. Nothing was changed.` | ERR |
> | 6 | `capture trig 5 64 100000 95` | `ERR: pre-trigger '95' -- want 0-90 %. Nothing was changed.` | ERR, no `armed:` line |
> | 7 | `threshold sweep 0 100 65535` | `ERR: steps '65535' -- want 1-1000. Nothing was changed.` | ERR, returns at once |
> | 8 | `cal gain 3100 67`, then `cfg` | `ERR: fraction '67' -- want 0.05-1.0 of full scale (not %). Nothing was changed.` | ERR; `cfg` still shows `u12b gain: 14.5` |
> | 9 | `beam duty 2x` | `ERR: duty '2x' -- want 0-100 %. Nothing was changed.` | ERR; `beam` duty unchanged |
> | 10 | `detect a b c d e f g h` (9 words) | `ERR: more than 8 words -- NOTHING was run.` | ERR |
> | 11 | `capture 0x20 2000 250000` | `# capture mask=0x20 n=2000 rate=250000 overran=0`, 2000 values, `# end` | hex mask still accepted |
> | 12 | beam **off**: `beam freq 5`, then `beam` | `5 Hz requested, 8 Hz actual (TOP=65535, …)`; readback `div 0x0ff0` on both slices | divider 255, not `0x0000` |
> | 13 | `beam freq 104166`, then `beam` | `TOP=1439`, phase **1311 ticks**, `div 0x0010` | restored to the saved carrier |
>
> Then **`BENCH_P6_STROBE.md` 6a** (Step 0 → 6a.3) and **6b** (Steps 1–4). First number that
> matters: **U5's clamp** (`strobe clamptest 200` / `1000`). Send the logs; I analyse them and
> update `STROBE_HW_LIMIT_US_ASSUMED` and the PIO overhead constants if they differ.
>
> **Open in parallel, deferred:** the optical acquisition window (~59.8 ms needed vs 32.768 ms
> at 500 ksps), the §3.7 20-pass set, CR-18 (a real ball impact on the mic). **Before 6c:** the
> 🔴 items of the §9 code-audit backlog.

> **Older resume blocks** — 2026-10-02 (strobe build), 2026-09-28, 2026-08-24, 2026-08-21 and the
> 2026-08-14 firmware handover — are in [`PROGRESS_ARCHIVE.md`](PROGRESS_ARCHIVE.md) §10, verbatim.

---

## 11. Incident 2026-08-17 — foil short across D12, and the latched servo

**Recorded because the board is still in this state, and because the diagnostic that caused
it was a bad instruction rather than a bad execution.**

**Summary:** foil laid over D12 bridged VIR (36 V through R77 10 kΩ) into the TIA summing
node. **U11B was destroyed** — its output sits opposite to what its inputs demand — and
board 1 is out of service until U11 is replaced. The forensic narrative (what happened, the
current budget, the measurements that closed it, why it did not self-recover) is in
[`PROGRESS_ARCHIVE.md`](PROGRESS_ARCHIVE.md) §11, verbatim.

### The part

```
U11 = OPA4323IPWR    TSSOP-14 (PW0014A)    LCSC C22419728
```

**U12 is the same part and is undamaged** — it is the LPF and output amp and worked normally
throughout. Only U11 needs replacing. Three of U11's four sections still test good (U11A
saturates correctly, U11C makes a clean 2.59 V, U11D inverts linearly), but they share one die.

**After replacement, verify:** TP6 and TP7 both **2.59 V** (DMM, beam off), `adc 2 256` ≈ **3212**,
then `hpf test` should still report CONFIRMED / TRACK = 0.

⚠ **CR-15 is unrelated and unchanged.** The TIA saturation above ~3 % duty was measured on a
healthy board *before* this incident, and it is still the actual Phase 3 blocker.


### Lessons worth keeping

1. **Never put conductive material near this board without stating what it must not touch.**
   D12 has 36 V on one leg.
2. **A rail is not a null.** If a test is supposed to *remove* a signal, the pass condition is
   "returns to baseline", not "goes quiet" — a saturated output is also very quiet.
3. Foil over the **photodiode** was chosen over the LED specifically to keep metal away from
   the switching loop. That reasoning was right; the omission was the contact warning.
