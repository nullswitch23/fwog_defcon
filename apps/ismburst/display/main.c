/* Front panel for ISM OOK capture-then-decode, plus owned-gadget replay.
 * Static chrome; RSSI is patched in place so the panel does not strobe. */
#include "fwog_display.h"
#include "ib_proto.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

FWOG_POWER_DEFAULT();

#define IB_HOLD_MS 800u
#define IB_BAR_N   150u
#define IB_BAR_X   8u
#define IB_BAR_W   2u
#define IB_BAR_MS  100u
#define IB_BAR_H   48u
#define IB_BAR_Y   88u
#define IB_HIST_H  28u
#define IB_HIST_Y  140u
#define IB_UI_LIVE 0
#define IB_UI_T9   1

typedef struct {
    uint32_t    hz;
    uint8_t     mod;
    const char *label;
    fwog_ant_t  ant;
} ib_preset_t;

static const ib_preset_t k_presets[] = {
    { 315000000u, IB_MOD_ASK,  "315 ASK",       FWOG_ANT_200MHZ },
    { 433920000u, IB_MOD_ASK,  "433.92 ASK",    FWOG_ANT_400MHZ },
    { 868350000u, IB_MOD_ASK,  "868.35 ASK",    FWOG_ANT_900MHZ },
    { 915000000u, IB_MOD_ASK,  "915 ASK",       FWOG_ANT_900MHZ },
    { 433920000u, IB_MOD_2FSK, "433.92 2FSK",   FWOG_ANT_400MHZ },
    { 868350000u, IB_MOD_2FSK, "868.35 2FSK",   FWOG_ANT_900MHZ },
};
#define IB_NPRESET ((int)(sizeof k_presets / sizeof k_presets[0]))

static fwog_link_rx_t s_rx;
static bool           s_link;
static bool           s_lcd;
static int            s_preset = 1; /* 433.92 */
static uint8_t        s_view;
static ib_status_t    s_st;
static bool           s_st_valid;
static bool           s_painted;
static bool           s_chrome_dirty = true;
static bool           s_status_dirty = true;
static bool           s_rssi_dirty = true;
static bool           s_decode_dirty = true;
static uint32_t       s_rssi_drawn_ms;
static uint32_t       s_hold_since[FWOG_BTN_COUNT];
static uint8_t        s_hold_fired;
static int16_t        s_bars[IB_BAR_N];
static unsigned       s_bar_n, s_bar_i;
static uint32_t       s_bar_ms;
static bool           s_bars_dirty = true;
static char           s_line_st[44];
static char           s_line_park[44];
static char           s_line_rssi[44];
static char           s_line_meta[44];
static char           s_line_t9[24];
static char           s_line_grp[8];
static char           s_line_ch[4];
static char           s_names[3][IB_LABEL_LEN];
static char           s_t9_buf[IB_LABEL_LEN];
static int            s_ui;
static int            s_t9_g, s_t9_li;
static uint32_t       s_yel_ms, s_grn_ms, s_blu_ms, s_gry_ms;
static bool           s_yel_was, s_grn_was, s_blu_was, s_gry_was;
static bool           s_yel_hold, s_grn_hold, s_blu_hold, s_gry_hold;

static void apply_antennas(void) {
    const fwog_ant_t a = k_presets[s_preset].ant;
    (void)fwog_ioexp_link_set_antennas(a, a);
}

static const char *k_slot_name[3] = { "FAN", "DOOR", "SPARE" };

static const char *slot_title(unsigned i) {
    i %= 3u;
    return s_names[i][0] ? s_names[i] : k_slot_name[i];
}

