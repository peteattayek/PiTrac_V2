// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
//
// Host tests for strobe_live.c: the ADC0 readback of a live firing, the rolling
// charge budget, the firing interval, the idle timeout and the `strobe cal`
// step logic. No hardware, no SDK.
#include "strobe_live.h"
#include "strobe_plan.h"
#include "board.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *test_name;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s failed: %s\n", __FILE__, __LINE__, test_name, #condition); \
        return false; \
    } \
} while (0)

// Compile-time relations between the board.h constants.
// The real pacing can never fill the budget table: one firing per interval.
_Static_assert(STROBE_LIVE_BUDGET_SLOTS > STROBE_LIVE_BUDGET_WINDOW_MS / STROBE_LIVE_MIN_INTERVAL_MS,
               "budget table can fill");
// The cal never asks for a step the staircase rule would refuse.
_Static_assert(STROBE_CAL_COARSE_STEP <= STROBE_LIVE_GATE_STEP_MAX, "coarse step > staircase");
_Static_assert(STROBE_CAL_FINE_STEP <= STROBE_LIVE_GATE_STEP_MAX, "fine step > staircase");
_Static_assert(STROBE_CAL_FINE_STEP < STROBE_CAL_COARSE_STEP, "fine step not finer");
// Worst case -- current shows at the first step and the target sits at the
// ceiling -- still fits the point table.
_Static_assert(1u + STROBE_LIVE_GATE_CEILING / STROBE_CAL_FINE_STEP + 1u <= STROBE_CAL_MAX_POINTS,
               "cal point table too small");

// One ADC code is 6.0 mA through the 0.135 R sense.
#define AMP_TOL 0.01f

static uint16_t buf[STROBE_LIVE_WAVE_MAX_SAMPLES];

static void fill(size_t n, uint16_t base) {
    for (size_t i = 0; i < n; i++) buf[i] = base;
}

static uint16_t code_for(float amps, uint16_t base) {
    return (uint16_t)(base + (uint16_t)lroundf(strobe_amps_to_code(amps)));
}

static void put_pulse(size_t start, size_t samples, float amps, uint16_t base) {
    uint16_t c = code_for(amps, base);
    for (size_t k = start; k < start + samples; k++) buf[k] = c;
}

static bool code_amps(void) {
    // 9 A x 0.135 R = 1.215 V = 1507.7 codes.
    CHECK(fabsf(strobe_amps_to_code(9.0f) - 1507.7f) < 0.1f);
    CHECK(fabsf(strobe_code_to_amps(strobe_amps_to_code(4.5f)) - 4.5f) < 1e-4f);
    CHECK(fabsf(strobe_code_to_amps(4095.0f) - 24.444f) < 0.01f);    // full scale
    CHECK(fabsf(strobe_code_to_amps(1.0f) - 0.00597f) < 0.0001f);    // one code
    // The stop current is well inside the ADC range.
    CHECK(strobe_amps_to_code(STROBE_LIVE_I_PEAK_STOP_A) < 4095.0f);
    return true;
}

static bool single_pulse(void) {
    strobe_live_meas_t m;
    fill(200, 3);
    put_pulse(100, 10, 9.0f, 3);                 // 20 us at 9 A, 200 us into the window
    strobe_live_measure(buf, 200, 80, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_OK);
    CHECK(m.detected && m.n_pulses == 1u);
    CHECK(fabsf(m.baseline_code - 3.0f) < 1e-3f);
    CHECK(fabsf(m.pulse[0].amps - 9.0f) < AMP_TOL);
    CHECK(fabsf(m.max_amps - 9.0f) < AMP_TOL && fabsf(m.min_amps - 9.0f) < AMP_TOL);
    CHECK(m.pulse[0].width_us == 20u && m.pulse[0].samples == 10u);
    CHECK(m.pulse[0].start_us == 200u);
    CHECK(!m.pulse[0].short_run);
    CHECK(fabsf(m.charge_mc - 0.18f) < 0.001f);   // 9 A x 20 us
    CHECK(!m.on_at_end);
    // The threshold sits half-way up the pulse.
    CHECK(m.threshold_code > 3u + 700u && m.threshold_code < 3u + 800u);
    return true;
}

