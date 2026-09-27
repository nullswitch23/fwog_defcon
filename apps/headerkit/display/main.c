#include "fwog_display.h"
#include "hk_proto.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

FWOG_POWER_DEFAULT();

static bool s_lcd, s_link, s_chrome_dirty = true, s_st_ok;
static hk_status_t s_st;
static fwog_link_rx_t s_rx;
static uint8_t s_page;
static char s_line_found[44];
static char s_line_addr[8][12];

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

static void send_scan(void) {
    hk_cmd_t m = { .type = HK_MSG_CMD, .cmd = HK_CMD_SCAN };
    if (s_link) (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void draw_centered(uint16_t cx, uint16_t y, const char *text,
                          uint16_t color, uint16_t bg) {
    const unsigned n = (unsigned)strlen(text);
    const unsigned w = lcd_text_width_px(n, 1u);
    const uint16_t x = cx > w / 2u ? (uint16_t)(cx - w / 2u) : 0u;
    lcd_text_draw_padded(x, y, text, n, 1, color, bg);
}

static void draw_physical_pinout(uint16_t bg, uint16_t fg, uint16_t dim,
                                 uint16_t acc, uint16_t yel) {
    static const char *const top[10] = {
        "5V", "VPIN", "3V3", "SCL", "SDA",
        "MISO", "G26", "SWC", "SWD", "GND"
    };
    static const char *const bottom[10] = {
        "CS", "G27", "RX", "CTS", "TX",
        "RTS", "MOSI", "SCK", "G25", "GND"
    };
    static const uint8_t top_group[10] = { 0, 0, 0, 1, 1, 2, 3, 4, 4, 5 };
    static const uint8_t bottom_group[10] = { 2, 3, 6, 6, 6, 6, 2, 2, 3, 5 };
    const uint16_t group_color[7] = {
        yel, st7789_rgb565(80, 210, 220), acc, fg,
        st7789_rgb565(220, 120, 220), dim, st7789_rgb565(255, 150, 90)
    };
    const uint16_t shell = st7789_rgb565(35, 40, 50);
    const uint16_t pin = st7789_rgb565(215, 215, 200);
    char number[4];

    lcd_text_draw_padded(8, 42, "SIDE HEADER - FACE VIEW", 40, 1, yel, bg);
    lcd_text_draw_padded(8, 54, "LCD up; key notch up", 40, 1, dim, bg);
    st7789_fill_rect(8, 92, 304, 44, shell);
    st7789_fill_rect(155, 92, 10, 7, bg);

    for (unsigned i = 0; i < 10u; i++) {
        const uint16_t cx = (uint16_t)(25u + i * 30u);
        const unsigned even = 2u + i * 2u;
        const unsigned odd = 1u + i * 2u;

        draw_centered(cx, 64, top[i], group_color[top_group[i]], bg);
        snprintf(number, sizeof number, "%u", even);
        draw_centered(cx, 80, number, fg, bg);
        st7789_fill_rect((uint16_t)(cx - 4u), 102, 8, 8, pin);

        st7789_fill_rect((uint16_t)(cx - 4u), 119, 8, 8, pin);
        snprintf(number, sizeof number, "%u", odd);
        draw_centered(cx, 142, number, fg, bg);
        draw_centered(cx, 156, bottom[i], group_color[bottom_group[i]], bg);
    }

    lcd_text_draw_padded(8, 180, "3.3V setup: jumper 4 VPIN -> 6", 40, 1, acc, bg);
    lcd_text_draw_padded(8, 194, "Pin 2 is 5V. Verify before wiring.", 40, 1, yel, bg);
}

static void paint(void) {
    if (!s_lcd) return;
    const uint16_t bg  = st7789_rgb565(8, 10, 18);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    const uint16_t fg  = st7789_rgb565(230, 230, 230);
    const uint16_t yel = st7789_rgb565(255, 214, 48);
    char line[40];
    if (s_chrome_dirty) {
        st7789_clear(bg);
        lcd_text_draw_padded(8, 8, "HeaderKit", 18, 2, acc, bg);
        if (s_page == 0u) {
            lcd_text_draw_padded(8, 42, "I2C SCAN   3.3 V ONLY", 40, 1, yel, bg);
            lcd_text_draw_padded(8, 58, "SDA GP16   SCL GP17   pull-ups ON",
                                 40, 1, dim, bg);
            lcd_text_draw_padded(8, 184, "SPI UART GPIO remain Hi-Z", 40, 1,
                                 acc, bg);
        } else if (s_page == 1u) {
            lcd_text_draw_padded(8, 42, "BREAKOUT PINOUT  1 / 3", 40, 1, yel, bg);
            lcd_text_draw_padded(8, 72, "I2C0   SDA 16    SCL 17", 40, 1, fg, bg);
            lcd_text_draw_padded(8, 96, "UART1  TX 8      RX 9", 40, 1, fg, bg);
            lcd_text_draw_padded(8, 120, "       CTS 10    RTS 11", 40, 1, fg, bg);
            lcd_text_draw_padded(8, 160, "Pin reference only; outputs Hi-Z.", 40,
                                 1, dim, bg);
        } else if (s_page == 2u) {
            lcd_text_draw_padded(8, 42, "BREAKOUT PINOUT  2 / 3", 40, 1, yel, bg);
            lcd_text_draw_padded(8, 72, "SPI1   CS 13     SCK 14", 40, 1, fg, bg);
            lcd_text_draw_padded(8, 96, "       MISO 12   MOSI 15", 40, 1, fg, bg);
            lcd_text_draw_padded(8, 120, "GPIO   IN 26     OUT 27", 40, 1, fg, bg);
            lcd_text_draw_padded(8, 160, "Use Retia for active bus terminal.", 40,
                                 1, dim, bg);
        } else {
            draw_physical_pinout(bg, fg, dim, acc, yel);
        }
        lcd_text_draw_padded(8, 216, "YEL/BLU pages  GRAY map  GREEN scan", 40, 1,
                             dim, bg);
        s_line_found[0] = '\0';
        memset(s_line_addr, 0, sizeof s_line_addr);
        s_chrome_dirty = false;
    }
    if (s_page != 0u) return;
    snprintf(line, sizeof line, "found %u   io %s   safe %s",
             s_st_ok ? (unsigned)s_st.n : 0,
             (s_st_ok && s_st.io_ok) ? "ok" : "wait",
             (s_st_ok && s_st.other_buses_hiz) ? "Hi-Z" : "wait");
    lcd_text_draw_padded_changed(8, 86, line, 40, 1, fg, bg,
                                 s_line_found, sizeof s_line_found);
    for (unsigned i = 0; i < 8u; i++) {
        if (s_st_ok && i < s_st.n) {
            snprintf(line, sizeof line, "0x%02X", s_st.addr[i]);
        } else {
            snprintf(line, sizeof line, "--");
        }
        lcd_text_draw_padded_changed((uint16_t)(8u + (i % 4u) * 72u),
                                     (uint16_t)(112u + (i / 4u) * 20u),
                                     line, 8, 1, fg, bg,
                                     s_line_addr[i], sizeof s_line_addr[i]);
    }
}

int main(void) {
    board_init();
    fwog_splash_bind("HeaderKit", "004");
    (void)ws2812_init(pio0, 0u);
    lcd_bringup();
    s_link = fwog_link_uart_init(FWOG_LINK_BAUD);
    fwog_link_rx_init(&s_rx);

    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        const fwog_power_t p = fwog_power_poll(now);
        uint8_t b;
        size_t n;
        while (fwog_link_uart_read(&b)) {
            if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
            if (fwog_ioexp_link_handle(s_rx.buf, n)) continue;
            if (n >= sizeof(hk_status_t) && s_rx.buf[0] == HK_MSG_ST) {
                memcpy(&s_st, s_rx.buf, sizeof s_st);
                s_st_ok = true;
            }
        }
        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) {
            s_page = s_page == 0u ? 3u : (uint8_t)(s_page - 1u);
            s_chrome_dirty = true;
        }
        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_BLUE)) {
            s_page = (uint8_t)((s_page + 1u) % 4u);
            s_chrome_dirty = true;
        }
        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GRAY)) {
            s_page = 3u;
            s_chrome_dirty = true;
        }
        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GREEN)) {
            s_page = 0u;
            s_chrome_dirty = true;
            send_scan();
        }
        paint();
        sleep_ms(2);
    }
}
