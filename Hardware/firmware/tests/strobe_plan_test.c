// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
//
// Host tests for strobe_plan.c: the section-15 schedule, burst validation, the
// PIO word encoding and the 6a/6b admission policy. No hardware, no SDK.
#include "strobe_plan.h"
#include "board.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *test_name;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s failed: %s\n", __FILE__, __LINE__, test_name, #condition); \
        return false; \
    } \
} while (0)

static bool near(float a, float b) { return fabsf(a - b) < 1e-4f; }

// Compile-time relations between the board.h live constants.
_Static_assert(STROBE_LIVE_GATE_CEILING <= DAC_TOP + 1u, "ceiling above full scale");
// The record buffer holds the whole readback window, and the 16384-sample ADC
// ring (ADC_RING_SAMPLES) holds the buffer.
_Static_assert(STROBE_LIVE_WAVE_MAX_SAMPLES * STROBE_LIVE_SAMPLE_US
               >= STROBE_LIVE_SPAN_MAX_US + STROBE_LIVE_PRE_US + STROBE_LIVE_POST_US,
               "wave buffer shorter than the readback window");
_Static_assert(STROBE_LIVE_WAVE_MAX_SAMPLES < 16384u, "wave buffer exceeds the ADC ring");

typedef struct {
    float    v;
    uint32_t width, period, gap, count, span;
    float    charge;
    bool     shed, clamped;
} row_t;

// Design section 15, rows recomputed from 1.0 mm blur, 42.67 mm spacing, 10
// pulses, 9 A and the 6.0 mC limit. Width rounds down, period to nearest.
static const row_t k_rows[] = {
    { 90.0f, 11,   474,   463, 10,  4277, 0.99f, false, false },
    { 50.0f, 20,   853,   833, 10,  7697, 1.80f, false, false },
    { 20.0f, 50,  2134,  2084, 10, 19256, 4.50f, false, false },
    { 10.0f, 100, 4267,  4167,  6, 21435, 5.40f, true,  false },
    {  2.0f, 100, 21335, 21235, 6, 106775, 5.40f, true, true  },
    {100.0f, 10,   427,   417, 10,  3853, 0.90f, false, false },
};

static bool schedule_table(void) {
    for (size_t i = 0; i < sizeof(k_rows) / sizeof(k_rows[0]); i++) {
        const row_t *r = &k_rows[i];
        strobe_schedule_t s;
        CHECK(strobe_compute_schedule(r->v, &s) == STROBE_PLAN_OK);
        CHECK(s.width_us == r->width);
        CHECK(s.period_us == r->period);
        CHECK(s.gap_us == r->gap);
        CHECK(s.count == r->count);
        CHECK(s.count_requested == BURST_PULSES_NOMINAL);
        CHECK(s.span_us == r->span);
        CHECK(near(s.charge_mc, r->charge));
        CHECK(s.shed == r->shed);
        CHECK(s.width_clamped == r->clamped);
        CHECK(!s.gap_raised);
        CHECK(s.gap_us >= (uint32_t)STROBE_MIN_GAP_US);
        CHECK(s.width_us <= (uint32_t)STROBE_SW_MAX_US);
        CHECK(s.charge_mc <= BURST_CHARGE_MAX_MC);
        strobe_burst_t b = { s.width_us, s.gap_us, s.count };
        CHECK(strobe_validate_burst(&b, (uint32_t)STROBE_SW_MAX_US) == STROBE_BURST_OK);
    }
    return true;
}

// The 10 m/s row is the only one in the design table over the limit: it must
// shed to the largest count that fits (6 x 0.9 mC = 5.4; 7 would be 6.3), keep
// the spacing, and never go below BURST_PULSES_MIN.
static bool shed_keeps_spacing(void) {
    strobe_schedule_t s;
    CHECK(strobe_compute_schedule(10.0f, &s) == STROBE_PLAN_OK);
    CHECK(s.shed && s.count == 6u);
    CHECK(strobe_burst_charge_mc(s.width_us, s.count + 1u) > BURST_CHARGE_MAX_MC);
    CHECK(s.period_us == 4267u);
    CHECK(s.count >= BURST_PULSES_MIN);
    return true;
}

