// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// service.h -- the background work, and the ONE way to block without stopping it.
//
// WHY THIS EXISTS
//
// power_fsm_step() used to be called from exactly one place: the superloop in
// main(). Every long-running CLI command then starved it, because they all
// blocked on bare sleep_ms():
//
//     scan carrier (16 pt)   ~600 s        cal demod         26 s
//     level                  up to 300 s   panel demo        24 s
//     cal model              128 s         threshold sweep    1.3 s
//     beam sweep             75 s          cfg save (erase)   0.01 s
//     hpf test               32 s
//
// During any of those the board had:
//   - no V5_MIN_SUSTAINED monitor, so a supply pulled mid-command would leave
//     the latch closed and back-feed +5 V through D8, a 1 A SS14;
//   - no fault detection;
//   - no button polling -- INCLUDING THE 5 SECOND LONG-PRESS ESCAPE HATCH,
//     which is the operator's only handle on a board that is mid-command;
//   - no Pi handshake progress.
//
// The give-away that this was an oversight rather than a decision:
// cfg_save_blocked_reason() REFUSES to write flash because a 4 KB erase blinds
// that same monitor for "tens of ms". The 0.01 s case was guarded and the 600 s
// case was not.
//
// Nothing had failed mid-command yet. It has to be fixed before Phase 6, where
// the escape hatch is the only handle on 9 A through a linear-mode FET.
//
// THE RULE: no command path calls sleep_ms(). It calls pitrac_yield_ms().

#ifndef PITRAC_SERVICE_H
#define PITRAC_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    YIELD_OK = 0,
    YIELD_ABORT_FAULT,   // a fault was raised while we were blocked
    YIELD_ABORT_KEY,     // the operator pressed a key
} yield_t;

// One pass of background work: the power FSM, the detect FIFO drain, the live
// strobe hold checks, and the indicators. Everything the superloop does EXCEPT
// cli_service(), which is omitted on purpose -- calling it from inside a
// command would re-enter the dispatcher.
//
// main() calls this too, so there is exactly one definition of "background
// work" and a yield can never service less than the superloop does.
yield_t pitrac_service(void);

// Block for `ms`, servicing in slices, and report any abort condition that
// appeared -- including one on the final slice (host-tested, audit SVC-03).
//
// It does NOT cut the delay short: a caller asking for 20 ms of settling gets
// exactly 20 ms, because a shortened settle silently corrupts whatever is
// measured next. Callers unwind at their next loop boundary; unwinding cleanly
// (beam off, chop ended, ADC mode restored) matters more than latency.
yield_t pitrac_yield_ms(uint32_t ms);

// Has the operator asked to stop? Latched by pitrac_service() when a character
// arrives, and sticky until cleared. Long loops poll this at their natural
// boundary -- per sweep point, per burst -- rather than at every sleep.
bool pitrac_abort_pending(void);

// Clear the latch, and the "taken" mark below. A command calls this on entry so
// a stray keystroke typed during the PREVIOUS command cannot abort this one.
void pitrac_abort_clear(void);

// The USB console's next input byte, or PICO_ERROR_TIMEOUT for none. A CR LF pair
// is ONE Enter: an LF that is the very next byte after a CR is dropped (image 1.16,
// review finding V115-F1). Both readers of the console use this -- the line editor
// (cli_service()) and the stop key (pitrac_yield_ms()) -- because the line editor
// runs a command as soon as it reads the CR, and the command's key poll then read
// the client's LF as a stop. Any other byte after the CR cancels the drop, so a
// later LF is a key like any other.
int pitrac_console_getc(void);

// A second source of abort requests, polled with the USB key (image 1.13): while
// the Pi runs a command over the link, pi_link.c registers one that reads the
// link for that command's CLI_ABORT (and sends its keepalive), and clears it
// afterwards. Polled ONLY from inside a blocking command (pitrac_yield_ms()),
// never from the superloop -- the same rule as the key, for the same reason: in
// the superloop it would consume the link's next request. NULL: none.
void pitrac_abort_source(bool (*poll)(void));

// A wait that a stop request ends EARLY (image 1.14, review finding V113-F3):
// up to `ms`, servicing as pitrac_yield_ms() does, in pieces of at most 20 ms,
// returning YIELD_ABORT_KEY as soon as the latch is set -- at once if it already
// is. `ms` 0 is still one poll and one service pass (image 1.15, V114-F4), so a
// loop with no wait between its steps still sees a stop and a new fault at every
// step. For the waits of a command that can stop part-way -- a ramp's step, a
// sweep's dwell, a demo's pattern -- never for a settling time (see
// pitrac_yield_ms()). YIELD_ABORT_FAULT: a fault was raised. A command that
// stops on YIELD_ABORT_KEY does its cleanup and calls pitrac_abort_taken().
yield_t pitrac_yield_stoppable_ms(uint32_t ms);

// "This command ENDED EARLY because a stop was asked" (image 1.14, V113-F3).
// Called by a command at the point where it acts on the latch. The Pi's answer
// says "stopped" only then: a stop the command did not act on -- it was too
// late, or the command does not stop part-way -- is reported as asked for, not
// as done. In 1.13 the answer read the latch alone, and `panel demo` reported
// "stopped" after running all 24 s. Cleared by pitrac_abort_clear().
void pitrac_abort_taken(void);
bool pitrac_abort_was_taken(void);

// ---------------------------------------------------------------------------
// THE WATCHDOG, AND WHY IT IS CONDITIONAL
//
// A watchdog reset resets the pads. GPIO15 goes high-Z, R12 pulls the latch
// gate low, and the +5 V rail opens -- so on this board a watchdog reset is a
// HARD POWER CUT to the Pi, not a recovery. A firmware hang would become a
// corrupted filesystem.
//
// DEFAULT: OFF. Changed 2026-08-31, after arming it by default cost a reflash
// cycle and two bench sessions.
//
// The Phase 1b policy was "enable the reset action ONLY while the Pi is down",
// on the reasoning that with a Pi up a hung MCU is the lesser evil. That policy
// is about protecting a PI. On the bench there is no Pi, so what it actually
// buys is a reset when the firmware hangs -- and per the paragraph above, that
// is a power cut in the middle of whatever was being measured. A hang is
// already obvious to an operator sitting in front of the board.
//
// It also broke `bootsel`: reset_usb_boot() reaches BOOTSEL through the
// watchdog's own scratch registers, so an armed watchdog fought it.
//
// WHERE IT EARNS ITS PLACE IS PHASE 6, where a hang with 9 A through a
// linear-mode FET is a genuinely different risk. Live strobe mode arms it
// (strobe_live_arm(), 2026-10-07) and disarms it again when live mode ends --
// unless it was already armed, or the operator typed `wdog on` while live, in
// which case it stays armed. NOT with a Pi powered (image 1.8, owner
// 2026-11-12): there a reset is a hard power cut to the Pi, and U5 and the PIO
// already bound every pulse and burst. "Pi powered" is the strobe's fresh
// observation (strobe_pi_observe(), image 1.9), and a live session ends if it
// changes or is unsettled. `wdog off` is refused only while a session armed
// without a Pi is live. `stat` shows the state.
//
// This was left as a comment in main() until 2026-08-28 and is implemented
// here now because pitrac_service() is the one function guaranteed to run on
// every path, including inside the long commands that used to starve the FSM.
// A watchdog without that guarantee would have fired during `cal model`.
//
// pitrac_service() kicks it. Nothing else should.
void pitrac_watchdog_enable(bool on);
bool pitrac_watchdog_enabled(void);

#endif // PITRAC_SERVICE_H
