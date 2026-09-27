/* AirMaraud — WIFIPROOF deauth operator front panel. */
#include "fwog_display.h"
#include "am_proto.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

FWOG_POWER_DEFAULT();

static bool           s_lcd, s_link, s_leds, s_chrome_dirty = true;
static am_status_t    s_st;
static bool           s_st_ok;
static fwog_link_rx_t s_rx;
static uint8_t        s_view = 0xFF;
static uint8_t        s_drawn_pct = 0xFF;
static char           s_line_a[44];
static char           s_line_b[44];
static char           s_line_c[44];
static char           s_line_d[44];
static char           s_line_e[44];
static char           s_line_ret[44];
static char           s_line_hint[44];
static char           s_line_tgt[AM_ROWS][44];
static bool           s_frozen;
static am_status_t    s_hold;

static void send_cmd(uint8_t cmd, uint8_t on) {
    am_cmd_t m = { .type = AM_MSG_CMD, .cmd = cmd, .on = on };
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

static void leds_state(void) {
    if (!s_leds) return;
    uint8_t r = 2, g = 2, b = 8;
    if (s_st_ok && s_st.wp_on) {
        if (s_st.armed) {
            r = 40;
            g = 8;
            b = 8;
        } else {
            r = 8;
            g = 36;
            b = 8;
        }
    }
    for (unsigned i = 0; i < FWOG_LED_COUNT; i++) {
        ws2812_set_color(i, r, g, b);
    }
    ws2812_process();
}

static void slots_reset(void) {
    s_line_a[0] = s_line_b[0] = s_line_c[0] = s_line_d[0] = s_line_e[0] = '\0';
    s_line_ret[0] = '\0';
    s_line_hint[0] = '\0';
    memset(s_line_tgt, 0, sizeof s_line_tgt);
    s_drawn_pct = 0xFF;
}

static uint8_t screen_id(uint8_t fl) {
    if (fl == AM_FLASH_HOLD) return 1u;
    if (fl == AM_FLASH_SYNC || fl == AM_FLASH_WRITE) return 2u;
    if (fl == AM_FLASH_OK) return 3u;
    if (fl == AM_FLASH_FAIL) return 4u;
    if (!s_st_ok || !s_st.hello) return 5u;
    return 6u;
}

static const char *mode_name(uint8_t m) {
    return m == AM_MODE_DASSOC ? "DASSOC" : "DEAUTH";
}

static void paint(void) {
    if (!s_lcd) return;
    const uint16_t bg  = st7789_rgb565(8, 10, 18);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    const uint16_t fg  = st7789_rgb565(230, 230, 230);
    const uint16_t yel = st7789_rgb565(255, 214, 48);
    const uint16_t rec = st7789_rgb565(220, 40, 40);
    char line[44];
    const uint8_t fl = s_st_ok ? s_st.flash : 0u;
    const uint8_t view = screen_id(fl);

    if (s_chrome_dirty) {
        st7789_fill_rect(0, 0, 320, 40, bg);
        lcd_text_draw_padded(8, 8, "AirMaraud", 16, 2, acc, bg);
        s_chrome_dirty = false;
        s_view = 0xFF;
    }

    if (view != s_view) {
        st7789_fill_rect(0, 40, 320, 200, bg);
        slots_reset();
        s_view = view;
    }

    if (fl == AM_FLASH_HOLD) {
        lcd_text_draw_padded_changed(8, 56, "flash wiliOG C6 image", 36, 1, yel, bg,
                                     s_line_a, sizeof s_line_a);
        lcd_text_draw_padded_changed(8, 76, "1 unplug Orca USB-C", 36, 1, fg, bg,
                                     s_line_b, sizeof s_line_b);
        lcd_text_draw_padded_changed(8, 92, "2 hold BOOT on the Orca", 36, 1, fg, bg,
                                     s_line_c, sizeof s_line_c);
        lcd_text_draw_padded_changed(8, 108, "3 tap Orca RESET (keep BOOT)", 36, 1, fg, bg,
                                     s_line_d, sizeof s_line_d);
        lcd_text_draw_padded_changed(8, 124, "4 OG GREEN, BOOT still held", 36, 1, fg, bg,
                                     s_line_e, sizeof s_line_e);
        lcd_text_draw_padded_changed(8, 210, "YELLOW cancels", 36, 1, dim, bg,
                                     s_line_hint, sizeof s_line_hint);
        leds_state();
        return;
    }
    if (fl == AM_FLASH_SYNC || fl == AM_FLASH_WRITE) {
        lcd_text_draw_padded_changed(8, 56,
                                     fl == AM_FLASH_SYNC ? "syncing ROM..." : "writing flash",
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
        leds_state();
        return;
    }
    if (fl == AM_FLASH_OK) {
        lcd_text_draw_padded_changed(8, 56, "C6 flash ok", 36, 1, acc, bg,
                                     s_line_a, sizeof s_line_a);
        lcd_text_draw_padded_changed(8, 76, "LET GO of BOOT first", 36, 1, yel, bg,
                                     s_line_b, sizeof s_line_b);
        lcd_text_draw_padded_changed(8, 96, "then tap Orca RESET", 36, 1, fg, bg,
                                     s_line_c, sizeof s_line_c);
        lcd_text_draw_padded_changed(8, 210, "YELLOW flash again", 36, 1, dim, bg,
                                     s_line_e, sizeof s_line_e);
        leds_state();
        return;
    }
    if (fl == AM_FLASH_FAIL) {
        lcd_text_draw_padded_changed(8, 56, "C6 flash failed", 36, 1, rec, bg,
                                     s_line_a, sizeof s_line_a);
        lcd_text_draw_padded_changed(8, 76, s_st.why[0] ? s_st.why : "see DIAG", 36, 1, fg, bg,
                                     s_line_b, sizeof s_line_b);
        lcd_text_draw_padded_changed(8, 210, "YELLOW retry", 36, 1, dim, bg,
                                     s_line_e, sizeof s_line_e);
        leds_state();
        return;
    }

    if (!s_st_ok || !s_st.hello) {
        lcd_text_draw_padded_changed(8, 56, "waiting for Bottlenose", 36, 1, dim, bg,
                                     s_line_a, sizeof s_line_a);
        lcd_text_draw_padded_changed(8, 76, "YELLOW flashes 115200 image", 36, 1, yel, bg,
                                     s_line_b, sizeof s_line_b);
        lcd_text_draw_padded_changed(8, 112,
                                     s_st.why[0] ? s_st.why : "rxb=0",
                                     36, 1, acc, bg, s_line_c, sizeof s_line_c);
        lcd_text_draw_padded_changed(8, 210, "GREEN starts once hello", 36, 1, dim, bg,
                                     s_line_e, sizeof s_line_e);
        leds_state();
        return;
    }

    const am_status_t *ui = (s_frozen && s_st.wp_on) ? &s_hold : &s_st;

    snprintf(line, sizeof line, "ch%2u %s  %s  tx=%lu",
             (unsigned)ui->channel,
             ui->sweep ? "SWEEP" : (ui->wp_on ? "ON" : "OFF"),
             s_frozen ? "HOLD" : (ui->armed ? "ARMED" : "safe"),
             (unsigned long)ui->tx_count);
    lcd_text_draw_padded_changed(8, 48, line, 40, 1,
                                 s_frozen ? yel : (ui->sweep || ui->wp_on ? acc : yel), bg,
                                 s_line_a, sizeof s_line_a);

    snprintf(line, sizeof line, "%s  %u APs",
             mode_name(ui->mode), (unsigned)ui->n);
    lcd_text_draw_padded_changed(8, 64, line, 40, 1, fg, bg,
                                 s_line_b, sizeof s_line_b);

    {
        const char *ret = ui->last_ret[0] ? ui->last_ret : "-";
        const uint16_t rc = (ui->last_ret[0] && strcmp(ui->last_ret, "ESP_OK") != 0)
                            ? rec : (ui->last_ret[0] ? acc : dim);
        lcd_text_draw_padded_changed(8, 80, ret, 40, 1, rc, bg,
                                     s_line_ret, sizeof s_line_ret);
    }

    if (ui->log[0]) {
        lcd_text_draw_padded_changed(8, 96, ui->log, 40, 1, dim, bg,
                                     s_line_c, sizeof s_line_c);
    } else {
        lcd_text_draw_padded_changed(8, 96, "scanning for beacons...", 40, 1, dim, bg,
                                     s_line_c, sizeof s_line_c);
    }

    unsigned shown = ui->n < AM_ROWS ? ui->n : AM_ROWS;
    unsigned start = 0;
    if (ui->n > AM_ROWS && ui->cursor >= AM_ROWS) {
        start = (unsigned)ui->cursor - (AM_ROWS - 1u);
    }
    for (unsigned i = 0; i < AM_ROWS; i++) {
        const uint16_t y = (uint16_t)(114u + i * 16u);
        if (i < shown) {
            const unsigned idx = start + i;
            const am_target_t *t = &ui->tgt[idx];
            const bool sel = (idx == ui->cursor);
            snprintf(line, sizeof line, "%c%2u %s  %s",
                     sel ? '>' : ' ',
                     (unsigned)t->ch,
                     t->bssid,
                     t->ssid[0] ? t->ssid : "-");
            lcd_text_draw_padded_changed(8, y, line, 40, 1,
                                         sel ? yel : fg, bg,
                                         s_line_tgt[i], sizeof s_line_tgt[i]);
        } else if (s_line_tgt[i][0] != '\0') {
            lcd_text_draw_padded(8, y, "", 40, 1, fg, bg);
            s_line_tgt[i][0] = '\0';
        }
    }

    if (s_frozen) {
        lcd_text_draw_padded_changed(8, 210,
                                     "YEL live  hold=C6  GRN stop",
                                     40, 1, yel, bg, s_line_hint, sizeof s_line_hint);
    } else if (ui->sweep) {
        lcd_text_draw_padded_changed(8, 210,
                                     "YEL FRZ  GRN tap=stop  BLUE G/R",
                                     40, 1, acc, bg, s_line_hint, sizeof s_line_hint);
    } else if (ui->wp_on) {
        lcd_text_draw_padded_changed(8, 210,
                                     "YEL FRZ  GRN swp  BLUE arm G/R scroll",
                                     40, 1, dim, bg, s_line_hint, sizeof s_line_hint);
    } else {
        lcd_text_draw_padded_changed(8, 210,
                                     "YEL C6/ch  GRN hold=sweep  BLUE mode",
                                     40, 1, dim, bg, s_line_hint, sizeof s_line_hint);
    }
    leds_state();
}

int main(void) {
    board_init();
    fwog_splash_bind("AirMaraud", "010");
    s_leds = (ws2812_init(pio0, 0u) == 0);
    lcd_bringup();
    s_link = fwog_link_uart_init(FWOG_LINK_BAUD);
    fwog_link_rx_init(&s_rx);

    uint32_t hold_y = 0;
    uint8_t y_fired = 0;
    uint32_t hold_g = 0;
    uint8_t g_fired = 0;

    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        const fwog_power_t p = fwog_power_poll(now);
        uint8_t b;
        size_t n;
        while (fwog_link_uart_read(&b)) {
            if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
            if (fwog_ioexp_link_handle(s_rx.buf, n)) continue;
            if (n >= sizeof(am_status_t) && s_rx.buf[0] == AM_MSG_ST) {
                memcpy(&s_st, s_rx.buf, sizeof s_st);
                s_st_ok = true;
            }
        }
        const uint8_t fl = s_st_ok ? s_st.flash : 0u;
        if (s_frozen && (s_st.sweep || !s_st.wp_on || !s_st.hello || fl != AM_FLASH_IDLE)) {
            s_frozen = false;
        }

        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) {
            hold_y = now;
            y_fired = 0;
            if (fl == AM_FLASH_HOLD || fl == AM_FLASH_FAIL || fl == AM_FLASH_OK) {
                send_cmd(AM_CMD_FLASH, 0u);
                y_fired = 1;
            } else if (fl != AM_FLASH_SYNC && fl != AM_FLASH_WRITE &&
                       (!s_st_ok || !s_st.hello)) {
                send_cmd(AM_CMD_FLASH, 1u);
                y_fired = 1;
            }
        }
        if (!y_fired && s_st_ok && s_st.hello &&
            (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) &&
            fl == AM_FLASH_IDLE && now - hold_y >= 700u) {
            y_fired = 1;
            s_frozen = false;
            send_cmd(AM_CMD_FLASH, 1u);
        }
        if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) && !y_fired &&
            fl == AM_FLASH_IDLE && s_st_ok && s_st.hello) {
            if (s_st.wp_on) {
                s_frozen = !s_frozen;
                if (s_frozen) s_hold = s_st;
            } else if (!s_frozen) {
                send_cmd(AM_CMD_CHANNEL, 0u);
            }
        }

        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GREEN)) {
            hold_g = now;
            g_fired = 0;
            if (fl == AM_FLASH_HOLD) {
                send_cmd(AM_CMD_FLASH, 2u);
                g_fired = 1;
            }
        }
        if (!g_fired && s_st_ok && s_st.hello &&
            (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GREEN)) &&
            fl == AM_FLASH_IDLE && now - hold_g >= 700u) {
            g_fired = 1;
            s_frozen = false;
            send_cmd(AM_CMD_SWEEP, 1u);
        }
        if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_GREEN)) && !g_fired &&
            fl != AM_FLASH_SYNC && fl != AM_FLASH_WRITE &&
            s_st_ok && s_st.hello) {
            if (s_st.sweep) {
                send_cmd(AM_CMD_SWEEP, 0u);
            } else {
                send_cmd(AM_CMD_START, s_st.wp_on ? 0u : 1u);
            }
        }

        if (fl == AM_FLASH_IDLE && s_st_ok && s_st.hello && !s_frozen) {
            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GRAY)) {
                send_cmd(AM_CMD_CURSOR, 0u);
            }
            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_RED)) {
                send_cmd(AM_CMD_CURSOR, 1u);
            }
            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_BLUE)) {
                if (s_st.wp_on) {
                    send_cmd(AM_CMD_ARM, 1u);
                } else {
                    send_cmd(AM_CMD_MODE, (uint8_t)(s_st.mode ? 0u : 1u));
                }
            }
        }

        paint();
        sleep_ms(20);
    }
}
