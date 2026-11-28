// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// stop_test.c -- does a stop STOP? Image 1.14, review finding V113-F3.
//
// The 1.13 review compiled the real `panel demo` and beam ramp with an inert
// output model and a yield that reported a stop: the demo ran all 24 s after a
// stop at 3 s, and a 2 %-to-6 % ramp wrote four more steps after a stop at
// 100 ms and reached 6 %. 1.13 told the Pi "stopped" in both cases. A triggered
// ADC capture's wait polled neither the USB key nor the Pi's CLI_ABORT.
//
// Here the REAL service.c (the yield, the latch, the "taken" mark) and the REAL
// beam.c run with these, copied verbatim out of cli.c by extract_functions.py
// (cli_stop_extract.c): `panel` (test and demo), `beam sweep`'s steps and the
// capture wait, with the parse helpers `panel` uses. A stop is a key that
// arrives at a chosen time -- the Pi's CLI_ABORT sets the same latch through
// pitrac_abort_source(). The register model is the SDK stand-in's (sdk_standin/).
//
// Image 1.15 adds the 1.14 review's cases: the duty at the LED read back from
// the PWM registers against U9 widths from every board's band (V114-F3); a
// sweep with no dwell (V114-F4); `cal model` stopped part-way, with its outer
// function copied out of cal.c and the point measurement scripted (V114-F5);
// and `capture` / `capture trig` stopped while they print (V114-F6).
//
// Image 1.16 adds the 1.15 review's: the 25 % on the PWM's high time, read from
// every compare write with no U9 width at all (V115-F2); the carrier put back
// register for register by `cal model` and by `scan carrier` (its outer function
// copied out of cal.c too) from a frequency that rounds (V115-F3); a stop that
// comes on a dump's last line (V115-F6); and `capture` refused under the watchdog
// when its unserviced acquisition is long (V115-N1).
//
// Image 1.17 adds the 1.16 review's: the carrier held to 1-200 kHz on every path,
// and the beam's pin traced through every change made while it runs -- the slices
// run in time in this model, and a stopped one holds its output high as the
// hardware does (V116-F1); and `capture`'s id said back in its header (V116-F3).

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

#include "sdk_standin.h"
#include "pico/stdlib.h"
#include "board.h"
#include "beam.h"
#include "service.h"
#include "panel.h"
#include "power_fsm.h"
#include "safe_state.h"
#include "adc_engine.h"
#include "cal.h"
#include "strobe.h"
#include "config_store.h"
#include "hardware/structs/watchdog.h"
#include "hardware/watchdog.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *test_name;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "%s:%d: %s failed: %s\n", __FILE__, __LINE__, test_name, #c); \
    return false; } } while (0)

// --- time, and the stop -------------------------------------------------------
static uint64_t g_now_us;
static uint64_t g_key_at_us;        // a key arrives at this time (0: never)
static unsigned g_key_polls;
static unsigned g_key_on_poll;      // ...or on this poll (1-based; 0: never)

uint64_t        time_us_64(void) { return g_now_us; }
absolute_time_t get_absolute_time(void) { return g_now_us; }
uint32_t        to_ms_since_boot(absolute_time_t t) { return (uint32_t)(t / 1000u); }
absolute_time_t make_timeout_time_ms(uint32_t ms) { return g_now_us + (uint64_t)ms * 1000u; }
int64_t absolute_time_diff_us(absolute_time_t a, absolute_time_t b) { return (int64_t)(b - a); }
void sleep_ms(uint32_t ms) { g_now_us += (uint64_t)ms * 1000u; }

// The USB console as a byte stream (image 1.16, V115-F1): byte k can be read from
// poll g_in_at[k] on, in order. Until it is all read, it is the only input.
static unsigned char g_in[512];
static unsigned      g_in_at[512];
static size_t        g_in_n, g_in_next;
static void type_in(const char *s, size_t n, unsigned from_poll) {
    for (size_t i = 0; i < n && g_in_n < sizeof g_in; i++) {
        g_in[g_in_n] = (unsigned char)s[i];
        g_in_at[g_in_n++] = from_poll;
    }
}

// ...and a stop: a space typed during the k-th service pass's key poll (1-based;
// 0: none). In a console run nothing but the dump services the board, so that is
// the dump's k-th poll.
static unsigned g_space_at_pass;
static unsigned g_services_seen(void);

int getchar_timeout_us(uint32_t us) {
    (void)us;
    g_key_polls++;
    if (g_in_next < g_in_n && g_key_polls >= g_in_at[g_in_next]) return g_in[g_in_next++];
    if (g_space_at_pass && g_services_seen() + 1u >= g_space_at_pass) {
        g_space_at_pass = 0u;
        return ' ';
    }
    if (g_in_next < g_in_n) return PICO_ERROR_TIMEOUT;
    if (g_key_at_us && g_now_us >= g_key_at_us) { g_key_at_us = 0u; return 'x'; }
    if (g_key_on_poll && g_key_polls == g_key_on_poll) return 'x';
    return PICO_ERROR_TIMEOUT;
}

// --- what service.c drives ------------------------------------------------------
watchdog_hw_t mock_watchdog_hw;
void watchdog_enable(uint32_t ms, bool pause) { (void)ms; (void)pause; }
void watchdog_update(void) { }
static uint32_t g_fault_gen;
uint32_t fault_generation(void) { return g_fault_gen; }
static unsigned g_services;
static unsigned g_fault_at_service;  // a fault latches in this pass (1-based; 0: never)
void power_fsm_step(void) {
    g_services++;
    if (g_fault_at_service && g_services == g_fault_at_service) g_fault_gen++;
}
static unsigned g_services_seen(void) { return g_services; }
void detect_service(void) { }
void shot_step(void) { }
void strobe_live_service(void) { }
void panel_onboard_update(void) { }
void panel_update(void) { }

// --- the rest of the board ------------------------------------------------------
bool        power_rails_ready(void) { return true; }
pstate_t    power_fsm_state(void) { return PS_BENCH_RUNNING; }
const char *power_state_name(pstate_t s) { (void)s; return "BENCH_RUNNING"; }

// The panel: what `panel test` and `panel demo` set, and when.
static int      g_forced = -1, g_pwr = -1, g_rdy = -1;
static unsigned g_panel_writes;
static uint64_t g_last_panel_write_us;
void panel_override(int pwr, int rdy) {
    if (pwr != -2) g_pwr = pwr;
    if (rdy != -2) g_rdy = rdy;
    g_panel_writes++;
    g_last_panel_write_us = g_now_us;
}
void panel_force_pattern(int p) { g_forced = p; g_panel_writes++; g_last_panel_write_us = g_now_us; }
int  panel_forced_pattern(void) { return g_forced; }
const char *panel_pattern_name(panel_pattern_t p) { (void)p; return "pattern"; }
int  panel_pattern_from_name(const char *s) { (void)s; return -1; }
panel_pattern_t panel_pattern_for_state(void) { return PANEL_PAT_RUNNING; }

// The beam's registers: every carrier compare write is logged with its time, and
// the largest share of the period any write ever set (V115-F2).
//
// Image 1.17 (V116-F1): the two beam slices also RUN, and the carrier PIN is traced.
// Time is counted in sysclk ticks (g_tick, carried into g_now_us at 150 a
// microsecond). A running slice counts once every `div` ticks and wraps after TOP,
// and its output is high while the count is below its level. A stopped slice holds
// its count AND its output, as the SDK says the hardware does (pwm_set_enabled());
// a level written while it runs takes effect at its next wrap (double-buffered), one
// written while it is stopped at once. The pin is the slice's output on the PWM
// function and the SIO level on SIO; every change of it goes through pin_note().
// Time moves only where firmware waits on it -- each pwm_get_counter() and
// tight_loop_contents() costs a few ticks -- and in run_ticks(), for a test.
static pwm_hw_t g_pwm;
pwm_hw_t *pwm_hw = &g_pwm;
static unsigned g_level_writes;
static uint64_t g_last_level_write_us;
static double   g_worst_pwm;
static bool     g_gpio_out[48];
static bool     g_gpio_dir[48];
static gpio_function_t g_gpio_fn[48];
static uint64_t g_tick;                    // sysclk ticks since the model began
static unsigned g_tick_frac;               // ticks not yet carried into g_now_us
static uint32_t g_div_acc[12];
static bool     g_slice_hi[12];            // each beam slice's output
static bool     g_cc_pending[12];
static uint32_t g_cc_next[12];
static uint32_t g_period_lo = 0xFFFFFFFFu, g_period_hi;   // the carrier periods set, in ticks
// The carrier pin's trace.
static bool     g_pin;
static uint64_t g_pin_since, g_last_high, g_highs, g_longest_high;
static unsigned g_pin_faults;              // a high followed by less than 3x its length low
static uint64_t g_fault_high, g_fault_low;
static bool     g_watch_first;             // record the next high stretch to end...
static uint64_t g_first_high;              // ...here

