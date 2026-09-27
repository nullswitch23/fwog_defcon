/* OrcaLobby AirMaraud tile. */
#include "ol_am.h"
#include "ol_link.h"
#include "am_proto.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

static bool           s_chrome_dirty = true;
static am_status_t    s_st;
static bool           s_st_ok;
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
static uint32_t       s_hold_b, s_hold_r;
static bool           s_blu_was, s_blu_hold, s_red_was, s_red_hold;

static void send_cmd(uint8_t cmd, uint8_t on) {
    am_cmd_t m = { .type = AM_MSG_CMD, .cmd = cmd, .on = on };
    if (ol_link_ok()) ol_link_send(&m, sizeof m);
}

static void leds_state(void) {
    if (!ol_leds_ok()) return;
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

void ol_am_paint(void) {
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
        lcd_text_draw_padded_changed(8, 76, "BLUE hold flashes C6 image", 36, 1, yel, bg,
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
                                     "YEL live  hold BLU C6  GRN stop",
                                     40, 1, yel, bg, s_line_hint, sizeof s_line_hint);
    } else if (ui->sweep) {
        lcd_text_draw_padded_changed(8, 210,
                                     "YEL FRZ  GRN tap=stop  BLUE G/R",
                                     40, 1, acc, bg, s_line_hint, sizeof s_line_hint);
    } else if (ui->wp_on) {
        lcd_text_draw_padded_changed(8, 210,
                                     "YEL FRZ  GRN swp  BLU arm  RED hold sweep",
                                     40, 1, dim, bg, s_line_hint, sizeof s_line_hint);
    } else {
        lcd_text_draw_padded_changed(8, 210,
                                     "YEL ch  hold BLU C6  GRN start  RED sweep",
                                     40, 1, dim, bg, s_line_hint, sizeof s_line_hint);
    }
    leds_state();
}

void ol_am_enter(void) {
    s_st_ok = false;
    memset(&s_st, 0, sizeof s_st);
    s_frozen = false;
    s_view = 0xFF;
    s_chrome_dirty = true;
    s_blu_was = s_blu_hold = s_red_was = s_red_hold = false;
    slots_reset();
}

void ol_am_frame(const uint8_t *buf, size_t n) {
    if (n >= sizeof(am_status_t) && buf[0] == AM_MSG_ST) {
        memcpy(&s_st, buf, sizeof s_st);
        s_st_ok = true;
    }
}

void ol_am_buttons(const fwog_power_t *p, bool y_long, bool g_long,
                   bool gray_long, bool red_tap) {
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    const uint8_t fl = s_st_ok ? s_st.flash : 0u;
    const bool bdown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) != 0;
    const bool rdown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_RED)) != 0;
    (void)gray_long;

    if (s_frozen && (s_st.sweep || !s_st.wp_on || !s_st.hello || fl != AM_FLASH_IDLE))
        s_frozen = false;

    if ((p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) && !y_long) {
        if (fl == AM_FLASH_HOLD || fl == AM_FLASH_FAIL || fl == AM_FLASH_OK)
            send_cmd(AM_CMD_FLASH, 0u);
        else if (fl == AM_FLASH_IDLE && s_st_ok && s_st.hello) {
            if (s_st.wp_on) {
                s_frozen = !s_frozen;
                if (s_frozen) s_hold = s_st;
            } else if (!s_frozen)
                send_cmd(AM_CMD_CHANNEL, 0u);
        }
    }

    if ((p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_GREEN)) && !g_long) {
        if (fl == AM_FLASH_HOLD)
            send_cmd(AM_CMD_FLASH, 2u);
        else if (fl != AM_FLASH_SYNC && fl != AM_FLASH_WRITE &&
                 s_st_ok && s_st.hello) {
            if (s_st.sweep)
                send_cmd(AM_CMD_SWEEP, 0u);
            else
                send_cmd(AM_CMD_START, s_st.wp_on ? 0u : 1u);
        }
    }

    if (bdown && !s_blu_was) {
        s_hold_b = now;
        s_blu_hold = false;
    }
    if (bdown && !s_blu_hold && (now - s_hold_b) >= 700u) {
        s_blu_hold = true;
        if (fl != AM_FLASH_SYNC && fl != AM_FLASH_WRITE)
            send_cmd(AM_CMD_FLASH, 1u);
    }
    if (!bdown && s_blu_was && !s_blu_hold &&
        fl == AM_FLASH_IDLE && s_st_ok && s_st.hello && !s_frozen) {
        if (s_st.wp_on)
            send_cmd(AM_CMD_ARM, 1u);
        else
            send_cmd(AM_CMD_MODE, (uint8_t)(s_st.mode ? 0u : 1u));
    }
    s_blu_was = bdown;

    if (rdown && !s_red_was) {
        s_hold_r = now;
        s_red_hold = false;
    }
    if (rdown && !s_red_hold && (now - s_hold_r) >= 700u) {
        s_red_hold = true;
        if (s_st_ok && s_st.hello && fl == AM_FLASH_IDLE) {
            s_frozen = false;
            send_cmd(AM_CMD_SWEEP, 1u);
        }
    }
    if (!rdown && s_red_was && !s_red_hold && red_tap &&
        fl == AM_FLASH_IDLE && s_st_ok && s_st.hello && !s_frozen)
        send_cmd(AM_CMD_CURSOR, 1u);
    s_red_was = rdown;

    if (fl == AM_FLASH_IDLE && s_st_ok && s_st.hello && !s_frozen &&
        (p->buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GRAY)))
        send_cmd(AM_CMD_CURSOR, 0u);
}
