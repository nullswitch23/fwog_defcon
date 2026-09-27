/* BattleBridge front panel: C6-hosted four-browser arena lobby. */
#include "fwog_display.h"
#include "bb_proto.h"
#include "qrcodegen.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

FWOG_POWER_DEFAULT();

static bool s_lcd, s_link, s_want_game, s_qr;
static uint32_t s_hold_g, s_hold_b;
static uint8_t s_fired;
static bb_status_t s_st;
static bool s_st_ok;
static fwog_link_rx_t s_rx;
static uint8_t s_view = 0xFF;
static char s_line[7][44];
static uint8_t s_qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(5)];
static char s_qr_pass[sizeof s_st.pass];
static bool s_qr_ok;

static void send_cmd(uint8_t cmd, uint8_t on) {
    bb_cmd_t m = { .type = BB_MSG_CMD, .cmd = cmd, .on = on };
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
        st7789_clear(st7789_rgb565(6, 9, 20));
        board_backlight(255);
        fwog_splash_boot();
    }
}

static void clear_slots(void) {
    memset(s_line, 0, sizeof s_line);
}

static void qr_refresh(void) {
    const char *pass = s_st.pass[0] ? s_st.pass : "";
    if (s_qr_ok && strcmp(s_qr_pass, pass) == 0) return;
    memset(s_qr_pass, 0, sizeof s_qr_pass);
    strncpy(s_qr_pass, pass, sizeof s_qr_pass - 1u);
    s_qr_ok = false;
    if (!pass[0]) return;
    char wifi[64];
    snprintf(wifi, sizeof wifi, "WIFI:T:WPA;S:FWOG-arena;P:%s;;", pass);
    uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(5)];
    s_qr_ok = qrcodegen_encodeText(wifi, tmp, s_qrcode, qrcodegen_Ecc_MEDIUM,
                                   2, 5, qrcodegen_Mask_AUTO, true);
}

static void qr_draw(uint16_t bg, uint16_t ink) {
    if (!s_qr_ok) {
        lcd_text_draw_padded(8, 80, "no QR until a password", 36, 1,
                             st7789_rgb565(140, 150, 170), bg);
        return;
    }
    const int n = qrcodegen_getSize(s_qrcode);
    const int scale = 5;
    const int px = n * scale;
    const int x0 = 8;
    const int y0 = 44;
    st7789_fill_rect((uint16_t)(x0 - 4), (uint16_t)(y0 - 4),
                     (uint16_t)(px + 8), (uint16_t)(px + 8),
                     st7789_rgb565(255, 255, 255));
    for (int y = 0; y < n; y++) {
        int run = 0, xs = 0;
        for (int x = 0; x <= n; x++) {
            const bool dark = x < n && qrcodegen_getModule(s_qrcode, x, y);
            if (dark) {
                if (!run) xs = x;
                run++;
            } else if (run) {
                st7789_fill_rect((uint16_t)(x0 + xs * scale),
                                 (uint16_t)(y0 + y * scale),
                                 (uint16_t)(run * scale), scale, ink);
                run = 0;
            }
        }
    }
}

static uint8_t view_id(void) {
    if (s_st.flash == BB_FLASH_HOLD) return 1;
    if (s_st.flash == BB_FLASH_SYNC || s_st.flash == BB_FLASH_WRITE) return 2;
    if (s_st.flash == BB_FLASH_OK) return 3;
    if (s_st.flash == BB_FLASH_FAIL) return 4;
    if (!s_st_ok || !s_st.hello) return 5;
    if (s_qr) return 7;
    return 6;
}

static void text(unsigned slot, uint16_t y, const char *s, uint16_t fg,
                 uint16_t bg) {
    lcd_text_draw_padded_changed(8, y, s, 40, 1, fg, bg,
                                 s_line[slot], sizeof s_line[slot]);
}

