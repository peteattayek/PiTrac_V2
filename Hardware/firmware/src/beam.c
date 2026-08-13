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

void beam_configure(uint32_t freq_hz, float duty, int32_t phase_ticks) {
    if (freq_hz == 0) return;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;

    // Round to the NEAREST period, not down.
    //
    // Plain truncation here used to cost a whole count. 150e6/104167 = 1439.995
    // truncates to 1439, then -1 gives TOP=1438: a 1439-count period, 104239 Hz,
    // 72 Hz ABOVE the requested frequency. It also broke the documented design
    // point -- TOP=1439 (period 1440) is what makes level 432 exactly 30.000%;
    // at TOP=1438 the closest is 431/1439 = 29.951%. And it made the CLI's
    // "nearest achievable" message false, since 104166.67 Hz was available.
    //
    // 64-bit intermediate so the +freq_hz/2 rounding term cannot overflow.
    uint64_t n = ((uint64_t)SYSCLK_HZ + freq_hz / 2u) / freq_hz;  // counts per period
    if (n < 2) n = 2;

    // The counter is 16-bit, so below ~2289 Hz one period will not fit at
    // clkdiv=1 and we must divide the clock. `beam clamp` asks for 1 kHz and
    // used to be silently clamped to TOP=65535 -- i.e. 2289 Hz with a 218 us
    // commanded high phase, not the 1 kHz / 500 us it printed. The measurement
    // still worked (218 us > the ~86 us clamp) but the LED ran at ~20 % duty
    // instead of the ~9 % the procedure assumes.
    //
    // NOTE: with div > 1 a phase tick is div * 6.67 ns, not 6.67 ns. Only the
    // clamp command goes there; 2a runs at 104 kHz where div is always 1.
    uint32_t div = 1;
    while (n > 65536 && div < 256) {
        div++;
        n = ((uint64_t)SYSCLK_HZ / div + freq_hz / 2u) / freq_hz;
    }
    if (n > 65536) n = 65536;          // still too slow even at div=255
    s_div = div;
    uint32_t top = (uint32_t)(n - 1);

    s_top   = top;
    s_duty  = duty;
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
    s_duty = duty;
    // Live update â€” the compare register can change while the slice runs.
    pwm_set_chan_level(s_slice_car, s_chan_car, (uint16_t)(duty * (s_top + 1)));
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
