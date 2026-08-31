// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// beam.h -- Phase 2: the modulated IR beam and its phase-locked demodulator clock.
//
//   GPIO31  Modulation_PWM   PWM slice 3 ch B  -> U9 74LVC1G123 -> U10 MCP1416 -> Q11 -> D11
//   GPIO39  Demodulation_PWM PWM slice 7 ch B  -> U13/U12A sign-switching demodulator
//
// Two things make this module less trivial than "set up a PWM":
//
// 1. PHASE LOCK. The demodulator has to run at exactly the carrier frequency with a
//    controllable phase offset. Slices 3 and 7 are separate counters, so they are
//    locked by loading both counters while DISABLED and then enabling both in a
//    SINGLE register write. Resolution is one sysclk tick, 6.67 ns, or 0.25 deg at
//    104 kHz.
//
//    Note we read-modify-write PWM_EN rather than using pwm_set_mask_enabled(),
//    which writes the register wholesale and would switch off the panel LEDs on
//    slices 5 and 6.
//
// 2. THE MCU DOES NOT DRIVE THE LED DIRECTLY. Modulation_PWM feeds a 74LVC1G123
//    monostable (U9) wired A=GND, B=~CLR=Modulation_PWM. A rising edge triggers it;
//    a falling edge clears it immediately. So the LED output tracks the input
//    exactly for pulses shorter than the one-shot period, and is HARD CLAMPED above
//    it. There is no disable path on this watchdog by design: a stuck-high
//    Modulation_PWM yields one clamped flash, not a cooked LED, and no DC beam mode
//    can exist. Whether the '123 can *recover* fast enough to reproduce 30 % duty at
//    104 kHz is open question Q2 -- measure it, do not assume.
//
// Electrically the beam is the biggest continuous load on the board: ~3 A peak at
// 30 % duty is ~0.95 A average from +5V, ~3.15 W in D11 and ~0.73 W in each ballast
// resistor. Hence the ramp helpers rather than a bare "on".

#ifndef PITRAC_BEAM_H
#define PITRAC_BEAM_H

#include <stdbool.h>
#include <stdint.h>

void beam_init(void);

// Reconfigure carrier and demod together. Re-establishes phase lock every time.
// duty is 0..1. phase_ticks is the demod offset in sysclk ticks, 0..top.
// Safe to call while running; the beam is briefly disabled during the update.
//
// APPLIES THE DUTY CEILING (see below). If the request would exceed it the duty
// is reduced to the legal maximum rather than refused -- this is the last line
// of defence, and a silently dark beam is harder to diagnose than one running at
// the ceiling. Check with beam_would_exceed_ceiling() first if you want to warn.
void beam_configure(uint32_t freq_hz, float duty, int32_t phase_ticks);

// ---------------------------------------------------------------------------
// DUTY CEILING
//
// Enforced on the EFFECTIVE duty -- what reaches the LED after U9's 122.68 us
// one-shot truncates the high phase -- not on the commanded duty. That
// distinction is the whole point:
//
//   1 kHz  @ 50 %  -> high 500.0 us, clamped -> effective 12.3 %   legal
//   104kHz @ 50 %  -> high   4.8 us, no clamp -> effective 50.0 %  refused
//
// So `beam clamp` (1 kHz / 50 %, the Q1 measurement) passes, while the trap it
// used to leave behind -- s_duty stuck at 0.50, then a bare `beam freq 104167`
// putting 1.6 A through D11 -- is caught. Every path that can change duty or
// frequency goes through beam_configure() or beam_set_duty(), and both check.
// ---------------------------------------------------------------------------

// Effective duty at the LED right now, after the U9 clamp.
float beam_effective_duty(void);

// Same, for a hypothetical top/div/duty. Exposed for beam_would_exceed_ceiling()
// and for the CLI's reporting.
float beam_effective_duty_at(uint32_t top, uint32_t div, float duty);

