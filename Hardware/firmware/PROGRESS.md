# PiTrac RP2354 Firmware — Progress & Resume Context

**Read this first when resuming work.** It records what is done, what is next, and the
decisions/measurements that must not be lost between sessions.

Last updated: 2026-07-30 · **Phases 0, 0.5, 1 (all 5 tests) and 1c complete on hardware.**
Phase 1b next — needs a scope/LA for the "reset ≠ shutdown" check.

**Verified build (SDK 2.3.0, toolchain 15_2_Rel1):** UF2 family `rp2350-arm-s` (ARM, not
RISC-V), target chip RP2350, ARM Secure image, USB stdin/stdout. **73 KB flash of 2 MB
(3.6 %); 35 KB RAM of 520 KB (6.7 %)** — mostly the 32 KB capture buffer. No warnings.
Includes phases 0–2 (`safe_state`, `adc_engine`, `power_fsm`, `panel`, `beam`, `cli`).

**Companion docs**

| File | Covers |
|---|---|
| `START_HERE.md` | Beginner walkthrough — build, flash, first commands |
| **`ARCHITECTURE.md`** | **Hardware-offload audit — what runs on PIO/PWM/DMA vs the CPU. Contains one 🔴 finding (A1) that must be fixed before Phase 3.** |
| `SETUP.md` | Toolchain detail + manual install path |
| `BENCH.md` | **Phases 0 → 1c** (toolchain, rails, latch, Pi shutdown, panel) — ✅ done except 1b |
| `BENCH_P2_BEAM.md` | **Phase 2** — carrier, phase lock, clamp (Q1), duty fidelity (Q2) |
| `BENCH_P3_DETECT.md` | **Phases 3 & 4** — photodiode chain, phase cal, `scan carrier`, trigger experiment |
| `BENCH_P6_STROBE.md` | **Phase 6** — ⚠ the dangerous one. 9 A, linear-mode FET. Read fully before powering. |
| `BENCH_P5_P7_MIC_CAMERA.md` | **Phases 5 & 7** — mic (USB power only, pull forward any time) and cameras |

---

## 0. Where things stand

| | Status |
|---|---|
| Board | Assembled and **powered on the bench supply. All rails verified good** — +5V, VIR 36 V, +12 V, +5VA, virtual ground. |
| Approved plan | `C:\Users\ATTAYEKP\.claude\plans\this-folder-contains-a-flickering-wreath.md` |
| Toolchain | **Installed** — VS Code Pico extension, private copies in `%USERPROFILE%\.pico-sdk`. SDK 2.3.0, toolchain 15_2_Rel1, ninja 1.13.2, cmake 4.3.4. |
| Bench equipment | Scope, logic analyzer, DMM, current-limited PSU, **FLIR thermal camera**. See the thermal section in `BENCH.md` for the four points where the FLIR matters. |
| Firmware | Phase 0 + 1 + 1b written, **builds clean**, flashed and running |
| Bench work done | **Phases 0, 0.5 and 1 complete.** E9 check passed. All rails verified. **All four Phase 1 tests pass**, including fail-safe-through-reset. Standby draw **32 mA**. Phase 1b next. |

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

**Immediate next action:** `BENCH.md` Phase 1b — the full FSM and Pi soft-shutdown against a
**simulated** Pi. This is the hard gate before any real Pi is ever seated on J8.

