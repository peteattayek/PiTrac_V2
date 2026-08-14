// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
#include "beam.h"
#include "board.h"
#include "power_fsm.h"

#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"

static uint     s_slice_car, s_chan_car;
static uint     s_slice_dem, s_chan_dem;

static uint32_t s_top    = CARRIER_TOP_DEFAULT;
static float    s_duty   = 0.02f;          // start low; ramp up deliberately
static int32_t  s_phase  = 0;
static uint32_t s_div    = 1;      // PWM clkdiv; >1 only below ~2289 Hz
static bool     s_on     = false;

// ---------------------------------------------------------------------------

static void beam_note_duty(float d);   // fwd; defined with the chop helpers

static inline uint32_t wrap_mod(int64_t v, int64_t m) {
    int64_t r = v % m;
    return (uint32_t)(r < 0 ? r + m : r);
}

// Enable or disable exactly our two slices, in ONE write, leaving every other
// slice untouched.
//
// The single write is what gives phase lock: both counters start on the same
// clock edge. Using the SDK's pwm_set_mask_enabled() here would be a bug â€” it
// assigns PWM_EN wholesale and would switch off slices 5 and 6, which are the
// panel LEDs.
static void beam_slices_enable(bool on) {
    uint32_t mask = (1u << s_slice_car) | (1u << s_slice_dem);
    uint32_t en   = pwm_hw->en;
    en = on ? (en | mask) : (en & ~mask);
    pwm_hw->en = en;
}

void beam_init(void) {
    s_slice_car = pwm_gpio_to_slice_num(PIN_MOD_PWM);
    s_chan_car  = pwm_gpio_to_channel(PIN_MOD_PWM);
    s_slice_dem = pwm_gpio_to_slice_num(PIN_DEMOD_PWM);
    s_chan_dem  = pwm_gpio_to_channel(PIN_DEMOD_PWM);

    // Leave the pins as plain GPIO outputs driven low by safe_state() until the
    // beam is actually commanded on. Handing them to the PWM block here would
    // start the carrier at boot.
    beam_configure(SYSCLK_HZ / (CARRIER_TOP_DEFAULT + 1), s_duty, 0);
}

// Effective duty at the LED, i.e. after U9's one-shot truncates the high phase.
//
// This is the quantity that sets average current and therefore junction
// temperature -- the COMMANDED duty does not, and the difference is the whole
// reason `beam clamp` is safe:
//
//   1 kHz  @ 50 %  -> high 500.0 us, clamped to 122.68 -> effective 12.3 %  SAFE
//   104kHz @ 50 %  -> high   4.8 us, under the clamp   -> effective 50.0 %  NOT
//
// A ceiling applied to the commanded duty would have to refuse `beam clamp`,
// which is a legitimate and safe measurement; a ceiling applied here catches
// exactly the dangerous combination and nothing else.
float beam_effective_duty_at(uint32_t top, uint32_t div, float duty) {
    if (top == 0) return duty;
    // Period and commanded high phase, in microseconds.
    float period_us = (float)((uint64_t)(top + 1) * div) * (1.0e6f / (float)SYSCLK_HZ);
    float high_us   = duty * period_us;
    float clamp_us  = (float)BEAM_ONESHOT_CLAMP_US;
    if (high_us > clamp_us) high_us = clamp_us;
    return (period_us > 0.0f) ? (high_us / period_us) : duty;
}

float beam_effective_duty(void) {
    return beam_effective_duty_at(s_top, s_div, s_duty);
}

// Compute what TOP/div beam_configure() would land on, WITHOUT applying them.
// Needed so the ceiling can be checked against the real effective duty before
// anything is written to the PWM block.
static void beam_plan(uint32_t freq_hz, uint32_t *out_top, uint32_t *out_div) {
    uint64_t n = ((uint64_t)SYSCLK_HZ + freq_hz / 2u) / freq_hz;
    if (n < 2) n = 2;
    uint32_t div = 1;
    while (n > 65536 && div < 256) {
        div++;
        n = ((uint64_t)SYSCLK_HZ / div + freq_hz / 2u) / freq_hz;
    }
    if (n > 65536) n = 65536;
    *out_div = div;
    *out_top = (uint32_t)(n - 1);
}

static float s_ceiling = BEAM_DUTY_CEILING;

void  beam_set_duty_ceiling(float c) { s_ceiling = (c < 0.0f) ? 0.0f : (c > 1.0f ? 1.0f : c); }
float beam_duty_ceiling(void)        { return s_ceiling; }

