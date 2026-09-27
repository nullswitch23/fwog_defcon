/* FunDesk TrailRF tile. BLUE hold T9 (GRAY hold is home). */
#include "fd_trf.h"
#include "fd_link.h"
#include "pedometer.h"
#include "trf_proto.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

#define TRF_ST_IDLE 0
#define TRF_ST_CAL  1
#define TRF_ST_RUN  2
#define TRF_UI_MAIN 0
#define TRF_UI_T9   1
#define TRF_HOLD_MS 750u
#define TRF_TITLE_N 9

#define TRF_BAR_N  150u
#define TRF_BAR_X  8u
#define TRF_BAR_W  2u
#define TRF_BAR_MS 100u
#define TRF_BAR_H  36u
#define TRF_BAR_Y0 72u
#define TRF_BAR_Y1 128u

typedef struct {
    uint32_t    hz;
    const char *label;
} trf_preset_t;

static const trf_preset_t k_park[] = {
    { 315000000u, "315 NA RKE" },
    { 433920000u, "433.92 EU RKE" },
    { 868350000u, "868.35 EU" },
    { 915000000u, "915 ISM" },
};
#define TRF_NPARK ((int)(sizeof k_park / sizeof k_park[0]))

typedef struct {
    int16_t  bars[TRF_BAR_N];
    unsigned n, i;
} trf_strip_t;

static bool s_lcd = true, s_accel, s_chrome_dirty = true;
static bool s_ship_noted, s_power_armed, s_bars_dirty = true;
static it_pedo_t s_pedo;
static it_rest_t s_rest;
static int      s_st = TRF_ST_IDLE;
static int      s_preset[2] = { 0, 1 }; /* 315 top, 433.92 bottom */
static uint32_t s_steps_shown;
static int16_t  s_rssi[2] = { -127, -127 };
static char     s_line_walk[44];
static char     s_line_rssi[2][44];
static trf_strip_t s_strip[2];
static uint32_t s_bar_ms;
static int      s_ui;
static char     s_title[TRF_TITLE_N];
static char     s_t9_buf[TRF_TITLE_N];
static int      s_t9_g, s_t9_li;
static uint32_t s_yel_ms, s_grn_ms, s_blu_ms, s_gry_ms;
static bool     s_yel_was, s_grn_was, s_blu_was, s_gry_was;
static bool     s_yel_hold, s_grn_hold, s_blu_hold, s_gry_hold;
static char     s_line_t9[24];
static char     s_line_grp[8];
static char     s_line_ch[4];

static fwog_ant_t ant_for_hz(uint32_t hz) {
    if (hz < 360000000u) return FWOG_ANT_200MHZ;
    if (hz < 600000000u) return FWOG_ANT_400MHZ;
    return FWOG_ANT_900MHZ;
}

static void apply_antennas(void) {
    (void)fwog_ioexp_link_set_antennas(ant_for_hz(k_park[s_preset[0]].hz),
                                       ant_for_hz(k_park[s_preset[1]].hz));
}

static void send_tune(unsigned radio) {
    trf_cmd_t c;
    memset(&c, 0, sizeof c);
    c.type = TRF_MSG_CMD;
    c.radio = (uint8_t)radio;
    c.freq_hz = k_park[s_preset[radio]].hz;
    if (fd_link_ok()) fd_link_send(&c, sizeof c);
    apply_antennas();
}

static bool take_sample(lis3dh_sample_t *out) {
    lis3dh_sample_t samp;
    samp.x = samp.y = samp.z = 0x7FFF;
    if (!lis3dh_process(LIS3DH_MOVE_THRESHOLD_DEFAULT, &samp, NULL)) return false;
    if (samp.x == 0x7FFF && samp.y == 0x7FFF && samp.z == 0x7FFF) return false;
    *out = samp;
    return true;
}

static void send_log(uint8_t cmd) {
    if (!fd_link_ok()) return;
    trf_log_t log;
    memset(&log, 0, sizeof log);
    log.type = TRF_MSG_LOG;
    log.cmd = cmd;
    log.rssi0 = s_rssi[0];
    log.rssi1 = s_rssi[1];
    log.step = s_pedo.steps;
    log.freq0_hz = k_park[s_preset[0]].hz;
    log.freq1_hz = k_park[s_preset[1]].hz;
    memcpy(log.title, s_title, sizeof log.title);
    (void)fd_link_send(&log, sizeof log);
}

static void t9_begin(void) {
    memset(s_t9_buf, 0, sizeof s_t9_buf);
    if (s_title[0]) strncpy(s_t9_buf, s_title, TRF_TITLE_N - 1u);
    s_t9_g = 0;
    s_t9_li = 0;
    s_ui = TRF_UI_T9;
    s_line_t9[0] = s_line_grp[0] = s_line_ch[0] = '\0';
    s_chrome_dirty = true;
}

