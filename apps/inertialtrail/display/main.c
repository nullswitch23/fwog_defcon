/* InertialTrail — pedometer with a clock, plus a scribble that drifts.
 *
 * Green starts (calibrate still, then run) and stops. Blue flips mode.
 * Map: Yellow enters scroll, Gray/Red/Blue/Yellow pan, auto-zoom.
 * Live CSV on the display CDC while a take runs — not 1200 baud.
 */
#include "fwog_display.h"
#include "pedometer.h"
#include "it_proto.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

FWOG_POWER_DEFAULT();

#define IT_MODE_PEDO     0
#define IT_MODE_SCRIBBLE 1
#define IT_ST_IDLE       0
#define IT_ST_CAL        1
#define IT_ST_RUN        2
#define IT_UI_MAIN       0
#define IT_UI_T9         1
#define IT_HOLD_MS       750u
#define IT_TITLE_N       9

#define IT_STRIDE_MIN 40
#define IT_STRIDE_MAX 150
#define IT_STRIDE_DEF 70

#define IT_PLOT_X  8u
#define IT_PLOT_Y  56u
#define IT_PLOT_W  304u
#define IT_PLOT_H  104u

static bool     s_lcd;
static bool     s_leds;
static bool     s_accel;
static bool     s_link;
static int      s_mode = IT_MODE_PEDO;
static int      s_st = IT_ST_IDLE;
static int      s_stride_cm = IT_STRIDE_DEF;
static bool     s_pan;
static int32_t  s_off_x, s_off_y;
static int32_t  s_span = 32;
static uint32_t s_laps;
static uint32_t s_t0_ms;
static uint32_t s_ui_ms;
static bool     s_chrome_dirty = true;
static bool     s_live_dirty = true;
static bool     s_plot_dirty = true;
static bool     s_painted;
static bool     s_step_flash;
static it_pedo_t     s_pedo;
static it_rest_t     s_rest;
static it_scribble_t s_sc;
static int32_t  s_last_ax, s_last_ay, s_last_az;
static int32_t  s_last_mag;
static uint8_t  s_spark[32];
static uint8_t  s_drawn_spark[32];
static bool     s_spark_inited;
static unsigned s_spark_i;
static uint32_t s_spark_ms;
static uint8_t  s_spark_acc;
static bool     s_ship_noted;
static bool     s_power_armed;
static char     s_line_a[44];
static char     s_line_b[44];
static char     s_line_c[44];
static int      s_ui;
static char     s_title[IT_TITLE_N];
static char     s_t9_buf[IT_TITLE_N];
static int      s_t9_g, s_t9_li;
static uint32_t s_yel_ms, s_grn_ms, s_blu_ms, s_gry_ms;
static bool     s_yel_was, s_grn_was, s_blu_was, s_gry_was;
static bool     s_yel_hold, s_grn_hold, s_blu_hold, s_gry_hold;
static char     s_line_t9[24];
static char     s_line_grp[8];
static char     s_line_ch[4];

static int32_t mag_of(const lis3dh_sample_t *s) {
    const int32_t x = lis3dh_raw_to_mg(s->x, LIS3DH_RANGE_2G);
    const int32_t y = lis3dh_raw_to_mg(s->y, LIS3DH_RANGE_2G);
    const int32_t z = lis3dh_raw_to_mg(s->z, LIS3DH_RANGE_2G);
    return it_mag_mg(x, y, z);
}

static uint32_t dist_cm(void) {
    return s_pedo.steps * (uint32_t)s_stride_cm;
}

static void last_xy(int16_t *x, int16_t *y) {
    if (s_sc.n == 0u && !s_sc.wrapped) {
        *x = 0;
        *y = 0;
        return;
    }
    const unsigned idx = (s_sc.i + IT_SCRIBBLE_N - 1u) % IT_SCRIBBLE_N;
    *x = s_sc.x[idx];
    *y = s_sc.y[idx];
}

