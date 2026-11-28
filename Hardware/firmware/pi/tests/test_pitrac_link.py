# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
"""Tests for pi/pitrac_link.py. Standard library only; runs on the Pi or any PC:

    python3 -m unittest discover -s pi/tests -p "test_pitrac_link.py"

The frame bytes are the ones tests/link_test.c checks the firmware against. Where
the firmware sources are next to this folder (a repo checkout), the tool's tables
are also checked against them.
"""
import contextlib
import gc
import hashlib
import io
import json
import os
import re
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import warnings
from pathlib import Path
from uuid import uuid4
from unittest import mock

HERE =os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import pitrac_link as pl  # noqa: E402

# Never this machine's shutdown, whatever a test or a mutation of the tool does: the suite runs
# on the Pi too. A test that wants the request seen puts its own recorder in.
pl.POWEROFF = [sys.executable, '-c', 'pass']

SRC = os.path.join(HERE, '..', '..', 'src')


def reset_tool():
    """The tool's per-run state as a fresh process has it: no Ctrl-C counted, nothing
    decided, no stream written to yet."""
    pl._CtrlC.count, pl._CtrlC.at_prompt, pl._CtrlC.decided = 0, False, None
    del pl._WRITERS[:]


class Base(unittest.TestCase):
    """Every test: SIGINT's handler and the tool's per-run state put back after it. main()
    puts its own SIGINT handler in (pl.quiet_ctrl_c(), image 1.20); a test run must not
    keep that."""

    def setUp(self):
        self._sigint = signal.getsignal(signal.SIGINT)
        reset_tool()

    def tearDown(self):
        if self._sigint is not None:
            signal.signal(signal.SIGINT, self._sigint)
        reset_tool()


class Stream(io.StringIO):
    """stdout or stderr for a test (no file descriptor: the tool writes it directly).
    dead: every write fails as a pipe with no reader does (BrokenPipeError). sigint: the
    first write raises SIGINT for real (signal.raise_signal() runs the handler before it
    returns), and `passed` counts the exceptions that came through the write from it."""

    def __init__(self, dead=False, sigint=False):
        super().__init__()
        self.dead, self.sigint, self.passed = dead, sigint, 0

    def write(self, s):
        if self.sigint:
            self.sigint = False
            try:
                signal.raise_signal(signal.SIGINT)
            except BaseException:
                self.passed += 1
                raise
        if self.dead:
            raise BrokenPipeError(32, 'Broken pipe')
        return super().write(s)


PL_FILE = os.path.normcase(os.path.abspath(pl.__file__))
# The tool's byte-at-a-time helpers: pure arithmetic, no decision in them. A SIGINT inside
# one is the same as one at the next line of its caller, and tracing them would make each
# sweep many times longer.
QUIET = {'crc16', 'push', 'encode', '_parse_block', 'parse_image_def', 'uf2_to_bin'}


class Sigints:
    """A tracer (sys.settrace) for one run of pl.main(): counts the line events in
    pitrac_link.py's own frames (not QUIET's), and at event k raises SIGINT for real, n
    times -- each runs the tool's handler before raise_signal() returns, as a Ctrl-C at
    that line would. decided_at: the event at which decide() was first entered."""

    def __init__(self, k=None, n=1, hook=None):
        self.k, self.n, self.events, self.decided_at = k, n, 0, None
        self.quiet_at, self.in_quiet = None, False   # the first event after quiet_ctrl_c()
        self.hook, self.seen = hook, None            # hook() at the Ctrl-C: what had happened

    def __call__(self, frame, event, arg):
        code = frame.f_code
        if os.path.normcase(code.co_filename) != PL_FILE or code.co_name in QUIET:
            return None
        return self.local

    def local(self, frame, event, arg):
        if event == 'line':
            name = frame.f_code.co_name
            if name == 'quiet_ctrl_c':
                self.in_quiet = True
            elif self.in_quiet and self.quiet_at is None:
                self.quiet_at = self.events
            if self.decided_at is None and name == 'decide':
                self.decided_at = self.events
            if self.events == self.k:
                if self.hook is not None:
                    self.seen = self.hook()
                for _ in range(self.n):
                    signal.raise_signal(signal.SIGINT)
            self.events += 1
        return self.local


def make_image(n, img_type=0x1021 | 0x8000, major=1, minor=1, block_at=0x110):
    """As tests/fw_update_test.c: a prefix, then the SDK's start block."""
    d = bytearray(((i * 2654435761) >> 13) & 0xFF for i in range(n))
    b = block_at
    struct.pack_into('<I', d, b, 0xffffded3)
    d[b + 4:b + 8] = bytes([0x42, 0x01, img_type & 0xFF, img_type >> 8])
    d[b + 8:b + 12] = bytes([0x48, 0x02, 0, 0])
    struct.pack_into('<HH', d, b + 12, minor, major)
    d[b + 16:b + 20] = bytes([0xff, 3, 0, 0])
    struct.pack_into('<I', d, b + 20, 0)
    struct.pack_into('<I', d, b + 24, 0xab123579)
    return bytes(d)


def make_uf2(data, base=0x10000000, family=pl.FAMILY_RP2350_ARM_S, skip=None):
    blocks = []
    n = (len(data) + 255) // 256
    for k in range(n):
        if skip is not None and k == skip:
            continue
        chunk = data[256 * k:256 * k + 256].ljust(256, b'\x00')
        hdr = struct.pack('<8I', pl.UF2_MAGIC0, pl.UF2_MAGIC1, 0x2000, base + 256 * k, 256, k, n, family)
        blocks.append(hdr + chunk + bytes(476 - 256) + struct.pack('<I', pl.UF2_MAGIC_END))
    return b''.join(blocks)


class Clock:
    def __init__(self):
        self.t = 0.0

    def __call__(self):
        return self.t


def pi_rule(words, state):
    """src/link_cli.c link_cli_power_rule(), for the fake board: None (runs), or the reason
    it is refused. state is the power state's name."""
    w = words
    if w[:1] == ['forceoff']:
        return None if w == ['forceoff', 'confirm'] else 'confirm'
    if w[:1] in (['reset'], ['bootsel']) and w[1:2] == ['force']:
        return None if len(w) == 3 and w[2] == 'confirm' else 'confirm'
    if w[:1] == ['adc5vcal'] and len(w) > 1:
        return None if len(w) == 3 and w[2] == 'confirm' else 'confirm'
    if w[:2] == ['fault', 'clear']:
        if len(w) == 3 and w[2] == 'confirm':
            return None
        return 'confirm' if state == 'FAULT' else None
    if w[:1] == ['off']:
        return None if state == 'RUNNING' else 'not orderly'
    return None


def strip_confirm(words, state):
    if words[-1:] == ['confirm'] and (words[0] in ('forceoff', 'adc5vcal') or
                                      words[:2] in (['reset', 'force'], ['bootsel', 'force'],
                                                    ['fault', 'clear'])):
        return words[:-1]
    return words


BOOK = 16


class FakeBoard:
    """The firmware's side of the link (link_proto.c, fw_update.c, and from protocol 4
    pi_link.c and link_cli.c's CLI with tickets and sessions), reduced to what the tool
    depends on. A board below protocol 4 answers the CLI family with E_LINK_UNKNOWN alone,
    as protocol 1 does; rollback() restarts it on protocol 1.
    drop = {(type, n)}: the n-th answer to that type is lost.

    The CLI: outputs = {line: [chunks]}; cli_lose = {n}: the n-th CLI request never
    arrives; cli_drop_final = {n}: the n-th final answer is lost; out_drop = {n}: the n-th
    CLI_OUT carrying text is lost; on_lose = fn(board): called when a CLI request is lost
    (cli_lose), e.g. another client's command; busy_s: a USB command holds the board for this long, so
    what arrives meanwhile waits in its receive buffer (and is then read, in order, with
    the look-ahead for a request's own cancel); busy_on_cli_s: the same, starting as the
    first CLI request arrives; after_final = {n: fn(board)}: called after
    the n-th final. 'long' runs until its own CLI_ABORT, then stops (F_STOPPED); 'chatty'
    the same, printing 'tick N' each second (on_tick(board, N) is called as it does);
    'slow' runs 3 s whatever is asked. A keepalive each second while one runs.

    The port failing (image 1.15, V114-F7): fail_read_after_run -- every read raises
    OSError once a command has run; fail_cli_write_at=k -- a CLI frame's write gets k
    bytes out, then raises OSError. The port stalling (image 1.21, V120-F3): writes go
    through the tool's own write_bounded(), and stall(after, until) makes the transport
    take `after` more bytes, then nothing until the fake clock reaches `until`; each wait
    for room moves the clock and calls on_wait(board). The update (V120-F1): UPD_STATUS
    says the board's real state, and UPD_BEGIN is refused while armed, as fw_update.c's;
    stage(state) puts an earlier update there. stale_abort_answer = payload: every CLI_ABORT answer
    is preceded by this one, on the same seq -- a late answer to an earlier request that
    had that seq (V114-F8). stale_cli_answers = [(type, payload)]: sent once, on a CLI
    request's own seq, before the board handles it -- late answers to another session's
    command with the same ticket number (V115-F4)."""

    def __init__(self, clock, partitioned=True, version=(1, 1), capacity=288 * 1024,
                 other_size=1016 * 1024, state=3, drop=(), offset_glitch=None, proto=4,
                 outputs=None, cli_lose=(), cli_drop_final=(), out_drop=(), busy_s=0.0,
                 busy_on_cli_s=0.0, after_final=None, pi_state='RUNNING', on_lose=None,
                 fail_read_after_run=False, fail_cli_write_at=None, stale_abort_answer=None,
                 stale_cli_answers=()):
        self.fail_read_after_run, self.fail_cli_write_at = fail_read_after_run, fail_cli_write_at
        self.stale_abort_answer, self.stale_cli_answers = stale_abort_answer, list(stale_cli_answers)
        self.clock, self.dec, self.out = clock, pl.Decoder(), bytearray()
        self.partitioned, self.version, self.capacity = partitioned, version, capacity
        self.other_size, self.power_state = other_size, state
        self.drop, self.counts, self.offset_glitch = set(drop), {}, offset_glitch
        self.upd = 'idle'
        self.size = self.received = self.last_n = 0
        self.staged = bytearray()
        self.digest = b''
        self.applied = 0
        self.frames_seen = []
        self.proto, self.outputs = proto, dict(outputs or {})
        self.cli_lose, self.cli_drop_final = set(cli_lose), set(cli_drop_final)
        self.out_drop, self.after_final = set(out_drop), dict(after_final or {})
        self.pi_state, self.on_lose = pi_state, on_lose
        self.cli_seen = self.finals = self.text_frames = self.ticks = 0
        self.cli_runs, self.running, self.on_tick = [], None, None
        self.busy_until = clock.t + busy_s if busy_s else 0.0
        self.busy_on_cli_s = busy_on_cli_s
        self.queue = []
        self.stall_after, self.stall_until, self.on_wait, self.waits = None, 0.0, None, 0
        self.staged_version = 0
        self.restart()

    def stall(self, after=0, until=float('inf')):
        """The transport takes `after` more bytes, then nothing until the clock reaches
        `until` (for ever by default), then all again."""
        self.stall_after, self.stall_until = after, until

    def stage(self, state, version=(1, 9), size=4096):
        """An earlier update, as another run left it: 'receiving', 'verified' or 'armed'."""
        self.upd, self.size = state, size
        self.received = size if state != 'receiving' else size // 2
        self.staged_version = (version[0] << 16) | version[1]

    def restart(self):
        """A board reset: a new session id, an empty book, nothing running."""
        self.session = 0x5EED0000 + (getattr(self, 'session', 0x5EED0000) & 0xFFFF) + 1
        self.issued, self.book, self.running = 0, {}, None

    def rollback(self):
        """The board comes back on older firmware (protocol 1): no CLI over the link."""
        self.proto = 1
        self.restart()

    # port
    def write(self, data, timeout=pl.WRITE_TIMEOUT_S, stop=None):
        if self.fail_cli_write_at is not None and len(data) > 2 and data[2] == pl.CLI:
            k, self.fail_cli_write_at = self.fail_cli_write_at, None
            for b in data[:k]:
                self.dec.push(b, self.clock())
            raise OSError(5, 'Input/output error')
        return pl.write_bounded(data, timeout, stop, self.take, self.wait_for_room, self.clock)

    def wait_for_room(self, s):
        self.clock.t += s
        self.waits += 1
        if self.waits > 20000:      # far past any deadline: a write without one fails here
            raise AssertionError('a write waited for room without end')
        if self.on_wait is not None:
            self.on_wait(self)

    def take(self, view):
        """What the transport takes now, into the board's decoder: the count."""
        n = len(view)
        if self.stall_after is not None:
            if self.clock() >= self.stall_until:
                self.stall_after = None
            else:
                n = min(n, self.stall_after)
                self.stall_after -= n
        for b in bytes(view[:n]):
            f = self.dec.push(b, self.clock())
            if not f:
                continue
            if f.type == pl.CLI and self.busy_on_cli_s:
                # A USB command starts just as the request arrives: it waits behind it.
                self.busy_until, self.busy_on_cli_s = self.clock() + self.busy_on_cli_s, 0.0
            if self.busy_until and self.clock() < self.busy_until:
                self.queue.append(f)                    # the board is in a USB command
            else:
                self.handle(f)
        return n

    def read(self, timeout):
        if self.fail_read_after_run and self.cli_runs:
            raise OSError(5, 'Input/output error')
        if self.busy_until and self.clock() >= self.busy_until:
            self.busy_until = 0.0
            q, self.queue = self.queue, []
            for k, f in enumerate(q):
                self.handle(f, ahead=q[k + 1:])
        if self.running is not None:
            self.tick()
        if not self.out:
            self.clock.t += timeout
            return b''
        d = bytes(self.out)
        self.out.clear()
        return d

    # the CLI (pi_link.c, link_cli.c)
    def issue(self):
        self.issued += 1
        t = self.issued
        self.book = {k: v for k, v in self.book.items() if k > t - BOOK}
        self.book[t] = {'state': 'issued'}
        return t

    def find(self, session, ticket):
        if session != self.session or ticket == 0 or ticket > self.issued:
            return pl.E_LINK_SESSION, None
        if ticket not in self.book:
            return pl.E_LINK_STALE, None
        return 0, self.book[ticket]

    def result(self, status, flags, ticket, nbytes=0, crc=0, session=None):
        """The 16-byte answer (protocol 4): the session is the board's own unless given --
        a refusal names the request's."""
        return struct.pack(pl.RESULT, status, flags, self.session if session is None else session,
                           ticket, nbytes, crc)

    def emit(self, text):
        r = self.running_out
        data = text.encode('ascii')
        self.text_frames += 1 if data else 0
        frame = pl.encode(pl.CLI_OUT | pl.RESPONSE, r['seq'],
                          struct.pack(pl.OUT_HDR, r['session'], r['ticket'], r['off']) + data)
        r['off'] += len(data)
        r['crc'] = pl.crc16(data, r['crc'])
        if data and self.text_frames in self.out_drop:
            return
        self.out += frame

    def final(self, status, flags):
        r = self.running_out
        slot = self.book.get(r['ticket'])
        payload = self.result(status, flags, r['ticket'], r['off'], r['crc'], session=r['session'])
        if slot is not None:
            slot['result'] = payload
        self.finals += 1
        if self.finals not in self.cli_drop_final:
            self.out += pl.encode(pl.CLI | pl.RESPONSE, r['seq'], payload)
        hook = self.after_final.pop(self.finals, None)
        if hook:
            hook(self)

    def tick(self):
        self.clock.t += 1.0
        r = self.running
        if r['kind'] in ('long', 'chatty') and r['stop']:
            self.emit('aborted\n')
            self.running = None
            return self.final(0, pl.F_STOP_ASKED | pl.F_STOPPED)
        if r['kind'] == 'slow' and self.clock.t >= r['end']:
            self.emit('done\n')
            self.running = None
            return self.final(0, pl.F_STOP_ASKED if r['stop'] else 0)
        if r['kind'] == 'chatty':
            self.ticks += 1
            self.emit('tick %d\n' % self.ticks)
            if self.on_tick:
                self.on_tick(self, self.ticks)
            return
        self.emit('')                                    # the keepalive

    def run(self, seq, ticket, line):
        text = line.decode('ascii')
        self.running_out = {'seq': seq, 'session': self.session, 'ticket': ticket, 'off': 0,
                            'crc': 0xFFFF}
        words = text.split()
        why = pi_rule(words, self.pi_state) if words else None
        if why:
            self.emit('REFUSED from the Pi: ... NOTHING was run.\n')
            return self.final(pl.E_LINK_PI_RULE, 0)
        text = ' '.join(strip_confirm(words, self.pi_state)) if words else text
        self.cli_runs.append(text)
        if text in ('long', 'slow', 'chatty'):
            self.running = dict(self.running_out, kind=text, stop=False, end=self.clock.t + 3.0)
            self.running_out = self.running
            return
        for chunk in self.outputs.get(text, ["? '%s' -- try 'help'\n" % text]):
            self.emit(chunk)
        return self.final(0, 0)

    def other_client(self, line):
        """Another client, between two of this one's frames: its own ticket, run now."""
        t = self.issue()
        self.book[t].update(state='ran', line=line.encode('ascii'))
        self.run(0x77, t, line.encode('ascii'))

    def handle_cli(self, f, ahead=()):
        self.cli_seen += 1
        if self.cli_seen in self.cli_lose:
            if self.on_lose:
                self.on_lose(self)
            return
        for t, payload in self.stale_cli_answers:
            self.out += pl.encode(t | pl.RESPONSE, f.seq, payload)
        self.stale_cli_answers = []
        if self.proto < 4:
            return self.answer(f, bytes([pl.E_LINK_UNKNOWN]))
        if self.running is not None:
            return                                        # dropped while one runs
        p = bytes(f.payload)
        if len(p) < 9:
            return self.answer(f, self.result(pl.E_LINK_LENGTH, 0, 0, session=0))
        session, ticket = struct.unpack_from('<II', p)
        line = p[8:]
        if any(g.type == pl.CLI_ABORT and bytes(g.payload) == p[:8] for g in ahead):
            self.cancel(session, ticket)                  # its own cancel, queued behind it
        if not 1 <= len(line) <= 95 or any(b < 32 or b > 126 for b in line):
            return self.answer(f, self.result(pl.E_LINK_LENGTH, 0, ticket, session=session))
        st, slot = self.find(session, ticket)
        if st:
            return self.answer(f, self.result(st, 0, ticket, session=session))
        if slot['state'] == 'issued':
            slot.update(state='ran', line=line)
            return self.run(f.seq, ticket, line)
        if slot['state'] == 'ran' and slot['line'] != line:
            return self.answer(f, self.result(pl.E_LINK_MISMATCH, 0, ticket, session=session))
        r = bytearray(slot['result'])
        r[1] |= pl.F_REPEAT
        return self.answer(f, bytes(r))

    def cancel(self, session, ticket):
        st, slot = self.find(session, ticket)
        if st:
            return self.result(st, 0, ticket, session=session)
        if slot['state'] == 'issued':
            slot.update(state='cancelled', result=self.result(0, pl.F_CANCELLED, ticket))
            return slot['result']
        r = bytearray(slot['result'])
        r[1] |= pl.F_REPEAT
        return bytes(r)

    def handle_cli_abort(self, f):
        p = bytes(f.payload)
        if self.running is not None:
            if p == struct.pack('<II', self.session, self.running['ticket']):
                self.running['stop'] = True               # no answer: the command's own ends it
            return
        if self.proto < 4:
            return self.answer(f, bytes([pl.E_LINK_UNKNOWN]))
        if len(p) != 8:
            return self.answer(f, self.result(pl.E_LINK_LENGTH, 0, 0, session=0))
        if self.stale_abort_answer is not None:
            self.out += pl.encode(f.type | pl.RESPONSE, f.seq, self.stale_abort_answer)
        return self.answer(f, self.cancel(*struct.unpack('<II', p)))

    def handle_cli_begin(self, f):
        if self.running is not None:
            return
        if self.proto < 4:
            return self.answer(f, bytes([pl.E_LINK_UNKNOWN]))
        return self.answer(f, struct.pack('<BII', 0, self.session, self.issue()))

    def close(self):
        pass

    def answer(self, f, payload):
        self.counts[f.type] = self.counts.get(f.type, 0) + 1
        if (f.type, self.counts[f.type]) in self.drop:
            return
        self.out += pl.encode(f.type | pl.RESPONSE, f.seq, payload)

    def handle(self, f, ahead=()):
        self.frames_seen.append((f.type, f.seq, len(f.payload)))
        v = (self.version[0] << 16) | self.version[1]
        if f.type == pl.CLI:
            return self.handle_cli(f, ahead)
        if f.type == pl.CLI_ABORT:
            return self.handle_cli_abort(f)
        if f.type == pl.CLI_BEGIN:
            return self.handle_cli_begin(f)
        if self.running is not None:
            return                                        # dropped while one runs
        if f.type == pl.PING:
            return self.answer(f, struct.pack('<BBI', 0, self.proto, v))
        if f.type == pl.STATUS:                         # RUNNING, no fault, 5.20 V
            return self.answer(f, struct.pack('<BBBHBII', 0, self.power_state, 0, 5200, 7,
                                              60000, 50000))
        if f.type == pl.HIST:
            return self.answer(f, b'\x00' + struct.pack('<16I', *range(16)))
        if f.type == pl.UPD_STATUS:                     # its real state (image 1.21)
            st = pl.STATES.index(self.upd)
            return self.answer(f, struct.pack('<BBBIIIBIIB', 0, st, 0, self.size if st else 0,
                                              self.received if st else 0,
                                              self.staged_version if st >= 2 else 0,
                                              0, 0, 0, 0))
        if f.type == pl.INFO:
            body = struct.pack('<BI', 0, v) + b'Oct 19 2026 12:34:56'
            body += struct.pack('<BbBIIIII', 0, 0, 1 if self.partitioned else 0, 0x2000,
                                1016 * 1024, 0x100000 if self.partitioned else 0,
                                self.other_size if self.partitioned else 0, self.capacity)
            return self.answer(f, body)
        if f.type == pl.UPD_BEGIN:
            size, = struct.unpack_from('<I', f.payload)
            if self.upd in ('armed', 'installing'):     # fw_update.c fwu_begin()
                return self.answer(f, struct.pack('<BI', 1, 251))
            if not self.partitioned:
                return self.answer(f, struct.pack('<BI', 2, 251))
            if size == 0 or size > self.capacity or size > self.other_size:
                return self.answer(f, struct.pack('<BI', 3, 251))
            self.upd, self.size, self.received, self.last_n = 'receiving', size, 0, 0
            self.staged, self.digest = bytearray(), bytes(f.payload[4:36])
            return self.answer(f, struct.pack('<BI', 0, 251))
        if f.type == pl.UPD_DATA:
            off, = struct.unpack_from('<I', f.payload)
            chunk = bytes(f.payload[4:])
            if self.upd != 'receiving':
                return self.answer(f, struct.pack('<BI', 1, self.received))
            if self.offset_glitch is not None and off == self.offset_glitch:
                self.offset_glitch = None                   # once: pretend it never came
                return self.answer(f, struct.pack('<BI', 4, self.received))
            if off == self.received:
                self.staged += chunk
                self.received += len(chunk)
                self.last_n = len(chunk)
                return self.answer(f, struct.pack('<BI', 0, self.received))
            if off + len(chunk) == self.received and len(chunk) == self.last_n and \
                    bytes(self.staged[off:]) == chunk:
                return self.answer(f, struct.pack('<BI', 0, self.received))
            return self.answer(f, struct.pack('<BI', 4, self.received))
        if f.type == pl.UPD_END:
            if self.upd == 'verified':
                return self.answer(f, struct.pack('<BI', 0, self.staged_version))
            if self.received != self.size:
                return self.answer(f, struct.pack('<BI', 6, 0))
            if hashlib.sha256(self.staged).digest() != self.digest:
                self.upd = 'idle'
                return self.answer(f, struct.pack('<BI', 7, 0))
            img = pl.parse_image_def(bytes(self.staged))
            if not img or not img['exe_arm_s_rp2350']:
                self.upd = 'idle'
                return self.answer(f, struct.pack('<BI', 8, 0))
            if not img['tbyb']:
                self.upd = 'idle'
                return self.answer(f, struct.pack('<BI', 9, 0))
            self.upd = 'verified'
            self.staged_version = (img['version'][0] << 16) | img['version'][1]
            return self.answer(f, struct.pack('<BI', 0, self.staged_version))
        if f.type == pl.UPD_APPLY:
            if self.upd == 'armed':
                return self.answer(f, b'\x00')
            if self.upd != 'verified':
                return self.answer(f, b'\x01')
            if self.power_state != 3:
                return self.answer(f, b'\x0a')
            self.upd = 'armed'
            self.applied += 1
            return self.answer(f, b'\x00')
        if f.type == pl.UPD_ABORT:
            self.upd = 'idle'
            return self.answer(f, b'\x00')
        return self.answer(f, bytes([pl.E_LINK_UNKNOWN]))


