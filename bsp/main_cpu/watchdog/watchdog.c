#include "watchdog/watchdog.h"

bool fwog_wdt_pause_step(uint32_t *depth) {
    return (*depth)++ == 0u;
}

bool fwog_wdt_resume_step(uint32_t *depth) {
    if (*depth == 0u) return false;     /* unbalanced; never underflow */
    return --(*depth) == 0u;
}

#ifndef HOST_TEST
#include "hardware/watchdog.h"

static uint32_t s_depth = 0;

void board_watchdog_init(void) {
#if FWOG_WATCHDOG_MS > 0
    watchdog_enable(FWOG_WATCHDOG_MS, true);
#endif
}

void board_watchdog_kick(void) {
#if FWOG_WATCHDOG_MS > 0
    watchdog_update();
#endif
}

void board_watchdog_pause(void) {
#if FWOG_WATCHDOG_MS > 0
    if (fwog_wdt_pause_step(&s_depth)) watchdog_enable(FWOG_WATCHDOG_LONG_MS, true);
#endif
}

void board_watchdog_resume(void) {
#if FWOG_WATCHDOG_MS > 0
    if (fwog_wdt_resume_step(&s_depth)) watchdog_enable(FWOG_WATCHDOG_MS, true);
#endif
}

bool board_watchdog_caused_reboot(void) {
    return watchdog_caused_reboot();
}
#endif
