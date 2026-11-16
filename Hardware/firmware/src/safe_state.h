// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// safe_state.h -- everything dangerous idles safe.
//
// safe_state_init() is the FIRST thing main() calls, before stdio, before
// clocks are touched, before anything else. Every fault path calls
// safe_state_now().

#ifndef PITRAC_SAFE_STATE_H
#define PITRAC_SAFE_STATE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FAULT_NONE = 0,
    FAULT_USB_POWER_ONLY,     // tried to latch while running on USB-C
    FAULT_RAIL_COLLAPSE,      // +5V_IN sagged right after closing the latch
    FAULT_SUPPLY_LOST,        // +5V_IN went away WHILE latched -- we were being
                              // back-fed from USB through D8. Latch dropped.
    FAULT_NO_PI_DETECTED,     // POWERING_ON timed out with no Pi 3V3
    FAULT_PI_BOOT_TIMEOUT,    // Pi 3V3 up but userspace never signalled
    FAULT_PI_SHUTDOWN_TIMEOUT,// requested shutdown, Pi never went down
    FAULT_ADC_OVERRUN,        // ADC FIFO overran -> round-robin phase lost
    FAULT_BEAM_BLOCKED,
    FAULT_CAM_TIMEOUT,
    FAULT_STROBE_OVERCURRENT, // live strobe: a plateau over STROBE_LIVE_I_STOP_A
    FAULT_STROBE_CLAMP,       // live strobe: current outside the pulse, or on past the U5 clamp
    FAULT_INTERNAL,
    FAULT_SUPPLY_UNREADABLE,  // +5V_IN could not be read FRESH when a power decision needed it
    FAULT__COUNT
} fault_t;

// Drive every output to its safe level and configure every input's pulls.
// Idempotent. Safe to call at any time from any context.
void safe_state_init(void);

// Same, but also drops the +5V latch. Use from fault paths where the rail
// itself is suspect. NOTE: with a Pi seated this is a hard power cut -- the
// power FSM prefers an orderly shutdown and only calls this as a last resort.
// Drive everything to its safe level and drop the rail.
//
// !! THIS REVERTS PIN FUNCTIONS TO SIO. !! out_low() calls gpio_init(), which
// takes the pad back from whatever peripheral owned it -- so GPIO44 (threshold
// DAC), GPIO31 (beam carrier) and GPIO39 (demod clock) all stop being PWM
// outputs. Nothing re-claims them on its own: beam_enable() only writes
// pwm_hw->en, not the function select, so after this `beam on` reports success
// and produces no light.
//
// Any caller that intends the board to keep working must call
// safe_state_reclaim_pins() afterwards. A caller that is latching a fault and
// expects a reboot does not need to.
void safe_state_now(void);

// Re-establish the peripheral pin functions that safe_state_now() tore down.
// Safe to call at any time; it only touches the function select, never a level.
// The strobe pins are deliberately NOT here: reclaiming GPIO28 would silently
// restore whatever gate setpoint was last written. strobe.c keeps GPIO25 SIO low
// between bursts and re-claims GPIO28 only after writing a fresh level.
void safe_state_reclaim_pins(void);

// J8.37, RPI5_SHUTDOWN: the one Pi-facing line that idles HIGH. Driven only
// while a Pi is powered and released otherwise, so the board never pushes current
// through R39 into an unpowered Pi's GPIO26 (board.h, "THE LINE'S REST STATE";
// PROGRESS.md section 6, 2026-10-16). safe_state_init() releases it; from then on
// the power FSM sets it on every step.
typedef enum {
    PI_LINE_RELEASED = 0,     // input, no pulls, input buffer off
    PI_LINE_DEASSERTED,       // driven to the inactive level
    PI_LINE_ASSERTED,         // driven to the shutdown-request level
} pi_line_t;

// Idempotent and glitch-free: a driven level is written before the driver is
// enabled, and repeating a mode changes nothing at the pad.
void        safe_state_pi_shutdown_line(pi_line_t mode);
// What the pad is doing now, read back from the hardware.
pi_line_t   safe_state_pi_shutdown_mode(void);
const char *safe_state_pi_line_name(pi_line_t mode);

// UART1 to the Pi: GPIO36 TX -> R30 220R -> J8.10 (Pi RXD) and J8.8 (Pi TXD) ->
// R31 220R -> GPIO37 RX. An idle UART holds TX HIGH, which through 220R would
// push ~12 mA into an unpowered Pi's GPIO15 clamp -- four times J8.37's
// worst case. So the pins are CONNECTED to UART1 only while a Pi is up and
// talking: latch closed, its 3V3 sensed, AND RPI5_ON high. RPI5_ON is driven by
// the Pi's SoC, so current fed into the Pi's 3V3 rail cannot hold it up the way
// it might hold up the 3V3 sense. RELEASED otherwise: SIO inputs, no pulls,
// input buffers off. safe_state_init() releases them; from then on the power
// FSM sets them on every step. Idempotent.
void        safe_state_pi_uart_lines(bool connect);
bool        safe_state_pi_uart_connected(void);   // read back from the pad's function

// Latch a fault. First fault wins (so the root cause survives the cascade).
// Each latch is also counted in the halt telemetry (telemetry.h).
void fault_raise(fault_t f);
void fault_clear(void);
fault_t fault_current(void);
const char *fault_name(fault_t f);

// Counts every fault that actually latched (NONE -> something). A command that
// snapshots this on entry sees ANY fault that latched while it ran -- including
// one with the same code as a fault already latched at entry and cleared since,
// which comparing fault codes cannot see (audit SVC-02, 2026-10-05).
uint32_t fault_generation(void);

#endif // PITRAC_SAFE_STATE_H
