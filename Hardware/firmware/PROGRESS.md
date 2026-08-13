# PiTrac RP2354 Firmware — Progress & Resume Context

**Read this first when resuming work.** It records what is done, what is next, and the
decisions/measurements that must not be lost between sessions.

Last updated: 2026-08-13 · **Phases 0, 0.5, 1, 1b, 1c and 2 are all complete and closed.**
`BENCH.md` and `BENCH_P2_BEAM.md` are finished end to end. The power path and the optical
transmit chain are both proven on hardware. **Q1 and Q2 are answered; Q5, Q7, Q9 closed.**
**Phase 3 (`BENCH_P3_DETECT.md`) is next — but fix `ARCHITECTURE.md` A1 first, not during.**

⚠ **The property that actually matters for a real Pi: an RP2354 reset is a hard power cut.**
Not a reboot. See §2 and §9. Q10 (the GPIO43 pull-down) is a *consequence* of this, and a
minor one — it was written up as a blocker on 2026-07-31 and that was an overstatement,
corrected below.

**Verified build (SDK 2.3.0, toolchain 15_2_Rel1):** UF2 family `rp2350-arm-s` (ARM, not
RISC-V), target chip RP2350, ARM Secure image, USB stdin/stdout. ~**72 KB flash of 2 MB;
35 KB RAM of 520 KB** — mostly the 32 KB capture buffer. No warnings.
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

**Companion docs**

