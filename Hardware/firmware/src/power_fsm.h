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

// Requests. All are honoured only from a state where they make sense.
void power_request_on(void);
void power_request_shutdown(void);
void power_request_force_off(void);   // escape hatch; skips the orderly sequence

// Acknowledge a latched PS_FAULT, exactly as a button press does: clear the
// fault code AND leave PS_FAULT via FORCE_OFF, so the rail drops and we return
// to STANDBY. Ignored in every other state.
//
// This exists because clearing only the fault CODE leaves the FSM latched in
// PS_FAULT, which the panel ring keys off -- so the ring kept double-blinking
// while the on-board red LED (which keys off the code) went dark. Two
// indicators disagreeing. Confirmed on the bench during Phase 1b, 2026-07-31.
void power_request_fault_ack(void);

// Is a Pi actually present? This is what selects PS_RUNNING vs PS_BENCH_RUNNING.
//
// Decided by polling PI_3V3_SENSE across a WINDOW (PI_DETECT_WINDOW_MS), not by a
// single sample -- and revisited afterwards by the debounced late-detect
// promotion in PS_BENCH_RUNNING. A single sample meant a Pi merely slow to raise
// its header 3V3 was classified as absent, and in PS_BENCH_RUNNING a button press
// is a hard FORCE_OFF. See board.h.
bool power_pi_present(void);

// "Pi is down" indicator, two-of-three. Exposed for the CLI so you can watch it
// while testing the simulated shutdown.
bool power_pi_is_down(void);

// Milliseconds spent in the current state (for the CLI status line).
uint32_t power_state_elapsed_ms(void);

#endif // PITRAC_POWER_FSM_H