// The plateau drops the two edge samples, which straddle the rise and fall.
static bool plateau_excludes_edges(void) {
    strobe_live_meas_t m;
    fill(200, 0);
    put_pulse(100, 10, 9.0f, 0);
    buf[100] = code_for(5.0f, 0);                // a slow first sample, still over half
    buf[109] = code_for(6.0f, 0);                // and a slow last one
    strobe_live_measure(buf, 200, 80, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_OK && m.n_pulses == 1u);
    CHECK(fabsf(m.pulse[0].amps - 9.0f) < AMP_TOL);
    CHECK(m.pulse[0].width_us == 20u);
    return true;
}

static bool nothing_detected(void) {
    strobe_live_meas_t m;
    for (size_t i = 0; i < 300; i++) buf[i] = (uint16_t)(3u + (i * 3u) % 7u);   // 3..9 codes
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_OK);
    CHECK(!m.detected && m.n_pulses == 0u);
    CHECK(m.max_amps == 0.0f && m.charge_mc == 0.0f);
    return true;
}

// A lone sample over the threshold is ADC noise, not current -- even in the
// baseline window or at the very end, where a real run would latch a fault.
static bool lone_spikes_ignored(void) {
    strobe_live_meas_t m;
    fill(300, 2);
    buf[40]  = 60;                               // in the baseline window
    buf[299] = 60;                               // the last sample
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_OK);
    CHECK(!m.detected && !m.on_at_end);
    return true;
}

static bool small_current_detected(void) {
    // 0.2 A = 33 codes: over the 12-code floor, so it counts.
    strobe_live_meas_t m;
    fill(200, 3);
    put_pulse(100, 10, 0.2f, 3);
    strobe_live_measure(buf, 200, 80, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_OK && m.detected && m.n_pulses == 1u);
    CHECK(fabsf(m.pulse[0].amps - 0.2f) < AMP_TOL);
    // 0.05 A = 8 codes: under the floor.
    fill(200, 3);
    put_pulse(100, 10, 0.05f, 3);
    strobe_live_measure(buf, 200, 80, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_OK && !m.detected);
    return true;
}

static bool burst_segmentation(void) {
    strobe_live_meas_t m;
    const float amps[4] = { 9.0f, 8.9f, 8.8f, 8.6f };
    fill(1000, 4);
    for (size_t k = 0; k < 4; k++) put_pulse(100u + k * 85u, 10u, amps[k], 4);  // 20 us, 150 us gaps
    strobe_live_measure(buf, 1000, 80, 4u, &m);
    CHECK(m.verdict == STROBE_VERDICT_OK);
    CHECK(m.n_pulses == 4u);
    for (size_t k = 0; k < 4; k++) {
        CHECK(fabsf(m.pulse[k].amps - amps[k]) < AMP_TOL);
        CHECK(m.pulse[k].start_us == (uint32_t)(100u + k * 85u) * 2u);
        CHECK(m.pulse[k].width_us == 20u);
    }
    CHECK(fabsf(m.max_amps - 9.0f) < AMP_TOL && fabsf(m.min_amps - 8.6f) < AMP_TOL);
    CHECK(fabsf(m.charge_mc - (9.0f + 8.9f + 8.8f + 8.6f) * 0.02f) < 0.002f);
    return true;
}

static bool dips_merge(void) {
    strobe_live_meas_t m;
    // A 2-sample dip inside a pulse is still one pulse ...
    fill(300, 3);
    put_pulse(100, 30, 9.0f, 3);
    buf[115] = 3; buf[116] = 3;
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_OK && m.n_pulses == 1u);
    CHECK(m.pulse[0].width_us == 60u);
    // ... a 3-sample one is two -- which, for one fired pulse, is not a clean firing.
    buf[117] = 3;
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(m.n_pulses == 2u);
    CHECK(m.verdict == STROBE_VERDICT_NOISY);
    return true;
}

