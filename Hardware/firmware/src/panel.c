// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
#include "panel.h"
#include "board.h"
#include "safe_state.h"
#include "power_fsm.h"

#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include <string.h>

// ~1 kHz PWM. Fast enough that no one perceives flicker, slow enough that the
// 47R/220R sink resistors and the FETs do not care about edge rate.
//   150 MHz / (150 * 1000) = 1000 Hz
#define PANEL_PWM_WRAP   999u
#define PANEL_PWM_DIV    150.0f

static uint s_slice_pwr, s_chan_pwr;
static uint s_slice_rdy, s_chan_rdy;

static int  s_ovr_pwr   = -1;   // -1 = automatic
static int  s_ovr_rdy   = -1;
static int  s_force_pat = -1;   // -1 = follow the FSM
static bool s_ready     = false;

static const char *const k_pat_names[PANEL_PAT__COUNT] = {
    [PANEL_PAT_OFF]      = "off",
    [PANEL_PAT_POWERING] = "powering",
    [PANEL_PAT_BOOTING]  = "booting",
    [PANEL_PAT_RUNNING]  = "running",
    [PANEL_PAT_SHUTDOWN] = "shutdown",
    [PANEL_PAT_FAULT]    = "fault",
};

const char *panel_pattern_name(panel_pattern_t p) {
    if (p >= PANEL_PAT__COUNT) return "?";
    const char *n = k_pat_names[p];
    return n ? n : "?";
}

int panel_pattern_from_name(const char *s) {
    for (int i = 0; i < PANEL_PAT__COUNT; i++)
        if (k_pat_names[i] && !strcmp(s, k_pat_names[i])) return i;
    // A few forgiving aliases for typing at 1am.
    if (!strcmp(s, "poweron") || !strcmp(s, "power")) return PANEL_PAT_POWERING;
    if (!strcmp(s, "boot"))                          return PANEL_PAT_BOOTING;
    if (!strcmp(s, "run")  || !strcmp(s, "solid"))   return PANEL_PAT_RUNNING;
    if (!strcmp(s, "shut") || !strcmp(s, "down"))    return PANEL_PAT_SHUTDOWN;
    return -1;
}

static void set_pct(uint slice, uint chan, int pct) {
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    pwm_set_chan_level(slice, chan, (uint16_t)((PANEL_PWM_WRAP + 1) * pct / 100));
}

void panel_init(void) {
    gpio_set_function(PIN_PWR_BTN_LED, GPIO_FUNC_PWM);
    gpio_set_function(PIN_READY_LED,   GPIO_FUNC_PWM);

    s_slice_pwr = pwm_gpio_to_slice_num(PIN_PWR_BTN_LED);
    s_chan_pwr  = pwm_gpio_to_channel(PIN_PWR_BTN_LED);
    s_slice_rdy = pwm_gpio_to_slice_num(PIN_READY_LED);
    s_chan_rdy  = pwm_gpio_to_channel(PIN_READY_LED);

    pwm_config c = pwm_get_default_config();
    pwm_config_set_wrap(&c, PANEL_PWM_WRAP);
    pwm_config_set_clkdiv(&c, PANEL_PWM_DIV);

    set_pct(s_slice_pwr, s_chan_pwr, 0);
    set_pct(s_slice_rdy, s_chan_rdy, 0);
    pwm_init(s_slice_pwr, &c, true);
    pwm_init(s_slice_rdy, &c, true);
    set_pct(s_slice_pwr, s_chan_pwr, 0);
    set_pct(s_slice_rdy, s_chan_rdy, 0);
}

// Triangle wave 0..100..0 over period_ms. Cheap, and reads as a breath without a
// gamma table -- the eye is forgiving at these levels.
static int breathe(uint32_t t, uint32_t period_ms) {
    uint32_t p = t % period_ms;
    uint32_t half = period_ms / 2;
    return (p < half) ? (int)(100 * p / half)
                      : (int)(100 * (period_ms - p) / half);
}

static int pattern_pct(panel_pattern_t pat, uint32_t t) {
    switch (pat) {
        case PANEL_PAT_POWERING: return breathe(t, 600);
        case PANEL_PAT_BOOTING:  return breathe(t, 2000);
        case PANEL_PAT_RUNNING:  return 100;
        case PANEL_PAT_SHUTDOWN: return ((t % 300) < 150) ? 100 : 0;
        case PANEL_PAT_FAULT: {
            // Two quick pulses then a pause -- distinct at a glance from the
            // steady blink of a normal transition.
            uint32_t p = t % 1200;
            return (p < 120 || (p >= 240 && p < 360)) ? 100 : 0;
        }
        case PANEL_PAT_OFF:
        default:                 return 0;
    }
}

panel_pattern_t panel_pattern_for_state(void) {
    if (fault_current() != FAULT_NONE) return PANEL_PAT_FAULT;

    switch (power_fsm_state()) {
        // Dark, and it cannot be otherwise -- the rail feeding it is open.
        case PS_STANDBY:        return PANEL_PAT_OFF;
        case PS_POWERING_ON:    return PANEL_PAT_POWERING;
        case PS_PI_BOOTING:     return PANEL_PAT_BOOTING;
        case PS_RUNNING:
        case PS_BENCH_RUNNING:  return PANEL_PAT_RUNNING;
        case PS_SHUTTING_DOWN:  return PANEL_PAT_SHUTDOWN;
        default:                return PANEL_PAT_FAULT;
    }
}

void panel_update(void) {
    uint32_t t = to_ms_since_boot(get_absolute_time());

    int pwr;
    if (s_ovr_pwr >= 0) {
        pwr = s_ovr_pwr;                                   // explicit brightness wins
    } else if (s_force_pat >= 0) {
        pwr = pattern_pct((panel_pattern_t)s_force_pat, t); // forced pattern
    } else {
        pwr = pattern_pct(panel_pattern_for_state(), t);    // automatic
    }

    int rdy = (s_ovr_rdy >= 0) ? s_ovr_rdy : (s_ready ? 100 : 0);

    set_pct(s_slice_pwr, s_chan_pwr, pwr);
    set_pct(s_slice_rdy, s_chan_rdy, rdy);
}

// Sentinels: >=0 sets a fixed brightness, -1 hands the LED back to the state
// machine, anything lower leaves that LED alone. The third case matters so the
// CLI can set one LED without silently resetting the other to automatic.
static int apply_ovr(int cur, int req) {
    if (req < -1) return cur;
    if (req < 0)  return -1;
    return req > 100 ? 100 : req;
}

void panel_override(int pwr_pct, int rdy_pct) {
    s_ovr_pwr = apply_ovr(s_ovr_pwr, pwr_pct);
    s_ovr_rdy = apply_ovr(s_ovr_rdy, rdy_pct);
}

bool panel_override_active(void) { return s_ovr_pwr >= 0 || s_ovr_rdy >= 0; }

void panel_force_pattern(int pattern) {
    s_force_pat = (pattern < 0 || pattern >= PANEL_PAT__COUNT) ? -1 : pattern;
    // A forced pattern is meaningless while a fixed brightness is pinned on top
    // of it, so release the brightness override at the same time.
    if (s_force_pat >= 0) s_ovr_pwr = -1;
}

int panel_forced_pattern(void) { return s_force_pat; }

void panel_set_ready(bool ready) { s_ready = ready; }
