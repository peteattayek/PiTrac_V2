// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// strobe_live.c -- see strobe_live.h. No hardware access.

#include "strobe_live.h"
#include "board.h"

#include <math.h>
#include <string.h>

// Below STROBE_LIVE_PRE_MIN_SAMPLES of baseline the record cannot be judged.
// A run must be at least this long to count as current; a lone sample above the
// floor is ADC noise, not a pulse (the shortest pulse, 5 us, is >= 2 samples).
#define MEAS_MIN_RUN   2u
// Runs separated by at most this many below-floor samples are one pulse.
#define MEAS_MERGE_GAP 2u
// A pulse's settled level is the median of at most this many of its samples.
// Every pulse short enough to pass the clamp check fits; a longer one is
// STUCK_ON whatever its level, and its median covers its first samples only.
#define MEAS_MEDIAN_MAX 128u
_Static_assert(STROBE_LIVE_MAX_ON_US / STROBE_LIVE_SAMPLE_US <= MEAS_MEDIAN_MAX,
               "a pulse that passes the clamp check must fit the median");

// Sort space for the median. strobe_live_measure() is never re-entered, and
// this keeps 256 bytes off the caller's stack.
static uint16_t s_sorted[MEAS_MEDIAN_MAX];

float strobe_code_to_amps(float code) {
    return code * (ADC_VREF_V / ADC_FULL_SCALE) / STROBE_SENSE_V_PER_A;
}

float strobe_amps_to_code(float amps) {
    return amps * STROBE_SENSE_V_PER_A * (ADC_FULL_SCALE / ADC_VREF_V);
}

static uint16_t code_at(const uint16_t *s, size_t i) { return (uint16_t)(s[i] & 0x0FFFu); }

// The median of the codes s[a..b], over at most MEAS_MEDIAN_MAX of them from a.
static float median_code(const uint16_t *s, size_t a, size_t b) {
    size_t n = b - a + 1u;
    if (n > MEAS_MEDIAN_MAX) n = MEAS_MEDIAN_MAX;
    for (size_t i = 0; i < n; i++) {                // insertion sort
        uint16_t x = code_at(s, a + i);
        size_t j = i;
        while (j > 0u && s_sorted[j - 1u] > x) { s_sorted[j] = s_sorted[j - 1u]; j--; }
        s_sorted[j] = x;
    }
    if (n & 1u) return (float)s_sorted[n / 2u];
    return 0.5f * ((float)s_sorted[n / 2u - 1u] + (float)s_sorted[n / 2u]);
}