class Args:
    def __init__(self, image, yes=True, no_poweroff=True):
        self.image, self.yes, self.no_poweroff = image, yes, no_poweroff


# ---------------------------------------------------------------------------
class Framing(Base):
    def test_crc_check_value(self):
        self.assertEqual(pl.crc16(b'123456789'), 0x29B1)

    def test_vectors_match_the_firmware(self):
        # tests/link_test.c encode_vectors
        self.assertEqual(pl.encode(0x01, 0x07), bytes([0xA5, 0x00, 0x01, 0x07, 0x4A, 0x8F]))
        pong = bytes([0xA5, 0x06, 0x81, 0x07, 0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x38, 0x0E])
        self.assertEqual(pl.encode(0x81, 0x07, bytes([0, 1, 1, 0, 1, 0])), pong)

    def test_round_trip_and_limits(self):
        for n in (0, 1, 200, 255):
            payload = bytes((i * 37 + n) & 0xFF for i in range(n))
            d, got = pl.Decoder(), None
            for b in pl.encode(0x11, 9, payload):
                got = d.push(b, 0.0) or got
            self.assertEqual((got.type, got.seq, got.payload), (0x11, 9, payload))
        with self.assertRaises(ValueError):
            pl.encode(0x11, 0, bytes(256))

    def test_junk_bad_crc_and_gap(self):
        d = pl.Decoder()
        stream = bytes([0x00, 0xA5, 0x03, 0x02, 0x01, 0x10, 0x20, 0x30, 0x40, 0x50])
        stream += pl.encode(0x02, 0x44, b'\x01\x02\x03')
        frames = [f for f in (d.push(b, 0.0) for b in stream) if f]
        self.assertEqual(len(frames), 1)
        self.assertEqual((frames[0].type, frames[0].seq), (0x02, 0x44))
        self.assertEqual(d.crc_errors, 1)
        # A frame cut short is dropped after BYTE_GAP_S.
        d = pl.Decoder()
        for b in pl.encode(0x11, 1, bytes(10))[:-3]:
            d.push(b, 1.0)
        out = [d.push(b, 1.0 + pl.BYTE_GAP_S + 0.001) for b in pl.encode(0x01, 2)]
        self.assertEqual(out[-1].type, 0x01)


class Images(Base):
    def test_parse(self):
        img = pl.parse_image_def(make_image(8192, minor=7))
        self.assertEqual((img['exe_arm_s_rp2350'], img['tbyb'], img['version']), (True, True, (1, 7)))
        self.assertFalse(pl.parse_image_def(make_image(8192, 0x1021))['tbyb'])
        for t in (0x1121 | 0x8000, 0x0021, 0x1011, 0x1002):
            self.assertFalse(pl.parse_image_def(make_image(8192, t))['exe_arm_s_rp2350'])
        self.assertIsNone(pl.parse_image_def(make_image(8192, block_at=4096)))
        self.assertIsNone(pl.parse_image_def(bytes(8192)))
        bad = bytearray(make_image(8192))
        bad[0x110 + 17] = 4                                   # LAST's size wrong
        self.assertIsNone(pl.parse_image_def(bytes(bad)))

    def test_check_image(self):
        pl.check_image(make_image(4096))
        with self.assertRaises(pl.UpdateError):
            pl.check_image(make_image(4096, 0x1021))          # not TBYB
        with self.assertRaises(pl.UpdateError):
            pl.check_image(make_image(4096, 0x1121 | 0x8000))  # RISC-V
        with self.assertRaises(pl.UpdateError):
            pl.check_image(make_image(8192), cap=4096)

    def test_uf2(self):
        data = make_image(5000)
        self.assertEqual(pl.uf2_to_bin(make_uf2(data))[:len(data)], data)
        with self.assertRaises(ValueError):
            pl.uf2_to_bin(make_uf2(data, skip=3))            # a hole
        with self.assertRaises(ValueError):
            pl.uf2_to_bin(make_uf2(data, family=0x12345678))  # nothing for us
        with self.assertRaises(ValueError):
            pl.uf2_to_bin(bytes(1024))


class Requests(Base):
    def link(self, **kw):
        clock = Clock()
        board = FakeBoard(clock, **kw)
        return pl.Link(board, clock=clock), board

    def test_retry_resends_the_same_frame(self):
        link, board = self.link(drop={(pl.PING, 1), (pl.PING, 2)})
        self.assertEqual(link.ping()['version'], 0x00010001)
        self.assertEqual(link.retries, 2)
        seqs = {s for (t, s, _) in board.frames_seen if t == pl.PING}
        self.assertEqual(len(seqs), 1)                       # one request, three sends
        self.assertEqual(len([1 for (t, _, _) in board.frames_seen if t == pl.PING]), 3)

    def test_gives_up(self):
        link, _ = self.link(drop={(pl.PING, k) for k in range(1, 20)})
        with self.assertRaises(pl.LinkError):
            link.ping()

    def test_info_fields(self):
        link, _ = self.link()
        i = link.info()
        self.assertEqual((i['version'], i['stamp'], i['partitioned']), (0x00010001, 'Oct 19 2026 12:34:56', True))
        self.assertEqual((i['run_start'], i['other_start'], i['capacity']), (0x2000, 0x100000, 288 * 1024))


class Update(Base):
    def run_update(self, data, **kw):
        clock = Clock()
        board = FakeBoard(clock, **kw)
        link = pl.Link(board, clock=clock)
        path = os.path.join(HERE, '_test_image.bin')
        with open(path, 'wb') as f:
            f.write(data)
        lines = []
        try:
            rc = pl.cmd_update(link, Args(path), out=lines.append)
        finally:
            os.remove(path)
        return rc, board, link, lines

    def test_whole_update_with_lost_answers(self):
        data = make_image(3 * 4096 + 77, minor=2)
        rc, board, link, _ = self.run_update(data, drop={(pl.UPD_DATA, 3), (pl.UPD_END, 1),
                                                         (pl.UPD_APPLY, 1)})
        self.assertEqual(rc, 0)
        self.assertEqual(bytes(board.staged), data)          # nothing doubled by a retry
        self.assertEqual(board.applied, 1)                   # a retried APPLY is not a second one
        self.assertEqual(board.upd, 'armed')
        self.assertEqual(link.retries, 3)
        self.assertTrue(all(n <= 4 + pl.DATA_MAX for (t, _, n) in board.frames_seen if t == pl.UPD_DATA))

    def test_resyncs_on_offset(self):
        data = make_image(2000)
        rc, board, _, _ = self.run_update(data, offset_glitch=480)
        self.assertEqual(rc, 0)
        self.assertEqual(bytes(board.staged), data)

    def test_board_without_partitions(self):
        with self.assertRaises(pl.UpdateError) as e:
            self.run_update(make_image(4096), partitioned=False)
        self.assertIn('partition table', str(e.exception))

    def test_board_not_running_the_pi(self):
        with self.assertRaises(pl.UpdateError) as e:
            self.run_update(make_image(4096), state=4)        # BENCH_RUNNING
        self.assertIn('POWER', str(e.exception))

    def test_too_big_for_the_board(self):
        with self.assertRaises(pl.UpdateError):
            self.run_update(make_image(5000), capacity=4096)

    def test_not_newer_is_announced(self):
        rc, _, _, lines = self.run_update(make_image(4096, minor=1), version=(1, 1))
        self.assertEqual(rc, 0)
        note = [line for line in lines if line.startswith('NOTE')]
        self.assertEqual(len(note), 1)
        # The boot ROM's rule as it is (image 1.20): the erase happens only when the old
        # image would win the next normal boot. Before, the note said it always happens.
        self.assertIn('whenever the old one would win the next normal boot: always for an '
                      'older version, and for an equal one when the running image is in A',
                      note[0])
        rc, _, _, lines = self.run_update(make_image(4096, minor=2), version=(1, 1))
        self.assertEqual([line for line in lines if line.startswith('NOTE')], [])

    def test_declined_drops_the_staged_image(self):
        clock = Clock()
        board = FakeBoard(clock)
        link = pl.Link(board, clock=clock)
        path = os.path.join(HERE, '_test_image.bin')
        with open(path, 'wb') as f:
            f.write(make_image(4096))
        try:
            rc = pl.cmd_update(link, Args(path, yes=False), out=lambda s: None, ask=lambda q: 'n')
        finally:
            os.remove(path)
        self.assertEqual(rc, 1)
        self.assertEqual((board.applied, board.upd), (0, 'idle'))


