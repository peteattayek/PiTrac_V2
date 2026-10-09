# PiTrac V2 — Hardware

KiCad design and RP2354 firmware for the PiTrac launch monitor controller board:
*"The Second Board To Rule Them All"*, Rev V1.

> **New to this repo?** Start with [`../DEVELOPER_GUIDE.md`](../DEVELOPER_GUIDE.md). It covers
> the repo map, the full pinout, how the firmware is built and why, and what is not done yet.

The board detects a golf ball crossing an IR light curtain, measures its speed from
the beam transit time, triggers two Mira220 global-shutter cameras, and fires a
high-power IR strobe in a precisely timed burst so each camera freezes the ball
~10 times in a single exposure. A Raspberry Pi 5 rides on top — powered *by* this
board through the 40-pin header — and handles image pull and analysis.

**The RP2354 owns everything with microsecond timing requirements. The Pi owns
everything with pixels.**

---

## Layout

```
Hardware/
├── The_Second_Board_To_Rule_Them_All.md     ← design document: theory of operation,
│                                              subsystem detail, test points, pin map
├── The_Second_Board_To_Rule_Them_All/       ← KiCad 10 project, 8 sheets
│   ├── *.kicad_sch, *.kicad_pcb
│   ├── *.net                                 netlist export (firmware is verified against this)
│   └── jlcpcb/production_files/              gerbers + BOM + CPL as sent to fab
└── firmware/                                ← RP2354B firmware, C / Pico SDK 2.x
```

`../HARDWARE_REFERENCE.md` (repo root) is **generated** from the netlist by
`firmware/tools/netlist_report.py`. Regenerate it rather than editing it.

### Camera calibration

[Calibration/README.md](Calibration/README.md) contains the camera-distortion
procedure, A4/Letter ChArUco print templates, capture checklist, and session/result
records. Use a separate calibration for each identified physical camera/lens
stack at its final filter, illumination, and locked-focus state. Digital target
checks do not establish physical print quality or calibrated camera coefficients.

### Firmware documents, in reading order

| File | What it is |
|---|---|
| **`firmware/START_HERE.md`** | **Start here if you have never built Pico firmware.** Toolchain install, build, flash, first CLI commands. Assumes no C or embedded experience. |
| `firmware/HANDOFF.md` | Working conventions, safety rules, tooling traps and hard-won measurement lessons, for anyone taking over the bench work. |
| `firmware/PROGRESS.md` | **The living record.** What is done, open questions with where each gets resolved, recent bench measurements, the decisions behind the design, and the code-audit backlog. §0 and §10 hold the live status. Read before changing anything. |
| `firmware/PROGRESS_ARCHIVE.md` | Closed history moved verbatim out of PROGRESS.md (older measurement log, superseded resume blocks, dated Phase 3 results). Nothing in it is current status. |
| `firmware/ARCHITECTURE.md` | Hardware-offload audit — what runs on PIO/PWM/DMA versus the CPU, and why. |
| `firmware/BRINGUP_NEW_BOARD.md` | **The driver for a newly assembled board.** Only the per-board steps, in order, with a sign-off table. |
| `firmware/BENCH.md` | Bring-up procedures, phases 0 → 1c. |
| `firmware/BENCH_P2_BEAM.md` | Phase 2 — beam carrier and phase-locked demodulator. |
| `firmware/BENCH_P3_DETECT.md` | Phases 3 & 4 — photodiode chain and trigger selection. |
| `firmware/BENCH_P5_P7_MIC_CAMERA.md` | Phases 5 & 7 — microphone and cameras. |
| `firmware/BENCH_P6_STROBE.md` | Phase 6 — ⚠ 9 A pulses, linear-mode FET. Read fully before powering. |
| `firmware/BENCH_P8_PI.md` | Phase 8 — real Pi 5 integration and clean-shutdown acceptance. |
| `firmware/NEXT_BOARD_REV.md` | Hardware change requests (CR-xx) for the next board spin. |
| `firmware/SETUP.md` | Toolchain detail and the manual (non-VS-Code) install path. |

---

## Status

Bring-up is staged, and each phase is independently provable on the bench before
the next begins. **The live status is always `firmware/PROGRESS.md` §0 and the top
block of §10** — this table is a summary as of 2026-10-05.

