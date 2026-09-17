<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 PiTrac contributors -->

# Raspberry Pi-side software

This directory contains software and setup guides that run on the Raspberry Pi.
It is separate from the existing board designs and microcontroller firmware in
[Hardware](../Hardware/); those files have not moved.

## Camera comparison

Start with the [tested quick start](camera-comparison/QUICKSTART.md) or the
[full setup guide](camera-comparison/README.md) for a
standalone, free-running NIR comparison between:

- ams OSRAM Mira220 EVM-SE, monochrome, on CAM/DISP1;
- INNO-MAKER CAM-MIPI296RAW-TRIGGER, monochrome IMX296, on CAM/DISP0.

The guide preserves an existing Raspberry Pi OS Trixie installation and begins
with inspection of the actual running kernel. It covers active cooling, power,
PCIe Gen 2 NVMe storage, a source-pinned Mira220 driver, reversible manual boot
configuration, direct V4L2 raw capture, and evidence-based verification.

**A 60-second simultaneous disk recording has passed on the inspected Pi 5**
with kernel 6.18.50, Mira220 RAW8 at approximately 89.080 fps and IMX296 packed
RAW10 at approximately 60.375 fps. This required 32 capture buffers per camera,
128 MiB asynchronous writers, and an explicitly approved 20% minimum storage
headroom (measured 24.32%, below the default 25%). See the guides for the exact
conditions, desktop-camera contention, and retained evidence. This does not
establish compatibility with every kernel or module revision.

## License

New code and documentation under this directory are **GPL-3.0-or-later**; see
the repository's [GPL license text](../LICENSE). Third-party drivers and tools
retain their own licenses. This does not change the licensing of existing
hardware or firmware.
