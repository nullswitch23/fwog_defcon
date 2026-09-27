#include "pd_qg.h"
#include "pd_link.h"
#include "qg_nick.h"
#include "qg_proto.h"
#include "lcd/fwog_t9.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

#define QG_HIST     40u
#define QG_UI_BENCH 0
#define QG_UI_T9    1

static bool s_chrome_dirty = true, s_st_ok;
static qg_status_t s_st;
static char s_line[QG_MAX][44];
static char s_hint[44];
static char s_foot[44];
static int16_t s_hist[QG_HIST];
static unsigned s_hist_n;
static uint8_t s_drawn[QG_HIST];
static int s_ui;
static int s_sel;
static char s_t9_buf[QG_NAME_N];
static int s_t9_g, s_t9_li;
static uint8_t s_t9_addr;
static char s_line_t9[24];
static char s_line_grp[8];
static char s_line_ch[4];

static const char *row_name(const qg_chan_t *c) {
    const char *n;
    if (!c) return "--";
    if (c->known) return c->name;
    n = qg_nick_get(c->addr);
    if (n && n[0]) return n;
    return c->name;
}

static bool row_unknown(void) {
    if (!s_st_ok || !s_st.n) return false;
    if (s_sel < 0 || (unsigned)s_sel >= s_st.n) return false;
    return s_st.chan[s_sel].known == 0u;
}

static void clamp_sel(void) {
    if (!s_st_ok || !s_st.n) {
        s_sel = 0;
        return;
    }
    if (s_sel < 0) s_sel = 0;
    if ((unsigned)s_sel >= s_st.n) s_sel = (int)s_st.n - 1;
}

static void t9_begin(uint8_t addr, const char *seed) {
    s_t9_addr = addr;
    memset(s_t9_buf, 0, sizeof s_t9_buf);
    if (seed && seed[0]) strncpy(s_t9_buf, seed, QG_NAME_N - 1u);
    s_t9_g = 0;
    s_t9_li = 0;
    s_ui = QG_UI_T9;
    s_line_t9[0] = s_line_grp[0] = s_line_ch[0] = '\0';
    s_chrome_dirty = true;
}

static void t9_finish(void) {
    qg_nick_set(s_t9_addr, s_t9_buf);
    s_ui = QG_UI_BENCH;
    s_chrome_dirty = true;
    memset(s_drawn, 0xFF, sizeof s_drawn);
}

static uint32_t s_blu_ms;
static bool s_blu_was, s_blu_hold;

void pd_qg_enter(void) {
    s_ui = QG_UI_BENCH;
    s_st_ok = false;
    s_hist_n = 0;
    s_sel = 0;
    s_chrome_dirty = true;
    memset(s_drawn, 0xFF, sizeof s_drawn);
    if (pd_link_ok()) {
        qg_cmd_t m = { .type = QG_MSG_CMD, .cmd = QG_CMD_SCAN };
        pd_link_send(&m, sizeof m);
    }
}

void pd_qg_frame(const uint8_t *buf, size_t n) {
    if (n >= sizeof(qg_status_t) && buf[0] == QG_MSG_ST) {
        memcpy(&s_st, buf, sizeof s_st);
        s_st_ok = true;
        clamp_sel();
        if (s_st.n) {
            if (s_hist_n < QG_HIST) {
                s_hist[s_hist_n++] = s_st.chan[0].plot;
            } else {
                memmove(s_hist, s_hist + 1, (QG_HIST - 1u) * sizeof s_hist[0]);
                s_hist[QG_HIST - 1u] = s_st.chan[0].plot;
            }
        }
    }
}

void pd_qg_buttons(const fwog_power_t *p, bool y_long, bool g_long,
                   bool gray_long, bool red_tap) {
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    const bool bdown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) != 0;

    if (bdown && !s_blu_was) {
        s_blu_ms = now;
        s_blu_hold = false;
    }
    if (bdown && !s_blu_hold && (now - s_blu_ms) >= 700u) {
        s_blu_hold = true;
        if (s_ui == QG_UI_T9) t9_finish();
    }
    s_blu_was = bdown;

    if (s_ui == QG_UI_T9) {
        if ((p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) && !y_long)
            fwog_t9_letter_prev(s_t9_g, &s_t9_li);
        if (!bdown && (p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_BLUE)) &&
            !s_blu_hold)
            fwog_t9_letter_next(s_t9_g, &s_t9_li);
        if ((p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_GRAY)) && !gray_long)
            fwog_t9_group_prev(&s_t9_g, &s_t9_li);
        if (red_tap && (p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_RED)))
            fwog_t9_group_next(&s_t9_g, &s_t9_li);
        if ((p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_GREEN)) && !g_long) {
            fwog_t9_insert(s_t9_buf, sizeof s_t9_buf,
                           fwog_t9_cur(s_t9_g, s_t9_li));
        }
        return;
    }
    if ((p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_GREEN)) && !g_long) {
        if (pd_link_ok()) {
            qg_cmd_t m = { .type = QG_MSG_CMD, .cmd = QG_CMD_SCAN };
            pd_link_send(&m, sizeof m);
        }
    }
    if ((p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_GRAY)) && !gray_long) {
        if (s_st_ok && s_st.n) {
            if (--s_sel < 0) s_sel = (int)s_st.n - 1;
        }
    }
    if (red_tap && (p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_RED))) {
        if (s_st_ok && s_st.n) s_sel = (s_sel + 1) % (int)s_st.n;
    }
    if (!bdown && (p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_BLUE)) &&
        !s_blu_hold && row_unknown()) {
        const qg_chan_t *c = &s_st.chan[s_sel];
        t9_begin(c->addr, qg_nick_get(c->addr));
    }
}