// Review finding 2026-10-07: one commanded pulse that conducts twice (a U5
// retrigger, ringing, an oscillating gate loop) was judged OK and raised the
// staircase. Two runs for one fired pulse is NOISY; for two fired it is fine.
static bool extra_conduction(void) {
    strobe_live_meas_t m;
    fill(400, 3);
    put_pulse(100, 10, 5.0f, 3);
    put_pulse(130, 10, 5.0f, 3);                 // 40 us later, nothing commanded
    strobe_live_measure(buf, 400, 80, 1u, &m);
    CHECK(m.n_pulses == 2u);
    CHECK(m.verdict == STROBE_VERDICT_NOISY);
    strobe_live_measure(buf, 400, 80, 2u, &m);
    CHECK(m.verdict == STROBE_VERDICT_OK);
    // Fewer runs than fired is fine: at low current a pulse can sit under the floor.
    fill(1000, 3);
    put_pulse(100, 10, 0.5f, 3);
    put_pulse(185, 10, 0.05f, 3);                // under the 72 mA floor
    put_pulse(270, 10, 0.5f, 3);
    strobe_live_measure(buf, 1000, 80, 3u, &m);
    CHECK(m.n_pulses == 2u && m.verdict == STROBE_VERDICT_OK);
    // An out-of-range count is treated as the engine's maximum.
    strobe_live_measure(buf, 1000, 80, 0u, &m);
    CHECK(m.fired == STROBE_BURST_MAX_PULSES);
    strobe_live_measure(buf, 1000, 80, 99u, &m);
    CHECK(m.fired == STROBE_BURST_MAX_PULSES);
    return true;
}

static bool short_run_kept(void) {
    strobe_live_meas_t m;
    fill(200, 3);
    put_pulse(100, 3, 4.0f, 3);                  // a 5 us pulse is 2-3 samples
    strobe_live_measure(buf, 200, 80, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_OK && m.n_pulses == 1u);
    CHECK(m.pulse[0].short_run);
    CHECK(fabsf(m.pulse[0].amps - 4.0f) < AMP_TOL);
    CHECK(m.pulse[0].width_us == 6u);
    return true;
}

// --- The verdicts that latch faults ---------------------------------------------

static bool stuck_baseline(void) {
    strobe_live_meas_t m;
    fill(200, (uint16_t)(STROBE_LIVE_BASELINE_MAX_CODES + 10u));   // ~0.3 A before any pulse
    put_pulse(100, 10, 9.0f, 0);
    strobe_live_measure(buf, 200, 80, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_STUCK_ON);
    // At the limit is still fine.
    fill(200, (uint16_t)STROBE_LIVE_BASELINE_MAX_CODES);
    put_pulse(100, 10, 9.0f, (uint16_t)STROBE_LIVE_BASELINE_MAX_CODES);
    strobe_live_measure(buf, 200, 80, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_OK);
    return true;
}

static bool stuck_early_run(void) {
    // A short run inside the baseline window, big enough to clear the half-peak
    // threshold but too short to lift the baseline mean over its own limit.
    strobe_live_meas_t m;
    fill(200, 3);
    put_pulse(70, 2, 8.0f, 3);
    put_pulse(100, 10, 9.0f, 3);
    strobe_live_measure(buf, 200, 80, 1u, &m);
    CHECK(m.baseline_code <= (float)STROBE_LIVE_BASELINE_MAX_CODES);
    CHECK(m.verdict == STROBE_VERDICT_STUCK_ON);
    return true;
}

static bool stuck_on_at_end(void) {
    strobe_live_meas_t m;
    fill(300, 3);
    put_pulse(100, 200, 2.0f, 3);                // still on at the last sample
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(m.on_at_end);
    CHECK(m.verdict == STROBE_VERDICT_STUCK_ON);
    return true;
}

