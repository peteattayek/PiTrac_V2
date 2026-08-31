// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
#include "cal.h"
#include "board.h"
#include "beam.h"
#include "detect.h"
#include "adc_engine.h"
#include "service.h"

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
            pitrac_yield_ms(settle_ms);
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
        if (si + 1 < snaps) pitrac_yield_ms(35);
    }
    if (n < 2) return -1.0f;
    double var = m2 / (n - 1);
    return (var > 0.0) ? (float)sqrt(var) : 0.0f;
}

// ---------------------------------------------------------------------------
// Intrinsic harmonic content of the trapezoidal lock-in response. See the long
// derivation above CAL_H3_TOL in cal.h -- these are geometry, not calibration.
// ---------------------------------------------------------------------------

static float sinc_pi(float x) {
    // sin(pi x)/(pi x), with the removable singularity at 0 handled.
    if (fabsf(x) < 1e-6f) return 1.0f;
    return sinf((float)M_PI * x) / ((float)M_PI * x);
}

// Duty is clamped away from the degenerate ends: 0 makes the pulse vanish and
// 0.5 makes it fill a demod half-cycle. BEAM_DUTY_CEILING keeps the real value
// well inside this, so the clamp only guards against a caller passing junk.
static float clamp_duty(float d) {
    if (d < 0.01f) return 0.01f;
    if (d > 0.45f) return 0.45f;
    return d;
}

float cal_h3_expected(float duty) {
    float d = clamp_duty(duty);
    return fabsf(sinc_pi(3.0f * d) / (3.0f * sinc_pi(d)));
}

float cal_form_factor(float duty) {
    float d = clamp_duty(duty);
    return 4.0f * sinc_pi(d) / (float)M_PI;
}

float cal_level_peak(uint32_t cycles, uint32_t chop_hz) {
    beam_chop_begin(beam_duty());
    float d = chopped_diff(cycles ? cycles : 1u,
                           chop_hz ? chop_hz : CAL_CHOP_HZ_DEFAULT);
    beam_chop_end();

    // Report the PEAK, not the fitted fundamental, so the number means the same
    // thing as the `peak` line in `cal demod` and can be compared to the same
    // 50-70 % target. Sign is irrelevant here -- it only says which side of the
    // correlation zero the phase sits on.
    float ff = cal_form_factor(beam_duty());
    return (ff > 0.01f) ? fabsf(d) / ff : fabsf(d);
}

// Fold a tick offset into (-period/2, +period/2]. The demod offset is only ever
// defined modulo one carrier period, and the small signed value is the one that
// means something physically -- a delay of 1348 ticks in a 1440-tick period is
// really an offset of -92.
static double wrap_ticks_centered(double t, double period) {
    double x = fmod(t, period);
    if (x < 0.0) x += period;
    if (x > period * 0.5) x -= period;
    return x;
}

// Strip the geometric (duty/2)*(TOP+1) term off a phase-tick count, leaving the
// chain delay. See the long note in cal.h above cal_phase_model_t.
static float delay_ns_from_ticks(double phase_ticks, double period, float duty) {
    double t = phase_ticks + (double)duty * period * 0.5;
    return (float)(wrap_ticks_centered(t, period) * (1.0e9 / (double)SYSCLK_HZ));
}

// ---------------------------------------------------------------------------