uint       pwm_gpio_to_slice_num(uint p) { return p >= 32u ? 8u + ((p >> 1) & 3u) : (p >> 1) & 7u; }
uint       pwm_gpio_to_channel(uint p) { return p & 1u; }
pwm_config pwm_get_default_config(void) { pwm_config c = { 0, 0, 0 }; return c; }
void       pwm_config_set_wrap(pwm_config *c, uint16_t w) { c->top = w; }
void       pwm_config_set_clkdiv_int(pwm_config *c, uint d) { c->div = d; }

static uint car_slice(void) { return pwm_gpio_to_slice_num(PIN_MOD_PWM); }
static bool slice_running(uint s) { return (g_pwm.en & (1u << s)) != 0u; }

static bool car_pin_level(void) {
    const uint p = PIN_MOD_PWM;
    if (g_gpio_fn[p] == GPIO_FUNC_PWM) return g_slice_hi[car_slice()];
    if (g_gpio_fn[p] == GPIO_FUNC_SIO) return g_gpio_dir[p] && g_gpio_out[p];
    return false;                                         // undriven: R69 holds it low
}

static void pin_note(void) {
    const bool now = car_pin_level();
    if (now == g_pin) return;
    const uint64_t len = g_tick - g_pin_since;
    if (g_pin) {                                          // a high stretch ended
        g_last_high = len;
        g_highs++;
        if (len > g_longest_high) g_longest_high = len;
        if (g_watch_first) { g_first_high = len; g_watch_first = false; }
    } else if (g_last_high && len < 3u * g_last_high) {   // too little dark after it
        if (!g_pin_faults) { g_fault_high = g_last_high; g_fault_low = len; }
        g_pin_faults++;
    }
    g_pin = now;
    g_pin_since = g_tick;
}

static void trace_reset(void) {
    g_pin = car_pin_level();
    g_pin_since = g_tick;
    g_last_high = g_highs = g_longest_high = 0u;
    g_pin_faults = 0u;
}

static void slice_tick(uint s, uint ch) {
    if (!slice_running(s)) return;                        // stopped: count and output held
    const uint32_t div = g_pwm.slice[s].div ? g_pwm.slice[s].div : 1u;
    if (++g_div_acc[s] < div) return;
    g_div_acc[s] = 0u;
    if (g_pwm.slice[s].ctr >= g_pwm.slice[s].top) {
        g_pwm.slice[s].ctr = 0u;
        if (g_cc_pending[s]) { g_pwm.slice[s].cc = g_cc_next[s]; g_cc_pending[s] = false; }
    } else {
        g_pwm.slice[s].ctr++;
    }
    const uint32_t cc = g_pwm.slice[s].cc;
    g_slice_hi[s] = g_pwm.slice[s].ctr < (ch ? (cc >> 16) : (cc & 0xFFFFu));
}

static void run_ticks(uint64_t n) {
    const uint sc = car_slice(), cc = pwm_gpio_to_channel(PIN_MOD_PWM);
    const uint sd = pwm_gpio_to_slice_num(PIN_DEMOD_PWM), cd = pwm_gpio_to_channel(PIN_DEMOD_PWM);
    while (n--) {
        g_tick++;
        if (++g_tick_frac >= SYSCLK_HZ / 1000000u) { g_tick_frac = 0u; g_now_us++; }
        slice_tick(sc, cc);
        slice_tick(sd, cd);
        pin_note();
    }
}

void       tight_loop_contents(void) { run_ticks(12u); }
uint16_t   pwm_get_counter(uint s) { run_ticks(12u); return (uint16_t)g_pwm.slice[s].ctr; }
void       pwm_init(uint s, const pwm_config *c, bool start) {
    (void)start;
    g_pwm.slice[s].top = c->top;
    g_pwm.slice[s].div = c->div;     // the integer divider (the register holds it << 4)
    g_cc_pending[s] = false;
    if (s == car_slice()) {
        const uint32_t ticks = (c->div ? c->div : 1u) * (c->top + 1u);
        if (ticks < g_period_lo) g_period_lo = ticks;
        if (ticks > g_period_hi) g_period_hi = ticks;
    }
}
void       pwm_set_counter(uint s, uint16_t v) { g_pwm.slice[s].ctr = v; }
void       pwm_set_chan_level(uint s, uint ch, uint16_t lvl) {
    uint32_t cc = g_cc_pending[s] ? g_cc_next[s] : g_pwm.slice[s].cc;
    cc = ch ? ((cc & 0xFFFFu) | ((uint32_t)lvl << 16)) : ((cc & 0xFFFF0000u) | lvl);
    if (slice_running(s)) { g_cc_next[s] = cc; g_cc_pending[s] = true; }   // at the next wrap
    else g_pwm.slice[s].cc = cc;
    if (s == car_slice()) {
        g_level_writes++;
        g_last_level_write_us = g_now_us;
        const double share = (double)lvl / ((double)g_pwm.slice[s].top + 1.0);
        if (share > g_worst_pwm) g_worst_pwm = share;
    }
}
// The SDK's gpio_set_function() writes the pad before the function select, so the
// switch lands some cycles after the call; the slices run meanwhile (the 1.17
// review's note V117-N1 found 1.17's model switched in no time).
void gpio_set_function(uint p, gpio_function_t fn) { run_ticks(40u); g_gpio_fn[p] = fn; pin_note(); }
void gpio_set_dir(uint p, bool out) { g_gpio_dir[p] = out; pin_note(); }
void gpio_put(uint p, bool v) { g_gpio_out[p] = v; pin_note(); }

// What the copied commands print: all of it, up to a full 16384-line dump.
static char   g_out[262144];
static size_t g_out_len;
static int capture_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(g_out + g_out_len, sizeof g_out - g_out_len, fmt, ap);
    va_end(ap);
    if (r > 0) g_out_len += ((size_t)r < sizeof g_out - g_out_len) ? (size_t)r : sizeof g_out - g_out_len - 1u;
    return r;
}
static void out_clear(void) { g_out[0] = '\0'; g_out_len = 0u; }

// --- the ADC capture, for `capture` and `capture trig` (V114-F6) ------------------
static uint16_t g_cap[ADC_CAPTURE_MAX_SAMPLES];
static size_t   g_cap_n;
static unsigned g_cap_mask;
uint32_t adc_capture_rate_for(uint32_t rate_hz) { return rate_hz; }
adc_trig_result_t adc_capture_triggered(unsigned chan, size_t n_samples, uint32_t rate_hz,
                                        uint16_t threshold, float pre_frac, uint32_t timeout_ms,
                                        adc_wait_poll_fn poll, uint16_t *out_base, size_t *out_trig) {
    (void)rate_hz; (void)threshold; (void)pre_frac; (void)timeout_ms; (void)poll;
    g_cap_n = n_samples;
    g_cap_mask = 1u << chan;
    *out_base = 100u;
    *out_trig = n_samples / 4u;
    return ADC_TRIG_OK;                                   // triggered at once
}
size_t adc_capture(unsigned chan_mask, size_t n_samples, uint32_t rate_hz) {
    (void)rate_hz;
    g_cap_mask = chan_mask;
    g_cap_n = n_samples;
    return n_samples;
}
const uint16_t *adc_capture_buffer(void) { return g_cap; }
size_t          adc_capture_count(void) { return g_cap_n; }
unsigned        adc_capture_mask(void) { return g_cap_mask; }
uint32_t        adc_capture_rate(void) { return 500000u; }
size_t          adc_capture_start(void) { return 0u; }
bool            adc_capture_overran(void) { return false; }

// --- `strobe wave`'s record: an inert waveform, nothing fired (V114-F6, V115-F6) ----
static strobe_live_shot_t g_shot;
static uint16_t           g_wave[512];
static size_t             g_wave_n;
const strobe_live_shot_t *strobe_live_last(void) { return &g_shot; }
size_t strobe_live_wave(const uint16_t **samples) { *samples = g_wave; return g_wave_n; }

// --- `cal model`'s point measurement, scripted (V114-F5) --------------------------
static unsigned g_phase_calls;
static unsigned g_stop_in_point;      // this point's sweep stops part-way (1-based; 0: none)
static unsigned g_stop_after_point;   // a stop arrives during this point's last chop, after it
                                      //   measured (the point itself is good)
