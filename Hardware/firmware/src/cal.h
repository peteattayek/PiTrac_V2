// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// cal.h -- demod phase calibration and the carrier search. BENCH_P3_DETECT 3.4/3.6.
//
// WHY THE .md's OWN PROCEDURE DOES NOT WORK, AND WHAT REPLACES IT
//
// BENCH_P3_DETECT.md 13.8 sweeps the demod phase while sampling ADC5 with a
// STATIC reflector. ADC5 sits after the 0.66 s gated HPF, so a static reflector
// produces no signal there at all in TRACK mode -- the sweep reads noise at
// every phase. The fix is to chop the light and measure the DIFFERENCE, which
// also rejects ambient drift for free.
//
// A REFLECTOR IS NOT NEEDED, AND ON THIS BOARD IT HURTS. Measured 2026-08-24
// with nothing in front of the board at all: D11 -> D12 crosstalk plus the
// floor return gives a 1.795 V swing at TP7, which demodulates to 6.30 V at
// ADC5 against a 3.3 V ceiling -- the calibration is over-driven by 1.9x before
// any target exists. Crosstalk traverses the same TIA -> demod -> LPF chain as
// target light and the path-length difference is ~2 m = 1 tick at 104 kHz, so
// it calibrates the phase perfectly well. Attenuate INTO D12 to get in range;
// moving or darkening a reflector cannot help with something that is not the
// reflector.
//
// CHOPPING MUST NOT USE beam_enable(). That stops the demodulator clock as well
// as the carrier, making the dark half a different circuit rather than a
// reference. See the note on beam_chop() in beam.h.

#ifndef PITRAC_CAL_H
#define PITRAC_CAL_H

#include <stdbool.h>
#include <stdint.h>

#define CAL_CHOP_HZ_DEFAULT   20u    // see the droop note in cal.c
#define CAL_PHASE_POINTS      64u
#define CAL_CHOP_CYCLES        8u

// ---------------------------------------------------------------------------
// 3.4 -- demod phase at ONE carrier frequency.
// ---------------------------------------------------------------------------

// Why there are three purity numbers here and not one.
//
// The original version validated on h2_ratio alone, and 2026-08-21 showed that
// is blind to the exact failure it was written to catch. A sweep that had 84 %
// of its points hard against the ADC rail -- a square wave, not a cosine --
// scored h2/h1 = 0.012 and sailed through a 0.25 bar.
//
// The reason is elementary once seen: SYMMETRIC CLIPPING PRODUCES ONLY ODD
// HARMONICS. An ideal square wave has h2/h1 = 0 exactly and h3/h1 = 1/3. So the
// even-harmonic test is not merely weak here, it is looking in the one place
// where clipping is guaranteed to leave no trace. The measured sweep scored
// h3/h1 = 0.306 against a square wave's 0.333.
//
// h2 is kept, but as a DIAGNOSTIC ONLY -- it is not in `valid`, and the reason
// is structural rather than empirical. The demodulator is a 50 % square wave,
// and correlating anything with a 50 % square produces ONLY ODD harmonics of
// the phase sweep, whatever the optical waveform looks like. So h2 is exactly
// zero for every possible input to this chain and cannot report on the signal
// at all. Reconstructing the sweep from a 50 MS/s capture of TP7 gives h2 =
// 0.0000 to four places.
//
// What it does report is distortion introduced AFTER the demodulator, which on
// this board means ADC5 running out of negative headroom: the node idles ~11
// codes above 0 V so the negative half of a bipolar chop has nowhere to go, and
// while the HPF is still relaxing from the previous phase point both halves sit
// against the rail. That shows up as points reading exactly 0.0 codes.
//
// Measured 2026-08-24, one board, one session:
//
//   64 points / 8 cycles (400 ms per point):  h2 = 0.124 .. 0.148
//   32 points / 6 cycles (300 ms per point):  h2 = 0.261 .. 0.381
//
// and doubling the amplitude at four frequencies moved it by <= 0.001. It
// tracks the sweep rate against the 0.66 s HPF, not the signal. A high value
// means slow down the sweep; it does not mean the phase is wrong.
typedef struct {
    uint32_t freq_hz;
    int32_t  best_ticks;    // from the cosine fit, wrapped into [0, TOP]
    int32_t  argmax_ticks;  // raw grid maximum, as a human cross-check
    float    amplitude;     // codes, fitted
    float    quad_null;     // response 90 deg from the peak; should be ~0
    float    h2_ratio;      // |2nd| / |fundamental|: DIAGNOSTIC ONLY, see above
    float    h3_ratio;      // |3rd| / |fundamental|: SYMMETRIC CLIPPING. 1/3 = square
    float    h3_expected;   // what a CLEAN response gives at this duty -- see below
    float    duty;          // beam duty the sweep ran at, 0..1
    float    form_factor;   // fitted fundamental / true peak = 4*sinc(D)/pi
    float    peak;          // amplitude / form_factor: the real excursion, codes
    float    chain_delay_ns;// PWM edge -> demod input transit time. THE physical
                            //   number; best_ticks is just how it is encoded.
    float    sat_frac;      // fraction of sweep points against the ADC rail
    bool     warm;          // was the beam at thermal plateau?
    bool     valid;
} cal_demod_t;

