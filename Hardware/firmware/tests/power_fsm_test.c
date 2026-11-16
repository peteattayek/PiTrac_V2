// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
#include "power_fsm.h"
#include "board.h"
#include "safe_state.h"
#include "adc_engine.h"
#include "beam.h"
#include "detect.h"
#include "strobe.h"
#include "camera.h"
#include "telemetry.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool pins[48];
static uint32_t clock_ms;
static float supply_v;
static adc_5vin_status_t supply_status;
static bool beam_on;
static fault_t fault;
static unsigned latch_rises;
static unsigned strobe_offs;
static unsigned camera_offs;
static pi_line_t line_mode;        // J8.37, as the FSM last set it
static unsigned  line_asserts;     // transitions into ASSERTED
static bool      uart_on;          // UART1 on GPIO36/37, as the FSM last set it
static unsigned  uart_connects;    // transitions into connected
static const char *test_name;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s failed: %s (state %s, latch %d)\n", \
                __FILE__, __LINE__, test_name, #condition, \
                power_state_name(power_fsm_state()), pins[PIN_LATCH_CONTROL]); \
        return false; \
    } \
} while (0)

// --- Reset injection (independent review 2026-10-19, F2) ----------------------
// A reset can land between any two things the firmware does. Every mock below,
// and every word a telemetry mirror commit stores (telemetry_test_store_hook),
// is an "event". With a reset armed at event k, event() does what an RP2354
// reset does to this board -- R12 opens the latch -- and jumps back to the test,
// which then boots the way main() does (telemetry_init()) and checks the counts.
static jmp_buf  s_reset_jmp;
static unsigned s_events;
static unsigned s_reset_at;          // 0 = no reset armed

static void event(void) {
    if (s_reset_at == 0u || ++s_events != s_reset_at) return;
    s_reset_at = 0u;
    telemetry_test_store_hook = NULL;
    pins[PIN_LATCH_CONTROL] = false;
    longjmp(s_reset_jmp, 1);
}

static void arm_reset(unsigned at) {
    s_events = 0u;
    s_reset_at = at;
    telemetry_test_store_hook = event;
}

static void disarm_reset(void) {
    s_reset_at = 0u;
    telemetry_test_store_hook = NULL;
}

absolute_time_t get_absolute_time(void) { return (uint64_t)clock_ms * 1000u; }
uint32_t to_ms_since_boot(absolute_time_t time) { return (uint32_t)(time / 1000u); }
bool gpio_get(unsigned pin) { return pins[pin]; }
bool gpio_get_out_level(unsigned pin) { return pins[pin]; }

// J8.37 goes only through safe_state_pi_shutdown_line() since 2026-10-19: a
// bare write to GPIO43 is a test failure, like any other unsafe write.
void gpio_put(unsigned pin, bool value) {
    if (pin >= sizeof(pins) / sizeof(pins[0]) || pin == PIN_PULSE_LIMIT_DIS ||
        pin == PIN_RPI5_SHUTDOWN || pin == PIN_UART_TX || pin == PIN_UART_RX ||
        (pin == PIN_LATCH_CONTROL && !value &&
         (beam_on || pins[PIN_STROBE_PULSE] || pins[PIN_GATE_PWM] || pins[PIN_CAM_TRIGGER] ||
          uart_on))) {
        fprintf(stderr, "Unsafe GPIO write in %s: pin %u, value %d\n",
                test_name, pin, value);
        exit(2);
    }
    event();                            // a reset just before the write...
    if (pin == PIN_LATCH_CONTROL && value && !pins[pin]) latch_rises++;
    pins[pin] = value;
    event();                            // ...or just after it
}

// The FSM decides on the STATUS (review round 5): a stale or unavailable value
// is never a reading. supply_v is what *volts carries either way.
adc_5vin_status_t adc_5vin_read(float *volts) {
    if (volts) *volts = supply_v;
    return supply_status;
}
bool beam_enable(bool on) { event(); beam_on = on; return true; }
void detect_hpf_safe_off(void) { gpio_put(PIN_HPF_TOGGLE, HPF_SEL_TRACK); }
// The real one drives GPIO25 SIO low and zeroes the gate DAC; the FSM must call
// it before the latch drops, which gpio_put() below checks.
void strobe_safe_off(void) {
    event();
    strobe_offs++;
    pins[PIN_STROBE_PULSE] = false;
    pins[PIN_GATE_PWM] = false;
}
// The real one takes GPIO10 back to SIO low and stops the handshake (Phase 7):
// before the latch drops too, which gpio_put() checks.
void camera_safe_off(void) {
    event();
    camera_offs++;
    pins[PIN_CAM_TRIGGER] = false;
}
void fault_raise(fault_t value) { if (fault == FAULT_NONE) fault = value; }
void fault_clear(void) { fault = FAULT_NONE; }
fault_t fault_current(void) { return fault; }

// The pad itself is safe_state.c's; the FSM decides the mode. pins[] keeps the
// level a driven line shows, so the older pulse checks still read it.
void safe_state_pi_shutdown_line(pi_line_t mode) {
    event();
    if (mode == PI_LINE_ASSERTED && line_mode != PI_LINE_ASSERTED) line_asserts++;
    line_mode = mode;
    pins[PIN_RPI5_SHUTDOWN] = mode == PI_LINE_DEASSERTED ? (bool)PI_SHUTDOWN_ACTIVE_LOW
                            : mode == PI_LINE_ASSERTED   ? !PI_SHUTDOWN_ACTIVE_LOW
                            : false;
}

// UART1's pins (safe_state.h): connected or released, nothing else. The latch
// may never drop while they are connected (gpio_put() above).
void safe_state_pi_uart_lines(bool connect) {
    event();
    if (connect && !uart_on) uart_connects++;
    uart_on = connect;
}
bool safe_state_pi_uart_connected(void) { return uart_on; }

