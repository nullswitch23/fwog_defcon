/* Front panel for sub-GHz ASK/OOK capture, unused-code queue, and PREDICT mode.
 *
 * PREDICT learns successive rolling codes and synthesizes the next waveform.
 * QUEUE replays stored unused bursts. Neither mode jams the receiver.
 */
#include "fwog_display.h"
#include "ed_link.h"
#include "ed_fob.h"
#include "fob_proto.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>



#define FOB_HOLD_MS 800u

typedef struct {
    uint32_t    hz;
    const char *label;
} fob_preset_t;

static const fob_preset_t k_presets[] = {
    { 313850000u, "313.85 Honda NA" },
    { 315000000u, "315.00 NA RKE" },
    { 433920000u, "433.92 EU RKE" },
    { 868350000u, "868.35 EU" },
    { 915000000u, "915.00 ISM" },
};
#define FOB_NPRESET ((int)(sizeof k_presets / sizeof k_presets[0]))

static fwog_link_rx_t s_rx;
static bool           s_link;
static bool           s_lcd;
static int            s_preset;
static uint8_t        s_radio;
static uint8_t        s_mode;
static uint8_t        s_predictor;
static uint8_t        s_want_cmd = FOB_CMD_LISTEN;
static fob_status_t   s_st;
static bool           s_st_valid;
static fob_status_t   s_shown;
static bool           s_shown_valid;
static bool           s_painted;
static bool           s_chrome_dirty = true;
static bool           s_status_dirty = true;
static bool           s_rssi_dirty = true;
static bool           s_env_dirty = true;
static uint32_t       s_rssi_drawn_ms;
static uint32_t       s_hold_since[FWOG_BTN_COUNT];
static uint8_t        s_hold_fired;
static bool           s_t9;
static char           s_t9_buf[FOB_STEM_N];
static int            s_t9_g;
static int            s_t9_li;
static char           s_line_t9[24];
static char           s_line_grp[8];
static char           s_line_ch[4];
static char           s_named[16];

static fwog_ant_t ant_for_hz(uint32_t hz) {
    if (hz < 360000000u) return FWOG_ANT_200MHZ;
    if (hz < 600000000u) return FWOG_ANT_400MHZ;
    return FWOG_ANT_900MHZ;
}

static void apply_antennas(void) {
    const fwog_ant_t a = ant_for_hz(k_presets[s_preset].hz);
    (void)fwog_ioexp_link_set_antennas(a, a);
}

static const char *mode_title(void) {
    switch (s_mode) {
    case FOB_MODE_QUEUE:   return "FOB QUEUE";
    case FOB_MODE_PREDICT: return "FOB PREDICT";
    default:               return "FOB SINGLE";
    }
}

static void send_cmd_named(uint8_t cmd, const char *stem) {
    fob_cmd_t m;
    memset(&m, 0, sizeof m);
    m.type = FOB_MSG_CMD;
    m.cmd = cmd;
    m.radio = s_radio;
    m.mode = s_mode;
    m.freq_hz = k_presets[s_preset].hz;
    m.predictor = s_predictor;
    if (stem && stem[0]) strncpy(m.name, stem, FOB_STEM_N - 1u);
    if (ed_link_ok()) (void)ed_link_send(&m, sizeof m);
}

static void send_cmd(uint8_t cmd) {
    send_cmd_named(cmd, NULL);
}

static void t9_begin(void) {
    memset(s_t9_buf, 0, sizeof s_t9_buf);
    s_t9_g = 0;
    s_t9_li = 0;
    s_t9 = true;
    s_line_t9[0] = s_line_grp[0] = s_line_ch[0] = '\0';
    s_painted = false;
    s_chrome_dirty = true;
}

static void t9_finish(void) {
    s_t9 = false;
    if (s_t9_buf[0]) {
        send_cmd_named(FOB_CMD_SAVE, s_t9_buf);
        snprintf(s_named, sizeof s_named, "%s.BIN", s_t9_buf);
        s_want_cmd = FOB_CMD_LISTEN;
    }
    s_painted = false;
    s_chrome_dirty = true;
    s_status_dirty = true;
}

