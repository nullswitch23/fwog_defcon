/* The bootloader's fixed-zone screen, over lcd_text/st7789.
 *
 * bl_ui.c owns a small set of independently-repainted zones (see
 * bl_ui_zone_id_t below) and draws the bootloader's five states, main's
 * STATUS line, and the version/title chrome into them via lcd_text_draw_padded().
 * DIAG() output is kept alongside every draw -- a blank panel otherwise has
 * two indistinguishable causes, driver broken or bl_ui never called -- and
 * both are fed by the same bl_ui_state_text()/bl_ui_bargraph()/
 * bl_ui_state_line() so they cannot disagree.
 *
 * Those three plus the zone table are pure and host-tested: they settle the
 * wording, the bar's flooring rule, and the geometry once, so nothing about
 * what should appear depends on the panel being present to check it. Only
 * the drawing itself -- bl_ui_show()/bl_ui_status()/bl_ui_tick(), and
 * everything below the HOST_TEST guard -- needs the ST7789. */
#ifndef FWOG_BL_UI_H
#define FWOG_BL_UI_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    BL_UI_WAITING = 0,   /* "waiting for main"  */
    BL_UI_UPDATING,      /* "updating", with a percentage */
    BL_UI_NO_APP,        /* "no valid app"      */
    BL_UI_CRC_FAIL,      /* "image CRC failed"  */
    BL_UI_CONSOLE        /* "console active"    */
} bl_ui_state_t;

/* Never NULL, including for an out-of-range value. */
const char *bl_ui_state_text(bl_ui_state_t s);

#define BL_BAR_CELLS 20u
#define BL_BAR_BUF   (BL_BAR_CELLS + 3u)   /* '[' + cells + ']' + NUL */

/* Render `percent` as a fixed-width text bar: "[####      ]".
 *
 * `out` must have room for BL_BAR_BUF bytes. Any value above 100 -- which
 * includes FWOG_BL_STATUS_NO_BAR -- draws an empty bar rather than wrapping
 * round to a nearly-full one.
 *
 * Text, not pixels, so the identical bar goes to DIAG(), to the console, and
 * to the ST7789 once plan 3 lands. Pure, so the flooring rule below is
 * actually tested.
 *
 * The fill is FLOORED, never rounded: a full bar is how a person reads
 * "finished", so 99% must leave a cell empty. */
void bl_ui_bargraph(char *out, uint8_t percent);

/* ---- Screen layout ----
 *
 * Fixed zones, each repainted independently: without a framebuffer a
 * scrolling log would mean repainting all 240 rows per line, and the panel
 * offers no read-back to shift pixels cheaply. */
typedef enum {
    BL_ZONE_TITLE = 0,
    BL_ZONE_STATE,
    BL_ZONE_BAR,
    BL_ZONE_VER_BL,
    BL_ZONE_VER_APP,
    BL_ZONE_COUNT
} bl_ui_zone_id_t;

typedef struct {
    uint16_t x, y, w, h;
    uint8_t  scale;      /* default scale; STATE drops to 1 for main's line */
} bl_ui_zone_t;

/* NULL for an out-of-range id. */
const bl_ui_zone_t *bl_ui_zone(bl_ui_zone_id_t id);

/* True when a zone must be filled before drawing, because the new text is
 * a different size from what is already there and smaller glyphs would
 * leave fragments of the larger ones behind. */
bool bl_ui_zone_needs_clear(uint8_t last_scale, uint8_t new_scale);

/* Longest line is "image CRC failed" (16) or "updating 100%" (13). */
#define BL_UI_LINE_BUF 24u

/* Compose the STATE zone's text: the state string, plus " N%" for
 * BL_UI_UPDATING with a percentage of 0-100. Always NUL-terminated. */
void bl_ui_state_line(char *out, size_t cap, bl_ui_state_t s, uint8_t percent);

/* ---- The readiness edge ----
 *
 * st7789_clear() is asynchronous, so the moment the panel becomes ready
 * splits in two: start the ~25 ms full-screen clear, then -- some number of
 * ticks later, with the bootloader's link loop having run throughout --
 * paint the chrome, replay whatever arrived while the panel was coming up,
 * and raise the backlight.
 *
 * Pure and host-tested. What the tests in tests/test_bl_ui.c actually pin is
 * the sequencing of the edge itself: the clear is issued exactly once,
 * nothing is issued while it is still on the wire, FINISH fires exactly once
 * when it retires, and DONE is terminal. That last one is the catastrophic
 * case -- a second CLEAR from DONE would wipe the screen on every tick.
 *
 * Two further properties fixed during plan 3's review must also not regress:
 * the backlight comes up only after the first painted frame, and the pending
 * replay runs last. Those live in bl_ui_tick()'s FINISH branch, below the
 * HOST_TEST guard, so nothing here asserts them -- they are the caller's
 * obligation, and changing that branch means re-checking them by reading. */
typedef enum {
    BL_UI_PAINT_IDLE = 0,   /* panel not ready, or ready but nothing sent  */
    BL_UI_PAINT_CLEARING,   /* the full-screen clear is on the wire        */
    BL_UI_PAINT_DONE        /* the screen is live; ordinary drawing allowed */
} bl_ui_paint_state_t;

typedef enum {
    BL_UI_PAINT_NONE = 0,   /* nothing to do this tick                     */
    BL_UI_PAINT_CLEAR,      /* start the clear, then go to CLEARING        */
    BL_UI_PAINT_FINISH      /* go to DONE, then chrome, replay, backlight  */
} bl_ui_paint_action_t;

/* What this tick must do, given the current state, whether the panel has
 * finished its staged init, and whether a fill is still on the wire. */
bl_ui_paint_action_t bl_ui_paint_step(bl_ui_paint_state_t st, bool ready,
                                      bool busy);

/* True once ordinary zone drawing is allowed. Before that, everything the
 * bootloader wants on screen has to be recorded and replayed. */
bool bl_ui_paint_drawable(bl_ui_paint_state_t st);

#ifndef HOST_TEST
/* Called once, when the bootloader first decides it is not handing off
 * immediately. Both strings must outlive the call -- they point into flash
 * or into the metadata record. Either may be NULL for "unknown". */
void bl_ui_init(const char *bl_version, const char *app_version);

/* Render. `percent` is used only by BL_UI_UPDATING.
 *
 * Callers should invoke this only when the state or the percentage
 * actually changed: with the DIAG implementation, calling it every loop
 * iteration floods the console. */
void bl_ui_show(bl_ui_state_t s, uint8_t percent);

/* Render a line main pushed over the link, with an optional bar.
 *
 * Distinct from bl_ui_show() because the text is main's, not one of the five
 * states: only main knows which step it is on or what it is waiting for.
 * `percent` is 0-100, or FWOG_BL_STATUS_NO_BAR for text only.
 *
 * Whatever this draws is replaced the next time the bootloader's own state
 * changes -- main's line is the more specific message, but the bootloader's
 * is the more current one. */
void bl_ui_status(const char *text, uint8_t percent);

/* Advance the panel's staged initialization. Cheap; call every loop
 * iteration once bl_ui_init() has run.
 *
 * This function is why plan 3 touches this header at all. The panel needs
 * ~125 ms of datasheet-mandated delays and bl_ui_init() runs inside the
 * bootloader's link loop, where main's per-chunk ACK_WAIT_MS is 200 ms.
 * Blocking there would spend main's retry budget; bl_ui_show() cannot
 * drive the sequence because it is only called when something changes,
 * and in BL_UI_WAITING nothing does. */
void bl_ui_tick(void);
#endif

#endif
