// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
//
// Host tests for service.c -- the yield / abort contract that every long
// command, and now the live strobe, depends on: an abort or a fault on the LAST
// yield slice is still reported (audit SVC-03), a fault is detected by its
// latch generation rather than its code (SVC-02), the delay is never cut short
// or overshot, the watchdog is kicked on every pass, and the live-strobe hook
// runs on every pass.
#include "service.h"
#include "safe_state.h"
#include "strobe.h"
#include "power_fsm.h"
#include "detect.h"
#include "shot.h"
#include "panel.h"
#include "hardware/structs/watchdog.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

static const char *test_name;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s failed: %s\n", __FILE__, __LINE__, test_name, #condition); \
        return false; \
    } \
} while (0)

watchdog_hw_t mock_watchdog_hw;

static uint64_t clock_us;
static unsigned polls;            // getchar_timeout_us() calls
static unsigned key_at_poll;      // deliver a key on this poll (1-based); 0 = never
static unsigned services;         // pitrac_service() passes, counted in power_fsm_step()
static unsigned hook_calls;
static unsigned kicks;
static uint64_t last_kick_us, max_kick_gap_us;

typedef void (*event_fn)(void);
static unsigned event_at_service; // run `event` inside this pass (1-based)
static event_fn event;

// --- Mocks --------------------------------------------------------------------

static fault_t  fault;
static uint32_t fault_gen;
void fault_raise(fault_t f) {
    if (f == FAULT_NONE) return;
    if (fault == FAULT_NONE) { fault = f; fault_gen++; }
}
void fault_clear(void) { fault = FAULT_NONE; }
fault_t fault_current(void) { return fault; }
uint32_t fault_generation(void) { return fault_gen; }

absolute_time_t get_absolute_time(void) { return clock_us; }
uint32_t to_ms_since_boot(absolute_time_t t) { return (uint32_t)(t / 1000u); }
absolute_time_t make_timeout_time_ms(uint32_t ms) { return clock_us + (uint64_t)ms * 1000u; }
int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to) {
    return (int64_t)(to - from);
}
void sleep_ms(uint32_t ms) { clock_us += (uint64_t)ms * 1000u; }

// A scripted console (image 1.16): byte k can be read from poll in_at[k] on
// (1-based), in order. Without a script, key_at_poll as before.
static int      in_ch[16];
static unsigned in_at[16], in_n, in_next;
static void script(const char *bytes, const unsigned *at) {
    in_n = in_next = 0u;
    for (; bytes[in_n]; in_n++) { in_ch[in_n] = (unsigned char)bytes[in_n]; in_at[in_n] = at[in_n]; }
}

int getchar_timeout_us(uint32_t timeout_us) {
    (void)timeout_us;
    polls++;
    if (in_next < in_n)
        return polls >= in_at[in_next] ? in_ch[in_next++] : PICO_ERROR_TIMEOUT;
    return (key_at_poll != 0u && polls == key_at_poll) ? 'x' : PICO_ERROR_TIMEOUT;
}

void watchdog_enable(uint32_t delay_ms, bool pause_on_debug) {
    (void)delay_ms;
    (void)pause_on_debug;
    mock_watchdog_hw.scratch[4] = 0x6ab73121u;   // the SDK's "armed by watchdog_enable" mark
    mock_watchdog_hw.ctrl |= WATCHDOG_CTRL_ENABLE_BITS;
    last_kick_us = clock_us;
}
void watchdog_update(void) {
    kicks++;
    uint64_t gap = clock_us - last_kick_us;
    if (gap > max_kick_gap_us) max_kick_gap_us = gap;
    last_kick_us = clock_us;
}

void power_fsm_step(void) {
    services++;
    if (event && services == event_at_service) event();
}
void detect_service(void) {}
void shot_step(void) {}
void panel_onboard_update(void) {}
void panel_update(void) {}
void strobe_live_service(void) { hook_calls++; }

static void raise_cam(void)     { fault_raise(FAULT_CAM_TIMEOUT); }
static void relatch_same(void)  { fault_clear(); fault_raise(FAULT_STROBE_CLAMP); }
static void clear_only(void)    { fault_clear(); }

static void reset_fixture(void) {
    clock_us = 1000000u;
    polls = key_at_poll = services = hook_calls = kicks = 0u;
    last_kick_us = max_kick_gap_us = 0u;
    event_at_service = 0u;
    event = NULL;
    fault = FAULT_NONE;
    fault_gen = 0u;
    in_n = in_next = 0u;
    pitrac_watchdog_enable(false);
    pitrac_abort_clear();
}

// --- Tests --------------------------------------------------------------------

