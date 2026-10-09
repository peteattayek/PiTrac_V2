// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// strobe_plan.h -- everything about the strobe that is ARITHMETIC or POLICY.
//
// No SDK headers and no hardware access, deliberately: this file is compiled
// into the native host tests (tests/strobe_plan_test.c) as well as the firmware,
// so the schedule math, the PIO word encoding and the admission rules are all
// checked on a PC before anything is ever driven.
//
// strobe.c owns the hardware and asks this file two questions before touching
// it: "is this burst legal?" and "may I do this right now?".

#ifndef PITRAC_STROBE_PLAN_H
#define PITRAC_STROBE_PLAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// strobe_burst.pio timing, at 1 us per PIO cycle.
//
// Counted from the program listing:
//   high phase = `set pins,1` + (N+1) x `jmp x--` + `set pins,0`  -> N + 2 cycles
//   low phase  = `set pins,0` + pull, mov, (M+1) x `jmp y--`, jmp, pull, mov,
//                `jmp !x` ... up to the next `set pins,1`         -> M + 8 cycles
// so a word N gives a pulse of N + 2 us and a word M a gap of M + 8 us.
//
// CONFIRMED on board 1's logic analyser in BENCH_P6_STROBE 6a.2 (2026-10-06):
// 5 single pulses and 4 bursts matched every commanded width and period, so no
// correction. If a later board disagrees by a whole microsecond, these two
// numbers are what change.
//
// A width word of 0 is the end-of-burst sentinel, so the shortest pulse the
// program can produce is 3 us (word 1). The IRQ0 completion flag is raised
// 8 us after the last falling edge.
// ---------------------------------------------------------------------------
#define STROBE_PIO_WIDTH_OVERHEAD_US   2u
#define STROBE_PIO_GAP_OVERHEAD_US     8u
#define STROBE_PIO_MIN_WIDTH_US        (STROBE_PIO_WIDTH_OVERHEAD_US + 1u)
#define STROBE_PIO_IRQ_AFTER_LAST_US   STROBE_PIO_GAP_OVERHEAD_US

// A uniform burst: `count` pulses of `width_us`, rising edges `width + gap` apart.
typedef struct {
    uint32_t width_us;
    uint32_t gap_us;     // ignored when count == 1
    uint32_t count;
} strobe_burst_t;

// ---------------------------------------------------------------------------
// Charge.
//
// Evaluated at STROBE_TARGET_CURRENT_A, and on the EFFECTIVE width: U5 truncates
// every pulse at its clamp, so a 1 ms clamp-test request can deliver at most a
// clamp-width of charge -- STROBE_HW_LIMIT_US, the worst case across boards.
// ---------------------------------------------------------------------------
float strobe_burst_charge_mc(uint32_t width_us, uint32_t count);

typedef enum {
    STROBE_BURST_OK = 0,
    STROBE_BURST_WIDTH,      // outside [STROBE_MIN_WIDTH_US, max_width_us]
    STROBE_BURST_GAP,        // outside [STROBE_MIN_GAP_US, STROBE_MAX_GAP_US]
    STROBE_BURST_COUNT,      // outside [1, STROBE_BURST_MAX_PULSES]
    STROBE_BURST_CHARGE,     // over BURST_CHARGE_MAX_MC at the target current
    STROBE_BURST_SPAN,       // live only: longer than the ADC0 readback window
} strobe_burst_err_t;

// max_width_us is STROBE_SW_MAX_US for every normal pulse, and
// STROBE_CLAMPTEST_MAX_US only for the single 6a.1 clamp measurement.
strobe_burst_err_t strobe_validate_burst(const strobe_burst_t *b, uint32_t max_width_us);
const char *strobe_burst_err_text(strobe_burst_err_t e);

// Live firings must also fit the ADC0 readback: enable->IRQ0 at most
// STROBE_LIVE_SPAN_MAX_US. Runs strobe_validate_burst() first.
strobe_burst_err_t strobe_validate_live_burst(const strobe_burst_t *b, uint32_t max_width_us);

