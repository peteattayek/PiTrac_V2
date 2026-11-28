# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
"""tools/scope.py's reader (image 1.16, review findings V115-F1 and V115-F5; image
1.17, V116-F2 and V116-F3; image 1.18, V117-F2 and V117-N3; image 1.19, V118-N1).

Three kinds of test:

- END TO END, through the board's own console code: `stop_test console` (tests/,
  host build) runs cli.c's line editor, dispatch and `capture` with service.c's
  key poll, reading what this file's port was written and printing what the board
  would. 1.15's scope.py wrote CR LF, firmware 1.15 read the LF as a stop, and a
  4096-sample capture came back as one sample with no key pressed (V115-F1). Other
  commands' records ahead of this request's are made by the same code (V116-F3).
  Run by CTest as `scope.reader`, which sets PITRAC_STOP_TEST; run by hand, the
  host build's Release or Debug stop_test is used if it is there, and these tests
  are SKIPPED, saying so, if it is not.
- THE READER ALONE, against scripted replies: only a whole record of this request
  is returned (V115-F5, V116-F2, V116-F3).
- WITH PYSERIAL'S OWN TIMEOUT, if pyserial is installed (skipped, saying so, if
  not): its in-memory loop:// port at the production 1 s timeout, a pause inside a
  row -- the 1.16 review's reproduction of V116-F2.

The other tests need the standard library only: scope.py's pyserial import is stood
in for when pyserial is not installed.
"""
import os
import re
import subprocess
import sys
import threading
import time
import types
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
try:
    import serial                                               # pyserial, if installed
    HAVE_PYSERIAL = hasattr(serial, 'serial_for_url')
except ImportError:
    sys.modules['serial'] = types.ModuleType('serial')         # scope.py imports it
    HAVE_PYSERIAL = False
sys.path.insert(0, os.path.dirname(HERE))
import scope                                                    # noqa: E402

FIRMWARE = os.path.dirname(os.path.dirname(HERE))


def find_stop_test():
    exe = os.environ.get('PITRAC_STOP_TEST')
    if exe:
        return exe
    for cfg in ('Release', 'Debug'):
        p = os.path.join(FIRMWARE, 'tests', 'build', cfg, 'stop_test.exe')
        if os.path.exists(p):
            return p
    p = os.path.join(FIRMWARE, 'tests', 'build', 'stop_test')
    return p if os.path.exists(p) else None


STOP_TEST = find_stop_test()


def console(data, stop_at=None):
    """What the board's console prints for these bytes typed at it."""
    cmd = [STOP_TEST, 'console'] + ([str(stop_at)] if stop_at else [])
    return subprocess.run(cmd, input=data, stdout=subprocess.PIPE, timeout=60, check=True).stdout


class BoardPort:
    """A serial port whose other end is the board's console code. What is written is
    handed to `stop_test console` at the first read; with stop_at, a space is typed at
    the dump's stop_at-th poll. `crlf` makes this a CR LF client: every CR written goes
    out as CR LF, as 1.15's scope.py sent. `typed_first` is what another client typed
    at the console just before this one wrote; `printed_first` is what other commands
    printed before this one's output began (the Pi's, say)."""

    def __init__(self, stop_at=None, crlf=False, typed_first=b'', printed_first=b''):
        self.stop_at, self.crlf = stop_at, crlf
        self.typed_first, self.printed_first = typed_first, printed_first
        self.sent, self.lines = bytearray(), None

    def reset_input_buffer(self):
        pass

    def write(self, data):
        self.sent += data.replace(b'\r', b'\r\n') if self.crlf else data

    def readline(self):
        if self.lines is None:
            out = self.printed_first + console(self.typed_first + bytes(self.sent), self.stop_at)
            self.lines = out.splitlines(keepends=True)
        return self.lines.pop(0) if self.lines else b''


SENT = re.compile(rb'^capture 0x20 4096 500000 id=0x([0-9a-f]{8})\r$')


@unittest.skipUnless(STOP_TEST, 'no host build of tests/stop_test: build tests/ first '
                     '(HANDOFF.md section 4), or set PITRAC_STOP_TEST')
