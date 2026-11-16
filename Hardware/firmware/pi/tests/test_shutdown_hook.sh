#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
#
# Tests for pitrac-rpi5-on-shutdown.sh (independent review 2026-10-19, F1).
# POSIX sh: runs on the Pi or any Linux, WSL included (dash is Debian's /bin/sh).
# Touches no GPIO and shuts nothing down: the hook reads a fixture mountinfo and
# calls a recording stand-in for pinctrl.
#
#     sh pi/tests/test_shutdown_hook.sh

set -u
HERE=$(cd "$(dirname "$0")" && pwd)
HOOK=$HERE/../pitrac-rpi5-on-shutdown.sh
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
fails=0
runs=0

cat > "$TMP/pinctrl" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$PINCTRL_TRACE"
EOF
chmod +x "$TMP/pinctrl"

# Real Raspberry Pi OS lines, trimmed. The last field is the superblock's options.
PROC='22 29 0:21 / /proc rw,nosuid,nodev,noexec,relatime shared:12 - proc proc rw'
SYS='21 29 0:20 / /sys rw,nosuid,nodev,noexec,relatime shared:2 - sysfs sysfs rw'
DEV='5 29 0:5 / /dev rw,nosuid,relatime shared:8 - devtmpfs udev rw,size=4012324k,mode=755'
RUN='25 29 0:24 / /run rw,nosuid,nodev,noexec,relatime shared:5 - tmpfs tmpfs rw,size=817920k,mode=755'
ROOT_RO='29 1 179:2 / / ro,noatime shared:1 - ext4 /dev/mmcblk0p2 ro'
ROOT_RW='29 1 179:2 / / rw,noatime shared:1 - ext4 /dev/mmcblk0p2 rw'
ROOT_REMOUNT_RO_OPT='29 1 179:2 / / rw,noatime shared:1 - ext4 /dev/mmcblk0p2 rw,errors=remount-ro'
ROOT_MOUNT_RO_SUPER_RW='29 1 179:2 / / ro,noatime shared:1 - ext4 /dev/mmcblk0p2 rw'
ROOT_NO_OPTIONAL='29 1 179:2 / / ro,noatime - ext4 /dev/mmcblk0p2 ro'
BOOT_RO='31 29 179:1 / /boot/firmware ro,relatime shared:3 - vfat /dev/mmcblk0p1 ro,fmask=0022,dmask=0022,errors=remount-ro'
BOOT_RW='31 29 179:1 / /boot/firmware rw,relatime shared:3 - vfat /dev/mmcblk0p1 rw,fmask=0022,dmask=0022,errors=remount-ro'
DATA_RW='40 29 8:1 / /mnt/data rw,relatime shared:20 master:3 - ext4 /dev/sda1 rw'
DATA_RO='40 29 8:1 / /mnt/data ro,relatime shared:20 master:3 - ext4 /dev/sda1 ro'
ZRAM_RW='41 29 254:1 / /var/log rw,relatime shared:21 - ext4 /dev/zram1 rw'
OVL_ROOT='30 1 0:30 / / rw,relatime shared:1 - overlay overlay rw,lowerdir=/media/root-ro,upperdir=/media/root-rw/overlay,workdir=/media/root-rw/overlay-workdir'
OVL_LOWER='32 30 179:2 / /media/root-ro ro,relatime shared:2 - ext4 /dev/mmcblk0p2 ro'
OVL_UPPER='33 30 0:31 / /media/root-rw rw,relatime shared:3 - tmpfs tmpfs-root rw'

fixture() {   # the lines, one per argument, into a fresh mountinfo file
    file=$TMP/mountinfo.$runs
    : > "$file"
    for l in "$@"; do printf '%s\n' "$l" >> "$file"; done
    echo "$file"
}

# expect NAME drop|keep MOUNTINFO
expect() {
    name=$1 want=$2 mi=$3
    runs=$((runs + 1))
    : > "$TMP/trace"
    rm -f "$TMP/kmsg"
    PITRAC_TEST_MOUNTINFO=$mi PITRAC_TEST_PINCTRL=$TMP/pinctrl PITRAC_TEST_KMSG=$TMP/kmsg \
        PINCTRL_TRACE=$TMP/trace sh "$HOOK" poweroff
    if grep -qx 'set 22 op dl' "$TMP/trace"; then got=drop; else got=keep; fi
    if [ "$got" != "$want" ]; then
        echo "FAIL $name: expected $want, got $got"
        fails=$((fails + 1))
        return
    fi
    # A refusal says why, where a serial console can see it.
    if [ "$want" = keep ] && ! grep -q 'left high' "$TMP/kmsg" 2>/dev/null; then
        echo "FAIL $name: kept the line high without logging why"
        fails=$((fails + 1))
        return
    fi
    echo "PASS $name"
}

