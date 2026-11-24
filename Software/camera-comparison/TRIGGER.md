<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 PiTrac contributors -->

# Triggered Mira220 capture

Two tools let the PiTrac V2 board trigger the Mira220, for bench steps 7c.1 and 7c.2
([`BENCH_P5_P7_MIC_CAMERA.md`](../../Hardware/firmware/BENCH_P5_P7_MIC_CAMERA.md)). In
**slave mode** the sensor exposes while its REQ EXP input is high, and the falling edge starts
readout. Nothing on the Pi drives REQ EXP: the board does, through a TXS0108E module.

Written 2026-11-16, tested against fakes, and used on board 1's Pi in 7c.1 (2026-11-18).

That run lost each capture's last-frame tail: up to 4 KB, held in v4l2-ctl's stdio buffer when
the capture was stopped. Fixed the same day: the frames now go out on v4l2-ctl's stdout,
unbuffered (`stdbuf -o0 … --stream-to=-`).

## Why a switch after every stream start

- **The driver forgets the switch.** The ams driver (the commit `install-mira220.sh` pins)
  writes master mode at every stream start, and powers the sensor down 1 s after the stream
  stops (runtime-PM autosuspend).
- **So the sensor must be switched again** after each start, while it streams. Once powered
  down it does not answer on I²C.
- **The register sequence** is upstream PiTrac's (`PiTracLM/PiTrac`, branch `feature/pico`,
  `CameraTools/mira220_trigger`, commit `afe689e`), checked against the driver. It writes to
  address 0x54:
  1. Stop the imager at a row boundary: `0x1003 = 0x02`, `0x10F0 = 0x00`. Wait 40 ms.
  2. Clear bits 6 and 0 of `0x1001`. The driver writes `0x41`.
  3. Restart as slave: `0x1003 = 0x08`, `0x10F0 = 0x01`.

  `master` sets both bits again and writes `0x1003 = 0x10`.

## Before the first run

- `command -v i2ctransfer`. If it prints nothing: `sudo apt install i2c-tools`.
- `ls /dev/i2c-*` must list the camera's bus, the number in the media entity name
  `mira220 <bus>-0054`. If it does not: `sudo modprobe i2c-dev`.
- `id` must list `i2c`. If it does not: `sudo usermod -aG i2c "$USER"`, then log out
  and in again. (`sudo` will not do for `trigger-capture.sh`: its camera lock needs your
  own runtime directory.)
- Pause the desktop camera clients ([`QUICKSTART.md`](QUICKSTART.md)), and use the 8-bit
  profile: `configure-cameras.sh` without `--mira-bit-depth 12`.

## `trigger-capture.sh`

```bash
CONFIG="$HOME/camera-trig-$(date +%Y%m%d-%H%M%S)"
bash scripts/configure-cameras.sh --output "$CONFIG" --camera mira220 --exposure-us 1000 --gain 1 --apply
bash scripts/trigger-capture.sh --config "$CONFIG" --output "$CONFIG/trig" --frames 3
```

What it does, in order:
1. It streams the Mira220 free-running and waits for the first frame (5 s at most).
2. It runs `mira220-trigger.sh slave`.
3. It waits 0.2 s for frames already in flight, then prints `READY` with the time on
   CLOCK_MONOTONIC.
4. It reports each triggered frame as it arrives.

It ends after `--frames` triggered frames, after `--wait-seconds` (600 by default), or on
Ctrl-C. Each way, it keeps what arrived. The stream stops when it ends, and the next stream
starts free-running.

It waits as long as it takes. Its `v4l2-ctl` runs without `--stream-poll`, so it blocks until a
frame comes. With `--stream-poll`, `v4l2-ctl` gives up after 2 s without a frame. That is why
`focus-preview.sh` and `record-dual.sh` cannot capture triggered frames.

**If frames keep coming at the free-running rate after `READY`,** the switch did not take. The
script stops and says so (`status: free-running`).

### What it writes, to `--output`