class Cli(Base):
    """The board's CLI over the link (protocol 4): tickets, at most once, cancel, stop, complete
    output, the Pi-only power rule. The cases named V113-F* are the 1.13 review's."""

    STAT = ['state    : RUNNING (5000 ms)\n', 'fault    : none\n']

    def setup(self, **kw):
        clock = Clock()
        outputs = {'stat': self.STAT, 'id': ['version : 1.16\n'], 'forceoff': ['FORCE OFF\n']}
        board = FakeBoard(clock, outputs=outputs, **kw)
        return pl.Link(board, clock=clock), board

    def run_line(self, link, line, stop=None):
        out, err = [], []
        rc = pl.run_cli(link, line, write=out.append, err=err.append, stop=stop)
        return rc, ''.join(out), ' '.join(err)

    def sent(self, board, ftype):
        return [s for (t, s, _) in board.frames_seen if t == ftype]

    def test_output_streams_and_runs_once(self):
        link, board = self.setup()
        rc, out, err = self.run_line(link, 'stat')
        self.assertEqual((rc, out, err), (pl.RC_OK, ''.join(self.STAT), ''))
        self.assertEqual(board.cli_runs, ['stat'])
        self.assertEqual(len(self.sent(board, pl.CLI)), 1)
        self.assertEqual(len(self.sent(board, pl.CLI_BEGIN)), 1)

    def test_lost_request_is_sent_again_and_runs_once(self):
        link, board = self.setup(cli_lose={1})
        rc, out, _ = self.run_line(link, 'stat')
        self.assertEqual((rc, out), (pl.RC_OK, ''.join(self.STAT)))
        self.assertEqual(board.cli_runs, ['stat'])
        self.assertEqual(len(set(self.sent(board, pl.CLI))), 1)    # one request, two sends
        self.assertEqual(link.retries, 1)

    def test_lost_answer_does_not_run_it_twice(self):
        link, board = self.setup(cli_drop_final={1})
        rc, out, err = self.run_line(link, 'stat')
        self.assertEqual(rc, pl.RC_OK)
        self.assertEqual(board.cli_runs, ['stat'])                  # once, despite the resend
        self.assertIn('had already run', err)
        self.assertEqual(out, ''.join(self.STAT))                    # all of it, the first time

    def test_v113_f1_two_clients(self):
        # A's answer to `stat` is lost; client B runs `id` before A sends again. 1.13 ran
        # stat, id, stat and A exited 0. Now A's resend is answered from the board's record.
        link, board = self.setup(cli_drop_final={1},
                                 after_final={1: lambda b: b.other_client('id')})
        rc, out, err = self.run_line(link, 'stat')
        self.assertEqual(board.cli_runs, ['stat', 'id'])
        self.assertEqual(rc, pl.RC_OK)
        self.assertEqual(out, ''.join(self.STAT))                    # B's output not taken for A's
        self.assertIn('had already run', err)

    def test_v113_f1_other_clients_output(self):
        # A's request is lost, and client B's command runs before A sends again: B's output
        # reaches this Pi first. It names B's ticket, so A takes none of it.
        link, board = self.setup(cli_lose={1}, on_lose=lambda b: b.other_client('id'))
        rc, out, err = self.run_line(link, 'stat')
        self.assertEqual((rc, out, err), (pl.RC_OK, ''.join(self.STAT), ''))
        self.assertEqual(board.cli_runs, ['id', 'stat'])

    def test_v113_f1_sequence_wrap(self):
        # After a `stat` and 255 other requests the seq is back where it was: 1.13 answered a
        # fresh `stat` from memory and did not run it. Each command now has its own ticket.
        link, board = self.setup()
        self.assertEqual(self.run_line(link, 'stat')[0], pl.RC_OK)
        for _ in range(255):
            link.ping()
        rc, out, err = self.run_line(link, 'stat')
        self.assertEqual((rc, err), (pl.RC_OK, ''))
        self.assertEqual(board.cli_runs, ['stat', 'stat'])

    def test_v113_f1_board_restart_is_unknown(self):
        # The answer is lost, then the board resets: the resend is refused as another
        # session's. It may have run before the reset, so the outcome is UNKNOWN -- and the
        # tool does not send it again.
        link, board = self.setup(cli_drop_final={1}, after_final={1: lambda b: b.restart()})
        rc, _, err = self.run_line(link, 'strobe pulse 20')
        self.assertEqual(rc, pl.RC_UNKNOWN)
        self.assertIn('UNKNOWN for ticket 5eed0001:1', err)
        self.assertIn('restarted', err)
        # V116-N2: the restart took the board's record; asking cannot tell, its USB log can.
        self.assertIn('No query can recover it', err)
        self.assertIn('[pi] <command>', err)
        self.assertNotIn('pitrac-link cli-cancel', err)
        self.assertEqual(board.cli_runs, ['strobe pulse 20'])
        self.assertEqual(len(self.sent(board, pl.CLI)), 2)            # the first, one resend

    def test_v113_f2_ctrl_c_after_a_lost_request(self):
        # The request is lost and Ctrl-C comes at 0.2 s. 1.13 sent request, abort, abort,
        # request: the command first ran after the cancel, and the exit was 0. Now the
        # request is never sent again, the board cancels the ticket, and nothing runs.
        link, board = self.setup(cli_lose={1})
        t0 = board.clock.t
        rc, _, err = self.run_line(link, 'stat', stop=lambda: board.clock.t - t0 >= 0.2)
        self.assertEqual(rc, pl.RC_CANCELLED)
        self.assertIn('cancelled before it ran', err)
        self.assertEqual(board.cli_runs, [])
        self.assertEqual(len(self.sent(board, pl.CLI)), 1)
        self.assertGreaterEqual(len(self.sent(board, pl.CLI_ABORT)), 1)
        # ...and stays cancelled: the same ticket cannot run later.
        session, ticket = link.last_ticket
        r = link.cli_cancel(session, ticket)
        self.assertEqual(r.outcome, 'cancelled')
        # The cancel's own answers lost for 2 s as well: the line is still never sent again,
        # though its first resend would have been due at CLI_FIRST_S.
        link, board = self.setup(cli_lose={1}, drop={(pl.CLI_ABORT, k) for k in range(1, 5)})
        t0 = board.clock.t
        rc, _, err = self.run_line(link, 'stat', stop=lambda: board.clock.t - t0 >= 0.2)
        self.assertEqual(rc, pl.RC_CANCELLED)
        self.assertEqual(len(self.sent(board, pl.CLI)), 1)
        self.assertGreaterEqual(len(self.sent(board, pl.CLI_ABORT)), 5)
        self.assertGreater(board.clock.t - t0, pl.CLI_FIRST_S)

    def test_v113_f2_queued_behind_usb_work(self):
        # A USB command holds the board for 5 s. The request, its resends and then the
        # cancel all wait in its buffer; when it reads them, the request finds its own
        # cancel behind it and never runs.
        link, board = self.setup(busy_on_cli_s=5.0)
        t0 = board.clock.t
        rc, _, err = self.run_line(link, 'stat', stop=lambda: board.clock.t - t0 >= 2.5)
        self.assertEqual(rc, pl.RC_CANCELLED)
        self.assertEqual(board.cli_runs, [])
        self.assertGreaterEqual(len(self.sent(board, pl.CLI)), 2)   # it was resent, queued

    def test_busy_board_at_begin(self):
        # Busy before the ticket is even issued: no ticket, no command sent -- a link error,
        # exit 1, with nothing that could run later.
        link, board = self.setup(busy_s=5.0)
        with self.assertRaises(pl.LinkError):
            link.cli('stat')
        self.assertEqual(self.sent(board, pl.CLI), [])
        self.assertIsNone(link.last_ticket)

    def test_no_answer_at_all_asks_how_it_ended(self):
        # Every request lost: after CLI_TRIES sends the tool stops sending it and asks. The
        # board never saw it, so the ticket is cancelled: nothing ran, and it says so.
        link, board = self.setup(cli_lose=set(range(1, 20)))
        rc, _, err = self.run_line(link, 'stat')
        self.assertEqual(rc, pl.RC_CANCELLED)
        self.assertIn('the board had not answered', err)
        self.assertEqual(len(self.sent(board, pl.CLI)), pl.CLI_TRIES)
        self.assertEqual(board.cli_runs, [])

    def test_v113_f3_stop_that_stops(self):
        link, board = self.setup()
        rc, out, err = self.run_line(link, 'long', stop=lambda: board.running is not None)
        self.assertEqual(rc, pl.RC_STOPPED)
        self.assertIn('ended early', err)
        self.assertIn('aborted', out)
        self.assertIsNone(board.running)

    def test_v113_f3_stop_that_does_not(self):
        # 1.13 said "stopped" for a command that ran to its end. It says what happened now.
        # (The stop comes once the line is out: before that, it is never sent -- V114-F1.)
        link, board = self.setup()
        rc, out, err = self.run_line(link, 'slow', stop=lambda: link.line_sent)
        self.assertEqual(rc, pl.RC_RAN_TO_END)
        self.assertIn('ran to its end', err)
        self.assertIn('done', out)

    def test_v113_f2_late_abort_for_an_earlier_command(self):
        # A CLI_ABORT for an earlier ticket, arriving while a later command runs, is not
        # taken as a stop of the later one.
        link, board = self.setup()
        self.assertEqual(self.run_line(link, 'stat')[0], pl.RC_OK)
        old = link.last_ticket
        calls = []

        def late():
            calls.append(1)
            if len(calls) == 2:
                board.write(pl.encode(pl.CLI_ABORT, 0x55, struct.pack('<II', *old)))
            return False
        rc, out, _ = self.run_line(link, 'slow', stop=late)
        self.assertEqual(rc, pl.RC_OK)
        self.assertIn('done', out)

    def test_v113_f5_lost_output_frame(self):
        # The first of `stat`'s two frames is lost. 1.13 printed the second line and exited 0.
        link, board = self.setup(out_drop={1})
        rc, out, err = self.run_line(link, 'stat')
        self.assertEqual(rc, pl.RC_INCOMPLETE)
        self.assertEqual(out, self.STAT[1])
        self.assertIn('INCOMPLETE OUTPUT', err)
        self.assertIn('%d of the %d bytes' % (len(self.STAT[1]), len(''.join(self.STAT))), err)

    def test_v113_f5_all_output_lost(self):
        link, board = self.setup(out_drop={1, 2})
        rc, out, err = self.run_line(link, 'stat')
        self.assertEqual((rc, out), (pl.RC_INCOMPLETE, ''))
        self.assertIn('0 of the', err)

    def test_v113_f5_lost_output_and_answer(self):
        # Output and the answer both lost: the record answers the resend, and the output it
        # names never came -- incomplete, not a clean 0.
        link, board = self.setup(out_drop={1, 2}, cli_drop_final={1})
        rc, _, err = self.run_line(link, 'stat')
        self.assertEqual(rc, pl.RC_INCOMPLETE)
        self.assertEqual(board.cli_runs, ['stat'])

    # -- the 1.14 review (V114-*) -------------------------------------------------------
    def test_v114_f1_stop_while_the_ticket_is_delayed(self):
        # The review's case: the board is busy for 0.55 s, so CLI_BEGIN's answer comes late,
        # and Ctrl-C comes at 0.20 s. 1.14 polled the stop first after sending the line, and
        # `stat` ran. Now the line is never sent, and nothing runs.
        link, board = self.setup(busy_s=0.55)
        t0 = board.clock.t
        rc, out, err = self.run_line(link, 'stat', stop=lambda: board.clock.t - t0 >= 0.20)
        self.assertEqual((rc, out), (pl.RC_CANCELLED, ''))
        self.assertIn('stopped before the command was sent', err)
        self.assertLess(board.clock.t - t0, 0.25)             # seen within one 0.1 s poll
        board.clock.t += 1.0
        board.read(0.0)                                         # the board catches up
        self.assertEqual(self.sent(board, pl.CLI), [])
        self.assertEqual(board.cli_runs, [])
        self.assertFalse(link.line_sent)

    def test_v114_f1_stop_while_the_ticket_is_lost(self):
        link, board = self.setup(drop={(pl.CLI_BEGIN, k) for k in range(1, 9)})
        t0 = board.clock.t
        rc, _, err = self.run_line(link, 'stat', stop=lambda: board.clock.t - t0 >= 0.20)
        self.assertEqual(rc, pl.RC_CANCELLED)
        self.assertIn('nothing ran', err)
        self.assertEqual(self.sent(board, pl.CLI), [])
        self.assertEqual(len(self.sent(board, pl.CLI_BEGIN)), 1)   # no resend after the stop
        # Stopped before it starts: not even CLI_BEGIN goes out.
        link, board = self.setup()
        self.assertEqual(self.run_line(link, 'stat', stop=lambda: True)[0], pl.RC_CANCELLED)
        self.assertEqual(self.sent(board, pl.CLI_BEGIN), [])

    def test_v114_f1_stop_just_as_the_ticket_arrives(self):
        # The stop lands after CLI_BEGIN answered, before the line's first write.
        link, board = self.setup()
        rc, _, err = self.run_line(link, 'strobe pulse 20', stop=lambda: link.last_ticket is not None)
        self.assertEqual(rc, pl.RC_CANCELLED)
        self.assertEqual(self.sent(board, pl.CLI), [])
        self.assertEqual(board.cli_runs, [])

    def test_v114_f1_second_ctrl_c_before_the_line(self):
        # A second Ctrl-C (give_up) while the ticket is awaited: nothing was sent, so the
        # outcome is known -- nothing ran -- not UNKNOWN. (Image 1.20: the second Ctrl-C is
        # polled, as the first is; 1.19 raised KeyboardInterrupt for it.)
        link, board = self.setup(busy_s=5.0)
        r = link.cli('stat', stop=lambda: True, give_up=lambda: True)
        self.assertEqual(pl.cli_exit(r, lambda s: None), pl.RC_CANCELLED)
        self.assertEqual(r.outcome, 'not sent')
        self.assertEqual(self.sent(board, pl.CLI), [])
        # After the line has gone out, the same two Ctrl-Cs, seen together: the first one's
        # stop goes out, then the second gives up waiting -- UNKNOWN, with the ticket, and
        # the running command asked to stop (image 1.21, review finding V120-F2: 1.20 gave
        # up first, sent no stop, and left the command running).
        link, board = self.setup()
        err = []
        r = link.cli('slow', stop=lambda: link.line_sent, give_up=lambda: link.line_sent)
        self.assertEqual(pl.cli_exit(r, err.append), pl.RC_UNKNOWN)
        self.assertIn('cli-cancel 5eed0001:1', ' '.join(err))
        self.assertIn('stopped waiting', ' '.join(err))
        self.assertIn('after asking the board to stop it', ' '.join(err))
        self.assertEqual(len(self.sent(board, pl.CLI_ABORT)), 1)
        self.assertTrue(board.running['stop'])                 # asked, and it will stop

    def test_v119_f1_give_up_is_never_polled_after_an_answer(self):
        # The second Ctrl-C is polled only before a wait. An answer that has arrived is the
        # outcome, whatever give_up() says once it has: 1.19 could lose it to a
        # KeyboardInterrupt raised after it was decoded (review finding V119-F1).
        link, board = self.setup()
        polls = []

        def give_up():
            polls.append(len(board.cli_runs))
            return bool(board.cli_runs) and len(polls) > 1
        r = link.cli('stat', give_up=give_up)
        self.assertEqual((r.outcome, pl.cli_exit(r, lambda s: None)), ('done', pl.RC_OK))
        self.assertEqual(polls, [1])        # once, before the first read: then the answer

    def test_v114_f6_stop_after_it_finished(self):
        # The review's race: the stop goes out after the line, and the board had already
        # finished. 1.14 exited 0 with stop_asked set. Now: ran to its end, exit 8.
        link, board = self.setup()
        rc, out, err = self.run_line(link, 'stat', stop=lambda: link.line_sent)
        self.assertEqual((rc, out), (pl.RC_RAN_TO_END, ''.join(self.STAT)))
        self.assertIn('had already finished', err)
        self.assertGreaterEqual(len(self.sent(board, pl.CLI_ABORT)), 1)
        self.assertEqual(board.cli_runs, ['stat'])

    def test_v114_f7_port_fails_after_it_ran(self):
        # The review's case: an I/O error on the first read after the board ran it. 1.14 let
        # the OSError escape and the ticket was lost. Now: UNKNOWN, with the ticket.
        link, board = self.setup(fail_read_after_run=True)
        rc, _, err = self.run_line(link, 'stat')
        self.assertEqual(rc, pl.RC_UNKNOWN)
        self.assertIn('serial port failed', err)
        self.assertIn('cli-cancel 5eed0001:1', err)
        self.assertEqual(board.cli_runs, ['stat'])

    def test_v114_f7_port_fails_in_main(self):
        # The same through main(), the process path the review ran: exit 5, ticket on stderr.
        clock = Clock()
        board = FakeBoard(clock, outputs={'stat': self.STAT}, fail_read_after_run=True)
        real_port, real_link = pl.SerialPort, pl.Link
        pl.SerialPort = lambda path: board
        pl.Link = lambda port: real_link(port, clock=clock)
        err, out = io.StringIO(), io.StringIO()
        try:
            with contextlib.redirect_stderr(err), contextlib.redirect_stdout(out):
                rc = pl.main(['cli', 'stat'])
        finally:
            pl.SerialPort, pl.Link = real_port, real_link
        self.assertEqual(rc, pl.RC_UNKNOWN)
        self.assertIn('cli-cancel 5eed0001:1', err.getvalue())
        self.assertEqual(board.cli_runs, ['stat'])

    def test_v114_f7_port_fails_part_way_through_the_line(self):
        # A partial write: the board never gets the whole frame, but this Pi cannot know
        # that -- UNKNOWN, with the ticket. cli-cancel then establishes it: cancelled.
        link, board = self.setup(fail_cli_write_at=7)
        rc, _, err = self.run_line(link, 'strobe pulse 20')
        self.assertEqual(rc, pl.RC_UNKNOWN)
        self.assertIn('cli-cancel 5eed0001:1', err)
        board.clock.t += 1.0                                    # the half frame times out
        self.assertEqual(link.cli_cancel(*link.last_ticket).outcome, 'cancelled')
        self.assertEqual(board.cli_runs, [])

    def test_v114_f7_port_fails_before_the_line(self):
        # Before the line is written nothing can run: the error is the tool's (exit 1).
        clock = Clock()
        board = FakeBoard(clock, outputs={'stat': self.STAT})

        def broken(data, *rest):
            raise OSError(5, 'Input/output error')
        board.write = broken
        real_port, real_link = pl.SerialPort, pl.Link
        pl.SerialPort = lambda path: board
        pl.Link = lambda port: real_link(port, clock=clock)
        err = io.StringIO()
        try:
            with contextlib.redirect_stderr(err):
                rc = pl.main(['cli', 'stat'])
        finally:
            pl.SerialPort, pl.Link = real_port, real_link
        self.assertEqual(rc, pl.RC_ERROR)
        self.assertIn('serial port failed', err.getvalue())
        self.assertEqual(board.cli_runs, [])

    def test_v114_f8_cli_cancel_takes_only_its_own_ticket(self):
        # The review's case: ticket 1 cancelled, ticket 2 ran `stat`; the earlier
        # cancellation answer comes again on the reused seq. 1.14 reported ticket 2 as
        # cancelled. Now that answer is passed over, and ticket 2's record is read.
        link, board = self.setup()
        first = link.cli_begin()
        self.assertEqual(link.cli_cancel(*first).outcome, 'cancelled')
        self.assertEqual(self.run_line(link, 'stat')[0], pl.RC_OK)
        second = link.last_ticket
        board.stale_abort_answer = board.result(0, pl.F_CANCELLED, first[1])
        r = link.cli_cancel(*second)
        self.assertEqual((r.outcome, r.ticket), ('done', second[1]))
        self.assertEqual(board.cli_runs, ['stat'])

    def test_v114_f8_only_another_tickets_answer_is_unknown(self):
        link, board = self.setup()
        first = link.cli_begin()
        link.cli_cancel(*first)
        self.assertEqual(self.run_line(link, 'stat')[0], pl.RC_OK)
        second = link.last_ticket
        board.stale_abort_answer = board.result(0, pl.F_CANCELLED, first[1])
        board.drop |= {(pl.CLI_ABORT, k) for k in range(1, 20)}   # its own never comes
        r = link.cli_cancel(*second)
        self.assertEqual(r.outcome, 'unknown')
        printed = []

        class A:
            ticket = '%08x:%d' % second
        board.drop = set()
        board.stale_abort_answer = None
        self.assertEqual(pl.cmd_cli_cancel(link, A(), out=printed.append, err=printed.append),
                         pl.RC_OK)
        self.assertIn('it ran', ' '.join(printed))

    def test_v114_n1_lost_refusal_text(self):
        # The power rule refused it, and its one line of explanation was lost. 1.14 exited 3
        # with nothing on either stream. The status stands, and the loss is said.
        link, board = self.setup(out_drop={1})
        rc, out, err = self.run_line(link, 'forceoff')
        self.assertEqual((rc, out), (pl.RC_PI_RULE, ''))
        self.assertIn('explanation was lost', err)
        self.assertEqual(board.cli_runs, [])
        link, board = self.setup()                              # arrived: nothing added
        rc, out, err = self.run_line(link, 'forceoff')
        self.assertEqual((rc, err), (pl.RC_PI_RULE, ''))
        self.assertIn('REFUSED', out)

    # -- the 1.15 review (V115-*) -------------------------------------------------------
    def test_v115_f4_recovery_query_another_sessions_cancel(self):
        # The review's case: `stat` ran as (S1, 1); the board restarted as S2; (S2, 1) was
        # cancelled; that cancellation's answer comes again, late, on the seq of a query
        # about (S1, 1), ahead of the board's SESSION refusal. 1.15 took it: "cancelled
        # before execution", for a command that had run. Now it names S2, and is passed over.
        link, board = self.setup()
        self.assertEqual(self.run_line(link, 'stat')[0], pl.RC_OK)
        first = link.last_ticket
        board.restart()
        second = link.cli_begin()
        self.assertEqual(second[1], first[1])                  # the same number, another session
        self.assertNotEqual(second[0], first[0])
        self.assertEqual(link.cli_cancel(*second).outcome, 'cancelled')
        board.stale_abort_answer = board.result(0, pl.F_CANCELLED, second[1], session=second[0])
        r = link.cli_cancel(*first)
        self.assertEqual(r.outcome, 'unknown')
        self.assertIn('restarted', r.why)
        printed = []

        class A:
            ticket = '%08x:%d' % first
        self.assertEqual(pl.cmd_cli_cancel(link, A(), out=printed.append, err=printed.append),
                         pl.RC_UNKNOWN)
        self.assertIn('UNKNOWN for ticket %08x:%d' % first, ' '.join(printed))
        self.assertNotIn('cancelled', ' '.join(printed))
        self.assertEqual(board.cli_runs, ['stat'])

    def test_v115_f4_streaming_takes_only_its_own_session(self):
        # While (S2, 1) runs, output and an answer for (S1, 1) -- the same number, the
        # board's previous session -- arrive first on the same seq. 1.15 matched the number
        # alone: it took S1's output and S1's answer. Now neither is taken.
        link, board = self.setup()
        old = board.session
        board.restart()
        board.stale_cli_answers = [
            (pl.CLI_OUT, struct.pack(pl.OUT_HDR, old, 1, 0) + b'old text\n'),
            (pl.CLI, board.result(0, pl.F_STOP_ASKED | pl.F_STOPPED, 1, 9, pl.crc16(b'old text\n'),
                                  session=old)),
        ]
        rc, out, err = self.run_line(link, 'stat')
        self.assertEqual((rc, out, err), (pl.RC_OK, ''.join(self.STAT), ''))
        self.assertEqual(link.last_ticket, (board.session, 1))
        self.assertEqual(board.cli_runs, ['stat'])

    def test_v115_f7_rollback_in_flight(self):
        # The review's case: `stat` ran once, its answer was lost, and the board came back on
        # protocol 1 before the resend. 1.15 exited 1 with "update it", and no ticket. Now:
        # UNKNOWN, exit 5, the ticket, and why -- through main(), the process path.
        clock = Clock()
        board = FakeBoard(clock, outputs={'stat': self.STAT}, cli_drop_final={1},
                          after_final={1: lambda b: b.rollback()})
        real_port, real_link = pl.SerialPort, pl.Link
        pl.SerialPort = lambda path: board
        pl.Link = lambda port: real_link(port, clock=clock)
        err, out = io.StringIO(), io.StringIO()
        try:
            with contextlib.redirect_stderr(err), contextlib.redirect_stdout(out):
                rc = pl.main(['cli', 'stat'])
        finally:
            pl.SerialPort, pl.Link = real_port, real_link
        self.assertEqual(rc, pl.RC_UNKNOWN)
        self.assertIn('UNKNOWN for ticket 5eed0001:1', err.getvalue())
        self.assertIn('went back to older firmware', err.getvalue())
        # V116-N2: no "ask again once it runs 1.16" -- the record went with the restart.
        self.assertIn('No query can recover it', err.getvalue())
        self.assertIn('[pi] <command>', err.getvalue())
        self.assertNotIn('again', err.getvalue())
        self.assertEqual(board.cli_runs, ['stat'])
        # The same when it is the cancel that meets the older firmware (a stop sent after).
        link, board = self.setup(cli_drop_final={1}, after_final={1: lambda b: b.rollback()})
        rc, _, err = self.run_line(link, 'slow', stop=lambda: link.line_sent)
        self.assertEqual(rc, pl.RC_UNKNOWN)
        self.assertIn('older firmware', err)

    def test_v115_f7_rollback_recovery_query(self):
        # A fresh cli-cancel once the board runs protocol 1: it cannot say how the ticket
        # ended, so UNKNOWN, exit 5, with the ticket. 1.15 raised "update it" (exit 1).
        link, board = self.setup()
        self.assertEqual(self.run_line(link, 'stat')[0], pl.RC_OK)
        ticket = link.last_ticket
        board.rollback()
        r = link.cli_cancel(*ticket)
        self.assertEqual((r.outcome, r.no_cli_now), ('unknown', True))
        printed = []

        class A:
            pass
        a = A()
        a.ticket = '%08x:%d' % ticket
        self.assertEqual(pl.cmd_cli_cancel(link, a, out=printed.append, err=printed.append),
                         pl.RC_UNKNOWN)
        self.assertIn('UNKNOWN for ticket %s' % a.ticket, ' '.join(printed))
        self.assertIn('No query can recover it', ' '.join(printed))      # V116-N2
        # Before any line went out, protocol 1 is still "update it", exit 1: nothing ran.
        with self.assertRaises(pl.LinkError):
            link.cli('stat')

    def test_v115_cli_cancel_port_fails(self):
        # The query itself meets a failing port: whether the CLI_ABORT went out (and
        # cancelled the ticket) is not known either -- UNKNOWN, with the ticket, not a crash.
        link, board = self.setup()
        ticket = link.cli_begin()

        def broken(data, *rest):
            raise OSError(5, 'Input/output error')
        board.write = broken
        r = link.cli_cancel(*ticket)
        self.assertEqual(r.outcome, 'unknown')
        self.assertIn('serial port failed', r.why)

    def main_with_port(self, argv, port):
        """pl.main(argv) with SerialPort replaced: `port` is what it returns, or an exception
        it raises. Returns (exit code, stdout, stderr)."""
        def opened(path):
            if isinstance(port, BaseException):
                raise port
            return port
        real = pl.SerialPort
        pl.SerialPort = opened
        err, out = io.StringIO(), io.StringIO()
        try:
            with contextlib.redirect_stderr(err), contextlib.redirect_stdout(out):
                rc = pl.main(argv)
        finally:
            pl.SerialPort = real
        return rc, out.getvalue(), err.getvalue()

    PORT_ERRORS = (OSError(2, 'No such file or directory'),
                   pl.PortBusy(16, '/dev/ttyAMA0 is held by another program (another pitrac-link?)'),
                   pl.PortBusy(16, '/dev/ttyAMA0 is in use by another pitrac-link'))

    def test_v116_f4_cli_cancel_cannot_open_the_port(self):
        # The review's cases, through main(): for `cli-cancel 5eed0001:1`, a port that is not
        # there returned 2 ("cannot open"), and a held one 1 ("Nothing was sent."), neither
        # with the ticket -- but the command asked about may already have run. Now UNKNOWN,
        # exit 5, the ticket, and that the query never went out.
        for e in self.PORT_ERRORS:
            rc, out, err = self.main_with_port(['cli-cancel', '5eed0001:1'], e)
            self.assertEqual(rc, pl.RC_UNKNOWN, e)
            self.assertIn('outcome UNKNOWN for ticket 5eed0001:1', err)
            self.assertIn('the query was never sent', err)
            self.assertIn('pitrac-link cli-cancel 5eed0001:1', err)    # ask again: it may tell
            self.assertNotIn('Nothing was sent', err)
        # A new command keeps its codes: nothing was sent, so nothing ran.
        for e, want in zip(self.PORT_ERRORS, (2, pl.RC_ERROR, pl.RC_ERROR)):
            rc, _, err = self.main_with_port(['cli', 'stat'], e)
            self.assertEqual(rc, want, e)
            self.assertNotIn('UNKNOWN', err)
        # A malformed ticket is no query: not run, and the port is not opened.
        rc, _, err = self.main_with_port(['cli-cancel', 'nonsense'], AssertionError('opened'))
        self.assertEqual(rc, pl.RC_NOT_RUN)
        rc, _, _ = self.main_with_port(['cli-cancel', '1ffffffff:1'], AssertionError('opened'))
        self.assertEqual(rc, pl.RC_NOT_RUN)

    def test_v116_f4_cli_cancel_query_fails_or_is_interrupted(self):
        # Once open: whatever ends the query without an answer leaves the command UNKNOWN.
        clock = Clock()
        board = FakeBoard(clock, outputs={'stat': self.STAT})
        real_link = pl.Link

        class Failing(real_link):
            def cli_cancel(self, session, ticket, stop=None):
                raise self.boom
        for boom, why in ((pl.LinkError('the frame was garbled'), 'garbled'),
                          (ValueError('bad answer'), 'bad answer')):
            Failing.boom = boom
            pl.Link = lambda port: Failing(port, clock=clock)
            try:
                rc, _, err = self.main_with_port(['cli-cancel', '5eed0001:1'], board)
            finally:
                pl.Link = real_link
            self.assertEqual(rc, pl.RC_UNKNOWN, why)
            self.assertIn('UNKNOWN for ticket 5eed0001:1', err)
            self.assertIn(why, err)

        # A real Ctrl-C (SIGINT) while the query waits for its answer (image 1.20: it is
        # counted, and the wait polls it).
        class CtrlCWhileWaiting(real_link):
            def __init__(self, port, **kw):
                real_link.__init__(self, port, **kw)
                self.reads = 0

                def read(timeout):
                    self.reads += 1
                    if self.reads == 1:
                        signal.raise_signal(signal.SIGINT)
                    clock.t += timeout                      # the read's wait, as it passes
                    return b''                              # the answer has not come yet
                port.read = read
        pl.Link = lambda port: CtrlCWhileWaiting(port, clock=clock)
        try:
            rc, _, err = self.main_with_port(['cli-cancel', '5eed0001:1'], board)
        finally:
            pl.Link = real_link
            del board.read
        self.assertEqual(rc, pl.RC_UNKNOWN)
        self.assertIn('UNKNOWN for ticket 5eed0001:1: the query was interrupted (Ctrl-C)', err)

    class TtyError(Exception):
        """As termios.error is: not an OSError (CPython builds it on Exception)."""

    @staticmethod
    def failing_close():
        raise OSError(5, 'Input/output error')

    def test_v117_f3_setup_interrupt_and_close(self):
        # The 1.17 review's entry-point checks, ticket 5eed0001:1. A Ctrl-C while the port
        # is opened, and a tty setup error that is not an OSError, escaped main() with no
        # ticket; a close error after the query replaced the exit 5 already decided.
        e = self.TtyError(25, 'Inappropriate ioctl for device')
        rc, _, err = self.main_with_port(['cli-cancel', '5eed0001:1'], e)
        self.assertEqual(rc, pl.RC_UNKNOWN, e)
        self.assertIn('outcome UNKNOWN for ticket 5eed0001:1', err)
        self.assertIn('cannot open', err)
        # A real Ctrl-C while the port opens (image 1.20: counted, not raised): the query
        # is never sent, and the outcome is UNKNOWN, with the ticket.
        clock = Clock()
        board = FakeBoard(clock, outputs={'stat': self.STAT})

        def opening(path):
            signal.raise_signal(signal.SIGINT)
            return board
        real, real_link = pl.SerialPort, pl.Link
        pl.SerialPort, pl.Link = opening, lambda port: real_link(port, clock=clock)
        err = io.StringIO()
        try:
            with contextlib.redirect_stderr(err):
                rc = pl.main(['cli-cancel', '5eed0001:1'])
        finally:
            pl.SerialPort, pl.Link = real, real_link
        self.assertEqual(rc, pl.RC_UNKNOWN)
        self.assertIn('outcome UNKNOWN for ticket 5eed0001:1: the query was interrupted', err.getvalue())
        self.assertEqual([t for (t, _, _) in board.frames_seen if t == pl.CLI_ABORT], [])
        # The query fails, then so does the close: still 5, and the close is said.
        clock = Clock()
        board = FakeBoard(clock, outputs={'stat': self.STAT})
        board.close = self.failing_close

        class NoAnswer(real_link):
            def cli_cancel(self, session, ticket, stop=None):
                raise pl.LinkError('no answer')
        pl.Link = lambda port: NoAnswer(port, clock=clock)
        try:
            rc, _, err = self.main_with_port(['cli-cancel', '5eed0001:1'], board)
        finally:
            pl.Link = real_link
        self.assertEqual(rc, pl.RC_UNKNOWN)
        self.assertIn('UNKNOWN for ticket 5eed0001:1', err)
        self.assertIn('did not close cleanly', err)

    def test_v117_f3_a_heard_answer_stands(self):
        # The board's answer, once heard, decides the exit: a failing close after it, or
        # a Ctrl-C while it is printed, does not change it.
        link, board = self.setup()
        self.assertEqual(self.run_line(link, 'stat')[0], pl.RC_OK)
        ticket = '%08x:%d' % link.last_ticket
        board.close = self.failing_close
        real_link = pl.Link
        pl.Link = lambda port: real_link(port, clock=link.clock)
        try:
            rc, out, err = self.main_with_port(['cli-cancel', ticket], board)
            self.assertEqual(rc, pl.RC_OK)
            self.assertIn('it ran: it ran to its end', out)
            self.assertIn('did not close cleanly', err)
            rc, out, err = self.main_with_port(['cli', 'stat'], board)     # any command
            self.assertEqual((rc, out), (pl.RC_OK, ''.join(self.STAT)))
            self.assertIn('did not close cleanly', err)
        finally:
            pl.Link = real_link

        class A:
            pass
        a = A()
        a.ticket = ticket
        # A real Ctrl-C as the answer is written: 1.18 lost the words to it (V118-F1). With
        # the tool's handler in (main() puts it in first), it is counted and nothing more.
        pl.quiet_ctrl_c()
        out = Stream(sigint=True)
        with contextlib.redirect_stdout(out):
            self.assertEqual(pl.cmd_cli_cancel(link, a), pl.RC_OK)
        self.assertIn('it ran: it ran to its end', out.getvalue())
        self.assertEqual((out.passed, pl._CtrlC.count), (0, 1))

    def test_v116_n2_a_restart_loses_the_record(self):
        # The review's note: `stat` ran, the board rolled back (UNKNOWN), then came back on
        # protocol 4 -- a new boot, a new session, an empty record. Asking still cannot tell,
        # and the tool must not say it could once the board runs 1.16 or later again.
        link, board = self.setup()
        self.assertEqual(self.run_line(link, 'stat')[0], pl.RC_OK)
        ticket = '%08x:%d' % link.last_ticket
        board.rollback()
        board.proto = 4
        board.restart()

        class A:
            pass
        a = A()
        a.ticket = ticket
        printed = []
        self.assertEqual(pl.cmd_cli_cancel(link, a, out=printed.append, err=printed.append),
                         pl.RC_UNKNOWN)
        text = ' '.join(printed)
        self.assertIn('restarted since', text)
        self.assertIn('No query can recover it', text)
        self.assertIn('[pi] <command>', text)
        self.assertNotIn('1.16 or later', text)
        self.assertEqual(board.cli_runs, ['stat'])

    def test_power_rule(self):
        link, board = self.setup()
        for line in ('forceoff', 'reset force', 'bootsel force', 'forceoff now',
                     'adc5vcal 5.20', 'adc5vcal confirm'):
            rc, out, _ = self.run_line(link, line)
            self.assertEqual(rc, pl.RC_PI_RULE, line)
            self.assertIn('REFUSED', out)
        self.assertEqual(board.cli_runs, [])
        rc, out, _ = self.run_line(link, 'forceoff confirm')
        self.assertEqual((rc, out), (pl.RC_OK, 'FORCE OFF\n'))
        self.assertEqual(board.cli_runs, ['forceoff'])               # 'confirm' removed

    def test_power_rule_off_and_fault(self):
        link, board = self.setup(pi_state='FAULT')
        for line in ('off', 'off confirm', 'fault clear'):
            self.assertEqual(self.run_line(link, line)[0], pl.RC_PI_RULE, line)
        self.assertEqual(board.cli_runs, [])
        self.assertEqual(self.run_line(link, 'fault clear confirm')[0], pl.RC_OK)
        self.assertEqual(board.cli_runs, ['fault clear'])
        link, board = self.setup()                                    # RUNNING
        self.assertEqual(self.run_line(link, 'off')[0], pl.RC_OK)
        self.assertEqual(self.run_line(link, 'fault clear')[0], pl.RC_OK)

    def test_bad_lines_never_leave_the_pi(self):
        link, board = self.setup()
        for line in ('', 'x' * 96, 'stat\tnow', 'café'):
            rc, _, err = self.run_line(link, line)
            self.assertEqual(rc, pl.RC_NOT_RUN, repr(line))
        self.assertEqual(self.sent(board, pl.CLI), [])
        self.assertEqual(self.sent(board, pl.CLI_BEGIN), [])
        rc, _, _ = self.run_line(link, 'x' * 95)                    # the longest line goes
        self.assertEqual(rc, pl.RC_OK)

    def test_older_boards(self):
        for proto in (1, 2):
            link, _ = self.setup(proto=proto)
            with self.assertRaises(pl.LinkError) as e:
                link.cli('stat')
            self.assertIn('image 1.23 on', str(e.exception))
            with self.assertRaises(pl.LinkError):
                pl.cmd_shell(link, None, read=lambda p, stop: 'exit')

    def test_update_still_works_on_older_boards(self):
        for proto in (1, 2):
            clock = Clock()
            board = FakeBoard(clock, proto=proto)
            link = pl.Link(board, clock=clock)
            path = os.path.join(HERE, '_test_image_p%d.bin' % proto)
            with open(path, 'wb') as f:
                f.write(make_image(4096, minor=14))
            try:
                rc = pl.cmd_update(link, Args(path), out=lambda s: None)
            finally:
                os.remove(path)
            self.assertEqual((rc, board.applied), (0, 1))

    def test_two_runs_of_the_tool(self):
        # Two runs of the tool, each with its own (random) seq, sending the same command back
        # to back: both run. 1.13 needed a seq file for this; tickets make it moot.
        clock = Clock()
        board = FakeBoard(clock, outputs={'strobe pulse 20': ['pulse\n']})
        for _ in range(2):
            link = pl.Link(board, clock=clock)
            link.seq = 42
            self.assertEqual(self.run_line(link, 'strobe pulse 20'), (pl.RC_OK, 'pulse\n', ''))
        self.assertEqual(board.cli_runs, ['strobe pulse 20', 'strobe pulse 20'])

    def test_cli_cancel_command(self):
        link, board = self.setup()
        self.assertEqual(self.run_line(link, 'stat')[0], pl.RC_OK)
        ran = '%08x:%d' % link.last_ticket
        issued = '%08x:%d' % link.cli_begin()
        printed = []

        class A:
            pass
        for ticket, want, text in ((ran, pl.RC_OK, 'it ran: it ran to its end'),
                                   (issued, pl.RC_CANCELLED, 'cancelled'),
                                   ('5eed0001:99', pl.RC_UNKNOWN, 'UNKNOWN for ticket'),
                                   ('nonsense', pl.RC_NOT_RUN, None)):
            a = A()
            a.ticket = ticket
            del printed[:]
            self.assertEqual(pl.cmd_cli_cancel(link, a, out=printed.append, err=printed.append),
                             want, ticket)
            if text:
                self.assertIn(text, ' '.join(printed))
        self.assertEqual(board.cli_runs, ['stat'])

    def test_shell(self):
        link, board = self.setup()
        lines = iter(['stat', '', 'nosuch', 'exit'])
        out, err = [], []
        printed = io.StringIO()
        with contextlib.redirect_stdout(printed):
            rc = pl.cmd_shell(link, None, read=lambda p, stop: next(lines), write=out.append,
                              err=err.append)
        self.assertEqual(rc, 0)
        self.assertEqual(board.cli_runs, ['stat', 'nosuch'])
        self.assertIn("? 'nosuch'", ''.join(out))


