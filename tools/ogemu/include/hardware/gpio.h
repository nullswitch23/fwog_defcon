/* Host stub: GPIO is injection-only in ogemu; these are no-ops if referenced. */
#ifndef OGEMU_HARDWARE_GPIO_H
#define OGEMU_HARDWARE_GPIO_H
#include <stdbool.h>
static inline bool gpio_get(unsigned pin) { (void)pin; return 1; }
static inline void gpio_put(unsigned pin, int v) { (void)pin; (void)v; }
#endif
