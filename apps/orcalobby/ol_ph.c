/* OrcaLobby PingHalo tile. */
#include "ol_ph.h"
#include "ol_link.h"
#include "ph_proto.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

static bool           s_chrome_dirty = true;
static ph_status_t    s_st;
static bool           s_st_ok;
static uint8_t        s_view = 0xFF;
static uint8_t        s_drawn_pct = 0xFF;
static char           s_line_a[44];
static char           s_line_b[44];
static char           s_line_c[44];
static char           s_line_d[44];
static char           s_line_e[44];
static char           s_line_why[44];
static char           s_line_scan[44];
static char           s_line_dev[PH_MAX][44];
static char           s_line_lib[PH_LIB_N][44];
static char           s_line_lib_foot[52];
static char           s_line_hint[52];
static uint8_t        s_drawn_hist_n = 0xFF;
static int8_t         s_drawn_hist[PH_HIST];

#define PH_HOLD_MS 750u
#define PH_UI_SCAN 0
#define PH_UI_LIB  1
#define PH_UI_T9   2

static int            s_ui;
static ph_lib_msg_t   s_lib;
static bool           s_lib_ok;
static bool           s_saving;
static int            s_lib_i;
static char           s_t9_buf[PH_NAME_N];
static int            s_t9_g, s_t9_li;
static uint8_t        s_t9_addr[6];
static uint32_t       s_yel_ms, s_grn_ms, s_blu_ms, s_gry_ms, s_red_ms;
static bool           s_yel_was, s_grn_was, s_blu_was, s_gry_was, s_red_was;
static bool           s_yel_hold, s_grn_hold, s_blu_hold, s_gry_hold, s_red_hold;
static uint32_t       s_get_ms;
static bool           s_lib_asked;
static char           s_line_t9[24];
static char           s_line_grp[8];
static char           s_line_ch[4];

static void send_cmd(uint8_t cmd, uint8_t on) {
    ph_cmd_t m = { .type = PH_MSG_CMD, .cmd = cmd, .on = on };
    if (ol_link_ok()) ol_link_send(&m, sizeof m);
}

static void send_lib_get(void) {
    ph_lib_msg_t m;
    memset(&m, 0, sizeof m);
    m.type = PH_MSG_LIB;
    m.cmd = (uint8_t)PH_LIB_GET;
    if (ol_link_ok()) ol_link_send(&m, sizeof m);
}

static void send_label(const uint8_t addr[6], const char *label) {
    ph_label_msg_t m;
    memset(&m, 0, sizeof m);
    m.type = PH_MSG_LABEL;
    memcpy(m.addr, addr, 6);
    if (label) strncpy(m.label, label, PH_NAME_N - 1u);
    if (ol_link_ok()) ol_link_send(&m, sizeof m);
}

static const ph_dev_t *pin_src(void) {
    if (!s_st_ok || !s_st.n) return NULL;
    if (s_st.locked) return &s_st.dev[0];
    if (s_st.cursor < s_st.n) return &s_st.dev[s_st.cursor];
    return &s_st.dev[0];
}

/* Empty library slot + Green: keep the Blue-pinned (or cursor) MAC. */
static bool lib_fill_slot(unsigned slot) {
    const ph_dev_t *d = pin_src();
    if (!d || slot >= PH_LIB_N) return false;
    s_lib.ent[slot].have = 1u;
    memcpy(s_lib.ent[slot].addr, d->addr, 6);
    memset(s_lib.ent[slot].label, 0, PH_NAME_N);
    if (d->name[0]) strncpy(s_lib.ent[slot].label, d->name, PH_NAME_N - 1u);
    s_saving = true;
    send_cmd(PH_CMD_LIBSAVE, (uint8_t)slot);
    return true;
}

static void t9_begin(const uint8_t addr[6], const char *seed) {
    memcpy(s_t9_addr, addr, 6);
    memset(s_t9_buf, 0, sizeof s_t9_buf);
    if (seed && seed[0]) strncpy(s_t9_buf, seed, PH_NAME_N - 1u);
    s_t9_g = 0;
    s_t9_li = 0;
    s_ui = PH_UI_T9;
    s_line_t9[0] = s_line_grp[0] = s_line_ch[0] = '\0';
    s_chrome_dirty = true;
}

