/* The red button's power-off wiring, in one call an app makes from its loop.
 *
 * ---- Why this exists separately from ship_mode.c ----
 *
 * ship_mode.c is PURE and host-tested, which is only possible because it does
 * not touch ws2812_driver.h. Its header says the LED mapping is deliberately
 * the application's job, and that is RIGHT ABOUT ship_mode.c. This file is a
 * different layer: already SDK-side and untestable on the host, so the
 * mapping costs nothing here. ship_mode.c stays pure and stays tested.
 *
 * What moved into this file is the wiring apps/bench/display had proved on a
 * board (the hardware record) -- the 6 s hold, the seven-pixel countdown, the
 * save/restore around an aborted hold, and the once-only fire. It was moved,
 * not reimplemented: it is the only version of this that has ever run.
 *
 * ---- Red powers the board OFF. Nothing here can power it ON. ----
 *
 * Waking is a GRAY hold, hardwired to the BQ25896's /QON pin (the hardware record),
 * or a USB attach. Both work with NOTHING RUNNING, which is exactly why they
 * work -- and exactly why no firmware in this repo can implement them. Do not
 * add a "power on" path here; there is nothing for it to do.
 *
 * ---- What the link error proves, and what it does NOT ----
 *
 * board_init() references fwog_power_policy_declared, which only
 * FWOG_POWER_DEFAULT or FWOG_POWER_CUSTOM defines. A display application that
 * declares NEITHER fails to link.
 *
 * That proves a policy was DECLARED. It does NOT prove fwog_power_poll() is
 * reached in the main loop -- no linker can see that. The obvious runtime
 * backstop, a watchdog noticing the poll went quiet, is unavailable: "never
 * add a watchdog to the display CPU" is an existing invariant and a more
 * important one. So do not read a successful link as proof the board can be
 * powered off. */
#ifndef FWOG_POWER_POLL_H
#define FWOG_POWER_POLL_H
#include <stdbool.h>
#include <stdint.h>
#include "input/buttons.h"

typedef struct {
    /* What this poll saw. USE THIS rather than calling fwog_buttons_poll()
     * again: that call carries the debounce state fwog_ship_step() depends
     * on, so a second poll in the same iteration consumes edges out from
     * under the ship-mode machine. */
    fwog_buttons_t buttons;
    unsigned       progress;   /* 0..100 through the hold; 0 when idle */
    bool           armed;      /* a countdown is running */
} fwog_power_t;

/* Call once per main-loop iteration with any monotonic millisecond clock.
 *
 * Polls the debounced red level, advances the 6 s hold machine, renders the
 * countdown on the WS2812 bar (only if ws2812_ready(), and restoring the
 * previous colours if the hold is aborted), and on FWOG_SHIP_FIRED draws
 * the goodbye splash (if fwog_splash_bind() ran) then calls
 * fwog_ship_enter() -- which powers the board off and does not return.
 *
 * Tests fwog_ship_step()'s RETURN VALUE, never s->phase. The field stays
 * FIRED while the button is held, so a caller polling the field would retry
 * the I2C write every iteration when the charger does not answer. That
 * contract exists because of this call site; see ship_mode.h. */
fwog_power_t fwog_power_poll(uint32_t now_ms);

/* Defined by exactly one of the two macros below, referenced by board_init().
 * See the header note for what its absence catches and what it does not. */
extern const unsigned char fwog_power_policy_declared;

/* Red held 6 s powers the board off, with the LED countdown. What almost
 * every display app should say. */
#define FWOG_POWER_DEFAULT() \
    const unsigned char fwog_power_policy_declared = 1

/* This app owns the power path itself and does not call fwog_power_poll().
 * Legitimate -- the bootloader does exactly this through bl_ship.c -- but it
 * is a claim the declarer is making, not one anything checks. */
#define FWOG_POWER_CUSTOM() \
    const unsigned char fwog_power_policy_declared = 2

#endif
