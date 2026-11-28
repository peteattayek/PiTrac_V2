#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
"""pitrac-link -- the Pi's end of the PiTrac board's serial link.

Talks to the board's RP2354 over the Pi 5's UART0 on the header (GPIO14 TXD on
pin 8, GPIO15 RXD on pin 10; /dev/ttyAMA0 with dtparam=uart0=on), 921600 8N1, in
the frames the firmware's src/link_frame.h defines. Standard library only.

    pitrac-link ping                      is the board there: protocol and version
    pitrac-link info                      image version, build, A/B partitions, boot
    pitrac-link status                    power state, fault, +5V_IN
    pitrac-link hist                      the board's halt telemetry
    pitrac-link upd-status                a staged update, and the last update's result
    pitrac-link abort                     drop a staged update
    pitrac-link update FILE [--yes]       send build/pitrac.bin (or .uf2), have the
                                          board check it, then install it: the board
                                          shuts this Pi down, installs, reboots into the
                                          new image on trial and powers the Pi back on
    pitrac-link cli COMMAND...            run one board CLI command (image 1.23 on), as at
                                          its USB console: e.g. pitrac-link cli strobe live
    pitrac-link shell                     the board's CLI, interactively ('exit' to leave)
    pitrac-link cli-cancel SESSION:TICKET how a command ended -- or cancel it, if it has
                                          not run (the ticket an UNKNOWN outcome names)

The CLI over the link (protocol 4) runs anything the USB CLI can, under the same
guards. It shows on the board's USB console as '[pi] <command>'. Each command
runs under a ticket the board issues, at most once, whatever is resent; an
answer is taken only if it names this command's session and ticket. A Ctrl-C
observed at the last check before writing on the first send, with zero accepted,
means the line is never sent; one after asks the board to stop the command (as a key does
on USB), or cancels it if it has not started; a second Ctrl-C stops waiting --
after attempting the ticket's stop, even when both come together. From the
Pi, what would cut this Pi's
power needs a trailing 'confirm' ('forceoff confirm', 'reset force confirm',
'bootsel force confirm', 'adc5vcal <volts> confirm', and 'fault clear confirm'
while a fault holds the rail up), and 'off' runs only as the orderly shutdown,
when the board is RUNNING this Pi.

Exit codes of 'cli': 0 it ran to its end and all its output arrived; 1 a link or
tool error before the command line was sent (the port busy, failing, or taking
none of the line in WRITE_TIMEOUT_S; no board; firmware before 1.23), so nothing
ran; 2 not run (not a valid line, refused as malformed, or the port could not be
opened); 3 not run: the Pi-only power rule (the board printed why; if that text
was lost, this tool says so); 4 it ran, but some of its output was lost on the
way; 5 the outcome is UNKNOWN (no answer, the board restarted or went back to
older firmware, or the port failed or stopped taking frames once the line, or
part of it, may have gone out): 'cli-cancel' with the ticket printed finds
out, unless the board has restarted since; 6 nothing ran: cancelled before it ran, or stopped before
the line was sent; 7 it acted on a stop and ended early; 8 a stop was asked
but it ran to its end -- it does not stop part-way, or the stop came after it
had finished.

'cli-cancel' exits the same way for how the command it asks about ended. If it
cannot hear the answer -- the port cannot be opened, another program holds it, it
is no tty, it fails, or Ctrl-C at any point before the answer -- that command's
outcome is UNKNOWN: exit 5, with its ticket. A board that restarted since has no
record of the ticket on any firmware: then only a log of its USB console, which
shows '[pi] <command>' for every command the Pi ran, can tell.

Ctrl-C does not interrupt this tool part-way (image 1.20). From main()'s first line
(before it, while Python starts and loads the tool, nothing has been sent, and Python's
own handling ends it) a Ctrl-C is only counted. It is acted on at a few points, each
while an outcome is still to be had: in 'cli' as above, and in 'shell' the same for
each command; at a prompt -- the shell's, the update's [y/N] -- it ends the wait, as
no answer; in 'cli-cancel' before the answer (UNKNOWN, exit 5); in ping, info,
status, hist and upd-status before the board's answer (exit 130, nothing to report);
in 'update' through the last stop check before its first APPLY write (then not
applied, exit 1). Stop checks and OS writes are not atomic. A stopped zero-byte
CLI retry goes to the abort for the original ticket. An apply once possibly sent
and an abort are waited for: their answer decides. Every exit status is decided at
one point; from there main() returns it whatever follows, and a Ctrl-C after it is
not acted on at all.

Each frame has one WRITE_TIMEOUT_S (1 s) deadline: the tool starts no further write
after observing expiry. This does not bound OS scheduling or close. Only one actual
producer may write the UART. An update first asks the board's update
state: if the board holds an update already, nothing of this one is sent, and
nothing is dropped. What this run's BEGIN starts is this run's, and is dropped if
the update goes no further (a stop, a 'no', a transfer error, a refused apply);
before that BEGIN nothing is dropped. An uncertain APPLY is never dropped: check
upd-status. Its acknowledgement means accepted, not installed.

Getting an outcome and saying it are separate (image 1.19). Once this tool has it,
nothing after changes the exit status: not a stdout or stderr that cannot be
written -- closed (a pipe with no reader), taking nothing for WRITE_WAIT_S, 2 s
(a pipe nobody reads, a terminal held by Ctrl-S; written WRITE_CHUNK bytes at a
time, each chunk taken counts), or with no thread to write it: then it is given up,
once, and not written to again -- not a port that will not close, not a diagnostic
about that or about a defect in this tool, and not the interpreter's flush at exit. A line that cannot go to its stream goes
to the other one, an UNKNOWN's ticket included; with both gone, only the exit
status is left. A command's output that stdout cannot take does not hold up the
command, or a Ctrl-C's stop: the tool listens to its end, the exit is still the
board's, and stderr says how much output went nowhere (the board's USB console
printed all of it). Nothing that must happen is put behind output: after an
accepted apply, this Pi's shutdown is requested before a line is written.

The board only listens while it powers this Pi and the Pi drives RPI5_ON (GPIO22)
high: pitrac-rpi5-on.service (README.md in this folder).
"""

import argparse
import collections
import hashlib
import os
import random
import signal
import struct
import subprocess
import sys
import threading
import time
import traceback

# ---------------------------------------------------------------------------
# Framing (src/link_frame.h): 0xA5 | len | type | seq | payload | crc16 LE
# ---------------------------------------------------------------------------
SYNC = 0xA5
MAX_PAYLOAD = 255
RESPONSE = 0x80
BYTE_GAP_S = 0.050

PING, INFO, STATUS, HIST = 0x01, 0x02, 0x03, 0x04
UPD_BEGIN, UPD_DATA, UPD_END, UPD_APPLY, UPD_ABORT, UPD_STATUS = 0x10, 0x11, 0x12, 0x13, 0x14, 0x15
CLI, CLI_OUT, CLI_ABORT, CLI_BEGIN = 0x20, 0x21, 0x22, 0x23
PROTO_VERSION = 4
UPDATE_PROTOS = (1, 2, 3, 4)  # the update requests are the same in all four
CLI_PROTO = 4             # the CLI with tickets and sessions: image 1.16 on (protocol 2, image
                          # 1.13, and protocol 3, images 1.14 and 1.15, were rejected unshipped)
DATA_MAX = 251            # LINK_DATA_MAX
CHUNK = 240               # what this tool sends per UPD_DATA

# The CLI (protocol 4, src/link_cli.h)
RESULT = '<BBIIIH'        # the CLI family's answer: status, flags, session, ticket, output
                          # bytes, output CRC (LINK_CLI_RESULT_BYTES)
OUT_HDR = '<III'          # a CLI_OUT's header: session, ticket, offset (LINK_CLI_OUT_HDR)
RESULT_BYTES, OUT_HDR_BYTES = struct.calcsize(RESULT), struct.calcsize(OUT_HDR)
CLI_LINE_MAX = 95         # LINK_CLI_LINE_MAX
F_REPEAT, F_STOP_ASKED, F_STOPPED, F_CANCELLED = 0x01, 0x02, 0x04, 0x08
CLI_FIRST_S = 1.0         # the board's first frame (output, keepalive or the end) within this,
                          # or the request is sent again
CLI_QUIET_S = 15.0        # silence this long once it has started: asked again (a keepalive
                          # comes each second while a command runs and yields)
CLI_TRIES = 8             # sends of one request; then the tool asks how it ended instead
ABORT_EVERY_S = 0.5       # CLI_ABORT is repeated this often until an answer comes
ABORT_TRIES = 10          # ...this many times; then the outcome is UNKNOWN

# fwu_err_t (src/fw_update.h), in order, then the link's own.
ERRORS = ['ok', 'STATE', 'NO_SLOT', 'SIZE', 'OFFSET', 'LENGTH', 'INCOMPLETE', 'DIGEST', 'IMAGE',
          'NOT_TBYB', 'POWER', 'ARM_TIMEOUT', 'INTERRUPTED', 'FLASH', 'REBOOT', 'CLEANUP']
E_LINK_MISMATCH, E_LINK_STALE, E_LINK_SESSION = 0xF9, 0xFA, 0xFB
E_LINK_PI_RULE, E_LINK_LENGTH, E_LINK_UNKNOWN = 0xFC, 0xFD, 0xFE

# run_cli()'s exit codes (the module docstring says what each means).
RC_OK, RC_ERROR, RC_NOT_RUN, RC_PI_RULE, RC_INCOMPLETE = 0, 1, 2, 3, 4
RC_UNKNOWN, RC_CANCELLED, RC_STOPPED, RC_RAN_TO_END = 5, 6, 7, 8
EXPLAIN = {
    'STATE': 'not allowed in the board\'s current update state (pitrac-link upd-status)',
    'NO_SLOT': 'the board has no A/B partition to install into: its partition table is '
               'installed once over USB (BENCH_P8_PI.md 8.7)',
    'SIZE': 'empty, or larger than the board can stage or its partition holds',
    'OFFSET': 'a chunk was not at the offset the board expected',
    'LENGTH': 'a chunk was empty or ran past the declared size',
    'INCOMPLETE': 'the board has not received every byte yet',
    'DIGEST': 'the SHA-256 of what arrived does not match: nothing was kept',
    'IMAGE': 'not an RP2350 Arm Secure executable (no valid IMAGE_DEF in the first 4 KB)',
    'NOT_TBYB': 'the image is not flagged try-before-you-buy: built with PITRAC_TBYB=OFF? '
                'Without it there is no automatic rollback, so the board refuses it',
    'POWER': 'the board must be RUNNING this Pi (it shuts it down to install)',
    'ARM_TIMEOUT': 'the board did not get the Pi powered down in time',
    'INTERRUPTED': 'the install was cut short: the board left STANDBY (the power button?) '
                   'or reset; the old image stays',
    'FLASH': 'a flash sector did not read back as written; the other partition was cleared '
             'and the old image stays',
    'REBOOT': 'the boot ROM refused the update reboot; the other partition was cleared and '
              'the old image stays',
    'CLEANUP': 'a failed install could NOT clear the other partition: it may hold an image a '
               'reset would launch WITHOUT a trial. Do not reset or power-cycle the board, '
               'except into BOOTSEL to reflash it over USB (BENCH_P8_PI.md 8.9)',
}
STATES = ['idle', 'receiving', 'verified', 'armed', 'installing']
RESULTS = ['none', 'installed', 'ROLLED BACK', 'INSTALL FAILED']
# The detail byte, per result (fwu_status_t; fwu_detail_name() in src/fw_update.c):
# installed: FWU_INSTALLED_*; ROLLED BACK: fwu_trial_t; INSTALL FAILED: an error above.
INSTALLED_HOW = ['it ran on trial and bought itself',
                 'it was launched WITHOUT a trial: no rollback was in place']