Setup note: with no Pi present, J8.1/J8.17 are **dead** (they're Pi outputs). Use the board's
own +3.3 V from **J5 pin 2**, jumpered to J8.1 (asserts PI_3V3_SENSE) and J8.15 (asserts
RPI5_ON). Several tests are deliberately slow — 15 s hold-off, 60 s and 90 s timeouts.

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
4. **Phase 1b must pass before a real Pi is ever attached.** It is the only thing between a
   firmware bug and a corrupted SD card.
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

---

## 3. Open questions and unverified numbers — **measure these, don't trust the .md**

| # | Item | Why it matters | Resolve in |
|---|---|---|---|
| Q1 | **One-shot clamp is probably ~86 µs, not 113 µs.** SN74LVC1G123 t_w ≈ K·R·C, K ≈ 0.7 at 3.3 V; 56K × 2.2 nF = 86 µs. The .md's 113 µs implies K ≈ 0.92. | `STROBE_SW_MAX_US` (100 µs) may be **above** the real hardware limit → every slow-ball pulse silently truncated | Phase 2 step 4 (U9), Phase 6a (U5) |
| Q2 | **74LVC1G123 recovery at 104 kHz.** Timing node must reset via ~CLR in the 6.7 µs low phase. | If it can't keep up, the LED won't reproduce 30 % duty and the carrier design shifts | Phase 2b step 3 |
| Q3 | **Best carrier frequency.** 104.1667 kHz is a starting point, not an answer. Boost fsw tolerance makes paper analysis undefensible. | Detection SNR | Phase 3.6 `scan carrier` |
| Q4 | **Does the Pi 5's header 3.3 V rail drop at halt?** | If not, `PI_3V3_SENSE` never fires and every shutdown hits the 60 s timeout. RPI5_ON fallback is implemented. | Phase 8.2 (`sudo halt`, DMM J8.1) |
| ~~Q5~~ | ~~**RP2350 erratum E9**~~ — **CLOSED 2026-07-29.** Empirical `pins` test: GPIO24/0/8/9 all read **0** floating. Silicon is revision **A4**, a later stepping than the A2 the erratum was documented against. | — | ✅ Resolved. No external pull-downs needed; R44's 100 kΩ holds GPIO24 down fine on this silicon. The `pins` reading is the authoritative evidence for this board. |
| Q6 | **Mira220 digital I/O is 1.8 V?** J4 drives 3.3 V through 220 Ω. | Can damage the sensor | Before Phase 7c |
| Q7 | **RPI5_SHUTDOWN polarity.** .md §13.5 implies active-high; `gpio-shutdown` overlay defaults active-low + pull-up. | Backwards = shutdown never works, or fires spuriously | Committed to **active-low** in `board.h`; confirm overlay params in Phase 8.1 |
| ~~Q9~~ | **The +5V_IN read path is ~5.9% LOW — MECHANISM RESOLVED 2026-07-30.** True **5.200 V** at J1 reads back as **4.893 V** (code 3036). Three measurements localise it: J1 = 5.200 V, R46/R47 junction = **2.578 V**, ADC reports **2.447 V**. So the divider contributes only **−0.85%** (fine for two 1% parts) and the **ADC conversion itself contributes −5.1%**. <br>**Leakage ruled out:** µA into the pin would have dragged the junction to ~2.45 V; it sits at 2.578 V. **Reference ruled out by arithmetic:** explaining the error would need VREF = 3.477 V, above the +3V3 rail feeding ADC_AVDD — and R27's 33 Ω can only drop it *lower*, which pushes the error the other way. Residual cause is ADC gain error + incomplete S/H settling through the 50 kΩ source (RP2350 wants ≤10 kΩ). | Nearly broke Phase 1: uncalibrated, a good bench supply read as 4.89 V — below `V5_MIN_FOR_LATCH` (5.05 V) — so the firmware would refuse to latch and report `USB_POWER_ONLY`. | ✅ **Closed.** Compile-time default scale **1.063** folds in both terms; `adc5vcal` trims per-unit residue. Nothing to check on ADC ground/reference. **Next board spin:** R46/R47 = 10K/10K gives 5 kΩ source impedance and removes the ADC term, for 260 µA extra quiescent draw. |
| Q8 | **The virtual ground TRACKS the +5V rail** — R75/R76 = 10K/10K makes +2V5 literally +5VA/2, not a regulated 2.5 V. **Measured 2.59 V** at TP6/7/9/10 on a 5.2 V rail. Confirmed correct; the .md's "2.50 V" assumed a 5.00 V rail this board never runs at. | Any *step* on +5V while the HPF is in HOLD (i.e. armed) shifts the whole chain's virtual ground by ΔV/2, which passes through C81 and hits the comparator amplified **×14.5**. A 100 mV rail step → ~725 mV at ADC5, well over a typical 0.1–0.5 V threshold → **false trigger**. In TRACK mode the 0.66 s HPF removes it; in HOLD it does not. | Phase 3: while armed and holding, scope +5V and ADC5 together. Mitigations if real: keep the rail stiff while armed, shorten the armed window, or don't freeze the baseline. |

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
- [ ] **1b** Full FSM + Pi soft-shutdown against a **simulated** Pi
- [x] **1c** Panel indicators on J7 — **complete.** §1c.2 function test passes, `panel demo`
      confirms all six ring patterns, ring current measured at 7.8 mA (safe, internally
      limited). Automatic state→pattern *mapping* still to be confirmed during 1b, since
      most of those states need a simulated Pi (BENCH §1c.4).
- [ ] **2** Beam carrier + demod phase lock (2a on LA first, then 2b ramped)
- [ ] **3** Photodiode: static health → phase cal → threshold → `scan carrier` → ball transit
- [ ] **4** Trigger source experiment (comparator vs. ADC bias table)
- [ ] **5** Microphone *(can be pulled forward — runs on USB power alone, no latch needed)*
- [ ] **6** Strobe: 6a dry → 6b gate DAC → 6c LED bank ramp → 6d clamp-with-current
- [ ] **7** Cameras: 7a loopback → 7b delayed sim → 7c real (needs Pi)
- [ ] **8** Real Pi integration + clean-shutdown acceptance test

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
| | **U9 beam one-shot clamp** | | **expect ~86 µs, not 113 (Q1)** |
| | **U5 strobe one-shot clamp** | | **expect ~86 µs — sets `STROBE_SW_MAX_US` (Q1)** |
| | Beam duty fidelity limit | | frequency where TP5 stops tracking commanded duty (Q2) |
| | Chosen carrier frequency | | from `scan carrier` max-SNR (Q3) |
| | Pi 3V3 at halt | | decides Q4 |
| | Pi shutdown duration | | set min hold-off ≈ 2× this |

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
  tools/openocd_pi5.cfg       SWD from a Pi 5 via linuxgpiod
  tools/flash_swd.sh
```

`src/board.h` carries the load-bearing hardware comments. Read it before touching anything.

---

## 8. Decisions made (so they aren't re-litigated)

- **C / Pico SDK 2.x**, not Rust — matches the .md pseudocode, first-class PIO/DMA/dual-core.
- **Carrier = 104.1667 kHz** (sysclk 150 MHz, TOP=1439, level=432 → exactly 30.000 %),
  as a *starting point* pending `scan carrier`.
- **ADC while armed = round-robin {ch5, ch7} at 250 ksps each**, resolving the .md's unstated
  conflict between the detect signal and the mic both wanting to free-run.
- **Trigger = comparator starts the timing, ADC refines during the camera-handshake dead
  time.** Gets the comparator's latency and the ADC's amplitude-independent accuracy for
  free. **Time the transit with a PIO state machine, not a GPIO ISR** (`ARCHITECTURE.md` A2)
  — one FIFO word gives the interval directly, with no handler and no jitter.
- **The CPU orchestrates; hardware executes.** Anything that must happen at a *specific
  time* goes to PWM/PIO/DMA/ADC. Anything that merely has to happen *soon* stays on a core.
  Full audit and allocation table in `ARCHITECTURE.md`.
- **RPI5_SHUTDOWN active-low**, matching the `gpio-shutdown` overlay default.
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
- `git init` at the project root — **recommended, not yet done.** No version control exists
  for either the KiCad project or the firmware. Worth doing before the first bench session.
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
- 🟡 Comparator timing and the camera handshake are both specced as CPU work in the .md
  (ISR and busy-wait respectively). Both should be PIO — `ARCHITECTURE.md` A2/A3. We have
  **8 free state machines across three PIO blocks**; the .md assumed two blocks and
  economised accordingly.

---

## 10. Next session — start here

Toolchain installed, firmware builds and runs, Phase 0 passed on USB power. So:

Phases 0, 0.5 and 1 are done and the whole power path is proven. So:

1. **`BENCH.md` Phase 1b** — full FSM + Pi soft-shutdown against a simulated Pi.
   **The hard gate before any real Pi touches J8.** Wiring: +3.3 V from **J5.2** jumpered
   to **J8.1** (PI_3V3_SENSE) and **J8.15** (RPI5_ON) — *not* J8.1→J8.15, since J8.1 is
   dead without a Pi driving it. Confirm each with `pisim` before starting.
   The single non-negotiable test is **reset ≠ shutdown**: scope GPIO43 through an SW2
   press and confirm no assertion edge.
2. **Then Phase 2** — `BENCH_P2_BEAM.md`. **Firmware is written and in the build.** The beam
   cannot start at boot; it stays dark until `beam on`. Do **2a on the logic analyzer first**
   (phase lock, LED dark) — trickiest code in the project, and free to prove before current
   flows. Then PSU limit ≥ 2.5 A for 2b, since the beam averages ~0.95 A at 30 %.
3. Fill in §6 as you go. The two measurements that change code once taken: the **U9/U5
   one-shot clamps** (Q1, expect ~86 µs not 113) and the **chosen carrier** (Q3).

**New CLI in this build (Phase 2):** `beam` (status), `beam on|off`, `beam freq <hz>`,
`beam duty <pct>` (refuses >35 %), `beam ramp <pct> [step_ms]` (1 % steps — use this, not
`duty`, above ~5 %), `beam phase <ticks>`, `beam clamp` (1 kHz/50 % for the Q1 measurement),
`beam sweep <f0> <f1> <n> <dwell_ms>` (Q2 fidelity sweep).

**Recommended before the next session:** `git init` at the project root. There is a lot of
working state now — a verified Phase 1 image, five bench documents, and a board that took
real effort to characterise — with no version control on any of it.

**Rebuilding from the command line** (faster than the IDE button):
```
& "$env:USERPROFILE\.pico-sdk\ninja\v1.13.2\ninja.exe" -C <firmware>\build
```