static void send_log(uint8_t cmd, uint32_t now) {
    it_log_t row;
    int16_t x = 0, y = 0;
    last_xy(&x, &y);
    memset(&row, 0, sizeof row);
    row.type = IT_MSG_LOG;
    row.cmd = cmd;
    row.mode = (uint8_t)s_mode;
    row.laps = (uint8_t)(s_laps > 255u ? 255u : s_laps);
    row.ms = now - s_t0_ms;
    row.steps = s_pedo.steps;
    row.dist_cm = dist_cm();
    row.mag = (int16_t)s_last_mag;
    row.x = x;
    row.y = y;
    memcpy(row.title, s_title, sizeof row.title);
    if (s_link) (void)fwog_link_uart_send_frame(&row, sizeof row);
}

static void t9_begin(void) {
    memset(s_t9_buf, 0, sizeof s_t9_buf);
    if (s_title[0]) strncpy(s_t9_buf, s_title, IT_TITLE_N - 1u);
    s_t9_g = 0;
    s_t9_li = 0;
    s_ui = IT_UI_T9;
    s_line_t9[0] = s_line_grp[0] = s_line_ch[0] = '\0';
    s_chrome_dirty = true;
    s_painted = false;
}

static void t9_finish(void) {
    memset(s_title, 0, sizeof s_title);
    strncpy(s_title, s_t9_buf, IT_TITLE_N - 1u);
    s_ui = IT_UI_MAIN;
    s_chrome_dirty = true;
    s_painted = false;
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

static void plot_px(int px, int py, unsigned w, uint16_t color) {
    if (px < (int)IT_PLOT_X || py < (int)IT_PLOT_Y) return;
    if (px >= (int)(IT_PLOT_X + IT_PLOT_W - (int)w)) return;
    if (py >= (int)(IT_PLOT_Y + IT_PLOT_H - (int)w)) return;
    st7789_fill_rect((uint16_t)px, (uint16_t)py, (uint16_t)w, (uint16_t)w, color);
}

static void plot_line(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        plot_px(x0, y0, 2u, color);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = err * 2;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void world_to_px(int32_t x, int32_t y, int32_t cam_x, int32_t cam_y,
                        int32_t span, int *px, int *py) {
    const int cx = (int)(IT_PLOT_X + IT_PLOT_W / 2u);
    const int cy = (int)(IT_PLOT_Y + IT_PLOT_H / 2u);
    const int inner_w = (int)IT_PLOT_W - 12;
    const int inner_h = (int)IT_PLOT_H - 12;
    *px = cx + (int)((x - cam_x) * inner_w / span);
    *py = cy - (int)((y - cam_y) * inner_h / span);
}

static void draw_plot(void) {
    if (!s_lcd || !s_plot_dirty) return;
    const uint16_t bg = st7789_rgb565(12, 16, 28);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t you = st7789_rgb565(230, 200, 70);
    const uint16_t origin = st7789_rgb565(70, 90, 120);
    const uint16_t frame = st7789_rgb565(50, 70, 90);
    st7789_fill_rect(IT_PLOT_X - 2u, IT_PLOT_Y - 2u,
                     IT_PLOT_W + 4u, IT_PLOT_H + 4u, frame);
    st7789_fill_rect(IT_PLOT_X, IT_PLOT_Y, IT_PLOT_W, IT_PLOT_H, bg);

    const unsigned n = s_sc.wrapped ? IT_SCRIBBLE_N : s_sc.n;
    int16_t lx = 0, ly = 0;
    last_xy(&lx, &ly);

    int32_t reach = 100;
    unsigned start = 0;
    if (n > 0u) {
        start = s_sc.wrapped ? s_sc.i : 0u;
        for (unsigned k = 0; k < n; k++) {
            const unsigned idx = (start + k) % IT_SCRIBBLE_N;
            int32_t ax = s_sc.x[idx];
            int32_t ay = s_sc.y[idx];
            if (ax < 0) ax = -ax;
            if (ay < 0) ay = -ay;
            if (ax > reach) reach = ax;
            if (ay > reach) reach = ay;
        }
    }
    int32_t span = reach * 2 + 40;
    if (s_pan) {
        span = span / 2;
        if (span < 80) span = 80;
    }
    s_span = span;

    int32_t cam_x = s_pan ? ((int32_t)lx + s_off_x) : 0;
    int32_t cam_y = s_pan ? ((int32_t)ly + s_off_y) : 0;

    int ox, oy;
    world_to_px(0, 0, cam_x, cam_y, span, &ox, &oy);
    plot_px(ox - 3, oy, 7u, origin);
    plot_px(ox, oy - 3, 7u, origin);

    if (n > 1u) {
        for (unsigned k = 1; k < n; k++) {
            const unsigned a = (start + k - 1u) % IT_SCRIBBLE_N;
            const unsigned b = (start + k) % IT_SCRIBBLE_N;
            int x0, y0, x1, y1;
            world_to_px(s_sc.x[a], s_sc.y[a], cam_x, cam_y, span, &x0, &y0);
            world_to_px(s_sc.x[b], s_sc.y[b], cam_x, cam_y, span, &x1, &y1);
            plot_line(x0, y0, x1, y1, acc);
        }
    }

    int px, py;
    world_to_px(lx, ly, cam_x, cam_y, span, &px, &py);
    plot_px(px - 2, py - 2, 5u, you);
    s_plot_dirty = false;
}

static void csv_header(void) {
    DIAG("# inertialtrail v008 mode=%s stride_cm=%d rest=%d,%d,%d dead=%d thresh=%d title=%s\n",
         s_mode == IT_MODE_PEDO ? "pedo" : "scribble",
         s_stride_cm, (int)s_rest.x, (int)s_rest.y, (int)s_rest.z,
         (int)s_rest.dead_mg, (int)s_pedo.thresh_mg,
         s_title[0] ? s_title : "-");
#ifndef HOST_TEST
    mcp7940_time_t t;
    if (mcp7940_read_time(&t)) {
        DIAG("# rtc 20%02u-%02u-%02u %02u:%02u:%02u osc=%u\n",
             (unsigned)t.year, (unsigned)t.month, (unsigned)t.date,
             (unsigned)t.hour, (unsigned)t.min, (unsigned)t.sec,
             (unsigned)mcp7940_is_oscillating(&t));
    }
#endif
    DIAG("kind,ms,steps,dist_cm,mag,laps,x,y\n");
}

static void leds_idle(void) {
    if (!s_leds || s_power_armed) return;
    const uint8_t g = (s_mode == IT_MODE_PEDO) ? 18 : 8;
    const uint8_t b = (s_mode == IT_MODE_SCRIBBLE) ? 22 : 4;
    for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++) {
        ws2812_set_color(i, 2, g, b);
    }
    ws2812_process();
}

static void leds_step(void) {
    if (!s_leds || s_power_armed) return;
    for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++) {
        ws2812_set_color(i, 4, 40, 8);
    }
    ws2812_process();
    s_step_flash = true;
}