static bool schedule_rejects_bad_speed(void) {
    const float bad[] = { 0.0f, -1.0f, 1.99f, 100.01f, NAN, INFINITY, -INFINITY };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        strobe_schedule_t s;
        CHECK(strobe_compute_schedule(bad[i], &s) == STROBE_PLAN_BAD_SPEED);
        CHECK(s.count == 0u);
    }
    strobe_schedule_t s;
    CHECK(strobe_compute_schedule(STROBE_V_MIN_MPS, &s) == STROBE_PLAN_OK);
    CHECK(strobe_compute_schedule(STROBE_V_MAX_MPS, &s) == STROBE_PLAN_OK);
    return true;
}

static bool validate_limits(void) {
    const uint32_t sw = (uint32_t)STROBE_SW_MAX_US;
    strobe_burst_t b = { 20u, 500u, 10u };
    CHECK(strobe_validate_burst(&b, sw) == STROBE_BURST_OK);

    b.width_us = (uint32_t)STROBE_MIN_WIDTH_US - 1u;
    CHECK(strobe_validate_burst(&b, sw) == STROBE_BURST_WIDTH);
    b.width_us = sw + 1u;
    CHECK(strobe_validate_burst(&b, sw) == STROBE_BURST_WIDTH);
    b.width_us = 20u;

    b.gap_us = (uint32_t)STROBE_MIN_GAP_US - 1u;
    CHECK(strobe_validate_burst(&b, sw) == STROBE_BURST_GAP);
    b.gap_us = (uint32_t)STROBE_MAX_GAP_US + 1u;
    CHECK(strobe_validate_burst(&b, sw) == STROBE_BURST_GAP);
    b.gap_us = 500u;

    b.count = 0u;
    CHECK(strobe_validate_burst(&b, sw) == STROBE_BURST_COUNT);
    b.count = (uint32_t)STROBE_BURST_MAX_PULSES + 1u;
    CHECK(strobe_validate_burst(&b, sw) == STROBE_BURST_COUNT);

    // The design's unshed slow-ball burst is exactly what the interlock exists for.
    strobe_burst_t slow = { 100u, 4167u, 10u };
    CHECK(strobe_validate_burst(&slow, sw) == STROBE_BURST_CHARGE);

    // A single pulse ignores the gap.
    strobe_burst_t one = { 50u, 0u, 1u };
    CHECK(strobe_validate_burst(&one, sw) == STROBE_BURST_OK);
    return true;
}

// The clamp test may command past the software limit, and only past it, and its
// charge is bounded by the U5 clamp rather than the commanded width.
static bool clamptest_bounds(void) {
    const uint32_t ct = (uint32_t)STROBE_CLAMPTEST_MAX_US;
    strobe_burst_t b = { 1000u, 0u, 1u };
    CHECK(strobe_validate_burst(&b, (uint32_t)STROBE_SW_MAX_US) == STROBE_BURST_WIDTH);
    CHECK(strobe_validate_burst(&b, ct) == STROBE_BURST_OK);
    b.width_us = ct + 1u;
    CHECK(strobe_validate_burst(&b, ct) == STROBE_BURST_WIDTH);
    CHECK(near(strobe_burst_charge_mc(ct, 1u),
               strobe_burst_charge_mc((uint32_t)STROBE_HW_LIMIT_US, 1u)));
    return true;
}

