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
`src/link_proto.h`).

```bash
pitrac-link ping          # board : PiTrac RP2354, protocol 4, version 1.23
pitrac-link info          # version, build stamp, A/B partition, boot type
pitrac-link status        # power state, fault, +5V_IN
pitrac-link hist          # the board's halt telemetry (its `hist`)
pitrac-link upd-status    # a staged update, and how the last one ended
```

No answer means the board is not listening: it must be `RUNNING` this Pi with GPIO22 high.
At the board's USB CLI, `link` shows whether the lines are connected and counts frames and CRC
errors. The board answers between CLI commands, so during a long one (`cal model`, `scan`) the
tool retries and may give up.

### The board's CLI from this Pi (image 1.23 on, protocol 4)

**The Pi can run anything the USB CLI can** (the owner's decision, 2026-11-24): drive the
beam, fire the strobe live, set the gate, `off`. Until image 1.12 the link was a fixed list
of requests and could do none of that. Image 1.13's protocol 2 failed its independent review
and never shipped, and so did 1.14 and 1.15 with protocol 3 (V114-F1–F8, V115-F1–F7), and 1.16
to 1.20 with protocol 4 (V116-F1–F4, V117-F1–F3, V118-F1/F2, V119-F1–F3, V120-F1–F4). This tool
needs protocol 4, image 1.23 on, for `cli` and `shell`; it still updates a protocol-1 board.

```bash
pitrac-link cli stat                      # one command, its output as it is printed
pitrac-link cli strobe live on confirm    # every word as typed at the USB console
pitrac-link shell                         # interactively; 'exit' or Ctrl-D leaves
pitrac-link cli-cancel 5eed0001:12        # how a command ended, or cancel it if it has not run
```

- **The same guards.** The line goes through the board's own dispatcher, so every refusal the
  USB CLI has applies:
  - the live-mode allowlist;
  - the strobe staircase, cap and budget;
  - the beam/live interlock;
  - `reset`/`bootsel` with a Pi powered;
  - the beam's 25 %, which is firmware for USB and the Pi alike: the PWM pin's high time is
    never more than a quarter of its period, and the carrier stays within 1-200 kHz. That
    binds the pin; the LED adds the board's drive stretch, measured per board (the owner,
    2026-11-26; image 1.17).

  A live strobe session is shared: USB and the Pi can each arm, fire and end it.
- **The Pi-only power rule.** What would cut this Pi's power with no shutdown runs from here
  only with a trailing `confirm`:
  - `forceoff confirm`;
  - `reset force confirm`, `bootsel force confirm`;
  - `adc5vcal <volts> confirm`: a wrong scale makes the supply monitor drop the rail;
  - `fault clear confirm`, while a fault holds the rail up.

  Plain `off` runs only when the board is RUNNING this Pi: there it is the orderly shutdown,
  and anywhere else it would be a hard cut. A refusal is exit 3, and the board prints why.
- **Seen at the bench.** Each command shows on the board's USB console as `[pi] <command>`,
  with its output. A key typed there asks it to stop.
- **At most once.** A command is not safe to repeat: `strobe pulse 20` twice is two pulses.
  - It first takes a ticket from the board. The board runs a ticket once, and only once.
  - The tool sends it again only if nothing comes back. The board answers a resend from its
    record, the last 16 tickets.
  - A ticket from before a board reset, or older than the record, runs nothing: the outcome is
    then reported UNKNOWN, never guessed.
- **Use one actual UART producer**: no other open writer, serial console or service
  on this link. Cooperating clients take `flock`; the tool also requests `TIOCEXCL`
  where supported. Neither revokes an already-open descriptor. The idle-state
  preflight is not atomic arbitration between independent producers.
- **Ctrl-C before the command line goes out** -- while the tool waits for its ticket, or
  observed at the last check before writing the first byte on the **first** send --
  means nothing ran, exit 6. The check and the OS write are not atomic. After partial
  progress the tool tries to finish that frame. A stop during a retry still at byte
  zero abandons the retry, not the original ticket. If that wait raises an I/O
  error, the client checks the pending stop before returning: it attempts the
  original ticket's abort, even after two Ctrl-Cs. A port that cannot take the
  abort still leaves UNKNOWN; a retry error without a stop does not auto-cancel.