# --- Read-only (or unmounted) disks: the acknowledgement may go low -----------
expect all_read_only            drop "$(fixture "$PROC" "$SYS" "$DEV" "$RUN" "$ROOT_RO" "$BOOT_RO")"
expect boot_unmounted           drop "$(fixture "$PROC" "$SYS" "$RUN" "$ROOT_RO")"
expect data_mount_read_only     drop "$(fixture "$ROOT_RO" "$BOOT_RO" "$DATA_RO")"
expect no_optional_fields       drop "$(fixture "$ROOT_NO_OPTIONAL")"
expect zram_is_ram              drop "$(fixture "$ROOT_RO" "$ZRAM_RW")"
expect overlay_root_tmpfs_upper drop "$(fixture "$OVL_ROOT" "$OVL_LOWER" "$OVL_UPPER")"
expect pseudo_fs_writable       drop "$(fixture "$PROC" "$SYS" "$DEV" "$RUN" "$ROOT_RO")"

# --- A disk still writable: the line stays high ------------------------------
expect root_writable            keep "$(fixture "$PROC" "$ROOT_RW" "$BOOT_RO")"
expect boot_writable            keep "$(fixture "$ROOT_RO" "$BOOT_RW")"
expect data_mount_writable      keep "$(fixture "$ROOT_RO" "$BOOT_RO" "$DATA_RW")"
expect errors_remount_ro_is_rw  keep "$(fixture "$ROOT_REMOUNT_RO_OPT")"
expect superblock_still_rw      keep "$(fixture "$ROOT_MOUNT_RO_SUPER_RW")"
expect writable_after_ro_lines  keep "$(fixture "$ROOT_RO" "$BOOT_RO" "$PROC" "$DATA_RW")"

# --- The check itself fails: the line stays high ------------------------------
expect mountinfo_missing        keep "$TMP/does-not-exist"
expect mountinfo_empty          keep "$(fixture)"
expect no_root_listed           keep "$(fixture "$PROC" "$SYS" "$BOOT_RO")"
expect malformed_short_line     keep "$(fixture "$ROOT_RO" "29 1 179:2 / /")"
expect malformed_no_separator   keep "$(fixture "$ROOT_RO" "40 29 8:1 / /mnt/data rw,relatime ext4 /dev/sda1 rw x y")"
expect malformed_extra_fields   keep "$(fixture "$ROOT_RO" "40 29 8:1 / /mnt/data rw - ext4 /dev/sda1 ro extra")"

# --- `check` reports and never touches GPIO22 --------------------------------
runs=$((runs + 1))
: > "$TMP/trace"
out=$(PITRAC_TEST_MOUNTINFO="$(fixture "$ROOT_RO")" PITRAC_TEST_PINCTRL=$TMP/pinctrl \
      PITRAC_TEST_KMSG=$TMP/kmsg PINCTRL_TRACE=$TMP/trace sh "$HOOK" check)
if [ -s "$TMP/trace" ] || [ "${out#*would drop}" = "$out" ]; then
    echo "FAIL check_safe: output '$out', pinctrl calls: $(cat "$TMP/trace")"
    fails=$((fails + 1))
else
    echo "PASS check_safe"
fi
runs=$((runs + 1))
: > "$TMP/trace"
out=$(PITRAC_TEST_MOUNTINFO="$(fixture "$ROOT_RW")" PITRAC_TEST_PINCTRL=$TMP/pinctrl \
      PITRAC_TEST_KMSG=$TMP/kmsg PINCTRL_TRACE=$TMP/trace sh "$HOOK" check)
if [ -s "$TMP/trace" ] || [ "${out#*NOT safe}" = "$out" ]; then
    echo "FAIL check_unsafe: output '$out', pinctrl calls: $(cat "$TMP/trace")"
    fails=$((fails + 1))
else
    echo "PASS check_unsafe"
fi

echo "$runs cases, $fails failed"
[ "$fails" -eq 0 ]
