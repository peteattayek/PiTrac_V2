# PiTrac RP2354 Firmware — Progress & Resume Context

**Read this first when resuming work.** It records what is done, what is next, and the
decisions/measurements that must not be lost between sessions.

Last updated: **2026-10-09** (handoff audit for a change of model/harness; the last bench entry is 2026-10-08, TP3 loop stability PASS on board 1). **Current status is in §0 and the TOP block of §10.**

**History moved 2026-10-05:** the 2026-08-24 overview, the Phase 2 bring-up fix table and the
old companion-docs table are in [`PROGRESS_ARCHIVE.md`](PROGRESS_ARCHIVE.md), verbatim. The
document map and reading order are in [`../README.md`](../README.md) and `HANDOFF.md`.

---

## 0. Where things stand

| | Status |
|---|---|
| **Handoff** | 🔵 **New to this work? Read `HANDOFF.md` first** (conventions, safety, tooling pitfalls, reading order; for a whole-repo developer overview see `DEVELOPER_GUIDE.md` at the repo root), then §10 for the live resume point. There is **no separate plan file** — the `~/.claude/plans/…` paths this table used to cite no longer exist. PROGRESS is the plan. |
| **Boards** | **Board 3** — UID `6d6fda754e367a40`, **reworked TIA (Rf 116 kΩ, Cf 0.99 pF)**, calibrated and saved (carrier 104166 Hz, phase 1311 ticks, slot A seq 5); runs the `Oct  5 2026 11:13:55` image; 6a.1 showed **no pulse at R61** (2026-10-05). **Board 2** — stock 470 kΩ TIA, calibrated; **a probe on R61 sparked 2026-10-05**, damage check pending. ⚠ One of boards 2/3 reads **10 Ω from R61 to GND even with R62 removed** (2026-10-06) — parked, R62 off; which board is to be confirmed (§6). 🟡 **Board 1** — UID `a764f5332ca5ac53`; U11B destroyed 2026-08-17 by foil across D12 (§11): **out of service for optics**, **the strobe board** — fast-path bring-up and **6a/6b PASS 2026-10-06**, TP3 loop stability **PASS 2026-10-08**; runs the **6c build** (`Oct  7 2026 08:07:33`, inferred from the 2026-10-07/08 logs — confirm with `id`); leads soldered to R33 (GPIO25), R61 pad 1 (U5 Q), TP3, TP4 and two on R66's GND pad. |
| Toolchain | Installed — VS Code Pico extension, private copies in `%USERPROFILE%\.pico-sdk`. SDK 2.3.0, toolchain 15_2_Rel1, ninja 1.13.2, cmake 4.3.4. ⚠ **Not on PATH** — see `HANDOFF.md` for the export line. |
| Bench equipment | Saleae **Logic Pro 8** (analog inputs **±10 V / 12 V max**, ~5 MHz analog bandwidth, up to 50 MS/s); **Tek MSO54B** (500 MHz, 4 ch, TPP1000 probes — **available again 2026-10-08**); Tektronix **TDS 1002** (60 MHz, 2 ch, 8-bit; the fallback, 2026-10-07); DMM, current-limited PSU, FLIR thermal camera. |
| **Firmware** | ✅ **Current image: `Oct  7 2026 08:07:33`** (Release), **184440 text / 0 data / 122720 bss**, `pitrac.uf2` SHA256 `DE0C6676…2DE80BC2`; **78/78 host tests PASS**. **On board 1** — inferred, not yet confirmed by `id`: its 2026-10-07 and 10-08 logs print `Dry pulses are now REFUSED`, wording that exists only in the 2026-10-07 builds, and this is the only one handed over. **No live (6c) pulse has been fired.** Board 3 still runs `Oct  5 2026 11:13:55` (13/13 smoke PASS). The 6c build is the 2026-10-05 image plus **strobe 6c/6d live mode** (`strobe live on confirm`: pulses with a setpoint under the staircase / 70 % ceiling / pacing / charge guards, ADC0 readback and verdict per firing, `STROBE_OVERCURRENT` and `STROBE_CLAMP` faults, watchdog armed by the strobe, CLI allowlist, `strobe cal`, `strobe wave`), the "before 6c" §9 items, and two fixes from an independent review (§6 2026-10-07). Dry behaviour (6a/6b) is unchanged. Written: power FSM, safe state, beam carrier + demod, ADC engine (DMA ring, block capture, **triggered capture**, freeze-and-copy), detect (threshold DAC, gated HPF, comparator PIO timing, pass log + retained waveforms), cal (`cal demod`, `cal model`, `scan carrier`), dual-slot config store, service/yield layer, shot-sequencer **skeleton**, **strobe 6a–6d** (PIO0 burst engine + DMA, gate DAC, §15 schedule, charge interlock, A7 fix, live mode), CLI (every numeric argument strictly parsed). ❌ **Not written:** reported ball velocity, Phase 5 onset detector + mic veto, the strobe in the shot sequencer (Phase 7), persisting the strobe current LUT, Phase 7 camera handshake, Phase 8.6 halt telemetry. Phase 8.0–8.5 power/shutdown firmware exists and passed against a **simulated** Pi (1b); no real Pi has been connected. **Committed 2026-10-09 as `a8e08b3`** (all Phase 6 work, tests and docs), at the owner's request. |
| **Bench work done** | ✅ Phases 0, 0.5, 1, 1b, 1c, 2 previously passed; **power-request regression found 2026-09-18**, fixed, and ✅ **BENCH.md Test 6 PASS (owner-reported 2026-09-28)**; see §6/§10. ✅ **Phase 3 §3.1–3.6 on board 3** — §3.6b is a *design finding* (Q8 ×7.43, CR-02), §3.6 Check 2 PASS. ✅ **Phase 5 mic bring-up** (2026-08-31). ✅ Board 3 detect control-path smoke and **first real optical ball waveform** (2026-09-18), **3.102 V peak / ~22.7 ms FWHM**, no observed clipping. Comparator edge timing and full §3.7/Phase 4 remain open. ✅ **Strobe 6a and 6b PASS on board 1 (2026-10-06)** — U5 clamp 135 µs, LA timing, schedule, A7, interlock, gate DAC (§6). |
| **Next** | **Strobe 6c on board 1** (`BENCH_P6_STROBE.md` 6c): (1) `id` → confirm `built : Oct  7 2026 08:07:33` (reflash `build/pitrac.uf2` if not); ✅ TP3 loop stability **PASS 2026-10-08** (MSO54B, §6); leads soldered per the 6c lead plan; (2) 6c Steps 1–6: refusals and arming, the first live pulse at gate 0, the 3 % staircase with ADC0 against TP4 (scope 20 MHz BW limit), **6d inside Step 3 at ~2 A**, `strobe cal 9`, bursts with the FLIR. Also: identify the parked 10 Ω board (Step −1 on boards 2 and 3). Optical acquisition-window constraint (~59.8 ms needed vs 32.768 ms ring at 500 ksps), §3.7 20-pass set and CR-18 mic impact remain open, deferred while strobe work proceeds. |

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
      **PASS 2026-10-08** (MSO54B, §6). Next: 6c on board 1.
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
    live mode ("no current — load open?"); fix the overstated `board.h` comment above
    `STROBE_LIVE_GATE_STEP_MAX` in the same change. Covered procedurally by the 6c Step 3 stop
    rule until then.
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