static unsigned g_reject_mask;        // bit k-1: point k is a rejected sweep
static float    g_point_duty;         // a point leaves the duty here (0: as it found it), as a
                                      //   point that backed it off did in 1.15 (V115-F3)
static bool latch_a_stop(void) {
    g_key_at_us = g_now_us;                               // a key, now
    (void)pitrac_yield_ms(0u);
    return pitrac_abort_pending();
}
bool cal_demod_phase(uint32_t freq_hz, uint32_t n_points, uint32_t cycles, uint32_t chop_hz,
                     cal_demod_t *out) {
    (void)n_points; (void)cycles; (void)chop_hz;
    const unsigned k = ++g_phase_calls;
    *out = (cal_demod_t){0};
    out->freq_hz = freq_hz;
    if (g_point_duty > 0.0f) beam_set_duty(g_point_duty);
    if (k == g_stop_in_point) {                           // as cal.c's abort path does
        if (!latch_a_stop()) return false;
        pitrac_abort_taken();
        out->stopped = true;
        return false;
    }
    if (g_reject_mask & (1u << (k - 1u))) return false;   // a clipped sweep, say
    out->best_ticks = (int32_t)((beam_top() + 1u) / 4u);
    out->peak = 2000.0f;
    out->valid = true;
    if (k == g_stop_after_point) (void)latch_a_stop();
    return true;
}

// `scan carrier`'s two measurements, which cal.c keeps static: a scene that never
// changes. Each takes the time the real one would.
static float chopped_diff(uint32_t cycles, uint32_t chop_hz) {
    (void)pitrac_yield_ms(cycles * 1000u / (chop_hz ? chop_hz : 1u));
    return 800.0f;
}
static float adc5_sigma(uint32_t ms) {
    (void)pitrac_yield_ms(ms);
    return 4.0f;
}

// The line editor's surroundings, for its copy (image 1.16, V115-F1): the command
// table reduced to `capture`, and no live-mode or background notices.
static int capture_putchar(int c) {
    if (g_out_len + 1u < sizeof g_out) { g_out[g_out_len++] = (char)c; g_out[g_out_len] = '\0'; }
    return c;
}
static void cmd_capture(int argc, char **argv);
static void live_exit_notice(void) { }
static void background_notices(void) { }
static void dispatch_command(int argc, char **argv) {
    if (argc == 0) return;
    if (!strcmp(argv[0], "capture")) { cmd_capture(argc, argv); return; }
    capture_printf("? '%s' -- try 'help'\n", argv[0]);
}

#define printf capture_printf
#define putchar capture_putchar
#include "cli_stop_extract.c"
#undef putchar
#undef printf

static bool has(const char *want) { return strstr(g_out, want) != NULL; }

static void reset_fixture(void) {
    g_now_us = 1000000u;
    g_key_at_us = 0u;
    g_key_polls = g_services = 0u;
    g_key_on_poll = g_fault_at_service = 0u;
    g_forced = g_pwr = g_rdy = -1;
    g_panel_writes = g_level_writes = 0u;
    g_phase_calls = g_stop_in_point = g_stop_after_point = g_reject_mask = 0u;
    g_point_duty = 0.0f;
    g_in_n = g_in_next = 0u;
    g_space_at_pass = 0u;
    out_clear();
    pitrac_abort_source(NULL);
    pitrac_abort_clear();
    beam_init();                                          // the ceiling as the board boots with it
    g_level_writes = 0u;
}

// A stop at `at_ms` from now.
static void stop_at(uint32_t at_ms) { g_key_at_us = g_now_us + (uint64_t)at_ms * 1000u; }

static double ms_since(uint64_t t0) { return (double)(g_now_us - t0) / 1000.0; }

// ---------------------------------------------------------------------------

// The review's ramp: 2 % to 6 %, 250 ms steps, a stop at 100 ms. 1.13 wrote
// four more steps and ended at 6 %. Now: the step at 0 ms (3 %) and nothing
// after the stop; the duty stays at 3 %.
static bool ramp_stops(void) {
    reset_fixture();
    beam_configure(104167u, 0.02f, 0);
    CHECK(fabsf(beam_duty() - 0.02f) < 1e-4f);
    g_level_writes = 0u;
    stop_at(100u);
    const uint64_t t0 = g_now_us;
    CHECK(beam_ramp_duty(0.06f, 250u) == YIELD_ABORT_KEY);
    CHECK(ms_since(t0) <= 120.0);                         // within one 20 ms piece of the stop
    CHECK(g_level_writes == 1u && g_last_level_write_us == t0);
    CHECK(fabsf(beam_duty() - 0.03f) < 1e-4f);
    CHECK(pitrac_abort_pending());

    // Down, the same.
    reset_fixture();
    beam_configure(104167u, 0.20f, 0);
    g_level_writes = 0u;
    stop_at(600u);
    CHECK(beam_ramp_duty(0.05f, 250u) == YIELD_ABORT_KEY);
    CHECK(g_level_writes == 3u);                          // 19 % at 0, 18 % at 250, 17 % at 500
    CHECK(fabsf(beam_duty() - 0.17f) < 1e-4f);

    // A 10 s step does not hold a stop for 10 s.
    reset_fixture();
    beam_configure(104167u, 0.02f, 0);
    stop_at(50u);
    const uint64_t t1 = g_now_us;
    CHECK(beam_ramp_duty(0.10f, 10000u) == YIELD_ABORT_KEY);
    CHECK(ms_since(t1) <= 70.0);

    // No stop: to the target.
    reset_fixture();
    beam_configure(104167u, 0.02f, 0);
    CHECK(beam_ramp_duty(0.06f, 250u) == YIELD_OK);
    CHECK(fabsf(beam_duty() - 0.06f) < 1e-4f);
    CHECK(!pitrac_abort_pending());
    return true;
}

// The owner's 25 % (image 1.14), on the PWM's high time since 1.16 (V115-F2):
// refused or held for duty, frequency and ramp targets alike, at every frequency,
// and the ceiling can only be lowered.
static bool beam_ceiling_25(void) {
    reset_fixture();
    CHECK(fabsf(beam_duty_ceiling() - 0.25f) < 1e-6f);
    CHECK(beam_duty_allowed(0.25f) && !beam_duty_allowed(0.2501f) && !beam_duty_allowed(0.50f));
    CHECK(beam_duty_allowed(0.0f) && !beam_duty_allowed(-0.01f));
    beam_set_duty_ceiling(0.35f);
    CHECK(fabsf(beam_duty_ceiling() - 0.25f) < 1e-6f);    // never raised
    beam_set_duty_ceiling(0.20f);
    CHECK(fabsf(beam_duty_ceiling() - 0.20f) < 1e-6f);    // lowered for a session
    CHECK(!beam_duty_allowed(0.21f));
    beam_set_duty_ceiling(BEAM_DUTY_OPERATING);

    beam_configure(104167u, 0.30f, 0);                    // `beam freq` with 30 % held
    CHECK(fabsf(beam_duty() - 0.25f) < 1e-6f && beam_level() == (beam_top() + 1u) / 4u);
    beam_set_duty(0.50f);                                 // anything that writes the duty
    CHECK(fabsf(beam_duty() - 0.25f) < 1e-6f);
    beam_configure(1000u, 0.50f, 0);                      // 1.15's `beam clamp`: held to 25 %
    CHECK(fabsf(beam_duty() - 0.25f) < 1e-6f && beam_level() == 12500u && beam_top() == 49999u);
    beam_configure(104167u, beam_duty(), 0);              // the old trap: back to 104 kHz
    CHECK(beam_level() <= (beam_top() + 1u) / 4u);
    CHECK(beam_ramp_duty(0.50f, 1u) == YIELD_OK && fabsf(beam_duty() - 0.25f) < 1e-6f);
    return true;
}

// `beam sweep`: a stop ends it within 20 ms of a step's dwell (1.13 waited out
// every remaining dwell); a sweep downwards steps evenly.
static bool sweep_stops(void) {
    reset_fixture();
    beam_configure(104167u, 0.10f, 0);
    out_clear();
    stop_at(2500u);
    const uint64_t t0 = g_now_us;
    yield_t why = YIELD_OK;
    CHECK(beam_sweep_steps(5000u, 250000u, 25u, 2000u, &why) == 2u);
    CHECK(why == YIELD_ABORT_KEY);
    CHECK(ms_since(t0) <= 2520.0);
    CHECK(has(" 2/25") && !has(" 3/25"));

    reset_fixture();
    CHECK(beam_sweep_steps(250000u, 5000u, 3u, 0u, &why) == 3u && why == YIELD_OK);
    CHECK(has("127500"));                                 // 1.13: ~2^31 Hz off
    CHECK(has("    5000"));
    return true;
}

