<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 PiTrac contributors -->

# Raspberry Pi-side software

This directory contains software and setup guides that run on the Raspberry Pi.
It is separate from the existing board designs and microcontroller firmware in
[Hardware](../Hardware/); those files have not moved.

## Camera comparison

Start with the [Pi 5 dual-camera setup guide](camera-comparison/README.md) for a
standalone, free-running NIR comparison between:

- ams OSRAM Mira220 EVM-SE, monochrome, on CAM/DISP0;
- INNO-MAKER CAM-MIPI296RAW-TRIGGER, monochrome IMX296, on CAM/DISP1.

The guide preserves an existing Raspberry Pi OS Trixie installation and begins
with inspection of the actual running kernel. It covers active cooling, power,
PCIe Gen 2 NVMe storage, a source-pinned Mira220 driver, reversible manual boot
configuration, direct V4L2 raw capture, and evidence-based verification.

**Hardware validation is pending.** No driver build or camera capture on the
target Pi has been verified here. Trixie alone is not a compatibility guarantee;
the guide includes mandatory stop points and an unresolved upstream Pi 5
streaming report. Do not reflash or upgrade the kernel just to follow it.

## License

New code and documentation under this directory are **GPL-3.0-or-later**; see
the repository's [GPL license text](../LICENSE). Third-party drivers and tools
retain their own licenses. This does not change the licensing of existing
hardware or firmware.
