#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/common.sh"

if [[ ${1:-} == --help ]]; then
    echo "Usage: bash inspect-platform.sh [--output NEW_DIRECTORY]"
    echo "Read-only inventory. Missing prerequisites are reported with a nonzero exit."
    exit 0
fi
output=
if (($#)); then
    [[ $# == 2 && $1 == --output ]] || die "Use --help for usage."
    new_directory "$2"
    output=$(cd -- "$2" && pwd)
    exec > >(tee "$output/inspection.txt") 2>&1
fi
missing=0
report() {
    printf '\n### %s\n' "$*"
    if ! "$@"; then
        printf 'UNAVAILABLE/FAILED: %s\n' "$*" >&2
        missing=1
    fi
}
report uname -a
report cat /etc/os-release
report cat /proc/device-tree/model
report free -h
report df -hT
report lsblk -o NAME,SIZE,TYPE,FSTYPE,MOUNTPOINTS,MODEL
report dpkg-query -W 'linux-image*' 'linux-headers*' 'v4l-utils' 'device-tree-compiler'
report modinfo -n mira220
report modinfo -F vermagic mira220
report modinfo -n imx296
report vcgencmd get_throttled
report vcgencmd measure_temp
report v4l2-ctl --version
report v4l2-ctl --list-devices
shopt -s nullglob
media=(/dev/media*)
((${#media[@]})) || { echo "No media devices found."; missing=1; }
for device in "${media[@]}"; do
    report media-ctl -d "$device" -p
done
report bash "$HERE/install-mira220.sh" --check
printf '\nInspection exit status: %s (0 means inventory checks succeeded, not camera validation).\n' "$missing"
exit "$missing"
