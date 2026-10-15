// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// strobe_live.h -- the ARITHMETIC of live strobe current (6c/6d): reading the
// ADC0 current-sense record of one firing, the rolling charge budget, the
// firing interval, and the step logic of `strobe cal`.
//
// No SDK headers and no hardware access, like strobe_plan.h: compiled into the
// native host tests (tests/strobe_live_test.c) as well as the firmware. The
// admission POLICY for live mode is in strobe_plan.h, next to the dry policy.
//
// THE SIGNAL. TP4 / CurrentSense_ADC is Q10's source: R65||R66 = 0.135 R to
// ground, 135 mV/A, into ADC0 (GPIO40) through R32 4K7 with no capacitor on the
// net (netlist checked 2026-10-07), so with ~10 pF of pin capacitance the path
// settles in ~50 ns. At BURST's 500 ksps every sample is a point sample of the
// current 2 us apart: a 20 us pulse is ~10 samples.

#ifndef PITRAC_STROBE_LIVE_H
#define PITRAC_STROBE_LIVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board.h"

// ADC0 code <-> amps through the 0.135 R sense: code x 3.3 / 4095 / 0.135.
float strobe_code_to_amps(float code);
float strobe_amps_to_code(float amps);

// ---------------------------------------------------------------------------
// One firing's ADC0 record.
//
// Input: `n` samples, OLDEST FIRST, STROBE_LIVE_SAMPLE_US apart. The first
// `pre_n` are guaranteed to be before the first rising edge and set the
// baseline. `fired` is the number of pulses the engine was told to produce.
//
// A pulse is a run of samples above threshold = baseline + max(DETECT_MIN,
// half the peak excursion). Runs separated by 1-2 samples are one pulse: the
// minimum gap the engine can produce is STROBE_MIN_GAP_US, 75 samples, so a
// short dip is noise, not a gap. The plateau is the mean of the run with its
// first and last sample dropped (they straddle the edges); runs of fewer than
// four samples keep every sample and are flagged short.
//
// MORE RUNS THAN `fired` is never OK (review finding 2026-10-07): one commanded
// pulse that conducts twice -- a U5 retrigger, ringing, an oscillating gate loop
// splitting the plateau -- must not count as a clean firing that raises the
// staircase. Fewer is fine: at low current a pulse can sit under the floor.
// ---------------------------------------------------------------------------
typedef enum {
    STROBE_VERDICT_OK = 0,        // within limits, or nothing above the detection floor
    STROBE_VERDICT_NO_DATA,       // too few samples, or < STROBE_LIVE_PRE_MIN_SAMPLES of baseline
    STROBE_VERDICT_STUCK_ON,      // current before the pulse, still on at the end, or on too long
    STROBE_VERDICT_OVERCURRENT,   // a plateau over I_STOP or a sample over I_PEAK_STOP
    STROBE_VERDICT_NOISY,         // more current pulses than were fired
} strobe_verdict_t;

typedef struct {
    float    amps;        // plateau
    float    peak_a;      // highest sample in the run, baseline removed
    uint32_t start_us;    // first sample of the run, from the start of the window
    uint32_t width_us;    // samples in the run x STROBE_LIVE_SAMPLE_US
    uint32_t samples;
    bool     short_run;   // < 4 samples: edges included in the plateau
} strobe_pulse_meas_t;

typedef struct {
    strobe_verdict_t    verdict;
    size_t              n_samples;
    size_t              pre_samples;
    uint32_t            fired;           // pulses commanded
    float               baseline_code;   // mean of the pre window
    uint16_t            threshold_code;  // absolute
    uint16_t            peak_code;       // absolute, whole window
    bool                detected;        // anything above the detection floor
    bool                on_at_end;       // the last sample is still above threshold
    uint32_t            n_pulses;        // runs found (can exceed the array: NOISY)
    strobe_pulse_meas_t pulse[STROBE_BURST_MAX_PULSES];
    float               max_amps;        // over the plateaus
    float               min_amps;
    float               max_peak_a;
    uint32_t            max_width_us;
    float               charge_mc;       // measured: sum of plateau x width
} strobe_live_meas_t;

// `fired` outside 1..STROBE_BURST_MAX_PULSES is treated as the maximum.
void        strobe_live_measure(const uint16_t *s, size_t n, size_t pre_n, uint32_t fired,
                                strobe_live_meas_t *m);
const char *strobe_verdict_text(strobe_verdict_t v);

// How many of a freeze's `got` samples (oldest first, the newest within one
// 2 us conversion of t_stop_us) are BASELINE: taken before the engine was enabled
// at t_en_us, with margin. The first edge is 3 us after the enable; the split
// leaves >= 5 us between the last baseline sample and it, for any delay between
// the enable and the freeze. 0 if t_stop_us is not after t_en_us.
size_t strobe_live_baseline_samples(size_t got, uint64_t t_en_us, uint64_t t_stop_us);

// The same split counted by the DMA instead of the clock. w_en is the ring's
// write index (samples written in this mode) read just before the engine was
// enabled -- a lower bound on the enable's index -- and w_stop the index when
// the conversions stopped; the record is the newest `got` samples ending there.
// Returns how many of them were written before w_en, less 2 for the conversion
// in flight and the DMA's lag. 0 if the three numbers are inconsistent (the copy
// reaching back before the mode began, or w_en after w_stop).
size_t strobe_live_baseline_by_index(size_t got, size_t w_en, size_t w_stop);

