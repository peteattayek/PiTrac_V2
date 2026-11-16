// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
//
// Host tests for strobe_live.c: the ADC0 readback of a live firing, the rolling
// charge budget, the firing interval, the idle timeout and the `strobe cal`
// step logic. No hardware, no SDK.
#include "strobe_live.h"
#include "strobe_plan.h"
#include "board.h"
#include "adc_ring_math.h"
#include "adc_engine.h"     // ADC_RING_SAMPLES; declarations only, nothing called

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
// The session cap's slope is a bound: at least one TP3 level (3.3 V x 3 / 1024)
// over the 0.135 R sense, the steepest the reported current can rise.
_Static_assert(STROBE_SESSION_CAP_A_PER_LEVEL >= 3.3f * 3.0f / 1024.0f / STROBE_SENSE_V_PER_A,
               "session cap slope below the physical bound");
// The highest cal target's abort sits at the stop, so a SOLVED cal's last point
// is under it and the cap has room to exist.
_Static_assert(STROBE_TARGET_CURRENT_A * STROBE_CAL_ABORT_RATIO <= STROBE_LIVE_I_STOP_A + 1e-4f,
               "cal can solve above the stop");

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
    // The threshold is the absolute floor, not half-way up the pulse (2026-10-15).
    CHECK(m.threshold_code == 3u + STROBE_LIVE_DETECT_MIN_CODES);
    CHECK(fabsf(m.pulse[0].settled_a - 9.0f) < AMP_TOL);
    CHECK(fabsf(m.pulse[0].held_a - 9.0f) < AMP_TOL && fabsf(m.max_held_a - 9.0f) < AMP_TOL);
    CHECK(m.shape == STROBE_SHAPE_CLEAN);
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
    CHECK(m.n_pulses == 1u);
    CHECK(m.pulse[0].width_us == 60u);
    // ... but not a clean one (2026-10-15): 4 us at zero inside 9 A is a dip,
    // not noise, and it ends live mode.
    CHECK(m.verdict == STROBE_VERDICT_SHAPE && m.shape == STROBE_SHAPE_DIP);
    CHECK(m.shape_at_us == 230u && m.shape_pulse == 0u);
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
    for (int v = STROBE_VERDICT_OK; v <= STROBE_VERDICT_SHAPE; v++)
        CHECK(strcmp(strobe_verdict_text((strobe_verdict_t)v), "?") != 0);
    for (int s = STROBE_SHAPE_CLEAN; s <= STROBE_SHAPE_DIP; s++)
        CHECK(strcmp(strobe_shape_text((strobe_shape_t)s), "?") != 0);
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

// --- 2026-10-12: the BURST ring started late -------------------------------
//
// Board 1, 6c Step 2, gate 0: the freeze copied 208 samples sized by the clock
// (~440 us since the switch) while the DMA had written 122. The 86 oldest were
// the IDLE ring's leftovers -- ch5 10, ch7 2055, ch1 3038, ch2 7, repeating --
// whose mean, 1277.5 codes, was judged a 7.6 A baseline: STROBE_CLAMP latched
// with no current flowing (TP4 flat on the scope and the Saleae, Q9 cold).

// Fill buf with what `strobe wave` returned that day.
static void fill_2026_10_12(void) {
    static const uint16_t idle_rr[4] = { 10u, 2055u, 3038u, 7u };
    for (size_t i = 0; i < 86u; i++) buf[i] = idle_rr[i % 4u];
    for (size_t i = 86u; i < 208u; i++) buf[i] = 10u;
}

static bool ring_copy_avail(void) {
    // That day: the clock allowed 218 raw slots, the DMA had written 122.
    CHECK(adc_ring_copy_avail(218u, 122u, 1u) == 122u);
    // A ring that started on time: the clock is the tighter bound.
    CHECK(adc_ring_copy_avail(218u, 220u, 1u) == 218u);
    // Whole round-robin groups only.
    CHECK(adc_ring_copy_avail(1000u, 10u, 4u) == 2u);
    CHECK(adc_ring_copy_avail(1000u, 3u, 4u) == 0u);
    CHECK(adc_ring_copy_avail(1000u, 0u, 1u) == 0u);
    CHECK(adc_ring_copy_avail(1000u, 10u, 0u) == 0u);

    // Walk back exactly as adc_engine.c does -- from the channel's newest slot,
    // which can wrap to the END of the ring when the channel has not been written
    // yet -- and check every copied slot was written in this mode (index < w).
    const unsigned nchs[3] = { 1u, 2u, 4u };
    for (size_t a = 0; a < 3u; a++) {
        unsigned nch = nchs[a];
        for (size_t w = 0; w <= 64u; w++) {
            for (unsigned slot = 0; slot < nch; slot++) {
                size_t i = (w + ADC_RING_SAMPLES - 1u) & (ADC_RING_SAMPLES - 1u);
                bool found = false;
                for (unsigned k = 0; k < nch; k++) {
                    if ((i % nch) == slot) { found = true; break; }
                    i = (i + ADC_RING_SAMPLES - 1u) & (ADC_RING_SAMPLES - 1u);
                }
                CHECK(found);
                size_t avail = adc_ring_copy_avail(UINT64_MAX, w, nch);
                for (size_t k = 0; k < avail; k++) {
                    CHECK(i < w);
                    i = (i + ADC_RING_SAMPLES - nch) & (ADC_RING_SAMPLES - 1u);
                }
            }
        }
    }
    return true;
}

static bool baseline_by_index(void) {
    // A healthy firing: 205 samples ending at write index 300, the enable read at
    // index 175, so the record starts at 95 and 80 samples precede the enable.
    CHECK(strobe_live_baseline_by_index(205u, 175u, 300u) == 78u);
    // A record reaching back before the mode began (the old clock-sized copy).
    CHECK(strobe_live_baseline_by_index(208u, 2u, 122u) == 0u);
    // The fixed copy that day: 122 samples, the enable at about index 2.
    CHECK(strobe_live_baseline_by_index(122u, 2u, 122u) == 0u);
    // Inconsistent or empty. One sample more than was written must not wrap the
    // unsigned start index into a small positive baseline.
    CHECK(strobe_live_baseline_by_index(123u, 5u, 122u) == 0u);
    CHECK(strobe_live_baseline_by_index(100u, 301u, 300u) == 0u);
    CHECK(strobe_live_baseline_by_index(100u, 202u, 300u) == 0u);   // first = 200
    CHECK(strobe_live_baseline_by_index(100u, 203u, 300u) == 1u);
    return true;
}

static bool baseline_checked(void) {
    const uint64_t t_en = 10000000u;
    // Covered: clock 76, DMA 78 -> 76.
    CHECK(strobe_live_baseline_checked(205u, t_en, t_en + 250u, 175u, 300u, true) == 76u);
    // Covered, DMA smaller (the ring started just before the enable): 3 -> 3.
    CHECK(strobe_live_baseline_checked(205u, t_en, t_en + 250u, 100u, 300u, true) == 3u);
    // Not covered -> 0 whatever the splits say.
    CHECK(strobe_live_baseline_checked(205u, t_en, t_en + 250u, 175u, 300u, false) == 0u);
    // Clock 0 -> 0.
    CHECK(strobe_live_baseline_checked(205u, t_en, t_en, 175u, 300u, true) == 0u);
    return true;
}

