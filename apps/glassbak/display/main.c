/* GlassBak — start a FatFs dump on main USB CDC. */
#include "fwog_display.h"
#include "gb_proto.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>

FWOG_POWER_DEFAULT();

static bool s_lcd, s_leds, s_link, s_chrome_dirty = true;

static void send_dump(void) {
    gb_cmd_t m = { .type = GB_MSG_CMD, .cmd = GB_CMD_DUMP };
    if (s_link) (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void lcd_bringup(void) {
    st7789_init_begin();
    const absolute_time_t deadline = make_timeout_time_ms(500);
    while (!st7789_ready() && !time_reached(deadline)) {
        st7789_init_step();
        sleep_ms(1);
    }
    s_lcd = st7789_ready();
    if (s_lcd) {
        st7789_clear(st7789_rgb565(8, 10, 18));
        board_backlight(255);
        fwog_splash_boot();
        s_chrome_dirty = true;
    }
}

static void paint(void) {
    if (!s_lcd || !s_chrome_dirty) return;
    const uint16_t bg = st7789_rgb565(8, 10, 18);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t fg = st7789_rgb565(230, 230, 230);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    const uint16_t hdr = st7789_rgb565(16, 28, 40);

    st7789_fill_rect(0, 0, ST7789_W, ST7789_H, bg);
    st7789_fill_rect(0, 0, ST7789_W, 36, hdr);
    lcd_text_draw_padded(8, 10, "GlassBak", 16, 2, acc, hdr);
    lcd_text_draw_padded(8, 52, "copy FatFs to this PC / restore", 40, 1, fg, bg);
    lcd_text_draw_padded(8, 80, "1 python tools/fsbak/fsbak.py --gui", 40, 1, dim, bg);
    lcd_text_draw_padded(8, 96, "2 GREEN dump   host Push restores", 40, 1, acc, bg);
    lcd_text_draw_padded(8, 128, "Uses main USB CDC. Never 1200", 40, 1, dim, bg);
    lcd_text_draw_padded(8, 144, "baud (that is BOOTSEL).", 40, 1, dim, bg);
    lcd_text_draw_padded(8, 176, s_link ? "link ok" : "link FAIL", 40, 1,
                         s_link ? acc : st7789_rgb565(230, 180, 70), bg);
    lcd_text_draw_padded(8, 220, "RED hold 6s power off", 40, 1, dim, bg);
    s_chrome_dirty = false;
}

int main(void) {
    board_init();
    fwog_splash_bind("GlassBak", "001");
    s_leds = ws2812_init(pio0, 0u);
    s_link = fwog_link_uart_init(FWOG_LINK_BAUD);
    lcd_bringup();
    if (s_leds) {
        for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++) {
            ws2812_set_color(i, 2, 8, 18);
        }
        ws2812_process();
    }

    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        const fwog_power_t p = fwog_power_poll(now);
        if (!p.armed &&
            (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GREEN))) {
            send_dump();
            DIAG("[glassbak] dump requested\n");
            if (s_leds) {
                for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++) {
                    ws2812_set_color(i, 8, 40, 8);
                }
                ws2812_process();
            }
        }
        paint();
        sleep_ms(2);
    }
}