static void t9_finish(void) {
    memset(s_title, 0, sizeof s_title);
    strncpy(s_title, s_t9_buf, TRF_TITLE_N - 1u);
    s_ui = TRF_UI_MAIN;
    s_chrome_dirty = true;
}

static void paint_t9(uint16_t bg, uint16_t acc, uint16_t fg, uint16_t dim) {
    char line[24];
    char ch[2];
    lcd_text_draw_padded(8, 44, "walk", 16, 1, dim, bg);
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

static void push_bars(int16_t r0, int16_t r1, uint32_t now) {
    int16_t v[2];
    unsigned s;
    if (s_bar_ms != 0u && (now - s_bar_ms) < TRF_BAR_MS) return;
    s_bar_ms = now;
    v[0] = r0;
    v[1] = r1;
    for (s = 0; s < 2u; s++) {
        s_strip[s].bars[s_strip[s].i] = v[s];
        s_strip[s].i++;
        if (s_strip[s].i >= TRF_BAR_N) s_strip[s].i = 0;
        if (s_strip[s].n < TRF_BAR_N) s_strip[s].n++;
    }
    s_bars_dirty = true;
}

static int bar_h(int16_t rssi) {
    int db = (int)rssi;
    if (db < -110) db = -110;
    if (db > -40) db = -40;
    return (int)TRF_BAR_H * (db + 110) / 70;
}

static uint16_t bar_color(int16_t rssi, uint16_t acc, uint16_t yel, uint16_t dim) {
    if (rssi >= -70) return acc;
    if (rssi >= -90) return yel;
    return dim;
}

static void draw_strip(unsigned s, uint16_t y0, uint16_t acc, uint16_t yel,
                       uint16_t dim, uint16_t bg) {
    unsigned n = s_strip[s].n;
    unsigned i = (s_strip[s].n < TRF_BAR_N) ? 0u : s_strip[s].i;
    unsigned k;
    st7789_fill_rect(TRF_BAR_X, y0, 304, TRF_BAR_H, bg);
    for (k = 0; k < n; k++) {
        const int16_t v = s_strip[s].bars[i];
        int h, x, y;
        i++;
        if (i >= TRF_BAR_N) i = 0;
        h = bar_h(v);
        if (h <= 0) continue;
        x = (int)TRF_BAR_X + (int)k * (int)TRF_BAR_W;
        y = (int)y0 + (int)TRF_BAR_H - h;
        st7789_fill_rect((uint16_t)x, (uint16_t)y, TRF_BAR_W - 1u, (uint16_t)h,
                         bar_color(v, acc, yel, dim));
    }
}

static void draw_bars(void) {
    const uint16_t bg  = st7789_rgb565(8, 10, 18);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t dim = st7789_rgb565(60, 70, 90);
    const uint16_t yel = st7789_rgb565(255, 214, 48);
    if (!s_lcd || !s_bars_dirty) return;
    draw_strip(0, TRF_BAR_Y0, acc, yel, dim, bg);
    draw_strip(1, TRF_BAR_Y1, acc, yel, dim, bg);
    s_bars_dirty = false;
}

static void paint(void) {
    const uint16_t bg  = st7789_rgb565(8, 10, 18);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    const uint16_t fg  = st7789_rgb565(230, 230, 230);
    const uint16_t yel = st7789_rgb565(255, 214, 48);
    char line[40];
    if (!s_lcd) return;
    if (s_ui == TRF_UI_T9) {
        if (s_chrome_dirty) {
            st7789_fill_rect(0, 0, 320, 240, bg);
            lcd_text_draw_padded(8, 8, "TrailRF", 18, 2, acc, bg);
            s_chrome_dirty = false;
            s_line_t9[0] = s_line_grp[0] = s_line_ch[0] = '\0';
        }
        paint_t9(bg, acc, fg, dim);
        return;
    }
    if (s_chrome_dirty) {
        st7789_fill_rect(0, 0, 320, 40, bg);
        lcd_text_draw_padded(8, 8, "TrailRF", 18, 2, acc, bg);
        lcd_text_draw_padded(8, 176, "GREEN start/stop   YELLOW mark walk", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 192, "GRAY top park  BLUE hold T9 / tap bottom", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 208, "RED 6s off         CSV /trailrf", 40, 1, dim, bg);
        s_line_walk[0] = '\0';
        s_line_rssi[0][0] = '\0';
        s_line_rssi[1][0] = '\0';
        s_chrome_dirty = false;
        s_bars_dirty = true;
    }
    if (s_st == TRF_ST_CAL) {
        snprintf(line, sizeof line, "hold still  %u / %u",
                 s_rest.n, (unsigned)IT_CAL_SAMPLES);
    } else if (s_title[0]) {
        snprintf(line, sizeof line, "%s  steps %u  [%s]",
                 s_st == TRF_ST_RUN ? "WALK" : "idle",
                 (unsigned)s_steps_shown, s_title);
    } else {
        snprintf(line, sizeof line, "%s  steps %u",
                 s_st == TRF_ST_RUN ? "WALK" : "idle", (unsigned)s_steps_shown);
    }
    lcd_text_draw_padded_changed(8, 40, line, 40, 1,
                                 s_st == TRF_ST_CAL ? yel : fg, bg,
                                 s_line_walk, sizeof s_line_walk);
    snprintf(line, sizeof line, "GRAY  %s  %+d dBm",
             k_park[s_preset[0]].label, (int)s_rssi[0]);
    lcd_text_draw_padded_changed(8, 56, line, 40, 1, fg, bg,
                                 s_line_rssi[0], sizeof s_line_rssi[0]);
    snprintf(line, sizeof line, "BLUE  %s  %+d dBm",
             k_park[s_preset[1]].label, (int)s_rssi[1]);
    lcd_text_draw_padded_changed(8, 112, line, 40, 1, fg, bg,
                                 s_line_rssi[1], sizeof s_line_rssi[1]);
    draw_bars();
}

void fd_trf_enter(void) {
    s_lcd = true;
    s_accel = fd_accel_ok();
    s_ui = TRF_UI_MAIN;
    s_st = TRF_ST_IDLE;
    s_chrome_dirty = true;
    s_bars_dirty = true;
    s_yel_was = s_grn_was = s_blu_was = s_gry_was = false;
    s_yel_hold = s_grn_hold = s_blu_hold = s_gry_hold = true;
    apply_antennas();
    send_tune(0);
    send_tune(1);
}

void fd_trf_leave(void) {
    if (s_ui == TRF_UI_T9) t9_finish();
    if (s_st == TRF_ST_RUN || s_st == TRF_ST_CAL) send_log(TRF_LOG_STOP);
    s_st = TRF_ST_IDLE;
    s_ui = TRF_UI_MAIN;
}

void fd_trf_frame(const uint8_t *buf, size_t n) {
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (n >= sizeof(trf_status_t) && buf[0] == TRF_MSG_ST) {
        trf_status_t st;
        memcpy(&st, buf, sizeof st);
        s_rssi[0] = st.rssi0;
        s_rssi[1] = st.rssi1;
        push_bars(st.rssi0, st.rssi1, now);
    }
}

void fd_trf_buttons(const fwog_power_t *p, bool y_long, bool g_long,
                    bool gray_long, bool red_tap) {
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    const bool ydown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) != 0;
    const bool gdown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_GREEN)) != 0;
    const bool bdown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) != 0;
    const bool rydown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_GRAY)) != 0;
    (void)red_tap;
    s_power_armed = p->armed;

    if (ydown && !s_yel_was) {
        s_yel_ms = now;
        s_yel_hold = false;
    }
    if (ydown && !s_yel_hold && (now - s_yel_ms) >= TRF_HOLD_MS) {
        s_yel_hold = true;
        if (s_ui == TRF_UI_T9) fwog_t9_backspace(s_t9_buf);
    }
    if (!ydown && s_yel_was && !s_yel_hold && !y_long) {
        if (s_ui == TRF_UI_T9) {
            fwog_t9_letter_prev(s_t9_g, &s_t9_li);
        } else if (s_st == TRF_ST_RUN) {
            DIAG("mark,%u,%d,%d,%u,%u\n", (unsigned)s_pedo.steps,
                 (int)s_rssi[0], (int)s_rssi[1],
                 (unsigned)k_park[s_preset[0]].hz,
                 (unsigned)k_park[s_preset[1]].hz);
            send_log(TRF_LOG_MARK);
        }
    }
    s_yel_was = ydown;

    if (gdown && !s_grn_was) {
        s_grn_ms = now;
        s_grn_hold = false;
    }
    if (gdown && !s_grn_hold && (now - s_grn_ms) >= TRF_HOLD_MS) {
        s_grn_hold = true;
        if (s_ui == TRF_UI_T9) t9_finish();
    }
    if (!gdown && s_grn_was && !s_grn_hold && !g_long) {
        if (s_ui == TRF_UI_T9) {
            fwog_t9_insert(s_t9_buf, sizeof s_t9_buf,
                           fwog_t9_cur(s_t9_g, s_t9_li));
        } else if (s_st == TRF_ST_IDLE) {
            it_pedo_reset(&s_pedo);
            it_rest_reset(&s_rest);
            s_steps_shown = 0;
            s_st = TRF_ST_CAL;
            DIAG("# cal start title=%s\n", s_title[0] ? s_title : "-");
            send_log(TRF_LOG_START);
            s_chrome_dirty = true;
        } else if (s_st == TRF_ST_CAL) {
            s_st = TRF_ST_IDLE;
            DIAG("# cal cancel\n");
            send_log(TRF_LOG_STOP);
            s_chrome_dirty = true;
        } else {
            DIAG("# walk stop steps=%u\n", (unsigned)s_pedo.steps);
            send_log(TRF_LOG_STOP);
            s_st = TRF_ST_IDLE;
            s_chrome_dirty = true;
        }
    }
    s_grn_was = gdown;

    if (bdown && !s_blu_was) {
        s_blu_ms = now;
        s_blu_hold = false;
    }
    if (bdown && !s_blu_hold && (now - s_blu_ms) >= TRF_HOLD_MS) {
        s_blu_hold = true;
        if (s_ui != TRF_UI_T9 && s_st == TRF_ST_IDLE) t9_begin();
    }
    if (!bdown && s_blu_was && !s_blu_hold) {
        if (s_ui == TRF_UI_T9) {
            fwog_t9_letter_next(s_t9_g, &s_t9_li);
        } else {
            s_preset[1] = (s_preset[1] + 1) % TRF_NPARK;
            send_tune(1);
            DIAG("# bottom %s %u Hz\n", k_park[s_preset[1]].label,
                 (unsigned)k_park[s_preset[1]].hz);
        }
    }
    s_blu_was = bdown;

    if (rydown && !s_gry_was) {
        s_gry_ms = now;
        s_gry_hold = false;
    }
    if (rydown && !s_gry_hold && (now - s_gry_ms) >= TRF_HOLD_MS)
        s_gry_hold = true;
    if (!rydown && s_gry_was && !s_gry_hold && !gray_long) {
        if (s_ui == TRF_UI_T9) {
            fwog_t9_group_prev(&s_t9_g, &s_t9_li);
        } else {
            s_preset[0] = (s_preset[0] + 1) % TRF_NPARK;
            send_tune(0);
            DIAG("# top %s %u Hz\n", k_park[s_preset[0]].label,
                 (unsigned)k_park[s_preset[0]].hz);
        }
    }
    s_gry_was = rydown;

    if (p->buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_RED)) {
        if (s_ui == TRF_UI_T9) fwog_t9_group_next(&s_t9_g, &s_t9_li);
    }
}