// Coverage, rounds 2 and 3 of the independent review (2026-10-12). Each snapshot
// brackets its landing between two SysTick readings b cycles apart; span_lo is the
// cycles between the brackets' earlier readings. 300 cycles per sample (150 MHz /
// 500 ksps). A typical window, enable -> post, is 125 samples.
#define CPS 300u
// The longest live record, in cycles, fits SysTick's 24 bits with room to spare.
_Static_assert((uint64_t)STROBE_LIVE_WAVE_MAX_SAMPLES * CPS < 0x01000000u,
               "a live window overflows SysTick");
// The widest acceptance window is narrower than one sample period, so a missing
// or extra sample can never fall inside it...
_Static_assert(2u * STROBE_LIVE_SNAP_BRACKET_MAX + 2u * STROBE_LIVE_DMA_SPREAD_CYCLES < CPS,
               "coverage window can hide a sample");
// ...and it is never wider than round 2's +-60 cycles.
_Static_assert(STROBE_LIVE_SNAP_BRACKET_MAX + STROBE_LIVE_DMA_SPREAD_CYCLES <= 60u,
               "coverage window widened");

// What the two snapshots would read when the landings are `periods` ADC periods
// plus `delta` (DMA latency difference) apart, each landing `x` cycles after its
// bracket's earlier reading (0 <= x <= b).
static uint32_t span_for(uint32_t periods, int32_t delta, uint32_t x1, uint32_t x2) {
    return (uint32_t)((int64_t)periods * CPS + delta - (int64_t)x2 + (int64_t)x1);
}

static bool coverage(void) {
    // Exact, centred.
    CHECK(strobe_live_coverage_ok(span_for(125u, 0, 10u, 10u), 20u, 20u, 125u, CPS));

    // Exhaustive over where each landing sits in its bracket, the bracket widths
    // and the DMA spread: an honest count is ALWAYS covered, and one sample
    // missing or extra NEVER is.
    static const uint32_t bs[] = { 0u, 1u, 20u, STROBE_LIVE_SNAP_BRACKET_MAX };
    static const int32_t  ds[] = { -(int32_t)STROBE_LIVE_DMA_SPREAD_CYCLES, 0,
                                   (int32_t)STROBE_LIVE_DMA_SPREAD_CYCLES };
    const uint32_t P = 125u;
    for (size_t i = 0; i < 4u; i++) {
        for (size_t j = 0; j < 4u; j++) {
            for (uint32_t x1 = 0; x1 <= bs[i]; x1++) {
                for (uint32_t x2 = 0; x2 <= bs[j]; x2++) {
                    for (size_t k = 0; k < 3u; k++) {
                        CHECK(strobe_live_coverage_ok(span_for(P, ds[k], x1, x2),
                                                      bs[i], bs[j], P, CPS));       // honest
                        CHECK(!strobe_live_coverage_ok(span_for(P + 1u, ds[k], x1, x2),
                                                       bs[i], bs[j], P, CPS));      // one missing
                        CHECK(!strobe_live_coverage_ok(span_for(P - 1u, ds[k], x1, x2),
                                                       bs[i], bs[j], P, CPS));      // one extra
                    }
                }
            }
        }
    }

    // The window's edges: brackets 40/40 -> +-(40 + 20) = +-60 about the centre.
    CHECK(strobe_live_coverage_ok(125u * CPS + 60u, 40u, 40u, 125u, CPS));
    CHECK(strobe_live_coverage_ok(125u * CPS - 60u, 40u, 40u, 125u, CPS));
    CHECK(!strobe_live_coverage_ok(125u * CPS + 61u, 40u, 40u, 125u, CPS));
    CHECK(!strobe_live_coverage_ok(125u * CPS - 61u, 40u, 40u, 125u, CPS));
    // Asymmetric brackets move the centre: b1 0, b2 40 -> centre span + 20, +-40.
    CHECK(strobe_live_coverage_ok(125u * CPS - 60u, 0u, 40u, 125u, CPS));
    CHECK(!strobe_live_coverage_ok(125u * CPS - 61u, 0u, 40u, 125u, CPS));
    CHECK(strobe_live_coverage_ok(125u * CPS + 20u, 0u, 40u, 125u, CPS));
    CHECK(!strobe_live_coverage_ok(125u * CPS + 21u, 0u, 40u, 125u, CPS));

    // A bracket one cycle too wide is refused, even when the count is honest.
    CHECK(!strobe_live_coverage_ok(125u * CPS, STROBE_LIVE_SNAP_BRACKET_MAX + 1u, 20u, 125u, CPS));
    CHECK(!strobe_live_coverage_ok(125u * CPS, 20u, STROBE_LIVE_SNAP_BRACKET_MAX + 1u, 125u, CPS));
    // A clock so slow that the window spans a sample period is refused.
    CHECK(!strobe_live_coverage_ok(125u * 80u, 20u, 20u, 125u, 80u));
    CHECK(strobe_live_coverage_ok(125u * 81u, 0u, 0u, 125u, 81u));
    // Nothing written; one instant; no exact clock ratio.
    CHECK(!strobe_live_coverage_ok(125u * CPS, 20u, 20u, 0u, CPS));
    CHECK(!strobe_live_coverage_ok(0u, 0u, 0u, 0u, CPS));
    CHECK(!strobe_live_coverage_ok(125u * CPS, 20u, 20u, 125u, 0u));
    // The round-1 +-4 hole is gone.
    CHECK(!strobe_live_coverage_ok(125u * CPS, 20u, 20u, 121u, CPS));
    // The longest live window (~30.4 ms): honest covered, one short not.
    CHECK(strobe_live_coverage_ok(span_for(15200u, 0, 5u, 5u), 10u, 10u, 15200u, CPS));
    CHECK(!strobe_live_coverage_ok(span_for(15201u, 0, 5u, 5u), 10u, 10u, 15200u, CPS));
    // SysTick counts DOWN, 24 bits.
    CHECK(adc_systick_elapsed(1000u, 400u) == 600u);
    CHECK(adc_systick_elapsed(10u, 0x00FFFFF0u) == 26u);                // across the wrap
    return true;
}

