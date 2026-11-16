// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
#include "safe_state.h"
#include "board.h"
#include "telemetry.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"

// RPI5_SHUTDOWN's inactive level: high for the active-low gpio-shutdown overlay.
#define PI_SHUTDOWN_IDLE_LEVEL  (PI_SHUTDOWN_ACTIVE_LOW ? true : false)

static volatile fault_t s_fault = FAULT_NONE;
static volatile uint32_t s_fault_gen;

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
    [FAULT_STROBE_OVERCURRENT]  = "STROBE_OVERCURRENT",
    [FAULT_STROBE_CLAMP]        = "STROBE_CLAMP",
    [FAULT_INTERNAL]            = "INTERNAL",
    [FAULT_SUPPLY_UNREADABLE]   = "SUPPLY_UNREADABLE",
};

static void out_low(uint pin) {
    gpio_init(pin);
    gpio_put(pin, 0);          // set the level BEFORE enabling the driver, so
    gpio_set_dir(pin, GPIO_OUT); // we never emit a glitch high on the way up
}

static void in_pull(uint pin, bool up, bool down) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_set_pulls(pin, up, down);
}

void safe_state_init(void) {
    // ---- Outputs, most dangerous first -----------------------------------

    // +5V rail off. R12 10K also pulls this down through reset, so the rail is
    // open before we ever execute -- this just makes it explicit and holds it.
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
    // if this were stuck high -- U9 clamps it. See board.h fact #5.
    out_low(PIN_MOD_PWM);
    out_low(PIN_DEMOD_PWM);

    // Comparator threshold to 0 V.
    out_low(PIN_THRESHOLD_PWM);

    // Baseline HPF: LOW at reset, which is also TRACK.
    //
    // GPIO33 = 0 selects S1 (R96 to GND), the tracking 0.66 s high-pass, and
    // GPIO33 = 1 is HOLD. Resolved on hardware 2026-08-17 and against the
    // TMUX1219 truth table -- see HPF_SEL_TRACK in board.h. (This comment used to
    // call tracking HIGH; that was the pre-bring-up guess, and it was backwards.)
    //
    // LOW is also the only safe level with the rail down. This pin drives the
    // SEL input of U14 (TMUX1219), whose VDD is +5VA. In STANDBY
    // that rail is dark, so driving SEL high pushes leakage current into an
    // unpowered device and back-feeds the rail: measured on the bench as +5VA
    // floating at ~0.45 V, showing up as ~0.225 V at TP6/TP7 via the R75/R76
    // divider. Harmless at ~22 uA, but it is bad practice and it makes the
    // Phase 0.5 quiescent readings confusing.
    //
    // The mux does nothing while unpowered, so the level here is irrelevant to
    // function. HOLD (HIGH) is selected only once +5VA is live -- detect_hpf_set()
    // refuses otherwise -- and detect_hpf_safe_off() drops it again whenever the
    // rail goes down.
    out_low(PIN_HPF_TOGGLE);

    // Cameras idle, no exposure requested.
    out_low(PIN_CAM_TRIGGER);

    // USB-A accessory power off.
    out_low(PIN_USB_ENABLE);

    // Pi-facing outputs.
    //
    // RPI5_SHUTDOWN (J8.37) starts RELEASED: input, no pulls, input buffer off.
    // Until 2026-10-19 it was driven high (deasserted) from here, and in STANDBY
    // that pushed up to ~2.7 mA through R39 into an unpowered Pi's GPIO26 clamp.
    // The power FSM now drives it only while a Pi is powered (board.h, "THE
    // LINE'S REST STATE"). Released, a powered Pi's own pull-up holds it
    // deasserted. INVARIANT, unchanged: a reset must never look like a shutdown
    // request.
    //
    // CAREFUL, and this used to be documented wrong here: the pad does NOT come
    // out of reset floating. PADS_BANK0_GPIO43_RESET = 0x116, so PDE=1 -- the
    // output driver is high-Z but an internal PULL-DOWN (~34K, measured) is
    // active. For an active-low signal that is the ASSERTED level. From the reset
    // edge until this line executes, GPIO43 is held low.
    //
    // With no Pi seated that is harmless (nothing is listening). With a Pi, that
    // pull-down divides against gpio-shutdown's ~50K pull-up through R39's 1K and
    // puts J8.37 below RP1's VIH. MEASURED 2026-07-31 (PROGRESS.md Q10): a real
    // Pi would see ~1.34 V, so the level fails. It is defence-in-depth rather than
    // a gate, because every reset also opens the +5V latch -- the Pi is losing
    // power in the same instant. Optional fix: a 10K pull-up from J8.37 to J8.1,
    // the Pi's own 3V3 (NEXT_BOARD_REV.md CR-04), which feeds nothing into an
    // unpowered Pi.
    //
    // Releasing it here also turns that pull-down off, so the window ends at this
    // line. Nothing here can shorten the part before main().
    gpio_init(PIN_RPI5_SHUTDOWN);
    safe_state_pi_shutdown_line(PI_LINE_RELEASED);
    out_low(PIN_SYSTEM_READY);
    out_low(PIN_IRQ_OUT);

    // UART1 to the Pi (J8.8/J8.10) starts RELEASED as well: nothing idles high
    // into J8.10 until the power FSM sees a Pi up (safe_state.h). Out of reset
    // both pads are isolated with their pull-downs on, which this also ends.
    safe_state_pi_uart_lines(false);

    // Indicators.
    out_low(PIN_LED_RED);
    out_low(PIN_LED_YELLOW);
    out_low(PIN_PWR_BTN_LED);
    out_low(PIN_READY_LED);

    // ---- Inputs ----------------------------------------------------------

    // Panel button: ACTIVE LOW, and there is NO external pull-up on the board.
    // The internal one is load-bearing. See board.h fact #1.
    in_pull(PIN_PWR_TOGGLE, true, false);

    // Comparator has an external 10K pull-up (R103) -- no internal pull.
    in_pull(PIN_D_COMPARATOR, false, false);

    // Pi 3V3 presence comes from an external R45/R44 divider -- no internal pull.
    in_pull(PIN_PI_3V3_SENSE, false, false);

    // Defined level with the connectors unpopulated.
    //
    // CAUTION (board.h fact #8): RP2350 erratum E9 -- an input with the internal
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

// See the header. safe_state_init() uses gpio_init(), which resets the pad's
// function select to SIO, so every peripheral-owned pin has to be handed back.
// Kept here rather than in the owning modules so there is exactly one list, next
// to the code that tore them down.
void safe_state_reclaim_pins(void) {
    gpio_set_function(PIN_MOD_PWM,       GPIO_FUNC_PWM);
    gpio_set_function(PIN_DEMOD_PWM,     GPIO_FUNC_PWM);
    gpio_set_function(PIN_THRESHOLD_PWM, GPIO_FUNC_PWM);
}

void safe_state_pi_shutdown_line(pi_line_t mode) {
    const uint pin = PIN_RPI5_SHUTDOWN;
    if (mode == PI_LINE_RELEASED) {
        gpio_set_dir(pin, GPIO_IN);              // the driver off first
        gpio_set_pulls(pin, false, false);
        gpio_set_input_enabled(pin, false);
        return;
    }
    gpio_set_input_enabled(pin, true);           // so `pins` can read the pad back
    gpio_put(pin, mode == PI_LINE_ASSERTED ? !PI_SHUTDOWN_IDLE_LEVEL : PI_SHUTDOWN_IDLE_LEVEL);
    gpio_set_dir(pin, GPIO_OUT);                 // the level is already set: no glitch
}

// Release: the SIO driver off first, then the pad handed from the UART to SIO,
// so nothing is driven at any point; then no pulls and no input buffer, as for
// J8.37. Connect: RX first, then TX, whose pad takes the UART's idle high.
static void uart_pin_release(uint pin) {
    gpio_set_dir(pin, GPIO_IN);
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_set_pulls(pin, false, false);
    gpio_set_input_enabled(pin, false);
}

void safe_state_pi_uart_lines(bool connect) {
    if (connect) {
        if (safe_state_pi_uart_connected()) return;
        gpio_set_pulls(PIN_UART_RX, false, false);
        gpio_set_pulls(PIN_UART_TX, false, false);
        gpio_set_function(PIN_UART_RX, GPIO_FUNC_UART);
        gpio_set_function(PIN_UART_TX, GPIO_FUNC_UART);
        return;
    }
    uart_pin_release(PIN_UART_TX);
    uart_pin_release(PIN_UART_RX);
}

bool safe_state_pi_uart_connected(void) {
    return gpio_get_function(PIN_UART_TX) == GPIO_FUNC_UART;
}

pi_line_t safe_state_pi_shutdown_mode(void) {
    if (!gpio_is_dir_out(PIN_RPI5_SHUTDOWN)) return PI_LINE_RELEASED;
    return gpio_get_out_level(PIN_RPI5_SHUTDOWN) == PI_SHUTDOWN_IDLE_LEVEL
         ? PI_LINE_DEASSERTED : PI_LINE_ASSERTED;
}

const char *safe_state_pi_line_name(pi_line_t mode) {
    switch (mode) {
        case PI_LINE_RELEASED:   return "released (latch open or no Pi 3V3: nothing driven into J8.37)";
        case PI_LINE_DEASSERTED: return "driven, deasserted";
        case PI_LINE_ASSERTED:   return "driven, ASSERTED (shutdown request)";
    }
    return "?";
}

void fault_raise(fault_t f) {
    if (f == FAULT_NONE) return;
    if (s_fault == FAULT_NONE) {              // first fault wins
        s_fault = f;
        s_fault_gen++;
        telemetry_fault((uint32_t)f, to_ms_since_boot(get_absolute_time()) / 1000u);
    }
}

void fault_clear(void)      { s_fault = FAULT_NONE; }
fault_t fault_current(void) { return s_fault; }
uint32_t fault_generation(void) { return s_fault_gen; }

const char *fault_name(fault_t f) {
    // No `f < 0` check: the enum has no negative members so its underlying type
    // is unsigned, and gcc rightly points out the comparison can never be true.
    if (f >= FAULT__COUNT) return "?";
    const char *n = k_fault_names[f];
    return n ? n : "?";
}
