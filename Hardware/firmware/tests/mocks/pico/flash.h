#ifndef TEST_PICO_FLASH_H
#define TEST_PICO_FLASH_H

#include <stdint.h>

// Used by config_store.c (tests/config_store_test.c defines it).
#define PICO_OK 0
int flash_safe_execute(void (*func)(void *), void *param, uint32_t enter_exit_timeout_ms);

#endif