// Round 3's reproduction: one conversion missing (126 periods, 125 samples), and
// the first snapshot's timestamp 310 cycles late, the second's 10. Read as one
// timestamp each, they cancel exactly: 126 x 300 + 10 - 310 = 125 x 300. With
// bracketing, the 310-cycle delay is INSIDE the first snapshot's bracket -- it
// cannot be hidden -- and a 320-cycle bracket is refused.
static bool snapshot_latency_cancel(void) {
    // The cancellation itself, on round 2's single-timestamp arithmetic.
    const uint32_t round2_span = 126u * CPS + 10u - 310u;
    CHECK(round2_span == 125u * CPS);
    // Bracketed: the first landing sits 310 cycles before its bracket closes, so
    // that bracket is at least 310 wide -- refused.
    CHECK(!strobe_live_coverage_ok(span_for(126u, 0, 0u, 0u), 320u, 20u, 125u, CPS));
    // With narrow, honest brackets the missing sample shows as 300 cycles out.
    CHECK(!strobe_live_coverage_ok(span_for(126u, 0, 10u, 10u), 20u, 20u, 125u, CPS));
    CHECK(strobe_live_coverage_ok(span_for(126u, 0, 10u, 10u), 20u, 20u, 126u, CPS));
    return true;
}

// Round 1's reproduction: the ring passed its readiness wait, then acquisition
// STOPPED at the enable. The copy still finds 205 older samples at 10 codes; the
// clock split alone judged them "within limits, no current", which would have
// advanced the staircase on a firing nobody saw. Between the snapshots: no
// samples, 125 periods of cycles.
static bool acquisition_stopped(void) {
    strobe_live_meas_t m;
    const uint64_t t_en = 10000000u, t_stop = t_en + 250u;
    fill(205, 10);
    CHECK(strobe_live_baseline_samples(205u, t_en, t_stop) == 76u);    // the clock alone
    strobe_live_measure(buf, 205u, 76u, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_OK);                             // what that would do
    bool covered = strobe_live_coverage_ok(span_for(125u, 0, 10u, 10u), 20u, 20u, 0u, CPS);
    CHECK(!covered);
    size_t pre = strobe_live_baseline_checked(205u, t_en, t_stop, 500u, 500u, covered);
    CHECK(pre == 0u);
    strobe_live_measure(buf, 205u, pre, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_NO_DATA);
    return true;
}

// Round 2's reproductions: a 6 us pause (3 samples) passed round 1's +-4.
// (a) It spans a whole 5 us pulse: the record holds no pulse at all.
static bool short_gap_pulse(void) {
    strobe_live_meas_t m;
    const uint64_t t_en = 10000000u, t_stop = t_en + 250u;
    fill(205, 10);                                   // the pulse fell in the gap
    strobe_live_measure(buf, 205u, 76u, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_OK);
    CHECK(!m.detected);                              // "no current" -- unobserved
    bool covered = strobe_live_coverage_ok(span_for(125u, 0, 10u, 10u), 20u, 20u, 122u, CPS);
    CHECK(!covered);
    strobe_live_measure(buf, 205u,
        strobe_live_baseline_checked(205u, t_en, t_stop, 175u, 297u, covered), 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_NO_DATA);
    return true;
}

// (b) It falls inside 206 us of current: the run reads 200 us, which is not over
// STROBE_LIVE_MAX_ON_US, so a clamp violation reads "within limits".
static bool short_gap_stuck(void) {
    strobe_live_meas_t m;
    const uint64_t t_en = 10000000u, t_stop = t_en + 450u;
    fill(225, 10);
    put_pulse(80, 100, 1.0f, 10);                    // 103 samples of current, 3 lost
    strobe_live_measure(buf, 225u, 76u, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_OK);
    CHECK(m.max_width_us == STROBE_LIVE_MAX_ON_US);  // 200 us: the limit, not over it
    bool covered = strobe_live_coverage_ok(span_for(225u, 0, 10u, 10u), 20u, 20u, 222u, CPS);
    CHECK(!covered);
    strobe_live_measure(buf, 225u,
        strobe_live_baseline_checked(225u, t_en, t_stop, 175u, 397u, covered), 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_NO_DATA);
    return true;
}

static bool short_baseline(void) {
    strobe_live_meas_t m;
    fill(300, 3);
    put_pulse(150, 10, 2.0f, 3);
    strobe_live_measure(buf, 300, STROBE_LIVE_PRE_MIN_SAMPLES - 1u, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_NO_DATA);
    strobe_live_measure(buf, 300, STROBE_LIVE_PRE_MIN_SAMPLES, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_OK);
    CHECK(m.detected);
    return true;
}

// The record itself, through every path. What it did that day, then what the
// fixed code makes of the same ring.
static bool replay_2026_10_12(void) {
    strobe_live_meas_t m;
    fill_2026_10_12();

    // Then: the clock-only split put 76 leftover samples in the baseline.
    strobe_live_measure(buf, 208u, 76u, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_STUCK_ON);
    CHECK(m.baseline_code > 1270.0f && m.baseline_code < 1285.0f);

    // Now, the same 208-sample copy: the DMA count refuses it (it reaches back
    // past the mode's start) even with coverage granted, so the record is NO
    // DATA, never a verdict.
    const uint64_t t_en = 10000000u, t_stop = t_en + 241u;
    size_t pre = strobe_live_baseline_checked(208u, t_en, t_stop, 2u, 122u, true);
    CHECK(pre == 0u);
    strobe_live_measure(buf, 208u, pre, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_NO_DATA);

    // And the copy the fixed freeze makes -- only the 122 samples written.
    CHECK(adc_ring_copy_avail(218u, 122u, 1u) == 122u);
    pre = strobe_live_baseline_checked(122u, t_en, t_stop, 2u, 122u, true);
    CHECK(pre < STROBE_LIVE_PRE_MIN_SAMPLES);
    strobe_live_measure(buf + 86u, 122u, pre, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_NO_DATA);

    // With the ring waited for (100 samples before the enable): a clean,
    // judged record at ~10 codes and no current.
    for (size_t i = 0; i < 300u; i++) buf[i] = 10u;
    pre = strobe_live_baseline_checked(205u, t_en, t_en + 250u, 175u, 300u,
                                       strobe_live_coverage_ok(span_for(125u, 0, 10u, 10u), 20u, 20u, 125u, CPS));
    CHECK(pre >= STROBE_LIVE_PRE_MIN_SAMPLES);
    strobe_live_measure(buf, 205u, pre, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_OK);
    CHECK(!m.detected);
    return true;
}

// The live window design leaves enough baseline to judge, with the ring waited for.
_Static_assert(STROBE_LIVE_PRE_MIN_SAMPLES + 10u <= (STROBE_LIVE_PRE_US - 40u) / STROBE_LIVE_SAMPLE_US,
               "PRE_MIN leaves no margin in the 160 us window");
_Static_assert(STROBE_LIVE_RING_READY_SAMPLES * STROBE_LIVE_SAMPLE_US >= STROBE_LIVE_PRE_US - 40u,
               "the ring wait does not cover the baseline window");
_Static_assert(STROBE_LIVE_RING_TIMEOUT_US > STROBE_LIVE_PRE_US, "ring timeout shorter than PRE");

