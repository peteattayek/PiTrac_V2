// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// strobe_plan.c -- see strobe_plan.h. No hardware access.

#include "strobe_plan.h"
#include "board.h"

#include <math.h>
#include <string.h>

// Cycles from enabling the state machine to the first `set pins, 1`:
// pull, mov x, jmp !x.
#define PIO_STARTUP_US  3u

float strobe_burst_charge_mc(uint32_t width_us, uint32_t count) {
    uint32_t clamp = (uint32_t)STROBE_HW_LIMIT_US;
    uint32_t effective = width_us < clamp ? width_us : clamp;
    return (float)((double)count * (double)STROBE_TARGET_CURRENT_A
                   * (double)effective / 1000.0);
}

static bool charge_over_limit(uint32_t width_us, uint32_t count) {
    // A hair of tolerance so a burst exactly at the limit is not refused by
    // float representation.
    return (double)strobe_burst_charge_mc(width_us, count)
         > (double)BURST_CHARGE_MAX_MC + 1e-6;
}

strobe_burst_err_t strobe_validate_burst(const strobe_burst_t *b, uint32_t max_width_us) {
    if (!b) return STROBE_BURST_COUNT;
    if (b->count < 1u || b->count > (uint32_t)STROBE_BURST_MAX_PULSES)
        return STROBE_BURST_COUNT;
    if (b->width_us < (uint32_t)STROBE_MIN_WIDTH_US || b->width_us > max_width_us)
        return STROBE_BURST_WIDTH;
    if (b->count > 1u &&
        (b->gap_us < (uint32_t)STROBE_MIN_GAP_US || b->gap_us > (uint32_t)STROBE_MAX_GAP_US))
        return STROBE_BURST_GAP;
    if (charge_over_limit(b->width_us, b->count)) return STROBE_BURST_CHARGE;
    return STROBE_BURST_OK;
}

const char *strobe_burst_err_text(strobe_burst_err_t e) {
    switch (e) {
        case STROBE_BURST_OK:     return "ok";
        case STROBE_BURST_WIDTH:  return "pulse width out of range";
        case STROBE_BURST_GAP:    return "gap out of range";
        case STROBE_BURST_COUNT:  return "pulse count out of range";
        case STROBE_BURST_CHARGE: return "burst charge over the limit";
        case STROBE_BURST_SPAN:   return "burst longer than the live ADC0 readback window";
    }
    return "?";
}

strobe_burst_err_t strobe_validate_live_burst(const strobe_burst_t *b, uint32_t max_width_us) {
    strobe_burst_err_t e = strobe_validate_burst(b, max_width_us);
    if (e != STROBE_BURST_OK) return e;
    if (strobe_burst_duration_us(b) > (uint32_t)STROBE_LIVE_SPAN_MAX_US) return STROBE_BURST_SPAN;
    return STROBE_BURST_OK;
}

size_t strobe_encode(const strobe_burst_t *b, uint32_t *words, size_t capacity) {
    if (!b || !words || b->count < 1u) return 0;
    if (b->width_us < STROBE_PIO_MIN_WIDTH_US) return 0;     // would encode the sentinel
    if (b->count > 1u && b->gap_us < STROBE_PIO_GAP_OVERHEAD_US) return 0;

    size_t needed = 2u * (size_t)b->count + 1u;
    if (capacity < needed) return 0;

    size_t n = 0;
    for (uint32_t i = 0; i < b->count; i++) {
        words[n++] = b->width_us - STROBE_PIO_WIDTH_OVERHEAD_US;
        bool last = (i + 1u == b->count);
        words[n++] = last ? 0u : b->gap_us - STROBE_PIO_GAP_OVERHEAD_US;
    }
    words[n++] = 0u;    // sentinel: raises IRQ0 and parks the state machine
    return n;
}

uint32_t strobe_burst_duration_us(const strobe_burst_t *b) {
    if (!b || b->count < 1u) return 0;
    return PIO_STARTUP_US
         + b->count * b->width_us
         + (b->count - 1u) * b->gap_us
         + STROBE_PIO_IRQ_AFTER_LAST_US;
}

