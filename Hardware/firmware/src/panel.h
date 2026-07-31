// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// panel.h â€” the two off-board panel indicators on J7.
//
//   J7.2  PWR_LED-   GPIO11, Q4 sink via R48 47R   -> the Adafruit 481's LED ring
//   J7.6  RDY_LED-   GPIO12, Q5 sink via R49 220R  -> D7 (BL-BGE1V1) in holder S1
//
// Both LEDs have their anodes on J7.1/J7.5, which are the **switched +5V rail**.
// Consequence, and it is a hardware fact not a firmware choice: NEITHER PANEL LED
// CAN INDICATE STANDBY. With the latch open they are dark no matter what GPIO11/12
// do. Pre-latch feedback has to come from the on-board D5/D6, which live on the
// always-on +3V3. (.md Section 17 item 7: a "soft-off glow" would need a wiring
// change, not firmware.)
//
// Driven by PWM so brightness is controllable and so patterns can breathe rather
// than just blink:
//   GPIO11 -> PWM slice 5 channel B
//   GPIO12 -> PWM slice 6 channel A
// Neither slice collides with the ones the detector needs later (2, 3, 7, 10).

#ifndef PITRAC_PANEL_H
#define PITRAC_PANEL_H

#include <stdbool.h>

// The ring-light patterns. Normally selected from the power FSM state, but each
// can be forced from the CLI so they are all testable on the bench -- most of
// these states are unreachable without a Pi (or a simulated one).
typedef enum {
    PANEL_PAT_OFF = 0,      // dark
    PANEL_PAT_POWERING,     // fast breath, 600 ms  -- rails coming up
    PANEL_PAT_BOOTING,      // slow breath, 2 s     -- waiting on the Pi
    PANEL_PAT_RUNNING,      // solid
    PANEL_PAT_SHUTDOWN,     // fast blink           -- teardown in progress
    PANEL_PAT_FAULT,        // double-blink         -- distinct at a glance
    PANEL_PAT__COUNT
} panel_pattern_t;

const char *panel_pattern_name(panel_pattern_t p);

// Parse a CLI name ("off", "powering", "booting", "running", "shutdown",
// "fault"). Returns -1 if unrecognised.
int panel_pattern_from_name(const char *s);

void panel_init(void);

// Call from the core 0 superloop. Resolves brightness in this precedence order:
//   1. an explicit brightness override   (panel pwr <pct>)
//   2. a forced pattern                  (panel pattern <name>)
//   3. automatic, from the power FSM
void panel_update(void);

// Brightness override: 0..100 percent, -1 back to automatic, < -1 leave unchanged.
void panel_override(int pwr_pct, int rdy_pct);
bool panel_override_active(void);

// Force a specific ring pattern, or -1 to follow the FSM again.
void panel_force_pattern(int pattern);
int  panel_forced_pattern(void);

// Which pattern the FSM state currently maps to (ignores any forcing).
panel_pattern_t panel_pattern_for_state(void);

// System_Ready mirror. The detector asserts this from Phase 3 onward; until then
// it stays false and the ready LED is dark.
void panel_set_ready(bool ready);

#endif // PITRAC_PANEL_H