// --- 2026-10-13: the half-peak threshold hid current -------------------------
//
// Until 2026-10-15 a pulse was a run above max(12 codes, half the record's
// peak). Board 1, before its gate-drive rework, overshot at turn-on. At gate
// 21 % the threshold sat over most of the ~0.1 A pulse: firings printed "no
// current" or "0.240 A plateau, peak 0.317 A, on 4 us (2 samples), short". At
// 24 % a 3.3 A spike put it at 266 codes, and the 0.72 A, 20 us pulse under it
// printed "no current" -- which the staircase counted as a pass. The same rule
// would have hidden a stuck-on current under any larger spike.

// What the old rule made of a record: its threshold, and how many samples
// cleared it. Kept here only to show each replay reproduces the bench.
static float old_threshold(const strobe_live_meas_t *m) {
    float rel = 0.5f * ((float)m->peak_code - m->baseline_code);
    if (rel < (float)STROBE_LIVE_DETECT_MIN_CODES) rel = (float)STROBE_LIVE_DETECT_MIN_CODES;
    return m->baseline_code + rel;
}

static size_t count_above(const uint16_t *s, size_t n, float thr) {
    size_t c = 0;
    for (size_t i = 0; i < n; i++) if ((float)s[i] > thr) c++;
    return c;
}

// `strobe wave` of a gate-21 % firing that day (gate level 215), as printed:
// 220 samples, the first 76 the baseline; the pulse at +164 us.
static const uint16_t k_wave_g21[220] = {
    11, 11, 11, 11, 11, 11, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 10, 10,
    11, 11, 10, 11, 11, 10, 11, 11, 10, 10, 11, 10, 11, 11, 11, 11, 11, 11, 10, 11,
    11, 11, 11, 11, 11, 11, 10, 11, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
    11, 10, 11, 11, 11, 11, 11, 11, 10, 11, 11, 12, 11, 11, 11, 11, 11, 11, 11, 11,
    11, 10, 64, 38, 25, 19, 24, 29, 28, 28, 29, 28, 11, 11, 11, 11, 11, 11, 11, 11,
    11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 10, 10, 11, 11, 10, 11, 10, 11, 11,
    11, 10, 11, 11, 11, 11, 12, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
    11, 10, 11, 11, 11, 11, 11, 11, 10, 11, 11, 10, 11, 10, 11, 10, 11, 11, 11, 11,
    11, 11, 11, 11, 10, 10, 11, 11, 11, 11, 11, 10, 11, 11, 11, 10, 11, 11, 11, 11,
    11, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
    11, 11, 11, 11, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 12, 11, 11, 11, 10, 11,
};

static bool replay_2026_10_13_g21(void) {
    strobe_live_meas_t m;
    strobe_live_measure(k_wave_g21, 220u, 76u, 1u, &m);
    // Then: the old threshold, 37 codes, cleared by 2 samples -- a 4 us "short" pulse.
    CHECK((uint16_t)old_threshold(&m) == 37u);
    CHECK(count_above(k_wave_g21, 220u, old_threshold(&m)) == 2u);
    // Now: the whole 20 us pulse above the 22-code floor...
    CHECK(m.threshold_code == 22u);
    CHECK(m.detected && m.n_pulses == 1u);
    CHECK(m.pulse[0].samples == 10u && m.pulse[0].width_us == 20u && m.pulse[0].start_us == 164u);
    CHECK(fabsf(m.pulse[0].settled_a - 0.1024f) < 0.002f);   // median 28 codes
    CHECK(fabsf(m.pulse[0].amps - 0.0994f) < 0.002f);        // mean, edges dropped
    CHECK(fabsf(m.pulse[0].peak_a - 0.3173f) < 0.002f);      // 64 codes
    // ...and its 3.1x turn-on overshoot is a finding, not a pass.
    CHECK(m.shape == STROBE_SHAPE_OVERSHOOT && m.shape_pulse == 0u && m.shape_at_us == 164u);
    CHECK(fabsf(m.shape_limit_a - 0.2713f) < 0.002f);        // 17.2 x 1.25 + 24 codes
    CHECK(m.verdict == STROBE_VERDICT_SHAPE);
    return true;
}

// Gate 24 %, the same day. No `strobe wave` was kept, so this is a
// RECONSTRUCTION: the printed threshold (266 codes on a ~10.7-code baseline)
// puts the highest sample at ~523 codes (3.06 A); the scope showed 3.3 A for
// ~2.5 us, a sag to ~0.34 A, then 0.72 A; the timing is the 21 % record's.
static void fill_2026_10_13_g24(void) {
    for (size_t i = 0; i < 220u; i++) buf[i] = (uint16_t)((i % 10u) < 7u ? 11u : 10u);
    static const uint16_t pulse[10] = { 523u, 211u, 67u, 67u, 71u, 131u, 131u, 131u, 131u, 131u };
    for (size_t k = 0; k < 10u; k++) buf[82u + k] = pulse[k];
}

static bool replay_2026_10_13_g24(void) {
    strobe_live_meas_t m;
    fill_2026_10_13_g24();
    strobe_live_measure(buf, 220u, 76u, 1u, &m);
    // Then: threshold 266, one sample over it -- no run, "no current".
    CHECK((uint16_t)old_threshold(&m) == 266u);
    CHECK(count_above(buf, 220u, old_threshold(&m)) == 1u);
    // Now: the pulse is found, settled at ~0.72 A whatever the spike did to the
    // mean, and the spike ends live mode.
    CHECK(m.detected && m.n_pulses == 1u && m.pulse[0].width_us == 20u);
    CHECK(fabsf(m.pulse[0].settled_a - 0.718f) < 0.005f);
    CHECK(m.pulse[0].amps < m.pulse[0].settled_a);           // the sag pulls the mean down
    CHECK(m.shape == STROBE_SHAPE_OVERSHOOT && m.shape_at_us == 164u);
    CHECK(fabsf(m.shape_a - 3.058f) < 0.01f);
    CHECK(fabsf(m.shape_limit_a - 1.041f) < 0.01f);
    CHECK(m.verdict == STROBE_VERDICT_SHAPE);
    return true;
}

// The safety half of the finding: a current that persists below half of a
// larger spike was invisible to every stuck-on check.
static bool stuck_under_spike(void) {
    strobe_live_meas_t m;
    // A 9 A turn-on spike, then 0.5 A that never stops.
    fill(400, 3);
    put_pulse(100, 2, 9.0f, 3);
    put_pulse(102, 298, 0.5f, 3);
    strobe_live_measure(buf, 400, 80, 1u, &m);
    CHECK(count_above(buf, 400u, old_threshold(&m)) == 2u);  // all the old rule saw
    CHECK(m.on_at_end);
    CHECK(m.verdict == STROBE_VERDICT_STUCK_ON);
    // The same current stopping after 250 us in all: longer than U5 allows.
    fill(400, 3);
    put_pulse(100, 2, 9.0f, 3);
    put_pulse(102, 123, 0.5f, 3);
    strobe_live_measure(buf, 400, 80, 1u, &m);
    CHECK(!m.on_at_end && m.max_width_us == 250u);
    CHECK(m.verdict == STROBE_VERDICT_STUCK_ON);
    // Current in the baseline window, under a later 12 A spike (under I_PEAK_STOP).
    fill(400, 3);
    put_pulse(70, 4, 0.5f, 3);
    put_pulse(100, 1, 12.0f, 3);
    put_pulse(101, 9, 0.5f, 3);
    strobe_live_measure(buf, 400, 80, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_STUCK_ON);
    return true;
}

