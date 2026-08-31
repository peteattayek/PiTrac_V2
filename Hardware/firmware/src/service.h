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

// One pass of background work: the power FSM, the detect FIFO drain, and the
// indicators. Everything the superloop does EXCEPT cli_service(), which is
// omitted on purpose -- calling it from inside a command would re-enter the
// dispatcher.
//
// main() calls this too, so there is exactly one definition of "background
// work" and a yield can never service less than the superloop does.
yield_t pitrac_service(void);

// Block for `ms`, servicing in slices. Returns as soon as an abort condition
// appears, so a caller that checks the result unwinds within a slice.
//
// Callers should still treat this as best-effort: aborting at the next loop
// boundary rather than mid-sleep is a difference of one slice, and unwinding
// cleanly (beam off, chop ended, ADC mode restored) matters more than latency.
yield_t pitrac_yield_ms(uint32_t ms);

// Has the operator asked to stop? Latched by pitrac_service() when a character
// arrives, and sticky until cleared. Long loops poll this at their natural
// boundary -- per sweep point, per burst -- rather than at every sleep.
bool pitrac_abort_pending(void);

// Clear the latch. A command calls this on entry so a stray keystroke typed
// during the PREVIOUS command cannot abort this one.
void pitrac_abort_clear(void);

// ---------------------------------------------------------------------------
// THE WATCHDOG, AND WHY IT IS CONDITIONAL
//
// A watchdog reset resets the pads. GPIO15 goes high-Z, R12 pulls the latch
// gate low, and the +5 V rail opens -- so on this board a watchdog reset is a
// HARD POWER CUT to the Pi, not a recovery. A firmware hang would become a
// corrupted filesystem.
//
// The policy decided in Phase 1b: enable the reset action ONLY while the Pi is
// down. With a Pi up, a hung MCU is the lesser evil -- the rail stays on, the
// Pi keeps running, and the operator can intervene.
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