static char k_panel[] = "panel", k_demo[] = "demo", k_test[] = "test";

// The review's demo: a stop at 3 s. 1.13 played all six patterns (24 s). Now it
// ends within 20 ms, back to automatic, and says it stopped.
static bool panel_demo_stops(void) {
    reset_fixture();
    char *argv[] = { k_panel, k_demo };
    stop_at(3000u);
    const uint64_t t0 = g_now_us;
    cmd_panel(2, argv);
    CHECK(ms_since(t0) <= 3020.0);
    CHECK(g_forced == -1);                                // automatic again
    CHECK(has("STOPPED") && !has("done --"));
    CHECK(pitrac_abort_pending() && pitrac_abort_was_taken());
    CHECK(g_last_panel_write_us <= g_now_us);

    reset_fixture();
    const uint64_t t1 = g_now_us;
    cmd_panel(2, argv);                                   // no stop: all of it
    CHECK(ms_since(t1) >= 24000.0 && ms_since(t1) <= 24010.0);
    CHECK(g_forced == -1 && has("done --") && !pitrac_abort_was_taken());
    return true;
}

static bool panel_test_stops(void) {
    reset_fixture();
    char *argv[] = { k_panel, k_test };
    stop_at(500u);
    const uint64_t t0 = g_now_us;
    cmd_panel(2, argv);
    CHECK(ms_since(t0) <= 520.0);
    CHECK(g_pwr == -1 && g_rdy == -1);                    // automatic again
    CHECK(has("STOPPED") && pitrac_abort_was_taken());

    reset_fixture();
    cmd_panel(2, argv);
    CHECK(has("done --") && !pitrac_abort_was_taken() && g_pwr == -1);
    return true;
}

// `capture trig`'s wait polls the key and the Pi's abort source (a zero-length
// yield): 1.13's bare pitrac_service() polled neither.
static unsigned g_source_polls;
static bool g_source_says_stop;
static bool source(void) { g_source_polls++; return g_source_says_stop; }

static bool capture_wait_polls(void) {
    reset_fixture();
    pitrac_abort_source(source);
    g_source_polls = 0u;
    g_source_says_stop = false;
    const unsigned keys0 = g_key_polls;
    CHECK(!capture_wait_poll());
    CHECK(g_key_polls == keys0 + 1u && g_source_polls == 1u && g_services == 1u);
    g_source_says_stop = true;                            // the Pi's CLI_ABORT
    CHECK(capture_wait_poll());
    pitrac_abort_clear();
    g_source_says_stop = false;
    stop_at(0u);                                          // a key at USB
    CHECK(capture_wait_poll());
    pitrac_abort_source(NULL);
    return true;
}

// --- V114-F3: the duty at the LED, from the registers ------------------------------
// What the LED gets: the carrier's compare level, TOP and divider as written to
// the PWM block, against a one-shot of width w. Worked out here from the
// registers, not by beam.c.
static double led_duty(double w_us) {
    const uint s = pwm_gpio_to_slice_num(PIN_MOD_PWM), ch = pwm_gpio_to_channel(PIN_MOD_PWM);
    const uint32_t cc = g_pwm.slice[s].cc;
    const double level = (double)(ch ? (cc >> 16) : (cc & 0xFFFFu));
    const double us_per_count = (double)g_pwm.slice[s].div * 1.0e6 / (double)SYSCLK_HZ;
    const double period_us = ((double)g_pwm.slice[s].top + 1.0) * us_per_count;
    double high_us = level * us_per_count;
    if (high_us > w_us) high_us = w_us;
    return high_us / period_us;
}

// --- V115-F2: the 25 % on the PWM's high time ----------------------------------------
// No U9 width at all: U9 can only shorten the PWM's pulse (its ~CLR is the PWM), so
// the LED's duty is at most the PWM's, led_duty() with no clamp. The mock's
// g_worst_pwm is the largest share of the period ANY carrier compare write set,
// so a write that a path makes and undoes still counts.
static bool pwm_within(double ceiling, uint32_t f, float duty, const char *path) {
    if (g_worst_pwm <= ceiling) return true;
    fprintf(stderr, "  %s: %lu Hz, %.2f %% asked: a write of %.6f %% of the period\n",
            path, (unsigned long)f, (double)duty * 100.0, g_worst_pwm * 100.0);
    return false;
}

static bool beam_bound_on_the_pwm(void) {
    reset_fixture();
    // The review's case: 1250 Hz, 50 % asked. 1.15 let it through -- 25.000 % at the
    // LED with a 200 us U9, 25.125 % with a 201 us one. Now 25 % of the period is
    // the most the PWM is high, so neither width can make more of it.
    g_worst_pwm = 0.0;
    beam_configure(1250u, 0.50f, 0);
    CHECK(pwm_within(0.25, 1250u, 0.50f, "the review's case"));
    CHECK(led_duty(1.0e9) <= 0.25 && led_duty(201.0) <= 0.25);
    beam_configure(2045u, 0.50f, 0);                      // 1.14's case
    CHECK(pwm_within(0.25, 2045u, 0.50f, "1.14's case"));
    // 1 Hz to 300 kHz -- 3 Hz steps to 5 kHz, through every divider the plan uses --
    // and every duty in 0.5 % steps, past 100 %: `beam freq`'s path, `beam duty`'s,
    // the chop's on, off and end, and a restore of a setting asking for it. Outside
    // 1-200 kHz the configure is refused (image 1.17), and the rest acts on the
    // setting before it.
    for (uint32_t f = 1u; f <= 300000u; f += (f < 5000u ? 3u : 997u)) {
        for (int k = 0; k <= 220; k++) {
            const float duty = (float)k / 200.0f;
            CHECK(beam_configure(f, duty, 0) == (f >= 1000u && f <= 200000u));
            beam_set_duty(duty);
            beam_chop_begin(duty);
            beam_chop(true);
            beam_chop(false);
            beam_chop(true);
            beam_chop_end();
            beam_setting_t s = beam_setting();
            s.duty = duty;
            beam_restore(&s);
            CHECK(pwm_within(0.25, f, duty, "a write"));
            CHECK(beam_level() <= (beam_top() + 1u) / 4u);
        }
        beam_set_duty(0.20f);
        CHECK(beam_ramp_duty(1.0f, 1u) == YIELD_OK);      // a ramp aimed past it: 5 steps, then held
        CHECK(fabsf(beam_duty() - 0.25f) < 1e-6f);
        CHECK(pwm_within(0.25, f, 1.0f, "beam_ramp_duty"));
    }
    // A session's lower ceiling binds every write the same way.
    beam_set_duty_ceiling(0.10f);
    g_worst_pwm = 0.0;
    for (uint32_t f = 1000u; f <= 200000u; f += 4999u) {
        CHECK(beam_configure(f, 0.25f, 0));
        beam_set_duty(0.25f);
        beam_chop_begin(0.25f);
        beam_chop(true);
        beam_chop_end();
        CHECK(pwm_within(0.10, f, 0.25f, "a lowered ceiling"));
    }
    beam_set_duty_ceiling(BEAM_DUTY_OPERATING);
    // The ceiling lowered under a chop: the chop's next "on" asks for the duty it
    // remembered, and the write itself holds it (car_write()).
    beam_configure(104167u, 0.25f, 0);
    beam_chop_begin(0.25f);
    beam_set_duty_ceiling(0.10f);
    g_worst_pwm = 0.0;
    beam_chop(true);
    CHECK(pwm_within(0.10, 104167u, 0.25f, "a chop under a lowered ceiling"));
    beam_chop_end();
    CHECK(pwm_within(0.10, 104167u, 0.25f, "its end"));
    beam_set_duty_ceiling(BEAM_DUTY_OPERATING);
    // `beam clamp`: 1 kHz at 25 %, 250 us high -- past every clamp measured, so the
    // LED shows U9's width (about 12.3 % with board 1's 122.68 us).
    beam_configure(1000u, BEAM_DUTY_OPERATING, 0);
    CHECK(led_duty(1.0e9) == 0.25 && fabs(led_duty(122.68) - 0.12268) < 1e-6);
    return true;
}

// --- V114-F4: a sweep with no dwell -------------------------------------------------
// 1.14 ran 1000 of 1000 steps with a key waiting, with no key poll and no service
// pass. Each step now polls and services once; a stop or a fault ends it with no
// carrier write after.
static unsigned g_src_polls, g_src_true_at;
static bool src(void) { g_src_polls++; return g_src_true_at && g_src_polls == g_src_true_at; }