| File | Covers |
|---|---|
| `START_HERE.md` | Beginner walkthrough — build, flash, first commands |
| **`ARCHITECTURE.md`** | **Hardware-offload audit — what runs on PIO/PWM/DMA vs the CPU. Contains one 🔴 finding (A1) that must be fixed before Phase 3.** |
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
| Bench work done | **Phases 0, 0.5, 1, 1b, 1c (2026-07-31) and 2 (2026-08-13) all complete.** Phase 2: phase lock to 0.15 ticks, beam ramped to 30 %, U9 clamp 122.68 µs, duty fidelity flat to 250 kHz, thermals characterised. E9 check passed. All rails verified. All Phase 1 tests pass including fail-safe-through-reset; **the full Phase 1b matrix passes**, covering both timeouts, the RPI5_ON fallback, the escape hatch and the reset invariant. Standby draw **32 mA**. **Phase 2 next.** |

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
5. **Resolve the Mira220 1.8 V I/O question before Phase 7c.** It can damage hardware.

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
| Q12 | **Which component is actually the beam thermal limit — D11 or the ballast R73/R74?** FLIR at 25 % duty read **R73 85.5 °C** vs **D11 53.6 °C**, which would mean the ballast is hotter despite D11 dissipating 4×. **But D11's reading is not trustworthy:** it came off a **domed lens**, a curved specular surface whose effective emissivity falls with angle. **Physics rules it out** — the heatsink measured **68.1 °C**, and D11 cannot be cooler than the sink it is feeding. So D11 is **> 68 °C**, most likely **74–82 °C** (heatsink + ~2 K/W interface × 2.74 W), junction **90–107 °C**. | Decides whether `NEXT_BOARD_REV.md` **CR-12** is a real constraint (🔴, sustained duty capped ~20 %) or a misattribution (🟢, D11 has 38–55 °C of margin). Also decides whether the **24 K/W** figure in §6 describes D11 or R73. | **Put matte tape on D11 and re-read at 25 % after a 5 min plateau.** Also read R73 and R74 separately — they are in series with identical 0.69 W and should match within a few °C. **Sanity rule: D11 must read hotter than its own heatsink; if it does not, the reading is wrong.** |
| Q6 | **Mira220 digital I/O is 1.8 V?** J4 drives 3.3 V through 220 Ω. | Can damage the sensor | Before Phase 7c |
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
- [ ] **3** Photodiode: static health → phase cal → threshold → `scan carrier` → ball transit
- [ ] **4** Trigger source experiment (comparator vs. ADC bias table)
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
| 8/13/2026 | **Beam thermal: R_th (hot spot)→ambient** | **~24 K/W** | ⚠ **Which component this describes is open — see Q12.** If the hot spot is R73 rather than D11 the figure belongs to the ballast, not the LED. **Two duty points agree** for whatever it is measuring: 25 % → plateau **87.5 °C**; 30 % → **~100 °C**. Ambient 23 °C, heatsink fitted, still air. Model predicts both. τ ≈ **70 s**, ~5 min to settle |
| 8/13/2026 | **Beam thermal: heatsink** | **68.1 °C** | ⚠ **Corrected** — an earlier 81.5 °C reading was the *board top max*, not the heatsink. With the board hot spot at 85.5 °C the sink is 17 °C cooler, so the interface is carrying heat. **But it also rules out the 53.6 °C D11 reading: D11 cannot be cooler than the sink it feeds.** See Q12 |
| 8/13/2026 | **Beam thermal: heatsink→ambient** | **~22 K/W** | ⚠ **The bottleneck — 90 % of the total.** Better paste buys nothing; airflow buys ~2-3× |
| 8/13/2026 | **R73 (ballast), 25 % duty** | **85.5 °C** | 0.687 W measured → implies **~90 K/W** to ambient. Bare 2512, no heatsinking. Well inside a 3 W part's rating and ~70 °C under its typical 155 °C limit |
| 8/13/2026 | **D11 package, 25 % duty** | **53.6 °C — NOT TRUSTED** | ❌ Read off a **domed lens** (curved, specular, emissivity falls with angle). **Physically impossible**: the heatsink it feeds measured 68.1 °C. True value is **> 68 °C, likely 74–82 °C** → junction 90–107 °C. **Re-read with matte tape — Q12** |
| 8/13/2026 | **Beam-off baseline** | **37.4 °C** | Rails latched, beam off, 23 °C ambient. +14 °C from the boost / R15-D4 shunt alone (see CR-07) |
| 8/13/2026 | **U9 beam one-shot clamp (Q1)** | **122.68 µs** | ✅ **ANSWERED.** Median over 1291 pulses, spread 0.22 µs (0.18 %). **Neither 86 nor 113 µs** — implied K = **0.996**, not the 0.70 assumed. **Concern inverted, and a second problem found:** `STROBE_SW_MAX_US` was **73 µs**, not the 100 µs I had been quoting — *below* the 100 µs that .md §15 needs at 10 m/s, so slow-ball pulses would have been firmware-truncated 27 %. With a 122.7 µs hardware clamp, raised to **100 µs** (18 % margin). Hardware never truncates. Captured on pre-fix fw (2289 Hz, 218 µs commanded — still 1.8x headroom, valid) |
| | **U5 strobe one-shot clamp** | | **Phase 6a.1. Expect ~122 µs** (identical part+RC to U9: 74LVC1G123, 56K, 2.2nF, BOM-confirmed). Tolerance band **109–136 µs**. **Set `STROBE_SW_MAX_US` from U5, not U9** |
| 8/13/2026 | **Beam duty fidelity limit (Q2)** | **none up to 250 kHz** | ✅ **ANSWERED.** At 250 kHz: period 4.0 µs, TP5 low 1.2 µs = **exactly 30 %**. Swept 5→250 kHz at 30 % with no degradation. **The reset is active** — ~CLR discharges C57 through an internal low-impedance transistor in ~0.1–0.2 µs, *not* through R68's 123 µs RC, so there was never a real recovery problem. **Carrier design stands; Q3's search space is unconstrained to 250 kHz** |
| | Chosen carrier frequency | | from `scan carrier` max-SNR (Q3) |
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
- 🔴 **`adc_read_avg()` stops and restarts the ADC** (see `ARCHITECTURE.md` A1). Harmless now;
  **breaks Phase 3**, where ADC5 must free-run into a DMA ring. The supply monitor calls it
  every 100 ms. **Fix before starting Phase 3**, not during.
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

**Phases 0 through 2 are complete.** `BENCH.md` and `BENCH_P2_BEAM.md` are both finished.
**Reflash first** — eight firmware fixes landed during Phase 2 bring-up (table at the top).

1. 🔴 **Fix `ARCHITECTURE.md` A1 BEFORE starting Phase 3, not during.**
   `adc_read_avg()` stops the ADC, drains the FIFO and clears round-robin — and the power
   FSM's supply monitor calls it every 100 ms. Phase 3 needs ADC5 free-running into a
   continuous DMA ring so a comparator edge has pre-trigger history. Ten teardowns a second
   will punch holes in that ring and rotate the round-robin phase, and the symptom is
   intermittent missing samples that read as an analog fault. **Much cheaper now than later.**

2. **Take the D11 tape reading (Q12).** Sixty seconds, and it decides whether
   `NEXT_BOARD_REV.md` CR-12 is a 🔴 design constraint or a 🟢 footnote. Matte tape on the
   LED, 25 % duty, 5 min plateau. Sanity rule: **D11 must read hotter than its heatsink.**

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