strobe_plan_err_t strobe_compute_schedule(float speed_mps, strobe_schedule_t *s) {
    memset(s, 0, sizeof(*s));
    // Written so NaN fails both comparisons.
    if (!(speed_mps >= STROBE_V_MIN_MPS && speed_mps <= STROBE_V_MAX_MPS))
        return STROBE_PLAN_BAD_SPEED;

    double v = (double)speed_mps;
    s->speed_mps = speed_mps;

    // um / (m/s) = us. Round the width DOWN so the blur budget holds; the tiny
    // offset keeps an exact quotient (1000/50 = 20) from flooring to 19.
    double width_exact = (double)STROBE_BLUR_BUDGET_UM / v;
    uint32_t width = (uint32_t)floor(width_exact + 1e-9);
    if (width > (uint32_t)STROBE_SW_MAX_US) {
        width = (uint32_t)STROBE_SW_MAX_US;
        s->width_clamped = true;
    }
    if (width < (uint32_t)STROBE_MIN_WIDTH_US) width = (uint32_t)STROBE_MIN_WIDTH_US;

    uint32_t period = (uint32_t)lround((double)STROBE_FREEZE_SPACING_UM / v);
    uint32_t gap;
    if (period < width + (uint32_t)STROBE_MIN_GAP_US) {
        gap = (uint32_t)STROBE_MIN_GAP_US;
        s->gap_raised = true;
    } else {
        gap = period - width;
    }

    uint32_t count = BURST_PULSES_NOMINAL;
    s->count_requested = count;
    while (count > BURST_PULSES_MIN && charge_over_limit(width, count)) {
        count--;
        s->shed = true;
    }

    s->width_us  = width;
    s->gap_us    = gap;
    s->period_us = width + gap;
    s->count     = count;
    s->span_us   = (count - 1u) * s->period_us + width;
    s->charge_mc = strobe_burst_charge_mc(width, count);

    if (charge_over_limit(width, count)) return STROBE_PLAN_OVER_CHARGE;
    return STROBE_PLAN_OK;
}

// Conditions shared by both actions. Order sets which reason the operator sees
// first, so the most fundamental one comes first.
static strobe_refusal_t common_checks(const strobe_snapshot_t *s) {
    if (!s->rails_ready)         return STROBE_REFUSE_RAIL;
    if (s->fault_latched)        return STROBE_REFUSE_FAULT;
    if (s->pi_present)           return STROBE_REFUSE_PI;
    if (s->pulse_limit_defeated) return STROBE_REFUSE_PULSE_LIMIT;
    if (s->ready_led_on_pwm)     return STROBE_REFUSE_A7;
    if (s->engine_busy)          return STROBE_REFUSE_BUSY;
    if (s->pulse_pin_high)       return STROBE_REFUSE_PIN_HIGH;
    return STROBE_OK;
}

strobe_refusal_t strobe_may_fire(const strobe_snapshot_t *s) {
    if (!s) return STROBE_REFUSE_RAIL;
    strobe_refusal_t r = common_checks(s);
    if (r != STROBE_OK) return r;
    if (s->gate_nonzero) return STROBE_REFUSE_GATE_NONZERO;
    return STROBE_OK;
}

strobe_refusal_t strobe_may_set_gate(const strobe_snapshot_t *s, uint16_t level) {
    if (!s) return STROBE_REFUSE_RAIL;
    if (level == 0u) return STROBE_OK;
    return common_checks(s);
}

bool strobe_gate_settling(uint64_t now_us, uint64_t zeroed_us) {
    if (zeroed_us == 0u) return false;
    if (now_us < zeroed_us) return true;      // a clock that went backwards: be safe
    return (now_us - zeroed_us) < (uint64_t)DAC_SETTLE_MS * 1000u;
}

// ---------------------------------------------------------------------------
// Live policy (6c/6d). Same ordering idea as common_checks(): the most
// fundamental reason first, so the operator fixes causes before symptoms.
// ---------------------------------------------------------------------------

uint16_t strobe_live_gate_limit(uint16_t proven) {
    uint32_t lim = (uint32_t)proven + (uint32_t)STROBE_LIVE_GATE_STEP_MAX;
    if (lim > (uint32_t)STROBE_LIVE_GATE_CEILING) lim = (uint32_t)STROBE_LIVE_GATE_CEILING;
    return (uint16_t)lim;
}

// The environment live mode needs, whether arming, holding or firing.
static strobe_refusal_t live_environment(const strobe_live_t *l) {
    if (l->beam_on)       return STROBE_REFUSE_BEAM;
    if (l->detect_armed)  return STROBE_REFUSE_DETECT;
    if (!l->adc_idle)     return STROBE_REFUSE_ADC;
    return STROBE_OK;
}

strobe_refusal_t strobe_live_may_arm(const strobe_snapshot_t *s, const strobe_live_t *l) {
    if (!s || !l) return STROBE_REFUSE_RAIL;
    strobe_refusal_t r = common_checks(s);
    if (r != STROBE_OK) return r;
    if (s->gate_nonzero) return STROBE_REFUSE_GATE_NONZERO;
    return live_environment(l);
}

