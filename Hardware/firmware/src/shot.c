// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
//
// The firing sequencer. See shot.h for what is real (a test shot's camera
// handshake and dry camera-gated burst, 2026-11-10) and what is not (anything
// fired from a detected pass), and for the A9 ordering constraint.

#include "shot.h"
#include "board.h"
#include "camera.h"
#include "cam_plan.h"
#include "detect.h"
#include "adc_engine.h"
#include "power_fsm.h"
#include "safe_state.h"
#include "strobe.h"

#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include <string.h>

static shot_state_t  s_state = SHOT_IDLE;
static uint64_t      s_deadline_us;      // for the current state; 0 = none
static uint32_t      s_seq;
static uint32_t      s_last_pass_seq;    // detect.c's pass sequence already taken
static bool          s_have_last_pass;
static bool          s_test;             // the shot in progress is a test shot
static shot_result_t s_cur;              // the shot in progress
static shot_result_t s_last;
static bool          s_have_last;

static const char *const k_names[SHOT__COUNT] = {
    [SHOT_IDLE]      = "IDLE",
    [SHOT_ARMED]     = "ARMED",
    [SHOT_TRIGGERED] = "TRIGGERED",
    [SHOT_ANALYSING] = "ANALYSING",
    [SHOT_CAM_WAIT]  = "CAM_WAIT",
    [SHOT_FIRING]    = "FIRING",
    [SHOT_LOGGING]   = "LOGGING",
    [SHOT_ABORT]     = "ABORT",
};

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

// Enter a state with a deadline (0: none). Audit SHOT-01: every state that
// waits on hardware has one, so a stuck engine ends the shot instead of the
// sequencer sitting in CAM_WAIT forever with the trigger high.
static void enter(shot_state_t s, uint32_t deadline_us) {
    s_state = s;
    s_deadline_us = deadline_us ? time_us_64() + deadline_us : 0u;
}

static bool past_deadline(void) { return s_deadline_us && time_us_64() > s_deadline_us; }

const char *shot_state_name(shot_state_t s) {
    return (s < SHOT__COUNT && k_names[s]) ? k_names[s] : "?";
}
shot_state_t shot_state(void) { return s_state; }

bool shot_busy(void) { return s_state != SHOT_IDLE && s_state != SHOT_ARMED; }

const char *shot_end_text(shot_end_t e) {
    switch (e) {
        case SHOT_END_NONE:            return "none";
        case SHOT_END_FIRED:           return "FIRED: handshake complete, dry burst complete";
        case SHOT_END_HANDSHAKE:       return "HANDSHAKE: complete, no burst asked for";
        case SHOT_END_CAM_TIMEOUT:     return "CAM_TIMEOUT: trigger dropped, nothing fired, fault latched";
        case SHOT_END_REFUSED:         return "REFUSED: nothing was driven";
        case SHOT_END_ABORTED:         return "ABORTED: the rail went down, safe-off, or a fault latched mid-shot";
        case SHOT_END_ENGINE:          return "ENGINE FAULT: a PIO engine misbehaved; everything stopped, INTERNAL fault latched";
        case SHOT_END_NOT_IMPLEMENTED: return "NOT IMPLEMENTED: a detected pass has no ball speed yet; nothing timed or fired";
    }
    return "?";
}

void shot_init(void) {
    s_state = SHOT_IDLE;
    s_deadline_us = 0u;
    s_seq = 0;
    s_have_last = false;
    s_have_last_pass = false;
    memset(&s_cur, 0, sizeof s_cur);
}

bool shot_arm(bool on) {
    if (on) {
        if (s_state != SHOT_IDLE) return false;
        // The rail gates everything downstream: the strobe, the cameras and the
        // analog chain all run from switched +5 V.
        if (!power_rails_ready()) return false;

        // ARMED means the baseline is FROZEN. detect_arm() does not do this --
        // it only starts the PIO edge timer -- and arming with the HPF still in
        // TRACK leaves the 0.66 s servo fighting the transit.
        if (!detect_hpf_set(HPF_HOLD)) return false;
        if (!detect_arm(true))         return false;

        // ch5 (detect) + ch7 (mic) free-running. NOT burst: see the A9 note in
        // shot.h. BURST is entered exactly once per shot, from SHOT_FIRING.
        adc_engine_set_mode(ADC_MODE_ARMED);

        // Only passes after this one count.
        detect_pass_t p;
        s_have_last_pass = detect_last_pass(&p);
        if (s_have_last_pass) s_last_pass_seq = p.seq;
        s_test = false;
        enter(SHOT_ARMED, 0u);
        return true;
    }

    if (shot_busy()) return false;     // a shot in flight finishes first
    detect_arm(false);
    adc_engine_set_mode(ADC_MODE_IDLE);
    detect_hpf_set(HPF_TRACK);
    enter(SHOT_IDLE, 0u);
    return true;
}

// Stop both engines now: the trigger to SIO low, the burst stopped with GPIO25
// SIO low. Records the strobe gate as the stopped engine showed it -- or, if the
// burst had already ended (the power FSM's safe-off runs first), as it was
// recorded then.
static void stop_engines(void) {
    camera_safe_off();
    if (s_cur.req.strobe) s_cur.gate = strobe_gated_cancel(&s_cur.strobe);
    camera_poll(&s_cur.cam);
}

