// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
#include "beam.h"
#include "board.h"
#include "power_fsm.h"
#include "service.h"

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
static uint16_t s_level;           // the carrier's compare as last written (car_write())
static uint64_t s_dark_until_us;   // the carrier pin stays low until at least this (car_cut())

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
// clock edge. Using the SDK's pwm_set_mask_enabled() here would be a bug -- it
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

// THE 25 % (the owner, 2026-11-24, PROGRESS.md section 8) IS ON THE PWM PIN,
// Modulation_PWM: the carrier's compare level -- its high time, in counts -- is
// never more than the ceiling's share of the period, at most (TOP + 1) / 4 (image
// 1.16, review finding V115-F2), and no reconfiguration stretches a pulse or runs
// two together (car_cut(), image 1.17, V116-F1).
//
// THAT IS THE PIN, NOT THE LED (the owner, 2026-11-26). U9's ~CLR is the pin, so U9
// can only shorten a pulse, whatever its width; but U9, U10 and Q11 each take
// longer or shorter to turn off than on, and the LED's duty is the pin's plus that
// stretch times the frequency. No part in the chain publishes a maximum for it.
// Board 1 read 1.2 of 4.0 us at TP5 for 30 % at 250 kHz (Q2, BENCH_P2_BEAM.md):
// two figures, so a stretch of up to about 50 ns either way. Each board's is
// measured at the carrier and at 150 and 200 kHz instead (BRINGUP_NEW_BOARD.md Step
// 6.3b, with its uncertainty: image 1.18, V117-F1), and the carrier is
// held to 1-200 kHz (board.h, BEAM_FREQ_*): until 1.16 it took 1 Hz to 75 MHz.
//
// 1.14 and 1.15 bounded the duty AT THE LED with an assumed U9 width, 122 us and
// then 200 us; no maximum for this R and C is published, so neither held on every
// board (V114-F3, V115-F2). What the pin's bound costs: `beam clamp` runs 1 kHz at
// 25 % (250 us high) rather than 50 %, still well past the clamps measured,
// 120.96-135 us.
static float s_ceiling = BEAM_DUTY_OPERATING;

void beam_set_duty_ceiling(float c) {
    s_ceiling = (c < 0.0f) ? 0.0f : (c > BEAM_DUTY_OPERATING ? BEAM_DUTY_OPERATING : c);
}
float beam_duty_ceiling(void) { return s_ceiling; }

bool beam_duty_allowed(float duty) { return duty >= 0.0f && duty <= s_ceiling; }

// A duty request as the beam can take it: 0..1, and never above the ceiling.
static float duty_admitted(float duty) {
    if (!(duty > 0.0f)) return 0.0f;                 // NaN too
    if (duty > 1.0f) duty = 1.0f;
    return duty > s_ceiling ? s_ceiling : duty;
}

// The compare level for `duty` at this TOP, rounded down.
static uint32_t duty_level(float duty, uint32_t top) {
    return (uint32_t)((double)duty * (double)(top + 1u));
}

// EVERY write of the carrier's compare is this one, and it holds the level to the
// ceiling's share of the period, rounded down: at 25 %, (TOP + 1) / 4 exactly. The
// callers ask for no more anyway (duty_admitted()); this is where it is made so.
static void car_write(uint32_t level) {
    const uint32_t most = (uint32_t)((double)s_ceiling * (double)(s_top + 1u));
    if (level > most) level = most;
    s_level = (uint16_t)level;
    pwm_set_chan_level(s_slice_car, s_chan_car, s_level);
}

// The LED's duty ESTIMATED with board 1's U9, 122 us (BEAM_ONESHOT_CLAMP_US): for
// printing only. Another board's U9 differs, and the drive chain's stretch is not
// in it; the bound is the pin's (above).
float beam_led_duty_estimate(void) {
    const float us_per_count = (float)s_div * (1.0e6f / (float)SYSCLK_HZ);
    const float period_us = (float)(s_top + 1u) * us_per_count;
    float high_us = (float)s_level * us_per_count;
    if (high_us > (float)BEAM_ONESHOT_CLAMP_US) high_us = (float)BEAM_ONESHOT_CLAMP_US;
    return (period_us > 0.0f) ? high_us / period_us : 0.0f;
}

