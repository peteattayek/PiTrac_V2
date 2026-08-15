// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
#include "cal.h"
#include "board.h"
#include "beam.h"
#include "detect.h"
#include "adc_engine.h"

#include "pico/stdlib.h"
#include <stdio.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// One chopped differential: mean(light on) - mean(light off).
//
// CHOP FREQUENCY. The bench doc says ~5 Hz. The HPF is tau = 0.66 s, so a
// 100 ms half-cycle droops by 1 - e^(-0.1/0.66) = 14 %. That is common-mode
// across phase steps, so it does not move the argmax -- but it costs amplitude
// and time for nothing. 20 Hz gives a 25 ms half-cycle, 3.7 % droop, and is
// still 80x above the 0.24 Hz HPF corner.
//
// Each half-cycle: settle for the first 60 %, average over the last 40 %. The
// settle covers both the HPF's response to the step and the LPF's 15.39 kHz
// group delay.
// ---------------------------------------------------------------------------
static float chopped_diff(uint32_t cycles, uint32_t chop_hz) {
    uint32_t half_ms   = 500u / (chop_hz ? chop_hz : 1u);
    uint32_t settle_ms = (half_ms * 3u) / 5u;
    if (settle_ms == 0) settle_ms = 1;
    uint32_t avg_ms    = half_ms - settle_ms;
    if (avg_ms == 0) avg_ms = 1;

    double on_sum = 0, off_sum = 0;
    uint32_t on_n = 0, off_n = 0;

    for (uint32_t c = 0; c < cycles; c++) {
        for (int phase = 0; phase < 2; phase++) {
            bool on = (phase == 0);
            beam_chop(on);
            sleep_ms(settle_ms);
            uint32_t t0 = to_ms_since_boot(get_absolute_time());
            while (to_ms_since_boot(get_absolute_time()) - t0 < avg_ms) {
                uint16_t code;
                if (adc_ring_avg(ADC_CH_DETECT, 64, &code)) {
                    if (on) { on_sum += code; on_n++; } else { off_sum += code; off_n++; }
                }
            }
        }
    }
    if (!on_n || !off_n) return 0.0f;
    return (float)(on_sum / on_n - off_sum / off_n);
}

// Standard deviation of ADC5 over `ms`, in codes.
static float adc5_sigma(uint32_t ms) {
    adc_ring_view_t v;
    if (!adc_ring_view(ADC_CH_DETECT, &v)) return -1.0f;

    // The ring holds 32.8 ms per channel, so take several snapshots spaced
    // slightly further apart than that to get independent data rather than
    // re-reading the same samples.
    uint32_t snaps = (ms + 34u) / 35u; if (!snaps) snaps = 1;
    size_t depth = adc_ring_depth();
    size_t take  = depth > 2048 ? 2048 : depth;
    if (take == 0 || snaps > 16) return -1.0f;

    // Welford's online algorithm: one pass, no buffer, and numerically stable
    // without needing the data kept around.
    //
    // The obvious alternatives are both wrong here. Sum(x^2) - n*mean^2 is what
    // detect_stats() deliberately avoids, and duplicating it here would leave two
    // functions in the same firmware disagreeing about how to compute a variance.
    // Buffering for a genuine two-pass would cost 64 KB of BSS to fix a stylistic
    // point. Welford gives the stability of two passes at the cost of one.
    double mean = 0, m2 = 0;
    uint32_t n = 0;
    for (uint32_t si = 0; si < snaps; si++) {
        if (!adc_ring_view(ADC_CH_DETECT, &v)) break;
        for (size_t k = 0; k < take; k++) {
            double x = (double)adc_ring_view_at(&v, k);
            n++;
            double d = x - mean;
            mean += d / n;
            m2   += d * (x - mean);
        }
        if (si + 1 < snaps) sleep_ms(35);
    }
    if (n < 2) return -1.0f;
    double var = m2 / (n - 1);
    return (var > 0.0) ? (float)sqrt(var) : 0.0f;
}

// ---------------------------------------------------------------------------

