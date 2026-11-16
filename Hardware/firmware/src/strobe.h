// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// strobe.h -- Phase 6: the strobe pulse engine, the current-setpoint DAC, and
// live current (6c/6d).
//
//   GPIO25  Strobe_Pulse  PIO0 SM0 -> U5 74LVC1G123 (clamp) -> U8 MCP1416 -> R64 -> Q10 gate
//   GPIO28  Gate_PWM      PWM 6A -> R55/C52, R58/C53 -> U6A x3 -> U7 -> TP3 -> R63 -> Q9 gate
//   GPIO40  ADC0          TP4 CurrentSense, 135 mV/A, read back after every live firing
//
// "When" and "how much" are separate controls (BENCH_P6_STROBE.md): Q10 switches,
// Q9 sits at a DC gate voltage that sets the current. LED-bank current needs BOTH.
//
// TWO MODES (policy in strobe_plan.h):
//   DRY (default) -- pulses need the gate at zero, and the gate rises only while
//     no pulse can start, so nothing can command strobe current whatever is on J3.
//   LIVE -- `strobe live on confirm`, from gate 0 only. Pulses fire with the
//     setpoint, under the staircase rule, the ceiling, the interval and charge
//     budget, with the watchdog armed unless a Pi is powered (image 1.8; see
//     strobe_plan.h, "A PI ON J8"); every firing is read back on ADC0 and
//     judged (strobe_live.h). Overcurrent or current outside the pulse latches a
//     fault and drops live mode with the gate at zero.
//
// No SDK headers here, on purpose: power_fsm.c and service.c include this file,
// and power_fsm.c is also compiled into the native host tests.

#ifndef PITRAC_STROBE_H
#define PITRAC_STROBE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "strobe_plan.h"
#include "strobe_live.h"

// After pio_alloc_init() and panel_init(). Leaves GPIO25 SIO low and GPIO28 on
// PWM at level 0. Panics if GPIO12 is still on PWM (A7).
void strobe_init(void);

// Drop everything: GPIO25 to SIO low first, then stop the state machine and the
// DMA, then the gate DAC to zero, then live mode. Safe from any state, before or
// after init, and re-entrant. The power FSM calls it on every route to rail-down.
void strobe_safe_off(void);

// --- Gate DAC (6b; live 6c) -------------------------------------------------
// level is 0..DAC_TOP+1 (1024 = 100 %). Admitted levels block for DAC_SETTLE_MS
// while servicing the FSM. Level 0 is always admitted. While live, a raise goes
// through strobe_live_may_set_gate() instead of the dry rule.
strobe_refusal_t strobe_gate_set(uint16_t level);
uint16_t         strobe_gate_level(void);

// --- Pulse engine (6a) ------------------------------------------------------
typedef enum {
    STROBE_RUN_OK = 0,
    STROBE_RUN_INVALID,        // failed strobe_validate_burst() or encoding
    STROBE_RUN_REFUSED,        // failed strobe_may_fire()
    STROBE_RUN_TIMEOUT,        // IRQ0 never arrived
    STROBE_RUN_ABORT_POWER,    // rail went down / strobe_safe_off() ran mid-burst
    STROBE_RUN_ABORT_FAULT,
    STROBE_RUN_ABORT_KEY,
    STROBE_RUN_NO_ADC,         // live: the ADC0 ring did not record a baseline in time, or was
                               // not ticking at the enable -- NOT fired
    STROBE_RUN_NO_FEED,        // the PIO feed's initial fill did not land, or the feed DMA's
                               // last stop did not complete -- NOT fired (dry or live)
    // Camera-gated bursts (Phase 7):
    STROBE_RUN_GATE_HELD,      // stopped with the camera gate still closed -- NOT fired
    STROBE_RUN_GATE_CUT,       // stopped after the gate opened, before IRQ0 -- cut short
    STROBE_RUN_NO_GATE,        // the state machine was not holding at the gate after the
                               // enable -- NOT fired
} strobe_run_t;

// What a camera-gated burst's engine showed when it was stopped (H7-F2,
// independent review 2026-11-10). Read with GPIO25 already back on SIO low and the
// state machine already disabled, so the PC is frozen. The burst program never
// jumps back to the gate, so a PC still on it means nothing past the gate ever ran.
typedef enum {
    STROBE_GATE_UNKNOWN = 0,   // no read-back was taken (not a gated run, or not started)
    STROBE_GATE_CLOSED,        // PC on the gate and no IRQ0: the burst never started
    STROBE_GATE_OPENED,        // PC past the gate, or IRQ0: the burst started; pulses may
                               // have reached GPIO25
} strobe_gate_seen_t;

