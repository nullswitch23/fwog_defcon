#include "bootloader/bl_policy.h"

bl_phase_t bl_phase_for(uint32_t elapsed_ms, bool red_held) {
    if (elapsed_ms >= BL_CONSOLE_AFTER_MS) return BL_PHASE_CONSOLE;
    if (red_held || elapsed_ms >= BL_VISIBLE_AFTER_MS) return BL_PHASE_VISIBLE;
    return BL_PHASE_SILENT;
}

bool bl_should_auto_boot(bool red_held, bool app_valid, bool run_received) {
    return run_received && app_valid && !red_held;
}

bool bl_debounce_feed(bl_debounce_t *d, bool raw) {
    if (!raw) {
        d->count = 0u;
        d->latched = false;
        return false;
    }
    if (d->latched) return false;
    if (++d->count >= BL_DEBOUNCE_SAMPLES) {
        d->latched = true;
        return true;
    }
    return false;
}

#ifndef HOST_TEST
#include "platform/board.h"
#include "hardware/gpio.h"

void bl_buttons_sample(bl_buttons_t *b) {
    b->gray  = !gpio_get(PIN_BTN_GRAY);    /* active low, pulled up */
    b->green = !gpio_get(PIN_BTN_GREEN);
    b->red   = !gpio_get(PIN_BTN_RED);
}
#endif