static void end_shot(shot_end_t e) {
    // The gate as the strobe engine recorded it when it stopped, on every path --
    // FIRED included -- so the shot and the strobe record never disagree.
    if (s_cur.req.strobe) s_cur.gate = s_cur.strobe.gate;
    s_cur.end = e;
    s_cur.ended_in = s_state;
    enter(SHOT_LOGGING, 0u);
}

static void engine_fault(void) {
    stop_engines();
    fault_raise(FAULT_INTERNAL);
    end_shot(SHOT_END_ENGINE);
}

bool shot_test_start(const shot_test_req_t *req, shot_result_t *out) {
    shot_result_t r;
    memset(&r, 0, sizeof r);
    r.test = true;
    r.end  = SHOT_END_REFUSED;
    if (req) r.req = *req;

    if (!req || s_state != SHOT_IDLE || camera_busy()) {
        r.cam_start = CAM_START_BUSY;
        goto refused;
    }

    // The handshake's hold covers the whole burst: it starts 4-5 us after FIRE
    // and ends before its IRQ0, which strobe_burst_duration_us() already counts.
    r.cam_req.first_delay_us = req->first_delay_us;
    r.cam_req.timeout_us     = req->timeout_us;
    r.cam_req.hold_us        = req->strobe
                             ? strobe_burst_duration_us(&req->burst) + CAM_HOLD_MARGIN_US
                             : req->hold_us;
    cam_plan_t plan;
    r.plan_err = cam_plan_make(&r.cam_req, clock_get_hz(clk_sys), &plan);
    if (r.plan_err != CAM_PLAN_OK) goto refused;

    // The strobe engine first, holding at its gate; then the camera, whose FIRE
    // opens it. In that order nothing can be missed: a FIRE before the gate was
    // armed is cleared by the arm, and there is no camera running yet to raise one.
    if (req->strobe &&
        !strobe_gated_arm(&req->burst, req->timeout_us + CAM_ENGINE_MARGIN_US, &r.strobe)) {
        r.gate = r.strobe.gate;
        // An arm that failed AFTER its engine ran past the gate is not a refusal:
        // it is an engine that did what its program cannot. Say so.
        if (r.gate == STROBE_GATE_OPENED) {
            fault_raise(FAULT_INTERNAL);
            r.end = SHOT_END_ENGINE;
        }
        goto refused;
    }

    r.cam_start = camera_start(&plan);
    if (r.cam_start != CAM_START_OK) {
        if (req->strobe) r.gate = strobe_gated_cancel(&r.strobe);
        if (r.gate == STROBE_GATE_OPENED) {       // nothing should have opened it
            fault_raise(FAULT_INTERNAL);
            r.end = SHOT_END_ENGINE;
        }
        goto refused;
    }
    camera_poll(&r.cam);

    s_cur  = r;
    s_cur.end = SHOT_END_NONE;
    s_test = true;
    enter(SHOT_CAM_WAIT, req->timeout_us + CAM_ENGINE_MARGIN_US);
    if (out) *out = s_cur;
    return true;

refused:
    // Logged like any other shot, so `cam` shows the last refusal too.
    r.seq  = ++s_seq;
    r.t_ms = now_ms();
    r.ended_in = s_state;
    s_last = r;
    s_have_last = true;
    if (out) *out = r;
    return false;
}

// SHOT_CAM_WAIT: the trigger is high and the strobe engine (if asked for) holds
// at its gate.
static void step_cam_wait(void) {
    cam_run_state_t c = camera_poll(&s_cur.cam);
    strobe_gated_t  g = STROBE_GATED_IDLE;
    if (s_cur.req.strobe) g = strobe_gated_poll(&s_cur.strobe);

    switch (c) {
    case CAM_RUN_READY:
    case CAM_RUN_DONE: {
        // Both strobes seen: FIRE is given or due. FIRING's deadline is the latest
        // the trigger can still be up, plus the margin.
        uint32_t hz = s_cur.cam.plan.sys_hz;
        uint32_t end_cycles = cam_release_after_fire_cycles(&s_cur.cam.plan,
                                                            s_cur.cam.res.fire_cycles);
        uint32_t end_us = (uint32_t)cam_cycles_to_us(end_cycles, hz) + 1u;
        enter(SHOT_FIRING, end_us + CAM_ENGINE_MARGIN_US);
        return;
    }
    case CAM_RUN_TIMEOUT:
        // The 7a abort path. The program dropped the trigger and raised no FIRE.
        // Stop the strobe engine and read its gate. CAM_TIMEOUT -- and its "nothing
        // fired" -- is certified only by a gate the stopped engine shows CLOSED.
        // Anything else contradicts the camera's report (independent review
        // 2026-11-10, H7-F2): an engine fault, never a clean timeout.
        if (s_cur.req.strobe) {
            s_cur.gate = strobe_gated_cancel(&s_cur.strobe);
            if (s_cur.gate != STROBE_GATE_CLOSED) {
                engine_fault();
                return;
            }
        }
        fault_raise(FAULT_CAM_TIMEOUT);
        end_shot(SHOT_END_CAM_TIMEOUT);
        return;
    case CAM_RUN_BAD_WORD:
    case CAM_RUN_NO_REPORT:
        engine_fault();
        return;
    case CAM_RUN_ABORTED:
        stop_engines();
        end_shot(SHOT_END_ABORTED);
        return;
    default:
        break;     // still WAITING
    }

    // The strobe engine ended while the camera still waits: the rail, safe-off
    // or a fault took it down. Nothing may continue.
    if (s_cur.req.strobe && g == STROBE_GATED_FINISHED) {
        stop_engines();
        end_shot(s_cur.strobe.outcome == STROBE_RUN_ABORT_POWER ||
                 s_cur.strobe.outcome == STROBE_RUN_ABORT_FAULT ||
                 fault_current() != FAULT_NONE ? SHOT_END_ABORTED : SHOT_END_ENGINE);
        if (s_cur.end == SHOT_END_ENGINE) fault_raise(FAULT_INTERNAL);
        return;
    }
    if (past_deadline()) engine_fault();
}

