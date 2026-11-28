// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// beam.h -- Phase 2: the modulated IR beam and its phase-locked demodulator clock.
//
//   GPIO31  Modulation_PWM   PWM slice 7 ch B  -> U9 74LVC1G123 -> U10 MCP1416 -> Q11 -> D11
//   GPIO39  Demodulation_PWM PWM slice 11 ch B -> U13/U12A sign-switching demodulator
//
// (Slice numbers corrected 2026-09-28: RP2350B maps GPIO >= 32 to slice
// 8 + ((gpio >> 1) & 3). Only this comment was wrong -- beam.c resolves both
// slices at runtime with pwm_gpio_to_slice_num(). Full map in board.h.)
//
// Two things make this module less trivial than "set up a PWM":
//
// 1. PHASE LOCK. The demodulator has to run at exactly the carrier frequency with a
//    controllable phase offset. Slices 7 and 11 are separate counters, so they are
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
//    can exist. Whether the '123 recovers fast enough to reproduce the commanded
//    duty was open question Q2 -- ANSWERED 2026-08-13: ~CLR resets the timing cap
//    through an internal transistor, and TP5's duty read the same as the PWM's to
//    two figures up to 250 kHz (PROGRESS Q2). The drive chain's stretch, which two
//    figures do not resolve, is measured per board (DUTY CEILING, below).
//
// Electrically the beam is the biggest continuous load on the board: ~3 A peak at
// 30 % duty is ~0.95 A average from +5V, ~3.15 W in D11 and ~0.73 W in each ballast
// resistor. Hence the ramp helpers rather than a bare "on".

#ifndef PITRAC_BEAM_H
#define PITRAC_BEAM_H

#include <stdbool.h>
#include <stdint.h>

#include "service.h"     // yield_t

void beam_init(void);

// Reconfigure carrier and demod together. Re-establishes phase lock every time.
// duty is 0..1. phase_ticks is the demod offset in sysclk ticks, 0..top.
// Safe to call while running: the carrier pin is held low across the update, a
// period of the old setting, and no pulse is stretched; the first after it may be
// short (image 1.17, V116-F1; image 1.18, V117-N1; beam.c).
//
// REFUSES A FREQUENCY OUTSIDE BEAM_FREQ_MIN_HZ..BEAM_FREQ_MAX_HZ (1-200 kHz,
// board.h): false, and nothing changes. Every path that sets the carrier comes
// through here or beam_restore(), so the carrier never leaves that range (the
// owner, 2026-11-26; until 1.16 the CLI took 1 Hz to 75 MHz). The commands check
// beam_freq_allowed() first and say so.
//
// APPLIES THE DUTY CEILING (see below). If the request would exceed it the duty
// is reduced to the legal maximum rather than refused -- this is the last line
// of defence, and a silently dark beam is harder to diagnose than one running at
// the ceiling. Check with beam_duty_allowed() first if you want to warn.
bool beam_configure(uint32_t freq_hz, float duty, int32_t phase_ticks);
bool beam_freq_allowed(uint32_t freq_hz);

// The carrier exactly as set -- TOP and divider, the duty admitted, the phase --
// and putting it back register for register (image 1.16, review finding V115-F3).
// For a command that moves the carrier and must leave it as it found it: a
// beam_configure() from the frequency read back re-plans TOP from a rounded
// integer and can land elsewhere (1021 Hz asked: TOP 48971 then 49019).
typedef struct {
    uint32_t top, div;
    float    duty;
    int32_t  phase;
} beam_setting_t;
beam_setting_t beam_setting(void);
bool           beam_restore(const beam_setting_t *s);   // false: outside the range, unchanged