static void draw_spark(void) {
    if (!s_lcd) return;
    const uint16_t bg = st7789_rgb565(8, 10, 18);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t x0 = 8, y0 = 140, bh = 28, bw = 7;
    if (!s_spark_inited) {
        st7789_fill_rect(x0, y0, 304, bh, bg);
        memset(s_drawn_spark, 0xFF, sizeof s_drawn_spark);
        s_spark_inited = true;
    }
    for (unsigned i = 0; i < 32u; i++) {
        const uint8_t v = (uint8_t)(s_spark[i] > 12u ? 12u : s_spark[i]);
        if (s_drawn_spark[i] == v) continue;
        const uint16_t x = (uint16_t)(x0 + i * 9u);
        st7789_fill_rect(x, y0, bw, bh, bg);
        if (v > 0u) {
            st7789_fill_rect(x, (uint16_t)(168u - (unsigned)v * 2u),
                             bw, (uint16_t)((unsigned)v * 2u), acc);
        }
        s_drawn_spark[i] = v;
    }
}

static void draw(void) {
    if (!s_lcd) return;
    if (s_ui == IT_UI_T9) {
        const uint16_t bg = st7789_rgb565(8, 10, 18);
        const uint16_t fg = st7789_rgb565(230, 230, 230);
        const uint16_t acc = st7789_rgb565(80, 200, 120);
        const uint16_t dim = st7789_rgb565(140, 150, 170);
        const uint16_t hdr = st7789_rgb565(16, 28, 40);
        if (!s_painted || s_chrome_dirty) {
            st7789_clear(bg);
            st7789_fill_rect(0, 0, ST7789_W, 36, hdr);
            lcd_text_draw_padded(8, 10, "InertialTrail", 16, 2, acc, hdr);
            s_painted = true;
            s_chrome_dirty = false;
            s_line_t9[0] = s_line_grp[0] = s_line_ch[0] = '\0';
        }
        paint_t9(bg, acc, fg, dim);
        return;
    }
    if (!s_chrome_dirty && !s_live_dirty) return;
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
        s_chrome_dirty = true;
    }

    if (s_chrome_dirty) {
        st7789_fill_rect(0, 0, ST7789_W, 36, hdr);
        lcd_text_draw_padded(8, 10, "InertialTrail", 16, 2, acc, hdr);
        st7789_fill_rect(0, 36, ST7789_W, 204, bg);
        if (s_pan) {
            lcd_text_draw_padded(8, 188, "GRAY/RED/YELLOW/BLUE  pan", 40, 1, acc, bg);
            lcd_text_draw_padded(8, 204, "GREEN  leave scroll", 40, 1, dim, bg);
        } else if (s_mode == IT_MODE_SCRIBBLE && s_st == IT_ST_RUN) {
            lcd_text_draw_padded(8, 188, "YELLOW  pan map   GREEN stop", 40, 1, dim, bg);
            lcd_text_draw_padded(8, 204, "up = forward; yellow follows steps", 40, 1, dim, bg);
        } else {
            lcd_text_draw_padded(8, 188, "GRAY/RED  stride", 40, 1, dim, bg);
            lcd_text_draw_padded(8, 204, "BLUE mode   GREEN start/stop", 40, 1, dim, bg);
        }
        lcd_text_draw_padded(8, 220, "Not GPS. Pedometer + a map.", 40, 1, warn, bg);
        if (s_st == IT_ST_IDLE) {
            lcd_text_draw_padded(8, 48, "GREEN  start (hold still 1.2 s)", 40, 1, fg, bg);
            if (s_mode == IT_MODE_PEDO) {
                snprintf(line, sizeof line, "stride %d cm   last %u steps",
                         s_stride_cm, (unsigned)s_pedo.steps);
            } else {
                snprintf(line, sizeof line, "map   one stride per step   (no gyro)");
            }
            lcd_text_draw_padded(8, 64, line, 40, 1, dim, bg);
            lcd_text_draw_padded(8, 80, "BLUE mode   GRAY/RED stride", 40, 1, dim, bg);
            if (s_title[0]) {
                snprintf(line, sizeof line, "walk [%s]   GRAY hold T9", s_title);
            } else {
                snprintf(line, sizeof line, "walk WALK nnnn   GRAY hold T9");
            }
            lcd_text_draw_padded(8, 96, line, 40, 1, dim, bg);
        } else if (s_st == IT_ST_CAL) {
            lcd_text_draw_padded(8, 80, "dead zone tracks while still", 40, 1, dim, bg);
        } else if (s_mode == IT_MODE_PEDO) {
            lcd_text_draw_padded(8, 112, "YELLOW lap     GREEN stop", 40, 1, dim, bg);
        }
        s_line_a[0] = s_line_b[0] = s_line_c[0] = '\0';
        s_spark_inited = false;
        s_chrome_dirty = false;
        s_plot_dirty = true;
        s_live_dirty = true;
    }

    if (!s_live_dirty) return;

    if (s_st == IT_ST_CAL) {
        snprintf(line, sizeof line, "hold still  %u / %u",
                 s_pedo.mag_n, (unsigned)IT_CAL_SAMPLES);
        lcd_text_draw_padded_changed(8, 48, line, 40, 2, warn, bg,
                                     s_line_a, sizeof s_line_a);
    } else if (s_st == IT_ST_RUN && s_mode == IT_MODE_PEDO) {
        snprintf(line, sizeof line, "%u", (unsigned)s_pedo.steps);
        lcd_text_draw_padded_changed(8, 44, line, 12, 3, fg, bg,
                                     s_line_a, sizeof s_line_a);
        snprintf(line, sizeof line, "%u.%02u m   stride %d cm",
                 (unsigned)(dist_cm() / 100u),
                 (unsigned)(dist_cm() % 100u),
                 s_stride_cm);
        lcd_text_draw_padded_changed(8, 80, line, 40, 1, acc, bg,
                                     s_line_b, sizeof s_line_b);
        const uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - s_t0_ms;
        snprintf(line, sizeof line, "%u s   laps %u   mag %d",
                 (unsigned)(elapsed / 1000u), (unsigned)s_laps,
                 (int)s_last_mag);
        lcd_text_draw_padded_changed(8, 96, line, 40, 1, dim, bg,
                                     s_line_c, sizeof s_line_c);
        draw_spark();
    } else if (s_st == IT_ST_RUN) {
        snprintf(line, sizeof line, "%s  %u st  %u s",
                 s_pan ? "SCROLL" : "MAP",
                 (unsigned)s_pedo.steps,
                 (unsigned)((to_ms_since_boot(get_absolute_time()) - s_t0_ms) / 1000u));
        lcd_text_draw_padded_changed(8, 40, line, 40, 1, warn, bg,
                                     s_line_a, sizeof s_line_a);
        snprintf(line, sizeof line, "X%+5d Y%+5d Z%+5d",
                 (int)s_last_ax, (int)s_last_ay, (int)s_last_az);
        lcd_text_draw_padded_changed(8, 164, line, 40, 1, dim, bg,
                                     s_line_b, sizeof s_line_b);
        draw_plot();
    }
    s_live_dirty = false;
}