static bool encode_words(void) {
    uint32_t w[8];
    strobe_burst_t b = { 20u, 500u, 2u };
    CHECK(strobe_encode(&b, w, 8) == 5u);
    CHECK(w[0] == 18u && w[1] == 492u && w[2] == 18u && w[3] == 0u && w[4] == 0u);

    strobe_burst_t one = { 1000u, 0u, 1u };
    CHECK(strobe_encode(&one, w, 8) == 3u);
    CHECK(w[0] == 998u && w[1] == 0u && w[2] == 0u);

    // The shortest encodable pulse must not collide with the sentinel.
    strobe_burst_t min = { STROBE_PIO_MIN_WIDTH_US, 0u, 1u };
    CHECK(strobe_encode(&min, w, 8) == 3u && w[0] == 1u);
    min.width_us = STROBE_PIO_MIN_WIDTH_US - 1u;
    CHECK(strobe_encode(&min, w, 8) == 0u);

    strobe_burst_t short_gap = { 20u, STROBE_PIO_GAP_OVERHEAD_US - 1u, 2u };
    CHECK(strobe_encode(&short_gap, w, 8) == 0u);
    strobe_burst_t big = { 20u, 500u, 4u };
    CHECK(strobe_encode(&big, w, 8) == 0u);       // needs 9 words
    strobe_burst_t none = { 20u, 500u, 0u };
    CHECK(strobe_encode(&none, w, 8) == 0u);

    // Every width word in a full-size burst is a non-zero, non-sentinel value.
    uint32_t full[2 * STROBE_BURST_MAX_PULSES + 1];
    strobe_burst_t max = { (uint32_t)STROBE_MIN_WIDTH_US, (uint32_t)STROBE_MIN_GAP_US,
                           (uint32_t)STROBE_BURST_MAX_PULSES };
    size_t n = strobe_encode(&max, full, sizeof(full) / sizeof(full[0]));
    CHECK(n == 2u * STROBE_BURST_MAX_PULSES + 1u);
    for (size_t i = 0; i + 1 < n; i += 2) CHECK(full[i] != 0u);
    CHECK(full[n - 1] == 0u);
    return true;
}

static bool burst_duration(void) {
    strobe_burst_t b = { 20u, 500u, 10u };
    // 3 startup + 10 x 20 + 9 x 500 + 8 to IRQ0
    CHECK(strobe_burst_duration_us(&b) == 3u + 200u + 4500u + 8u);
    strobe_burst_t one = { 200u, 0u, 1u };
    CHECK(strobe_burst_duration_us(&one) == 3u + 200u + 8u);
    return true;
}

static strobe_snapshot_t safe_state(void) {
    strobe_snapshot_t s;
    memset(&s, 0, sizeof(s));
    s.rails_ready = true;
    return s;
}

static bool policy_basic(void) {
    strobe_snapshot_t s = safe_state();
    CHECK(strobe_may_fire(&s) == STROBE_OK);
    CHECK(strobe_may_set_gate(&s, 1u) == STROBE_OK);

    s.gate_nonzero = true;
    CHECK(strobe_may_fire(&s) == STROBE_REFUSE_GATE_NONZERO);
    s = safe_state();
    s.engine_busy = true;
    CHECK(strobe_may_set_gate(&s, 512u) == STROBE_REFUSE_BUSY);
    s = safe_state();
    s.rails_ready = false;
    CHECK(strobe_may_fire(&s) == STROBE_REFUSE_RAIL);
    CHECK(strobe_may_set_gate(&s, 1u) == STROBE_REFUSE_RAIL);
    s = safe_state();
    s.pulse_limit_defeated = true;
    CHECK(strobe_may_fire(&s) == STROBE_REFUSE_PULSE_LIMIT);
    s = safe_state();
    s.ready_led_on_pwm = true;
    CHECK(strobe_may_set_gate(&s, 1u) == STROBE_REFUSE_A7);
    return true;
}

// Every combination of the eight inputs. The invariants are the whole point of
// the 6a/6b build:
//   - a pulse is never admitted with the gate non-zero, and never unless every
//     other safety input is clear;
//   - the gate is never raised while a pulse could start;
//   - lowering the gate to zero is always admitted.
static bool policy_exhaustive(void) {
    for (unsigned m = 0; m < 256u; m++) {
        strobe_snapshot_t s;
        s.rails_ready          = (m & 1u)   != 0;
        s.fault_latched        = (m & 2u)   != 0;
        s.pi_present           = (m & 4u)   != 0;
        s.pulse_limit_defeated = (m & 8u)   != 0;
        s.ready_led_on_pwm     = (m & 16u)  != 0;
        s.engine_busy          = (m & 32u)  != 0;
        s.pulse_pin_high       = (m & 64u)  != 0;
        s.gate_nonzero         = (m & 128u) != 0;

        bool clear = s.rails_ready && !s.fault_latched && !s.pi_present
                  && !s.pulse_limit_defeated && !s.ready_led_on_pwm
                  && !s.engine_busy && !s.pulse_pin_high;

        bool fire = strobe_may_fire(&s) == STROBE_OK;
        CHECK(fire == (clear && !s.gate_nonzero));

        for (unsigned level = 1u; level <= DAC_TOP + 1u; level += 341u) {
            bool gate = strobe_may_set_gate(&s, (uint16_t)level) == STROBE_OK;
            CHECK(gate == clear);
            CHECK(!(gate && (s.engine_busy || s.pulse_pin_high)));
        }
        CHECK(strobe_may_set_gate(&s, 0u) == STROBE_OK);
    }
    return true;
}