class Saying(Base):
    """Image 1.19, review finding V118-F1: getting an outcome and saying it are separate.
    Once the tool has it, nothing after changes the exit: a stdout or stderr that cannot
    be written, a Ctrl-C, a failing close, a failing diagnostic about that. Through
    main(), with stdout and stderr replaced by Streams (no descriptor: written directly);
    Writer runs the threaded writer on real pipes, and AsAProcess the whole process."""

    STAT = Cli.STAT

    def board(self, **kw):
        clock = Clock()
        return FakeBoard(clock, outputs={'stat': self.STAT}, **kw), clock

    def ran_stat(self, board, clock):
        """`stat` run once: its ticket, as cli-cancel takes it."""
        link = pl.Link(board, clock=clock)
        self.assertEqual(link.cli('stat').outcome, 'done')
        return '%08x:%d' % link.last_ticket

    def main(self, argv, board, clock, out=None, err=None, link=None):
        """pl.main(argv) against board, stdout and stderr the given Streams, and Link the
        given class: (exit code, stdout's text, stderr's text)."""
        out = Stream() if out is None else out
        err = Stream() if err is None else err
        real_port, real_link = pl.SerialPort, pl.Link
        make = real_link if link is None else link
        pl.SerialPort = lambda path: board
        pl.Link = lambda port: make(port, clock=clock)
        try:
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                rc = pl.main(argv)
        finally:
            pl.SerialPort, pl.Link = real_port, real_link
        return rc, out.getvalue(), err.getvalue()

    def test_v118_f1_closed_stdout_keeps_the_answer(self):
        # The review's first check: `stat` ran once; cli-cancel for its ticket exited 0
        # with stdout working, and 5 (UNKNOWN, "the query failed") with stdout's pipe
        # closed. The answer stands, and goes to stderr.
        board, clock = self.board()
        ticket = self.ran_stat(board, clock)
        rc, out, err = self.main(['cli-cancel', ticket], board, clock)
        self.assertEqual((rc, err), (pl.RC_OK, ''))
        self.assertIn('it ran: it ran to its end', out)
        rc, _, err = self.main(['cli-cancel', ticket], board, clock, out=Stream(dead=True))
        self.assertEqual(rc, pl.RC_OK)
        self.assertIn('(stdout could not be written) it ran: it ran to its end', err)
        self.assertNotIn('UNKNOWN', err)
        # Both streams gone: nothing can be said, and the exit still stands.
        rc, _, _ = self.main(['cli-cancel', ticket], board, clock, out=Stream(dead=True),
                             err=Stream(dead=True))
        self.assertEqual(rc, pl.RC_OK)

    def test_v118_f1_ctrl_c_while_an_unknown_is_said(self):
        # The review's second check: the board restarted, so the ticket is UNKNOWN; a Ctrl-C
        # came while that was printed, and 1.18 exited 5 with the ticket on neither stream.
        board, clock = self.board()
        ticket = self.ran_stat(board, clock)
        board.restart()
        want = 'outcome UNKNOWN for ticket %s' % ticket
        # A real Ctrl-C (SIGINT) as the line is written: counted, and nothing comes through
        # the write (image 1.20: the handler never raises outside a prompt).
        err = Stream(sigint=True)
        rc, _, text = self.main(['cli-cancel', ticket], board, clock, err=err)
        self.assertEqual(rc, pl.RC_UNKNOWN)
        self.assertIn(want, text)
        self.assertEqual((err.passed, pl._CtrlC.count), (0, 1))
        # stderr gone: the UNKNOWN and its ticket go to stdout.
        rc, text, _ = self.main(['cli-cancel', ticket], board, clock, err=Stream(dead=True))
        self.assertEqual(rc, pl.RC_UNKNOWN)
        self.assertIn('(stderr could not be written) pitrac-link: ' + want, text)

    def test_v118_f1_a_failing_close_and_its_failing_diagnostic(self):
        # The review's third check: the answer printed, port.close() failed, and so did the
        # writer of the warning about it; 1.18 let that OSError out of main().
        board, clock = self.board()
        ticket = self.ran_stat(board, clock)

        def failing():
            raise OSError(5, 'Input/output error')
        board.close = failing
        rc, out, _ = self.main(['cli-cancel', ticket], board, clock, err=Stream(dead=True))
        self.assertEqual(rc, pl.RC_OK)
        self.assertIn('it ran: it ran to its end', out)
        self.assertIn('(stderr could not be written) pitrac-link: (the port did not close', out)
        rc, _, _ = self.main(['cli-cancel', ticket], board, clock, out=Stream(dead=True),
                             err=Stream(dead=True))
        self.assertEqual(rc, pl.RC_OK)

        # _close() itself, with a writer that raises: nothing escapes it.
        def raising(text):
            raise OSError(32, 'Broken pipe')
        pl._close(board, raising)

    def test_v118_f1_ctrl_c_after_the_answer(self):
        # Two real Ctrl-Cs once the answer is in: the answer stands, for cli-cancel and for
        # cli. Two before it: cli-cancel's outcome is UNKNOWN, with the ticket. (Sweep does
        # this at every line.)
        board, clock = self.board()
        ticket = self.ran_stat(board, clock)
        real_link = pl.Link

        def ctrl_c_twice():
            signal.raise_signal(signal.SIGINT)
            signal.raise_signal(signal.SIGINT)

        class LateCtrlC(real_link):
            def cli_cancel(self, session, ticket, stop=None):
                r = real_link.cli_cancel(self, session, ticket, stop)
                ctrl_c_twice()
                return r

            def cli(self, line, out=None, stop=None, give_up=None):
                r = real_link.cli(self, line, out, stop, give_up)
                ctrl_c_twice()
                return r
        rc, out, err = self.main(['cli-cancel', ticket], board, clock, link=LateCtrlC)
        self.assertEqual(rc, pl.RC_OK)
        self.assertIn('it ran: it ran to its end', out)
        self.assertNotIn('UNKNOWN', err)
        rc, out, err = self.main(['cli', 'stat'], board, clock, link=LateCtrlC)
        self.assertEqual((rc, out, err), (pl.RC_OK, ''.join(self.STAT), ''))
        self.assertEqual(pl._CtrlC.count, 2)

        class EarlyCtrlC(real_link):
            def request(self, *a, **kw):
                ctrl_c_twice()
                return real_link.request(self, *a, **kw)
        rc, _, err = self.main(['cli-cancel', ticket], board, clock, link=EarlyCtrlC)
        self.assertEqual(rc, pl.RC_UNKNOWN)
        self.assertIn('UNKNOWN for ticket %s: the query was interrupted' % ticket, err)

    def test_v118_f1_cli_output_to_a_closed_stdout(self):
        # `cli` whose output meets a closed stdout: 1.18 took the stdout error for the serial
        # port failing -- UNKNOWN -- for a command that ran to its end. Now the tool listens
        # to the end, the exit is the board's, and stderr says how much went nowhere.
        board, clock = self.board()
        rc, _, err = self.main(['cli', 'stat'], board, clock, out=Stream(dead=True))
        self.assertEqual(rc, pl.RC_OK)
        self.assertIn('%d characters of the command\'s output went nowhere'
                      % len(''.join(self.STAT)), err)
        self.assertNotIn('serial port', err)
        self.assertNotIn('UNKNOWN', err)
        self.assertEqual(board.cli_runs, ['stat'])

    def test_v119_f1_one_decision(self):
        # Image 1.20, review finding V119-F1: the SIGINT handler counts and never raises
        # outside a prompt; a Stop counts from its base; once decide() has an exit status,
        # the first stands and no Stop is acted on.
        pl.quiet_ctrl_c()
        self.assertIs(signal.getsignal(signal.SIGINT), pl._on_sigint)
        early = pl.Stop()
        signal.raise_signal(signal.SIGINT)
        later = pl.Stop()
        self.assertEqual((early(), early.give_up(), later(), pl.Stop(0)()), (True, False, False, True))
        signal.raise_signal(signal.SIGINT)
        self.assertEqual((early.give_up(), later(), later.give_up()), (True, True, False))
        pl.decide(5)
        pl.decide(0)
        self.assertEqual(pl._CtrlC.decided, 5)
        self.assertEqual((early(), early.give_up(), later(), pl.Stop(0)()), (False, False, False, False))
        # main() starts afresh: nothing counted, nothing decided.
        board, clock = self.board()
        self.assertEqual(self.main(['ping'], board, clock)[0], 0)
        self.assertEqual((pl._CtrlC.count, pl._CtrlC.decided), (0, 0))

    def test_v119_f1_main_returns_the_decided_status(self):
        # After the decision, an error the tool catches, or a defect it does not, changes
        # nothing: main() returns the decided status. Before it, a defect is raised as is.
        board, clock = self.board()
        real = pl.show_ping

        def then(exc):
            def handler(link, args, stop=None):
                real(link, args, stop)
                raise exc
            return handler
        try:
            pl.show_ping = then(pl.LinkError('after the answer'))
            rc, out, err = self.main(['ping'], board, clock)
            self.assertEqual(rc, 0)
            self.assertIn('board    : PiTrac RP2354', out)
            self.assertIn('after the answer', err)
            pl.show_ping = then(AttributeError('a defect'))
            rc, _, err = self.main(['ping'], board, clock)
            self.assertEqual(rc, 0)
            self.assertIn('a defect in this tool, after the outcome was decided', err)
            self.assertIn('AttributeError: a defect', err)

            def early(link, args, stop=None):
                raise AttributeError('early')
            pl.show_ping = early
            with self.assertRaises(AttributeError):
                self.main(['ping'], board, clock)
        finally:
            pl.show_ping = real

    def test_v118_f1_the_other_commands(self):
        # A closed stdout changes no other command's exit either; their lines go to stderr.
        # Each decides its exit (image 1.20) before saying it.
        board, clock = self.board()
        for argv, want in ((['ping'], 'board    : PiTrac RP2354'), (['info'], 'version  :'),
                           (['status'], 'power    : RUNNING'), (['hist'], '  boots'),
                           (['upd-status'], 'staged   : idle'), (['abort'], 'aborted')):
            rc, _, err = self.main(argv, board, clock, out=Stream(dead=True))
            self.assertEqual((rc, pl._CtrlC.decided), (0, 0), argv)
            self.assertIn('(stdout could not be written) ' + want, err, argv)
        path = os.path.join(HERE, '_test_image_v120.bin')
        with open(path, 'wb') as f:
            f.write(make_image(4096, minor=14))
        try:
            rc, _, err = self.main(['update', path, '--yes', '--no-poweroff'], board, clock,
                                   out=Stream(dead=True))
            self.assertEqual((rc, board.applied, pl._CtrlC.decided), (0, 1, 0))
            self.assertIn('(stdout could not be written) accepted :', err)
            # No answer at the prompt -- the end of input, a Ctrl-C there -- is no.
            for no in (EOFError, pl.Interrupted, KeyboardInterrupt):
                board, clock = self.board()
                reset_tool()

                def ask(prompt):
                    raise no()
                rc = pl.cmd_update(pl.Link(board, clock=clock), Args(path, yes=False),
                                   out=lambda s: None, ask=ask)
                self.assertEqual((rc, board.applied, board.upd, pl._CtrlC.decided),
                                 (1, 0, 'idle', 1), no)
        finally:
            os.remove(path)

    def test_v119_f1_update_stopped_before_the_apply(self):
        # A stop (a Ctrl-C) at any point before the apply goes out: it never goes out, the
        # board is asked to drop what it was sent, exit 1. Once sent, the apply is waited
        # for, and a Ctrl-C then changes nothing.
        path = os.path.join(HERE, '_test_image_v120s.bin')
        with open(path, 'wb') as f:
            f.write(make_image(2000, minor=20))
        try:
            # The stop turns True once the board has seen `after` frames: while the
            # after-th request (PING, INFO, UPD_STATUS, BEGIN, 9 DATA, END) waits for its
            # answer. Before the BEGIN, no abort (image 1.21, V120-F1).
            for after, sent_any in ((0, False), (2, False), (3, False), (4, True), (7, True),
                                    (14, True)):
                board, clock = self.board()
                link = pl.Link(board, clock=clock)
                reset_tool()
                lines = []
                rc = pl.cmd_update(link, Args(path), out=lines.append,
                                   stop=lambda: len(board.frames_seen) >= after)
                self.assertEqual((rc, board.applied, board.upd, pl._CtrlC.decided),
                                 (1, 0, 'idle', 1), after)
                self.assertIn('not applied: interrupted (Ctrl-C) before the apply went out',
                              lines[-1])
                types = [t for (t, _, _) in board.frames_seen]
                self.assertNotIn(pl.UPD_APPLY, types)
                # Nothing after the stop but the drop: the request it came during got no more.
                self.assertEqual(types[after:], [pl.UPD_ABORT] if sent_any else [], after)
                self.assertIn('none of the image was sent' if not sent_any else 'was dropped', lines[-1])
            # A stop once the image is staged, before the apply: the last look, just before
            # it is written, sees it; and the prompt is not shown.
            for yes in (True, False):
                board, clock = self.board()
                reset_tool()
                lines, asked = [], []
                rc = pl.cmd_update(pl.Link(board, clock=clock), Args(path, yes=yes),
                                   out=lines.append, ask=lambda q: asked.append(q) or 'y',
                                   stop=lambda: any(s.startswith('ready') for s in lines))
                types = [t for (t, _, _) in board.frames_seen]
                self.assertEqual((rc, board.applied, board.upd, asked), (1, 0, 'idle', []), yes)
                self.assertEqual(types[-2:], [pl.UPD_END, pl.UPD_ABORT], yes)
            # Ctrl-Cs while the apply is answered: counted, and the apply's answer decides.
            board, clock = self.board()
            real_apply = board.handle

            def handle(f, ahead=()):
                if f.type == pl.UPD_APPLY:
                    signal.raise_signal(signal.SIGINT)
                    signal.raise_signal(signal.SIGINT)
                return real_apply(f, ahead)
            board.handle = handle
            pl.quiet_ctrl_c()
            rc = pl.cmd_update(pl.Link(board, clock=clock), Args(path), out=lambda s: None,
                               stop=pl.Stop(0))
            self.assertEqual((rc, board.applied, board.upd, pl._CtrlC.count), (0, 1, 'armed', 2))
        finally:
            os.remove(path)

    def test_v119_f2_poweroff_before_a_line(self):
        # Image 1.20, review finding V119-F2: after an accepted apply this Pi's shutdown is
        # requested before anything is said; --no-poweroff leaves it to the board.
        path = os.path.join(HERE, '_test_image_v120p.bin')
        with open(path, 'wb') as f:
            f.write(make_image(2000, minor=20))
        said, real = [], pl.request_poweroff
        try:
            pl.request_poweroff = lambda: said.append('POWEROFF') or 'requested (recorded)'
            for no_poweroff in (False, True):
                board, clock = self.board()
                del said[:]
                rc = pl.cmd_update(pl.Link(board, clock=clock), Args(path, no_poweroff=no_poweroff),
                                   out=said.append)
                self.assertEqual((rc, board.applied), (0, 1))
                at = said.index('accepted : the board accepted APPLY. When the Pi is back, check:')
                if no_poweroff:
                    self.assertNotIn('POWEROFF', said)
                else:
                    self.assertEqual(said[at - 1], 'POWEROFF')       # just before the first line
                    self.assertEqual(said[-1], 'poweroff : requested (recorded)')
        finally:
            pl.request_poweroff = real
            os.remove(path)

    def test_v119_f2_request_poweroff(self):
        # The request itself, with a stand-in command: its exit, a failure's words, a
        # command that cannot run, and one that does not finish in time. (The module puts a
        # stand-in in at import: the suite must never shut down the machine it runs on.)
        self.assertEqual(pl.POWEROFF, [sys.executable, '-c', 'pass'])
        with open(pl.__file__, encoding='utf-8') as f:
            self.assertIn("\nPOWEROFF = ['systemctl', 'poweroff']", f.read())   # the tool's own
        real, wait = pl.POWEROFF, pl.POWEROFF_WAIT_S
        try:
            pl.POWEROFF = [sys.executable, '-c', 'pass']
            self.assertIn('requested', pl.request_poweroff())
            pl.POWEROFF = [sys.executable, '-c', 'import sys; sys.stderr.write("Access denied"); '
                           'sys.exit(1)']
            self.assertIn('failed (exit 1): Access denied', pl.request_poweroff())
            pl.POWEROFF = [os.path.join(HERE, 'no-such-systemctl')]
            self.assertIn('could not run', pl.request_poweroff())
            pl.POWEROFF, pl.POWEROFF_WAIT_S = [sys.executable, '-c', 'import time; time.sleep(5)'], 0.2
            with warnings.catch_warnings():
                # The tool leaves a request that has not finished running (it is the
                # shutdown), so Python warns that its process and pipe are still open.
                warnings.simplefilter('ignore', ResourceWarning)
                self.assertIn('had not finished after 0.2 s', pl.request_poweroff())
                gc.collect()
        finally:
            pl.POWEROFF, pl.POWEROFF_WAIT_S = real, wait

    def test_v118_f1_shell(self):
        # The shell with a closed stdout: each command runs, and the loss is said for each.
        board, clock = self.board()
        link = pl.Link(board, clock=clock)
        lines, err = iter(['stat', 'stat', 'exit']), []
        with contextlib.redirect_stdout(Stream(dead=True)), contextlib.redirect_stderr(Stream()):
            rc = pl.cmd_shell(link, None, read=lambda p, stop: next(lines), err=err.append)
        self.assertEqual((rc, pl._CtrlC.decided), (0, 0))
        self.assertEqual(board.cli_runs, ['stat', 'stat'])
        self.assertEqual(sum('went nowhere' in e for e in err), 2)

    def test_v120_n2_a_ctrl_c_just_before_the_prompt_waits(self):
        # A Ctrl-C counted after the prompt's write, before the wait began: 1.20 waited on
        # for a line. Now the prompt sees it as it begins to wait, and ends at once, as no
        # answer, with nothing read (image 1.21, review note V120-N2).
        pl.quiet_ctrl_c()
        stop = pl.Stop()

        class NoRead(io.StringIO):
            def readline(self, *a):
                raise AssertionError('read after a Ctrl-C')
        real = sys.stdout, sys.stdin
        sys.stdout, sys.stdin = Stream(sigint=True), NoRead()  # the prompt's write: a Ctrl-C
        try:
            with self.assertRaises(pl.Interrupted):
                pl._ask('Apply now? ', stop)
        finally:
            sys.stdout, sys.stdin = real
        self.assertEqual((pl._CtrlC.count, pl._CtrlC.at_prompt), (1, False))

    def test_v120_n2_the_updates_prompt(self):
        # The same through the update's own prompt: the Ctrl-C counted as the prompt is
        # written ends it as no answer, with nothing read: not applied, and this run's image
        # dropped.
        board, clock = self.board()
        link = pl.Link(board, clock=clock)
        path = os.path.join(HERE, '_test_image_v121p.bin')
        with open(path, 'wb') as f:
            f.write(make_image(2000, minor=21))

        class NoRead(io.StringIO):
            def readline(self, *a):
                raise AssertionError('read after a Ctrl-C')
        pl.quiet_ctrl_c()
        lines, real = [], (sys.stdout, sys.stdin)
        sys.stdout, sys.stdin = Stream(sigint=True), NoRead()  # only the prompt goes there
        try:
            rc = pl.cmd_update(link, Args(path, yes=False), out=lines.append, stop=pl.Stop())
        finally:
            sys.stdout, sys.stdin = real
            os.remove(path)
        self.assertEqual((rc, board.upd, board.applied), (1, 'idle', 0))
        self.assertIn('not applied; what the board had of this image was dropped', lines[-1])

    def test_v119_n1_ask(self):
        # The prompt: a reply is a line; the end of input, a stdin that fails, and no stdin
        # at all are EOFError (image 1.20, review note V119-N1: None raised AttributeError).
        # A real Ctrl-C while it waits raises Interrupted -- and only there.
        pl.quiet_ctrl_c()

        class CtrlCInRead(io.StringIO):
            def readline(self, *a):
                signal.raise_signal(signal.SIGINT)
                return super().readline(*a)
        closed = io.StringIO()
        closed.close()
        with contextlib.redirect_stdout(Stream(dead=True)), contextlib.redirect_stderr(Stream()):
            for stdin, want in ((io.StringIO('y\n'), 'y'), (io.StringIO(''), EOFError),
                                (closed, EOFError), (None, EOFError),
                                (CtrlCInRead('y\n'), pl.Interrupted)):
                real, sys.stdin = sys.stdin, stdin
                try:
                    if isinstance(want, str):
                        self.assertEqual(pl._ask('q? '), want)
                    else:
                        self.assertRaises(want, pl._ask, 'q? ')
                finally:
                    sys.stdin = real
                self.assertFalse(pl._CtrlC.at_prompt)
        signal.raise_signal(signal.SIGINT)                  # not at a prompt: counted only
        self.assertEqual(pl._CtrlC.count, 2)
        # The update and the shell, through main(), with no stdin at all.
        board, clock = self.board()
        path = os.path.join(HERE, '_test_image_v120n.bin')
        with open(path, 'wb') as f:
            f.write(make_image(2000, minor=20))
        real = sys.stdin
        try:
            sys.stdin = None
            rc, out, _ = self.main(['update', path, '--no-poweroff'], board, clock)
            self.assertEqual((rc, board.applied, board.upd), (1, 0, 'idle'))
            self.assertIn('not applied', out)
            self.assertEqual(self.main(['shell'], board, clock)[0], 0)
        finally:
            sys.stdin = real
            os.remove(path)

    def test_v118_f1_put(self):
        # A character the stream cannot encode is written escaped, not taken for a failure.
        ascii_out = io.TextIOWrapper(io.BytesIO(), encoding='ascii', newline='\n')
        self.assertTrue(pl._put(ascii_out, 'café\n'))
        self.assertEqual(ascii_out.buffer.getvalue(), b'caf\\xe9\n')
        self.assertTrue(pl._put(ascii_out, 'more\n'))
        self.assertFalse(pl._put(None, 'x\n'))


