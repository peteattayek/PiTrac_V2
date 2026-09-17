#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/common.sh"
output= gib=24
while (($#)); do
    case $1 in
        --help)
            echo "Usage: bash benchmark-storage.sh --output NEW_DIR_ON_NVME [--gib 24]"
            echo "Writes and flushes a new random-data test file with fio direct I/O."
            echo "Removes only its own completed test file; keeps logs and storage.tsv."
            exit 0 ;;
        --output|--gib)
            (($# >= 2)) || die "Missing value for $1"
            case $1 in --output) output=$2 ;; --gib) gib=$2 ;; esac
            shift 2 ;;
        *) die "Unknown argument: $1" ;;
    esac
done
[[ -n $output ]] || die "--output NEW_DIR_ON_NVME is required."
positive_integer "$gib" && ((gib <= 256)) || die "--gib must be 1..256."
need fio stat df sync date awk
require_pi
new_directory "$output"
output=$(cd -- "$output" && pwd)
bytes=$((gib * 1024 * 1024 * 1024))
available=$(df -B1 --output=avail "$output" | awk 'NR==2 {print $1}')
((available >= bytes + 1073741824)) || die "Not enough space for the benchmark plus 1 GiB reserve."
printf 'INCOMPLETE\n' > "$output/status"
echo "Writing $gib GiB to $output/write-test.bin; no block device will be overwritten."
start=$(date +%s%N)
(cd -- "$output" && fio --name=pitrac-camera-storage --filename=write-test.bin --size="$bytes" \
    --rw=write --bs=1M --ioengine=psync --direct=1 --end_fsync=1 \
    --refill_buffers=1 --scramble_buffers=1 --randrepeat=0 --group_reporting \
    --output=fio.txt)
[[ $(stat -c %s "$output/write-test.bin") == "$bytes" ]] || die "Benchmark file size mismatch."
sync -f "$output/write-test.bin"
end=$(date +%s%N)
elapsed=$(awk -v start="$start" -v end="$end" 'BEGIN {printf "%.9f", (end-start)/1e9}')
mbps=$(awk -v bytes="$bytes" -v elapsed="$elapsed" 'BEGIN {if (elapsed<=0) exit 1; printf "%.6f", bytes/elapsed/1e6}')
{
    kv schema 1; kv bytes "$bytes"; kv elapsed_seconds "$elapsed"; kv mbps "$mbps"
    kv filesystem_device "$(stat -c %d "$output")"
    kv boot_id "$(< /proc/sys/kernel/random/boot_id)"
    kv kernel "$(uname -r)"; kv method fio-direct-random-end-fsync
} > "$output/storage.tsv"
rm -- "$output/write-test.bin"
printf 'COMPLETE\n' > "$output/status"
echo "Measured $mbps MB/s including allocation and flush. Report: $output/storage.tsv"
echo "Repeat after SSD cache/temperature stabilize; this is not a camera capture test."
