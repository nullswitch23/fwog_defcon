/* ChirpMail v007 — 24 canned slots (FatFs + helper), T9 override, ACK PIN. */
#include "fwog_display.h"
#include "cm_proto.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

FWOG_POWER_DEFAULT();

#define CM_HOLD_MS 750u

static bool     s_lcd, s_link, s_leds;
static bool     s_chrome_dirty = true;
static bool     s_t9;
static char     s_compose[CM_TEXT + 1u];
static char     s_inbox[CM_INBOX][CM_TEXT + 1u];
static uint8_t  s_in_n, s_in_i;
static cm_st_t  s_st;
static bool     s_st_ok;
static char     s_t9_buf[CM_TEXT + 1u];
static int      s_t9_g, s_t9_li;
static fwog_link_rx_t s_rx;
static char     s_line_st[44];
static char     s_line_cmp[44];
static int      s_macro = -1;
static uint8_t  s_ntx;
static uint32_t s_led_until;
static uint8_t  s_led_r, s_led_g, s_led_b;
static char     s_slot[CM_NSLOT][CM_TEXT + 1u];
static bool     s_pulled;

static const char *const k_def[8] = {
    "MEET NOC STEPS NOW",
    "HORN HAND IS ME",
    "BADGE SWAP VILLAGE",
    "SHAKA IF YOU COPY",
    "WHITEHAT WAVE 2FL",
    "FOX BY THE FLAG",
    "ACK PIN CODE",
    "SAME UF2 FIND ME",
};

static void slots_default(void) {
    unsigned i;
    memset(s_slot, 0, sizeof s_slot);
    for (i = 0; i < 8u; i++) {
        strncpy(s_slot[i], k_def[i], CM_TEXT);
        s_slot[i][CM_TEXT] = '\0';
    }
}

static uint32_t cm_rand(void) {
    static uint32_t s;
    const uint32_t t = to_ms_since_boot(get_absolute_time());
    s ^= t + 0x9E3779B9u;
    s = s * 1664525u + 1013904223u;
    return s;
}

static void load_macro(int i) {
    s_macro = i;
    if (i < 0 || i >= (int)CM_NSLOT) return;
    if (strcmp(s_slot[i], "ACK PIN CODE") == 0) {
        snprintf(s_compose, sizeof s_compose, "ACK PIN %04u",
                 (unsigned)(cm_rand() % 10000u));
    } else {
        strncpy(s_compose, s_slot[i], CM_TEXT);
        s_compose[CM_TEXT] = '\0';
    }
    s_chrome_dirty = true;
}