def fill(fd):
    """Fill a pipe's buffer, so that the next write to it blocks until it is read: the
    bytes written."""
    os.set_blocking(fd, False)
    n = 0
    try:
        while True:
            n += os.write(fd, b'#' * 4096)
    except BlockingIOError:
        pass
    finally:
        os.set_blocking(fd, True)
    return n


class Writer(Base):
    """Image 1.20, review finding V119-F2: a stream with a file descriptor is written by a
    thread of its own; one that takes nothing for WRITE_WAIT_S is given up, and nothing
    waits on it again. Real OS pipes, in this process (AsAProcess runs the whole tool)."""

    def setUp(self):
        super().setUp()
        self.wait, pl.WRITE_WAIT_S = pl.WRITE_WAIT_S, 0.3
        self.fds = []

    def tearDown(self):
        pl.WRITE_WAIT_S = self.wait
        # What is in each pipe is read first, so that no thread is left blocked writing to
        # one when its descriptors close (a test that failed part-way can leave one so).
        for r in self.fds[0::2]:
            try:
                self.read_all(r)
            except OSError:
                pass
        for fd in self.fds:
            try:
                os.close(fd)
            except OSError:
                pass
        super().tearDown()

    def pipe(self, full=False):
        """(read end, a text stream on the write end); self.filled: the bytes put in first."""
        r, w = os.pipe()
        self.fds += [r, w]
        self.filled = fill(w) if full else 0
        return r, open(w, 'w', closefd=False, encoding='utf-8', newline='\n')

    def bounded(self, fn, limit=5.0):
        """fn(), in a thread of its own: what it returns. If it has not returned in limit s,
        the test fails there (a wait without end fails the test; it never hangs the suite)."""
        box = []
        t = threading.Thread(target=lambda: box.append(fn()), daemon=True)
        t.start()
        t.join(limit)
        self.assertFalse(t.is_alive(), 'still waiting after %g s' % limit)
        return box[0]

    def read_all(self, r):
        os.set_blocking(r, False)
        data = b''
        try:
            while True:
                b = os.read(r, 1 << 16)
                if not b:
                    break
                data += b
        except BlockingIOError:
            pass
        os.set_blocking(r, True)
        return data.replace(b'#', b'')

    def test_a_pipe_that_is_read(self):
        r, stream = self.pipe()
        w = pl._writer(stream)
        self.assertTrue(w.put('hello\n'))
        self.assertTrue(w.thread.daemon)
        self.assertEqual(self.read_all(r), b'hello\n')

    def test_a_stalled_pipe_is_given_up(self):
        r, stream = self.pipe(full=True)
        w = pl._writer(stream)
        self.assertIsNotNone(w.fd)                              # written by a thread
        self.assertTrue(self.bounded(lambda: w.put('a\n', wait=False)))   # blocks the thread
        self.assertTrue(self.bounded(lambda: w.put('b\n', wait=False)))   # waits behind it
        t0 = time.monotonic()
        self.assertFalse(self.bounded(lambda: w.put('line\n')))
        took = time.monotonic() - t0
        self.assertTrue(0.25 <= took < 2.0, took)
        self.assertEqual((w.dead, w.stalled), (True, True))
        given_up = (w.written, len(w.queue))
        t0 = time.monotonic()
        self.assertFalse(w.put('another\n'))                    # never waited on again
        self.assertLess(time.monotonic() - t0, 0.05)
        # A line for it goes to the other stream, saying so.
        r2, other = self.pipe()
        real = sys.stdout, sys.stderr
        sys.stdout, sys.stderr = stream, other
        try:
            pl._out('to stdout')
        finally:
            sys.stdout, sys.stderr = real
        self.assertEqual(self.read_all(r2), b'pitrac-link: (stdout could not be written) to stdout\n')
        # Once read, the blocked write ends, and the thread with it: the line it was taking
        # arrives, and nothing queued after it is written to a stream given up. Nor is that
        # line then counted as written: what CommandOutput counts as lost is what was
        # unwritten when the stream was given up (the report says it may still appear).
        got = self.read_all(r)
        w.thread.join(2.0)
        self.assertFalse(w.thread.is_alive())
        self.assertEqual(got + self.read_all(r), b'a\n')
        self.assertEqual((w.written, len(w.queue)), given_up)

    def test_a_closed_pipe_fails(self):
        r, stream = self.pipe()
        os.close(r)
        w = pl._writer(stream)
        self.assertFalse(w.put('x\n'))
        self.assertEqual((w.dead, w.stalled), (True, False))

    def test_a_slow_pipe_is_waited_for(self):
        # Taking something at least every WRITE_WAIT_S, it is not given up, however long
        # all of it takes. A pipe frees room a page at a time (16 KB on the Pi 5's kernel:
        # 4 KB taken every 0.1 s there frees room only every 0.4 s), so the reader takes
        # 64 KB every 0.1 s and each item is 64 KB: one item done per 0.1 s on 4, 16 and
        # 64 KB pages, and the six take about 0.6 s, twice the test's WRITE_WAIT_S.
        chunk = 1 << 16
        r, stream = self.pipe(full=True)
        w = pl._writer(stream)
        for _ in range(6):
            self.assertTrue(w.put('#' * chunk, wait=False))
        stop = threading.Event()

        def reader():                       # non-blocking: it never outlives the test
            left = self.filled + 6 * chunk
            os.set_blocking(r, False)
            while left > 0 and not stop.is_set():
                time.sleep(0.1)
                took = 0
                while took < chunk and left > 0 and not stop.is_set():
                    try:
                        b = os.read(r, min(chunk - took, left))
                    except BlockingIOError:
                        time.sleep(0.001)
                        continue
                    if not b:
                        return
                    took += len(b)
                    left -= len(b)
        t = threading.Thread(target=reader, daemon=True)
        t0 = time.monotonic()
        t.start()
        try:
            self.bounded(w.drain)
            took = time.monotonic() - t0
        finally:
            stop.set()
            t.join(5.0)
        self.assertFalse(t.is_alive())
        self.assertEqual((w.written, w.dead), (6, False))
        self.assertGreater(took, pl.WRITE_WAIT_S)

    def steady_reader(self, r, total, chunk=1 << 16):
        """A thread taking `chunk` bytes from r every 0.1 s, `total` in all, never blocking:
        (thread, stop event, the byte counts taken). Stop and join it before the test ends."""
        stop, got = threading.Event(), []

        def reader():
            left = total
            os.set_blocking(r, False)
            while left > 0 and not stop.is_set():
                time.sleep(0.1)
                took = 0
                while took < chunk and left > 0 and not stop.is_set():
                    try:
                        b = os.read(r, min(chunk - took, left))
                    except BlockingIOError:
                        time.sleep(0.001)
                        continue
                    if not b:
                        return
                    took += len(b)
                    left -= len(b)
                    got.append(len(b))
        t = threading.Thread(target=reader, daemon=True)
        t.start()
        return t, stop, got

    def test_many_short_items_read_slowly(self):
        # Items of one chunk each (4 KB) on a pipe read slowly but steadily: an item's end is
        # then the only sign that the stream took something, and it counts. 160 of them take
        # about a second, several times the test's WRITE_WAIT_S, and the stream is not given up.
        r, stream = self.pipe(full=True)
        w = pl._writer(stream)
        n = 160
        for _ in range(n):
            self.assertTrue(w.put('#' * pl.WRITE_CHUNK, wait=False))
        t0 = time.monotonic()
        t, stop, got = self.steady_reader(r, self.filled + n * pl.WRITE_CHUNK)
        try:
            self.bounded(w.drain, limit=20.0)
            took = time.monotonic() - t0
        finally:
            stop.set()
            t.join(5.0)
        self.assertEqual((w.written, w.dead), (n, False))
        self.assertGreater(took, 2 * pl.WRITE_WAIT_S)

    def test_a_long_item_taken_steadily(self):
        # One item far longer than a pipe holds (1 MB), read steadily, 64 KB every 0.1 s: it
        # takes over a second, several times the test's WRITE_WAIT_S, but the stream takes
        # something every 0.1 s, so it is not given up (image 1.21, review note V120-N1:
        # 1.20 counted only whole items, and gave such a pipe up).
        chunk, size = 1 << 16, 1 << 20
        r, stream = self.pipe()
        w = pl._writer(stream)
        stop, got = threading.Event(), []

        def reader():
            left = size
            os.set_blocking(r, False)
            while left > 0 and not stop.is_set():
                time.sleep(0.1)
                took = 0
                while took < chunk and left > 0 and not stop.is_set():
                    try:
                        b = os.read(r, min(chunk - took, left))
                    except BlockingIOError:
                        time.sleep(0.001)
                        continue
                    if not b:
                        return
                    took += len(b)
                    left -= len(b)
                    got.append(len(b))
        t = threading.Thread(target=reader, daemon=True)
        t0 = time.monotonic()
        t.start()
        try:
            ok = self.bounded(lambda: w.put('#' * size), limit=20.0)
            took = time.monotonic() - t0
            t.join(10.0)
        finally:
            stop.set()
            t.join(5.0)
        self.assertTrue(ok)
        self.assertEqual((w.written, w.dead, sum(got)), (1, False, size))
        self.assertGreater(took, 2 * pl.WRITE_WAIT_S)

    def test_queued_output_does_not_wait(self):
        # A command's output to a stalled stdout: queued at once, never waited for while it
        # streams; refused once something has waited WRITE_WAIT_S; drain() is bounded; all
        # of it counted as lost.
        r, stream = self.pipe(full=True)
        real = sys.stdout
        sys.stdout = stream
        try:
            output = pl.CommandOutput()
            t0 = time.monotonic()
            for _ in range(100):
                output('tick\n')
            self.assertLess(time.monotonic() - t0, 0.1)
            time.sleep(pl.WRITE_WAIT_S + 0.1)
            output('late\n')                                   # given up here
            self.assertTrue(pl._writer(stream).stalled)
            t0 = time.monotonic()
            output.drain()
            self.assertLess(time.monotonic() - t0, 0.05)
        finally:
            sys.stdout = real
        self.assertEqual(output.lost, 100 * 5 + 5)
        said = []
        output.report(said.append)
        self.assertIn('505 characters of the command\'s output went nowhere', said[0])


