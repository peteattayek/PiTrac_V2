# PiTrac V2 — Hardware

KiCad design and RP2354 firmware for the PiTrac launch monitor controller board:
*"The Second Board To Rule Them All"*, Rev V1.

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

### Firmware documents, in reading order

| File | What it is |
|---|---|
| **`firmware/START_HERE.md`** | **Start here if you have never built Pico firmware.** Toolchain install, build, flash, first CLI commands. Assumes no C or embedded experience. |
| `firmware/PROGRESS.md` | **The living record.** What is done, open questions with where each gets resolved, every bench measurement taken, and the decisions behind the design. Read before changing anything. |
| `firmware/ARCHITECTURE.md` | Hardware-offload audit — what runs on PIO/PWM/DMA versus the CPU, and why. |
| `firmware/BENCH.md` | Bring-up procedures, phases 0 → 1c. |
| `firmware/BENCH_P2_BEAM.md` | Phase 2 — beam carrier and phase-locked demodulator. |
| `firmware/BENCH_P3_DETECT.md` | Phases 3 & 4 — photodiode chain and trigger selection. |
| `firmware/BENCH_P6_STROBE.md` | Phase 6 — ⚠ 9 A pulses, linear-mode FET. Read fully before powering. |
| `firmware/BENCH_P5_P7_MIC_CAMERA.md` | Phases 5 & 7 — microphone and cameras. |
| `firmware/SETUP.md` | Toolchain detail and the manual (non-VS-Code) install path. |

---

## Status

Bring-up is staged, and each phase is independently provable on the bench before
the next begins. Current state is always in `firmware/PROGRESS.md` §5.

| Phase | |
|---|---|
| 0 · toolchain, CLI, ADC/DMA capture | ✅ |
| 0.5 · rails, boost, virtual ground | ✅ |
| 1 · power button, latch, supply guards | ✅ |
| 1b · Pi soft-shutdown vs. a simulated Pi | in progress |
| 1c · panel indicators | ✅ |
| 2 · beam carrier + demod phase lock | code written, untested |
| 3–8 · detection, mic, strobe, cameras, Pi | planned, protocols written |

---

## Safety rules that outrank convenience

1. **No Pi 5 on J8 until Phase 7c**, and not before Phase 1b passes. The +5 V latch
   *is* the Pi's power switch; a firmware bug corrupts its SD card.
2. **`PULSE_LIMIT_DISABLE` (GPIO27) stays 0.** It defeats the strobe hardware
   watchdog. It is written in exactly one place in the firmware and there is
   deliberately no CLI path to it.
3. **Never run the +5 V rail from USB power.** Two independent guards enforce this;
   both exist because the failure mode is a Pi 5 fed through a 1 A diode.
4. **Resolve the Mira220 1.8 V I/O question before connecting J4.** It can damage
   the sensor and must not be discovered empirically.

---

## Building the firmware

```
cd firmware
cmake -B build -G Ninja
cmake --build build
```

Needs Pico SDK **2.1+** and an ARM toolchain — `firmware/SETUP.md` covers both, and
the VS Code Pico extension installs everything itself if you would rather not.

The board is an **RP2354B**: RP2350B core, 48 GPIO, 2 MB stacked internal flash,
**ARM** (not RISC-V). `CMakeLists.txt` forces the correct board and there is a
compile-time assert that fails loudly if anything overrides it.

---

## Design corrections

The firmware documents several places where the design document is wrong or
unverified. These are recorded with evidence in `PROGRESS.md`, and are worth
knowing before trusting the .md:

- **The virtual ground is not 2.50 V.** R75/R76 = 10K/10K makes it literally
  +5VA ÷ 2 — measured **2.59 V** on the 5.2 V rail.
- **The one-shot clamp is probably ~86 µs, not 113 µs** (0.7·R·C at 3.3 V). This
  changes the strobe pulse-width limits. Measured in Phase 2c.
- **`cal_demod_phase()` in §13.8 cannot work as written** — it samples after a
  0.66 s high-pass with a *static* reflector, so it reads noise at every phase.
- **RPI5_SHUTDOWN polarity** in §13.5 is backwards for the default
  `gpio-shutdown` overlay.

---

## License

**TODO — pick one before publishing.** For an open-source hardware project the
usual pairing is CERN-OHL-S or -W for the board files and MIT or Apache-2.0 for
the firmware. Add `LICENSE` at the repo root and note any split here.