// J8.37 after every step (board.h, "THE LINE'S REST STATE"): released whenever
// the latch is open; asserted only in SHUTTING_DOWN; otherwise driven deasserted
// exactly while the Pi's 3V3 reads up. UART1 after every step: connected exactly
// while the latch is closed, the Pi's 3V3 reads up and RPI5_ON is high.
static void check_line(void) {
    const bool latch = pins[PIN_LATCH_CONTROL], v33 = pins[PIN_PI_3V3_SENSE];
    const pstate_t st = power_fsm_state();
    const char *bad = NULL;
    const bool uart_wanted = latch && v33 && pins[PIN_RPI5_ON];
    if (uart_on != uart_wanted)
        bad = uart_on ? "UART1 connected without a Pi up (latch, 3V3 and RPI5_ON)"
                      : "UART1 released with a Pi up";
    else if (!latch && line_mode != PI_LINE_RELEASED)
        bad = "J8.37 driven with the latch open";
    else if (line_mode == PI_LINE_ASSERTED && st != PS_SHUTTING_DOWN)
        bad = "J8.37 asserted outside SHUTTING_DOWN";
    else if (latch && line_mode != PI_LINE_ASSERTED &&
             line_mode != (v33 ? PI_LINE_DEASSERTED : PI_LINE_RELEASED))
        bad = v33 ? "J8.37 not deasserted with a Pi powered" : "J8.37 driven with no Pi 3V3";
    if (bad) {
        fprintf(stderr, "%s: %s (state %s, latch %d, 3v3 %d, mode %d)\n", test_name, bad,
                power_state_name(st), latch, v33, (int)line_mode);
        exit(2);
    }
}

static void reset_fixture(void) {
    memset(pins, 0, sizeof(pins));
    pins[PIN_PWR_TOGGLE] = true;
    line_mode = PI_LINE_ASSERTED;      // power_fsm_init() must release it
    line_asserts = 0;
    uart_on = true;                    // ...and these
    uart_connects = 0;
    clock_ms = 0;
    supply_v = 5.20f;
    supply_status = ADC_5VIN_FRESH;
    beam_on = false;
    fault = FAULT_NONE;
    latch_rises = 0;
    strobe_offs = 0;
    camera_offs = 0;
    telemetry_test_power_on();
    telemetry_init(NULL, TELE_BOOT_POWER_ON);
    power_fsm_init();
    line_asserts = 0;
    uart_connects = 0;
    check_line();
}

static void step(uint32_t elapsed_ms) {
    clock_ms += elapsed_ms;
    power_fsm_step();
    check_line();
}

static bool start_bench(void) {
    power_request_on();
    step(0);
    CHECK(power_fsm_state() == PS_POWERING_ON);
    CHECK(pins[PIN_LATCH_CONTROL]);
    step(PI_DETECT_WINDOW_MS);
    CHECK(power_fsm_state() == PS_BENCH_RUNNING);
    return true;
}

static bool start_pi(void) {
    pins[PIN_PI_3V3_SENSE] = true;
    power_request_on();
    step(0);
    step(RAIL_SETTLE_MS);
    CHECK(power_fsm_state() == PS_PI_BOOTING);
    pins[PIN_RPI5_ON] = true;
    step(1);
    CHECK(power_fsm_state() == PS_RUNNING);
    return true;
}

static bool stays_off(void) {
    const unsigned rises = latch_rises;
    for (unsigned i = 0; i < 60; i++) {
        step(100);
        CHECK(power_fsm_state() == PS_STANDBY);
        CHECK(!pins[PIN_LATCH_CONTROL]);
        CHECK(!beam_on);
        CHECK(pins[PIN_HPF_TOGGLE] == HPF_SEL_TRACK);
        CHECK(!pins[PIN_STROBE_PULSE] && !pins[PIN_GATE_PWM]);
        CHECK(!pins[PIN_CAM_TRIGGER]);
        CHECK(latch_rises == rises);
    }
    return true;
}

static bool stop_bench(void) {
    power_request_shutdown();
    step(1);
    step(1);
    CHECK(stays_off());
    return true;
}

static bool redundant_on_shutdown(void) {
    CHECK(start_bench());
    beam_enable(true);
    pins[PIN_HPF_TOGGLE] = HPF_SEL_HOLD;
    for (unsigned i = 0; i < 3; i++) {
        power_request_on();
        step(1);
    }
    CHECK(stop_bench());
    CHECK(start_bench());
    CHECK(stop_bench());
    return true;
}

static bool redundant_on_forceoff(void) {
    CHECK(start_bench());
    power_request_on();
    step(1);
    power_request_force_off();
    step(1);
    CHECK(stays_off());
    return true;
}

static bool on_during_startup(void) {
    power_request_on();
    step(0);
    power_request_on();
    step(PI_DETECT_WINDOW_MS);
    CHECK(power_fsm_state() == PS_BENCH_RUNNING);
    CHECK(stop_bench());
    return true;
}

static bool on_during_shutdown(void) {
    CHECK(start_pi());
    beam_enable(true);
    power_request_on();
    step(1);
    power_request_shutdown();
    step(1);
    CHECK(power_fsm_state() == PS_SHUTTING_DOWN);
    CHECK(!beam_on);
    CHECK(!pins[PIN_RPI5_SHUTDOWN]);
    power_request_on();
    step(PI_SHUTDOWN_PULSE_MS);
    CHECK(pins[PIN_RPI5_SHUTDOWN]);
    pins[PIN_RPI5_ON] = false;
    step(PI_SHUTDOWN_MIN_HOLDOFF_MS - PI_SHUTDOWN_PULSE_MS - 1);
    CHECK(power_fsm_state() == PS_SHUTTING_DOWN);
    CHECK(pins[PIN_LATCH_CONTROL]);
    step(1);
    step(1);
    CHECK(stays_off());
    return true;
}

static bool on_during_fault(void) {
    supply_v = 4.85f;
    power_request_on();
    step(0);
    CHECK(power_fsm_state() == PS_FAULT);
    CHECK(fault == FAULT_USB_POWER_ONLY);
    power_request_on();
    step(1);
    supply_v = 5.20f;
    power_request_fault_ack();
    step(1);
    step(1);
    CHECK(fault == FAULT_NONE);
    CHECK(stays_off());
    return true;
}

static bool standby_off(void) {
    power_request_shutdown();
    step(1);
    CHECK(start_bench());
    step(100);
    CHECK(power_fsm_state() == PS_BENCH_RUNNING);
    CHECK(stop_bench());
    return true;
}

static bool pending_on_shutdown(void) {
    power_request_on();
    power_request_shutdown();
    step(1);
    CHECK(stays_off());
    CHECK(latch_rises == 0);
    return true;
}

static bool pending_on_forceoff(void) {
    power_request_on();
    power_request_force_off();
    power_request_on();
    step(1);
    CHECK(stays_off());
    CHECK(latch_rises == 0);
    return true;
}

