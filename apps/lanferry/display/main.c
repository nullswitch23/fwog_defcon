/* LanFerry — Bottlenose AP file-drop front panel. */
#include "fwog_display.h"
#include "lf_proto.h"
#include "qrcodegen.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

FWOG_POWER_DEFAULT();

static bool           s_lcd, s_link, s_chrome_dirty = true;
static bool           s_want_ap;
static bool           s_qr;
static lf_status_t    s_st;
static bool           s_st_ok;
static fwog_link_rx_t s_rx;
static uint8_t        s_view = 0xFF;
static uint8_t        s_drawn_pct = 0xFF;
static uint32_t       s_hold_g;
static uint8_t        s_fired;
static char           s_line_a[44];
static char           s_line_b[44];
static char           s_line_c[44];
static char           s_line_d[44];
static char           s_line_e[44];
static uint8_t        s_qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(5)];
static char           s_qr_pass[LF_PASS_N];
static bool           s_qr_ok;

static void send_cmd(uint8_t cmd, uint8_t on) {
    lf_cmd_t m = { .type = LF_MSG_CMD, .cmd = cmd, .on = on };
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

static void slots_reset(void) {
    s_line_a[0] = s_line_b[0] = s_line_c[0] = s_line_d[0] = s_line_e[0] = '\0';
    s_drawn_pct = 0xFF;
}

static void qr_refresh(void) {
    const char *pass = s_st.pass[0] ? s_st.pass : "";
    if (s_qr_ok && strcmp(s_qr_pass, pass) == 0) return;
    memset(s_qr_pass, 0, sizeof s_qr_pass);
    strncpy(s_qr_pass, pass, sizeof s_qr_pass - 1u);
    s_qr_ok = false;
    if (!pass[0]) return;
    char wifi[64];
    snprintf(wifi, sizeof wifi, "WIFI:T:WPA;S:FWOG-ferry;P:%s;;", pass);
    uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(5)];
    s_qr_ok = qrcodegen_encodeText(wifi, tmp, s_qrcode, qrcodegen_Ecc_MEDIUM,
                                   2, 5, qrcodegen_Mask_AUTO, true);
}

static void qr_draw(uint16_t bg, uint16_t fg) {
    if (!s_qr_ok) {
        lcd_text_draw_padded(8, 80, "no QR until a password", 36, 1,
                             st7789_rgb565(140, 150, 170), bg);
        return;
    }
    const int n = qrcodegen_getSize(s_qrcode);
    const int scale = 5;
    const int px = n * scale;
    const int x0 = 8;
    const int y0 = 48;
    st7789_fill_rect((uint16_t)x0, (uint16_t)y0, (uint16_t)(px + 8),
                     (uint16_t)(px + 8), st7789_rgb565(255, 255, 255));
    for (int y = 0; y < n; y++) {
        int run = 0, xs = 0;
        for (int x = 0; x <= n; x++) {
            const bool dark = (x < n) && qrcodegen_getModule(s_qrcode, x, y);
            if (dark) {
                if (!run) xs = x;
                run++;
            } else if (run) {
                st7789_fill_rect((uint16_t)(x0 + 4 + xs * scale),
                                 (uint16_t)(y0 + 4 + y * scale),
                                 (uint16_t)(run * scale), (uint16_t)scale, fg);
                run = 0;
            }
        }
    }
}

static uint8_t screen_id(uint8_t fl) {
    if (fl == LF_FLASH_HOLD) return 1u;
    if (fl == LF_FLASH_SYNC || fl == LF_FLASH_WRITE) return 2u;
    if (fl == LF_FLASH_OK) return 3u;
    if (fl == LF_FLASH_FAIL) return 4u;
    if (!s_st_ok || !s_st.hello) return 5u;
    if (s_qr) return 7u;
    return 6u;
}