// I_STOP is checked on the level held for two samples in a row as well as the
// plateau: a pulse that creeps up as Q9 heats can cross it with its mean under.
static bool held_overcurrent(void) {
    strobe_live_meas_t m;
    fill(300, 3);
    for (size_t k = 0; k < 20u; k++) buf[100u + k] = code_for(9.5f + 0.1f * (float)k, 3);  // to 11.4 A
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(m.max_amps < STROBE_LIVE_I_STOP_A);
    CHECK(m.max_held_a > STROBE_LIVE_I_STOP_A && m.max_peak_a < STROBE_LIVE_I_PEAK_STOP_A);
    CHECK(m.shape == STROBE_SHAPE_CLEAN);                    // a 20 % creep is not a spike
    CHECK(m.verdict == STROBE_VERDICT_OVERCURRENT);
    // One sample at 12 A on 9 A is not held, so no fault -- but it is a spike.
    fill(300, 3);
    put_pulse(100, 10, 9.0f, 3);
    buf[104] = code_for(12.0f, 3);
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(m.max_held_a < STROBE_LIVE_I_STOP_A && m.max_peak_a < STROBE_LIVE_I_PEAK_STOP_A);
    CHECK(m.verdict == STROBE_VERDICT_SHAPE && m.shape == STROBE_SHAPE_OVERSHOOT);
    // Two in a row over I_STOP: held, so OVERCURRENT, which outranks SHAPE.
    buf[105] = code_for(11.0f, 3);
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_OVERCURRENT);
    // Held at either end of the run counts too -- the first pair and the last,
    // edge samples included (11 A is inside the shape bound on a 9 A plateau).
    // The held LEVEL is checked, not only the verdict: since two of three
    // (policy_two_of_three) any two in a row also fault through that rule.
    fill(300, 3);
    put_pulse(100, 10, 9.0f, 3);
    buf[108] = code_for(11.0f, 3);
    buf[109] = code_for(11.0f, 3);
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(fabsf(m.max_held_a - 11.0f) < AMP_TOL && fabsf(m.pulse[0].held_a - 11.0f) < AMP_TOL);
    CHECK(m.shape == STROBE_SHAPE_CLEAN && m.verdict == STROBE_VERDICT_OVERCURRENT);
    fill(300, 3);
    put_pulse(100, 10, 9.0f, 3);
    buf[100] = code_for(11.0f, 3);
    buf[101] = code_for(11.0f, 3);
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(fabsf(m.max_held_a - 11.0f) < AMP_TOL && fabsf(m.pulse[0].held_a - 11.0f) < AMP_TOL);
    CHECK(m.shape == STROBE_SHAPE_CLEAN && m.verdict == STROBE_VERDICT_OVERCURRENT);
    return true;
}

// The shape bounds exactly: settled x 1.25 + 24 codes above, settled x 0.75 -
// 24 codes below, both strict; the lower bound skips the two edge samples, the
// upper does not. Baseline 0 and a 400-code (2.39 A) plateau: bounds 524 and 276.
static bool shape_bounds(void) {
    strobe_live_meas_t m;
    fill(300, 0);
    for (size_t k = 100; k < 120; k++) buf[k] = 400;
    buf[105] = 524;                        // on the upper bound: clean
    buf[110] = 276;                        // on the lower bound: clean
    buf[100] = 100;                        // a slow first sample...
    buf[119] = 30;                         // ...and last: edges may be low
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(m.shape == STROBE_SHAPE_CLEAN && m.verdict == STROBE_VERDICT_OK);
    CHECK(fabsf(m.pulse[0].settled_a - strobe_code_to_amps(400.0f)) < 1e-4f);
    buf[105] = 525;                        // one code over
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_SHAPE && m.shape == STROBE_SHAPE_OVERSHOOT);
    CHECK(m.shape_at_us == 210u);
    CHECK(fabsf(m.shape_limit_a - strobe_code_to_amps(524.0f)) < 1e-4f);
    buf[105] = 524;
    buf[110] = 275;                        // one code under
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_SHAPE && m.shape == STROBE_SHAPE_DIP);
    CHECK(m.shape_at_us == 220u);
    CHECK(fabsf(m.shape_limit_a - strobe_code_to_amps(276.0f)) < 1e-4f);
    buf[110] = 276;
    // An edge sample is not exempt from the upper bound: a turn-on spike is one.
    buf[100] = 525;
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(m.shape == STROBE_SHAPE_OVERSHOOT && m.shape_at_us == 200u);
    buf[100] = 100;
    // The first violation in time is the one reported: here a dip before a spike.
    buf[103] = 200;
    buf[112] = 600;
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(m.shape == STROBE_SHAPE_DIP && m.shape_at_us == 206u);
    // The median, not the mean, sets the level: a spike over 3 of 10 samples
    // leaves the bounds where the plateau is.
    fill(300, 0);
    for (size_t k = 100; k < 110; k++) buf[k] = 400;
    buf[101] = 1200; buf[102] = 1200; buf[103] = 1200;
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(fabsf(m.pulse[0].settled_a - strobe_code_to_amps(400.0f)) < 1e-4f);
    CHECK(m.shape == STROBE_SHAPE_OVERSHOOT && m.shape_at_us == 202u);
    // Near the floor the margin keeps noise from tripping it: a 20-code (0.12 A)
    // pulse with a one-sample dip to the baseline and a sample at 44 is clean.
    fill(300, 0);
    for (size_t k = 100; k < 110; k++) buf[k] = 20;
    buf[104] = 0;
    buf[106] = 44;
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(m.n_pulses == 1u && m.shape == STROBE_SHAPE_CLEAN && m.verdict == STROBE_VERDICT_OK);
    // A short run (< 4 samples) has no interior to settle on and is not judged.
    fill(300, 0);
    buf[100] = 40; buf[101] = 900; buf[102] = 40;
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(m.n_pulses == 1u && m.pulse[0].short_run && m.verdict == STROBE_VERDICT_OK);
    return true;
}

// What board 1 read after the rework (2026-10-15): flat pulses of 0.100 A over
// 10 samples at 21 %, 0.682-0.690 A over 10 and 25 at 24 %, and the 6d clamp
// test's 67 samples creeping ~3 % as Q9 warms. Records shaped like those, with
// +-1 code of noise, must stay clean, whole and right.
static void put_ramp(size_t start, size_t samples, float a0, float a1, float base) {
    for (size_t k = 0; k < samples; k++) {
        float a = a0 + (a1 - a0) * (float)k / (float)(samples - 1u);
        int noise = (int)((k * 7u) % 3u) - 1;              // -1, 0, +1
        buf[start + k] = (uint16_t)lroundf(base + strobe_amps_to_code(a) + (float)noise);
    }
}

