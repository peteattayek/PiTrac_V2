#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
#
# mira220-trigger.sh -- switch a STREAMING Mira220 between free-running (master) and
# REQ EXP-triggered (slave) exposure, or show its trigger registers. For PiTrac V2
# Hardware/firmware/BENCH_P5_P7_MIC_CAMERA.md 7c.1.
#
# Only while a stream runs: the ams driver (the commit install-mira220.sh pins) writes
# master control at every stream start and powers the sensor down 1 s after the stream
# stops (runtime-PM autosuspend). Powered down, it does not answer on I2C, and the next
# start undoes a switch. trigger-capture.sh calls this after its stream has started.
#
# The sequence is the one upstream PiTrac uses (PiTracLM/PiTrac, branch feature/pico,
# CameraTools/mira220_trigger, commit afe689e), checked against the driver's own
# register use. This file is written for this repository; it is not a copy.
#   0x1003 IMAGER_STATE  0x02 stop at a row boundary, 0x08 slave, 0x10 master
#   0x10F0 IMAGER_RUN    0x00 stop, 0x01 start
#   0x1001 bit 6 EXT_EVENT_SEL, bit 0 EXT_EXP_PW_SEL; the driver writes 0x41 (both set)
# Both clear: the sensor exposes while REQ EXP is high, and the fall starts readout.
# The illumination output (0x10D7/0x10D8) is left as the driver set it.
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/common.sh"

usage() {
    echo "Usage: bash mira220-trigger.sh --config CONFIG_DIR status|slave|master"
    echo "       bash mira220-trigger.sh --bus N status|slave|master"
    echo "Run while the Mira220 streams (the driver powers it down 1 s after a stream stops)."
    echo "  status  read the trigger registers; change nothing"
    echo "  slave   expose while REQ EXP is high; its fall starts readout (no frames without it)"
    echo "  master  free-running again, as the driver starts it"
    echo "The bus comes from the configured entity ('mira220 <bus>-0054'). Needs i2c-tools and"
    echo "access to /dev/i2c-<bus> (the i2c group; 'sudo modprobe i2c-dev' if it is missing)."
}

config= bus= action=
while (($#)); do
    case $1 in
        --help) usage; exit 0 ;;
        --config|--bus)
            (($# >= 2)) || die "Missing value for $1"
            case $1 in --config) config=$2 ;; --bus) bus=$2 ;; esac
            shift 2 ;;
        status|slave|master)
            [[ -z $action ]] || die "Give one action only."
            action=$1; shift ;;
        *) die "Unknown argument: $1 (--help for usage)" ;;
    esac
done
[[ -n $action ]] || die "Give an action: status, slave or master (--help for usage)."
[[ -n $config && -z $bus || -z $config && -n $bus ]] || die "Give exactly one of --config CONFIG_DIR or --bus N."
if [[ -n $config ]]; then
    [[ -f $config/mira220.tsv ]] ||
        die "No mira220.tsv in $config: run configure-cameras.sh with --camera mira220 or both first."
    entity=$(value "$config/mira220.tsv" entity) || die "No single entity in $config/mira220.tsv."
    [[ $entity =~ ^mira220\ ([0-9]{1,3})-0054$ ]] ||
        die "Unexpected Mira220 entity '$entity'; expected 'mira220 <bus>-0054'."
    bus=${BASH_REMATCH[1]}
fi
[[ $bus =~ ^[0-9]{1,3}$ ]] || die "The bus must be a number, as in 'mira220 6-0054'."
need i2ctransfer
readonly ADDR=0x54

hex8() { printf '0x%02x' "$1"; }
stamp() { date +%H:%M:%S.%3N; }

# read_reg 0x1001 -> 0xNN on stdout; nonzero if the sensor does not answer sanely.
read_reg() {
    local out
    out=$(i2ctransfer -f -y "$bus" "w2@$ADDR" "$(hex8 $((($1 >> 8) & 0xff)))" "$(hex8 $(($1 & 0xff)))" r1) ||
        return 1
    [[ $out =~ ^0x[0-9a-fA-F]{2}$ ]] || return 1
    hex8 "$((out))"
}
write_reg() {
    i2ctransfer -f -y "$bus" "w3@$ADDR" "$(hex8 $((($1 >> 8) & 0xff)))" "$(hex8 $(($1 & 0xff)))" "$(hex8 "$2")"
}
no_answer() {
    die "No answer from the Mira220 at bus $bus, address $ADDR. Is its stream running? The driver" \
        "powers the sensor down 1 s after a stream stops. Also check i2c-tools, /dev/i2c-$bus" \
        "('sudo modprobe i2c-dev') and access (the i2c group, or sudo)."
}
# Bits 6 and 0 of 0x1001, in words.
bits_text() {
    case $(($1 & 0x41)) in
        65) echo "bits 6 and 0 set: the sensor's own exposure and frame timing (driver default)" ;;
        0)  echo "bits 6 and 0 clear: REQ EXP sets the exposure, its fall starts readout" ;;
        *)  echo "only one of bits 6 and 0 set: neither mode this tool writes" ;;
    esac
}

before=$(read_reg 0x1001) || no_answer

if [[ $action == status ]]; then
    echo "Mira220 on bus $bus, address $ADDR, $(stamp)"
    echo "  0x1001 $before  $(bits_text "$before")"
    for reg in 0x1002 0x1003 0x10f0 0x10d7 0x10d8; do
        v=$(read_reg "$reg") || no_answer
        echo "  $reg $v"
    done
    echo "  (0x1002 run mode, 0x1003 imager state, 0x10F0 run, 0x10D7/0x10D8 illumination output:"
    echo "   shown as read; this tool interprets only 0x1001)"
    exit 0
fi

if [[ $action == slave ]]; then
    target=$((before & ~0x41)) state=0x08 want=0
else
    target=$((before | 0x41)) state=0x10 want=65
fi
stopped=0
failed() {
    if ((stopped)); then
        die "$1. The imager may be stopped: stop the stream and start it again (the driver restarts it as master)."
    fi
    die "$1. Nothing was changed."
}
echo "$(stamp) stop at a row boundary, then switch to $action"
write_reg 0x1003 0x02 || failed "Writing 0x1003 failed"
stopped=1
write_reg 0x10f0 0x00 || failed "Writing 0x10F0 failed"
sleep 0.04
write_reg 0x1001 "$target" || failed "Writing 0x1001 failed"
write_reg 0x1003 "$state" || failed "Writing 0x1003 failed"
write_reg 0x10f0 0x01 || failed "Writing 0x10F0 failed"
after=$(read_reg 0x1001) || failed "0x1001 could not be read back"
(((after & 0x41) == want)) || failed "0x1001 reads $after after writing $(hex8 "$target")"
echo "$(stamp) $action: 0x1001 $before -> $after, 0x1003 $(hex8 "$state"), running."
echo "  $(bits_text "$after")"
if [[ $action == slave ]]; then
    echo "  No frames now until REQ EXP goes high and falls (the board's 'cam trig' or 'cam shot')."
fi
