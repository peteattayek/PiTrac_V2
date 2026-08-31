// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// shot.h -- the firing sequencer. One shot, start to finish.
//
// WHY THIS MODULE EXISTS BEFORE IT DOES ANYTHING
//
// Phases 4, 6 and 7 all need the same sequence:
//
//   comparator edge -> ADC refinement (ch5) + mic analysis (ch7)
//                   -> camera handshake -> strobe burst -> log
//
// That belongs to none of the existing modules. detect.c is the edge timer,
// power_fsm.c is power, cli.c is the operator interface. With nowhere to live,
// it would have leaked into cli.c one command at a time -- which is exactly how
// dispatch() reached 1064 lines.
//
// So the skeleton lands first, wired into the superloop and doing nothing, and
// each phase fills in its own state.
//
// ---------------------------------------------------------------------------
// THE ORDERING CONSTRAINT THIS MODULE OWNS (ARCHITECTURE.md A9)
//
// The ADC ring is ONE resource with mode-scoped contents. Entering BURST
// restarts it and changes the stride, so every ch5 and ch7 sample already
// captured becomes UNREADABLE -- not stale, unreadable.
//
//   Phase 3/4 reads ch5 after the comparator edge
//   Phase 5   reads ch7 for the same shot
//   Phase 6   destroys both by selecting BURST
//
// So the firing path MUST finish all ch5/ch7 analysis before switching to
// BURST. The Phase 4 design happens to be ordered correctly, and nothing
// enforces it: the failure is silent, adc_ring_view() simply returns false and
// the pass is flagged DQ_NO_ADC with the ADC column quietly missing.
//
// A sequencer that owns the mode transition can make that ordering structural
// instead of a comment. shot_step() is the only place that should ever call
// adc_engine_set_mode(ADC_MODE_BURST), and it does so only from SHOT_FIRING,
// which is unreachable until SHOT_ANALYSING has completed.
// ---------------------------------------------------------------------------

#ifndef PITRAC_SHOT_H
#define PITRAC_SHOT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SHOT_IDLE = 0,     // disarmed. Nothing is watching the comparator.
    SHOT_ARMED,        // HPF in HOLD, detect armed, waiting for an edge
    SHOT_TRIGGERED,    // an edge closed a pass; hand it to analysis
    SHOT_ANALYSING,    // ch5 refinement + ch7 mic. MUST complete before BURST
    SHOT_CAM_WAIT,     // trigger asserted, waiting on both strobe returns (A3)
    SHOT_FIRING,       // BURST mode, strobe schedule running
    SHOT_LOGGING,      // record the pass, restore ADC mode, re-arm or idle
    SHOT_ABORT,        // something timed out; unwind safely
    SHOT__COUNT
} shot_state_t;

const char *shot_state_name(shot_state_t s);
shot_state_t shot_state(void);

void shot_init(void);

// Stepped from the superloop, next to detect_service(). Non-blocking: every
// state either completes immediately or checks a deadline and returns.
//
// NOTHING IN THE FIRING PATH MAY BLOCK. A blocking wait here would stall the
// power FSM exactly the way the CLI commands used to -- see service.h -- and
// this path runs with the strobe armed, which is the worst possible time.
void shot_step(void);

// Arm / disarm the whole sequence. Distinct from detect_arm(), which only
// starts the PIO edge timer: this also owns the HPF mode, the ADC mode and the
// camera and strobe interlocks.
bool shot_arm(bool on);

// Per-shot outcome, for the Phase 4 comparison and the Phase 6 energy budget.
typedef struct {
    uint32_t seq;
    uint32_t t_ms;
    bool     cam_ok;         // both strobes returned inside the timeout
    uint32_t t_cam_us;       // measured camera latency (A3 gives this for free)
    uint8_t  pulses_fired;   // after the BURST_CHARGE_MAX interlock sheds any
    uint32_t abort_reason;   // 0 = none
} shot_result_t;

bool shot_last(shot_result_t *out);

#endif // PITRAC_SHOT_H