static void t9_finish(void) {
    int slot = -1;
    unsigned i;
    for (i = 0; i < PH_LIB_N; i++) {
        if (s_lib.ent[i].have && memcmp(s_lib.ent[i].addr, s_t9_addr, 6) == 0) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0) {
        for (i = 0; i < PH_LIB_N; i++) {
            if (!s_lib.ent[i].have) {
                slot = (int)i;
                break;
            }
        }
    }
    if (slot >= 0) {
        s_lib.ent[slot].have = 1u;
        memcpy(s_lib.ent[slot].addr, s_t9_addr, 6);
        memset(s_lib.ent[slot].label, 0, PH_NAME_N);
        strncpy(s_lib.ent[slot].label, s_t9_buf, PH_NAME_N - 1u);
        s_saving = true;
        send_label(s_t9_addr, s_t9_buf);
    }
    s_ui = PH_UI_SCAN;
    s_chrome_dirty = true;
}

static void paint_t9(uint16_t bg, uint16_t acc, uint16_t fg, uint16_t dim) {
    char line[24];
    char ch[2];
    lcd_text_draw_padded(8, 44, "name", 16, 1, dim, bg);
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
    lcd_text_draw_padded(8, 192, "GRN tap add  hold done", 40, 1, dim, bg);
    lcd_text_draw_padded(8, 208, "YEL hold backspace", 40, 1, dim, bg);
}

static void paint_lib(uint16_t bg, uint16_t acc, uint16_t fg, uint16_t dim,
                      uint16_t yel) {
    char line[44];
    char mac[13];
    lcd_text_draw_padded(8, 48, "LIBRARY  PHLIB.BIN", 40, 1, acc, bg);
    for (unsigned i = 0; i < PH_LIB_N && i < 8u; i++) {
        const uint16_t y = (uint16_t)(68u + i * 16u);
        const bool sel = ((int)i == s_lib_i);
        if (!s_lib.ent[i].have) {
            snprintf(line, sizeof line, "%c%u  empty", sel ? '>' : ' ', i + 1u);
        } else {
            snprintf(mac, sizeof mac, "%02X%02X%02X%02X%02X%02X",
                     s_lib.ent[i].addr[0], s_lib.ent[i].addr[1],
                     s_lib.ent[i].addr[2], s_lib.ent[i].addr[3],
                     s_lib.ent[i].addr[4], s_lib.ent[i].addr[5]);
            snprintf(line, sizeof line, "%c%u %s",
                     sel ? '>' : ' ', i + 1u,
                     s_lib.ent[i].label[0] ? s_lib.ent[i].label : mac);
        }
        lcd_text_draw_padded_changed(8, y, line, 40, 1, sel ? yel : fg, bg,
                                     s_line_lib[i], sizeof s_line_lib[i]);
    }
    if (!s_lib.ent[s_lib_i].have && pin_src()) {
        lcd_text_draw_padded_changed(8, 210,
                                     "GRN save pin here  BLU back  GRY/RED",
                                     48, 1, dim, bg,
                                     s_line_lib_foot, sizeof s_line_lib_foot);
    } else if (s_lib.ent[s_lib_i].have) {
        lcd_text_draw_padded_changed(8, 210,
                                     "GRN hunt  YEL delete  BLU back",
                                     48, 1, dim, bg,
                                     s_line_lib_foot, sizeof s_line_lib_foot);
    } else {
        lcd_text_draw_padded_changed(8, 210,
                                     "pin a tag first  BLU back  GRY/RED",
                                     48, 1, dim, bg,
                                     s_line_lib_foot, sizeof s_line_lib_foot);
    }
}

static void leds_rssi(int8_t rssi) {
    if (!ol_leds_ok()) return;
    int lit = 0;
    if (rssi > -95) {
        lit = (int)(rssi + 95) / 8;
        if (lit < 0) lit = 0;
        if (lit > (int)FWOG_LED_COUNT) lit = (int)FWOG_LED_COUNT;
    }
    for (unsigned i = 0; i < FWOG_LED_COUNT; i++) {
        const bool on = (int)i < lit;
        ws2812_set_color(i, on ? 8u : 2u, on ? 36u : 2u, 8u);
    }
    ws2812_process();
}