class EndToEnd(unittest.TestCase):
    def test_scope_capture_whole(self):
        port = BoardPort()
        header, columns, rows = scope.read_capture_on(port, 0x20, 4096, 500000, timeout=5.0)
        m = SENT.match(bytes(port.sent))                                # one terminator
        self.assertTrue(m, port.sent)
        self.assertEqual(header['id'], m.group(1).decode())            # said back
        self.assertEqual(columns, ['ch5'])
        self.assertEqual(header['n'], '4096')
        self.assertEqual([r[0] for r in rows], [i & 0xFFF for i in range(4096)])

    def test_crlf_client_whole(self):
        # The firmware takes CR LF as one Enter: a client that still sends it is whole.
        port = BoardPort(crlf=True)
        _, _, rows = scope.read_capture_on(port, 0x20, 4096, 500000, timeout=5.0)
        self.assertTrue(port.sent.endswith(b'\r\n'))
        self.assertEqual(len(rows), 4096)

    def test_two_channels(self):
        _, columns, rows = scope.read_capture_on(BoardPort(), 0x21, 1000, 500000, timeout=5.0)
        self.assertEqual(columns, ['ch0', 'ch5'])
        self.assertEqual(len(rows), 500)
        self.assertEqual(rows[1], [2, 3])

    def test_a_stop_after_the_command(self):
        # A key typed while it prints: the board ends it PARTIAL, and no record comes back.
        with self.assertRaisesRegex(scope.CaptureRejected, 'PARTIAL -- stopped after 513 of 4096'):
            scope.read_capture_on(BoardPort(stop_at=3, crlf=True), 0x20, 4096, 500000,
                                  timeout=5.0)

    def test_a_stop_on_the_last_line(self):
        # V115-F6: 257 of 257 were out when the stop came. The record is whole.
        _, _, rows = scope.read_capture_on(BoardPort(stop_at=2), 0x20, 257, 500000, timeout=5.0)
        self.assertEqual(len(rows), 257)
        with self.assertRaisesRegex(scope.CaptureRejected, 'stopped after 1 of 257'):
            scope.read_capture_on(BoardPort(stop_at=1), 0x20, 257, 500000, timeout=5.0)

    # --- V116-F3: another command's record first --------------------------------------
    def test_v116_f3_the_review_case(self):
        # The Pi's `capture` of channel 7, four samples, printed on the console ahead of
        # this request for channel 5, 4096 samples. 1.16 returned the four channel-7
        # samples and left the channel-5 record unread.
        pi = b'\r\n[pi] capture 0x80 4 500000\r\n' + console(b'capture 0x80 4 500000\r')
        port = BoardPort(printed_first=pi)
        header, columns, rows = scope.read_capture_on(port, 0x20, 4096, 500000, timeout=5.0)
        self.assertEqual((header['mask'], columns, len(rows)), ('0x20', ['ch5'], 4096))
        self.assertLessEqual(len(port.lines), 1)                       # read to its end

    def test_v116_f3_same_channel_first(self):
        # An earlier request's record, the same channel, count and rate -- with another
        # id, and with none (an older tool, or a person) -- is passed over too.
        for earlier in (b'capture 0x20 4096 500000 id=0x00000001\r', b'capture 0x20 4096 500000\r'):
            port = BoardPort(printed_first=console(earlier))
            header, _, rows = scope.read_capture_on(port, 0x20, 4096, 500000, timeout=5.0)
            self.assertEqual(header['id'], SENT.match(bytes(port.sent)).group(1).decode())
            self.assertEqual(len(rows), 4096)
            self.assertLessEqual(len(port.lines), 1, earlier)

    def test_v116_f3_a_record_still_printing(self):
        # Another client's capture is still printing when this request is typed: its
        # first byte stops that dump (any key does), and the echo is the rest. Nothing is
        # returned -- not the other's partial record either.
        port = BoardPort(typed_first=b'capture 0x20 4096 500000\r')
        with self.assertRaisesRegex(scope.CaptureRejected, r"echo is not one clean line.*apture"):
            scope.read_capture_on(port, 0x20, 4096, 500000, timeout=5.0)

    def test_v117_f2_same_id_before_the_echo(self):
        # The 1.17 review's case, from the console code: the Pi's echo and a whole record
        # of the same channel, count, rate AND id, ahead of this command. 1.17 took that
        # record. Now nothing before this command's own echo is taken: the record after it
        # is (both are made the same way, so which one is told by what is left unread).
        # The Pi's command shows as `[pi] <line>` and its output, with no USB echo: the
        # console's run of the same line, its echo dropped.
        req = scope.Request.plain(0x20, 4096, 500000, ident=0x10203040)
        out = console(req.command.encode() + b'\r')
        old = b'\r\n[pi] ' + req.command.encode() + b'\r\n' + out[out.index(b'\n') + 1:]
        port = BoardPort(printed_first=old)
        header, _, rows = scope.read_block_on(port, req, timeout=5.0)
        self.assertEqual((header['id'], len(rows)), ('10203040', 4096))
        self.assertLessEqual(len(port.lines), 1)                       # read to its own end


