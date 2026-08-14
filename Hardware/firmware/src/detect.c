// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// detect.c -- threshold DAC, gated-HPF control, comparator. See detect.h.

#include "detect.h"
#include "board.h"
#include "adc_engine.h"
#include "power_fsm.h"

#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include <math.h>

static uint     s_slice_thr, s_chan_thr;
static uint16_t s_thr_level;
static float    s_vref = 3.3f;
static hpf_mode_t s_hpf = HPF_TRACK;

// ---------------------------------------------------------------------------

void detect_init(void) {
    // GPIO44 = slice 10 channel A. Resolved at runtime, never from a constant --
    // the hand-written slice numbers in this file's ancestor were wrong three
    // times out of four (see the note in board.h).
    //
    // *** GPIO36 (UART_TX) IS THE SAME SLICE AND CHANNEL. *** It coexists only
    // because it stays SIO/UART. See the high-bank collision table in board.h.
    s_slice_thr = pwm_gpio_to_slice_num(PIN_THRESHOLD_PWM);
    s_chan_thr  = pwm_gpio_to_channel(PIN_THRESHOLD_PWM);

    pwm_config c = pwm_get_default_config();
    pwm_config_set_wrap(&c, (uint16_t)DAC_TOP);
    pwm_config_set_clkdiv_int(&c, 1);          // 150 MHz / 1024 = 146.48 kHz
    pwm_init(s_slice_thr, &c, false);

    // Threshold starts at zero, i.e. the comparator trips on anything. That is
    // the safe direction: a threshold stuck HIGH would look like "no ball ever",
    // which is indistinguishable from a dead detector.
    s_thr_level = 0;
    pwm_set_chan_level(s_slice_thr, s_chan_thr, 0);

    // Hand the pad to the PWM block. Until this point safe_state.c has it as an
    // SIO output driven low.
    //
    // NOTE GPIO44 is also ADC4. adc_engine_init() deliberately never calls
    // adc_gpio_init() on it and ADC_VALID_MASK excludes ch4 -- sampling a pin
    // that is being driven as a digital output reads the driver, not a signal.
    gpio_set_function(PIN_THRESHOLD_PWM, GPIO_FUNC_PWM);
    pwm_set_enabled(s_slice_thr, true);

    // GPIO46: leave it exactly as safe_state.c set it -- SIO input, NO internal
    // pulls. R103 is an external 10K pull-up on an open-collector output; an
    // internal pull-down would fight it and an internal pull-up would weaken the
    // logic-0 level.
    gpio_set_dir(PIN_D_COMPARATOR, GPIO_IN);

    // GPIO33 stays wherever safe_state.c left it (low) until the rail is up.
    s_hpf = (gpio_get_out_level(PIN_HPF_TOGGLE) == HPF_SEL_TRACK) ? HPF_TRACK : HPF_HOLD;
}

// --- Threshold DAC ---------------------------------------------------------

void detect_threshold_set_duty(float duty) {
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;
    s_thr_level = (uint16_t)(duty * (float)(DAC_TOP + 1u) + 0.5f);
    if (s_thr_level > DAC_TOP) s_thr_level = (uint16_t)DAC_TOP;
    pwm_set_chan_level(s_slice_thr, s_chan_thr, s_thr_level);
    sleep_ms(DAC_SETTLE_MS);
}

void detect_threshold_set_volts(float v) {
    detect_threshold_set_duty((s_vref > 0.0f) ? (v / s_vref) : 0.0f);
}

float    detect_threshold_duty(void)  { return (float)s_thr_level / (float)(DAC_TOP + 1u); }
float    detect_threshold_volts(void) { return detect_threshold_duty() * s_vref; }
uint16_t detect_threshold_level(void) { return s_thr_level; }
void     detect_threshold_set_vref(float v) { if (v > 0.5f && v < 6.0f) s_vref = v; }
float    detect_threshold_vref(void)  { return s_vref; }

// --- Gated HPF -------------------------------------------------------------

bool detect_hpf_set(hpf_mode_t m) {
    if (!power_rails_ready()) return false;
    gpio_put(PIN_HPF_TOGGLE, (m == HPF_TRACK) ? HPF_SEL_TRACK : HPF_SEL_HOLD);
    s_hpf = m;
    return true;
}

void detect_hpf_safe_off(void) {
    gpio_put(PIN_HPF_TOGGLE, 0);
    s_hpf = (HPF_SEL_TRACK == 0) ? HPF_TRACK : HPF_HOLD;
}

hpf_mode_t  detect_hpf_mode(void) { return s_hpf; }
const char *detect_hpf_name(hpf_mode_t m) { return (m == HPF_TRACK) ? "TRACK" : "HOLD"; }

bool detect_comparator(void) { return gpio_get(PIN_D_COMPARATOR) != 0; }

