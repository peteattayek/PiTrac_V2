// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// camera.h -- Phase 7: the camera handshake engine (cam_handshake.pio on PIO1).
//
//   GPIO10  D_Cam_Trigger  out, J4.3/4 via R18 220R   SIO low except during a handshake
//   GPIO8   Cam_Strobe_0   in,  J4.7   via R19 220R
//   GPIO9   Cam_Strobe_1   in,  J4.8   via R24 220R
//
// The arithmetic is cam_plan.h; this file owns the pins and the state machine.
// Everything here is NON-BLOCKING: shot.c starts a handshake and polls it from
// the superloop.
//
// 🔴 J4 is 3.3 V. A Mira220 (1.8 V I/O, not 3.3 V tolerant; NEXT_BOARD_REV.md
// CR-09) goes on it only through a TXS0108E module, as in BENCH_P5_P7_MIC_CAMERA.md
// 7c. Nothing here can tell what is on J4.
// The trigger is driven only with the +5 V rail up (a camera on J4 is powered
// from the Pi, which is on that rail), and it drops on every route to rail-down.
//
// No SDK headers here, on purpose: power_fsm.c includes it for camera_safe_off(),
// and power_fsm.c is compiled into the native host tests.

#ifndef PITRAC_CAMERA_H
#define PITRAC_CAMERA_H

#include <stdbool.h>
#include <stdint.h>

#include "cam_plan.h"

// After pio_alloc_init(). Leaves GPIO10 SIO low and the state machine stopped.
void camera_init(void);

// GPIO10 to SIO low FIRST, then the state machine stopped and cleared. Safe from
// any state, before or after init, and re-entrant. The power FSM calls it on
// every route to rail-down.
void camera_safe_off(void);

typedef enum {
    CAM_START_OK = 0,
    CAM_START_NOT_INIT,
    CAM_START_BUSY,             // a handshake is already running
    CAM_START_RAIL,             // the +5 V rail is not up
    CAM_START_FAULT,            // a fault is latched
    CAM_START_TRIGGER_HIGH,     // GPIO10 is already high ('gpio 10 0' first)
    CAM_START_STROBES_HIGH,     // a strobe input reads high BEFORE the trigger
} cam_start_t;

const char *camera_start_text(cam_start_t s);

// Start one handshake with plan `p` (cam_plan_make()). Returns at once: the
// trigger is high and the state machine is polling when this returns OK.
cam_start_t camera_start(const cam_plan_t *p);

// The trigger driven by hand (`gpio 10 <0|1>`; the 7a static loopback check).
// LOW is always admitted: it stops any handshake and leaves GPIO10 SIO low
// (camera_safe_off()). HIGH is admitted by the same rules as a handshake -- the
// rail up, no fault, initialised, no handshake running -- and leaves GPIO10 SIO
// high until a low (`gpio 10 0`, camera_safe_off()) or the next rail-down. A
// handshake does NOT end it: camera_start() refuses while the trigger is
// already high (CAM_START_TRIGGER_HIGH) and leaves it high. (Corrected in 1.6;
// the 1.5 confirm review's note.)
// Until 2026-11-10 `gpio 10 1` wrote the pad with no check at all, rail down
// included (independent review H7-F3). Refused: GPIO10 unchanged.
cam_start_t camera_manual_trigger(bool high);

typedef enum {
    CAM_RUN_NONE = 0,       // never started
    CAM_RUN_WAITING,        // trigger high, strobes not seen yet
    CAM_RUN_READY,          // both seen; FIRE at res.fire_cycles; trigger still high
    CAM_RUN_DONE,           // the trigger dropped after the hold (END)
    CAM_RUN_TIMEOUT,        // no strobes within the timeout: the trigger dropped, no FIRE
    CAM_RUN_BAD_WORD,       // the engine sent a word it cannot produce: stopped
    CAM_RUN_NO_REPORT,      // the engine did not report by its own end time: stopped
    CAM_RUN_ABORTED,        // camera_safe_off() ran mid-handshake
} cam_run_state_t;

typedef struct {
    bool            valid;           // a handshake has been started since boot
    cam_run_state_t state;
    cam_plan_t      plan;
    cam_decoded_t   res;             // the first word, decoded (valid from READY / TIMEOUT)
    uint32_t        words;           // words received
    uint32_t        word0, word1;
    uint64_t        t_start_us;      // when the state machine was enabled
    uint32_t        cpu_ready_us;    // start -> the CPU saw the first word (coarse, polled)
    uint32_t        cpu_end_us;      // start -> the CPU saw the end (coarse, polled)
    bool            trigger_low_after;   // GPIO10's pad read low once it was over
} cam_run_t;

const char *camera_run_text(cam_run_state_t s);

// Poll: drain the state machine's words and advance the run. Non-blocking.
// Returns the current state; *out (may be NULL) gets the whole record.
cam_run_state_t camera_poll(cam_run_t *out);

bool camera_busy(void);

// The most recent run (valid == false before the first).
const cam_run_t *camera_last(void);

// For `cam`: read back from the hardware (ARCHITECTURE.md A8), not mirrored.
typedef struct {
    bool     inited;
    bool     busy;
    unsigned pio_index;         // 1: PIO1
    unsigned sm;
    unsigned offset;            // where the program was loaded
    unsigned length;
    bool     sm_enabled;        // CTRL.SM_ENABLE
    unsigned pc;                // ADDR, absolute
    unsigned tx_level, rx_level;
    bool     trigger_pio;       // GPIO10 function is this PIO
    bool     trigger_pad;       // GPIO10 pad level
    bool     trigger_out_sio;   // GPIO10's SIO output level (what 'gpio 10' drives)
    bool     strobe0_pad, strobe1_pad;
    bool     fire_flag;         // PIO0 flag 4 right now (set and not yet taken by the gate)
    uint32_t sys_hz;
} camera_info_t;

void camera_info(camera_info_t *out);

// A label for an absolute PC value of the handshake program (poll, delay, ...).
const char *camera_pc_label(unsigned pc);

#endif // PITRAC_CAMERA_H
