/* LVGL 9 on the FreeWili OG display CPU: the ST7789 panel and the five
 * front-panel buttons, wired up in one call.
 *
 * ---- LVGL IS NOT SHIPPED WITH THIS BSP ----
 *
 * This is the PORT, not the library. Your project supplies LVGL and the BSP
 * builds `fwog_display_lvgl` against it -- the same arrangement the BSP has
 * with the Pico SDK. Add LVGL however you like (submodule, FetchContent, a
 * vendored copy) so long as a target named `lvgl` exists before
 * add_subdirectory() on this BSP, then link `fwog_display_lvgl`:
 *
 *     set(LV_CONF_PATH ${FWOG_LVGL_CONF} CACHE STRING "" FORCE)
 *     add_subdirectory(third_party/lvgl)
 *     add_subdirectory(wiliOGbsp/bsp)
 *     target_link_libraries(myapp fwog_display_lvgl)
 *
 * FWOG_LVGL_CONF names the lv_conf.h shipped beside this header, tuned for
 * this panel: RGB565, no OS, a 64 KB LVGL heap. Point LV_CONF_PATH at your
 * own copy to change it. Developed against LVGL v9.2.2.
 *
 * ---- The main loop ----
 *
 *     fwog_lvgl_init();
 *     while (true) {
 *         const uint32_t now = to_ms_since_boot(get_absolute_time());
 *         const fwog_power_t p = fwog_power_poll(now);
 *         fwog_lvgl_feed_buttons(p.buttons);
 *         lv_timer_handler();
 *         sleep_ms(2);
 *     }
 *
 * ---- Why buttons are FED rather than polled ----
 *
 * This port deliberately does NOT call fwog_buttons_poll() itself, and you
 * must not either. fwog_power_poll() carries the debounce state the 6 s
 * red-hold power-off machine depends on, and a second poll in the same
 * iteration consumes edges out from under it (see power/power_poll.h). So
 * the app polls once and hands the result here.
 *
 * The consequence worth knowing: RED is both LV_KEY_DOWN and the power-off
 * hold. Holding it to scroll down for six seconds powers the board off. That
 * is the board's behaviour, not this port's, and it is why the mapping puts
 * the least-held direction on red.
 *
 * ---- Colour format ----
 *
 * LVGL renders native-endian RGB565 and st7789_blit() byte-swaps to the
 * panel's big-endian order itself. No LV_COLOR_16_SWAP, and do not "optimise"
 * the conversion out of st7789.c -- lcd_text.c and every other caller depend
 * on it too. */
#ifndef FWOG_LVGL_H
#define FWOG_LVGL_H

#include "lvgl.h"
#include "input/buttons.h"

/* Panel geometry, as LVGL sees it. Asserted against ST7789_W/ST7789_H in the
 * implementation, so a driver change cannot silently disagree with this. */
#define FWOG_LVGL_HOR 320
#define FWOG_LVGL_VER 240

/* Partial-render buffer height, in lines. Two buffers of
 * FWOG_LVGL_HOR * FWOG_LVGL_BUF_LINES pixels are allocated statically.
 *
 * WHAT LVGL COSTS ON THIS PART, measured at these defaults against v9.2.2
 * with one button and one label on screen:
 *
 *     flash  ~390 KB   (a bare display app is ~32 KB)
 *     RAM    ~143 KB   of the RP2040's 264 KB
 *
 * The RAM is the number that bites: 2 x 25,600 bytes of buffer here, plus
 * LV_MEM_SIZE (64 KB in the shipped lv_conf.h), plus LVGL's own statics.
 * That leaves roughly 120 KB for everything else including the stack, so an
 * app doing much besides UI should lower one of the two. Halving this to 20
 * gives 25 KB back and costs only more, smaller flushes -- the flush is
 * synchronous, so it trades RAM for a little more time in flush_cb, not for
 * tearing. Flash is not a concern: the display app slot has the whole 16 MB
 * above the bootloader's 128 KB reserve. */
#ifndef FWOG_LVGL_BUF_LINES
#define FWOG_LVGL_BUF_LINES 40
#endif

/* Bring LVGL up: lv_init(), the tick source, the ST7789 display, and a
 * keypad input device for the five buttons.
 *
 * The PANEL MUST BE READY FIRST -- st7789_init_begin() then st7789_init_step()
 * until st7789_ready(). This call does not do that for you, and st7789_blit()
 * silently discards everything before the panel is up, so LVGL would render
 * into a void with no error anywhere. Safe to call more than once; repeat
 * calls are ignored. */
void fwog_lvgl_init(void);

/* Hand LVGL the buttons this iteration saw. Pass fwog_power_poll()'s
 * `.buttons` -- see the note above on why this is not polled here.
 *
 * The mapping is AGENTS.md's recommended arrow pad:
 *   gray   -> LV_KEY_UP        blue  -> LV_KEY_RIGHT
 *   yellow -> LV_KEY_LEFT      red   -> LV_KEY_DOWN
 *   green  -> LV_KEY_ENTER
 *
 * With several down at once the first in that order wins; LVGL's keypad
 * model carries one key at a time. Calling this is optional -- skip it and
 * the input device simply reports nothing. */
void fwog_lvgl_feed_buttons(fwog_buttons_t buttons);

/* The keypad input device, for lv_indev_set_group(). NULL before
 * fwog_lvgl_init(). Widgets only receive keys once they are in a group this
 * is attached to -- that wiring is the application's, not the BSP's. */
lv_indev_t *fwog_lvgl_indev(void);

/* The display, for lv_display_set_rotation() and friends. NULL before
 * fwog_lvgl_init(). */
lv_display_t *fwog_lvgl_display(void);

#endif