const char *strobe_gate_seen_text(strobe_gate_seen_t g);

typedef struct {
    bool               valid;          // false until the first run attempt
    bool               live;           // fired in live mode, with ADC0 readback
    strobe_run_t       outcome;
    strobe_refusal_t   refusal;
    strobe_burst_err_t burst_err;
    strobe_burst_t     burst;
    uint32_t           max_width_us;
    uint32_t           words;
    uint32_t           expected_us;    // enable to IRQ0, nominal
    uint32_t           observed_us;    // enable to IRQ0 SEEN -- coarse, polled
    bool               irq_seen;
    bool               pin_low_after;
    // Live only:
    uint16_t           gate_level;     // the setpoint it fired at
    float              booked_mc;      // charge for the budget (design current)...
    bool               booked;         // ...actually booked: after admission and the ring wait
    bool               readback_err;   // ADC FIFO overflow (FCS.OVER) or a conversion-error flag
    uint32_t           ring_first_us;  // BURST switch -> first ADC0 sample written (UINT32_MAX: none)
    uint32_t           ring_ready_us;  // ... -> STROBE_LIVE_RING_READY_SAMPLES written, or gave up
    // Camera-gated only (Phase 7): the gate, as the stopped engine showed it.
    strobe_gate_seen_t gate;
} strobe_result_t;

// Validate, check admission, then run one uniform burst on PIO0 SM0, DMA-fed,
// and wait for IRQ0 while servicing the FSM. max_width_us is STROBE_SW_MAX_US
// except for the clamp test (STROBE_CLAMPTEST_MAX_US).
//
// DRY or LIVE by the current mode. Live adds: the live policy, the readback
// window (STROBE_BURST_SPAN), the 6d rule for a clamp test, ADC0 in BURST
// around the firing -- fired only once the ring has provably written
// STROBE_LIVE_RING_READY_SAMPLES of baseline, else STROBE_RUN_NO_ADC with
// nothing fired and live mode ended -- and the verdict, which can latch a fault
// and drop live mode. strobe_live_last() has the measurement.
strobe_result_t strobe_run(const strobe_burst_t *b, uint32_t max_width_us);
const char     *strobe_run_text(strobe_run_t r);

// --- Camera-gated burst (Phase 7: BENCH_P5_P7_MIC_CAMERA.md 7a/7b) -------------
// The burst is loaded and the state machine started on strobe_gate.pio's
// `wait 1 irq 4`, so it fires only when the camera handshake (camera.h) says
// FIRE -- at max(first delay, t_cam), in hardware -- and never if it times out.
// NON-BLOCKING, unlike strobe_run(): shot.c arms it, starts the handshake and
// polls both from the superloop.
//
// DRY ONLY in this firmware: refused while live mode is armed
// (STROBE_REFUSE_GATED_LIVE), and admitted by the same dry rule as strobe_run()
// (gate DAC provably zero, rail up, no fault, engine idle ...; a Pi powered is
// not a refusal from image 1.8). Live current from the shot path is a later,
// deliberate change.
//
// Arm: true with the engine holding at the gate (state HELD); false with *r
// saying why (INVALID, REFUSED, NO_FEED, NO_GATE) and nothing fired.
// gate_window_us: how long the gate may be held before the CPU stops it -- the
// handshake's own timeout, plus its margin.
bool strobe_gated_arm(const strobe_burst_t *b, uint32_t gate_window_us, strobe_result_t *r);

typedef enum {
    STROBE_GATED_IDLE = 0,     // nothing armed since the last result was collected
    STROBE_GATED_HELD,         // armed; the gate has not opened
    STROBE_GATED_RUNNING,      // the gate opened; the burst is playing
    STROBE_GATED_FINISHED,     // over: *r has the outcome (OK = IRQ0 seen)
} strobe_gated_t;

// Poll. FINISHED is reported until the next arm. strobe_safe_off() ends a gated
// burst as ABORT_POWER. Every way a gated burst ends records r->gate, read from
// the stopped engine (strobe_gate_seen_t) -- never inferred from the outcome.
strobe_gated_t strobe_gated_poll(strobe_result_t *r);