class Sweep(Base):
    """Image 1.20, review finding V119-F1, line by line: for each command, a run with a
    real SIGINT (one, or two) at each line of pitrac_link.py that the run executes, in
    turn -- through main(), against the fake board, stdout and stderr Streams. Each run
    starts as a fresh process does: Python's own SIGINT handler, the tool's state empty.

    - Before main() has put its handler in, Python's KeyboardInterrupt is the only allowed
      end (nothing has been sent).
    - Once the run has entered decide(), it must end exactly as the run without a Ctrl-C:
      the same exit status, the same words on both streams.
    - Before that, it must end in one of the outcomes a Ctrl-C there is allowed, true of
      the board; every one of them must occur somewhere in the sweep (a Ctrl-C that is
      never acted on would leave one out); and after the Ctrl-C, at most one request
      other than a stop (CLI_ABORT) or a drop (UPD_ABORT) reaches the board: the one whose
      last look for a Ctrl-C came before it.
    - Every run's exit status is the one decide() recorded.
    - No run drops an update it did not begin: a UPD_ABORT only after this run's
      UPD_BEGIN (image 1.21, review finding V120-F1). The update is swept against a board
      that holds an earlier update, verified or armed, too."""

    STAT = Cli.STAT
    IMAGE = os.path.join(HERE, '_test_image_sweep.bin')
    STOPS = (pl.CLI_ABORT, pl.UPD_ABORT)

    @classmethod
    def setUpClass(cls):
        with open(cls.IMAGE, 'wb') as f:
            f.write(make_image(600, minor=20))

    @classmethod
    def tearDownClass(cls):
        os.remove(cls.IMAGE)

    def board(self, ran_stat=True, prep=None):
        clock = Clock()
        board = FakeBoard(clock, outputs={'stat': self.STAT})
        if ran_stat:
            self.assertEqual(pl.Link(board, clock=clock).cli('stat').outcome, 'done')
        if prep is not None:
            prep(board)
        return board, clock

    def run_main(self, argv, board, clock, k=None, n=1, stdin=None, port=None):
        """One run, as a fresh process starts it: Python's own SIGINT handler, the tool's
        per-run state empty. rc is 'KeyboardInterrupt' if that escaped main()."""
        out, err = Stream(), Stream()
        tracer = Sigints(k, n, hook=lambda: len(board.frames_seen))
        real_port, real_link, real_in = pl.SerialPort, pl.Link, sys.stdin
        reset_tool()
        signal.signal(signal.SIGINT, signal.default_int_handler)

        def opened(path):
            if isinstance(port, BaseException):
                raise port
            return board
        pl.SerialPort = opened
        pl.Link = lambda p: real_link(p, clock=clock)
        sys.stdin = io.StringIO(stdin or '')
        try:
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                sys.settrace(tracer)
                try:
                    rc = pl.main(argv)
                except KeyboardInterrupt:
                    rc = 'KeyboardInterrupt'
                finally:
                    sys.settrace(None)
        finally:
            pl.SerialPort, pl.Link, sys.stdin = real_port, real_link, real_in
        return rc, out.getvalue(), err.getvalue(), tracer

    @staticmethod
    def same(text):
        return re.sub(r'in \d+\.\d s', 'in _ s', text)

    def sweep(self, argv, before, outcomes, ns=(1, 2), stdin=None, port=None, quiet=True,
              prep=None):
        """Every line, n Ctrl-Cs at it, for each n. before(rc, out, err, board, where)
        checks a run whose Ctrl-C came before decide(); outcomes[n] is the set of exit
        statuses those runs must show, all of them. quiet: at most one request but a stop
        or a drop after the Ctrl-C (not for the shell, which reads its next command).
        prep(board): the board as each run finds it. Returns the number of runs."""
        board, clock = self.board(prep=prep)
        rc0, out0, err0, t0 = self.run_main(argv, board, clock, stdin=stdin, port=port)
        self.only_its_own_drop(board, argv, argv)
        self.assertEqual(pl._CtrlC.decided, rc0, argv)
        self.assertIsNotNone(t0.decided_at, argv)
        runs = 0
        for n in ns:
            seen = set()
            for k in range(t0.events):
                board, clock = self.board(prep=prep)
                rc, out, err, t = self.run_main(argv, board, clock, k=k, n=n, stdin=stdin,
                                                port=port)
                where = '%s, %d Ctrl-C at line event %d of %d (decide() at %d)' % (
                    argv, n, k, t0.events, t0.decided_at)
                runs += 1
                self.only_its_own_drop(board, argv, where)
                if rc == 'KeyboardInterrupt':
                    # Python's own handler: only before main() has put the tool's in.
                    self.assertLess(k, t0.quiet_at, where)
                    continue
                self.assertGreaterEqual(k, t0.quiet_at, where)
                self.assertEqual(pl._CtrlC.decided, rc, where)
                if k >= t0.decided_at:
                    self.assertEqual((rc, self.same(out), self.same(err)),
                                     (rc0, self.same(out0), self.same(err0)), where)
                    continue
                before(rc, out, err, board, where)
                seen.add(rc)
                if quiet:
                    after = [f for (f, _, _) in board.frames_seen[t.seen:] if f not in self.STOPS]
                    self.assertLessEqual(len(after), 1, '%s: %s' % (where, after))
            self.assertEqual(seen, set(outcomes[n] if isinstance(outcomes, dict) else outcomes),
                             '%s, %d Ctrl-C' % (argv, n))
        return runs

    def only_its_own_drop(self, board, argv, where):
        """A UPD_ABORT reaches the board only after this run's UPD_BEGIN (V120-F1), but
        for `pitrac-link abort`, whose UPD_ABORT is the operator's. The setup's frames are
        CLI_BEGIN and CLI only."""
        if argv[0] == 'abort':
            return
        types = [f for (f, _, _) in board.frames_seen]
        if pl.UPD_ABORT in types:
            self.assertIn(pl.UPD_BEGIN, types[:types.index(pl.UPD_ABORT)], where)

    def test_v119_f1_cli_cancel(self):
        def before(rc, out, err, board, where):
            if rc == pl.RC_UNKNOWN:
                self.assertIn('outcome UNKNOWN for ticket 5eed0001:1: the query was '
                              'interrupted (Ctrl-C)', err, where)
            else:
                self.assertIn('it ran: it ran to its end', out, where)   # never "not run"
        self.sweep(['cli-cancel', '5eed0001:1'], before, {pl.RC_OK, pl.RC_UNKNOWN})

    def test_v119_f1_cli(self):
        def before(rc, out, err, board, where):
            if rc in (pl.RC_OK, pl.RC_RAN_TO_END):
                self.assertEqual((board.cli_runs, out), (['stat', 'stat'], ''.join(self.STAT)), where)
            elif rc == pl.RC_CANCELLED:
                self.assertEqual(board.cli_runs, ['stat'], where)         # only the setup's
                self.assertIn('nothing ran', err, where)
            elif rc == pl.RC_UNKNOWN:
                self.assertIn('outcome UNKNOWN for ticket 5eed0001:2: this tool stopped '
                              'waiting for its answer (a second Ctrl-C), after asking the '
                              'board to stop it', err, where)
                self.assertIn(pl.CLI_ABORT, [f for (f, _, _) in board.frames_seen], where)
            else:
                self.fail('%s: exit %d' % (where, rc))
        # One Ctrl-C: not sent (6), a stop after the line (8: stat had finished), or after the
        # answer (0). Two: before the line, not sent (6); once it may be out, the first one's
        # stop goes out and then the second gives up waiting (5), in the same look (image
        # 1.21, V120-F2: 1.20 gave up first, with no stop sent); after the answer, 0.
        self.sweep(['cli', 'stat'], before, {1: {pl.RC_OK, pl.RC_CANCELLED, pl.RC_RAN_TO_END},
                                             2: {pl.RC_OK, pl.RC_CANCELLED, pl.RC_UNKNOWN}})

    def test_v120_f2_two_ctrl_cs_still_stop_it(self):
        # The review's case, at every line: a command that keeps running (`slow`, 3 s) and two
        # Ctrl-Cs together. Whenever the outcome is UNKNOWN (given up), the board was asked to
        # stop that command, by its ticket, before the tool stopped waiting: 1.20 sent no stop
        # and left it running.
        def before(rc, out, err, board, where):
            aborts = [f for (f, _, _) in board.frames_seen if f == pl.CLI_ABORT]
            if rc == pl.RC_UNKNOWN:
                self.assertGreaterEqual(len(aborts), 1, where)
                self.assertTrue(board.running is None or board.running['stop'], where)
            elif rc == pl.RC_CANCELLED:
                self.assertEqual(board.cli_runs, ['stat'], where)
            else:
                self.assertEqual(rc, pl.RC_OK, where)            # after its answer
        self.sweep(['cli', 'slow'], before, {2: {pl.RC_OK, pl.RC_CANCELLED, pl.RC_UNKNOWN}},
                   ns=(2,))

    def test_v119_f1_update(self):
        def before(rc, out, err, board, where):
            if rc == 0:
                self.assertEqual((board.applied, board.upd), (1, 'armed'), where)
                self.assertIn('accepted :', out, where)
            else:
                self.assertEqual((rc, board.applied, board.upd), (1, 0, 'idle'), where)
                self.assertIn('not applied: interrupted (Ctrl-C) before the apply went out',
                              out, where)
        self.sweep(['update', self.IMAGE, '--yes', '--no-poweroff'], before, {0, 1})

    def test_v119_f1_update_at_the_prompt(self):
        def before(rc, out, err, board, where):
            if rc == 0:
                self.assertEqual((board.applied, board.upd), (1, 'armed'), where)
            else:
                self.assertEqual((rc, board.applied, board.upd), (1, 0, 'idle'), where)
                self.assertIn('not applied', out, where)
        self.sweep(['update', self.IMAGE, '--no-poweroff'], before, {0, 1}, ns=(1,), stdin='y\n')

    def test_v120_f1_an_earlier_update_is_left_alone(self):
        # The review's case, at every line: the board holds an earlier update, verified or
        # armed. Whatever happens -- a Ctrl-C anywhere, or none -- this run sends no BEGIN and
        # no ABORT, and that update is as it was (1.20's abort turned both to idle).
        for state in ('verified', 'armed'):
            def before(rc, out, err, board, where, state=state):
                types = [f for (f, _, _) in board.frames_seen]
                self.assertEqual(rc, 1, where)
                self.assertEqual((board.upd, board.applied), (state, 0), where)
                self.assertNotIn(pl.UPD_BEGIN, types, where)
                self.assertNotIn(pl.UPD_ABORT, types, where)
            runs = self.sweep(['update', self.IMAGE, '--yes', '--no-poweroff'], before, {1},
                              ns=(1,), prep=lambda b, state=state: b.stage(state))
            board, clock = self.board(prep=lambda b, state=state: b.stage(state))
            rc, out, err, _ = self.run_main(['update', self.IMAGE, '--yes', '--no-poweroff'],
                                            board, clock)
            before(rc, out, err, board, state)
            self.assertIn('the board already holds an update (%s, version 1.9)' % state, err)
            self.assertGreater(runs, 50)

    def test_v119_f1_queries_and_abort(self):
        for argv, said in ((['ping'], 'board    : PiTrac RP2354'), (['info'], 'version  :'),
                           (['status'], 'power    : RUNNING'), (['hist'], '  boots'),
                           (['upd-status'], 'staged   : idle')):
            def before(rc, out, err, board, where, said=said):
                if rc == 0:
                    self.assertIn(said, out, where)
                else:
                    self.assertEqual(rc, pl.RC_INTERRUPTED, where)
                    self.assertIn('interrupted (Ctrl-C) before the board answered', err, where)
                    self.assertEqual(out, '', where)
            self.sweep(argv, before, {0, pl.RC_INTERRUPTED}, ns=(1,))

        def waited_for(rc, out, err, board, where):           # abort: always waited for
            self.assertEqual((rc, out), (0, 'aborted\n'), where)
        self.sweep(['abort'], waited_for, {0}, ns=(1,))

    def test_v119_f1_shell(self):
        # 130 only for a Ctrl-C before the board answered the shell's first ping; after
        # that, each command is as `cli`, and a Ctrl-C at the prompt gives a fresh one.
        def before(rc, out, err, board, where):
            if rc == pl.RC_INTERRUPTED:
                self.assertEqual(board.cli_runs, ['stat'], where)
                self.assertIn('interrupted (Ctrl-C) before the board answered', err, where)
                return
            self.assertEqual(rc, 0, where)
            self.assertIn(board.cli_runs, (['stat'], ['stat', 'stat']), where)
        self.sweep(['shell'], before, {0, pl.RC_INTERRUPTED}, stdin='stat\nexit\n', quiet=False)

    def test_v119_f1_terminal_failures(self):
        # Exits decided without the board: each the same, wherever the Ctrl-C lands.
        for argv, port, want in (
                (['ping'], OSError(2, 'No such file or directory'), 2),
                (['ping'], pl.PortBusy(16, '/dev/ttyAMA0 is held'), pl.RC_ERROR),
                (['cli-cancel', 'nonsense'], None, pl.RC_NOT_RUN),
                (['cli-cancel', '5eed0001:1'], pl.PortBusy(16, '/dev/ttyAMA0 is held'), pl.RC_UNKNOWN),
                (['update', os.path.join(HERE, 'no-such-image.bin')], None, 2)):
            def same(rc, out, err, board, where, want=want):
                self.assertEqual(rc, want, where)
                self.assertEqual([f for (f, _, _) in board.frames_seen          # the setup's
                                  if f not in (pl.CLI_BEGIN, pl.CLI)], [], where)    # stat only
            self.sweep(argv, same, {want}, ns=(1,), port=port)


def main_in_process(argv, board, clock, stdin=''):
    """pl.main(argv) against board, as Sweep runs it but with no tracer: (rc, out, err)."""
    out, err = Stream(), Stream()
    real_port, real_link, real_in = pl.SerialPort, pl.Link, sys.stdin
    reset_tool()
    pl.SerialPort = lambda path: board
    pl.Link = lambda p: real_link(p, clock=clock)
    sys.stdin = io.StringIO(stdin)
    try:
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = pl.main(argv)
    finally:
        pl.SerialPort, pl.Link, sys.stdin = real_port, real_link, real_in
    return rc, out.getvalue(), err.getvalue()


def sigints(n):
    """n real Ctrl-Cs, now."""
    for _ in range(n):
        signal.raise_signal(signal.SIGINT)


class V120(Base):
    """Image 1.21: the 1.20 review's findings, each through main() against the fake board,
    whose port runs the tool's own write_bounded() (V120-F3), whose UPD_STATUS says its
    real state and whose UPD_BEGIN is refused while armed (V120-F1)."""

    STAT = Cli.STAT
    YES = ['--yes', '--no-poweroff']

    def setUp(self):
        super().setUp()
        self.dir = tempfile.mkdtemp(prefix='pitrac-link-v120-')
        self.image = os.path.join(self.dir, 'image.bin')
        with open(self.image, 'wb') as f:
            f.write(make_image(2000, minor=21))

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)
        super().tearDown()

    def board(self, **kw):
        clock = Clock()
        return FakeBoard(clock, outputs={'stat': self.STAT}, **kw), clock

    @staticmethod
    def types(board):
        return [f for (f, _, _) in board.frames_seen]

    @staticmethod
    def after(board, ftype, then):
        """then(board) just after the board has handled its first frame of ftype."""
        real = board.handle

        def handle(f, ahead=()):
            r = real(f, ahead)
            if f.type == ftype and not getattr(handle, 'done', False):
                handle.done = True
                then(board)
            return r
        board.handle = handle

    # -- V120-F1: an update never drops one it did not begin ------------------------------
    def test_f1_an_earlier_update_is_never_dropped(self):
        for state in ('receiving', 'verified', 'armed'):
            board, clock = self.board()
            board.stage(state)
            rc, out, err = main_in_process(['update', self.image] + self.YES, board, clock)
            self.assertEqual((rc, board.upd, board.applied), (1, state, 0), state)
            self.assertEqual(self.types(board), [pl.PING, pl.INFO, pl.UPD_STATUS], state)
            self.assertIn('the board already holds an update (%s' % state, err)
            self.assertIn('drops nothing', err)

    def test_f1_a_stop_while_the_begin_waits_for_the_port(self):
        # The port takes nothing from the BEGIN on, and a Ctrl-C comes while the BEGIN waits
        # for room: none of it went out, so nothing is dropped.
        board, clock = self.board()
        self.after(board, pl.UPD_STATUS, lambda b: b.stall())
        board.on_wait = lambda b: b.waits == 3 and sigints(1)
        rc, out, err = main_in_process(['update', self.image] + self.YES, board, clock)
        self.assertEqual(rc, 1)
        self.assertIn('interrupted (Ctrl-C) before the apply went out; none of the image was sent',
                      out)
        self.assertEqual(self.types(board), [pl.PING, pl.INFO, pl.UPD_STATUS])
        self.assertEqual(board.waits, 3)                     # seen at the next look

    def test_f1_this_runs_image_is_dropped_when_it_goes_no_further(self):
        # After this run's BEGIN, what the board holds is this run's: a transfer that fails
        # (every DATA answer lost) and a refused apply (the board is not RUNNING the Pi) both
        # drop it.
        board, clock = self.board(drop={(pl.UPD_DATA, k) for k in range(1, 50)})
        rc, out, err = main_in_process(['update', self.image] + self.YES, board, clock)
        self.assertEqual((rc, board.upd), (1, 'idle'))
        self.assertEqual(self.types(board)[-1], pl.UPD_ABORT)
        self.assertIn('no answer from the board', err)
        self.assertIn('Not applied: what the board had of this image was dropped', err)
        board, clock = self.board(state=4)
        rc, out, err = main_in_process(['update', self.image] + self.YES, board, clock)
        self.assertEqual((rc, board.upd, board.applied), (1, 'idle', 0))
        self.assertEqual(self.types(board)[-2:], [pl.UPD_APPLY, pl.UPD_ABORT])
        self.assertIn('UPD_APPLY: POWER', err)
        self.assertIn('was dropped', err)
        # A BEGIN the board refused: it holds none of this image, and nothing is dropped.
        board, clock = self.board(capacity=1000)
        board.capacity = 1000                       # INFO says more; the BEGIN finds less
        real_info = board.handle

        def handle(f, ahead=()):
            if f.type == pl.INFO:
                board.capacity = 288 * 1024
                r = real_info(f, ahead)
                board.capacity = 1000
                return r
            return real_info(f, ahead)
        board.handle = handle
        rc, out, err = main_in_process(['update', self.image] + self.YES, board, clock)
        self.assertEqual((rc, board.upd), (1, 'idle'))
        self.assertIn('UPD_BEGIN: SIZE', err)
        self.assertNotIn(pl.UPD_ABORT, self.types(board))

    def test_f1_an_apply_that_may_have_gone_out_is_not_dropped(self):
        # The apply's answers all lost: it may have been taken (here it was: armed). An
        # abort would stop the install, so none goes out.
        board, clock = self.board(drop={(pl.UPD_APPLY, k) for k in range(1, 20)})
        rc, out, err = main_in_process(['update', self.image] + self.YES, board, clock)
        self.assertEqual((rc, board.upd, board.applied), (1, 'armed', 1))
        self.assertNotIn(pl.UPD_ABORT, self.types(board))
        self.assertEqual(err, 'Apply outcome unknown; nothing was dropped. Check upd-status.\n')
        # An apply the port takes none of in WRITE_TIMEOUT_S never went out: this run's
        # image is dropped once the port takes again.
        board, clock = self.board()
        self.after(board, pl.UPD_END, lambda b: b.stall(until=clock.t + 1.5))
        rc, out, err = main_in_process(['update', self.image] + self.YES, board, clock)
        self.assertEqual((rc, board.upd, board.applied), (1, 'idle', 0))
        self.assertNotIn(pl.UPD_APPLY, self.types(board))
        self.assertEqual(self.types(board)[-1], pl.UPD_ABORT)
        self.assertIn('the apply never went out; what the board had of this image was dropped', err)
        # An apply the port took only part of: it may have gone out as far as this run can
        # tell, so nothing is dropped (the board in fact dropped the cut frame, and the
        # image stays verified, for `pitrac-link abort` or the next update to find).
        board, clock = self.board()
        self.after(board, pl.UPD_END, lambda b: b.stall(after=3, until=clock.t + 5.0))
        rc, out, err = main_in_process(['update', self.image] + self.YES, board, clock)
        self.assertEqual((rc, board.upd, board.applied), (1, 'verified', 0))
        self.assertNotIn(pl.UPD_ABORT, self.types(board))
        self.assertEqual(err, 'Apply outcome unknown; nothing was dropped. Check upd-status.\n')

    def test_f1_a_ctrl_c_once_the_apply_is_out(self):
        # The apply's first answer is lost, and Ctrl-Cs come once the board has it (armed).
        # The apply is out, so it is sent again and waited for, whatever the Ctrl-Cs: its
        # answer decides, 0. A stop seen at the resend's write, or while it waits, would
        # make it "not applied" and drop an armed install.
        board, clock = self.board(drop={(pl.UPD_APPLY, 1)})
        real = board.handle

        def handle(f, ahead=()):
            r = real(f, ahead)
            if f.type == pl.UPD_APPLY and board.applied == 1 and not getattr(handle, 'done', 0):
                handle.done = 1
                sigints(2)
            return r
        board.handle = handle
        rc, out, err = main_in_process(['update', self.image] + self.YES, board, clock)
        self.assertEqual((rc, board.upd, board.applied), (0, 'armed', 1))
        self.assertEqual(self.types(board).count(pl.UPD_APPLY), 2)
        self.assertNotIn(pl.UPD_ABORT, self.types(board))
        self.assertIn('accepted :', out)

    # -- V120-F2: two Ctrl-Cs still send the stop -----------------------------------------
    def test_f2_the_reviews_case(self):
        # `chatty` through main(), Ctrl-C on its first tick: one -- one CLI_ABORT, it stops,
        # 7; two together -- the same CLI_ABORT goes out first, then the tool stops waiting
        # (5, with the ticket), and the board has the stop. 1.20 sent none for two.
        for n, want in ((1, pl.RC_STOPPED), (2, pl.RC_UNKNOWN)):
            board, clock = self.board()
            board.on_tick = lambda b, k, n=n: k == 1 and sigints(n)
            rc, out, err = main_in_process(['cli', 'chatty'], board, clock)
            self.assertEqual(rc, want, n)
            self.assertEqual(self.types(board).count(pl.CLI_ABORT), 1, n)
            if n == 2:
                self.assertTrue(board.running['stop'])
                self.assertIn('after asking the board to stop it', err)
                self.assertIn('cli-cancel 5eed0001:1', err)
            else:
                self.assertIsNone(board.running)

    # -- V120-F3: every write is bounded ---------------------------------------------------
    def test_f3_a_port_that_takes_nothing(self):
        # A query: a LinkError once WRITE_TIMEOUT_S has passed (the fake clock), exit 1.
        for argv in (['ping'], ['abort']):
            board, clock = self.board()
            board.stall()
            rc, out, err = main_in_process(argv, board, clock)
            self.assertEqual(rc, 1, argv)
            self.assertIn('the serial port took none of a 6-byte frame in 1 s', err)
            self.assertAlmostEqual(clock.t, pl.WRITE_TIMEOUT_S, delta=pl.WRITE_POLL_S)
            self.assertEqual(board.frames_seen, [])
        # Two Ctrl-Cs while a query waits for the port: acted on at the next look, 130.
        board, clock = self.board()
        board.stall()
        board.on_wait = lambda b: b.waits == 2 and sigints(2)
        rc, out, err = main_in_process(['ping'], board, clock)
        self.assertEqual(rc, pl.RC_INTERRUPTED)
        self.assertEqual(board.waits, 2)

    def test_f3_a_command_line_the_port_does_not_take(self):
        # The review's case: the port takes nothing from the command line on, two Ctrl-Cs
        # while the line waits, then the port takes again. The line never goes out: 6,
        # nothing ran, and nothing more is written (1.20 wrote it once the port was free,
        # the board ran it, and the exit was UNKNOWN).
        board, clock = self.board()
        self.after(board, pl.CLI_BEGIN, lambda b: b.stall(until=clock.t + 0.5))
        board.on_wait = lambda b: b.waits == 2 and sigints(2)
        rc, out, err = main_in_process(['cli', 'stat'], board, clock)
        self.assertEqual(rc, pl.RC_CANCELLED)
        self.assertIn('stopped before the command was sent', err)
        clock.t += 1.0
        board.read(0.0)
        self.assertEqual((self.types(board), board.cli_runs), ([pl.CLI_BEGIN], []))
        # No Ctrl-C: none of the line in WRITE_TIMEOUT_S is a link error, 1, nothing ran.
        board, clock = self.board()
        self.after(board, pl.CLI_BEGIN, lambda b: b.stall())
        rc, out, err = main_in_process(['cli', 'stat'], board, clock)
        self.assertEqual(rc, pl.RC_ERROR)
        self.assertIn('none of the command line went out, so nothing ran', err)
        self.assertEqual(board.cli_runs, [])
        # Part of it, then nothing: UNKNOWN, with the ticket. The board dropped the frame
        # cut short, so cli-cancel finds that it never ran, and cancels it.
        board, clock = self.board()
        self.after(board, pl.CLI_BEGIN, lambda b: b.stall(after=5, until=clock.t + 2.0))
        rc, out, err = main_in_process(['cli', 'stat'], board, clock)
        self.assertEqual(rc, pl.RC_UNKNOWN)
        self.assertIn('only part of the command line went out', err)
        self.assertIn('cli-cancel 5eed0001:1', err)
        clock.t += 2.0
        r = pl.Link(board, clock=clock).cli_cancel(0x5EED0001, 1)
        self.assertEqual((r.outcome, board.cli_runs), ('cancelled', []))

    def test_f3_a_stop_while_the_apply_waits_for_the_port(self):
        # The port takes nothing from the apply on, and a Ctrl-C comes while the apply waits
        # for room: none of it went out, so it never will; this run's image is dropped once
        # the port takes again. Not applied, 1.
        board, clock = self.board()
        self.after(board, pl.UPD_END, lambda b: b.stall(until=clock.t + 0.5))
        board.on_wait = lambda b: b.waits == 2 and sigints(1)
        rc, out, err = main_in_process(['update', self.image] + self.YES, board, clock)
        self.assertEqual((rc, board.upd, board.applied), (1, 'idle', 0))
        self.assertNotIn(pl.UPD_APPLY, self.types(board))
        self.assertEqual(self.types(board)[-1], pl.UPD_ABORT)
        self.assertIn('not applied: interrupted (Ctrl-C) before the apply went out; what the '
                      'board had of this image was dropped', out)

    def test_f3_a_stop_the_port_does_not_take(self):
        # A running command, a Ctrl-C, and the port takes nothing from then: the stop cannot
        # be sent within WRITE_TIMEOUT_S, and the outcome is UNKNOWN, with the ticket.
        board, clock = self.board()

        def tick(b, k):
            if k == 1:
                sigints(1)
                b.stall()
        board.on_tick = tick
        rc, out, err = main_in_process(['cli', 'chatty'], board, clock)
        self.assertEqual(rc, pl.RC_UNKNOWN)
        self.assertIn('the stop could not be sent', err)
        self.assertNotIn(pl.CLI_ABORT, self.types(board))

    def test_f3_serial_port_write(self):
        # SerialPort.write() itself, with os.write() and select() stubbed: the port's own
        # loop, as the review exercised it.
        port = object.__new__(pl.SerialPort)
        port.fd = 99
        frame = pl.encode(pl.PING, 1)

        def run(writes, timeout=0.3, stop=None):
            seq, selects = list(writes), []

            def os_write(fd, view):
                v = seq.pop(0) if seq else 'block'
                if v == 'block':
                    raise BlockingIOError()
                return v

            def select(r, w, x, t):
                selects.append(t)
                if len(selects) > 400:      # 20 s of looks: a write without a deadline
                    raise AssertionError('a write waited for room without end')
                time.sleep(t)
                return [], [], []
            with mock.patch('os.write', os_write), mock.patch('select.select', select):
                return port.write(frame, timeout, stop)
        t0 = time.monotonic()
        with self.assertRaises(pl.WriteTimeout) as e:
            run([])                                         # never writable
        self.assertEqual(e.exception.taken, 0)
        self.assertTrue(0.3 <= time.monotonic() - t0 < 0.6)
        looks = []
        with self.assertRaises(pl.WriteStopped):            # a stop while it waits
            run([], timeout=5.0, stop=lambda: looks.append(1) or len(looks) > 3)
        answers = iter([False] + [True] * 1000)
        with self.assertRaises(pl.WriteTimeout) as e:       # part, then nothing; a stop
            run([4], stop=lambda: next(answers))            # once a byte is out is not seen
        self.assertEqual(e.exception.taken, 4)
        self.assertEqual(run(['block', 'block', 2, 'block', len(frame) - 2]), len(frame))

    @unittest.skipUnless(os.name == 'posix', 'needs a POSIX pty')
    def test_f3_serial_port_write_on_a_pty(self):
        # A real tty whose other end reads nothing: the write ends in WRITE_TIMEOUT_S with
        # none of the frame taken; a stop while it waits ends it at once; and once the
        # other end reads, the same write goes through.
        import pty
        import select
        master, slave = pty.openpty()
        port = pl.SerialPort(os.ttyname(slave))
        frame = pl.encode(pl.PING, 1)
        try:
            try:
                while True:
                    os.write(port.fd, b'#' * 256)
            except BlockingIOError:
                pass
            t0 = time.monotonic()
            real_select = select.select

            def watched_select(r, w, x, timeout):
                # The port is real; the independent stop is the test's, not the
                # deadline we deliberately remove in P109.
                self.assertLess(time.monotonic() - t0, 3.0, 'write ignored its deadline')
                return real_select(r, w, x, timeout)

            with mock.patch('select.select', watched_select), self.assertRaises(pl.WriteTimeout) as e:
                port.write(frame, 0.3)
            self.assertEqual(e.exception.taken, 0)
            self.assertTrue(0.3 <= time.monotonic() - t0 < 1.0)
            soon = time.monotonic() + 0.1
            with self.assertRaises(pl.WriteStopped):
                port.write(frame, 5.0, stop=lambda: time.monotonic() > soon)

            def drain():
                time.sleep(0.1)
                os.set_blocking(master, False)
                try:
                    while os.read(master, 1 << 16):
                        pass
                except BlockingIOError:
                    pass
            t = threading.Thread(target=drain, daemon=True)
            t.start()
            self.assertEqual(port.write(frame, 2.0), len(frame))
            t.join(2.0)
        finally:
            port.close()
            os.close(master)
            os.close(slave)

    # -- V120-F4: no thread for a stream ---------------------------------------------------
    def test_f4_no_thread_for_a_stream(self):
        # A stream whose writer thread cannot start is unavailable at once, with nothing
        # left queued; a line goes to the other stream; with neither, nothing is said, and
        # nothing raises.
        fds = []
        try:
            streams = []
            for _ in range(2):
                r, w = os.pipe()
                fds += [r, w]
                streams.append(open(w, 'w', closefd=False, encoding='utf-8'))
            real = sys.stdout, sys.stderr
            sys.stdout, sys.stderr = streams
            try:
                with mock.patch.object(threading.Thread, 'start',
                                       side_effect=RuntimeError("can't start new thread")):
                    w0 = pl._writer(streams[0])
                    self.assertFalse(w0.put('x\n'))
                    self.assertEqual((w0.dead, w0.queued, len(w0.queue), w0.thread),
                                     (True, 0, 0, None))
                    self.assertFalse(pl._say('a line'))
            finally:
                sys.stdout, sys.stderr = real
        finally:
            for fd in fds:
                os.close(fd)

    def test_f4_a_defect_after_the_decision_that_cannot_be_said(self):
        # Decided 0, then a defect in this tool, and saying it fails as well: main() still
        # returns 0 (1.20 let the second failure out, and the process exited 1).
        board, clock = self.board()

        def show_ping(link, args, stop=None):
            pl.decide(0)
            raise RuntimeError('a defect')
        with mock.patch.object(pl, 'show_ping', show_ping), \
                mock.patch.object(pl, '_err', side_effect=RuntimeError('no thread')):
            rc, out, err = main_in_process(['ping'], board, clock)
        self.assertEqual(rc, 0)


