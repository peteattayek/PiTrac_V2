// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// power_fsm.h â€” Phase 1 (latch) + Phase 1b (Pi soft-shutdown).
//
// This is the highest-risk-to-the-Pi code in the project: the +5V latch IS the
// Pi's power switch. Every failure path here is meant to be proven against a
// SIMULATED Pi (jumper wires or a spare Pico) before a real one is ever seated.
//
// Simulating the Pi, from the plan:
//   RPI5_ON        GPIO0   <- J8.15 via R29 1K   : jumper J8.15 -> J8.1 (+3V3)
//   PI_3V3_SENSE   GPIO24  <- R45/R44 divider    : feed 3.3 V into the top of R45
//   RPI5_SHUTDOWN  GPIO43  -> J8.37 via R39 1K   : scope it

#ifndef PITRAC_POWER_FSM_H
#define PITRAC_POWER_FSM_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PS_STANDBY = 0,     // +3V3 only. Pi, boost and analog 5 V all dark.
    PS_POWERING_ON,     // latch closed, waiting for rails (and maybe a Pi)
    PS_PI_BOOTING,      // Pi 3V3 up, waiting for userspace
    PS_RUNNING,         // Pi up and signalling
    PS_BENCH_RUNNING,   // no Pi present: rails up, skip the boot handshake
    PS_SHUTTING_DOWN,   // shutdown requested, waiting for the Pi to halt
    PS_FORCE_OFF,       // drop the latch now
    PS_FAULT,
    PS__COUNT
} pstate_t;

void        power_fsm_init(void);
void        power_fsm_step(void);       // call from the core 0 superloop
pstate_t    power_fsm_state(void);
const char *power_state_name(pstate_t s);

// True once the rails have been up long enough that the 36 V boost has
// finished its ~86 ms soft start. Nothing high-energy may run before this.
bool power_rails_ready(void);

// Requests. Both are honoured only from a state where they make sense.
void power_request_on(void);
void power_request_shutdown(void);
void power_request_force_off(void);   // escape hatch; skips the orderly sequence

// Is a Pi actually present? Decided by PI_3V3_SENSE at POWERING_ON time. This
// is what selects PS_RUNNING vs PS_BENCH_RUNNING.
bool power_pi_present(void);

// "Pi is down" indicator, two-of-three. Exposed for the CLI so you can watch it
// while testing the simulated shutdown.
bool power_pi_is_down(void);

// Milliseconds spent in the current state (for the CLI status line).
uint32_t power_state_elapsed_ms(void);

#endif // PITRAC_POWER_FSM_H
