# START HERE

Written for someone who has never built firmware for a Pico. No C knowledge
assumed. Follow it top to bottom; each step says what to expect so you can tell
whether it worked.

**Total time:** about 30–45 minutes for the first build, most of it downloads.

---

## What you're actually about to do

Four things, in order:

1. **Install a toolchain** — a compiler that turns `.c` files into a binary the
   RP2354 chip can run. Your PC's normal compiler makes Windows programs; this
   one makes ARM microcontroller programs.
2. **Build** — run the compiler. Produces `pitrac.uf2`, a single file.
3. **Flash** — copy that file onto the chip. The RP2354 has a trick where it
   pretends to be a USB memory stick, so "flashing" is literally drag-and-drop.
4. **Talk to it** — the firmware has a text menu over the USB cable. You open a
   terminal, type `stat`, and it answers.

Nothing here can damage the board. You're on USB power only, the +5 V rail is
off, and the firmware refuses to turn it on while running from USB.

---

## Step 1 — Install the toolchain

Use the VS Code extension. It downloads its own private copies of everything
into `C:\Users\<you>\.pico-sdk` and doesn't touch your system PATH, which on
Windows saves a great deal of pain.

1. Open **VS Code**.
2. Click the **Extensions** icon in the left sidebar (four little squares), or
   press `Ctrl+Shift+X`.
3. Search for **`Raspberry Pi Pico`**. Install the one published by
   **Raspberry Pi**. (Not "Pico-Go", not "Pymakr" — those are for MicroPython.)
4. After it installs, a small Raspberry Pi logo appears in the left sidebar.

> The extension downloads ~1–2 GB the first time it configures a project. That's
> normal and it only happens once.

---

## Step 2 — Open the project and let it configure

1. **File → Open Folder** → choose:
   ```
   C:\Users\ATTAYEKP\Github-Personal\PiTrac_V2\Hardware\firmware
   ```
   Open the **`firmware`** folder itself, not the folder above it. This matters —
   `CMakeLists.txt` has to be at the top level of what you open.

2. Press `Ctrl+Shift+P` to open the command palette (a text box at the top).
   Type **`Pico: Import Project`** and press Enter.

   > **⚠ It will look like it hangs. It hasn't.** The first thing this command
   > does is open a *native Windows folder picker*, and on Windows that dialog
   > very often opens **behind** the VS Code window. **Alt+Tab to find it.**
   > After the folder picker there is a second step: a **form in a new tab**
   > with an **Import button at the bottom that you have to scroll down to**.
   > Nothing downloads until you click it.

3. In that form:

   | Field | Choose |
   |---|---|
   | **ARM vs RISC-V** | **ARM.** Not negotiable — see below. |
   | **SDK version** | Newest offered, and it **must be ≥ 2.1**. Earlier SDKs don't know about the RP2354. |
   | **Board type** | Anything. `CMakeLists.txt` forces the real board and a compile-time assert catches any override. |
   | **Debugger** | Default is fine. Not used yet. |

   > **Why ARM matters:** the RP2350 die carries *both* dual Cortex-M33 (ARM)
   > and dual Hazard3 (RISC-V) cores, and the extension will cheerfully set you
   > up for RISC-V. All of this firmware assumes ARM, and the `.uf2` has to be
   > family `rp2350-arm-s`. Picking RISC-V produces confusing build failures.

4. Click **Import**, then wait. *Now* it downloads (~1–2 GB, once).

