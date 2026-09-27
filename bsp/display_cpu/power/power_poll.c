/* See power_poll.h. Moved from apps/bench/display/main.c, where every line
 * below had already run on a board (the hardware record). */
#include "power/power_poll.h"
#include "power/ship_mode.h"
#include "leds/ws2812_driver.h"
#include "lcd/fwog_splash.h"
#include "platform/board.h"
#include "common/diag.h"

static fwog_ship_state_t s_ship;

/* The colours in place before a countdown started, so an ABORTED hold can put
 * them back. Without this, brushing the red button would silently wipe
 * whatever the application had set -- and "hold 3 s, release, LEDs return to
 * idle" is the bench step that proves the abort guard works at all, so the
 * restore is part of the thing being tested, not a nicety. */
static uint8_t s_led_saved[FWOG_LED_COUNT][3];
static bool    s_led_saved_valid;

/* Rate limit for the countdown render. ws2812_process() pushes 7 words with
 * pio_sm_put_blocking() -- ~210 us per frame at 800 kHz -- and the chain
 * latches on a >50 us idle gap, so calling it every main-loop iteration would
 * both starve the link service and run frames together with no reset gap
 * between them. 50 ms is 20 Hz: 120 frames across the 6 s hold, far smoother
 * than a bar with only seven steps needs. */
#define FWOG_SHIP_RENDER_MS 50u
static uint32_t s_next_render_ms;

/* 0..100 as a filling red bar, ALWAYS at least one pixel. An operator needs
 * to see that the hold registered, and "no LEDs yet" is indistinguishable
 * from "the button did nothing" -- so a plain proportional map is wrong here:
 * it leaves the chain dark for the first 60 ms (progress stays 0 until
 * elapsed reaches 6000/100). This is only ever called while the machine is
 * not IDLE, so a floor of 1 cannot light the bar when nothing is held. */
static void ship_render(unsigned progress) {
    unsigned lit = (progress * FWOG_LED_COUNT + 99u) / 100u;
    if (lit < 1u) lit = 1u;
    for (unsigned i = 0u; i < FWOG_LED_COUNT; i++) {
        ws2812_set_color(i, (i < lit) ? 64u : 0u, 0u, 0u);
    }
    ws2812_process();
}

static void ship_save_leds(void) {
    for (unsigned i = 0u; i < FWOG_LED_COUNT; i++) {
        if (!ws2812_get_color(i, &s_led_saved[i][0], &s_led_saved[i][1],
                              &s_led_saved[i][2])) {
            s_led_saved[i][0] = s_led_saved[i][1] = s_led_saved[i][2] = 0u;
        }
    }
    s_led_saved_valid = true;
}

static void ship_restore_leds(void) {
    if (!s_led_saved_valid) return;
    for (unsigned i = 0u; i < FWOG_LED_COUNT; i++) {
        ws2812_set_color(i, s_led_saved[i][0], s_led_saved[i][1],
                         s_led_saved[i][2]);
    }
    ws2812_process();
    s_led_saved_valid = false;
}

fwog_power_t fwog_power_poll(uint32_t now_ms) {
    const fwog_buttons_t b = fwog_buttons_poll(now_ms);
    const bool red_down = (b.down & FWOG_BTN_BIT(FWOG_BTN_RED)) != 0u;

    const fwog_ship_phase_t was = s_ship.phase;
    const fwog_ship_phase_t ev = fwog_ship_step(&s_ship, red_down, now_ms);

    if (ws2812_ready()) {
        if (was == FWOG_SHIP_IDLE && ev == FWOG_SHIP_ARMING) {
            ship_save_leds();
            s_next_render_ms = now_ms;   /* render immediately */
        } else if (was != FWOG_SHIP_IDLE && ev == FWOG_SHIP_IDLE) {
            ship_restore_leds();
        }
        /* Signed-difference wrap comparison, the same idiom the rest of this
           tree uses for uint32_t millisecond deadlines. */
        if (s_ship.phase != FWOG_SHIP_IDLE &&
            (int32_t)(now_ms - s_next_render_ms) >= 0) {
            s_next_render_ms = now_ms + FWOG_SHIP_RENDER_MS;
            ship_render(fwog_ship_progress(&s_ship, now_ms));
        }
    }

    /* The return value, never s_ship.phase -- see ship_mode.h. Testing the
       field would retry the I2C write every iteration when the charger does
       not answer. */
    if (ev == FWOG_SHIP_FIRED) {
        DIAG("OK ship entering (red held %u ms)\n", FWOG_SHIP_HOLD_MS);
        fwog_splash_ship();
        if (!fwog_ship_enter()) {
            /* Reachable: the board is still on and the operator is still
               holding the button. fwog_ship_step()'s once-only rule is what
               stops this line repeating forever. */
            DIAG("ERR ship charger-write-failed\n");
        }
    }

    fwog_power_t out;
    out.buttons  = b;
    out.progress = fwog_ship_progress(&s_ship, now_ms);
    out.armed    = (s_ship.phase != FWOG_SHIP_IDLE);
    return out;
}