strobe_refusal_t strobe_live_may_set_gate(const strobe_snapshot_t *s, const strobe_live_t *l,
                                          uint16_t level) {
    if (!s || !l) return STROBE_REFUSE_RAIL;
    if (level == 0u) return STROBE_OK;
    if (!l->live) return STROBE_REFUSE_NOT_LIVE;
    strobe_refusal_t r = common_checks(s);
    if (r != STROBE_OK) return r;
    if (level > STROBE_LIVE_GATE_CEILING) return STROBE_REFUSE_CEILING;
    if (level > strobe_live_gate_limit(l->gate_proven)) return STROBE_REFUSE_STEP;
    return STROBE_OK;
}

strobe_refusal_t strobe_live_may_fire(const strobe_snapshot_t *s, const strobe_live_t *l) {
    if (!s || !l) return STROBE_REFUSE_RAIL;
    if (!l->live) return STROBE_REFUSE_NOT_LIVE;
    strobe_refusal_t r = common_checks(s);
    if (r != STROBE_OK) return r;
    if (!l->watchdog_armed) return STROBE_REFUSE_WATCHDOG;
    r = live_environment(l);
    if (r != STROBE_OK) return r;
    if (!l->gate_hw_match) return STROBE_REFUSE_GATE_MISMATCH;
    if (l->gate_level > STROBE_LIVE_GATE_CEILING) return STROBE_REFUSE_CEILING;
    if (l->gate_level > strobe_live_gate_limit(l->gate_proven)) return STROBE_REFUSE_STEP;
    if (!l->interval_ok) return STROBE_REFUSE_INTERVAL;
    if (!l->budget_ok)   return STROBE_REFUSE_BUDGET;
    return STROBE_OK;
}

strobe_refusal_t strobe_live_may_continue(const strobe_snapshot_t *s, const strobe_live_t *l) {
    if (!s || !l) return STROBE_REFUSE_RAIL;
    if (!l->live) return STROBE_REFUSE_NOT_LIVE;
    if (!s->rails_ready)         return STROBE_REFUSE_RAIL;
    if (s->fault_latched)        return STROBE_REFUSE_FAULT;
    if (s->pi_present)           return STROBE_REFUSE_PI;
    if (s->pulse_limit_defeated) return STROBE_REFUSE_PULSE_LIMIT;
    if (s->ready_led_on_pwm)     return STROBE_REFUSE_A7;
    if (!l->watchdog_armed)      return STROBE_REFUSE_WATCHDOG;
    strobe_refusal_t r = live_environment(l);
    if (r != STROBE_OK) return r;
    if (!l->gate_hw_match) return STROBE_REFUSE_GATE_MISMATCH;
    if (l->gate_level > STROBE_LIVE_GATE_CEILING) return STROBE_REFUSE_CEILING;
    return STROBE_OK;
}

const char *strobe_refusal_text(strobe_refusal_t r) {
    switch (r) {
        case STROBE_OK:                  return "ok";
        case STROBE_REFUSE_RAIL:         return "+5V rail not ready (U8 and the gate amp run from VIR)";
        case STROBE_REFUSE_FAULT:        return "a fault is latched";
        case STROBE_REFUSE_PI:           return "a Pi is present -- Phase 6 runs with J8 empty";
        case STROBE_REFUSE_PULSE_LIMIT:  return "GPIO27 is not provably SIO-low: the U5 limiter may be defeated";
        case STROBE_REFUSE_A7:           return "GPIO12 is on PWM again: it would share the gate DAC (A7)";
        case STROBE_REFUSE_BUSY:         return "a burst is in progress";
        case STROBE_REFUSE_PIN_HIGH:     return "GPIO25 is high with the engine idle";
        case STROBE_REFUSE_GATE_NONZERO: return "gate DAC is not at zero, or was zeroed < 20 ms ago and Q9's gate is still decaying (dry mode never pulses with a setpoint)";
        case STROBE_REFUSE_NOT_LIVE:     return "live mode is off -- a non-zero gate needs 'strobe live on confirm' first";
        case STROBE_REFUSE_WATCHDOG:     return "the hardware watchdog is not armed";
        case STROBE_REFUSE_BEAM:         return "the beam is on -- 'beam off' first";
        case STROBE_REFUSE_DETECT:       return "the detector is armed -- 'detect disarm' first";
        case STROBE_REFUSE_ADC:          return "the ADC is not in IDLE -- 'adcmode idle' first (live readback needs it)";
        case STROBE_REFUSE_GATE_MISMATCH: return "GPIO28 is not on PWM, or its compare register differs from the commanded level";
        case STROBE_REFUSE_CEILING:      return "over the live gate ceiling";
        case STROBE_REFUSE_STEP:         return "over the staircase limit: fire and measure at a lower level first";
        case STROBE_REFUSE_INTERVAL:     return "too soon after the last live firing";
        case STROBE_REFUSE_BUDGET:       return "the rolling charge budget is used up -- wait";
        case STROBE_REFUSE_6D:           return "clamptest needs a live firing at THIS gate level that measured 0.5-2.5 A";
    }
    return "?";
}