// Encode for strobe_burst.pio: (width, gap) per pulse, then the 0 sentinel.
// The gap after the LAST pulse is encoded as the minimum (word 0) so IRQ0 is
// not delayed by a gap nothing follows. Returns the number of words written,
// or 0 if the burst cannot be encoded or does not fit in `capacity`.
//
// Encoding checks only what the PIO program needs; call
// strobe_validate_burst() first for the electrical limits.
size_t strobe_encode(const strobe_burst_t *b, uint32_t *words, size_t capacity);

// Nominal time from enabling the state machine to IRQ0. The PIO starts with
// three instructions before the first rising edge.
uint32_t strobe_burst_duration_us(const strobe_burst_t *b);

// ---------------------------------------------------------------------------
// The section-15 schedule (.md 13.4 compute_schedule()).
//
//   width  = floor(blur budget / v), clamped to [STROBE_MIN_WIDTH_US, STROBE_SW_MAX_US]
//   period = round(freeze spacing / v); gap = period - width, at least STROBE_MIN_GAP_US
//   count  = BURST_PULSES_NOMINAL, shed one at a time (spacing kept) while the
//            burst charge exceeds BURST_CHARGE_MAX_MC, down to BURST_PULSES_MIN
//
// Width rounds DOWN so the blur budget is never exceeded; the period rounds to
// nearest. The design's first-pulse delay is not computed: it needs the camera
// latency t_cam and the beam-to-FOV geometry, which belong to Phase 7.
// ---------------------------------------------------------------------------
typedef struct {
    float    speed_mps;
    uint32_t width_us;
    uint32_t period_us;        // rising edge to rising edge
    uint32_t gap_us;
    uint32_t count;            // after any shedding
    uint32_t count_requested;  // BURST_PULSES_NOMINAL
    uint32_t span_us;          // first rising edge to last falling edge
    float    charge_mc;        // at STROBE_TARGET_CURRENT_A
    bool     width_clamped;    // the blur budget wanted more than STROBE_SW_MAX_US
    bool     gap_raised;       // the spacing gave less than STROBE_MIN_GAP_US
    bool     shed;             // the charge interlock removed pulses
} strobe_schedule_t;

typedef enum {
    STROBE_PLAN_OK = 0,
    STROBE_PLAN_BAD_SPEED,     // NaN, or outside [STROBE_V_MIN_MPS, STROBE_V_MAX_MPS]
    STROBE_PLAN_OVER_CHARGE,   // still over the limit at BURST_PULSES_MIN
} strobe_plan_err_t;

strobe_plan_err_t strobe_compute_schedule(float speed_mps, strobe_schedule_t *out);

// ---------------------------------------------------------------------------
// ADMISSION POLICY -- DRY (6a/6b) and LIVE (6c/6d).
//
// Q9's gate (the DC current setpoint) and Q10's gate (the pulse) BOTH have to be
// on for LED-bank current to flow.
//
// DRY, the default, never lets them be on together:
//   - a pulse is admitted only while the gate DAC is provably at zero;
//   - the gate is raised only while no pulse can start (engine idle, GPIO25 low).
// So with live mode off this firmware cannot command strobe current, whatever is
// connected to J3. Lowering the gate to zero is always admitted: removing the
// setpoint is never the unsafe direction.
//
// LIVE is entered only by `strobe live on confirm`, and only from gate 0. It
// relaxes exactly one rule -- a pulse may fire with the gate non-zero -- and adds
// guards of its own: the gate ceiling, the staircase rule (no raise more than
// STROBE_LIVE_GATE_STEP_MAX above the highest level already fired and measured),
// the watchdog armed, beam off, detector disarmed, the ADC free for the readback,
// the firing interval and the rolling charge budget. See board.h for the numbers.
// ---------------------------------------------------------------------------
typedef struct {
    bool rails_ready;            // power_rails_ready(): U8 and the gate amp run from VIR
    bool fault_latched;
    bool pi_present;             // Phase 6 runs with no Pi on J8
    bool pulse_limit_defeated;   // GPIO27 not provably SIO, output, driven 0
    bool ready_led_on_pwm;       // A7 regression: GPIO12 back on slice 6A
    bool engine_busy;
    bool pulse_pin_high;         // GPIO25 pad, read directly (high with the engine idle = refuse)
    bool gate_nonzero;           // commanded level, hardware readback, OR still decaying
} strobe_snapshot_t;

