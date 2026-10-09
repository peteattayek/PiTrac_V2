// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// strobe_live.c -- see strobe_live.h. No hardware access.

#include "strobe_live.h"
#include "board.h"

#include <math.h>
#include <string.h>

// Below this many baseline samples the record cannot be judged.
#define MEAS_MIN_PRE   8u
// A run must be at least this long to count as current; a lone sample above the
// threshold is ADC noise, not a pulse (the shortest pulse, 5 us, is >= 2 samples).
#define MEAS_MIN_RUN   2u
// Runs separated by at most this many below-threshold samples are one pulse.
#define MEAS_MERGE_GAP 2u

float strobe_code_to_amps(float code) {
    return code * (ADC_VREF_V / ADC_FULL_SCALE) / STROBE_SENSE_V_PER_A;
}

float strobe_amps_to_code(float amps) {
    return amps * STROBE_SENSE_V_PER_A * (ADC_FULL_SCALE / ADC_VREF_V);
}

static uint16_t code_at(const uint16_t *s, size_t i) { return (uint16_t)(s[i] & 0x0FFFu); }

// Close one run [rs, re] into the record.
static void emit_run(const uint16_t *s, size_t rs, size_t re, float base, strobe_live_meas_t *m) {
    uint32_t count = (uint32_t)(re - rs + 1u);
    uint32_t idx = m->n_pulses++;
    if (idx >= STROBE_BURST_MAX_PULSES) return;     // counted, not stored: NOISY

    size_t a = rs, b = re;
    bool short_run = count < 4u;
    if (!short_run) { a++; b--; }                   // drop the two edge samples
    double sum = 0.0;
    uint16_t peak = 0;
    for (size_t i = a; i <= b; i++) sum += code_at(s, i);
    for (size_t i = rs; i <= re; i++) if (code_at(s, i) > peak) peak = code_at(s, i);
    float mean = (float)(sum / (double)(b - a + 1u));

    strobe_pulse_meas_t *p = &m->pulse[idx];
    p->amps      = strobe_code_to_amps(mean - base);
    p->peak_a    = strobe_code_to_amps((float)peak - base);
    p->start_us  = (uint32_t)rs * STROBE_LIVE_SAMPLE_US;
    p->width_us  = count * STROBE_LIVE_SAMPLE_US;
    p->samples   = count;
    p->short_run = short_run;
}

void strobe_live_measure(const uint16_t *s, size_t n, size_t pre_n, uint32_t fired,
                         strobe_live_meas_t *m) {
    if (!m) return;
    memset(m, 0, sizeof(*m));
    if (fired < 1u || fired > STROBE_BURST_MAX_PULSES) fired = STROBE_BURST_MAX_PULSES;
    m->n_samples   = n;
    m->pre_samples = pre_n;
    m->fired       = fired;
    m->verdict     = STROBE_VERDICT_NO_DATA;
    if (!s || pre_n < MEAS_MIN_PRE || n < pre_n + 4u) return;

    double sum = 0.0;
    for (size_t i = 0; i < pre_n; i++) sum += code_at(s, i);
    float base = (float)(sum / (double)pre_n);
    m->baseline_code = base;

    uint16_t peak = 0;
    for (size_t i = 0; i < n; i++) if (code_at(s, i) > peak) peak = code_at(s, i);
    m->peak_code = peak;

    float excursion = (float)peak - base;
    float rel = excursion * 0.5f;
    if (rel < (float)STROBE_LIVE_DETECT_MIN_CODES) rel = (float)STROBE_LIVE_DETECT_MIN_CODES;
    float thr = base + rel;
    m->threshold_code = (uint16_t)(thr > 4095.0f ? 4095.0f : thr);

    // Runs strictly above the threshold, merging dips of <= MEAS_MERGE_GAP.
    bool have = false;
    bool early = false;        // a run that starts inside the baseline window
    size_t rs = 0, re = 0;
    for (size_t i = 0; i < n; i++) {
        if ((float)code_at(s, i) <= thr) continue;
        if (have && i - re <= MEAS_MERGE_GAP + 1u) { re = i; continue; }
        if (have && re - rs + 1u >= MEAS_MIN_RUN) {
            if (rs < pre_n) early = true;
            emit_run(s, rs, re, base, m);
        }
        rs = re = i;
        have = true;
    }
    if (have && re - rs + 1u >= MEAS_MIN_RUN) {
        if (rs < pre_n) early = true;
        emit_run(s, rs, re, base, m);
    }
    // A real run reaching the last sample, not a lone noise sample there.
    m->on_at_end = have && re == n - 1u && re - rs + 1u >= MEAS_MIN_RUN;
    m->detected  = m->n_pulses > 0u;

    uint32_t stored = m->n_pulses < STROBE_BURST_MAX_PULSES ? m->n_pulses
                                                             : STROBE_BURST_MAX_PULSES;
    for (uint32_t k = 0; k < stored; k++) {
        const strobe_pulse_meas_t *p = &m->pulse[k];
        if (k == 0 || p->amps > m->max_amps)   m->max_amps = p->amps;
        if (k == 0 || p->amps < m->min_amps)   m->min_amps = p->amps;
        if (p->width_us > m->max_width_us)     m->max_width_us = p->width_us;
        m->charge_mc += p->amps * (float)p->width_us / 1000.0f;
    }
    // The peak check is over the WHOLE window, not just the runs: a sample this
    // far above the stop current is not ADC noise, whatever it belongs to.
    m->max_peak_a = strobe_code_to_amps(excursion);

    if (base > (float)STROBE_LIVE_BASELINE_MAX_CODES || early || m->on_at_end ||
        m->max_width_us > (uint32_t)STROBE_LIVE_MAX_ON_US) {
        m->verdict = STROBE_VERDICT_STUCK_ON;
    } else if (m->max_amps > STROBE_LIVE_I_STOP_A || m->max_peak_a > STROBE_LIVE_I_PEAK_STOP_A) {
        m->verdict = STROBE_VERDICT_OVERCURRENT;
    } else if (m->n_pulses > fired) {
        m->verdict = STROBE_VERDICT_NOISY;
    } else {
        m->verdict = STROBE_VERDICT_OK;
    }
}

