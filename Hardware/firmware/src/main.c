// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// PiTrac "Second Board To Rule Them All" â€” RP2354B firmware
//
// Phase 0  : board bring-up, safe state, CLI, ADC block capture
// Phase 1  : power button + latch, USB-power guard
// Phase 1b : full power FSM incl. Pi soft-shutdown (test against a simulated Pi)
//
// Core 1 is unused so far. It will own the hot path (comparator edge timing,
// speed math, camera handshake, strobe burst) from Phase 3 onward.
//
// See PROGRESS.md for what is done, what is next, and the measurements that
// still have to be taken. See src/board.h before touching any pin.

#include "board.h"
#include "safe_state.h"
#include "adc_engine.h"
#include "power_fsm.h"
#include "panel.h"
#include "beam.h"
#include "cli.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"

// Belt and braces against a build configured for the wrong chip variant.
// RP2354B is an RP2350*B*: 80-pin, 48 GPIOs. If something (an IDE wizard, a
// stale CMake cache) built this for an RP2350A we would get 30 GPIOs, and every
// pin from GPIO31 up â€” the beam carrier, the demod clock, the threshold DAC,
// the comparator, all five ADC channels â€” would quietly not exist.
// Fail at compile time instead.
_Static_assert(NUM_BANK0_GPIOS >= 48,
    "Built for the wrong chip variant. This board is an RP2354B (RP2350B core, "
    "48 GPIOs). Delete the build/ directory and re-run cmake â€” see START_HERE.md.");

// ---------------------------------------------------------------------------
// Status LEDs. D6 red (GPIO18) and D5 yellow (GPIO19) are on the always-on
// +3V3 rail, so they work on USB power with the +5V latch open â€” which is what
// makes them the only usable feedback for phases 0 and 1. The panel LEDs on J7
// are fed from the switched rail and cannot indicate standby.
//
//   yellow slow blink   standby, healthy
//   yellow solid        rails up (bench or running)
//   yellow fast blink   shutting down
//   red    fast blink   fault latched (code readable over the CLI)
// ---------------------------------------------------------------------------
static void leds_update(void) {
    uint32_t t = to_ms_since_boot(get_absolute_time());
    bool slow = (t % 2000) < 100;
    bool fast = (t % 300)  < 150;

    if (fault_current() != FAULT_NONE) {
        gpio_put(PIN_LED_RED, fast);
        gpio_put(PIN_LED_YELLOW, 0);
        return;
    }
    gpio_put(PIN_LED_RED, 0);

    switch (power_fsm_state()) {
        case PS_STANDBY:        gpio_put(PIN_LED_YELLOW, slow); break;
        case PS_SHUTTING_DOWN:  gpio_put(PIN_LED_YELLOW, fast); break;
        case PS_RUNNING:
        case PS_BENCH_RUNNING:  gpio_put(PIN_LED_YELLOW, 1);    break;
        default:                gpio_put(PIN_LED_YELLOW, (t % 1000) < 500); break;
    }
}

int main(void) {
    // FIRST. Before stdio, before peripherals, before anything. Every output
    // that could energise something lands at its safe level here.
    safe_state_init();

    stdio_init_all();

    adc_engine_init();
    adc_engine_set_mode(ADC_MODE_IDLE);

    power_fsm_init();
    panel_init();
    // Configures the PWM slices but leaves GPIO31/39 as SIO outputs driven low.
    // The beam stays dark until 'beam on' is typed, and cannot start at boot.
    beam_init();

    // Give the USB host a moment to enumerate before the CLI banner, otherwise
    // the first thing you see after a reflash is a truncated help text.
    sleep_ms(500);
    cli_init();

    // NOTE: the hardware watchdog is deliberately NOT enabled yet.
    //
    // A watchdog reset resets the pads, GPIO15 goes high-Z, R12 pulls it low,
    // and the Pi's power is yanked with no warning â€” a firmware crash becomes a
    // hard power cut. The policy decided in Phase 1b is to enable the reset
    // action only while the Pi is down. Wiring that up is a Phase 1b/8 task;
    // until then, running without it is the safer of the two wrong answers.

    for (;;) {
        power_fsm_step();
        cli_service();
        leds_update();     // on-board D5/D6, always-on +3V3
        panel_update();    // off-board J7 indicators, switched +5V
        tight_loop_contents();
    }
}
