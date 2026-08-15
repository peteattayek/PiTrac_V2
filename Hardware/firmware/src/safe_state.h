// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// safe_state.h â€” everything dangerous idles safe.
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
    FAULT_SUPPLY_LOST,        // +5V_IN went away WHILE latched â€” we were being
                              // back-fed from USB through D8. Latch dropped.
    FAULT_NO_PI_DETECTED,     // POWERING_ON timed out with no Pi 3V3
    FAULT_PI_BOOT_TIMEOUT,    // Pi 3V3 up but userspace never signalled
    FAULT_PI_SHUTDOWN_TIMEOUT,// requested shutdown, Pi never went down
    FAULT_ADC_OVERRUN,        // ADC FIFO overran -> round-robin phase lost
    FAULT_BEAM_BLOCKED,
    FAULT_CAM_TIMEOUT,
    FAULT_INTERNAL,
    FAULT__COUNT
} fault_t;

// Drive every output to its safe level and configure every input's pulls.
// Idempotent. Safe to call at any time from any context.
void safe_state_init(void);

// Same, but also drops the +5V latch. Use from fault paths where the rail
// itself is suspect. NOTE: with a Pi seated this is a hard power cut â€” the
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
void safe_state_reclaim_pins(void);

// Latch a fault. First fault wins (so the root cause survives the cascade).
void fault_raise(fault_t f);
void fault_clear(void);
fault_t fault_current(void);
const char *fault_name(fault_t f);

#endif // PITRAC_SAFE_STATE_H