void ed_fob_frame(const uint8_t *buf, size_t n) {
        if (n >= sizeof(fob_status_t) && buf[0] == FOB_MSG_STATUS) {
            fob_status_t in;
            memcpy(&in, buf, sizeof in);
            const bool first = !s_st_valid;
            const bool layout = first ||
                in.state != s_st.state || in.radio != s_st.radio ||
                in.ok != s_st.ok || in.edges != s_st.edges ||
                in.freq_hz != s_st.freq_hz || in.mode != s_st.mode ||
                in.slots != s_st.slots || in.unused != s_st.unused ||
                in.next != s_st.next ||
                in.last_total_us != s_st.last_total_us ||
                in.predictor != s_st.predictor ||
                in.pred_key_ok != s_st.pred_key_ok ||
                in.pred_counter != s_st.pred_counter ||
                in.pred_frame != s_st.pred_frame;
            const bool env = first ||
                memcmp(in.env, s_st.env, FOB_ENV_BINS) != 0;
            const bool rssi = first || in.rssi_dbm != s_st.rssi_dbm;
            s_st = in;
            s_st_valid = true;
            if (layout) s_status_dirty = true;
            if (env) s_env_dirty = true;
            if (rssi) s_rssi_dirty = true;
        }
}

static const char *state_text(uint8_t st) {
    switch (st) {
    case FOB_ST_LISTEN:  return "listen";
    case FOB_ST_WAIT:    return "waiting for burst";
    case FOB_ST_CAPTURE: return "capturing";
    case FOB_ST_HAVE:    return "ready";
    case FOB_ST_REPLAY:  return "replaying";
    case FOB_ST_BURST:   return "queue burst";
    case FOB_ST_PREDICT: return "predict TX";
    case FOB_ST_ERROR:   return "radio error";
    default:             return "boot";
    }
}

static void leds_status(void) {
    const uint8_t st = s_st_valid ? s_st.state : FOB_ST_BOOT;
    const uint8_t count = s_st_valid
        ? (s_mode == FOB_MODE_PREDICT ? s_st.slots : s_st.unused)
        : 0u;
    for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++) {
        uint8_t r = 0, g = 0, b = 0;
        if (st == FOB_ST_WAIT || st == FOB_ST_CAPTURE) r = 40u;
        else if (st == FOB_ST_REPLAY || st == FOB_ST_BURST || st == FOB_ST_PREDICT) {
            r = 40u; g = 24u;
        } else if (st == FOB_ST_ERROR) { r = 40u; b = 8u; }
        else if (i < count) g = 32u;
        else g = 4u;
        ws2812_set_color(i, r, g, b);
    }
    ws2812_process();
}

static void draw_env(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    const uint16_t bg = st7789_rgb565(12, 16, 28);
    const uint16_t fg = st7789_rgb565(80, 200, 120);
    st7789_fill_rect(x, y, w, h, bg);
    if (!s_st_valid) return;
    const unsigned bin_w = (unsigned)w / FOB_ENV_BINS;
    if (bin_w == 0u) return;
    for (unsigned i = 0; i < FOB_ENV_BINS; i++) {
        unsigned bh = s_st.env[i];
        if (bh > h - 2u) bh = h - 2u;
        if (bh == 0u) continue;
        st7789_fill_rect((uint16_t)(x + i * bin_w),
                         (uint16_t)(y + h - 1u - bh),
                         (uint16_t)bin_w,
                         (uint16_t)bh, fg);
    }
}

static void draw_t9(uint16_t bg, uint16_t acc, uint16_t fg, uint16_t dim) {
    char line[24];
    char ch[2];
    lcd_text_draw_padded(8, 44, "name  8.3 /fobreplay", 32, 1, dim, bg);
    snprintf(line, sizeof line, "[%s]", s_t9_buf);
    lcd_text_draw_padded_changed(8, 56, line, 12, 2, fg, bg,
                                 s_line_t9, sizeof s_line_t9);
    lcd_text_draw_padded(8, 92, "grp", 8, 1, dim, bg);
    lcd_text_draw_padded_changed(8, 104, fwog_t9_group[s_t9_g], 5, 4, acc, bg,
                                 s_line_grp, sizeof s_line_grp);
    lcd_text_draw_padded(180, 92, "char", 8, 1, dim, bg);
    ch[0] = fwog_t9_cur(s_t9_g, s_t9_li);
    ch[1] = '\0';
    lcd_text_draw_padded_changed(180, 104, ch, 2, 4, fg, bg,
                                 s_line_ch, sizeof s_line_ch);
    lcd_text_draw_padded(8, 176, "GRY/RED grp  YEL/BLU char", 40, 1, dim, bg);
    lcd_text_draw_padded(8, 192, "GRN tap add  hold save", 40, 1, dim, bg);
    lcd_text_draw_padded(8, 208, "YEL hold backspace  empty=FOB", 40, 1, dim, bg);
}