static bool take_sample(lis3dh_sample_t *out) {
    lis3dh_sample_t samp;
    samp.x = samp.y = samp.z = 0x7FFF;
    if (!lis3dh_process(LIS3DH_MOVE_THRESHOLD_DEFAULT, &samp, NULL)) {
        return false;
    }
    if (samp.x == 0x7FFF && samp.y == 0x7FFF && samp.z == 0x7FFF) {
        return false;
    }
    *out = samp;
    return true;
}

static void begin_cal(void) {
    it_pedo_reset(&s_pedo);
    it_rest_reset(&s_rest);
    it_scribble_reset(&s_sc);
    s_laps = 0;
    s_spark_i = 0;
    s_spark_acc = 0;
    memset(s_spark, 0, sizeof s_spark);
    s_pan = false;
    s_off_x = s_off_y = 0;
    s_plot_dirty = true;
    s_st = IT_ST_CAL;
    s_chrome_dirty = true;
    s_painted = false;
    DIAG("# cal start\n");
}

static void begin_run(uint32_t now) {
    it_pedo_cal_finish(&s_pedo);
    it_rest_cal_finish(&s_rest);
    it_pedo_arm_residual(&s_pedo, s_rest.dead_mg);
    it_scribble_push_point(&s_sc);
    s_t0_ms = now;
    s_spark_ms = now;
    s_st = IT_ST_RUN;
    s_chrome_dirty = true;
    s_plot_dirty = true;
    s_painted = false;
    csv_header();
    send_log(IT_LOG_START, now);
}