// ---------------------------------------------------------------------------
// DUTY CEILING -- ON THE PWM PIN'S HIGH TIME (image 1.16, review finding V115-F2)
//
// The carrier's compare level is never more than the ceiling's share of the
// period, so at 25 % never more than (TOP + 1) / 4 counts. EVERY write of it goes
// through one function in beam.c that makes it so: configure, duty, the chop's
// on and off, the ramp, a restore. And no reconfiguration stretches a pulse on the
// pin or runs two together (image 1.17, V116-F1).
//
// IT BOUNDS THE PIN, Modulation_PWM, NOT THE LED (the owner, 2026-11-26). U9's
// ~CLR is the pin, so U9 can only shorten a pulse, whatever its width (point 2
// above). But the drive chain (U9, U10, Q11) can take longer to turn off than on,
// and the LED's duty is the pin's plus that stretch times the frequency: each 10 ns
// is 0.10 % at 104 kHz, 0.20 % at 200 kHz. No part publishes a maximum for it, so
// each board's is measured at TP5 at the carrier and at 150 and 200 kHz
// (BRINGUP_NEW_BOARD.md Step 6.3b), and
// the carrier is held to 1-200 kHz (beam_configure()). 1.14 and 1.15 bounded the
// LED's duty with an assumed U9 width instead (V114-F3, V115-F2). The cost of the
// pin's bound: `beam clamp` runs at 25 % -- 250 us high at 1 kHz, still past every
// clamp measured (121-135 us).
// ---------------------------------------------------------------------------

// Is `duty` (0..1) within the ceiling? Does not change anything.
bool beam_duty_allowed(float duty);

// The LED's duty ESTIMATED with board 1's measured U9 (BEAM_ONESHOT_CLAMP_US), for
// printing: the pin's high time cut to that clamp, without the chain's stretch. On
// another board something else. Not a bound; the ceiling is the pin's.
float beam_led_duty_estimate(void);

// BEAM_DUTY_OPERATING (0.25), the owner's operating limit, enforced since image
// 1.14 for USB and the Pi alike (before, the default was BEAM_DUTY_CEILING, the
// 0.35 design ceiling, and 25 % was procedure). This can lower it for a session;
// it never goes above 0.25.
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
// Duty can be changed live -- the PWM compare register is double-buffered and
// takes the new level at the next wrap, so every pulse is whole.
// Phase cannot: the counters have to be preloaded while the slices are disabled,
// so beam_set_phase() briefly stops and restarts both, with the carrier pin held
// low across it (image 1.17): a period of dark, and the first pulse after it may
// be short, which the demodulator settles out in well under a millisecond and does
// not matter for a phase sweep.
void beam_set_duty(float duty);
void beam_set_phase(int32_t phase_ticks);

// Enable/disable both slices. Refuses to enable unless the +5V rail is up --
// U10 (MCP1416) and the LED both run from the switched rail. Off puts the carrier
// pin low at once; on starts the slices and gives the pin to the PWM no sooner than
// a period after the last pulse was cut (image 1.17). On while on: nothing.
bool beam_enable(bool on);
bool beam_enabled(void);

// Step duty up gradually rather than slamming to the target. Blocking.
// Exists because going straight to 30 % at 3 A from cold is how ballast
// resistors and LEDs get damaged before anyone can look at a thermal camera.
// YIELD_OK: at the target. YIELD_ABORT_KEY (a stop was asked) or
// YIELD_ABORT_FAULT: stopped part-way, the duty left at the last step written
// (beam_duty()), and nothing written after (image 1.14, V113-F3).
yield_t beam_ramp_duty(float target_duty, uint32_t step_ms);

uint32_t beam_freq_hz(void);
float    beam_duty(void);
// The carrier's compare level as last written: the PWM's high time, in counts.
uint32_t beam_level(void);
int32_t  beam_phase_ticks(void);
uint32_t beam_top(void);

// PWM clock divider. 1 everywhere above ~2289 Hz; below that (`beam clamp`'s 1 kHz)
// more, because one period will not fit in the 16-bit counter at div=1.
// With div > 1 a phase tick is div * 6.67 ns.
uint32_t beam_clkdiv(void);

// Actual achieved frequency for the current TOP, which differs from the requested
// value whenever SYSCLK/freq is not an integer.
uint32_t beam_actual_freq_hz(void);

#endif // PITRAC_BEAM_H