static bool clean_pulses_2026_10_15(void) {
    static const struct { float a0, a1; size_t samples; } cases[] = {
        { 0.100f, 0.100f, 10u }, { 0.682f, 0.684f, 10u }, { 0.689f, 0.690f, 25u },
        { 0.680f, 0.700f, 67u },
    };
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        strobe_live_meas_t m;
        for (size_t i = 0; i < 400u; i++) buf[i] = (uint16_t)(i % 3u == 0u ? 11u : 10u);
        put_ramp(82u, cases[c].samples, cases[c].a0, cases[c].a1, 10.33f);
        strobe_live_measure(buf, 400u, 76u, 1u, &m);
        CHECK(m.verdict == STROBE_VERDICT_OK && m.shape == STROBE_SHAPE_CLEAN);
        CHECK(m.n_pulses == 1u && m.pulse[0].samples == cases[c].samples);
        CHECK(m.pulse[0].start_us == 164u);
        float mid = 0.5f * (cases[c].a0 + cases[c].a1);
        CHECK(fabsf(m.pulse[0].amps - mid) < 0.01f);
        CHECK(fabsf(m.pulse[0].settled_a - mid) < 0.01f);
    }
    return true;
}

// A lone sample over the floor is still not a pulse -- but it is reported, so
// "no current" is never printed over a sample nobody explained.
static bool lone_sample_reported(void) {
    strobe_live_meas_t m;
    fill(300, 10);
    buf[150] = 60;
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(m.verdict == STROBE_VERDICT_OK && !m.detected);
    CHECK(m.peak_code == 60u && m.peak_us == 300u);
    CHECK((float)m.peak_code > m.baseline_code + (float)STROBE_LIVE_DETECT_MIN_CODES);
    CHECK(fabsf(m.max_peak_a - strobe_code_to_amps(50.0f)) < 1e-4f);
    return true;
}

// The floor does not move with the record: the same 12 codes at 0.1 A and 9 A.
static bool floor_is_absolute(void) {
    strobe_live_meas_t m;
    static const float amps[3] = { 0.1f, 0.7f, 9.0f };
    for (size_t k = 0; k < 3u; k++) {
        fill(300, 10);
        put_pulse(100, 10, amps[k], 10);
        strobe_live_measure(buf, 300, 80, 1u, &m);
        CHECK(m.threshold_code == 10u + STROBE_LIVE_DETECT_MIN_CODES);
        CHECK(m.verdict == STROBE_VERDICT_OK && m.pulse[0].samples == 10u);
    }
    return true;
}

// A small current between burst pulses -- Q10 not fully off, or a retrigger --
// is a run of its own now. Under half of a 9 A peak it was invisible.
static bool residual_between_pulses(void) {
    strobe_live_meas_t m;
    fill(600, 3);
    put_pulse(100, 10, 9.0f, 3);
    put_pulse(150, 3, 0.2f, 3);
    put_pulse(200, 10, 9.0f, 3);
    strobe_live_measure(buf, 600, 80, 2u, &m);
    CHECK(m.n_pulses == 3u);
    CHECK(m.verdict == STROBE_VERDICT_NOISY);
    return true;
}

// 2026-10-15: the clamp width was taken over STORED runs only. Sixteen pulses
// fired and a 17th conduction 300 us long -- a U5 that stopped clamping -- read
// as NOISY, with no STROBE_CLAMP.
static bool long_run_beyond_stored(void) {
    strobe_live_meas_t m;
    fill(3000, 3);
    for (size_t k = 0; k < STROBE_BURST_MAX_PULSES; k++) put_pulse(100u + k * 80u, 4u, 1.0f, 3);
    put_pulse(100u + STROBE_BURST_MAX_PULSES * 80u, 150u, 1.0f, 3);
    strobe_live_measure(buf, 3000, 80, STROBE_BURST_MAX_PULSES, &m);
    CHECK(m.n_pulses == STROBE_BURST_MAX_PULSES + 1u);
    CHECK(m.max_width_us == 300u);
    CHECK(m.verdict == STROBE_VERDICT_STUCK_ON);
    // Likewise the held level: a 17th run at 11.5 A (over I_STOP, under
    // I_PEAK_STOP) is OVERCURRENT, not merely NOISY.
    fill(3000, 3);
    for (size_t k = 0; k < STROBE_BURST_MAX_PULSES; k++) put_pulse(100u + k * 80u, 4u, 1.0f, 3);
    put_pulse(100u + STROBE_BURST_MAX_PULSES * 80u, 10u, 11.5f, 3);
    strobe_live_measure(buf, 3000, 80, STROBE_BURST_MAX_PULSES, &m);
    CHECK(m.max_held_a > STROBE_LIVE_I_STOP_A && m.max_peak_a < STROBE_LIVE_I_PEAK_STOP_A);
    CHECK(m.verdict == STROBE_VERDICT_OVERCURRENT);
    // And the plateau (independent review 2026-10-15): a 17th run alternating
    // 10.5 / 11.7 A has a mean of 11.1 A but no two samples in a row over
    // 10.8 A and none over 13 A. Stored, it was OVERCURRENT; as run 17 it read
    // as NOISY, because its mean was never computed.
    fill(3000, 3);
    for (size_t k = 0; k < STROBE_BURST_MAX_PULSES; k++) put_pulse(100u + k * 80u, 4u, 1.0f, 3);
    for (size_t k = 0; k < 10u; k++)
        buf[100u + STROBE_BURST_MAX_PULSES * 80u + k] = code_for((k & 1u) ? 11.7f : 10.5f, 3);
    strobe_live_measure(buf, 3000, 80, STROBE_BURST_MAX_PULSES, &m);
    CHECK(m.max_held_a < STROBE_LIVE_I_STOP_A && m.max_peak_a < STROBE_LIVE_I_PEAK_STOP_A);
    CHECK(m.max_amps > STROBE_LIVE_I_STOP_A);
    CHECK(m.verdict == STROBE_VERDICT_OVERCURRENT);
    // ...the same run as pulse 1, for comparison.
    fill(3000, 3);
    for (size_t k = 0; k < 10u; k++) buf[100u + k] = code_for((k & 1u) ? 11.7f : 10.5f, 3);
    strobe_live_measure(buf, 3000, 80, 1u, &m);
    CHECK(fabsf(m.max_amps - 11.1f) < 0.01f);
    CHECK(m.verdict == STROBE_VERDICT_OVERCURRENT);
    // And two of three: a 17th run alternating 9 / 12 A, which only that rule
    // catches (mean 10.5 A, held 9 A), is OVERCURRENT, not NOISY.
    fill(3000, 3);
    for (size_t k = 0; k < STROBE_BURST_MAX_PULSES; k++) put_pulse(100u + k * 80u, 4u, 1.0f, 3);
    for (size_t k = 0; k < 10u; k++)
        buf[100u + STROBE_BURST_MAX_PULSES * 80u + k] = code_for((k & 1u) ? 12.0f : 9.0f, 3);
    strobe_live_measure(buf, 3000, 80, STROBE_BURST_MAX_PULSES, &m);
    CHECK(m.max_amps < STROBE_LIVE_I_STOP_A && m.max_held_a < STROBE_LIVE_I_STOP_A);
    CHECK(m.over_2of3);
    CHECK(m.verdict == STROBE_VERDICT_OVERCURRENT);
    return true;
}