static void send_name(void) {
    ib_name_t m;
    memset(&m, 0, sizeof m);
    m.type = IB_MSG_CMD;
    m.cmd = IB_CMD_NAME;
    m.slot = s_view;
    memcpy(m.name, s_t9_buf, IB_LABEL_LEN);
    m.name[IB_LABEL_LEN - 1u] = '\0';
    if (s_link) (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void t9_begin(void) {
    memset(s_t9_buf, 0, sizeof s_t9_buf);
    {
        const char *seed = s_names[s_view % 3u];
        if (seed[0]) strncpy(s_t9_buf, seed, IB_LABEL_LEN - 1u);
    }
    s_t9_g = 0;
    s_t9_li = 0;
    s_ui = IB_UI_T9;
    s_hold_fired = 0xFFu;
    s_yel_was = s_grn_was = s_blu_was = s_gry_was = false;
    s_yel_hold = s_grn_hold = s_blu_hold = s_gry_hold = false;
    s_line_t9[0] = s_line_grp[0] = s_line_ch[0] = '\0';
    s_chrome_dirty = true;
}

static void t9_finish(void) {
    unsigned i = s_view % 3u;
    memset(s_names[i], 0, IB_LABEL_LEN);
    strncpy(s_names[i], s_t9_buf, IB_LABEL_LEN - 1u);
    send_name();
    s_ui = IB_UI_LIVE;
    s_hold_fired = 0xFFu;
    s_chrome_dirty = true;
    s_status_dirty = true;
}

static void paint_t9(uint16_t bg, uint16_t acc, uint16_t fg, uint16_t dim) {
    char line[24];
    char ch[2];
    lcd_text_draw_padded(8, 44, "name", 16, 1, dim, bg);
    snprintf(line, sizeof line, "[%s]", s_t9_buf);
    lcd_text_draw_padded_changed(8, 56, line, 12, 2, fg, bg,
                                 s_line_t9, sizeof s_line_t9);
    lcd_text_draw_padded(8, 92, "grp", 8, 1, dim, bg);
    if (s_t9_g < 0 || s_t9_g >= FWOG_T9_NGROUP) s_t9_g = 0;
    lcd_text_draw_padded_changed(8, 104, fwog_t9_group[s_t9_g], 5, 4, acc, bg,
                                 s_line_grp, sizeof s_line_grp);
    lcd_text_draw_padded(180, 92, "char", 8, 1, dim, bg);
    ch[0] = fwog_t9_cur(s_t9_g, s_t9_li);
    ch[1] = '\0';
    lcd_text_draw_padded_changed(180, 104, ch, 2, 4, fg, bg,
                                 s_line_ch, sizeof s_line_ch);
    lcd_text_draw_padded(8, 176, "GRY/RED grp  YEL/BLU char", 40, 1, dim, bg);
    lcd_text_draw_padded(8, 192, "GRN tap add  hold done", 40, 1, dim, bg);
    lcd_text_draw_padded(8, 208, "YEL hold backspace", 40, 1, dim, bg);
    lcd_text_draw_padded(8, 224, slot_title(s_view), 12, 1, acc, bg);
}

static void send_cmd(uint8_t cmd) {
    ib_cmd_t m = {
        .type = IB_MSG_CMD,
        .cmd = cmd,
        .slot = s_view,
        .mod = k_presets[s_preset].mod,
        .freq_hz = k_presets[s_preset].hz,
    };
    if (s_link) (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void poll_link(void) {
    uint8_t b;
    size_t n;
    while (fwog_link_uart_read(&b)) {
        if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
        if (fwog_ioexp_link_handle(s_rx.buf, n)) continue;
        if (n >= sizeof(ib_status_t) && s_rx.buf[0] == IB_MSG_ST) {
            ib_status_t in;
            memcpy(&in, s_rx.buf, sizeof in);
            const bool first = !s_st_valid;
            const bool layout = first ||
                in.state != s_st.state || in.ok != s_st.ok ||
                in.guess != s_st.guess || in.edges != s_st.edges ||
                in.freq_hz != s_st.freq_hz || in.cap_hz != s_st.cap_hz ||
                in.bits != s_st.bits || in.duration_ms != s_st.duration_ms ||
                in.slot != s_st.slot || in.slots != s_st.slots ||
                in.coding != s_st.coding || in.saved != s_st.saved ||
                in.wait_s != s_st.wait_s || in.hunt != s_st.hunt ||
                in.mod != s_st.mod ||
                in.temp_c_x10 != s_st.temp_c_x10 ||
                in.humidity != s_st.humidity || in.channel != s_st.channel ||
                memcmp(in.label, s_st.label, IB_LABEL_LEN) != 0 ||
                memcmp(in.name, s_st.name, IB_LABEL_LEN) != 0 ||
                memcmp(in.hex, s_st.hex, IB_HEX_BYTES) != 0;
            const bool hist = first ||
                memcmp(in.hist, s_st.hist, IB_HIST_BINS) != 0;
            const bool rssi = first || in.rssi != s_st.rssi ||
                in.peak_rssi != s_st.peak_rssi || in.bursts != s_st.bursts;
            s_st = in;
            s_st_valid = true;
            s_view = (uint8_t)(s_st.slot % 3u);
            if (s_ui != IB_UI_T9) {
                memcpy(s_names[s_view], s_st.name, IB_LABEL_LEN);
                s_names[s_view][IB_LABEL_LEN - 1u] = '\0';
            }
            if (layout) {
                s_status_dirty = true;
                s_decode_dirty = true;
            }
            if (hist) s_decode_dirty = true;
            if (rssi) s_rssi_dirty = true;
            s_bars_dirty = true;
        }
    }
}

static const char *state_text(uint8_t st) {
    switch (st) {
    case IB_ST_IDLE:    return "idle";
    case IB_ST_ARMED:   return "armed";
    case IB_ST_CAPTURE: return "capturing";
    case IB_ST_HAVE:    return "captured";
    case IB_ST_DECODE:  return "decode";
    case IB_ST_ERROR:   return "radio error";
    case IB_ST_REPLAY:  return "playing";
    default:            return "boot";
    }
}

static const char *coding_text(uint8_t c) {
    switch (c) {
    case IB_GUESS_PWM:        return "PWM";
    case IB_GUESS_PPM:        return "PPM";
    case IB_GUESS_MANCHESTER: return "MC";
    default:                  return "OOK";
    }
}

static void leds_status(void) {
    const uint8_t st = s_st_valid ? s_st.state : IB_ST_IDLE;
    const uint8_t slots = s_st_valid ? s_st.slots : 0u;
    for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++) {
        uint8_t r = 0, g = 0, b = 0;
        if (st == IB_ST_ARMED || st == IB_ST_CAPTURE) {
            r = 36u;
            g = (st == IB_ST_ARMED) ? 20u : 0u;
        } else if (st == IB_ST_ERROR) {
            r = 40u;
            b = 8u;
        } else if (st == IB_ST_REPLAY) {
            b = 40u;
            g = 8u;
        } else if (s_st_valid && s_st.hunt) {
            g = 20u;
            b = 28u;
        } else if (st == IB_ST_DECODE) {
            g = 16u;
            b = 28u;
        } else if (i < slots) {
            g = 28u;
        } else {
            g = 4u;
        }
        ws2812_set_color(i, r, g, b);
    }
    ws2812_process();
}

static int bar_h(int16_t rssi, uint16_t h) {
    int db = (int)rssi;
    if (db < -110) db = -110;
    if (db > -40) db = -40;
    return (int)h * (db + 110) / 70;
}

static uint16_t bar_color(int16_t rssi, uint16_t acc, uint16_t yel, uint16_t dim) {
    if (rssi >= -70) return acc;
    if (rssi >= -90) return yel;
    return dim;
}

static void push_rssi_bar(int16_t rssi, uint32_t now) {
    if (s_bar_ms != 0u && (now - s_bar_ms) < IB_BAR_MS) return;
    s_bar_ms = now ? now : 1u;
    s_bars[s_bar_i] = rssi;
    s_bar_i++;
    if (s_bar_i >= IB_BAR_N) s_bar_i = 0;
    if (s_bar_n < IB_BAR_N) s_bar_n++;
    s_bars_dirty = true;
}

static void draw_rssi_strip(uint16_t y0, uint16_t h, uint16_t acc, uint16_t yel,
                            uint16_t dim, uint16_t bg) {
    unsigned n = s_bar_n;
    unsigned i = (s_bar_n < IB_BAR_N) ? 0u : s_bar_i;
    unsigned k;
    st7789_fill_rect(IB_BAR_X, y0, 304, h, bg);
    for (k = 0; k < n; k++) {
        const int16_t v = s_bars[i];
        int bh, x, y;
        i++;
        if (i >= IB_BAR_N) i = 0;
        bh = bar_h(v, h);
        if (bh <= 0) continue;
        x = (int)IB_BAR_X + (int)k * (int)IB_BAR_W;
        y = (int)y0 + (int)h - bh;
        st7789_fill_rect((uint16_t)x, (uint16_t)y, IB_BAR_W - 1u, (uint16_t)bh,
                         bar_color(v, acc, yel, dim));
    }
}

static void draw_hist(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    const uint16_t bg = st7789_rgb565(12, 16, 28);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t yel = st7789_rgb565(255, 214, 48);
    st7789_fill_rect(x, y, w, h, bg);
    if (!s_st_valid) return;
    const unsigned bin_w = (unsigned)w / IB_HIST_BINS;
    if (bin_w == 0u) return;
    for (unsigned i = 0; i < IB_HIST_BINS; i++) {
        unsigned bh = s_st.hist[i];
        if (bh > h - 2u) bh = h - 2u;
        if (bh == 0u) continue;
        st7789_fill_rect((uint16_t)(x + i * bin_w + 1u),
                         (uint16_t)(y + h - 1u - bh),
                         (uint16_t)(bin_w > 1u ? bin_w - 1u : 1u),
                         (uint16_t)bh, (i & 1u) ? yel : acc);
    }
}

static void draw(uint32_t now_ms) {
    if (!s_lcd) return;
    const uint16_t bg = st7789_rgb565(8, 10, 18);
    const uint16_t fg = st7789_rgb565(230, 230, 230);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    const uint16_t yel = st7789_rgb565(255, 214, 48);
    const uint16_t bar_dim = st7789_rgb565(60, 70, 90);
    char line[44];
    const bool have = s_st_valid && s_st.edges > 0u &&
        s_st.state != IB_ST_ARMED && s_st.state != IB_ST_CAPTURE;

    if (!s_painted) {
        st7789_clear(bg);
        s_painted = true;
        s_chrome_dirty = s_status_dirty = s_rssi_dirty = s_decode_dirty = true;
        s_bars_dirty = true;
    }

    if (s_st_valid) push_rssi_bar(s_st.rssi, now_ms);

    if (s_ui == IB_UI_T9) {
        if (s_chrome_dirty) {
            st7789_clear(bg);
            st7789_fill_rect(0, 0, ST7789_W, 36, st7789_rgb565(16, 28, 40));
            lcd_text_draw_padded(8, 8, "ISMburst", 16, 2, acc,
                                 st7789_rgb565(16, 28, 40));
            s_chrome_dirty = false;
            s_line_t9[0] = s_line_grp[0] = s_line_ch[0] = '\0';
        }
        paint_t9(bg, acc, fg, dim);
        return;
    }

    if (s_chrome_dirty) {
        st7789_fill_rect(0, 0, ST7789_W, 36, st7789_rgb565(16, 28, 40));
        lcd_text_draw_padded(8, 8, "ISMburst", 16, 2, acc,
                             st7789_rgb565(16, 28, 40));
        lcd_text_draw_padded(8, 176, "GREEN arm/abort   hold clear RAM", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 192, "GRAY FAN/DOOR/SPARE  hold hunt", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 208, "YEL park/save     empty=load BURST", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 224, "BLUE park/play    RED name / 6s off", 40, 1, dim, bg);
        s_line_st[0] = s_line_park[0] = s_line_rssi[0] = s_line_meta[0] = '\0';
        s_chrome_dirty = false;
        s_status_dirty = s_rssi_dirty = s_decode_dirty = true;
    }

    {
        const char *name = slot_title(s_view);
        const char *st = s_st_valid ? state_text(s_st.state) : "waiting for main";
        const char *store = "empty";
        if (s_st_valid && s_st.state == IB_ST_REPLAY) store = "playing";
        else if (s_st_valid && s_st.saved) store = "on disk";
        else if (s_st_valid && s_st.edges > 0u) store = "in ram";
        if (s_st_valid && s_st.state == IB_ST_ARMED && s_st.wait_s > 0u) {
            snprintf(line, sizeof line, "%s %s  %us",
                     s_st.hunt ? "hunt" : st, name, (unsigned)s_st.wait_s);
        } else {
            snprintf(line, sizeof line, "%s  %s  %s  h%u",
                     st, name, store, s_st_valid ? (unsigned)s_st.bursts : 0u);
        }
        lcd_text_draw_padded_changed(8, 40, line, 40, 1, acc, bg,
                                     s_line_st, sizeof s_line_st);
        lcd_text_draw_padded_changed(8, 56, k_presets[s_preset].label, 26, 1, fg, bg,
                                     s_line_park, sizeof s_line_park);
    }

    if ((now_ms - s_rssi_drawn_ms) >= 120u || s_rssi_dirty) {
        if (s_st_valid) {
            if (have) {
                snprintf(line, sizeof line, "%+d now  peak %+d dBm",
                         (int)s_st.rssi, (int)s_st.peak_rssi);
            } else {
                snprintf(line, sizeof line, "RSSI %+d dBm", (int)s_st.rssi);
            }
            lcd_text_draw_padded_changed(8, 72, line, 40, 1, fg, bg,
                                         s_line_rssi, sizeof s_line_rssi);
        } else {
            lcd_text_draw_padded_changed(8, 72, "waiting for main CPU...", 40, 1, dim, bg,
                                         s_line_rssi, sizeof s_line_rssi);
        }
        s_rssi_drawn_ms = now_ms;
        s_rssi_dirty = false;
    }

    if (have) {
        if (s_decode_dirty) {
            snprintf(line, sizeof line, "%u ms  %u edges  %u bits",
                     (unsigned)s_st.duration_ms, (unsigned)s_st.edges,
                     (unsigned)s_st.bits);
            lcd_text_draw_padded(8, 88, line, 40, 1, fg, bg);
            snprintf(line, sizeof line, "%s  pw %u/%u  gap %u/%u",
                     coding_text(s_st.coding),
                     (unsigned)s_st.pw_short_us, (unsigned)s_st.pw_long_us,
                     (unsigned)s_st.gap_short_us, (unsigned)s_st.gap_long_us);
            lcd_text_draw_padded(8, 102, line, 40, 1, dim, bg);
            {
                unsigned pos = 0;
                const unsigned show = s_st.hex_n > IB_HEX_BYTES
                    ? IB_HEX_BYTES : (unsigned)s_st.hex_n;
                for (unsigned i = 0; i < show && pos + 2u < sizeof line; i++) {
                    pos += (unsigned)snprintf(line + pos, sizeof line - pos,
                                              "%02X", s_st.hex[i]);
                }
                if (pos == 0u) snprintf(line, sizeof line, "--");
                lcd_text_draw_padded(8, 116, line, 40, 1, acc, bg);
            }
            s_decode_dirty = false;
        }
        lcd_text_draw_padded_changed(8, IB_HIST_Y - 12u,
            (s_st.guess == IB_GUESS_PROLOGUE || s_st.guess == IB_GUESS_NEXUS ||
             s_st.guess == IB_GUESS_OREGON || s_st.guess == IB_GUESS_PT2262 ||
             s_st.guess == IB_GUESS_FIXED || s_st.guess == IB_GUESS_TPMS)
                ? s_st.label : "no protocol",
            40, 1, fg, bg, s_line_meta, sizeof s_line_meta);
        draw_hist(8, IB_HIST_Y, 304, IB_HIST_H);
        s_bars_dirty = false;
    } else {
        if (s_bars_dirty) {
            draw_rssi_strip(IB_BAR_Y, IB_BAR_H, acc, yel, bar_dim, bg);
            s_bars_dirty = false;
        }
        s_decode_dirty = false;
    }
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
        s_painted = false;
        s_chrome_dirty = s_status_dirty = s_rssi_dirty = s_decode_dirty = true;
        s_bars_dirty = true;
    } else {
        DIAG("[ismburst] LCD init failed\n");
    }
}

static bool hold_edge(uint8_t id, fwog_buttons_t btn, uint32_t now) {
    const uint8_t bit = (uint8_t)FWOG_BTN_BIT(id);
    if (btn.pressed & bit) {
        s_hold_since[id] = now;
        s_hold_fired &= (uint8_t)~bit;
    }
    if ((btn.down & bit) && !(s_hold_fired & bit) &&
        (now - s_hold_since[id]) >= IB_HOLD_MS) {
        s_hold_fired |= bit;
        return true;
    }
    return false;
}

static bool tap_edge(uint8_t id, fwog_buttons_t btn) {
    const uint8_t bit = (uint8_t)FWOG_BTN_BIT(id);
    return (btn.released & bit) && !(s_hold_fired & bit);
}

int main(void) {
    board_init();
    fwog_splash_bind("ISMburst", "007");
    (void)ws2812_init(pio0, 0u);
    lcd_bringup();
    apply_antennas();

    s_link = fwog_link_uart_init(FWOG_LINK_BAUD);
    fwog_link_rx_init(&s_rx);
    send_cmd(IB_CMD_LISTEN);
    s_chrome_dirty = true;

    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        const fwog_power_t p = fwog_power_poll(now);
        poll_link();

        (void)hold_edge(FWOG_BTN_RED, p.buttons, now);

        if (s_ui == IB_UI_T9) {
            const bool ydown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) != 0;
            const bool gdown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GREEN)) != 0;
            const bool bdown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) != 0;
            const bool rydown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GRAY)) != 0;

            if (ydown && !s_yel_was) {
                s_yel_ms = now;
                s_yel_hold = false;
            }
            if (ydown && !s_yel_hold && (now - s_yel_ms) >= IB_HOLD_MS) {
                s_yel_hold = true;
                fwog_t9_backspace(s_t9_buf);
            }
            if (!ydown && s_yel_was && !s_yel_hold)
                fwog_t9_letter_prev(s_t9_g, &s_t9_li);
            s_yel_was = ydown;

            if (gdown && !s_grn_was) {
                s_grn_ms = now;
                s_grn_hold = false;
            }
            if (gdown && !s_grn_hold && (now - s_grn_ms) >= IB_HOLD_MS) {
                s_grn_hold = true;
                t9_finish();
            }
            if (!gdown && s_grn_was && !s_grn_hold)
                fwog_t9_insert(s_t9_buf, sizeof s_t9_buf,
                               fwog_t9_cur(s_t9_g, s_t9_li));
            s_grn_was = gdown;

            if (bdown && !s_blu_was) {
                s_blu_ms = now;
                s_blu_hold = false;
            }
            if (!bdown && s_blu_was && !s_blu_hold)
                fwog_t9_letter_next(s_t9_g, &s_t9_li);
            s_blu_was = bdown;

            if (rydown && !s_gry_was) {
                s_gry_ms = now;
                s_gry_hold = false;
            }
            if (!rydown && s_gry_was && !s_gry_hold)
                fwog_t9_group_prev(&s_t9_g, &s_t9_li);
            s_gry_was = rydown;

            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_RED))
                fwog_t9_group_next(&s_t9_g, &s_t9_li);
        } else {
            if (hold_edge(FWOG_BTN_YELLOW, p.buttons, now)) {
                const bool empty = !(s_st_valid && s_st.edges > 0u);
                send_cmd(empty ? IB_CMD_LOAD : IB_CMD_SAVE);
                s_status_dirty = true;
            } else if (tap_edge(FWOG_BTN_YELLOW, p.buttons)) {
                if (--s_preset < 0) s_preset = IB_NPRESET - 1;
                apply_antennas();
                send_cmd(IB_CMD_LISTEN);
                s_chrome_dirty = true;
            }

            if (hold_edge(FWOG_BTN_BLUE, p.buttons, now)) {
                send_cmd(IB_CMD_REPLAY);
                s_status_dirty = true;
            } else if (tap_edge(FWOG_BTN_BLUE, p.buttons)) {
                if (++s_preset >= IB_NPRESET) s_preset = 0;
                apply_antennas();
                send_cmd(IB_CMD_LISTEN);
                s_chrome_dirty = true;
            }

            if (hold_edge(FWOG_BTN_GREEN, p.buttons, now)) {
                send_cmd(IB_CMD_CLEAR);
                s_status_dirty = true;
                s_decode_dirty = true;
            } else if (tap_edge(FWOG_BTN_GREEN, p.buttons)) {
                const bool busy = s_st_valid &&
                    (s_st.state == IB_ST_ARMED || s_st.state == IB_ST_CAPTURE ||
                     s_st.state == IB_ST_REPLAY);
                send_cmd(busy ? IB_CMD_ABORT : IB_CMD_ARM);
                s_status_dirty = true;
            }

            if (hold_edge(FWOG_BTN_GRAY, p.buttons, now)) {
                send_cmd(IB_CMD_HUNT);
                s_status_dirty = true;
            } else if (tap_edge(FWOG_BTN_GRAY, p.buttons)) {
                s_view = (uint8_t)((s_view + 1u) % 3u);
                send_cmd(IB_CMD_PAGE);
                s_status_dirty = true;
                s_decode_dirty = true;
            }

            if (tap_edge(FWOG_BTN_RED, p.buttons))
                t9_begin();
        }

        if (!p.armed) leds_status();
        draw(now);
        sleep_ms(2);
    }
}