TRIAL = ['none recorded', 'the image reaches into the config sectors',
         '+5V_IN could not be read fresh', 'the boot ROM\'s explicit buy failed']
POWER = ['STANDBY', 'POWERING_ON', 'PI_BOOTING', 'RUNNING', 'BENCH_RUNNING', 'SHUTTING_DOWN',
         'FORCE_OFF', 'FAULT']
FAULTS = ['none', 'USB_POWER_ONLY', 'RAIL_COLLAPSE', 'SUPPLY_LOST', 'NO_PI_DETECTED',
          'PI_BOOT_TIMEOUT', 'PI_SHUTDOWN_TIMEOUT', 'ADC_OVERRUN', 'BEAM_BLOCKED', 'CAM_TIMEOUT',
          'STROBE_OVERCURRENT', 'STROBE_CLAMP', 'INTERNAL', 'SUPPLY_UNREADABLE']
HIST_NAMES = ['boots', 'boots after a watchdog hang', 'Pi shutdown requests',
              '  ...Pi read down in time', '  ...timed out (latch opened anyway)',
              'Pi went down unrequested', '  ...and came back (Pi reboot)',
              'power cuts to a powered Pi', 'faults latched']
BOOT_TYPES = {0: 'normal', 2: 'BOOTSEL', 3: 'RAM image', 4: 'flash update', 0xd: 'PC/SP'}


# A board that answers a CLI request with LINK_E_UNKNOWN alone runs firmware without the
# CLI over the link (protocol 1 or 2). Once this command's line may have gone out, that says
# nothing about whether the firmware it went to ran it (image 1.16, review finding V115-F7).
NO_CLI_NOW = ('the board now answers as firmware without the CLI over the link (protocol 1 '
              'or 2, image 1.12 or older): it went back to older firmware -- an update '
              'rolled back, or another image was loaded -- after this command may have '
              'reached it')


def err_name(code):
    if code < len(ERRORS):
        return ERRORS[code]
    return {E_LINK_MISMATCH: 'MISMATCH', E_LINK_STALE: 'STALE', E_LINK_SESSION: 'SESSION',
            E_LINK_PI_RULE: 'PI_RULE', E_LINK_LENGTH: 'LENGTH (link)',
            E_LINK_UNKNOWN: 'UNKNOWN'}.get(code, '0x%02X' % code)


def result_detail(result, detail):
    """What UPD_STATUS's detail byte says about the last update's result."""
    if result == RESULTS.index('installed'):
        return INSTALLED_HOW[detail] if detail < len(INSTALLED_HOW) else '?'
    if result == RESULTS.index('ROLLED BACK'):
        if detail == 0:
            return 'the new image never bought itself (it crashed, hung or did not start)'
        return TRIAL[detail] if detail < len(TRIAL) else '?'
    if result == RESULTS.index('INSTALL FAILED'):
        return '%s -- %s' % (err_name(detail), EXPLAIN.get(err_name(detail), ''))
    return ''


def crc16(data, crc=0xFFFF):
    """CRC-16/CCITT-FALSE. crc16(b'123456789') == 0x29B1."""
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode(ftype, seq, payload=b''):
    if len(payload) > MAX_PAYLOAD:
        raise ValueError('payload over 255 bytes')
    body = bytes([len(payload), ftype, seq]) + bytes(payload)
    c = crc16(body)
    return bytes([SYNC]) + body + bytes([c & 0xFF, c >> 8])


class Frame:
    def __init__(self, ftype, seq, payload):
        self.type, self.seq, self.payload = ftype, seq, payload


class Decoder:
    """Byte-at-a-time, as src/link_frame.c: hunt for 0xA5, drop a frame whose CRC
    fails or whose bytes stop for BYTE_GAP_S."""

    def __init__(self):
        self.buf = None
        self.t_last = 0.0
        self.crc_errors = 0

    def push(self, b, now):
        if self.buf is not None and now - self.t_last > BYTE_GAP_S:
            self.buf = None
        self.t_last = now
        if self.buf is None:
            if b == SYNC:
                self.buf = bytearray()
            return None
        self.buf.append(b)
        if len(self.buf) < 3 or len(self.buf) < 3 + self.buf[0] + 2:
            return None
        body, rx = bytes(self.buf[:-2]), self.buf[-2] | (self.buf[-1] << 8)
        self.buf = None
        if crc16(body) != rx:
            self.crc_errors += 1
            return None
        return Frame(body[1], body[2], body[3:])


# ---------------------------------------------------------------------------
# Images (src/fw_update.c fwu_parse_image)
# ---------------------------------------------------------------------------
MARKER_START, MARKER_END = 0xffffded3, 0xab123579


def _parse_block(d, n, at):
    i, words, img_type, version = at + 4, 0, None, None
    while i + 4 <= n:
        t = d[i]
        size = (d[i + 1] | (d[i + 2] << 8)) if t & 0x80 else d[i + 1]
        if t == 0xFF:
            if size != words or i + 12 > n or struct.unpack_from('<I', d, i + 8)[0] != MARKER_END:
                return None
            return None if img_type is None else (img_type, version)
        if size == 0 or i + 4 * size > n:
            return None
        if t == 0x42 and size == 1:
            img_type = d[i + 2] | (d[i + 3] << 8)
        elif t == 0x48 and size >= 2:
            minor, major = struct.unpack_from('<HH', d, i + 4)
            version = (major, minor)
        i += 4 * size
        words += size
    return None


def parse_image_def(data):
    """The IMAGE_DEF in the first 4 KB: dict(type, exe_arm_s_rp2350, tbyb, version) or None."""
    n = min(len(data), 4096)
    for at in range(0, n - 15, 4):
        if struct.unpack_from('<I', data, at)[0] != MARKER_START:
            continue
        r = _parse_block(data, n, at)
        if r:
            img_type, version = r
            return {
                'type': img_type,
                'exe_arm_s_rp2350': (img_type & 0x773F) == 0x1021,
                'tbyb': bool(img_type & 0x8000),
                'version': version or (0, 0),
            }
    return None


UF2_MAGIC0, UF2_MAGIC1, UF2_MAGIC_END = 0x0A324655, 0x9E5D5157, 0x0AB16F30
FAMILY_RP2350_ARM_S = 0xe48bff59
XIP_BASE = 0x10000000