static bool refusal_texts(void) {
    for (int r = STROBE_OK; r <= STROBE_REFUSE__LAST; r++)
        CHECK(strcmp(strobe_refusal_text((strobe_refusal_t)r), "?") != 0);
    for (int e = STROBE_BURST_OK; e <= STROBE_BURST_SPAN; e++)
        CHECK(strcmp(strobe_burst_err_text((strobe_burst_err_t)e), "?") != 0);
    return true;
}

// Review finding 2026-10-02: zeroing the DAC register does not zero Q9's gate.
// After a non-zero -> zero transition the gate must count as non-zero for the
// whole DAC_SETTLE_MS, so `strobe off` then `strobe pulse` cannot pulse into a
// still-charged gate.
static bool gate_settling(void) {
    const uint64_t window = (uint64_t)DAC_SETTLE_MS * 1000u;
    const uint64_t t0 = 5000000u;
    CHECK(!strobe_gate_settling(0u, 0u));
    CHECK(!strobe_gate_settling(123456789u, 0u));       // never zeroed from non-zero
    CHECK(strobe_gate_settling(t0, t0));
    CHECK(strobe_gate_settling(t0 + window - 1u, t0));
    CHECK(!strobe_gate_settling(t0 + window, t0));
    CHECK(!strobe_gate_settling(t0 + 10u * window, t0));
    CHECK(strobe_gate_settling(t0 - 1u, t0));           // clock went backwards: refuse

    // Wired through the policy: a settling gate is reported as non-zero, so a
    // pulse is refused for exactly the gate reason.
    strobe_snapshot_t s = safe_state();
    s.gate_nonzero = strobe_gate_settling(t0 + 1000u, t0);
    CHECK(strobe_may_fire(&s) == STROBE_REFUSE_GATE_NONZERO);
    s.gate_nonzero = strobe_gate_settling(t0 + window, t0);
    CHECK(strobe_may_fire(&s) == STROBE_OK);
    return true;
}

// ---------------------------------------------------------------------------
// Live policy (6c/6d)
// ---------------------------------------------------------------------------

static strobe_live_t live_ok(void) {
    strobe_live_t l;
    memset(&l, 0, sizeof(l));
    l.live = true;
    l.watchdog_armed = true;
    l.adc_idle = true;
    l.gate_hw_match = true;
    l.interval_ok = true;
    l.budget_ok = true;
    return l;
}

static bool live_gate_limit(void) {
    CHECK(strobe_live_gate_limit(0u) == STROBE_LIVE_GATE_STEP_MAX);
    CHECK(strobe_live_gate_limit(100u) == 100u + STROBE_LIVE_GATE_STEP_MAX);
    CHECK(strobe_live_gate_limit(STROBE_LIVE_GATE_CEILING) == STROBE_LIVE_GATE_CEILING);
    CHECK(strobe_live_gate_limit(STROBE_LIVE_GATE_CEILING - 1u) == STROBE_LIVE_GATE_CEILING);
    CHECK(strobe_live_gate_limit(DAC_TOP + 1u) == STROBE_LIVE_GATE_CEILING);
    CHECK(strobe_live_gate_limit(65535u) == STROBE_LIVE_GATE_CEILING);   // no 16-bit wrap
    return true;
}