bool cal_demod_phase(uint32_t freq_hz, uint32_t n_points, uint32_t cycles,
                     uint32_t chop_hz, cal_demod_t *out) {
    if (!out || n_points < 8) return false;
    *out = (cal_demod_t){0};
    out->freq_hz = freq_hz;
    out->warm    = beam_duty_stable_ms() >= BEAM_WARMUP_MS;

    // Capture the duty BEFORE chopping starts. beam_chop() drives the compare
    // level directly and leaves s_duty alone, so beam_duty() stays the on-duty
    // for the whole sweep -- but reading it once here keeps the record honest
    // if that ever changes.
    out->duty        = beam_duty();
    out->h3_expected = cal_h3_expected(out->duty);
    out->form_factor = cal_form_factor(out->duty);

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

        // Abort at the point boundary, not mid-chop: a half-finished chop would
        // leave the beam at duty 0 and the caller none the wiser.
        if (pitrac_abort_pending()) {
            beam_chop_end();
            printf("\nABORTED after %lu of %lu points -- nothing committed.\n",
                   (unsigned long)(i + 1), (unsigned long)n_points);
            return false;
        }
    }
    beam_chop_end();

    out->argmax_ticks = (int32_t)(((uint64_t)best_i * (top + 1u)) / n_points);

    // Fit a cosine rather than trusting the grid maximum.
    //
    // The response is A*cos(theta - theta0). Taking the argmax of a 64-point
    // sweep throws away 63 points and is hostage to one noisy sample; the
    // fundamental DFT bin uses all of them, is immune to a single outlier, and
    // gives sub-step resolution for free.
    double I = 0, Q = 0, I2 = 0, Q2 = 0, I3 = 0, Q3 = 0;
    for (uint32_t i = 0; i < n_points; i++) {
        double a = 2.0 * M_PI * i / n_points;
        I += d[i] * cos(a);      Q += d[i] * sin(a);
        I2 += d[i] * cos(2 * a); Q2 += d[i] * sin(2 * a);
        I3 += d[i] * cos(3 * a); Q3 += d[i] * sin(3 * a);
    }
    double mag1 = hypot(I, Q), mag2 = hypot(I2, Q2), mag3 = hypot(I3, Q3);
    out->amplitude = (float)(2.0 * mag1 / n_points);
    out->h2_ratio  = (mag1 > 0.0) ? (float)(mag2 / mag1) : 1.0f;
    out->h3_ratio  = (mag1 > 0.0) ? (float)(mag3 / mag1) : 1.0f;

    // The fit returns the FUNDAMENTAL; the rail is hit by the PEAK. For a
    // trapezoid those differ by 4*sinc(D)/pi -- 1.146 at 25 % duty -- so
    // comparing the fundamental against full scale rejects clean signals.
    out->peak = (out->form_factor > 0.01f)
              ? out->amplitude / out->form_factor : out->amplitude;

    // How much of the sweep is pinned against the ADC rail. This is the direct
    // observation; the harmonic ratios are the same statement made indirectly.
    // Kept separate because it needs no fitting and cannot be argued with.
    {
        uint32_t nsat = 0;
        for (uint32_t i = 0; i < n_points; i++)
            if (fabsf(d[i]) > 0.97f * ADC_FULL_SCALE) nsat++;
        out->sat_frac = (float)nsat / (float)n_points;
    }

    double theta0 = atan2(Q, I);                 // radians, peak location
    if (theta0 < 0) theta0 += 2.0 * M_PI;
    out->best_ticks = (int32_t)((theta0 / (2.0 * M_PI)) * (top + 1u) + 0.5);
    out->best_ticks %= (int32_t)(top + 1u);

    // MUST come after best_ticks is assigned, not next to `peak` where it reads
    // naturally. Placed there on 2026-08-24 it ran while best_ticks was still 0,
    // so the reported delay collapsed to the geometric (duty/2)*(TOP+1) term
    // alone -- a constant 1200 ns at 25 % duty whatever the phase was. It looked
    // like a plausible number, which is why the bench dump caught it and the
    // build did not.
    out->chain_delay_ns = delay_ns_from_ticks((double)out->best_ticks,
                                              (double)(top + 1u), out->duty);

    // Quadrature null: the response 90 degrees away should be ~0.
    //
    // Anchor it to the FITTED peak, not to best_i. On a clipped sweep the grid
    // argmax is degenerate -- dozens of points share the maximum and best_i is
    // whichever the loop happened to see first -- so a null taken 90 degrees
    // from it is measured from an arbitrary place. That is what produced the
    // 2026-08-21 reading of -4086 against an expected ~0.
    {
        int32_t  qt = (out->best_ticks + (int32_t)((top + 1u) / 4u)) % (int32_t)(top + 1u);
        uint32_t qi = ((uint32_t)qt * n_points) / (top + 1u);
        if (qi >= n_points) qi = n_points - 1u;
        out->quad_null = d[qi];
    }

    // A response that is not a clean cosine is not a lock-in response, and
    // committing a phase from it would be committing to an artifact.
    //
    // 2026-08-21: this test used to be `amplitude > 1 && h2_ratio < 0.25`, and
    // it accepted a sweep that was 84 % hard against the ADC rail. Three of the
    // four terms below would each have caught that on their own:
    //
    //   sat_frac   0.84  vs 0.10 -- the direct observation
    //   h3_ratio   0.306 vs 0.15 -- symmetric clipping, which h2 cannot see
    //   amplitude  5128  vs 4095 -- a fitted amplitude ABOVE full scale is
    //                               impossible for a real signal. It happens
    //                               because a square wave of height A has a
    //                               fundamental of 4A/pi = 1.27A.
    //   quad_null  -4086 vs ~0   -- computed and PRINTED, but never actually
    //                               enforced. It failed loudly and was ignored.
    //
    // The last one is the one worth remembering: the check existed, ran, and
    // reported failure, and the code still committed the phase.
    // h2 is NOT in this test, and cannot be. A 50 % square demodulator can only
    // produce ODD harmonics of the phase sweep, so h2 is structurally zero for
    // every optical input -- whatever it reads is a property of the measurement,
    // not of the signal. 2026-08-24 measured it flat at 0.26 across a 2x change
    // in amplitude, and at 0.14 for the same signal swept more slowly. It stays
    // computed and printed as a sweep-health diagnostic; it must not gate.
    bool ok_amp   = (out->amplitude > 1.0f) && (out->peak <= ADC_FULL_SCALE);
    bool ok_h3    = (out->h3_ratio  < out->h3_expected + CAL_H3_TOL);
    bool ok_sat   = (out->sat_frac  < CAL_SAT_MAX);
    bool ok_null  = (fabsf(out->quad_null) < CAL_QUAD_NULL_MAX * out->amplitude);

    out->valid = ok_amp && ok_h3 && ok_sat && ok_null;
    return out->valid;
}