// SHOT_FIRING: FIRE is given or due; the trigger is held; the burst plays.
static void step_firing(void) {
    cam_run_state_t c = camera_poll(&s_cur.cam);
    if (c == CAM_RUN_BAD_WORD || c == CAM_RUN_NO_REPORT) { engine_fault(); return; }
    if (c == CAM_RUN_ABORTED) { stop_engines(); end_shot(SHOT_END_ABORTED); return; }

    if (!s_cur.req.strobe) {
        if (c == CAM_RUN_DONE) { end_shot(SHOT_END_HANDSHAKE); return; }
        if (past_deadline()) engine_fault();
        return;
    }

    strobe_gated_t g = strobe_gated_poll(&s_cur.strobe);

    // The camera has finished its hold -- so it passed its FIRE long ago -- and
    // the gate is still shut: the FIRE did not reach the strobe engine.
    if (c == CAM_RUN_DONE && g == STROBE_GATED_HELD) { engine_fault(); return; }

    if (g == STROBE_GATED_FINISHED) {
        strobe_run_t o = s_cur.strobe.outcome;
        if (o == STROBE_RUN_ABORT_POWER || o == STROBE_RUN_ABORT_FAULT) {
            stop_engines();
            end_shot(SHOT_END_ABORTED);
            return;
        }
        if (o != STROBE_RUN_OK) { engine_fault(); return; }
        if (c == CAM_RUN_DONE) {
            s_cur.pulses_fired = (uint8_t)s_cur.strobe.burst.count;
            end_shot(SHOT_END_FIRED);
            return;
        }
        // The burst is over; the hold still runs (by CAM_HOLD_MARGIN_US). Wait.
    }
    if (past_deadline()) engine_fault();
}

void shot_step(void) {
    switch (s_state) {

    case SHOT_IDLE:
        break;

    case SHOT_ARMED: {
        // detect_service() (called from pitrac_service, before this) closes a
        // pass once the coalesce window expires. A new pass is the trigger.
        detect_pass_t p;
        if (detect_last_pass(&p) && (!s_have_last_pass || p.seq != s_last_pass_seq)) {
            s_have_last_pass = true;
            s_last_pass_seq  = p.seq;
            memset(&s_cur, 0, sizeof s_cur);
            s_cur.t_ms = p.t_ms;
            enter(SHOT_TRIGGERED, 0u);
        }
        break;
    }

    case SHOT_TRIGGERED:
        enter(SHOT_ANALYSING, 0u);
        break;

    case SHOT_ANALYSING:
        // 🔴 EVERYTHING that reads ch5 or ch7 must finish in this state. The
        // moment a shot selects BURST the ring restarts and both channels become
        // unreadable -- see the A9 note in shot.h. detect.c already does the ch5
        // refinement inside close_pass(); the mic analysis will join it here.
        //
        // Then the shot needs the ball speed (for the S15 schedule) and the
        // first delay (from the beam-to-FOV geometry). Neither exists yet, so a
        // detected pass ends here, said plainly (audit SHOT-02). It used to
        // fall through CAM_WAIT and FIRING claiming cam_ok with 0 pulses.
        end_shot(SHOT_END_NOT_IMPLEMENTED);
        break;

    case SHOT_CAM_WAIT:
        step_cam_wait();
        break;

    case SHOT_FIRING:
        step_firing();
        break;

    case SHOT_LOGGING:
        s_cur.seq  = ++s_seq;
        if (!s_cur.t_ms) s_cur.t_ms = now_ms();
        s_cur.test = s_test;
        s_last = s_cur;
        s_have_last = true;
        enter(s_test ? SHOT_IDLE : SHOT_ARMED, 0u);
        s_test = false;
        break;

    case SHOT_ABORT:
    default:
        stop_engines();
        end_shot(SHOT_END_ABORTED);
        break;
    }
}

bool shot_last(shot_result_t *out) {
    if (!s_have_last || !out) return false;
    *out = s_last;
    return true;
}