// BENCH_P6 raises the gate in typed 3 % steps. Every one must fit the staircase
// from the level before it, all the way to `strobe gate 70` at the ceiling --
// rounded exactly the way cmd_strobe() rounds a percentage.
static bool live_typed_steps(void) {
    uint16_t proven = 0;
    for (int pct = 3; pct <= 70; pct += (pct < 69 ? 3 : 1)) {
        uint16_t level = (uint16_t)lroundf((float)pct * (float)(DAC_TOP + 1u) / 100.0f);
        CHECK(level <= strobe_live_gate_limit(proven));
        proven = level;
    }
    CHECK(proven == STROBE_LIVE_GATE_CEILING);
    return true;
}

static bool live_policy_reasons(void) {
    strobe_snapshot_t s = safe_state();
    strobe_live_t l = live_ok();
    CHECK(strobe_live_may_fire(&s, &l) == STROBE_OK);
    CHECK(strobe_live_may_continue(&s, &l) == STROBE_OK);

    l = live_ok(); l.live = false;
    CHECK(strobe_live_may_fire(&s, &l) == STROBE_REFUSE_NOT_LIVE);
    CHECK(strobe_live_may_set_gate(&s, &l, 1u) == STROBE_REFUSE_NOT_LIVE);
    l = live_ok(); l.watchdog_armed = false;
    CHECK(strobe_live_may_fire(&s, &l) == STROBE_REFUSE_WATCHDOG);
    CHECK(strobe_live_may_continue(&s, &l) == STROBE_REFUSE_WATCHDOG);
    l = live_ok(); l.beam_on = true;
    CHECK(strobe_live_may_fire(&s, &l) == STROBE_REFUSE_BEAM);
    l = live_ok(); l.detect_armed = true;
    CHECK(strobe_live_may_fire(&s, &l) == STROBE_REFUSE_DETECT);
    l = live_ok(); l.adc_idle = false;
    CHECK(strobe_live_may_fire(&s, &l) == STROBE_REFUSE_ADC);
    l = live_ok(); l.gate_hw_match = false;
    CHECK(strobe_live_may_fire(&s, &l) == STROBE_REFUSE_GATE_MISMATCH);
    CHECK(strobe_live_may_continue(&s, &l) == STROBE_REFUSE_GATE_MISMATCH);
    l = live_ok(); l.gate_level = STROBE_LIVE_GATE_STEP_MAX + 1u;
    CHECK(strobe_live_may_fire(&s, &l) == STROBE_REFUSE_STEP);
    CHECK(strobe_live_may_set_gate(&s, &l, STROBE_LIVE_GATE_STEP_MAX + 1u) == STROBE_REFUSE_STEP);
    CHECK(strobe_live_may_set_gate(&s, &l, STROBE_LIVE_GATE_STEP_MAX) == STROBE_OK);
    l = live_ok(); l.gate_proven = STROBE_LIVE_GATE_CEILING; l.gate_level = STROBE_LIVE_GATE_CEILING + 1u;
    CHECK(strobe_live_may_fire(&s, &l) == STROBE_REFUSE_CEILING);
    CHECK(strobe_live_may_set_gate(&s, &l, STROBE_LIVE_GATE_CEILING + 1u) == STROBE_REFUSE_CEILING);
    CHECK(strobe_live_may_continue(&s, &l) == STROBE_REFUSE_CEILING);
    l = live_ok(); l.interval_ok = false;
    CHECK(strobe_live_may_fire(&s, &l) == STROBE_REFUSE_INTERVAL);
    CHECK(strobe_live_may_continue(&s, &l) == STROBE_OK);         // pacing gates firing only
    l = live_ok(); l.budget_ok = false;
    CHECK(strobe_live_may_fire(&s, &l) == STROBE_REFUSE_BUDGET);

    // THE ONE RULE LIVE MODE RELAXES: a firing with the gate non-zero. Dry still
    // refuses it, and arming still needs the gate provably zero.
    s.gate_nonzero = true;
    l = live_ok(); l.gate_level = STROBE_LIVE_GATE_STEP_MAX;
    CHECK(strobe_live_may_fire(&s, &l) == STROBE_OK);
    CHECK(strobe_may_fire(&s) == STROBE_REFUSE_GATE_NONZERO);
    CHECK(strobe_live_may_arm(&s, &l) == STROBE_REFUSE_GATE_NONZERO);

    // The safety-input refusals come before every live one.
    s = safe_state(); s.fault_latched = true;
    l = live_ok(); l.beam_on = true; l.interval_ok = false;
    CHECK(strobe_live_may_fire(&s, &l) == STROBE_REFUSE_FAULT);
    CHECK(strobe_live_may_continue(&s, &l) == STROBE_REFUSE_FAULT);
    s = safe_state(); s.pulse_limit_defeated = true;
    CHECK(strobe_live_may_continue(&s, &l) == STROBE_REFUSE_PULSE_LIMIT);

    // Lowering to zero is always admitted, live or not, rail or not.
    s = safe_state(); s.rails_ready = false;
    l = live_ok(); l.live = false;
    CHECK(strobe_live_may_set_gate(&s, &l, 0u) == STROBE_OK);

    // NULLs refuse rather than crash.
    s = safe_state(); l = live_ok();
    CHECK(strobe_live_may_fire(NULL, &l) != STROBE_OK);
    CHECK(strobe_live_may_fire(&s, NULL) != STROBE_OK);
    CHECK(strobe_live_may_arm(NULL, &l) != STROBE_OK);
    CHECK(strobe_live_may_set_gate(&s, NULL, 1u) != STROBE_OK);
    CHECK(strobe_live_may_continue(&s, NULL) != STROBE_OK);
    return true;
}