// Compute what TOP/div beam_configure() would land on, WITHOUT applying them.
// Every frequency in BEAM_FREQ_MIN_HZ..BEAM_FREQ_MAX_HZ lands on a period of
// BEAM_PERIOD_MIN_TICKS..BEAM_PERIOD_MAX_TICKS (1 kHz: divider 3, TOP 49999).
static void beam_plan(uint32_t freq_hz, uint32_t *out_top, uint32_t *out_div) {
    uint64_t n = ((uint64_t)SYSCLK_HZ + freq_hz / 2u) / freq_hz;
    if (n < 2) n = 2;
    uint32_t div = 1;
    // 255, not 256: the divider's integer field is 8 bits, and beam_configure()
    // writes (uint8_t)div. 256 used to wrap to 0 below ~9 Hz, so the hardware
    // ran at a divider the duty-ceiling check never saw. (Since image 1.17 nothing
    // below 1 kHz is planned at all: beam_configure() refuses it first.)
    while (n > 65536 && div < 255) {
        div++;
        n = ((uint64_t)SYSCLK_HZ / div + freq_hz / 2u) / freq_hz;
    }
    if (n > 65536) n = 65536;
    *out_div = div;
    *out_top = (uint32_t)(n - 1);
}

// ---------------------------------------------------------------------------
// THE PIN THROUGH A RECONFIGURATION (image 1.17, review finding V116-F1)
//
// A slice is stopped to preload its counter (that is how phase lock is set), and a
// stopped slice holds its output where it was -- high, if it stopped inside a pulse
// (the SDK's pwm_set_enabled()). Until 1.16 the pin stayed on the PWM through
// that, and the counter restarted at 0, which is the start of a pulse: a pulse cut
// by the stop ran on through it and straight into a whole new one, up to twice the
// level with no low between, and no compare register showing it.
//
// Now the pin is handed to SIO driven low before any slice stops (car_cut()), and
// back to the PWM no sooner than one whole period of the setting it was cut from
// (car_resume()). A pulse cut short -- at most a quarter of that period -- is then
// followed by at least the whole period dark, three times its length and more. The
// pin rejoins the running carrier wherever its period is: inside a pulse, the first
// pulse it shows is that pulse's remainder, SHORTER than the level, never longer.
// So every high stretch on the pin is at most its period's level, and is followed
// by at least three times its length low. Costs at most one period of dark per
// change (plus a microsecond): about 11 us at 104 kHz, 1 ms at 1 kHz.
//
// Image 1.17 also waited for the counter to pass the level before handing the pin
// back, to make the first pulse whole. It could not promise that -- the counter
// can wrap between the read and the switch, which itself takes time (review note
// V117-N1) -- and the bound above never needed it, so 1.18 does not wait.
// ---------------------------------------------------------------------------

// One period of the carrier as set now, rounded up to whole microseconds.
static uint32_t period_us_ceil(void) {
    const uint64_t ticks = (uint64_t)s_div * ((uint64_t)s_top + 1u);
    return (uint32_t)((ticks * 1000000u + SYSCLK_HZ - 1u) / SYSCLK_HZ);
}

// The carrier pin low NOW, whatever its slice is doing: SIO, its level and driver
// set first so the switch emits no glitch high. Then dark for a period of the
// setting as it is (+ 1: time_us_64() counts whole microseconds).
static void car_cut(void) {
    gpio_put(PIN_MOD_PWM, 0);
    gpio_set_dir(PIN_MOD_PWM, GPIO_OUT);
    gpio_set_function(PIN_MOD_PWM, GPIO_FUNC_SIO);
    const uint64_t until = time_us_64() + period_us_ceil() + 1u;
    if (until > s_dark_until_us) s_dark_until_us = until;
}

// The carrier pin back to the PWM, once the dark car_cut() set is over. It joins
// the running carrier wherever it is: at worst inside a pulse, whose remainder is
// then the first pulse, short.
static void car_resume(void) {
    while (time_us_64() < s_dark_until_us) tight_loop_contents();
    gpio_set_function(PIN_MOD_PWM, GPIO_FUNC_PWM);
}

