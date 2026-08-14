// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// beam.h â€” Phase 2: the modulated IR beam and its phase-locked demodulator clock.
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
//    104 kHz is open question Q2 â€” measure it, do not assume.
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

// Change duty or phase WITHOUT recomputing TOP.
//
// Prefer these over beam_configure() when the frequency is not changing. Going
// back through beam_configure(beam_freq_hz(), ...) round-trips TOP through an
// integer division and can land one count away from where it started.
//
// Duty can be changed live â€” the PWM compare register updates without stopping.
// Phase cannot: the counters have to be preloaded while the slices are disabled,
// so beam_set_phase() briefly stops and restarts both. That puts one partial
// period in the carrier, which the demodulator settles out in well under a
// millisecond and does not matter for a phase sweep.
void beam_set_duty(float duty);
void beam_set_phase(int32_t phase_ticks);

// Enable/disable both slices. Refuses to enable unless the +5V rail is up â€”
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
