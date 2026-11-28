// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
#include "service.h"
#include "board.h"
#include "power_fsm.h"
#include "safe_state.h"   // fault_generation()
#include "detect.h"
#include "shot.h"
#include "panel.h"
#include "strobe.h"

#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"

// Slice length for pitrac_yield_ms(). Everything downstream is debounced in
// hundreds of milliseconds (V5 monitor 500 ms, button 5 s for the long press),
// so 5 ms is far finer than anything needs while still costing nothing.
#define SERVICE_SLICE_MS 5u

// pitrac_yield_stoppable_ms(): how long a stop can go unseen.
#define STOPPABLE_PIECE_MS 20u

static volatile bool s_abort;
static bool s_abort_taken;

// Faults are LATCHED until acknowledged, so "a fault exists" is the wrong test
// -- it would make every command refuse instantly while the board sat in
// PS_FAULT, including the ones you would run to diagnose it. What a command
// cares about is a fault that LATCHED WHILE IT WAS RUNNING, so snapshot on entry.
//
// The snapshot is the fault GENERATION, not the code (audit SVC-02). Comparing
// codes misses a fault that was latched at entry, cleared mid-command (the
// button acknowledges PS_FAULT from inside any yield), and latched again with
// the same code. The generation counts every latch, so it cannot.
static uint32_t s_fault_gen_at_entry;

// Watchdog period. Must comfortably exceed the longest gap between two
// pitrac_service() calls. The board's own worst case is a single yield slice
// (5 ms) plus one FSM step, so 1 s is three orders of magnitude of headroom --
// deliberately, because the failure mode of being too tight is a spurious power
// cut to a Pi. Output is NOT inside that bound: printf waits on USB, and a host
// that stalls or trickles can hold one longer than a second (image 1.12, review
// note V111-N1). Do not arm the watchdog with a powered Pi on this margin.
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
    // Clear the enable bit directly. (SDK 2.3.0's watchdog_disable() does the
    // same; this line predates it, and the host tests mock the register.)
    hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);

    // watchdog_enable() left its mark in scratch[4], and the next boot reads a
    // reset with that mark as the watchdog having FIRED -- a hang, in the halt
    // telemetry (telemetry.h). Disarmed, no reset can be that, so clear it.
    // watchdog_reboot() (`reset`) and the boot ROM's UF2 reboot clear it as well;
    // this covers whatever else resets the board after a live session.
    watchdog_hw->scratch[4] = 0u;
}

bool pitrac_watchdog_enabled(void) { return s_wdog_on; }

yield_t pitrac_service(void) {
    // Kick FIRST, before any work that could itself hang. A watchdog that is
    // fed only after the risky part is a watchdog that never fires.
    if (s_wdog_on) watchdog_update();

    power_fsm_step();
    detect_service();        // drain the PIO transit FIFO and coalesce chatter
    shot_step();             // firing sequencer; does nothing until armed
    strobe_live_service();   // live strobe mode: hold conditions, idle timeout
    panel_onboard_update();  // D5/D6, always-on +3V3
    panel_update();          // J7 indicators, switched +5V

    // 🔴 NO getchar() HERE. See the note above pitrac_yield_ms().
    if (s_abort) return YIELD_ABORT_KEY;

    if (fault_generation() != s_fault_gen_at_entry) return YIELD_ABORT_FAULT;
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
static bool (*s_abort_source)(void);

void pitrac_abort_source(bool (*poll)(void)) { s_abort_source = poll; }

// The USB console's input, CR LF folded into one Enter (image 1.16, review
// finding V115-F1). The line editor (cli_service()) dispatches on the CR and runs
// the command there and then, so the LF of a client that ends its lines CR LF was
// still waiting -- and the command's first key poll read it as a stop.
// tools/scope.py sent CR LF, and its 4096-sample capture stopped after one sample
// with no key pressed. The LF is dropped only as the very next byte after a CR,
// whenever it arrives and whoever reads it; any other byte cancels that, so an LF
// typed later is a key like any other.
static bool s_lf_ends_cr;

int pitrac_console_getc(void) {
    for (;;) {
        const int ch = getchar_timeout_us(0);
        if (ch == PICO_ERROR_TIMEOUT) return PICO_ERROR_TIMEOUT;   // the LF may yet come
        const bool pair = s_lf_ends_cr && ch == '\n';
        s_lf_ends_cr = (ch == '\r');
        if (!pair) return ch;
    }
}

static void poll_abort_key(void) {
    if (pitrac_console_getc() != PICO_ERROR_TIMEOUT) s_abort = true;
    // The Pi's CLI_ABORT, while the Pi runs a command (pi_link.c). Called every
    // slice even once aborted: it also sends the link's keepalive.
    if (s_abort_source && s_abort_source()) s_abort = true;
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

yield_t pitrac_yield_stoppable_ms(uint32_t ms) {
    if (s_abort) return YIELD_ABORT_KEY;
    // A zero wait is still one poll of both stop sources (the USB key, the Pi's
    // CLI_ABORT) and one service pass, as pitrac_yield_ms(0) is (image 1.15,
    // review finding V114-F4). In 1.14 it returned at once, so a `beam sweep`
    // with a 0 ms dwell ran all 1000 steps with no poll and no FSM step, and a
    // fault latched meanwhile went unseen by the loop.
    if (ms == 0u) return pitrac_yield_ms(0u);
    // No check of the latch in the loop: a piece that saw a stop returns it
    // (pitrac_service() reports the latch), so one that returns YIELD_OK leaves
    // none to find.
    absolute_time_t end = make_timeout_time_ms(ms);
    for (;;) {
        int64_t left_us = absolute_time_diff_us(get_absolute_time(), end);
        if (left_us <= 0) return YIELD_OK;
        uint32_t left_ms = (uint32_t)(left_us / 1000);
        if (left_ms == 0u) left_ms = 1u;
        yield_t r = pitrac_yield_ms(left_ms < STOPPABLE_PIECE_MS ? left_ms : STOPPABLE_PIECE_MS);
        if (r != YIELD_OK) return r;
    }
}

bool pitrac_abort_pending(void) { return s_abort; }

void pitrac_abort_taken(void) { s_abort_taken = true; }
bool pitrac_abort_was_taken(void) { return s_abort_taken; }

void pitrac_abort_clear(void) {
    s_abort = false;
    s_abort_taken = false;
    s_fault_gen_at_entry = fault_generation();
}