bool beam_would_exceed_ceiling(uint32_t freq_hz, float duty, float *out_effective) {
    if (freq_hz == 0) return false;
    uint32_t top, div;
    beam_plan(freq_hz, &top, &div);
    float eff = beam_effective_duty_at(top, div, duty);
    if (out_effective) *out_effective = eff;
    return eff > s_ceiling;
}

void beam_configure(uint32_t freq_hz, float duty, int32_t phase_ticks) {
    if (freq_hz == 0) return;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;

    // THE ceiling check, in the one place every path goes through.
    //
    // It used to live only in the `beam duty` CLI branch, so `beam freq`,
    // `beam clamp` and `beam sweep` all skipped it. The concrete trap: clamp
    // writes s_duty = 0.50 persistently, so a bare `beam freq 104167` afterwards
    // ran the carrier at 104 kHz / 50 % -- ~1.6 A against a 0.95 A design point,
    // ~5.25 W in a D11 that already sits at 104 C on 3.23 W. U9 does not save you
    // there: a 50 % high phase at 104 kHz is 4.8 us, nowhere near the 122.7 us
    // clamp. See PROGRESS.md section 9.
    uint32_t p_top, p_div;
    beam_plan(freq_hz, &p_top, &p_div);
    {
        float eff = beam_effective_duty_at(p_top, p_div, duty);
        if (eff > s_ceiling) {
            // Back the commanded duty off until the EFFECTIVE duty is legal.
            // Clamping rather than refusing, because this is the last line of
            // defence and a silently dark beam is harder to debug than a beam
            // running at the ceiling. Callers that care print the difference.
            float period_us = (float)((uint64_t)(p_top + 1) * p_div)
                              * (1.0e6f / (float)SYSCLK_HZ);
            float allowed_high_us = s_ceiling * period_us;
            if (allowed_high_us > (float)BEAM_ONESHOT_CLAMP_US)
                allowed_high_us = (float)BEAM_ONESHOT_CLAMP_US;
            duty = (period_us > 0.0f) ? (allowed_high_us / period_us) : 0.0f;
        }
    }

    // Same numbers the ceiling check just used -- beam_plan() is the single
    // definition of "what TOP and clkdiv does this frequency land on".
    uint32_t top = p_top;
    s_div = p_div;

    s_top   = top;
    s_duty  = duty;
    beam_note_duty(duty);
    s_phase = (int32_t)wrap_mod(phase_ticks, (int64_t)top + 1);

    bool was_on = s_on;
    beam_slices_enable(false);         // counters must be still to preload them

    pwm_config c = pwm_get_default_config();
    pwm_config_set_wrap(&c, (uint16_t)top);
    pwm_config_set_clkdiv_int(&c, (uint8_t)s_div);  // 1 = full 6.67 ns phase resolution
    pwm_init(s_slice_car, &c, false);
    pwm_init(s_slice_dem, &c, false);

    // Carrier: `duty` high. Demod: 50 % square, as the sign-switching mux wants.
    pwm_set_chan_level(s_slice_car, s_chan_car, (uint16_t)(duty * (top + 1)));
    pwm_set_chan_level(s_slice_dem, s_chan_dem, (uint16_t)((top + 1) / 2));

    // Preload the counters so the demod leads the carrier by phase_ticks.
    pwm_set_counter(s_slice_car, 0);
    pwm_set_counter(s_slice_dem, (uint16_t)wrap_mod((int64_t)top + 1 - s_phase,
                                                    (int64_t)top + 1));

    if (was_on) beam_slices_enable(true);
}

void beam_set_duty(float duty) {
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;

    // Same ceiling as beam_configure(), on the same effective-duty basis. This
    // path does not go through beam_configure() -- it deliberately preserves TOP
    // rather than round-tripping through an integer division -- so the check has
    // to be repeated here rather than delegated.
    if (beam_effective_duty_at(s_top, s_div, duty) > s_ceiling) {
        float period_us = (float)((uint64_t)(s_top + 1) * s_div)
                          * (1.0e6f / (float)SYSCLK_HZ);
        float allowed_high_us = s_ceiling * period_us;
        if (allowed_high_us > (float)BEAM_ONESHOT_CLAMP_US)
            allowed_high_us = (float)BEAM_ONESHOT_CLAMP_US;
        duty = (period_us > 0.0f) ? (allowed_high_us / period_us) : 0.0f;
    }

    s_duty = duty;
    beam_note_duty(duty);
    // Live update â€” the compare register can change while the slice runs.
    pwm_set_chan_level(s_slice_car, s_chan_car, (uint16_t)(duty * (s_top + 1)));
}

// --- chopping and the warm-up tracker ---------------------------------------

static float    s_chop_on_duty;
static bool     s_chop_active;
static uint32_t s_warm_since_ms;
static bool     s_warm_running;

#define BEAM_WARM_DUTY_MIN  0.20f   // below this the LED is not heating usefully