class ScriptedPort:
    """Replies as given, a line at a time; then silence. A bytes item is handed out as
    it is, as readline() hands out what it has when the port's timeout runs out."""

    def __init__(self, *lines):
        self.lines = [ln if isinstance(ln, bytes) else ln.encode() + b'\n' for ln in lines]
        self.sent = b''

    def reset_input_buffer(self):
        pass

    def write(self, data):
        self.sent += data

    def readline(self):
        return self.lines.pop(0) if self.lines else b''


ID = 0x1a2b3c4d
ECHO = '> capture 0x20 4 500000 id=0x1a2b3c4d'
HEAD1 = '# capture mask=0x20 n=4 rate=500000 overran=0 id=1a2b3c4d'
COLS1 = '# columns: ch5'


def req4():
    return scope.Request.plain(0x20, 4, 500000, ident=ID)


class Reader(unittest.TestCase):
    """The board's reply, scripted. Unless echo=False, it starts with this command's own
    echo at the prompt, as the console prints it."""

    def read(self, *lines, req=None, echo=True):
        req = req or req4()
        if echo:
            lines = ('> ' + req.command,) + lines
        return scope.read_block_on(ScriptedPort(*lines), req, timeout=0.3)

    def rejected(self, why, *lines, req=None, echo=True):
        with self.assertRaisesRegex(scope.CaptureRejected, why):
            self.read(*lines, req=req, echo=echo)

    def test_whole(self):
        h, c, r = self.read(HEAD1, COLS1, '10', '11', '12', '13', '# end', '> ')
        self.assertEqual((h['n'], c, r), ('4', ['ch5'], [[10], [11], [12], [13]]))

    def test_the_review_case_timeout(self):
        # One sample of four, then nothing: 1.15 returned [[42]].
        self.rejected(r'no end line: 1 of 4 samples', HEAD1, COLS1, '42')

    def test_the_review_case_plain_end(self):
        self.rejected(r'after 1 of 4 samples', HEAD1, COLS1, '42', '# end')

    def test_missing_footer(self):
        self.rejected(r'no end line: 4 of 4', HEAD1, COLS1, '1', '2', '3', '4')

    def test_missing_row(self):
        self.rejected(r'after 3 of 4', HEAD1, COLS1, '1', '2', '4', '# end')

    def test_extra_row(self):
        self.rejected(r'more than the 4', HEAD1, COLS1, '1', '2', '3', '4', '5', '# end')

    def test_partial(self):
        self.rejected(r'PARTIAL', HEAD1, COLS1, '1', '# end PARTIAL -- stopped after 1 of 4 samples')

    def test_row_width(self):
        req = scope.Request.plain(0x21, 4, 500000, ident=ID)
        head = '# capture mask=0x21 n=4 rate=500000 overran=0 id=1a2b3c4d'
        self.rejected(r'row 2 is not 2 samples', head, '# columns: ch0 ch5', '1,2', '3', '4,5',
                      '# end', req=req)
        self.rejected(r'row 1 is not 2 samples', head, '# columns: ch0 ch5', '1,2,3', '4',
                      '# end', req=req)

    def test_out_of_range(self):
        self.rejected(r'0-4095', HEAD1, COLS1, '1', '4096', '3', '4', '# end')

    def test_stray_line(self):
        self.rejected(r'not a row of samples', HEAD1, COLS1, '1', 'FAULT: something', '3', '4', '# end')

    def test_columns(self):
        self.rejected(r'no `# columns:`', HEAD1, '1', '2', '3', '4', '# end')
        self.rejected(r'not the mask', HEAD1, '# columns: ch7', '1', '2', '3', '4', '# end')

    def test_header(self):
        self.rejected(r'usable n', '# capture mask=0x20 rate=500000 id=1a2b3c4d', COLS1, '# end')

    def test_no_block(self):
        self.rejected(r"no echo of this command", 'capture 0x20 4 500000', '> ', echo=False)
        self.rejected(r"no `# capture` block after this command", '> ')

    def test_board_refusals(self):
        self.rejected(r'NO TRIGGER', 'NO TRIGGER: nothing reached 80 codes')
        self.rejected(r'REFUSED', 'REFUSED: 16384 samples at 733 Hz take 22352 ms to acquire')
        self.rejected(r"\? 'nosuch'", "? 'nosuch' -- try 'help'")
        with self.assertRaises(SystemExit):
            self.read('ERR: capture failed')

    def test_notes_before_the_block(self):
        _, _, r = self.read('acquiring for 22.4 s: a stop is seen only once the print-out starts.',
                            HEAD1, COLS1, '1', '2', '3', '4', '# end')
        self.assertEqual(len(r), 4)

    def test_one_terminator(self):
        port = ScriptedPort(ECHO, HEAD1, COLS1, '1', '2', '3', '4', '# end')
        scope.read_block_on(port, req4(), timeout=0.3)
        self.assertEqual(port.sent, b'capture 0x20 4 500000 id=0x1a2b3c4d\r')

    # --- V116-F2: a line cut by the read timeout ----------------------------------------
    def test_v116_f2_the_review_case(self):
        # Rows 1234, 2 and 3 under a header declaring four, the first cut by a pause:
        # 1.16 returned [[12], [34], [2], [3]] as a whole record.
        self.rejected(r'after 3 of 4', HEAD1, COLS1, b'12', b'34\n', '2', '3', '# end')

    def test_v116_f2_split_but_whole(self):
        # The same pause inside a row of a whole record: the row is joined, and the record
        # comes back as it was sent.
        _, _, r = self.read(HEAD1, COLS1, b'1', b'0\n', '11', b'1', b'2\n', '13', '# end')
        self.assertEqual(r, [[10], [11], [12], [13]])

    def test_v116_f2_split_header_and_end(self):
        _, _, r = self.read(b'# capture mask=0x20 n=4 ra', b'te=500000 overran=0 id=1a2b3c4d\n',
                            COLS1, '1', '2', '3', '4', b'# e', b'nd\n')
        self.assertEqual(len(r), 4)
        # Split inside the two-channel row, at the comma.
        req = scope.Request.plain(0x21, 4, 500000, ident=ID)
        _, _, r = self.read('# capture mask=0x21 n=4 rate=500000 overran=0 id=1a2b3c4d',
                            '# columns: ch0 ch5', b'1,', b'2\n', '3,4', '# end', req=req)
        self.assertEqual(r, [[1, 2], [3, 4]])

    def test_v116_f2_cut_at_the_timeout(self):
        # A line never finished is never a line: not an end, not a row.
        self.rejected(r"no end line: 4 of 4 .*cut short: b'# e'", HEAD1, COLS1, '1', '2', '3', '4',
                      b'# e')
        self.rejected(r'no end line: 3 of 4', HEAD1, COLS1, '1', '2', '3', b'4')

    # --- V116-F3: whose record -----------------------------------------------------------
    def test_v116_f3_another_record_first(self):
        _, c, r = self.read('[pi] capture 0x80 4 500000', '# capture mask=0x80 n=4 rate=500000 overran=0',
                            '# columns: ch7', '1', '2', '3', '4', '# end', ECHO,
                            HEAD1, COLS1, '10', '11', '12', '13', '# end', echo=False)
        self.assertEqual((c, r), (['ch5'], [[10], [11], [12], [13]]))

    def test_v116_f3_same_shape_first(self):
        for other in ('# capture mask=0x20 n=4 rate=500000 overran=0 id=00000001',
                      '# capture mask=0x20 n=4 rate=500000 overran=0'):
            _, _, r = self.read(other, COLS1, '1', '2', '3', '4', '# end', ECHO,
                                HEAD1, COLS1, '10', '11', '12', '13', '# end', echo=False)
            self.assertEqual(r, [[10], [11], [12], [13]], other)

    def test_v116_f3_another_refusal_first(self):
        # Another command's REFUSED or ERR, before this one's echo, is not this one's.
        _, _, r = self.read('REFUSED: not now', 'ERR: something else', ECHO,
                            HEAD1, COLS1, '1', '2', '3', '4', '# end', echo=False)
        self.assertEqual(len(r), 4)

    def test_v116_f3_another_record_after_the_command(self):
        self.rejected(r"not this request's, after its command",
                      '# capture mask=0x20 n=4 rate=500000 overran=0 id=00000001', COLS1)

    def test_v116_f3_only_with_its_id(self):
        # Firmware before 1.23 (1.17 to 1.22, which had it, never left the PC) ignores the
        # id: its record, after this command, has none.
        self.rejected(r'without an id, after this command: .*older than 1\.23',
                      '# capture mask=0x20 n=4 rate=500000 overran=0', COLS1, '1', '2', '3', '4',
                      '# end')

    # --- V117-F2: the echo, not the id, opens this command's output -----------------------
    def test_v117_f2_the_review_case(self):
        # The 1.17 review's reproduction: tag 10203040 asked; ahead of this command's echo,
        # the Pi's echo of the same line and an older four-sample record carrying the same
        # tag and rate. 1.17 returned [42, 42, 42, 42]; the next unread line was this
        # command's echo, and its record [1, 2, 3, 4].
        req = scope.Request.plain(0x20, 4, 500000, ident=0x10203040)
        head = '# capture mask=0x20 n=4 rate=500000 overran=0 id=10203040'
        _, _, r = self.read('[pi] ' + req.command, head, COLS1, '42', '42', '42', '42', '# end',
                            '> ' + req.command, head, COLS1, '1', '2', '3', '4', '# end',
                            req=req, echo=False)
        self.assertEqual(r, [[1], [2], [3], [4]])

    def test_v117_f2_no_echo_no_record(self):
        # This request's id on a record, but no echo of this command: never taken.
        self.rejected(r'no echo of this command .*1 record\(s\) from before it passed over',
                      HEAD1, COLS1, '1', '2', '3', '4', '# end', echo=False)
        self.rejected(r'no echo of this command', '[pi] ' + req4().command,
                      HEAD1, COLS1, '1', '2', '3', '4', '# end', echo=False)

    def test_v117_f2_the_echo_exactly(self):
        # Only the line as sent, after prompts, opens this command's output. One that
        # holds the id but is not that line means the command did not arrive as one line.
        for echo in ('> > ' + req4().command, '>' + req4().command, req4().command):
            _, _, r = self.read(echo, HEAD1, COLS1, '1', '2', '3', '4', '# end', echo=False)
            self.assertEqual(len(r), 4, echo)
        for bad in ('> apture 0x20 4 500000 id=0x1a2b3c4d', '> capture 0x20 4 500000 id=0x1a2b3c4d x',
                    'note > capture 0x20 4 500000 id=0x1a2b3c4d'):
            self.rejected(r'echo is not one clean line', bad, HEAD1, COLS1, '1', '2', '3', '4',
                          '# end', echo=False)

    def test_v117_f2_ids_count_up_in_a_run(self):
        # One more each time, from a random start, modulo 2^32: no repeat within a run until
        # 2^32 requests have been made (image 1.19, review note V118-N1: 1.18 said "never").
        ids = [scope.Request.plain(0x20, 4, 500000).ident for _ in range(1000)]
        self.assertEqual(len(set(ids)), 1000)
        self.assertTrue(all((b - a) & 0xFFFFFFFF == 1 for a, b in zip(ids, ids[1:])))
        saved = scope._last_id[0]
        try:
            scope._last_id[0] = 0xFFFFFFFF
            self.assertEqual(scope.Request.plain(0x20, 4, 500000).ident, 0)
        finally:
            scope._last_id[0] = saved

    def test_v116_f3_shape(self):
        # Its id, but not what was asked: rejected, never labelled as the request.
        self.rejected(r'mask 0x80, not the 0x20', '# capture mask=0x80 n=4 rate=500000 overran=0 '
                      'id=1a2b3c4d')
        self.rejected(r'declares 8 samples where this request makes 4',
                      '# capture mask=0x20 n=8 rate=500000 overran=0 id=1a2b3c4d')
        self.rejected(r'rate is 250000 Hz where this request runs at 500000',
                      '# capture mask=0x20 n=4 rate=250000 overran=0 id=1a2b3c4d')
        self.rejected(r'triggered record for a plain capture',
                      '# capture mask=0x20 n=4 rate=500000 overran=0 trig=1 base=100 id=1a2b3c4d')

    def test_v116_f3_triggered(self):
        req = scope.Request.triggered(5, 80, 500000, 25, 30, ident=ID)
        self.assertEqual(req.command, 'capture trig 5 80 500000 25 30 id=0x1a2b3c4d')
        rows = [str(i & 0xFFF) for i in range(16384)]
        head = '# capture mask=0x20 n=16384 rate=500000 overran=0 trig=4096 base=100 id=1a2b3c4d'
        h, _, r = self.read(head, COLS1, *rows, '# end', req=req)
        self.assertEqual((h['trig'], len(r)), ('4096', 16384))
        self.rejected(r'without a usable trig', head.replace('trig=4096 ', ''), req=req)
        self.rejected(r'trigger at 16384', head.replace('trig=4096', 'trig=16384'), req=req)

    def test_v117_n3_trigger_and_baseline(self):
        # The 1.17 review's fixture: a whole, tagged 25 % pre-trigger record with trig=1
        # and base=9999, which 1.17 returned. The firmware puts this trigger at 4096, and a
        # baseline is a 12-bit code.
        req = scope.Request.triggered(5, 80, 500000, 25, 30, ident=ID)
        head = '# capture mask=0x20 n=16384 rate=500000 overran=0 trig=1 base=9999 id=1a2b3c4d'
        self.rejected(r'trigger at 1, where .* puts it at 4096', head, req=req)
        self.rejected(r'baseline 9999 is not an ADC code',
                      head.replace('trig=1', 'trig=4096'), req=req)
        self.rejected(r'baseline -1 is not', head.replace('trig=1', 'trig=4096')
                      .replace('9999', '-1'), req=req)
        # The offset as adc_capture_triggered() rounds it, in single precision.
        offsets = {p: scope.Request.triggered(5, 80, 500000, p, 30).trig_at for p in (0, 10, 25, 33, 90)}
        self.assertEqual(offsets, {0: 0, 10: 1638, 25: 4096, 33: 5406, 90: 14745})

    def test_the_firmware_rounding(self):
        # adc_capture()'s count: cut to the buffer, then to whole rows.
        self.assertEqual(scope.Request.plain(0x21, 1001, 500000).n, 1000)
        self.assertEqual(scope.Request.plain(0xa1, 100, 500000).n, 99)
        self.assertEqual(scope.Request.plain(0x20, 20000, 500000).n, 16384)
        # The ADC divider's rate (adc_ring_math.h): the documented 499 kHz -> 494.8 kHz.
        self.assertEqual(scope.adc_rate_for(500000), 500000)
        self.assertEqual(scope.adc_rate_for(499000), 494845)
        self.assertEqual(scope.adc_rate_for(250000), 250000)
        self.assertEqual(scope.adc_rate_for(733), 733)
        self.assertEqual((scope.adc_rate_for(732), scope.adc_rate_for(500001)), (0, 0))


