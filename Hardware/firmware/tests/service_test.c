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
int getchar_timeout_us(uint32_t timeout_us) {
    (void)timeout_us;
    polls++;
    return (key_at_poll != 0u && polls == key_at_poll) ? 'x' : PICO_ERROR_TIMEOUT;
}

void watchdog_enable(uint32_t delay_ms, bool pause_on_debug) {
    (void)delay_ms;
    (void)pause_on_debug;
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
        TEST(watchdog_kicks), TEST(strobe_hook_every_pass)
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
