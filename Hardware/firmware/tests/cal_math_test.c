// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
//
// Host tests for cal_math.c (image 1.6, 2026-11-11; 1.7 removed the all-phase
// level and its three tests with it): the demod response's geometry, and the
// verdict on a `cal demod` sweep with its 30 % floor. No SDK, no hardware.
//
// The geometry's reference is a DIRECT simulation of the lock-in -- a duty-D
// optical pulse correlated with the 50 % +-1 demod square, integrated
// numerically -- not the Fourier series cal.h derives, so the test does not
// share the formula it is checking.
#include "cal.h"
#include "board.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *test_name;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s failed: %s\n", __FILE__, __LINE__, test_name, #condition); \
        return false; \
    } \
} while (0)

static bool close_to(double a, double b, double tol) { return fabs(a - b) <= tol; }

// The lock-in output at demod offset `phi` (fraction of a period): the pulse is
// V for t in [0, D), the reference +1 for (t - phi) mod 1 in [0, 0.5), else -1.
// Its true peak is D * V (the pulse wholly inside one half of the reference).
static double lockin(double phi, double D, double V) {
    const int M = 4000;
    double s = 0.0;
    for (int k = 0; k < M; k++) {
        double t = ((double)k + 0.5) / (double)M;
        if (t >= D) continue;                      // no light outside the pulse
        double x = fmod(t - phi, 1.0);
        if (x < 0.0) x += 1.0;
        s += (x < 0.5) ? V : -V;
    }
    return s / (double)M;
}

// A record shaped like Board 4's `cal demod` output (2026-11-11, run 4).
static cal_demod_t good_run(void) {
    cal_demod_t r;
    memset(&r, 0, sizeof(r));
    r.duty        = 0.25f;
    r.form_factor = cal_form_factor(0.25f);
    r.h3_expected = cal_h3_expected(0.25f);
    r.amplitude   = 3232.0f;
    r.peak        = 2819.0f;
    r.h3_ratio    = 0.104f;
    r.sat_frac    = 0.0f;
    r.quad_null   = 0.2f;
    return r;
}

// ---------------------------------------------------------------------------

// The constants cal.h derives, moved unchanged from cal.c.
static bool geometry(void) {
    CHECK(close_to(cal_form_factor(0.25f), 1.1463, 0.001));   // 4 sinc(0.25)/pi
    CHECK(close_to(cal_h3_expected(0.25f), 1.0 / 9.0, 0.001)); // exactly 1/9 at 25 %
    CHECK(close_to(cal_form_factor(0.02f), 1.2724, 0.001));
    // And they describe the simulated lock-in: its fundamental over its peak.
    const int P = 360;
    double I = 0.0, Q = 0.0, pk = 0.0;
    for (int k = 0; k < P; k++) {
        double v = lockin((double)k / P, 0.25, 1.0);
        I += v * cos(2.0 * M_PI * k / P);
        Q += v * sin(2.0 * M_PI * k / P);
        if (fabs(v) > pk) pk = fabs(v);
    }
    CHECK(close_to(pk, 0.25, 0.001));                          // peak = D * V
    CHECK(close_to(2.0 * hypot(I, Q) / P / pk, cal_form_factor(0.25f), 0.005));
    return true;
}

// Board 4's four sweeps of 2026-11-11, as printed. Run 2 (nothing within 3 m,
// 9 % of full scale) was clean and was committed by 1.5; from 1.6 it is not.
static bool judge_board4_runs(void) {
    cal_demod_t r = good_run();                            // run 4: 69 %
    cal_demod_judge(&r);
    CHECK(r.valid && !r.too_small && cal_demod_commit_ok(&r));

    r = good_run();                                        // run 3: 78 %
    r.amplitude = 3677.0f; r.peak = 3208.0f; r.h3_ratio = 0.102f; r.quad_null = 0.1f;
    cal_demod_judge(&r);
    CHECK(r.valid && !r.too_small && cal_demod_commit_ok(&r));

    r = good_run();                                        // run 2: 9 %
    r.amplitude = 440.0f; r.peak = 384.0f; r.h3_ratio = 0.118f; r.quad_null = 0.0f;
    cal_demod_judge(&r);
    CHECK(r.valid);                                        // clean...
    CHECK(r.too_small);                                    // ...but under the floor
    CHECK(!cal_demod_commit_ok(&r));

    r = good_run();                                        // run 1: clipped, 105 %
    r.amplitude = 4936.0f; r.peak = 4306.0f; r.h3_ratio = 0.215f;
    r.sat_frac = 0.69f; r.quad_null = -0.4f;
    cal_demod_judge(&r);
    CHECK(!r.valid && !r.too_small && !cal_demod_commit_ok(&r));
    return true;
}

// The floor is "at least 30 %": exactly 30 % commits, anything below does not.
static bool judge_floor_boundary(void) {
    const float floor_codes = CAL_PEAK_MIN_FRAC * ADC_FULL_SCALE;
    cal_demod_t r = good_run();
    r.peak = floor_codes;
    r.amplitude = floor_codes * r.form_factor;
    cal_demod_judge(&r);
    CHECK(r.valid && !r.too_small && cal_demod_commit_ok(&r));

    r = good_run();
    r.peak = nextafterf(floor_codes, 0.0f);
    r.amplitude = r.peak * r.form_factor;
    cal_demod_judge(&r);
    CHECK(r.valid && r.too_small && !cal_demod_commit_ok(&r));
    return true;
}

// Each of the four quality terms, moved unchanged from cal.c, still fails a
// sweep on its own -- and none of them is mistaken for the floor.
static bool judge_quality_terms(void) {
    cal_demod_t r;

    r = good_run(); r.sat_frac = CAL_SAT_MAX;                       // at the limit fails
    cal_demod_judge(&r);
    CHECK(!r.valid && !r.too_small && !cal_demod_commit_ok(&r));

    r = good_run(); r.h3_ratio = r.h3_expected + CAL_H3_TOL;        // at the bar fails
    cal_demod_judge(&r);
    CHECK(!r.valid && !cal_demod_commit_ok(&r));

    r = good_run(); r.quad_null = -CAL_QUAD_NULL_MAX * r.amplitude; // at the bar fails
    cal_demod_judge(&r);
    CHECK(!r.valid && !cal_demod_commit_ok(&r));

    r = good_run(); r.peak = nextafterf(ADC_FULL_SCALE, 1e9f);      // above full scale
    cal_demod_judge(&r);
    CHECK(!r.valid && !r.too_small && !cal_demod_commit_ok(&r));

    r = good_run(); r.peak = ADC_FULL_SCALE;                        // full scale itself is legal
    cal_demod_judge(&r);
    CHECK(r.valid && cal_demod_commit_ok(&r));

    r = good_run(); r.amplitude = 1.0f;                             // no response at all
    cal_demod_judge(&r);
    CHECK(!r.valid && !cal_demod_commit_ok(&r));
    return true;
}

int main(int argc, char **argv) {
    static const struct { const char *name; bool (*run)(void); } cases[] = {
#define TEST(name) {#name, name}
        TEST(geometry), TEST(judge_board4_runs), TEST(judge_floor_boundary),
        TEST(judge_quality_terms)
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
