#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
#
# Installed as /usr/lib/systemd/system-shutdown/pitrac-rpi5-on.shutdown by
# install.sh. systemd-shutdown runs every executable there at the very end of a
# halt, poweroff or reboot ($1).
#
# Dropping RPI5_ON (GPIO22 -> J8.15) tells the RP2354 "the Pi is down". After a
# shutdown the board asked for, it opens the +5 V latch as soon as it sees that
# (once its holdoff, 10 s from firmware 1.3, has passed). So the low is an ACKNOWLEDGEMENT that the
# storage is safe to lose power, and it is given only once that has been CHECKED:
#
#   - Reaching this hook proves nothing. systemd-shutdown runs these hooks even
#     when it gave up unmounting or remounting a filesystem read-only, and it
#     syncs once more after them (systemd src/shutdown/shutdown.c; independent
#     review 2026-10-19, F1).
#   - So every filesystem on a block device -- a /dev/... source in
#     /proc/self/mountinfo, apart from RAM-backed /dev/zram* -- must be
#     read-only at the superblock, and the root must be listed.
#   - Anything else leaves the line HIGH: a writable data mount, a failed
#     remount, a mountinfo that cannot be read or does not parse. The board then
#     waits for its 60 s timeout, and counts a PI_SHUTDOWN_TIMEOUT, not a clean
#     shutdown -- or for the Pi's 3V3 to fall.
#
# `sh pitrac-rpi5-on.shutdown check` prints the verdict and never touches GPIO22.
# The PITRAC_TEST_* variables exist for pi/tests/test_shutdown_hook.sh only.

MOUNTINFO=${PITRAC_TEST_MOUNTINFO:-/proc/self/mountinfo}
PINCTRL=${PITRAC_TEST_PINCTRL:-/usr/bin/pinctrl}
KMSG=${PITRAC_TEST_KMSG:-/dev/kmsg}

# Succeeds when the storage is safe to lose power. Otherwise prints the first
# reason it is not, and fails.
storage_safe() {
    if [ ! -r "$MOUNTINFO" ]; then
        echo "cannot read $MOUNTINFO"
        return 1
    fi
    root_seen=0
    while IFS= read -r line; do
        # ID PARENT MAJ:MIN ROOT MOUNTPOINT OPTIONS [OPTIONAL...] - FSTYPE SOURCE SUPER_OPTIONS
        set -f
        # shellcheck disable=SC2086
        set -- $line
        set +f
        if [ $# -lt 10 ]; then
            echo "malformed mountinfo line: $line"
            return 1
        fi
        mountpoint=$5
        shift 6
        while [ $# -gt 0 ] && [ "$1" != "-" ]; do shift; done
        if [ $# -ne 4 ]; then
            echo "malformed mountinfo line: $line"
            return 1
        fi
        source=$3
        superopts=$4
        if [ "$mountpoint" = "/" ]; then root_seen=1; fi
        case $source in
            /dev/zram*) continue ;;      # RAM: nothing persists anyway
            /dev/*) ;;                   # a disk: must be read-only
            *) continue ;;               # proc, sysfs, tmpfs, overlay...: no disk of its own
        esac
        case ",$superopts," in
            *,ro,*) ;;
            *)
                echo "$mountpoint ($source) is still writable"
                return 1
                ;;
        esac
    done < "$MOUNTINFO"
    if [ "$root_seen" -ne 1 ]; then
        echo "no root filesystem in $MOUNTINFO"
        return 1
    fi
    return 0
}

if [ "${1:-}" = check ]; then
    if why=$(storage_safe); then
        echo "pitrac: every disk filesystem is read-only: would drop RPI5_ON (GPIO22)"
    else
        echo "pitrac: NOT safe -- $why: would leave RPI5_ON (GPIO22) high"
    fi
    exit 0
fi

if why=$(storage_safe); then
    "$PINCTRL" set 22 op dl
else
    # The journal is gone by now; the kernel log still reaches a serial console.
    { echo "pitrac: $why: RPI5_ON (GPIO22) left high" > "$KMSG"; } 2>/dev/null || true
fi
