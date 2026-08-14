// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// detect.h -- Phase 3: the ball-detection end of the optical chain.
//
// This module owns three pins, and they are the last three in the signal path:
//
//   GPIO44  Threshold_PWM   PWM -> two RC poles -> Threshold_DC (TP8) -> U15 pin 2
//   GPIO33  HPF_Toggle      -> U14 TMUX1219 SEL: baseline TRACK or HOLD
//   GPIO46  D_Comparator    <- U15 pin 1, open collector, R103 10K pull-up to +3V3
//
// The chain ahead of them, for orientation (verified against the netlist, not the
// .md -- several numbers there are wrong, see BENCH_P3_DETECT.md):
//
//   D12 -> U11A TIA (Rf 470K) -> U13 chopper demod -> 4th-order LPF f0 15.39 kHz,
//   gain 2 -> TP9 -> C81 330nF -> [U14 gate] -> R99 -> U12B non-inverting x14.5
//   -> U15 (+) vs Threshold_DC (-), and -> R102 -> D14 clamp -> ADC5
//
// THREE THINGS THAT ARE NOT OBVIOUS FROM THE SCHEMATIC AND WILL COST YOU AN
// AFTERNOON IF YOU ASSUME OTHERWISE:
//
// 1. ADC5 IDLES NEAR 0 V, not at the 2.59 V virtual ground. Everything from TP6
//    to TP10 sits at +5VA/2, but U12B is referenced to GROUND (its + input gets
//    its DC level through R99 from a node held at 0 V by R96 2M), so the ADC5
//    node starts at zero and a ball is a positive bump from there.
//
// 2. ADC5 SATURATES AROUND 3.3 V. D14 (BAT54S) clamps the node to the +3V3 rail
//    through R102's 1K. U12B is rail-to-rail on +5VA and will happily drive 5 V
//    into it. A clipped peak makes the 50 %-of-peak refinement meaningless, which
//    is why detect_refine() has to report saturation rather than average through it.
//
// 3. U15 HAS NO HYSTERESIS. There is no resistor from D_Comparator back to pin 3
//    anywhere on the board -- I checked every net that touches U15. Chatter on a
//    slow-slewing edge is a property of this board, not a firmware bug. See the
//    glitch-reject note in detect.pio.
//
// Detection is ACTIVE HIGH on GPIO46: signal above threshold pulls the open
// collector off and R103 takes the pin to +3V3.

#ifndef PITRAC_DETECT_H
#define PITRAC_DETECT_H

#include <stdbool.h>
#include <stdint.h>

void detect_init(void);

// ---------------------------------------------------------------------------
// THRESHOLD DAC  (GPIO44 -> TP8 -> U15 pin 2)
//
// PWM at DAC_TOP+1 = 1024 counts -> 146.5 kHz, 3.2 mV steps, DC = duty x 3V3.
//
// Settling is DAC_SETTLE_MS (20 ms), and that number is not the one the bench
// doc quotes. R86/C74 and R89/C76 are 10K/0.1uF each, so "two 1 ms poles" is the
// obvious reading -- but the second section LOADS the first, so the real poles of
// the cascaded ladder are at RC/0.382 = 2.62 ms and RC/2.618 = 0.382 ms. Five
// time constants of the dominant pole is 13 ms; the doc's 10 ms is about four,
// leaving ~2 % of a step uncrossed. On a 3.3 V full scale that is 66 mV, or 20
// threshold steps -- enough to smear the flip point the cross-calibration is
// trying to find.
// ---------------------------------------------------------------------------

void  detect_threshold_set_duty(float duty);   // 0..1, blocks for DAC_SETTLE_MS
void  detect_threshold_set_volts(float v);
float detect_threshold_duty(void);
float detect_threshold_volts(void);            // duty x vref
uint16_t detect_threshold_level(void);         // raw PWM compare level, 0..DAC_TOP

// Nominal 3.3 V. The real +3V3 measured 3.246 V on this board (PROGRESS.md 6),
// so trim this if you want detect_threshold_volts() to agree with a DMM on TP8.
// TP8 is the truth; this is only how the firmware reports its intent.
void  detect_threshold_set_vref(float v);
float detect_threshold_vref(void);

