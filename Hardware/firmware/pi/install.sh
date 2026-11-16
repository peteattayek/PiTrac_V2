#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
#
# Install the Pi-side pieces of the PiTrac power contract. Run ON the Pi 5:
#     sudo ./install.sh
# README.md in this folder says what each piece is for and how to check it.
#
#   1. pitrac-rpi5-on.service   raises RPI5_ON (GPIO22) once userspace is up
#   2. pitrac-rpi5-on.shutdown  drops it after the filesystems are read-only
#   3. persistent journald      the Pi's record of its own halts (8.6)
#   4. pitrac-link              the serial link to the board, and firmware updates
#
# It does NOT edit /boot/firmware/config.txt: it checks for the gpio-shutdown
# and uart0 lines and prints any that are missing.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ $EUID -ne 0 ]]; then
    echo "run with sudo" >&2
    exit 1
fi
if ! command -v pinctrl >/dev/null 2>&1; then
    echo "pinctrl not found -- it ships with Raspberry Pi OS Bookworm (raspi-utils)" >&2
    exit 1
fi

# Copy without carriage returns: a Windows checkout may have added them, and
# systemd would read them as part of the values.
install_lf() {   # source destination mode
    tr -d '\r' < "$1" > "$2"
    chmod "$3" "$2"
}

install_lf "$HERE/pitrac-rpi5-on.service" /etc/systemd/system/pitrac-rpi5-on.service 0644
install -d /usr/lib/systemd/system-shutdown
install_lf "$HERE/pitrac-rpi5-on-shutdown.sh" \
           /usr/lib/systemd/system-shutdown/pitrac-rpi5-on.shutdown 0755
install -d /etc/systemd/journald.conf.d
install_lf "$HERE/90-pitrac-persistent.conf" \
           /etc/systemd/journald.conf.d/90-pitrac-persistent.conf 0644
install_lf "$HERE/pitrac_link.py" /usr/local/bin/pitrac-link 0755

mkdir -p /var/log/journal
systemd-tmpfiles --create --prefix /var/log/journal
systemctl restart systemd-journald

systemctl daemon-reload
systemctl enable pitrac-rpi5-on.service
echo "installed: RPI5_ON rises at the next boot."
echo "  (now: sudo systemctl start pitrac-rpi5-on; check: pinctrl get 22)"

# The shutdown hook's verdict on this running system. "NOT safe" is the right
# answer here (the root is writable while the Pi runs): it shows the hook can read
# this image's mount table. `check` never touches GPIO22.
echo "shutdown hook self-check:"
sh /usr/lib/systemd/system-shutdown/pitrac-rpi5-on.shutdown check || true

CFG=/boot/firmware/config.txt
for LINE in 'dtoverlay=gpio-shutdown,gpio_pin=26,active_low=1,gpio_pull=up' \
            'dtparam=uart0=on'; do
    if grep -qxF "$LINE" "$CFG" 2>/dev/null; then
        echo "config.txt: present: $LINE"
    else
        echo "config.txt: ADD this line to $CFG, then reboot (README.md):"
        echo "    $LINE"
    fi
done