static bool fresh_on_after_cancel(void) {
    power_request_on();
    power_request_shutdown();
    CHECK(start_bench());
    step(1);
    CHECK(power_fsm_state() == PS_BENCH_RUNNING);
    CHECK(stop_bench());
    return true;
}

static bool shutdown_during_startup(void) {
    power_request_on();
    step(0);
    power_request_shutdown();
    step(PI_DETECT_WINDOW_MS);
    CHECK(power_fsm_state() == PS_BENCH_RUNNING);
    step(1);
    step(1);
    CHECK(stays_off());
    return true;
}

static bool shutdown_during_pi_boot(void) {
    pins[PIN_PI_3V3_SENSE] = true;
    power_request_on();
    step(0);
    step(RAIL_SETTLE_MS);
    CHECK(power_fsm_state() == PS_PI_BOOTING);
    power_request_shutdown();
    step(10);
    CHECK(power_fsm_state() == PS_PI_BOOTING);
    CHECK(pins[PIN_LATCH_CONTROL]);
    pins[PIN_RPI5_ON] = true;
    step(1);
    step(1);
    CHECK(power_fsm_state() == PS_SHUTTING_DOWN);
    CHECK(pins[PIN_LATCH_CONTROL]);
    pins[PIN_RPI5_ON] = false;
    step(PI_SHUTDOWN_MIN_HOLDOFF_MS);
    step(1);
    CHECK(stays_off());
    return true;
}

static bool supply_loss(void) {
    CHECK(start_bench());
    power_request_on();
    supply_v = 4.85f;
    step(V5_MONITOR_INTERVAL_MS);
    step(V5_LOW_DEBOUNCE_MS - 1);
    CHECK(pins[PIN_LATCH_CONTROL]);
    step(V5_MONITOR_INTERVAL_MS);
    CHECK(fault == FAULT_SUPPLY_LOST);
    CHECK(!pins[PIN_LATCH_CONTROL]);
    supply_v = 5.20f;
    CHECK(stays_off());
    return true;
}

static bool rail_collapse(void) {
    power_request_on();
    step(0);
    power_request_on();
    supply_v = 4.5f;
    step(RAIL_SETTLE_MS);
    step(1);
    CHECK(fault == FAULT_RAIL_COLLAPSE);
    CHECK(!pins[PIN_LATCH_CONTROL]);
    supply_v = 5.20f;
    CHECK(stays_off());
    return true;
}

// --- Supply freshness (independent review round 5, 2026-10-12) ---------------
// A stalled ring used to hand back stale samples stamped fresh, which would have
// blinded every check below. Now only a FRESH reading can close the latch or
// pass the rail check, a STALLED one counts as low, and a NOT_SAMPLED one (a
// mode without ch1) is never judged at all.

static bool supply_unreadable_latch(void) {
    supply_status = ADC_5VIN_STALLED;          // supply_v says a healthy 5.20 V
    power_request_on();
    step(0);
    CHECK(power_fsm_state() == PS_FAULT);
    CHECK(fault == FAULT_SUPPLY_UNREADABLE);
    CHECK(latch_rises == 0);
    return true;
}

static bool supply_not_sampled_latch(void) {
    supply_status = ADC_5VIN_NOT_SAMPLED;      // the last good value, 5.20 V, is not a reading
    power_request_on();
    step(0);
    CHECK(power_fsm_state() == PS_FAULT);
    CHECK(fault == FAULT_SUPPLY_UNREADABLE);
    CHECK(latch_rises == 0);
    return true;
}

static bool supply_unreadable_rail(void) {
    power_request_on();
    step(0);
    CHECK(pins[PIN_LATCH_CONTROL]);
    supply_status = ADC_5VIN_STALLED;          // after the latch, before the rail check
    step(RAIL_SETTLE_MS);
    step(1);
    CHECK(fault == FAULT_SUPPLY_UNREADABLE);
    CHECK(!pins[PIN_LATCH_CONTROL]);
    supply_status = ADC_5VIN_FRESH;
    CHECK(stays_off());
    return true;
}

static bool supply_stall_running(void) {
    CHECK(start_bench());
    supply_status = ADC_5VIN_STALLED;          // healthy value, dead ring
    step(V5_MONITOR_INTERVAL_MS);
    step(V5_LOW_DEBOUNCE_MS - 1);
    CHECK(pins[PIN_LATCH_CONTROL]);
    step(V5_MONITOR_INTERVAL_MS);
    CHECK(fault == FAULT_SUPPLY_UNREADABLE);
    CHECK(!pins[PIN_LATCH_CONTROL]);
    supply_status = ADC_5VIN_FRESH;
    CHECK(stays_off());
    return true;
}

// Round 6: samples that arrived but cannot be trusted (an overflow, a DMA or
// conversion error) are INVALID, and the FSM treats them as unreadable.
static bool supply_invalid_latch(void) {
    supply_status = ADC_5VIN_INVALID;          // supply_v says 5.20 V; the samples are void
    power_request_on();
    step(0);
    CHECK(power_fsm_state() == PS_FAULT);
    CHECK(fault == FAULT_SUPPLY_UNREADABLE);
    CHECK(latch_rises == 0);
    return true;
}

static bool supply_invalid_running(void) {
    CHECK(start_bench());
    supply_status = ADC_5VIN_INVALID;
    step(V5_MONITOR_INTERVAL_MS);
    step(V5_LOW_DEBOUNCE_MS - 1);
    CHECK(pins[PIN_LATCH_CONTROL]);
    step(V5_MONITOR_INTERVAL_MS);
    CHECK(fault == FAULT_SUPPLY_UNREADABLE);
    CHECK(!pins[PIN_LATCH_CONTROL]);
    supply_status = ADC_5VIN_FRESH;
    CHECK(stays_off());
    return true;
}

static bool supply_not_sampled_running(void) {
    CHECK(start_bench());
    // ARMED / BURST: ch1 is not converted. A stale LOW value must not be judged
    // (the old code would have shut down on it)...
    supply_status = ADC_5VIN_NOT_SAMPLED;
    supply_v = 4.0f;
    for (unsigned i = 0; i < 30; i++) {
        step(V5_MONITOR_INTERVAL_MS);
        CHECK(pins[PIN_LATCH_CONTROL]);
        CHECK(fault == FAULT_NONE);
    }
    // ...and when sampling resumes low, the debounce starts THEN.
    supply_status = ADC_5VIN_FRESH;
    supply_v = 4.85f;
    step(V5_MONITOR_INTERVAL_MS);
    step(V5_LOW_DEBOUNCE_MS - 1);
    CHECK(pins[PIN_LATCH_CONTROL]);
    step(V5_MONITOR_INTERVAL_MS);
    CHECK(fault == FAULT_SUPPLY_LOST);
    CHECK(!pins[PIN_LATCH_CONTROL]);
    supply_v = 5.20f;
    CHECK(stays_off());
    return true;
}

