/* See fwog_lvgl.h. Extracted from a working application port and reshaped
 * into the BSP, plus the keypad input device the application did not have. */
#include "lvgl/fwog_lvgl.h"

#include "lcd/st7789.h"
#include "pico/stdlib.h"

_Static_assert(FWOG_LVGL_HOR == ST7789_W && FWOG_LVGL_VER == ST7789_H,
               "LVGL panel geometry disagrees with the ST7789 driver's own");

#define BUF_PX (FWOG_LVGL_HOR * FWOG_LVGL_BUF_LINES)

/* Two partial-render buffers, so LVGL can render one while the other is
 * being flushed. Static rather than heap: this is the largest allocation in
 * a typical display app and a failure at runtime would be far worse than a
 * link error. */
static lv_color_t s_buf1[BUF_PX];
static lv_color_t s_buf2[BUF_PX];

static lv_display_t *s_disp;
static lv_indev_t   *s_indev;
static bool          s_ready;

/* What the last fwog_lvgl_feed_buttons() saw. LVGL's keypad model is one key
 * at a time, so this is a single key plus a state rather than a mask. */
static uint32_t s_key;
static lv_indev_state_t s_state = LV_INDEV_STATE_RELEASED;

static uint32_t tick_cb(void) {
    return to_ms_since_boot(get_absolute_time());
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
    const uint16_t x = (uint16_t)area->x1;
    const uint16_t y = (uint16_t)area->y1;
    const uint16_t w = (uint16_t)(area->x2 - area->x1 + 1);
    const uint16_t h = (uint16_t)(area->y2 - area->y1 + 1);

    /* No st7789_dma_wait() here, and none is needed: st7789_set_window() and
       st7789_blit() each wait internally, and st7789_blit() is a synchronous
       chunked spi_write_blocking() -- it has fully drained the bus by the
       time it returns. Only st7789_fill_rect() is DMA-asynchronous, and this
       path does not use it. That is why flush_ready() can be called
       immediately rather than from a DMA completion. */
    st7789_set_window(x, y, w, h);
    st7789_blit((const uint16_t *)px, (size_t)w * (size_t)h);

    lv_display_flush_ready(disp);
}

/* AGENTS.md's recommended arrow pad. Order is priority: with several buttons
 * down at once, the first match wins, because a keypad device carries one
 * key. ENTER is checked first so a select cannot be masked by a direction
 * the user has not let go of yet. */
static const struct { fwog_btn_id_t btn; uint32_t key; } KEYMAP[] = {
    { FWOG_BTN_GREEN,  LV_KEY_ENTER },
    { FWOG_BTN_GRAY,   LV_KEY_UP    },
    { FWOG_BTN_YELLOW, LV_KEY_LEFT  },
    { FWOG_BTN_BLUE,   LV_KEY_RIGHT },
    { FWOG_BTN_RED,    LV_KEY_DOWN  },
};

void fwog_lvgl_feed_buttons(fwog_buttons_t buttons) {
    for (unsigned i = 0; i < count_of(KEYMAP); i++) {
        if (buttons.down & FWOG_BTN_BIT(KEYMAP[i].btn)) {
            s_key = KEYMAP[i].key;
            s_state = LV_INDEV_STATE_PRESSED;
            return;
        }
    }
    /* Nothing down. Report RELEASED against whichever key was last pressed:
       LVGL needs the release to pair with the press it already saw, so s_key
       is deliberately NOT cleared here. */
    s_state = LV_INDEV_STATE_RELEASED;
}

static void indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    data->key = s_key;
    data->state = s_state;
}

void fwog_lvgl_init(void) {
    if (s_ready) return;

    lv_init();
    lv_tick_set_cb(tick_cb);

    s_disp = lv_display_create(FWOG_LVGL_HOR, FWOG_LVGL_VER);
    lv_display_set_flush_cb(s_disp, flush_cb);
    lv_display_set_buffers(s_disp, s_buf1, s_buf2, sizeof(s_buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(s_indev, indev_read_cb);

    s_ready = true;
}

lv_indev_t *fwog_lvgl_indev(void) { return s_indev; }
lv_display_t *fwog_lvgl_display(void) { return s_disp; }