bool cal_demod_phase(uint32_t freq_hz, uint32_t n_points, uint32_t cycles,
                     uint32_t chop_hz, cal_demod_t *out) {
    if (!out || n_points < 8) return false;
    *out = (cal_demod_t){0};
    out->freq_hz = freq_hz;
    out->warm    = beam_duty_stable_ms() >= BEAM_WARMUP_MS;

    uint32_t top = beam_top();
    float d[CAL_PHASE_POINTS];
    if (n_points > CAL_PHASE_POINTS) n_points = CAL_PHASE_POINTS;

    beam_chop_begin(beam_duty());

    float best = -1e30f; uint32_t best_i = 0;
    for (uint32_t i = 0; i < n_points; i++) {
        int32_t ticks = (int32_t)(((uint64_t)i * (top + 1u)) / n_points);
        beam_set_phase(ticks);
        d[i] = chopped_diff(cycles, chop_hz);
        if (d[i] > best) { best = d[i]; best_i = i; }
        printf("  phase %4ld ticks -> %+8.1f codes\n", (long)ticks, (double)d[i]);
    }
    beam_chop_end();

    out->argmax_ticks = (int32_t)(((uint64_t)best_i * (top + 1u)) / n_points);

    // Fit a cosine rather than trusting the grid maximum.
    //
    // The response is A*cos(theta - theta0). Taking the argmax of a 64-point
    // sweep throws away 63 points and is hostage to one noisy sample; the
    // fundamental DFT bin uses all of them, is immune to a single outlier, and
    // gives sub-step resolution for free.
    double I = 0, Q = 0, I2 = 0, Q2 = 0;
    for (uint32_t i = 0; i < n_points; i++) {
        double a = 2.0 * M_PI * i / n_points;
        I += d[i] * cos(a);      Q += d[i] * sin(a);
        I2 += d[i] * cos(2 * a); Q2 += d[i] * sin(2 * a);
    }
    double mag1 = hypot(I, Q), mag2 = hypot(I2, Q2);
    out->amplitude = (float)(2.0 * mag1 / n_points);
    out->h2_ratio  = (mag1 > 0.0) ? (float)(mag2 / mag1) : 1.0f;

    double theta0 = atan2(Q, I);                 // radians, peak location
    if (theta0 < 0) theta0 += 2.0 * M_PI;
    out->best_ticks = (int32_t)((theta0 / (2.0 * M_PI)) * (top + 1u) + 0.5);
    out->best_ticks %= (int32_t)(top + 1u);

    // Quadrature null: the response 90 degrees away should be ~0. This is the
    // bench doc's own sanity check, and h2_ratio is the same statement made
    // quantitative -- a clean cosine is what a real lock-in response looks like.
    {
        uint32_t qi = (best_i + n_points / 4u) % n_points;
        out->quad_null = d[qi];
    }

    // A response that is not a clean cosine is not a lock-in response, and
    // committing a phase from it would be committing to an artifact.
    out->valid = (out->amplitude > 1.0f) && (out->h2_ratio < 0.25f);
    return out->valid;
}

// ---------------------------------------------------------------------------