static void fmt_addr(char *out, size_t n, const uint8_t *a) {
    snprintf(out, n, "%02X%02X%02X%02X%02X%02X",
             a[0], a[1], a[2], a[3], a[4], a[5]);
}

static const char *filt_name(uint8_t f) {
    if (f == PH_FILT_NAMED) return "named";
    if (f == PH_FILT_APPLE) return "Apple";
    return "all";
}

static void slots_reset(void) {
    s_line_a[0] = s_line_b[0] = s_line_c[0] = s_line_d[0] = s_line_e[0] = '\0';
    s_line_why[0] = '\0';
    s_line_scan[0] = s_line_hint[0] = '\0';
    memset(s_line_dev, 0, sizeof s_line_dev);
    memset(s_line_lib, 0, sizeof s_line_lib);
    s_line_lib_foot[0] = '\0';
    s_line_t9[0] = s_line_grp[0] = s_line_ch[0] = '\0';
    s_drawn_pct = 0xFF;
}

#define PH_CHART_X  8
#define PH_CHART_Y  84
#define PH_CHART_W  304
#define PH_CHART_H  48

static uint8_t bar_trend(const int8_t *hist, unsigned i) {
    if (!hist || i == 0u) return PH_TREND_HOLD;
    const int cur = (int)hist[i];
    const int prev = (int)hist[i - 1u];
    if (cur >= prev + PH_DEADBAND_DB) return PH_TREND_CLOSE;
    if (cur <= prev - PH_DEADBAND_DB) return PH_TREND_FAR;
    return PH_TREND_HOLD;
}

static int rssi_bar_h(int8_t rssi, int chart_h) {
    int span = (int)rssi + 100; /* -100 dBm → 0, -40 dBm → 60 */
    if (span < 0) span = 0;
    if (span > 60) span = 60;
    int h = 2 + (span * (chart_h - 2)) / 60;
    if (h < 2) h = 2;
    if (h > chart_h) h = chart_h;
    return h;
}

static uint16_t trend_color(uint8_t t, uint16_t acc, uint16_t blu, uint16_t rec) {
    if (t == PH_TREND_CLOSE) return acc;
    if (t == PH_TREND_FAR) return rec;
    return blu;
}

static const char *trend_name(uint8_t t) {
    if (t == PH_TREND_CLOSE) return "closer";
    if (t == PH_TREND_FAR) return "farther";
    if (t == PH_TREND_HOLD) return "steady";
    return "tag";
}

static void leds_trend(uint8_t t, int8_t rssi) {
    if (!ol_leds_ok()) return;
    uint8_t r = 2, g = 2, b = 8;
    if (t == PH_TREND_CLOSE) {
        r = 8;
        g = 40;
        b = 8;
    } else if (t == PH_TREND_FAR) {
        r = 40;
        g = 8;
        b = 8;
    } else if (t == PH_TREND_HOLD) {
        r = 8;
        g = 12;
        b = 40;
    }
    int lit = 0;
    if (rssi > -95) {
        lit = (int)(rssi + 95) / 8;
        if (lit < 0) lit = 0;
        if (lit > (int)FWOG_LED_COUNT) lit = (int)FWOG_LED_COUNT;
    }
    for (unsigned i = 0; i < FWOG_LED_COUNT; i++) {
        const bool on = (int)i < lit;
        ws2812_set_color(i, on ? r : 2u, on ? g : 2u, on ? b : 4u);
    }
    ws2812_process();
}

static void leds_saving(void) {
    if (!ol_leds_ok()) return;
    for (unsigned i = 0; i < FWOG_LED_COUNT; i++) {
        ws2812_set_color(i, 28u, 18u, 2u);
    }
    ws2812_process();
}

static uint8_t screen_id(uint8_t fl) {
    if (fl == PH_FLASH_HOLD) return 1u;
    if (fl == PH_FLASH_SYNC || fl == PH_FLASH_WRITE) return 2u;
    if (fl == PH_FLASH_OK) return 3u;
    if (fl == PH_FLASH_FAIL) return 4u;
    if (!s_st_ok || !s_st.hello) return 5u;
    if (s_saving || (s_st_ok && s_st.lib_op == PH_LIBOP_SAVE)) return 10u;
    if (s_ui == PH_UI_T9) return 8u;
    if (s_ui == PH_UI_LIB) return 9u;
    if (s_st.locked) return 7u;
    return 6u;
}