// Called from every path that changes the compare level, so the warm-up clock
// cannot be fooled by a route that bypasses beam_set_duty().
static void beam_note_duty(float d) {
    if (d >= BEAM_WARM_DUTY_MIN) {
        if (!s_warm_running) {
            s_warm_running  = true;
            s_warm_since_ms = to_ms_since_boot(get_absolute_time());
        }
    } else {
        // A chop's "off" half must NOT reset the warm-up clock: a 25 ms dark
        // interval does not cool a 70 s thermal mass, and resetting here would
        // make every chopped measurement look permanently cold.
        if (!s_chop_active) s_warm_running = false;
    }
}

uint32_t beam_duty_stable_ms(void) {
    if (!s_warm_running || !s_on) return 0;
    return to_ms_since_boot(get_absolute_time()) - s_warm_since_ms;
}

void beam_chop_begin(float on_duty) {
    s_chop_on_duty = on_duty;
    s_chop_active  = true;
    beam_set_duty(on_duty);
}

void beam_chop(bool on) {
    // Compare level 0 -> the carrier pin never rises -> U9 never triggers -> the
    // LED is dark. The DEMOD slice is untouched and keeps running, which is the
    // whole point: beam_enable(false) would stop it and make the dark half a
    // different circuit rather than a reference.
    pwm_set_chan_level(s_slice_car, s_chan_car,
                       on ? (uint16_t)(s_chop_on_duty * (s_top + 1)) : 0u);
}

void beam_chop_end(void) {
    s_chop_active = false;
    beam_set_duty(s_chop_on_duty);
}

void beam_set_phase(int32_t phase_ticks) {
    s_phase = (int32_t)wrap_mod(phase_ticks, (int64_t)s_top + 1);

    // Counters can only be preloaded while stopped, so both slices stop and
    // restart together â€” preserving the lock. Costs one partial carrier period.
    bool was_on = (pwm_hw->en & (1u << s_slice_car)) != 0;
    beam_slices_enable(false);
    pwm_set_counter(s_slice_car, 0);
    pwm_set_counter(s_slice_dem, (uint16_t)wrap_mod((int64_t)s_top + 1 - s_phase,
                                                    (int64_t)s_top + 1));
    if (was_on) beam_slices_enable(true);
}

bool beam_enable(bool on) {
    if (on) {
        // U10 (MCP1416) and D11 both run from the SWITCHED +5V rail. Enabling the
        // carrier with the latch open does nothing useful and hides the real
        // reason nothing lights up.
        if (!power_rails_ready()) return false;

        gpio_set_function(PIN_MOD_PWM,   GPIO_FUNC_PWM);
        gpio_set_function(PIN_DEMOD_PWM, GPIO_FUNC_PWM);
        beam_slices_enable(true);
        s_on = true;
    } else {
        beam_slices_enable(false);
        // Hand the pins back to SIO and drive them low, so the beam is off even
        // if a slice is later re-enabled for some other reason. R69 backs this up.
        gpio_set_function(PIN_MOD_PWM,   GPIO_FUNC_SIO);
        gpio_set_function(PIN_DEMOD_PWM, GPIO_FUNC_SIO);
        gpio_set_dir(PIN_MOD_PWM,   GPIO_OUT);
        gpio_set_dir(PIN_DEMOD_PWM, GPIO_OUT);
        gpio_put(PIN_MOD_PWM,   0);
        gpio_put(PIN_DEMOD_PWM, 0);
        s_on = false;
    }
    return true;
}

bool beam_enabled(void) { return s_on; }

void beam_ramp_duty(float target, uint32_t step_ms) {
    if (target < 0.0f) target = 0.0f;
    if (target > 1.0f) target = 1.0f;
    if (step_ms == 0) step_ms = 250;

    float d = s_duty;
    const float step = 0.01f;          // 1 % at a time

    while (d < target - 0.0005f) {
        d += step;
        if (d > target) d = target;
        beam_set_duty(d);
        sleep_ms(step_ms);
    }
    while (d > target + 0.0005f) {
        d -= step;
        if (d < target) d = target;
        beam_set_duty(d);
        sleep_ms(step_ms);
    }
    beam_set_duty(target);
}

uint32_t beam_freq_hz(void)        { return SYSCLK_HZ / s_div / (s_top + 1); }
uint32_t beam_actual_freq_hz(void) { return SYSCLK_HZ / s_div / (s_top + 1); }
uint32_t beam_clkdiv(void)         { return s_div; }
float    beam_duty(void)           { return s_duty; }
int32_t  beam_phase_ticks(void)    { return s_phase; }
uint32_t beam_top(void)            { return s_top; }
