#include "ogemu.h"
#include "pico/stdlib.h"
#include <stdint.h>

static uint32_t s_now_ms;

void ogemu_time_reset(void) { s_now_ms = 0u; }
uint32_t ogemu_now_ms(void) { return s_now_ms; }

void ogemu_advance_ms(uint32_t ms) {
    s_now_ms += ms;
}

absolute_time_t get_absolute_time(void) { return s_now_ms; }

absolute_time_t make_timeout_time_ms(uint32_t ms) {
    return s_now_ms + ms;
}

bool time_reached(absolute_time_t t) {
    return (int32_t)(s_now_ms - t) >= 0;
}

uint32_t to_ms_since_boot(absolute_time_t t) { return t; }

void sleep_ms(uint32_t ms) { ogemu_on_sleep(ms); }

void sleep_us(uint64_t us) {
    uint32_t ms = (uint32_t)((us + 999ull) / 1000ull);
    if (ms == 0u && us > 0u) ms = 1u;
    ogemu_on_sleep(ms);
}