void fd_trf_tick(uint32_t now) {
    if (s_power_armed && (s_st == TRF_ST_RUN || s_st == TRF_ST_CAL)) {
        if (!s_ship_noted) {
            DIAG("# ship-armed, flushing /trailrf\n");
            send_log(TRF_LOG_STOP);
            s_ship_noted = true;
        }
    } else {
        s_ship_noted = false;
    }

    if (fd_accel_ok() && (s_st == TRF_ST_CAL || s_st == TRF_ST_RUN)) {
        lis3dh_sample_t samp;
        if (take_sample(&samp)) {
            const int32_t ax = lis3dh_raw_to_mg(samp.x, LIS3DH_RANGE_2G);
            const int32_t ay = lis3dh_raw_to_mg(samp.y, LIS3DH_RANGE_2G);
            const int32_t az = lis3dh_raw_to_mg(samp.z, LIS3DH_RANGE_2G);
            if (s_st == TRF_ST_CAL) {
                it_pedo_cal_add(&s_pedo, it_mag_mg(ax, ay, az));
                it_rest_cal_add(&s_rest, ax, ay, az);
                if (it_rest_cal_ready(&s_rest)) {
                    it_pedo_cal_finish(&s_pedo);
                    it_rest_cal_finish(&s_rest);
                    it_pedo_arm_residual(&s_pedo, s_rest.dead_mg);
                    s_st = TRF_ST_RUN;
                    DIAG("# walk start rest=%d,%d,%d dead=%d thresh=%d\n",
                         (int)s_rest.x, (int)s_rest.y, (int)s_rest.z,
                         (int)s_rest.dead_mg, (int)s_pedo.thresh_mg);
                    s_chrome_dirty = true;
                }
            } else {
                int32_t rx, ry, rz;
                (void)it_rest_apply(&s_rest, ax, ay, az, &rx, &ry, &rz);
                if (it_pedo_feed_xyz(&s_pedo, rx, ry, rz, now)) {
                    s_steps_shown = s_pedo.steps;
                    DIAG("step,%u,%d,%d,%u,%u\n", (unsigned)s_pedo.steps,
                         (int)s_rssi[0], (int)s_rssi[1],
                         (unsigned)k_park[s_preset[0]].hz,
                         (unsigned)k_park[s_preset[1]].hz);
                    send_log(TRF_LOG_ROW);
                }
            }
        }
    }
}

void fd_trf_paint(void) {
    paint();
}
