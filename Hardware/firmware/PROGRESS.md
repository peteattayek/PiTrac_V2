# PiTrac RP2354 Firmware — Progress & Resume Context

**Read this first when resuming work.** It records what is done, what is next, and the
decisions/measurements that must not be lost between sessions.

Last updated: 2026-08-14 · **Phases 0, 0.5, 1, 1b, 1c and 2 are all complete and closed.**
`BENCH.md` and `BENCH_P2_BEAM.md` are finished end to end. The power path and the optical
transmit chain are both proven on hardware. **Q1, Q2 and Q12 answered; Q5, Q7, Q9 closed. Open: Q3, Q4, Q6, Q8, Q10, Q11.**
**Phase 3 (`BENCH_P3_DETECT.md`) is next. A1 is fixed, so nothing blocks it.**

⚠ **The property that actually matters for a real Pi: an RP2354 reset is a hard power cut.**
Not a reboot. See §2 and §9. Q10 (the GPIO43 pull-down) is a *consequence* of this, and a
minor one — it was written up as a blocker on 2026-07-31 and that was an overstatement,
corrected below.

**Verified build (SDK 2.3.0, toolchain 15_2_Rel1):** UF2 family `rp2350-arm-s` (ARM, not
RISC-V), target chip RP2350, ARM Secure image, USB stdin/stdout. **78 KB flash of 2 MB;
67 KB RAM of 520 KB (12.9 %)** — the 32 KB capture buffer plus the 32 KB A1 DMA ring.
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
| `BENCH_P3_DETECT.md` | **Phases 3 & 4** — photodiode chain, phase cal, `scan carrier`, trigger experiment |
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