// Every combination of the eight safety inputs and the eight live inputs, at
// gate and proven levels around every boundary. The invariants:
//   - a live firing is admitted exactly when every input is clear, the gate is
//     within the ceiling AND the staircase, and pacing allows it;
//   - so never above the ceiling or more than STEP_MAX over the proven level;
//   - raising the gate obeys the same staircase; lowering to 0 always works;
//   - arming needs the gate provably zero;
//   - holding needs the safety inputs, the watchdog and the environment.
static bool live_policy_exhaustive(void) {
    const uint16_t levels[] = { 0u, 1u, STROBE_LIVE_GATE_STEP_MAX, STROBE_LIVE_GATE_STEP_MAX + 1u,
                                100u, 131u, 132u, STROBE_LIVE_GATE_CEILING,
                                STROBE_LIVE_GATE_CEILING + 1u, DAC_TOP + 1u };
    const uint16_t proven[] = { 0u, 100u, STROBE_LIVE_GATE_CEILING - 10u, STROBE_LIVE_GATE_CEILING };
    for (unsigned m = 0; m < 65536u; m++) {
        strobe_snapshot_t s;
        s.rails_ready          = (m & 1u)   != 0;
        s.fault_latched        = (m & 2u)   != 0;
        s.pi_present           = (m & 4u)   != 0;
        s.pulse_limit_defeated = (m & 8u)   != 0;
        s.ready_led_on_pwm     = (m & 16u)  != 0;
        s.engine_busy          = (m & 32u)  != 0;
        s.pulse_pin_high       = (m & 64u)  != 0;
        s.gate_nonzero         = (m & 128u) != 0;
        strobe_live_t l;
        l.live           = (m & 0x100u)  != 0;
        l.watchdog_armed = (m & 0x200u)  != 0;
        l.beam_on        = (m & 0x400u)  != 0;
        l.detect_armed   = (m & 0x800u)  != 0;
        l.adc_idle       = (m & 0x1000u) != 0;
        l.gate_hw_match  = (m & 0x2000u) != 0;
        l.interval_ok    = (m & 0x4000u) != 0;
        l.budget_ok      = (m & 0x8000u) != 0;

        bool held   = s.rails_ready && !s.fault_latched && !s.pi_present
                   && !s.pulse_limit_defeated && !s.ready_led_on_pwm;
        bool common = held && !s.engine_busy && !s.pulse_pin_high;
        bool env    = !l.beam_on && !l.detect_armed && l.adc_idle;

        l.gate_level = 0u;
        l.gate_proven = 0u;
        CHECK((strobe_live_may_arm(&s, &l) == STROBE_OK) == (common && !s.gate_nonzero && env));

        for (size_t a = 0; a < sizeof(levels) / sizeof(levels[0]); a++) {
            for (size_t b = 0; b < sizeof(proven) / sizeof(proven[0]); b++) {
                uint16_t lv = levels[a];
                l.gate_level  = lv;
                l.gate_proven = proven[b];
                uint16_t lim  = strobe_live_gate_limit(proven[b]);

                bool fire = strobe_live_may_fire(&s, &l) == STROBE_OK;
                CHECK(fire == (l.live && common && l.watchdog_armed && env && l.gate_hw_match
                               && lv <= STROBE_LIVE_GATE_CEILING && lv <= lim
                               && l.interval_ok && l.budget_ok));
                CHECK(!(fire && (lv > STROBE_LIVE_GATE_CEILING || lv > lim)));
                CHECK(!(fire && !l.watchdog_armed));

                bool gate = strobe_live_may_set_gate(&s, &l, lv) == STROBE_OK;
                CHECK(gate == (lv == 0u || (l.live && common && lv <= lim)));

                bool hold = strobe_live_may_continue(&s, &l) == STROBE_OK;
                CHECK(hold == (l.live && held && l.watchdog_armed && env && l.gate_hw_match
                               && lv <= STROBE_LIVE_GATE_CEILING));
            }
        }
    }
    return true;
}