static bool usb_guard(void) {
    supply_v = 4.85f;
    power_request_on();
    step(0);
    CHECK(power_fsm_state() == PS_FAULT);
    CHECK(fault == FAULT_USB_POWER_ONLY);
    CHECK(latch_rises == 0);
    power_request_fault_ack();
    step(1);
    step(1);
    CHECK(stays_off());
    return true;
}

static void short_press(void) {
    pins[PIN_PWR_TOGGLE] = false;
    step(1);
    step(BUTTON_DEBOUNCE_MS);
    pins[PIN_PWR_TOGGLE] = true;
    step(1);
    step(BUTTON_DEBOUNCE_MS);
}

static bool button_cycle(void) {
    short_press();
    CHECK(power_fsm_state() == PS_POWERING_ON);
    step(PI_DETECT_WINDOW_MS);
    CHECK(power_fsm_state() == PS_BENCH_RUNNING);
    power_request_on();
    step(1);
    short_press();
    step(1);
    CHECK(stays_off());
    return true;
}

static bool long_press(void) {
    CHECK(start_bench());
    pins[PIN_PWR_TOGGLE] = false;
    step(1);
    step(BUTTON_DEBOUNCE_MS);
    power_request_on();
    step(BUTTON_LONG_PRESS_MS);
    CHECK(!pins[PIN_LATCH_CONTROL]);
    pins[PIN_PWR_TOGGLE] = true;
    step(1);
    step(BUTTON_DEBOUNCE_MS);
    CHECK(stays_off());
    return true;
}

static bool stale_fault_ack(void) {
    power_request_fault_ack();
    step(1);
    supply_v = 4.85f;
    power_request_on();
    step(1);
    step(1);
    CHECK(power_fsm_state() == PS_FAULT);
    CHECK(fault == FAULT_USB_POWER_ONLY);
    CHECK(!pins[PIN_LATCH_CONTROL]);
    return true;
}

static bool request_on_result(void) {
    CHECK(power_request_on());
    CHECK(power_request_on());
    step(0);
    CHECK(!power_request_on());
    pins[PIN_PI_3V3_SENSE] = true;
    step(RAIL_SETTLE_MS);
    CHECK(power_fsm_state() == PS_PI_BOOTING);
    CHECK(!power_request_on());
    pins[PIN_RPI5_ON] = true;
    step(1);
    CHECK(!power_request_on());
    power_request_shutdown();
    step(1);
    CHECK(!power_request_on());
    power_request_force_off();
    step(1);
    CHECK(stays_off());
    power_request_force_off();
    CHECK(!power_request_on());
    step(1);
    CHECK(stays_off());
    return true;
}

static bool pi_boot_timeout(void) {
    pins[PIN_PI_3V3_SENSE] = true;
    power_request_on();
    step(0);
    step(RAIL_SETTLE_MS);
    CHECK(power_fsm_state() == PS_PI_BOOTING);
    CHECK(!power_request_on());
    step(PI_BOOT_TIMEOUT_MS + 1);
    CHECK(power_fsm_state() == PS_FAULT);
    CHECK(fault == FAULT_PI_BOOT_TIMEOUT);
    CHECK(pins[PIN_LATCH_CONTROL]);
    CHECK(!power_request_on());
    power_request_fault_ack();
    step(1);
    step(1);
    CHECK(stays_off());
    return true;
}

static bool pi_shutdown_timeout(void) {
    CHECK(start_pi());
    power_request_shutdown();
    step(1);
    step(PI_SHUTDOWN_MAX_WAIT_MS + 1);
    CHECK(fault == FAULT_PI_SHUTDOWN_TIMEOUT);
    step(1);
    CHECK(stays_off());
    return true;
}

// Every route to rail-down must stop the strobe BEFORE the latch drops (the
// gpio_put() mock exits if it does not), and an orderly Pi shutdown must stop it
// at the START of teardown, not a holdoff later.
static void strobe_active(void) {
    pins[PIN_STROBE_PULSE] = true;
    pins[PIN_GATE_PWM] = true;
}

static bool strobe_teardown(void) {
    CHECK(start_bench());
    strobe_active();
    power_request_shutdown();
    step(1);
    step(1);
    CHECK(strobe_offs >= 1u);
    CHECK(stays_off());

    CHECK(start_bench());
    strobe_active();
    power_request_force_off();
    step(1);
    CHECK(stays_off());

    CHECK(start_bench());
    strobe_active();
    supply_v = 4.85f;
    step(V5_MONITOR_INTERVAL_MS);
    step(V5_LOW_DEBOUNCE_MS);
    step(V5_MONITOR_INTERVAL_MS);
    CHECK(fault == FAULT_SUPPLY_LOST);
    supply_v = 5.20f;
    CHECK(stays_off());

    fault_clear();
    CHECK(start_pi());
    strobe_active();
    unsigned before = strobe_offs;
    power_request_shutdown();
    step(1);
    CHECK(power_fsm_state() == PS_SHUTTING_DOWN);
    CHECK(strobe_offs > before);
    CHECK(!pins[PIN_STROBE_PULSE] && !pins[PIN_GATE_PWM]);
    CHECK(pins[PIN_LATCH_CONTROL]);
    return true;
}

// Phase 7: the camera trigger (GPIO10) drops on every route to rail-down, before
// the latch (gpio_put() exits if not), and at the START of an orderly shutdown:
// a camera on J4 is powered from the Pi, and a trigger driven into an unpowered
// camera back-powers its 1.8 V I/O (CR-09).
static bool camera_teardown(void) {
    CHECK(start_bench());
    pins[PIN_CAM_TRIGGER] = true;
    power_request_shutdown();
    step(1);
    step(1);
    CHECK(camera_offs >= 1u);
    CHECK(stays_off());

    CHECK(start_bench());
    pins[PIN_CAM_TRIGGER] = true;
    power_request_force_off();
    step(1);
    CHECK(stays_off());

    CHECK(start_bench());
    pins[PIN_CAM_TRIGGER] = true;
    supply_v = 4.85f;
    step(V5_MONITOR_INTERVAL_MS);
    step(V5_LOW_DEBOUNCE_MS);
    step(V5_MONITOR_INTERVAL_MS);
    CHECK(fault == FAULT_SUPPLY_LOST);
    supply_v = 5.20f;
    CHECK(stays_off());

    fault_clear();
    CHECK(start_pi());
    pins[PIN_CAM_TRIGGER] = true;
    unsigned before = camera_offs;
    power_request_shutdown();
    step(1);
    CHECK(power_fsm_state() == PS_SHUTTING_DOWN);
    CHECK(camera_offs > before);
    CHECK(!pins[PIN_CAM_TRIGGER]);
    CHECK(pins[PIN_LATCH_CONTROL]);           // the Pi is still powered: only the trigger dropped
    return true;
}