// ---------------------------------------------------------------------------
// HPF polarity self-test.
//
// The discriminator is drift, and the two states differ by orders of magnitude:
//
//   TRACK  the node is pinned to 0 V through R96 2M. Op-amp bias (~10 pA) into
//          2M is ~20 uV. It does not move.
//   HOLD   the node is open. TMUX1219 off-leakage of order 1 nA into C81 330 nF
//          is ~3 mV/s at the node, x14.5 = ~44 mV/s at ADC5 = ~55 codes/s.
//
// So over a few seconds HOLD walks by tens to hundreds of codes and TRACK does
// not. Anything less than a clear separation is a hardware finding, not a
// firmware result, and the caller must say so rather than pick a winner.
// ---------------------------------------------------------------------------

static float sample_drift_v(uint32_t window_ms, float *out_mean_v) {
    // Let the mux settle and the HPF do whatever it is going to do first.
    sleep_ms(200);

    uint16_t code;
    if (!adc_ring_avg(ADC_CH_DETECT, 256, &code)) { if (out_mean_v) *out_mean_v = 0.0f; return -1.0f; }
    float first = adc_code_to_volts(code);
    float lo = first, hi = first, sum = first;
    uint32_t n = 1;

    uint32_t t0 = to_ms_since_boot(get_absolute_time());
    while (to_ms_since_boot(get_absolute_time()) - t0 < window_ms) {
        sleep_ms(50);
        if (!adc_ring_avg(ADC_CH_DETECT, 256, &code)) continue;
        float v = adc_code_to_volts(code);
        if (v < lo) lo = v;
        if (v > hi) hi = v;
        sum += v; n++;
    }
    if (out_mean_v) *out_mean_v = sum / (float)n;
    return hi - lo;
}

void detect_hpf_test(hpf_test_t *out, uint32_t window_ms) {
    if (!out) return;
    hpf_mode_t restore = s_hpf;

    detect_hpf_set(HPF_TRACK);
    out->track_drift_v = sample_drift_v(window_ms, &out->track_mean_v);

    detect_hpf_set(HPF_HOLD);
    out->hold_drift_v = sample_drift_v(window_ms, &out->hold_mean_v);

    detect_hpf_set(restore);

    if (out->track_drift_v < 0.0f || out->hold_drift_v < 0.0f) {
        out->ratio = 0.0f; out->conclusive = false; out->polarity_ok = false;
        return;
    }

    // Floor the denominator at one ADC LSB so a perfectly still TRACK trace does
    // not divide by zero and report an infinite, meaningless ratio.
    float floor_v = adc_code_to_volts(1);
    float denom   = (out->track_drift_v > floor_v) ? out->track_drift_v : floor_v;
    out->ratio    = out->hold_drift_v / denom;

    // 4x separation. The predicted separation is far larger, so this is a loose
    // bar deliberately -- it only has to beat noise, and demanding more would
    // turn an ambiguous board into a failed test.
    out->conclusive  = (out->ratio >= 4.0f) || (out->ratio <= 0.25f);
    out->polarity_ok = out->conclusive && (out->ratio >= 4.0f);
}

// ---------------------------------------------------------------------------
// Threshold / comparator cross-calibration.
// ---------------------------------------------------------------------------

void detect_threshold_sweep(threshold_sweep_t *out, float lo, float hi, uint16_t steps) {
    if (!out || steps == 0) return;
    uint16_t restore = s_thr_level;

    out->found = false;
    out->steps = steps;
    out->adc5_span_v = 0.0f;

    float adc_lo = 1e9f, adc_hi = -1e9f;
    bool  prev_high = false;
    bool  first = true;

    for (uint16_t i = 0; i <= steps; i++) {
        float duty = lo + (hi - lo) * ((float)i / (float)steps);
        detect_threshold_set_duty(duty);          // includes DAC_SETTLE_MS

        uint16_t code = 0;
        adc_ring_avg(ADC_CH_DETECT, 256, &code);
        float v = adc_code_to_volts(code);
        if (v < adc_lo) adc_lo = v;
        if (v > adc_hi) adc_hi = v;

        bool high = detect_comparator();

        // The comparator idles HIGH when the signal is above threshold. Sweeping
        // the threshold UP, the flip we want is high -> low: the point where the
        // threshold overtakes the resting signal. Record the first transition in
        // either direction so the caller can sweep whichever way it likes.
        if (!first && high != prev_high) {
            if (!out->found) {
                out->found          = true;
                out->flip_duty      = duty;
                out->flip_volts     = duty * s_vref;
                out->adc5_at_flip   = code;
                out->adc5_at_flip_v = v;
            }
        }
        prev_high = high;
        first = false;
    }

    // ADC5 movement across the whole sweep. With no target and a steady beam the
    // only thing changing is the threshold, so anything here is GPIO44 crosstalk
    // into the detect node -- the 146.5 kHz DAC carrier is only 42 kHz from the
    // 104.17 kHz optical carrier. Two RC poles predict ~120 dB of rejection, so
    // the expected answer is "nothing"; the point is to have measured it.
    out->adc5_span_v = adc_hi - adc_lo;

    pwm_set_chan_level(s_slice_thr, s_chan_thr, restore);
    s_thr_level = restore;
    sleep_ms(DAC_SETTLE_MS);
}