// ---------------------------------------------------------------------------

bool cal_demod_model(uint32_t f0, uint32_t f1, uint32_t npts,
                     cal_phase_model_t *out) {
    if (!out || npts < 3 || npts > 8 || f1 <= f0) return false;
    *out = (cal_phase_model_t){0};

    double fs[8], th[8]; uint32_t n = 0;

    // The sweep walks the carrier across the whole band and MUST put it back.
    // Without this the beam is left at f1 -- 200 kHz by default -- and a
    // `cal demod` typed afterwards silently calibrates the wrong frequency and
    // commits the answer. That happened on 2026-08-24 and cost a bench session.
    // The giveaway is in the sweep listing: phases step by (TOP+1)/64, so a
    // period of 750 ticks instead of 1440 means 200 kHz, not 104 kHz.
    uint32_t f_restore  = beam_actual_freq_hz();
    int32_t  ph_restore = beam_phase_ticks();

    // Optical stability across the run. See CAL_MODEL_AMP_SPREAD_MAX in cal.h:
    // the per-point checks cannot catch a scene that changes BETWEEN points,
    // because each point is individually valid.
    float pk_min = 1e30f, pk_max = 0.0f;

    for (uint32_t i = 0; i < npts; i++) {
        uint32_t f = f0 + (uint32_t)(((uint64_t)(f1 - f0) * i) / (npts - 1));
        beam_configure(f, beam_duty(), 0);
        cal_demod_t r;
        printf("-- model point %lu/%lu: %lu Hz\n",
               (unsigned long)(i + 1), (unsigned long)npts, (unsigned long)f);
        // Same points and cycles as `cal demod`, not the 32/6 this used to use.
        // 32 points at 6 cycles is 300 ms per point against a 0.66 s HPF, so
        // every point is still relaxing from the one before it. Measured
        // 2026-08-24 on one board in one session: h2 ran 0.26-0.38 at
        // 300 ms/point and 0.12-0.15 at 400 ms/point for the same signal, with
        // no dependence on amplitude across a 2x change. Costs ~128 s for five
        // points instead of ~48 s.
        if (!cal_demod_phase(f, CAL_PHASE_POINTS, CAL_CHOP_CYCLES,
                             CAL_CHOP_HZ_DEFAULT, &r)) {
            printf("   REJECTED -- peak %.0f, sat %.0f %%, h3 %.3f vs %.3f expected, null %.1f\n",
                   (double)r.peak, (double)(r.sat_frac * 100.0f),
                   (double)r.h3_ratio, (double)r.h3_expected, (double)r.quad_null);
            if (r.sat_frac >= CAL_SAT_MAX ||
                r.h3_ratio >= r.h3_expected + CAL_H3_TOL)
                printf("      clipping -- reduce the light before re-running the model\n");
            continue;
        }
        if (r.peak < pk_min) pk_min = r.peak;
        if (r.peak > pk_max) pk_max = r.peak;

        if (pitrac_abort_pending()) {
            printf("ABORTED after %lu of %lu model points.\n",
                   (unsigned long)(i + 1), (unsigned long)npts);
            beam_configure(f_restore, beam_duty(), ph_restore);
            return false;
        }

        fs[n] = (double)f;
        th[n] = 2.0 * M_PI * (double)r.best_ticks / (double)(beam_top() + 1u);
        n++;
    }
    // Put the carrier back before any early return below can skip it.
    beam_configure(f_restore, beam_duty(), ph_restore);

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

    // Judge "pure delay" on the CHAIN DELAY, not on the coefficients.
    //
    // The old test was `|a0| < 0.15 && |a2|*f^2 < 0.15`, and a0 is a wrapped
    // constant with no physical meaning -- it read -45.7 deg at DC on a healthy
    // 2026-08-24 fit and printed "NOT a pure delay" for a chain whose delay
    // varies by 3.5 % across the whole band. The curvature half of that test
    // passed. Only the meaningless half fired.
    out->amp_spread = (pk_min > 1.0f) ? (pk_max / pk_min - 1.0f) : 0.0f;
    out->duty  = beam_duty();
    out->valid = true;               // set first: the helpers below check it

    out->delay_lo_ns  = cal_model_delay_ns(out, out->f_lo);
    out->delay_hi_ns  = cal_model_delay_ns(out, out->f_hi);
    out->span_err_deg = fabsf(out->delay_hi_ns - out->delay_lo_ns) * 0.5f
                      * 1.0e-9f * (float)out->f_hi * 360.0f;
    out->pure_delay   = (out->span_err_deg < CAL_PURE_DELAY_MAX_DEG);
    return true;
}

