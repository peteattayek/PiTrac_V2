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
// h2 is still worth keeping -- it catches ASYMMETRIC distortion, which is a
// different fault (one rail hit, or a rectifying nonlinearity). The two
// harmonics answer different questions.
typedef struct {
    uint32_t freq_hz;
    int32_t  best_ticks;    // from the cosine fit, wrapped into [0, TOP]
    int32_t  argmax_ticks;  // raw grid maximum, as a human cross-check
    float    amplitude;     // codes, fitted
    float    quad_null;     // response 90 deg from the peak; should be ~0
    float    h2_ratio;      // |2nd| / |fundamental|: ASYMMETRIC distortion
    float    h3_ratio;      // |3rd| / |fundamental|: SYMMETRIC CLIPPING. 1/3 = square
    float    sat_frac;      // fraction of sweep points against the ADC rail
    bool     warm;          // was the beam at thermal plateau?
    bool     valid;
} cal_demod_t;

// Purity bars. A clean cosine sits near zero on both harmonic ratios.
#define CAL_H2_MAX        0.25f   // asymmetric distortion
#define CAL_H3_MAX        0.15f   // symmetric clipping (square wave = 0.333)
#define CAL_SAT_MAX       0.10f   // fraction of points allowed against the rail
#define CAL_QUAD_NULL_MAX 0.15f   // |quad null| as a fraction of amplitude

// Sweeps phase across a full carrier period, chopping the beam at each point.
// Blocking, roughly n_points * cycles / chop_hz seconds (64 x 8 / 20 = ~26 s).
// Needs a static reflector at the operating distance and the HPF in TRACK.
bool cal_demod_phase(uint32_t freq_hz, uint32_t n_points, uint32_t cycles,
                     uint32_t chop_hz, cal_demod_t *out);

// ---------------------------------------------------------------------------
// The phase model -- why one sweep is not enough, and why five is.
//
// If the chain were a PURE DELAY t_d, the required offset in sysclk ticks would
// be (TOP+1) * f * t_d = SYSCLK * t_d with clkdiv 1 -- INDEPENDENT of frequency,
// because a tick is a fixed 6.67 ns and the offset is a fixed time. One number
// would then serve every carrier.
//
// It is not a pure delay. The TIA and each filter pole contribute arctan(f/f_p),
// which is not linear in f, and a sweep at ONE frequency cannot separate t_d
// from f_p -- it yields theta at that f and nothing else. So the model is fitted
// from phase measured at several frequencies and is only valid between them.
//
// This matters because scan carrier compares candidates by SNR: hold the phase
// ticks fixed across a frequency sweep and every candidate is measured at a
// different phase error, which makes the ranking meaningless.
// ---------------------------------------------------------------------------

typedef struct {
    float    a0, a1, a2;    // theta_rad(f) = a0 + a1*f + a2*f^2, f in Hz
    uint32_t f_lo, f_hi;    // calibrated range. DO NOT extrapolate.
    uint8_t  n_points;
    float    resid_rms_rad;
    bool     pure_delay;    // a2 negligible and a0 ~ 0: one number covers all f
    bool     valid;
} cal_phase_model_t;

bool    cal_demod_model(uint32_t f0, uint32_t f1, uint32_t npts,
                        cal_phase_model_t *out);
int32_t cal_model_phase_ticks(const cal_phase_model_t *m, uint32_t freq_hz);
bool    cal_model_covers(const cal_phase_model_t *m, uint32_t freq_hz);

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