static void send_cmd(uint8_t cmd, uint8_t slot, const char *text) {
    cm_cmd_t m;
    memset(&m, 0, sizeof m);
    m.type = CM_MSG_CMD;
    m.cmd = cmd;
    m.seq = slot;
    if (text) {
        m.n = (uint8_t)strlen(text);
        if (m.n > CM_TEXT) m.n = (uint8_t)CM_TEXT;
        memcpy(m.text, text, m.n);
    }
    if (s_link) (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void save_slot(void) {
    if (s_macro < 0 || s_macro >= (int)CM_NSLOT) return;
    strncpy(s_slot[s_macro], s_compose, CM_TEXT);
    s_slot[s_macro][CM_TEXT] = '\0';
    send_cmd(CM_CMD_SET, (uint8_t)s_macro, s_slot[s_macro]);
}

static uint16_t k_bg, k_fg, k_acc, k_dim, k_hdr, k_warn;

static void colors(void) {
    k_bg   = st7789_rgb565(8, 10, 18);
    k_fg   = st7789_rgb565(230, 230, 230);
    k_acc  = st7789_rgb565(80, 200, 140);
    k_dim  = st7789_rgb565(140, 150, 170);
    k_hdr  = st7789_rgb565(16, 28, 40);
    k_warn = st7789_rgb565(230, 180, 70);
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
        board_backlight(255);
        fwog_splash_boot();
        colors();
        st7789_clear(k_bg);
        s_chrome_dirty = true;
    }
}

static void send_tx(uint32_t now) {
    cm_cmd_t m;
    memset(&m, 0, sizeof m);
    m.type = CM_MSG_CMD;
    m.cmd = CM_CMD_TX;
    m.n = (uint8_t)strlen(s_compose);
    memcpy(m.text, s_compose, m.n);
    if (s_link) (void)fwog_link_uart_send_frame(&m, sizeof m);
    if (s_ntx < 255u) s_ntx++;
    s_led_r = 40u;
    s_led_g = 28u;
    s_led_b = 4u;
    s_led_until = now + 400u;
    s_chrome_dirty = true;
}

static void leds_rx(uint32_t now) {
    s_led_r = 4u;
    s_led_g = 36u;
    s_led_b = 8u;
    s_led_until = now + 400u;
}

static void leds_poll(uint32_t now) {
    unsigned i;
    if (!s_leds) return;
    if ((int32_t)(now - s_led_until) >= 0) {
        s_led_r = 2u;
        s_led_g = 4u;
        s_led_b = 6u;
    }
    for (i = 0; i < (unsigned)FWOG_LED_COUNT; i++) {
        ws2812_set_color(i, s_led_r, s_led_g, s_led_b);
    }
    ws2812_process();
}

static void inbox_push(const char *t) {
    memset(s_inbox[s_in_i], 0, sizeof s_inbox[0]);
    strncpy(s_inbox[s_in_i], t ? t : "", CM_TEXT);
    s_in_i = (uint8_t)((s_in_i + 1u) % CM_INBOX);
    if (s_in_n < CM_INBOX) s_in_n++;
    s_chrome_dirty = true;
}

static void paint_t9(void) {
    char ch[2] = { 0, 0 };
    lcd_text_draw_padded(8, 8, "ChirpMail T9", 18, 2, k_acc, k_hdr);
    lcd_text_draw_padded(8, 44, s_t9_buf[0] ? s_t9_buf : "(empty)", 40, 1, k_fg, k_bg);
    lcd_text_draw_padded(8, 104, fwog_t9_group[s_t9_g], 5, 4, k_acc, k_bg);
    ch[0] = fwog_t9_cur(s_t9_g, s_t9_li);
    lcd_text_draw_padded(160, 104, ch, 2, 4, k_fg, k_bg);
    lcd_text_draw_padded(8, 188, "GRN tap insert  hold done", 40, 1, k_dim, k_bg);
    lcd_text_draw_padded(8, 204, "YEL hold bksp   GRY hold cancel", 40, 1, k_dim, k_bg);
}

static void paint(void) {
    char line[48];
    unsigned i;
    if (!s_lcd) return;
    if (!s_chrome_dirty && !s_t9) {
        snprintf(line, sizeof line, "r0 %s  rssi %+d  rx %u  sent %u",
                 s_st_ok && s_st.ok ? "RF" : "--",
                 s_st_ok ? (int)s_st.rssi : 0,
                 s_st_ok ? (unsigned)s_st.nrx : 0u,
                 (unsigned)s_ntx);
        lcd_text_draw_padded_changed(8, 48, line, 40, 1, k_dim, k_bg,
                                     s_line_st, sizeof s_line_st);
        lcd_text_draw_padded_changed(8, 172, s_compose[0] ? s_compose : "(empty — GRY hold T9)",
                                     40, 1, k_fg, k_bg,
                                     s_line_cmp, sizeof s_line_cmp);
        return;
    }
    st7789_fill_rect(0, 0, ST7789_W, 36, k_hdr);
    st7789_fill_rect(0, 36, ST7789_W, ST7789_H - 36, k_bg);
    if (s_t9) {
        paint_t9();
        s_chrome_dirty = false;
        return;
    }
    lcd_text_draw_padded(8, 8, "ChirpMail", 16, 2, k_acc, k_hdr);
    snprintf(line, sizeof line, "r0 %s  rssi %+d  rx %u  sent %u",
             s_st_ok && s_st.ok ? "RF" : "--",
             s_st_ok ? (int)s_st.rssi : 0,
             s_st_ok ? (unsigned)s_st.nrx : 0u,
             (unsigned)s_ntx);
    lcd_text_draw_padded(8, 48, line, 40, 1, k_dim, k_bg);
    lcd_text_draw_padded(8, 68, "inbox", 16, 1, k_warn, k_bg);
    for (i = 0; i < CM_INBOX; i++) {
        const char *row = " ";
        if (s_in_n > i) {
            unsigned idx = (s_in_i + CM_INBOX - s_in_n + i) % CM_INBOX;
            row = s_inbox[idx];
        }
        lcd_text_draw_padded(8, (uint16_t)(84u + i * 16u), row, 40, 1, k_fg, k_bg);
    }
    if (s_macro >= 0) {
        snprintf(line, sizeof line, "out  %d/%u  sent %u",
                 s_macro + 1, (unsigned)CM_NSLOT, (unsigned)s_ntx);
    } else {
        snprintf(line, sizeof line, "out  sent %u", (unsigned)s_ntx);
    }
    lcd_text_draw_padded(8, 156, line, 28, 1, k_warn, k_bg);
    lcd_text_draw_padded(8, 172, s_compose[0] ? s_compose : "(empty — GRY hold T9)",
                         40, 1, k_fg, k_bg);
    lcd_text_draw_padded(8, 204, "YEL/BLU 24  GRN send  GRY T9 saves slot",
                         50, 1, k_dim, k_bg);
    lcd_text_draw_padded(8, 220, "433.92 2-FSK ~10 dBm  ISM", 40, 1, k_warn, k_bg);
    s_line_st[0] = s_line_cmp[0] = '\0';
    s_chrome_dirty = false;
}

static void t9_open(void) {
    s_t9 = true;
    strncpy(s_t9_buf, s_compose, CM_TEXT);
    s_t9_buf[CM_TEXT] = '\0';
    s_t9_g = 0;
    s_t9_li = 0;
    s_chrome_dirty = true;
}

static void t9_close(bool keep) {
    if (keep) {
        strncpy(s_compose, s_t9_buf, CM_TEXT);
        s_compose[CM_TEXT] = '\0';
        save_slot();
    }
    s_t9 = false;
    s_chrome_dirty = true;
}

int main(void) {
    bool ywas = false, gwas = false, bwas = false, rwas = false;
    bool yhold = false, ghold = false, rhold = false;
    uint32_t yms = 0, gms = 0, rms = 0;

    board_init();
    fwog_splash_bind("ChirpMail", "007");
    colors();
    slots_default();
    s_leds = ws2812_init(pio0, 0u);
    lcd_bringup();
    /* UART after splash: a PULL/ack during the dwell blocks on CTS while
     * main is still in its post-update sleep, both FIFOs fill, main's
     * watchdog resets, GUI_NRESET, splash forever. */
    s_link = fwog_link_uart_init(FWOG_LINK_BAUD);
    fwog_link_rx_init(&s_rx);
    (void)fwog_ioexp_link_set_antennas(FWOG_ANT_400MHZ, FWOG_ANT_400MHZ);

    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        const fwog_power_t p = fwog_power_poll(now);
        const bool ydown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) != 0;
        const bool gdown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GREEN)) != 0;
        const bool bdown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) != 0;
        const bool rydown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GRAY)) != 0;
        uint8_t b;
        size_t n;

        while (fwog_link_uart_read(&b)) {
            if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
            if (fwog_ioexp_link_handle(s_rx.buf, n)) continue;
            if (n >= sizeof(cm_st_t) && s_rx.buf[0] == CM_MSG_ST) {
                memcpy(&s_st, s_rx.buf, sizeof s_st);
                s_st_ok = true;
            }
            if (n >= sizeof(cm_slot_t) && s_rx.buf[0] == CM_MSG_SLOT) {
                cm_slot_t sl;
                char got[CM_TEXT + 1u];
                unsigned nn;
                memcpy(&sl, s_rx.buf, sizeof sl);
                if (sl.slot >= CM_NSLOT) continue;
                nn = sl.n > CM_TEXT ? CM_TEXT : sl.n;
                memset(got, 0, sizeof got);
                if (nn) memcpy(got, sl.text, nn);
                if (strcmp(s_slot[sl.slot], got) == 0) continue;
                memcpy(s_slot[sl.slot], got, sizeof got);
                if (s_macro == (int)sl.slot) {
                    strncpy(s_compose, got, CM_TEXT);
                    s_compose[CM_TEXT] = '\0';
                }
            }
            if (n >= sizeof(cm_rx_t) && s_rx.buf[0] == CM_MSG_RX) {
                cm_rx_t m;
                memcpy(&m, s_rx.buf, sizeof m);
                m.text[CM_TEXT - 1u] = '\0';
                inbox_push(m.text);
                leds_rx(now);
            }
        }

        if (!s_pulled && s_st_ok) {
            s_pulled = true;
            send_cmd(CM_CMD_PULL, 0, NULL);
        }

        if (p.armed) {
            leds_poll(now);
            paint();
            sleep_ms(2);
            continue;
        }

        if (s_t9) {
            if (gdown && !gwas) {
                gms = now;
                ghold = false;
            }
            if (gdown && !ghold && (now - gms) >= CM_HOLD_MS) {
                ghold = true;
                t9_close(true);
            }
            if (!gdown && gwas && !ghold) {
                fwog_t9_insert(s_t9_buf, sizeof s_t9_buf,
                               fwog_t9_cur(s_t9_g, s_t9_li));
                s_chrome_dirty = true;
            }
            gwas = gdown;

            if (ydown && !ywas) {
                yms = now;
                yhold = false;
            }
            if (ydown && !yhold && (now - yms) >= CM_HOLD_MS) {
                yhold = true;
                fwog_t9_backspace(s_t9_buf);
                s_chrome_dirty = true;
            }
            if (!ydown && ywas && !yhold) {
                fwog_t9_letter_prev(s_t9_g, &s_t9_li);
                s_chrome_dirty = true;
            }
            ywas = ydown;

            if (!bdown && bwas) {
                fwog_t9_letter_next(s_t9_g, &s_t9_li);
                s_chrome_dirty = true;
            }
            bwas = bdown;

            if (rydown && !rwas) {
                rms = now;
                rhold = false;
            }
            if (rydown && !rhold && (now - rms) >= CM_HOLD_MS) {
                rhold = true;
                t9_close(false);
            }
            if (!rydown && rwas && !rhold) {
                fwog_t9_group_prev(&s_t9_g, &s_t9_li);
                s_chrome_dirty = true;
            }
            rwas = rydown;
            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_RED)) {
                fwog_t9_group_next(&s_t9_g, &s_t9_li);
                s_chrome_dirty = true;
            }
            leds_poll(now);
            paint();
            sleep_ms(2);
            continue;
        }

        if (rydown && !rwas) {
            rms = now;
            rhold = false;
        }
        if (rydown && !rhold && (now - rms) >= CM_HOLD_MS) {
            rhold = true;
            t9_open();
            rwas = rydown;
            leds_poll(now);
            paint();
            sleep_ms(2);
            continue;
        }
        rwas = rydown;

        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GREEN)) {
            if (s_compose[0]) send_tx(now);
        }
        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) {
            load_macro((s_macro <= 0) ? ((int)CM_NSLOT - 1) : (s_macro - 1));
        }
        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_BLUE)) {
            load_macro((s_macro + 1) % (int)CM_NSLOT);
        }

        leds_poll(now);
        paint();
        sleep_ms(2);
    }
}