static bool clamp_width_limit(void) {
    strobe_live_meas_t m;
    // 135 us -- what U5 measured -- is fine, and so is exactly the limit ...
    fill(400, 3);
    put_pulse(100, 68, 2.0f, 3);
    strobe_live_measure(buf, 400, 80, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_OK && m.max_width_us == 136u);
    fill(400, 3);
    put_pulse(100, STROBE_LIVE_MAX_ON_US / STROBE_LIVE_SAMPLE_US, 2.0f, 3);
    strobe_live_measure(buf, 400, 80, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_OK);
    // ... one sample longer is a clamp that did not clamp.
    fill(400, 3);
    put_pulse(100, STROBE_LIVE_MAX_ON_US / STROBE_LIVE_SAMPLE_US + 1u, 2.0f, 3);
    strobe_live_measure(buf, 400, 80, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_STUCK_ON);
    // A 1 ms clamp test that came back 1 ms long.
    fill(1000, 3);
    put_pulse(100, 500, 2.0f, 3);
    strobe_live_measure(buf, 1000, 80, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_STUCK_ON && m.max_width_us == 1000u);
    return true;
}

static bool overcurrent_plateau(void) {
    strobe_live_meas_t m;
    fill(200, 3);
    put_pulse(100, 10, STROBE_LIVE_I_STOP_A + 0.2f, 3);
    strobe_live_measure(buf, 200, 80, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_OVERCURRENT);
    fill(200, 3);
    put_pulse(100, 10, STROBE_LIVE_I_STOP_A - 0.1f, 3);
    strobe_live_measure(buf, 200, 80, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_OK);
    // One pulse of a burst is enough.
    fill(600, 3);
    put_pulse(100, 10, 9.0f, 3);
    put_pulse(200, 10, 11.5f, 3);
    put_pulse(300, 10, 9.0f, 3);
    strobe_live_measure(buf, 600, 80, 3u, &m);
    CHECK(m.verdict == STROBE_VERDICT_OVERCURRENT);
    return true;
}

static bool overcurrent_peak(void) {
    strobe_live_meas_t m;
    fill(200, 3);
    put_pulse(100, 10, 9.0f, 3);
    buf[104] = code_for(STROBE_LIVE_I_PEAK_STOP_A + 0.5f, 3);   // plateau mean stays under I_STOP
    strobe_live_measure(buf, 200, 80, 1u, &m);
    CHECK(m.max_amps < STROBE_LIVE_I_STOP_A);
    CHECK(m.verdict == STROBE_VERDICT_OVERCURRENT);
    return true;
}

// A clamp failure outranks an overcurrent: it is the hardware failure.
static bool stuck_outranks_overcurrent(void) {
    strobe_live_meas_t m;
    fill(300, 3);
    put_pulse(100, 200, 12.0f, 3);
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_STUCK_ON);
    return true;
}

static bool noisy(void) {
    strobe_live_meas_t m;
    fill(2000, 3);
    for (size_t k = 0; k < STROBE_BURST_MAX_PULSES + 1u; k++) put_pulse(100u + k * 20u, 4u, 1.0f, 3);
    strobe_live_measure(buf, 2000, 80, STROBE_BURST_MAX_PULSES, &m);
    CHECK(m.n_pulses == STROBE_BURST_MAX_PULSES + 1u);
    CHECK(m.verdict == STROBE_VERDICT_NOISY);
    return true;
}

static bool no_data(void) {
    strobe_live_meas_t m;
    fill(200, 3);
    strobe_live_measure(buf, 200, 7, 1u, &m);        // too little baseline
    CHECK(m.verdict == STROBE_VERDICT_NO_DATA);
    strobe_live_measure(buf, 83, 80, 1u, &m);        // nothing after it
    CHECK(m.verdict == STROBE_VERDICT_NO_DATA);
    strobe_live_measure(NULL, 200, 80, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_NO_DATA);
    strobe_live_measure(buf, 200, 80, 1u, NULL);     // must not crash
    return true;
}