const char *strobe_verdict_text(strobe_verdict_t v) {
    switch (v) {
        case STROBE_VERDICT_OK:          return "within limits";
        case STROBE_VERDICT_NO_DATA:     return "NO USABLE ADC0 DATA";
        case STROBE_VERDICT_STUCK_ON:    return "CURRENT OUTSIDE THE PULSE -- before it, still on at the end, or on longer than the clamp allows";
        case STROBE_VERDICT_OVERCURRENT: return "OVERCURRENT";
        case STROBE_VERDICT_NOISY:       return "MORE CURRENT PULSES THAN WERE FIRED -- a retrigger, ringing or an oscillating gate loop";
    }
    return "?";
}

// Counted back from the stop instant: `after` samples are at or after the
// enable, +2 for the newest sample's +-1 conversion and integer flooring, and
// the baseline gives up 2 more.
size_t strobe_live_baseline_samples(size_t got, uint64_t t_en_us, uint64_t t_stop_us) {
    if (t_stop_us <= t_en_us) return 0u;
    uint64_t after = (t_stop_us - t_en_us) / STROBE_LIVE_SAMPLE_US + 2u;
    if ((uint64_t)got <= after + 2u) return 0u;
    return got - (size_t)after - 2u;
}

// ---------------------------------------------------------------------------

void strobe_budget_reset(strobe_budget_t *b) {
    if (b) memset(b, 0, sizeof(*b));
}

static uint32_t slot_of(const strobe_budget_t *b, uint32_t k) {
    // k = 0 is the oldest booking still held.
    return (b->head + STROBE_LIVE_BUDGET_SLOTS - b->count + k) % STROBE_LIVE_BUDGET_SLOTS;
}

static bool in_window(uint32_t now_ms, uint32_t t_ms) {
    return (uint32_t)(now_ms - t_ms) < (uint32_t)STROBE_LIVE_BUDGET_WINDOW_MS;
}

float strobe_budget_used_mc(const strobe_budget_t *b, uint32_t now_ms) {
    if (!b) return 0.0f;
    float used = 0.0f;
    for (uint32_t k = 0; k < b->count; k++) {
        uint32_t i = slot_of(b, k);
        if (in_window(now_ms, b->t_ms[i])) used += b->mc[i];
    }
    return used;
}

bool strobe_budget_admits(const strobe_budget_t *b, uint32_t now_ms, float mc) {
    if (!b || !(mc >= 0.0f)) return false;
    // Full, and the oldest booking still counts: there is nowhere to record this
    // one without forgetting a charge that is still in the window.
    if (b->count >= STROBE_LIVE_BUDGET_SLOTS && in_window(now_ms, b->t_ms[slot_of(b, 0)]))
        return false;
    return (double)strobe_budget_used_mc(b, now_ms) + (double)mc
        <= (double)STROBE_LIVE_BUDGET_MC + 1e-6;
}

void strobe_budget_book(strobe_budget_t *b, uint32_t now_ms, float mc) {
    if (!b || !(mc >= 0.0f)) return;
    b->t_ms[b->head] = now_ms;
    b->mc[b->head]   = mc;
    b->head = (b->head + 1u) % STROBE_LIVE_BUDGET_SLOTS;
    if (b->count < STROBE_LIVE_BUDGET_SLOTS) b->count++;
}

bool strobe_live_interval_ok(uint64_t now_us, uint64_t last_us) {
    if (last_us == 0u) return true;
    if (now_us < last_us) return false;
    return (now_us - last_us) >= (uint64_t)STROBE_LIVE_MIN_INTERVAL_MS * 1000u;
}

bool strobe_live_idle_expired(uint32_t now_ms, uint32_t touch_ms) {
    return (uint32_t)(now_ms - touch_ms) >= (uint32_t)STROBE_LIVE_IDLE_TIMEOUT_MS;
}

// ---------------------------------------------------------------------------

strobe_cal_step_t strobe_cal_next(uint16_t level, float amps, bool detected, float target_a,
                                  uint16_t *next_level) {
    if (next_level) *next_level = level;
    // Written so a NaN measurement aborts.
    if (!(amps <= target_a * STROBE_CAL_ABORT_RATIO)) return STROBE_CAL_OVERSHOOT;
    if (amps >= target_a) return STROBE_CAL_SOLVED;
    if (level >= STROBE_LIVE_GATE_CEILING) return STROBE_CAL_CEILING;
    uint32_t step = detected ? STROBE_CAL_FINE_STEP : STROBE_CAL_COARSE_STEP;
    uint32_t nx = (uint32_t)level + step;
    if (nx > STROBE_LIVE_GATE_CEILING) nx = STROBE_LIVE_GATE_CEILING;
    if (next_level) *next_level = (uint16_t)nx;
    return STROBE_CAL_STEP;
}

uint16_t strobe_cal_solve(uint16_t l0, float a0, uint16_t l1, float a1, float target_a) {
    if (l1 <= l0) return l1;
    if (!(a1 > a0)) return l1;
    double f = ((double)target_a - (double)a0) / ((double)a1 - (double)a0);
    if (!(f >= 0.0)) f = 0.0;
    if (f > 1.0) f = 1.0;
    double l = (double)l0 + f * (double)(l1 - l0);
    return (uint16_t)floor(l + 0.5);
}