| File | What |
|---|---|
| `mira220.raw` | every frame, in order: the free-running ones first |
| `capture.log` | `v4l2-ctl`'s per-frame records |
| `switch.log` | `mira220-trigger.sh`'s output: 0x1001 before and after |
| `frames.tsv` | each frame: sequence, device time, gap, `free-running` or `triggered`, and for a triggered frame its `.pgm`, mean, min, 1st and 99th percentile, max, and saturated fraction |
| `trigger-NN.pgm` | each triggered frame, 8-bit; GIMP and IrfanView open it |
| `summary.tsv` | the counts, the free-running interval, the `READY` time, whether frames kept coming after it, and `status` |

A frame counts as triggered when its device timestamp is later than `READY`. Both are
CLOCK_MONOTONIC.

`conversion/triggered_frames.py` writes the report. It can be rerun on a capture:

```bash
python3 -m conversion.triggered_frames --help
```

## Two cameras (7c.2)

Added 2026-11-20 and tested against fakes; not yet run on a Pi.

- **The Pi:** both overlays in the Pi 5 section of `config.txt`: `camera_auto_detect=0`,
  `dtoverlay=mira220-nir,cam0` and `dtoverlay=mira220-nir`.
- **The wiring:** each camera behind its own TXS0108E, powered by that camera's 1.8 V
  ([`WIRING_DIAGRAM.md`](../../WIRING_DIAGRAM.md) §7.2). The board's one trigger line reaches both.
- **Configure each into its own directory,** choosing it by its I²C bus, the number in
  `mira220 <bus>-0054`:

  ```bash
  bash scripts/configure-cameras.sh --output "$CONF0" --camera mira220 --bus 11 --exposure-us 1000 --gain 1 --apply
  bash scripts/configure-cameras.sh --output "$CONF1" --camera mira220 --bus 10 --exposure-us 1000 --gain 1 --apply
  ```

  Without `--bus`, two Mira220s are refused, with both names listed.
- **Capture both** with one `trigger-capture.sh`: the camera lock is per user, so two cannot
  run side by side.

  ```bash
  bash scripts/trigger-capture.sh --config "$CONF0" --config "$CONF1" --output "$CONF0/trig2" --frames 3
  ```

  - It streams both, switches each to slave once its first frame arrives, and prints `READY`.
  - It ends when each camera has `--frames` triggered frames.
  - Each camera's files, as listed above, go into `bus<N>/`.
  - `pairs.tsv` and `summary.tsv` on top describe the pair.
- **The pairs** (`conversion/triggered_pairs.py`):
  - A triggered frame from each camera pairs up when each is the other's nearest, within 50 ms.
  - A missed trigger leaves its frame unpaired, and does not shift the rest.
  - `delta_us` is the second camera's device time minus the first's. It shows which frames
    belong together. How far apart the exposures really start is the analyser's job.

## `mira220-trigger.sh`

```bash
bash scripts/mira220-trigger.sh --config "$CONFIG" status     # read; change nothing
bash scripts/mira220-trigger.sh --config "$CONFIG" slave
bash scripts/mira220-trigger.sh --config "$CONFIG" master
bash scripts/mira220-trigger.sh --bus 6 status                # without a configuration
```

- **While a stream runs only.** With `--config`, the bus comes from the configured entity name.
- **`slave` and `master`** read 0x1001 back after the switch, and fail if bits 6 and 0 are not
  as written.
- **`status`** prints 0x1001 interpreted, and 0x1002, 0x1003, 0x10F0, 0x10D7 and 0x10D8 as read.

## Tests

From this directory:

```bash
bash tests/test-trigger.sh            # mira220-trigger.sh against a fake i2ctransfer
bash tests/test-trigger-capture.sh    # trigger-capture.sh end to end, with a fake v4l2-ctl
python3 -m unittest conversion.test_triggered_frames conversion.test_triggered_pairs
```

`test-trigger-capture.sh` covers two cameras too: a fake sensor per bus, and one trigger file
that both see.