static bool verdict_texts(void) {
    for (int v = STROBE_VERDICT_OK; v <= STROBE_VERDICT_NOISY; v++)
        CHECK(strcmp(strobe_verdict_text((strobe_verdict_t)v), "?") != 0);
    return true;
}

// Review finding 2026-10-07: the baseline/pulse split was taken from a clock read
// that an interrupt could separate from the ADC stop, so a healthy pulse could
// land in the baseline and latch STROBE_CLAMP. The split is now counted back
// from the stop instant itself. Sweep every enable-to-stop delay and every
// position of the newest sample within one conversion of the stop: the last
// baseline sample must stay >= 5 us before the first edge (enable + 3 us), and
// the split must not give up more than a few samples of real baseline.
static bool baseline_split(void) {
    const uint64_t t_en = 10000000u;
    const size_t got = 2000u;
    for (uint64_t d = 1u; d <= 3000u; d++) {
        uint64_t t_stop = t_en + d;
        size_t pre = strobe_live_baseline_samples(got, t_en, t_stop);
        CHECK(pre > 0u);
        for (int off = -2; off <= 2; off++) {
            int64_t newest = (int64_t)t_stop + off;
            int64_t last_base = newest - (int64_t)STROBE_LIVE_SAMPLE_US * (int64_t)(got - pre);
            CHECK(last_base <= (int64_t)t_en + 3 - 5);
        }
        CHECK(pre + 6u >= got - (size_t)(d / STROBE_LIVE_SAMPLE_US));
    }
    CHECK(strobe_live_baseline_samples(got, t_en, t_en) == 0u);
    CHECK(strobe_live_baseline_samples(got, t_en, t_en - 5u) == 0u);
    CHECK(strobe_live_baseline_samples(3u, t_en, t_en + 100u) == 0u);
    // A 20 us pulse: ~250 us from enable to stop, with 160 us of window before the enable.
    CHECK(strobe_live_baseline_samples(205u, t_en, t_en + 250u) == 76u);
    return true;
}

// --- Pacing -----------------------------------------------------------------

static bool budget_window(void) {
    strobe_budget_t b;
    strobe_budget_reset(&b);
    const uint32_t t = 1000u;
    CHECK(strobe_budget_admits(&b, t, STROBE_LIVE_BUDGET_MC));
    CHECK(!strobe_budget_admits(&b, t, STROBE_LIVE_BUDGET_MC + 0.01f));
    strobe_budget_book(&b, t, 20.0f);
    CHECK(fabsf(strobe_budget_used_mc(&b, t) - 20.0f) < 1e-4f);
    CHECK(strobe_budget_admits(&b, t, 10.0f));
    CHECK(!strobe_budget_admits(&b, t, 10.01f));
    CHECK(fabsf(strobe_budget_used_mc(&b, t + STROBE_LIVE_BUDGET_WINDOW_MS - 1u) - 20.0f) < 1e-4f);
    CHECK(strobe_budget_used_mc(&b, t + STROBE_LIVE_BUDGET_WINDOW_MS) == 0.0f);
    CHECK(strobe_budget_admits(&b, t + STROBE_LIVE_BUDGET_WINDOW_MS, STROBE_LIVE_BUDGET_MC));
    return true;
}

static bool budget_wrap(void) {
    strobe_budget_t b;
    strobe_budget_reset(&b);
    strobe_budget_book(&b, 0xFFFFFF00u, 25.0f);
    CHECK(fabsf(strobe_budget_used_mc(&b, 0x00000100u) - 25.0f) < 1e-4f);   // 512 ms later
    CHECK(!strobe_budget_admits(&b, 0x00000100u, 6.0f));
    CHECK(strobe_budget_used_mc(&b, 0xFFFFFF00u + STROBE_LIVE_BUDGET_WINDOW_MS) == 0.0f);
    return true;
}