static void draw(uint32_t now_ms) {
    if (!s_lcd) return;
    const uint16_t bg = st7789_rgb565(8, 10, 18);
    const uint16_t fg = st7789_rgb565(230, 230, 230);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    const uint16_t warn = st7789_rgb565(230, 180, 70);
    const uint16_t hdr = st7789_rgb565(16, 28, 40);
    char line[44];

    if (!s_painted) {
        st7789_clear(bg);
        s_painted = true;
        s_chrome_dirty = s_status_dirty = s_rssi_dirty = s_env_dirty = true;
    }

    if (s_t9) {
        if (s_chrome_dirty) {
            st7789_fill_rect(0, 0, ST7789_W, 36, hdr);
            lcd_text_draw_padded(8, 8, "FOB NAME", 18, 2, acc, hdr);
            s_chrome_dirty = false;
        }
        draw_t9(bg, acc, fg, dim);
        return;
    }

    if (s_chrome_dirty) {
        st7789_fill_rect(0, 0, ST7789_W, 36, hdr);
        lcd_text_draw_padded(8, 8, mode_title(), 18, 2, acc, hdr);
        snprintf(line, sizeof line, "%s", k_presets[s_preset].label);
        lcd_text_draw_padded(8, 44, line, 26, 2, fg, bg);
        if (s_mode == FOB_MODE_PREDICT) {
            lcd_text_draw_padded(8, 150, "GRAY/RED freq  BLUE mode", 40, 1, dim, bg);
            lcd_text_draw_padded(8, 164, "GREEN capture  hold=pred", 40, 1, dim, bg);
            lcd_text_draw_padded(8, 178, "YELLOW synth   hold=x3", 40, 1, dim, bg);
            lcd_text_draw_padded(8, 192, "BLUE hold radio  GRAY hold T9", 40, 1, dim, bg);
            lcd_text_draw_padded(8, 212, "Synthesizes next code, never replay.", 40, 1, warn, bg);
            lcd_text_draw_padded(8, 226, "Capture 2+ presses out of range.", 40, 1, warn, bg);
        } else {
            lcd_text_draw_padded(8, 150, "GRAY/RED freq     BLUE mode", 40, 1, dim, bg);
            lcd_text_draw_padded(8, 164, "GREEN capture     hold=clear", 40, 1, dim, bg);
            lcd_text_draw_padded(8, 178, "YELLOW replay     hold=burst", 40, 1, dim, bg);
            lcd_text_draw_padded(8, 192, "BLUE hold radio  GRAY hold T9", 40, 1, dim, bg);
            lcd_text_draw_padded(8, 212, "Queue replays unused codes only.", 40, 1, warn, bg);
            lcd_text_draw_padded(8, 226, "Heard rolling codes stay dead.", 40, 1, warn, bg);
        }
        s_chrome_dirty = false;
    }

    if (s_status_dirty) {
        snprintf(line, sizeof line, "radio %u  %s",
                 (unsigned)s_radio, ed_link_ok() ? "link ok" : "no link");
        lcd_text_draw_padded(8, 68, line, 40, 1, dim, bg);
        if (s_st_valid) {
            if (s_mode == FOB_MODE_PREDICT) {
                const char *pred = s_st.predictor == FOB_PRED_KL ? "KL" : "CTR";
                snprintf(line, sizeof line,
                         "%s  cap %u/%u  %s key %s",
                         state_text(s_st.state),
                         (unsigned)s_st.slots, (unsigned)FOB_SLOTS,
                         pred, s_st.pred_key_ok ? "Y" : "N");
                lcd_text_draw_padded(8, 96, line, 40, 1, acc, bg);
                snprintf(line, sizeof line, "ctr 0x%04X  frame 0x%08lX",
                         (unsigned)s_st.pred_counter,
                         (unsigned long)s_st.pred_frame);
                lcd_text_draw_padded(8, 110, line, 40, 1, fg, bg);
            } else {
                snprintf(line, sizeof line, "%s   slots %u/%u unused %u",
                         state_text(s_st.state),
                         (unsigned)s_st.slots, (unsigned)FOB_SLOTS,
                         (unsigned)s_st.unused);
                lcd_text_draw_padded(8, 96, line, 40, 1, acc, bg);
                lcd_text_draw_padded(8, 110, s_named[0] ? s_named : "", 40, 1,
                                     s_named[0] ? dim : bg, bg);
            }
        }
        s_status_dirty = false;
    }

    if (s_rssi_dirty) {
        const bool rssi_only = s_shown_valid && s_st_valid &&
            s_st.last_total_us == s_shown.last_total_us &&
            s_st.edges == s_shown.edges &&
            s_st.state == s_shown.state;
        if (!(rssi_only && (now_ms - s_rssi_drawn_ms) < 200u)) {
            if (s_st_valid) {
                snprintf(line, sizeof line, "RSSI %+d dBm  %u us  %u edges",
                         (int)s_st.rssi_dbm, (unsigned)s_st.last_total_us,
                         (unsigned)s_st.edges);
                lcd_text_draw_padded(8, 82, line, 40, 1, fg, bg);
                s_shown = s_st;
                s_shown_valid = true;
            } else {
                lcd_text_draw_padded(8, 82, "waiting for main CPU...", 40, 1, dim, bg);
            }
            s_rssi_drawn_ms = now_ms;
            s_rssi_dirty = false;
        }
    }

    if (s_env_dirty) {
        const uint16_t y = (s_mode == FOB_MODE_PREDICT) ? 124u : 114u;
        draw_env(8, y, 304, 28);
        s_env_dirty = false;
    }
}



