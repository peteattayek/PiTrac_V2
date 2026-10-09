#ifndef TEST_PICO_STDLIB_H
#define TEST_PICO_STDLIB_H

#include <stdbool.h>
#include <stdint.h>

typedef uint64_t absolute_time_t;

absolute_time_t get_absolute_time(void);
uint32_t to_ms_since_boot(absolute_time_t time);

// Used by service.c (tests/service_test.c defines them).
#define PICO_ERROR_TIMEOUT (-1)
absolute_time_t make_timeout_time_ms(uint32_t ms);
int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to);
void sleep_ms(uint32_t ms);
int getchar_timeout_us(uint32_t timeout_us);

#endif