typedef enum {
    STROBE_OK = 0,
    STROBE_REFUSE_RAIL,
    STROBE_REFUSE_FAULT,
    STROBE_REFUSE_PI,
    STROBE_REFUSE_PULSE_LIMIT,
    STROBE_REFUSE_A7,
    STROBE_REFUSE_BUSY,
    STROBE_REFUSE_PIN_HIGH,
    STROBE_REFUSE_GATE_NONZERO,
    // Live mode (6c/6d)
    STROBE_REFUSE_NOT_LIVE,       // a live-only request with live mode off
    STROBE_REFUSE_WATCHDOG,
    STROBE_REFUSE_BEAM,
    STROBE_REFUSE_DETECT,
    STROBE_REFUSE_ADC,
    STROBE_REFUSE_GATE_MISMATCH,  // GPIO28 not on PWM, or its compare != the commanded level
    STROBE_REFUSE_CEILING,
    STROBE_REFUSE_STEP,           // the staircase rule
    STROBE_REFUSE_INTERVAL,
    STROBE_REFUSE_BUDGET,
    STROBE_REFUSE_6D,             // clamptest without a measured 0.5-2.5 A at this level
} strobe_refusal_t;

#define STROBE_REFUSE__LAST STROBE_REFUSE_6D

strobe_refusal_t strobe_may_fire(const strobe_snapshot_t *s);
strobe_refusal_t strobe_may_set_gate(const strobe_snapshot_t *s, uint16_t level);
const char *strobe_refusal_text(strobe_refusal_t r);

// --- Live policy --------------------------------------------------------------
typedef struct {
    bool     live;             // armed by `strobe live on confirm`
    bool     watchdog_armed;
    bool     beam_on;
    bool     detect_armed;
    bool     adc_idle;         // ADC engine in IDLE with the ring running
    bool     gate_hw_match;    // GPIO28 on PWM, compare == commanded level
    uint16_t gate_level;       // commanded
    uint16_t gate_proven;      // highest level fired and measured within limits
    bool     interval_ok;      // >= STROBE_LIVE_MIN_INTERVAL_MS since the last firing
    bool     budget_ok;        // this firing fits the rolling charge budget
} strobe_live_t;

// Highest level the gate may be raised to right now: the ceiling, or the
// staircase limit above the proven level, whichever is lower.
uint16_t strobe_live_gate_limit(uint16_t proven);

// Arming: every common check, the gate PROVABLY ZERO (including the decay
// window), beam off, detector disarmed, ADC idle. `live` is ignored.
strobe_refusal_t strobe_live_may_arm(const strobe_snapshot_t *s, const strobe_live_t *l);

// Raising the gate while live. Level 0 is always admitted.
strobe_refusal_t strobe_live_may_set_gate(const strobe_snapshot_t *s, const strobe_live_t *l,
                                          uint16_t level);

// Firing while live: everything above plus the hardware gate matching the
// command, the gate within the staircase limit, the interval and the budget.
strobe_refusal_t strobe_live_may_fire(const strobe_snapshot_t *s, const strobe_live_t *l);

// What must STAY true while live mode is armed and idle. Any refusal from this
// drops live mode. Excludes busy and the interval/budget, which only gate firing.
strobe_refusal_t strobe_live_may_continue(const strobe_snapshot_t *s, const strobe_live_t *l);

// Is Q9's gate still decaying after the DAC was zeroed?
//
// Zeroing the PWM compare does not zero Q9's gate: the two-section 10K/0.1 uF
// filter's dominant pole is 2.62 ms, so TP3 is still volts high for several
// milliseconds after `strobe off` -- and the compare register itself only takes
// effect at the next PWM wrap. Found in review 2026-10-02: `strobe off` followed
// immediately by `strobe pulse` passed admission on digital state alone.
//
// So the gate counts as non-zero until DAC_SETTLE_MS (20 ms, 7.6 tau: 9.9 V decays
// to ~5 mV) after the last non-zero -> zero transition. zeroed_us == 0 means "never
// zeroed from a non-zero level", which is the boot state.
bool strobe_gate_settling(uint64_t now_us, uint64_t zeroed_us);

#endif // PITRAC_STROBE_PLAN_H