void ol_ph_paint(void) {
    const uint16_t bg  = st7789_rgb565(8, 10, 18);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    const uint16_t fg  = st7789_rgb565(230, 230, 230);
    const uint16_t yel = st7789_rgb565(255, 214, 48);
    const uint16_t rec = st7789_rgb565(220, 40, 40);
    const uint16_t blu = st7789_rgb565(40, 90, 220);
    char line[44];
    char mac[13];
    const uint8_t fl = s_st_ok ? s_st.flash : 0u;
    const uint8_t view = screen_id(fl);

    if (s_chrome_dirty) {
        st7789_fill_rect(0, 0, 320, 40, bg);
        lcd_text_draw_padded(8, 8, "PingHalo", 16, 2, acc, bg);
        s_chrome_dirty = false;
    }

    if (view != s_view) {
        st7789_fill_rect(0, 40, 320, 200, bg);
        slots_reset();
        s_view = view;
        if (view == 1u) {
            lcd_text_draw_padded(8, 210, "YELLOW cancels", 36, 1, dim, bg);
        } else if (view == 6u || view == 7u || view == 8u || view == 9u || view == 10u) {
            lcd_text_draw_padded(8, 210, "", 36, 1, dim, bg);
            s_drawn_hist_n = 0xFF;
        }
    }

    if (fl == PH_FLASH_HOLD) {
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
        lcd_text_draw_padded_changed(8, 156,
                                     s_st.why[0] ? s_st.why : "8=0 9=0",
                                     36, 1, acc, bg, s_line_why, sizeof s_line_why);
        leds_rssi(-127);
        return;
    }
    if (fl == PH_FLASH_SYNC || fl == PH_FLASH_WRITE) {
        lcd_text_draw_padded_changed(8, 56,
                                     fl == PH_FLASH_SYNC ? "syncing ROM..." : "writing flash",
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
        lcd_text_draw_padded_changed(8, 210,
                                     fl == PH_FLASH_SYNC ? "keep BOOT held" : "BOOT can be released",
                                     36, 1, dim, bg, s_line_e, sizeof s_line_e);
        leds_rssi(-127);
        return;
    }
    if (fl == PH_FLASH_OK) {
        lcd_text_draw_padded_changed(8, 56, "C6 flash ok", 36, 1, acc, bg,
                                     s_line_a, sizeof s_line_a);
        lcd_text_draw_padded_changed(8, 76, "LET GO of BOOT first", 36, 1, yel, bg,
                                     s_line_b, sizeof s_line_b);
        lcd_text_draw_padded_changed(8, 96, "then tap Orca RESET", 36, 1, fg, bg,
                                     s_line_c, sizeof s_line_c);
        lcd_text_draw_padded_changed(8, 112,
                                     s_st.why[0] ? s_st.why : "USB ESP-ROM = still in loader",
                                     36, 1, dim, bg,
                                     s_line_d, sizeof s_line_d);
        lcd_text_draw_padded_changed(8, 210, "YELLOW flash again", 36, 1, dim, bg,
                                     s_line_e, sizeof s_line_e);
        leds_rssi(-127);
        return;
    }
    if (fl == PH_FLASH_FAIL) {
        lcd_text_draw_padded_changed(8, 56, "C6 flash failed", 36, 1, rec, bg,
                                     s_line_a, sizeof s_line_a);
        lcd_text_draw_padded_changed(8, 76, s_st.why[0] ? s_st.why : "see DIAG", 36, 1, fg, bg,
                                     s_line_b, sizeof s_line_b);
        lcd_text_draw_padded_changed(8, 96, "unplug USB-C, keep BOOT held,", 36, 1, dim, bg,
                                     s_line_c, sizeof s_line_c);
        lcd_text_draw_padded_changed(8, 112, "tap RESET, then GREEN", 36, 1, dim, bg,
                                     s_line_d, sizeof s_line_d);
        lcd_text_draw_padded_changed(8, 210, "YELLOW retry", 36, 1, dim, bg,
                                     s_line_e, sizeof s_line_e);
        leds_rssi(-127);
        return;
    }

    if (!s_st_ok || !s_st.hello) {
        lcd_text_draw_padded_changed(8, 56, "waiting for Bottlenose", 36, 1, dim, bg,
                                     s_line_a, sizeof s_line_a);
        lcd_text_draw_padded_changed(8, 76, "BLUE hold flashes C6 image", 36, 1, yel, bg,
                                     s_line_b, sizeof s_line_b);
        lcd_text_draw_padded_changed(8, 96, "unplug USB-C, hold BOOT,", 36, 1, dim, bg,
                                     s_line_c, sizeof s_line_c);
        lcd_text_draw_padded_changed(8, 112, "tap RESET, GREEN while held", 36, 1, dim, bg,
                                     s_line_d, sizeof s_line_d);
        lcd_text_draw_padded_changed(8, 156,
                                     s_st.why[0] ? s_st.why : "rxb=0",
                                     36, 1, acc, bg, s_line_why, sizeof s_line_why);
        lcd_text_draw_padded_changed(8, 210, "GREEN starts scan once hello", 36, 1, dim, bg,
                                     s_line_e, sizeof s_line_e);
        leds_rssi(-127);
        return;
    }

    if (s_saving || s_st.lib_op == PH_LIBOP_SAVE) {
        lcd_text_draw_padded_changed(8, 56, "SAVING LIBRARY", 36, 2, yel, bg,
                                     s_line_a, sizeof s_line_a);
        lcd_text_draw_padded_changed(8, 88, "BLE address + name only", 40, 1, fg, bg,
                                     s_line_b, sizeof s_line_b);
        lcd_text_draw_padded_changed(8, 108, "not RSSI / not the signal", 40, 1, dim, bg,
                                     s_line_c, sizeof s_line_c);
        lcd_text_draw_padded_changed(8, 140, "writing PHLIB.BIN on main", 40, 1, acc, bg,
                                     s_line_d, sizeof s_line_d);
        lcd_text_draw_padded_changed(8, 210, "wait  flash erase in progress", 40, 1, yel, bg,
                                     s_line_e, sizeof s_line_e);
        leds_saving();
        return;
    }
    if (s_st.lib_op == PH_LIBOP_FAIL) {
        lcd_text_draw_padded_changed(8, 56, "library save failed", 36, 1, rec, bg,
                                     s_line_a, sizeof s_line_a);
        lcd_text_draw_padded_changed(8, 76, "FatFs write  try Green again", 40, 1, fg, bg,
                                     s_line_b, sizeof s_line_b);
        leds_rssi(-127);
        return;
    }

    if (s_ui == PH_UI_T9) {
        paint_t9(bg, acc, fg, dim);
        return;
    }
    if (s_ui == PH_UI_LIB) {
        paint_lib(bg, acc, fg, dim, yel);
        return;
    }

    snprintf(line, sizeof line, "%s%s  %s  %u%s",
             s_st.scan_on ? "BLE scan" : "GREEN to scan",
             s_st.freeze ? " FROZEN" : "",
             filt_name(s_st.filter),
             (unsigned)s_st.total,
             s_st.freeze ? " locked" : " heard");
    lcd_text_draw_padded_changed(8, 48, line, 36, 1, s_st.scan_on ? acc : yel, bg,
                                 s_line_scan, sizeof s_line_scan);

    if (s_st.locked && s_st.n) {
        const ph_dev_t *d = &s_st.dev[0];
        const uint8_t hn = s_st.hist_n > PH_HIST ? (uint8_t)PH_HIST : s_st.hist_n;
        uint8_t last_t = PH_TREND_HOLD;
        if (hn >= 2u) last_t = bar_trend(s_st.hist, (unsigned)hn - 1u);
        fmt_addr(mac, sizeof mac, d->addr);
        snprintf(line, sizeof line, "* %s  %d dBm  %s",
                 d->name[0] ? d->name : mac, (int)d->rssi,
                 trend_name(s_st.trend));
        lcd_text_draw_padded_changed(8, 66, line, 40, 1, yel, bg,
                                     s_line_dev[0], sizeof s_line_dev[0]);
        if (s_drawn_hist_n != hn ||
            memcmp(s_drawn_hist, s_st.hist, hn) != 0) {
            const unsigned slot = (unsigned)PH_CHART_W / PH_HIST;
            const unsigned bw = slot > 2u ? slot - 1u : 1u;
            st7789_fill_rect(PH_CHART_X, PH_CHART_Y,
                             PH_CHART_W, PH_CHART_H, bg);
            st7789_fill_rect(PH_CHART_X, (uint16_t)(PH_CHART_Y + PH_CHART_H - 1),
                             PH_CHART_W, 1, dim);
            for (unsigned i = 0; i < hn; i++) {
                const int h = rssi_bar_h(s_st.hist[i], PH_CHART_H);
                const uint16_t col = trend_color(bar_trend(s_st.hist, i),
                                                 acc, blu, rec);
                const uint16_t x = (uint16_t)(PH_CHART_X + i * slot);
                const uint16_t y = (uint16_t)(PH_CHART_Y + PH_CHART_H - h);
                st7789_fill_rect(x, y, (uint16_t)bw, (uint16_t)h, col);
            }
            s_drawn_hist_n = hn;
            memcpy(s_drawn_hist, s_st.hist, sizeof s_drawn_hist);
        }
        unsigned shown = s_st.n < PH_MAX ? s_st.n : PH_MAX;
        for (unsigned i = 1; i < PH_MAX; i++) {
            const uint16_t y = (uint16_t)(136u + (i - 1u) * 18u);
            if (i < shown) {
                const ph_dev_t *row = &s_st.dev[i];
                const bool sel = (i == s_st.cursor);
                fmt_addr(mac, sizeof mac, row->addr);
                snprintf(line, sizeof line, "%c%4d %s %s %s",
                         sel ? '>' : ' ',
                         (int)row->rssi,
                         row->apple ? "A" : ".",
                         row->name[0] ? row->name : mac,
                         row->apple ? "FindMy?" : "");
                lcd_text_draw_padded_changed(8, y, line, 40, 1,
                                             sel ? yel : (row->apple ? yel : fg),
                                             bg, s_line_dev[i],
                                             sizeof s_line_dev[i]);
            } else if (s_line_dev[i][0] != '\0') {
                lcd_text_draw_padded(8, y, "", 40, 1, fg, bg);
                s_line_dev[i][0] = '\0';
            }
        }
        lcd_text_draw_padded_changed(8, 210,
                                     "BLU unpin  hold lib  GRN freeze  RED hold T9",
                                     48, 1, dim, bg, s_line_hint, sizeof s_line_hint);
        leds_trend(last_t, d->rssi);
        return;
    }

    int8_t meter = -127;
    unsigned shown = s_st.n < PH_MAX ? s_st.n : PH_MAX;
    for (unsigned i = 0; i < PH_MAX; i++) {
        if (i < shown) {
            const ph_dev_t *d = &s_st.dev[i];
            const bool sel = (i == s_st.cursor);
            if (s_st.locked && sel) meter = d->rssi;
            else if (!s_st.locked && d->rssi > meter) meter = d->rssi;
            fmt_addr(mac, sizeof mac, d->addr);
            snprintf(line, sizeof line, "%c%4d %s %s %s",
                     sel ? (s_st.locked ? '*' : '>') : ' ',
                     (int)d->rssi,
                     d->apple ? "A" : ".",
                     d->name[0] ? d->name : mac,
                     d->apple ? "FindMy?" : "");
            lcd_text_draw_padded_changed(8, (uint16_t)(68u + i * 18u), line, 40, 1,
                                         sel ? yel : (d->apple ? yel : fg), bg,
                                         s_line_dev[i], sizeof s_line_dev[i]);
        } else if (s_line_dev[i][0] != '\0') {
            lcd_text_draw_padded(8, (uint16_t)(68u + i * 18u), "", 40, 1, fg, bg);
            s_line_dev[i][0] = '\0';
        }
    }
    {
        const char *hint;
        if (!s_st.scan_on) {
            hint = "GRN freeze  BLU hold C6/lib  YEL filt";
        } else if (s_st.freeze) {
            hint = "GRN live  GRY/RED scroll  BLU pin";
        } else {
            hint = "GRN freeze  BLU pin  hold lib  YEL filt";
        }
        lcd_text_draw_padded_changed(8, 210, hint, 48, 1, dim, bg,
                                     s_line_hint, sizeof s_line_hint);
    }
    leds_rssi(meter);
}

void ol_ph_enter(void) {
    s_st_ok = false;
    memset(&s_st, 0, sizeof s_st);
    s_ui = PH_UI_SCAN;
    s_saving = false;
    s_lib_ok = false;
    s_lib_i = 0;
    s_view = 0xFF;
    s_chrome_dirty = true;
    s_yel_was = s_grn_was = s_blu_was = s_gry_was = s_red_was = false;
    s_yel_hold = s_grn_hold = s_blu_hold = s_gry_hold = s_red_hold = false;
    slots_reset();
    /* Stamp now. First LIB_GET waits 3 s so main can FatFs-load the
     * library without the display filling CTS (PingHalo 023 / ChirpMail). */
    s_get_ms = to_ms_since_boot(get_absolute_time());
    s_lib_asked = false;
}

void ol_ph_frame(const uint8_t *buf, size_t n) {
    if (n >= sizeof(ph_status_t) && buf[0] == PH_MSG_ST) {
        memcpy(&s_st, buf, sizeof s_st);
        s_st_ok = true;
        if (s_st.lib_op == PH_LIBOP_SAVE) s_saving = true;
        if (s_st.lib_op == PH_LIBOP_OK || s_st.lib_op == PH_LIBOP_FAIL)
            s_saving = false;
    } else if (n >= sizeof(ph_lib_msg_t) && buf[0] == PH_MSG_LIB) {
        if (s_ui != PH_UI_T9 && !s_saving) {
            memcpy(&s_lib, buf, sizeof s_lib);
            if (s_lib.magic == PH_LIB_MAGIC) s_lib_ok = true;
        }
        if (s_st.lib_op == PH_LIBOP_OK || s_st.lib_op == PH_LIBOP_FAIL)
            s_saving = false;
    }
}

void ol_ph_buttons(const fwog_power_t *p, bool y_long, bool g_long,
                   bool gray_long, bool red_tap) {
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    const uint8_t fl = s_st_ok ? s_st.flash : 0u;
    const bool ydown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) != 0;
    const bool gdown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_GREEN)) != 0;
    const bool bdown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) != 0;
    const bool rydown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_GRAY)) != 0;
    const bool rdown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_RED)) != 0;

    if (!s_lib_ok && ol_link_ok()) {
        const uint32_t wait = s_lib_asked ? 2000u : 3000u;
        if ((now - s_get_ms) >= wait) {
            s_get_ms = now;
            s_lib_asked = true;
            send_lib_get();
        }
    }

    if (p->buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) {
        s_yel_ms = now;
        s_yel_hold = false;
        if (fl == PH_FLASH_HOLD || fl == PH_FLASH_FAIL || fl == PH_FLASH_OK) {
            send_cmd(PH_CMD_FLASH, 0u);
            s_yel_hold = true;
        }
    }
    if (ydown && !s_yel_was) {
        s_yel_ms = now;
        s_yel_hold = false;
    }
    (void)y_long;
    if (!ydown && s_yel_was && !s_yel_hold) {
        if (s_ui == PH_UI_T9) {
            fwog_t9_letter_prev(s_t9_g, &s_t9_li);
        } else if (fl == PH_FLASH_IDLE && s_st_ok && s_st.hello &&
                   s_ui == PH_UI_SCAN) {
            send_cmd(PH_CMD_FILT, (uint8_t)((s_st.filter + 1u) % 3u));
        } else if (s_ui == PH_UI_LIB && s_lib.ent[s_lib_i].have) {
            send_cmd(PH_CMD_LIBDEL, (uint8_t)s_lib_i);
            s_lib.ent[s_lib_i].have = 0u;
            memset(&s_lib.ent[s_lib_i], 0, sizeof s_lib.ent[s_lib_i]);
            s_saving = true;
        }
    }
    s_yel_was = ydown;

    if (gdown && !s_grn_was) {
        s_grn_ms = now;
        s_grn_hold = false;
    }
    (void)g_long;
    if (!gdown && s_grn_was && !s_grn_hold) {
        if (fl == PH_FLASH_HOLD) {
            send_cmd(PH_CMD_FLASH, 2u);
        } else if (s_ui == PH_UI_T9) {
            fwog_t9_insert(s_t9_buf, sizeof s_t9_buf,
                           fwog_t9_cur(s_t9_g, s_t9_li));
        } else if (s_ui == PH_UI_LIB) {
            if (s_lib.ent[s_lib_i].have) {
                send_cmd(PH_CMD_LIBPIN, (uint8_t)s_lib_i);
                s_ui = PH_UI_SCAN;
                s_chrome_dirty = true;
            } else if (lib_fill_slot((unsigned)s_lib_i)) {
                s_chrome_dirty = true;
            }
        } else if (fl == PH_FLASH_IDLE && s_st_ok && s_st.hello &&
                   s_ui == PH_UI_SCAN) {
            send_cmd(PH_CMD_FREEZE, s_st.freeze ? 0u : 1u);
        }
    }
    s_grn_was = gdown;

    if (bdown && !s_blu_was) {
        s_blu_ms = now;
        s_blu_hold = false;
    }
    if (bdown && !s_blu_hold && (now - s_blu_ms) >= PH_HOLD_MS) {
        s_blu_hold = true;
        if (s_ui == PH_UI_T9) {
            t9_finish();
        } else if ((!s_st_ok || !s_st.hello || fl != PH_FLASH_IDLE) &&
                   fl != PH_FLASH_SYNC && fl != PH_FLASH_WRITE) {
            send_cmd(PH_CMD_FLASH, 1u);
        } else if (s_ui == PH_UI_SCAN && fl == PH_FLASH_IDLE &&
                   s_st_ok && s_st.hello) {
            s_ui = PH_UI_LIB;
            s_chrome_dirty = true;
        }
    }
    if (!bdown && s_blu_was && !s_blu_hold) {
        if (s_ui == PH_UI_T9) {
            fwog_t9_letter_next(s_t9_g, &s_t9_li);
        } else if (s_ui == PH_UI_LIB) {
            s_ui = PH_UI_SCAN;
            s_chrome_dirty = true;
        } else if (fl == PH_FLASH_IDLE && s_st_ok && s_st.hello) {
            send_cmd(PH_CMD_LOCK, 1u);
        }
    }
    s_blu_was = bdown;

    if (rydown && !s_gry_was) {
        s_gry_ms = now;
        s_gry_hold = false;
    }
    (void)gray_long;
    if (!rydown && s_gry_was && !s_gry_hold) {
        if (s_ui == PH_UI_T9) {
            fwog_t9_group_prev(&s_t9_g, &s_t9_li);
        } else if (s_ui == PH_UI_LIB) {
            if (--s_lib_i < 0) s_lib_i = (int)PH_LIB_N - 1;
        } else if (fl == PH_FLASH_IDLE && s_st_ok && s_st.hello) {
            send_cmd(PH_CMD_CURSOR, 0u);
        }
    }
    s_gry_was = rydown;

    if (rdown && !s_red_was) {
        s_red_ms = now;
        s_red_hold = false;
    }
    if (rdown && !s_red_hold && (now - s_red_ms) >= PH_HOLD_MS) {
        s_red_hold = true;
        if (s_ui == PH_UI_T9) {
            fwog_t9_backspace(s_t9_buf);
        } else if (s_ui == PH_UI_SCAN && fl == PH_FLASH_IDLE &&
                   s_st_ok && s_st.hello && s_st.n) {
            const ph_dev_t *d = &s_st.dev[s_st.cursor];
            t9_begin(d->addr, d->name);
        }
    }
    if (!rdown && s_red_was && !s_red_hold) {
        if (s_ui == PH_UI_T9) {
            fwog_t9_group_next(&s_t9_g, &s_t9_li);
        } else if (s_ui == PH_UI_LIB) {
            s_lib_i = (s_lib_i + 1) % (int)PH_LIB_N;
        } else if (fl == PH_FLASH_IDLE && s_st_ok && s_st.hello) {
            send_cmd(PH_CMD_CURSOR, 1u);
        }
    }
    s_red_was = rdown;
    (void)red_tap;
}
