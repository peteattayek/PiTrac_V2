# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
"""pitrac_link.main() as its own process, against the tests' FakeBoard instead of a serial
port: test_pitrac_link.py's AsAProcess runs it with real OS pipes for stdout and stderr
(image 1.19, review finding V118-F1; image 1.20, V119-F1 and F2; image 1.21, V120-F4).
Not a test itself.

    python3 -B fake_board_process.py SCENARIO PITRAC-LINK-ARGS...

Before main() runs, `stat` has run once on the board, as ticket 5eed0001:1. SCENARIO is a
comma-separated list, or empty:
    restart            the board restarts: that ticket's record is gone (UNKNOWN)
    close-fails        closing the port raises OSError
    sigint-on-stderr   a real Ctrl-C (SIGINT) is raised as stderr's first line is written
    chatty-sigint=N    a real Ctrl-C as the board's `chatty` command prints its N-th tick
    no-threads         no thread can be started: threading.Thread.start() raises
                       RuntimeError, as when the process has none left (image 1.21, V120-F4)
    first-thread-fails the first thread start raises so (stdout's writer, for cli-cancel)
    v121:...           V121Process: clean board, transport fault and exact wire transcript
Environment, each optional:
    PITRAC_TEST_POWEROFF_MARK=PATH   the poweroff request creates PATH (no shutdown is run)
    PITRAC_TEST_SIGINT_AT=K          a real SIGINT at main()'s K-th line event (Sigints)...
    PITRAC_TEST_SIGINT_N=N           ...N of them (default 1)
    PITRAC_TEST_TRACE=PATH           main()'s line events and where decide() came: JSON
    PITRAC_TEST_STATE=PATH           the board afterwards: applies, update state, CLI runs
The exit status is main()'s, through sys.exit(), as the installed tool's is.
"""
import json
import os
import signal
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import test_pitrac_link as t  # noqa: E402

pl = t.pl


class SigintOnFirstWrite:
    """A stream that raises SIGINT for real as its first line is written."""

    def __init__(self, stream):
        self.stream, self.armed = stream, True

    def write(self, s):
        if self.armed:
            self.armed = False
            signal.raise_signal(signal.SIGINT)
        return self.stream.write(s)

    def flush(self):
        return self.stream.flush()


class WireBoard(t.FakeBoard):
    """Inject below the client's actual bounded writer. A write attempt is not delivery:
    record its entire frame, the prefix actually taken, and separately each decoded frame."""

    def __init__(self, clock, case):
        super().__init__(clock, outputs={'stat': t.Cli.STAT})
        self.case = case.split(':')
        self.attempts, self.delivered, self.signals, self.prompts, self.reads = [], [], [], [], []
        self.release = None
        if self.case[0] == 'retry':
            self.cli_lose = {1}
        if self.case[0] == 'lost-acks':
            self.drop = {(pl.UPD_APPLY, n) for n in range(1, 9)}
        if self.case[0] == 'apply-retry':
            self.drop = {(pl.UPD_APPLY, 1)}

    def write(self, data, timeout=pl.WRITE_TIMEOUT_S, stop=None):
        self.attempt = {'frame': data.hex(), 'taken': '', 'puts': [], 'start': self.clock()}
        self.attempts.append(self.attempt)
        self.ftype = data[2]
        try:
            if self.case == ['apply-retry', 'state'] and self.ftype == pl.UPD_APPLY and self.applied:
                self.upd = 'idle'    # staging cleared between sends; next APPLY answers STATE
            if self.case[:2] == ['uncounted', str(self.ftype)]:
                # This port implementation did not report a prefix: not proof of zero.
                raise OSError(5, 'Input/output error')
            return super().write(data, timeout, stop)
        finally:
            self.attempt['end'] = self.clock()

    def take(self, view):
        at = self.clock()
        self.attempt['puts'].append(at)
        taken = len(self.attempt['taken']) // 2
        case = self.case
        if case[0] == 'running-retry' and self.cli_seen:
            if self.ftype == pl.CLI and case[2] == 'partial-eio' and taken == 0:
                n = super().take(view[:2])
                self.attempt['taken'] += bytes(view[:n]).hex()
                return n
            if self.ftype == pl.CLI or (self.ftype == pl.CLI_ABORT and case[3] == 'blocked'):
                return 0
        if case == ['apply-retry', 'eio'] and self.ftype == pl.UPD_APPLY and self.applied:
            raise OSError(5, 'Input/output error')
        if case[0] == 'retry' and self.cli_seen >= 1:
            if self.release is None or at < self.release:
                return 0
        elif case[:2] == ['fault', str(self.ftype)]:
            prefix = int(case[2])
            if taken < prefix:
                view = view[:prefix - taken]
            elif case[3] == 'write-eio':
                self.clock.t += 0.125  # the board drops a cut frame before a cleanup request
                raise OSError(5, 'Input/output error')
            else:
                return 0
        elif case[0] == 'deadline' and len(self.attempts) == 1:
            if case[1] == 'progress':
                view = view[:1]
            elif taken == 0:
                view = view[:2]
            elif at < 1:
                return 0
        n = super().take(view)
        self.attempt['taken'] += bytes(view[:n]).hex()
        if case == ['deadline', 'progress']:
            self.clock.t += 0.25
        return n

    def wait_for_room(self, s):
        super().wait_for_room(s)
        case = self.case
        if case[0] == 'running-retry' and self.ftype == pl.CLI:
            for _ in range(int(case[1])):
                self.signals.append([self.ftype, self.attempt['taken'], list(self.cli_runs)])
                signal.raise_signal(signal.SIGINT)
            if case[2] in ('eio', 'partial-eio'):
                raise OSError(5, 'Input/output error')
        elif case[0] == 'retry' and self.release is None:
            self.release = self.clock() + 0.3 if case[2] == 'release' else float('inf')
            for _ in range(int(case[1])):
                self.signals.append([self.ftype, self.attempt['taken'], list(self.cli_runs)])
                signal.raise_signal(signal.SIGINT)
        elif case[:2] == ['fault', str(self.ftype)] and case[3] == 'wait-eio':
            self.clock.t += 0.125
            raise OSError(5, 'Input/output error')
        elif case == ['deadline', 'wake']:
            self.clock.t += 1.25  # descheduled; writable again when the client wakes

    def emit(self, text):
        if not text and self.case[0] == 'running-retry':
            return  # the command is alive, but its keepalive did not reach the client
        super().emit(text)

    def handle(self, f, ahead=()):
        self.delivered.append(pl.encode(f.type, f.seq, f.payload).hex())
        return super().handle(f, ahead)

    def state(self):
        return dict(attempts=self.attempts, delivered=self.delivered, applied=self.applied,
                    upd=self.upd, cli_runs=self.cli_runs, signals=self.signals, reads=self.reads,
                    prompts=self.prompts, tickets={k: v['state'] for k, v in self.book.items()},
                    running=self.running and [self.running['ticket'], self.running['stop']])