// pitrac_yield_ms(20) polls the key and services once up front, then once after
// each of four 5 ms slices: five passes. The last one is the final slice.
static bool final_slice_key(void) {
    reset_fixture();
    key_at_poll = 5u;
    uint64_t t0 = clock_us;
    CHECK(pitrac_yield_ms(20u) == YIELD_ABORT_KEY);
    CHECK(polls == 5u && services == 5u);
    CHECK(clock_us - t0 == 20000u);                 // the delay is still honoured
    CHECK(pitrac_abort_pending());
    pitrac_abort_clear();
    CHECK(!pitrac_abort_pending());
    return true;
}

static bool final_slice_fault(void) {
    reset_fixture();
    event = raise_cam;
    event_at_service = 5u;
    CHECK(pitrac_yield_ms(20u) == YIELD_ABORT_FAULT);
    CHECK(services == 5u);
    return true;
}

// A fault that was already latched when the command started is not a reason
// to abort it -- you have to be able to run commands to diagnose it.
static bool fault_at_entry_is_not_abort(void) {
    reset_fixture();
    fault_raise(FAULT_STROBE_CLAMP);
    pitrac_abort_clear();
    CHECK(pitrac_yield_ms(20u) == YIELD_OK);
    CHECK(pitrac_service() == YIELD_OK);
    return true;
}

// SVC-02: the same code cleared and latched again mid-command is a NEW fault.
// Comparing codes, as service.c did until 2026-10-07, returned YIELD_OK here.
static bool same_code_relatched(void) {
    reset_fixture();
    fault_raise(FAULT_STROBE_CLAMP);
    pitrac_abort_clear();
    event = relatch_same;
    event_at_service = 2u;
    CHECK(pitrac_yield_ms(20u) == YIELD_ABORT_FAULT);
    CHECK(fault_current() == FAULT_STROBE_CLAMP);
    return true;
}

static bool cleared_fault_is_not_abort(void) {
    reset_fixture();
    fault_raise(FAULT_CAM_TIMEOUT);
    pitrac_abort_clear();
    event = clear_only;
    event_at_service = 2u;
    CHECK(pitrac_yield_ms(20u) == YIELD_OK);
    return true;
}

static bool no_overshoot(void) {
    const uint32_t asks[] = { 0u, 1u, 3u, 5u, 7u, 20u, 1003u };
    for (size_t i = 0; i < sizeof(asks) / sizeof(asks[0]); i++) {
        reset_fixture();
        uint64_t t0 = clock_us;
        CHECK(pitrac_yield_ms(asks[i]) == YIELD_OK);
        CHECK(clock_us - t0 == (uint64_t)asks[i] * 1000u);
        CHECK(services >= 1u);
    }
    return true;
}

// The watchdog is fed on every pass, so the longest gap is one slice -- and a
// disarmed watchdog really is disarmed.
static bool watchdog_kicks(void) {
    reset_fixture();
    pitrac_watchdog_enable(true);
    CHECK(pitrac_watchdog_enabled());
    CHECK((mock_watchdog_hw.ctrl & WATCHDOG_CTRL_ENABLE_BITS) != 0u);
    CHECK(pitrac_yield_ms(1000u) == YIELD_OK);
    CHECK(kicks == services);
    CHECK(max_kick_gap_us <= 5000u);
    pitrac_watchdog_enable(false);
    CHECK(!pitrac_watchdog_enabled());
    CHECK((mock_watchdog_hw.ctrl & WATCHDOG_CTRL_ENABLE_BITS) == 0u);
    unsigned before = kicks;
    CHECK(pitrac_yield_ms(50u) == YIELD_OK);
    CHECK(kicks == before);
    return true;
}

// 2026-10-19: disarming clears watchdog_enable()'s scratch[4] mark, so a reset
// after a live session (`bootsel`, SW2) is never read at the next boot as the
// watchdog having fired (telemetry.h; main.c, boot_reason()).
static bool disarm_clears_mark(void) {
    reset_fixture();
    pitrac_watchdog_enable(true);
    CHECK(mock_watchdog_hw.scratch[4] == 0x6ab73121u);
    pitrac_watchdog_enable(false);
    CHECK(mock_watchdog_hw.scratch[4] == 0u);
    return true;
}

// Image 1.13 (1.14 unchanged): the Pi's CLI_ABORT source (pi_link.c) is polled with the key on
// every yield slice -- so it also sends its keepalive -- and NEVER by a bare
// pitrac_service(), which is the superloop's call: there it would consume the
// link's next request. Unregistered, nothing is polled.
static unsigned source_polls, source_true_at;
static bool source(void) { source_polls++; return source_true_at != 0u && source_polls == source_true_at; }