// --- J8.37 (2026-10-19) -----------------------------------------------------
// The line follows the Pi's power. check_line() runs after every step of every
// test; these walk the transitions on purpose.

static bool line_follows_pi(void) {
    CHECK(line_mode == PI_LINE_RELEASED);           // STANDBY: nothing driven
    power_request_on();
    step(0);
    CHECK(pins[PIN_LATCH_CONTROL]);
    CHECK(line_mode == PI_LINE_RELEASED);           // latch closed, no Pi 3V3 yet
    step(RAIL_SETTLE_MS);
    pins[PIN_PI_3V3_SENSE] = true;                  // the Pi's 3V3 comes up
    step(1);
    CHECK(power_fsm_state() == PS_PI_BOOTING);
    CHECK(line_mode == PI_LINE_DEASSERTED);
    pins[PIN_RPI5_ON] = true;
    step(1);
    CHECK(power_fsm_state() == PS_RUNNING);
    CHECK(line_mode == PI_LINE_DEASSERTED);

    // The request: asserted for exactly PI_SHUTDOWN_PULSE_MS, once.
    power_request_shutdown();
    step(1);
    CHECK(line_mode == PI_LINE_ASSERTED);
    step(PI_SHUTDOWN_PULSE_MS - 1);
    CHECK(line_mode == PI_LINE_ASSERTED);
    step(1);
    CHECK(line_mode == PI_LINE_DEASSERTED);
    // The Pi halts and its 3V3 drops (Q4's other answer): released at once,
    // though the latch holds until the holdoff.
    pins[PIN_RPI5_ON] = false;
    pins[PIN_PI_3V3_SENSE] = false;
    step(1);
    CHECK(pins[PIN_LATCH_CONTROL]);
    CHECK(line_mode == PI_LINE_RELEASED);
    step(PI_SHUTDOWN_MIN_HOLDOFF_MS);
    step(1);
    CHECK(stays_off());
    CHECK(line_mode == PI_LINE_RELEASED);
    CHECK(line_asserts == 1u);
    return true;
}

// Bench sessions never drive it: no Pi, no 3V3.
static bool line_bench_released(void) {
    CHECK(start_bench());
    for (unsigned i = 0; i < 20; i++) {
        step(100);
        CHECK(line_mode == PI_LINE_RELEASED);
    }
    CHECK(stop_bench());
    CHECK(line_asserts == 0u);
    return true;
}

// --- UART1, the serial link (2026-10-19) --------------------------------------
// The pins follow the Pi, not the FSM: connected only while the latch is closed,
// its 3V3 is up AND RPI5_ON is high. check_line() holds that after every step of
// every test; these walk the transitions on purpose.

static bool uart_follows_pi(void) {
    CHECK(!uart_on);                                // STANDBY: released
    pins[PIN_PI_3V3_SENSE] = true;
    power_request_on();
    step(0);
    CHECK(pins[PIN_LATCH_CONTROL] && !uart_on);     // latch and 3V3, no userspace yet
    step(RAIL_SETTLE_MS);
    CHECK(power_fsm_state() == PS_PI_BOOTING && !uart_on);
    pins[PIN_RPI5_ON] = true;
    step(1);
    CHECK(power_fsm_state() == PS_RUNNING && uart_on && uart_connects == 1u);
    // A Pi reboot: RPI5_ON drops with userspace, released; back up, connected.
    pins[PIN_RPI5_ON] = false;
    step(1);
    CHECK(!uart_on);
    pins[PIN_RPI5_ON] = true;
    step(1);
    CHECK(uart_on && uart_connects == 2u);
    // The 3V3 alone dropping releases it too.
    pins[PIN_PI_3V3_SENSE] = false;
    step(1);
    CHECK(!uart_on);
    pins[PIN_PI_3V3_SENSE] = true;
    step(1);
    CHECK(uart_on);
    // An orderly shutdown: the link stays up until the Pi's hook drops RPI5_ON,
    // then released while the latch still holds for the holdoff.
    power_request_shutdown();
    step(1);
    CHECK(power_fsm_state() == PS_SHUTTING_DOWN && uart_on);
    pins[PIN_RPI5_ON] = false;
    step(1);
    CHECK(!uart_on && pins[PIN_LATCH_CONTROL]);
    step(PI_SHUTDOWN_MIN_HOLDOFF_MS);
    step(1);
    CHECK(stays_off());
    CHECK(!uart_on && uart_connects == 3u);
    return true;
}

// Every route to the latch dropping under a connected link releases the link
// first: gpio_put() ends the run if the latch drops while it is connected.
static bool uart_released_before_latch(void) {
    CHECK(start_pi());
    CHECK(uart_on);
    power_request_force_off();                      // `forceoff`
    step(1);
    CHECK(stays_off() && !uart_on);

    pins[PIN_RPI5_ON] = false;
    CHECK(start_pi());
    supply_v = 4.50f;                               // the supply goes in RUNNING
    step(V5_MONITOR_INTERVAL_MS);
    step(V5_LOW_DEBOUNCE_MS + V5_MONITOR_INTERVAL_MS);
    CHECK(fault == FAULT_SUPPLY_LOST);
    step(1);
    CHECK(stays_off() && !uart_on);
    supply_v = 5.20f;
    fault = FAULT_NONE;

    pins[PIN_RPI5_ON] = false;
    CHECK(start_pi());
    power_request_shutdown();                       // the 5 s hold mid-shutdown, Pi still up
    step(1);
    CHECK(uart_on);
    pins[PIN_PWR_TOGGLE] = false;
    step(1);
    step(BUTTON_DEBOUNCE_MS);
    step(BUTTON_LONG_PRESS_MS);
    CHECK(!pins[PIN_LATCH_CONTROL] && !uart_on);
    pins[PIN_PWR_TOGGLE] = true;
    step(1);
    step(BUTTON_DEBOUNCE_MS);
    CHECK(stays_off());
    return true;
}