float cal_model_delay_ns(const cal_phase_model_t *m, uint32_t f) {
    if (!m || !m->valid || f == 0) return 0.0f;
    double period = (double)SYSCLK_HZ / (double)f;
    double th     = (double)m->a0 + (double)m->a1 * f + (double)m->a2 * (double)f * f;
    return delay_ns_from_ticks(th / (2.0 * M_PI) * period, period, m->duty);
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

    // Same trap `cal model` had until 2026-08-24: this loop walks the carrier
    // across the band and must put it back, or the beam is simply left at f1 and
    // everything typed afterwards silently runs at the wrong frequency. Note the
    // restore has to happen on the REFUSED path below too, which is why it is
    // captured before the warm-up gate rather than after it.
    uint32_t f_restore  = beam_actual_freq_hz();
    int32_t  ph_restore = beam_phase_ticks();

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
        beam_set_duty_ceiling(saved_ceiling);
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
        pitrac_yield_ms(50);

        cal_scan_point_t p = { .freq_hz = f, .phase_ticks = ph };

        beam_chop_begin(beam_duty());
        beam_chop(true);  pitrac_yield_ms(50);
        p.sigma_noise = adc5_sigma(130);
        beam_chop(false); pitrac_yield_ms(50);
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

        if (pitrac_abort_pending()) {
            printf("# ABORTED after %lu of %lu candidates.\n",
                   (unsigned long)(i + 1), (unsigned long)npts);
            beam_configure(f_restore, beam_duty(), ph_restore);
            beam_set_duty_ceiling(saved_ceiling);
            return false;
        }
    }

    printf("\nBEST by SNR: %lu Hz, SNR %.2f, phase %ld ticks%s\n",
           (unsigned long)best->freq_hz, (double)best->snr,
           (long)best->phase_ticks, warm ? "" : "   *** COLD, NOT TRUSTWORTHY ***");

    // Put the carrier and phase back. The winner is a RECOMMENDATION -- adopting
    // it means editing board.h and re-running cal demod, not leaving the beam
    // wherever the last scan point happened to land.
    beam_configure(f_restore, beam_duty(), ph_restore);
    beam_set_duty_ceiling(saved_ceiling);
    printf("Carrier restored to %lu Hz, phase %ld ticks. The winner above is a\n"
           "RECOMMENDATION -- to adopt it, set it in board.h and re-run 'cal demod'.\n",
           (unsigned long)f_restore, (long)ph_restore);
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