static bool abort_source_yield_only(void) {
    reset_fixture();
    source_polls = 0u;
    source_true_at = 0u;
    pitrac_abort_source(source);
    CHECK(pitrac_service() == YIELD_OK);
    CHECK(source_polls == 0u);                      // the superloop's pass: not polled
    CHECK(pitrac_yield_ms(20u) == YIELD_OK);
    CHECK(source_polls == polls && polls == 5u);    // every slice, with the key
    CHECK(!pitrac_abort_pending());

    reset_fixture();
    source_polls = 0u;
    source_true_at = 5u;                            // a CLI_ABORT on the final slice
    CHECK(pitrac_yield_ms(20u) == YIELD_ABORT_KEY);
    CHECK(pitrac_abort_pending());

    reset_fixture();
    source_polls = 0u;
    source_true_at = 2u;                            // early: still polled after (keepalive)
    CHECK(pitrac_yield_ms(20u) == YIELD_ABORT_KEY);
    CHECK(source_polls == 5u);

    pitrac_abort_source(NULL);
    reset_fixture();
    source_polls = 0u;
    CHECK(pitrac_yield_ms(20u) == YIELD_OK);
    CHECK(source_polls == 0u);
    return true;
}

// Image 1.14 (V113-F3): a stoppable wait ends within one 20 ms piece of a stop
// request -- a key here; the Pi's CLI_ABORT arrives by the same latch -- where
// pitrac_yield_ms() would wait out the whole delay. It services all the while.
static bool stoppable_ends_early(void) {
    reset_fixture();
    key_at_poll = 12u;                              // in the third 20 ms piece
    uint64_t t0 = clock_us;
    CHECK(pitrac_yield_stoppable_ms(4000u) == YIELD_ABORT_KEY);
    CHECK(clock_us - t0 == 60000u);                 // three pieces, not 4 s
    CHECK(pitrac_abort_pending());
    CHECK(services == 15u && kicks == 0u);          // serviced throughout (5 per piece)

    // Nothing asked: the whole wait, and YIELD_OK.
    reset_fixture();
    t0 = clock_us;
    CHECK(pitrac_yield_stoppable_ms(1003u) == YIELD_OK);
    CHECK(clock_us - t0 >= 1003000u && clock_us - t0 <= 1004000u);
    CHECK(!pitrac_abort_pending());

    // Already asked: no wait at all.
    reset_fixture();
    key_at_poll = 1u;
    CHECK(pitrac_yield_ms(0u) == YIELD_ABORT_KEY);  // the latch is set
    t0 = clock_us;
    unsigned s0 = services;
    CHECK(pitrac_yield_stoppable_ms(500u) == YIELD_ABORT_KEY);
    CHECK(clock_us == t0 && services == s0);

    // A fault ends it too, as a fault.
    reset_fixture();
    event = raise_cam;
    event_at_service = 7u;
    t0 = clock_us;
    CHECK(pitrac_yield_stoppable_ms(4000u) == YIELD_ABORT_FAULT);
    CHECK(clock_us - t0 == 40000u);
    return true;
}

// Image 1.15 (V114-F4): a zero-length stoppable wait still polls the key and
// the Pi's source once and services once -- as pitrac_yield_ms(0) does -- and
// reports a stop or a new fault. 1.14 returned YIELD_OK at once: none of them.
static bool stoppable_zero_still_polls(void) {
    reset_fixture();
    source_polls = 0u;
    source_true_at = 0u;
    pitrac_abort_source(source);
    const uint64_t t0 = clock_us;
    CHECK(pitrac_yield_stoppable_ms(0u) == YIELD_OK);
    CHECK(polls == 1u && source_polls == 1u && services == 1u && clock_us == t0);

    reset_fixture();                                // a key at USB
    key_at_poll = 1u;
    CHECK(pitrac_yield_stoppable_ms(0u) == YIELD_ABORT_KEY);
    CHECK(pitrac_abort_pending());

    reset_fixture();                                // the Pi's CLI_ABORT
    source_polls = 0u;
    source_true_at = 1u;
    CHECK(pitrac_yield_stoppable_ms(0u) == YIELD_ABORT_KEY);

    reset_fixture();                                // a fault latched in the pass
    source_true_at = 0u;
    event = raise_cam;
    event_at_service = 1u;
    CHECK(pitrac_yield_stoppable_ms(0u) == YIELD_ABORT_FAULT);
    pitrac_abort_source(NULL);
    return true;
}