// WHY THE PURITY BARS ARE DUTY-AWARE, AND WHY A FIXED h3 BAR WAS A TRAP
//
// The response is NOT a cosine and never was. It is a duty-D rectangular pulse
// correlated with a 50 % +-1 square, which is a TRAPEZOID, and a trapezoid has
// intrinsic odd harmonics. Working it out from the two Fourier series, the nth
// harmonic of the response (n odd) is
//
//     R_n = 4 * D * dV * sinc(n*D) / (n*pi),      sinc(x) = sin(pi x)/(pi x)
//
// and the true peak of the response is D * dV. Two consequences, both measured
// on hardware 2026-08-24 and both previously wrong in this file:
//
// 1. h3/h1 = |sinc(3D) / (3 sinc(D))| for a PERFECTLY CLEAN signal. At 25 %
//    duty that is exactly 1/9 = 0.1111, because sin(0.75 pi) == sin(0.25 pi) so
//    the sinc ratio collapses to 0.25/0.75. Measured: 0.110 and 0.119.
//
//    The old fixed bar of 0.15 therefore sat only 1.35x above a healthy 25 %
//    signal, and REJECTED A CLEAN SIGNAL below about 20 % duty:
//
//        duty   25 %   20 %   15 %   12.5 %   10 %    5 %
//        h3     0.111  0.180  0.242  0.268    0.291   0.323
//
//    That is a trap, not just a tight bar: the saturation message tells the
//    operator to reduce the light, and dropping the duty is the obvious way to
//    do it. The bar is now `h3_expected(D) + CAL_H3_TOL`.
//
//    !! h3 LOSES ITS POWER AT LOW DUTY. As D -> 0 the clean response becomes a
//    square wave in phase and h3_expected -> 1/3, which is exactly what hard
//    clipping gives. Below ~15 % duty h3 cannot separate the two and sat_frac
//    -- a direct count of points against the rail, with no duty dependence --
//    is the guard that carries the load. Do not read a passing h3 at low duty
//    as evidence of anything.
//
// 2. The fitted fundamental is NOT the peak. R_1/peak = 4*sinc(D)/pi, which is
//    1.146 at 25 % duty (1.273 as D -> 0, the square-wave limit; 0.811 at 50 %,
//    the triangle limit). So the old `amplitude <= ADC_FULL_SCALE` test was
//    biased by ~15 % against a clean 25 % signal and capped usable range at
//    ~87 % of full scale. The check is now on the recovered `peak`.
#define CAL_H2_MAX        0.25f   // DIAGNOSTIC ONLY -- not in valid, see above
#define CAL_H3_TOL        0.05f   // allowed EXCESS over h3_expected(duty)
#define CAL_SAT_MAX       0.10f   // fraction of points allowed against the rail
#define CAL_QUAD_NULL_MAX 0.15f   // |quad null| as a fraction of amplitude

// Intrinsic harmonic content of a clean lock-in response at beam duty `duty`.
// Both are pure geometry -- no board constants, nothing measured.
float cal_h3_expected(float duty);   // |sinc(3D)/(3 sinc(D))|
float cal_form_factor(float duty);   // fitted fundamental / true peak