static bool sweep_zero_dwell(void) {
    yield_t why = YIELD_OK;
    reset_fixture();                                      // a key already waiting
    beam_configure(104167u, 0.10f, 0);
    g_level_writes = 0u;
    stop_at(0u);
    CHECK(beam_sweep_steps(5000u, 250000u, 1000u, 0u, &why) == 1u && why == YIELD_ABORT_KEY);
    CHECK(g_level_writes == 1u);                          // step 1's, nothing after
    CHECK(g_key_polls == 1u && g_services == 1u);

    reset_fixture();                                      // the Pi's CLI_ABORT, at step 5
    beam_configure(104167u, 0.10f, 0);
    g_level_writes = 0u;
    g_src_polls = 0u;
    g_src_true_at = 5u;
    pitrac_abort_source(src);
    CHECK(beam_sweep_steps(5000u, 250000u, 1000u, 0u, &why) == 5u && why == YIELD_ABORT_KEY);
    CHECK(g_level_writes == 5u);
    pitrac_abort_source(NULL);

    reset_fixture();                                      // a fault latched in step 3's pass
    beam_configure(104167u, 0.10f, 0);
    g_level_writes = 0u;
    g_fault_at_service = 3u;
    CHECK(beam_sweep_steps(5000u, 250000u, 1000u, 0u, &why) == 3u && why == YIELD_ABORT_FAULT);
    CHECK(g_level_writes == 3u);

    reset_fixture();                                      // no stop: all of it, each step polled
    CHECK(beam_sweep_steps(5000u, 250000u, 1000u, 0u, &why) == 1000u && why == YIELD_OK);
    CHECK(g_key_polls == 1000u && g_services == 1000u);
    return true;
}

// --- V114-F5: `cal model` stopped part-way --------------------------------------------
// The review's case: three good points, then a stop inside the fourth point's
// sweep. 1.14 took it for a rejected point, measured the fifth, and published a
// three-point model with the stop marked taken.
static bool model_held(const cal_phase_model_t *m) {
    return m->valid && m->a0 == 1.25f && m->n_points == 7u;
}

static bool cal_model_stops(void) {
    reset_fixture();
    beam_configure(104167u, 0.25f, 333);
    const uint32_t f_before = beam_actual_freq_hz();
    const int32_t ph_before = beam_phase_ticks();
    cal_phase_model_t m = { .a0 = 1.25f, .n_points = 7u, .valid = true };   // the model held
    g_stop_in_point = 4u;
    CHECK(!cal_demod_model(80000u, 200000u, 5u, &m));
    CHECK(g_phase_calls == 4u);                           // the fifth is never measured
    CHECK(model_held(&m));                                // nothing published
    CHECK(pitrac_abort_was_taken());
    CHECK(beam_actual_freq_hz() == f_before && beam_phase_ticks() == ph_before);
    CHECK(has("STOPPED at model point 4 of 5"));

    // A stop that lands in a good point's last chop ends the run there too.
    reset_fixture();
    beam_configure(104167u, 0.25f, 333);
    m = (cal_phase_model_t){ .a0 = 1.25f, .n_points = 7u, .valid = true };
    g_stop_after_point = 3u;
    CHECK(!cal_demod_model(80000u, 200000u, 5u, &m));
    CHECK(g_phase_calls == 3u && model_held(&m) && pitrac_abort_was_taken());

    // A rejected point is not a stop: the run goes on, and the fit of the rest is
    // published.
    reset_fixture();
    beam_configure(104167u, 0.25f, 333);
    m = (cal_phase_model_t){ .a0 = 1.25f, .n_points = 7u, .valid = true };
    g_reject_mask = 1u << 1;                              // point 2
    CHECK(cal_demod_model(80000u, 200000u, 5u, &m));
    CHECK(g_phase_calls == 5u && m.valid && m.n_points == 4u);
    CHECK(m.f_lo == 80000u && m.f_hi == 200000u && !pitrac_abort_pending());

    // Too few good points: false, and the model held stays (1.14 had zeroed it).
    reset_fixture();
    beam_configure(104167u, 0.25f, 333);
    m = (cal_phase_model_t){ .a0 = 1.25f, .n_points = 7u, .valid = true };
    g_reject_mask = 0x7u;                                 // points 1-3
    CHECK(!cal_demod_model(80000u, 200000u, 5u, &m));
    CHECK(g_phase_calls == 5u && model_held(&m));
    return true;
}

// --- V114-F6: a dump stopped while it prints ------------------------------------------
// The lines of the dump that are samples: digits and commas only.
static size_t sample_lines(void) {
    size_t n = 0u;
    for (const char *p = g_out; *p; ) {
        const char *e = strchr(p, '\n');
        if (!e) break;
        bool digits = e > p;
        for (const char *q = p; q < e; q++)
            if (!((*q >= '0' && *q <= '9') || *q == ',')) { digits = false; break; }
        if (digits) n++;
        p = e + 1;
    }
    return n;
}

static char k_capture[] = "capture", k_trig[] = "trig", k_ch5[] = "5";
static char k_mask[] = "0x21", k_count[] = "4096", k_rate[] = "500000";

// `capture trig`: 16384 lines. 1.14 serviced them with a bare pitrac_service():
// a stop went unseen, and the Pi was told it ran to its end.
static bool capture_trig_dump_stops(void) {
    char *argv[] = { k_capture, k_trig, k_ch5 };
    reset_fixture();                                      // whole: every sample, then "# end"
    for (size_t i = 0; i < ADC_CAPTURE_MAX_SAMPLES; i++) g_cap[i] = (uint16_t)(i & 0xFFFu);
    cmd_capture(3, argv);
    CHECK(sample_lines() == ADC_CAPTURE_MAX_SAMPLES);
    CHECK(has("\n# end\n") && !has("PARTIAL") && !pitrac_abort_was_taken());
    CHECK(g_services == 65u);                             // one pass per 256 lines, and the last

    reset_fixture();                                      // the Pi's CLI_ABORT, at the 3rd poll
    g_src_polls = 0u;
    g_src_true_at = 3u;
    pitrac_abort_source(src);
    cmd_capture(3, argv);
    pitrac_abort_source(NULL);
    CHECK(has("# end PARTIAL -- stopped after 513 of 16384 samples\n"));
    CHECK(sample_lines() == 513u && !has("\n# end\n") && pitrac_abort_was_taken());

    reset_fixture();                                      // a key at USB, at the 2nd
    g_key_on_poll = 2u;
    cmd_capture(3, argv);
    CHECK(has("# end PARTIAL -- stopped after 257 of 16384 samples\n"));
    CHECK(sample_lines() == 257u && pitrac_abort_was_taken());
    return true;
}

// Plain `capture`, two channels: the same.
static bool capture_dump_stops(void) {
    char *argv[] = { k_capture, k_mask, k_count, k_rate };
    reset_fixture();
    cmd_capture(4, argv);
    CHECK(sample_lines() == 2048u && has("\n# end\n") && !has("PARTIAL"));
    reset_fixture();
    g_key_on_poll = 2u;
    cmd_capture(4, argv);
    CHECK(has("# end PARTIAL -- stopped after 258 of 4096 samples\n"));
    CHECK(sample_lines() == 129u && pitrac_abort_was_taken());
    return true;
}

// --- V115-F6: a stop on a dump's last line ---------------------------------------------
// The review's case: one channel, 257 samples, a stop at the poll after line 257.
// 1.15 printed all 257, then "# end PARTIAL -- stopped after 257 of 257" and marked
// the stop taken: scope.py dropped a whole record and the Pi read "stopped early".
static char k_mask20[] = "0x20", k_257[] = "257", k_514[] = "514";

static bool capture_stop_on_the_last_line(void) {
    char *one[] = { k_capture, k_mask20, k_257, k_rate };
    reset_fixture();                                      // polls after lines 1 and 257
    g_key_on_poll = 2u;
    cmd_capture(4, one);
    CHECK(sample_lines() == 257u && has("\n# end\n") && !has("PARTIAL"));
    CHECK(has("# end\n(a stop came after the last sample: the record above is whole)\n"));
    CHECK(pitrac_abort_pending() && !pitrac_abort_was_taken());   // asked: "ran to its end"
    CHECK(g_key_polls == 2u);

    reset_fixture();                                      // after line 1, 256 to come: cut
    g_key_on_poll = 1u;
    cmd_capture(4, one);
    CHECK(has("# end PARTIAL -- stopped after 1 of 257 samples\n") && sample_lines() == 1u);
    CHECK(!has("\n# end\n") && pitrac_abort_was_taken());

    reset_fixture();                                      // no stop: no note
    cmd_capture(4, one);
    CHECK(sample_lines() == 257u && has("\n# end\n") && !has("a stop came"));

    char *two[] = { k_capture, k_mask, k_514, k_rate };   // two channels: lines 1, 129, 257
    reset_fixture();
    g_key_on_poll = 3u;
    cmd_capture(4, two);
    CHECK(sample_lines() == 257u && has("\n# end\n") && !pitrac_abort_was_taken());
    reset_fixture();
    g_key_on_poll = 2u;
    cmd_capture(4, two);
    CHECK(has("# end PARTIAL -- stopped after 258 of 514 samples\n"));

    char *trig[] = { k_capture, k_trig, k_ch5 };          // `capture trig`: the 65th poll is the last
    reset_fixture();
    g_key_on_poll = 65u;
    cmd_capture(3, trig);
    CHECK(sample_lines() == ADC_CAPTURE_MAX_SAMPLES && has("\n# end\n") && !pitrac_abort_was_taken());
    reset_fixture();
    g_key_on_poll = 64u;                                  // after line 16129: still cut
    cmd_capture(3, trig);
    CHECK(has("# end PARTIAL -- stopped after 16129 of 16384 samples\n") && pitrac_abort_was_taken());
    return true;
}