// A full table refuses rather than forgetting a booking still in the window.
static bool budget_full_table(void) {
    strobe_budget_t b;
    strobe_budget_reset(&b);
    for (uint32_t k = 0; k < STROBE_LIVE_BUDGET_SLOTS; k++) {
        CHECK(strobe_budget_admits(&b, 5000u + k, 0.01f));
        strobe_budget_book(&b, 5000u + k, 0.01f);
    }
    CHECK(!strobe_budget_admits(&b, 5000u + STROBE_LIVE_BUDGET_SLOTS, 0.01f));
    // Once the oldest leaves the window there is room again.
    CHECK(strobe_budget_admits(&b, 5000u + STROBE_LIVE_BUDGET_WINDOW_MS, 0.01f));
    return true;
}

static bool budget_rejects_bad(void) {
    strobe_budget_t b;
    strobe_budget_reset(&b);
    CHECK(!strobe_budget_admits(&b, 0u, -1.0f));
    CHECK(!strobe_budget_admits(&b, 0u, NAN));
    CHECK(!strobe_budget_admits(NULL, 0u, 1.0f));
    strobe_budget_book(&b, 0u, NAN);
    strobe_budget_book(&b, 0u, -5.0f);
    CHECK(b.count == 0u);
    return true;
}

// A 20 us cal pulse and a full-limit burst, booked at the design current.
static bool budget_bookings(void) {
    CHECK(fabsf(strobe_burst_charge_mc(STROBE_CAL_WIDTH_US, 1u) - 0.18f) < 1e-4f);
    // A whole cal fits in one window even at the minimum interval.
    float per_window = strobe_burst_charge_mc(STROBE_CAL_WIDTH_US, 1u)
                     * (float)(STROBE_LIVE_BUDGET_WINDOW_MS / STROBE_LIVE_MIN_INTERVAL_MS);
    CHECK(per_window <= STROBE_LIVE_BUDGET_MC);
    return true;
}

static bool interval(void) {
    const uint64_t t = 50000000u;
    const uint64_t gap = (uint64_t)STROBE_LIVE_MIN_INTERVAL_MS * 1000u;
    CHECK(strobe_live_interval_ok(t, 0u));                  // never fired
    CHECK(!strobe_live_interval_ok(t + gap - 1u, t));
    CHECK(strobe_live_interval_ok(t + gap, t));
    CHECK(!strobe_live_interval_ok(t - 1u, t));             // clock went backwards
    return true;
}

static bool idle_timeout(void) {
    const uint32_t t = 1000u;
    CHECK(!strobe_live_idle_expired(t, t));
    CHECK(!strobe_live_idle_expired(t + STROBE_LIVE_IDLE_TIMEOUT_MS - 1u, t));
    CHECK(strobe_live_idle_expired(t + STROBE_LIVE_IDLE_TIMEOUT_MS, t));
    CHECK(!strobe_live_idle_expired(5u, 0xFFFFFF00u));       // across the wrap: 261 ms
    return true;
}

// --- `strobe cal` -------------------------------------------------------------

static bool cal_next_steps(void) {
    uint16_t nx = 0;
    CHECK(strobe_cal_next(0u, 0.0f, false, 9.0f, &nx) == STROBE_CAL_STEP);
    CHECK(nx == STROBE_CAL_COARSE_STEP);
    CHECK(strobe_cal_next(300u, 0.5f, true, 9.0f, &nx) == STROBE_CAL_STEP);
    CHECK(nx == 300u + STROBE_CAL_FINE_STEP);
    CHECK(strobe_cal_next(400u, 9.0f, true, 9.0f, &nx) == STROBE_CAL_SOLVED);
    CHECK(strobe_cal_next(400u, 9.0f * STROBE_CAL_ABORT_RATIO, true, 9.0f, &nx) == STROBE_CAL_SOLVED);
    CHECK(strobe_cal_next(400u, 9.0f * STROBE_CAL_ABORT_RATIO + 0.01f, true, 9.0f, &nx)
          == STROBE_CAL_OVERSHOOT);
    CHECK(strobe_cal_next(400u, NAN, true, 9.0f, &nx) == STROBE_CAL_OVERSHOOT);
    CHECK(strobe_cal_next(STROBE_LIVE_GATE_CEILING, 5.0f, true, 9.0f, &nx) == STROBE_CAL_CEILING);
    CHECK(strobe_cal_next(STROBE_LIVE_GATE_CEILING - 3u, 0.0f, false, 9.0f, &nx) == STROBE_CAL_STEP);
    CHECK(nx == STROBE_LIVE_GATE_CEILING);                   // capped, not past
    CHECK(strobe_cal_next(5u, 0.0f, false, 9.0f, NULL) == STROBE_CAL_STEP);
    return true;
}