static void paint_t9(uint16_t bg, uint16_t acc, uint16_t fg, uint16_t dim) {
    char line[24];
    char ch[2];
    lcd_text_draw_padded(8, 44, "nick  RAM", 16, 1, dim, bg);
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
    lcd_text_draw_padded(8, 192, "GRN tap add  hold home", 40, 1, dim, bg);
    lcd_text_draw_padded(8, 208, "BLU hold save nick", 40, 1, dim, bg);
}

void pd_qg_paint(void) {
    const uint16_t bg  = st7789_rgb565(8, 10, 18);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    const uint16_t fg  = st7789_rgb565(230, 230, 230);
    const uint16_t yel = st7789_rgb565(255, 214, 48);
    const uint16_t bar = st7789_rgb565(60, 180, 220);
    char line[44];
    unsigned i;

    if (s_chrome_dirty) {
        st7789_fill_rect(0, 0, 320, 240, bg);
        lcd_text_draw_padded(8, 8, "QwiicBench", 16, 2, acc, bg);
        s_chrome_dirty = false;
        memset(s_line, 0, sizeof s_line);
        s_hint[0] = s_foot[0] = '\0';
        memset(s_drawn, 0xFF, sizeof s_drawn);
        s_line_t9[0] = s_line_grp[0] = s_line_ch[0] = '\0';
    }

    if (s_ui == QG_UI_T9) {
        paint_t9(bg, acc, fg, dim);
        return;
    }

    clamp_sel();
    snprintf(line, sizeof line, "%s  %u on bus",
             (s_st_ok && s_st.io_ok) ? "breakout I2C" : "io wait",
             s_st_ok ? (unsigned)s_st.n : 0u);
    lcd_text_draw_padded_changed(8, 48, line, 40, 1, dim, bg, s_hint, sizeof s_hint);

    for (i = 0; i < QG_MAX; i++) {
        const bool sel = (s_st_ok && s_st.n && (int)i == s_sel);
        if (s_st_ok && i < s_st.n) {
            snprintf(line, sizeof line, "%c%s  %s",
                     sel ? '>' : ' ',
                     row_name(&s_st.chan[i]), s_st.chan[i].val);
        } else {
            snprintf(line, sizeof line, "--");
        }
        lcd_text_draw_padded_changed(8, (uint16_t)(68u + i * 18u), line, 40, 1,
                                     sel ? yel : fg, bg,
                                     s_line[i], sizeof s_line[i]);
    }

    {
        const uint16_t x0 = 8, y0 = 148, bh = 50;
        if (s_hist_n) {
            int16_t mn = s_hist[0], mx = s_hist[0];
            for (i = 1; i < s_hist_n; i++) {
                if (s_hist[i] < mn) mn = s_hist[i];
                if (s_hist[i] > mx) mx = s_hist[i];
            }
            if (mx == mn) mx = (int16_t)(mn + 1);
            for (i = 0; i < QG_HIST; i++) {
                uint8_t h = 0;
                if (i < s_hist_n) {
                    int v = (int)(s_hist[i] - mn) * (int)bh / (int)(mx - mn);
                    if (v < 0) v = 0;
                    if (v > (int)bh) v = (int)bh;
                    h = (uint8_t)v;
                }
                if (s_drawn[i] == h) continue;
                {
                    const uint16_t x = (uint16_t)(x0 + i * 7u);
                    st7789_fill_rect(x, y0, 6, bh, bg);
                    if (h) st7789_fill_rect(x, (uint16_t)(y0 + bh - h), 6, h, bar);
                    s_drawn[i] = h;
                }
            }
        }
    }
    {
        const char *hint = "GRN rescan  BLU nick  3.3V";
        if (row_unknown()) hint = "GRN rescan  BLU T9 nick  3.3V";
        else if (s_st_ok && s_st.n > 1u) hint = "GRN rescan  GRY/RED row  3.3V";
        lcd_text_draw_padded_changed(8, 210, hint, 40, 1, dim, bg,
                                     s_foot, sizeof s_foot);
    }
}