static bool hold_edge(uint8_t id, fwog_buttons_t btn, uint32_t now) {
    const uint8_t bit = (uint8_t)FWOG_BTN_BIT(id);
    if (btn.pressed & bit) {
        s_hold_since[id] = now;
        s_hold_fired &= (uint8_t)~bit;
    }
    if ((btn.down & bit) && !(s_hold_fired & bit) &&
        (now - s_hold_since[id]) >= FOB_HOLD_MS) {
        s_hold_fired |= bit;
        return true;
    }
    return false;
}

static bool tap_edge(uint8_t id, fwog_buttons_t btn) {
    const uint8_t bit = (uint8_t)FWOG_BTN_BIT(id);
    return (btn.released & bit) && !(s_hold_fired & bit);
}



static absolute_time_t s_fob_next_cmd;

void ed_fob_enter(void) {
    s_lcd = true;
    s_painted = false;
    s_chrome_dirty = s_status_dirty = s_rssi_dirty = s_env_dirty = true;
    s_t9 = false;
    s_radio = 0;
    apply_antennas();
    s_want_cmd = FOB_CMD_LISTEN;
    send_cmd(FOB_CMD_LISTEN);
    s_fob_next_cmd = make_timeout_time_ms(250);
}

void ed_fob_paint(void) {
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (time_reached(s_fob_next_cmd)) {
        s_fob_next_cmd = make_timeout_time_ms(250);
        if (s_want_cmd == FOB_CMD_LISTEN) send_cmd(FOB_CMD_LISTEN);
    }
    draw(now);
}