static void stop_run(uint32_t now) {
    DIAG("# stop ms=%u steps=%u dist_cm=%u laps=%u\n",
         (unsigned)(now - s_t0_ms), (unsigned)s_pedo.steps,
         (unsigned)dist_cm(), (unsigned)s_laps);
    send_log(IT_LOG_STOP, now);
    s_st = IT_ST_IDLE;
    s_pan = false;
    s_off_x = s_off_y = 0;
    s_chrome_dirty = true;
    s_painted = false; /* wipe plot leftovers */
    leds_idle();
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
        s_chrome_dirty = true;
    } else {
        DIAG("[trail] LCD init failed\n");
    }
}

int main(void) {
    board_init();
    fwog_splash_bind("InertialTrail", "008");
    s_leds = ws2812_init(pio0, 0u);
    lis3dh_init();
    s_accel = lis3dh_configure(LIS3DH_RANGE_2G);
    lcd_bringup();
    leds_idle();
    s_link = fwog_link_uart_init(FWOG_LINK_BAUD);
    DIAG("[trail] accel=%s lcd=%s link=%s  (CSV CDC + /trail; not 1200 baud)\n",
         s_accel ? "ok" : "FAIL", s_lcd ? "ok" : "FAIL",
         s_link ? "ok" : "FAIL");

    it_pedo_reset(&s_pedo);

    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        const fwog_power_t p = fwog_power_poll(now);
        s_power_armed = p.armed;

        if (p.armed && s_st == IT_ST_RUN) {
            if (!s_ship_noted) {
                DIAG("# ship-armed, flushing /trail\n");
                send_log(IT_LOG_STOP, now);
                s_ship_noted = true;
            }
        } else {
            s_ship_noted = false;
        }

        const bool ydown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) != 0;
        const bool gdown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GREEN)) != 0;
        const bool bdown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) != 0;
        const bool rydown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GRAY)) != 0;

        if (ydown && !s_yel_was) {
            s_yel_ms = now;
            s_yel_hold = false;
        }
        if (ydown && !s_yel_hold && (now - s_yel_ms) >= IT_HOLD_MS) {
            s_yel_hold = true;
            if (s_ui == IT_UI_T9) fwog_t9_backspace(s_t9_buf);
        }
        if (!ydown && s_yel_was && !s_yel_hold) {
            if (s_ui == IT_UI_T9) {
                fwog_t9_letter_prev(s_t9_g, &s_t9_li);
            } else if (s_st == IT_ST_RUN && s_mode == IT_MODE_SCRIBBLE) {
                if (!s_pan) {
                    s_pan = true;
                    s_off_x = s_off_y = 0;
                } else {
                    s_off_x -= (s_span > 24) ? (s_span / 6) : 4;
                }
                s_plot_dirty = true;
                s_chrome_dirty = true;
            } else if (s_st == IT_ST_RUN && s_mode == IT_MODE_PEDO) {
                s_laps++;
                DIAG("mark,%u,%u,%u,%d,%u\n",
                     (unsigned)(now - s_t0_ms),
                     (unsigned)s_pedo.steps,
                     (unsigned)dist_cm(), (int)s_last_mag,
                     (unsigned)s_laps);
                s_live_dirty = true;
            }
        }
        s_yel_was = ydown;

        if (gdown && !s_grn_was) {
            s_grn_ms = now;
            s_grn_hold = false;
        }
        if (gdown && !s_grn_hold && (now - s_grn_ms) >= IT_HOLD_MS) {
            s_grn_hold = true;
            if (s_ui == IT_UI_T9) t9_finish();
        }
        if (!gdown && s_grn_was && !s_grn_hold) {
            if (s_ui == IT_UI_T9) {
                fwog_t9_insert(s_t9_buf, sizeof s_t9_buf,
                               fwog_t9_cur(s_t9_g, s_t9_li));
            } else if (s_pan) {
                s_pan = false;
                s_off_x = s_off_y = 0;
                s_plot_dirty = true;
                s_chrome_dirty = true;
            } else if (s_st == IT_ST_IDLE) {
                begin_cal();
            } else if (s_st == IT_ST_CAL) {
                s_st = IT_ST_IDLE;
                s_chrome_dirty = true;
                s_painted = false;
                DIAG("# cal cancel\n");
            } else {
                stop_run(now);
            }
        }
        s_grn_was = gdown;

        if (bdown && !s_blu_was) {
            s_blu_ms = now;
            s_blu_hold = false;
        }
        if (bdown && !s_blu_hold && (now - s_blu_ms) >= IT_HOLD_MS) {
            s_blu_hold = true;
        }
        if (!bdown && s_blu_was && !s_blu_hold) {
            if (s_ui == IT_UI_T9) {
                fwog_t9_letter_next(s_t9_g, &s_t9_li);
            } else if (s_pan) {
                s_off_x += (s_span > 24) ? (s_span / 6) : 4;
                s_plot_dirty = true;
                s_chrome_dirty = true;
            } else if (s_st == IT_ST_IDLE) {
                s_mode = (s_mode == IT_MODE_PEDO) ? IT_MODE_SCRIBBLE
                                                  : IT_MODE_PEDO;
                s_chrome_dirty = true;
                if (!p.armed) leds_idle();
            }
        }
        s_blu_was = bdown;

        if (rydown && !s_gry_was) {
            s_gry_ms = now;
            s_gry_hold = false;
        }
        if (rydown && !s_gry_hold && (now - s_gry_ms) >= IT_HOLD_MS) {
            s_gry_hold = true;
            if (s_ui != IT_UI_T9 && s_st == IT_ST_IDLE) t9_begin();
        }
        if (!rydown && s_gry_was && !s_gry_hold) {
            if (s_ui == IT_UI_T9) {
                fwog_t9_group_prev(&s_t9_g, &s_t9_li);
            } else if (s_pan) {
                s_off_y += (s_span > 24) ? (s_span / 6) : 4;
                s_plot_dirty = true;
                s_chrome_dirty = true;
            } else if (s_mode == IT_MODE_PEDO) {
                s_stride_cm -= 5;
                if (s_stride_cm < IT_STRIDE_MIN) s_stride_cm = IT_STRIDE_MIN;
                s_chrome_dirty = true;
            }
        }
        s_gry_was = rydown;

        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_RED)) {
            if (s_ui == IT_UI_T9) {
                fwog_t9_group_next(&s_t9_g, &s_t9_li);
            } else if (s_pan) {
                s_off_y -= (s_span > 24) ? (s_span / 6) : 4;
                s_plot_dirty = true;
                s_chrome_dirty = true;
            } else if (s_mode == IT_MODE_PEDO) {
                s_stride_cm += 5;
                if (s_stride_cm > IT_STRIDE_MAX) s_stride_cm = IT_STRIDE_MAX;
                s_chrome_dirty = true;
            }
        }

        if (s_accel && (s_st == IT_ST_CAL || s_st == IT_ST_RUN)) {
            lis3dh_sample_t samp;
            if (take_sample(&samp)) {
                const int32_t mag = mag_of(&samp);
                const int32_t ax = lis3dh_raw_to_mg(samp.x, LIS3DH_RANGE_2G);
                const int32_t ay = lis3dh_raw_to_mg(samp.y, LIS3DH_RANGE_2G);
                const int32_t az = lis3dh_raw_to_mg(samp.z, LIS3DH_RANGE_2G);
                s_last_mag = mag;
                s_last_ax = ax;
                s_last_ay = ay;
                s_last_az = az;

                if (s_st == IT_ST_CAL) {
                    it_pedo_cal_add(&s_pedo, mag);
                    it_rest_cal_add(&s_rest, ax, ay, az);
                    s_live_dirty = true;
                    if (it_rest_cal_ready(&s_rest)) begin_run(now);
                } else {
                    int32_t rx, ry, rz;
                    (void)it_rest_apply(&s_rest, ax, ay, az, &rx, &ry, &rz);
                    if (it_pedo_feed_xyz(&s_pedo, rx, ry, rz, now)) {
                        leds_step();
                        s_spark_acc++;
                        if (s_mode == IT_MODE_SCRIBBLE) {
                            it_scribble_step(&s_sc, s_pedo.peak_ax, s_pedo.peak_ay,
                                             s_stride_cm);
                            it_scribble_push_point(&s_sc);
                            s_plot_dirty = true;
                        }
                        DIAG("step,%u,%u,%u,%d,%u\n",
                             (unsigned)(now - s_t0_ms),
                             (unsigned)s_pedo.steps,
                             (unsigned)dist_cm(), (int)mag,
                             (unsigned)s_laps);
                        send_log(IT_LOG_ROW, now);
                        s_live_dirty = true;
                    }
                }
            }
        }

        if (s_st == IT_ST_RUN && s_mode == IT_MODE_PEDO &&
            now - s_spark_ms >= 1000u) {
            s_spark[s_spark_i] = s_spark_acc;
            s_spark_acc = 0;
            s_spark_i = (s_spark_i + 1u) % 32u;
            s_spark_ms = now;
            s_live_dirty = true;
        }

        if (s_step_flash && !s_power_armed && s_st == IT_ST_RUN &&
            (now - s_pedo.last_step_ms) > 80u) {
            leds_idle();
            s_step_flash = false;
        }

        if (now - s_ui_ms >= 80u) {
            s_ui_ms = now;
            if (s_st == IT_ST_RUN && s_mode == IT_MODE_SCRIBBLE) {
                s_live_dirty = true;
            }
            draw();
        }
        sleep_ms(2);
    }
}