// Two of any three consecutive samples over I_STOP is OVERCURRENT (2026-10-15,
// the owner's decision on independent review's recommendation; board.h, "Stop
// current"). It is checked over the whole window, so it depends on neither the
// runs nor their storage.
static bool policy_two_of_three(void) {
    strobe_live_meas_t m;
    // Alternating 9 / 12 A: mean 10.5 A, no two in a row over 10.8 A, every
    // sample under 13 A, ripple inside the shape bounds -- it passed every
    // other rule, and this one is the only one that catches it.
    fill(300, 3);
    for (size_t k = 0; k < 10u; k++) buf[100u + k] = code_for((k & 1u) ? 12.0f : 9.0f, 3);
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(fabsf(m.max_amps - 10.5f) < 0.01f && m.max_held_a < STROBE_LIVE_I_STOP_A);
    CHECK(m.max_peak_a < STROBE_LIVE_I_PEAK_STOP_A && m.shape == STROBE_SHAPE_CLEAN);
    CHECK(m.over_2of3 && m.over_2of3_us == 202u);         // samples 101 and 103
    CHECK(m.verdict == STROBE_VERDICT_OVERCURRENT);
    // Isolated over-limit samples are not two of three: 12 A at +204 and +210 us,
    // three positions apart, on 9 A is a spike (SHAPE), not a fault ...
    fill(300, 3);
    put_pulse(100, 10, 9.0f, 3);
    buf[102] = code_for(12.0f, 3);
    buf[105] = code_for(12.0f, 3);
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(!m.over_2of3 && m.max_held_a < STROBE_LIVE_I_STOP_A);
    CHECK(m.verdict == STROBE_VERDICT_SHAPE);
    // ... two positions apart they are, and OVERCURRENT outranks the SHAPE.
    buf[105] = code_for(9.0f, 3);
    buf[104] = code_for(12.0f, 3);
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(m.over_2of3 && m.over_2of3_us == 204u && m.shape == STROBE_SHAPE_OVERSHOOT);
    CHECK(m.verdict == STROBE_VERDICT_OVERCURRENT);
    // A run's edge samples count: its first and third, and its third-to-last and
    // last, at 11 A on 9 A (inside the shape bound, never two in a row).
    fill(300, 3);
    put_pulse(100, 10, 9.0f, 3);
    buf[100] = code_for(11.0f, 3);
    buf[102] = code_for(11.0f, 3);
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(m.max_held_a < STROBE_LIVE_I_STOP_A && m.shape == STROBE_SHAPE_CLEAN);
    CHECK(m.over_2of3 && m.verdict == STROBE_VERDICT_OVERCURRENT);
    fill(300, 3);
    put_pulse(100, 10, 9.0f, 3);
    buf[107] = code_for(11.0f, 3);
    buf[109] = code_for(11.0f, 3);
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(m.max_held_a < STROBE_LIVE_I_STOP_A && m.shape == STROBE_SHAPE_CLEAN);
    CHECK(m.over_2of3 && m.over_2of3_us == 214u && m.verdict == STROBE_VERDICT_OVERCURRENT);
    // The record's own edges: its first three samples and its last three. (Both
    // are current outside the pulse too, and STUCK_ON outranks OVERCURRENT.) At
    // the start they are in the baseline window and lift it to ~53 codes, so
    // 12 A, which still reads 11.7 A above that.
    fill(300, 3);
    buf[0] = code_for(12.0f, 3);
    buf[2] = code_for(12.0f, 3);
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(m.over_2of3 && m.over_2of3_us == 0u && m.verdict == STROBE_VERDICT_STUCK_ON);
    fill(300, 3);
    buf[297] = code_for(11.0f, 3);
    buf[299] = code_for(11.0f, 3);
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(m.over_2of3 && m.over_2of3_us == 594u && m.verdict == STROBE_VERDICT_STUCK_ON);
    // The exact boundary, on a zero baseline: 10.8 A is 1809.25 codes, so 1810
    // (10.804 A) is over and 1809 (10.799 A) is not. Alternating with 1500 codes
    // (8.95 A).
    CHECK(strobe_code_to_amps(1810.0f) > STROBE_LIVE_I_STOP_A);
    CHECK(strobe_code_to_amps(1809.0f) < STROBE_LIVE_I_STOP_A);
    fill(300, 0);
    for (size_t k = 0; k < 10u; k++) buf[100u + k] = (uint16_t)((k & 1u) ? 1810u : 1500u);
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(m.over_2of3 && m.verdict == STROBE_VERDICT_OVERCURRENT);
    for (size_t k = 0; k < 10u; k++) buf[100u + k] = (uint16_t)((k & 1u) ? 1809u : 1500u);
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(!m.over_2of3 && m.verdict == STROBE_VERDICT_OK);
    return true;
}

// The dip bound only bites above ~0.19 A: below a settled level of 32 codes,
// settled x 0.75 - 24 codes is under the baseline, so even a dip to zero is
// inside it. (Independent review 2026-10-15: "a <= 4 us dip to zero now ends
// live mode" was too broad.) A 0.1 A pulse with a 2-sample dip to zero is OK; a
// 0.2 A one is a DIP.
static bool policy_low_current_dip(void) {
    strobe_live_meas_t m;
    fill(300, 3);
    put_pulse(100, 10, 0.1f, 3);
    buf[104] = 3; buf[105] = 3;
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(m.n_pulses == 1u && m.pulse[0].samples == 10u);
    CHECK(m.shape == STROBE_SHAPE_CLEAN && m.verdict == STROBE_VERDICT_OK);
    fill(300, 3);
    put_pulse(100, 10, 0.2f, 3);
    buf[104] = 3; buf[105] = 3;
    strobe_live_measure(buf, 300, 80, 1u, &m);
    CHECK(m.n_pulses == 1u && m.shape == STROBE_SHAPE_DIP && m.shape_at_us == 208u);
    CHECK(m.verdict == STROBE_VERDICT_SHAPE);
    return true;
}

// In a burst the first violation in time is kept: pulse 1 dips, pulse 2 spikes.
static bool shape_first_in_burst(void) {
    strobe_live_meas_t m;
    fill(600, 3);
    put_pulse(100, 10, 5.0f, 3);
    buf[105] = code_for(2.0f, 3);          // a dip in pulse 1, at 210 us
    put_pulse(200, 10, 5.0f, 3);
    buf[203] = code_for(8.0f, 3);          // a spike in pulse 2, at 406 us
    strobe_live_measure(buf, 600, 80, 2u, &m);
    CHECK(m.n_pulses == 2u && m.verdict == STROBE_VERDICT_SHAPE);
    CHECK(m.shape == STROBE_SHAPE_DIP && m.shape_pulse == 0u && m.shape_at_us == 210u);
    return true;
}