// Stop it now, if it is still running, and return the gate as the stopped engine
// showed it: CLOSED means nothing fired. If it had already ended (safe-off, a
// poll), return what was recorded THEN. UNKNOWN if no gated burst ran. Calling it
// again returns the same. *r: GATE_HELD (closed), GATE_CUT (opened), OK (IRQ0 had
// come), or the earlier outcome.
strobe_gate_seen_t strobe_gated_cancel(strobe_result_t *r);

// --- Live mode (6c/6d) --------------------------------------------------------
typedef enum {
    STROBE_LIVE_EXIT_NONE = 0,     // never armed since boot
    STROBE_LIVE_EXIT_OPERATOR,     // `strobe live off`
    STROBE_LIVE_EXIT_SAFE_OFF,     // `strobe off`, or the power FSM took the rail down
    STROBE_LIVE_EXIT_CONDITION,    // a hold condition failed -- see exit_refusal
    STROBE_LIVE_EXIT_IDLE,         // STROBE_LIVE_IDLE_TIMEOUT_MS with no strobe command
    STROBE_LIVE_EXIT_OVERCURRENT,  // FAULT_STROBE_OVERCURRENT latched
    STROBE_LIVE_EXIT_CLAMP,        // FAULT_STROBE_CLAMP latched
    STROBE_LIVE_EXIT_READBACK,     // no usable ADC0 record
    STROBE_LIVE_EXIT_RUN,          // timeout or abort mid-firing
    STROBE_LIVE_EXIT_SHAPE,        // NOISY or SHAPE: not one clean pulse per pulse fired
} strobe_live_exit_t;

const char *strobe_live_exit_text(strobe_live_exit_t e);

// `strobe live on confirm`. Admitted by strobe_live_may_arm(). Arms the
// watchdog if it is off and no Pi is powered (image 1.8), and disarms it again
// on exit. Records whether a Pi is present: the session is bound to it. Starts
// the staircase, the budget and the idle timer from zero.
strobe_refusal_t strobe_live_arm(void);
// `strobe live off`: gate to 0, engine stopped, watchdog restored.
void strobe_live_disarm(void);
bool strobe_live_armed(void);

// `wdog on` while live: the operator wants the watchdog for its own sake, so
// live mode must not disarm it on exit.
void strobe_live_keep_watchdog(void);

// Live mode is armed WITHOUT a Pi, so it needs the watchdog: `wdog off` refuses.
// False with live off, or for a session armed with a Pi (image 1.8).
bool strobe_live_watchdog_required(void);

// From pitrac_service(), every pass. Drops live mode if a hold condition fails
// or the idle timer runs out. Does nothing during a firing.
void strobe_live_service(void);

// How many times live mode has ended, by ANY route (counted in one place, so
// the power FSM's teardown is included). The CLI acknowledges each one when it
// prints a notice, and checks before and after every command, so an exit is
// announced exactly once. 2026-10-12: an idle timeout went unannounced and the
// operator's next `strobe pulse` fired dry; the independent review found the
// first fix could also double-print one and miss a power-FSM teardown.
uint32_t strobe_live_exit_count(void);

// The measurement of the most recent live firing (valid == false before one).
typedef struct {
    bool               valid;
    uint16_t           gate_level;
    strobe_burst_t     burst;
    bool               readback_err;   // the freeze could not prove the record complete...
    uint32_t           freeze_flags;   // ...and why: ADC_FREEZE_* (adc_engine.h)
    uint32_t           ring_first_us;  // see strobe_result_t
    uint32_t           ring_ready_us;
    size_t             pre_by_time;    // the two baseline splits; meas.pre_samples is the smaller
    size_t             pre_by_index;
    // Coverage: between the bracketed snapshots at the enable and after the post
    // window, the DMA wrote cov_samples; cov_span_lo is SysTick cycles between
    // the brackets' earlier readings, cov_b1 / cov_b2 the brackets' widths.
    size_t             cov_samples;
    uint32_t           cov_span_lo;
    uint32_t           cov_b1, cov_b2;
    uint32_t           cov_cps;        // cycles per sample (300 at 150 MHz)
    bool               cov_snapshots;  // both taken, same ring generation
    bool               covered;        // ...and strobe_live_coverage_ok()
    strobe_live_meas_t meas;
} strobe_live_shot_t;

const strobe_live_shot_t *strobe_live_last(void);

// The ADC0 record of the most recent live firing, oldest first, 2 us apart.
size_t strobe_live_wave(const uint16_t **samples);