// Would this freq/duty pair exceed the ceiling? Does not change anything.
// `out_effective` receives the effective duty that would result.
bool beam_would_exceed_ceiling(uint32_t freq_hz, float duty, float *out_effective);

// Default BEAM_DUTY_CEILING (0.35). Lower it for a thermally constrained
// session -- CR-12 puts the sustained operating point at 0.25.
void  beam_set_duty_ceiling(float c);
float beam_duty_ceiling(void);

// ---------------------------------------------------------------------------
// CHOPPING, for the differential measurements in Phase 3.4 and 3.6
//
// *** CHOP WITH THESE, NOT WITH beam_enable(false). ***
//
// beam_enable() disables BOTH PWM slices, which stops the DEMODULATOR CLOCK as
// well as the carrier. U13's mux then sits at one fixed sign, so the "off" half
// of the cycle is not a dark reference -- it is a different circuit, with a
// different DC operating point, and the difference you measure is dominated by
// that rather than by the light.
//
// Setting the carrier's compare level to 0 instead means the carrier pin never
// goes high, so U9 (a rising-edge one-shot) never fires and the LED is dark --
// while the demod slice keeps running with its phase lock untouched. Only the
// light changes, which is what a differential measurement assumes.
//
// BENCH-VERIFY ONCE: scope TP5 to confirm it stays low at compare 0, and that
// GPIO39 keeps toggling across a chop.
// ---------------------------------------------------------------------------
void beam_chop_begin(float on_duty);   // remember the "on" level
void beam_chop(bool on);               // 0 <-> the remembered level
void beam_chop_end(void);              // restore the remembered level

// Milliseconds the beam has been continuously at a duty high enough to be
// thermally significant. Resets whenever it drops below the threshold.
//
// This exists because the LED loses 25-45 % of its optical output between cold
// and thermal plateau while EVERY ELECTRICAL READING SAYS NOTHING HAPPENED
// (measured: current +1.7 %, power +0.6 %). A carrier or threshold chosen cold
// is wrong warm, and the discrepancy looks like drift or a detection fault
// rather than a calibration error. tau is about 70 s.
uint32_t beam_duty_stable_ms(void);

#define BEAM_WARMUP_MS   300000u   // 5 min, ~4.3 tau

// Change duty or phase WITHOUT recomputing TOP.
//
// Prefer these over beam_configure() when the frequency is not changing. Going
// back through beam_configure(beam_freq_hz(), ...) round-trips TOP through an
// integer division and can land one count away from where it started.
//
// Duty can be changed live -- the PWM compare register updates without stopping.
// Phase cannot: the counters have to be preloaded while the slices are disabled,
// so beam_set_phase() briefly stops and restarts both. That puts one partial
// period in the carrier, which the demodulator settles out in well under a
// millisecond and does not matter for a phase sweep.
void beam_set_duty(float duty);
void beam_set_phase(int32_t phase_ticks);

// Enable/disable both slices. Refuses to enable unless the +5V rail is up --
// U10 (MCP1416) and the LED both run from the switched rail.
bool beam_enable(bool on);
bool beam_enabled(void);

// Step duty up gradually rather than slamming to the target. Blocking.
// Exists because going straight to 30 % at 3 A from cold is how ballast
// resistors and LEDs get damaged before anyone can look at a thermal camera.
void beam_ramp_duty(float target_duty, uint32_t step_ms);

uint32_t beam_freq_hz(void);
float    beam_duty(void);
int32_t  beam_phase_ticks(void);
uint32_t beam_top(void);

// PWM clock divider. 1 everywhere above ~2289 Hz; only `beam clamp` (1 kHz)
// needs more, because one period will not fit in the 16-bit counter at div=1.
// With div > 1 a phase tick is div * 6.67 ns.
uint32_t beam_clkdiv(void);

// Actual achieved frequency for the current TOP, which differs from the requested
// value whenever SYSCLK/freq is not an integer.
uint32_t beam_actual_freq_hz(void);

#endif // PITRAC_BEAM_H