// Close one run [rs, re] into the record.
static void emit_run(const uint16_t *s, size_t rs, size_t re, float base, strobe_live_meas_t *m) {
    uint32_t count = (uint32_t)(re - rs + 1u);

    // Every quantity a FAULT is judged on -- the width, the plateau, and the
    // highest level held for two samples in a row -- is taken over EVERY run,
    // before the storage limit below: a verdict must not depend on whether the
    // run fits the array. Until 2026-10-15 the width and the plateau came from
    // the 16 stored runs only, so a long or over-current 17th run read as NOISY
    // instead of latching STROBE_CLAMP or STROBE_OVERCURRENT (the plateau case
    // found by independent review, 2026-10-15).
    if (count * STROBE_LIVE_SAMPLE_US > m->max_width_us)
        m->max_width_us = count * STROBE_LIVE_SAMPLE_US;
    uint16_t held = 0;
    for (size_t i = rs; i < re; i++) {
        uint16_t lo = code_at(s, i) < code_at(s, i + 1u) ? code_at(s, i) : code_at(s, i + 1u);
        if (lo > held) held = lo;
    }
    float held_a = strobe_code_to_amps((float)held - base);
    if (held_a > m->max_held_a) m->max_held_a = held_a;

    size_t a = rs, b = re;
    bool short_run = count < 4u;
    if (!short_run) { a++; b--; }                   // drop the two edge samples
    double sum = 0.0;
    for (size_t i = a; i <= b; i++) sum += code_at(s, i);
    float mean   = (float)(sum / (double)(b - a + 1u));
    float amps   = strobe_code_to_amps(mean - base);
    if (m->n_pulses == 0u || amps > m->max_amps) m->max_amps = amps;

    uint32_t idx = m->n_pulses++;
    if (idx >= STROBE_BURST_MAX_PULSES) return;     // counted, not stored: NOISY

    uint16_t peak = 0;
    for (size_t i = rs; i <= re; i++) if (code_at(s, i) > peak) peak = code_at(s, i);
    float settled = median_code(s, a, b);

    strobe_pulse_meas_t *p = &m->pulse[idx];
    p->amps      = amps;
    p->settled_a = strobe_code_to_amps(settled - base);
    p->held_a    = held_a;
    p->peak_a    = strobe_code_to_amps((float)peak - base);
    p->start_us  = (uint32_t)rs * STROBE_LIVE_SAMPLE_US;
    p->width_us  = count * STROBE_LIVE_SAMPLE_US;
    p->samples   = count;
    p->short_run = short_run;

    // Shape: the first sample in time beyond the bounds (board.h). A short run
    // has no interior to settle on, so it is not judged; only the first
    // violation in the record is kept.
    if (short_run || m->shape != STROBE_SHAPE_CLEAN) return;
    float lvl = settled - base;
    if (lvl < 0.0f) lvl = 0.0f;
    const float margin = (float)STROBE_LIVE_SHAPE_MARGIN_CODES;
    const float hi = base + lvl * (1.0f + STROBE_LIVE_SHAPE_TOL) + margin;
    const float lo = base + lvl * (1.0f - STROBE_LIVE_SHAPE_TOL) - margin;
    for (size_t i = rs; i <= re; i++) {
        float c = (float)code_at(s, i);
        strobe_shape_t why = STROBE_SHAPE_CLEAN;
        float bound = 0.0f;
        if (c > hi)                          { why = STROBE_SHAPE_OVERSHOOT; bound = hi; }
        else if (i > rs && i < re && c < lo) { why = STROBE_SHAPE_DIP;       bound = lo; }
        if (why == STROBE_SHAPE_CLEAN) continue;
        m->shape           = why;
        m->shape_pulse     = idx;
        m->shape_at_us     = (uint32_t)i * STROBE_LIVE_SAMPLE_US;
        m->shape_a         = strobe_code_to_amps(c - base);
        m->shape_settled_a = p->settled_a;
        m->shape_limit_a   = strobe_code_to_amps(bound - base);
        return;
    }
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
    if (!s || pre_n < STROBE_LIVE_PRE_MIN_SAMPLES || n < pre_n + 4u) return;

    double sum = 0.0;
    for (size_t i = 0; i < pre_n; i++) sum += code_at(s, i);
    float base = (float)(sum / (double)pre_n);
    m->baseline_code = base;

    uint16_t peak = 0;
    size_t peak_i = 0;
    for (size_t i = 0; i < n; i++) {
        if (code_at(s, i) > peak) { peak = code_at(s, i); peak_i = i; }
    }
    m->peak_code = peak;
    m->peak_us   = (uint32_t)peak_i * STROBE_LIVE_SAMPLE_US;

    // The floor is absolute: nothing in the record -- a spike least of all --
    // moves it (2026-10-13; strobe_live.h).
    float thr = base + (float)STROBE_LIVE_DETECT_MIN_CODES;
    m->threshold_code = (uint16_t)(thr > 4095.0f ? 4095.0f : thr);

    // Runs strictly above the floor, merging dips of <= MEAS_MERGE_GAP.
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
        if (k == 0 || p->amps < m->min_amps)   m->min_amps = p->amps;
        m->charge_mc += p->amps * (float)p->width_us / 1000.0f;
    }
    // The peak check is over the WHOLE window, not just the runs: a sample this
    // far above the stop current is not ADC noise, whatever it belongs to.
    m->max_peak_a = strobe_code_to_amps((float)peak - base);

    // Two of any three consecutive samples over I_STOP, also over the WHOLE
    // window, so it depends on neither runs nor storage (2026-10-15, the owner's
    // decision on independent review's recommendation). Repeated over-limit
    // peaks -- ringing, an oscillating gate loop -- passed the plateau and the
    // two-in-a-row checks whenever the mean stayed under and no two adjacent
    // samples were both over (9 / 12 A alternating). Every window [i, i + 2]
    // with i + 2 < n is checked, the record's first and last included.
    for (size_t i = 0; i + 2u < n; i++) {
        unsigned over = 0u;
        for (size_t j = i; j <= i + 2u; j++)
            if (strobe_code_to_amps((float)code_at(s, j) - base) > STROBE_LIVE_I_STOP_A) over++;
        if (over >= 2u) {
            m->over_2of3    = true;
            m->over_2of3_us = (uint32_t)i * STROBE_LIVE_SAMPLE_US;
            break;
        }
    }

    // The held-level check is now implied by two of three (two samples in a row
    // over I_STOP always lie in some window of three), so no record can tell the
    // two apart. It is kept on purpose: the review recommended adding two of
    // three while retaining every existing check.
    if (base > (float)STROBE_LIVE_BASELINE_MAX_CODES || early || m->on_at_end ||
        m->max_width_us > (uint32_t)STROBE_LIVE_MAX_ON_US) {
        m->verdict = STROBE_VERDICT_STUCK_ON;
    } else if (m->max_amps > STROBE_LIVE_I_STOP_A || m->max_held_a > STROBE_LIVE_I_STOP_A ||
               m->over_2of3 || m->max_peak_a > STROBE_LIVE_I_PEAK_STOP_A) {
        m->verdict = STROBE_VERDICT_OVERCURRENT;
    } else if (m->n_pulses > fired) {
        m->verdict = STROBE_VERDICT_NOISY;
    } else if (m->shape != STROBE_SHAPE_CLEAN) {
        m->verdict = STROBE_VERDICT_SHAPE;
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
        case STROBE_VERDICT_SHAPE:       return "PULSE NOT CLEAN -- a spike or a dip far from the pulse's settled level";
    }
    return "?";
}