bool cal_demod_model(uint32_t f0, uint32_t f1, uint32_t npts,
                     cal_phase_model_t *out) {
    if (!out || npts < 3 || npts > 8 || f1 <= f0) return false;
    *out = (cal_phase_model_t){0};

    double fs[8], th[8]; uint32_t n = 0;

    for (uint32_t i = 0; i < npts; i++) {
        uint32_t f = f0 + (uint32_t)(((uint64_t)(f1 - f0) * i) / (npts - 1));
        beam_configure(f, beam_duty(), 0);
        cal_demod_t r;
        printf("-- model point %lu/%lu: %lu Hz\n",
               (unsigned long)(i + 1), (unsigned long)npts, (unsigned long)f);
        if (!cal_demod_phase(f, 32, 6, CAL_CHOP_HZ_DEFAULT, &r)) {
            printf("   REJECTED (amplitude %.1f, h2 %.2f) -- not a clean cosine\n",
                   (double)r.amplitude, (double)r.h2_ratio);
            continue;
        }
        fs[n] = (double)f;
        th[n] = 2.0 * M_PI * (double)r.best_ticks / (double)(beam_top() + 1u);
        n++;
    }
    if (n < 3) { printf("model needs >=3 good points, got %lu\n", (unsigned long)n); return false; }

    // Unwrap: theta is measured mod 2*pi, so adjacent points must differ by
    // less than pi for the sequence to be reconstructible. The residual below
    // is what catches it if the spacing was too coarse.
    for (uint32_t i = 1; i < n; i++) {
        while (th[i] - th[i-1] >  M_PI) th[i] -= 2.0 * M_PI;
        while (th[i] - th[i-1] < -M_PI) th[i] += 2.0 * M_PI;
    }

    // Quadratic least squares. Three coefficients, solved with Cramer's rule --
    // no iteration, no matrix library. Captures the delay term and the leading
    // curvature of the pole term over a bounded band.
    double S[5] = {0}, T[3] = {0};
    for (uint32_t i = 0; i < n; i++) {
        double x = fs[i] * 1e-5;             // scale so x is O(1); f is ~1e5
        double p = 1;
        for (int k = 0; k < 5; k++) { S[k] += p; p *= x; }
        p = 1;
        for (int k = 0; k < 3; k++) { T[k] += th[i] * p; p *= x; }
    }
    double m[3][4] = {
        { S[0], S[1], S[2], T[0] },
        { S[1], S[2], S[3], T[1] },
        { S[2], S[3], S[4], T[2] },
    };
    // Gaussian elimination with partial pivoting.
    for (int c = 0; c < 3; c++) {
        int piv = c;
        for (int r = c + 1; r < 3; r++) if (fabs(m[r][c]) > fabs(m[piv][c])) piv = r;
        if (fabs(m[piv][c]) < 1e-12) return false;
        if (piv != c) for (int k = 0; k < 4; k++) { double t = m[c][k]; m[c][k] = m[piv][k]; m[piv][k] = t; }
        for (int r = 0; r < 3; r++) {
            if (r == c) continue;
            double fct = m[r][c] / m[c][c];
            for (int k = c; k < 4; k++) m[r][k] -= fct * m[c][k];
        }
    }
    double c0 = m[0][3] / m[0][0], c1 = m[1][3] / m[1][1], c2 = m[2][3] / m[2][2];

    // Undo the 1e-5 scaling so the coefficients are in terms of Hz.
    out->a0 = (float)c0;
    out->a1 = (float)(c1 * 1e-5);
    out->a2 = (float)(c2 * 1e-10);

    double resid = 0;
    for (uint32_t i = 0; i < n; i++) {
        double f = fs[i];
        double p = out->a0 + out->a1 * f + out->a2 * f * f;
        resid += (p - th[i]) * (p - th[i]);
    }
    // With exactly 3 points a quadratic fit is EXACT, so the residual is
    // identically zero and says nothing about fit quality. Report it as
    // unavailable rather than as a perfect score.
    out->resid_rms_rad = (n > 3) ? (float)sqrt(resid / n) : -1.0f;
    out->f_lo = (uint32_t)fs[0];
    out->f_hi = (uint32_t)fs[n-1];
    out->n_points = (uint8_t)n;

    // Pure delay would mean theta = 2*pi*f*t_d exactly: no constant term, no
    // curvature. Then phase_ticks = SYSCLK * t_d at every frequency, one number
    // for the whole band. Worth knowing, and cheap to test.
    out->pure_delay = (fabsf(out->a0) < 0.15f) &&
                      (fabsf(out->a2) * (float)fs[n-1] * (float)fs[n-1] < 0.15f);
    out->valid = true;
    return true;
}

