// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// shot.h -- the firing sequencer. One shot, start to finish.
//
// WHY THIS MODULE EXISTS
//
// Phases 4, 6 and 7 all need the same sequence:
//
//   comparator edge -> ADC refinement (ch5) + mic analysis (ch7)
//                   -> camera handshake -> strobe burst -> log
//
// That belongs to none of the other modules. detect.c is the edge timer,
// power_fsm.c is power, cli.c is the operator interface. With nowhere to live,
// it would have leaked into cli.c one command at a time.
//
// STATUS 2026-11-10 (Phase 7 firmware):
//   - CAM_WAIT and FIRING are real, for a TEST shot started from the CLI
//     (`cam shot`, `cam trig`; BENCH_P5_P7_MIC_CAMERA.md 7a/7b): the camera
//     handshake in PIO1 (camera.h) and a DRY camera-gated burst in PIO0
//     (strobe_gated_arm()). The strobe fires at max(first delay, t_cam), timed in
//     hardware; a camera timeout latches FAULT_CAM_TIMEOUT and fires nothing.
//   - A DETECTED pass (SHOT_ARMED, reached only through shot_arm(), which nothing
//     calls yet) ends SHOT_END_NOT_IMPLEMENTED after ANALYSING: there is no ball
//     speed, so no schedule and no first delay to fire with (audit SHOT-02).
//   - Every state that waits has a deadline (audit SHOT-01).
//
// ---------------------------------------------------------------------------
// THE ORDERING CONSTRAINT THIS MODULE OWNS (ARCHITECTURE.md A9)
//
// The ADC ring is ONE resource with mode-scoped contents. Entering BURST
// restarts it and changes the stride, so every ch5 and ch7 sample already
// captured becomes UNREADABLE -- not stale, unreadable.
//
// So the firing path MUST finish all ch5/ch7 analysis before switching to
// BURST. shot_step() is the only place that should ever select BURST for a shot,
// and only from SHOT_FIRING, which a detected pass reaches only after
// SHOT_ANALYSING. The test shot fires DRY and never touches the ADC mode.
// ---------------------------------------------------------------------------

#ifndef PITRAC_SHOT_H
#define PITRAC_SHOT_H

#include <stdbool.h>
#include <stdint.h>

#include "cam_plan.h"
#include "camera.h"
#include "strobe.h"

typedef enum {
    SHOT_IDLE = 0,     // nothing running. Nothing is watching the comparator.
    SHOT_ARMED,        // HPF in HOLD, detect armed, waiting for an edge
    SHOT_TRIGGERED,    // an edge closed a pass; hand it to analysis
    SHOT_ANALYSING,    // ch5 refinement + ch7 mic. MUST complete before BURST
    SHOT_CAM_WAIT,     // trigger high, waiting on both strobe returns (camera.h)
    SHOT_FIRING,       // FIRE given or due; the burst and the hold running
    SHOT_LOGGING,      // record the shot, then back to IDLE (test) or ARMED
    SHOT_ABORT,        // something unexpected; unwind safely
    SHOT__COUNT
} shot_state_t;

const char *shot_state_name(shot_state_t s);
shot_state_t shot_state(void);

void shot_init(void);

// Stepped from the superloop, next to detect_service(). Non-blocking: every
// state either completes immediately or checks a deadline and returns.
//
// NOTHING IN THE FIRING PATH MAY BLOCK. It runs from pitrac_service(), which also
// runs inside every blocking command's yield -- so it must never call anything
// that yields (strobe_run(), strobe_gate_set(), pitrac_yield_ms()).
void shot_step(void);

// Arm / disarm the detected-pass sequence. Distinct from detect_arm(), which only
// starts the PIO edge timer: this also owns the HPF mode and the ADC mode.
// Nothing calls it yet (no CLI path): a detected pass has nothing to fire with.
bool shot_arm(bool on);

typedef enum {
    SHOT_END_NONE = 0,
    SHOT_END_FIRED,            // both strobes seen, FIRE, the dry burst completed (IRQ0)
    SHOT_END_HANDSHAKE,        // both strobes seen, FIRE, trigger released; no burst asked for
    SHOT_END_CAM_TIMEOUT,      // FAULT_CAM_TIMEOUT latched: trigger dropped, and (with a burst
                               // armed) the stopped strobe engine PROVED its gate never opened
    SHOT_END_REFUSED,          // the plan, the camera or the strobe refused: nothing was driven
    SHOT_END_ABORTED,          // the rail went down, safe-off, or a fault latched, mid-shot
    SHOT_END_ENGINE,           // an engine misbehaved (bad word, no report, FIRE not reaching
                               // the gate, a burst timeout, a deadline, a camera timeout the
                               // strobe gate contradicts): FAULT_INTERNAL
    SHOT_END_NOT_IMPLEMENTED,  // a detected pass: no ball speed yet, nothing timed or fired
} shot_end_t;

const char *shot_end_text(shot_end_t e);

// A test shot: no beam, no detector -- the shot starts at the camera handshake.
typedef struct {
    uint32_t       first_delay_us;  // trigger -> FIRE when the cameras are already exposing
    uint32_t       timeout_us;      // trigger -> CAM_TIMEOUT
    bool           strobe;          // false: the handshake alone (`cam trig`)
    strobe_burst_t burst;           // the dry burst, when strobe
    uint32_t       hold_us;         // FIRE -> trigger released, when !strobe (with a
                                    // burst: its duration + CAM_HOLD_MARGIN_US)
} shot_test_req_t;

typedef struct {
    uint32_t         seq;
    uint32_t         t_ms;
    bool             test;           // a test shot, not a detected pass
    shot_end_t       end;
    shot_state_t     ended_in;       // the state it ended from
    shot_test_req_t  req;
    cam_req_t        cam_req;        // what the camera engine was asked
    cam_plan_err_t   plan_err;       // REFUSED by the plan
    cam_start_t      cam_start;      // REFUSED by the camera engine
    cam_run_t        cam;            // the handshake's record (camera.h)
    strobe_result_t  strobe;         // the gated burst's record, when req.strobe
    strobe_gate_seen_t gate;         // the strobe gate as the stopped engine showed it
                                     // (strobe.h); UNKNOWN when no burst was armed
    uint8_t          pulses_fired;   // the burst's count when it completed, else 0
} shot_result_t;

// Start a test shot. Returns at once. false: refused and nothing was driven;
// *out says why. true: running -- poll shot_busy(), then shot_last().
bool shot_test_start(const shot_test_req_t *req, shot_result_t *out);

// A shot is between its start and its log (not IDLE, not ARMED).
bool shot_busy(void);

bool shot_last(shot_result_t *out);

#endif // PITRAC_SHOT_H
