// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
#include "pio_alloc.h"
#include "board.h"

#include "pico/stdlib.h"

// Catch a pin/block mismatch at COMPILE time rather than discovering it as a
// PICO_ERROR_BAD_ALIGNMENT on the bench.
_Static_assert(PIN_D_COMPARATOR >= PIO_BASE_HIGH &&
               PIN_D_COMPARATOR <  PIO_BASE_HIGH + 32,
               "D_Comparator is outside the high PIO block's 32-pin window");
_Static_assert(PIN_STROBE_PULSE >= PIO_BASE_LOW &&
               PIN_STROBE_PULSE <  PIO_BASE_LOW + 32,
               "Strobe_Pulse is outside the low PIO block's 32-pin window");
_Static_assert(PIN_CAM_STROBE_0 >= PIO_BASE_CAMERA && PIN_CAM_STROBE_1 >= PIO_BASE_CAMERA &&
               PIN_CAM_TRIGGER  >= PIO_BASE_CAMERA &&
               PIN_CAM_STROBE_0 < PIO_BASE_CAMERA + 32 && PIN_CAM_STROBE_1 < PIO_BASE_CAMERA + 32 &&
               PIN_CAM_TRIGGER  < PIO_BASE_CAMERA + 32,
               "camera pins are outside the camera PIO block's 32-pin window");
_Static_assert(PIN_I2S_SCK < PIO_BASE_MIC + 32 && PIN_I2S_DATA < PIO_BASE_MIC + 32,
               "mic pins are outside the mic PIO block's 32-pin window");

void pio_alloc_init(void) {
    // Order matters only in that this must precede every pio_add_program().
    // pio_set_gpio_base() checks the block's used-instruction-space mask and
    // returns PICO_ERROR_INVALID_STATE if anything is already loaded.
    hard_assert(pio_set_gpio_base(PIO_BLK_LOW,    PIO_BASE_LOW)    == PICO_OK);
    hard_assert(pio_set_gpio_base(PIO_BLK_CAMERA, PIO_BASE_CAMERA) == PICO_OK);
    hard_assert(pio_set_gpio_base(PIO_BLK_HIGH,   PIO_BASE_HIGH)   == PICO_OK);

    // The mic shares the strobe's block (and its base). If it ever moves, it
    // needs its own base set here.
    hard_assert(PIO_BLK_MIC == PIO_BLK_LOW);

    // The camera's FIRE is `irq prev set 4`: the flag lands in the block
    // numbered one below the camera's. That must be the strobe's.
    hard_assert(pio_get_index(PIO_BLK_CAMERA) == pio_get_index(PIO_BLK_LOW) + 1u);
}
