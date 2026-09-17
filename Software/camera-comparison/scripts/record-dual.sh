#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/common.sh"
config= output= seconds=10 sink=0 storage=
while (($#)); do
    case $1 in
        --help)
            echo "Usage: bash record-dual.sh --config CONFIG_DIR --output NEW_RUN_DIR [--seconds 10]"
            echo "       --sink | --storage-report PATH_TO_STORAGE_TSV"
            echo "Records all configured cameras. Seconds must be 1..60."
            echo "Retains one extra second per camera so overlap can cover the requested duration."
            exit 0 ;;
        --config|--output|--seconds|--storage-report)
            (($# >= 2)) || die "Missing value for $1"
            case $1 in
                --config) config=$2 ;; --output) output=$2 ;;
                --seconds) seconds=$2 ;; --storage-report) storage=$2 ;;
            esac
            shift 2 ;;
        --sink) sink=1; shift ;;
        *) die "Unknown argument: $1" ;;
    esac
done
[[ -d $config && -n $output ]] || die "--config and --output are required."
positive_integer "$seconds" && ((seconds <= 60)) || die "--seconds must be 1..60."
[[ $(< "$config/status") == CONFIGURED ]] || die "Configuration is incomplete."
if ((sink)); then
    [[ -z $storage ]] || die "--sink and --storage-report are mutually exclusive."
else
    [[ -f $storage ]] || die "Disk recording requires --storage-report from benchmark-storage.sh."
fi
need v4l2-ctl media-ctl awk flock fuser timeout sync stat df sha256sum
require_pi
lock_cameras
new_directory "$output"
output=$(cd -- "$output" && pwd)
printf 'INVALID: recording not yet verified\n' > "$output/status"
declare -a pids=() sensors=()
declare -A nodes=() counts=()
cleanup() {
    local rc=$? pid
    trap - EXIT INT TERM
    for pid in "${pids[@]}"; do
        # GNU timeout forwards TERM to its managed command/process group.
        if kill -0 "$pid" 2>/dev/null; then kill -TERM "$pid" 2>/dev/null || true; fi
    done
    for pid in "${pids[@]}"; do wait "$pid" 2>/dev/null || true; done
    if ((rc)); then
        printf 'INVALID: recorder exited %s; retain logs and partial files for diagnosis\n' "$rc" > "$output/status"
        echo "Recording failed. Diagnostics: $output" >&2
    fi
    exit "$rc"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
total_bytes=0
rate=0
buffers=0
for sensor in mira220 imx296; do
    [[ -f $config/$sensor.tsv ]] || continue
    load_camera "$config/$sensor.tsv"
    assert_camera_live
    graph=$(media-ctl -d "${CAM[media]}" -p)
    idle_graph "$graph"
    sensors+=("$sensor")
    nodes[$sensor]=${CAM[video]}
    counts[$sensor]=$(awk -v fps="${CAM[fps]}" -v duration="$seconds" \
        'BEGIN {n=fps*(duration+1); printf "%d", int(n)+(n>int(n))+1}')
    ((total_bytes+=counts[$sensor] * CAM[sizeimage]))
    ((buffers+=8 * CAM[sizeimage]))
    rate=$(awk -v total="$rate" -v fps="${CAM[fps]}" -v size="${CAM[sizeimage]}" \
        'BEGIN {printf "%.6f", total+fps*size}')
    cp -- "$config/$sensor.tsv" "$output/$sensor.tsv"
    printf '%s\n' "$graph" > "$output/$sensor.topology.txt"
    v4l2-ctl -d "${CAM[subdev]}" --list-ctrls > "$output/$sensor.controls-before.txt"
done
((${#sensors[@]})) || die "No supported camera configurations found."
memory=$(awk '/^MemAvailable:/ {printf "%.0f", $2*1024}' /proc/meminfo)
((memory >= buffers * 3 + 134217728)) || die "Insufficient available RAM for bounded capture buffers."
if ((!sink)); then
    available=$(df -B1 --output=avail "$output" | awk 'NR==2 {print $1}')
    ((available >= total_bytes * 5 / 4 + 67108864)) || die "Insufficient disk space including 25% headroom."
    validate_storage_report "$storage" "$(< /proc/sys/kernel/random/boot_id)" \
        "$(uname -r)" "$(stat -c %d "$output")" "$total_bytes" "$rate"
    cp -- "$storage" "$output/storage.tsv"
fi
{
    kv schema 1; kv seconds "$seconds"; kv warmup 5; kv sink "$sink"
    kv cameras "${sensors[*]}"; kv kernel "$(uname -r)"
    kv boot_id "$(< /proc/sys/kernel/random/boot_id)"
    kv filesystem_device "$(stat -c %d "$output")"
    kv expected_total_bytes "$total_bytes"; kv payload_bytes_per_second "$rate"
    kv v4l2_version "$(v4l2-ctl --version)"
    for sensor in "${sensors[@]}"; do kv "$sensor.frames" "${counts[$sensor]}"; done
} > "$output/run.tsv"
v4l2-ctl --list-devices > "$output/devices.txt"
hash_files=(run.tsv)
((sink)) || hash_files+=(storage.tsv)
for sensor in "${sensors[@]}"; do
    modinfo "$sensor" > "$output/$sensor-module.txt"
    hash_files+=("$sensor.tsv")
done
(cd -- "$output" && sha256sum -- "${hash_files[@]}") > "$output/manifest.sha256"
start=$(date +%s%N)
for sensor in "${sensors[@]}"; do
    destination="$output/$sensor.raw"
    ((sink)) && destination=/dev/null
    timeout --signal=TERM --kill-after=5s "$((seconds + 15))s" \
        v4l2-ctl -d "${nodes[$sensor]}" --verbose --stream-no-query \
        --stream-mmap=8 --stream-poll --stream-skip=5 \
        "--stream-count=${counts[$sensor]}" "--stream-to=$destination" \
        > "$output/$sensor.capture.log" 2>&1 &
    pids+=("$!")
done
while ((${#pids[@]})); do
    active=()
    for pid in "${pids[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            active+=("$pid")
        else
            if wait "$pid"; then :; else
                rc=$?
                die "Capture process $pid failed (exit $rc); inspect per-camera logs."
            fi
        fi
    done
    pids=("${active[@]}")
    ((${#pids[@]} == 0)) || sleep 0.2
done
for sensor in "${sensors[@]}"; do
    load_camera "$output/$sensor.tsv"
    assert_camera_live
    v4l2-ctl -d "${CAM[subdev]}" --list-ctrls > "$output/$sensor.controls-after.txt"
done
if ((!sink)); then timeout --kill-after=5s 30s sync -f "$output"; fi
end=$(date +%s%N)
{
    kv elapsed_including_flush_seconds "$(awk -v start="$start" -v end="$end" 'BEGIN {printf "%.6f", (end-start)/1e9}')"
    kv durable "$((1-sink))"
    kv controls_readback_passed 1
} > "$output/completion.tsv"
bash "$HERE/verify-recording.sh" --run "$output"
echo "Completed: $output"