// `strobe wave`: the same, every 32 lines and on the last. The 1.15 review's inert
// 512-sample fixture: a stop at the second poll printed 33 lines (and 1.15's M40
// mutation, a bare service pass there, printed all 512).
static bool strobe_wave_dump_stops(void) {
    reset_fixture();
    g_shot.valid = true;
    g_wave_n = 512u;
    for (size_t i = 0; i < g_wave_n; i++) g_wave[i] = (uint16_t)(1000u + i);
    cmd_strobe_wave();
    CHECK(sample_lines() == 512u && has("\n# end\n") && g_services == 17u);   // 16, and the last
    CHECK(has("# capture mask=0x01 n=512 rate=500000"));
    reset_fixture();
    g_key_on_poll = 2u;
    cmd_strobe_wave();
    CHECK(has("# end PARTIAL -- stopped after 33 of 512 samples\n") && sample_lines() == 33u);
    CHECK(pitrac_abort_was_taken());
    reset_fixture();
    g_key_on_poll = 17u;                                  // on the last line: too late, whole
    cmd_strobe_wave();
    CHECK(sample_lines() == 512u && has("\n# end\n") && !pitrac_abort_was_taken());
    CHECK(has("a stop came after the last sample"));
    g_wave_n = 0u;
    return true;
}

// --- V115-N1: `capture` under the watchdog ---------------------------------------------
// Its acquisition is not serviced, so with the watchdog armed (1 s) a long one is
// refused before anything runs; a short one runs. Unarmed, a long one runs and says
// that only its print-out can be stopped.
static char k_16384[] = "16384", k_733[] = "733", k_200[] = "200";

static bool capture_refused_under_watchdog(void) {
    char *slow[] = { k_capture, k_mask20, k_16384, k_733 };   // 22.4 s
    char *fast[] = { k_capture, k_mask20, k_200, k_rate };    // 0.4 ms
    reset_fixture();
    g_cap_n = 0u;
    pitrac_watchdog_enable(true);
    cmd_capture(4, slow);
    CHECK(has("REFUSED: 16384 samples at 733 Hz take 22352 ms to acquire") && g_cap_n == 0u);
    CHECK(sample_lines() == 0u && !has("# capture"));
    out_clear();
    cmd_capture(4, fast);
    CHECK(sample_lines() == 200u && has("\n# end\n") && !has("REFUSED"));
    pitrac_watchdog_enable(false);
    out_clear();
    cmd_capture(4, slow);
    CHECK(has("acquiring for 22.4 s: a stop is seen only once the print-out starts.\n"));
    CHECK(sample_lines() == 16384u && has("\n# end\n"));
    return true;
}

// --- V115-F3: the carrier put back, register for register -------------------------------
// The review's case, inside the new ceiling: 1021 Hz asked -- it lands on 1020.99 Hz,
// which reads back as 1020 and plans another TOP (48971, then 49019) -- at 25 %,
// phase 333, and every point leaves the duty at 5 %, as a point that backed it off
// did in 1.15. Every way out of `cal model` and `scan carrier` puts back the same
// registers.
typedef struct { uint32_t div, ctr, cc, top; } slice_regs_t;
static slice_regs_t g_car0, g_dem0;
static slice_regs_t regs_of(uint s) {
    const slice_regs_t r = { g_pwm.slice[s].div, g_pwm.slice[s].ctr, g_pwm.slice[s].cc, g_pwm.slice[s].top };
    return r;
}
static bool regs_eq(slice_regs_t a, slice_regs_t b) {
    return a.div == b.div && a.ctr == b.ctr && a.cc == b.cc && a.top == b.top;
}
static void carrier_1021(void) {
    beam_configure(1021u, 0.25f, 333);
    g_car0 = regs_of(pwm_gpio_to_slice_num(PIN_MOD_PWM));
    g_dem0 = regs_of(pwm_gpio_to_slice_num(PIN_DEMOD_PWM));
}
static bool carrier_as_before(void) {
    return regs_eq(regs_of(pwm_gpio_to_slice_num(PIN_MOD_PWM)), g_car0) &&
           regs_eq(regs_of(pwm_gpio_to_slice_num(PIN_DEMOD_PWM)), g_dem0) &&
           beam_top() == 48971u && beam_clkdiv() == 3u && beam_duty() == 0.25f &&
           beam_phase_ticks() == 333 && beam_level() == 12243u;
}

static bool carrier_put_back(void) {
    static const struct { unsigned stop_in, stop_after, reject; bool ok; } runs[] = {
        { 1u, 0u, 0u,   false },     // the review's: stopped in the first point
        { 0u, 3u, 0u,   false },     // stopped after a good point
        { 0u, 0u, 0u,   true  },     // the whole run: a fit published
        { 0u, 0u, 0x7u, false },     // too few good points
    };
    for (size_t k = 0; k < sizeof runs / sizeof runs[0]; k++) {
        reset_fixture();
        carrier_1021();
        CHECK(beam_top() == 48971u && beam_actual_freq_hz() == 1020u);   // it rounds
        g_point_duty = 0.05f;
        g_stop_in_point = runs[k].stop_in;
        g_stop_after_point = runs[k].stop_after;
        g_reject_mask = runs[k].reject;
        cal_phase_model_t m = { .a0 = 1.25f, .n_points = 7u, .valid = true };
        CHECK(cal_demod_model(80000u, 200000u, 5u, &m) == runs[k].ok);
        CHECK(g_phase_calls >= 1u && fabsf(g_point_duty - 0.05f) < 1e-6f);
        if (!carrier_as_before()) {
            fprintf(stderr, "  cal model run %u: TOP %lu div %lu duty %.4f phase %ld level %lu\n",
                    (unsigned)k, (unsigned long)beam_top(), (unsigned long)beam_clkdiv(),
                    (double)beam_duty(), (long)beam_phase_ticks(), (unsigned long)beam_level());
            return false;
        }
    }

    // `scan carrier`, forced cold: stopped part-way, and run to its end.
    cal_scan_point_t best;
    reset_fixture();
    carrier_1021();
    stop_at(20000u);                                      // in the second (15.6 s each)
    CHECK(!cal_scan_carrier(90000u, 120000u, 4u, NULL, true, &best));
    CHECK(has("# ABORTED after 2 of 4 candidates.") && carrier_as_before());
    reset_fixture();
    carrier_1021();
    CHECK(cal_scan_carrier(90000u, 120000u, 4u, NULL, true, &best));
    CHECK(has("Carrier restored to 1020 Hz, phase 333 ticks.") && carrier_as_before());
    return true;
}

