// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
//
// The firing sequencer. See shot.h for why this exists before it does anything,
// and for the A9 ordering constraint it is here to make structural.
//
// STATUS 2026-08-28: skeleton. SHOT_IDLE and SHOT_ARMED are real; everything
// from SHOT_TRIGGERED on is a stub that falls through to SHOT_LOGGING. Phase 4
// fills in ANALYSING, Phase 7 fills in CAM_WAIT, Phase 6 fills in FIRING.
//
// It is wired into the superloop already so that the wiring is not a later
// change: shot_step() runs every pass and does nothing until armed.

#include "shot.h"
#include "board.h"
#include "detect.h"
#include "adc_engine.h"
#include "power_fsm.h"

#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

static shot_state_t  s_state = SHOT_IDLE;
static uint32_t      s_t_state;          // ms at entry to the current state
static uint32_t      s_seq;
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
static void enter(shot_state_t s) { s_state = s; s_t_state = now_ms(); }

const char *shot_state_name(shot_state_t s) {
    return (s < SHOT__COUNT && k_names[s]) ? k_names[s] : "?";
}
shot_state_t shot_state(void) { return s_state; }

void shot_init(void) {
    s_state = SHOT_IDLE;
    s_t_state = now_ms();
    s_seq = 0;
    s_have_last = false;
}

bool shot_arm(bool on) {
    if (on) {
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
        enter(SHOT_ARMED);
        return true;
    }

    detect_arm(false);
    adc_engine_set_mode(ADC_MODE_IDLE);
    detect_hpf_set(HPF_TRACK);
    enter(SHOT_IDLE);
    return true;
}

void shot_step(void) {
    switch (s_state) {

    case SHOT_IDLE:
        break;

    case SHOT_ARMED: {
        // detect_service() (called from pitrac_service, before this) closes a
        // pass once the coalesce window expires. A new pass is the trigger.
        detect_pass_t p;
        if (detect_last_pass(&p) && p.seq != s_last.seq) {
            memset(&s_last, 0, sizeof(s_last));
            s_last.seq  = p.seq;
            s_last.t_ms = p.t_ms;
            enter(SHOT_TRIGGERED);
        }
        break;
    }

    case SHOT_TRIGGERED:
        // PHASE 4 lands here: nothing to do yet beyond moving on.
        enter(SHOT_ANALYSING);
        break;

    case SHOT_ANALYSING:
        // PHASE 4 / PHASE 5 land here.
        //
        // 🔴 EVERYTHING that reads ch5 or ch7 must finish in this state. The
        // moment SHOT_FIRING selects BURST the ring restarts and both channels
        // become unreadable -- see the A9 note in shot.h. detect.c already does
        // the ch5 refinement inside close_pass(); the mic analysis will join it
        // here.
        enter(SHOT_CAM_WAIT);
        break;

    case SHOT_CAM_WAIT:
        // PHASE 7 lands here: assert D_Cam_Trigger, wait for both strobe
        // returns with a timeout, and record t_cam. ARCHITECTURE.md A3 says
        // this belongs in PIO, which also yields t_cam as a measured number
        // instead of a busy-wait.
        //
        // Until that exists there are no cameras to wait for.
        s_last.cam_ok   = true;
        s_last.t_cam_us = 0;
        enter(SHOT_FIRING);
        break;

    case SHOT_FIRING:
        // PHASE 6 lands here, and this is the ONLY place in the firmware that
        // may select ADC_MODE_BURST. Reaching it requires SHOT_ANALYSING to
        // have completed, which is what makes the A9 ordering structural rather
        // than a comment somebody has to remember.
        //
        // No strobe firmware exists yet, so nothing fires and the mode is left
        // alone -- selecting BURST here today would destroy ch5/ch7 for no
        // benefit.
        s_last.pulses_fired = 0;
        enter(SHOT_LOGGING);
        break;

    case SHOT_LOGGING:
        s_seq++;
        s_have_last = true;
        // Straight back to armed. The pass itself is already in detect.c's log;
        // shot_result_t carries only what the sequencer knows and detect does
        // not -- the camera latency and the pulse count.
        enter(SHOT_ARMED);
        break;

    case SHOT_ABORT:
        // Unwind to a safe state and stay disarmed. Whoever set SHOT_ABORT is
        // responsible for having set abort_reason first.
        shot_arm(false);
        break;

    default:
        enter(SHOT_ABORT);
        break;
    }
}

bool shot_last(shot_result_t *out) {
    if (!s_have_last || !out) return false;
    *out = s_last;
    return true;
}