// A bench session with RPI5_ON jumpered high but no Pi 3V3: never connected.
static bool uart_bench_released(void) {
    pins[PIN_RPI5_ON] = true;
    CHECK(start_bench());
    for (unsigned i = 0; i < 20; i++) {
        step(100);
        CHECK(!uart_on);
    }
    CHECK(stop_bench());
    CHECK(uart_connects == 0u);
    return true;
}

// --- Halt telemetry (BENCH_P8_PI.md 8.6, 2026-10-19) -------------------------

static bool tele_clean_shutdown(void) {
    CHECK(start_pi());
    power_request_shutdown();
    step(1);
    CHECK(telemetry_get(TELE_PI_SHUTDOWN_REQ) == 1u);
    pins[PIN_RPI5_ON] = false;          // the Pi halts; its 3V3 stays up (Pi 5 default)
    step(PI_SHUTDOWN_MIN_HOLDOFF_MS);
    step(1);
    CHECK(stays_off());
    CHECK(telemetry_get(TELE_PI_SHUTDOWN_CLEAN) == 1u);
    CHECK(telemetry_get(TELE_PI_SHUTDOWN_TIMEOUT) == 0u);
    CHECK(telemetry_get(TELE_PI_POWER_CUT) == 0u);
    CHECK(telemetry_get(TELE_PI_DOWN_UNREQ) == 0u);
    return true;
}

static bool tele_shutdown_timeout(void) {
    CHECK(start_pi());
    power_request_shutdown();
    step(1);
    step(PI_SHUTDOWN_MAX_WAIT_MS + 1);
    CHECK(fault == FAULT_PI_SHUTDOWN_TIMEOUT);
    step(1);
    CHECK(stays_off());
    CHECK(telemetry_get(TELE_PI_SHUTDOWN_REQ) == 1u);
    CHECK(telemetry_get(TELE_PI_SHUTDOWN_TIMEOUT) == 1u);
    CHECK(telemetry_get(TELE_PI_SHUTDOWN_CLEAN) == 0u);
    CHECK(telemetry_get(TELE_PI_POWER_CUT) == 1u);  // the latch opened on a running Pi
    return true;
}

// Every route that drops the latch under a powered Pi counts once; routes with
// no powered Pi do not.
static bool tele_power_cuts(void) {
    CHECK(start_pi());
    power_request_force_off();                      // `forceoff` from RUNNING
    step(1);
    CHECK(stays_off());
    CHECK(telemetry_get(TELE_PI_POWER_CUT) == 1u);

    pins[PIN_RPI5_ON] = false;                      // a Pi still booting...
    power_request_on();
    step(0);
    step(RAIL_SETTLE_MS);
    CHECK(power_fsm_state() == PS_PI_BOOTING);
    pins[PIN_PWR_TOGGLE] = false;                   // ...and the 5 s hold
    step(1);
    step(BUTTON_DEBOUNCE_MS);
    step(BUTTON_LONG_PRESS_MS);
    CHECK(!pins[PIN_LATCH_CONTROL]);
    pins[PIN_PWR_TOGGLE] = true;
    step(1);
    step(BUTTON_DEBOUNCE_MS);
    CHECK(stays_off());
    CHECK(telemetry_get(TELE_PI_POWER_CUT) == 2u);

    pins[PIN_PI_3V3_SENSE] = false;                 // a bench session: no Pi
    CHECK(start_bench());
    power_request_force_off();
    step(1);
    CHECK(stays_off());
    CHECK(telemetry_get(TELE_PI_POWER_CUT) == 2u);

    pins[PIN_PI_3V3_SENSE] = true;                  // a Pi that vanishes mid-boot
    power_request_on();
    step(0);
    step(RAIL_SETTLE_MS);
    CHECK(power_fsm_state() == PS_PI_BOOTING);
    pins[PIN_PI_3V3_SENSE] = false;
    step(1);
    CHECK(fault == FAULT_NO_PI_DETECTED);
    step(1);
    CHECK(stays_off());
    CHECK(telemetry_get(TELE_PI_POWER_CUT) == 2u);  // its 3V3 was already gone
    return true;
}

// PI_BOOT_TIMEOUT leaves the rails up; acknowledging it cuts a Pi that is up.
static bool tele_boot_timeout_ack(void) {
    pins[PIN_PI_3V3_SENSE] = true;
    power_request_on();
    step(0);
    step(RAIL_SETTLE_MS);
    step(PI_BOOT_TIMEOUT_MS + 1);
    CHECK(power_fsm_state() == PS_FAULT);
    CHECK(pins[PIN_LATCH_CONTROL]);
    CHECK(line_mode == PI_LINE_DEASSERTED);
    power_request_fault_ack();
    step(1);
    step(1);
    CHECK(stays_off());
    CHECK(telemetry_get(TELE_PI_POWER_CUT) == 1u);
    return true;
}

static bool tele_pi_down_unrequested(void) {
    CHECK(start_pi());
    pins[PIN_RPI5_ON] = false;                      // userspace gone; nobody asked
    step(1);
    step(PI_DOWN_DEBOUNCE_MS - 2);
    CHECK(telemetry_get(TELE_PI_DOWN_UNREQ) == 0u); // still debouncing
    CHECK(!power_pi_down_unrequested(NULL));
    step(2);
    CHECK(telemetry_get(TELE_PI_DOWN_UNREQ) == 1u);
    uint32_t ms = 1u;
    CHECK(power_pi_down_unrequested(&ms));
    CHECK(ms == 0u);
    // Logged, not acted on: the rail stays up, and it is counted once.
    for (unsigned i = 0; i < 50; i++) step(100);
    CHECK(power_fsm_state() == PS_RUNNING);
    CHECK(pins[PIN_LATCH_CONTROL]);
    CHECK(telemetry_get(TELE_PI_DOWN_UNREQ) == 1u);
    CHECK(power_pi_down_unrequested(&ms));
    CHECK(ms == 5000u);
    // It comes back without a power cycle: a Pi reboot.
    pins[PIN_RPI5_ON] = true;
    step(1);
    step(PI_PRESENT_DEBOUNCE_MS - 1);
    CHECK(telemetry_get(TELE_PI_BACK_UP) == 0u);
    step(1);
    CHECK(telemetry_get(TELE_PI_BACK_UP) == 1u);
    CHECK(!power_pi_down_unrequested(NULL));
    // And the session still ends cleanly.
    power_request_shutdown();
    step(1);
    pins[PIN_RPI5_ON] = false;
    step(PI_SHUTDOWN_MIN_HOLDOFF_MS);
    step(1);
    CHECK(stays_off());
    CHECK(telemetry_get(TELE_PI_SHUTDOWN_CLEAN) == 1u);
    CHECK(telemetry_get(TELE_PI_POWER_CUT) == 0u);
    CHECK(telemetry_get(TELE_PI_DOWN_UNREQ) == 1u);
    return true;
}

