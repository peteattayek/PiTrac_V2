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

The board only listens while it powers this Pi and the Pi drives RPI5_ON (GPIO22)
high: pitrac-rpi5-on.service (README.md in this folder).
"""

import argparse
import hashlib
import os
import random
import struct
import subprocess
import sys
import time

# ---------------------------------------------------------------------------
# Framing (src/link_frame.h): 0xA5 | len | type | seq | payload | crc16 LE
# ---------------------------------------------------------------------------
SYNC = 0xA5
MAX_PAYLOAD = 255
RESPONSE = 0x80
BYTE_GAP_S = 0.050

PING, INFO, STATUS, HIST = 0x01, 0x02, 0x03, 0x04
UPD_BEGIN, UPD_DATA, UPD_END, UPD_APPLY, UPD_ABORT, UPD_STATUS = 0x10, 0x11, 0x12, 0x13, 0x14, 0x15
PROTO_VERSION = 1
DATA_MAX = 251            # LINK_DATA_MAX
CHUNK = 240               # what this tool sends per UPD_DATA

# fwu_err_t (src/fw_update.h), in order, then the link's own.
ERRORS = ['ok', 'STATE', 'NO_SLOT', 'SIZE', 'OFFSET', 'LENGTH', 'INCOMPLETE', 'DIGEST', 'IMAGE',
          'NOT_TBYB', 'POWER', 'ARM_TIMEOUT', 'INTERRUPTED', 'FLASH', 'REBOOT', 'CLEANUP']
E_LINK_LENGTH, E_LINK_UNKNOWN = 0xFD, 0xFE
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


def err_name(code):
    if code < len(ERRORS):
        return ERRORS[code]
    return {E_LINK_LENGTH: 'LENGTH (link)', E_LINK_UNKNOWN: 'UNKNOWN'}.get(code, '0x%02X' % code)


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
class SerialPort:
    """Raw 921600 8N1 on a Linux tty, through termios: no pyserial needed."""

    def __init__(self, path, baud=921600):
        import termios
        speed = getattr(termios, 'B%d' % baud)
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(self.fd)
        attrs[0] = 0                                              # iflag: raw
        attrs[1] = 0                                              # oflag
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL   # cflag: 8N1, no flow control
        attrs[3] = 0                                              # lflag: no echo, no canon
        attrs[4] = attrs[5] = speed
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        termios.tcflush(self.fd, termios.TCIOFLUSH)

    def write(self, data):
        import select
        view = memoryview(data)
        while view:
            select.select([], [self.fd], [], 1.0)
            try:
                n = os.write(self.fd, view)
            except BlockingIOError:
                n = 0
            view = view[n:]

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
        os.close(self.fd)


class LinkError(Exception):
    pass


class Link:
    """One request at a time, each answered by type | 0x80 with the same seq. A retry
    resends the same frame: every request is safe to repeat (src/fw_update.h)."""

    def __init__(self, port, timeout=0.3, tries=8, clock=time.monotonic):
        self.port, self.timeout, self.tries, self.clock = port, timeout, tries, clock
        self.seq = random.randrange(256)
        self.dec = Decoder()
        self.retries = 0

    def request(self, ftype, payload=b'', timeout=None):
        self.seq = (self.seq + 1) & 0xFF
        frame = encode(ftype, self.seq, payload)
        timeout = self.timeout if timeout is None else timeout
        for attempt in range(self.tries):
            if attempt:
                self.retries += 1
            self.port.write(frame)
            deadline = self.clock() + timeout
            while True:
                left = deadline - self.clock()
                if left <= 0:
                    break
                for b in self.port.read(left):
                    f = self.dec.push(b, self.clock())
                    if f and f.type == (ftype | RESPONSE) and f.seq == self.seq:
                        if not f.payload:
                            raise LinkError('an empty answer to 0x%02X' % ftype)
                        return f.payload
        raise LinkError('no answer from the board after %d tries (is it RUNNING this Pi, '
                        'with RPI5_ON high? `link` at its USB CLI shows the lines)' % self.tries)

    # -- requests -------------------------------------------------------------
    def ping(self):
        r = self.request(PING)
        st, proto, version = struct.unpack_from('<BBI', r)
        return {'status': st, 'proto': proto, 'version': version}

    def info(self):
        r = self.request(INFO)
        (st, version) = struct.unpack_from('<BI', r)
        stamp = r[5:25].decode('ascii', 'replace').rstrip()
        btype, bpart, flags, rs, rn, os_, on, cap = struct.unpack_from('<BbBIIIII', r, 25)
        return {'status': st, 'version': version, 'stamp': stamp, 'boot_type': btype,
                'boot_partition': bpart, 'partitioned': bool(flags & 1), 'trial': bool(flags & 2),
                'bought': bool(flags & 4), 'run_start': rs, 'run_size': rn,
                'other_start': os_, 'other_size': on, 'capacity': cap}

    def status(self):
        st, ps, fault, mv, flags, up, el = struct.unpack_from('<BBBHBII', self.request(STATUS))
        return {'status': st, 'state': ps, 'fault': fault, 'mv': mv, 'pi_present': bool(flags & 1),
                'latch': bool(flags & 2), 'rails': bool(flags & 4), 'uptime_ms': up,
                'state_ms': el}

    def hist(self):
        r = self.request(HIST)
        return list(struct.unpack_from('<16I', r, 1))

    def upd_status(self):
        r = self.request(UPD_STATUS)
        st, state, err, size, rcv, ver, res, frm, to, det = struct.unpack_from('<BBBIIIBIIB', r)
        return {'status': st, 'state': state, 'error': err, 'size': size, 'received': rcv,
                'version': ver, 'result': res, 'from': frm, 'to': to, 'detail': det}

    def begin(self, size, digest):
        st, = struct.unpack_from('<B', self.request(UPD_BEGIN, struct.pack('<I', size) + digest))
        return st

    def data(self, offset, chunk):
        st, nxt = struct.unpack_from('<BI', self.request(UPD_DATA, struct.pack('<I', offset) + chunk))
        return st, nxt

    def end(self):
        st, ver = struct.unpack_from('<BI', self.request(UPD_END, timeout=1.0))
        return st, ver

    def apply(self):
        return self.request(UPD_APPLY)[0]

    def abort(self):
        return self.request(UPD_ABORT)[0]


# ---------------------------------------------------------------------------
# The update
# ---------------------------------------------------------------------------
class UpdateError(Exception):
    pass


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


def send_image(link, data, out=print):
    """BEGIN, DATA, END. Returns the version the board read from the staged image."""
    digest = hashlib.sha256(data).digest()
    st = link.begin(len(data), digest)
    if st:
        raise UpdateError('UPD_BEGIN: %s -- %s' % (err_name(st), EXPLAIN.get(err_name(st), '')))
    t0, at, shown = time.monotonic(), 0, -1
    while at < len(data):
        chunk = data[at:at + CHUNK]
        st, nxt = link.data(at, chunk)
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
    st, ver = link.end()
    if st:
        raise UpdateError('UPD_END: %s -- %s' % (err_name(st), EXPLAIN.get(err_name(st), '')))
    out('sent     : %d B in %.1f s, %d retries; the board checked the SHA-256 and the image'
        % (len(data), time.monotonic() - t0, link.retries))
    return ver


def cmd_update(link, args, out=print, ask=input):
    data = load_image(args.image)
    img = check_image(data)
    new = (img['version'][0] << 16) | img['version'][1]
    out('image    : %s, %d B, version %s, try-before-you-buy, SHA-256 %s'
        % (args.image, len(data), vstr(new), hashlib.sha256(data).hexdigest()[:16]))
    ping = link.ping()
    if ping['proto'] != PROTO_VERSION:
        raise UpdateError('the board speaks protocol %d; this tool %d' % (ping['proto'], PROTO_VERSION))
    info = link.info()
    if not info['partitioned']:
        raise UpdateError(EXPLAIN['NO_SLOT'])
    check_image(data, min(info['capacity'], info['other_size']))
    out('board    : version %s (built %s) at 0x%06X; installs into 0x%06X'
        % (vstr(info['version']), info['stamp'], info['run_start'], info['other_start']))
    if new <= info['version']:
        out('NOTE     : %s is not newer than %s. It still installs, but when it buys itself the boot '
            'ROM erases the old image: no fallback afterwards.' % (vstr(new), vstr(info['version'])))
    ver = send_image(link, data, out)
    if ver != new:
        raise UpdateError('the board read version %s, this file says %s' % (vstr(ver), vstr(new)))
    out('ready    : version %s is staged in the board\'s RAM' % vstr(ver))
    if not args.yes:
        a = ask('Apply now? The board shuts this Pi down, installs %s, reboots into it on trial, '
                'and powers this Pi back on (1-2 min). [y/N] ' % vstr(ver))
        if a.strip().lower() not in ('y', 'yes'):
            link.abort()
            out('not applied; the staged image was dropped')
            return 1
    st = link.apply()
    if st:
        raise UpdateError('UPD_APPLY: %s -- %s' % (err_name(st), EXPLAIN.get(err_name(st), '')))
    out('applied  : the board is shutting this Pi down (J8.37). When the Pi is back, check:')
    out('           pitrac-link upd-status')
    if not args.no_poweroff:
        # The board's request reaches systemd through the gpio-shutdown overlay. This is
        # the same request made directly, for a Pi without the overlay.
        try:
            subprocess.run(['systemctl', 'poweroff'], check=False)
        except OSError:
            pass
    return 0


# ---------------------------------------------------------------------------
def show_ping(link, args):
    p = link.ping()
    print('board    : PiTrac RP2354, protocol %d, version %s' % (p['proto'], vstr(p['version'])))


def show_info(link, args):
    i = link.info()
    print('version  : %s   built %s' % (vstr(i['version']), i['stamp']))
    trial = (', on trial: %s' % ('bought itself' if i['bought'] else 'NOT bought')) if i['trial'] else ''
    print('boot     : %s boot%s' % (BOOT_TYPES.get(i['boot_type'] & 0x7F, '?'), trial))
    if i['partitioned']:
        me = 'A' if i['run_start'] < i['other_start'] else 'B'
        print('layout   : partition %s at 0x%06X (%d KB); partner at 0x%06X (%d KB)'
              % (me, i['run_start'], i['run_size'] // 1024, i['other_start'], i['other_size'] // 1024))
    else:
        print('layout   : no partition table (flat at 0x%06X): updates need the one-time '
              'conversion over USB (BENCH_P8_PI.md 8.7)' % i['run_start'])
    print('staging  : %d KB of RAM' % (i['capacity'] // 1024))


def show_status(link, args):
    s = link.status()
    state = POWER[s['state']] if s['state'] < len(POWER) else '?'
    fault = FAULTS[s['fault']] if s['fault'] < len(FAULTS) else '?'
    print('power    : %s (%.1f s), fault %s' % (state, s['state_ms'] / 1000.0, fault))
    print('5V_IN    : %s' % ('not read' if s['mv'] == 0xFFFF else '%.3f V' % (s['mv'] / 1000.0)))
    print('flags    : Pi present %s, latch %s, rails ready %s'
          % (s['pi_present'], 'closed' if s['latch'] else 'open', s['rails']))
    print('uptime   : %.1f s' % (s['uptime_ms'] / 1000.0))


def show_hist(link, args):
    w = link.hist()
    for k, name in enumerate(HIST_NAMES):
        print('  %-38s %10d' % (name, w[k]))
    if w[8]:
        f = FAULTS[w[9]] if w[9] < len(FAULTS) else '?'
        print('last fault: %s, %d s into boot %d' % (f, w[10], w[11]))


def show_upd_status(link, args):
    s = link.upd_status()
    state = STATES[s['state']] if s['state'] < len(STATES) else '?'
    line = 'staged   : %s' % state
    if s['state'] == 1:
        line += ', %d of %d B' % (s['received'], s['size'])
    elif s['state'] >= 2:
        line += ', version %s, %d B' % (vstr(s['version']), s['size'])
    print(line)
    if s['error']:
        print('last err : %s -- %s' % (err_name(s['error']), EXPLAIN.get(err_name(s['error']), '')))
    if s['result']:
        res = RESULTS[s['result']] if s['result'] < len(RESULTS) else '?'
        print('update   : %s, %s -> %s' % (res, vstr(s['from']), vstr(s['to'])))
        print('           %s' % result_detail(s['result'], s['detail']))
    else:
        print('update   : none since the board was last powered on')


def do_abort(link, args):
    st = link.abort()
    print('aborted' if st == 0 else 'abort: %s' % err_name(st))


def main(argv=None):
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
    args = ap.parse_args(argv)

    if args.cmd == 'update':
        # Before opening the port: a wrong file never reaches the board.
        try:
            check_image(load_image(args.image))
        except (OSError, ValueError, UpdateError) as e:
            print('pitrac-link: %s' % e, file=sys.stderr)
            return 2
    try:
        port = SerialPort(args.port)
    except OSError as e:
        print('pitrac-link: cannot open %s: %s (dtparam=uart0=on in config.txt; README.md)'
              % (args.port, e), file=sys.stderr)
        return 2
    link = Link(port)
    handlers = {'ping': show_ping, 'info': show_info, 'status': show_status, 'hist': show_hist,
                'upd-status': show_upd_status, 'abort': do_abort}
    try:
        if args.cmd == 'update':
            return cmd_update(link, args)
        handlers[args.cmd](link, args)
        return 0
    except (LinkError, UpdateError, ValueError) as e:
        print('pitrac-link: %s' % e, file=sys.stderr)
        return 1
    finally:
        port.close()


if __name__ == '__main__':
    sys.exit(main())
