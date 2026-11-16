# Pi-side files for the board (Phase 8)

These go on the Raspberry Pi 5 that sits on J8. They are the Pi's half of the power contract
that the RP2354 firmware already implements, and the Pi's end of the serial link to the
RP2354. Written 2026-10-19 against Bookworm. **Installed on a new Pi 5, off the board, on
2026-10-26** (Raspberry Pi OS Lite, Trixie, kernel 6.18.50): every check in "Check it before
seating the Pi" passed. Not yet run on the board: [`BENCH_P8_PI.md`](../BENCH_P8_PI.md) is where
that happens.

| File | Installed as | What it does |
|---|---|---|
| `pitrac-rpi5-on.service` | `/etc/systemd/system/` | Raises **RPI5_ON** (Pi GPIO22, J8.15) once userspace is up |
| `pitrac-rpi5-on-shutdown.sh` | `/usr/lib/systemd/system-shutdown/pitrac-rpi5-on.shutdown` | At the very end of a shutdown or reboot, drops RPI5_ON **only if** every disk filesystem is read-only |
| `tests/test_shutdown_hook.sh` | — (not installed) | Tests the hook against sample mount tables; any Linux, no GPIO |
| `90-pitrac-persistent.conf` | `/etc/systemd/journald.conf.d/` | Keeps the journal across power cycles (8.6) |
| `pitrac_link.py` | `/usr/local/bin/pitrac-link` | The serial link to the RP2354: status, and **firmware updates** (below) |
| `tests/test_pitrac_link.py` | — (not installed) | Tests the link tool against a simulated board; any machine with Python 3 |
| `install.sh` | — | Installs the four, enables the unit, and checks `config.txt` |

Two lines in `/boot/firmware/config.txt` are also needed, and `install.sh` only checks for them:

```
dtoverlay=gpio-shutdown,gpio_pin=26,active_low=1,gpio_pull=up
dtparam=uart0=on
```

The second puts the Pi 5's UART0 on header pins 8 (GPIO14, TXD) and 10 (GPIO15, RXD), as
`/dev/ttyAMA0`. Nothing else may use that port: no serial console (`console=` in
`/boot/firmware/cmdline.txt` must not name `ttyAMA0` or `serial0`), and no getty on it.

## The contract, from the Pi's side