static void paint(void) {
    if (!s_lcd) return;
    const uint16_t bg  = st7789_rgb565(8, 10, 18);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    const uint16_t fg  = st7789_rgb565(230, 230, 230);
    const uint16_t yel = st7789_rgb565(255, 214, 48);
    const uint16_t rec = st7789_rgb565(220, 40, 40);
    const uint16_t ink = st7789_rgb565(8, 10, 18);
    char line[44];
    const uint8_t fl = s_st_ok ? s_st.flash : 0u;
    const uint8_t view = screen_id(fl);

    if (s_chrome_dirty) {
        st7789_fill_rect(0, 0, 320, 40, bg);
        lcd_text_draw_padded(8, 8, "LanFerry", 16, 2, acc, bg);
        s_chrome_dirty = false;
        s_view = 0xFF;
    }

    if (view != s_view) {
        st7789_fill_rect(0, 40, 320, 200, bg);
        slots_reset();
        s_view = view;
        if (view == 1u) {
            lcd_text_draw_padded(8, 148, "ROM UART is GPIO16/17.", 36, 1, dim, bg);
            lcd_text_draw_padded(8, 164, "takes about a minute.", 36, 1, dim, bg);
        } else if (view == 5u) {
            lcd_text_draw_padded(8, 128, "will not speak BN.", 36, 1, dim, bg);
        } else if (view == 6u) {
            lcd_text_draw_padded(8, 96, "password", 36, 1, dim, bg);
            lcd_text_draw_padded(8, 148, "http://192.168.4.1/", 36, 1, acc, bg);
            lcd_text_draw_padded(8, 210, "GREEN AP  YEL C6  GRAY wipe  BLU QR", 40, 1, dim, bg);
        } else if (view == 7u) {
            qr_refresh();
            qr_draw(dim, ink);
            lcd_text_draw_padded(200, 56, "scan to join", 20, 1, dim, bg);
            lcd_text_draw_padded(200, 210, "BLUE back", 20, 1, dim, bg);
        }
    }

    if (fl == LF_FLASH_HOLD) {
        lcd_text_draw_padded_changed(8, 56, "flash wiliOG C6 image", 36, 1, yel, bg,
                                     s_line_a, sizeof s_line_a);
        lcd_text_draw_padded_changed(8, 80, "1 hold BOOT on Bottlenose", 36, 1, fg, bg,
                                     s_line_b, sizeof s_line_b);
        lcd_text_draw_padded_changed(8, 96, "2 tap RESET, release BOOT", 36, 1, fg, bg,
                                     s_line_c, sizeof s_line_c);
        lcd_text_draw_padded_changed(8, 112, "3 press GREEN", 36, 1, fg, bg,
                                     s_line_d, sizeof s_line_d);
        lcd_text_draw_padded_changed(8, 210, "YELLOW cancels", 36, 1, dim, bg,
                                     s_line_e, sizeof s_line_e);
        return;
    }
    if (fl == LF_FLASH_SYNC || fl == LF_FLASH_WRITE) {
        lcd_text_draw_padded_changed(8, 56,
                                     fl == LF_FLASH_SYNC ? "syncing ROM..." : "writing flash",
                                     36, 1, yel, bg, s_line_a, sizeof s_line_a);
        snprintf(line, sizeof line, "%u %%", (unsigned)s_st.pct);
        lcd_text_draw_padded_changed(8, 88, line, 16, 2, acc, bg,
                                     s_line_b, sizeof s_line_b);
        if (s_drawn_pct != s_st.pct) {
            unsigned bar = (unsigned)s_st.pct * 3u;
            if (bar > 300u) bar = 300u;
            st7789_fill_rect(8, 130, 304, 18, bg);
            if (bar) st7789_fill_rect(8, 130, (uint16_t)bar, 18, acc);
            s_drawn_pct = s_st.pct;
        }
        lcd_text_draw_padded_changed(8, 210, "keep BOOT released", 36, 1, dim, bg,
                                     s_line_e, sizeof s_line_e);
        return;
    }
    if (fl == LF_FLASH_OK) {
        lcd_text_draw_padded_changed(8, 56, "C6 flash ok  tap RESET", 36, 1, acc, bg,
                                     s_line_a, sizeof s_line_a);
        lcd_text_draw_padded_changed(8, 76, "waiting for BN HELLO", 36, 1, dim, bg,
                                     s_line_b, sizeof s_line_b);
        lcd_text_draw_padded_changed(8, 210, "YELLOW flash again", 36, 1, dim, bg,
                                     s_line_e, sizeof s_line_e);
        return;
    }
    if (fl == LF_FLASH_FAIL) {
        lcd_text_draw_padded_changed(8, 56, "C6 flash failed", 36, 1, rec, bg,
                                     s_line_a, sizeof s_line_a);
        lcd_text_draw_padded_changed(8, 76, s_st.file[0] ? s_st.file : "see DIAG", 36, 1, fg, bg,
                                     s_line_b, sizeof s_line_b);
        lcd_text_draw_padded_changed(8, 96, "hold BOOT, tap RESET,", 36, 1, dim, bg,
                                     s_line_c, sizeof s_line_c);
        lcd_text_draw_padded_changed(8, 112, "YELLOW then GREEN", 36, 1, dim, bg,
                                     s_line_d, sizeof s_line_d);
        lcd_text_draw_padded_changed(8, 210, "YELLOW retry", 36, 1, dim, bg,
                                     s_line_e, sizeof s_line_e);
        return;
    }

    if (!s_st_ok || !s_st.hello) {
        lcd_text_draw_padded_changed(8, 56, "waiting for Bottlenose", 36, 1, dim, bg,
                                     s_line_a, sizeof s_line_a);
        lcd_text_draw_padded_changed(8, 76, "YELLOW flashes wiliOG", 36, 1, yel, bg,
                                     s_line_b, sizeof s_line_b);
        lcd_text_draw_padded_changed(8, 96, "image from this UF2.", 36, 1, dim, bg,
                                     s_line_c, sizeof s_line_c);
        lcd_text_draw_padded_changed(8, 112, "stock Orca firmware", 36, 1, dim, bg,
                                     s_line_d, sizeof s_line_d);
        lcd_text_draw_padded_changed(8, 210, "GREEN starts AP once hello", 36, 1, dim, bg,
                                     s_line_e, sizeof s_line_e);
        return;
    }

    if (s_qr) {
        lcd_text_draw_padded_changed(200, 76, s_st.ssid[0] ? s_st.ssid : "FWOG-ferry",
                                     20, 1, fg, bg, s_line_a, sizeof s_line_a);
        lcd_text_draw_padded_changed(200, 96, s_st.pass[0] ? s_st.pass : "--------",
                                     20, 1, yel, bg, s_line_b, sizeof s_line_b);
        if (s_st.pass[0] && strcmp(s_qr_pass, s_st.pass) != 0) {
            s_view = 0xFF;
        }
        return;
    }

    lcd_text_draw_padded_changed(8, 56,
                                 s_st.ap_on ? "AP on   your machines only" : "AP off",
                                 36, 1, s_st.ap_on ? acc : dim, bg,
                                 s_line_a, sizeof s_line_a);
    snprintf(line, sizeof line, "SSID  %s", s_st.ssid[0] ? s_st.ssid : "FWOG-ferry");
    lcd_text_draw_padded_changed(8, 76, line, 36, 1, fg, bg,
                                 s_line_b, sizeof s_line_b);
    lcd_text_draw_padded_changed(8, 112, s_st.pass[0] ? s_st.pass : "--------", 16, 2, yel, bg,
                                 s_line_c, sizeof s_line_c);
    snprintf(line, sizeof line, "clients %u   %u files  %u/%uK",
             (unsigned)s_st.clients, (unsigned)s_st.nfiles,
             (unsigned)((s_st.bytes + 1023u) / 1024u), (unsigned)LF_POOL_KB);
    lcd_text_draw_padded_changed(8, 168, line, 36, 1, fg, bg,
                                 s_line_d, sizeof s_line_d);
    if (s_st.ap_on && s_st.pct && !fl) {
        snprintf(line, sizeof line, "pipe %s  %u%%",
                 s_st.file[0] ? s_st.file : "live", (unsigned)s_st.pct);
    } else if (s_st.file[0] && s_st.file[0] != '-') {
        snprintf(line, sizeof line, "last %s", s_st.file);
    } else {
        snprintf(line, sizeof line, "drop files, other laptop pulls");
    }
    lcd_text_draw_padded_changed(8, 188, line, 40, 1, dim, bg,
                                 s_line_e, sizeof s_line_e);
}