// Why STROBE_CAL_MIN_A is 2 A, not 1: at the steepest slope the guards assume
// (~6 A/V at TP3, 9.64 mV per level), the first coarse step that shows current
// can land up to floor + 20 levels = ~1.23 A -- over 1.2 x a 1 A target. At the
// minimum target neither step size may be able to overshoot.
static bool cal_steps_cannot_overshoot(void) {
    const float a_per_level = 6.0f * 3.29f * 3.0f / 1024.0f;
    const float floor_a = strobe_code_to_amps((float)STROBE_LIVE_DETECT_MIN_CODES);
    float first_detect = floor_a + (float)STROBE_CAL_COARSE_STEP * a_per_level;
    CHECK(first_detect <= STROBE_CAL_MIN_A * STROBE_CAL_ABORT_RATIO);
    float fine = (float)STROBE_CAL_FINE_STEP * a_per_level;
    CHECK(fine <= STROBE_CAL_MIN_A * (STROBE_CAL_ABORT_RATIO - 1.0f));
    // And the 6d window sits inside what `strobe cal` can target.
    const float min_a = STROBE_CAL_MIN_A, lo_6d = STROBE_6D_MIN_A, hi_6d = STROBE_6D_MAX_A;
    CHECK(min_a >= lo_6d && min_a <= hi_6d);
    return true;
}

static bool cal_solve_interp(void) {
    CHECK(strobe_cal_solve(100u, 8.0f, 105u, 9.2f, 9.0f) == 104u);   // 104.17
    CHECK(strobe_cal_solve(100u, 8.0f, 105u, 9.2f, 9.2f) == 105u);
    CHECK(strobe_cal_solve(100u, 8.0f, 105u, 9.2f, 8.0f) == 100u);
    CHECK(strobe_cal_solve(100u, 8.0f, 105u, 9.2f, 7.0f) == 100u);   // clamped into [l0, l1]
    CHECK(strobe_cal_solve(100u, 9.0f, 105u, 8.0f, 8.5f) == 105u);   // not increasing: l1
    CHECK(strobe_cal_solve(105u, 8.0f, 100u, 9.0f, 8.5f) == 100u);   // l1 <= l0: l1
    CHECK(strobe_cal_solve(0u, 0.0f, 20u, 9.5f, 9.0f) == 19u);       // solved at the first step
    return true;
}

// The whole cal loop against a model of Q9: no current below a threshold, then
// linear. Run it for the steepest slope the guards were sized for (~6 A/V at TP3)
// and a shallow one. It must solve within 5 %, never step further than the
// staircase allows, never see more than the abort ratio, and fit the table.
typedef struct { float vth_v; float a_per_v; } plant_t;

static float plant_amps(const plant_t *p, uint16_t level) {
    float tp3 = 3.29f * 3.0f * (float)level / 1024.0f;
    float a = (tp3 - p->vth_v) * p->a_per_v;
    return a > 0.0f ? a : 0.0f;
}