static bool live_validate_span(void) {
    const uint32_t sw = (uint32_t)STROBE_SW_MAX_US;
    // The 10 m/s schedule (6 x 100 us at 4267 us, 21.4 ms) fits the readback
    // window; the 2 m/s one (106.8 ms) does not, though it is a valid dry burst.
    strobe_schedule_t s;
    CHECK(strobe_compute_schedule(10.0f, &s) == STROBE_PLAN_OK);
    strobe_burst_t b = { s.width_us, s.gap_us, s.count };
    CHECK(strobe_validate_live_burst(&b, sw) == STROBE_BURST_OK);
    CHECK(strobe_compute_schedule(2.0f, &s) == STROBE_PLAN_OK);
    strobe_burst_t slow = { s.width_us, s.gap_us, s.count };
    CHECK(strobe_validate_burst(&slow, sw) == STROBE_BURST_OK);
    CHECK(strobe_validate_live_burst(&slow, sw) == STROBE_BURST_SPAN);

    // Exactly at the window, then one microsecond over.
    strobe_burst_t edge = { 20u, 29949u, 2u };
    CHECK(strobe_burst_duration_us(&edge) == (uint32_t)STROBE_LIVE_SPAN_MAX_US);
    CHECK(strobe_validate_live_burst(&edge, sw) == STROBE_BURST_OK);
    edge.gap_us++;
    CHECK(strobe_validate_live_burst(&edge, sw) == STROBE_BURST_SPAN);

    // The dry checks still come first.
    strobe_burst_t bad = { 0u, 0u, 1u };
    CHECK(strobe_validate_live_burst(&bad, sw) == STROBE_BURST_WIDTH);
    CHECK(strobe_validate_live_burst(NULL, sw) == STROBE_BURST_COUNT);

    // The 6d clamp test, at its longest, fits.
    strobe_burst_t ct = { (uint32_t)STROBE_CLAMPTEST_MAX_US, 0u, 1u };
    CHECK(strobe_validate_live_burst(&ct, (uint32_t)STROBE_CLAMPTEST_MAX_US) == STROBE_BURST_OK);

    return true;
}

int main(int argc, char **argv) {
    static const struct { const char *name; bool (*run)(void); } cases[] = {
#define TEST(name) {#name, name}
        TEST(schedule_table), TEST(shed_keeps_spacing), TEST(schedule_rejects_bad_speed),
        TEST(validate_limits), TEST(clamptest_bounds), TEST(encode_words),
        TEST(burst_duration), TEST(policy_basic), TEST(policy_exhaustive),
        TEST(refusal_texts), TEST(gate_settling), TEST(live_gate_limit),
        TEST(live_typed_steps), TEST(live_policy_reasons), TEST(live_policy_exhaustive),
        TEST(live_validate_span)
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
