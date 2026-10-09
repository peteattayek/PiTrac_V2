#ifndef TEST_HARDWARE_STRUCTS_WATCHDOG_H
#define TEST_HARDWARE_STRUCTS_WATCHDOG_H

#include <stdint.h>

typedef struct {
    volatile uint32_t ctrl;
} watchdog_hw_t;

extern watchdog_hw_t mock_watchdog_hw;
#define watchdog_hw (&mock_watchdog_hw)

#define WATCHDOG_CTRL_ENABLE_BITS 0x40000000u

static inline void hw_clear_bits(volatile uint32_t *addr, uint32_t mask) { *addr &= ~mask; }

#endif