// --- `strobe cal [A]` --------------------------------------------------------
typedef struct {
    uint16_t level;
    float    amps;        // max plateau of the 20 us pulse (0 if nothing detected)
    bool     detected;
} strobe_cal_point_t;

typedef enum {
    STROBE_CAL_RES_SOLVED = 0,
    STROBE_CAL_RES_REFUSED,      // a firing or a gate step was refused -- see refusal
    STROBE_CAL_RES_OVERSHOOT,    // a step measured over STROBE_CAL_ABORT_RATIO x target
    STROBE_CAL_RES_CEILING,      // reached STROBE_LIVE_GATE_CEILING below the target
    STROBE_CAL_RES_LIVE_LOST,    // live mode dropped mid-cal (fault, rail, verdict)
    STROBE_CAL_RES_ABORTED,      // key or fault during a yield
    STROBE_CAL_RES_TOO_MANY,     // STROBE_CAL_MAX_POINTS without an answer
} strobe_cal_res_t;

typedef struct {
    bool             valid;          // false until the first `strobe cal`
    strobe_cal_res_t res;
    strobe_refusal_t refusal;
    float            target_a;
    uint16_t         solved_level;
    float            confirm_a[STROBE_CAL_CONFIRM_PULSES];
    uint32_t         n_confirm;
    float            confirm_mean_a;
    bool             confirm_ok;     // every confirmation within STROBE_CAL_CONFIRM_TOL
    uint16_t         session_cap;    // the cap this cal set (board.h); 0 unless SOLVED
} strobe_cal_t;

// Called after every measured step and confirmation pulse, for progress output.
typedef void (*strobe_cal_report_fn)(const strobe_cal_point_t *p, bool confirm);

// Live only. Steps the gate up from 0 with one 20 us pulse per step, solves the
// level for target_a by interpolation, sets it, and fires the confirmation
// pulses. On anything but SOLVED the gate is left at 0. The table of points is
// kept in RAM (strobe_cal_points()) -- cfg cannot be saved between shots.
//
// The session cap (board.h): cleared when a cal starts, set when it SOLVES, from
// the first point at or above the target. Cleared again by re-arming and by
// leaving live mode.
strobe_cal_t strobe_cal(float target_a, strobe_cal_report_fn report);

// The session cap in force now; 0 = none (no solved cal since live mode armed).
uint16_t     strobe_live_session_cap(void);
size_t       strobe_cal_points(const strobe_cal_point_t **pts);
const strobe_cal_t *strobe_cal_last(void);
const char  *strobe_cal_res_text(strobe_cal_res_t r);

// --- Status, for the CLI -----------------------------------------------------
typedef struct {
    bool              inited;
    bool              busy;
    uint16_t          gate_level;      // commanded
    uint16_t          gate_hw_level;   // read back from the PWM compare register
    bool              gate_pin_pwm;    // GPIO28 function is PWM
    bool              gate_settling;   // zeroed < DAC_SETTLE_MS ago; Q9's gate still decaying
    bool              pulse_pin_pio;   // GPIO25 function is PIO0 (only during a burst)
    bool              pulse_pin_pad;   // GPIO25 pad level
    bool              ready_led_sio;   // GPIO12 function is SIO (A7)
    bool              limit_out_sio;   // GPIO27: SIO, output
    bool              limit_out_level; // GPIO27: driven level (must be 0)
    bool              limit_pad;       // GPIO27: pad level (must be 0)
    unsigned          pio_offset;
    unsigned          gate_offset;       // strobe_gate.pio, at pio_offset - 1
    unsigned          sm_pc;             // the state machine's PC, read back
    bool              gated;             // a camera-gated burst is armed or playing
    bool              fire_flag;         // PIO0 flag 4 (the camera's FIRE), right now
    int               dma_channel;
    strobe_snapshot_t snap;
    strobe_result_t   last;
    // Live mode
    bool              live;
    bool              live_owns_wdog;    // live mode armed the watchdog and will disarm it
    strobe_live_t     live_in;           // the policy inputs right now
    uint32_t          live_for_ms;       // since arming
    uint32_t          idle_left_ms;      // before the idle timeout drops live mode
    float             budget_used_mc;    // in the current window
    strobe_live_exit_t live_exit;        // why live mode last ended
    strobe_refusal_t  live_exit_refusal; // for STROBE_LIVE_EXIT_CONDITION
} strobe_info_t;

void strobe_info(strobe_info_t *out);

#endif // PITRAC_STROBE_H