class BoundaryOutput:
    """No fd: writes synchronously on the main thread at the prompt/report boundary."""

    def __init__(self, board, stream):
        self.board, self.stream = board, stream

    def write(self, text):
        b = self.board
        if b.case == ['accepted-output-close'] and b.applied:
            raise BrokenPipeError()
        n = self.stream.write(text)
        self.stream.flush()
        if text == 'pi> ':
            b.prompts.append(text)
            if len(b.prompts) == 1:
                signal.raise_signal(signal.SIGINT)
            if len(b.prompts) > 3:
                raise AssertionError('the shell replayed the same SIGINT')
        return n

    def flush(self):
        return self.stream.flush()


class PromptInput:
    def __init__(self, board, stream):
        self.board, self.stream = board, stream

    def readline(self):
        self.board.reads.append(len(self.board.prompts))
        with open(os.environ['PITRAC_TEST_READING'], 'x') as f:
            f.write(str(len(self.board.prompts)))
        return self.stream.readline()


def main():
    scenario, argv = sys.argv[1].split(','), sys.argv[2:]
    env = os.environ.get
    clock = t.Clock()
    probe = scenario[0].startswith('v121:')
    if probe:
        board = WireBoard(clock, scenario[0][5:])
        if board.case in (['shell'], ['accepted-output-close']):
            sys.stdout = BoundaryOutput(board, sys.stdout)
        if board.case == ['shell']:
            sys.stdin = PromptInput(board, sys.stdin)
    else:
        board = t.FakeBoard(clock, outputs={'stat': t.Cli.STAT})
        first = pl.Link(board, clock=clock)
        if first.cli('stat').outcome != 'done' or first.last_ticket != (0x5EED0001, 1):
            raise SystemExit('fake_board_process: the setup run of stat failed')
    if 'restart' in scenario:
        board.restart()
    if 'close-fails' in scenario:
        def failing():
            raise OSError(5, 'Input/output error')
        board.close = failing
    if 'sigint-on-stderr' in scenario:
        sys.stderr = SigintOnFirstWrite(sys.stderr)
    if 'no-threads' in scenario or 'first-thread-fails' in scenario:
        import threading
        real_start, starts = threading.Thread.start, []

        def start(thread):
            starts.append(thread)
            if 'no-threads' in scenario or len(starts) == 1:
                raise RuntimeError("can't start new thread")
            return real_start(thread)
        threading.Thread.start = start
    for s in scenario:
        if s.startswith('chatty-sigint='):
            at = int(s.split('=')[1])

            def on_tick(b, n, at=at):
                if n == at:
                    signal.raise_signal(signal.SIGINT)
            board.on_tick = on_tick
    if env('PITRAC_TEST_POWEROFF_MARK'):
        pl.POWEROFF = [sys.executable, '-c', 'import sys; open(sys.argv[1], "ab").write(b"1")',
                       env('PITRAC_TEST_POWEROFF_MARK')]
    real_link = pl.Link
    pl.SerialPort = lambda path: board
    def link(port):
        result = real_link(port, clock=clock)
        if probe:
            result.seq = 0  # deterministic wire evidence; the real client advances the seqs
        return result
    pl.Link = link
    tracer = t.Sigints(int(env('PITRAC_TEST_SIGINT_AT', '-1')), int(env('PITRAC_TEST_SIGINT_N', '1')))
    try:
        if not probe:
            sys.settrace(tracer)
        try:
            return pl.main(argv)
        finally:
            sys.settrace(None)
    finally:
        if env('PITRAC_TEST_TRACE'):
            with open(env('PITRAC_TEST_TRACE'), 'w') as f:
                json.dump({'events': tracer.events, 'decided_at': tracer.decided_at}, f)
        if env('PITRAC_TEST_STATE'):
            with open(env('PITRAC_TEST_STATE'), 'w') as f:
                json.dump(board.state() if probe else {'applied': board.applied, 'upd': board.upd,
                          'cli_runs': board.cli_runs}, f)


if __name__ == '__main__':
    sys.exit(main())
