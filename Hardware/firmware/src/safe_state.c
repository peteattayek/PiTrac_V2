// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
#include "safe_state.h"
#include "board.h"

#include "hardware/gpio.h"

static volatile fault_t s_fault = FAULT_NONE;

static const char *const k_fault_names[FAULT__COUNT] = {
    [FAULT_NONE]                = "none",
    [FAULT_USB_POWER_ONLY]      = "USB_POWER_ONLY",
    [FAULT_RAIL_COLLAPSE]       = "RAIL_COLLAPSE",
    [FAULT_SUPPLY_LOST]         = "SUPPLY_LOST",
    [FAULT_NO_PI_DETECTED]      = "NO_PI_DETECTED",
    [FAULT_PI_BOOT_TIMEOUT]     = "PI_BOOT_TIMEOUT",
    [FAULT_PI_SHUTDOWN_TIMEOUT] = "PI_SHUTDOWN_TIMEOUT",
    [FAULT_ADC_OVERRUN]         = "ADC_OVERRUN",
    [FAULT_BEAM_BLOCKED]        = "BEAM_BLOCKED",
    [FAULT_CAM_TIMEOUT]         = "CAM_TIMEOUT",
    [FAULT_INTERNAL]            = "INTERNAL",
};

static void out_low(uint pin) {
    gpio_init(pin);
    gpio_put(pin, 0);          // set the level BEFORE enabling the driver, so
    gpio_set_dir(pin, GPIO_OUT); // we never emit a glitch high on the way up
}

static void out_high(uint pin) {
    gpio_init(pin);
    gpio_put(pin, 1);
    gpio_set_dir(pin, GPIO_OUT);
}

static void in_pull(uint pin, bool up, bool down) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_set_pulls(pin, up, down);
}

void safe_state_init(void) {
    // ---- Outputs, most dangerous first -----------------------------------

    // +5V rail off. R12 10K also pulls this down through reset, so the rail is
    // open before we ever execute â€” this just makes it explicit and holds it.
    out_low(PIN_LATCH_CONTROL);

    // Strobe pulse gate low. R57 1K backs this up.
    out_low(PIN_STROBE_PULSE);

    // Strobe hardware watchdog ARMED. This is the ONLY write to this pin in the
    // entire firmware. Do not add another one. See board.h fact #2.
    out_low(PIN_PULSE_LIMIT_DIS);

    // Strobe current setpoint to zero (plain GPIO low until the PWM DAC is
    // configured; either way Q9's gate sits at 0 V and the sink is off).
    out_low(PIN_GATE_PWM);

    // Beam off. R69 1K backs this up. Note the beam cannot be driven DC even
    // if this were stuck high â€” U9 clamps it. See board.h fact #5.
    out_low(PIN_MOD_PWM);
    out_low(PIN_DEMOD_PWM);

    // Comparator threshold to 0 V.
    out_low(PIN_THRESHOLD_PWM);

    // Baseline HPF: LOW at reset.
    //
    // Tracking (HIGH) is the right *operating* idle -- the detector follows
    // ambient drift rather than sitting on a stale frozen baseline -- but this
    // pin drives the SEL input of U14 (TMUX1219), whose VDD is +5VA. In STANDBY
    // that rail is dark, so driving SEL high pushes leakage current into an
    // unpowered device and back-feeds the rail: measured on the bench as +5VA
    // floating at ~0.45 V, showing up as ~0.225 V at TP6/TP7 via the R75/R76
    // divider. Harmless at ~22 uA, but it is bad practice and it makes the
    // Phase 0.5 quiescent readings confusing.
    //
    // The mux does nothing while unpowered, so the level here is irrelevant to
    // function. Whoever brings up the detector sets this HIGH once +5VA is live
    // (Phase 3 / on arm), which is where it belongs anyway.
    out_low(PIN_HPF_TOGGLE);

    // Cameras idle, no exposure requested.
    out_low(PIN_CAM_TRIGGER);

    // USB-A accessory power off.
    out_low(PIN_USB_ENABLE);

    // Pi-facing outputs.
    // RPI5_SHUTDOWN idles DEASSERTED. With active-low polarity that means HIGH.
    // INVARIANT: a reset must never look like a shutdown request. Through reset
    // the pad is high-Z and the Pi's internal pull-up holds it deasserted;
    // here we take over and drive it actively so a floating 1K line cannot
    // couple a spurious request into the Pi's KEY_POWER input.
#if PI_SHUTDOWN_ACTIVE_LOW
    out_high(PIN_RPI5_SHUTDOWN);
#else
    out_low(PIN_RPI5_SHUTDOWN);
#endif
    out_low(PIN_SYSTEM_READY);
    out_low(PIN_IRQ_OUT);

    // Indicators.
    out_low(PIN_LED_RED);
    out_low(PIN_LED_YELLOW);
    out_low(PIN_PWR_BTN_LED);
    out_low(PIN_READY_LED);

    // ---- Inputs ----------------------------------------------------------

    // Panel button: ACTIVE LOW, and there is NO external pull-up on the board.
    // The internal one is load-bearing. See board.h fact #1.
    in_pull(PIN_PWR_TOGGLE, true, false);

    // Comparator has an external 10K pull-up (R103) â€” no internal pull.
    in_pull(PIN_D_COMPARATOR, false, false);

    // Pi 3V3 presence comes from an external R45/R44 divider â€” no internal pull.
    in_pull(PIN_PI_3V3_SENSE, false, false);

    // Defined level with the connectors unpopulated.
    //
    // CAUTION (board.h fact #8): RP2350 erratum E9 â€” an input with the internal
    // pull-down enabled and a high-impedance source can latch near 2.2 V rather
    // than reading 0. These three pins are exactly that configuration when J4
    // is empty and no Pi is seated. Treat them as advisory, never as an
    // interlock, until the die revision is checked (`picotool info -a`).
    in_pull(PIN_CAM_STROBE_0, false, true);
    in_pull(PIN_CAM_STROBE_1, false, true);
    in_pull(PIN_RPI5_ON,      false, true);

    // Spare handshakes: inputs with pull-downs until someone commits them.
    in_pull(PIN_HANDSHAKE_0, false, true);
    in_pull(PIN_HANDSHAKE_1, false, true);
}

void safe_state_now(void) {
    safe_state_init();
    // Explicit, after the fact: drop the rail. safe_state_init() already did
    // this, but stating it here documents the difference between the two calls.
    gpio_put(PIN_LATCH_CONTROL, 0);
}

void fault_raise(fault_t f) {
    if (f == FAULT_NONE) return;
    if (s_fault == FAULT_NONE) s_fault = f;   // first fault wins
}

void fault_clear(void)      { s_fault = FAULT_NONE; }
fault_t fault_current(void) { return s_fault; }

const char *fault_name(fault_t f) {
    // No `f < 0` check: the enum has no negative members so its underlying type
    // is unsigned, and gcc rightly points out the comparison can never be true.
    if (f >= FAULT__COUNT) return "?";
    const char *n = k_fault_names[f];
    return n ? n : "?";
}