int32_t cal_model_phase_ticks(const cal_phase_model_t *m, uint32_t f) {
    if (!m || !m->valid) return 0;
    double th = m->a0 + (double)m->a1 * f + (double)m->a2 * (double)f * f;
    // Required ticks = theta/(2*pi) of one period, and one period is TOP+1 ticks
    // at that frequency.
    //
    // This recomputes TOP+1 rather than calling beam_plan(), which owns that
    // knowledge -- acceptable only because it is exact for clkdiv 1, and clkdiv
    // is 1 for everything above ~2289 Hz. The whole scan range is far above that.
    // If this is ever asked about a sub-kHz carrier it will be wrong.
    double top1 = (double)SYSCLK_HZ / (double)f;
    double frac = th / (2.0 * M_PI);
    frac -= floor(frac);
    return (int32_t)(frac * top1 + 0.5);
}

bool cal_model_covers(const cal_phase_model_t *m, uint32_t f) {
    return m && m->valid && f >= m->f_lo && f <= m->f_hi;
}

// ---------------------------------------------------------------------------

bool cal_scan_carrier(uint32_t f0, uint32_t f1, uint32_t npts,
                      const cal_phase_model_t *model, bool force_cold,
                      cal_scan_point_t *best) {
    if (!best || npts < 2) return false;

    // A scan is minutes of continuous beam. The 35 % ceiling is a DESTRUCTION
    // limit, not an operating point -- CR-12 measured the junction at 123-133 C
    // at 30 % against a 145 C maximum. Hold the ceiling at the sustainable duty
    // for the duration so nothing inside the scan can wander above it, and put
    // it back afterwards so the ceiling means the same thing everywhere else.
    float saved_ceiling = beam_duty_ceiling();
    beam_set_duty_ceiling(BEAM_DUTY_OPERATING);
    if (beam_duty() > BEAM_DUTY_OPERATING) beam_set_duty(BEAM_DUTY_OPERATING);

    // WARM-UP GATE. From cold to plateau the LED loses 25-45 % of its optical
    // output while current moves +1.7 % and power +0.6 % -- so nothing
    // electrical reports it. A carrier picked cold is picked against a signal
    // that will be substantially smaller a few minutes into an armed session,
    // and it will look like drift rather than a calibration error.
    uint32_t warm_ms = beam_duty_stable_ms();
    bool warm = warm_ms >= BEAM_WARMUP_MS;
    if (!warm && !force_cold) {
        printf("REFUSED: beam has been warm for %lu s, needs %lu s.\n",
               (unsigned long)(warm_ms / 1000u), (unsigned long)(BEAM_WARMUP_MS / 1000u));
        printf("  Optical output falls 25-45%% from cold to plateau and NOTHING\n"
               "  electrical shows it. Wait, or 'scan carrier ... force' and accept\n"
               "  that the numbers are cold.\n");
        return false;
    }

    // ONE warm-up covers the whole scan. Frequency does not change the thermal
    // load at fixed duty, and the sub-second beam-off intervals below do not
    // cool a 70 s thermal mass. Re-warming per candidate would turn a 10 minute
    // scan into hours for no benefit.
    printf("f_hz,phase_ticks,sigma_noise,sigma_floor,signal,snr,beam_noise_ratio\n");

    *best = (cal_scan_point_t){0};
    float best_snr = -1.0f;

    for (uint32_t i = 0; i < npts; i++) {
        uint32_t f = f0 + (uint32_t)(((uint64_t)(f1 - f0) * i) / (npts - 1));

        int32_t ph = 0;
        if (cal_model_covers(model, f)) {
            ph = cal_model_phase_ticks(model, f);
        } else if (model && model->valid) {
            printf("# %lu Hz is outside the model range %lu..%lu -- phase is a\n"
                   "# GUESS and this row's SNR is not comparable. Extend the model.\n",
                   (unsigned long)f, (unsigned long)model->f_lo, (unsigned long)model->f_hi);
        }
        beam_configure(f, beam_duty(), ph);
        sleep_ms(50);

        cal_scan_point_t p = { .freq_hz = f, .phase_ticks = ph };

        beam_chop_begin(beam_duty());
        beam_chop(true);  sleep_ms(50);
        p.sigma_noise = adc5_sigma(130);
        beam_chop(false); sleep_ms(50);
        p.sigma_floor = adc5_sigma(130);
        beam_chop(true);
        p.signal = chopped_diff(6, CAL_CHOP_HZ_DEFAULT);
        beam_chop_end();

        p.snr = (p.sigma_noise > 0.0f) ? (p.signal / p.sigma_noise) : 0.0f;
        p.beam_noise_ratio = (p.sigma_floor > 0.0f)
                           ? (p.sigma_noise / p.sigma_floor) : 0.0f;

        printf("%lu,%ld,%.2f,%.2f,%.1f,%.2f,%.2f\n",
               (unsigned long)f, (long)ph, (double)p.sigma_noise,
               (double)p.sigma_floor, (double)p.signal, (double)p.snr,
               (double)p.beam_noise_ratio);

        if (p.snr > best_snr) { best_snr = p.snr; *best = p; }
    }

    printf("\nBEST by SNR: %lu Hz, SNR %.2f, phase %ld ticks%s\n",
           (unsigned long)best->freq_hz, (double)best->snr,
           (long)best->phase_ticks, warm ? "" : "   *** COLD, NOT TRUSTWORTHY ***");
    beam_set_duty_ceiling(saved_ceiling);
    printf("Also read beam_noise_ratio: it is sigma_noise/sigma_floor, i.e. how much\n"
           "noise the BEAM adds over ambient. That is the boost-harmonic folding this\n"
           "scan exists to dodge. A candidate with high SNR and a ratio near 1.0 is\n"
           "genuinely clean; high SNR with a large ratio just got lucky on amplitude.\n");
    return true;
}

