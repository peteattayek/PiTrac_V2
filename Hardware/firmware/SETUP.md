# Toolchain Setup

> **✅ Already installed on this machine (as of 2026-07-29) — you do not need this document.**
> Path A was taken. Installed: **SDK 2.3.0, toolchain 15_2_Rel1, ninja 1.13.2, cmake 4.3.4,
> picotool 2.3.0**, all in `%USERPROFILE%\.pico-sdk`. The firmware builds clean.
>
> Rebuild from the command line, faster than the IDE button:
> ```
> & "$env:USERPROFILE\.pico-sdk\ninja\v1.13.2\ninja.exe" -C <firmware>\build
> ```
>
> What follows is kept for a **second machine or a fresh checkout.** The original note said
> "nothing is installed" — true when written on 2026-07-28, misleading now.

Pick **one** of the two paths below. The VS Code extension path is much less fiddly on Windows
and is what I'd recommend — it installs its own private copies of CMake, Ninja, the ARM
toolchain, picotool, OpenOCD, and the SDK into `%USERPROFILE%\.pico-sdk`, so nothing pollutes
your system PATH.

---

## Path A — Raspberry Pi Pico VS Code extension (recommended)

1. In VS Code, install the extension **"Raspberry Pi Pico"** (publisher: Raspberry Pi,
   id `raspberry-pi.raspberry-pi-pico`).
2. Open the `firmware/` folder in VS Code.
3. The extension will offer to install the SDK and toolchain — accept. Choose **SDK 2.1.x or
   newer** (RP2350/RP2354 support is required; 2.0 is too old for RP2354).
4. Command palette → **"Raspberry Pi Pico: Configure CMake"**, then build.

The extension writes a `.vscode/settings.json` pointing at its private toolchain. That file is
gitignored here on purpose — it contains machine-specific absolute paths.

**Verify it took:**
```
%USERPROFILE%\.pico-sdk\sdk\<version>\      <- SDK
%USERPROFILE%\.pico-sdk\toolchain\<ver>\    <- arm-none-eabi
%USERPROFILE%\.pico-sdk\picotool\<ver>\
```

---

## Path B — Manual, command line

1. **ARM toolchain** — Arm GNU Toolchain, `arm-none-eabi`, Windows installer:
   <https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads>
   Tick "Add path to environment variable" at the end.
2. **CMake** ≥ 3.13 — <https://cmake.org/download/> (add to PATH).
3. **Ninja** — <https://github.com/ninja-build/ninja/releases> (put `ninja.exe` on PATH).
4. **Pico SDK 2.1+**:
   ```powershell
   git clone -b master --recurse-submodules https://github.com/raspberrypi/pico-sdk.git C:\pico-sdk
   [Environment]::SetEnvironmentVariable('PICO_SDK_PATH','C:\pico-sdk','User')
   ```
   Reopen the shell so `PICO_SDK_PATH` takes effect.
5. **picotool** (optional but very useful — `picotool info -a` reads the die revision we need
   for erratum E9): <https://github.com/raspberrypi/picotool>, or it comes with Path A.

---

## Building

```powershell
cd firmware
cmake -B build -G Ninja -DPICO_BOARD=pitrac_ltb_v1 -DPICO_PLATFORM=rp2350
cmake --build build
```

Output: `build/pitrac.uf2` (and `.elf` for gdb).

`PICO_BOARD=pitrac_ltb_v1` picks up `boards/pitrac_ltb_v1.h` — `CMakeLists.txt` already adds
`boards/` to `PICO_BOARD_HEADER_DIRS`.

**Sanity-check the build before flashing:**
```powershell
picotool info build\pitrac.uf2
```
Family must be `rp2350-arm-s`. Anything else means the platform/board flags didn't apply.

---

## Flashing

> **Freshly assembled board?** Flash it, then work through **`BRINGUP_NEW_BOARD.md`** before
> using it. Two of those steps are safety gates — the ADC +5 V scale is per-chip, and a wrong
> one can let the board latch on USB power and back-feed a Pi through a 1 A diode.


### USB BOOTSEL
Hold **SW1** (BOOTSEL), tap **SW2** (RUN), release SW1. A drive named `RP2350` appears — drag
`build/pitrac.uf2` onto it. Or, with the board in BOOTSEL:
```powershell
picotool load -x build\pitrac.uf2
```
From the 2026-10-19 image on, `picotool … -f` cannot reboot a running board into BOOTSEL: the
firmware has no USB reset path (`BENCH_P8_PI.md` §8.0). Use `bootsel` at the CLI, or SW1 + SW2.

From image 1.1 (2026-10-20) every reflash is a **trial**: the image buys itself at the end of its
init, and one that never gets there is replaced by the previous image within ~17 s. A board with
the A/B partition table (installed once, `BENCH_P8_PI.md` §8.7) puts each drag into the partition
that is not running. **Never load an image older than 1.1 onto such a board** (HANDOFF §4).

### From a Pi on the board
`pitrac-link update build/pitrac.bin` on the Pi: `pi/README.md`. Needs the partition table.

### SWD from a Pi 5 (flying leads — Pi NOT seated on J8)
See `tools/openocd_pi5.cfg` and `tools/flash_swd.sh`, and §4 of `PROGRESS.md` for the wiring.
From image 1.1 the script refuses unless told otherwise: SWD writes at flash offset 0, over the
partition table, and a try-before-you-buy image never starts after an SWD reset. It remains
only for a `-DPITRAC_TBYB=OFF` build on a board without the table.

---

## Talking to the board

The CLI is USB-CDC. On Windows the board appears as a COM port. Any terminal works
(PuTTY, `screen`, VS Code serial monitor). `tools/scope.py` uses pyserial:

```powershell
python -m pip install pyserial matplotlib
python tools\scope.py --port COM7 --channel 1 --samples 2000
```

Type `help` at the CLI prompt for the command list.
