#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
# Flash the RP2354 over SWD from a Raspberry Pi 5 using flying leads.
# Run this ON the Pi. See openocd_pi5.cfg for the wiring and the Pi 5 gotchas.
#
#   ./flash_swd.sh [path/to/pitrac.elf]

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
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