// ---------------------------------------------------------------------------
// R98 selection.
//
//   G     = 1 + R101 / (R100 || R98)      R101 = 27k, R100 = 2k
//   R_gnd = R101 / (G - 1)
//   R98   = R100 * R_gnd / (R100 - R_gnd)     valid for R_gnd < R100, i.e. G > 14.5
// ---------------------------------------------------------------------------

#define U12B_RF     27000.0f
#define U12B_RG     2000.0f

static float e24_nearest(float r) {
    static const float e24[24] = {
        1.0f,1.1f,1.2f,1.3f,1.5f,1.6f,1.8f,2.0f,2.2f,2.4f,2.7f,3.0f,
        3.3f,3.6f,3.9f,4.3f,4.7f,5.1f,5.6f,6.2f,6.8f,7.5f,8.2f,9.1f };
    if (r <= 0.0f) return 0.0f;
    float dec = powf(10.0f, floorf(log10f(r)));
    float m = r / dec, bestm = e24[0], bd = 1e30f;
    for (int i = 0; i < 24; i++) {
        float d = fabsf(m - e24[i]);
        if (d < bd) { bd = d; bestm = e24[i]; }
    }
    return bestm * dec;
}

void cal_gain_recommend(float peak_codes, float target_fraction, cal_gain_t *out) {
    if (!out) return;
    float g_now = 1.0f + U12B_RF / U12B_RG;          // 14.5 as built

    // Usable full scale is the ADC's, not the diode's: ADC5 saturates at code
    // 4095 before D14 conducts at ~3.6 V.
    float target_codes = 4095.0f * target_fraction;
    float g_target = (peak_codes > 1.0f) ? (g_now * target_codes / peak_codes) : g_now;
    if (g_target < g_now) g_target = g_now;          // R98 can only RAISE the gain

    out->gain_now    = g_now;
    out->gain_target = g_target;

    float r_gnd = U12B_RF / (g_target - 1.0f);
    if (r_gnd >= U12B_RG - 1.0f) {
        out->r98_ideal = out->r98_e24 = 0.0f;        // leave it unpopulated
    } else {
        out->r98_ideal = U12B_RG * r_gnd / (U12B_RG - r_gnd);
        out->r98_e24   = e24_nearest(out->r98_ideal);
    }

    // dTP9 at which ADC5 saturates, in millivolts.
    out->clip_mv_now = 3300.0f / g_now;
    float g_eff = (out->r98_e24 > 0.0f)
                ? (1.0f + U12B_RF / (U12B_RG * out->r98_e24 / (U12B_RG + out->r98_e24)))
                : g_now;
    out->gain_target = g_eff;
    out->clip_mv_new = 3300.0f / g_eff;
}