// ---------------------------------------------------------------------------
// Live level, for aiming the target BEFORE spending 26 s on a sweep.
//
// One chopped reading at the CURRENT phase, returned as a peak in codes -- the
// same quantity cal_demod_t.peak reports, so it is directly comparable to the
// 50-70 % of full scale that `cal demod` wants.
//
// WHY THIS EXISTS. Getting the level right is the single most repeated failure
// in phase 3: too much light saturates the sweep, too little and there is
// nothing to fit, and the only feedback was to run the 26 s sweep and read the
// verdict afterwards. Worse, the natural way to add signal by hand -- holding
// something up in front of the board -- is exactly what corrupts a `cal model`
// run, because the scene has to hold still for two minutes. A live number lets
// the target be positioned and CLAMPED before anything is committed.
//
// It is only the TRUE peak if the demod phase is already near it, since it
// samples one phase rather than sweeping. Run `cal demod` once first.
#define CAL_LEVEL_CYCLES 4u          // 4 chop cycles at 20 Hz = 200 ms = 5 Hz update
float cal_level_peak(uint32_t cycles, uint32_t chop_hz);

// Sweeps phase across a full carrier period, chopping the beam at each point.
// Blocking, roughly n_points * cycles / chop_hz seconds (64 x 8 / 20 = ~26 s).
// Needs the HPF in TRACK and a warm beam. No reflector -- see the note above.
bool cal_demod_phase(uint32_t freq_hz, uint32_t n_points, uint32_t cycles,
                     uint32_t chop_hz, cal_demod_t *out);

// ---------------------------------------------------------------------------
// The phase model -- what actually varies with frequency, and what does not.
//
// !! The old comment here claimed that a pure delay would make `phase_ticks`
// INDEPENDENT of frequency, and that the observed variation therefore proved
// dispersion. Both halves are wrong, and the 2026-08-24 sweep shows why.
//
// best_ticks has to absorb TWO things, and only one of them is the chain:
//
//   phase_ticks(f) = t_chain_ticks  -  (duty/2) * (TOP+1)
//
// The second term is pure geometry: a duty-D pulse is centred D/2 of the way
// into the period, and the period in ticks scales as SYSCLK/f. At 25 % duty it
// is -0.125*(TOP+1), which swings from -234 ticks at 80 kHz to -94 at 200 kHz.
// THAT is what makes best_ticks run 1734 -> 746 across the band. It is not
// dispersion, and it would be there for a perfectly delay-like chain.
//
// The first term is the real chain delay, and it is nearly constant across the
// band -- about 570-620 ns, reproducible between sessions to ~50 ns.
//
// !! THE DROOP ACROSS THE BAND IS NOT A WELL-DETERMINED QUANTITY. Two fits on
// one board, both with residuals under 0.09 deg:
//
//   2026-08-24:  620.8 ns at 80 kHz -> 599.1 at 200 kHz   (-3.5 %)
//   2026-08-25:  571.2 ns at 80 kHz -> 571.0 at 200 kHz   (-0.05 %)
//
// The spread is a DIFFERENCE OF TWO LARGE NUMBERS, so a few-tick shift in the
// fit moves it enormously while barely touching either endpoint. An earlier
// revision of this comment quoted the 3.5 % as a property of the hardware and
// attributed a quarter of it to the TIA pole. That was over-reading one
// measurement, and the second one does not reproduce it.
//
// What IS robust: the absolute delay, and the pure-delay verdict -- both runs
// pass the CAL_PURE_DELAY_MAX_DEG bar by a wide margin (0.78 and 0.01 deg).
// Treat the span as "small, and smaller than the bar", not as a measured
// dispersion figure.
//
// So why fit a model at all? Because holding one TICK COUNT is catastrophic:
// at 140 kHz and 170 kHz the error passes quadrature (+107 deg and -162 deg)
// and the correlation INVERTS. scan carrier would rank those candidates
// negative. The model exists to recompute the ticks per frequency, not because
// the chain is badly dispersive.
//
// pure_delay is therefore judged on the DELAY SPREAD across the fitted band,
// not on the polynomial coefficients. a0 in particular is meaningless on its
// own -- it absorbs the 2*pi wrap, and reading it physically gives -45.7 deg at
// DC, which no real chain has. The old test compared |a0| < 0.15 and so
// reported "NOT a pure delay" for a chain that is very nearly one.
// ---------------------------------------------------------------------------