**Immediate next action:** 🔴 **Fix `ARCHITECTURE.md` A1 before starting Phase 3.**
`adc_read_avg()` tears down the ADC ten times a second; Phase 3 needs ADC5 free-running into
a DMA ring. Then `BENCH_P3_DETECT.md`.

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
| Q3 | **Best carrier frequency.** 104.1667 kHz is a starting point, not an answer. Boost fsw tolerance makes paper analysis undefensible. **Search space is now known to be unconstrained by hardware to at least 250 kHz** (Q2), so this is purely a detection-performance question. ⚠ **Run it warm** — optical output falls 25–45 % from cold to plateau while every electrical reading says nothing changed. | Detection SNR | Phase 3.6 `scan carrier` |
| Q4 | **Does the Pi 5's header 3.3 V rail drop at halt?** | If not, `PI_3V3_SENSE` never fires and every shutdown hits the 60 s timeout. RPI5_ON fallback is implemented. Also decides whether `pi_down_unrequested` telemetry (§9) can rest on 3V3 or must use RPI5_ON alone. | **`BENCH_P8_PI.md` §8.2** — `sudo halt`, DMM on J8.1 |
| ~~Q5~~ | ~~**RP2350 erratum E9**~~ — **CLOSED 2026-07-29.** Empirical `pins` test: GPIO24/0/8/9 all read **0** floating. Silicon is revision **A4**, a later stepping than the A2 the erratum was documented against. | — | ✅ Resolved. No external pull-downs needed; R44's 100 kΩ holds GPIO24 down fine on this silicon. The `pins` reading is the authoritative evidence for this board. |
| ~~Q12~~ | ✅ **RESOLVED 2026-08-14.** The 53.6 °C D11 reading was an emissivity artifact off the **domed lens**, exactly as the physics predicted (it cannot be cooler than the 68 °C heatsink it feeds). Re-measured with the IR camera pointed **sideways at the LED base**, avoiding the lens: **104 °C at 30 % duty**. **D11 and the ballast resistors sit at the same temperature** — D11 carries 4× the power (3.23 W vs 0.81 W) through a ~4× better path (25 vs 100 K/W), and they are millimetres apart on shared copper. | — | ✅ **The 24 K/W figure in §6 is validated and does describe the LED**: it predicts 25 % → 89 °C (measured 87.5) and 30 % → 102 °C (measured 104) from one constant. **CR-12 confirmed 🔴** — junction at 30 % is **123–133 °C** against a 145 °C max. |
| ~~Q6~~ | ✅ **ANSWERED 2026-08-14 from the datasheet (DS000642 v9-00). YES, the I/O domain is 1.8 V** — VDD18 = 1.70/1.80/1.90 V, and **VIH max is specified as VDD18 itself, so there is no 3.3 V tolerance.** VOH min = 0.8·VDD18 = **1.44 V**, ceiling 1.80 V. | **BOTH directions fail and the 220 Ω resistors fix neither — they limit current, they shift no levels.** *Read:* RP2350 VIH ≈ 2.15 V > the sensor's 1.80 V ceiling → the strobe inputs can never read high → **the Phase 7 handshake cannot work.** *Write:* 3.3 V through 220 Ω injects **3.6 mA** into the sensor's clamp. Latch-up is not the risk (ISCR = ±100 mA, 27× margin), but the I/O rail draws only **0.6 mA max**, so 3.6 mA is 6× its own consumption and lifts VDD18 out of spec. **The destructive case is driving J4 with the camera unpowered** — that back-powers VDD18 through the ESD diode and violates the power-up sequence. | 🔧 **CR-09 is now unblocked and 🔴.** Needs a real translator on all three signals (TXB0104 or BSS138 discretes) plus a 1.8 V reference at J4, which the current pinout does not provide (pins 1/2/5/6 are all GND). **One unknown remains: whether the camera board already level-shifts.** J4 lands on its header, not on raw sensor pins — get that schematic. Until then **do not connect J4**, and never drive D_Cam_Trigger high with the camera unpowered. |
| ~~Q7~~ | ~~**RPI5_SHUTDOWN polarity**~~ — **RESOLVED on hardware 2026-07-31.** Active-low is correct and proven: a shutdown request produces a clean **200 ms low pulse at GPIO43**, measured during Phase 1b. | — | ✅ Firmware side closed. Only the Pi-side overlay params remain to be written and verified — `BENCH_P8_PI.md` §8.1. |
| Q10 | 🟢 **Measured 2026-07-31 — the level fails, but the impact is small. Demoted from blocker.** GPIO43's reset-default pull-down (§2: PDE=1) holds the *asserted* level from the reset edge until `safe_state_init()` runs. Measured with a 20 kΩ emulated pull-up: **2.07 V** at J8.37 against a **3.246 V** rail. `X = V·R/(3V3−V)` = **35.2 kΩ**, i.e. a **~34 kΩ pad pull-down** — stronger than the 50–80 kΩ assumed. A real Pi's ~50 kΩ pull-up would see **`V_pi` = 1.34 V**, below RP1's VIH, and even a typical 60 kΩ pull-down gives 1.81 V — so the *level* fails across the whole plausible range. | ⚠ **Corrects the 2026-07-31 write-up, which called this a blocker.** It is not. **Every reset also opens the +5 V latch** (§2), so the spurious request reaches a Pi that is losing its rail in the same instant. There is no case on this board where GPIO43 goes low but the latch holds — both pads reset together and the Pi has no other power source. The real hazard is the power cut; this is a footnote to it. | 🔧 **Optional defence-in-depth: 10 kΩ from J8.37 to +3V3** (or to **J8.1**, the Pi's own 3V3 — both are header pins, so no PCB work). Gives 2.67 V through reset and 0.35 V when firmware asserts. **Fit it if convenient; it does not gate Phase 8.** The mitigation that actually matters is the `reset`/`bootsel` CLI guard, already implemented. |
| Q11 | **How long does a Pi 5 take to raise its header 3V3 after +5V is applied?** Never measured. `PI_DETECT_WINDOW_MS` (3000) is currently a guess. | `POWERING_ON` classifies the Pi as absent when this window expires, and in `BENCH_RUNNING` a button press is a hard `FORCE_OFF` — a power cut on a booting Pi. The late-detect promotion backstops it, but the window should be right on its own. | **`BENCH_P8_PI.md` §8.3** — scope J8.2 (+5V) against J8.1 (Pi 3V3), measure to the **2.54 V** crossing (that is 2.31 V VIH ÷ the R45/R44 0.909 divider, i.e. when firmware can actually see it). |
| ~~Q9~~ | **The +5V_IN read path is ~5.9% LOW — MECHANISM RESOLVED 2026-07-30.** True **5.200 V** at J1 reads back as **4.893 V** (code 3036). Three measurements localise it: J1 = 5.200 V, R46/R47 junction = **2.578 V**, ADC reports **2.447 V**. So the divider contributes only **−0.85%** (fine for two 1% parts) and the **ADC conversion itself contributes −5.1%**. <br>**Leakage ruled out:** µA into the pin would have dragged the junction to ~2.45 V; it sits at 2.578 V. **Reference ruled out by arithmetic:** explaining the error would need VREF = 3.477 V, above the +3V3 rail feeding ADC_AVDD — and R27's 33 Ω can only drop it *lower*, which pushes the error the other way. Residual cause is ADC gain error + incomplete S/H settling through the 50 kΩ source (RP2350 wants ≤10 kΩ). | Nearly broke Phase 1: uncalibrated, a good bench supply read as 4.89 V — below `V5_MIN_FOR_LATCH` (5.05 V) — so the firmware would refuse to latch and report `USB_POWER_ONLY`. | ✅ **Closed.** Compile-time default scale **1.063** folds in both terms; `adc5vcal` trims per-unit residue. Nothing to check on ADC ground/reference. **Next board spin:** R46/R47 = 10K/10K — see `NEXT_BOARD_REV.md` **CR-03**, which also flags that `ADC5V_SCALE_DEFAULT` (1.063) must drop to ~1.00 in the same commit or every reading goes ~6 % HIGH. |
| Q8 | 🔧 **Board fix proposed — `NEXT_BOARD_REV.md` CR-02** (regulate +2V5, or a unity diff amp taking TP9−TP6). **The virtual ground TRACKS the +5V rail** — R75/R76 = 10K/10K makes +2V5 literally +5VA/2, not a regulated 2.5 V. **Measured 2.59 V** at TP6/7/9/10 on a 5.2 V rail. Confirmed correct; the .md's "2.50 V" assumed a 5.00 V rail this board never runs at. | Any *step* on +5V while the HPF is in HOLD (i.e. armed) shifts the whole chain's virtual ground by ΔV/2, which passes through C81 and hits the comparator amplified **×14.5**. A 100 mV rail step → ~725 mV at ADC5, well over a typical 0.1–0.5 V threshold → **false trigger**. In TRACK mode the 0.66 s HPF removes it; in HOLD it does not. | Phase 3: while armed and holding, scope +5V and ADC5 together. Mitigations if real: keep the rail stiff while armed, shorten the armed window, or don't freeze the baseline. |

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
- [~] **3** Photodiode: **FIRMWARE COMPLETE 2026-08-14, builds clean, NOT YET RUN ON HARDWARE.**
      All of steps 1-7 written: threshold DAC, HPF control + polarity self-test, PIO transit
      timer, ADC refinement, `cal demod` + phase model, `scan carrier`, flash config.
      **Bench work STARTED 2026-08-17 and is BLOCKED at §3.3.** Passing so far: boot, PIO
      allocation on silicon, threshold DAC vs DMM, static health (`adc 2` = 2.5884 V),
      **`hpf test` → GPIO33 = 0 is TRACK** (agrees with the TMUX1219 truth table), `cfg save`.
      🔴 **Blocked by CR-15:** the TIA saturates on beam coupling above ~3 % duty, so §3.4
      onward cannot produce trustworthy numbers. Mechanism (optical vs beam-current) still open.
