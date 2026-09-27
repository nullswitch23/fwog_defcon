/* Host stub: the time and sleep surface display apps actually call. */
#ifndef OGEMU_PICO_STDLIB_H
#define OGEMU_PICO_STDLIB_H
#include <stdbool.h>
#include <stdint.h>
#include "pico/stdio.h"

typedef uint32_t absolute_time_t;

absolute_time_t get_absolute_time(void);
absolute_time_t make_timeout_time_ms(uint32_t ms);
bool time_reached(absolute_time_t t);
uint32_t to_ms_since_boot(absolute_time_t t);
void sleep_ms(uint32_t ms);
void sleep_us(uint64_t us);

static inline void tight_loop_contents(void) {}

#endif