typedef struct {
    float    a0, a1, a2;    // theta_rad(f) = a0 + a1*f + a2*f^2, f in Hz
                            //   LOCAL INTERPOLANT. The coefficients have no
                            //   individual physical meaning -- see above.
    uint32_t f_lo, f_hi;    // calibrated range. DO NOT extrapolate.
    uint8_t  n_points;
    float    resid_rms_rad;
    float    duty;          // beam duty the sweep ran at -- needed to strip the
                            //   geometric term back out and recover the delay
    float    delay_lo_ns;   // chain delay at f_lo   } the physically meaningful
    float    delay_hi_ns;   // chain delay at f_hi   } pair
    float    amp_spread;    // (max peak / min peak) - 1 across the accepted
                            //   points. THE OPTICAL-STABILITY CHECK -- see below.
    float    span_err_deg;  // worst-case error from using ONE delay number
    bool     pure_delay;    // span_err_deg < CAL_PURE_DELAY_MAX_DEG
    bool     valid;
} cal_phase_model_t;

// How far the chain delay may drift across the fitted band and still be called
// a pure delay, expressed as the worst-case phase error at f_hi from holding a
// single number. 5 deg costs 0.4 % of signal.
#define CAL_PURE_DELAY_MAX_DEG  5.0f

// Optical stability across a model run.
//
// Every point in a model sweep sees the same optics, so the peak amplitude
// should barely move between them -- frequency changes the phase, not how much
// light comes back. A large spread means the SCENE changed during the run, and
// then the five points are not measurements of one system.
//
// Measured 2026-08-28 on one board, back to back:
//
//   static target, nothing moving : 7401..7602 codes  ->  2.7 % spread
//   operator holding a hand up    : 6221..7907 codes  -> 27.1 % spread
//
// A 10x separation, so the bar is easy to place. The bad run also clipped two
// of its five points and produced a 0.37 deg residual against 0.08 deg for a
// good one. Nothing in the per-point checks catches this: each point on its own
// looks fine, because each point IS fine -- they just describe different scenes.
#define CAL_MODEL_AMP_SPREAD_MAX 0.20f

bool    cal_demod_model(uint32_t f0, uint32_t f1, uint32_t npts,
                        cal_phase_model_t *out);
int32_t cal_model_phase_ticks(const cal_phase_model_t *m, uint32_t freq_hz);
bool    cal_model_covers(const cal_phase_model_t *m, uint32_t freq_hz);

// The chain delay the model implies at `freq_hz`, in nanoseconds: the geometric
// (duty/2)*(TOP+1) term stripped back off. This is the number to compare across
// frequencies, against the netlist, and between boards -- best_ticks is not.
float   cal_model_delay_ns(const cal_phase_model_t *m, uint32_t freq_hz);

// ---------------------------------------------------------------------------
// 3.6 -- scan carrier.
// ---------------------------------------------------------------------------

typedef struct {
    uint32_t freq_hz;
    int32_t  phase_ticks;
    float    sigma_noise;      // beam ON, no target, TRACK   (codes)
    float    sigma_floor;      // beam OFF                    (codes)
    float    signal;           // chopped reflector           (codes)
    float    snr;              // signal / sigma_noise
    float    beam_noise_ratio; // sigma_noise / sigma_floor
} cal_scan_point_t;

// Per candidate: phase set from the model, then noise, floor and signal.
// Prints a row per candidate as it goes. Blocking and slow -- minutes.
// Refuses on a cold beam unless `force_cold`.
bool cal_scan_carrier(uint32_t f0, uint32_t f1, uint32_t npts,
                      const cal_phase_model_t *model, bool force_cold,
                      cal_scan_point_t *best);

// ---------------------------------------------------------------------------
// U12B gain / R98 selection. See detect.h for why this is a continuous knob.
// ---------------------------------------------------------------------------

typedef struct {
    float gain_now, gain_target;
    float r98_ideal, r98_e24;     // ohms; 0 = leave unpopulated
    float clip_mv_now, clip_mv_new;   // dTP9 at which ADC5 saturates
} cal_gain_t;

// Given the measured peak (ADC5 codes above baseline) for the WEAKEST target
// that must still trigger, recommend an R98 value.
void cal_gain_recommend(float measured_peak_codes, float target_fraction,
                        cal_gain_t *out);

#endif // PITRAC_CAL_H