- **Ctrl-C after** sends `CLI_ABORT` for the command's ticket, and the command is never sent
  again. The board then does one of three things:
  - not started yet: cancels it, and it never runs;
  - running: asks it to stop;
  - already done: says how it ended. A stop that arrives after the command finished is exit 8.

  A second Ctrl-C stops waiting after attempting the ticket's stop, also when the two
  come together (image 1.21, V120-F2: 1.20 then gave up with no stop sent, and the command
  ran on). Once the line may have gone out, the outcome is then UNKNOWN, and the tool prints
  the ticket for `cli-cancel`. Both are looked for before each wait, never between an answer
  and the outcome made from it: an answer that has arrived is the outcome.
- **If the serial port fails** once the line may have gone out, the outcome is UNKNOWN, with
  the ticket. Before that, nothing ran: exit 1.
- **One 1 s write deadline per frame.** After a wait or partial progress the tool
  starts no further write after it observes expiry. A zero-byte failure on the first
  CLI send is exit 1; a partial or unavailable count is UNKNOWN with the ticket. A
  retry taking zero bytes does not undo an earlier send. The usual eight fixed-request
  attempts each budget 1 s for writing and 0.3 s for the reply; this is not a bound
  on scheduling, kernel close, or total process duration.
- **If the board answers as older firmware** (protocol 1, after a rollback, say) once the line
  may have gone out, the outcome is UNKNOWN too, with the ticket and why. Before the line went
  out, the same answer is exit 1 (update the board).
- **A restart loses the board's record of its tickets, on any firmware** (image 1.17, review
  note V116-N2). Each boot starts a new session with an empty record, so going back to 1.17
  or later does not bring a ticket from before the restart back: `cli-cancel` then says no
  query can recover it. A log of the board's USB console, which shows `[pi] <command>` for
  every command the Pi ran, is then the only record. 1.16's tool said to ask again once the
  board ran 1.16; that could not work.
- **`cli-cancel` asks about a command that may already have run.** If it cannot hear the
  answer, the outcome stays UNKNOWN: exit 5, with the ticket, and whether the query was never
  sent or did not finish. That covers a port that cannot be opened, another program holding
  it, a device that is no tty, a failing port, and a Ctrl-C at any point before the answer
  (V116-F4, V117-F3). 1.16 exited 2 or 1, as for a new command, with no ticket; 1.17 still let a
  Ctrl-C while opening, a tty that would not set up, and a failing close escape or replace the
  exit. Otherwise it exits as `cli` would for how that command ended. Its UNKNOWN goes to
  stderr, as every other UNKNOWN does, and its reason names the exception's class.
- **Ctrl-C never interrupts the tool part-way** (image 1.20, V119-F1). From its start it is
  only counted, and acted on where an outcome is still to be had: as above for `cli` and each
  `shell` command; at a prompt (`shell`'s, the update's) it ends the wait, as no answer; for
  `cli-cancel` before the answer (UNKNOWN, 5); for `ping`, `info`, `status`, `hist` and
  `upd-status` before the answer (exit 130, nothing to report); for `update` through the
  last stop check before its first APPLY write (not applied, exit 1, and the board drops what this run sent
  it). An apply once any of it is taken, and an
  `abort`, are waited for: their answer decides. Every exit is
  decided at one point, and from there nothing changes it. 1.19 let a Ctrl-C between an
  answer and its report replace it: UNKNOWN for a heard answer, a traceback after an accepted
  update.
- **An outcome once had stands, for every command** (image 1.19, V118-F1). Getting the outcome
  and saying it are two steps. After the first, nothing changes the exit:
  - a stdout or stderr that cannot be written (`| head`, a closed pipe, `>&-`): the line goes
    to the other stream, an UNKNOWN's ticket included. With both gone, only the exit is left;
  - a port that will not close, or a message about that which cannot be written either.

  `cli` output that stdout cannot take does not disturb the command: the tool listens to its
  end, the exit is the board's, and stderr says how many characters went nowhere (the board's
  USB console printed them all). 1.18 turned a heard `cli-cancel` answer into UNKNOWN when
  stdout was a closed pipe, lost an UNKNOWN's ticket to a Ctrl-C while it was printed, and
  took a `cli` stdout error for the serial port failing.
- **Output holds nothing up** (image 1.20, V119-F2). Each stream is written by a thread of its
  own. One that takes nothing for 2 s -- a pipe nobody reads, a terminal paused with Ctrl-S --
  is given up as a closed one is, once (the line it was taking may still appear later). It is
  written 4 KB at a time, and each 4 KB taken counts, so a long piece read steadily is not
  given up (image 1.21, V120-N1). A stream whose thread cannot be started -- the process is
  out of threads -- is unavailable, as a closed one is, and the exit still stands (V120-F4:
  1.20 let that error out, and the exit became 1). A command's output is queued, so a stalled
  stdout never holds up the command or its stop. After an accepted apply, this Pi's shutdown is
  requested before anything is printed.
