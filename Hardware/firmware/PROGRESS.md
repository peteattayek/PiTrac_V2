# PiTrac RP2354 Firmware — Progress & Resume Context

**Read this first when resuming work.** It records what is done, what is next, and the
decisions/measurements that must not be lost between sessions.

Last updated: **2026-08-24** · **Phases 0, 0.5, 1, 1b, 1c and 2 are complete and closed.**
`BENCH.md` and `BENCH_P2_BEAM.md` are finished end to end. The power path and the optical
transmit chain are both proven on hardware. **Q1, Q2 and Q12 answered; Q5, Q7, Q9 closed. Open: Q3, Q4, Q6, Q8, Q10, Q11.**
**Phase 3 is IN PROGRESS on board 2.** §3.1–3.4 are done: ambient rejection proven at ~48 dB,
CR-15 mitigated to 25 % duty, **`demod_phase_ticks` = 1348 at 104166 Hz**, and the phase model
fitted over 80–200 kHz. **The chain delay is 620 ns and the chain is very nearly a pure
delay** (3.5 % drift across the band). **§3.5 is next**, after a `cfg save`.

⚠ **The property that actually matters for a real Pi: an RP2354 reset is a hard power cut.**
Not a reboot. See §2 and §9. Q10 (the GPIO43 pull-down) is a *consequence* of this, and a
minor one — it was written up as a blocker on 2026-07-31 and that was an overstatement,
corrected below.

**Verified build (SDK 2.3.0, toolchain 15_2_Rel1):** UF2 family `rp2350-arm-s` (ARM, not
RISC-V), target chip RP2350, ARM Secure image, USB stdin/stdout. **124 KB flash of 2 MB;
86 KB RAM of 520 KB (16.6 %)** as of 2026-08-24 — the 32 KB capture buffer plus the 32 KB A1
DMA ring dominate the RAM.
No warnings.
Includes phases 0–2 (`safe_state`, `adc_engine`, `power_fsm`, `panel`, `beam`, `cli`).

**Firmware fixes made during Phase 2 bring-up (2026-08-13)** — reflash before any bench work:

| Fix | Symptom it caused |
|---|---|
| `beam_configure()` rounds to **nearest** period | `beam freq 104167` gave TOP=**1438** (104239 Hz, +72 Hz) and broke the exact-30 %-at-level-432 design point |
| `beam_configure()` engages a **clkdiv** below ~2289 Hz | `beam clamp` printed "1 kHz / 500 µs" while producing **2289 Hz / 218 µs** and ~20 % LED duty |
| `beam` prints a **hardware readback** (PWM_EN, funcsel, pad ISO, CSR/TOP/CC, live counter) | Nothing distinguished "firmware not driving the pin" from "probe is wrong" |
| `adc <ch>` rejects `ch > 7` | `adc 32` passed validation — shift-by-≥32 is UB and wrapped to bit 0 |
| `fault clear` fully acknowledges `PS_FAULT` | Ring kept double-blinking while the red LED cleared |
| Pi detection is a **3 s window** + debounced late-detect | A slow Pi was classed absent → next press was a hard power cut |
| `reset`/`bootsel` refuse while a Pi is powered | Either command silently drops the latch = hard power cut |
| `board.h` strobe constants **86/73 → 122/100** | `STROBE_SW_MAX_US` 73 µs was *below* the .md §15 slow-ball requirement |
| **A1: continuous 32 KB DMA ring** (2026-08-14) | The ADC was torn down 10×/s by the supply monitor — would have holed the Phase 3 pre-trigger history |

**Companion docs**

| File | Covers |
|---|---|
| `START_HERE.md` | Beginner walkthrough — build, flash, first commands |
| **`ARCHITECTURE.md`** | **Hardware-offload audit — what runs on PIO/PWM/DMA vs the CPU. A1 ✅ fixed 2026-08-14. Open: A2 (comparator timing → PIO, Phase 4) and 🔴 A7 (PWM slice collision, Phase 6b).** |
| `SETUP.md` | Toolchain detail + manual install path |
| `BENCH.md` | **Phases 0 → 1c** (toolchain, rails, latch, Pi shutdown, panel) — ✅ **all done** |
| `BENCH_P2_BEAM.md` | **Phase 2** — carrier, phase lock, clamp (Q1), duty fidelity (Q2) |
| `BENCH_P3_DETECT.md` | **Phases 3 & 4** — photodiode chain, phase cal, carrier verification, trigger experiment |
| 🔵 **`BRINGUP_NEW_BOARD.md`** | **THE DRIVER for any new board.** Everything per-board, in order, with a sign-off table. See §0.5 |
| `BENCH_P6_STROBE.md` | **Phase 6** — ⚠ the dangerous one. 9 A, linear-mode FET. Read fully before powering. |
| `BENCH_P5_P7_MIC_CAMERA.md` | **Phases 5 & 7** — mic (USB power only, pull forward any time) and cameras |
| **`BENCH_P8_PI.md`** | **Phase 8** — real Pi 5 integration. Gate checklist, Q4/Q7/Q10/Q11, 20-cycle shutdown acceptance, halt telemetry |
| **`NEXT_BOARD_REV.md`** | **Hardware change list for the next spin.** CR-01…CR-09, plus layout design rules. Add to it as bring-up finds things. |

---

## 0. Where things stand

| | Status |
|---|---|
| Board | Assembled and **powered on the bench supply. All rails verified good** — +5V, VIR 36 V, +12 V, +5VA, virtual ground. |
| Approved plan | `C:\Users\ATTAYEKP\.claude\plans\this-folder-contains-a-flickering-wreath.md` |
| Toolchain | **Installed** — VS Code Pico extension, private copies in `%USERPROFILE%\.pico-sdk`. SDK 2.3.0, toolchain 15_2_Rel1, ninja 1.13.2, cmake 4.3.4. |
| Bench equipment | Scope, logic analyzer, DMM, current-limited PSU, **FLIR thermal camera**. See the thermal section in `BENCH.md` for the four points where the FLIR matters. |
| Firmware | Phases 0–2 written, **builds clean**, flashed and running |
| Bench work done | **Phases 0, 0.5, 1, 1b, 1c (2026-07-31) and 2 (2026-08-13) all complete.** Phase 2: phase lock to 0.15 ticks, beam ramped to 30 %, U9 clamp 122.68 µs, duty fidelity flat to 250 kHz, thermals characterised. E9 check passed. All rails verified. All Phase 1 tests pass including fail-safe-through-reset; **the full Phase 1b matrix passes**, covering both timeouts, the RPI5_ON fallback, the escape hatch and the reset invariant. Standby draw **32 mA**. |
| **Phase 3 bench, 2026-08-17** | 🔴 **STARTED, then BLOCKED.** Firmware boots and works: PIO on block 2 / GPIOBASE 16, threshold DAC correct on a DMM, `hpf test` resolved the GPIO33 polarity (**TRACK = 0**, matching the TMUX1219 datasheet), `cfg save` persists. **Blocked at §3.3 by CR-15** — the TIA saturates on beam coupling above ~3 % duty. |
| ⚠ **BOARD STATE RIGHT NOW** | **The DC servo is latched at its positive rail and TP7 sits at 0 V.** Caused by a diagnostic step that laid conductive foil over D12; see §11. **Every rail, D12 and the reference test good and the current budget closes to 0.3 µA — no damage indicated.** Clearing it needs a long power-down. **Read §11 before touching the board.** |

**Bug found and fixed on the bench 2026-07-30 — ✅ fix verified (Phase 1 test 5).**

*Symptom:* with USB and the PSU both connected and the latch closed, unplugging the PSU left
the board running. The latch stayed closed, so the +5 V rail — Pi, boost, beam LED, analog —
was silently back-fed from USB VBUS through **D8, an SS14 rated 1 A**. It looked stable with
no Pi attached, which is exactly what made it dangerous: with a Pi 5 on the header that is a
multi-amp load through a 1 A diode.

*Root cause:* the USB-vs-supply guard ran **only at the instant of latching**. Nothing
re-checked afterwards.

*Fix:* a continuous `V5_MIN_SUSTAINED` (4.90 V) monitor with a 500 ms debounce, active in
every latched state. On trip it raises `FAULT_SUPPLY_LOST` and drops the latch. The debounce
is load-bearing — from Phase 6 the rail is *expected* to sag during strobe bursts (the boost
UVLO is bracketed at 4.74/4.52 V for that), so a bare threshold would trip on every shot.

**Immediate next action:** 🔴 **`cfg save`.** The demod phase and the phase model are in RAM
only; reacquiring them costs a 26 s sweep plus a 128 s model fit. Then `BENCH_P3_DETECT.md`
§3.5. *(This line used to read "fix `ARCHITECTURE.md` A1 first" — A1 and A2 were both fixed
2026-08-14. Only A7 remains open, and it surfaces in Phase 6b.)*

**Phase 2 closed 2026-08-13.** Both open questions answered:
- **Q1 — U9 one-shot clamp = 122.68 µs.** Neither the .md's 113 µs nor the calculated 86 µs;
  K ≈ 1.0, not 0.7. Corrected the strobe constants in `board.h`.
- **Q2 — no duty-fidelity limit up to 250 kHz.** The premise was wrong: ~CLR resets C57
  through an internal low-impedance transistor (~0.1–0.2 µs), not through R68's 123 µs
  *charging* RC. Carrier stays 104.167 kHz.

**Phase 1b closed 2026-07-31.** The full matrix passed against a simulated Pi, including the
four rows added after the FSM changed (detect window, late-detect promotion, its debounce,
and `fault clear` as a full acknowledgement). The whole power path — latch, USB guard,
sustained supply monitor, Pi boot and soft-shutdown handshake, fault handling, panel
indication — is now proven on hardware. Everything in `BENCH.md` is done.

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
   a bench find — see §9. Without the second one, pulling the supply leaves a Pi 5 being fed
   through D8, a 1 A diode.
4. ~~**Phase 1b must pass before a real Pi is ever attached.**~~ ✅ **Passed 2026-07-31.**
   Superseded by: **an RP2354 reset is a hard power cut to the Pi** (§2). `reset` and
   `bootsel` are guarded in firmware; **SW2 is not, and cannot be** — that is operator
   discipline. Tape over SW2 whenever a Pi is seated.
5. ~~**Resolve the Mira220 1.8 V I/O question before Phase 7c.**~~ ✅ **ANSWERED 2026-08-14** — the
   sensor I/O is 1.8 V with no 3.3 V tolerance. Both directions fail; CR-09 is unblocked and
   red. **Still do not connect J4** until the camera board's own schematic confirms whether
   it level-shifts.

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
- [~] **3** Photodiode: **§3.1–3.5 COMPLETE — board 2 and board 3. §3.6b is next.**
      🔵 **Per-board parts (§3.2–3.5) are now driven by `BRINGUP_NEW_BOARD.md` §7–§10**, not by
      this document. What remains here is design validation: §3.6b (Q8) and §3.6 (carrier
      margin), each done **once**, plus §3.7 which is per-board.
      Passing: boot, PIO allocation on silicon, threshold DAC vs DMM, static health,
      **`hpf test` → GPIO33 = 0 is TRACK** (three independent confirmations), `cfg save`,
      **CR-15 mitigated — linear to 25 % duty**, **ambient rejection ~48 dB at 120 Hz**,
      **`cal demod` → 1348 ticks = 620 ns chain delay**, **`cal model` fitted 80–200 kHz,
      residual 0.074°**. Remaining: §3.5 threshold/comparator cross-cal, §3.6b Q8 rail step,
      §3.6 carrier **verification** (no longer a per-board scan — the carrier is fixed),
      §3.7 ramp transits + `cal gain`.
      ⚠ **CR-15 stays 🟡** — the mitigation is mechanical and depends on operator discipline.
      ⚠ **The chopped calibration is over-driven by crosstalk alone** and needs optical
      attenuation over D12; it will need re-tuning on any board or geometry change.
- [~] **4** Trigger source experiment — **firmware complete** (`detect log` / `detect stats`,
      including the bias-vs-1/peak regression). Bench work not started. **No longer gated on
      CR-15** — the front end is linear to 25 % duty — but it needs §3.5 and `detect path`.
- [ ] **5** Microphone. 🔴 **No mic firmware exists** — no onset detection, no veto logic.
      ✅ **But the bring-up IS runnable today** on USB power alone: `adc 7`, `capture 0x80`,
      `tools/scope.py`. *(Pull it forward whenever you are blocked on something else.)*
- [ ] **6** Strobe: 6a dry → 6b gate DAC → 6c LED bank ramp → 6d clamp-with-current.
      🔴 **NO STROBE FIRMWARE EXISTS AT ALL** — see the 8/25 audit row in §6. `strobe_burst.pio`
      is present but **not in `CMakeLists.txt`**, so it is never even assembled. Nothing in
      `BENCH_P6_STROBE.md` can be run until it is written. **A7 must be fixed in the same
      change that adds the gate DAC.**
- [ ] **7** Cameras: 7a loopback → 7b delayed sim → 7c real (needs Pi).
      🔴 **No camera firmware exists** — no handshake, no `t_cam`, no FIRING FSM, no
      `CAM_TIMEOUT`. 🔴 **And CR-09 (Mira220 1.8 V I/O) is unresolved** — get the camera
      board's schematic before connecting J4 to anything.
- [ ] **8** Real Pi integration + clean-shutdown acceptance — `BENCH_P8_PI.md`.
      ✅ **The firmware for 8.0–8.5 exists and is proven** (full Phase 1b matrix). ❌ 8.6 halt
      telemetry is not written. ⚠ **NOT gated on the Q10 rework** — that was demoted to
      optional on 2026-07-31; the gate is phases 2–7. Closes Q4, Q7 (Pi side) and Q11.

---

## 6. Measurement log — **fill this in as you go**