void ed_fob_buttons(const fwog_power_t *p, uint32_t now,
                    bool y_long, bool g_long, bool gray_long, bool red_tap) {
    (void)hold_edge(FWOG_BTN_BLUE, p->buttons, now);
    (void)hold_edge(FWOG_BTN_YELLOW, p->buttons, now);
    (void)hold_edge(FWOG_BTN_GREEN, p->buttons, now);
    (void)hold_edge(FWOG_BTN_GRAY, p->buttons, now);
    (void)hold_edge(FWOG_BTN_RED, p->buttons, now);

    if (s_t9) {
        if (p->buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_RED))
            fwog_t9_group_next(&s_t9_g, &s_t9_li);
        if (tap_edge(FWOG_BTN_GRAY, p->buttons) && !gray_long)
            fwog_t9_group_prev(&s_t9_g, &s_t9_li);
        if (tap_edge(FWOG_BTN_BLUE, p->buttons))
            fwog_t9_letter_next(s_t9_g, &s_t9_li);
        if (hold_edge(FWOG_BTN_BLUE, p->buttons, now))
            t9_finish();
        else if (tap_edge(FWOG_BTN_GREEN, p->buttons) && !g_long) {
            fwog_t9_insert(s_t9_buf, sizeof s_t9_buf, fwog_t9_cur(s_t9_g, s_t9_li));
        }
        if (tap_edge(FWOG_BTN_YELLOW, p->buttons) && !y_long)
            fwog_t9_letter_prev(s_t9_g, &s_t9_li);
        return;
    }

    const bool bdown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) != 0;
    if (bdown) {
        /* shift: former GRY hold T9 / GRN hold clear / YEL hold burst */
        if (tap_edge(FWOG_BTN_GRAY, p->buttons) && !gray_long) {
            t9_begin();
            return;
        }
        if (tap_edge(FWOG_BTN_GREEN, p->buttons) && !g_long) {
            s_want_cmd = FOB_CMD_LISTEN;
            if (s_mode == FOB_MODE_PREDICT) {
                s_predictor = (s_predictor == FOB_PRED_CTR) ? FOB_PRED_KL : FOB_PRED_CTR;
                send_cmd(FOB_CMD_LISTEN);
                s_chrome_dirty = true;
            } else {
                send_cmd(FOB_CMD_CLEAR);
            }
            s_status_dirty = true;
            return;
        }
        if (tap_edge(FWOG_BTN_YELLOW, p->buttons) && !y_long) {
            s_want_cmd = FOB_CMD_LISTEN;
            if (s_mode == FOB_MODE_PREDICT) send_cmd(FOB_CMD_PREDICT_BURST);
            else send_cmd(FOB_CMD_BURST);
            s_status_dirty = true;
            return;
        }
        if (hold_edge(FWOG_BTN_BLUE, p->buttons, now)) {
            /* radio CS1 dropped: combo keeps one live CC1101 (CS0). */
            s_mode = (uint8_t)((s_mode + 1u) % 3u);
            s_want_cmd = FOB_CMD_LISTEN;
            send_cmd(FOB_CMD_LISTEN);
            s_chrome_dirty = s_status_dirty = true;
        }
        return;
    }
    if (tap_edge(FWOG_BTN_GRAY, p->buttons) && !gray_long) {
        if (--s_preset < 0) s_preset = FOB_NPRESET - 1;
        apply_antennas();
        s_want_cmd = FOB_CMD_LISTEN;
        send_cmd(FOB_CMD_LISTEN);
        s_chrome_dirty = true;
    }
    if (red_tap && tap_edge(FWOG_BTN_RED, p->buttons)) {
        if (++s_preset >= FOB_NPRESET) s_preset = 0;
        apply_antennas();
        s_want_cmd = FOB_CMD_LISTEN;
        send_cmd(FOB_CMD_LISTEN);
        s_chrome_dirty = true;
    }
    if (tap_edge(FWOG_BTN_BLUE, p->buttons)) {
        s_mode = (uint8_t)((s_mode + 1u) % 3u);
        s_want_cmd = FOB_CMD_LISTEN;
        send_cmd(FOB_CMD_LISTEN);
        s_chrome_dirty = s_status_dirty = true;
    }
    if (tap_edge(FWOG_BTN_GREEN, p->buttons) && !g_long) {
        const bool capturing = s_st_valid &&
            (s_st.state == FOB_ST_WAIT || s_st.state == FOB_ST_CAPTURE);
        send_cmd(capturing ? FOB_CMD_ABORT : FOB_CMD_CAPTURE);
        s_want_cmd = FOB_CMD_LISTEN;
        s_status_dirty = true;
    }
    if (tap_edge(FWOG_BTN_YELLOW, p->buttons) && !y_long) {
        s_want_cmd = FOB_CMD_LISTEN;
        if (s_mode == FOB_MODE_PREDICT) send_cmd(FOB_CMD_PREDICT_TX);
        else send_cmd(FOB_CMD_REPLAY);
        s_status_dirty = true;
    }
}
