// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// power_fsm.h -- Phase 1 (latch) + Phase 1b (Pi soft-shutdown).
//
// This is the highest-risk-to-the-Pi code in the project: the +5V latch IS the
// Pi's power switch. Every failure path here is meant to be proven against a
// SIMULATED Pi (jumper wires or a spare Pico) before a real one is ever seated.
//
// Simulating the Pi, from the plan:
//   RPI5_ON        GPIO0   <- J8.15 via R29 1K   : jumper J8.15 -> J8.1 (+3V3)
//   PI_3V3_SENSE   GPIO24  <- R45/R44 divider    : feed 3.3 V into the top of R45
//   RPI5_SHUTDOWN  GPIO43  -> J8.37 via R39 1K   : scope it. Driven only while
//                  the latch is closed and PI_3V3_SENSE reads up; released
//                  otherwise (board.h, "THE LINE'S REST STATE").
//   UART1          GPIO36/37 <-> J8.10/J8.8 via 220R : connected only while the
//                  latch is closed, PI_3V3_SENSE reads up and RPI5_ON is high;
//                  released otherwise (safe_state.h).

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

// Start only from STANDBY with no pending stop. False means no start was queued.
bool power_request_on(void);
// Cancels a pending start. During startup, shutdown waits for the normal ready
// state; in STANDBY it leaves no request for a future power cycle.
void power_request_shutdown(void);
void power_request_force_off(void);   // cancels pending requests; skips the orderly sequence

// The shutdown a Pi may ask for itself (`off` over the link; image 1.14, review
// finding V113-F4): queued only when it will be the ORDERLY one -- RUNNING now,
// so the next step is begin_shutdown() -- and true then. Anywhere else nothing
// is queued and it returns false: in FAULT and BENCH_RUNNING the same request
// drops the latch at once, and before RUNNING it would wait to be consumed in
// whichever of those the FSM reaches. A request queued in RUNNING cannot be
// consumed anywhere else: RUNNING's only exits are begin_shutdown(), which takes
// it, and FORCE_OFF, which clears every request. power_request_shutdown(), for
// USB and the button's equivalent, is unchanged.
bool power_request_shutdown_orderly(void);

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

// The Pi's header 3V3 as sensed right now (PI_3V3_SENSE, GPIO24), unfiltered.
// power_pi_present() above is the FSM's debounced, conservative classification;
// the strobe's live policy needs both (strobe_pi_observe(); image 1.9, V18-F1).
bool power_pi_3v3_sensed(void);

// MAY a Pi be on J8 right now? The FSM classifies one, OR its 3V3 is sensed now:
// when the two disagree, the answer is yes. For every guard that must not act
// against a powered Pi -- `reset`, `bootsel`, `cfg save` and the flash writes
// behind its guard (image 1.10, review finding V19-F1).
//
// power_pi_present() alone is not enough there. It stays false while a Pi's 3V3
// is already up in two places: in POWERING_ON until RAIL_SETTLE_MS, and in
// BENCH_RUNNING for PI_PRESENT_DEBOUNCE_MS of late detection. A plain `reset`
// was admitted in that window, and its reset opens the latch under the Pi.
// The FSM's own states, debounces and shutdown decisions are unchanged.
bool power_pi_maybe_present(void);

// "Pi is down" indicator, two-of-three. Exposed for the CLI so you can watch it
// while testing the simulated shutdown.
bool power_pi_is_down(void);

// RUNNING, and the Pi has read down for PI_DOWN_DEBOUNCE_MS with no request from
// the board (BENCH_P8_PI.md 8.6). Counted in the halt telemetry; the rail stays
// up. *for_ms (optional) is how long ago it was counted.
bool power_pi_down_unrequested(uint32_t *for_ms);

// Milliseconds spent in the current state (for the CLI status line).
uint32_t power_state_elapsed_ms(void);

#endif // PITRAC_POWER_FSM_H