// --- V116-F1: the carrier's range, on every path ------------------------------------------
// 1.16 let four commands put the carrier anywhere from 1 Hz to 75 MHz. Now beam.c
// refuses any period outside 1-200 kHz, whoever asks, and changes nothing: the
// frequency path, a restore of a setting, a sweep across and past the range, and
// `cal model` and `scan carrier` asked outside it.
static bool beam_carrier_range(void) {
    reset_fixture();
    CHECK(!beam_freq_allowed(0u) && !beam_freq_allowed(999u) && beam_freq_allowed(1000u));
    CHECK(beam_freq_allowed(200000u) && !beam_freq_allowed(200001u));
    CHECK(!beam_freq_allowed(250000u) && !beam_freq_allowed(75000000u));
    CHECK(beam_configure(1000u, 0.25f, 0) && beam_top() == 49999u && beam_clkdiv() == 3u);
    CHECK(beam_configure(200000u, 0.25f, 0) && beam_top() == 749u && beam_clkdiv() == 1u);

    // Refused: not the setting, not one register.
    CHECK(beam_configure(104167u, 0.20f, 77));
    const beam_setting_t was = beam_setting();
    const slice_regs_t car = regs_of(car_slice());
    const slice_regs_t dem = regs_of(pwm_gpio_to_slice_num(PIN_DEMOD_PWM));
    const unsigned writes = g_level_writes;
    static const uint32_t bad_f[] = { 0u, 1u, 999u, 200001u, 250000u, 15000000u, 75000000u };
    for (size_t i = 0; i < sizeof bad_f / sizeof bad_f[0]; i++)
        CHECK(!beam_configure(bad_f[i], 0.25f, 0));
    static const beam_setting_t bad_s[] = {
        { 599u, 1u, 0.25f, 0 },          // 250 kHz
        { 748u, 1u, 0.25f, 0 },          // 200.27 kHz: one count short
        { 49999u, 4u, 0.25f, 0 },        // 750 Hz
        { 65535u, 255u, 0.25f, 0 },      // 9 Hz
        { 1439u, 0u, 0.25f, 0 },         // no divider
        { 1439u, 256u, 0.25f, 0 },       // a divider the register cannot hold
        { 70000u, 1u, 0.25f, 0 },        // a TOP it cannot hold
    };
    for (size_t i = 0; i < sizeof bad_s / sizeof bad_s[0]; i++)
        CHECK(!beam_restore(&bad_s[i]));
    const beam_setting_t now = beam_setting();
    CHECK(now.top == was.top && now.div == was.div && now.duty == was.duty && now.phase == was.phase);
    CHECK(regs_eq(regs_of(car_slice()), car));
    CHECK(regs_eq(regs_of(pwm_gpio_to_slice_num(PIN_DEMOD_PWM)), dem));
    CHECK(g_level_writes == writes);
    CHECK(beam_restore(&was));                            // and back inside: taken

    // Every period any path set, from here on: `beam sweep`'s steps from 1 Hz to 75 MHz
    // (its command refuses these ends; its steps are what beam.c is handed).
    g_period_lo = 0xFFFFFFFFu;
    g_period_hi = 0u;
    yield_t why;
    (void)beam_sweep_steps(1u, 400000u, 41u, 0u, &why);   // 1 Hz, then 10 kHz steps to 400 kHz
    (void)beam_sweep_steps(75000000u, 1u, 40u, 0u, &why);
    CHECK(g_period_lo >= BEAM_PERIOD_MIN_TICKS && g_period_hi <= BEAM_PERIOD_MAX_TICKS);
    CHECK(g_period_lo == 750u && g_period_hi == 15000u);  // 200 and 10 kHz: the steps inside ran

    // `cal model` and `scan carrier` asked outside: refused before a point is measured.
    cal_phase_model_t m = { .valid = false };
    g_phase_calls = 0u;
    out_clear();
    CHECK(!cal_demod_model(150000u, 250000u, 5u, &m) && g_phase_calls == 0u);
    CHECK(has("REFUSED: 150000..250000 Hz leaves the carrier's 1000-200000 Hz."));
    cal_scan_point_t best;
    out_clear();
    CHECK(!cal_scan_carrier(500u, 100000u, 4u, NULL, true, &best));
    CHECK(has("REFUSED: 500..100000 Hz leaves the carrier's") && !has("f_hz,"));
    return true;
}

// --- V116-F1: the pin through a reconfiguration -------------------------------------------
// Every change made while the beam runs, each in the MIDDLE of a pulse: 1.16 stopped
// the slices there with the pin still on the PWM, which held it high, and restarted
// at count 0, the start of a pulse -- a cut pulse ran on into a whole new one. The
// trace checks every high stretch on the pin is followed by at least three times its
// length low (25 %), that the first pulse after a change is no longer than the level,
// and that the beam pulses again after each change.
static bool mid_pulse(void) {
    for (uint64_t k = 0; k < 400000u; k++) {
        if (g_pin && g_gpio_fn[PIN_MOD_PWM] == GPIO_FUNC_PWM &&
            g_pwm.slice[car_slice()].ctr >= beam_level() / 2u) return true;
        run_ticks(1u);
    }
    return false;
}

static bool pin_clean(const char *what) {
    if (!g_pin_faults) return true;
    fprintf(stderr, "  %s: a high of %llu ticks, then only %llu low\n", what,
            (unsigned long long)g_fault_high, (unsigned long long)g_fault_low);
    return false;
}

// Called just after a change returns: runs `periods` carrier periods and says whether
// the pin pulsed in them -- and, with `bounded`, whether the first pulse to end was
// no longer than the new level. It may be shorter: the pin rejoins the running
// carrier wherever its period is (image 1.18, review note V117-N1; 1.17 claimed a
// whole first pulse, which the time the switch takes can break). A live duty or chop
// write is not bounded this way: the pulse running when it is written ends at the
// old level, as the hardware's double-buffered compare does.
static bool pulses_after(uint32_t periods, bool bounded) {
    const uint64_t h0 = g_highs;
    const uint64_t level_ticks = (uint64_t)beam_level() * beam_clkdiv();
    g_watch_first = true;
    run_ticks((uint64_t)periods * beam_clkdiv() * (beam_top() + 1u));
    if (bounded && g_first_high > level_ticks) {
        fprintf(stderr, "  the first pulse after the change: %llu ticks, over the level's %llu\n",
                (unsigned long long)g_first_high, (unsigned long long)level_ticks);
        return false;
    }
    return g_highs >= h0 + periods / 2u;
}

static bool beam_pin_never_stretched(void) {
    reset_fixture();
    beam_enable(false);
    CHECK(beam_configure(104167u, 0.25f, 0));
    CHECK(beam_enable(true) && g_gpio_fn[PIN_MOD_PWM] == GPIO_FUNC_PWM);
    trace_reset();
    CHECK(pulses_after(20u, true) && g_longest_high == 360u && pin_clean("running"));
    const beam_setting_t at_104k = beam_setting();

    CHECK(mid_pulse());
    beam_set_phase(100);
    CHECK(pulses_after(20u, true) && pin_clean("a phase"));
    CHECK(mid_pulse());
    CHECK(beam_configure(200000u, 0.25f, 0));
    CHECK(pulses_after(20u, true) && pin_clean("up to 200 kHz"));
    CHECK(mid_pulse());
    CHECK(beam_configure(1000u, 0.25f, 0));
    CHECK(pulses_after(4u, true) && pin_clean("down to 1 kHz"));
    CHECK(mid_pulse());
    CHECK(beam_configure(200000u, 0.25f, 0));             // a 250 us pulse cut: 750 us dark at least
    CHECK(pulses_after(20u, true) && pin_clean("1 kHz up to 200 kHz"));
    CHECK(mid_pulse());
    CHECK(beam_restore(&at_104k));
    CHECK(pulses_after(20u, true) && pin_clean("a restore"));
    CHECK(mid_pulse());
    beam_enable(false);
    CHECK(!g_pin);                                        // low at once
    CHECK(beam_enable(true));                             // and straight back on
    CHECK(pulses_after(20u, true) && pin_clean("off and on"));
    CHECK(mid_pulse());
    beam_set_phase(700);
    beam_set_phase(5);                                    // two in a row
    CHECK(pulses_after(20u, true) && pin_clean("two phases"));
    CHECK(mid_pulse());
    beam_set_duty(0.10f);                                 // live: at the next wrap
    CHECK(pulses_after(20u, false) && pin_clean("a duty"));
    beam_chop_begin(0.25f);
    CHECK(mid_pulse());
    beam_chop(false);
    run_ticks(5u * 1440u);
    beam_chop(true);
    beam_chop_end();
    CHECK(pulses_after(20u, false) && pin_clean("a chop"));
    CHECK(g_gpio_fn[PIN_MOD_PWM] == GPIO_FUNC_PWM);

    // Off for good: low at once, and no pulse after.
    CHECK(mid_pulse());
    const uint64_t h = g_highs;
    beam_enable(false);
    CHECK(!g_pin);
    run_ticks(10u * 1440u);
    CHECK(!g_pin && g_highs == h + 1u && pin_clean("off"));   // the cut one, and no other
    return true;
}

static void fill_pattern(void);
static void console_until_read(void);

// --- V116-F3: `capture ... id=<n>` says the id back -----------------------------------------
static char k_id[] = "id=0x1a2b3c4d", k_idbad[] = "id=zz";

