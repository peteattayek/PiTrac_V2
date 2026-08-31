// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
#include "service.h"
#include "board.h"
#include "power_fsm.h"
#include "safe_state.h"   // fault_t / fault_current()
#include "detect.h"
#include "shot.h"
#include "panel.h"

#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"

// Slice length for pitrac_yield_ms(). Everything downstream is debounced in
// hundreds of milliseconds (V5 monitor 500 ms, button 5 s for the long press),
// so 5 ms is far finer than anything needs while still costing nothing.
#define SERVICE_SLICE_MS 5u

static volatile bool s_abort;

// Faults are LATCHED until acknowledged, so "a fault exists" is the wrong test
// -- it would make every command refuse instantly while the board sat in
// PS_FAULT, including the ones you would run to diagnose it. What a command
// cares about is a fault that appeared WHILE IT WAS RUNNING, so snapshot the
// state on entry and compare against that.
static fault_t s_fault_at_entry = FAULT_NONE;

// Watchdog period. Must comfortably exceed the longest gap between two
// pitrac_service() calls. The worst case is a single yield slice (5 ms) plus
// whatever one FSM step and one printf cost, so 1 s is three orders of
// magnitude of headroom -- deliberately, because the failure mode of being too
// tight is a spurious power cut to a Pi.
#define WATCHDOG_MS 1000u

static bool s_wdog_on;

void pitrac_watchdog_enable(bool on) {
    s_wdog_on = on;
    if (on) {
        watchdog_enable(WATCHDOG_MS, true);
        return;
    }

    // DISARM PROPERLY. An earlier version of this function just stopped kicking,
    // which is worse than useless -- it makes the watchdog fire SOONER, at the
    // exact moment the caller was trying to make it stop.
    //
    // The SDK has no watchdog_disable(), so clear the enable bit directly.
    hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
}

bool pitrac_watchdog_enabled(void) { return s_wdog_on; }

yield_t pitrac_service(void) {
    // Kick FIRST, before any work that could itself hang. A watchdog that is
    // fed only after the risky part is a watchdog that never fires.
    if (s_wdog_on) watchdog_update();

    power_fsm_step();
    detect_service();        // drain the PIO transit FIFO and coalesce chatter
    shot_step();             // firing sequencer; does nothing until armed
    panel_onboard_update();  // D5/D6, always-on +3V3
    panel_update();          // J7 indicators, switched +5V

    // 🔴 NO getchar() HERE. See the note above pitrac_yield_ms().
    if (s_abort) return YIELD_ABORT_KEY;

    fault_t f = fault_current();
    if (f != FAULT_NONE && f != s_fault_at_entry) return YIELD_ABORT_FAULT;
    return YIELD_OK;
}

// Poll for the operator's abort keystroke.
//
// 🔴 THIS MUST NOT RUN FROM THE SUPERLOOP, only from inside a blocking command.
//
// pitrac_service() has two callers: the superloop and pitrac_yield_ms(). The
// keypress poll was originally inside pitrac_service() with a comment claiming
// it was safe "because cli_service() is not on this path" -- which is true of
// the yield path and false of the superloop, where pitrac_service() runs one
// line BEFORE cli_service().
//
// The result: every idle pass of the superloop swallowed a character, so the
// CLI only ever saw a command with its first letter missing.
//
//     > cfg          ->  ? 'fg' -- try 'help'
//     > bootsel      ->  ? 'ootsel' -- try 'help'
//
// Reasoning about one caller and writing the conclusion down as a comment is
// what made it look correct. Introduced and found 2026-08-31.
static void poll_abort_key(void) {
    if (getchar_timeout_us(0) != PICO_ERROR_TIMEOUT) s_abort = true;
}

yield_t pitrac_yield_ms(uint32_t ms) {
    absolute_time_t end = make_timeout_time_ms(ms);
    poll_abort_key();
    yield_t r = pitrac_service();

    for (;;) {
        // Never overshoot: a caller asking for 20 ms of settling must not get
        // 25 because the last slice ran full length.
        int64_t left_us = absolute_time_diff_us(get_absolute_time(), end);
        if (left_us <= 0) break;
        uint32_t left_ms = (uint32_t)(left_us / 1000);
        sleep_ms(left_ms < SERVICE_SLICE_MS ? (left_ms ? left_ms : 1u) : SERVICE_SLICE_MS);

        // Report an abort but still honour the delay. Cutting a settling time
        // short would be a silent, hard-to-trace corruption of whatever the
        // caller measures next; costing one extra sleep on the way out is the
        // cheaper mistake. Callers unwind at their next loop boundary.
        poll_abort_key();
        yield_t s = pitrac_service();
        if (s != YIELD_OK) r = s;
    }
    return r;
}

bool pitrac_abort_pending(void) { return s_abort; }

void pitrac_abort_clear(void) {
    s_abort = false;
    s_fault_at_entry = fault_current();
}
