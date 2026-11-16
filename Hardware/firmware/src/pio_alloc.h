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

// --- Block 0: base 0. The strobe (and the microphone, if it is ever built). -
// Instruction memory: strobe_burst.pio (12) at offset 20, strobe_gate.pio (1)
// at 19, directly below it (strobe.c). 19 free.
#define PIO_BLK_LOW       pio0
#define PIO_BASE_LOW      0u
#define PIO_SM_STROBE     0u      // Phase 6, GPIO25
// The camera's FIRE: a flag in THIS block, raised from the camera's block with
// `irq prev set 4` (RP2350), waited on by strobe_gate.pio. Flags 4-7 never
// reach a system interrupt, so nothing else sees it.
#define PIO_IRQ_CAM_FIRE  4u

// The I2S mic (Phase 5, GPIO4/5/6, optional, deferred) was planned for PIO1 SM0.
// The camera now fills PIO1, so the mic belongs here, in PIO0's 19 free
// instructions. Nothing is written for it.
#define PIO_BLK_MIC       pio0
#define PIO_BASE_MIC      0u
#define PIO_SM_I2S        1u      // Phase 5, GPIO4/5/6 -- planned, no firmware

// --- Block 1: base 0. The camera handshake (Phase 7, ARCHITECTURE.md A3). ---
// cam_handshake.pio is 29 instructions, so it has the block to itself. Its
// FIRE reaches the strobe engine through `irq prev`, which names the block
// numbered one below: PIO1 -> PIO0. pio_alloc_init() checks that relation.
#define PIO_BLK_CAMERA    pio1
#define PIO_BASE_CAMERA   0u
#define PIO_SM_CAMERA     0u      // GPIO8/9 in, GPIO10 out

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