static bool capture_says_its_id(void) {
    char *plain[] = { k_capture, k_mask20, k_257, k_rate, k_id };
    reset_fixture();
    cmd_capture(5, plain);
    CHECK(has("# capture mask=0x20 n=257 rate=500000 overran=0 id=1a2b3c4d\n# columns: ch5\n"));
    CHECK(sample_lines() == 257u && has("\n# end\n"));
    char *none[] = { k_capture, k_mask20, k_257, k_rate };   // none asked: none printed
    reset_fixture();
    cmd_capture(4, none);
    CHECK(has("# capture mask=0x20 n=257 rate=500000 overran=0\n# columns: ch5\n"));
    char *trig[] = { k_capture, k_trig, k_ch5, k_id };
    reset_fixture();
    cmd_capture(4, trig);
    CHECK(has(" trig=4096 base=100 id=1a2b3c4d\n# columns: ch5\n"));
    char *bad[] = { k_capture, k_mask20, k_257, k_rate, k_idbad };
    reset_fixture();
    g_cap_n = 0u;
    cmd_capture(5, bad);
    CHECK(has("ERR: id 'zz'") && g_cap_n == 0u && !has("# capture"));
    // Through the console, as scope.py sends them: the longest is eight words.
    static const struct { const char *line, *want; } lines[] = {
        { "capture 0x20 4 500000 id=0x1a2b3c4d\r",
          "capture 0x20 4 500000 id=0x1a2b3c4d\n# capture mask=0x20 n=4 rate=500000 overran=0 id=1a2b3c4d\n" },
        { "capture trig 5 80 500000 25 30 id=0x0000002a\r",
          " trig=4096 base=100 id=0000002a\n" },
    };
    for (size_t i = 0; i < sizeof lines / sizeof lines[0]; i++) {
        reset_fixture();
        fill_pattern();
        type_in(lines[i].line, strlen(lines[i].line), 0u);
        console_until_read();
        CHECK(has(lines[i].want) && has("\n# end\n"));
    }
    return true;
}

// --- V117-N2: a saved carrier comes back as it was saved ----------------------------------
// config_store.c's own cfg_apply_beam() and s_cfg, copied out. 1.17's boot guard took
// 5-250 kHz while the beam takes 1-200 kHz: the review's saved 1250 Hz, phase 333
// came back as 104166 Hz, phase 0. Now its range is the beam's own.
static bool cfg_carrier_comes_back(void) {
    static const struct { uint32_t hz, top, div; } good[] = {
        { 1250u, 59999u, 2u }, { 1000u, 49999u, 3u }, { 4999u, 30005u, 1u },   // 1.17 took neither
        { 104166u, 1439u, 1u }, { 200000u, 749u, 1u },
    };
    for (size_t i = 0; i < sizeof good / sizeof good[0]; i++) {
        reset_fixture();
        s_cfg.carrier_hz = good[i].hz;
        s_cfg.demod_phase_ticks = 333;
        cfg_apply_beam();
        CHECK(beam_actual_freq_hz() == good[i].hz && beam_top() == good[i].top);
        CHECK(beam_clkdiv() == good[i].div && beam_phase_ticks() == 333);
    }
    // Outside the range: the carrier stays as it booted, phase and all.
    static const uint32_t bad[] = { 0u, 999u, 200001u, 250000u, 75000000u };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        reset_fixture();
        const beam_setting_t booted = beam_setting();
        s_cfg.carrier_hz = bad[i];
        s_cfg.demod_phase_ticks = 333;
        cfg_apply_beam();
        const beam_setting_t now = beam_setting();
        CHECK(now.top == booted.top && now.div == booted.div && now.phase == booted.phase);
    }
    return true;
}

// --- V115-F1: the USB console, end to end -------------------------------------------------
// cli.c's own line editor, dispatch and `capture`, and service.c's key poll, fed the
// bytes a client writes. 1.15 read a CR LF client's LF as a stop: the line editor
// runs the command on the CR, and the dump's first key poll found the LF. scope.py
// wrote CR LF, and its 4096-sample capture ended after one sample with no key
// pressed. tools/tests/test_scope.py runs scope.py's own reader against this code
// through `stop_test console` (below).
static const char k_scope_cmd[] = "capture 0x20 4096 500000";

static void fill_pattern(void) {
    for (size_t i = 0; i < ADC_CAPTURE_MAX_SAMPLES; i++) g_cap[i] = (uint16_t)(i & 0xFFFu);
}

static void console_until_read(void) {
    for (int k = 0; k < 1000 && g_in_next < g_in_n; k++) cli_service();
    cli_service();
}

static bool console_whole_capture(void) {
    const unsigned n = (unsigned)strlen(k_scope_cmd);
    static const struct { const char *end; unsigned lf_late; const char *who; } clients[] = {
        { "\r",   0u, "scope.py, CR" },
        { "\r\n", 0u, "a CR LF client" },
        { "\n",   0u, "an LF client" },
        { "\r\n", 1u, "CR LF, the LF read at the dump's second poll" },
    };
    for (size_t k = 0; k < sizeof clients / sizeof clients[0]; k++) {
        reset_fixture();
        fill_pattern();
        type_in(k_scope_cmd, n, 0u);
        type_in(clients[k].end, 1u, 0u);
        // Polls 1..n read the command, n+1 the CR; the dump polls at n+2, then n+3.
        if (clients[k].end[1]) type_in("\n", 1u, clients[k].lf_late ? n + 3u : 0u);
        console_until_read();
        if (sample_lines() != 4096u || !has("\n# end\n") || has("PARTIAL") ||
            pitrac_abort_pending() || g_in_next != g_in_n) {
            fprintf(stderr, "  %s: %u sample lines, %s\n", clients[k].who,
                    (unsigned)sample_lines(), has("PARTIAL") ? "PARTIAL" : "no PARTIAL");
            return false;
        }
        CHECK(has("capture 0x20 4096 500000\n# capture mask=0x20 n=4096"));   // echo, then the block
    }
    return true;
}

// A genuine stop -- a key typed while the dump runs, after a CR LF -- still stops it.
static bool console_stop_after_the_command(void) {
    reset_fixture();
    fill_pattern();
    type_in(k_scope_cmd, strlen(k_scope_cmd), 0u);
    type_in("\r\n", 2u, 0u);
    g_space_at_pass = 3u;                                 // the dump's third poll
    console_until_read();
    CHECK(has("# end PARTIAL -- stopped after 513 of 4096 samples\n") && sample_lines() == 513u);
    CHECK(pitrac_abort_was_taken());
    reset_fixture();                                      // a CR as the stop: its LF goes with it
    fill_pattern();
    type_in(k_scope_cmd, strlen(k_scope_cmd), 0u);
    type_in("\r", 1u, 0u);
    type_in("\r\n", 2u, (unsigned)strlen(k_scope_cmd) + 3u);
    console_until_read();
    CHECK(has("# end PARTIAL -- stopped after 257 of 4096 samples\n") && g_in_next == g_in_n);
    CHECK(!has("> \n"));                                  // and no empty line ran after it
    return true;
}

// `stop_test console [k]`, for tools/tests/test_scope.py: stdin is every byte the
// client wrote, all there from the start; with k, a space is typed at the dump's
// k-th poll; stdout is everything the board printed. The capture buffer holds
// sample i = i mod 4096.
static int console_pipe(int argc, char **argv) {
#if defined(_WIN32)
    (void)_setmode(_fileno(stdin), _O_BINARY);
    (void)_setmode(_fileno(stdout), _O_BINARY);
#endif
    reset_fixture();
    fill_pattern();
    static char buf[sizeof g_in];
    const size_t n = fread(buf, 1u, sizeof buf, stdin);
    type_in(buf, n, 0u);
    if (argc > 2) g_space_at_pass = (unsigned)strtoul(argv[2], NULL, 10);
    console_until_read();
    fwrite(g_out, 1u, g_out_len, stdout);
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "console")) return console_pipe(argc, argv);
    static const struct { const char *name; bool (*run)(void); } cases[] = {
#define TEST(n) { #n, n }
        TEST(ramp_stops), TEST(beam_ceiling_25), TEST(sweep_stops), TEST(panel_demo_stops),
        TEST(panel_test_stops), TEST(capture_wait_polls), TEST(beam_bound_on_the_pwm),
        TEST(sweep_zero_dwell), TEST(cal_model_stops), TEST(capture_trig_dump_stops),
        TEST(capture_dump_stops), TEST(capture_stop_on_the_last_line),
        TEST(capture_refused_under_watchdog), TEST(carrier_put_back),
        TEST(console_whole_capture), TEST(console_stop_after_the_command),
        TEST(strobe_wave_dump_stops), TEST(beam_carrier_range), TEST(beam_pin_never_stretched),
        TEST(capture_says_its_id), TEST(cfg_carrier_comes_back),
#undef TEST
    };
    if (argc == 2) {
        for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
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
