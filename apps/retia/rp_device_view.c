#include "rp_device_view.h"
#include "rp_mode.h"
#include "lcd/lcd_text.h"
#include "lcd/st7789.h"
#include "lcd/fwog_splash.h"
#include "leds/ws2812_driver.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

#define RP_BG  0x080A12u
#define RP_ACC 0x50C878u
#define RP_DIM 0x8C96AAu
#define RP_FG  0xC8DCB4u
#define RP_BOX 0x4208u

static bool s_lcd;
static rp_state_t s_shown;
static bool s_dirty = true;
static char s_title[32];

static uint16_t rgb(uint32_t c) {
    return st7789_rgb565((uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
}

static void lcd_bringup(void) {
    absolute_time_t deadline;
    st7789_init_begin();
    deadline = make_timeout_time_ms(500);
    while (!st7789_ready() && !time_reached(deadline)) {
        st7789_init_step();
        sleep_ms(1);
    }
    s_lcd = st7789_ready();
    if (s_lcd) {
        st7789_clear(rgb(RP_BG));
        board_backlight(255);
        fwog_splash_boot();
        s_dirty = true;
    }
}

void rp_device_view_init(void) {
    (void)ws2812_init(pio0, 0u);
    fwog_splash_bind("Retia BP", "005");
    lcd_bringup();
    memset(&s_shown, 0xFF, sizeof s_shown);
}

void rp_device_view_attach(void) {
    s_lcd = st7789_ready();
    s_dirty = true;
    memset(&s_shown, 0xFF, sizeof s_shown);
}

void rp_device_view_paint(const rp_state_t *st) {
    if (!s_lcd || !st) return;
    if (!s_dirty && memcmp(st, &s_shown, sizeof *st) == 0) return;

    const uint16_t bg  = rgb(RP_BG);
    const uint16_t acc = rgb(RP_ACC);
    const uint16_t dim = rgb(RP_DIM);
    const uint16_t fg  = rgb(RP_FG);
    const uint16_t box = rgb(RP_BOX);

    st7789_fill_rect(0, 0, 320, 36, bg);
    st7789_fill_rect(0, 36, 320, 204, bg);

    snprintf(s_title, sizeof s_title, "MODE %s", rp_mode_name((rp_mode_t)st->mode));
    lcd_text_draw_padded(8, 6, s_title, 20, 2, acc, bg);

    char sub[40];
    snprintf(sub, sizeof sub, "%s%s", st->status,
             st->pullups ? "  PU" : "");
    lcd_text_draw_padded(8, 28, sub, 38, 1, dim, bg);

    if (st->line_count == 0) {
        st7789_fill_rect(20, 52, 280, 160, box);
        lcd_text_draw_padded(40, 120, "Nothing to display", 18, 1, fg, box);
    } else {
        int y = 48;
        for (unsigned i = 0; i < st->line_count && i < RP_MAX_LINES; i++) {
            const int h = 28;
            st7789_fill_rect(20, (uint16_t)y, 280, (uint16_t)h, box);
            lcd_text_draw_padded(28, (uint16_t)(y + 8), st->lines[i], 26, 1, fg, box);
            y += h + 6;
        }
    }

    memcpy(&s_shown, st, sizeof s_shown);
    s_dirty = false;
}