The board side is in [`DEVELOPER_GUIDE.md` §9.2](../../../DEVELOPER_GUIDE.md#92-the-power-contract-from-the-pis-point-of-view).
In short:

1. **Power on.** The board closes its latch and the Pi gets +5 V on J8.2/4, its only supply.
   The Pi's 3V3 on J8.1 must appear within 3 s (`PI_DETECT_WINDOW_MS`).
2. **Boot.** GPIO22 must go high within 90 s, or the board raises `PI_BOOT_TIMEOUT` and stops
   in FAULT with the rail still up. `pitrac-rpi5-on.service` does this after
   `multi-user.target`, when `systemd-logind` is already running.
3. **A shutdown the board asks for** (its button, or `off` at its CLI): GPIO26 goes low for
   200 ms. The `gpio-shutdown` overlay turns that into a KEY_POWER press, and `systemd-logind`
   powers the Pi off. The board waits **at least 10 s** (15 s before firmware 1.3), then opens the latch as soon as the
   Pi's 3V3 or GPIO22 reads low, and **at 60 s** whatever happens (`PI_SHUTDOWN_TIMEOUT`).
4. **What GPIO22 going low means: "the storage is safe; cut the power when you like".** So it
   drops only at the very end, and only after a check:
   - `pitrac-rpi5-on.shutdown` runs at the end of the shutdown. But systemd runs these hooks
     even when it could not unmount a filesystem or remount it read-only, and it syncs once
     more after them, so reaching the hook proves nothing (independent review 2026-10-19).
   - The hook therefore reads `/proc/self/mountinfo`, and drops the line only if every
     filesystem on a block device (`/dev/...`, apart from RAM-backed zram) is read-only at the
     superblock and the root is listed.
   - A writable mount, or a mount table it cannot read or parse, leaves the line **high**. The
     board then waits for its 60 s timeout and counts a `PI_SHUTDOWN_TIMEOUT`, not a clean
     shutdown. The reason goes to the kernel log, which a serial console shows:
     `pitrac: ... RPI5_ON (GPIO22) left high`.
   - The same reason is why the unit has no `ExecStop`: dropping the line any earlier could let
     the board cut the power with the SD card still writable.
5. **A halt or reboot the board did not ask for** (`sudo poweroff`, `sudo reboot`, a crash
   that reboots): GPIO22 drops, and the board counts it after 1 s and says so at its CLI. It
   keeps the rail up. After a reboot, the unit raises GPIO22 again and the board counts that as
   well. `hist` on the board shows the counts.
6. **J8.37 (GPIO26).** The board drives it only while the Pi's 3V3 is up, and releases it
   otherwise. The overlay's pull-up holds it high in between.
7. **The serial link (J8.8/J8.10).** The board connects its UART only while it powers this Pi
   **and** the Pi drives GPIO22 high, and releases both pins otherwise: an idle UART holds its
   TX high, which through the 220 Ω on J8.10 would push ~12 mA into an unpowered Pi. So the link
   works from `multi-user.target` until the very end of a shutdown, and not before the RPI5_ON
   unit has run.

## The serial link: `pitrac-link`

921600 baud, 8N1, framed and CRC-checked (the board's `src/link_frame.h`; the requests are in
`src/link_proto.h`). The board answers a fixed list of requests, not its USB CLI: nothing on
the link can drive a pin, fire the strobe or reset the board.

```bash
pitrac-link ping          # board : PiTrac RP2354, protocol 1, version 1.1
pitrac-link info          # version, build stamp, A/B partition, boot type
pitrac-link status        # power state, fault, +5V_IN
pitrac-link hist          # the board's halt telemetry (its `hist`)
pitrac-link upd-status    # a staged update, and how the last one ended
```

No answer means the board is not listening: it must be `RUNNING` this Pi with GPIO22 high.
At the board's USB CLI, `link` shows whether the lines are connected and counts frames and CRC
errors. The board answers between CLI commands, so during a long one (`cal model`, `scan`) the
tool retries and may give up.

## Updating the RP2354's firmware from this Pi

Any RP2354 reset cuts this Pi's power, so the Pi cannot flash the board the usual ways. The
update therefore runs in the board's own firmware, and its one reset comes after the Pi is
off:

```bash
pitrac-link update pitrac.bin        # build/pitrac.bin (or build/pitrac.uf2) from the PC
```

1. The tool checks the file (an RP2350 Arm image, flagged try-before-you-buy), sends it in
   240-byte chunks into the board's RAM (288 KB at most; ~3 s for a 220 KB image), and the
   board checks its SHA-256 and the image's header.
2. It asks before going on (`--yes` skips the question). From the answer on, the board owes this
   Pi its power back; it notes that before anything else, so no reset of the board loses it.
   Then the board shuts this Pi down the normal way, through J8.37, and the tool also runs
   `systemctl poweroff`.
3. With the Pi off and the latch open, the board writes the image into its **other** A/B flash
   partition, a sector at a time, reading each back, and reboots into it **on trial**.
4. The new image checks itself and buys itself (the boot ROM's "try before you buy"), then powers
   this Pi back on. If it never buys itself -- it crashed, hung, or failed its check -- the boot
   ROM runs the previous image again after at most 16.7 s, and that image powers the Pi back on.
   If the install itself fails, the board clears what it wrote, keeps the old image and powers
   the Pi back on.
5. When the Pi is back, `pitrac-link info` shows the version running, and `pitrac-link
   upd-status` gives the result, with a line under it that says why:
   - `installed, 1.1 -> 1.2`: it ran on trial and bought itself;
   - `ROLLED BACK`: the new image never bought itself, and why;
   - `INSTALL FAILED`: the install did not finish, and why.

**`INSTALL FAILED ... CLEANUP`** means the board could not clear a failed install. Its other
partition may hold an image that a reset would launch without a trial.
- Do not reset or power-cycle the board, except into BOOTSEL to reflash it over USB
  ([`BENCH_P8_PI.md`](../BENCH_P8_PI.md) §8.9).
- It needs flash that refuses both programming and erasing, so expect never to see it.

Before the first update, each board needs its partition table, once, over USB
([`BENCH_P8_PI.md`](../BENCH_P8_PI.md) §8.7). Without it the board answers `NO_SLOT`.

**Limits.**
- An image that passes its trial but cannot talk on the link can only be replaced over USB.
- The board refuses an image without the try-before-you-buy flag (a `-DPITRAC_TBYB=OFF`
  build): it could not roll back.
- Bump the version for every image (`PITRAC_FW_VERSION_MINOR` in `CMakeLists.txt`). An image
  no newer than the running one still installs, but buying it erases the old one
  whenever the old one would otherwise win the next normal boot: always for an older version, and for an equal one when the old image is in A (equal versions boot A). Then there is no fallback. The tool warns before it sends. Its warning says the
  erase always happens, which overstates it; it will be reworded with the next change to the
  tool.

## Install

Do this with the Pi **off the board**, on its own USB-C supply. Never power a seated Pi from
its own USB-C: that supply would feed the board's switched +5 V rail through J8.2/4.

**A new Pi** (added 2026-10-26):
1. Raspberry Pi Imager, **Raspberry Pi OS Lite (64-bit)**. A desktop image may take the power
   key (below). In the OS customisation, set a hostname, a user, the network and SSH on. Once
   the Pi sits on the board, SSH is the way in: its USB-C stays unplugged.
2. After the first boot: `sudo apt update && sudo apt full-upgrade -y`, `sudo apt install -y
   gpiod` (for `gpioinfo`), `sudo rpi-eeprom-update -a`, then `sudo reboot`.
3. No serial console on the UART: if `/boot/firmware/cmdline.txt` contains
   `console=serial0,115200`, remove it with `sudo sed -i 's/console=serial0,115200 //'
   /boot/firmware/cmdline.txt`. Then `cat /boot/firmware/cmdline.txt` names neither `serial0`
   nor `ttyAMA0`.
4. From the PC, in `Hardware\firmware`: `scp -r pi <user>@<hostname>.local:~/pitrac-pi`.

```bash
# on the Pi, in ~/pitrac-pi
sudo bash ./install.sh               # bash: a copy from Windows may lose the execute bit
sudoedit /boot/firmware/config.txt   # add the two lines above, if install.sh asked
sudo bash ./install.sh               # again: both lines now "present"
sudo reboot
```

Then the checks below. Once they pass, **image the card**: `BENCH_P8_PI.md` §8.0 wants a
backup that has been restored and booted once before the Pi is seated.

## Check it before seating the Pi

| Check | Expect |
|---|---|
| `systemctl status pitrac-rpi5-on` | `active (exited)` |
| `pinctrl get 22` | GPIO22 is an output, driven high (`op`, `dh`, `hi`) |
| `gpioinfo \| grep -i gpio26` (package `gpiod`) | GPIO26 is claimed: the line shows a consumer, the `gpio-shutdown` key |
| `journalctl --list-boots`, after two power cycles | one entry per boot, so the journal is on the card |
| The board's shutdown request, by hand: a female-female jumper from header pin 37 (GPIO26) to pin 39 (GND) for ~0.5 s. GPIO26 is an input with a pull-up, so this is a key press | the Pi starts an orderly shutdown within ~1 s. A menu, or nothing, means the request will not work on the board (see "Desktop images" below) |
| `sudo sh /usr/lib/systemd/system-shutdown/pitrac-rpi5-on.shutdown check` | `pitrac: NOT safe -- / (/dev/mmcblk0p2) is still writable: would leave RPI5_ON (GPIO22) high`. That is the right answer on a running system: it shows the hook reads this image's mount table. `check` never touches GPIO22 |
| `sh tests/test_shutdown_hook.sh`, in this folder | `21 cases, 0 failed` |
| `python3 -m unittest discover -s tests -p test_pitrac_link.py`, in this folder | `Ran 23 tests` ... `OK (skipped=4)`. The four skipped tests compare the tool's tables with the firmware's C sources and run only in a repo checkout, where all 23 pass |
| `ls -l /dev/ttyAMA0` (after the reboot with `dtparam=uart0=on`), then `sudo fuser /dev/ttyAMA0` | the device exists, and `fuser` prints nothing: no console or getty holds it |
| `pinctrl get 14-15` | GPIO14 is `TXD0` and GPIO15 is `RXD0`: UART0 is on header pins 8 and 10 |
| `groups` | includes `dialout`, so `pitrac-link` can open `/dev/ttyAMA0` without `sudo` |
| Optional: DMM on header pin 15 (GPIO22) to pin 6 (GND) during `sudo poweroff` | ~3.3 V until the very end of the shutdown, then ~0 V |

**Desktop images:** the desktop session may take KEY_POWER and show a dialog instead of
powering off. Then the board's request does nothing visible, and it cuts the power at 60 s
with a `PI_SHUTDOWN_TIMEOUT`. Raspberry Pi OS Lite leaves the key to `systemd-logind`
(`HandlePowerKey=poweroff`). On a desktop image, boot to the console instead (`sudo
raspi-config`: System Options, Boot / Auto Login, Console). With no desktop session running,
`systemd-logind` takes the key. The pin 37 jumper check above shows which you have, before the
Pi is seated; [`BENCH_P8_PI.md`](../BENCH_P8_PI.md) §8.1 checks it again on the board.

## Uninstall

```bash
sudo systemctl disable pitrac-rpi5-on
sudo rm /etc/systemd/system/pitrac-rpi5-on.service \
        /usr/lib/systemd/system-shutdown/pitrac-rpi5-on.shutdown \
        /etc/systemd/journald.conf.d/90-pitrac-persistent.conf \
        /usr/local/bin/pitrac-link
sudo systemctl daemon-reload
```