> ### ✅ RESUME POINT — 2026-10-09: board 1 on the 6c build, TP3 stability PASS; next is 6c's first live pulse
>
> **New model or harness?** Read `HANDOFF.md` first (updated 2026-10-09 for this handoff), then
> this block, then `BENCH_P6_STROBE.md` 6c. Nothing below depends on the previous session.
>
> **Firmware:** `build/pitrac.uf2`, Release **`Oct  7 2026 08:07:33`**, 184440 text / 122720 bss,
> SHA256 `DE0C6676…2DE80BC2`, 78/78 host tests. **Board 1 is running it** — inferred from its
> 2026-10-07/08 logs (`captures/COM5_*`), which print `Dry pulses are now REFUSED`; no `id` was
> logged. In it: strobe **live mode** (6c/6d) with its guards; `strobe cal`, `strobe wave`; the
> "before 6c" §9 items; two review fixes (§6 2026-10-07). **No live pulse has been fired.**
> Everything through this handoff is committed (`a8e08b3`, 2026-10-09); later work is not, unless
> `git log` says otherwise. The owner commits.
>
> **Board 1** (`a764f5332ca5ac53`, COM5): leads on R33, R61 pad 1, TP3, TP4 and two on R66's GND
> pad. 6a/6b PASS 2026-10-06; **TP3 loop stability PASS 2026-10-08** (MSO54B: AC RMS 3.6–3.8 mV
> at every gate, flat; §6).
>
> **Order (all in `BENCH_P6_STROBE.md`):**
> 1. J3 **disconnected**: `id` → `built : Oct  7 2026 08:07:33` and `fw` line `+ 6c/6d live current
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
> after the first live pulses.
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