**Expect:** `C:\Users\<you>\.pico-sdk\` appears and grows. That folder existing
is the signal that the extension actually started work — if it's still absent,
the form was never submitted.

---

## Step 3 — Build

Press `Ctrl+Shift+P` → **`Pico: Compile Project`**.

Or click the **Compile** button that the extension adds to the blue status bar
at the bottom of the window.

**Expect:** a wall of scrolling text, then something like
```
[100%] Built target pitrac
```
and a new file at `firmware/build/pitrac.uf2`.

**The firmware builds clean** (SDK 2.3.0, toolchain 15_2_Rel1, no warnings) — roughly
73 KB of 2 MB flash and 35 KB of 520 KB RAM. If *your* first build throws errors it is
almost certainly the toolchain or the import step, not the code; see Troubleshooting at
the bottom.

**Faster than the IDE button**, once it has configured itself once:
```
& "$env:USERPROFILE\.pico-sdk\ninja\v1.13.2\ninja.exe" -C <firmware>\build
```

---

## Step 4 — Flash the board

The RP2354 boots into a USB-drive mode when you hold a button during reset.

1. Plug a USB-C cable from your PC into **J6** on the board. Nothing else
   connected — no bench supply, no Pi, and the **J2 jumper off**.
2. On the board, find **SW1** (BOOTSEL) and **SW2** (RUN). They're the two small
   tactile buttons.
3. **Hold SW1 down. While holding it, press and release SW2. Then let go of SW1.**
4. Windows should pop up a removable drive named **`RPI-RP2`**.
5. Drag `firmware\build\pitrac.uf2` onto that drive.
6. The drive disappears on its own — that's success, not an error. The chip
   reboots and starts running your firmware.

**Expect:** the yellow LED (D5) starts blinking slowly, roughly once every two
seconds. That's "standby, healthy."

> If no `RPI-RP2` drive appears: try a different USB-C cable. A surprising number
> of USB-C cables are charge-only and carry no data lines.

---

## Step 5 — Talk to the board

The firmware presents a text menu over the same USB cable.

**Find the COM port:** Windows Device Manager → **Ports (COM & LPT)** → look for
something like "USB Serial Device (COM7)". Note the number.

**Connect.** Easiest option, since you already have VS Code open:

1. `Ctrl+Shift+P` → **`Serial Monitor: Start Monitoring`**
   (install the **Serial Monitor** extension by Microsoft if that command isn't
   there — it's a small one-click install).
2. Pick your COM port. Baud rate doesn't matter for USB serial; leave the default.
3. Set line ending to **CR** or **LF** if there's a dropdown.

**Expect:** press Enter and you get a `>` prompt. Type `help` and you get the
command list.

---

## Step 6 — Your first real test

Type these three, in order:

```
id
```
Shows the firmware build time and the chip's unique ID. Confirms you're talking
to the right thing.

```
stat
```
The important one. Expect roughly:
```
state    : STANDBY (12345 ms)
fault    : none
5V_IN    : 4.850 V   (latch guard 5.05 V, load floor 4.60 V, scale 1.0630)
latch    : 0   railsready 0
pi       : present 0  3v3 0  userspace 0  isdown 1
button   : released
adcmode  : 1
```
The number that matters is **5V_IN in the 4.6–4.85 V range** — 4.85 V was measured on
this board, but USB VBUS legitimately varies by port and 4.59 V has been seen on another.
That is USB power arriving through diode D8, and it is below the **5.05 V latch guard**,
which is the point.

> **Two different thresholds appear on that line and they are not interchangeable.**
> `latch guard` (5.05 V) is the USB-vs-real-supply discriminator, asked once before the
> latch closes. `load floor` (4.60 V) is a much lower bar asked *after* the latch closes,
> because a real supply is allowed to sag once the Pi and the boost start drawing. A third
> threshold, 4.90 V, runs continuously while latched. See `board.h` for why all three exist.
>
> **`scale` should read 1.0630, not 1.0000.** The +5V_IN read path is ~5.9 % low
> (`PROGRESS.md` Q9), so that compile-time correction is what makes a good 5.2 V supply
> read as 5.2 V instead of 4.89 V. If you see 1.0000, you are running an old build and the
> board will refuse to latch on a perfectly good supply.

```
adc5v
```
Must print **"USB-only, latch INHIBITED"**.

**That message is the whole point of this first test.** It means the firmware
correctly refuses to switch on the +5 V rail — the rail that would feed a
Raspberry Pi 5 — while running on USB power that can't support it. If you ever
see it say "latch permitted" while on USB alone, stop and tell me.

One more, to prove the analog path and the data plumbing:
```
capture 0x02 1000 100000
```
This samples the +5 V monitor 1000 times and prints the numbers as CSV. Expect a
flat run of nearly identical values **around 2830** on USB power. (These are raw ADC
codes, before the 1.063 scale is applied — that is why the number looks low against
`stat`'s 4.850 V.) This same command is what makes the later optical work possible —
it turns the board into its own oscilloscope.

---

## After that

You've finished **Phase 0**. Next is **Phase 0.5**, the first time the board sees
real power — and the first time anything can actually go wrong.

Open **`BENCH.md`** and work through Phase 0.5. It needs your bench supply,
current-limited, and you go up in stages: 0.3 A first to confirm only the low
power domain draws, then 2 A to bring up the 36 V boost.

Record every measurement in the table in **`PROGRESS.md` §6** as you take it.
Several later decisions depend on those numbers.

---

## Troubleshooting

**`Pico: Import Project` seems to hang and nothing happens**
It's waiting on a dialog you can't see. Alt+Tab for a native folder picker
hiding behind the VS Code window, and check for a new tab containing a form with
an Import button at the bottom. Confirm whether it ever really started by
checking that `C:\Users\<you>\.pico-sdk\` exists — no folder means no work has
begun, regardless of what the UI looks like.

**"PICO_SDK_PATH is not set"**
The extension didn't finish configuring. Re-run `Pico: Import Project`. If it
still fails, delete the `build` folder and try again.

**Build errors mentioning `riscv`, or a linker that can't find ARM libraries**
The project got imported for the RISC-V cores. Re-run `Pico: Import Project` and
choose **ARM**, then delete the `build` folder before rebuilding.

**Errors that mention `adc_fifo_setup`, `adc_set_round_robin`, or argument counts**
An SDK version difference — those function signatures changed between SDK
releases. Paste the exact error to me; it's a one-line fix each.

**`Built for the wrong chip variant`**
The compile-time guard caught a bad configuration. Delete the whole `build`
folder and rebuild — a stale CMake cache is the usual cause.

**Build succeeds but no `RPI-RP2` drive appears**
Almost always the cable. Try another one, ideally one you know carries data.
Second most likely: you released SW1 too early. Hold it through the entire SW2
press and for a moment after.

**COM port appears but typing does nothing**
Check the line-ending setting in your terminal — the CLI needs CR or LF on
Enter. Also try pressing Enter once first; the banner may have scrolled past
before you connected.

**The board disconnects every time you rebuild**
Expected. Reflashing resets the chip, which drops the USB serial connection.
Reconnect the serial monitor after each flash.

**You want to reflash without the button dance**
That's what SWD is for — `tools/flash_swd.sh` and `tools/openocd_pi5.cfg`, using
your Pi 5 with flying leads (the Pi is *not* seated on the header). Worth setting
up once you're iterating; the buttons get old around the twentieth cycle.

---

## Cheat sheet

| I want to | Do this |
|---|---|
| Build | `Ctrl+Shift+P` → `Pico: Compile Project` |
| Flash | Hold SW1, tap SW2, release SW1, drag the `.uf2` |
| Connect | `Ctrl+Shift+P` → `Serial Monitor: Start Monitoring` |
| See the commands | type `help` |
| See board state | type `stat` |
| Reboot to flash mode without buttons | type `bootsel` |
| Start over on a bad build | delete `firmware\build`, rebuild |

**Document map**

| Doc | What it is |
|---|---|
| `START_HERE.md` | You are here — build, flash, first commands |
| **`PROGRESS.md`** | **The living record.** What's done, open questions, measurement log. Read this first when resuming. |
| `BENCH.md` | Phases 0 → 1c: toolchain, rails, latch, Pi shutdown, panel |
| `BENCH_P2_BEAM.md` | Phase 2 — beam carrier and demod phase lock |
| `BENCH_P3_DETECT.md` | Phases 3 & 4 — photodiode chain and trigger selection |
| `BENCH_P5_P7_MIC_CAMERA.md` | Phases 5 & 7 — mic (USB power only) and cameras |
| `BENCH_P6_STROBE.md` | Phase 6 — ⚠ 9 A, linear-mode FET. Read fully before powering. |
| `BENCH_P8_PI.md` | Phase 8 — real Pi integration |
| `ARCHITECTURE.md` | What runs on PIO/PWM/DMA vs the CPU |
| `SETUP.md` | Toolchain detail and the manual install path |

**Current status: phases 0 through 1c are complete on hardware. Phase 2 is next.**