class AsAProcess(Base):
    """V118-F1, V119-F1 and V119-F2 through the process entry point, as the reviews asked:
    pitrac_link.main() run as its own process by fake_board_process.py, against the tests'
    FakeBoard, with stdout and stderr real OS pipes -- one whose reading end is closed
    before the process starts, so every write to it fails, or one already full, so a
    write to it blocks until it is read. The exit status is the process's, the
    interpreter's flush at exit included. In the process, `stat` has run once, as ticket
    5eed0001:1."""

    DRIVER = os.path.join(HERE, 'fake_board_process.py')
    TICKET = '5eed0001:1'

    def setUp(self):
        super().setUp()
        self.dir = tempfile.mkdtemp(prefix='pitrac-link-test-')

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)
        super().tearDown()

    def image(self):
        path = os.path.join(self.dir, 'image.bin')
        with open(path, 'wb') as f:
            f.write(make_image(600, minor=20))
        return path

    def start(self, scenario, argv, close=(), full=(), unbuffered=False, env=None):
        """The driver, started: (process, {'out': read end, 'err': read end}). close names the
        pipes whose reading end is closed first, full those filled before it starts."""
        ends = {}
        for name in ('out', 'err'):
            r, w = os.pipe()
            if name in full:
                fill(w)
            if name in close:
                os.close(r)
                r = None
            ends[name] = (r, w)
        p = subprocess.Popen([sys.executable, '-B'] + (['-u'] if unbuffered else []) +
                             [self.DRIVER, scenario] + list(argv), stdin=subprocess.DEVNULL,
                             stdout=ends['out'][1], stderr=ends['err'][1],
                             env=dict(os.environ, **(env or {})))
        for r, w in ends.values():
            os.close(w)
        return p, {name: r for name, (r, w) in ends.items()}

    @staticmethod
    def collect(p, reads, timeout=60):
        """(exit status, stdout, stderr), read to their ends; a full pipe's filler dropped.
        A process still running after timeout s is killed, and the test fails: a tool that
        never ends fails its test, and never hangs the suite."""
        killed = []

        def kill():
            if p.poll() is None:
                killed.append(True)
                p.kill()
        timer = threading.Timer(timeout, kill)
        timer.daemon = True
        timer.start()
        try:
            texts = {}
            for name, r in reads.items():
                if r is None:
                    texts[name] = ''
                else:
                    with os.fdopen(r, 'rb') as f:
                        texts[name] = f.read().replace(b'#', b'').decode('utf-8', 'replace')
            rc = p.wait(timeout=timeout)
        finally:
            timer.cancel()
        if killed:
            raise AssertionError('the tool was still running after %d s: killed. stderr: %s'
                                 % (timeout, texts['err'][-2000:]))
        return rc, texts['out'], texts['err']

    def run_process(self, scenario, argv, close=(), unbuffered=False, env=None):
        p, reads = self.start(scenario, argv, close=close, unbuffered=unbuffered, env=env)
        return self.collect(p, reads)

    def test_v118_f1_closed_stdout(self):
        # The review's check, exactly: buffered and unbuffered.
        rc, out, err = self.run_process('', ['cli-cancel', self.TICKET])
        self.assertEqual((rc, err), (pl.RC_OK, ''))
        self.assertIn('it ran: it ran to its end', out)
        for unbuffered in (False, True):
            rc, _, err = self.run_process('', ['cli-cancel', self.TICKET], close=('out',),
                                          unbuffered=unbuffered)
            self.assertEqual(rc, pl.RC_OK, err)
            self.assertIn('(stdout could not be written) it ran: it ran to its end', err)

    def test_v118_f1_both_closed_and_the_close_fails(self):
        rc, _, _ = self.run_process('close-fails', ['cli-cancel', self.TICKET],
                                    close=('out', 'err'))
        self.assertEqual(rc, pl.RC_OK)

    def test_v118_f1_ctrl_c_while_the_unknown_is_said(self):
        rc, _, err = self.run_process('restart,sigint-on-stderr', ['cli-cancel', self.TICKET])
        self.assertEqual(rc, pl.RC_UNKNOWN, err)
        self.assertIn('outcome UNKNOWN for ticket %s' % self.TICKET, err)
        rc, out, _ = self.run_process('restart', ['cli-cancel', self.TICKET], close=('err',))
        self.assertEqual(rc, pl.RC_UNKNOWN)
        self.assertIn('outcome UNKNOWN for ticket %s' % self.TICKET, out)

    def test_v118_f1_cli_and_ping_to_a_closed_stdout(self):
        rc, _, err = self.run_process('', ['cli', 'stat'], close=('out',))
        self.assertEqual(rc, pl.RC_OK, err)
        self.assertIn('characters of the command\'s output went nowhere', err)
        self.assertNotIn('serial port', err)
        rc, _, err = self.run_process('', ['ping'], close=('out',))
        self.assertEqual(rc, 0, err)
        self.assertIn('(stdout could not be written) board    : PiTrac RP2354', err)

    @unittest.skipUnless(os.name == 'posix', 'needs a POSIX shell')
    def test_v118_f1_no_stdout_at_all(self):
        # BENCH_P8_PI.md 8.11 Step 5's `>&-`: the process starts with descriptor 1 closed,
        # and Python then has no sys.stdout at all (None).
        for argv, want in ((['cli-cancel', self.TICKET], '(stdout could not be written) it ran'),
                           (['cli', 'stat'], 'output went nowhere')):
            p = subprocess.run(['sh', '-c', 'exec "$@" >&-', 'sh', sys.executable, '-B',
                                self.DRIVER, ''] + argv, stdin=subprocess.DEVNULL,
                               stderr=subprocess.PIPE, timeout=60, universal_newlines=True)
            self.assertEqual(p.returncode, pl.RC_OK, p.stderr)
            self.assertIn(want, p.stderr)

    @unittest.skipUnless(os.name == 'posix', 'needs a POSIX shell')
    def test_v119_n1_no_stdin_at_all(self):
        # The review's note: descriptor 0 closed, so Python has no sys.stdin. The update's
        # prompt takes that as no answer (1, nothing applied, the staged image dropped), and
        # the shell leaves, 0.
        state = os.path.join(self.dir, 'state.json')
        for argv, want, board in ((['update', self.image(), '--no-poweroff'], 1,
                                   {'applied': 0, 'upd': 'idle'}),
                                  (['shell'], 0, None)):
            p = subprocess.run(['sh', '-c', 'exec "$@" <&-', 'sh', sys.executable, '-B',
                                self.DRIVER, ''] + argv, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, timeout=60, universal_newlines=True,
                               env=dict(os.environ, PITRAC_TEST_STATE=state))
            self.assertEqual(p.returncode, want, p.stderr)
            self.assertNotIn('Traceback', p.stderr)
            if board:
                with open(state) as f:
                    got = json.load(f)
                self.assertEqual({k: got[k] for k in board}, board)

    def test_v120_f4_no_thread_for_either_stream(self):
        # The review's reproduction: stat's ticket queried through main() in a fresh process
        # with every thread start refused. Decided 0, and the process exits 0 (1.20: 1, with
        # a traceback). Nothing can be said: both streams stay empty.
        rc, out, err = self.run_process('no-threads', ['cli-cancel', self.TICKET])
        self.assertEqual((rc, out, err), (0, '', ''))
        rc, out, err = self.run_process('no-threads', ['ping'])
        self.assertEqual((rc, out, err), (0, '', ''))
        # Only stdout's writer refused: the answer goes to stderr, saying so.
        rc, out, err = self.run_process('first-thread-fails', ['cli-cancel', self.TICKET])
        self.assertEqual((rc, out), (0, ''))
        self.assertIn('(stdout could not be written) it ran: it ran to its end', err)

    def test_v119_f2_poweroff_while_stdout_is_stalled(self):
        # The review's reproduction: an accepted apply, stdout open but not read. The
        # shutdown request is made while stdout is still not read; the tool gives stdout up
        # after WRITE_WAIT_S, says the rest on stderr, and exits 0. --no-poweroff: none.
        image = self.image()
        for no_poweroff in (False, True):
            mark = os.path.join(self.dir, 'poweroff-%d' % no_poweroff)
            p, reads = self.start('', ['update', image, '--yes'] +
                                  (['--no-poweroff'] if no_poweroff else []), full=('out',),
                                  env={'PITRAC_TEST_POWEROFF_MARK': mark})
            if not no_poweroff:
                t0 = time.monotonic()
                while not os.path.exists(mark) and time.monotonic() - t0 < 30:
                    time.sleep(0.05)
                self.assertTrue(os.path.exists(mark))           # stdout not read yet
            try:
                rc = p.wait(timeout=30)                          # nor now: it exits anyway
            finally:
                rc, out, err = self.collect(p, reads)
            self.assertEqual(rc, 0, err)
            self.assertEqual(os.path.exists(mark), not no_poweroff)
            self.assertIn('(stdout could not be written) accepted :', err)
            if not no_poweroff:
                self.assertIn('(stdout could not be written) poweroff : requested', err)

    def test_v119_f2_a_stop_is_not_held_up_by_stdout(self):
        # A command printing all the while, stdout open but not read, and a Ctrl-C at its
        # third line: the stop goes out, the board stops it, and the tool exits 7 (stopped)
        # without stdout ever being read. 1.19 blocked on the first line.
        p, reads = self.start('chatty-sigint=3', ['cli', 'chatty'], full=('out',))
        try:
            p.wait(timeout=30)                              # stdout still not read
        finally:
            rc, out, err = self.collect(p, reads)
        self.assertEqual(rc, pl.RC_STOPPED, err)
        self.assertIn('(stopped: it ended early)', err)
        self.assertIn('characters of the command\'s output went nowhere', err)

    def control(self, argv, env):
        trace = os.path.join(self.dir, 'trace.json')
        rc, out, err = self.run_process('', argv, env=dict(env, PITRAC_TEST_TRACE=trace))
        with open(trace) as f:
            t = json.load(f)
        return rc, out, err, t['events'], t['decided_at']

    def process_sweep(self, argv, before, lead=40):
        """Sweep's check through the process: two real SIGINTs at each line event from `lead`
        events before decide() to the end."""
        state = os.path.join(self.dir, 'state.json')
        env = {'PITRAC_TEST_STATE': state}
        rc0, out0, err0, events, d = self.control(argv, env)
        self.assertIsNotNone(d)
        for k in range(max(0, d - lead), events):
            rc, out, err = self.run_process('', argv, env=dict(env, PITRAC_TEST_SIGINT_AT=str(k),
                                                               PITRAC_TEST_SIGINT_N='2'))
            where = '%s: 2 Ctrl-C at line event %d of %d (decide() at %d)' % (argv, k, events, d)
            if k >= d:
                self.assertEqual((rc, Sweep.same(out), Sweep.same(err)),
                                 (rc0, Sweep.same(out0), Sweep.same(err0)), where)
            else:
                with open(state) as f:
                    before(rc, out, err, json.load(f), where)

    def test_v119_f1_cli_cancel_as_a_process(self):
        def before(rc, out, err, board, where):
            self.assertIn(rc, (pl.RC_OK, pl.RC_UNKNOWN), where)
            if rc == pl.RC_UNKNOWN:
                self.assertIn('UNKNOWN for ticket 5eed0001:1: the query was interrupted', err, where)
        self.process_sweep(['cli-cancel', self.TICKET], before)

    def test_v119_f1_apply_as_a_process(self):
        def before(rc, out, err, board, where):
            if rc == 0:
                self.assertEqual((board['applied'], board['upd']), (1, 'armed'), where)
            else:
                self.assertEqual((rc, board['applied'], board['upd']), (1, 0, 'idle'), where)
        self.process_sweep(['update', self.image(), '--yes', '--no-poweroff'], before)


