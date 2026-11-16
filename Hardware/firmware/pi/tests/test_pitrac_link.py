# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
"""Tests for pi/pitrac_link.py. Standard library only; runs on the Pi or any PC:

    python3 -m unittest discover -s pi/tests -p "test_pitrac_link.py"

The frame bytes are the ones tests/link_test.c checks the firmware against. Where
the firmware sources are next to this folder (a repo checkout), the tool's tables
are also checked against them.
"""
import contextlib
import hashlib
import io
import os
import re
import struct
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import pitrac_link as pl  # noqa: E402

SRC = os.path.join(HERE, '..', '..', 'src')


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


class FakeBoard:
    """The firmware's side of the link (link_proto.c, fw_update.c), reduced to what the
    tool depends on. drop = {(type, n)}: the n-th answer to that type is lost."""

    def __init__(self, clock, partitioned=True, version=(1, 1), capacity=288 * 1024,
                 other_size=1016 * 1024, state=3, drop=(), offset_glitch=None):
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

    # port
    def write(self, data):
        for b in data:
            f = self.dec.push(b, self.clock())
            if f:
                self.handle(f)

    def read(self, timeout):
        if not self.out:
            self.clock.t += timeout
            return b''
        d = bytes(self.out)
        self.out.clear()
        return d

    def close(self):
        pass

    def answer(self, f, payload):
        self.counts[f.type] = self.counts.get(f.type, 0) + 1
        if (f.type, self.counts[f.type]) in self.drop:
            return
        self.out += pl.encode(f.type | pl.RESPONSE, f.seq, payload)

    def handle(self, f):
        self.frames_seen.append((f.type, f.seq, len(f.payload)))
        v = (self.version[0] << 16) | self.version[1]
        if f.type == pl.PING:
            return self.answer(f, struct.pack('<BBI', 0, pl.PROTO_VERSION, v))
        if f.type == pl.INFO:
            body = struct.pack('<BI', 0, v) + b'Oct 19 2026 12:34:56'
            body += struct.pack('<BbBIIIII', 0, 0, 1 if self.partitioned else 0, 0x2000,
                                1016 * 1024, 0x100000 if self.partitioned else 0,
                                self.other_size if self.partitioned else 0, self.capacity)
            return self.answer(f, body)
        if f.type == pl.UPD_BEGIN:
            size, = struct.unpack_from('<I', f.payload)
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
class Framing(unittest.TestCase):
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


class Images(unittest.TestCase):
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


class Requests(unittest.TestCase):
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


class Update(unittest.TestCase):
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
        self.assertTrue(any(line.startswith('NOTE') for line in lines))

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


class UpdStatus(unittest.TestCase):
    """What upd-status prints for each result the board can report."""

    class OneAnswer:
        def __init__(self, **kw):
            self.s = dict(status=0, state=0, error=0, size=0, received=0, version=0,
                          result=0, frm=0, to=0, detail=0)
            self.s.update(kw)
            self.s['from'] = self.s.pop('frm')

        def upd_status(self):
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
class InStepWithTheFirmware(unittest.TestCase):
    """The tool's tables against the C enums they mirror."""

    def enum(self, header, name):
        text = open(os.path.join(SRC, header), encoding='utf-8').read()
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
        text = open(os.path.join(SRC, 'fw_update.h'), encoding='utf-8').read()
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
        text = open(os.path.join(SRC, 'link_proto.h'), encoding='utf-8').read()
        codes = dict(re.findall(r'LINK_([A-Z_]+)\s*=\s*(0x[0-9A-Fa-f]+)', text))
        for name in ('PING', 'INFO', 'STATUS', 'HIST', 'UPD_BEGIN', 'UPD_DATA', 'UPD_END',
                     'UPD_APPLY', 'UPD_ABORT', 'UPD_STATUS'):
            self.assertEqual(int(codes[name], 16), getattr(pl, name))
        self.assertIn('LINK_DATA_MAX        251u', text)
        self.assertIn('LINK_PROTO_VERSION  %du' % pl.PROTO_VERSION, text)


if __name__ == '__main__':
    unittest.main()