// Set TOP, divider, duty and phase together, re-establishing phase lock: the one
// body behind beam_configure() (TOP and divider planned from a frequency) and
// beam_restore() (TOP and divider exactly as saved). False, with NOTHING changed,
// for a period outside the carrier's range (board.h, BEAM_FREQ_*): whatever path
// asked, the carrier never leaves it (image 1.17, V116-F1).
//
// The duty is held to the ceiling here, and car_write() holds the level written.
// Until 1.14 the ceiling lived only in the `beam duty` CLI branch, so `beam freq`,
// `beam clamp` and `beam sweep` skipped it. The concrete trap: `beam clamp` wrote
// s_duty = 0.50 persistently, so a bare `beam freq 104167` afterwards ran the
// carrier at 104 kHz / 50 % -- ~1.6 A against a 0.95 A design point, ~5.25 W in a
// D11 that already sits at 104 C on 3.23 W. See PROGRESS.md section 9. Held, not
// refused: this is the last line of defence, and a silently dark beam is harder
// to debug than one at the ceiling. Callers that care check beam_duty_allowed().
static bool beam_apply(uint32_t top, uint32_t div, float duty, int32_t phase_ticks) {
    const uint64_t ticks = (uint64_t)div * ((uint64_t)top + 1u);
    if (div < 1u || div > 255u || top > 65535u ||
        ticks < BEAM_PERIOD_MIN_TICKS || ticks > BEAM_PERIOD_MAX_TICKS) return false;
    duty = duty_admitted(duty);

    const bool was_on = s_on;
    if (was_on) car_cut();             // with the period it runs now, before it changes
    beam_slices_enable(false);         // counters must be still to preload them

    s_top   = top;
    s_div   = div;
    s_duty  = duty;
    beam_note_duty(duty);
    s_phase = (int32_t)wrap_mod(phase_ticks, (int64_t)top + 1);

    pwm_config c = pwm_get_default_config();
    pwm_config_set_wrap(&c, (uint16_t)top);
    pwm_config_set_clkdiv_int(&c, (uint8_t)s_div);  // 1 = full 6.67 ns phase resolution
    pwm_init(s_slice_car, &c, false);
    pwm_init(s_slice_dem, &c, false);

    // Carrier: `duty` high. Demod: 50 % square, as the sign-switching mux wants.
    car_write(duty_level(duty, top));
    pwm_set_chan_level(s_slice_dem, s_chan_dem, (uint16_t)((top + 1) / 2));

    // Preload the counters so the demod leads the carrier by phase_ticks.
    pwm_set_counter(s_slice_car, 0);
    pwm_set_counter(s_slice_dem, (uint16_t)wrap_mod((int64_t)top + 1 - s_phase,
                                                    (int64_t)top + 1));

    if (was_on) {
        beam_slices_enable(true);
        car_resume();
    }
    return true;
}

bool beam_freq_allowed(uint32_t freq_hz) {
    return freq_hz >= BEAM_FREQ_MIN_HZ && freq_hz <= BEAM_FREQ_MAX_HZ;
}

bool beam_configure(uint32_t freq_hz, float duty, int32_t phase_ticks) {
    if (!beam_freq_allowed(freq_hz)) return false;
    // beam_plan() is the single definition of "what TOP and clkdiv does this
    // frequency land on".
    uint32_t top, div;
    beam_plan(freq_hz, &top, &div);
    return beam_apply(top, div, duty, phase_ticks);
}

// Image 1.16, review finding V115-F3: a command that moves the carrier and puts it
// back (`cal model`, `scan carrier`) saved the frequency READ BACK, an integer, and
// re-planned from it -- 1021 Hz asked lands on 1020.x Hz, which reads back 1020
// and plans to another TOP (48971 -> 49019) -- and put back beam_duty() as it was
// AFTER its points. The setting itself is put back now, register for register.
beam_setting_t beam_setting(void) {
    const beam_setting_t s = { s_top, s_div, s_duty, s_phase };
    return s;
}

bool beam_restore(const beam_setting_t *s) {
    if (!s) return false;
    return beam_apply(s->top, s->div, s->duty, s->phase);
}

void beam_set_duty(float duty) {
    // This path does not go through beam_apply() -- it deliberately preserves TOP
    // rather than round-tripping through an integer division -- and holds the duty
    // the same way.
    duty = duty_admitted(duty);
    s_duty = duty;
    beam_note_duty(duty);
    // Live update -- the compare register can change while the slice runs.
    car_write(duty_level(duty, s_top));
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
    s_chop_active  = true;
    beam_set_duty(on_duty);
    // The duty beam_set_duty() let through, not the one asked for (image 1.15).
    s_chop_on_duty = s_duty;
}