const char *strobe_shape_text(strobe_shape_t s) {
    switch (s) {
        case STROBE_SHAPE_CLEAN:     return "clean";
        case STROBE_SHAPE_OVERSHOOT: return "OVERSHOOT";
        case STROBE_SHAPE_DIP:       return "DIP";
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

// The record is the newest `got` samples, ending at ring write index w_stop, so
// it starts at w_stop - got. Of those, the ones at indices below w_en were
// written before the enable; 2 more are given up for the conversion in flight
// when w_en was read and the DMA's own lag.
size_t strobe_live_baseline_by_index(size_t got, size_t w_en, size_t w_stop) {
    if (got > w_stop || w_en > w_stop) return 0u;
    size_t first = w_stop - got;
    if (w_en <= first + 2u) return 0u;
    return w_en - first - 2u;
}

// The first issue event is somewhere in [0, b1] after its bracket's earlier reading,
// the second in [span_lo, span_lo + b2]: their separation lies in
// [span_lo - b1, span_lo + b2], centre span_lo + (b2 - b1) / 2, half-width
// (b1 + b2) / 2. Widened by the DMA latency spread, it must contain
// samples x cps. Doubled throughout to stay in integers.
bool strobe_live_coverage_ok(uint32_t span_lo, uint32_t b1, uint32_t b2, size_t samples,
                             uint32_t cycles_per_sample) {
    if (cycles_per_sample == 0u || samples == 0u) return false;
    if (b1 > STROBE_LIVE_SNAP_BRACKET_MAX || b2 > STROBE_LIVE_SNAP_BRACKET_MAX) return false;
    // The whole acceptance window must be narrower than one sample period, or
    // a missing sample could still land inside it.
    uint64_t twice_half = (uint64_t)b1 + b2 + 2u * (uint64_t)STROBE_LIVE_DMA_SPREAD_CYCLES;
    if (twice_half >= cycles_per_sample) return false;
    int64_t twice_centre = 2 * (int64_t)span_lo + (int64_t)b2 - (int64_t)b1;
    int64_t twice_want   = 2 * (int64_t)samples * (int64_t)cycles_per_sample;
    int64_t d = twice_centre - twice_want;
    if (d < 0) d = -d;
    return (uint64_t)d <= twice_half;
}

size_t strobe_live_baseline_checked(size_t got, uint64_t t_en_us, uint64_t t_stop_us,
                                    size_t w_en, size_t w_stop, bool covered) {
    if (!covered) return 0u;
    size_t by_time  = strobe_live_baseline_samples(got, t_en_us, t_stop_us);
    size_t by_index = strobe_live_baseline_by_index(got, w_en, w_stop);
    return by_time < by_index ? by_time : by_index;
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

uint16_t strobe_session_cap(uint16_t l1, float a1, uint16_t solved) {
    uint32_t cap = solved;
    // Written so a NaN current anchors nothing.
    if (l1 >= solved && a1 >= 0.0f && a1 < STROBE_LIVE_I_STOP_A) {
        float n = (STROBE_LIVE_I_STOP_A - a1) / STROBE_SESSION_CAP_A_PER_LEVEL;
        uint32_t steps = (n < (float)STROBE_LIVE_GATE_CEILING)
                       ? (uint32_t)n : (uint32_t)STROBE_LIVE_GATE_CEILING;
        cap = (uint32_t)l1 + steps;
    }
    if (cap < solved) cap = solved;
    if (cap > (uint32_t)STROBE_LIVE_GATE_CEILING) cap = (uint32_t)STROBE_LIVE_GATE_CEILING;
    return (uint16_t)cap;
}