static void paint(void) {
    if (!s_lcd) return;
    const uint16_t bg = st7789_rgb565(6, 9, 20);
    const uint16_t hdr = st7789_rgb565(12, 24, 43);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t fg = st7789_rgb565(230, 235, 245);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    const uint16_t yel = st7789_rgb565(255, 214, 48);
    const uint16_t red = st7789_rgb565(232, 48, 40);
    const uint16_t ink = st7789_rgb565(0, 0, 0);
    char line[44];
    const uint8_t view = view_id();
    if (view != s_view) {
        st7789_clear(bg);
        st7789_fill_rect(0, 0, 320, 34, hdr);
        lcd_text_draw_padded(8, 6, "BattleBridge", 16, 2, acc, hdr);
        clear_slots();
        s_view = view;
        /* Draw the QR once per enter (or password change). Filling the
         * modules every 20 ms is what made the code flash. */
        if (view == 7) {
            qr_refresh();
            qr_draw(bg, ink);
            lcd_text_draw_padded(210, 56, "scan Wi-Fi", 18, 1, dim, bg);
            lcd_text_draw_padded(210, 78, "FWOG-arena", 18, 1, fg, bg);
            lcd_text_draw_padded(210, 188, "GRAY new pass", 18, 1, dim, bg);
            lcd_text_draw_padded(210, 210, "BLUE back", 18, 1, dim, bg);
        }
    }

    if (view == 1) {
        text(0, 56, "flash wiliOG C6 image", yel, bg);
        text(1, 80, "1 hold BOOT on Bottlenose", fg, bg);
        text(2, 96, "2 tap RESET, release BOOT", fg, bg);
        text(3, 112, "3 press GREEN", fg, bg);
        text(4, 210, "YELLOW cancels", dim, bg);
        return;
    }
    if (view == 2) {
        text(0, 56, s_st.flash == BB_FLASH_SYNC ?
             "syncing C6 ROM..." : "writing C6 game host", yel, bg);
        snprintf(line, sizeof line, "%u %%", (unsigned)s_st.pct);
        text(1, 88, line, acc, bg);
        st7789_fill_rect(8, 124, 304, 18, bg);
        if (s_st.pct) {
            st7789_fill_rect(8, 124, (uint16_t)(s_st.pct * 3u), 18, acc);
        }
        text(4, 210, "release BOOT while writing", dim, bg);
        return;
    }
    if (view == 3) {
        text(0, 56, "C6 flash ok", acc, bg);
        text(1, 76, "release BOOT, tap RESET", fg, bg);
        text(4, 210, "waiting for BN HELLO", dim, bg);
        return;
    }
    if (view == 4) {
        text(0, 56, "C6 flash failed", red, bg);
        text(1, 76, s_st.why[0] ? s_st.why : "see main CDC", fg, bg);
        text(2, 96, "hold BOOT, tap RESET,", dim, bg);
        text(3, 112, "YELLOW then GREEN", dim, bg);
        return;
    }
    if (view == 5) {
        text(0, 56, "waiting for Bottlenose", dim, bg);
        text(1, 80, "YELLOW flashes game host", yel, bg);
        text(2, 104, "C6 serves all four browsers.", fg, bg);
        text(4, 210, "hold BOOT, RESET, GREEN", dim, bg);
        return;
    }
    if (view == 7) {
        if (s_st.pass[0] && strcmp(s_qr_pass, s_st.pass) != 0) {
            s_view = 0xFF;
        }
        qr_refresh();
        lcd_text_draw_padded_changed(210, 100,
                                     s_st.pass[0] ? s_st.pass : "--------",
                                     18, 1, yel, bg, s_line[0],
                                     sizeof s_line[0]);
        return;
    }

    text(0, 48, s_st.game_on ? "arena AP ON" : "arena AP off", 
         s_st.game_on ? acc : dim, bg);
    snprintf(line, sizeof line, "SSID  %s",
             s_st.ssid[0] ? s_st.ssid : "FWOG-arena");
    text(1, 70, line, fg, bg);
    snprintf(line, sizeof line, "PASS  %s",
             s_st.pass[0] ? s_st.pass : "--------");
    text(2, 92, line, yel, bg);
    snprintf(line, sizeof line, "players %u / 4  %s  ph %u",
             (unsigned)s_st.players, s_st.teams ? "2v2" : "FFA",
             (unsigned)s_st.phase);
    text(3, 126, line, fg, bg);
    {
        static const char *bot_lbl[] = { "off   ", "sleepy", "normal", "melee " };
        unsigned b = s_st.bots > 3u ? 3u : s_st.bots;
        snprintf(line, sizeof line, "bots %s", bot_lbl[b]);
        text(4, 148, line, b ? acc : dim, bg);
    }
    if (s_st.sudden) {
        text(5, 168, "SUDDEN DEATH", yel, bg);
    } else if (s_st.phase == 2u || s_st.phase == 1u) {
        snprintf(line, sizeof line, "clock %us", (unsigned)s_st.clock_s);
        text(5, 168, line, fg, bg);
    } else {
        text(5, 168, "90s match", dim, bg);
    }
    text(6, 188, "open http://192.168.4.1/", acc, bg);
    lcd_text_draw_padded(8, 210, "G AP  GRAY go/skill  YEL C6  BLUE QR/2v2", 40, 1,
                         dim, bg);
}

