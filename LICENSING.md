# Licensing — PiTrac V2

This repository contains two kinds of work under two different licenses. Both are
**strongly reciprocal**: modify and distribute, and your changes must be published
under the same terms. There is deliberately no path here to a closed fork.

| What | License | Full text |
|---|---|---|
| **Board design** — schematics, PCB, footprints, netlist, BOM, gerbers, design doc | **CERN-OHL-S v2** | `LICENSES/CERN-OHL-S-2.0.txt` |
| **Firmware** — C sources, headers, PIO, CMake, host tools | **GPL-3.0-or-later** | `LICENSE` (repo root) |

---

## Hardware — CERN-OHL-S v2

**Covers** everything under `Hardware/The_Second_Board_To_Rule_Them_All/` — schematics,
PCB layout, symbol and footprint libraries, netlists, the BOM, and the gerbers in
`jlcpcb/production_files/` — plus the design document
`Hardware/The_Second_Board_To_Rule_Them_All.md`.

The notice to reproduce, per §3.1 of the licence:

```
SPDX-License-Identifier: CERN-OHL-S-2.0

Copyright (C) 2026 PiTrac contributors

This source describes Open Hardware and is licensed under the CERN-OHL-S v2.
You may redistribute and modify this source and make products using it under the
terms of the CERN-OHL-S v2 (https://ohwr.org/cern_ohl_s_v2.txt).

This source is distributed WITHOUT ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING OF
MERCHANTABILITY, SATISFACTORY QUALITY AND FITNESS FOR A PARTICULAR PURPOSE.
Please see the CERN-OHL-S v2 for applicable conditions.

Source location: https://github.com/<your-org>/PiTrac_V2
```

**What strongly reciprocal means in practice:** if you make and distribute a board
based on this design — including selling it — you must publish the complete design
sources for *your* version under CERN-OHL-S v2 and tell recipients where to get them.
Building boards for your own use triggers nothing at all.

> **Fill in the `Source location:` URL** once the repo is public. The licence requires
> it to point somewhere the sources can actually be obtained.

---

## Firmware — GPL-3.0-or-later

**Covers** everything under `Hardware/firmware/`: C sources, headers, PIO programs,
CMake files, and the host tools in `firmware/tools/`. Also the bring-up and
architecture documentation (`START_HERE.md`, `BENCH*.md`, `PROGRESS.md`,
`ARCHITECTURE.md`, `SETUP.md`), which is covered along with the code it describes.

Every source file carries an SPDX header:

```c
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
```

---

## Why this pairing

CERN-OHL-S is the hardware analogue of the GPL — the reciprocity obligation
propagates to anyone distributing derived boards. Pairing it with GPL-3.0 firmware
keeps the two halves of the project under matched terms, so a downstream user cannot
open one and close the other.

The practical consequence worth understanding: **someone can absolutely build and
sell these boards.** What they cannot do is keep their improvements private. That is
the intended outcome for a community instrument.

---

## Third-party components

The firmware builds against the **Raspberry Pi Pico SDK** (BSD-3-Clause), which is
*not* redistributed here — CMake fetches it at build time. BSD-3-Clause is compatible
with GPL-3.0 and imposes no additional obligations on this project's sources.

Manufacturer symbols and footprints under `Library.pretty/` and
`RP2350_80QFN_minimal.pretty/` may carry terms from their originating libraries.
Check those before assuming CERN-OHL-S covers them.

---

## Contributing

Contributions are accepted under these same licences. Opening a pull request means
agreeing your work may be distributed under CERN-OHL-S v2 (hardware) or
GPL-3.0-or-later (firmware), as appropriate to what you changed.

**Relicensing later requires the consent of every copyright holder.** Settling this
now, with one contributor, is enormously easier than after the project has several.