void beam_chop(bool on) {
    // Compare level 0 -> the carrier pin never rises -> U9 never triggers -> the
    // LED is dark. The DEMOD slice is untouched and keeps running, which is the
    // whole point: beam_enable(false) would stop it and make the dark half a
    // different circuit rather than a reference. Through car_write(), as every
    // compare write is (image 1.16).
    car_write(on ? duty_level(s_chop_on_duty, s_top) : 0u);
}

void beam_chop_end(void) {
    s_chop_active = false;
    beam_set_duty(s_chop_on_duty);
}

void beam_set_phase(int32_t phase_ticks) {
    s_phase = (int32_t)wrap_mod(phase_ticks, (int64_t)s_top + 1);

    // Counters can only be preloaded while stopped, so both slices stop and
    // restart together -- preserving the lock. The carrier pin is held low across
    // it (car_cut(), image 1.17, V116-F1): a period of dark, never a stretched
    // pulse.
    const bool was_on = s_on;
    if (was_on) car_cut();
    beam_slices_enable(false);
    pwm_set_counter(s_slice_car, 0);
    pwm_set_counter(s_slice_dem, (uint16_t)wrap_mod((int64_t)s_top + 1 - s_phase,
                                                    (int64_t)s_top + 1));
    if (was_on) {
        beam_slices_enable(true);
        car_resume();
    }
}

bool beam_enable(bool on) {
    if (on) {
        // U10 (MCP1416) and D11 both run from the SWITCHED +5V rail. Enabling the
        // carrier with the latch open does nothing useful and hides the real
        // reason nothing lights up.
        if (!power_rails_ready()) return false;
        if (s_on) return true;             // running already: nothing to restart

        // The slices first, with the carrier pin still SIO low; the pin goes to
        // the PWM no sooner than a period after the last pulse was cut (image
        // 1.17, V116-F1). Until 1.16 the pin went to the PWM first, showing
        // whatever the stopped slice had held, high if it had stopped inside a
        // pulse.
        beam_slices_enable(true);
        gpio_set_function(PIN_DEMOD_PWM, GPIO_FUNC_PWM);
        s_on = true;
        car_resume();
    } else {
        // The carrier pin low at once (car_cut()), then the slices stopped. Both pins
        // stay SIO driven low, so the beam is off even if a slice is later
        // re-enabled for some other reason. R69 backs this up.
        car_cut();
        beam_slices_enable(false);
        gpio_put(PIN_DEMOD_PWM, 0);
        gpio_set_dir(PIN_DEMOD_PWM, GPIO_OUT);
        gpio_set_function(PIN_DEMOD_PWM, GPIO_FUNC_SIO);
        s_on = false;
    }
    return true;
}

bool beam_enabled(void) { return s_on; }

// Stops on a stop request or a fault (image 1.14, review finding V113-F3): the
// duty stays at the last step written, and nothing more is written. In 1.13 a
// stop sent during a ramp was ignored -- four more steps went out after it and
// the ramp reached its target -- which is the opposite of what an operator
// stopping a rising beam wants. Each step's wait is pitrac_yield_stoppable_ms(),
// so a stop is seen within 20 ms even with a 10 s step.
yield_t beam_ramp_duty(float target, uint32_t step_ms) {
    target = duty_admitted(target);       // a ramp never climbs past the ceiling
    if (step_ms == 0) step_ms = 250;

    float d = s_duty;
    const float step = 0.01f;          // 1 % at a time

    while (d < target - 0.0005f) {
        d += step;
        if (d > target) d = target;
        beam_set_duty(d);
        yield_t y = pitrac_yield_stoppable_ms(step_ms);
        if (y != YIELD_OK) return y;
    }
    while (d > target + 0.0005f) {
        d -= step;
        if (d < target) d = target;
        beam_set_duty(d);
        yield_t y = pitrac_yield_stoppable_ms(step_ms);
        if (y != YIELD_OK) return y;
    }
    beam_set_duty(target);
    return YIELD_OK;
}

uint32_t beam_freq_hz(void)        { return SYSCLK_HZ / s_div / (s_top + 1); }
uint32_t beam_actual_freq_hz(void) { return SYSCLK_HZ / s_div / (s_top + 1); }
uint32_t beam_clkdiv(void)         { return s_div; }
float    beam_duty(void)           { return s_duty; }
uint32_t beam_level(void)          { return s_level; }
int32_t  beam_phase_ticks(void)    { return s_phase; }
uint32_t beam_top(void)            { return s_top; }