- **Answers name the session and the ticket** (protocol 4). Numbers restart at 1 when the board
  restarts, so the tool, `cli-cancel` included, takes only an answer with both. None -- or the
  command is still running, which the board takes as a stop -- is UNKNOWN.
- **Output is checked.** Every chunk carries its offset, and the end carries the total and a
  CRC. If any was lost on the way, the exit is 4, even though the command ran. The board's USB
  console still has all of it.
- **One at a time on the board.** While a command runs, the board reads nothing else from the
  link but that command's own `CLI_ABORT`.
- **Procedural rules still apply to Pi software:** nothing conductive near D12, eyes away from
  the LED bank, the 30 % open-load stop on a strobe staircase (`HANDOFF.md` §3).
- **Exit codes of `cli`:**

  | Code | Meaning |
  |---|---|
  | 0 | it ran to its end, and all its output arrived |
  | 1 | a link or tool error before the line went out -- the port busy, failing, or taking none of the line in 1 s, no board, or firmware before 1.23: nothing ran |
  | 2 | not run: not a valid line (1-95 printable ASCII), refused as malformed, or the port could not be opened |
  | 3 | not run: the Pi-only power rule (if the board's explanation was lost, the tool says so) |
  | 4 | it ran, but some of its output was lost |
  | 5 | outcome UNKNOWN (no answer, the board restarted or went back to older firmware, or the port failed or stopped taking frames after the line, or part of it, may have gone out). The tool says whether `cli-cancel` with the ticket it printed can still find out: not once the board has restarted |
  | 6 | nothing ran: cancelled before it ran, or stopped before the line went out |
  | 7 | it acted on a stop and ended early |
  | 8 | a stop was asked, but it ran to its end: it does not stop part-way, or the stop came after it had finished |

## Updating the RP2354's firmware from this Pi

Any RP2354 reset cuts this Pi's power, so the Pi cannot flash the board the usual ways. The
update therefore runs in the board's own firmware, and its one reset comes after the Pi is
off:

```bash
pitrac-link update pitrac.bin        # build/pitrac.bin (or build/pitrac.uf2) from the PC
```

1. The tool checks the file (an RP2350 Arm image, flagged try-before-you-buy). It asks the
   board's update state first: if the board already holds an update -- receiving, verified,
   armed or installing -- it stops there, sends none of the image and drops nothing, since
   that update is not this run's (`pitrac-link upd-status` shows it, `pitrac-link abort`
   drops it). 1.20 could drop such an update (V120-F1). Then it sends the image in 240-byte
   chunks into the board's RAM (288 KB at most; ~3 s for a 220 KB image), and the board checks
   its SHA-256 and the image's header. With one actual producer, the board may hold
   this run's staging after BEGIN, or nothing (a cut frame, reset or USB abort).
   A stop, a 'no', a transfer failure or refused APPLY may trigger cleanup. If **any**
   APPLY attempt may have gone out and no conclusive answer was heard, nothing is
   dropped. Reconcile with `pitrac-link upd-status`; do not start a new apply on
   that evidence. An acknowledgement means **accepted**, not installed.
2. It asks before going on (`--yes` skips the question; no answer -- the end of input, a
   Ctrl-C, no stdin at all -- is no, and the staged image is dropped). From APPLY acceptance, the board owes this
   Pi its power back; it notes that before anything else, so no reset of the board loses it.
   Then the board shuts this Pi down the normal way, through J8.37, and the tool also runs
   `systemctl poweroff`, before it prints anything (image 1.20): a terminal or pipe that does
   not take its output cannot hold the request up. `--no-poweroff` leaves it to the board.
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
  whenever the old one would otherwise win the next normal boot: always for an older version, and for an equal one when the old image is in A (equal versions boot A). Then there is no fallback. The tool warns before it sends, in those
  words (image 1.20; before, its warning said the erase always happens).

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
| `python3 -m unittest discover -s tests -p test_pitrac_link.py`, in this folder | For 1.23 expect `Ran 141 tests` and `OK` in a Pi checkout with firmware sources. Without `src/`, four table comparisons skip. On Windows six need POSIX. Tests include the real process with closed/full pipes, SIGINT sweeps, and exact diagnostics, wire order and fake-board state on retry and transport failures. No test runs the real `systemctl poweroff`. |
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