// Image 1.14 (V113-F3): "taken" -- a command ended early because of the stop --
// is its own mark, set only by the command, and cleared with the latch.
static bool abort_taken_mark(void) {
    reset_fixture();
    CHECK(!pitrac_abort_was_taken());
    key_at_poll = 1u;
    CHECK(pitrac_yield_ms(5u) == YIELD_ABORT_KEY);
    CHECK(pitrac_abort_pending() && !pitrac_abort_was_taken());   // asked, not taken
    pitrac_abort_taken();
    CHECK(pitrac_abort_was_taken());
    pitrac_abort_clear();
    CHECK(!pitrac_abort_pending() && !pitrac_abort_was_taken());
    return true;
}

// Image 1.16 (V115-F1): CR LF is ONE Enter at the console. The line editor runs a
// command on the CR, and in 1.15 the command's first key poll read the client's
// LF as a stop: scope.py's 4096-sample capture ended after one sample. The LF is
// dropped only as the very next byte after a CR -- however late it comes, and
// whoever reads it -- and an LF after anything else is a key.
static void console_start(void) {           // a byte that is not CR: no LF owed
    static const unsigned at1[] = { 1u };
    script("z", at1);
    (void)pitrac_console_getc();
}

static bool console_crlf_is_one_enter(void) {
    reset_fixture();
    console_start();
    static const unsigned now[] = { 0u, 0u, 0u, 0u, 0u };
    script("\r\n", now);                           // together
    CHECK(pitrac_console_getc() == '\r');
    CHECK(pitrac_console_getc() == PICO_ERROR_TIMEOUT && in_next == 2u);   // the LF, dropped

    // The LF arrives after the command has started polling: still the Enter's.
    static const unsigned late[] = { 0u, 4u, 6u };
    polls = 0u;
    script("\r\nx", late);
    CHECK(pitrac_console_getc() == '\r');          // poll 1: the line editor dispatches
    pitrac_abort_clear();                          // as dispatch() does
    CHECK(pitrac_yield_ms(0u) == YIELD_OK);        // poll 2: nothing yet
    CHECK(pitrac_yield_ms(0u) == YIELD_OK);        // poll 3
    CHECK(pitrac_yield_ms(0u) == YIELD_OK);        // poll 4: the LF -- not a stop (5: nothing)
    CHECK(!pitrac_abort_pending() && in_next == 2u && polls == 5u);
    CHECK(pitrac_yield_ms(0u) == YIELD_ABORT_KEY && pitrac_abort_pending());   // poll 6: a key

    // An LF that is not the very next byte after a CR is a key.
    reset_fixture();
    console_start();
    script("\n", now);
    CHECK(pitrac_console_getc() == '\n');
    script("\ra\n", now);
    CHECK(pitrac_console_getc() == '\r' && pitrac_console_getc() == 'a');
    CHECK(pitrac_console_getc() == '\n');
    script("\r\r\n\n", now);                       // CR CR LF: the second CR's LF; then a key
    CHECK(pitrac_console_getc() == '\r' && pitrac_console_getc() == '\r');
    CHECK(pitrac_console_getc() == '\n' && in_next == 4u);
    // A CR typed as a stop is a stop, and its own LF is dropped with it.
    reset_fixture();
    console_start();
    script("\r\n", now);
    CHECK(pitrac_yield_ms(0u) == YIELD_ABORT_KEY);
    CHECK(pitrac_console_getc() == PICO_ERROR_TIMEOUT && in_next == 2u);
    return true;
}

// Live strobe mode's hold checks and idle timeout run on every pass.
static bool strobe_hook_every_pass(void) {
    reset_fixture();
    CHECK(pitrac_yield_ms(100u) == YIELD_OK);
    CHECK(hook_calls == services && services == 21u);
    CHECK(pitrac_service() == YIELD_OK);
    CHECK(hook_calls == 22u);
    return true;
}

int main(int argc, char **argv) {
    static const struct { const char *name; bool (*run)(void); } cases[] = {
#define TEST(name) {#name, name}
        TEST(final_slice_key), TEST(final_slice_fault), TEST(fault_at_entry_is_not_abort),
        TEST(same_code_relatched), TEST(cleared_fault_is_not_abort), TEST(no_overshoot),
        TEST(watchdog_kicks), TEST(strobe_hook_every_pass), TEST(disarm_clears_mark),
        TEST(abort_source_yield_only), TEST(stoppable_ends_early), TEST(abort_taken_mark),
        TEST(stoppable_zero_still_polls), TEST(console_crlf_is_one_enter)
#undef TEST
    };
    if (argc == 2) {
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            if (strcmp(argv[1], cases[i].name) != 0) continue;
            test_name = cases[i].name;
            if (!cases[i].run()) return 1;
            printf("PASS %s\n", test_name);
            return 0;
        }
    }
    fprintf(stderr, "Expected one registered test name\n");
    return 2;
}
