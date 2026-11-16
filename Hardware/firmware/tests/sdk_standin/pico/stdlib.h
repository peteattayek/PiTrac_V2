// Stand-in for the Pico SDK header of the same name: see ../sdk_standin.h.
#include "sdk_standin.h"

// What service.c uses, for strobe_fsm_test (image 1.9), which links the real
// service.c. Declarations only: a test that links service.c defines them. The
// same four the 1.8 independent review's probe added to its copy of this header.
#define PICO_ERROR_TIMEOUT (-1)
absolute_time_t make_timeout_time_ms(uint32_t ms);
int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to);
void sleep_ms(uint32_t ms);
int getchar_timeout_us(uint32_t timeout_us);
