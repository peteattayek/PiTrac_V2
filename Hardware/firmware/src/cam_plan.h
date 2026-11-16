// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// cam_plan.h -- Phase 7: the camera handshake's ARITHMETIC. No SDK headers and no
// hardware access, so it is compiled into the native host tests as well as the
// firmware (tests/cam_plan_test.c, and tests/cam_pio_test.c, which runs the real
// assembled cam_handshake.pio in a cycle-counting simulator and checks every
// number below against it).
//
// THE HANDSHAKE (cam_handshake.pio, PIO1; ARCHITECTURE.md A3)
//
//   t = 0       D_Cam_Trigger (GPIO10) goes high.
//   wait        both Cam_Strobe inputs (GPIO8, GPIO9) high = both cameras exposing.
//   FIRE        at max(first delay, t_cam): `irq prev set 4` opens the strobe
//               engine's gate in PIO0 (strobe_gate.pio), and the burst starts.
//   hold        the trigger stays high for the hold time after FIRE, then drops.
//   timeout     no t_cam within the timeout: the trigger drops, NOTHING fires.
//
// It is the .md's `delay_us(max(0, first_delay_us - t_cam))`, done by a state
// machine: the first delay keeps counting while the cameras respond, so a t_cam
// past the first delay fires at once (clamped to zero, never an underflow), and
// one shorter waits out the rest. No core spins, and t_cam is a measured number.
//
// The state machine runs at the full system clock, and every loop in it is 5
// cycles (33.3 ns at 150 MHz). Each poll takes ONE sample of both strobes
// (independent review 2026-11-10, H7-F1). The CPU sends three counts, in loops:
//   D  the first delay      T  the late window after it      H  the hold
// and gets back one word when both strobes are seen (or one on timeout), then
// one when the trigger drops.

#ifndef PITRAC_CAM_PLAN_H
#define PITRAC_CAM_PLAN_H

#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Cycle counts, from cam_handshake.pio's listing. Cycle 0 is the `set pins, 1`
// that raises the trigger. PROVEN by tests/cam_pio_test.c, which runs the
// assembled program: change the program and that test says which of these moved.
// ---------------------------------------------------------------------------
#define CAM_PIO_LOOP_CYCLES          5u   // every poll, delay and hold loop
#define CAM_PIO_EARLY_POLL0_CYCLES   1u   // early poll k samples both strobes at 1 + 5k
#define CAM_PIO_FIRE_EARLY_CYCLES   14u   // ready in the early phase: FIRE at 5D + 14
#define CAM_PIO_LATE_POLL0_CYCLES    9u   // late poll j samples at 5D + 9 + 5j
#define CAM_PIO_FIRE_LATE_CYCLES    13u   // ready in the late phase: FIRE 13 after that sample
#define CAM_PIO_HOLD_RELEASE_CYCLES  9u   // trigger low at FIRE + 5H + 9
#define CAM_PIO_TIMEOUT_RELEASE_CYCLES 18u // timeout: trigger low at 5D + 5T + 18

// The words the state machine pushes. A ready word carries the remaining count:
// X in the early phase (top bit clear), ~X in the late phase (top bit set).
// D, T and H are kept at or under CAM_COUNT_MAX so the ranges cannot overlap.
#define CAM_WORD_TIMEOUT  0x80000000u
#define CAM_WORD_END      0xFFFFFFFFu
#define CAM_COUNT_MAX     0x3FFFFFFFu

// What a handshake is asked to do, in microseconds.
typedef struct {
    uint32_t first_delay_us;  // trigger -> FIRE, when the cameras are already exposing
    uint32_t timeout_us;      // trigger -> give up: CAM_TIMEOUT. At least first_delay_us.
    uint32_t hold_us;         // FIRE -> trigger released
} cam_req_t;

// What the state machine is sent: the three counts, in 5-cycle loops.
typedef struct {
    uint32_t sys_hz;
    uint32_t d, t, h;
} cam_plan_t;

typedef enum {
    CAM_PLAN_OK = 0,
    CAM_PLAN_BAD_CLOCK,        // sys_hz outside CAM_SYS_HZ_MIN..MAX
    CAM_PLAN_FIRST_DELAY,      // over CAM_FIRST_DELAY_MAX_US
    CAM_PLAN_TIMEOUT,          // under CAM_TIMEOUT_MIN_US, over CAM_TIMEOUT_MAX_US, or under the first delay
    CAM_PLAN_HOLD,             // under CAM_HOLD_MIN_US or over CAM_HOLD_MAX_US
} cam_plan_err_t;

#define CAM_SYS_HZ_MIN   10000000u
#define CAM_SYS_HZ_MAX  300000000u

cam_plan_err_t cam_plan_make(const cam_req_t *r, uint32_t sys_hz, cam_plan_t *out);
const char    *cam_plan_err_text(cam_plan_err_t e);

// Microseconds <-> cycles at sys_hz. cam_us_to_cycles() saturates at UINT32_MAX.
uint32_t cam_us_to_cycles(uint32_t us, uint32_t sys_hz);
float    cam_cycles_to_us(uint32_t cycles, uint32_t sys_hz);

// The nominal times the plan produces, in cycles from the trigger's rise.
uint32_t cam_fire_early_cycles(const cam_plan_t *p);           // 5D + 14
uint32_t cam_timeout_release_cycles(const cam_plan_t *p);      // 5D + 5T + 18
uint32_t cam_release_after_fire_cycles(const cam_plan_t *p,
                                       uint32_t fire_cycles);  // fire + 5H + 9

// Decoding the first word of a handshake.
typedef enum {
    CAM_OUT_EARLY = 0,   // both strobes seen within the first delay: FIRE at the first delay
    CAM_OUT_LATE,        // seen after it: FIRE at once (the delay clamped to zero)
    CAM_OUT_TIMEOUT,     // not seen within the timeout: trigger dropped, nothing fired
    CAM_OUT_BAD_WORD,    // a word the program cannot produce for this plan (a firmware fault)
} cam_outcome_t;

typedef struct {
    cam_outcome_t outcome;
    uint32_t      x;              // the remaining count the word carried
    uint32_t      detect_cycles;  // trigger -> the poll that saw both strobes high (t_cam,
                                  // to one loop: the sample that saw both)
    uint32_t      fire_cycles;    // trigger -> FIRE
    bool          delay_clamped;  // t_cam was past the first delay: FIRE at once
} cam_decoded_t;

cam_decoded_t cam_decode(uint32_t word, const cam_plan_t *p);
const char   *cam_outcome_text(cam_outcome_t o);

#endif // PITRAC_CAM_PLAN_H