| Phase | |
|---|---|
| 0 · toolchain, CLI, ADC/DMA capture | ✅ |
| 0.5 · rails, boost, virtual ground | ✅ |
| 1 · power button, latch, supply guards | ✅ (Test 6, stale power requests: ✅ owner-reported 2026-09-28) |
| 1b · Pi soft-shutdown vs. a simulated Pi | ✅ |
| 1c · panel indicators | ✅ |
| 2 · beam carrier + demod phase lock | ✅ |
| 3 · photodiode chain and calibration | 🟡 §3.1–3.6 done on board 3; first real ball waveform passed; comparator timing and the 20-pass set are open |
| 4 · trigger-source experiment | 🟡 firmware written, bench not started |
| 5 · microphone | 🟡 front end validated; no onset-detection or veto firmware yet |
| 6 · strobe | 🟡 **6a/6b dry tests PASS on board 1 (2026-10-06)** — U5 clamp 135 µs, PIO timing, schedule, A7, interlock, gate DAC. **6c/6d live-current firmware written 2026-10-07** (guarded: staircase, 70 % ceiling, ADC0 readback per firing, overcurrent/stuck-on faults, watchdog); TP3 loop stability PASS 2026-10-08; next is the first live pulses (6c) |
| 7 · cameras | ❌ no firmware yet; blocked on the 1.8 V I/O translator (CR-09) |
| 8 · Pi integration | 🟡 power and shutdown handshake done; UART protocol not written |

---

## Safety rules that outrank convenience

1. **No Pi 5 on J8 until Phase 7c.** The +5 V latch *is* the Pi's power switch, and any
   RP2354 reset — SW2, `reset`, `bootsel`, the watchdog, a brownout — opens it: a hard power
   cut, not a reboot.
2. **`PULSE_LIMIT_DISABLE` (GPIO27) stays 0.** It defeats the strobe hardware
   watchdog. It is written in exactly one place in the firmware and there is
   deliberately no CLI path to it.
3. **Never run the +5 V rail from USB power.** Two independent guards enforce this;
   both exist because the failure mode is a Pi 5 fed through a 1 A diode.
4. **Do not connect J4 (cameras).** The Mira220 I/O is 1.8 V with no 3.3 V tolerance
   (answered 2026-08-14). J4 needs a level translator first — `firmware/NEXT_BOARD_REV.md`
   CR-09.
5. **Nothing conductive near the photodiode D12.** Its cathode sits at 36 V; foil across it
   destroyed an op-amp on board 1.

---

## Building the firmware

```
cd firmware
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Needs Pico SDK **2.1+** (the project pins 2.3.0) and an ARM toolchain — `firmware/SETUP.md`
covers both, and the VS Code Pico extension installs everything itself if you would rather
not. The extension keeps its tools in `~/.pico-sdk` and does **not** put them on `PATH`;
see `firmware/HANDOFF.md` §4 for the command-line build.

The board is an **RP2354B**: RP2350B core, 48 GPIO, 2 MB stacked internal flash,
**ARM** (not RISC-V). `CMakeLists.txt` forces the correct board and there is a
compile-time assert that fails loudly if anything overrides it.

---

## Design corrections

The design document predates bring-up, and several of its numbers were superseded by
measurement. The evidence is in `firmware/PROGRESS.md`; the full list is in
[`../DEVELOPER_GUIDE.md`](../DEVELOPER_GUIDE.md) §4. The ones most worth knowing:

- **The virtual ground is not 2.50 V.** R75/R76 = 10K/10K makes it literally
  +5VA ÷ 2 — measured **2.59 V** on the 5.2 V rail. It also moves with the rail (CR-02).
- **The one-shot clamp is 122.68 µs** (U9, measured in Phase 2c), not the .md's 113 µs
  and not the 86 µs the datasheet's K ≈ 0.7 predicted. U5, the strobe one-shot, is still
  unmeasured.
- **`cal_demod_phase()` in §13.8 cannot work as written** — it samples after a
  0.66 s high-pass with a *static* reflector, so it reads noise at every phase. The
  firmware's chopped-beam `cal demod` replaces it.
- **RPI5_SHUTDOWN is active-low**, matching the default `gpio-shutdown` overlay —
  confirmed on hardware as a 200 ms low pulse. The .md pseudocode is backwards.
- **The PWM slice numbers in the .md are RP2040 numbering.** On the RP2350B the beam
  carrier is slice 7B, the demod clock 11B and the gate DAC 6A — which collides with the
  ready LED (CR-01).
- **`HPF_Toggle` low is TRACK**, not HOLD.

---

## License

See [`../LICENSING.md`](../LICENSING.md). Board design files are **CERN-OHL-S v2**
(`../LICENSES/CERN-OHL-S-2.0.txt`); firmware and host tools are **GPL-3.0-or-later**
(`../LICENSE`).