static bool run_cal(const plant_t *p, float target, uint16_t *solved, uint32_t *steps) {
    uint16_t level = 0, prev_level = 0;
    float prev_amps = 0.0f;
    for (uint32_t n = 1; n <= STROBE_CAL_MAX_POINTS; n++) {
        float a = plant_amps(p, level);
        bool det = strobe_amps_to_code(a) > (float)STROBE_LIVE_DETECT_MIN_CODES;
        if (!det) a = 0.0f;
        CHECK(a <= target * STROBE_CAL_ABORT_RATIO);
        uint16_t nx = 0;
        strobe_cal_step_t st = strobe_cal_next(level, a, det, target, &nx);
        if (st == STROBE_CAL_SOLVED) {
            *solved = strobe_cal_solve(prev_level, prev_amps, level, a, target);
            *steps = n;
            return true;
        }
        CHECK(st == STROBE_CAL_STEP);
        CHECK(nx > level && nx - level <= STROBE_LIVE_GATE_STEP_MAX);
        prev_level = level;
        prev_amps = a;
        level = nx;
    }
    return false;
}

static bool cal_converges(void) {
    const plant_t plants[] = { { 2.5f, 6.0f }, { 2.0f, 3.0f }, { 3.5f, 5.0f }, { 2.47f, 6.0f } };
    const float targets[] = { 9.0f, 4.5f, STROBE_CAL_MIN_A };
    for (size_t i = 0; i < sizeof(plants) / sizeof(plants[0]); i++) {
        for (size_t j = 0; j < sizeof(targets) / sizeof(targets[0]); j++) {
            uint16_t solved = 0;
            uint32_t steps = 0;
            CHECK(run_cal(&plants[i], targets[j], &solved, &steps));
            CHECK(steps <= STROBE_CAL_MAX_POINTS);
            CHECK(solved <= STROBE_LIVE_GATE_CEILING);
            float err = (plant_amps(&plants[i], solved) - targets[j]) / targets[j];
            CHECK(err <= STROBE_CAL_CONFIRM_TOL && err >= -STROBE_CAL_CONFIRM_TOL);
        }
    }
    return true;
}

// A plant that never reaches the target hits the ceiling and stops.
static bool cal_ceiling(void) {
    const plant_t weak = { 6.0f, 2.0f };            // ~1.8 A at the ceiling
    uint16_t level = 0;
    for (uint32_t n = 0; n < STROBE_CAL_MAX_POINTS; n++) {
        float a = plant_amps(&weak, level);
        uint16_t nx = 0;
        strobe_cal_step_t st = strobe_cal_next(level, a, a > 0.07f, 9.0f, &nx);
        if (st == STROBE_CAL_CEILING) {
            CHECK(level == STROBE_LIVE_GATE_CEILING);
            return true;
        }
        CHECK(st == STROBE_CAL_STEP);
        level = nx;
    }
    return false;
}

int main(int argc, char **argv) {
    static const struct { const char *name; bool (*run)(void); } cases[] = {
#define TEST(name) {#name, name}
        TEST(code_amps), TEST(single_pulse), TEST(plateau_excludes_edges),
        TEST(nothing_detected), TEST(lone_spikes_ignored), TEST(small_current_detected),
        TEST(burst_segmentation), TEST(dips_merge), TEST(extra_conduction), TEST(short_run_kept),
        TEST(stuck_baseline), TEST(stuck_early_run), TEST(stuck_on_at_end),
        TEST(clamp_width_limit), TEST(overcurrent_plateau), TEST(overcurrent_peak),
        TEST(stuck_outranks_overcurrent), TEST(noisy), TEST(no_data), TEST(verdict_texts),
        TEST(baseline_split),
        TEST(budget_window), TEST(budget_wrap), TEST(budget_full_table),
        TEST(budget_rejects_bad), TEST(budget_bookings), TEST(interval), TEST(idle_timeout),
        TEST(cal_next_steps), TEST(cal_steps_cannot_overshoot), TEST(cal_solve_interp),
        TEST(cal_converges), TEST(cal_ceiling)
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