// A dip shorter than the debounce is not a halt, on either sense line.
static bool tele_glitch_not_counted(void) {
    CHECK(start_pi());
    for (unsigned k = 0; k < 2; k++) {
        unsigned pin = k ? PIN_PI_3V3_SENSE : PIN_RPI5_ON;
        pins[pin] = false;
        step(1);
        step(PI_DOWN_DEBOUNCE_MS - 2);
        pins[pin] = true;
        step(1);
        step(PI_DOWN_DEBOUNCE_MS);
    }
    CHECK(telemetry_get(TELE_PI_DOWN_UNREQ) == 0u);
    CHECK(telemetry_get(TELE_PI_BACK_UP) == 0u);
    CHECK(power_fsm_state() == PS_RUNNING);
    return true;
}

// The mirror: a reset (here, the next telemetry_init() without a power-on)
// counts a power cut exactly when a Pi was powered at that moment.
static bool tele_reset_cuts_pi(void) {
    CHECK(start_pi());
    telemetry_init(NULL, TELE_BOOT_WATCHDOG);       // the board resets under the Pi
    CHECK(telemetry_source() == TELE_FROM_RAM);
    CHECK(telemetry_get(TELE_BOOTS) == 2u);
    CHECK(telemetry_get(TELE_BOOTS_WATCHDOG) == 1u);
    CHECK(telemetry_get(TELE_PI_POWER_CUT) == 1u);

    reset_fixture();                                // a clean session, then a reset
    CHECK(start_pi());
    power_request_shutdown();
    step(1);
    pins[PIN_RPI5_ON] = false;
    step(PI_SHUTDOWN_MIN_HOLDOFF_MS);
    step(1);
    CHECK(stays_off());
    telemetry_init(NULL, TELE_BOOT_SOFTWARE);
    CHECK(telemetry_get(TELE_PI_POWER_CUT) == 0u);
    CHECK(telemetry_get(TELE_BOOTS_WATCHDOG) == 0u);

    reset_fixture();                                // a bench session: nothing to cut
    CHECK(start_bench());
    telemetry_init(NULL, TELE_BOOT_SOFTWARE);
    CHECK(telemetry_get(TELE_PI_POWER_CUT) == 0u);
    return true;
}

// --- Independent review 2026-10-19 -------------------------------------------

// F2: one physical power cut is one count, wherever a reset lands in FORCE_OFF:
// before the count, between any two words of its mirror commit, in the
// teardown, or on either side of the latch write. History not yet in flash
// survives. (The review's probe counted 2 here before the fix.)
static bool reset_during_force_off(void) {
    for (volatile unsigned k = 1u; k < 200u; k++) {
        reset_fixture();
        CHECK(start_pi());
        telemetry_count(TELE_FAULTS);                   // history, not yet in flash
        power_request_force_off();
        arm_reset(k);
        if (setjmp(s_reset_jmp) == 0) {
            step(1);                                    // FORCE_OFF, through to STANDBY
            disarm_reset();
            CHECK(k > 25u);                             // every event above was a reset point
            CHECK(!pins[PIN_LATCH_CONTROL]);
            CHECK(telemetry_get(TELE_PI_POWER_CUT) == 1u);
            telemetry_init(NULL, TELE_BOOT_SOFTWARE);   // a reset afterwards adds nothing
            CHECK(telemetry_get(TELE_PI_POWER_CUT) == 1u);
            CHECK(telemetry_get(TELE_FAULTS) == 1u);
            return true;
        }
        telemetry_init(NULL, TELE_BOOT_SOFTWARE);       // the reset at event k
        CHECK(telemetry_source() == TELE_FROM_RAM);
        CHECK(telemetry_get(TELE_PI_POWER_CUT) == 1u);
        CHECK(telemetry_get(TELE_FAULTS) == 1u);
        CHECK(telemetry_get(TELE_BOOTS) == 2u);
    }
    return false;
}

// F2, the clean path: from the Pi's acknowledgement to the latch opening, a reset
// anywhere counts this shutdown exactly once -- as clean, or as a cut if it lands
// before the clean decision is committed. Never both, never neither.
static bool reset_during_clean_off(void) {
    for (volatile unsigned k = 1u; k < 200u; k++) {
        reset_fixture();
        CHECK(start_pi());
        power_request_shutdown();
        step(1);
        pins[PIN_RPI5_ON] = false;              // the Pi's hook confirmed read-only storage
        step(PI_SHUTDOWN_MIN_HOLDOFF_MS - 10u);
        CHECK(power_fsm_state() == PS_SHUTTING_DOWN);
        arm_reset(k);
        if (setjmp(s_reset_jmp) == 0) {
            step(10);                           // the clean decision
            step(1);                            // FORCE_OFF
            disarm_reset();
            CHECK(k > 25u);
            CHECK(power_fsm_state() == PS_STANDBY);
            CHECK(telemetry_get(TELE_PI_SHUTDOWN_CLEAN) == 1u);
            CHECK(telemetry_get(TELE_PI_POWER_CUT) == 0u);
            telemetry_init(NULL, TELE_BOOT_SOFTWARE);
            CHECK(telemetry_get(TELE_PI_POWER_CUT) == 0u);
            return true;
        }
        telemetry_init(NULL, TELE_BOOT_SOFTWARE);
        CHECK(telemetry_get(TELE_PI_SHUTDOWN_REQ) == 1u);
        CHECK(telemetry_get(TELE_PI_SHUTDOWN_CLEAN) + telemetry_get(TELE_PI_POWER_CUT) == 1u);
    }
    return false;
}

