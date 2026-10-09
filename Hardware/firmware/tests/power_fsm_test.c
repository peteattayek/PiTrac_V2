// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
#include "power_fsm.h"
#include "board.h"
#include "safe_state.h"
#include "adc_engine.h"
#include "beam.h"
#include "detect.h"
#include "strobe.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool pins[48];
static uint32_t clock_ms;
static float supply_v;
static bool beam_on;
static fault_t fault;
static unsigned latch_rises;
static unsigned strobe_offs;
static const char *test_name;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s failed: %s (state %s, latch %d)\n", \
                __FILE__, __LINE__, test_name, #condition, \
                power_state_name(power_fsm_state()), pins[PIN_LATCH_CONTROL]); \
        return false; \
    } \
} while (0)

absolute_time_t get_absolute_time(void) { return (uint64_t)clock_ms * 1000u; }
uint32_t to_ms_since_boot(absolute_time_t time) { return (uint32_t)(time / 1000u); }
bool gpio_get(unsigned pin) { return pins[pin]; }

void gpio_put(unsigned pin, bool value) {
    if (pin >= sizeof(pins) / sizeof(pins[0]) || pin == PIN_PULSE_LIMIT_DIS ||
        (pin == PIN_LATCH_CONTROL && !value &&
         (beam_on || pins[PIN_STROBE_PULSE] || pins[PIN_GATE_PWM]))) {
        fprintf(stderr, "Unsafe GPIO write in %s: pin %u, value %d\n",
                test_name, pin, value);
        exit(2);
    }
    if (pin == PIN_LATCH_CONTROL && value && !pins[pin]) latch_rises++;
    pins[pin] = value;
}

float adc_read_5vin_volts(void) { return supply_v; }
bool beam_enable(bool on) { beam_on = on; return true; }
void detect_hpf_safe_off(void) { gpio_put(PIN_HPF_TOGGLE, HPF_SEL_TRACK); }
// The real one drives GPIO25 SIO low and zeroes the gate DAC; the FSM must call
// it before the latch drops, which gpio_put() below checks.
void strobe_safe_off(void) {
    strobe_offs++;
    pins[PIN_STROBE_PULSE] = false;
    pins[PIN_GATE_PWM] = false;
}
void fault_raise(fault_t value) { if (fault == FAULT_NONE) fault = value; }
void fault_clear(void) { fault = FAULT_NONE; }
fault_t fault_current(void) { return fault; }

static void reset_fixture(void) {
    memset(pins, 0, sizeof(pins));
    pins[PIN_PWR_TOGGLE] = true;
    pins[PIN_RPI5_SHUTDOWN] = PI_SHUTDOWN_ACTIVE_LOW;
    clock_ms = 0;
    supply_v = 5.20f;
    beam_on = false;
    fault = FAULT_NONE;
    latch_rises = 0;
    strobe_offs = 0;
    power_fsm_init();
}

static void step(uint32_t elapsed_ms) {
    clock_ms += elapsed_ms;
    power_fsm_step();
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
// at the START of teardown, not 15 s later.
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

int main(int argc, char **argv) {
    static const struct { const char *name; bool (*run)(void); } cases[] = {
#define TEST(name) {#name, name}
        TEST(redundant_on_shutdown), TEST(redundant_on_forceoff),
        TEST(on_during_startup), TEST(on_during_shutdown), TEST(on_during_fault),
        TEST(standby_off), TEST(pending_on_shutdown), TEST(pending_on_forceoff),
        TEST(fresh_on_after_cancel), TEST(shutdown_during_startup),
        TEST(shutdown_during_pi_boot), TEST(supply_loss), TEST(rail_collapse),
        TEST(usb_guard), TEST(button_cycle), TEST(long_press), TEST(stale_fault_ack),
        TEST(request_on_result), TEST(pi_boot_timeout), TEST(pi_shutdown_timeout),
        TEST(strobe_teardown)
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