def uf2_to_bin(data):
    """The rp2350-arm-s blocks of a UF2, as one image from 0x10000000; ValueError if they are
    not contiguous."""
    parts = {}
    for off in range(0, len(data) - 511, 512):
        m0, m1, flags, addr, size, _blk, _n, fam = struct.unpack_from('<8I', data, off)
        if m0 != UF2_MAGIC0 or m1 != UF2_MAGIC1 or \
                struct.unpack_from('<I', data, off + 508)[0] != UF2_MAGIC_END:
            raise ValueError('not a UF2 file (bad magic at block %d)' % (off // 512))
        if flags & 0x1 or not flags & 0x2000 or fam != FAMILY_RP2350_ARM_S:
            continue                       # not main flash, or not an rp2350-arm-s block
        parts[addr] = data[off + 32:off + 32 + size]
    if not parts:
        raise ValueError('no rp2350-arm-s blocks in this UF2')
    out, at = bytearray(), XIP_BASE
    for addr in sorted(parts):
        if addr != at:
            raise ValueError('UF2 blocks are not contiguous at 0x%08X' % at)
        out += parts[addr]
        at += len(parts[addr])
    return bytes(out)


def load_image(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] == struct.pack('<I', UF2_MAGIC0):
        data = uf2_to_bin(data)
    return data


def vstr(v):
    return '%d.%d' % (v >> 16, v & 0xFFFF) if isinstance(v, int) else '%d.%d' % v


# ---------------------------------------------------------------------------
# Transport
# ---------------------------------------------------------------------------
class PortBusy(OSError):
    """Another program has the link's port."""


WRITE_TIMEOUT_S = 1.0     # absolute deadline for starting writes of one frame
WRITE_POLL_S = 0.05       # how often a write that waits for room looks at a stop


class WriteStopped(Exception):
    """A stop was asked before the port took any byte of a frame: none of it went out."""


class LinkError(Exception):
    pass


class WriteFailure(LinkError):
    """Local write failure with the known accepted prefix of this attempt."""

    def __init__(self, taken, size, cause):
        super().__init__(taken, size, cause)
        self.taken, self.size, self.cause = taken, size, cause

    def __str__(self):
        return ('the serial port took %s of a %d-byte frame before %s'
                % ('%d bytes' % self.taken if self.taken else 'none', self.size, self.cause))


class WriteTimeout(WriteFailure):
    """The port did not take a whole frame in time. taken: the bytes of it that it did
    take (0: none of it went out)."""

    def __init__(self, taken, size, timeout):
        super().__init__(taken, size, timeout)
        self.taken, self.size, self.timeout = taken, size, timeout

    def __str__(self):
        return ('the serial port took %s of a %d-byte frame in %g s (its output is stuck)'
                % ('none' if not self.taken else '%d bytes' % self.taken, self.size,
                   self.timeout))


def write_bounded(data, timeout, stop, put, wait, clock=time.monotonic):
    """Write one frame using nonblocking put(view): bytes accepted, or error (no
    bytes accepted by that syscall). Count progress across calls, including a
    failure while waiting for room. Do not start another put after observing
    expiry. The last poll of stop() and the following OS write are not atomic."""
    view = memoryview(data)
    taken, deadline = 0, clock() + timeout
    while view:
        if not taken and stop is not None and stop():
            raise WriteStopped()
        left = deadline - clock()
        if left <= 0:
            raise WriteTimeout(taken, len(data), timeout)
        try:
            n = put(view)
            if n:
                taken += n
                view = view[n:]
            else:
                wait(min(max(0.0, deadline - clock()), WRITE_POLL_S))
        except (OSError, ValueError) as e:
            raise WriteFailure(taken, len(data), e) from e
    return taken


class SerialPort:
    """Raw 921600 8N1 on a Linux tty, through termios: no pyserial needed.

    Cooperating clients take flock before flushing or writing. Request TIOCEXCL
    where supported. Neither revokes an already-open fd: the update protocol
    requires one actual UART producer."""

    def __init__(self, path, baud=921600):
        import errno
        import fcntl
        import termios
        speed = getattr(termios, 'B%d' % baud)
        try:
            self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        except OSError as e:
            if e.errno == errno.EBUSY:
                raise PortBusy(errno.EBUSY, '%s is held by another program (another '
                               'pitrac-link?)' % path)
            raise
        try:
            try:
                fcntl.flock(self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError:
                raise PortBusy(errno.EBUSY, '%s is in use by another pitrac-link' % path)
            try:
                fcntl.ioctl(self.fd, termios.TIOCEXCL)
            except OSError:
                pass                                              # not a tty that supports it
            # termios raises termios.error, which is not an OSError: as an OSError, a
            # device that opens but is no tty fails the way a missing one does, for every
            # command (image 1.18, review finding V117-F3).
            try:
                attrs = termios.tcgetattr(self.fd)
                attrs[0] = 0                                      # iflag: raw
                attrs[1] = 0                                      # oflag
                attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL   # 8N1, no flow control
                attrs[3] = 0                                      # lflag: no echo, no canon
                attrs[4] = attrs[5] = speed
                attrs[6][termios.VMIN] = 0
                attrs[6][termios.VTIME] = 0
                termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
                termios.tcflush(self.fd, termios.TCIOFLUSH)
            except termios.error as e:
                raise OSError(errno.ENOTTY, '%s cannot be set up as a serial port (%s)'
                              % (path, e))
        except BaseException:
            os.close(self.fd)
            raise

    def write(self, data, timeout=WRITE_TIMEOUT_S, stop=None):
        """One frame, with write_bounded()'s deadline and prefix reporting."""
        import select

        def put(view):
            try:
                return os.write(self.fd, view)
            except BlockingIOError:
                return 0

        def wait(s):
            select.select([], [self.fd], [], s)
        return write_bounded(data, timeout, stop, put, wait)

    def read(self, timeout):
        import select
        r, _, _ = select.select([self.fd], [], [], max(0.0, timeout))
        if not r:
            return b''
        try:
            return os.read(self.fd, 4096)
        except BlockingIOError:
            return b''

    def close(self):
        import fcntl
        import termios
        try:
            fcntl.ioctl(self.fd, termios.TIOCNXCL)    # TIOCEXCL outlives this fd otherwise
        except OSError:
            pass
        os.close(self.fd)


class Stopped(Exception):
    """A stop was asked before a request's answer came: nothing more is sent
    (Link.request(stop=...))."""


class CliResult:
    """How one command over the link ended, as far as this Pi can tell (protocol 4).

    outcome: 'done' (it ran: status 0, flags say how), 'not run' (refused; status says
    why), 'cancelled' (cancelled before it ran: nothing ran), 'not sent' (stopped before
    the command line went out: nothing ran), or 'unknown' (whether it ran cannot be
    established: why says why)."""

    def __init__(self, session, ticket):
        self.session, self.ticket = session, ticket
        self.outcome, self.why = 'unknown', ''
        self.status, self.flags = None, 0
        self.out_bytes = None      # what the board says it printed
        self.got = 0               # what arrived here, in order
        self.complete = False      # all of it, and its CRC matches
        self.stop_asked = False    # this tool asked the board to stop or cancel it
        self.gave_up = False       # ...because the board did not answer
        self.no_cli_now = False    # the board answered as firmware without the CLI (NO_CLI_NOW)
        self.lost = False          # the board's record of the ticket is gone: no query can tell

    def ident(self):
        return '%08x:%d' % (self.session, self.ticket)


class Link:
    """One request at a time, each answered by type | 0x80 with the same seq. A retry
    resends the same frame: every fixed request is safe to repeat (src/fw_update.h), and a
    CLI command runs at most once under its ticket whatever is resent (src/link_cli.h). The
    seq only pairs an answer with its request: it starts anywhere and need not be unique
    across runs of this tool."""

    def __init__(self, port, timeout=0.3, tries=8, clock=time.monotonic):
        self.port, self.timeout, self.tries, self.clock = port, timeout, tries, clock
        self.seq = random.randrange(256)
        self.dec = Decoder()
        self.retries = 0
        self.last_ticket = None    # (session, ticket) of the last command, for cli-cancel
        self.line_sent = False     # the last command's line has gone out, or some of it
        self.bytes_out = 0         # known accepted bytes; a bare port error supplies no count

    def _next_seq(self):
        self.seq = (self.seq + 1) & 0xFF
        return self.seq

    def _send(self, frame, stop=None):
        """Count known accepted bytes, including the prefix reported on failure.
        A port that raises plain OSError supplies no prefix, not proof of zero."""
        try:
            self.port.write(frame, WRITE_TIMEOUT_S, stop)
        except WriteFailure as e:
            self.bytes_out += e.taken
            raise
        self.bytes_out += len(frame)

    def request(self, ftype, payload=b'', timeout=None, stop=None, accept=None,
                first_write_stop=None):
        """Send a request and return its answer's payload, resending on silence.

        stop(), if given, is polled by every send until the port takes its first byte
        (write_bounded(): before the first try to write it, and while it waits for room),
        and at least every 0.1 s while waiting for the answer: once it is True nothing more
        is sent and Stopped is raised (image 1.15, review finding
        V114-F1: a stop during CLI_BEGIN; image 1.21, V120-F3: during the send). Each send
        has one WRITE_TIMEOUT_S deadline for starting writes, then a reply timeout.
        accept(payload), if given, says whether an answer of the right type and seq is this
        request's: one it refuses -- a late answer to another request that had the same
        8-bit seq -- is passed over and the wait goes on (V114-F8). first_write_stop(), if
        given, is looked at only while the first send waits for the port to take its first
        byte: Stopped then, and none of it went out; once any of it is out, the request
        is waited for (the apply's, image 1.21)."""
        self._next_seq()
        frame = encode(ftype, self.seq, payload)
        timeout = self.timeout if timeout is None else timeout
        for attempt in range(self.tries):
            if attempt:
                self.retries += 1
            try:
                self._send(frame, stop if stop is not None else
                           first_write_stop if not attempt else None)
            except WriteStopped:
                raise Stopped()
            deadline = self.clock() + timeout
            while True:
                left = deadline - self.clock()
                if left <= 0:
                    break
                if stop is not None and stop():
                    raise Stopped()
                for b in self.port.read(left if stop is None else min(left, 0.1)):
                    f = self.dec.push(b, self.clock())
                    if f and f.type == (ftype | RESPONSE) and f.seq == self.seq:
                        if not f.payload:
                            raise LinkError('an empty answer to 0x%02X' % ftype)
                        if accept is None or accept(f.payload):
                            return f.payload
        raise LinkError('no answer from the board after %d tries (is it RUNNING this Pi, '
                        'with RPI5_ON high? `link` at its USB CLI shows the lines)' % self.tries)

    # -- requests -------------------------------------------------------------
    # stop, where taken, is request()'s: Stopped once it is True. apply() looks at it only
    # until the port takes the apply's first byte, abort() never: once sent, their answer
    # is waited for. The write and reply budgets do not bound process scheduling.
    def ping(self, stop=None):
        r = self.request(PING, stop=stop)
        st, proto, version = struct.unpack_from('<BBI', r)
        return {'status': st, 'proto': proto, 'version': version}

    def info(self, stop=None):
        r = self.request(INFO, stop=stop)
        (st, version) = struct.unpack_from('<BI', r)
        stamp = r[5:25].decode('ascii', 'replace').rstrip()
        btype, bpart, flags, rs, rn, os_, on, cap = struct.unpack_from('<BbBIIIII', r, 25)
        return {'status': st, 'version': version, 'stamp': stamp, 'boot_type': btype,
                'boot_partition': bpart, 'partitioned': bool(flags & 1), 'trial': bool(flags & 2),
                'bought': bool(flags & 4), 'run_start': rs, 'run_size': rn,
                'other_start': os_, 'other_size': on, 'capacity': cap}

    def status(self, stop=None):
        st, ps, fault, mv, flags, up, el = struct.unpack_from('<BBBHBII',
                                                              self.request(STATUS, stop=stop))
        return {'status': st, 'state': ps, 'fault': fault, 'mv': mv, 'pi_present': bool(flags & 1),
                'latch': bool(flags & 2), 'rails': bool(flags & 4), 'uptime_ms': up,
                'state_ms': el}

    def hist(self, stop=None):
        r = self.request(HIST, stop=stop)
        return list(struct.unpack_from('<16I', r, 1))

    def upd_status(self, stop=None):
        r = self.request(UPD_STATUS, stop=stop)
        st, state, err, size, rcv, ver, res, frm, to, det = struct.unpack_from('<BBBIIIBIIB', r)
        return {'status': st, 'state': state, 'error': err, 'size': size, 'received': rcv,
                'version': ver, 'result': res, 'from': frm, 'to': to, 'detail': det}

    def begin(self, size, digest, stop=None):
        st, = struct.unpack_from('<B', self.request(UPD_BEGIN, struct.pack('<I', size) + digest,
                                                    stop=stop))
        return st

    def data(self, offset, chunk, stop=None):
        st, nxt = struct.unpack_from('<BI', self.request(UPD_DATA, struct.pack('<I', offset) + chunk,
                                                         stop=stop))
        return st, nxt

    def end(self, stop=None):
        st, ver = struct.unpack_from('<BI', self.request(UPD_END, timeout=1.0, stop=stop))
        return st, ver

    def apply(self, stop=None):
        """UPD_APPLY. stop() only until the port takes the apply's first byte: Stopped then,
        and none of it went out. Once any of it is out, it is waited for."""
        return self.request(UPD_APPLY, first_write_stop=stop)[0]

    def abort(self):
        return self.request(UPD_ABORT)[0]

    def cli_begin(self, stop=None):
        """A new ticket from the board (CLI_BEGIN): (session, ticket). Stopped if stop()
        turns True first."""
        r = self.request(CLI_BEGIN, stop=stop)
        if len(r) == 1 and r[0] == E_LINK_UNKNOWN:
            raise LinkError('this board\'s firmware has no CLI over the link with tickets '
                            '(protocol 4, image 1.23 on): update it first')
        if len(r) < 9 or r[0]:
            raise LinkError('CLI_BEGIN: %s' % err_name(r[0]))
        _, session, ticket = struct.unpack_from('<BII', r)
        return session, ticket

    def cli(self, line, out=None, stop=None, give_up=None):
        """Run one board CLI command (protocol 4). out(text) gets its output as it streams;
        stop() and give_up() are polled. Returns a CliResult.

        AT MOST ONCE (src/link_cli.h; image 1.14, review findings V113-F1/F2). The command
        takes a ticket first, and the board runs a ticket at most once, so a resend is safe
        and is answered from the board's record. The request is sent again only when nothing
        has come back -- CLI_FIRST_S before the first frame, CLI_QUIET_S of silence after it
        -- up to CLI_TRIES sends. Then, or once stop() returns True, it is never sent again:
        a CLI_ABORT for the ticket goes out every ABORT_EVERY_S instead. That cancels it if
        it has not run, asks it to stop if it is running, and reports how it ended if it has
        -- three different answers. When none of them can be had, the outcome is 'unknown'.

        COMPLETE OUTPUT (V113-F5): each CLI_OUT carries its offset, and the answer the total
        and a CRC of everything printed. A gap, a short total or a wrong CRC makes the
        result incomplete, even when the command ran.

        A STOP BEFORE THE LINE GOES OUT (image 1.15, review finding V114-F1) means it
        never goes out: stop() is polled while the ticket is fetched, and by the line's
        last stop check before each zero-prefix write. On the first send the outcome
        is then 'not sent'; on a retry the original ticket still needs an abort. In 1.14 a Ctrl-C while
        the board was busy answering CLI_BEGIN was first seen after the line had been
        sent, and the command ran. Once the port has taken a byte of it, the line is
        written whole if the port takes it, and a stop is a CLI_ABORT for the ticket.

        A PORT THAT TAKES NONE OF THE LINE in WRITE_TIMEOUT_S: nothing went out, a
        LinkError (exit 1, nothing ran). ONE THAT TAKES PART OF IT and then nothing: the
        board drops a frame cut short, but whether it ran is not established here, so
        the outcome is 'unknown', with the ticket (V120-F3; 1.20 waited on such a port
        for ever).

        A PORT ERROR ONCE THE LINE MAY HAVE GONE OUT (V114-F7) makes the outcome
        'unknown', with the ticket, so cli-cancel can find out; 1.14 let the OSError
        escape and the ticket was lost. Before that write, nothing can have run, and
        the error is raised as it is.

        ONLY THIS COMMAND'S ANSWERS (image 1.16, review finding V115-F4): its output
        and its answer are taken only if they name its session AND ticket. Numbers
        restart at 1 in every session, and 1.15 matched the number alone, so a late
        answer about another session's ticket 1 could be taken for this one's.

        A PROTOCOL-1 ANSWER ONCE THE LINE MAY HAVE GONE OUT (V115-F7) -- the board went
        back to older firmware -- is outcome 'unknown', with the ticket and why. 1.15
        raised it as "update the board" and exited 1, and the ticket was lost.

        GIVING UP WAITING (image 1.20, review finding V119-F1): once give_up() returns True,
        polled where stop() is once the line may have gone out, the outcome is 'unknown',
        with the ticket. It is polled before each wait, never between an answer and the
        result made from it: an answer that has arrived is the outcome. 1.19 gave up by a
        KeyboardInterrupt, which could land after the answer was decoded, and then the
        answer was lost. Giving up implies the stop: stop() and give_up() are looked at
        together, and the stop is sent before the tool stops waiting (image 1.21, V120-F2):
        1.20 looked at give_up() first, and two Ctrl-Cs between two looks gave up with no
        stop sent, the command left running.
        Each frame has one deadline for starting writes, not a wall-clock bound.
        A failed retry cannot establish non-execution of the earlier send."""
        data = line.encode('ascii', 'strict')
        if not 1 <= len(data) <= CLI_LINE_MAX or any(b < 32 or b > 126 for b in data):
            raise ValueError('a command line is 1-%d printable ASCII characters' % CLI_LINE_MAX)
        self.last_ticket, self.line_sent = None, False
        try:
            session, ticket = self.cli_begin(stop)
        except Stopped:
            res = CliResult(0, 0)
            res.outcome, res.stop_asked = 'not sent', True
            return res
        self.last_ticket = (session, ticket)
        res = CliResult(session, ticket)
        try:
            return self._cli_exchange(res, data, out, stop, give_up)
        except OSError as e:
            res.outcome = 'unknown'
            res.why = 'the serial port failed once the command may have been sent (%s)' % e
            return res

    def _cli_exchange(self, res, data, out, stop, give_up):
        """cli() from the first write of the line to its end."""
        session, ticket = res.session, res.ticket
        ident = struct.pack('<II', session, ticket)
        seq = self._next_seq()
        ours = {seq}                    # the seqs our answers come back with
        frame = encode(CLI, seq, ident + data)
        try:
            self._send(frame, stop)
        except WriteStopped:
            res.outcome, res.stop_asked = 'not sent', True
            return res
        except WriteFailure as e:
            if not e.taken:
                raise LinkError('%s: none of the command line went out, so nothing ran' % e)
            self.line_sent = True
            res.why = ('%s: only part of the command line went out. The board drops a frame '
                       'cut short, but that it did is not established here' % e)
            return res
        self.line_sent = True           # from here, the board may have the line
        sends, started, last = 1, False, self.clock()
        expect, crc, gap = 0, 0xFFFF, False
        cancel_at, aborts = None, 0
        while True:
            now = self.clock()
            # Both looks first, then act on them: giving up implies the stop, so two Ctrl-Cs
            # landing between the two looks still send it (image 1.21, V120-F2).
            asked = stop is not None and stop()
            quit_waiting = give_up is not None and give_up()
            if cancel_at is None and (asked or quit_waiting):
                cancel_at, res.stop_asked = now, True
            if cancel_at is not None and now >= cancel_at:
                if aborts >= ABORT_TRIES:
                    res.why = ('no answer to %d cancel requests: the board may be busy, or '
                               'running it' % aborts)
                    return res
                aseq = self._next_seq()
                ours.add(aseq)
                try:
                    self._send(encode(CLI_ABORT, aseq, ident))
                except WriteFailure as e:
                    res.why = '%s: the stop could not be sent' % e
                    return res
                aborts += 1
                cancel_at = now + ABORT_EVERY_S
            # The second Ctrl-C acted on only here: after the stop has gone out, and before a
            # wait, never between an answer and its result (image 1.21, V120-F2).
            if quit_waiting:
                res.why = ('this tool stopped waiting for its answer (a second Ctrl-C)%s'
                           % (', after asking the board to stop it' if aborts else ''))
                return res
            if cancel_at is not None:
                wait = max(0.0, min(cancel_at - self.clock(), 0.1))
            else:
                left = last + (CLI_QUIET_S if started else CLI_FIRST_S) - now
                if left <= 0:
                    if sends >= CLI_TRIES:
                        # No more sends: ask how it ended (a CLI_ABORT also cancels it if it
                        # never arrived) rather than guess.
                        cancel_at, res.gave_up = now, True
                        continue
                    try:
                        self._send(frame, stop)
                    except WriteStopped:
                        pass
                    except WriteFailure as e:
                        # A wait can count SIGINT and then fail before the writer polls again.
                        if e.taken != 0 or stop is None or not stop():
                            res.why = '%s, sending the command line again' % e
                            return res
                    else:
                        sends += 1
                        self.retries += 1
                        last = self.clock()
                        continue
                    # This copy sent nothing; stop the original ticket.
                    cancel_at, res.stop_asked = now, True
                    continue
                wait = min(left, 0.1)
            for b in self.port.read(wait):
                f = self.dec.push(b, self.clock())
                if not f:
                    continue
                if f.type == (CLI_OUT | RESPONSE):
                    if len(f.payload) < OUT_HDR_BYTES:
                        continue
                    s, t, off = struct.unpack_from(OUT_HDR, f.payload)
                    if (s, t) != (session, ticket):
                        continue                # another command's: another client, a late
                                                # one, or the same number in another session
                    started, last = True, self.clock()
                    text = bytes(f.payload[OUT_HDR_BYTES:])
                    if off > expect:
                        gap = True              # a frame was lost: its bytes never come
                    elif off < expect:
                        text = text[expect - off:]   # already had these
                        off = expect
                    if text:
                        crc = crc16(text, crc)
                        res.got += len(text)
                        expect = off + len(text)
                        if out is not None:
                            out(text.decode('ascii', 'replace'))
                elif f.type in (CLI | RESPONSE, CLI_ABORT | RESPONSE) and f.seq in ours:
                    if len(f.payload) == 1 and f.payload[0] == E_LINK_UNKNOWN:
                        # The line went to newer firmware, or did not get there; this board
                        # cannot say which (V115-F7).
                        res.why, res.no_cli_now, res.lost = NO_CLI_NOW, True, True
                        return res
                    if len(f.payload) < RESULT_BYTES:
                        continue
                    st, fl, s, t, nbytes, ocrc = struct.unpack_from(RESULT, f.payload)
                    if (s, t) != (session, ticket):
                        continue                # not this command's (V115-F4)
                    return self._cli_ended(res, st, fl, nbytes, ocrc, expect, crc, gap,
                                           first_only=(sends == 1 and cancel_at is None))

    @staticmethod
    def _cli_ended(res, st, fl, nbytes, ocrc, expect, crc, gap, first_only):
        res.status, res.flags, res.out_bytes = st, fl, nbytes
        if fl & F_CANCELLED:
            res.outcome = 'cancelled'
        elif st in (E_LINK_SESSION, E_LINK_STALE):
            # Nothing ran NOW. If this answers the one and only send, nothing ran at all;
            # otherwise an earlier copy may have run, before a restart or before the board's
            # record moved on.
            if first_only:
                res.outcome = 'not run'
            else:
                res.why = ('the board has restarted since the command was sent'
                           if st == E_LINK_SESSION else
                           'the board\'s record no longer holds this ticket (%s)' % err_name(st))
                res.lost = True
        elif st:
            res.outcome = 'not run'
            if st == E_LINK_PI_RULE:
                # The board printed why it refused; whether that arrived (V114-N1).
                res.complete = not gap and expect == nbytes and crc == ocrc
        else:
            res.outcome = 'done'
            res.complete = not gap and expect == nbytes and crc == ocrc
        return res

    def cli_cancel(self, session, ticket, stop=None):
        """CLI_ABORT for a ticket, as a request: cancels it if it has not run, or reports
        how it ended. Only an answer that names this session AND ticket is taken (image
        1.16, review finding V115-F4; 1.15 matched the ticket number alone, and a late
        "cancelled" for the same number in a later session was taken for the answer
        about a command that had run; 1.14 took any answer with the request's seq).
        No such answer -- none at all, or the command is running and the board took this
        as a stop -- is an 'unknown' outcome. So is a protocol-1 board (V115-F7: 1.15
        raised "update it", exit 1): firmware that old cannot say how a ticket ended.
        So is a stop() before the answer (a Ctrl-C: image 1.20)."""
        def ours(p):
            return (len(p) == 1 and p[0] == E_LINK_UNKNOWN) or \
                   (len(p) >= RESULT_BYTES and struct.unpack_from('<II', p, 2) == (session, ticket))
        res = CliResult(session, ticket)
        try:
            r = self.request(CLI_ABORT, struct.pack('<II', session, ticket), stop=stop, accept=ours)
        except Stopped:
            res.why = 'the query was interrupted (Ctrl-C) before the board\'s answer'
            return res
        except LinkError:
            res.why = ('no answer for this ticket (while it is still running the board takes '
                       'this as a stop, and answers once it has ended)')
            return res
        except OSError as e:
            res.why = 'the serial port failed (%s)' % e
            return res
        if len(r) == 1 and r[0] == E_LINK_UNKNOWN:
            res.why, res.no_cli_now, res.lost = NO_CLI_NOW, True, True
            return res
        st, fl, _, _, nbytes, ocrc = struct.unpack_from(RESULT, r)
        res.status, res.flags, res.out_bytes = st, fl, nbytes
        if fl & F_CANCELLED:
            res.outcome = 'cancelled'
        elif st in (E_LINK_SESSION, E_LINK_STALE):
            res.why = ('the board has restarted since' if st == E_LINK_SESSION
                       else 'the board\'s record no longer holds this ticket')
            res.lost = True
        elif st:
            res.outcome = 'not run'
        else:
            res.outcome = 'done'
        return res


# ---------------------------------------------------------------------------
# Ctrl-C (image 1.20, review finding V119-F1)
# ---------------------------------------------------------------------------
# From the start of main(), SIGINT does not raise: its handler counts it, and nothing
# more. The count is read only while an outcome is still to be had -- by a Stop, polled
# while a request waits for its answer, while a write waits for the port to take a frame's
# first byte (image 1.21), in the CLI's wait for its end, before an update's apply goes
# out, and as a prompt begins to wait -- and, once decide() has the exit status, never.
# So a Ctrl-C
# cannot land between an answer and its report: there is nothing for it to interrupt.
# 1.19 let SIGINT raise KeyboardInterrupt anywhere until a hold() after the answer, and
# one that landed in between replaced the answer: a cli-cancel's or a cli's (UNKNOWN),
# an accepted apply's and a ping's (a traceback). Its terminal errors had no hold at all.
#
# The one place a Ctrl-C raises is a prompt (the update's [y/N], the shell's pi>), while
# the tool waits on stdin and nothing is decided: there it raises Interrupted, which only
# the prompt's caller catches, as no answer.

RC_INTERRUPTED = 130      # ping, info, status, hist, upd-status: a Ctrl-C before the answer


class Interrupted(Exception):
    """A Ctrl-C at a prompt: no answer."""


class _CtrlC:
    count = 0             # Ctrl-Cs since quiet_ctrl_c()
    at_prompt = False     # waiting at a prompt: a Ctrl-C there raises Interrupted, once
    decided = None        # the exit status, once decide() has it


def _on_sigint(signum, frame):
    _CtrlC.count += 1
    if _CtrlC.at_prompt:
        _CtrlC.at_prompt = False
        raise Interrupted()


def quiet_ctrl_c():
    """From here SIGINT is only counted (at a prompt it raises Interrupted). main() calls
    this first. Outside the main thread Python's own handling stays."""
    _CtrlC.count, _CtrlC.at_prompt, _CtrlC.decided = 0, False, None
    try:
        signal.signal(signal.SIGINT, _on_sigint)
    except ValueError:                       # not the main thread
        pass


def decide(rc):
    """The exit status, decided. Each terminal outcome of a run passes here once, before
    it is said: from here no Stop is acted on, and main() returns this status whatever
    follows -- what is said after is only said (image 1.20, review finding V119-F1). The
    first decision stands."""
    if _CtrlC.decided is None:
        _CtrlC.decided = rc


class Stop:
    """Is a stop asked? The Ctrl-Cs counted since base (by default, since this was made):
    one asks to stop, two to give up waiting. Never once the exit status is decided."""

    def __init__(self, base=None):
        self.base = _CtrlC.count if base is None else base

    def asked(self, n):
        return _CtrlC.decided is None and _CtrlC.count - self.base >= n

    def __call__(self):
        return self.asked(1)

    def give_up(self):
        return self.asked(2)


# ---------------------------------------------------------------------------
# Saying what happened (image 1.19, V118-F1; image 1.20, V119-F2)
# ---------------------------------------------------------------------------
# Getting an outcome and saying it are two steps. Once the tool has the outcome, nothing
# after changes the exit: not a stdout or stderr that cannot be written, not a port that
# will not close, not a diagnostic about either, not the interpreter's flush at exit.
# Nor may saying it hold anything up without bound. 1.19 wrote each line and waited for
# it, so a stdout that blocked -- open, but not read -- held up what came after, an
# update's shutdown request among it, for as long as it blocked, with Ctrl-C ignored
# (V119-F2). Now each standard stream is written by a thread of its own. The tool waits
# for a line until the stream has taken nothing for WRITE_WAIT_S, and then gives the
# stream up, as a closed one, and never writes to it again. A command's output is queued
# as it streams, and not waited for until the command has ended.

WRITE_WAIT_S = 2.0        # a stream that takes nothing for this long is given up; a healthy
                          # one takes a line in well under a millisecond
WRITE_CHUNK = 4096        # the most the writer hands a stream at once: what it took is seen
                          # each time it takes one, not only when a whole item has gone
                          # (image 1.21, review note V120-N1)


class _Writer:
    """What this tool writes to one stream.

    A stream with a file descriptor -- this process's stdout or stderr: a terminal, a
    pipe, a file -- is written by a thread of its own, with os.write(), past the stream's
    buffer. A write that blocks holds up only that thread; the buffer stays empty, so the
    interpreter's flush at exit has nothing to write or to block on; and the thread is a
    daemon, which the tool does not wait for at exit. A stream with no descriptor (a
    test's) is written directly. A character the stream cannot encode is written escaped.

    put() and drain() wait while the stream takes something at least every WRITE_WAIT_S:
    the thread writes WRITE_CHUNK bytes at most at a time, and each chunk taken counts
    (image 1.21, review note V120-N1: 1.20 counted only whole items, and gave up a pipe
    that was taking a long one steadily). If it takes nothing for that long, it is given
    up (stalled): nothing more is written to it, and a line it was taking may still reach
    it later. A thread that cannot be started (the process is out of threads or memory)
    makes the stream unavailable at once, as a closed one is (image 1.21, V120-F4: 1.20
    let that RuntimeError out of put())."""

    def __init__(self, stream):
        self.stream, self.dead, self.stalled = stream, False, False
        try:
            self.fd = stream.fileno()
        except (AttributeError, OSError, ValueError):     # io.UnsupportedOperation is both
            self.fd = None
        self.encoding = getattr(stream, 'encoding', None) or 'utf-8'
        self.queued = self.written = 0       # items handed to the thread; items it wrote
        self.since = 0.0                     # when it last took a chunk, or had one to start
        self.queue = collections.deque()
        self.cv = threading.Condition()
        self.thread = None

    def _direct(self, text):
        try:
            try:
                self.stream.write(text)
            except UnicodeEncodeError:
                self.stream.write(text.encode('ascii', 'backslashreplace').decode('ascii'))
            self.stream.flush()
            return True
        except (OSError, ValueError):
            self.dead = True
            return False

    def _run(self):
        """The thread: the queue, in order, until the stream fails or is given up."""
        while True:
            with self.cv:
                while not self.queue and not self.dead:
                    self.cv.wait()
                if self.dead:
                    return
                data = self.queue[0]
            try:
                while True:
                    data = data[os.write(self.fd, data[:WRITE_CHUNK]):]
                    if not data:
                        break
                    with self.cv:
                        if self.dead:        # given up while that chunk was blocked
                            return
                        self.since = time.monotonic()    # it took something
                        self.cv.notify_all()
            except OSError:
                with self.cv:
                    self.dead = True
                    self.cv.notify_all()
                return
            with self.cv:
                if self.dead:                # given up while this write was blocked
                    return
                self.queue.popleft()
                self.written += 1
                self.since = time.monotonic()
                self.cv.notify_all()

    def put(self, text, wait=True):
        """text to the stream. wait: True once it is written, False if the stream fails
        or is given up. Not wait: True once it is queued, False if the stream is gone --
        or given up here, for having had something waiting WRITE_WAIT_S untaken."""
        if self.dead:
            return False
        if self.fd is None:
            return self._direct(text)
        try:
            data = text.encode(self.encoding, 'backslashreplace')
        except LookupError:                  # a stream naming an encoding Python lacks
            data = text.encode('utf-8', 'backslashreplace')
        with self.cv:
            now = time.monotonic()
            if self.queue and now - self.since > WRITE_WAIT_S:
                return self._give_up()
            if self.thread is None:
                t = threading.Thread(target=self._run, name='pitrac-link-writer', daemon=True)
                try:
                    t.start()
                except Exception:            # RuntimeError: can't start new thread; MemoryError
                    self.dead = True         # unavailable, as a closed stream is; nothing
                    self.cv.notify_all()     # was queued, so nothing waits on a thread
                    return False
                self.thread = t
            if not self.queue:
                self.since = now
            self.queue.append(data)
            self.queued += 1
            n = self.queued
            self.cv.notify_all()
            return self._until(n) if wait else True

    def drain(self):
        """Wait until all that is queued is written, or the stream is gone or given up."""
        if self.fd is not None:
            with self.cv:
                self._until(self.queued)

    def _until(self, n):
        """With self.cv held: wait until item n is written. False if the stream fails, or
        takes nothing for WRITE_WAIT_S: then it is given up."""
        while self.written < n:
            if self.dead:
                return False
            left = self.since + WRITE_WAIT_S - time.monotonic()
            if left <= 0:
                return self._give_up()
            self.cv.wait(left)
        return True

    def _give_up(self):
        self.dead = self.stalled = True
        self.cv.notify_all()
        return False


_WRITERS = []             # [(stream, _Writer)]: one for each stream this run writes to


def _writer(stream):
    for s, w in _WRITERS:
        if s is stream:
            return w
    w = _Writer(stream)
    _WRITERS.append((stream, w))
    return w


def _put(stream, text, wait=True):
    """text to stream (_Writer.put). False for no stream at all (Python has none when the
    process starts without that descriptor)."""
    if stream is None:
        return False
    return _writer(stream).put(text, wait)


def _say(text, err=False):
    """One line of what happened: to stdout, or to stderr if err; to the other stream if
    that one fails, saying so. Returns whether it was written anywhere; with both streams
    gone, nothing can be, and the exit status is all that is left."""
    first, second = (sys.stderr, sys.stdout) if err else (sys.stdout, sys.stderr)
    if _put(first, text + '\n'):
        return True
    return _put(second, 'pitrac-link: (%s could not be written) %s\n'
                % ('stderr' if err else 'stdout', text))


def _out(text=''):
    """A line to stdout (this tool's print)."""
    _say(text)


def _err(text):
    """A line to stderr."""
    _say(text, err=True)


def _ask(prompt, stop=None):
    """A line from stdin, after the prompt. A Ctrl-C while it waits raises Interrupted:
    the one place SIGINT raises (quiet_ctrl_c()). So does one counted before the wait
    began, if stop() says it was (image 1.21, review note V120-N2: in 1.20 a Ctrl-C
    between the prompt's write and the wait was counted, and the wait went on for a line).
    EOFError at the end of input, for a stdin that fails, and for none at all (image 1.20,
    review note V119-N1: 1.19 raised AttributeError there, and an update's staged image
    was left behind)."""
    if not _put(sys.stdout, prompt):
        _put(sys.stderr, prompt)
    stdin = sys.stdin
    if stdin is None:
        raise EOFError()
    _CtrlC.at_prompt = True
    try:
        # From the flag on, a Ctrl-C raises; one before it is seen here.
        if stop is not None and stop():
            raise Interrupted()
        line = stdin.readline()
    except (OSError, ValueError):
        raise EOFError()
    finally:
        _CtrlC.at_prompt = False
    if not line:
        raise EOFError()
    return line.rstrip('\r\n')


class CommandOutput:
    """A command's output as it streams, to stdout: queued, and not waited for while the
    command runs, so a stdout that blocks holds up neither the command nor its stop (image
    1.20, V119-F2). If stdout cannot take it -- closed, or taking nothing for WRITE_WAIT_S
    -- the command is not disturbed: the tool listens to the board to the end, and the exit
    is still the board's. drain() then waits for stdout to take what is queued, and
    report() says on stderr how much went nowhere. 1.18 let a stdout error out of the read
    loop, where it was taken for the serial port failing."""

    def __init__(self):
        self.lost = 0             # characters that went nowhere
        self.queued = []          # [(writer, item, characters)] not yet known to be written

    def __call__(self, text):
        stream = sys.stdout
        if not _put(stream, text, wait=False):
            self.lost += len(text)
            return
        w = _writer(stream)
        if w.fd is not None:
            self.queued.append((w, w.queued, len(text)))

    def drain(self):
        for w in {id(w): w for w, _, _ in self.queued}.values():
            w.drain()
        self.lost += sum(c for w, n, c in self.queued if n > w.written)
        self.queued = []

    def report(self, err=_err):
        if self.lost:
            err('pitrac-link: stdout could not be written (closed, or taking nothing for '
                '%g s): %d characters of the command\'s output went nowhere (from a stdout '
                'that stalled, the piece it was taking may still appear). The exit status is '
                'still the command\'s; the board\'s USB console printed all of it'
                % (WRITE_WAIT_S, self.lost))


# ---------------------------------------------------------------------------
# The update
# ---------------------------------------------------------------------------
class UpdateError(Exception):
    """An update that cannot go on. nothing_staged: the board refused this run's BEGIN,
    so it holds none of this image (and what it held before, by the check before the
    BEGIN, was nothing)."""

    def __init__(self, text, nothing_staged=False):
        super().__init__(text)
        self.nothing_staged = nothing_staged


class ApplyUnknown(UpdateError):
    """No conclusive board reply and no proof that every APPLY sent zero."""

    def __init__(self):
        super().__init__('Apply outcome unknown; nothing was dropped. Check upd-status.')


POWEROFF = ['systemctl', 'poweroff']   # this Pi's shutdown, as the tool requests it
POWEROFF_WAIT_S = 10.0                  # how long it waits to hear how the request went


def check_image(data, cap=None):
    """Local checks before anything is sent. Returns the parsed IMAGE_DEF."""
    img = parse_image_def(data)
    if not img:
        raise UpdateError('no IMAGE_DEF in the first 4 KB: not an RP2350 image (use build/pitrac.bin)')
    if not img['exe_arm_s_rp2350']:
        raise UpdateError('not an RP2350 Arm Secure executable (IMAGE_TYPE 0x%04X)' % img['type'])
    if not img['tbyb']:
        raise UpdateError('not flagged try-before-you-buy (built with PITRAC_TBYB=OFF?): the board '
                          'refuses it, because a broken image could not roll back')
    if cap is not None and len(data) > cap:
        raise UpdateError('%d B: the board can stage at most %d B' % (len(data), cap))
    return img


def send_image(link, data, out=_out, stop=None):
    """BEGIN, DATA, END. Returns the version the board read from the staged image.
    Stopped once stop() is True, before the next request goes out or while one waits."""
    digest = hashlib.sha256(data).digest()
    st = link.begin(len(data), digest, stop=stop)
    if st:
        raise UpdateError('UPD_BEGIN: %s -- %s' % (err_name(st), EXPLAIN.get(err_name(st), '')),
                          nothing_staged=True)
    t0, at, shown = time.monotonic(), 0, -1
    while at < len(data):
        chunk = data[at:at + CHUNK]
        st, nxt = link.data(at, chunk, stop=stop)
        if st == ERRORS.index('OFFSET') and nxt <= len(data):
            at = nxt                      # resynchronise on what the board has
            continue
        if st:
            raise UpdateError('UPD_DATA at %d: %s -- %s' % (at, err_name(st),
                                                            EXPLAIN.get(err_name(st), '')))
        at = nxt
        pct = 100 * at // len(data)
        if pct // 10 != shown:
            shown = pct // 10
            out('sending  : %3d%%' % pct)
    st, ver = link.end(stop=stop)
    if st:
        raise UpdateError('UPD_END: %s -- %s' % (err_name(st), EXPLAIN.get(err_name(st), '')))
    out('sent     : %d B in %.1f s, %d retries; the board checked the SHA-256 and the image'
        % (len(data), time.monotonic() - t0, link.retries))
    return ver


def request_poweroff():
    """This Pi's shutdown, requested directly. The board's own request reaches systemd
    through the gpio-shutdown overlay; this is the same request, for a Pi without it.
    Made once the apply is accepted, before a line is said (image 1.20, review finding
    V119-F2: 1.19 wrote two lines first, and a stdout that blocked held the request up
    for as long as it blocked). In a session of its own, so a Ctrl-C at the terminal
    does not reach it; its output never goes to this tool's streams. Returns what
    happened, to be said."""
    cmd = ' '.join(POWEROFF)
    try:
        p = subprocess.Popen(POWEROFF, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                             stderr=subprocess.PIPE, start_new_session=True)
    except OSError as e:
        return 'could not run `%s`: %s' % (cmd, e)
    try:
        _, why = p.communicate(timeout=POWEROFF_WAIT_S)
    except subprocess.TimeoutExpired:
        return '`%s` was started; it had not finished after %g s' % (cmd, POWEROFF_WAIT_S)
    if p.returncode == 0:
        return 'requested (`%s`)' % cmd
    return '`%s` failed (exit %d): %s' % (cmd, p.returncode,
                                          why.decode('utf-8', 'replace').strip())


def _drop(link):
    """UPD_ABORT, waited for: drop the image this run began sending. Called only once this
    run's BEGIN may have reached the board, which was idle when this run looked just
    before it. With one actual UART producer, what the board holds then is this run's or nothing
    (image 1.21, review finding V120-F1: the abort is not scoped to an image, and 1.20 sent
    it before any BEGIN had gone out, dropping an earlier update's verified or armed
    image). Returns what happened, to be said."""
    try:
        st = link.abort()
    except (LinkError, OSError) as e:
        return ('asking the board to drop what this run sent failed (%s): `pitrac-link '
                'upd-status` shows what it holds, and `pitrac-link abort` drops it' % _what(e))
    return ('what the board had of this image was dropped' if st == 0 else
            'the board did not drop what this run sent (abort: %s)' % err_name(st))


def _not_applied(link, out, why, begun):
    """No apply went out, and none will. If this run's BEGIN may have reached the board,
    what it holds is this run's, and it is dropped (_drop()). Otherwise nothing is sent:
    what the board holds is not this run's to drop. Exit 1."""
    if not begun:
        decide(1)
        out('%s; none of the image was sent' % why)
        return 1
    dropped = _drop(link)
    decide(1)
    out('%s; %s' % (why, dropped))
    return 1


def cmd_update(link, args, out=_out, ask=None, stop=None):
    """Send, check and install an image.

    THE BOARD'S UPDATE STATE IS ASKED FIRST (image 1.21, review finding V120-F1). If it
    holds an update already -- receiving, verified, armed or installing -- this run stops
    there, sending none of its image and dropping nothing: that update is not this run's.
    From this run's BEGIN on, with one actual producer, the board may hold this
    run's staging or nothing. Cleanup is attempted if the update goes no further: a stop, a 'no',
    an error in the transfer, a refused apply. Before the BEGIN, nothing is dropped. 1.20
    counted the update as begun before the BEGIN went out, and a Ctrl-C there sent an
    abort that dropped an earlier update's verified or armed image.

    A stop observed at the last check before the first APPLY write, or anything but a 'yes' at the prompt
    (a Ctrl-C there is no answer, also one counted just before the prompt waits) means it
    is never written: the exit is 1, not applied. Once the port has taken any of it, the
    apply is waited for, and its answer decides (image 1.20, review finding V119-F1). An
    apply whose answer is not heard may have been taken: nothing is dropped then, as that
    would stop an install. After an accepted apply, this Pi's shutdown is requested
    first, then said (V119-F2)."""
    stop = stop if stop is not None else (lambda: False)
    if ask is None:
        def ask(question):
            return _ask(question, stop)
    data = load_image(args.image)
    img = check_image(data)
    new = (img['version'][0] << 16) | img['version'][1]
    out('image    : %s, %d B, version %s, try-before-you-buy, SHA-256 %s'
        % (args.image, len(data), vstr(new), hashlib.sha256(data).hexdigest()[:16]))
    mark = None                             # bytes_out just before the BEGIN

    def begun():
        return mark is not None and link.bytes_out > mark
    try:
        ping = link.ping(stop=stop)
        if ping['proto'] not in UPDATE_PROTOS:
            raise UpdateError('the board speaks protocol %d; this tool updates over %s'
                              % (ping['proto'], ' or '.join(str(p) for p in UPDATE_PROTOS)))
        info = link.info(stop=stop)
        if not info['partitioned']:
            raise UpdateError(EXPLAIN['NO_SLOT'])
        check_image(data, min(info['capacity'], info['other_size']))
        out('board    : version %s (built %s) at 0x%06X; installs into 0x%06X'
            % (vstr(info['version']), info['stamp'], info['run_start'], info['other_start']))
        u = link.upd_status(stop=stop)
        if u['state']:
            state = STATES[u['state']] if u['state'] < len(STATES) else '?'
            raise UpdateError(
                'the board already holds an update (%s%s). This run sends none of its image '
                'and drops nothing: that update is not this run\'s. `pitrac-link upd-status` '
                'shows it; `pitrac-link abort` drops it -- an armed or installing one too, '
                'and then the board does not power this Pi back on after it'
                % (state, ', version %s' % vstr(u['version']) if u['state'] >= 2 else
                   ', %d of %d B' % (u['received'], u['size'])))
        if new <= info['version']:
            out('NOTE     : %s is not newer than %s. It still installs, but if it buys itself, '
                'the boot ROM erases the old image whenever the old one would win the next '
                'normal boot: always for an older version, and for an equal one when the '
                'running image is in A (equal versions boot A). Then there is no fallback.'
                % (vstr(new), vstr(info['version'])))
        mark = link.bytes_out               # from the BEGIN's first byte, what it holds is ours
        ver = send_image(link, data, out, stop)
        if ver != new:
            raise UpdateError('the board read version %s, this file says %s' % (vstr(ver), vstr(new)))
        out('ready    : version %s is staged in the board\'s RAM' % vstr(ver))
        if not args.yes:
            if stop():
                raise Stopped()
            try:
                a = ask('Apply now? The board shuts this Pi down, installs %s, reboots into it '
                        'on trial, and powers this Pi back on (1-2 min). [y/N] ' % vstr(ver))
            except (EOFError, Interrupted, KeyboardInterrupt):
                a = ''                      # no answer is no (image 1.19)
            if a.strip().lower() not in ('y', 'yes'):
                return _not_applied(link, out, 'not applied', True)
    except Stopped:
        return _not_applied(link, out, 'not applied: interrupted (Ctrl-C) before the apply '
                            'went out', begun())
    except (UpdateError, LinkError, OSError) as e:
        # Said by _run(), exit 1. This run's image, if the board may hold some, is dropped.
        if begun() and not getattr(e, 'nothing_staged', False):
            raise UpdateError('%s. Not applied: %s' % (e, _drop(link))) from e
        raise
    at, retries_before_apply = link.bytes_out, link.retries
    try:
        st = link.apply(stop)               # waited for once taken: its answer decides
    except Stopped:                         # the port had taken none of it: never sent
        return _not_applied(link, out, 'not applied: interrupted (Ctrl-C) before the apply '
                            'went out', True)
    except (LinkError, OSError) as e:
        if isinstance(e, WriteFailure) and link.bytes_out == at:
            raise UpdateError('%s. Not applied: the apply never went out; %s'
                              % (e, _drop(link))) from e
        raise ApplyUnknown() from e
    if st:
        # A refusal of a retry only describes that copy (e.g. staging was cleared
        # after a first accepted APPLY with a lost ACK).
        if link.retries != retries_before_apply:
            raise ApplyUnknown()
        raise UpdateError('UPD_APPLY: %s -- %s. Not applied: %s'
                          % (err_name(st), EXPLAIN.get(err_name(st), ''), _drop(link)))
    decide(0)
    how = None if args.no_poweroff else request_poweroff()    # before any line (V119-F2)
    out('accepted : the board accepted APPLY. When the Pi is back, check:')
    out('           pitrac-link upd-status')
    if how is not None:
        out('poweroff : %s' % how)
    return 0


# ---------------------------------------------------------------------------
# A query's answer is decided as it arrives; a Ctrl-C before it is Stopped (_run: 130).
def show_ping(link, args, stop=None):
    p = link.ping(stop=stop)
    decide(0)
    _out('board    : PiTrac RP2354, protocol %d, version %s' % (p['proto'], vstr(p['version'])))


def show_info(link, args, stop=None):
    i = link.info(stop=stop)
    decide(0)
    _out('version  : %s   built %s' % (vstr(i['version']), i['stamp']))
    trial = (', on trial: %s' % ('bought itself' if i['bought'] else 'NOT bought')) if i['trial'] else ''
    _out('boot     : %s boot%s' % (BOOT_TYPES.get(i['boot_type'] & 0x7F, '?'), trial))
    if i['partitioned']:
        me = 'A' if i['run_start'] < i['other_start'] else 'B'
        _out('layout   : partition %s at 0x%06X (%d KB); partner at 0x%06X (%d KB)'
             % (me, i['run_start'], i['run_size'] // 1024, i['other_start'], i['other_size'] // 1024))
    else:
        _out('layout   : no partition table (flat at 0x%06X): updates need the one-time '
             'conversion over USB (BENCH_P8_PI.md 8.7)' % i['run_start'])
    _out('staging  : %d KB of RAM' % (i['capacity'] // 1024))


def show_status(link, args, stop=None):
    s = link.status(stop=stop)
    decide(0)
    state = POWER[s['state']] if s['state'] < len(POWER) else '?'
    fault = FAULTS[s['fault']] if s['fault'] < len(FAULTS) else '?'
    _out('power    : %s (%.1f s), fault %s' % (state, s['state_ms'] / 1000.0, fault))
    _out('5V_IN    : %s' % ('not read' if s['mv'] == 0xFFFF else '%.3f V' % (s['mv'] / 1000.0)))
    _out('flags    : Pi present %s, latch %s, rails ready %s'
         % (s['pi_present'], 'closed' if s['latch'] else 'open', s['rails']))
    _out('uptime   : %.1f s' % (s['uptime_ms'] / 1000.0))


def show_hist(link, args, stop=None):
    w = link.hist(stop=stop)
    decide(0)
    for k, name in enumerate(HIST_NAMES):
        _out('  %-38s %10d' % (name, w[k]))
    if w[8]:
        f = FAULTS[w[9]] if w[9] < len(FAULTS) else '?'
        _out('last fault: %s, %d s into boot %d' % (f, w[10], w[11]))


def show_upd_status(link, args, stop=None):
    s = link.upd_status(stop=stop)
    decide(0)
    state = STATES[s['state']] if s['state'] < len(STATES) else '?'
    line = 'staged   : %s' % state
    if s['state'] == 1:
        line += ', %d of %d B' % (s['received'], s['size'])
    elif s['state'] >= 2:
        line += ', version %s, %d B' % (vstr(s['version']), s['size'])
    _out(line)
    if s['error']:
        _out('last err : %s -- %s' % (err_name(s['error']), EXPLAIN.get(err_name(s['error']), '')))
    if s['result']:
        res = RESULTS[s['result']] if s['result'] < len(RESULTS) else '?'
        _out('update   : %s, %s -> %s' % (res, vstr(s['from']), vstr(s['to'])))
        _out('           %s' % result_detail(s['result'], s['detail']))
    else:
        _out('update   : none since the board was last powered on')


def do_abort(link, args, stop=None):
    """An abort is waited for, Ctrl-C or not: its answer says what the board did."""
    st = link.abort()
    decide(0)
    _out('aborted' if st == 0 else 'abort: %s' % err_name(st))


# ---------------------------------------------------------------------------
# The board's CLI over the link (protocol 4)
# ---------------------------------------------------------------------------
# Ctrl-C while a command runs (a Stop): the first sends CLI_ABORT for its ticket -- the
# board stops it, as a key does at its USB console, or cancels it if it has not started
# -- and the command is never sent again; the second gives up waiting. Both are seen
# only where Link.cli() polls them, never once an answer has arrived.

def _unknown_hint(ident, lost=False):
    """What can still be found out. A board whose record of the ticket is gone -- it
    restarted, on whatever firmware, or its record moved on -- cannot be asked: 1.16 or
    later starts every boot with a new session and an empty record, so going back to it
    does not bring the record back (image 1.17, review note V116-N2; 1.16 said to ask
    again once it ran 1.16)."""
    if lost:
        return ('No query can recover it: the board no longer holds a record of this ticket, '
                'and the board starts each boot with a new, empty one, on any firmware. The board\'s '
                'USB console printed `[pi] <command>` for every command the Pi ran: a log of '
                'it, if one was kept, is the only record')
    return ('`pitrac-link cli-cancel %s` reports how it ended, or cancels it if it has not '
            'run' % ident)


def parse_ticket(text):
    """SESSION:TICKET, as an UNKNOWN outcome prints it (e.g. 5eed0001:12): (session,
    ticket). ValueError if it is not one."""
    s, t = text.split(':')
    session, ticket = int(s, 16), int(t, 10)
    if not (0 <= session <= 0xFFFFFFFF and 0 <= ticket <= 0xFFFFFFFF):
        raise ValueError(text)
    return session, ticket


def _what(e):
    """An exception for a message, with its class: a transport failure (OSError,
    LinkError) is then told from a defect in this tool (image 1.19, a note in the 1.18
    review)."""
    return '%s: %s' % (type(e).__name__, e)


def _recovery_unknown(ident, why, err=_err):
    """cli-cancel could not ask, or did not hear: the command's outcome stays UNKNOWN,
    exit 5, with its ticket (image 1.17, review finding V116-F4: 1.16 exited 2 for a
    port it could not open and 1 for a busy one, with no ticket -- as if a new command
    had not been sent, when the command asked about may already have run). Decided,
    then said."""
    decide(RC_UNKNOWN)
    err('pitrac-link: outcome UNKNOWN for ticket %s: %s. Whether it ran is not known; %s.'
        % (ident, why, _unknown_hint(ident)))
    return RC_UNKNOWN


def cli_verdict(r):
    """The exit code for a CliResult, and the line for stderr (None for a clean run)."""
    if r.outcome == 'not sent':
        return RC_CANCELLED, '(stopped before the command was sent: nothing ran)'
    if r.outcome == 'cancelled':
        return RC_CANCELLED, ('(cancelled before it ran: nothing ran%s)'
                              % (' -- the board had not answered, so this tool cancelled it'
                                 if r.gave_up else ''))
    if r.outcome == 'unknown':
        return RC_UNKNOWN, ('pitrac-link: outcome UNKNOWN for ticket %s: %s. %s.'
                            % (r.ident(), r.why or 'no answer', _unknown_hint(r.ident(), r.lost)))
    if r.outcome == 'not run':
        if r.status == E_LINK_PI_RULE:
            # The board printed why. If that text was lost, say so (image 1.15, review
            # note V114-N1: 1.14 exited 3 with nothing on either stream).
            if r.complete:
                return RC_PI_RULE, None
            return RC_PI_RULE, ('pitrac-link: not run: refused by the Pi-only power rule, and '
                                'the board\'s explanation was lost (%d of %d bytes arrived). '
                                'From the Pi, forceoff, reset force, bootsel force, adc5vcal '
                                '<volts> and a fault clear that drops the rail need a trailing '
                                'confirm; off runs only while the board is RUNNING this Pi'
                                % (r.got, r.out_bytes or 0))
        if r.status == E_LINK_LENGTH:
            return RC_NOT_RUN, ('pitrac-link: the board refused the line: empty, over %d '
                                'characters, or not printable ASCII. Nothing ran' % CLI_LINE_MAX)
        return RC_NOT_RUN, 'pitrac-link: not run: the board answered %s' % err_name(r.status)
    if not r.complete:
        rc, text = RC_INCOMPLETE, ('pitrac-link: INCOMPLETE OUTPUT: %d of the %d bytes it printed '
                                   'arrived. It ran; what it printed is not all here (the '
                                   'board\'s USB console has it all)' % (r.got, r.out_bytes or 0))
    elif r.flags & F_STOPPED:
        rc, text = RC_STOPPED, '(stopped: it ended early)'
    elif r.flags & F_STOP_ASKED:
        rc, text = RC_RAN_TO_END, ('(a stop was asked, but it ran to its end: it does not stop '
                                   'part-way, or the stop came too late)')
    elif r.stop_asked:
        # This tool sent the stop, and the board had finished before it arrived (image 1.15,
        # review finding V114-F6: 1.14 exited 0, as if no stop had been asked).
        rc, text = RC_RAN_TO_END, '(a stop was sent, but it had already finished: it ran to its end)'
    else:
        rc, text = RC_OK, None
    if r.flags & F_REPEAT:
        text = ('(answered from the board\'s record: it had already run, and did not run '
                'again; its output is not sent again)' + ('\n' + text if text else ''))
    return rc, text


def cli_exit(r, err=_err):
    """The exit code for a CliResult, with its line said on stderr."""
    rc, text = cli_verdict(r)
    if text:
        err(text)
    return rc


def cli_outcome(link, line, write=None, stop=None):
    """One command, its output to write() as it comes: (exit code, the line for stderr or
    None). stop: a Stop, or any callable (then there is no giving up)."""
    try:
        r = link.cli(line, out=write, stop=stop, give_up=getattr(stop, 'give_up', None))
    except ValueError as e:
        return RC_NOT_RUN, 'pitrac-link: %s' % e
    return cli_verdict(r)


def run_cli(link, line, write=None, err=_err, stop=None):
    """One command, as the shell runs it: the exit code (RC_*, the module docstring), its
    line said on stderr."""
    rc, text = cli_outcome(link, line, write, stop)
    if text:
        err(text)
    return rc


def cmd_cli(link, args, stop=None, err=_err):
    """`pitrac-link cli`: the outcome decided, then the output drained and the outcome said."""
    output = CommandOutput()
    rc, text = cli_outcome(link, ' '.join(args.words), output,
                           stop if stop is not None else Stop())
    decide(rc)
    output.drain()
    if text:
        err(text)
    output.report(err)
    return rc


def cancel_answer(r):
    """cli-cancel's exit code and words for how the command ended: (rc, text, for stderr).
    An UNKNOWN goes to stderr, as every other UNKNOWN does (image 1.19)."""
    if r.outcome == 'cancelled':
        return RC_CANCELLED, 'cancelled: it had not run, and now never will', False
    if r.outcome == 'unknown':
        return RC_UNKNOWN, ('pitrac-link: outcome UNKNOWN for ticket %s: %s; whether it ran '
                            'cannot be told. %s.'
                            % (r.ident(), r.why, _unknown_hint(r.ident(), r.lost))), True
    if r.outcome == 'not run':
        return (RC_NOT_RUN if r.status != E_LINK_PI_RULE else RC_PI_RULE,
                'not run: the board answered %s' % err_name(r.status), False)
    how = ('it ended early on a stop' if r.flags & F_STOPPED else
           'a stop was asked, but it ran to its end' if r.flags & F_STOP_ASKED else
           'it ran to its end')
    rc = RC_STOPPED if r.flags & F_STOPPED else RC_RAN_TO_END if r.flags & F_STOP_ASKED else RC_OK
    return rc, 'it ran: %s; it printed %d bytes (not sent again)' % (how, r.out_bytes), False


def say_cancel_answer(r, out=_out, err=_err):
    """Decide the exit from the answer, then say it (image 1.18, V117-F3; image 1.19,
    V118-F1): the words go to the other stream if theirs fails."""
    rc, text, to_err = cancel_answer(r)
    decide(rc)
    (err if to_err else out)(text)
    return rc


def cmd_cli_cancel(link, args, out=_out, err=_err, stop=None):
    try:
        session, ticket = parse_ticket(args.ticket)
    except ValueError:
        decide(RC_NOT_RUN)
        err('pitrac-link: a ticket is SESSION:TICKET as printed, e.g. 5eed0001:12')
        return RC_NOT_RUN
    return say_cancel_answer(link.cli_cancel(session, ticket, stop=stop), out, err)


def _close(port, err=_err):
    """Close the port. How the command ended is decided by then, and a failure here
    cannot change it: it is said, not raised (image 1.18, review finding V117-F3: a
    close error replaced cli-cancel's exit 5). Nor can a failure to say it (image 1.19,
    V118-F1: 1.18 let that escape main())."""
    try:
        port.close()
    except BaseException as e:
        try:
            err('pitrac-link: (the port did not close cleanly: %r)' % e)
        except BaseException:
            pass


def cli_cancel_process(args, ident, stop=None, err=_err):
    """`pitrac-link cli-cancel` from the ticket checked to the port closed: open, set the
    tty up, ask, close. It asks about a command that may already have run, so whatever
    keeps it from hearing the board's answer -- a port that will not open, is held, is no
    tty or fails, a query that fails, a Ctrl-C before the answer (stop) -- leaves that
    command's outcome UNKNOWN: exit 5, with its ticket (image 1.17, V116-F4; image 1.18,
    V117-F3). An answer once heard is the outcome, decided before it is said (image 1.19,
    V118-F1; image 1.20, V119-F1). The reason for an UNKNOWN names the exception's
    class."""
    try:
        port = SerialPort(args.port)
    except PortBusy as e:
        return _recovery_unknown(ident, 'the query was never sent: %s' % e.strerror, err)
    except Exception as e:                    # OSError, a tty that will not set up, ...
        return _recovery_unknown(ident, 'the query was never sent: cannot open %s: %s'
                                 % (args.port, _what(e)), err)
    try:
        try:
            r = Link(port).cli_cancel(*parse_ticket(args.ticket), stop=stop)
        except Exception as e:
            return _recovery_unknown(ident, 'the query failed: %s' % _what(e), err)
        return say_cancel_answer(r, err=err)
    finally:
        _close(port, err)


def cmd_shell(link, args, read=_ask, write=None, err=_err, stop=None):
    """The board's CLI, a line at a time. Each command as `cli`, with a Stop of its own;
    at the prompt a Ctrl-C gives a fresh prompt. Output that stdout cannot take is
    counted, per command, as for `cli`. Leaves 0 at 'exit' or the end of input."""
    p = link.ping(stop=stop)
    if p['proto'] < CLI_PROTO:
        raise LinkError('this board\'s firmware has no CLI over the link with tickets (protocol '
                        '%d; protocol 4 is image 1.23 on): update it first' % p['proto'])
    _out('board    : version %s, the CLI over the link. Ctrl-C stops a command; '
         '\'exit\' leaves.' % vstr(p['version']))
    while True:
        try:
            line = read('pi> ', Stop()).strip()
        except EOFError:
            decide(0)
            _out()
            return 0
        except (Interrupted, KeyboardInterrupt):
            _out()
            continue
        if not line:
            continue
        if line in ('exit', 'quit'):
            decide(0)
            return 0
        output = CommandOutput() if write is None else None
        try:
            _, text = cli_outcome(link, line, write or output, Stop())
        except LinkError as e:
            text = 'pitrac-link: %s' % e
        if output is not None:
            output.drain()
        if text:
            err(text)
        if output is not None:
            output.report(err)


def main(argv=None):
    """The tool: its exit status. SIGINT is quieted first (quiet_ctrl_c()). Once the exit
    status is decided (decide()), it is what this returns, whatever follows: a defect in
    this tool after it is said, with its traceback, if it can be, and does not change it;
    nor does a failure to say it (image 1.21, review finding V120-F4: in 1.20, with no
    thread to be had for stderr, saying the defect raised again, and the exit was 1)."""
    quiet_ctrl_c()
    ap = argparse.ArgumentParser(prog='pitrac-link', description=__doc__.split('\n')[0])
    ap.add_argument('--port', default=os.environ.get('PITRAC_LINK_PORT', '/dev/ttyAMA0'),
                    help='the UART on header pins 8/10 (default /dev/ttyAMA0)')
    sub = ap.add_subparsers(dest='cmd', required=True)
    for name in ('ping', 'info', 'status', 'hist', 'upd-status', 'abort'):
        sub.add_parser(name)
    up = sub.add_parser('update', help='send, check and install a firmware image')
    up.add_argument('image', help='build/pitrac.bin (or build/pitrac.uf2)')
    up.add_argument('--yes', action='store_true', help='apply without asking')
    up.add_argument('--no-poweroff', action='store_true',
                    help='leave the shutdown to the board\'s request (gpio-shutdown overlay)')
    cp = sub.add_parser('cli', help='run one board CLI command (image 1.23 on)')
    cp.add_argument('words', nargs=argparse.REMAINDER, help='the command, as typed at the USB CLI')
    sub.add_parser('shell', help='the board\'s CLI, interactively')
    cc = sub.add_parser('cli-cancel', help='how a command ended, or cancel it if it has not run')
    cc.add_argument('ticket', help='SESSION:TICKET, as an UNKNOWN outcome printed it')
    args = ap.parse_args(argv)
    if args.cmd == 'cli' and not args.words:
        ap.error('cli needs a command, e.g. pitrac-link cli stat')
    try:
        rc = _run(args)
    except Exception:
        if _CtrlC.decided is None:
            raise
        try:
            _err('pitrac-link: a defect in this tool, after the outcome was decided (the exit '
                 'status stands):\n' + traceback.format_exc().rstrip())
        except Exception:
            pass                            # nothing can be said: the exit is all there is
        rc = _CtrlC.decided
    return rc if _CtrlC.decided is None else _CtrlC.decided


def _run(args):
    """main() once the arguments are parsed: the exit code, decided (decide()) before it
    is said. Every line goes through _out() or _err() (_Writer)."""
    stop = Stop(0)                          # every Ctrl-C since main() started
    if args.cmd == 'update':
        # Before opening the port: a wrong file never reaches the board.
        try:
            check_image(load_image(args.image))
        except (OSError, ValueError, UpdateError) as e:
            decide(2)
            _err('pitrac-link: %s' % e)
            return 2
    # cli-cancel asks about a command that may already have run: if it cannot ask, that
    # command's outcome is UNKNOWN, exit 5, with its ticket -- never a code that says
    # nothing ran (image 1.17, review finding V116-F4). The ticket is checked first, then
    # the whole of the rest is cli_cancel_process()'s (image 1.18, V117-F3).
    if args.cmd == 'cli-cancel':
        try:
            ident = '%08x:%d' % parse_ticket(args.ticket)
        except ValueError:
            decide(RC_NOT_RUN)
            _err('pitrac-link: a ticket is SESSION:TICKET as printed, e.g. 5eed0001:12')
            return RC_NOT_RUN
        return cli_cancel_process(args, ident, stop)
    try:
        port = SerialPort(args.port)
    except PortBusy as e:
        decide(RC_ERROR)
        _err('pitrac-link: %s. Nothing was sent.' % e.strerror)
        return RC_ERROR
    except OSError as e:
        decide(2)
        _err('pitrac-link: cannot open %s: %s (dtparam=uart0=on in config.txt; README.md)'
             % (args.port, e))
        return 2
    link = Link(port)
    handlers = {'ping': show_ping, 'info': show_info, 'status': show_status, 'hist': show_hist,
                'upd-status': show_upd_status, 'abort': do_abort}
    try:
        if args.cmd == 'update':
            return cmd_update(link, args, stop=stop)
        if args.cmd == 'cli':
            return cmd_cli(link, args, stop)
        if args.cmd == 'shell':
            return cmd_shell(link, args, stop=stop)
        handlers[args.cmd](link, args, stop)
        return 0
    except Stopped:
        decide(RC_INTERRUPTED)
        _err('pitrac-link: interrupted (Ctrl-C) before the board answered: nothing to report')
        return RC_INTERRUPTED
    except ApplyUnknown as e:
        decide(1)
        _err(str(e))
        return 1
    except (LinkError, UpdateError, ValueError) as e:
        # A `cli` gets here only before its line went out (no ticket, no CLI on the board,
        # or a port that took none of the line): after, it is UNKNOWN with the ticket
        # (V115-F7). An update's error carries what became of the image it sent (dropped).
        decide(1)
        _err('pitrac-link: %s' % e)
        return 1
    except OSError as e:
        # The port failed. A `cli` whose line may have gone out never gets here: it
        # reports UNKNOWN with its ticket (Link.cli(); image 1.15, V114-F7). Nor does a
        # stdout or stderr that cannot be written: _out() and _err() do not raise for it.
        decide(1)
        _err('pitrac-link: the serial port failed: %s' % e)
        return 1
    finally:
        _close(port)                        # cannot replace the exit decided above


if __name__ == '__main__':
    sys.exit(main())