| Date | What | Measured | Notes |
|---|---|---|---|
| 7/29/2026 | RP2354 die revision | **A4**, QFN80, chipid `0xa764f5332ca5ac53` | ✅ Later stepping than the A2 that erratum E9 was documented against. Flash 2048K, ARM Secure, debug enabled. |
| 7/29/2026 | **E9 `pins` check** | **all four read 0** | ✅ GPIO24 / 0 / 8 / 9 floating with nothing connected. **Q5 closed** — no external pull-downs needed. |
| 7/29/2026 | +3V3 rail (USB only) | 3.3V | expect 3.30 V |
| 7/30/2026 | Standby current (+3V3 only) | **32 mA** | ✅ +5V rail down. MCU at 150 MHz + NCP1117 quiescent ≈ 166 mW at 5.2 V. **Baseline** — a later jump here means something is leaking. |
| 7/30/2026 | **Rail-up idle current** | **129 mA** @ 5.2 V | ✅ Latched, no beam, no Pi, ring LED on. ≈670 mW. Dominated by the boost feeding R15/D4 (~40 mA) — see note below. **Baseline for comparison.** |
| 7/30/2026 | **Ring LED current (R48)** | **0.365 V → 7.8 mA** | ✅ Adafruit 481 ring **has internal limiting** — safe to run at 100 % indefinitely. R48 dissipates 2.8 mW. No duty cap needed, no thermal check needed. |
| 7/29/2026 | +5V_IN on USB only | **4.85 V** | ✅ higher than the 4.6–4.7 V predicted. Host VBUS 5.2 V − ~0.35 V (D8/SS14). **Drove the guard change to `V5_MIN_FOR_LATCH` 5.05 V.** |
| 7/29/2026 | +5V_IN on Meanwell/PSU | **5.20 V** | ✅ → ADC1 ≈ 2.60 V |
| | ADC1 scale factor | | `adc5vcal <dmm volts>` — RAM only, lost on reset |
| 7/29/2026 | VIR (J2 fitted, no load) | **36.0 V** | ✅ expect 36 V |
| 7/29/2026 | TP2 +12 V zener | **12.3 V** | ✅ expect 11.4–12.7 V (BZT52B12 window) |
| 7/29/2026 | TP6 +2V5 | **2.59 V** | ✅ **correct.** = +5VA/2 (R75/R76 10K/10K) on a 5.2 V rail. The .md's "2.50 V" is wrong — see Q8. |
| 7/29/2026 | TP7 TIA_Out, beam off | **2.59 V** | ✅ matches TP6 → DC servo working, no ambient saturation |
| 8/31/2026 | **HOLD switch leakage into C81** (board 3) | **~55 pA** (2.2–2.7 mV/s at ADC5) | ✅ `hpf test`. Two independent statistics agree — mean displacement gives 61 pA, p-p less TRACK's p-p gives 50 pA. Board 2 was 52 pA, board 1 ~250 pA. **Board 3 is the quietest measured**, not the 1.16 nA a beam-step droop had wrongly implied. Armed window ~37 s to walk 100 mV |
| 8/31/2026 | **Q8 coupling, HOLD (armed)** (board 3) | 🔴 **×7.43** | 🔴 **§3.6b FAILS.** +75.0 mV rail → +556.9 mV at the comparator input, and it **stays**. The ×7.25 prediction was right to 2.5 %. At a 300 mV threshold a **39 mV** rail step fires the detector |
| 8/31/2026 | **Q8 coupling, TRACK** (board 3) | ✅ **×3.84 peak, decays** | ✅ Same step, back to baseline in 3.4 s. **The HPF works** — the exposure is the armed window and nothing else |
| 8/31/2026 | **HPF time constant, measured** | **τ ≈ 0.75 s** | ✅ From the TRACK decay in the §3.6b capture, against a nominal 0.66 s (R96 2M × C81 330 nF). Within tolerance of two parts |
| 8/31/2026 | **U12B negative clamp** (board 3) | **21 mV** below quiescent | 🔴 Single supply, gain taken to GND (R98.1, R100.1), so quiescent **is** the bottom of its range. A falling rail saturates it and **the detector goes blind** — through 120 mV of droop that ×7.43 says should have moved it 870 mV |
| 8/31/2026 | **Switcher fundamental on +5 V** | **801.1 kHz**, band 762.9–833.1 | ⚠ LA, 1.52 s at 50 MS/s, Welch ×1162. Harmonics at 1602.9 (2.001×) and 2404.8 kHz (3.002×) → a real switcher. **Dithered ±4.4 %**, 0.173 mV. ⚠ **L1 (LM5157) or L2 (RP2350 core buck) — unresolved from a rail measurement** |
| 8/31/2026 | **Beam signature on +5 V**, 25 % duty | **8.67 mV** at 104.2 kHz | ✅ Harmonics 8.67 / 8.20 / 4.30 / **0.33** / 2.50 / 2.39 mV for n=1..6. 🔵 **n=4 is nulled** — `sinc(4×0.25) = 0` — an independent confirmation of the trapezoid model **and** that the duty is a true 25 % |
| 8/31/2026 | **`scan carrier` flatness, 95–115 kHz** | ✅ **σ_noise 4.57–5.08, 10.8 % spread** | ✅ **§3.6 Check 2 PASSES.** `beam_noise_ratio` 2.86–3.41, flat. 🔵 At 110.0 / 112.5 / 115.0 kHz the 7th harmonic lands **inside** the 762.9–833.1 kHz switcher band and those rows are the **quietest** — the collision was tested head-on and nothing folded |
| 7/29/2026 | TP9 LPF out / TP10 demod out | **2.59 V** | ✅ both at virtual ground → demod + both LPF stages healthy |
| 7/30/2026 | R46/R47 junction (DMM) | **2.578 V** | vs 2.600 V ideal = −0.85%, fine for two 1% parts. **Divider healthy; leakage ruled out.** (Q9) |
| 7/30/2026 | ADC1 raw, same instant | **2.4466 V** (code 3036) | −5.1% vs the 2.578 V actually present → the error is inside the ADC, not the divider (Q9) |
| 7/30/2026 | `adc5vcal` scale factor | 1.0627 | should land near the **1.063** compile-time default |
| 7/31/2026 | **RPI5_SHUTDOWN pulse at GPIO43** | **200 ms low** | ✅ Polarity confirmed active-low. **Q7 closed** (firmware side) |
| 7/31/2026 | **GPIO43 through SW2 reset** | **low for the whole hold; 116 ms on the fastest press** | ⚠ Expected — the pad's reset pull-down, held as long as RUN is low. **The width is a measure of your thumb, not the firmware** — see the note below the table |
| 7/31/2026 | **+3V3 at J8.37, driven high** | **3.246 V** | Reference for the Q10 extraction |
| 7/31/2026 | **J8.37 during reset, 20 kΩ emulated pull-up** | **2.07 V** | → X = 35.2 kΩ → **V_pi = 1.34 V with a real Pi — level fails.** Impact minor: every reset opens the latch anyway (§2). 10 kΩ pull-up optional |
| 7/31/2026 | **RP2350 GPIO pad pull-down (derived)** | **~34 kΩ** | Stronger than the 50–80 kΩ assumed. Useful for any future pin that relies on the internal pull |
| 7/31/2026 | **Phase 1b matrix — post-FSM-change rows** | **all 4 pass** | ✅ Detect window (ring breathes 3 s), late-detect promotion, its debounce, `fault clear` acknowledgement. **Phase 1b closed.** |
| | **RUN rising → GPIO43 rising** | | *Optional.* The boot window before `safe_state_init()`. Curiosity now that Q10 is demoted — take it if the scope is already set up |
| | **Pi 5 +5V → header 3V3 latency** | | **Phase 8.3.** Sets `PI_DETECT_WINDOW_MS` (currently 3000, a guess). Measure to the 2.54 V crossing at J8.1 (Q11) |
| | **Pi 3V3 at halt** | | **Phase 8.2.** Decides Q4 — whether `PI_3V3_SENSE` is real or `RPI5_ON` does all the work |
| 7/31/2026 | **`beam freq` rounding bug** | **found and fixed** | `beam freq 104167` gave TOP=**1438** (104239 Hz, +72 Hz) — `150e6/104167` truncated to 1439, then `-1`. Broke the exact-30 %-at-level-432 design point (best was 29.951 %). Now rounds to nearest. **Reflash before Phase 2** |
| 7/31/2026 | **Phase tick scale** (2a check 5) | **6.6667 ns/tick** | ✅ `beam phase 720` measured **4800.00 ns**; 4800/720 = 6.6667, exactly nominal. The 360 and 1439 captures gave 6.6639 / 6.6661 (0.04 %) |
| 7/31/2026 | **2a checks 1-3** | **PASS** | Carrier 186.51 ns high (28.0 ticks), demod 4800.78 ns (50.008 %), both 9599.98 ns period = 104167.2 Hz. **Phase at `phase 0`: mean +0.78 ns, spread 2.0 ns = 0.30 ticks** (histogram {0:50, 2:32} = 1 LA sample). Reference offset ~0 ns. Analysed with `tools/la_phase.py` |
| 7/31/2026 | **Phase-lock scatter** (2a check 7) | **1.02 ns = 0.15 ticks** | ✅ **PASS.** 6 captures with a full `beam freq 104166` reconfiguration between each; spread is half an LA sample. **The atomic enable is proven** — Phase 3 phase calibration has solid ground |
| 7/31/2026 | **2a checks 4, 5, 6** | **all PASS** | phase 360 → 2399.00 ns (−0.15 ticks); **720 → 4800.00 ns (0.00 ticks)**; 1439 → 9592.50 ns (−0.12); return to 0 → −0.79 ns. Monotonic, exact, wraps cleanly |
| | *(note)* | **~−0.9 ns constant bias** | Sub-tick, so not a counter effect — PWM edges land only on 6.67 ns boundaries. LA channel skew or pad delay. Absorbed by the Phase 3 optical calibration |
| 8/13/2026 | **Beam electrical, 25 % duty, COLD** | **3.111 A**, Vf **3.440 V** | TP5 on 0.0320 V (Rds_on 10.3 mΩ). P(LED) **2.72 W**, ballast 0.664 W each |
| 8/13/2026 | **Beam electrical, 25 % duty, HOT (85 °C, 7 min)** | **3.165 A**, Vf **3.403 V** | TP5 on 0.0470 V (Rds_on **14.8 mΩ, +44 %**). P(LED) **2.74 W**, ballast 0.687 W each |
| 8/13/2026 | **Cold→hot drift** | **current +1.7 %, power +0.6 %** | ⚠ **Corrects an earlier prediction of +21 %.** At 3 A most of Vf is I·Rs, and Rs rises with temperature, opposing the bandgap term. Measured Vf tempco ≈ **−0.5 mV/K for the stack**, ~7× smaller than the low-current figure. **No meaningful thermal feedback; power is essentially temperature-independent** |
| 8/13/2026 | **Independent temperature confirmation** | **Q11 Rds_on ×1.44** | ✅ Matches AO3400A's ~+0.7 %/K over ~60 K. Confirms the silicon really reached the FLIR temperature — the small Vf shift is a property of the LED at 3 A, not a measurement artifact |
| 8/13/2026 | **Beam ramp verification** | **2.92–3.02 A across 8→30 % duty** | ✅ Current is set by rail/Vf/ballast, **not duty** — flat over a 3.6× duty change. Ramp steps 1 %/493 ms, period 9.600 µs throughout. True duty at "30 %" = 30.00 % (the +40 ns measured tail is the TP5 recovery artifact, constant at 2 % and 30 %) |
| 8/13/2026 | **Beam thermal: R_th D11→ambient** | **~24.5 K/W** | ✅ **Validated 2026-08-14 — it does describe the LED** (Q12 closed). Predicts 25 % → 89 °C and 30 % → 102 °C from one constant; measured 87.5 and 104. **Two duty points agree:** 25 % → plateau **87.5 °C**; 30 % → **~100 °C**. Ambient 23 °C, heatsink fitted, still air. Model predicts both. τ ≈ **70 s**, ~5 min to settle |
| 8/14/2026 | **A1 DMA ring — on hardware** | ✅ `ring RUNNING`, `5V_IN fresh` | Confirmed via `stat` in IDLE. One DMA channel, RP2350 ENDLESS mode, 32 KB, **32.8 ms history/channel**. The supply monitor now reads ch1 out of the ring and stops the ADC never |
| 8/14/2026 | **D11 base, 30 % duty, 10 min** | **104 °C** (100 °C at 5 min) | ✅ **The trustworthy reading** — IR pointed **sideways at the LED base**, avoiding the domed lens. **Junction = 104 + 3.23 W × (6…9) = 123–133 °C** vs T_j max 145 → only **12–22 °C margin. 30 % is not a sustainable operating point.** ⚠ **Still rising at 10 min** (+4 °C over the second 5 min), so 104 °C is a **lower bound** — there is a slow board/heatsink time constant well beyond the 70 s local one |
| 8/14/2026 | **Heatsink, 30 % duty, 10 min** | **76 °C** | Base→heatsink gradient **28 °C at 3.23 W = 8.7 K/W**; heatsink→ambient **16.4 K/W**. ⚠ **Corrects an earlier ~2 K/W interface estimate** derived from misattributed readings — the interface is **35 % of the total**, not 8 %, so it is worth improving alongside airflow |
| 8/14/2026 | **D11 vs ballast at 30 %** | **same temperature** | ✅ Not a contradiction: D11 has 4× the power (3.23 W vs 0.81 W) through a ~4× better path (25 vs 100 K/W), and they are millimetres apart on shared copper. **Closes Q12** |
| 8/13/2026 | **Beam thermal: heatsink (superseded)** | **68.1 °C** | ⚠ **Corrected** — an earlier 81.5 °C reading was the *board top max*, not the heatsink. With the board hot spot at 85.5 °C the sink is 17 °C cooler, so the interface is carrying heat. **But it also rules out the 53.6 °C D11 reading: D11 cannot be cooler than the sink it feeds.** See Q12 |
| 8/13/2026 | **Beam thermal: heatsink→ambient** | **~22 K/W** | ⚠ **The bottleneck — 90 % of the total.** Better paste buys nothing; airflow buys ~2-3× |
| 8/13/2026 | **R73 (ballast), 25 % duty** | **85.5 °C** | 0.687 W measured → implies **~90 K/W** to ambient. Bare 2512, no heatsinking. Well inside a 3 W part's rating and ~70 °C under its typical 155 °C limit |
| 8/13/2026 | **D11 package, 25 % duty (lens)** | **53.6 °C — ARTIFACT, superseded** | ❌ Read off a **domed lens** (curved, specular, emissivity falls with angle). **Physically impossible**: the heatsink it feeds measured 68.1 °C. True value is **> 68 °C, likely 74–82 °C** → junction 90–107 °C. **Re-read with matte tape — Q12** |
| 8/13/2026 | **Beam-off baseline** | **37.4 °C** | Rails latched, beam off, 23 °C ambient. +14 °C from the boost / R15-D4 shunt alone (see CR-07) |
| 8/13/2026 | **U9 beam one-shot clamp (Q1)** | **122.68 µs** | ✅ **ANSWERED.** Median over 1291 pulses, spread 0.22 µs (0.18 %). **Neither 86 nor 113 µs** — implied K = **0.996**, not the 0.70 assumed. **Concern inverted, and a second problem found:** `STROBE_SW_MAX_US` was **73 µs**, not the 100 µs I had been quoting — *below* the 100 µs that .md §15 needs at 10 m/s, so slow-ball pulses would have been firmware-truncated 27 %. With a 122.7 µs hardware clamp, raised to **100 µs** (18 % margin). Hardware never truncates. Captured on pre-fix fw (2289 Hz, 218 µs commanded — still 1.8x headroom, valid) |
| | **U5 strobe one-shot clamp** | | **Phase 6a.1. Expect ~122 µs** (identical part+RC to U9: 74LVC1G123, 56K, 2.2nF, BOM-confirmed). Tolerance band **109–136 µs**. **Set `STROBE_SW_MAX_US` from U5, not U9** |
| 8/13/2026 | **Beam duty fidelity limit (Q2)** | **none up to 250 kHz** | ✅ **ANSWERED.** At 250 kHz: period 4.0 µs, TP5 low 1.2 µs = **exactly 30 %**. Swept 5→250 kHz at 30 % with no degradation. **The reset is active** — ~CLR discharges C57 through an internal low-impedance transistor in ~0.1–0.2 µs, *not* through R68's 123 µs RC, so there was never a real recovery problem. **Carrier design stands; Q3's search space is unconstrained to 250 kHz** |
| 8/14/2026 | **GPIO36 / GPIO44 PWM slice collision** | **both slice 10A** | 🔴 **Found reading the SDK while writing the threshold DAC.** Same slice AND channel — a fourth pair of the CR-01 kind. `board.h`'s "16 apart" rule is **wrong above GPIO32**, where `slice = 8 + ((gpio>>1)&3)` gives period **8**. Safe today only because GPIO36 stays SIO/UART. **Never put GPIO36 on PWM** or the Pi link and the comparator threshold share one compare register |
| 8/14/2026 | **GPIO33 not driven low on rail-down** | **fixed** | 🔴 `safe_state.c` handles boot because SEL high into an unpowered U14 back-feeds +5VA — but `hpf track` then `off` walked straight back into it. `PS_FORCE_OFF` now calls `detect_hpf_safe_off()` |
| 8/14/2026 | **U15 has no hysteresis** | **netlist-confirmed** | 🔴 No resistor from `D_Comparator` to pin 3 anywhere. Chatter on slow edges is a board property → **CR-13**. Firmware counts fragments and reports a lower bound rather than a fake transit |
| 8/14/2026 | **LM393 CM ceiling vs U12B swing** | **~3.5 V vs ~5.2 V** | 🟡 U15 runs from the **digital** +5 V; U12B is rail-to-rail on +5VA → **CR-14**. ADC5 saturating at 3.3 V is a conservative early warning. **Measure the real ceiling** |
| 8/14/2026 | **LPF f0 from fitted values** | **15.39 kHz** | 4.7 k / 2.2 nF. The schematic's 15.9 kHz annotation is stale (3 %) |
| 8/14/2026 | **TP8 settle time** | **2.62 ms dominant pole** | The two RC sections load each other — real poles 2.62 ms and 0.382 ms, not two 1 ms poles. `DAC_SETTLE_MS` = 20 ms. The doc's 10 ms was ~4τ ≈ 20 threshold codes of error |
| 8/14/2026 | **R98, not R100, is the DNP part** | **gain is continuous** | R98 ∥ R100 from GND to U12B IN−; R100 (2 k) is fitted. Left unpopulated so its **value** could be picked after measuring. `cal gain <peak>` solves it and returns an E24 value |
| 8/14/2026 | **All 30 `board.h` pins + 5 ADC channels vs netlist** | **all correct** | ✅ Checked mechanically, not by eye. Every pin maps to a real named net; none to an unconnected or missing node |
| 8/14/2026 | **DNP inventory, all 10 sheets** | **R98, C9, R11 only** | ✅ Complete. Everything else flagged is a mounting hole or off-board. **No second tuning option exists anywhere** |
| 8/14/2026 | **+2V5 divider is FILTERED** | **C66 1 µF → f_c 31.8 Hz** | 🔵 R75∥R76 = 5 kΩ × 1 µF, τ = 5 ms. Then a U11C buffer, then R79 10 Ω into C71 10 µF (1.6 kHz). **The virtual ground tracks the rail only below ~32 Hz.** §3.6b's beam step is effectively DC → measures the fully-coupled worst case, so **×7.25 stands for that test**. But the "will false-trigger on its own strobe" warning is **pessimistic**: a ~200 µs strobe transient is attenuated ~150×, a ~5 ms burst envelope ~6×. **Measure in Phase 6** |
| 8/14/2026 | **DC servo corner, derived** | **2.27 Hz** | ✅ R83 1M × C73 330 nF = 0.48 Hz, × R80/R78 (470K/100K = 4.7) = 2.27 Hz — **reproduces the schematic annotation exactly**, which validates the whole netlist parse. Justifies the 20 Hz chop: 0.6 % loss vs **8.9 % at the doc's original 5 Hz** |
| 8/14/2026 | **TIA feedback pole** | **Cf 0.5 pF → 677 kHz** | 🔵 C68 **in series with** C70 (1 pF + 1 pF) — sub-pF parts are unbuyable and parasitics would dominate. Phase lag **8.7° at 104 kHz → 20.3° at 250 kHz**. **This is the concrete source of the dispersion that makes a single phase number fail across the scan range**, and a testable prediction. ✅ **The 8.7° was confirmed exactly on 2026-08-24** — measured total chain lag at 104 kHz is 23.2°, of which the TIA's share is 8.74°. ⚠ **But the conclusion drawn from it was wrong.** The dispersion is real and in the predicted direction, yet tiny in absolute terms: the chain delay is nearly flat across 80–200 kHz. ⚠ **Do not quote the droop figure** — see the 8/25 row: it gave −3.5 % once and −0.05 % on the re-run, and the attribution of a quarter of it to this pole does not survive that. A single phase number does fail across the scan range, but for a **geometric** reason — see the 8/24 rows. 🔴 **"If it reports `pure_delay`, distrust the measurement" was bad advice, was written into a firmware test, and made that test report the opposite of the truth for two sessions.** The chain IS a pure delay, to within 0.78° |
| 8/14/2026 | **ADC1 has no filter cap** | **R46/R47 + pin only** | Consistent with Q9 (50 kΩ into an ADC wanting ≤10 kΩ, no reservoir for the S/H). CR-03 fixes it |
| 8/28/2026 | **Carrier frequency** | **104.1667 kHz — FIXED BY DECISION** | 🔵 Not scanned per board. See §8. |
| | **LM5157 boost SW frequency, board 1 / 2 / 3** | | 🔴 **never recorded on any board.** It is what the carrier's −9.6 %/+7.1 % harmonic margin has to cover. Scope it at `BENCH.md` 0.5d |
| | `demod_phase_ticks` | | from `cal demod`. Persisted by `cfg save` |
| 8/19/2026 | ✅ **CR-15 MITIGATED — linear to 25 % duty** | **no saturation at any duty** | 🟢 **PHASE 3 UNBLOCKED.** Duty sweep with good baffles, hands clear: 2 % = 3084-3253; 4 % = 2750-3285; 8 % = 2373-3374; 12 % = 1891-3418; **25 % = 2076-3606**. **No rails, no 4095.** Pulse depth below the off-level fell **2.58 V → 0.132 V at 2 %, ~20×**. Servo signature tracks the model (off-levels 2.617/2.641/2.688/2.733/2.876 V vs 2.589 beam-off), and the excursion **stops growing after 12 %** (1527 vs 1530 codes) — the pulse settles past the 235 ns TIA τ and amplitude is then set by *peak* photocurrent, which Phase 2 independently showed is flat vs duty. ⚠ Low-duty swings **understate**: 500 ksps ÷ 104.1667 kHz = exactly 4.8, so sampling is coherent over only **24 phase points** and the 192-384 ns pulse is rarely caught; **25 % is trustworthy** (2.4 µs pulse > 2 µs interval). **CR-16 now binds tighter than CR-15**: 0.39 V to the ADC ceiling vs 1.67 V to the op-amp rail |
| 8/19/2026 | ✅ **CR-15 mechanism: OPTICAL, and largely the OPERATOR'S HAND** | **confirmed with better baffles** | ✅ The coupling is overwhelmingly optical — the resistor substitution was not needed. **A large part was a hand held in front of the board reflecting the LED back into D12.** That is what made every earlier baffle comparison uninterpretable: the no-baffle control moved **339 → 19 codes** at 2 % duty between sessions with nothing nominally changed, and a hand at varying distance is exactly a variable reflector. **Keep hands and torso clear of the optical path while capturing.** Strengthens the case for the **aperture / field stop** mitigation, which rejects off-axis returns by construction rather than relying on operator discipline. ⚠ **Two limits now apply at 25 % duty and the ADC one binds first:** ADC top clip needs ΔV < **2.22 V**, op-amp bottom rail needs ΔV < **3.81 V**. Measured ΔV was ~3.8 V, so a **~1.7× crosstalk reduction** puts ADC2 fully in range — a much lower bar than the 5-10× previously discussed |
| 8/19/2026 | ⚠ **ADC ceiling invalidated half of CR-15** | **4095 = 3.3 V, not the rail** | 🔴 **CR-16.** The chain runs on +5VA (rail-to-rail, 0-5.2 V) into a **3.3 V ADC**, so ~36 % of every signal is invisible and `4095` means only *at least 3.3 V*. Logic analyser at 50 MS/s confirms **TP7's top is clean** with >1 V in hand; only the **bottom** rails, and that part is real. **Why it clips low and not high:** the TIA is inverting so the pulse goes down, and the servo forces the *mean* to 2.59 V, pushing the off-level **above** it — `(1-f)·V_off = 2.59`. At 2 % duty (192 ns pulse + ~700 ns tail, f = 0.093) that predicts **V_off = 2.86 V against 2.85 V measured**. V_off reaches ~3.8 V at 25 %, over the ADC ceiling but 1.4 V below the rail. ⚠ **Clipping also droops the ADC reference** — 1.6 mA through R82 into R27's 33 Ω is ~53 mV — so beam-on ADC2 data is unreliable near a clip. **Bottom clipping still costs detection:** the demodulator correlates across the whole period, so a ball reflecting extra light during a railed interval produces no extra output |
| 8/19/2026 | 🟡 **No test point on the comparator input** | **CR-17** | `Net-(U12B-OUT2)` is what U15 thresholds and what the ADC refinement measures — **the only node in the detect chain with no TP.** TP8 gives the threshold but not the signal. Probe via **R102 pad 2** or R101 pad 2, identified by continuity to U15 pin 3. ADC5 is this node through R102 + D14, i.e. a copy **truncated at 3.3 V**, so without a TP the true comparator input is unobservable by firmware *or* on the bench |
| 8/18/2026 | ✅ **Board 2 quiescent TIA_Out** | **3213.3 codes = 2.589 V, 2.4 mV p-p** | ✅ `capture 0x04 400 500000`, beam off, VIR 36 V. Range 3212-3215 = **3 codes = +/-1.5 LSB**, i.e. the ADC's own noise floor. Matches the expected 2.59 V exactly. **Also closes the earlier `adc 2` wander** (3109/3200/3177, 91 codes): that spread was between calls *seconds apart* and was measured with **VIR parked at 27 V**; within a single 0.8 ms window the node is rock steady. **Consequence for CR-15: the front end is provably quiet and centred before the beam, so the whole 339-3672 excursion at 2 % is beam-induced.** The pulse consumes 2.58 V of the 2.59 V downward headroom — **~99 %** — which is why 3 % tips it over |
| 8/18/2026 | ✅ **CR-15 CONFIRMED on a second board** | **ceiling 2 % (board 1: 3 %)** | 🔴 **The saturation is a DESIGN property, not board 1's assembly.** Board 2, independently built, identical sweep, no baffle: 2 % = 339-3672 **linear**; 3 % = **8**-3680 railed; 4 % = 5-3775; 6 % = 4-3981; 8 %/10 %/12 %/25 % = 3-**4095** both rails. Crosstalk swing at 2 % agrees closely (2.69 V vs board 1's 2.51 V). **Two boards saturating within one step of each other rules out an assembly artifact** — this was the entire reason for bringing up board 2. ⚠ Confirms the **magnitude**, not the **mechanism**: optical vs beam-current coupling is still open, and the **resistor substitution** (1.1 Ω non-inductive in D11's footprint) is the test that settles it |
| 8/18/2026 | 🔴 **PSU current limit parked VIR at 27 V** | **bench trap, not a board fault** | A 5.2 V supply left at **0.3 A** could not deliver the startup inrush into VIR's bulk caps (~100 uF to 36 V = ~65 mJ, amps for milliseconds). The supply went into CC, **the boost never finished soft-start and parked at 27 V.** Set to 2 A, **both boards now read 36 V.** ⚠ **Nothing downstream revealed it:** +5V_IN read 5.207 V, the latch guard passed, the supply monitor never tripped, and the CLI/LED/detect chain all worked. Idle current was **80 mA vs ~129** — which reads as less load, not a fault, and is explained by the shunt drawing (27-12)/4K7 = 3.2 mA instead of 5.1. **Two boards showed it at once, which mimicked a BOM error.** Firmware could not have caused or seen it — **no MCU pin touches U1**. Strengthened **CR-08** (no VIR sense) from 🟢 to 🟡 and added the PSU limit as step 1 of `BRINGUP_NEW_BOARD.md` |
| 8/17/2026 | **U9 beam one-shot fidelity at 10 kHz** | **exact, 210/210 edges** | ✅ Saleae, 50 MS/s, 21 ms, GPIO31 + TP5. Both channels **100.000 µs period, sd 1.9 ns**; GPIO31 HIGH **25.000 µs** → TP5 LOW **25.06 µs** (TP5 is the MOSFET drain, so low = conducting). **60 ns of error through the one-shot, no double-pulsing.** ⚠ This was taken to test my claim of *two* disturbance events per PWM period in an earlier ADC2 capture — **that claim was a miscount** of a pasted 1000-line column (~17 counted vs ~42 actual). There is one event per period. The drive is exonerated |
| 8/17/2026 | 🔴 **TIA saturates on LED crosstalk** | **linear only ≤ ~2-4 % duty** | 🔴 **PHASE 3 BLOCKER — `NEXT_BOARD_REV.md` CR-15.** `capture 0x04` at 500 ksps: 2 % duty → TIA_Out 600-3709 (linear); **10 % and 25 % → 2-4095, railed both ends**. ⚠ **Mechanism NOT established.** The duty scaling does NOT prove it optical — it is fully explained by TIA settling (pulse shorter than the 235 ns feedback τ), which applies identically to an electrically coupled pulse. **Shading and a baffle both changed nothing** (⚠ material unverified — most black plastics are near-transparent at 850 nm), and the 10 kHz width test shows the disturbance is **sustained through the 25 µs conduction period, not an edge spike** — which kills pure dV/dt coupling but does NOT separate optical from current-loop coupling, since 3 A flowing for 25 µs droops the rail for exactly that long. Next discriminator: **aluminium foil over D12** (opaque at 850 nm, and far from the switching loop so it cannot perturb the electrical path). Candidate electrical routes (rail ripple via FB2 into U11's PSRR, or VIR ripple through D12's junction capacitance into the summing node). Crosstalk swing at 2 % = 2.50 V = **~5.3 µA** through R80 470K — **96 % of the ±2.6 V headroom at the lowest usable duty**. The DC servo compounds it by re-centring the mean as duty rises. **Knock-on:** full-scale square into the demod → the LPF's OPA4323s slew-limit (1.5 V/µs needs 3.3 µs for 5 V vs a 9.6 µs period) → **a slew-limited filter stops filtering** → **313 mV of carrier on ADC5** vs an 18 mV small-signal prediction. Fix is an **optical baffle between D11 and D12** — crosstalk and signal are the same 850 nm light, so it cannot be filtered electrically |
| 8/17/2026 | **Ambient flicker at ADC5, beam OFF** | **162 mV p-p, ~120 Hz** | 🔵 `capture 0x20 4000 125000` under ordinary LED room lighting. 201 codes, ramp-then-collapse (rectified LED supply, not sinusoidal). Works back to ~5.6 mV at TP7 = **~12 nA photocurrent ripple**. **The DC servo nulls DC only** — its corner is 2.27 Hz — and with the beam off the demod is static (GPIO39 low → U13 passes TIA_Out), so there is **no lock-in rejection either**; gain TP7→ADC5 is ×29. For scale, a 100 mV TP9 bump is ~1.45 V at ADC5, so flicker is ~11 % of a nominal ball. **`hpf test` must be run shaded.** ✅ **Use this as the lock-in proof:** repeat with the demod running and it should collapse ~64 dB |
| 8/17/2026 | **HPF SEL polarity (GPIO33)** | **GPIO33=0 is TRACK** | ✅ **CLOSED. Confirmed two independent ways.** TMUX1219 truth table (SEL=0 → S1, and S1 is the R96/GND leg), and `hpf test` **shaded**: level 0 gave +22.8 / +19.1 mV (consistent, near zero) vs level 1 at +67.8 / +88.8 mV — displaced *and still climbing between visits*, which is a floating node continuing to integrate. Separation 3.7×. **The bench doc's original `gpio 33 1` = TRACK was backwards.** `HPF_SEL_TRACK` = 0. ⚠ **The first two runs were UNSHADED and reported ORDER-DEPENDENT / INVERTED — see the flicker row above; the answer only appears with the photodiode covered** |
| 8/17/2026 | **HOLD baseline drift (superseded)** | **~11 mV/s at ADC5** | TRACK ~1.9 mV/s, ratio ~6. Implies **~250 pA** of TMUX1219 off-leakage into C81 — **4× lower than the 1 nA I predicted**, so the test's separation was 6× not the ~20× expected and the 4× conclusiveness bar only just cleared. Lengthen the window rather than tightening the bar: a floating node's drift grows with time, a pinned one's does not |
| 8/17/2026 | **Phase 3 bring-up steps 1-4** | **all pass** | ✅ Boots; PIO on **block 2, GPIOBASE 16, SM 0** (A2 allocation confirmed on silicon); `D_Comparator` reads high with the rail down as predicted (U15 unpowered, R103 pulls up); threshold DAC correct on a DMM at 0/25/50/75 %; `adc 2` = **2.5884 V** vs the expected 2.59; 5V_IN 5.207 V, ring RUNNING |
| 8/21/2026 | ✅ **`hpf test` on board 2 — CONFIRMED, and clean** | **2.9× separation** | ✅ GPIO33=0 → +0.0093 / +0.0092 V; GPIO33=1 → +0.0271 / +0.0269 V. **The repeats are the result worth recording, not the ratio:** the two visits to a level agree to **0.1 and 0.2 mV** against **17.8 mV between levels**, so `order_effect` is false by a factor of ~100. Compare the earlier ORDER-DEPENDENT runs at 1.2× and 1.4×. **Polarity matches the compiled `HPF_SEL_TRACK` — no code change.** Third independent confirmation (board 1 bench, TMUX1219 truth table, board 2 bench). Conditions that made it work: photodiode shaded, board at steady temperature, hands clear for the full 32 s |
| 8/21/2026 | **HOLD leakage, board 2** (supersedes the 8/17 row) | **≤ 2.3 mV/s at ADC5 → ~52 pA** | 🔵 HOLD p-p **8.9 mV** over a 3 s window vs TRACK's 0.8–2.4 mV noise floor, so drift ≤ (8.9−2)/3 ≈ 2.3 mV/s. Back through ×14.5 and C81 330 nF → **~52 pA** of TMUX1219 off-leakage, vs board 1's ~250 pA. Both are inside TI's spec for a precision switch and leakage roughly doubles per 10 °C, so **treat this as a 50–250 pA range, not a constant** |
| 8/21/2026 | ✅ **F6 CLOSED — how long HOLD stays usable** | **tens of seconds** | ✅ The open question from planning was whether a floating C81 drifts too fast to arm ahead of a shot. At 2.3 mV/s it takes **~43 s** to walk 100 mV at ADC5 (~9 s even at board 1's 250 pA). **A ball transit is 1–10 ms → ≤ 23 µV of walk.** The concern was wrong by three to four orders of magnitude. Arming well before a shot is safe; only an arm-and-forget of many seconds needs thought |
| 8/21/2026 | ✅ **ADC5 noise floor, beam ON 25 %, TRACK** | **σ 2.19 mV, mean 9.2 mV** | ✅ `capture 0x20 400 500000`. **The mean lands on the TRACK settled offset (9.3 mV from `hpf test`) to within 0.1 mV** — the HPF is removing the entire static beam-coupling DC, which is exactly its job and the first direct evidence it works with the beam running. p-p 12.1 mV, no spectral structure above ~1.3 codes. **This is the σ_noise that denominates every `scan carrier` SNR figure** |
| 8/21/2026 | ⚠ **Second capture unexplained — 6.4× higher** | **mean 58.7 mV, σ 4.52 mV** | ⚠ **The two captures were not labelled lights-on vs lights-off, so the ambient-rejection figure cannot be extracted.** Something added ~50 mV of in-band signal and doubled σ. **Do not read this as a rejection failure** — the HPF's τ is 0.66 s, so *any* scene change within ~2 s of the capture (lights switching, a hand withdrawing) leaves the node still settling and would produce exactly this. Re-run labelled, with ≥3 s of stillness before each capture |
| 8/21/2026 | 🔵 **2nd harmonic of the carrier at ADC5** | **~5 codes ≈ 4 mV at 208.33 kHz** | 🔵 Present in the 58.7 mV capture, absent from the quiet one. The top **two** FFT bins are 207.50 and 208.75 kHz — the pair straddling exactly 2 × 104.166 kHz — which is hard to get by chance. **2f is the signature of genuine carrier-frequency light being demodulated** (chopped DC ambient lands at f and *odd* harmonics, not 2f). ⚠ **It cannot have come down the signal path:** the 4th-order 15.39 kHz LPF sits *after* the demodulator and gives −90 dB at 208 kHz, which would need 9.4 V pre-LPF to leave 4 mV at ADC5. So it is **coupling around the filter** — PCB or supply, from the demod clock. Harmless at 0.12 % of full scale; **watch it, do not raise a CR yet.** Confirm with a longer 500 ksps capture (finer bins) before drawing conclusions from one 400-sample window |
| 8/21/2026 | ⚠ **Correction to my own step-2 instruction** | **800 µs cannot see 120 Hz** | ⚠ I specified `capture 0x20 400 500000` as the ambient-rejection test. **That window is 800 µs — a tenth of one mains half-cycle** — so it cannot resolve flicker even in principle, and the 8/17 baseline it was meant to be compared against was taken at `4000 125000` (32 ms). **For flicker use ~200 ms**: `capture 0x20 2000 10000`. Keep the 500 ksps capture as well, but only for σ and carrier harmonics |
| 8/21/2026 | ✅ **LOCK-IN AMBIENT REJECTION PROVEN** | **~48 dB at 120 Hz** | ✅ `capture 0x20 2000 10000` (200 ms = 24 mains cycles), beam on 25 %, TRACK, labelled lights-off vs lights-on. **120 Hz component: 0.12 mV dark, 0.32 mV lit.** Against the beam-off baseline of **162 mV p-p** that is **~250×, ≈48 dB** of rejection. (I had predicted ~64 dB; the shortfall is worth noting but the residual is ~7 % of σ and not a limiting term.) **Mains flicker is a solved problem** — this was the central premise of the whole synchronous-detection design and it is now measured, not assumed |
| 8/21/2026 | ✅ **True quiet-baseline σ at ADC5** | **0.55–0.65 mV** | ✅ The 1344/2000 (dark) and 1418/2000 (lit) samples that sit at 9–10 codes have σ = **0.81 / 0.68 codes**. That is **3–4× quieter than the 2.19 mV** the 800 µs capture suggested, because that short window happened to sit inside a burst. **This is the floor the design achieves when nothing is happening** |
| 8/21/2026 | ⚠ **BUT the noise is bursty, not Gaussian** | **16–21 % of samples >20 codes, peaks to 72** | ⚠ **σ(all) is 6.16 mV dark / 9.15 mV lit — 10× the quiet baseline — and it is all impulsive.** Bursts last tens of ms with quiet gaps between; spectrum is **5–30 Hz, monotonically falling**, and 3× larger with the lights **on** (10 Hz: 9.74 vs 3.32 codes). **σ_noise is therefore NOT yet cleanly measured**, and `scan carrier`'s SNR denominator would currently include whatever this is. 🔴 **Resolve before `scan carrier`** |
| 8/21/2026 | 🔴 **`cal demod` ran SATURATED — 84 % of the sweep against the ADC rail** | **response is a square wave, not a cosine** | 🔴 The sweep sits at **±4086 codes** for 54 of 64 points (full scale 4095). Fitted **amplitude 5128 codes — above full scale, which is impossible for a real signal** and is the signature of a square wave, whose fundamental is 4A/π = 1.27A. Measured **h3/h1 = 0.306** against an ideal square's 0.333. **Root cause: too much light.** ⚠ U12B is already at its **minimum** gain of 14.5 (R98 is DNP and fitting it only *raises* gain), so the only lever is optical — move the reflector back, use a grey card not white, or ND-filter D12 |
| 8/21/2026 | 🔴 **FIRMWARE DEFECT — `h2_ratio` is blind to clipping** | **h2/h1 = 0.012 PASSED a 0.25 bar** | 🔴 **The purity check could not detect the one failure it was written for.** Symmetric clipping produces **only odd harmonics** — an ideal square wave has h2/h1 = **0 exactly**. So the even-harmonic test was looking in the one place clipping is guaranteed to leave no trace. ✅ **Fixed:** added `h3_ratio`, a direct `sat_frac` (limit 10 %), plus an amplitude sanity check. Each of the three catches this sweep alone. ⚠ **Two of those bars were superseded on 2026-08-24** — `h3_ratio`'s fixed 0.15 sat *below* a clean signal at any duty under 20 %, and the amplitude check compared the fundamental against full scale when a trapezoid's fundamental is 1.146× its peak. Both are now duty-aware |
| 8/21/2026 | 🔴 **FIRMWARE DEFECT — the quadrature null was printed but never enforced** | **null = −4086 where ~0 was required** | 🔴 **The check existed, ran, reported failure in plain text, and the code committed the phase anyway.** `valid` was `amplitude > 1 && h2_ratio < 0.25` — `quad_null` was computed, printed, and dropped. ✅ **Fixed:** now enforced at \|null\| < 15 % of amplitude. ⚠ Also fixed a second bug in the same lines: the null was sampled 90° from **`best_i`, the grid argmax**, which on a clipped sweep is degenerate (dozens of points tie) — so it was measured from an arbitrary index. Now anchored to the **fitted** peak |
| 8/21/2026 | ✅ **The phase answer is probably right anyway — 66 ticks** | **two independent confirmations** | ✅ **Symmetric clipping preserves zero crossings**, so the phase survives even though the amplitude is meaningless. Two checks agree: (a) the raw sweep crosses zero at **427 and 1147 ticks**, and a peak at 66 predicts **426 and 1146**; (b) the independent 5-point `cal model` evaluates to **65.4 ticks** at 104166 Hz against the fit's 66.4. ⚠ **Re-run unsaturated to confirm** — this is a lucky escape, not a valid measurement |
| 8/21/2026 | ✅ **SUPERSEDED — `cal model`: "NOT a pure delay" was a TEST ARTIFACT** 🔴 **read the 8/24 rows instead** | **θ(f) = −0.6769 + 9.783e−6·f − 5.243e−12·f²** | ✅ The netlist predicted the filter poles would contribute curvature and the firmware was told to distrust a `pure_delay` verdict. It reports **"NOT a pure delay"**, residual **0.0080 rad (0.46°)** over 80–200 kHz. Evaluates to **4.1° at 80 kHz, 16.3° at 104 kHz, 61.3° at 200 kHz**. ⚠ **Do not read the coefficients physically** — the −0.6769 rad constant is −38.8° at DC, which no real chain has. It is a local interpolant over a narrow band and the terms trade off against each other, which is exactly why the firmware refuses to extrapolate. ⚠ **Every model point was saturated too; re-run after fixing the light** |
| 8/21/2026 | ✅ **BURST SOURCE CLOSED — it is the beam returning off the room** | **cover D11 → σ 10.30 mV falls to 2.91 mV** | ✅ **Three captures with D11's output aperture covered, everything else identical.** σ: **6.49/10.37/14.03 → 2.62/3.39/2.91 mV**. Peak: **77 → 33 codes.** And the distribution changes character completely — **excess kurtosis −0.3 / −0.3 / +3.0 (open) → +0.1 / +0.5 / +0.1 (covered)**, i.e. from wildly non-Gaussian to **essentially Gaussian**. ✅ **Stop the light leaving the board and the bursts stop. The hypothesis is confirmed.** |
| 8/21/2026 | 🔵 **The negative kurtosis was the tell, and I missed it first time** | **−0.3 means SIGNAL, not noise** | 🔵 Impulsive *noise* has **positive** excess kurtosis (heavy tails). Two of the three open captures ran at **−0.3**, which is a *flattened / bimodal* distribution — the signature of a **modulated signal filling the range**, not of noise. **The detector was working correctly the whole time**; it was reporting light. ⚠ I called this "bursty noise" for two rounds before computing the fourth moment that would have said otherwise on day one |
| 8/21/2026 | ⚠ **σ_noise is not a constant — it is set by the optical background** | **0.65 / 2.91 / 6.5–14 mV** | ⚠ Three different numbers on one board in one session: **0.65 mV** (quiet population, nothing returning), **2.91 mV** (D11 covered — the cover itself reflects light back into D12 at close range, and shot noise goes as √I), **6.5–14 mV** (open to the room). 🔴 **Consequence: there is no bench σ_noise worth quoting.** The 2.91 mV figure is a usable *bench reference* only. ⚠ **The "must be run in the final geometry" conclusion was correct while §3.6 RANKED carriers; since the carrier was fixed on 2026-08-28 it no longer binds** — §3.6 now compares nine rows inside one run under identical optics, where a common background cancels |
| 8/21/2026 | ✅ **Operator motion RULED OUT as the burst source** | **bursts got *worse* with the operator away** | ✅ Three repeat captures, lights on, operator as far from the beam as possible: **52.6 %, 43.0 %, 19.6 %** of samples above 20 codes, against 20.6 % with them standing at the bench. Peaks to **77 codes**. The motion hypothesis predicted the opposite, so it is dead |
| 8/21/2026 | ✅ **The quiet floor is the reproducible part** | **σ = 0.61–0.70 mV across all 5 captures** | ✅ 0.83 / 0.75 / 0.87 codes here, 0.81 / 0.68 earlier — **both lighting conditions, operator near and far.** This is the genuine electronic noise floor and it is rock solid. ❌ **σ(all), by contrast, is NOT reproducible: 6.49 / 10.37 / 14.03 mV** — a 2× swing between captures taken seconds apart. Whatever drives the bursts varies on a seconds timescale |
| 8/21/2026 | 🔵 **The bursts are strictly one-sided POSITIVE** | **min = 9 codes in all three, no floor pile-up** | 🔵 Checked specifically for a bipolar signal clipped at the ADC's 0 V rail — **it is not that.** Zero samples below 8 codes against a 9–10 code baseline, while excursions reach 77. **More light only.** Electrical coupling could push either sign; **an optical return can only add light.** Combined with the operator result this points at **the beam reflecting off the room and returning to D12** — which the lock-in passes faithfully because it is genuinely modulated at the carrier. ⚠ **Not yet proven; see the discriminator row** |
| 8/21/2026 | ⚠ **Why "operator further away" made it worse** | **hypothesis: they were blocking the return** | ⚠ Standing at the bench, the operator's body blocks the beam's path into the room; stepping clear lets it reach walls and equipment and come back. Fits the sign, fits the direction, and fits the second-to-second variability if anything in the room moves at all. 🔴 **Discriminator: cover D11's *output aperture*** (tape-wrapped foil or the carbon-black baffle) so no light leaves the board, and repeat. Optical return vanishes; electrical coupling does not. ⚠ **Cover D11, NOT D12 — and nothing bare-metal near either.** See §11 |
| 8/21/2026 | 🔵 **If it IS room return, it may not be a fault at all** | **reframing worth holding** | 🔵 The detector's job is to report light modulated at the carrier. A room that reflects some back is **background, not noise** — and in the final installation the beam points into a defined space with its own background. That argued `scan carrier` must run **in the final geometry and lighting** — true while it ranked carriers, and superseded by the 2026-08-28 fixed-carrier decision. The reframing still stands: this background legitimately belongs in σ_noise, and it does **not** argue for chasing it away on the bench |
| 8/21/2026 | 🔵 **Superseded: the 5–30 Hz "envelope periodicity"** | **artifact of my own window** | ⚠ I reported burst-rate lines at 5–30 Hz. With 40 envelope points over 200 ms the bin spacing **is** 5 Hz, so the "5 Hz line" is just the lowest bin — slow drift, not periodicity. **The window cannot resolve burst rate at all.** A longer capture (16384 samples at 10 ksps = 1.6 s) is needed before any claim about periodicity |
| 8/21/2026 | 🔵 **Two candidate causes for the bursts (superseded — see above)** | **motion now ruled out** | 🔵 **(a) Motion in the beam** — the lock-in passes anything modulated at the carrier, so a hand, body or fan blade reflecting 850 nm *is* a genuine detection and the bursts being worse with the lights on fits an operator who can see to move. **(b) Room-light switching harmonics near the carrier** — ⚠ **the lock-in's rejection is frequency-selective, not universal.** It rejects 120 Hz by ~48 dB but does nothing about ambient energy *near 104 kHz*, and LED drivers, electronic ballasts and SMPS all have harmonics up there. A harmonic landing within a few Hz of the carrier demodulates to exactly this: a slow 5–30 Hz beat. **Discriminator: lights on, beam on, scene genuinely static, three repeat captures.** Reproducible statistics → the lights. Wildly varying → motion |
| 8/21/2026 | 🔵 **Aliased carrier feedthrough at 10 ksps** | **~2.6 mV at 4165 Hz** | 🔵 Expected and harmless, but do not mistake it for signal: at 10 ksps the 104166 Hz carrier aliases to **4166 Hz** and 2f to 1668 Hz. Clearly present in the dark capture (3.25 codes at 4165 Hz). **10 ksps is the right rate for flicker and the wrong rate for anything carrier-related** |
| 8/21/2026 | ⚠ **Board 2 beam thermal, 25 % duty, 10 min** | **base 98 °C, heatsink 78 °C** | ⚠ **~10 °C hotter than board 1 at the same duty** (87.5 / 68.1 °C). Junction = 98 + 2.69 W × (6…9) = **114–122 °C**, margin to 145 °C is **23–31 °C** vs board 1's 33–41 °C. **Acceptable, not comfortable.** ✅ **The interface is fine — the base→heatsink gradient is 20.0 K vs board 1's 19.4 K**, so the mounting and paste are as good. The whole difference is **heatsink→ambient**: R_th(base→amb) = 27.9 K/W vs 24.0. ⚠ **Two things unmeasured that decide whether this matters:** (a) **room ambient** — at 33 °C instead of 23 °C the two boards are *identical* and there is nothing to explain; (b) **it is not a plateau.** CR-12 saw +4 °C over the second five minutes at 30 % and says a true plateau needs 20–30 min, so 98 °C is a **lower bound**. 🔵 **Knock-on beyond safety:** output falls 0.3–0.6 %/K, so a 10 K hotter junction is **3–6 % less light than board 1** — record the temperature alongside every `scan carrier` result or the SNR numbers are not comparable |
| | Beam path width (mm) | | `detect path <mm>`. No velocity is reported until set |
| | Pi shutdown duration | | **Phase 8.2.** Set `PI_SHUTDOWN_MIN_HOLDOFF_MS` ≈ 2× this. It is 15 s on an assumption today |
| 8/24/2026 | 🔴 **FIRMWARE DEFECT — `off` did not turn the beam off** | **found at the bench** | 🔴 **`s_on` in `beam.c` survived every rail-down.** The power FSM never touched the beam: `PS_FORCE_OFF` dropped the latch and called `detect_hpf_safe_off()` for GPIO33, but nothing cleared the beam, so the PWM slices stayed enabled and GPIO31/GPIO39 stayed in `GPIO_FUNC_PWM`. **The beam only looked off because it had no power** — the next `on` relit D11 the instant the rail came up, with no `beam on`, no duty ramp, no cold-LED warning and `beam_duty_stable_ms()` already running. ⚠ **It also drove 3.3 V logic into an unpowered U10 (MCP1416), which runs from the SWITCHED +5 V rail** — the same back-feed condition GPIO33 is driven low for three lines later. ✅ **Fixed in `power_fsm.c`, in the FSM rather than in the CLI**: `beam_enable(false)` in `begin_shutdown()` (orderly path — dark at the *start* of teardown, not 15 s later when the Pi reports down) and as the **first** statement of `PS_FORCE_OFF`, before the latch drops. 🔵 **The CLI was the wrong place**: `off` is only one of six routes to rail-down — short press, 5 s escape hatch, `FAULT_SUPPLY_LOST`, `FAULT_PI_SHUTDOWN_TIMEOUT`, `power_request_force_off()` and the defensive `default:` — and all six land in `PS_FORCE_OFF` |
| 8/24/2026 | 🔴 **FIRMWARE DEFECT — `cal model` leaves the beam at 200 kHz** | **cost a bench session** | 🔴 `cal_demod_model()` walks the carrier 80→200 kHz and **never restored it**, so every `cal demod` typed afterwards silently calibrated **200 kHz**, printed no frequency, and committed the answer. Two runs were spent "dialling in the light" at the wrong carrier and a phase of **744 ticks** was committed as if it were the 104 kHz value. 🔵 **The tell is in the sweep listing:** phases step by (TOP+1)/64, so steps of 11/23/35 mean a 750-tick period = 200 kHz, against 22/45/67 for 1440 = 104 kHz. ✅ **Fixed:** frequency and phase saved on entry and restored before every return path, and `cal demod` now prints `at <f> Hz (TOP+1 = <n>)` in its header |
| 8/24/2026 | 🔴 **`scan carrier` had the SAME carrier-restore bug — fixed before it bit** | **found by reading, not at the bench** | 🔴 `cal_scan_carrier()` walks the carrier across the band in exactly the same way `cal_demod_model()` did and **also never restored it** — so a `scan carrier` would have left the beam at f1 (200 kHz by default) and every command afterwards would have run at the wrong frequency, silently. ✅ **Fixed the same way**: frequency and phase captured on entry, restored on the normal path **and on the warm-up REFUSED path**, with the duty-ceiling restore moved onto that path too (it leaked before). The command now prints `Carrier restored to <f> Hz` and states that the winner is a **recommendation** — adopting it means editing `board.h` and re-running `cal demod`, not leaving the beam wherever the scan ended |
| 8/25/2026 | 🔴 **DOC AUDIT — phases 5, 6 and 7 have NO FIRMWARE, and the docs did not say so** | **three BENCH docs described procedures that cannot be run** | 🔴 `src/` contains no strobe, mic or camera module, and the CLI has **no `strobe`, `gate`, `burst`, `mic` or `cam` command**. 🔴 **`src/strobe_burst.pio` exists but is NOT in `CMakeLists.txt`** — only `detect.pio` is passed to `pico_generate_pio_header()`, so it has never been assembled. `compute_schedule()` does not exist. `BURST_CHARGE_MAX_mC` does not exist in `board.h`. ⚠ **`BENCH_P6_STROBE.md` reads as a runnable procedure throughout** — "load the PIO burst program", "DMA-feed a schedule", "unit-test `compute_schedule()`" — with nothing marking any of it as unwritten. ✅ **Fixed:** every future BENCH doc now opens with a FIRMWARE STATUS table separating what exists from what does not. ✅ **The one genuine exception is Phase 5 bring-up**, which needs only `adc 7`, `capture 0x80` and `tools/scope.py` — runnable today on USB power alone |
| 8/25/2026 | 🔴 **THE PHASE MODEL WAS NEVER RESTORED TO RAM — `scan carrier` would have been meaningless** | **`cfg` said "fitted", `s_phase_model.valid` was false** | 🔴 **Found by reading a `cfg` dump from the bench, not at the bench.** `cfg_init()` pushes `adc5v_scale`, `detect_coalesce_us` and `path_mm` back into the modules that own them, and `cfg_apply_beam()` pushes carrier and phase into the beam — **but nothing restored the phase model.** So after every reset `cfg` printed `phase mdl: fitted, dispersive (80000..200000 Hz)` while the RAM copy was empty. 🔴 **The symptom would have been §3.6:** `scan carrier` warns "no phase model … every candidate will be measured at phase 0 … the SNR ranking will be meaningless" and then **runs anyway**. ⚠ **This is the same defect class as the 2026-08-14 fix in §10** — "saved carrier_hz / demod_phase_ticks but never restored them" — surviving in a different field of the same record. ✅ **Fixed:** `restore_phase_model()` in `cli_init()`, which also **recomputes** `pure_delay` rather than trusting a verdict saved by an older build. ✅ `cfg` now prints `[NOT IN RAM]` if the two ever disagree again |
| 8/25/2026 | 🔴 **The saved phase is uninterpretable without the duty it was measured at — `cal_duty` added** | **166 ticks of error at the power-on default** | 🔴 The config stored `demod_phase_ticks` but **not the beam duty**, and `phase_ticks = t_chain_ticks − (duty/2)·(TOP+1)` — so the number is only correct at the duty it was measured at. `s_duty` powers up at **2 %** ("start low; ramp up deliberately") while the phase was calibrated at **25 %**, which is `1440 × (0.25−0.02)/2` = **166 ticks off**, or 41°, i.e. **cos 41° = 75 % of signal lost**. The 8/25 bench dump shows exactly that state: `duty 2.00 %` with `phase 1348 ticks` restored. ✅ **Fixed:** `cal_duty` added to the record, written by `cal demod`, and `cfg` now prints it **and warns with the tick error** whenever the live duty differs by more than 0.5 %. 🔵 **Carved out of `reserved`, not appended** — `slot_valid()` compares `size` against `sizeof(pitrac_cfg_t)` and there is a `_Static_assert(… == 256)`, so growing the struct would have invalidated every saved record and silently reverted the board to defaults. The unused `telemetry[16]` block absorbed the shift, so existing records read `cal_duty = 0` = "not recorded", which the code handles explicitly |
| 8/25/2026 | ⚠ **`cfg` reported "dispersive" from a STALE STORED FLAG, not from the data** | **the verdict was saved by the pre-fix firmware** | ⚠ The 8/25 bench dump reads `phase mdl: fitted, dispersive`, but the corrected `pure_delay` test gives **0.78° against a 5° bar — a pure delay**. `phase_pure_delay` is a stored boolean, written when `cal model` ran on the build *before* the 8/24 fix, and `cfg` prints it verbatim. **The coefficients in flash are fine; only the boolean is wrong.** ✅ `restore_phase_model()` now recomputes it from a0/a1/a2 whenever `cal_duty` is known, so the flag self-corrects on the next boot after a `cal demod` + `cal model` + `cfg save` |
| 8/25/2026 | ⚠ **`id` reported `fw: pitrac phase0/1/1b` — three phases out of date** | **a version string nobody updates** | ⚠ Hardcoded in `cli.c` and never touched through phases 2, 3 and 4. ✅ Now reads **`pitrac phases 0-4 (5/6/7 not implemented)`**, which also surfaces the 8/25 audit finding at the top of every session instead of only in a doc. 🔵 A stale version string is worse than none, because it reads as authoritative |
| 8/25/2026 | ⚠ **`hpf` warned "polarity is a compiled HYPOTHESIS" forever — it never checked** | **`hpf test` passed on 8/21 and the config records it** | ⚠ The line printed **unconditionally**, with no reference to `cfg()->hpf_sel_track`. Meanwhile `hpf test` writes that field on CONFIRMED and the 8/25 bench dump shows `hpf sel : TRACK = 0` — i.e. it has run, passed, and been saved (0xff would read "NEVER MEASURED"). **No re-run was needed.** ✅ **Fixed:** three states now — not run (warn), confirmed and matching the build (say so, and say nothing needs re-running), or **config disagrees with the compiled `HPF_SEL_TRACK`**, which is a loud warning that did not previously exist and is the genuinely dangerous case: it means a board calibrated under one polarity is running an image built with the other |
| 8/25/2026 | 🔵 **PATTERN worth hunting: messages that do not consult the state they describe** | **four instances found in one `cfg`/`hpf`/`id` dump** | 🔵 All four came out of a single bench dump the user pasted, and none would have been found by running the firmware — only by reading the output against the code: **(1)** `id` hardcoded `phase0/1/1b` through three phases of work; **(2)** `hpf` warning HYPOTHESIS after the test had confirmed and saved; **(3)** `cfg` printing `phase mdl: fitted` from flash while the RAM copy `scan carrier` uses was empty; **(4)** `phase_pure_delay` printed verbatim from a flag written by an older build whose test was wrong. ⚠ **Three of the four read as authoritative and were wrong**, which is worse than printing nothing. 🔴 **When adding any status line, ask what would have to change for it to become false, and make it read that thing.** Worth a sweep of the remaining `printf` status text in `cli.c` before Phase 6 adds more |
| 8/25/2026 | ✅ **REPRODUCIBILITY: four `cal demod` runs spread 7 ticks = 1.75°** | **costs 0.012 % of signal** | ✅ At 104166 Hz / 25 % duty, across amplitudes spanning 2.3×: **1347** (saturated 77 %), **1350** (peak 34 % FS), **1348** (50 %), **1343** (81 %). Spread **7 ticks**; worst case against the mean is 0.88° → **cos 0.88° = 99.988 % of signal**. 🔵 **The phase measurement is not the limiting term in anything.** ⚠ **Weak downward trend with amplitude** (1350 → 1348 → 1343 as peak goes 34 → 50 → 81 % of full scale) — monotonic on three points, which is 1-in-6 by chance, so it is not evidence yet. **Plausible mechanism if real:** the negative half of the chop sits deeper against ADC5's bottom rail as amplitude grows, so the one-sided clipping asymmetry grows with it. **Not worth acting on at 1.75°**; worth re-checking if a fifth run continues the trend |
| 8/25/2026 | 🔴 **MY BUG — `cal demod`'s delay line read the geometric term only** | **a constant 1200 ns whatever the phase** | 🔴 **Introduced 2026-08-24 when the delay reporting was added.** `out->chain_delay_ns` was computed next to `out->peak`, where it reads naturally — but `out->best_ticks` is not assigned until **25 lines later**, so it ran against a zero and collapsed to `wrap((duty/2)·(TOP+1))` = **exactly 180 ticks = 1200 ns at 25 % duty, for every run**. ⚠ **It looked like a plausible number**, which is why the build did not catch it and a bench dump did. ✅ **Fixed** — moved after the assignment. **Correct values: 1343 ticks → 553 ns, 1348 → 587 ns, 1350 → 600 ns.** 🔵 **A reported quantity that is constant across runs it should vary with is the tell.** The docs' 620.8 → 599.1 ns model figures were computed offline in Python and are unaffected |
| 8/25/2026 | 🔴 **I OVER-READ ONE MEASUREMENT: the "3.5 % delay droop" does not reproduce** | **−3.50 % on 8/24, −0.05 % on 8/25** | 🔴 **Same board, same procedure, residuals 0.074° and 0.080°.** The 8/24 fit gave **620.8 → 599.1 ns** across 80–200 kHz; the 8/25 fit gave **571.2 → 571.0 ns**. ⚠ **The span is a difference of two large numbers**, so a few-tick shift in the fit moves it enormously while barely touching either endpoint — it is not a well-determined quantity and should never have been quoted as one. 🔴 **I put that 3.5 % into `cal.h`, `BENCH_P3_DETECT.md` and `BRINGUP_NEW_BOARD.md` on 8/24, and additionally credited the TIA's 677 kHz pole with about a quarter of it.** That attribution rested entirely on the magnitude being real. ✅ **All three corrected**: quote the **absolute delay (~570–620 ns, reproducible to ~50 ns)** and the **verdict**, never a droop figure |
| 8/28/2026 | ✅ **DOC RESTRUCTURE — `BRINGUP_NEW_BOARD.md` is now THE driver for a new board** | **per-board vs design-validation, made explicit everywhere** | ✅ The organising distinction is now stated in three places and used consistently: **§0.5 of this file** (the full classification, plus the rule for deciding which a new test is), the **head of `BRINGUP_NEW_BOARD.md`** (what is settled vs what must be measured, with § pointers), and a **table at the head of `BENCH_P3_DETECT.md`** marking each of §3.1–3.7. 🔵 **The rule: ask what would have to change for the answer to change.** If it is the design — a netlist value, a firmware mechanism, a physical law — it is settled once. If it is this assembly — a tolerance, a mounting, an optical alignment — it repeats |
| 8/31/2026 | 🔴 **23 operator-visible strings were printing mojibake, and every source file carried a BOM** | **55 double-encoded em-dashes, 14 BOMs, 1 mangled ±** | 🔴 Found by sweeping the source for non-ASCII while auditing today's changes. Every `printf` and `help` line containing an em-dash was emitting **`â€"`** to the serial console — `beam ramp`, `beam clamp`, `stat`'s "DOWN — beam cannot run", every `panel` pattern name, the FIFO-overrun warning, and the `? 'beam x'` error. 🔵 **The operator sees this, not the source.** ✅ 55 × `â€"` → `--`, 1 × `Â±` → `+/-`, and **14 BOMs stripped** (13 source files plus `CMakeLists.txt`; all were leading, so harmless — but a stray BOM is exactly what broke `strobe_burst.pio` on 2026-08-30). ⚠ **Root cause is an editor writing UTF-8 that something re-read as cp1252 and re-encoded** — it will come back unless the tooling is fixed. Sweep for `[^\x00-\x7f]` in `src/` before believing any CLI output is clean |
| 8/31/2026 | ✅ **§3.6 Check 2 PASSES — and it accidentally ran the worst case** | **sigma_noise 4.57–5.08 across 95–115 kHz, 10.8 % spread, no outlier** | ✅ With the settling bug fixed, the sweep is flat. 🔵 **Better than flat — it tested the collision head-on without meaning to.** At **110.0 / 112.5 / 115.0 kHz the 7th harmonic lands at 770.0 / 787.5 / 805.0 kHz, all INSIDE the measured 762.9–833.1 kHz switcher band** — and those three rows are among the **quietest** (110 kHz gave the lowest sigma_noise of all nine, 4.57). ✅ **So the 0.173 mV of switcher ripple does not reach the detector, and the −2.3 % drift margin is a paper number, not an operational risk.** The operating carrier 104.1667 kHz stays |
| 8/31/2026 | 🔴 **`scan carrier`'s SNR column cannot rank frequencies — `signal` is a TIME trend, not a frequency response** | **546.0 → 574.4 monotonic across all 9 rows; p ≈ 3×10⁻⁶ by chance** | 🔴 The signal column rises **strictly monotonically in row order** — 546.0, 555.9, 560.4, 561.2, 564.9, 567.0, 570.5, 571.5, 574.4. Nine values landing pre-sorted is 1/9! ≈ **1 in 363,000**, so this tracks **elapsed time, not carrier frequency.** 🔵 **Cause: the scan chops the beam**, halving its average duty from 25 % to ~12.5 %, so the LED *cools* through the run and its output climbs — the same 25–45 % cold-to-plateau effect the warm-up gate exists for, running in reverse. ✅ Combined with sigma_noise's ±1.7 % scatter, **"BEST by SNR: 110000" is +1.5 sigma from the mean of the other rows — noise.** 🔴 **Do not adopt it.** ✅ The command now reports **flatness** as the verdict and says the carrier is fixed |
| 8/31/2026 | ✅ **`scan carrier` no longer tells the operator to do the thing the project decided against** | **"Put the winner in board.h" contradicted the fixed-carrier decision** | 🔴 The closing advice said *"The winner above is a RECOMMENDATION -- to adopt it, set it in board.h and re-run `cal demod`"* and *"record it in PROGRESS.md section 6"* — but §3.6 had already established that **one carrier is used on every board** and that this scan is a **verification, not an optimisation**. The printout survived that decision unchanged. ✅ Now prints a **FLATNESS** verdict (min/mean/max, spread %, PASS under 25 %, and names the outlier row with what to check if not), states the carrier is fixed and BEST-by-SNR is diagnostic only, and corrects the `beam_noise_ratio` guidance: **read it for flatness, not closeness to 1.0 — a lit LED cannot add zero noise and 2–4× is ordinary** (board 3 reads 2.86–3.41, flat) |
| 8/31/2026 | ✅ **The beam's 4th harmonic is NULLED on the +5 V rail — an independent confirmation of the trapezoid model at exactly 25 % duty** | **0.33 mV against 4.30 and 2.50 mV either side** | ✅ Beam-on rail capture, 2.06 s at 50 MS/s: harmonics of 104166.7 Hz measure **8.67, 8.20, 4.30, 0.33, 2.50, 2.39 mV** for n = 1..6. 🔵 **n = 4 is suppressed ~10×, and that is not noise — it is structural.** A duty-D pulse has harmonic amplitude ∝ `sinc(nD)`, and at D = 0.25 `sinc(4×0.25) = sinc(1) = 0`. **The null lands exactly where the model says**, measured on a completely different node by a completely different instrument from the one the model was fitted on. ✅ It also confirms the duty is a true 25 %, since the null would fill in if it were not |
| 8/31/2026 | ⚠ **A beam-ON rail capture cannot isolate the switcher — the beam's own comb sits on top of it** | **the 700–900 kHz peak moves to 729.4 kHz = 7×104166.7** | ⚠ With the beam on, the strongest line in the switcher band is **729.4 kHz at 1.28 mV** — that is the beam's **7th harmonic** (7 × 104166.7 = 729.17 kHz, agreeing to 0.03 %), not the boost, and it is **7.4× larger** than the 0.173 mV switcher hump it buries. ✅ **The beam-OFF capture is the one that measures the switcher**; beam-on measures the beam. 🔵 Useful either way: the beam puts **8.67 mV at 104.2 kHz** on +5 V, but C66's 31.8 Hz pole attenuates that by ~3276× on the Q8 path, so it reaches the comparator input as ~2.6 µV |
| 8/31/2026 | 🔴 **`scan carrier` had the SAME 50 ms-vs-0.66 s bug `hpf test` was rebuilt to eliminate — its "BEST by SNR" was first-point bias** | **95 kHz "won" by 225× because it was measured first** | 🔴 `adc5_sigma()` ran **50 ms** after a beam step against the HPF's **660 ms τ** — **0.076 τ, four times worse than the 200 ms already condemned in detect.h.** So σ did not measure noise: it measured the HPF still recovering, and **that slope is proportional to the signal.** 🔵 **The data says so outright — `signal`/`sigma_noise` is 17.8 ± 0.5 across eight of the nine rows** (2390/134.4, 2386/134.1, 2382/134.1 …), which no real noise process does. ✅ **Row 1 was the only one preceded by a settled beam** — the operator's 5 min warm-up — so it alone read a true floor (**0.26** codes vs 90–134) and took "BEST by SNR" at **5042 vs 17.8**. ✅ Fixed: `CAL_SIGMA_SETTLE_MS` 5000 (7.6 τ, matching `HPF_TEST_SETTLE_MS`), adds ~90 s to the scan. 🔴 **DO NOT ADOPT 95 kHz** |
| 8/31/2026 | 🔵 **The tell for a settling artifact is that σ tracks the SIGNAL, not the floor** | **a fixed signal/σ ratio across a swept parameter** | 🔵 Additive noise is independent of signal amplitude, so a **constant** ratio between them means σ is a *fraction of the step* — i.e. a transient being sampled, not a noise process. Here the HPF drops V·e^(−t/τ) by 19.7 % across the 130 ms window; a monotonic ramp of extent Δ has σ = Δ/√12, giving **σ ≈ 0.058·V → ratio 17.3** against the measured 17.8. ⚠ `sigma_floor` was flat at 0.45–0.50 for a different reason: with the beam off the node sits **on U12B's ground rail** (§3.6b), so the "floor" was a clamped node. **Neither column measured what its name says** |
| 8/31/2026 | 🔴 **The switcher is at 801 kHz, not the 1.055 MHz assumed — and that cuts the fold margin from −9.6 % to −2.3 %** | **762.9–833.1 kHz band, dithered ±4.4 %** | ✅ Measured from the **+5 V rail with a logic analyser**, 1.52 s at 50 MS/s, Welch-averaged over 1162 segments — no scope and no 36 V node touched. **801.1 kHz fundamental with harmonics at 1602.9 (2.001×) and 2404.8 kHz (3.002×)** — exact multiples, so a switcher, not a resonance. 🔵 **It is dithered**, which is why it appears as a smooth ~70 kHz hump and why the first peak-search found nothing and I wrongly concluded "the boost is not switching." 🔴 **The 7th harmonic of the carrier (729.2 kHz) is only 33.8 kHz from the band edge**, so the boost may drift just **−2.3 %** before folding — *inside its own dither spread*. ⚠ **Attribution unsettled: L1 (LM5157, straight off +5 V) vs L2 (RP2350 core buck, U3.63 VREG_LX, behind U2's LDO).** Scope on L1 pad 1 settles it |
| 8/31/2026 | 🔵 **A logic analyser measured a 0.17 mV tone on a 5.2 V rail — the method is worth keeping** | **broadband floor 0.0144 mV after 1162 averages** | 🔵 The LA's ±12 V input cannot go near the 36 V SW node, but the switcher's current is drawn **through L1 from +5 V**, so it is visible on a rail the LA can read directly. ✅ **Its own noise floor is flat within 4.5 dB from 100 kHz to 5 MHz and only rolls off above 8 MHz** — measured from the capture itself, so the null result at 1.055 MHz is trustworthy rather than an instrument limit. ⚠ **Check the instrument's response before believing a null.** 🔴 I also read a 104.2 kHz peak as "the beam is on" when the beam was **off** — that came from a max-over-±3-bins statistic, which is biased upward against a noise floor. **Use a median baseline, not a windowed max, when deciding whether a line exists** |
| 8/31/2026 | 🔴 **§3.6b FAILS. Q8 is real, at the predicted magnitude — ×7.43 measured vs ×7.25 predicted** | **+75.0 mV rail → +556.9 mV at the comparator input, and it STAYS** | 🔴 Method A run properly on board 3: TRACK and HOLD captures, bidirectional hand ramp, 1 MS/s. **In HOLD the coupling is ×7.43 and permanent.** The analysis that predicted ×7.25 from R75/R76 = +5VA/2 through C81 into U12B ×14.5 was **right to 2.5 %.** ✅ **TRACK works exactly as designed:** the same step peaks at ×3.84 and decays with **τ ≈ 0.75 s** (nominal 0.66 s), back to baseline in 3.4 s. 🔵 **So the exposure is exactly the armed window and nothing else.** 🔴 **Against the pass criterion this is not close:** "under 20 % of the §3.5 threshold" would need a threshold above **2.8 V**, i.e. 85 % of the DAC's 3.268 V full scale. **At a 300 mV threshold a 39 mV rail step fires the detector; at 100 mV it takes 13 mV.** 🔧 **CR-02 is now a measured requirement, not a proposal** |
| 8/31/2026 | 🔴 **A rail SAG does not just risk a false trigger — it BLINDS the detector** | **U12B clamps 21 mV below quiescent and stops responding** | 🔴 The coupling is **positive**: rail up → comparator input up. So a **falling** rail drives U12B's output into its **negative rail**, which is ground — its gain is taken to GND (R98.1, R100.1) on a single +5VA supply, so quiescent **is** the bottom of its range. **Measured: it clamps 21 mV below quiescent and then does not move**, through a further **120 mV** of rail droop that ×7.43 says should have swung it **870 mV**. 🔵 **While the rail is sagging, a ball cannot move the comparator input at all — the amplifier is saturated.** ⚠ **This matters most in Phase 6, where strobe bursts sag the rail deliberately**: the sag blinds, and the recovery — a rising rail — is the false-trigger edge. 🔵 **The 500 ms supply-monitor debounce was sized for the sag; nothing yet covers the recovery** |
| 8/31/2026 | ✅ **The bidirectional step worked — and it proved the previous capture had measured nothing** | **the 8/31 down-only capture was the clamp, not a coupling** | ✅ Predicted from the netlist that U12B had **~14 mV** of downward headroom against a 725 mV excursion; **measured 21 mV.** The earlier down-only capture read **rail −94.6 mV → comparator −55.7 mV, "×0.59"** — that was the amplifier sitting on its rail plus the ground artifact, and **it looked like a comfortable pass.** 🔵 **The failure mode to remember: a saturated stage reports a SMALL number, which reads as a good result.** ⚠ The p-p noise gives no warning — it is the instrument floor, not the node. ✅ **Both directions, every time**, and §3.6b now says so |
| 8/31/2026 | ⚠ **§3.6b Method A run on board 3 — HOLD confirmed, but the number is a BOUND, not a value** | **rail −94.6 mV, comparator input −55.7 mV, raw ratio ×0.59 against a predicted ×7.25** | ✅ **HOLD is confirmed beyond doubt:** 3 s after the ramp stopped, a TRACK baseline would have decayed to **−12 mV** (4.5 τ); the node sat at **−61 mV** and never moved. So this is the armed case. ⚠ **But the coupling cannot be extracted.** Raw ratio ×0.59; after subtracting a unity common-mode the residual *changes sign* (−0.26 to −0.48 for k = 0.85…1.07). 🔵 **What survives every reading is the magnitude bound: |coupling| ≤ 0.6, at least 12× below the predicted ×7.25.** The pass criterion is met with room to spare either way — a 100 mV rail step moves the comparator input ≤60 mV, not 725 mV |
| 8/31/2026 | 🔴 **§3.6b as written could not measure Q8 in one direction — U12B has 14 mV of downward headroom against a 725 mV prediction** | **netlist: R98.1 and R100.1 both go to GND** | 🔴 `Net-(U12B-IN2-)` = R98.2 + R100.2 + R101.1, and **both R98.1 and R100.1 are on GND** — U12B's gain is taken to **ground**, not +2V5. U12.4 = +5VA, U12.11 = GND: **single supply.** So with the beam off its output sits at **~0 V** (`hpf test` reads **+13.75 mV**), and that is its entire downward range. **A rail step in the direction that drives the comparator input down rails the amplifier after ~14 mV.** ⚠ **There is no tell in the data when this happens:** the ~80 mV p-p is the instrument floor, not the node — the capture reads the node **121 mV below ground**, which a single-supply output cannot do. ✅ **Fix: step the rail BOTH ways, 5.20 → 5.10 → 5.20.** 🔵 **The UP direction is the one that matters** — the comparator triggers on a rising input, so the sign decides whether a Phase 6 strobe burst's **sag** is dangerous or its **recovery overshoot** is |
| 8/31/2026 | 🔴 **The 8/28 ground artifact was never fixed, and §3.6b's numbered steps still described the method its own warning box forbids** | **coupling flat at ×0.81–0.90 from 500 Hz to 5 Hz, r ≈ 0.9; 60 Hz still the largest tone at 6.2 mV** | 🔴 **(1)** The 8/28 entry said "shorten the ground lead and reference it at the board before measuring Q8 properly." It was not done, so the new capture carries the same **unity-gain common-mode** — and a flat ~1.0 transfer across three decades between two different nets is a shared moving ground, never circuit coupling. At ~12 mV against a 56 mV signal it is **~20 % of the measurement**, which is why the sign cannot be recovered. 🔴 **(2)** §3.6b's **Step 1/2/3 still said `beam on`/`beam off`** — the exact beam-step method the box directly above them forbids in bold. The box was added without updating the steps it superseded, and the steps are what gets followed. ✅ Both fixed: steps rewritten as Method A, bidirectional, with Step 1 (TRACK) now serving as the **artifact reference that Step 2 subtracts** |
| 8/31/2026 | ✅ **Board 3 HOLD leakage is ~55 pA — the 1.16 nA was an artifact, and F6 stands** | **2.2–2.7 mV/s at ADC5, vs the 50.8 mV/s inferred** | ✅ `hpf test` on board 3: TRACK +13.9/+13.6 mV, HOLD +31.4/+31.0 mV, repeats agreeing to **0.3 mV** while the levels differ by **17.4 mV** — conclusive, no order effect, TRACK = 0, matching both the compiled constant and the TMUX1219 truth table. 🔵 **Two independent statistics agree**, which is what makes the number trustworthy: the mean displacement (17.4 mV over 6.5 s to the window centre) gives **2.7 mV/s → 61 pA**; the p-p within the window, less TRACK's p-p as the noise floor, gives **2.2 mV/s → 50 pA**. Board 2 was 52 pA, board 1 ~250 pA — **board 3 is the quietest board measured, not 22× the worst.** ✅ Armed window **~37 s** to walk 100 mV at ADC5; a 1–10 ms transit walks **~27 µV**. **F6 needs no revision** |
| 8/31/2026 | 🔵 **WHY the beam-step inference was wrong — it measured a different quantity** | **a droop with the beam ON is not switch leakage** | 🔴 The §3.6b number came from "in HOLD with the **beam on**, the node droops 2.187 → 2.085 V over 2.00 s". Two defects. **(1) The beam was on**, so ADC5 carried the demodulated signal, not a quiet baseline — that 100 mV droop is only 6.9 mV at U12B's input, ~240 µV at TP7, which **a few tenths of a percent of LED output falling as CR-12 warms fully explains**. **(2) 2.00 s is 3 τ of the 0.66 s HPF**, so it can freeze mid-relaxation. `hpf test` uses **5 s = 7.6 τ** precisely for this, and that constant exists because *this same error broke the first version of the test.* 🔵 **The rule: the beam-step droop measures the optical/thermal stability of the beam path; `hpf test` measures switch leakage. Only the second one answers F6.** 🔴 I proposed the beam-step method — it reintroduced a settling error the firmware had already been fixed for |
| 8/31/2026 | ✅ **`hpf test` now reports the leakage instead of asserting board 1's number** | **the preamble hardcoded "~11 mV/s measured on this board" on every board** | 🔴 That string was board 1's rate printed verbatim regardless of which board was in front of you — 4× high for board 3, 5× high for board 2, and stated as a measured fact. ✅ Replaced with the honest range, and the test now **computes and prints** drift rate by both statistics, implied leakage in pA against the other boards, and the armed window in seconds. 🔵 **The back-of-envelope that found this should not have been mine to do** — the measurement was already in the struct |
| 8/31/2026 | 🔴 **TWO REGRESSIONS FROM THE REFACTOR, both the same shape** | **a claim about context that was only half true** | 🔴 **(1) The CLI ate the first character of every command** — `cfg` → `? 'fg'`. `pitrac_service()` polled `getchar_timeout_us()` and has TWO callers: the superloop and `pitrac_yield_ms()`. The comment said it was safe "because cli_service() is not on this path" — **true of the yield path, false of the superloop, which calls it one line BEFORE cli_service()**. 🔴 **(2) `bootsel` stopped working.** `reset_usb_boot()` reaches BOOTSEL *through* the watchdog's scratch registers, so an armed watchdog fights it — and even on success a still-armed watchdog resets the chip ~1 s later, before the host enumerates RPI-RP2. ✅ Poll moved into the yield path only; `bootsel`/`reset` now disarm first. 🔵 **Both were reasoning about one caller and writing the conclusion down as a comment, which is what made them look reviewed** |
| 8/31/2026 | ✅ **`pitrac_watchdog_enable(false)` did not disarm — it made things WORSE** | **it only stopped kicking** | ✅ The first version set a flag and ceased calling `watchdog_update()`, which makes the watchdog fire **sooner** — at exactly the moment a caller was trying to make it stop. The SDK has no `watchdog_disable()`; the enable bit has to be cleared directly with `hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS)`. ✅ Fixed |
| 8/31/2026 | 🔵 **DECISION — the watchdog defaults OFF until Phase 6** | **it cost a reflash and two sessions in one day** | 🔵 The Phase 1b policy (armed while no Pi is powered) is about protecting a **Pi** from a hung MCU. On the bench there is no Pi, so what it actually buys is a reset when the firmware hangs — and on this board **a reset DROPS THE LATCH**. That is a power cut in the middle of whatever was being measured, and a hang is already obvious to an operator sitting in front of the board. ✅ **Where it earns its place is Phase 6**, where a hang with 9 A through a linear-mode FET is a genuinely different risk — arm it there, deliberately, in the strobe code rather than as an ambient default nobody remembers is on. ✅ `wdog on` arms it per session; `stat` shows the state |
| 8/31/2026 | ✅ **Board 3 post-reflash verification — everything the refactor touched is sound** | **UID `6d6fda754e367a40`, scale 1.0620, +5V_IN 5.210 V** | ✅ 28 commands via the new dispatch table, `help` reports nothing undocumented, `cfg` reads back **slot B seq 4** with carrier 104166 / phase 1311 / model fitted — so the 10 KB binary growth did not reach the config sectors. 🔵 **Two new guards fired correctly on their first real outing:** the model now reports **"pure delay"** where the old broken test said "dispersive", and the `cal duty` warning caught the beam sitting at the 2 % power-on default against a phase calibrated at 25 % — **"the stored phase is 166 ticks off"**, exactly the predicted figure |
| 8/28/2026 | ✅ **Two per-board steps were missing from bring-up entirely** | **boost SW frequency, and §3.5** | ✅ **§2 now records the LM5157 SW frequency** — the scope is already on that node for the snubber decision, and with the carrier fixed the boost's part-to-part spread is the whole risk. ✅ **New §10 covers the threshold / comparator cross-calibration**, which produces two genuinely per-part constants (**DAC vref** and **LM393 Vos**) and had no place in the bring-up flow at all. ⚠ **Both were being treated as one-off because they had only ever been done once** — which is exactly the confusion §0.5 exists to prevent |
| 8/28/2026 | ✅ **Sign-off table rebuilt: three boards, § numbers, and the gaps visible** | **27 rows** | ✅ Now carries a column per board and the § that produces each number, so an incomplete bring-up is obvious at a glance rather than buried. 🔴 **It immediately surfaced what is missing:** board 3 has **never run `hpf test`**, the boost SW frequency is unrecorded on **all three** boards, and most of board 3's rail measurements were never written down |
| 8/25/2026 | 🔴 **FIRMWARE — `threshold 5` silently did nothing and looked like it worked** | **no branch matched, so it printed status and returned** | 🔴 `threshold` only accepted `duty`, `volts` and `sweep`; anything else **fell through to the status print**. An operator typed `threshold 0` then `threshold 5`, got a plausible status line back both times, and the level stayed at **51 (4.98 %)** throughout — only discovered three commands later. ✅ **Fixed:** unknown subcommands now print `ERR: ... NOTHING WAS SET.` plus usage, and return. 🔵 **A command that ignores its argument is worse than one that errors**, because the status line it prints is real |
| 8/25/2026 | ✅ **§3.5 COMPLETE — threshold DAC and comparator cross-calibrated over a 110× range** | **vref 3.268 V, Vos +11.0 mV** | ✅ Two flips, solved as `ADC5 = vref·duty + Vos`: **low** duty 0.23 %, TP8 7.7 mV, ADC5 18.5 mV; **high** duty 62.50 %, TP8 2062.5 mV, ADC5 2053.3 mV (held at 2645 codes via `beam phase 1000`). The model reproduces **both points to 0.1 mV**. 🔵 **The pair is worth far more than either point**: the low one is dominated by the comparator offset, the high one by the reference, so together they separate the two. **Both paths agree across a 110× range of level.** ⚠ Dominant uncertainty is the low point's 0.078 % sweep granularity → vref lands in **3.266–3.270 V**; Vos is ±2 mV |
| 8/25/2026 | 🟡 **BOARD 3 — TIA feedback reworked. Rf 470k → 116k, Cf 0.5 → 0.99 pF** | **all figures CALCULATED, none measured yet** | 🟡 **154 kΩ soldered in parallel with R80, and 100 pF C0G across ONE leg of the C68/C70 series pair.** This implements `NEXT_BOARD_REV.md` CR-15 mitigation #2. Rf = 470k ∥ 154k = **116.0 kΩ** (0.116 V/µA, **4.05× less gain**); Cf = 1 pF series (1+100) pF = **0.990 pF**; TIA pole **677.3 kHz → 1.386 MHz**; lag at 104.1667 kHz **8.74° → 4.30°**; DC servo corner **2.267 → 0.559 Hz** (τ 70 → 285 ms), since `f = [1/(2π·R83·C73)]·(Rf/R78)` scales linearly with Rf. ⚠ **C68 and C70 are in SERIES** — 100 pF across *one* leg shorts that leg and leaves the other 1 pF. Across *the pair* it would be 100.5 pF and a **13.7 kHz** pole, below the carrier. **100× apart, and only one works** |
| 8/25/2026 | ✅ **BOARD 3 REWORK CONFIRMED — phase 1311, chain delay 340 ns** | **all gates pass; 1.80× further than predicted** | ✅ `cal demod` on the reworked board: **1311 ticks**, delay **340 ns**, peak 3285 = 80 % FS, **sat 0 %**, quad null **0.0**, **h3 0.101** against the intrinsic 0.111, warm yes. `cal model` accepted all five points, **PURE DELAY to 1.01°**, and evaluates to **1314.5 ticks at 104166 Hz against the measured 1311 — 3.5 ticks**. 🔴 **But the shift from board 2 is −32 ticks where the TIA pole change predicts −17.8: 1.80× too far.** Direction and order are right, so the rework did what it was for; the excess is unattributed |
| 8/27/2026 | 🔴 **TP7 SCOPE — the Rf change took, the Cf change probably did NOT** | **two independent readings agree** | 🔴 **(1) Phase.** The measured 32-tick shift fits **Cf = 0.5 pF (predicts 26.3, needs 38 ns of board-to-board slack)** far better than Cf = 0.99 pF (predicts 17.8, needs 95 ns). **(2) Damping.** TP7 overshoot is **~12 % of swing** against board 2's **2.9 %** — 3–4× less damped, which is impossible if Cf/Cf_required were unchanged at 0.53. At Cf = 0.5 pF the ratio is **0.27**. ✅ **The resistor definitely IS in:** TP7 swings **0.76 V**, giving 0.76/116k = **6.55 µA** against board 2's 1.795/470k = **3.82 µA**, a **1.72×** optical difference that independently matches the 80 %-vs-47 % full-scale figure from `cal demod`. 🔴 **Check the 100 pF: visual, then an LCR meter across the leg unpowered** — 101 pF vs 1 pF is unmistakable |
| 8/28/2026 | 🔴 **RETRACTED — the Cf change DID take. I compared two instruments, not two boards** | **LA analog input vs a 500 MHz scope** | 🔴 I argued Cf was still 0.5 pF because board 3's TP7 overshoot (~12 % of swing) was 3–4× worse than board 2's (2.9 %). **Those numbers are not comparable.** Board 2's came from a coherent average of a **logic-analyser analog capture at 50 MS/s**, whose analog front end rolls off right where the ring lives (~1 MHz); board 3's came from a **500 MHz scope at 6.25 GS/s**. The LA under-reports the overshoot, so the comparison measured the instruments. ✅ **Settled by the operator: the oscillation was significantly worse BEFORE the 100 pF went on**, so the cap is in the loop and Cf ≈ 0.99 pF. 🔵 **Rule: never compare a ringing amplitude across instruments of different bandwidth** — compare frequency, or re-take both on the same instrument |
| 8/28/2026 | 🔴 **`cal model` run with a HAND as the target — unusable, and the numbers say why** | **amplitude spread 27.1 % vs 2.7 % static** | 🔴 Peak-to-peak per model point: a static scene held **7401–7602 codes (2.7 %)**; a hand held up and not quite still gave **6221–7907 (27.1 %)**. **A 10× difference.** The hand run **clipped two of its five points**, got 170 kHz rejected at sat 11 %, fitted only 4 points, and returned a **0.37° residual** against 0.08° for a good run — plus a delay trend of **+33.8 %** and a pure-delay verdict of 3.51° that only just cleared the 5° bar. 🔵 **Every individual point was valid** — clean h3, clean null, no saturation on four of five. **They simply described different scenes**, and the fit read the optics change as a phase change |
| 8/28/2026 | ✅ **New guard: `amp spread` across model points, limit 20 %** | **catches a failure no per-point check can** | ✅ `cal_demod_model()` now tracks min/max `peak` across the accepted points and `cal model` prints **`amp spread: NN %`**, flagging **`*** THE SCENE MOVED ***`** above 20 %. 🔵 **The reasoning that makes the check valid: frequency changes the phase, not how much light comes back.** So amplitude variation between points is optics, never physics. The 2.7 %-vs-27.1 % separation puts the bar in an easy place. ⚠ **This is the first check in `cal.c` that looks ACROSS points rather than within one** — every earlier guard (sat, h3, quad null, amplitude) is per-point, and per-point checks are structurally blind to a scene that changes between points |
| 8/28/2026 | ✅ **New `level` command — live % of full scale for aiming the target** | **closes a loop that used to cost 26 s per attempt** | ✅ Chops at 20 Hz and prints the peak as a percentage of full scale **~5×/s** with a bar and a verdict, until a key is pressed (300 s cap). It reports **the same `peak` number `cal demod` reports**, so the 50–70 % target is directly comparable. 🔵 **Getting the level right is the single most repeated failure in Phase 3** — saturated sweeps on 8/21 and 8/24, too-low signal on 8/28 — and the only feedback was to run the sweep and read the verdict afterwards. ⚠ It samples ONE phase, so it is the true peak only when the demod phase is already near it; the header says so and tells you to run `cal demod` first |
| 8/28/2026 | ✅ **§3.4 CLOSED ON BOARD 3 — 1311 ticks, 340 ns, SAVED** | **identical to the earlier clean run** | ✅ With the target aimed by `level` to 66 % and clamped: `cal demod` → **1311 ticks / 340 ns**, peak 3078 = 75 % FS, sat 0 %, quad null 0.7, h3 0.102 vs 0.111 expected. 🔵 **Bit-identical to the previous clean run** (1311 / 340 ns) taken at a different level (80 % FS) — so the phase is independent of amplitude over 75–80 %, which is what it should be. `cal model`: **θ(f) = 5.4274 + 3.271e−6·f − 2.995e−12·f²**, residual **0.26°**, all 5 points, **amp spread 4 %**, **PURE DELAY to 0.97°**, model→1314.5 ticks vs measured 1311 (**3.5 ticks**). ✅ `beam off` then `cfg save` → **saved**, so `cal_duty` is now recorded too |
| 8/28/2026 | ✅ **§3.5 COMPLETE ON BOARD 3 — vref 3.256 V, Vos +5.4 mV** | **vref agrees with board 2 to 0.36 %** | ✅ Low flip 0.23 % (TP8 7.7 mV, ADC5 12.9 mV); high flip 53.12 % (TP8 1753.1 mV, ADC5 1735.0 mV) with the node held at 2225 codes via `beam phase 1035`. Solving `ADC5 = vref·duty + Vos` reproduces both points exactly. 🔵 **Board 2 gave vref 3.268 V, board 3 gives 3.256 V — 0.36 % apart, and both sit between the compiled 3.300 V and the 3.246 V measured on board 2's +3V3 rail.** That cross-board agreement is the best evidence yet that the two-point method measures what it claims to. ⚠ Vos differs (11.0 vs 5.4 mV) and should: it is a per-part LM393 offset, spec'd ±15 mV |
| 8/28/2026 | 🔴 **§3.6b named the WRONG probe pad, and contradicted itself doing it** | **"scope ADC5 ... probe R102 pad 2"** | 🔴 Caught by the operator reading the schematic. **R102 pad 1 is `Comparator_ADC` (GPIO45/ADC5); R102 pad 2 is `Net-(U12B-OUT2)`**, which is U15.3, U12.7 and R101.2. The gear line said to scope **ADC5** and then pointed at **pad 2**, which is not ADC5 — two different nets, one sentence. ✅ **And pad 2 is the one you want anyway:** the comparator thresholds that node, the ×7.25 coupling lands on it, and it swings **0–5.2 V** while ADC5 is **D14-clamped at 3.3 V**. 🔴 **Probing ADC5 would saturate exactly when the answer starts to matter, and the error would be in the SAFE direction** — a rail step big enough to be interesting is one ADC5 cannot show. ✅ Section rewritten: pad 2 primary, pad 1 on a third channel to measure the D14 truncation (CR-16, never measured) |
| 8/28/2026 | 🔴 **§3.6b's BEAM-STEP METHOD CANNOT MEASURE Q8 — the optical term is 26× the electrical one** | **rail −20 mV, comparator input +3801 mV** | 🔴 Board 3, 50 MS/s on the comparator input (R102 pad 2) and +5 V together. Switching the beam moves the rail by only **−20 mV** — Q8 predicts **145 mV** at ×7.25 — but the measured excursion is **+3801 mV**, because the beam-on step changes the **light** as well as the rail. The apparent coupling of **190×** is not a coupling ratio, it is the detector working. ⚠ **Removing the target does not fix it:** bare crosstalk is smaller but still exceeds 145 mV on any board that can be calibrated at all. 🔵 **The doc has said "no target present, so the only optical change is crosstalk" since it was written, and treated that as if crosstalk were negligible — it never was** |
| 8/28/2026 | 🔵 **DECISION — the carrier is FIXED at 104.1667 kHz for all boards and users** | **uniformity over per-board SNR** | 🔵 `scan carrier` was doing two jobs and only one is dropped: **optimisation** ("best carrier on this board") is deliberately abandoned; **robustness** ("does the chosen carrier fold something into the passband") becomes MORE important, because one bad choice is now bad on every board forever. ✅ **Margin computed:** the nearest odd harmonics to the LM5157's 1.055 MHz are **9·f_c = 937.5 kHz (117.5 kHz away)** and **11·f_c = 1145.8 kHz (90.8 kHz away)** against a 15.39 kHz passband — **the boost can drift −9.6 % / +7.1 % before anything folds**. ⚠ **You cannot choose the collision away:** odd harmonics are 208 kHz apart, so any carrier near 100 kHz has one within ±104 kHz of the boost. The choice only sets the distance |
| 8/28/2026 | 🔴 **The LM5157 SW frequency has NEVER been recorded on any board** | **and it is the number the margin has to cover** | 🔴 `BENCH.md` Phase 0.5 step d already puts a scope on the SW node — to decide the snubber — and nobody wrote the frequency down. With the carrier now fixed, **the part-to-part spread of the boost is the whole risk**: the computed margin is ±~8 %, and if the population spread approaches that, it stops being comfortable. ✅ Added as a required bring-up record. 🔵 **A measurement that was already in front of us three times, for free, and was never taken because nothing depended on it until the design decision changed** |
| 8/28/2026 | ✅ **§3.6 rewritten as verification, not optimisation — and it is a better test** | **flatness near the carrier, not a 16-point ranking** | ✅ Replaces `scan carrier 80000 200000 16` (10+ min, ranks candidates you have decided not to adopt) with **`scan carrier 95000 115000 9`** (~5 min): nine points across ±10 kHz, and you want σ_noise and **`beam_noise_ratio` FLAT**, not a winner. 🔵 **`beam_noise_ratio` is the column that matters** — σ_noise ÷ σ_floor is how much noise the *beam* adds over ambient, which is exactly the folding this check exists to find. 🔵 **And the geometry objection largely dissolves:** comparing nine rows inside one run under identical optics means a common background cancels, where ranking across a whole band did not. ✅ Removed from `BRINGUP_NEW_BOARD.md` as a per-board step |
| 8/28/2026 | ✅ **Two methods that DO isolate Q8, both free** | **PSU step, or a beam step at the demod null** | ✅ **Method A — step the PSU with the beam off.** 5.20 → 5.10 V by hand: zero optical change, so whatever moves is Q8. 🔵 **A slow hand-turned ramp is exactly right, not a compromise** — the +2V5 divider is filtered at 31.8 Hz, so a slow change is the fully-coupled worst case this test wants, while a fast transient is attenuated by that pole and would flatter the result. ✅ **Method B — beam step with `beam phase` at a quadrature null.** The optical crosstalk is carrier-modulated and the demodulator nulls it there (board 3 read ±1 code at phase 225/973); the rail coupling is **not** carrier-modulated — it shifts the virtual ground, which moves the whole analog section identically in both demodulator sign states. 🔵 **So the phase removes the optical term and leaves the electrical one untouched, with the same 0.79 A load transient.** Method A is cleanest, B reproduces the real transient |
| 8/28/2026 | ❌ **SUPERSEDED 8/31 — "Board 3 HOLD leakage is ~1.16 nA" was an artifact.** ❌ **`hpf test` measured ~50–61 pA, 20× lower. F6 is unchanged.** Original entry kept below because the ERROR is the lesson** | **50.8 mV/s at the comparator input** | 🔴 In HOLD with the beam on, the node droops **2.187 V → 2.085 V over 2.00 s** = **−50.8 mV/s**. Back through the ×14.5 and C81 330 nF that is **~1.16 nA**, against board 2's **52 pA** and board 1's ~250 pA. ⚠ **F6 concluded "arming well before a shot is safe; only an arm-and-forget of many seconds needs thought" from board 2's 2.3 mV/s.** At board 3's rate the node walks **100 mV in 2 s**, not 43 s. A 1–10 ms transit still only walks 0.5 mV so detection is unaffected — **but the armed window is now seconds, not tens of seconds.** 🔵 Leakage doubles per 10 °C; 22× needs ~45 °C, which is a lot. **Re-measure with `hpf test`, which measures exactly this**, before trusting the number |
| 8/28/2026 | ⚠ **The capture carries 12 mV of common-mode 60 Hz — ratio 0.99 on both channels** | **a measurement artifact, not circuit coupling** | ⚠ In the quiet beam-off window the comparator input and +5 V correlate at **r = 0.998 with a slope of 0.99 V/V**, and the spectrum is **60 Hz** plus harmonics on both. 🔵 **Real coupling would be ~7.25× — or ~3.6× at 60 Hz once the 31.8 Hz +2V5 pole is allowed for — never 1.00.** A ratio of exactly unity between two different nets is the signature of **mains pickup on a shared measurement ground**. ✅ Harmless against the 3.8 V signals here, but it puts a **~12 mV floor** under any small number in this capture. **Shorten the ground lead and reference it at the board** before measuring Q8 properly |
| 8/28/2026 | ✅ **What the capture DOES establish: TRACK works, HOLD holds, and the supply is stiff** | **τ 0.45 s, 28 mΩ source impedance** | ✅ **TRACK removes the step:** a 3.8 V peak decays to **+0.096 V** within ~3 s, with a fitted **τ = 0.448 s** against the gated HPF's 0.66 s — faster because the LED's thermal droop pulls the same way. ✅ **HOLD holds:** the step sits at ~2.1 V for seconds, which is the fifth independent confirmation that GPIO33 = 1 is HOLD. ✅ **The supply is stiff:** 0.79 A of beam moves the rail only 20 mV, i.e. **~28 mΩ** of source impedance including leads. 🔵 That stiffness is *why* the beam is a poor Q8 stimulus — a better supply makes the electrical term smaller while the optical term is unchanged |
| 8/28/2026 | 🔵 **The §3.5 and §3.6b numbers only compare if they live on the same node** | **both belong at the comparator input** | 🔵 §3.5 solves `ADC5 = vref·duty + Vos` and the threshold it produces is a voltage **at the comparator input** — that is what U15 compares against. §3.6b's pass criterion is "the excursion stays under 20 % of that threshold". **Both numbers therefore belong at R102 pad 2**, and reading one at ADC5 and the other at pad 2 would compare a clipped copy against an unclipped bar. ✅ Pass criteria reworded to say so explicitly |
| 8/28/2026 | ✅ **RESOLVED 8/31 — `hpf test` has now run and is saved (slot A seq 5).** 🔴 **`hpf test` HAD NEVER RUN ON BOARD 3 — bring-up §7 was skipped** | **caught by the new three-state `hpf` message** | 🔴 `hpf` reports **"polarity is a compiled HYPOTHESIS -- `hpf test` has not run on this board"**, i.e. `cfg()->hpf_sel_track` is still the **0xff** sentinel. So §3.4 *and* §3.5 were both completed on board 3 with the TRACK/HOLD polarity unverified, and `cfg save` has persisted a record that says so. 🔵 **The message earned itself on its first outing** — the old unconditional version said the same thing on every board forever, so it carried no information and nobody would have noticed |
| 8/28/2026 | ✅ **…but the §3.5 log PROVES the polarity anyway, for free** | **HOLD held a 2225-code step for seconds** | ✅ The high-point step only works if HOLD actually holds: freeze the node, step the beam, read a level seconds later. **If the polarity were inverted, `hpf hold` would select TRACK and the step would decay with τ = 0.66 s** — the `adc 5` read and the 1.3 s sweep both happen well after that, so there would be nothing to find. Board 3 read **2225 codes** and then flipped at **1.735 V**. 🔵 **Fourth independent confirmation that GPIO33 = 0 is TRACK** (board 1 bench, TMUX1219 datasheet, board 2 `hpf test`, board 3 §3.5). ⚠ **Still run `hpf test`** — 32 s, and it is what records the polarity in the config and measures the switch leakage |
| 8/28/2026 | ⚠ **Board 3 beam-off crosstalk 3.2 mV — at the bar, 2× board 2** | **not an Rf effect** | ⚠ `ADC5 movement across the sweep` with the beam off: board 2 **1.6 mV**, board 3 **3.2 mV**, against a "< ~3 mV" bar. Harmless against a working threshold of hundreds of mV. 🔵 **It will not scale with the TIA rework:** GPIO44 couples into the detect node **after** the ×14.5, so a fixed injected voltage stays a fixed voltage at ADC5 whatever Rf is. This is a board/layout difference. ⚠ Re-check if a later board reads higher; the fix is `DAC_TOP` 2047 → 73 kHz or 511 → 293 kHz |
| 8/28/2026 | ✅ **The `amp spread` guard did its job on its first real run — 4 %** | **against 27 % for the hand** | ✅ First use of the new across-points check: a clamped card gave **4 %**, comfortably inside the 20 % limit, against the **27 %** the moving hand produced the day before. 🔵 **The guard now has both a pass and a fail on record**, which is what makes a threshold trustworthy rather than guessed |
| 8/28/2026 | 🔵 **`level` measured the HPF time constant from its own settling curve — τ = 0.653 s** | **design value 0.66 s** | 🔵 Board 3's first `level` run climbed 9 % → 66 % over ~8 s. The deficit from the final value fell by a constant factor **0.736 per 200 ms reading**, i.e. **τ = 0.653 s** — the gated HPF's own R96 2 MΩ × C81 330 nF = **0.66 s**, recovered by the command from its own output with no extra instrument. ⚠ **Practical consequence: the first ~3.5 s (5τ) of `level` is the filter, not the target.** ✅ Those readings are now labelled `settling` so nobody reads "9 %" and reaches for the card |
| 8/28/2026 | ⚠ **Board 3 shows a REPRODUCIBLE +8 % delay trend; board 2 did not** | **+8.4 % and +7.9 % vs −3.5 % and −0.05 %** | ⚠ Two board-3 model runs with stable scenes: delay **335 → 363 ns (+8.4 %)** and **342 → 369 ns (+7.9 %)** across 80–200 kHz. Board 2 gave **−3.5 %** and **−0.05 %** — not reproducible with itself. 🔵 **So the span may be better determined per board than the board-2 data suggested**, and board 3's sign is the opposite of what a single TIA pole gives (a pole makes delay DECREASE with frequency). ⚠ **Do not chase it yet:** both runs still pass PURE DELAY at ~1°, and board 3's residual is 0.23–0.26° against board 2's 0.07–0.08°, so the fit is worse even as the span is steadier. Worth revisiting only if a third board reproduces a per-board sign |
| 8/28/2026 | ⚠ **`help` was missing `cal`, `scan` and everything after `detect`** | **the two most important Phase 3 commands were undocumented** | ⚠ `cal demod`, `cal model`, `cal gain` and `scan carrier` appear nowhere in `k_help`, so the only way to discover them was the bench docs. ✅ Added, with their runtimes (~26 s, ~128 s, minutes) and their preconditions (`cal model`: nothing may move; `scan carrier`: needs a fitted model, run in final geometry) — the two things most likely to waste a run |
| 8/28/2026 | ⚠ **`help` printed literal `%%` — it is emitted with `%s`, not as a format string** | **6 places** | ⚠ `k_help` is passed to `printf("%s", k_help)`, so C's `%%` escape never gets processed and the text showed `30%%`, `~5%%`, `50%%` and so on. Pre-existing, cosmetic, and exactly the kind of thing that survives because nobody reads their own help text. ✅ Fixed all 6 |
| 8/28/2026 | ⚠ **The "no reflector" instruction was conditional and I wrote it as absolute** | **right for board 2, wrong for board 3** | ⚠ §3.4 said flatly **"Setup: no reflector"**, which was correct for board 2 (stock 470 kΩ, on the bench, crosstalk alone 1.91× over the ceiling) and **wrong for board 3** (116 kΩ, final location, too little return to calibrate bare). Following it as written is what led to a hand being used as a target. ✅ **Rewritten as a level target — 50–70 % of full scale — with a table of which direction each situation needs.** 🔵 The underlying error is recording a *conclusion* that depended on unstated conditions, rather than the *criterion* that generated it |
| 8/28/2026 | ✅ **The `cal demod` phase from that run is still usable: 1315 ticks** | **4 ticks from the previous 1311** | ✅ Despite the moving hand, `cal demod` gave **1315 ticks / 367 ns**, against **1311 / 340 ns** on the clean run — **4 ticks apart, inside the 7-tick run-to-run scatter** seen on board 2. All gates clean (sat 0 %, quad null 0.0, h3 0.099 vs 0.111). 🔵 **Why the single-frequency sweep survived what the model did not:** the zero crossings set the phase, and a slowly varying amplitude scales the whole response without moving them. **The model is the fragile one**, because it compares amplitudes across points taken minutes apart. ⚠ **It was NOT saved** — `cfg save` refused with "the beam is on" |
| 8/28/2026 | ✅ **So the 95 ns non-TIA delay difference stands, and it is board-to-board** | **a ~320 ns path nobody has measured** | ✅ With Cf = 0.99 pF confirmed, the TIA accounts for 119 ns of the 213 ns phase drop and the remaining **95 ns is genuine board-to-board variation** in GPIO31 → U10 → U9 → FET → D11. That path is ~320 ns on board 2 and has never been measured on any board, so a 30 % spread across two boards is unremarkable. ✅ **The two-channel GPIO31/TP7 capture would still settle it outright** — third time it would have answered a question |
| 8/28/2026 | ✅ **Cf_opt can be read straight off the ring frequency — no Cin, no GBW** | **Cf_opt = 1/(2π·Rf·f_ring)** | ✅ For a TIA the ring sits at `f_n = sqrt(GBW/(2π·Rf·(Cin+Cf)))` and the maximally-flat value is `Cf_opt = sqrt(Cin/(2π·Rf·GBW))`. **Combine them and both unknowns cancel**, leaving `Cf_opt = 1/(2π·Rf·f_ring)`. 🔵 **That matters here because Cin and GBW are both estimates** — `NEXT_BOARD_REV` uses Cin ~20 pF and 8 MHz, flags Cin as unconfirmed, and the answer swings 1.3–2.6 pF across plausible values. **Measuring f_ring on the scope removes the guesswork entirely.** From the 8/27 photo f_ring ≈ 0.8–1.0 MHz → **Cf_opt 1.4–1.7 pF**, against 0.99 pF fitted |
| 8/28/2026 | ✅ **Sizing the extra Cf: +0.5 pF on the other leg is the calculated answer** | **Cf 0.99 → 1.48 pF** | ✅ Leg 1 is 101 pF (1 pF + the 100 pF), so the total is set by leg 2. **Add 0.5 pF → leg 2 = 1.5 pF → Cf = 1.48 pF, pole 928 kHz, lag 6.40° at 104 kHz.** ⚠ **1 pF would also work but gives back the rework's benefit:** Cf = 1.96 pF, pole **700 kHz**, lag **8.47°** — essentially board 2's stock 8.74°, and τ back to 227 ns. 🔵 At 25 % duty the 2.4 µs pulse settles in 10 τ either way, so it only matters if low-duty operation ever returns. 🔴 **Either change moves the demod phase — re-run `cal demod` and `cal model`, and this time do a BEFORE run on the same board first** |
| 8/27/2026 | ✅ **`NEXT_BOARD_REV` called this in advance — "0.5 pF is badly under-compensated at ~100 kΩ"** | **the doc was right and I did not check it against the evidence** | ✅ The CR-15 mitigation #2 write-up already carried a compensation table saying that dropping Rf to ~100 kΩ while leaving Cf at 0.5 pF is **badly under-compensated** — and that is exactly the state the scope shows. ⚠ **I computed the Cf = 0.99 pF case and stopped there**, because the rework description said 100 pF had been added; I never asked whether the measurements were consistent with it. **Two numbers already in hand (the 1.80× phase overshoot and the visible ringing) both pointed the other way** |
| 8/27/2026 | 🔵 **The ringing does NOT corrupt the detection measurement — it is a margin problem** | **~0.8–1 MHz, against a 15.39 kHz LPF** | 🔵 The ring sits far above the 4th-order 15.39 kHz LPF that follows the demodulator: even after mixing to 900 ± 104 kHz it is attenuated by roughly **−140 dB**, so the demodulated output is essentially unaffected — and indeed every `cal demod` gate passed (sat 0 %, quad null 0.0, h3 0.101). ⚠ **What it does cost:** ~90 mV of headroom, and stability margin that degrades with temperature and part tolerance. ✅ **Fix:** get Cf to ~1.85 pF. If the 100 pF is simply not connected, fixing that gives 0.99 pF (ratio 0.53, same as board 2 — acceptable). **~1 pF on the other leg as well gives 1.96 pF, ratio 1.06 — properly damped** |
| 8/27/2026 | ⚠ **The scope showed a Ch1 CLIPPING warning — re-take with more vertical range** | **200 mV/div** | ⚠ The Tek 5-Series flagged **Clipping** on Ch1 at 200 mV/div. The visible trace sits inside the window, so it may be a transient elsewhere in the 625 kpt record — but **if anything clipped, the overshoot reading is a lower bound and the swing may be understated.** 🔵 Both of those feed the Cf argument above, so re-take at 500 mV/div before treating the 12 % overshoot figure as firm |
| 8/25/2026 | 🔴 **95 ns of the delay drop is NOT the TIA, and this experiment cannot say what it is** | **non-TIA delay 320 ns (b2) vs 225 ns (b3)** | 🔴 Taking the TIA's own contribution out — 233 ns at 8.74° on board 2, 115 ns at 4.30° on board 3 — leaves **320 ns and 225 ns** of everything else. That path is **GPIO31 → U10 → U9 → FET → D11**, and it has never been measured on any board. ⚠ **Board 2 and board 3 are different boards**, so the TIA change and board-to-board variation are confounded. ✅ **The measurement that settles it: two LA channels on GPIO31 and TP7, read the delay directly** — one board, no inference. That has been on the "worth doing" list since 8/24 and this is the second time it would have answered a question |
| 8/25/2026 | 🔵 **PROCESS — a rework acceptance test needs a BEFORE run on the SAME board** | **26 seconds that would have closed this** | 🔵 Board 3 was reworked and then calibrated, with no stock baseline of its own. So the only available comparison is against board 2, which differs in every uncontrolled way as well as in the one controlled way. **A `cal demod` before touching the iron would have made the comparison exact.** ⚠ Written into `BRINGUP_NEW_BOARD.md` §9 as a red instruction, because the cost is 26 s and the alternative is a permanently ambiguous result |
| 8/25/2026 | ⚠ **Board 3 model quality is 3× worse than board 2 — watch it** | **residual 0.23° vs 0.07–0.08°** | ⚠ Still comfortably usable and still **PURE DELAY**, and the model agrees with `cal demod` to 3.5 ticks. But the fit residual is 0.0040 rad against board 2's 0.0013–0.0014, and the delay now trends **+8.4 %** across 80–200 kHz where board 2 gave −3.5 % and −0.05 %. 🔵 **The span figure was already established as not-well-determined** (8/25 row above), so +8.4 % is not itself alarming — **the residual is the number to watch.** If more boards get reworked and they all sit near 0.23°, that is a property of the lower-Rf configuration, not noise |
| 8/25/2026 | ⚠ **Board 3 sits at 80 % of full scale — back it off before `scan carrier`** | **predicted 47 %, measured 80 %** | ⚠ I predicted the D12 attenuator would become unnecessary at 4.05× less gain (board 2's unattenuated 6.30 V scaling to 1.55 V = 47 % FS). Board 3 reads **80 %**, so its optical coupling is roughly **1.7× higher** than board 2's — different baffling or geometry. ✅ Not a failure: **sat 0 %** and nothing clipped. 🔴 **But §3.6 sweeps 80–200 kHz on this same attenuation**, and any frequency returning more signal would clip and corrupt that row's σ and SNR. **Trim toward ~65 % before the scan** |
| 8/25/2026 | 🔵 **The acceptance test for the board 3 rework: a −18 tick phase shift** | **1343 → ~1325, delay 553 → ~435 ns** | 🔵 The TIA's lag at 104.1667 kHz falls **8.74° → 4.30°**, so the chain needs **4.44° less** demod offset = **17.8 ticks** = 118 ns. **That prediction follows from the pole move and nothing else**, which makes it a clean check on whether the rework did what the arithmetic says. ⚠ **If `cal demod` lands near 1343 instead, check which pads the 100 pF bridges before trusting anything downstream.** 🔵 Also expect the D12 attenuator to become unnecessary: board 2's unattenuated 6.30 V peak against a 3.3 V ceiling scales to **1.55 V = 47 % of full scale** |
| 8/25/2026 | ⚠ **CORRECTION — the rework does NOT improve TIA damping** | **Cf/Cf_required 0.54 → 0.53** | ⚠ **I first said "both changes push the right way" on stability. That was wrong.** Against `NEXT_BOARD_REV.md`'s own compensation table (Cin ~20 pF, OPA4323 8 MHz GBW), the required Cf scales as `sqrt(Cin/(2π·Rf·GBW))` — so cutting Rf by 4.05× raises the requirement from 0.92 to 1.85 pF at the same time as the fitted value goes 0.50 → 0.99 pF. **The ratio is unchanged.** Expect **similar overshoot at roughly 2× the frequency** — the ~700 kHz ring measured on board 2's TP7 coherent average should move toward ~1.4 MHz. ✅ **To actually damp it:** ~1 pF across the OTHER leg too gives legs of 101 pF and 2 pF → Cf = **1.96 pF** against the 1.85 pF wanted |
| 8/25/2026 | ✅ **Docs updated for the per-board TIA variant** | **`BENCH_P3_DETECT`, `BRINGUP_NEW_BOARD`, `NEXT_BOARD_REV`** | ✅ Nearly every number in `BENCH_P3_DETECT.md` scales with Rf, so the variant is now a **first-class per-board property**: a variant table at the head of the TIA section, the servo corner marked as Rf-dependent, and a **new `BRINGUP_NEW_BOARD.md` §0** that makes reading R80 and the Cf leg the first thing done on any board — before anything is energised. ✅ **New `BRINGUP_NEW_BOARD.md` §9** covers `cal demod` and `cal model` step-by-step (board state, level targeting, pass criteria, `cfg save`), and the sign-off table now carries the variant, the phase, the chain delay and the model verdict. 🔵 **The old §9 hand-off listed `cal model` as a to-do with no procedure**; it now points at §9 and keeps only `detect path`, which is genuinely a §3.7 measurement |
| 8/25/2026 | ⚠ **The DAC reference is 3.268 V, not the compiled 3.300 — every printed threshold reads ~1 % high** | **measured, between the two candidates** | ⚠ `detect_threshold_volts()` multiplies duty by a hardcoded **3.300 V**, and the CLI already hedged with "measured +3V3 was 3.246 -- TP8 is the truth". The two-point solve puts the effective reference at **3.268 V**, i.e. between the nominal and the rail measurement, and closer to the nominal. ✅ **Added `threshold vref <volts>`** — `detect_threshold_set_vref()` already existed with **no CLI path**, so the number had nowhere to go. 🔵 **RAM only, on purpose:** 1 % on a threshold that §3.7 tunes empirically does not justify another field in a fixed-size config record. If absolute threshold accuracy ever matters, this is the number to persist next to `adc5v_scale` |
| 8/25/2026 | ✅ **Comparator input offset measured: +11.0 mV** | **LM393 spec is ±15 mV** | ✅ Falls straight out of the two-point solve, and it explains the low point that looked wrong: a flip at TP8 = 7.7 mV against an ADC5 reading of 18.5 mV is **not** a scale error, it is the comparator deciding 11 mV early. 🔵 **Consequence for §3.7:** the effective detection threshold is ~11 mV **below** whatever the CLI prints. Against a working threshold of a few hundred mV that is 2–5 %, so it does not change the design — but it does mean the threshold should be set from the measured transit peak, never from the printed volts alone |
| 8/25/2026 | ✅ **GPIO44 crosstalk: 1.6 mV, beam off — PASS** | **vs 12.9 mV with the beam on** | ✅ `threshold sweep 0 100 64` with the beam **off** and the HPF in TRACK gives **1.6 mV** of ADC5 movement, against a 3 mV bar and the §3.3 quiet σ of 0.55–0.65 mV. **The two RC poles are doing their job and DAC_TOP does not need to move.** 🔵 The same sweep with the beam **on** read 12.9 mV — that difference is the optical background, not crosstalk, and it is why this check has to be run dark |
| 8/25/2026 | 🔵 **A beam-off threshold sweep DOES flip, and it means nothing** | **ADC5 idles at ~9.7 mV, not 0** | 🔵 Board 2 reported `FLIP at duty 1.56 % = 0.0516 V` with the beam off. That is not a fault and not a signal: ADC5 sits at its TRACK quiescent, the comparator crosses it, and a 0–100 % sweep in 64 steps has **51.6 mV granularity** — so a flip in the first step resolves nothing beyond "below 51.6 mV". ⚠ **My doc said "ignore the NO FLIP", which presumed there would not be one.** Corrected to ignore the flip line either way; only the movement number matters in that sweep |
| 8/25/2026 | 🔴 **My §3.5 high-point procedure railed ADC5 — the held step is NOT the chopped step** | **predicted 50–70 % FS, got 4095 and NO FLIP** | 🔴 I wrote step 3 expecting the beam-on step in HOLD to land at 50–70 % of full scale, reasoning from `cal demod`’s 81 % peak. **Wrong by construction:** `cal demod` reports the *chopped differential*, and the 0.66 s HPF **centres** a chopped square so each half sits at roughly half the swing. **A held step is not centred — the whole excursion lands on one side.** So an 81 % chop is a held step well past the rail. ✅ `threshold sweep` was right to report NO FLIP: the comparator input was above the DAC’s own 3.3 V ceiling, so there was no crossing to find. ✅ **Fixed with a knob that costs nothing:** `beam phase` scales the held step down the trapezoid, reversibly, with no optics touched — start at **1035** (~40 % of peak), target `adc 5` = **2000–2500**, and `beam phase 1343` afterwards |
| 8/25/2026 | ⚠ **The crosstalk check must run with the BEAM OFF** | **12.9 mV beam-on is mostly light, not GPIO44** | ⚠ `ADC5 movement across the sweep` is printed by every `threshold sweep`, and my doc gave a **< 10 mV** bar without saying what the beam should be doing. Board 2 read **12.9 mV** on a beam-ON sweep, which reads as a crosstalk failure — but §3.3 already established that the optical background alone is **2.9–14 mV** depending on geometry. ✅ **Corrected:** run it beam-off, ignore the NO FLIP, and expect **< 3 mV** against the §3.3 quiet σ of 0.55–0.65 mV |
| 8/25/2026 | ✅ **§3.5 low point on board 2 — passes, and shows why the high point is needed** | **TP8 2.6 mV vs ADC5 12.1 mV** | ✅ `FLIP at duty 0.08 % = 0.0026 V nominal at TP8`, with `ADC5 there: code 15 = 0.0121 V`. They disagree by **9.5 mV**, and that is a **pass**: the flip landed between DAC level 0 and level 1, so the resolution there is the DAC’s whole **3.2 mV** step, and the LM393’s input offset is spec’d to **±15 mV**. 🔵 **The low point tests the offset and proves both paths respond; it cannot test the scale.** That is what step 3 is for |
| 8/25/2026 | ✅ **What IS robust in the phase model, across two sessions** | **verdict, absolute delay, and cross-agreement** | ✅ **(a) The pure-delay verdict** — both runs pass `CAL_PURE_DELAY_MAX_DEG` by a wide margin (**0.78°** and **0.01°** against a 5° bar). **(b) The absolute delay** — 620.8 vs 571.2 ns at 80 kHz, i.e. reproducible to **~50 ns / 7 ticks**. **(c) Model vs direct measurement** — the model implies **1352.9** and **1346.9** ticks at 104166 Hz against `cal demod`'s measured **1348** and **1343**, agreeing to **4.9 and 3.9 ticks** on each occasion. 🔵 **And the two methods moved TOGETHER between sessions** (−5 ticks measured, −6.0 ticks modelled, same direction) — so the session-to-session shift is something real about the setup, not fit noise. **Fit residual is 0.07–0.08° both times** |
| 8/25/2026 | ✅ **`cal model` re-run — θ(f) = 5.4626 + 4.206e−6·f − 2.213e−12·f²** | **residual 0.0014 rad = 0.08°** | ✅ Five points, 80–200 kHz, all accepted, run on the 8/24 12:49 build (which has the corrected `pure_delay` test). Reports **PURE DELAY to within 0.01°** — the verdict the old `|a0| < 0.15` test could never give. Chain delay **571 ns**, flat across the band. Compare 8/24: 5.4848 + 4.219e−6·f − 1.949e−12·f², residual 0.074°. 🔵 **`cal model`'s delay figure is correct on that build** — the 1200 ns bug is only in `cal_demod_phase`, so `cal demod` printed rubbish while `cal model` did not |
| 8/25/2026 | ✅ **All six BENCH docs given board-state tables and step-by-step procedures** | **state, commands, expected output, pass criteria** | ✅ Every procedure in `BENCH.md`, `BENCH_P2_BEAM.md`, `BENCH_P3_DETECT.md`, `BENCH_P5_P7_MIC_CAMERA.md`, `BENCH_P6_STROBE.md` and `BENCH_P8_PI.md` now opens with a **board state** table — rail up/down, beam on/off with frequency and duty, HPF mode, ADC mode, jumper positions, what must NOT be connected, and what gear is needed — followed by numbered steps with the exact serial commands, expected output and pass criteria. 🔵 **The state that is easiest to get wrong is what must be DISCONNECTED**: J3 for all of Phase 6a/6b, cameras for 7a/7b, J2 for Phase 1, and the Pi for everything before 7c |
| 8/25/2026 | 🔴 **`BENCH_P8_PI.md` prereq was wrong — the Q10 rework is not a gate** | **demoted 2026-07-31, doc never updated** | 🔴 The header read "Prereq: Phases 1b (including the **Q10 rework**) and 2–7 passed", but Q10 was measured on 2026-07-31 and **demoted to optional defence-in-depth** in §3 of this very file. **Every reset also opens the +5 V latch**, so the spurious request reaches a Pi that is losing its rail in the same instant — the level failure is a footnote to the power cut, not an independent hazard. ✅ **Corrected**, with the reasoning inline so it does not creep back. ✅ Also added the real gates to §8.0: phases 2–7 complete, camera-board schematic in hand, SW2 taped |
| 8/25/2026 | 🔴 **`BENCH_P5_P7` §7.0 still told you to go and answer Q6 — it was answered 8/14** | **two sections of one doc disagreed** | 🔴 §7.0 opened "Resolve the 1.8 V I/O question FIRST … get the specific sensor-board schematic and establish what sits between the module header and the sensor pins", while a block **40 lines below it in the same file** carried the full datasheet answer. ✅ **Reconciled:** §7.0 now states the verdict — both directions fail, the 220 Ω resistors fix neither — and names the **one thing genuinely still unknown**, which is whether the *camera board* level shifts. 🔵 **7a is unaffected either way** and remains the right first step: it jumpers J4 to itself with no camera present, so no 1.8 V domain is involved |
| 8/25/2026 | ⚠ **`BENCH_P6` 6a.1 showed the strobe constants as unset placeholders** | **`<measured>` — but Phase 2c set them 8/13** | ⚠ The doc printed `#define STROBE_HW_LIMIT_US_ASSUMED <measured>` as if nothing had been done, when `board.h` has carried **122** and **STROBE_SW_MAX_US 100** since Phase 2c measured U9 at 122.68 µs. ✅ **Corrected to show the real values**, with the open question stated precisely: **U5 has not been measured**, and it is U5 that the constant actually governs. Below ~118 µs the margin tightens; below 100 µs the §15 slow-ball rows do need re-deriving |
| 8/24/2026 | ✅ **§3.4 CLOSED — demod_phase_ticks = 1348 at 104166 Hz** | **four independent agreements** | ✅ `cal demod` clean: **amplitude 2327 (peak 2030 = 50 % of full scale), sat 0 %, quad null 0.4, h3 0.119 against the intrinsic 0.111, warm yes** — committed. Confirmed by the saturated run (**1347**), the offline fit of the rejected 32-point sweeps (**1355**) and the firmware's own model (**1353**). All four inside **8 ticks (2°)**, and a first clean run earlier the same day gave 1350. ⚠ The 8/21 value of **66 ticks is superseded**, not reconciled — conditions were never recorded, and `cal model` leaving the carrier at 200 kHz is the likely explanation |
| 8/24/2026 | ✅ **PHASE MODEL FITTED — θ(f) = 5.4848 + 4.219e−6·f − 1.949e−12·f²** | **residual 0.0013 rad = 0.074°** | ✅ Five points, 80–200 kHz, all accepted at 64 pts / 8 cycles. ⚠ **It printed "NOT a pure delay" and that verdict was wrong — see the rows below.** Evaluates to **1352.9 ticks at 104166 Hz against `cal demod`'s measured 1348** — 5 ticks. θ runs **332.88 → 339.50 → 345.91 → 352.12 → 358.14°**. ⚠ **Do not read the coefficients physically** — a0 = 5.4848 rad absorbs the 2π wrap; it is a local interpolant, which is why the command refuses to extrapolate. 🔴 **`cfg save` to persist** — the phase and the model are both in the config record and both are lost on reset otherwise |
| 8/24/2026 | ✅ **h3 and amplitude guards are now DUTY-AWARE** | **h3_exp = \|sinc(3D)/(3·sinc(D))\|, form = 4·sinc(D)/π** | ✅ The response is a duty-D pulse correlated with a 50 % square — a **trapezoid**, with intrinsic odd harmonics. Both follow from the Fourier series: harmonic n (odd) is **R_n = 4·D·ΔV·sinc(nD)/(nπ)** and the true peak is **D·ΔV**. Consequences: **(1)** a clean response has h3/h1 = **0.111 at 25 %**, **0.180 at 20 %**, **0.291 at 10 %**, so the old fixed 0.15 bar **rejected a clean signal below ~20 % duty** — a trap, since the saturation message says to reduce the light and dropping duty is the obvious way. Bar is now `h3_expected(duty) + 0.05`. **(2)** the fitted fundamental is **1.146× the peak** at 25 % duty, so `amplitude <= 4095` was ~15 % biased against a clean signal; the check is now on the recovered **peak**. ✅ **Replayed against all 8 historical runs: every verdict unchanged**, and the 77 %-saturated run still fails on three independent grounds |
| 8/24/2026 | ⚠ **h3 loses its discriminating power below ~15 % duty** | **both clean and clipped → 1/3** | ⚠ As duty → 0 the clean response becomes a square wave in phase, so h3_expected → **0.333** — exactly what hard clipping gives. At 10 % duty the bar is 0.341 against a square's 0.333, i.e. **no contrast left**. 🔵 **`sat_frac` is the guard that carries the load there**: it is a direct count of points against the rail with no duty dependence at all. The CLI now prints this warning itself whenever duty < 15 %. 🔵 h5 would have better contrast (0.129 clean vs 0.200 square at 10 % duty) if this ever needs strengthening |
| 8/24/2026 | ✅ **h2/h1 CANNOT measure this chain — removed from the validity test** | **structurally zero for any optical input** | ✅ **A 50 % square demodulator emits only ODD harmonics of the phase sweep**, whatever the optical waveform is, so h2 ≡ 0 for every possible signal — reconstructing the sweep from the 50 MS/s TP7 capture gives **0.0000 to four places**. It was gating `cal model` at 0.26–0.38 and rejecting all 5 points. 🔵 **What it actually tracks is sweep rate, not signal:** 400 ms/point → **0.124–0.148**, 300 ms/point → **0.261–0.381**, and **doubling the amplitude at four frequencies moved it by ≤ 0.001**. ⚠ My own prediction that raising the amplitude would fix it was wrong and the data refuted it cleanly. ✅ Kept computed and printed as a sweep-health diagnostic; `valid` is now `amp && h3 && sat && null`. Those still catch the saturation — the 34 %-saturated run failed on **sat** and on **amplitude 4427 > 4095** |
| 8/24/2026 | ✅ **`cal model` now sweeps at 64 points / 8 cycles** | **was 32/6 = 300 ms/point** | ✅ 300 ms per point against a **0.66 s HPF** leaves every point still relaxing from the one before it, which is what drove h2 to 0.26–0.38 and widened the dead zone to 4 points. Now identical to the `cal demod` sweep that passes. Costs **~128 s for five points instead of ~48 s**. 🔵 **The dead zone is real and survives desaturation** — 3–4 points per sweep read *exactly* ±0.0 codes, which is both chop halves against ADC5's bottom rail: the node idles ~11 codes above 0 V, so a bipolar chop has essentially no negative headroom |
| 8/24/2026 | ✅ **Phase model fitted OFFLINE from the rejected points — the data was always good** | **residual 0.34°** | ✅ The five sweeps `cal model` threw away fit **θ(f) = 5.3690 + 5.959e−6·f − 6.994e−12·f² rad** over 80–200 kHz with a residual of **0.0059 rad (0.34°)**. θ runs **332.11 → 340.91 → 347.33 → 353.83 → 360.05°**, smooth and monotonic, no bad point anywhere. It evaluates to **1355 ticks at 104166 Hz against `cal demod`'s measured 1350** — two independent methods agreeing to **5 ticks (1.25°)**. **Only the gate was wrong** |
| 8/24/2026 | ✅ **First clean `cal demod` — 1350 ticks at 104166 Hz** | **superseded by the 1348 re-run, 2 ticks** | ⚠ **Not the final number** — see the §3.4 CLOSED row above, which supersedes this at **1348** from a re-run with a better amplitude (2327 vs 1615 fitted). The 2-tick difference is 13 ns and inside the scatter. Recorded because it was the first sweep to pass every gate. ✅ Saturated run **1347**, this run **1350**, offline model fit **1355**. All inside 8 ticks (2°). ⚠ **The 8/21 value of 66 ticks is superseded, not reconciled** — it was taken under conditions that were never recorded. ✅ **`h3/h1 = 0.110` against the predicted intrinsic 1/9 = 0.1111** for a 25 %-duty pulse against a square demodulator, and `quad null = −0.0`: the trapezoid model of the lock-in response is confirmed on hardware |
| 8/24/2026 | ⚠ **The lock-in response is a TRAPEZOID, not a cosine — three guards had physics-mismatch bugs** | **h3 intrinsic = (1/3)·\|sinc(3D)/sinc(D)\|** | ⚠ The response is a duty-D pulse correlated with a 50 % square, so it is trapezoidal and carries **intrinsic odd harmonics**: h3/h1 = **0.111 at 25 % duty** (exactly 1/9, because sin(0.75π) = sin(0.25π)), **0.180 at 20 %**, **0.268 at 12.5 %**. 🔴 **So the 0.15 h3 bar sits only 1.35× above a perfectly clean 25 % signal and REJECTS a clean signal below ~20 % duty** — while the saturation message tells you to reduce the light, and lowering duty is the obvious way to do it. ⚠ Also: the cosine fit reads **12.8 % HIGH** on a clean trapezoid (fundamental/peak = 1.1285), so `amplitude <= ADC_FULL_SCALE` really caps usable signal at **~89 % of full scale**. 🔴 **Both bars should be made duty-aware before anyone calibrates at a duty other than 25 %** |
| 8/24/2026 | ✅ **Saturation quantified from the TP7 capture, independent of the ADC** | **1.91× over the 3.3 V ceiling** | ✅ 50 MS/s logic-analyser capture of TP7, 562 periods coherently averaged: 104.1667 kHz, **25.42 % duty**, top 2.914 V, bottom 1.172 V, **swing 1.795 V**, with a 52 mV overshoot and ~700 kHz ringing on the trailing edge (the TIA's 677 kHz pole, mildly underdamped). Correlating that against a 50 % square gives a peak of **434.4 mV at TP10 → 6.30 V at ADC5 against 3.3 V**. 🔴 **Taken with NO reflector — so D11→D12 crosstalk plus floor return alone over-drives the chopped calibration by ~2×.** The bench doc's advice (move the reflector back, grey card, remove it) cannot work; **only attenuating light into D12 does**. 🔵 A reflector is not needed for this calibration at all — crosstalk traverses the same chain and the path difference is ~2 m = **1 tick** at 104 kHz |
| 8/24/2026 | ✅ **CHAIN DELAY = 620 ns, PWM edge to demod input** | **the physical result behind `phase_ticks`** | ✅ Strip the geometric term off the tick count and what is left is a time: `t_chain = phase_ticks + (duty/2)·(TOP+1)`, unwrapped. Across the fitted band: **620.8 ns at 80 kHz, 619.3 at 104 kHz, 613.3 at 140 kHz, 606.6 at 170 kHz, 599.1 at 200 kHz** — a **−3.5 %** droop. 🔵 **This is the number that transfers**: it compares between boards, against the netlist, and across carriers. `phase_ticks` does none of those. `cal demod` and `cal model` now both print it. ⚠ **Budget: the TIA accounts for 233 ns** (R80 470 k × 0.5 pF = 677 kHz, 8.74° at 104 kHz — exactly the netlist prediction), leaving **~386 ns unexplained**, most plausibly the drive path GPIO31 → U10 → U9 → FET → D11, which no measurement has ever covered |
| 8/24/2026 | 🔴 **The frequency dependence of `phase_ticks` is GEOMETRY, not dispersion** | **`phase_ticks = t_chain − (duty/2)·(TOP+1)`** | 🔴 **This corrects the premise the whole phase model was built on.** A duty-D pulse is centred D/2 of the way into the period, and the period in ticks scales as SYSCLK/f — so at 25 % duty the second term alone swings **−234 ticks at 80 kHz to −94 at 200 kHz**. That is what makes `phase_ticks` run **1734 → 746** across the band, and it would be there for a perfectly delay-like chain. ✅ The two-term formula reproduces all five fitted points to **≤ 3 ticks**. ⚠ **Consequence: the phase also moves if the DUTY changes** — `Δticks = (TOP+1)·(D₁−D₂)/2`, i.e. 90 ticks between 25 % and 12.5 %. Re-run `cal demod` after any duty change |
| 8/24/2026 | 🔴 **`cal model`'s `pure_delay` test was reading a meaningless coefficient — FIXED** | **|a0| < 0.15, and a0 is a 2π wrap** | 🔴 The test was `|a0| < 0.15 && |a2|·f² < 0.15`. On the good 8/24 fit **a0 = 5.4848 rad, which is −45.7° at DC** — physically impossible, and PROGRESS already said not to read the coefficients physically. The curvature half **passed** (0.078 < 0.15); only the meaningless half fired, so it printed **"NOT a pure delay" for a chain whose delay varies 3.5 %**. ✅ **Fixed:** the verdict is now the delay spread across the fitted band, expressed as the worst-case phase error from holding one number — **0.78° here**, against a 5° bar. ⚠ **This is the fourth guard in `cal.c` to compare a physically meaningless quantity against a fixed bar** (after h2-vs-clipping, the unenforced quad null, and the duty-blind h3). The pattern is worth watching for |
| 8/24/2026 | ✅ **Why the model is still required, and it is not dispersion** | **holding one tick count INVERTS the response mid-band** | ✅ Signal scales as cos(phase error). Holding `phase_ticks` = 1348 across the scan band: **80 kHz −74° (27 % of signal), 140 kHz +107°, 170 kHz −162°, 200 kHz −71° (32 %)**. 🔴 **At 140 and 170 kHz the error passes quadrature and the correlation goes NEGATIVE** — `scan carrier` would rank those candidates below zero, not merely badly. ✅ By contrast, holding the constant **delay** costs **1.46° worst case at 200 kHz = 99.97 % of signal**. 🔵 **So the model exists to recompute ticks per frequency, not because the chain is badly dispersive** |

> ⚠ **Correction to earlier guidance in this file.** An earlier revision said the
> reset test passes or fails on the **width** of the GPIO43 low — "a few ms = the pad,
> 200 ms = a real assertion." **That criterion is not usable, and the 7/31 bench data
> is why:** SW2 holds the chip *in* reset, so the low lasts as long as the button is
> held. 116 ms measured the operator's reflexes; a deliberate hold sails past 200 ms
> with nothing wrong.
>
> **The correct discriminator is correlation, not duration.** Scope RUN and GPIO43 on
> two channels: GPIO43 low *only* while RUN is low, plus a short boot tail → pass.
> GPIO43 low at any moment while RUN is **high** → firmware asserted → fail.
>
> The number actually worth recording is **RUN rising edge → GPIO43 rising edge**: the
> true "firmware not yet running" window, independent of press duration. **Not yet
> taken** — it needs a two-channel capture.

---

## 7. Source layout

```
firmware/
  CMakeLists.txt              build; PICO_BOARD=pitrac_ltb_v1, PICO_PLATFORM=rp2350
  pico_sdk_import.cmake
  boards/pitrac_ltb_v1.h      RP2354B board header (48 GPIO, 2 MB internal flash, 12 MHz XOSC)
  src/board.h                 PIN MAP — single source of truth. Netlist-verified. Start here.
  src/safe_state.[ch]         GPIO safe defaults + fault latch. Called first in main().
  src/adc_engine.[ch]         ADC modes (IDLE/ARMED/BURST), DMA block capture, volts helpers
  src/power_fsm.[ch]          Phase 1 latch + Phase 1b Pi soft-shutdown FSM
  src/panel.[ch]              Phase 1c J7 indicators — PWM brightness, named patterns
  src/beam.[ch]               Phase 2 carrier + phase-locked demod clock  [IN BUILD, inert]
  src/strobe_burst.pio        Phase 6 PIO burst engine   [NOT in the build yet]
  src/cli.[ch]                USB-CDC line CLI, incl. `capture` (the bench instrument)
  src/main.c                  core 0 superloop
  tools/scope.py              plots a `capture` block from the CLI
  tools/la_phase.py           Phase 2a checks 1-7 from a 2-channel LA CSV. Auto-identifies
                              carrier vs demod by duty; circular statistics throughout
                              (sign convention and wrap both matter -- see its docstring);
                              `--compare` does the check-7 run-to-run spread
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

- ~~The code has never been compiled.~~ **Builds clean as of 2026-07-29.** First-build fixes
  applied: `adc_engine.h` used the SDK's `uint` typedef without including SDK headers (now
  plain `unsigned`, so the header is self-contained); `cli.c` needed `hardware/clocks.h`;
  two dead `enum < 0` comparisons removed. The ADC API (`adc_fifo_setup`,
  `adc_set_round_robin`, `adc_fifo_drain`) matches SDK 2.3.0 as written.
- **Only the USB-power paths have been exercised on hardware.** Phase 0 proved the CLI, the
  ADC read path, and DMA block capture. Everything that needs the +5 V rail — the latch, the
  boost, the whole analog chain — is still untested. A clean build proves nothing about
  runtime behaviour, and Phase 0 only touched the always-on +3V3 domain.
- ~~`git init` at the project root.~~ **Done** — the repo has commits through
  *"Initial commit - phase 1 complete"*. (This entry used to say "not yet done"; it is stale.)
- **Hardware watchdog is intentionally not enabled yet** (see the comment in `main.c`). A
  watchdog reset drops GPIO15 → R12 pulls low → Pi power yanked. Policy from the plan: enable
  the reset action only while the Pi is down. Wire that up during Phase 1b/8.
- **`adc5vcal` scale is RAM-only** — lost on reset, but the *compile-time default* is now
  **1.063**, not 1.0, so a cold boot is approximately right. ⚠ **Corrects an earlier claim**
  that the guard worked uncalibrated: it did not. The raw read path is ~5.9% low (see Q9),
  so with a 1.0 scale the firmware reported a good 5.2 V supply as 4.89 V — under
  `V5_MIN_FOR_LATCH` — and would have refused to latch at all. With the 1.063 default it is
  correct on a cold boot; `adc5vcal` trims the remaining per-unit error.
  **Flash persistence is scheduled for Phase 3**, when `demod_phase_ticks` makes it genuinely
  necessary (nobody wants to re-run a 64-point phase sweep every boot). Build it once as a
  versioned, CRC'd config block: on RP2350 the writing code must run from RAM with interrupts
  disabled and core 1 parked, since the chip executes XIP from that same flash. Until then,
  write the scale in §6 and re-enter it when accuracy matters.
- ✅ **`reset` and `bootsel` are guarded** (added 2026-07-31). Both refuse while
  `power_pi_present()` and the latch is closed, because both reset the pads and therefore
  **open the +5 V latch** — a hard power cut to the Pi, not a reboot (§2). `reset force` /
  `bootsel force` override, deliberately. This closes the most plausible accidental path:
  typing `reset` on a console during Phase 8 without thinking about the machine on the header.
  **It does not close the others** — SW2, the watchdog, and a +5V_IN brownout all still cut
  the Pi. SW2 is operator discipline; the watchdog is already policy-limited to "reset action
  enabled only while the Pi is down"; the brownout is unavoidable and takes +3V3 with it anyway.
- 📋 **Halt telemetry — record repeated or unrequested Pi shutdowns.** Requested 2026-07-31.
  If the Pi 5 halts repeatedly, and especially if it halts when nobody asked, that must leave
  a durable trace: it is the exact symptom of a Q10-class fault and the kind of intermittent
  problem that is invisible without a counter. **Full design note in `BENCH_P8_PI.md` §8.6.**
  The short version:
  - **The RP2354 is the authoritative witness** — it is the only party that knows *intent*
    (did we assert RPI5_SHUTDOWN?), and it survives the failure. If the SD card is the
    casualty, the Pi's own log is the least trustworthy record of what happened to it.
    Pi-side journald is corroborating detail, not the primary record.
  - **Ride it on the Phase 3 flash config block** (below) — the counters are a handful of
    words; the hard part is the flash-write machinery, which is being built anyway.
  - Counters: `boot_count`, `pi_shutdown_requested`, `pi_shutdown_clean`,
    `pi_shutdown_timeout`, and the important one, **`pi_down_unrequested`**.
  - ⚠ **Prerequisite that does not exist yet:** `PS_RUNNING` never watches for the Pi going
    down — it only looks for a button press or a shutdown request. If the Pi halts on its
    own the FSM sits in `RUNNING` forever with the rail up and never notices. **That
    detection has to be added before `pi_down_unrequested` can mean anything**, and what the
    board should *do* about it interacts with Q4 (if the header 3V3 does not drop at halt,
    detection rests entirely on `RPI5_ON`).
- Core 1 is unused. It takes the hot path (comparator ISR, speed math, camera handshake,
  strobe burst) from Phase 3 onward.
- PIO strobe burst engine, detection engine, mic, camera, UART protocol — phases 3–8.
- **`capture` is blocking and prints over CDC**, so a 16 k dump takes a few seconds. Fine for
  bench use; it is not a streaming telemetry path. Same for `beam ramp`, `beam sweep`,
  `panel demo` — all correctly blocking, because they are interactive bench tools. The rule
  that matters: **nothing in the armed or firing path may block.**
- ~~🔴 `adc_read_avg()` stops and restarts the ADC.~~ ✅ **FIXED 2026-08-14** — see
  `ARCHITECTURE.md` A1. A 32 KB DMA ring (one channel, RP2350 ENDLESS mode) runs continuously
  in every mode; `adc_read_5vin_volts()` averages ch1 out of it and disturbs nothing.
  `adc_read_avg()` survives for the CLI's `adc <ch>` and is documented as disruptive.
  **Two constraints worth not breaking:** the round-robin channel count must stay 1, 2 or 4
  (the ring size must divide by it or the channel phase rotates on wrap), and an A↔B DMA
  chain does **not** work as a substitute — transfer counts do not reload, so the pair
  stalls silently after one lap each.
- 🟡 **The 35 % beam duty ceiling is enforced in exactly one CLI path, and it is not the
  dangerous one.** `beam duty` checks it (`cli.c`, the `REFUSED` branch), but `beam freq`,
  `beam clamp` and `beam sweep` all reach `beam_configure()` directly and skip the check.
  The concrete trap: **`beam clamp` writes `s_duty = 0.50` persistently**, so a bare
  `beam freq 104167` afterwards runs the carrier at **104 kHz / 50 % duty** — ~1.6 A average
  against a 0.95 A design point, ~5.25 W in D11 against 3.15 W. **U9 does not save you**: its
  one-shot clamps pulse *width* (~86 µs) and a 50 % high phase at 104 kHz is only 4.8 µs,
  nowhere near it. Phase 2's own ordering happens to be safe because 2d opens with
  `beam duty 30`; nothing else is. **Recommended fix:** move the ceiling down into
  `beam_configure()`, which closes all three paths at once. **Not applied** — it is a
  behaviour change and Phase 2 is written around the current behaviour. Until then: type
  `beam duty 2` immediately after every `beam clamp`.
- 🔴 **PWM slice collision: the ready LED and the strobe current DAC are the same channel.**
  Found 2026-07-31 during a doc review. `GPIO12` (READY_LED) and `GPIO28` (GATE_PWM) both map
  to **slice 6A** on RP2350B, so they share one compare register and one wrap/clkdiv and
  cannot be driven independently. `panel.c` owns 6A today; Phase 6b wants it for the 9 A
  current setpoint. **Fix in 6b — take the ready LED off PWM** (plain on/off, or software PWM
  off the A4 timer). `ARCHITECTURE.md` A7 and the PWM SLICE MAP in `board.h`.
  Two further pairs collide and are safe **only** because those pins stay SIO:
  **GPIO15 (the +5 V latch) shares 7B with the beam carrier**, and **GPIO27 (the strobe
  watchdog defeat) shares 5B with the panel button LED.** Never put either on PWM.
  *Also corrected: the slice numbers in `board.h`, `ARCHITECTURE.md` and `BENCH_P2_BEAM.md`
  were all wrong (3B/7B/2A). RP2350B has 12 slices and GPIO ≥ 32 uses
  `8 + ((gpio >> 1) & 3)`. The code resolves slices at runtime, so only the docs were wrong.*
- 🟡 Comparator timing and the camera handshake are both specced as CPU work in the .md
  (ISR and busy-wait respectively). Both should be PIO — `ARCHITECTURE.md` A2/A3. We have
  **8 free state machines across three PIO blocks**; the .md assumed two blocks and
  economised accordingly.

---

## 10. Next session — start here

> ### ✅ RESUME POINT — §3.4 CLOSED, §3.5 is next (2026-08-24)
>
> **`cal demod` and `cal model` both pass on board 2.** `demod_phase_ticks = 1348` at 104166 Hz,
> and the phase model is fitted over 80–200 kHz with a 0.074° residual. Four independent
> measurements of the phase agree to 8 ticks. See §6 for the numbers.
>
> **In physical terms: the chain delay is 620 ns**, PWM edge to demod input, and it is
> nearly constant (620.8 → 599.1 ns over 80–200 kHz). The TIA accounts for 233 ns of it;
> ~386 ns is unexplained and is probably the LED drive path. **Compare the DELAY between
> boards, never `phase_ticks`** — the tick count carries a geometric duty/period term.
>
> 🔴 **FIRST ACTION NEXT SESSION: reflash, then re-run `cal demod` + `cal model`, then
> `cfg save`.** ✅ The calibration IS saved (slot A seq 5) and survives a reset. But the
> record predates `cal_duty`, so the board cannot check the stored phase against the live
> duty — and the beam powers up at **2 %** while the phase was measured at **25 %**, which
> is 166 ticks off. Re-running at 25 % populates `cal_duty` and refreshes the `pure_delay`
> flag, which is stale in the current record. Budget ~3 minutes plus the 5 minute warm-up.
>
> ⚠ **Whatever you do, set `beam duty 25` before trusting the restored phase.**
>
> ✅ **§3.5 is DONE on board 2 (2026-08-25)** — vref 3.268 V, comparator offset +11.0 mV,
> crosstalk 1.6 mV.
>
> ✅ **BOARD 3 is up and calibrated, with a REWORKED TIA** (Rf 116 kΩ, Cf 0.99 pF).
> `demod_phase_ticks` **1311**, chain delay **340 ns**, model residual 0.23°, PURE DELAY.
> **Board 2's numbers do not transfer** — nearly all of them scale with Rf.
>
> ✅ **§3.4 CLOSED on board 3 and SAVED (2026-08-28).** `demod_phase_ticks` **1311**,
> chain delay **340 ns**, model residual 0.26°, amp spread 4 %, PURE DELAY. Target is a
> clamped card at ~66 % of full scale — leave it exactly where it is.
>
> ✅ **§3.5 CLOSED on board 3 (2026-08-28)** — DAC vref **3.256 V**, comparator offset
> **+5.4 mV**, beam-off crosstalk 3.2 mV.
>
> ### Status after 2026-08-31 — most of this list is now closed.
>
> | | | |
> |---|---|---|
> | ✅ | `hpf test` | **DONE**, `cfg save` persisted (slot A seq 5). TRACK = 0 confirmed, leakage **~55 pA** |
> | ⚠ | Fix the scope ground | **STILL NOT DONE.** The 8/31 §3.6b captures carry the same unity-gain common-mode as the 8/28 one |
> | ✅ | **§3.6b Method A** | **DONE — and it FAILS.** Q8 measured **×7.43** in HOLD against ×7.25 predicted. See Q8 |
> | ✅ | **§3.6 Check 2** | **DONE — PASS.** σ_noise flat 4.57–5.08 across 95–115 kHz |
> | 🟡 | §3.6 Check 1 | **Deferred**, needs a scope. Downgraded: Check 2 already answered the question it supports |
> | 🔴 | **`detect`** | **STILL NEVER RUN ON BOARD 3.** 30 s. Confirms GPIO46 reads and the SM arms |
> | 🔴 | **§3.7** | **NEXT** — the first real transits |
>
> 🔵 **The carrier is FIXED at 104.1667 kHz** (decided 2026-08-28, §8), and §3.6 Check 2
> has now verified it is clear. §3.6 is a one-off robustness check, not a per-board scan.
>
> 🔴 **Do NOT adopt `BEST by SNR` from a `scan carrier` run.** Board 3's said 110000 Hz;
> it was **+1.5σ of scatter** on a `signal` column that rises monotonically with elapsed time
> because chopping cools the LED. The firmware now says this itself.
>
> 🔵 **Use `level` whenever the optics are disturbed** — fastest way back to a known
> level, and both §3.5 step 3 and §3.6b disturb them by design.
> ✅ **`BENCH_P3_DETECT.md` carries step-by-step procedures for all
> three**, including the board state each one needs (rail, beam, duty, HPF mode, ADC mode,
> target), the exact serial commands, expected output, and pass criteria. **Read the
> "Running order for 3.5 → 3.7" block first — the section numbers are not the run order**
> (3.5, then **3.6b**, then 3.6, then 3.7).
>
> ⚠ **`scan carrier` must run in the final geometry and lighting**: σ_noise is set by the
> optical background, not the electronics (0.65 / 2.91 / 6.5–14 mV on one board in one
> session), and the scan divides by it.
>
> ⚠ **Two config fields still gate Phase 4:** `path 0.00 mm` (`detect path <mm>`; nothing
> reports velocity until it is set) and the R98 decision, which waits on §3.7.
>
> ⚠ **The chopped calibration is over-driven by crosstalk alone on this board.** With no
> reflector at all, D11→D12 leakage plus floor return puts 6.30 V at ADC5 against a 3.3 V
> ceiling. Attenuation over D12 is currently mechanical and **will need re-tuning on any
> board or geometry change** — target `cal demod` peak at 50–70 % of full scale. Nothing
> conductive near D12 (§11).
>
> ---
>
> ### (below: the 2026-08-21 resume block, superseded)
>
> ### ✅ RESUME POINT — board 2 live, Phase 3 unblocked, §3.4 is next (2026-08-21)
>
> **Board 2 is healthy and configured. Board 1 is set aside** pending a U11 replacement
> (OPA4323IPWR, TSSOP-14, LCSC C22419728) — see §11 for how it died.
>
> **Complete on board 2:** rails ✅ (after the PSU current-limit trap, §6), `adc5vcal` →
> scale **1.0617** ✅, **`hpf test` CONFIRMED — GPIO33 = 0 is TRACK**, 2.9× separation with
> the repeats agreeing to 0.1–0.2 mV ✅, `cfg save` → slot A seq 3 ✅, **CR-15 mitigated —
> linear to 25 % duty** with good baffles and hands clear ✅, and ADC5 quiet in TRACK with
> the beam on: mean 9.2 mV (= the TRACK offset exactly), σ 2.19 mV ✅.
>
> **Next is §3.4 `cal demod`.** *(Superseded: no reflector is needed — see the 8/24 block
> above.)* It needs a **static reflector** and a **warm beam**
> (5 min at 25 % — optical output falls 25–45 % cold→plateau while the electrical
> readings barely move, so a carrier chosen cold is wrong warm).
>
> **Two config fields are still unset, and both gate Phase 4:**
> `path 0.00 mm` (nothing reports velocity until `detect path <mm>`) and
> `phase mdl: not fitted` (needs `cal model`).
>
> ✅ **Ambient rejection is proven** — ~48 dB at 120 Hz, measured labelled at 10 ksps.
> Mains flicker is a solved problem and the synchronous-detection premise holds. The true
> quiet-baseline σ at ADC5 is **0.55–0.65 mV**, 3–4× better than first thought.
>
> ✅ **The "bursty noise" is closed, and it was not noise.** Covering D11's output aperture
> drops σ from 10.30 mV to **2.91 mV**, peaks from 77 to 33 codes, and excess kurtosis from
> −0.3 to +0.1 — from wildly non-Gaussian to essentially Gaussian. **It was the beam
> returning off the room**, which the lock-in passes faithfully because it is genuinely
> modulated at the carrier. The detector was working correctly throughout.
>
> ⚠ **The lasting consequence: σ_noise is not a constant.** Three values on one board in one
> session — **0.65 mV** (nothing returning), **2.91 mV** (D11 covered), **6.5–14 mV** (open to
> the room). It is set by the optical background, not by the electronics.
> 🔴 **So `scan carrier` must run in the final geometry and lighting**, or it ranks carrier
> frequencies against the wrong denominator. There is no bench σ_noise worth quoting.
>
> 🔴 **§3.4 ran and was SATURATED — re-run it.** `cal demod` put **84 % of its sweep against
> the ADC rail**: the response is a square wave, fitted amplitude 5128 codes is *above* full
> scale, h3/h1 = 0.306 against a square's 0.333.
>
> **Fix the light before re-running — the gain cannot go lower.** U12B is already at its
> minimum 14.5 (R98 is DNP and fitting it only raises gain). In order of preference: move the
> reflector further away, use a grey card instead of white, or ND-filter D12.
>
> ✅ **Two firmware defects found and fixed** (reflash needed):
> `h2_ratio` cannot see symmetric clipping — a square wave has h2/h1 = 0 *exactly*, so the
> purity check was blind to the one failure it existed for. Added `h3_ratio`, `sat_frac` and
> an amplitude ≤ full-scale check. And the **quadrature null was printed but never enforced**
> — it reported −4086 where ~0 was required and the phase committed regardless.
>
> ✅ **The answer is probably still 66 ticks.** Clipping preserves zero crossings: the sweep
> crosses at 427 / 1147 where a 66-tick peak predicts 426 / 1146, and the independent
> `cal model` gives 65.4 ticks at 104166 Hz. **Confirm it unsaturated — this was luck.**
>
> ---
>
> ### (below: the 2026-08-14 firmware handover, still accurate for the code)
>
> **All Phase 3/4 firmware is written and builds clean. None of it has run on hardware.**
> Branch `phase3-detection`. Approved plan lives in the user's `.claude/plans/` directory
> (`great-questions-1-let-s-refactored-kahan.md`).
>
> **Build** (the toolchain is private to the Pico extension, not on PATH):
> ```bash
> export PATH="$USERPROFILE/.pico-sdk/cmake/v4.3.4/bin:$USERPROFILE/.pico-sdk/ninja/v1.13.2:$USERPROFILE/.pico-sdk/toolchain/15_2_Rel1/bin:$PATH"
> cmake --build build
> ```
>
> #### New modules
>
> | File | What |
> |---|---|
> | `src/detect.[ch]` | threshold DAC, HPF control, PIO consumer, ADC refinement, pass log |
> | `src/detect.pio` | comparator transit timer (A2) |
> | `src/pio_alloc.[ch]` | PIO block/base map — **the allocation is forced, see A2** |
> | `src/cal.[ch]` | `cal demod`, phase model, `scan carrier`, R98 selection |
> | `src/config_store.[ch]` | versioned CRC'd dual-slot flash config |
>
> #### 🔴 THE BENCH ORDER THAT MATTERS
>
> 1. **`hpf test` FIRST.** The GPIO33 SEL polarity is a compiled *hypothesis* — the netlist
>    encodes only the pin name. Everything downstream (3.4, 3.6, and §3.6b's whole
>    TRACK-vs-HOLD isolation) is wrong if it is inverted. If the test says INVERTED, flip
>    `HPF_SEL_TRACK` in `board.h` and reflash.
> 2. **`detect` with the rail up** — confirm GPIO46 reads and the SM arms. If the PIO never
>    triggers, the fallback is `pio_gpio_init()` + immediate `pindirs = in`; the comment in
>    `detect_pio_init()` explains why it is omitted.
> 3. `threshold sweep` — cross-calibrates the DAC against ADC5 and measures GPIO44 crosstalk.
> 4. §3.6b Q8 rail step — needs step 1 settled first, or it measures the wrong state.
> 5. `cal model` then `scan carrier` — **warm beam, 25 % duty**. ✅ `cal demod` and `cal model`
>    both **done 2026-08-24**. ⚠ **No reflector** — crosstalk alone over-drives the chopped
>    measurement; what it needs is *attenuation* over D12. See §3.4.
> 6. §3.7 ramp transits, then `cal gain <peak>` to pick R98.
> 7. `cfg save` to persist. It refuses unless the machine is quiet — a 4 KB erase blinds the
>    supply monitor for tens of ms.
>
> #### ✅ Two review passes done 2026-08-14 — 14 defects found and fixed
>
> Reviewed after the firmware was written. **Three would have produced confidently wrong
> results at the bench**, which is worse than a crash because nothing looks broken.
>
> | | Defect | Why it mattered |
> |---|---|---|
> | 🔴 | `refine()` window anchored to "now", not to the falling edge | `close_pass()` fires from the coalesce timeout, so refine ran ≥2 ms late; the guard was half a transit, so **any transit under ~4 ms put the bump entirely outside the window**. At 40 m/s it measured baseline noise and reported it as a transit. Now takes the edge age and offsets the window. |
> | 🔴 | Flash config saved `carrier_hz`/`demod_phase_ticks` but **never restored them** | `beam_init()` runs after `cfg_init()` and overwrote both with the compile-time default, so `cfg` reported saved values while the beam ran on defaults. Split into `cfg_apply_beam()`, called after `beam_init()`. |
> | 🔴 | `hpf_sel_track`, `u12b_gain`, `cal_warm` never written by anything | `cfg` would report `NEVER MEASURED` forever. Now written by `hpf test`, `cal gain`, `cal demod`. |
> | 🟠 | Stale ADC fields leaked between passes | `refine()` returns early on `DQ_NO_ADC` without touching peak/baseline/sat/asym, so the **previous** pass's values were logged as this one's. Record is zeroed first. |
> | 🟠 | `safe_state_now()` reverts every PWM pad to SIO, nothing reclaimed them | `gpio_init()` drops the function select, so the threshold DAC and beam would be dead after any fault teardown — and `beam on` would report success and emit no light. Added `safe_state_reclaim_pins()`. **Pre-existing.** |
> | 🟠 | Dropped PIO words invisible | `push noblock` discards silently when the FIFO fills, and `detect_fragments()` counts what was *read*. Heavy chatter — the thing the count exists to measure — undercounted invisibly. Now latches `RXSTALL`. |
> | 🟡 | `peak_idx` underflowed to 65535 | Whenever the peak landed in a trailing partial decimation group. Found by enumeration, not by reading. |
> | 🟡 | Chattered passes got the *tightest* refine window | Window was sized from `transit_us`, which for chatter is deliberately a lower bound — so the passes that most need the ADC path got the worst window. Guard now widens. |
> | 🟡 | `adc_ring_view_valid()` biased toward calling lapped data valid | Inferred the snapshot write index as `newest+1`; now records the real one. |
> | 🟡 | `ARCHITECTURE.md` put comparator timing on PIO0 SM1 | **Not implementable** — PIO0 needs base 0 for strobe/camera, GPIO46 needs 16. Gate DAC also still read 2A instead of 6A. |
> | 🟢 | Fit residual vacuous at exactly 3 points | An exact fit gives residual 0, reported as a perfect score. Returns −1 = unavailable. |
> | 🟢 | `adc5_sigma()` used the shortcut `detect_stats()` avoids | Replaced with Welford — one pass, no buffer. (A buffered two-pass would have cost 64 KB of BSS.) |
> | 🟢 | FSM `default:` skipped teardown on the way to STANDBY | Routed through `PS_FORCE_OFF`. |
> | 🟢 | `BEAM_DUTY_OPERATING` was decorative | `scan carrier` now holds the ceiling at 25 % for its duration and restores it. 35 % is a destruction limit, not an operating point. |
>
> **New cross-phase constraint — `ARCHITECTURE.md` A9.** The ADC ring is one resource with
> mode-scoped contents: entering BURST restarts it and changes the stride, so every ch5/ch7
> sample already captured becomes *unreadable*, not stale. Phase 3/4 reads ch5 after the
> comparator edge, Phase 5 reads ch7 for the same shot, and Phase 6 destroys both. **The
> firing path must finish all ch5/ch7 analysis before switching to BURST.** Phase 4 happens
> to be ordered correctly; nothing enforces it, and the failure is silent (`DQ_NO_ADC`).
> Also recorded in `BENCH_P5_P7_MIC_CAMERA.md` and `BENCH_P6_STROBE.md`.
>
> #### Unverified assumptions the firmware makes
>
> - **`DETECT_PIO_OVERHEAD_TICKS` = 2** is derived from the instruction listing, never
>   executed. A constant offset, so it cancels between methods — but verify against known
>   bit-banged pulse widths before trusting absolute transits.
> - **PIO can read GPIO46 with FUNCSEL = SIO.** The datasheet says pad inputs reach every
>   peripheral; not confirmed on this silicon.
> - **`beam_set_duty(0)` really extinguishes the LED** (compare 0 → no rising edge → U9
>   never fires) and the demod slice keeps running across a chop. Scope TP5 and GPIO39 once.
> - **Beam path width is unset**, so no velocity is reported until `detect path <mm>`.
>
> #### Deferred deliberately
>
> - Core 1 is still unused; `detect_service()` runs in the core-0 superloop. A transit is
>   2–130 ms and the PIO holds the timing regardless of when it is read, so this is fine
>   until the strobe/camera path exists. **`pico_multicore` is not linked**, which is why
>   `flash_safe_execute()` takes its simple path — when Phase 6 launches core 1,
>   `flash_safe_execute_core_init()` must be called on it.
> - The phase model lives in RAM in `cli.c`; `cfg save` persists its coefficients but
>   `cfg_init()` does not rehydrate `s_phase_model` from them. Re-run `cal model` after a
>   reset, or wire that up.

**Phases 0 through 2 are complete.** `BENCH.md` and `BENCH_P2_BEAM.md` are both finished.
**Reflash first** — eight firmware fixes landed during Phase 2 bring-up (table at the top).

1. ✅ **A1 is done (2026-08-14).** IDLE/ARMED/BURST free-run into a 32 KB DMA ring that never
   stops — one channel in RP2350 ENDLESS mode, zero CPU, **32.8 ms of history per channel**.
   `adc_ring_history()` is the Phase 3 pre-trigger read. Nothing else is blocking.

2. **Optional, whenever convenient — try a better insulating TIM** under the heatsink.
   `NEXT_BOARD_REV.md` CR-12: the vias and the thin 1.04 mm board are already good, so the
   interface carries most of the 8.7 K/W. A thinner, higher-k pad is a **materials change with
   no board revision** worth ~2–2.5 K/W, and fitting it *measures* how much of that stage is
   really the TIM. Not blocking anything.

3. **Then Phase 3 — `BENCH_P3_DETECT.md`.** Order matters: static health (3.2) → demod
   phase calibration (3.4, and use the chopped-beam method — the .md's `cal_demod_phase()`
   is buggy) → threshold cross-calibration (3.5) → `scan carrier` (3.6) → ball transit (3.7).
   ⚠ **Let the beam reach thermal plateau (~5 min) before calibrating anything** — optical
   output falls 25–45 % from cold and it is invisible electrically.

4. **Watch for Q8 in Phase 3.** The virtual ground tracks the +5 V rail, so a rail step while
   the HPF is in HOLD (i.e. armed) reaches the comparator amplified ×14.5. Scope +5 V and
   ADC5 together while armed. Board fix proposed as CR-02.

**Beam commands, as they now behave:** `beam` (status **+ hardware readback**), `beam on|off`,
`beam freq <hz>` (rounds to nearest; prints period in counts and the achievable step),
`beam duty <pct>` (refuses >35 %), `beam ramp <pct> [step_ms]`, `beam phase <ticks>`,
`beam clamp` (now genuinely 1 kHz / 500 µs via clkdiv=3), `beam sweep <f0> <f1> <n> <dwell>`.

> ⚠ **The 35 % duty ceiling still only guards `beam duty`.** `beam freq`, `beam clamp` and
> `beam sweep` reach `beam_configure()` directly and skip it. `beam clamp` leaves the duty at
> **50 %**, so type `beam duty 2` immediately after. See §9 for the one-line fix.

**Analysis tooling:** `tools/la_phase.py` for 2a-style LA captures (and Phase 3 phase work) —
handles multi-GB CSVs by windowed sampling, and uses circular statistics because the naive
nearest-edge approach fails at exactly phase 0, TOP/2 and TOP.

**Rebuilding from the command line** (faster than the IDE button):
```
& "$env:USERPROFILE\.pico-sdk\ninja\v1.13.2\ninja.exe" -C <firmware>\build
```

---

## 11. Incident 2026-08-17 — foil short across D12, and the latched servo

**Recorded because the board is still in this state, and because the diagnostic that caused
it was a bad instruction rather than a bad execution.**

### What happened

Chasing the CR-15 mechanism, the next step was to block light with something definitively
opaque at 850 nm. The instruction given was to lay **aluminium foil over D12** — with no
warning about contact. D12's cathode sits on **VIR, 36 V, through R77 10 kΩ**, and its anode
is the TIA summing node. Foil across the package bridges them.

```
(36 − 2.59) / 10 kΩ = 3.3 mA into U11A's summing node
U11A must sink it through R80 470 kΩ → would need 1570 V → rails low
```

The give-away was direction: the capture collapsed to ~8 mV, i.e. the **negative** rail. The
TIA is inverting, so *removing* light drives the output **up**. A negative rail means current
flowing **into** the node — the opposite of what blocking light does. **Absence of signal is
baseline (2.59 V), not a rail.**

### Current state, and why no damage is indicated

| point | reading | verdict |
|---|---|---|
| TP6 (+2V5) | 2.59 V | ✅ +5VA and the reference healthy; U11C alive |
| TP2 (+12 V) | 12.3 V | ✅ boost fine |
| D12 cathode | 36 V | ✅ **D12 not conducting**; diode-checks good out of circuit |
| D12 anode (summing node) | 4.26 V | = the R78/R80 divider, see below |
| R78 far pad (U11D out) | 5.2 V | 🔴 **servo latched at the positive rail** |
| TP7 (TIA_Out) | 0 V | saturated — the *correct* response, see below |

**The current budget closes:**

```
in  via R78:  (5.20 − 4.26) / 100 kΩ = 9.40 µA
out via R80:  (4.26 − 0.00) / 470 kΩ = 9.06 µA
                          unaccounted =  0.34 µA
```

Every microamp is accounted for by two resistors. **Damage shows up as unexplained current;
there is none.** The summing node at 4.26 V is exactly 5.2 × 470/(470+100) = 4.29 V, the
passive divider between a railed servo and a railed-low output.

And U11A is behaving **correctly**: to hold the node at 2.59 V against 9.4 µA through R78 it
would need its output at 2.59 − (9.4 µA × 470 kΩ) = **−1.8 V**, below its rail. Saturating is
the right answer, not a fault.

Stepping back once more, U11D is a unity inverter (R84 = R85 = 10 kΩ) around +2V5, so
`V_U11D = 5.18 − V_U11B` puts **U11B, the integrator, at its negative rail.**

### 🔴 RESOLVED 2026-08-17 — U11B is damaged, replace U11

A 10-minute full power removal (USB and PSU both out) did **not** clear it, which rules out a
latched integrator — C73 is fully discharged by then. It is a real fault.

**The measurements that close it**, with the rail up and the beam off:

| point | reading | |
|---|---|---|
| C73, resistance, power off | **open** | ✅ not shorted — eliminates the one passive that could fake this |
| R84, U11.7 side = **U11B output** | **0 V** | 🔴 negative rail |
| R84, U11.13 side = **U11D inverting input** | **2.59 V** | ✅ its virtual ground — U11D is *linear*, not saturated |
| R78 far pad = U11D output | 5.2 V | ✅ consistent: a unity inverter fed 0 V gives 2.59 + 2.59 = **5.18 V** |

**U11D is provably healthy** — holding its virtual ground and inverting faithfully.

**U11B is provably broken.** Its inputs are:

- V+ (U11.5) = `+2V5` = **2.59 V**
- V− (U11.6) = **0 V**, pulled there through R83 from TIA_Out. C73 is a capacitor and carries
  no DC, so R83 alone sets this node.

**V+ exceeds V− by 2.59 V, so a working op-amp drives its output to the POSITIVE rail.** It sits
at the negative one. That is not a latch, an operating point, or a loop needing time — the
output is opposite to what the inputs demand.

The tell: a healthy U11B would sit at **+5.2 V**, U11D would invert to **~0 V**, R78 would pull
current *out* of the summing node and the TIA would recover. **Every node is exactly inverted
from the healthy state.**

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

### Why it did not self-recover R83 1 MΩ from TP7 at 0 V against the 2.59 V reference gives
2.59 µA into C73 330 nF = **7.85 V/s**, a full traverse in ~0.3 s. It has had far longer.

**Clearing it:** power down for **several minutes** — C73 is 330 nF and with U11 unpowered it
discharges only through high-impedance internal structures, so a 30 s cycle may not be enough.
If a long power-down does not clear it, the suspicion moves to **C73 or U11B**, neither of
which the foil touched.

### Lessons worth keeping

1. **Never put conductive material near this board without stating what it must not touch.**
   D12 has 36 V on one leg.
2. **A rail is not a null.** If a test is supposed to *remove* a signal, the pass condition is
   "returns to baseline", not "goes quiet" — a saturated output is also very quiet.
3. Foil over the **photodiode** was chosen over the LED specifically to keep metal away from
   the switching loop. That reasoning was right; the omission was the contact warning.