// F1, the board's side: a Pi whose shutdown hook could not confirm read-only
// storage leaves RPI5_ON high, and its 3V3 stays up. The holdoff alone opens
// nothing: the board holds the latch to PI_SHUTDOWN_MAX_WAIT_MS, then counts a
// timeout and a cut -- never a clean shutdown.
static bool shutdown_unconfirmed_times_out(void) {
    CHECK(start_pi());
    power_request_shutdown();
    step(1);
    step(PI_SHUTDOWN_MIN_HOLDOFF_MS);
    CHECK(power_fsm_state() == PS_SHUTTING_DOWN);
    CHECK(pins[PIN_LATCH_CONTROL]);
    step(PI_SHUTDOWN_MAX_WAIT_MS - PI_SHUTDOWN_MIN_HOLDOFF_MS);
    CHECK(power_fsm_state() == PS_SHUTTING_DOWN);  // 60 s exactly: still waiting
    CHECK(pins[PIN_LATCH_CONTROL]);
    step(1);
    CHECK(fault == FAULT_PI_SHUTDOWN_TIMEOUT);
    step(1);
    CHECK(stays_off());
    CHECK(telemetry_get(TELE_PI_SHUTDOWN_CLEAN) == 0u);
    CHECK(telemetry_get(TELE_PI_SHUTDOWN_TIMEOUT) == 1u);
    CHECK(telemetry_get(TELE_PI_POWER_CUT) == 1u);
    return true;
}

// Image 1.10 (review finding V19-F1): power_pi_maybe_present() is the FSM's
// flag OR the 3V3 sensed now. The flag alone is false with a Pi's 3V3 up in
// POWERING_ON before RAIL_SETTLE_MS; and it stays true in RUNNING after the 3V3
// is lost. In both, a Pi may be present.
static bool maybe_present_power_up(void) {
    CHECK(!power_pi_maybe_present());               // STANDBY, nothing sensed
    pins[PIN_PI_3V3_SENSE] = true;                  // STANDBY: a Pi powered some other way
    CHECK(!power_pi_present());
    CHECK(power_pi_maybe_present());
    pins[PIN_PI_3V3_SENSE] = false;

    power_request_on();
    step(0);
    CHECK(power_fsm_state() == PS_POWERING_ON);
    CHECK(!power_pi_maybe_present());
    pins[PIN_PI_3V3_SENSE] = true;                  // the Pi's 3V3 up before the settle
    step(RAIL_SETTLE_MS - 1);
    CHECK(power_fsm_state() == PS_POWERING_ON);
    CHECK(!power_pi_present());
    CHECK(power_pi_maybe_present());
    step(1);
    CHECK(power_fsm_state() == PS_PI_BOOTING);
    CHECK(power_pi_present() && power_pi_maybe_present());

    pins[PIN_RPI5_ON] = true;
    step(1);
    CHECK(power_fsm_state() == PS_RUNNING);
    pins[PIN_PI_3V3_SENSE] = false;                 // the 3V3 lost under RUNNING
    pins[PIN_RPI5_ON] = false;
    step(1);
    step(PI_DOWN_DEBOUNCE_MS + 10);
    CHECK(power_fsm_state() == PS_RUNNING);
    CHECK(power_pi_present());                      // the FSM keeps its classification
    CHECK(power_pi_maybe_present());
    return true;
}

// ...and late detection in BENCH_RUNNING: the 3V3 up for PI_PRESENT_DEBOUNCE_MS
// before the flag. A glitch shorter than that leaves nothing behind.
static bool maybe_present_late_pi(void) {
    CHECK(start_bench());
    CHECK(!power_pi_maybe_present());
    pins[PIN_PI_3V3_SENSE] = true;                  // a glitch
    step(1);
    CHECK(power_pi_maybe_present());
    pins[PIN_PI_3V3_SENSE] = false;
    step(1);
    CHECK(power_fsm_state() == PS_BENCH_RUNNING);
    CHECK(!power_pi_maybe_present());

    pins[PIN_PI_3V3_SENSE] = true;                  // a late Pi
    step(1);
    step(PI_PRESENT_DEBOUNCE_MS - 1);
    CHECK(power_fsm_state() == PS_BENCH_RUNNING);
    CHECK(!power_pi_present());
    CHECK(power_pi_maybe_present());
    step(1);
    CHECK(power_fsm_state() == PS_PI_BOOTING);
    CHECK(power_pi_present() && power_pi_maybe_present());
    return true;
}

int main(int argc, char **argv) {
    static const struct { const char *name; bool (*run)(void); } cases[] = {
#define TEST(name) {#name, name}
        TEST(reset_during_force_off), TEST(reset_during_clean_off),
        TEST(shutdown_unconfirmed_times_out),
        TEST(line_follows_pi), TEST(line_bench_released),
        TEST(uart_follows_pi), TEST(uart_released_before_latch), TEST(uart_bench_released),
        TEST(tele_clean_shutdown), TEST(tele_shutdown_timeout), TEST(tele_power_cuts),
        TEST(tele_boot_timeout_ack), TEST(tele_pi_down_unrequested),
        TEST(tele_glitch_not_counted), TEST(tele_reset_cuts_pi),
        TEST(redundant_on_shutdown), TEST(redundant_on_forceoff),
        TEST(on_during_startup), TEST(on_during_shutdown), TEST(on_during_fault),
        TEST(standby_off), TEST(pending_on_shutdown), TEST(pending_on_forceoff),
        TEST(fresh_on_after_cancel), TEST(shutdown_during_startup),
        TEST(shutdown_during_pi_boot), TEST(supply_loss), TEST(rail_collapse),
        TEST(usb_guard), TEST(button_cycle), TEST(long_press), TEST(stale_fault_ack),
        TEST(request_on_result), TEST(pi_boot_timeout), TEST(pi_shutdown_timeout),
        TEST(strobe_teardown), TEST(camera_teardown),
        TEST(supply_unreadable_latch), TEST(supply_not_sampled_latch),
        TEST(supply_unreadable_rail), TEST(supply_stall_running),
        TEST(supply_not_sampled_running), TEST(supply_invalid_latch),
        TEST(supply_invalid_running), TEST(maybe_present_power_up),
        TEST(maybe_present_late_pi)
#undef TEST
    };
    if (argc == 2) {
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            if (strcmp(argv[1], cases[i].name) != 0) continue;
            test_name = cases[i].name;
            reset_fixture();
            if (!cases[i].run()) return 1;
            printf("PASS %s\n", test_name);
            return 0;
        }
    }
    fprintf(stderr, "Expected one registered test name\n");
    return 2;
}