// ---------------------------------------------------------------------------
// GATED HPF  (GPIO33 -> U14 TMUX1219 SEL)
//
// U14 is an SPDT with only ONE throw connected:
//   S1 -> R96 2M -> GND   with C81 330nF this is the 0.66 s HPF   -> TRACK
//   S2 -> not connected   the node floats and C81 holds its charge -> HOLD
//
// So HOLD is a genuine open circuit, not a second filter corner. The U12B input
// DC level is undefined there and walks on switch leakage and op-amp bias --
// roughly 1 nA into 330 nF is 3 mV/s at the node, x14.5 = ~44 mV/s at ADC5. That
// is the effect detect_hpf_test() measures, and it is also why ARMED (which means
// HOLD) cannot be held open indefinitely.
//
// WHICH GPIO33 LEVEL SELECTS WHICH PATH IS UNVERIFIED. The netlist encodes only
// the pin name "SEL". Everything here goes through HPF_SEL_TRACK in board.h; if
// detect_hpf_test() says the sense is inverted, flip that one line.
// ---------------------------------------------------------------------------

typedef enum { HPF_TRACK = 0, HPF_HOLD } hpf_mode_t;

// Refuses unless the +5V rail is up. U14 runs from +5VA, and driving SEL high
// into an unpowered mux back-feeds the analog rail through its protection
// structures -- the exact condition safe_state.c drives GPIO33 low to prevent.
bool        detect_hpf_set(hpf_mode_t m);
hpf_mode_t  detect_hpf_mode(void);
const char *detect_hpf_name(hpf_mode_t m);

// Drive GPIO33 low unconditionally. Must be called whenever the +5V rail is
// dropped, or a preceding `hpf track` leaves SEL high into a dark U14 --
// reachable through an ordinary `hpf track` then `off`. safe_state.c covers the
// boot case; this covers the runtime one.
void        detect_hpf_safe_off(void);

typedef struct {
    float track_drift_v;      // ADC5 excursion over the window, TRACK
    float hold_drift_v;       // same, HOLD
    float track_mean_v;
    float hold_mean_v;
    float ratio;              // hold_drift / track_drift
    bool  conclusive;         // ratio big enough to call it
    bool  polarity_ok;        // HPF_SEL_TRACK as defined matches what TRACK does
} hpf_test_t;

// Sit in each mode for `window_ms` and compare how far the ADC5 baseline walks.
// TRACK is pinned to 0 V through 2M and should barely move; HOLD floats and
// should visibly ramp. Blocking, ~2 x window_ms. Needs the ADC ring running and
// ch5 in the current round-robin set (IDLE or ARMED).
void detect_hpf_test(hpf_test_t *out, uint32_t window_ms);

// ---------------------------------------------------------------------------
// COMPARATOR  (GPIO46)
// ---------------------------------------------------------------------------

bool detect_comparator(void);      // true = above threshold (active high)

// ---------------------------------------------------------------------------
// THRESHOLD / COMPARATOR CROSS-CALIBRATION  (BENCH_P3_DETECT.md 3.5)
//
// Sweep the threshold and find the duty at which D_Comparator flips. That single
// measurement ties the threshold DAC to the ADC5 scale and exercises both output
// paths of U12B at once, with no external gear.
//
// It doubles as the GPIO44 crosstalk check: the 146.5 kHz DAC carrier is only
// 42 kHz from the optical carrier, and while the two RC poles should bury it,
// PCB coupling from the GPIO44 trace would not care. If ADC5 moves as the
// threshold steps, that is crosstalk -- change DAC_TOP (2047 -> 73 kHz, or
// 511 -> 293 kHz) and re-run.
// ---------------------------------------------------------------------------

typedef struct {
    bool     found;
    float    flip_duty;        // first duty where the comparator went high
    float    flip_volts;       // threshold there, nominal
    uint16_t adc5_at_flip;     // raw ADC5 code at the flip
    float    adc5_at_flip_v;
    float    adc5_span_v;      // total ADC5 movement across the sweep = crosstalk
    uint16_t steps;
} threshold_sweep_t;

// Sweeps duty from `lo` to `hi` in `steps` increments. Blocking:
// steps x DAC_SETTLE_MS, so 64 steps is ~1.3 s. Restores the previous threshold.
void detect_threshold_sweep(threshold_sweep_t *out,
                            float lo, float hi, uint16_t steps);

#endif // PITRAC_DETECT_H
