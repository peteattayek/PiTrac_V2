// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// pio_alloc.h -- which PIO block owns what, and why it is not a free choice.
//
// RP2350 gives each PIO block ONE `GPIOBASE` register, and it holds only 0 or
// 16 (PIO_GPIOBASE_BITS = 0x10). So a block sees a 32-pin window: GPIO 0-31 or
// GPIO 16-47, never a mix. Every pin field in that block -- in/out/set/sideset
// bases and `jmp pin` -- lives inside that window.
//
// This project's PIO pins do not fit in one window:
//
//   GPIO 4/5/6    I2S mic          Phase 5   base 0 only
//   GPIO 8/9      Cam_Strobe_0/1   Phase 7   base 0 only
//   GPIO 10       Cam_Trigger      Phase 7   base 0 only
//   GPIO 25       Strobe_Pulse     Phase 6   either
//   GPIO 46       D_Comparator     Phase 3   base 16 only
//
// So the detector CANNOT share a block with the cameras or the mic. That is a
// hardware partition, not a preference -- an allocation that violates it does
// not merely perform badly, it fails to configure. The SDK enforces it:
// pio_sm_set_config() returns PICO_ERROR_BAD_ALIGNMENT if a config references
// pins 0-15 on a base-16 block or pins 32-47 on a base-0 block.
//
// ORDERING CONSTRAINT: pio_set_gpio_base() REFUSES once any program has been
// loaded into that block (it checks the block's used-instruction-space mask).
// Hence one pio_alloc_init() called from main() before every other init that
// might touch PIO. Getting this wrong is silent at compile time and fatal at
// run time, which is exactly why the map is a declared artifact here rather
// than something each module discovers for itself.
//
// We have 8 free state machines across three blocks, so spending a whole block
// on one detector costs nothing. Do not "optimise" this by consolidating.

#ifndef PITRAC_PIO_ALLOC_H
#define PITRAC_PIO_ALLOC_H

#include "hardware/pio.h"

// --- Block 0: base 0. Strobe and cameras, all in the low bank. --------------
#define PIO_BLK_LOW       pio0
#define PIO_BASE_LOW      0u
#define PIO_SM_STROBE     0u      // Phase 6, GPIO25
#define PIO_SM_CAMERA     1u      // Phase 7, GPIO8/9/10 (ARCHITECTURE.md A3)

// --- Block 1: base 0. Microphone. -------------------------------------------
#define PIO_BLK_MIC       pio1
#define PIO_BASE_MIC      0u
#define PIO_SM_I2S        0u      // Phase 5, GPIO4/5/6

// --- Block 2: base 16. Everything in the high bank. -------------------------
// GPIO46 forces this. Any future PIO use of GPIO32-47 belongs here too.
#define PIO_BLK_HIGH      pio2
#define PIO_BASE_HIGH     16u
#define PIO_SM_DETECT     0u      // Phase 3/4, GPIO46

// Sets both GPIO bases. MUST be called before any pio_add_program() anywhere.
// Panics if either call fails -- a wrong base is not a condition to limp along
// with, because every pin field configured afterwards would be silently wrong.
void pio_alloc_init(void);

#endif // PITRAC_PIO_ALLOC_H