- [~] **4** Trigger source experiment — **firmware complete** (`detect log` / `detect stats`,
      including the bias-vs-1/peak regression). Bench work not started, and **gated on CR-15** —
      the comparator would chatter at 104 kHz on the carrier feedthrough.
- [ ] **5** Microphone *(can be pulled forward — runs on USB power alone, no latch needed)*
- [ ] **6** Strobe: 6a dry → 6b gate DAC → 6c LED bank ramp → 6d clamp-with-current
- [ ] **7** Cameras: 7a loopback → 7b delayed sim → 7c real (needs Pi)
- [ ] **8** Real Pi integration + clean-shutdown acceptance — `BENCH_P8_PI.md`.
      **Gated on the Q10 rework.** Closes Q4, Q7 (Pi side), Q10 end-to-end and Q11.

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
| 8/14/2026 | **TIA feedback pole** | **Cf 0.5 pF → 677 kHz** | 🔵 C68 **in series with** C70 (1 pF + 1 pF) — sub-pF parts are unbuyable and parasitics would dominate. Phase lag **8.7° at 104 kHz → 20.3° at 250 kHz**. **This is the concrete source of the dispersion that makes a single phase number fail across the scan range**, and a testable prediction: `cal model` should find roughly this curvature. If it reports `pure_delay`, distrust the measurement |
| 8/14/2026 | **ADC1 has no filter cap** | **R46/R47 + pin only** | Consistent with Q9 (50 kΩ into an ADC wanting ≤10 kΩ, no reservoir for the S/H). CR-03 fixes it |
| | Chosen carrier frequency | | from `scan carrier` max-SNR (Q3) |
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
| 8/21/2026 | ✅ **BURST SOURCE CLOSED — it is the beam returning off the room** | **cover D11 → σ 10.30 mV falls to 2.91 mV** | ✅ **Three captures with D11's output aperture covered, everything else identical.** σ: **6.49/10.37/14.03 → 2.62/3.39/2.91 mV**. Peak: **77 → 33 codes.** And the distribution changes character completely — **excess kurtosis −0.3 / −0.3 / +3.0 (open) → +0.1 / +0.5 / +0.1 (covered)**, i.e. from wildly non-Gaussian to **essentially Gaussian**. ✅ **Stop the light leaving the board and the bursts stop. The hypothesis is confirmed.** |
| 8/21/2026 | 🔵 **The negative kurtosis was the tell, and I missed it first time** | **−0.3 means SIGNAL, not noise** | 🔵 Impulsive *noise* has **positive** excess kurtosis (heavy tails). Two of the three open captures ran at **−0.3**, which is a *flattened / bimodal* distribution — the signature of a **modulated signal filling the range**, not of noise. **The detector was working correctly the whole time**; it was reporting light. ⚠ I called this "bursty noise" for two rounds before computing the fourth moment that would have said otherwise on day one |
| 8/21/2026 | ⚠ **σ_noise is not a constant — it is set by the optical background** | **0.65 / 2.91 / 6.5–14 mV** | ⚠ Three different numbers on one board in one session: **0.65 mV** (quiet population, nothing returning), **2.91 mV** (D11 covered — the cover itself reflects light back into D12 at close range, and shot noise goes as √I), **6.5–14 mV** (open to the room). 🔴 **Consequence for §3.6: there is no bench σ_noise worth quoting.** `scan carrier` divides by this, so it **must be run in the final geometry and lighting** or its SNR ranking is against the wrong denominator. The 2.91 mV figure is a usable *bench reference* only |
| 8/21/2026 | ✅ **Operator motion RULED OUT as the burst source** | **bursts got *worse* with the operator away** | ✅ Three repeat captures, lights on, operator as far from the beam as possible: **52.6 %, 43.0 %, 19.6 %** of samples above 20 codes, against 20.6 % with them standing at the bench. Peaks to **77 codes**. The motion hypothesis predicted the opposite, so it is dead |
| 8/21/2026 | ✅ **The quiet floor is the reproducible part** | **σ = 0.61–0.70 mV across all 5 captures** | ✅ 0.83 / 0.75 / 0.87 codes here, 0.81 / 0.68 earlier — **both lighting conditions, operator near and far.** This is the genuine electronic noise floor and it is rock solid. ❌ **σ(all), by contrast, is NOT reproducible: 6.49 / 10.37 / 14.03 mV** — a 2× swing between captures taken seconds apart. Whatever drives the bursts varies on a seconds timescale |
| 8/21/2026 | 🔵 **The bursts are strictly one-sided POSITIVE** | **min = 9 codes in all three, no floor pile-up** | 🔵 Checked specifically for a bipolar signal clipped at the ADC's 0 V rail — **it is not that.** Zero samples below 8 codes against a 9–10 code baseline, while excursions reach 77. **More light only.** Electrical coupling could push either sign; **an optical return can only add light.** Combined with the operator result this points at **the beam reflecting off the room and returning to D12** — which the lock-in passes faithfully because it is genuinely modulated at the carrier. ⚠ **Not yet proven; see the discriminator row** |
| 8/21/2026 | ⚠ **Why "operator further away" made it worse** | **hypothesis: they were blocking the return** | ⚠ Standing at the bench, the operator's body blocks the beam's path into the room; stepping clear lets it reach walls and equipment and come back. Fits the sign, fits the direction, and fits the second-to-second variability if anything in the room moves at all. 🔴 **Discriminator: cover D11's *output aperture*** (tape-wrapped foil or the carbon-black baffle) so no light leaves the board, and repeat. Optical return vanishes; electrical coupling does not. ⚠ **Cover D11, NOT D12 — and nothing bare-metal near either.** See §11 |
| 8/21/2026 | 🔵 **If it IS room return, it may not be a fault at all** | **reframing worth holding** | 🔵 The detector's job is to report light modulated at the carrier. A room that reflects some back is **background, not noise** — and in the final installation the beam points into a defined space with its own background. That argues `scan carrier` must run **in the final geometry and lighting**, and that this background legitimately belongs in σ_noise. It does **not** argue for chasing it away on the bench |
| 8/21/2026 | 🔵 **Superseded: the 5–30 Hz "envelope periodicity"** | **artifact of my own window** | ⚠ I reported burst-rate lines at 5–30 Hz. With 40 envelope points over 200 ms the bin spacing **is** 5 Hz, so the "5 Hz line" is just the lowest bin — slow drift, not periodicity. **The window cannot resolve burst rate at all.** A longer capture (16384 samples at 10 ksps = 1.6 s) is needed before any claim about periodicity |
| 8/21/2026 | 🔵 **Two candidate causes for the bursts (superseded — see above)** | **motion now ruled out** | 🔵 **(a) Motion in the beam** — the lock-in passes anything modulated at the carrier, so a hand, body or fan blade reflecting 850 nm *is* a genuine detection and the bursts being worse with the lights on fits an operator who can see to move. **(b) Room-light switching harmonics near the carrier** — ⚠ **the lock-in's rejection is frequency-selective, not universal.** It rejects 120 Hz by ~48 dB but does nothing about ambient energy *near 104 kHz*, and LED drivers, electronic ballasts and SMPS all have harmonics up there. A harmonic landing within a few Hz of the carrier demodulates to exactly this: a slow 5–30 Hz beat. **Discriminator: lights on, beam on, scene genuinely static, three repeat captures.** Reproducible statistics → the lights. Wildly varying → motion |
| 8/21/2026 | 🔵 **Aliased carrier feedthrough at 10 ksps** | **~2.6 mV at 4165 Hz** | 🔵 Expected and harmless, but do not mistake it for signal: at 10 ksps the 104166 Hz carrier aliases to **4166 Hz** and 2f to 1668 Hz. Clearly present in the dark capture (3.25 codes at 4165 Hz). **10 ksps is the right rate for flicker and the wrong rate for anything carrier-related** |
| 8/21/2026 | ⚠ **Board 2 beam thermal, 25 % duty, 10 min** | **base 98 °C, heatsink 78 °C** | ⚠ **~10 °C hotter than board 1 at the same duty** (87.5 / 68.1 °C). Junction = 98 + 2.69 W × (6…9) = **114–122 °C**, margin to 145 °C is **23–31 °C** vs board 1's 33–41 °C. **Acceptable, not comfortable.** ✅ **The interface is fine — the base→heatsink gradient is 20.0 K vs board 1's 19.4 K**, so the mounting and paste are as good. The whole difference is **heatsink→ambient**: R_th(base→amb) = 27.9 K/W vs 24.0. ⚠ **Two things unmeasured that decide whether this matters:** (a) **room ambient** — at 33 °C instead of 23 °C the two boards are *identical* and there is nothing to explain; (b) **it is not a plateau.** CR-12 saw +4 °C over the second five minutes at 30 % and says a true plateau needs 20–30 min, so 98 °C is a **lower bound**. 🔵 **Knock-on beyond safety:** output falls 0.3–0.6 %/K, so a 10 K hotter junction is **3–6 % less light than board 1** — record the temperature alongside every `scan carrier` result or the SNR numbers are not comparable |
| | Beam path width (mm) | | `detect path <mm>`. No velocity is reported until set |
| | Pi shutdown duration | | **Phase 8.2.** Set `PI_SHUTDOWN_MIN_HOLDOFF_MS` ≈ 2× this. It is 15 s on an assumption today |

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
- **Carrier = 104.1667 kHz** (sysclk 150 MHz, TOP=1439, level=432 → exactly 30.000 %),
  as a *starting point* pending `scan carrier`. ✅ **Validated on hardware 2026-08-13:**
  phase lock reproducible to 0.15 ticks, duty exact to 250 kHz, peak current 3.11 A. The
  frequency choice is now purely a detection-SNR question (Q3), not a hardware one.
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
> **Next is §3.4 `cal demod`.** It needs a **static reflector** and a **warm beam**
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
> ✅ **Nothing blocks §3.4 any more.** `cal demod` and `cal model` average synchronously over
> many cycles, so the room return averages down and does not corrupt a phase fit.
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
> 5. `cal model` then `scan carrier` — **warm beam, 25 % duty**, static reflector.
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