int main(void) {
    board_init();
    fwog_splash_bind("BattleBridge", "001");
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
            if (n >= sizeof(bb_status_t) && s_rx.buf[0] == BB_MSG_ST) {
                memcpy(&s_st, s_rx.buf, sizeof s_st);
                s_st_ok = true;
            }
        }
        const uint8_t fl = s_st_ok ? s_st.flash : 0;
        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) {
            if (fl == BB_FLASH_HOLD || fl == BB_FLASH_FAIL ||
                fl == BB_FLASH_OK) {
                send_cmd(BB_CMD_FLASH, 0);
            } else if (fl != BB_FLASH_SYNC && fl != BB_FLASH_WRITE) {
                send_cmd(BB_CMD_FLASH, 1);
            }
        }
        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GREEN)) {
            if (fl == BB_FLASH_HOLD) {
                send_cmd(BB_CMD_FLASH, 2);
            } else if (fl != BB_FLASH_SYNC && fl != BB_FLASH_WRITE &&
                       s_st_ok && s_st.hello) {
                s_want_game = !s_want_game;
                send_cmd(BB_CMD_GAME, s_want_game ? 1 : 0);
            }
        }
        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GRAY)) {
            s_hold_g = now;
            s_fired &= (uint8_t)~FWOG_BTN_BIT(FWOG_BTN_GRAY);
        }
        const bool live = s_st_ok && s_st.hello &&
                          fl != BB_FLASH_SYNC && fl != BB_FLASH_WRITE &&
                          fl != BB_FLASH_HOLD;
        if (live && (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GRAY)) &&
            !(s_fired & FWOG_BTN_BIT(FWOG_BTN_GRAY)) &&
            now - s_hold_g >= 700u) {
            s_fired |= (uint8_t)FWOG_BTN_BIT(FWOG_BTN_GRAY);
            if (s_qr) send_cmd(BB_CMD_WIPE, 1);
            else send_cmd(BB_CMD_BOTS, (uint8_t)((s_st.bots + 1u) % 4u));
        }
        if (live && (p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_GRAY)) &&
            !(s_fired & FWOG_BTN_BIT(FWOG_BTN_GRAY))) {
            send_cmd(BB_CMD_GO, 1);
        }
        if (s_st_ok && s_st.hello &&
            (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_BLUE))) {
            s_hold_b = now;
            s_fired &= (uint8_t)~FWOG_BTN_BIT(FWOG_BTN_BLUE);
        }
        if (live && (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) &&
            !(s_fired & FWOG_BTN_BIT(FWOG_BTN_BLUE)) &&
            now - s_hold_b >= 700u) {
            s_fired |= (uint8_t)FWOG_BTN_BIT(FWOG_BTN_BLUE);
            send_cmd(BB_CMD_TEAMS, s_st.teams ? 0 : 1);
        }
        if (live && (p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_BLUE)) &&
            !(s_fired & FWOG_BTN_BIT(FWOG_BTN_BLUE))) {
            s_qr = !s_qr;
            s_view = 0xFF;
        }
        if (ws2812_ready() && !p.armed) {
            for (unsigned i = 0; i < FWOG_LED_COUNT; i++) {
                ws2812_set_color(i, 3, s_st.game_on ? 20 : 4,
                                 i < s_st.players ? 20 : 3);
            }
            ws2812_process();
        }
        paint();
        sleep_ms(20);
    }
}