class LoopPort:
    """pyserial's in-memory loop:// port at scope.py's own 1 s timeout. The 'board's'
    bytes are fed in by a thread once the request is written, with pauses (a float is
    seconds of silence); what the reader writes is kept apart."""

    def __init__(self, *chunks):
        self.port = serial.serial_for_url('loop://', timeout=1.0)
        self.chunks, self.sent = chunks, b''

    def reset_input_buffer(self):
        self.port.reset_input_buffer()

    def write(self, data):
        self.sent += data
        threading.Thread(target=self.feed, daemon=True).start()

    def feed(self):
        for c in self.chunks:
            if isinstance(c, float):
                time.sleep(c)
            else:
                self.port.write(c)

    def readline(self):
        return self.port.readline()


@unittest.skipUnless(HAVE_PYSERIAL, 'pyserial is not installed: python -m pip install pyserial')
class RealTimeout(unittest.TestCase):
    """The 1.16 review's reproduction of V116-F2, with pyserial itself."""
    LEAD = (ECHO + '\n' + HEAD1 + '\n' + COLS1 + '\n').encode()

    def test_v116_f2_pause_inside_a_row(self):
        port = LoopPort(self.LEAD, b'12', 1.3, b'34\n2\n3\n# end\n')
        with self.assertRaisesRegex(scope.CaptureRejected, 'after 3 of 4'):
            scope.read_block_on(port, req4(), timeout=5.0)

    def test_v116_f2_pause_inside_a_whole_record(self):
        port = LoopPort(self.LEAD, b'1', 1.3, b'0\n11\n12\n13\n# end\n')
        _, _, rows = scope.read_block_on(port, req4(), timeout=5.0)
        self.assertEqual(rows, [[10], [11], [12], [13]])


if __name__ == '__main__':
    unittest.main()