// A TIMING CROSS-CHECK, NOT THE PROOF (review round 6). That no ADC0 sample is
// missing is proven from hardware flags by adc_ring_freeze_copy() (adc_engine.h,
// "WHY A FREEZE'S RECORD IS COMPLETE"): the datasheet guarantees that a lost
// conversion sets FCS.OVER. This check only adds a second, independent line:
// it can REJECT a record, never accept one the flags rejected; its bound below
// is derived, not proven, and a counterexample to it (a 220-cycle latency
// difference) exists -- which is why it is not what safety rests on.
//
// Was the firing OBSERVED, with not one sample missing? Two snapshots -- at the
// enable and after the post window (adc_ring_snapshot()) -- each bracket the
// instant a ring transfer was ISSUED (the DMA write index advancing; not a
// completed write) between two SysTick readings, b1 and b2 cycles apart.
// span_lo is the cycles between the two brackets' EARLIER readings, and the DMA
// issued `samples` transfers between the two events. Each issue follows its
// conversion by a latency whose spread between the snapshots is bounded by
// STROBE_LIVE_DMA_SPREAD_CYCLES -- a DERIVED bound, still an open review item
// (round 5). True iff:
//   - both brackets are <= STROBE_LIVE_SNAP_BRACKET_MAX (a stall or contention
//     inside a snapshot is rejected, never absorbed);
//   - the acceptance window, b1 + b2 + 2 x STROBE_LIVE_DMA_SPREAD_CYCLES wide,
//     is narrower than one sample period;
//   - the events' measured separation, widened by the DMA spread, contains
//     samples x cycles_per_sample.
// cycles_per_sample 0 (no exact clock ratio) or no samples: false. (Independent
// review 2026-10-12: a stopped ADC; a 6 us pause across a 5 us pulse or a
// 206 us stuck-on; an unmeasured snapshot latency that could cancel one
// missing sample; and the event being an issue, not a completed write.)
bool strobe_live_coverage_ok(uint32_t span_lo, uint32_t b1, uint32_t b2, size_t samples,
                             uint32_t cycles_per_sample);

// What a live firing uses: 0 unless `covered` (strobe_live_coverage_ok(), the
// same ring generation at both snapshots, and both snapshots taken), else the
// SMALLER of the two splits. A baseline must be before the enable by the clock AND by the
// DMA's own count, so neither a late ring start (2026-10-12) nor an interrupt
// between a clock read and the stop (2026-10-07) can put a pulse sample, or
// another mode's sample, into it. Below STROBE_LIVE_PRE_MIN_SAMPLES,
// strobe_live_measure() returns NO_DATA.
size_t strobe_live_baseline_checked(size_t got, uint64_t t_en_us, uint64_t t_stop_us,
                                    size_t w_en, size_t w_stop, bool covered);

// ---------------------------------------------------------------------------
// Rolling charge budget: at most STROBE_LIVE_BUDGET_MC in any
// STROBE_LIVE_BUDGET_WINDOW_MS. Times are 32-bit milliseconds and every
// comparison is a wrap-safe difference. A full table refuses rather than
// forgetting a booking.
// ---------------------------------------------------------------------------
typedef struct {
    uint32_t t_ms[STROBE_LIVE_BUDGET_SLOTS];
    float    mc[STROBE_LIVE_BUDGET_SLOTS];
    uint32_t head;     // next slot to write
    uint32_t count;    // slots in use (oldest = head - count)
} strobe_budget_t;

void  strobe_budget_reset(strobe_budget_t *b);
float strobe_budget_used_mc(const strobe_budget_t *b, uint32_t now_ms);
bool  strobe_budget_admits(const strobe_budget_t *b, uint32_t now_ms, float mc);
void  strobe_budget_book(strobe_budget_t *b, uint32_t now_ms, float mc);

// At least STROBE_LIVE_MIN_INTERVAL_MS since the last live firing. last_us == 0
// means none yet. A clock that went backwards refuses.
bool strobe_live_interval_ok(uint64_t now_us, uint64_t last_us);

// STROBE_LIVE_IDLE_TIMEOUT_MS with no strobe command. Wrap-safe.
bool strobe_live_idle_expired(uint32_t now_ms, uint32_t touch_ms);

// ---------------------------------------------------------------------------
// `strobe cal [A]` -- one 20 us pulse per step, from gate 0 upward.
//
// strobe_cal_next() decides what to do with the measurement at `level`:
//   amps > target x STROBE_CAL_ABORT_RATIO -> OVERSHOOT (abort, gate to 0)
//   amps >= target                       -> SOLVED (bracketed by the previous point)
//   level at the ceiling                 -> CEILING (fail)
//   else STEP to level + COARSE (nothing detected yet) or + FINE, capped at the
//   ceiling. Both steps are under STROBE_LIVE_GATE_STEP_MAX, so the cal never
//   asks for something the staircase rule would refuse.
//
// strobe_cal_solve() interpolates linearly between the last point below the
// target (l0, a0) and the first at or above it (l1, a1), rounds to nearest and
// clamps into [l0, l1] -- so the solved level was itself fired, or lies between
// two levels that were. a1 <= a0 returns l1.
// ---------------------------------------------------------------------------
typedef enum {
    STROBE_CAL_STEP = 0,
    STROBE_CAL_SOLVED,
    STROBE_CAL_OVERSHOOT,
    STROBE_CAL_CEILING,
} strobe_cal_step_t;

strobe_cal_step_t strobe_cal_next(uint16_t level, float amps, bool detected, float target_a,
                                  uint16_t *next_level);
uint16_t strobe_cal_solve(uint16_t l0, float a0, uint16_t l1, float a1, float target_a);

#endif // PITRAC_STROBE_LIVE_H