int main(void) {
    board_init();
    fwog_splash_bind("LanFerry", "006");
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
            if (n >= sizeof(lf_status_t) && s_rx.buf[0] == LF_MSG_ST) {
                memcpy(&s_st, s_rx.buf, sizeof s_st);
                s_st_ok = true;
            }
        }
        const uint8_t fl = s_st_ok ? s_st.flash : 0u;
        const bool live = s_st_ok && s_st.hello &&
                          fl != LF_FLASH_SYNC && fl != LF_FLASH_WRITE;
        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) {
            if (fl == LF_FLASH_HOLD || fl == LF_FLASH_FAIL || fl == LF_FLASH_OK) {
                send_cmd(LF_CMD_FLASH, 0u);
            } else if (fl != LF_FLASH_SYNC && fl != LF_FLASH_WRITE) {
                send_cmd(LF_CMD_FLASH, 1u);
            }
        }
        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GREEN)) {
            if (fl == LF_FLASH_HOLD) {
                send_cmd(LF_CMD_FLASH, 2u);
            } else if (fl != LF_FLASH_SYNC && fl != LF_FLASH_WRITE) {
                s_want_ap = !s_want_ap;
                send_cmd(LF_CMD_AP, s_want_ap ? 1u : 0u);
            }
        }
        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_BLUE)) {
            s_fired &= (uint8_t)~FWOG_BTN_BIT(FWOG_BTN_BLUE);
        }
        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GRAY)) {
            s_hold_g = now;
            s_fired &= (uint8_t)~FWOG_BTN_BIT(FWOG_BTN_GRAY);
        }
        if (live && (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GRAY)) &&
            !(s_fired & FWOG_BTN_BIT(FWOG_BTN_GRAY)) &&
            now - s_hold_g >= 700u) {
            s_fired |= (uint8_t)FWOG_BTN_BIT(FWOG_BTN_GRAY);
            send_cmd(LF_CMD_WIPE, 1u);
            s_qr = false;
            s_view = 0xFF;
        }
        if (live && (p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_BLUE)) &&
            !(s_fired & FWOG_BTN_BIT(FWOG_BTN_BLUE))) {
            s_qr = !s_qr;
            s_view = 0xFF;
        }
        paint();
        sleep_ms(20);
    }
}