@unittest.skipUnless(hasattr(os, 'openpty') and os.name == 'posix', 'needs a POSIX pty')
class PortLock(Base):
    """V113-F1: the port is one pitrac-link's alone before a byte moves."""

    def test_second_open_is_refused(self):
        import fcntl
        master, slave = os.openpty()
        try:
            path = os.ttyname(slave)
            first = pl.SerialPort(path, baud=115200)
            try:
                with self.assertRaises(pl.PortBusy):
                    pl.SerialPort(path, baud=115200)
                # Each lock on its own: the flock (against another pitrac-link, whatever its
                # user) ...
                with self.assertRaises(OSError):
                    fcntl.flock(slave, fcntl.LOCK_EX | fcntl.LOCK_NB)
                # ... and TIOCEXCL (against any other program's open(), but root's).
                if os.geteuid() != 0:
                    with self.assertRaises(OSError):
                        os.close(os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK))
            finally:
                first.close()
            pl.SerialPort(path, baud=115200).close()                  # free again once closed
        finally:
            os.close(master)
            os.close(slave)

    def run_tool(self, *argv):
        import subprocess
        tool = os.path.join(os.path.dirname(HERE), 'pitrac_link.py')
        p = subprocess.run([sys.executable, '-B', tool] + list(argv), stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, timeout=30, universal_newlines=True)
        return p.returncode, p.stderr

    def test_v116_f4_cli_cancel_as_a_process(self):
        # The process itself (image 1.17, V116-F4): a port that is not there, and one this
        # test holds -- UNKNOWN, exit 5, the ticket. A new `cli` still says nothing was sent.
        rc, err = self.run_tool('--port', '/dev/pitrac-no-such-tty', 'cli-cancel', '5eed0001:1')
        self.assertEqual(rc, pl.RC_UNKNOWN, err)
        self.assertIn('UNKNOWN for ticket 5eed0001:1', err)
        master, slave = os.openpty()
        try:
            path = os.ttyname(slave)
            held = pl.SerialPort(path, baud=115200)
            try:
                rc, err = self.run_tool('--port', path, 'cli-cancel', '5eed0001:1')
                self.assertEqual(rc, pl.RC_UNKNOWN, err)
                self.assertIn('the query was never sent', err)
                rc, err = self.run_tool('--port', path, 'cli', 'stat')
                self.assertEqual(rc, pl.RC_ERROR, err)
                self.assertIn('Nothing was sent', err)
            finally:
                held.close()
        finally:
            os.close(master)
            os.close(slave)

    def test_v117_f3_not_a_tty(self):
        # A device that opens but is no tty (a plain file): termios raises termios.error,
        # not an OSError, and 1.17 let it escape. Now it is an OSError: cli-cancel is
        # UNKNOWN, exit 5, with the ticket; a new command cannot open it, exit 2.
        import tempfile
        with tempfile.NamedTemporaryFile() as f:
            with self.assertRaises(OSError):
                pl.SerialPort(f.name, baud=115200)
            rc, err = self.run_tool('--port', f.name, 'cli-cancel', '5eed0001:1')
            self.assertEqual(rc, pl.RC_UNKNOWN, err)
            self.assertIn('UNKNOWN for ticket 5eed0001:1', err)
            self.assertIn('cannot be set up as a serial port', err)
            rc, err = self.run_tool('--port', f.name, 'cli', 'stat')
            self.assertEqual(rc, 2, err)
            self.assertIn('cannot open', err)


class V121Process(Base):
    """1.21 confirm findings. Each observation is one real process and one clean board."""

    IDENT = struct.pack('<II', 0x5EED0001, 1)
    UNKNOWN = 'Apply outcome unknown; nothing was dropped. Check upd-status.\n'
    ACCEPTED = ('accepted : the board accepted APPLY. When the Pi is back, check:\n'
                '           pitrac-link upd-status\n')
    DROP = 'what the board had of this image was dropped'
    CLI_BEGIN = pl.encode(pl.CLI_BEGIN, 1)
    STAT = pl.encode(pl.CLI, 2, IDENT + b'stat')
    STOP = pl.encode(pl.CLI_ABORT, 3, IDENT)
    PING = pl.encode(pl.PING, 1)

    def setUp(self):
        super().setUp()
        self.dir = Path(HERE) / ('_v121_' + uuid4().hex)
        self.dir.mkdir()
        self.addCleanup(shutil.rmtree, self.dir)
        self.data = make_image(300, minor=22)
        (self.dir / 'image.bin').write_bytes(self.data)
        self.query = [self.PING, pl.encode(pl.INFO, 2), pl.encode(pl.UPD_STATUS, 3)]
        begin = struct.pack('<I', len(self.data)) + hashlib.sha256(self.data).digest()
        self.begin = pl.encode(pl.UPD_BEGIN, 4, begin)
        self.staged = self.query + [self.begin,
                                    pl.encode(pl.UPD_DATA, 5, bytes(4) + self.data[:240]),
                                    pl.encode(pl.UPD_DATA, 6, struct.pack('<I', 240) + self.data[240:]),
                                    pl.encode(pl.UPD_END, 7)]
        self.apply = pl.encode(pl.UPD_APPLY, 8)

    @staticmethod
    def unknown_cli(why):
        return ('pitrac-link: outcome UNKNOWN for ticket 5eed0001:1: %s. '
                '`pitrac-link cli-cancel 5eed0001:1` reports how it ended, or cancels it '
                'if it has not run.\n' % why)

    @staticmethod
    def cause(n, size, mode):
        return ('the serial port took %s of a %d-byte frame %s' %
                ('%d bytes' % n if n else 'none', size, 'in 1 s (its output is stuck)'
                 if mode == 'timeout' else 'before [Errno 5] Input/output error'))

    def check_process(self, case, argv, exit_code, stderr, wire, *, upd='idle', applied=0,
                      ticket=None, signals=(), reads=(), prompts=(), stdout='', poweroffs=0,
                      runs=(), running=None):
        """wire: complete frames or (attempted frame, length actually taken). A complete
        prefix alone is not delivery: check the decoder independently. Never infer return
        status from main() or from text. Keep raw stdout/stderr (including line endings)."""
        key = case.replace(':', '-').replace(',', '-')
        state = self.dir / (key + '.json')
        reading = self.dir / (key + '.reading')
        mark = self.dir / (key + '.poweroff')
        env = {k: v for k, v in os.environ.items() if not k.startswith('PITRAC_TEST_')}
        env.update(PITRAC_TEST_STATE=str(state), PITRAC_TEST_READING=str(reading),
                   PITRAC_TEST_POWEROFF_MARK=str(mark), PYTHONIOENCODING='utf-8')
        cmd = [sys.executable, '-B', str(Path(HERE) / 'fake_board_process.py'), 'v121:' + case] + argv
        with subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, cwd=self.dir, env=env) as proc:
            try:
                if case == 'shell':
                    # Keep stdin open and empty through the first prompt and signal. Only
                    # supply 'exit' once readline actually starts; record WHICH prompt read.
                    until = time.monotonic() + 10
                    while not reading.exists() and proc.poll() is None and time.monotonic() < until:
                        time.sleep(0.01)
                out, err = proc.communicate(b'exit\n' if case == 'shell' else b'', timeout=15)
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.communicate()
        self.assertTrue(state.exists(), (proc.returncode, out, err))
        board = json.loads(state.read_bytes())
        recorded = mark.read_bytes() if mark.exists() else b''
        if os.environ.get('PITRAC_V121_EVIDENCE'):
            print('CASE ' + json.dumps(dict(case=case, argv=argv, os_exit=proc.returncode,
                                            stdout=out.decode(), stderr=err.decode(),
                                            poweroff_record=recorded.decode(), **board)), flush=True)
        # Progress is not a verdict. Only these complete update progress lines are optional;
        # a contradictory preamble, duplicate outcome, traceback, etc. must not disappear.
        terminal = out.decode().replace('\r\n', '\n')
        if argv[0] == 'update':
            terminal = re.sub(r'^(?:image    : image\.bin, 300 B, version 1\.22, try-before-you-buy, '
                              r'SHA-256 [0-9a-f]{16}|board    : version 1\.1 \(built Oct 19 2026 '
                              r'12:34:56\) at 0x002000; installs into 0x100000|sending  : +(?:80|100)%|'
                              r'sent     : 300 B in \d+\.\d s, 0 retries; the board checked the '
                              r'SHA-256 and the image|ready    : version 1\.22 is staged in the '
                              r"board's RAM)\n", '', terminal, flags=re.MULTILINE)
        expected = [(frame, len(frame)) if isinstance(frame, bytes) else frame for frame in wire]
        # One assertion for the classification, OS status, wire order, accepted bytes and
        # final state. In particular UNKNOWN/5 alone would miss a command which really ran.
        got = (proc.returncode, terminal, err.decode().replace('\r\n', '\n'),
               [(a['frame'], a['taken']) for a in board['attempts']], board['delivered'],
               board['upd'], board['applied'], board['cli_runs'], board['tickets'],
               board['signals'], board['reads'], board['prompts'], recorded, board['running'])
        want = (exit_code, stdout, stderr,
                [(f.hex(), f[:n].hex()) for f, n in expected],
                [f.hex() for f, n in expected if len(f) == n], upd, applied, list(runs),
                {'1': ticket} if ticket else {}, list(signals), list(reads), list(prompts),
                b'1' * poweroffs, running)
        self.assertEqual(got, want, case)
        return board['attempts']

    def test_v122_f1_signal_and_retry_failure_together(self):
        cmd = pl.encode(pl.CLI, 2, self.IDENT + b'long')
        for n, ending, room in [(0, 'eio', 'open'), (1, 'partial-eio', 'open'),
                                (1, 'wake', 'open'), (2, 'wake', 'open'),
                                (1, 'eio', 'open'), (2, 'eio', 'open'),
                                (1, 'eio', 'blocked'), (2, 'eio', 'blocked')]:
            with self.subTest(n=n, ending=ending, room=room):
                taken = 2 if ending == 'partial-eio' else 0
                wire = [self.CLI_BEGIN, cmd, (cmd, taken)]
                abort = n > 0 and taken == 0
                stopped = abort and n == 1 and room == 'open'
                if abort:
                    wire.append(self.STOP if room == 'open' else (self.STOP, 0))
                if not abort:
                    why = self.cause(taken, len(cmd), 'wait-eio') + ', sending the command line again'
                elif room == 'blocked':
                    why = self.cause(0, len(self.STOP), 'timeout') + ': the stop could not be sent'
                else:
                    why = ('this tool stopped waiting for its answer (a second Ctrl-C), '
                           'after asking the board to stop it')
                attempts = self.check_process(
                    'running-retry:%d:%s:%s' % (n, ending, room), ['cli', 'long'],
                    7 if stopped else 5, '(stopped: it ended early)\n' if stopped else self.unknown_cli(why),
                    wire, ticket='ran', signals=[[pl.CLI, cmd[:taken].hex(), ['long']]] * n,
                    runs=['long'], running=None if stopped else [1, abort and room == 'open'],
                    stdout='aborted\n' if stopped else '')
                if room == 'blocked':
                    self.assertAlmostEqual(attempts[-1]['end'] - attempts[-1]['start'],
                                           pl.WRITE_TIMEOUT_S)

    def test_f1_stopping_a_blocked_retry(self):
        for n in (1, 2):
            for room in ('release', 'blocked'):
                with self.subTest(n=n, room=room):
                    delivered = room == 'release'
                    why = ('this tool stopped waiting for its answer (a second Ctrl-C), after '
                           'asking the board to stop it' if delivered else
                           self.cause(0, len(self.STOP), 'timeout') + ': the stop could not be sent')
                    cancelled = delivered and n == 1
                    attempts = self.check_process(
                        'retry:%d:%s' % (n, room), ['cli', 'stat'], 6 if cancelled else 5,
                        '(cancelled before it ran: nothing ran)\n' if cancelled else self.unknown_cli(why),
                        [self.CLI_BEGIN, self.STAT, (self.STAT, 0),
                         self.STOP if delivered else (self.STOP, 0)],
                        ticket='cancelled' if delivered else 'issued', signals=[[pl.CLI, '', []]] * n)
                    retry, abort = attempts[-2:]
                    self.assertLess(retry['end'] - retry['start'], 0.3)
                    self.assertLessEqual(abort['end'] - abort['start'], 1.000001)

    def test_f2_byte_counts(self):
        for typ in (pl.CLI, pl.UPD_BEGIN, pl.UPD_APPLY):
            for n in (0, 2):
                for mode in ('timeout', 'write-eio', 'wait-eio'):
                    with self.subTest(typ=typ, n=n, mode=mode):
                        frame = {pl.CLI: self.STAT, pl.UPD_BEGIN: self.begin, pl.UPD_APPLY: self.apply}[typ]
                        why = self.cause(n, len(frame), mode)
                        case = 'fault:%d:%d:%s' % (typ, n, mode)
                        if typ == pl.CLI:
                            why += (': only part of the command line went out. The board drops a '
                                    'frame cut short, but that it did is not established here'
                                    if n else ': none of the command line went out, so nothing ran')
                            self.check_process(case, ['cli', 'stat'], 5 if n else 1,
                                               self.unknown_cli(why) if n else 'pitrac-link: ' + why + '\n',
                                               [self.CLI_BEGIN, (frame, n)], ticket='issued')
                        else:
                            wire = (self.query if typ == pl.UPD_BEGIN else self.staged) + [(frame, n)]
                            err = 'pitrac-link: ' + why
                            if typ == pl.UPD_BEGIN and n:
                                wire.append(pl.encode(pl.UPD_ABORT, 5))
                                err += '. Not applied: ' + self.DROP
                            if typ == pl.UPD_APPLY and not n:
                                wire.append(pl.encode(pl.UPD_ABORT, 9))
                                err += '. Not applied: the apply never went out; ' + self.DROP
                            possible = typ == pl.UPD_APPLY and n
                            self.check_process(case, ['update', 'image.bin', '--yes'], 1,
                                               self.UNKNOWN if possible else err + '\n', wire,
                                               upd='verified' if possible else 'idle')

    def test_f2_uncounted_port_error_is_not_proof_of_zero(self):
        self.check_process('uncounted:%d' % pl.UPD_APPLY, ['update', 'image.bin', '--yes'], 1,
                           self.UNKNOWN, self.staged + [(self.apply, 0)], upd='verified')

    def test_f3_no_write_at_or_after_expiry(self):
        for case, n, puts in [('progress', 4, [0, .25, .5, .75]), ('wake', 2, [0, 0])]:
            with self.subTest(case=case):
                attempts = self.check_process('deadline:' + case, ['ping'], 1,
                                              'pitrac-link: ' + self.cause(n, 6, 'timeout') + '\n',
                                              [(self.PING, n)])
                self.assertEqual(attempts[0]['puts'], puts)
                self.assertGreaterEqual(attempts[0]['end'], 1.0)

    def test_f4_lost_ack_and_acceptance(self):
        argv = ['update', 'image.bin', '--yes']
        self.check_process('lost-acks', argv, 1, self.UNKNOWN, self.staged + [self.apply] * 8,
                           upd='armed', applied=1)

    def test_f4_accepted_not_completed(self):
        self.check_process('accepted', ['update', 'image.bin', '--yes', '--no-poweroff'], 0, '',
                           self.staged + [self.apply], upd='armed', applied=1, stdout=self.ACCEPTED)

    def test_f4_retry_cannot_speak_for_the_first_apply(self):
        argv = ['update', 'image.bin', '--yes']
        for fault, retry, state in [('eio', (self.apply, 0), 'armed'),
                                    ('state', self.apply, 'idle')]:
            with self.subTest(fault=fault):
                self.check_process('apply-retry:' + fault, argv, 1, self.UNKNOWN,
                                   self.staged + [self.apply, retry], upd=state, applied=1)

    def test_f4_accepted_survives_output_and_close(self):
        case = 'accepted-output-close,close-fails'
        mark = self.dir / (case.replace(',', '-') + '.poweroff')
        command = '%s -c import sys; open(sys.argv[1], "ab").write(b"1") %s' % (sys.executable, mark)
        lines = (self.ACCEPTED + 'poweroff : requested (`%s`)\n' % command).splitlines()
        err = ''.join('pitrac-link: (stdout could not be written) ' + line + '\n' for line in lines)
        err += "pitrac-link: (the port did not close cleanly: OSError(5, 'Input/output error'))\n"
        self.check_process(case, ['update', 'image.bin', '--yes'], 0, err,
                           self.staged + [self.apply], upd='armed', applied=1, poweroffs=1)

    def test_n2_fresh_shell_prompt_before_read(self):
        self.check_process('shell', ['shell'], 0, '', [self.PING], prompts=['pi> ', 'pi> '],
                           reads=[2], stdout="board    : version 1.1, the CLI over the link. "
                           "Ctrl-C stops a command; 'exit' leaves.\npi> \npi> ")


class UpdStatus(Base):
    """What upd-status prints for each result the board can report."""

    class OneAnswer:
        def __init__(self, **kw):
            self.s = dict(status=0, state=0, error=0, size=0, received=0, version=0,
                          result=0, frm=0, to=0, detail=0)
            self.s.update(kw)
            self.s['from'] = self.s.pop('frm')

        def upd_status(self, stop=None):
            return self.s

    def shown(self, **kw):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            pl.show_upd_status(self.OneAnswer(**kw), None)
        return out.getvalue()

    def test_each_result(self):
        self.assertIn('none since', self.shown())
        self.assertIn('installed, 1.1 -> 1.2', self.shown(result=1, frm=0x10001, to=0x10002))
        self.assertIn('bought itself', self.shown(result=1))
        self.assertIn('WITHOUT a trial', self.shown(result=1, detail=1))
        self.assertIn('never bought itself', self.shown(result=2))
        self.assertIn('fresh', self.shown(result=2, detail=2))
        self.assertIn('INSTALL FAILED', self.shown(result=3, detail=pl.ERRORS.index('FLASH')))
        self.assertIn('was cleared', self.shown(result=3, detail=pl.ERRORS.index('FLASH')))
        self.assertIn('cut short', self.shown(result=3, detail=pl.ERRORS.index('INTERRUPTED')))

    def test_cleanup_is_a_warning(self):
        text = self.shown(result=3, detail=pl.ERRORS.index('CLEANUP'))
        self.assertIn('CLEANUP', text)
        self.assertIn('Do not reset or power-cycle', text)
        self.assertIn('reflash it over USB', text)


@unittest.skipUnless(os.path.isfile(os.path.join(SRC, 'fw_update.h')), 'firmware sources not here')
class InStepWithTheFirmware(Base):
    """The tool's tables against the C enums they mirror."""

    @staticmethod
    def source(name):
        """A firmware source file's text, the file closed again (1.18's tests left it open,
        and Python warned: a note in the 1.18 brief)."""
        with open(os.path.join(SRC, name), encoding='utf-8') as f:
            return f.read()

    def enum(self, header, name):
        text = self.source(header)
        body = re.search(r'typedef enum \{([^{}]*)\}\s*' + name + ';', text).group(1)
        return [m.group(1) for m in re.finditer(r'^\s*([A-Z][A-Z0-9_]*)', body, re.M)]

    def test_errors(self):
        names = [n for n in self.enum('fw_update.h', 'fwu_err_t') if not n.endswith('__COUNT')]
        self.assertEqual(['ok' if n == 'FWU_OK' else n[len('FWU_E_'):] for n in names], pl.ERRORS)

    def test_states_results_trials(self):
        st = [n for n in self.enum('fw_update.h', 'fwu_state_t') if not n.endswith('__STATES')]
        self.assertEqual([n[len('FWU_'):].lower() for n in st], pl.STATES)
        self.assertEqual(len(self.enum('fw_update.h', 'fwu_result_t')), len(pl.RESULTS))
        tr = [n for n in self.enum('fw_update.h', 'fwu_trial_t') if not n.endswith('__COUNT')]
        self.assertEqual(len(tr), len(pl.TRIAL))
        text = self.source('fw_update.h')
        how = dict(re.findall(r'#define FWU_INSTALLED_([A-Z_]+)\s+(\d+)u', text))
        self.assertEqual(sorted(int(v) for v in how.values()), list(range(len(pl.INSTALLED_HOW))))
        self.assertIn('trial and bought', pl.INSTALLED_HOW[int(how['ON_TRIAL'])])
        self.assertIn('WITHOUT a trial', pl.INSTALLED_HOW[int(how['NO_TRIAL'])])

    def test_power_states_and_faults(self):
        ps = [n for n in self.enum('power_fsm.h', 'pstate_t') if not n.endswith('__COUNT')]
        self.assertEqual([n[len('PS_'):] for n in ps], pl.POWER)
        fl = [n for n in self.enum('safe_state.h', 'fault_t') if not n.endswith('__COUNT')]
        self.assertEqual(['none' if n == 'FAULT_NONE' else n[len('FAULT_'):] for n in fl], pl.FAULTS)

    def test_request_codes(self):
        text = self.source('link_proto.h')
        codes = dict(re.findall(r'LINK_([A-Z_]+)\s*=\s*(0x[0-9A-Fa-f]+)', text))
        for name in ('PING', 'INFO', 'STATUS', 'HIST', 'UPD_BEGIN', 'UPD_DATA', 'UPD_END',
                     'UPD_APPLY', 'UPD_ABORT', 'UPD_STATUS', 'CLI', 'CLI_OUT', 'CLI_ABORT',
                     'CLI_BEGIN'):
            self.assertEqual(int(codes[name], 16), getattr(pl, name))
        self.assertIn('LINK_DATA_MAX        251u', text)
        self.assertIn('LINK_PROTO_VERSION  %du' % pl.PROTO_VERSION, text)
        self.assertEqual(pl.CLI_PROTO, pl.PROTO_VERSION)
        defs = dict(re.findall(r'#define LINK_([A-Z_]+)\s+(0x[0-9A-Fa-f]+)u', text))
        for name, value in (('CLI_F_REPEAT', pl.F_REPEAT), ('CLI_F_STOP_ASKED', pl.F_STOP_ASKED),
                            ('CLI_F_STOPPED', pl.F_STOPPED), ('CLI_F_CANCELLED', pl.F_CANCELLED),
                            ('E_MISMATCH', pl.E_LINK_MISMATCH), ('E_STALE', pl.E_LINK_STALE),
                            ('E_SESSION', pl.E_LINK_SESSION), ('E_PI_RULE', pl.E_LINK_PI_RULE),
                            ('E_LENGTH', pl.E_LINK_LENGTH), ('E_UNKNOWN', pl.E_LINK_UNKNOWN)):
            self.assertEqual(int(defs[name], 16), value, name)
        cli = self.source('link_cli.h')
        nums = dict(re.findall(r'#define LINK_CLI_([A-Z_]+)\s+(\d+)u', cli))
        self.assertEqual(int(nums['LINE_MAX']), pl.CLI_LINE_MAX)
        self.assertEqual(int(nums['KEEPALIVE_MS']), 1000)
        self.assertEqual(int(nums['BOOK']), BOOK)               # the fake board's record
        self.assertEqual(int(nums['RESULT_BYTES']), pl.RESULT_BYTES)
        self.assertEqual(int(nums['OUT_HDR']), pl.OUT_HDR_BYTES)
        self.assertEqual((pl.RESULT_BYTES, pl.OUT_HDR_BYTES), (16, 12))
        self.assertLess(1.0, pl.CLI_QUIET_S)                  # several keepalives of slack


if __name__ == '__main__':
    unittest.main()
