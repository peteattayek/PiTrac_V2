#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
# Flash the RP2354 over SWD from a Raspberry Pi 5 using flying leads.
# Run this ON the Pi. See openocd_pi5.cfg for the wiring and the Pi 5 gotchas.
#
#   ./flash_swd.sh --flat-board-no-tbyb [path/to/pitrac.elf]
#
# SINCE 2026-10-20 (image 1.1) THIS IS NOT THE WAY TO FLASH, and it refuses
# unless told otherwise:
#   - SWD writes the ELF at flash offset 0. On a board with the A/B partition
#     table (BENCH_P8_PI.md 8.7) that overwrites the table and partition A's
#     start, and the board then boots nothing until it is reloaded over USB.
#   - The default build is flagged try-before-you-buy (CMakeLists.txt,
#     PITRAC_TBYB). The boot ROM launches such an image only on a FLASH_UPDATE
#     boot; after this script's plain reset it launches nothing, and the board
#     sits in BOOTSEL.
# So SWD is only for an image built with -DPITRAC_TBYB=OFF, on a board WITHOUT
# the partition table. Flash over USB (BOOTSEL, drag the UF2) or from the Pi
# (pitrac-link update) instead.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ "${1:-}" != "--flat-board-no-tbyb" ]]; then
    echo "REFUSED: read the header of $0 first." >&2
    echo "  SWD destroys the A/B partition table, and a try-before-you-buy image" >&2
    echo "  (the default build) never starts after an SWD reset." >&2
    echo "  Only for a -DPITRAC_TBYB=OFF build on a board without the table:" >&2
    echo "      $0 --flat-board-no-tbyb [path/to/pitrac.elf]" >&2
    exit 1
fi
shift
ELF="${1:-$HERE/../build/pitrac.elf}"

if [[ ! -f "$ELF" ]]; then
    echo "no ELF at: $ELF" >&2
    echo "build first:  cmake -B build -G Ninja && cmake --build build" >&2
    exit 1
fi

# The gpiochip number moved between Pi kernels; help the user find it early
# rather than failing inside OpenOCD with something cryptic.
if command -v gpioinfo >/dev/null 2>&1; then
    CHIP=$(gpioinfo 2>/dev/null | awk '/^gpiochip/{c=$1} /GPIO24/{print c; exit}' | tr -d ':')
    if [[ -n "${CHIP:-}" ]]; then
        NUM="${CHIP#gpiochip}"
        echo "GPIO24 lives on $CHIP -- openocd_pi5.cfg must say: adapter gpio chip $NUM"
    fi
fi

echo "flashing $ELF ..."
openocd -f "$HERE/openocd_pi5.cfg" \
        -c "program \"$ELF\" verify reset exit"

echo "done. The CLI should enumerate over USB-C in a second or two."
