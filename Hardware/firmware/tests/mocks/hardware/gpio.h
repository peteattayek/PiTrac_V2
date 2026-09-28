#ifndef TEST_HARDWARE_GPIO_H
#define TEST_HARDWARE_GPIO_H

#include <stdbool.h>

bool gpio_get(unsigned pin);
void gpio_put(unsigned pin, bool value);

#endif