// The shape bound sits between the two overcurrent limits at the 9 A design
// current, and the margin is wider than the floor it protects.
_Static_assert(STROBE_LIVE_SHAPE_MARGIN_CODES > STROBE_LIVE_DETECT_MIN_CODES, "margin under the floor");
static bool shape_bound_at_design_current(void) {
    float settled = strobe_amps_to_code(STROBE_TARGET_CURRENT_A);
    float bound = strobe_code_to_amps(settled * (1.0f + STROBE_LIVE_SHAPE_TOL)
                                      + (float)STROBE_LIVE_SHAPE_MARGIN_CODES);
    CHECK(bound > STROBE_LIVE_I_STOP_A && bound < STROBE_LIVE_I_PEAK_STOP_A);
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

// --- The session cap (2026-10-19, board.h) -----------------------------------

static bool session_cap_math(void) {
    // Board 1's 2026-10-16 cal: 435 -> 8.825 A, 440 -> 9.071 A, solved 439.
    CHECK(strobe_session_cap(440u, 9.071f, 439u) == 463u);
    // Floor, not round: (10.8 - 9.02) / 0.075 = 23.7 levels -> 23; 24.1 -> 24.
    CHECK(strobe_session_cap(400u, 9.02f, 398u) == 423u);
    CHECK(strobe_session_cap(400u, 8.99f, 398u) == 424u);
    // Clamped to the ceiling; never below the solved level.
    CHECK(strobe_session_cap(700u, 2.0f, 690u) == STROBE_LIVE_GATE_CEILING);
    CHECK(strobe_session_cap(STROBE_LIVE_GATE_CEILING, 9.0f, STROBE_LIVE_GATE_CEILING)
          == STROBE_LIVE_GATE_CEILING);
    // Points that cannot anchor it give no headroom: the solved level.
    CHECK(strobe_session_cap(440u, STROBE_LIVE_I_STOP_A, 439u) == 439u);
    CHECK(strobe_session_cap(440u, 11.0f, 439u) == 439u);
    CHECK(strobe_session_cap(440u, NAN, 439u) == 439u);
    CHECK(strobe_session_cap(440u, -0.1f, 439u) == 439u);
    CHECK(strobe_session_cap(430u, 9.0f, 439u) == 439u);       // l1 below solved
    // A small reading still anchors: (10.8 - 0.05) / 0.075 = 143.3 levels.
    CHECK(strobe_session_cap(100u, 0.05f, 95u) == 100u + 143u);
    return true;
}

// The guarantee: on a plant whose current rises no faster than the bound, the
// current AT the cap stays under the stop -- for every target `strobe cal`
// accepts, including the low ones where the plant's own slope would mislead.
// Board 1's measured curve (2026-10-13..16), then 0.0716 A per level above
// 440 -- the physical maximum, steeper than board 1 really is.
static float board1_amps(uint16_t level) {
    static const struct { float l, a; } k[] = {
        { 215.0f, 0.107f }, { 246.0f, 0.702f }, { 276.0f, 1.685f }, { 307.0f, 2.915f },
        { 435.0f, 8.825f }, { 440.0f, 9.071f },
    };
    const float lv = (float)level;
    if (lv <= k[0].l) return 0.0f;
    for (size_t i = 1; i < sizeof(k) / sizeof(k[0]); i++)
        if (lv <= k[i].l)
            return k[i - 1].a + (k[i].a - k[i - 1].a) * (lv - k[i - 1].l) / (k[i].l - k[i - 1].l);
    return k[5].a + (lv - k[5].l) * (3.3f * 3.0f / 1024.0f / STROBE_SENSE_V_PER_A);
}

static bool session_cap_bounds_current(void) {
    for (float target = STROBE_CAL_MIN_A; target <= STROBE_TARGET_CURRENT_A + 1e-3f; target += 0.25f) {
        uint16_t level = 0, prev_level = 0, cap = 0, solved = 0;
        float prev_amps = 0.0f;
        bool done = false;
        for (uint32_t n = 0; n < STROBE_CAL_MAX_POINTS && !done; n++) {
            float a = board1_amps(level);
            bool det = strobe_amps_to_code(a) > (float)STROBE_LIVE_DETECT_MIN_CODES;
            if (!det) a = 0.0f;
            uint16_t nx = 0;
            strobe_cal_step_t st = strobe_cal_next(level, a, det, target, &nx);
            if (st == STROBE_CAL_SOLVED) {
                solved = strobe_cal_solve(prev_level, prev_amps, level, a, target);
                cap = strobe_session_cap(level, a, solved);
                done = true;
                break;
            }
            CHECK(st == STROBE_CAL_STEP);
            prev_level = level;
            prev_amps = a;
            level = nx;
        }
        CHECK(done);
        CHECK(cap >= solved);
        CHECK(board1_amps(cap) <= STROBE_LIVE_I_STOP_A);
        // ...and the operating point stays reachable above the solved level.
        CHECK(cap > solved || board1_amps(solved) > STROBE_LIVE_I_STOP_A - 0.1f);
    }
    return true;
}

int main(int argc, char **argv) {
    static const struct { const char *name; bool (*run)(void); } cases[] = {
#define TEST(name) {#name, name}
        TEST(session_cap_math), TEST(session_cap_bounds_current),
        TEST(code_amps), TEST(single_pulse), TEST(plateau_excludes_edges),
        TEST(nothing_detected), TEST(lone_spikes_ignored), TEST(small_current_detected),
        TEST(burst_segmentation), TEST(dips_merge), TEST(extra_conduction), TEST(short_run_kept),
        TEST(stuck_baseline), TEST(stuck_early_run), TEST(stuck_on_at_end),
        TEST(clamp_width_limit), TEST(overcurrent_plateau), TEST(overcurrent_peak),
        TEST(stuck_outranks_overcurrent), TEST(noisy), TEST(no_data), TEST(verdict_texts),
        TEST(baseline_split),
        TEST(ring_copy_avail), TEST(baseline_by_index), TEST(baseline_checked),
        TEST(coverage), TEST(snapshot_latency_cancel), TEST(acquisition_stopped), TEST(short_gap_pulse), TEST(short_gap_stuck),
        TEST(short_baseline), TEST(replay_2026_10_12),
        TEST(replay_2026_10_13_g21), TEST(replay_2026_10_13_g24), TEST(stuck_under_spike),
        TEST(held_overcurrent), TEST(shape_bounds), TEST(clean_pulses_2026_10_15),
        TEST(lone_sample_reported), TEST(floor_is_absolute), TEST(residual_between_pulses),
        TEST(long_run_beyond_stored), TEST(shape_first_in_burst),
        TEST(shape_bound_at_design_current), TEST(policy_two_of_three),
        TEST(policy_low_current_dip),
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
