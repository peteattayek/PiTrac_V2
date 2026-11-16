// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
//
// cal_math.c -- the pure arithmetic of the demod calibration: the response's
// geometry and the verdict on a finished `cal demod` sweep. No SDK, no hardware,
// nothing that waits: the host tests compile this file as it is
// (tests/cal_math_test.c). The measuring half, which drives the beam and reads
// the ADC, stays in cal.c. Split out for image 1.6 (2026-11-11). 1.6's all-phase
// level fit was removed in 1.7 with the all-phase `level` (cal.h, V16-F1).
#include "cal.h"
#include "board.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// Intrinsic harmonic content of the trapezoidal lock-in response. See the long
// derivation above CAL_H3_TOL in cal.h -- these are geometry, not calibration.
// (Moved unchanged from cal.c.)
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

// ---------------------------------------------------------------------------
// The verdict on a finished sweep. The four quality terms are the ones
// cal_demod_phase() always used, moved here unchanged with their reasoning (see
// the long comment at the call in cal.c). `too_small` is new in 1.6 and is NOT in
// `valid`: a small response is still a clean one, and `cal model` keeps judging
// its points on quality alone. Only the commit in `cal demod` refuses it.
// ---------------------------------------------------------------------------

void cal_demod_judge(cal_demod_t *r) {
    bool ok_amp  = (r->amplitude > 1.0f) && (r->peak <= ADC_FULL_SCALE);
    bool ok_h3   = (r->h3_ratio  < r->h3_expected + CAL_H3_TOL);
    bool ok_sat  = (r->sat_frac  < CAL_SAT_MAX);
    bool ok_null = (fabsf(r->quad_null) < CAL_QUAD_NULL_MAX * r->amplitude);

    r->valid     = ok_amp && ok_h3 && ok_sat && ok_null;
    r->too_small = (r->peak < CAL_PEAK_MIN_FRAC * ADC_FULL_SCALE);
}

bool cal_demod_commit_ok(const cal_demod_t *r) {
    return r->valid && !r->too_small;
}
