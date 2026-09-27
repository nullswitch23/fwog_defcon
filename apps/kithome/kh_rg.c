/* KitHome RigGlass chrome. Host telemetry from main CDC. */
#include "kh_rg.h"
#include "rg_proto.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#define RG_COLS  25u
#define RG_SCALE 2u

static bool s_lcd, s_leds, s_chrome_dirty = true, s_st_ok;
static rg_status_t s_st;
static char s_host[20];
static char s_line[5][32];
static uint8_t s_drawn[5];

static void bar(uint16_t x, uint16_t y, uint8_t pct, uint16_t col, uint16_t bg,
                uint8_t *drawn) {
    if (*drawn == pct) return;
    unsigned w = (unsigned)pct * 3u;
    if (w > 300u) w = 300u;
    st7789_fill_rect(x, y, 300, 8, bg);
    if (w) st7789_fill_rect(x, y, (uint16_t)w, 8, col);
    *drawn = pct;
}

static void leds_load(uint8_t cpu) {
    if (!s_leds) return;
    const unsigned lit = cpu > 100u ? 7u : (unsigned)cpu * 7u / 100u;
    for (unsigned i = 0; i < FWOG_LED_COUNT; i++) {
        const bool on = i < lit;
        ws2812_set_color(i, on ? 8u : 2u, on ? 36u : 2u, on ? 8u : 4u);
    }
    ws2812_process();
}

static void fmt_pct3(char *dst, size_t n, const char *tag,
                     uint8_t a, uint8_t b, uint8_t c) {
    char sa[8], sb[8], sc[8];
    if (a == (uint8_t)RG_NA) snprintf(sa, sizeof sa, " --");
    else snprintf(sa, sizeof sa, "%3u%%", (unsigned)a);
    if (b == (uint8_t)RG_NA) snprintf(sb, sizeof sb, " --");
    else snprintf(sb, sizeof sb, "%3u%%", (unsigned)b);
    if (c == (uint8_t)RG_NA) snprintf(sc, sizeof sc, " --");
    else snprintf(sc, sizeof sc, "%3u%%", (unsigned)c);
    snprintf(dst, n, "%s %s %s %s", tag, sa, sb, sc);
}

static void fmt_tmp3(char *dst, size_t n, const char *tag,
                     uint8_t a, uint8_t b, uint8_t c) {
    char sa[8], sb[8], sc[8];
    if (a == (uint8_t)RG_NA) snprintf(sa, sizeof sa, " --");
    else snprintf(sa, sizeof sa, "%3uC", (unsigned)a);
    if (b == (uint8_t)RG_NA) snprintf(sb, sizeof sb, " --");
    else snprintf(sb, sizeof sb, "%3uC", (unsigned)b);
    if (c == (uint8_t)RG_NA) snprintf(sc, sizeof sc, " --");
    else snprintf(sc, sizeof sc, "%3uC", (unsigned)c);
    snprintf(dst, n, "%s %s %s %s", tag, sa, sb, sc);
}

static uint8_t bar_pct(uint8_t v) {
    if (v == (uint8_t)RG_NA) return 0;
    return v > 100u ? 100u : v;
}

void kh_rg_enter(void) {
    s_lcd = true;
    s_leds = true;
    s_st_ok = false;
    s_chrome_dirty = true;
    memset(&s_st, 0, sizeof s_st);
}

void kh_rg_leave(void) { }

void kh_rg_frame(const uint8_t *buf, size_t n) {
    if (n >= sizeof(rg_status_t) && buf && buf[0] == RG_MSG_ST) {
        memcpy(&s_st, buf, sizeof s_st);
        s_st.host[sizeof s_st.host - 1u] = '\0';
        s_st_ok = true;
    }
}

void kh_rg_leds(void) {
    leds_load(s_st_ok ? s_st.cpu : 0u);
}

void kh_rg_paint(void) {
    if (!s_lcd) return;
    const uint16_t bg  = st7789_rgb565(8, 10, 18);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    const uint16_t fg  = st7789_rgb565(230, 230, 230);
    const uint16_t yel = st7789_rgb565(255, 214, 48);
    const uint16_t blu = st7789_rgb565(60, 140, 220);
    const uint16_t mag = st7789_rgb565(180, 120, 220);
    const uint16_t org = st7789_rgb565(255, 140, 64);
    char line[32];

    if (s_chrome_dirty) {
        st7789_fill_rect(0, 0, 320, 36, bg);
        lcd_text_draw_padded(8, 8, "RigGlass", 8, RG_SCALE, acc, bg);
        lcd_text_draw_padded(8, 210, "now  2m  10m   YEL/GRN/GRY hold home", 40, 1, dim, bg);
        s_chrome_dirty = false;
        s_host[0] = '\0';
        memset(s_line, 0, sizeof s_line);
        memset(s_drawn, 0xFF, sizeof s_drawn);
    }

    if (s_st_ok && s_st.host[0]) {
        snprintf(line, sizeof line, "%s", s_st.host);
    } else {
        snprintf(line, sizeof line, "waiting");
    }
    lcd_text_draw_padded_changed(112, 8, line, 17, RG_SCALE, yel, bg,
                                 s_host, sizeof s_host);

    const rg_status_t *st = &s_st;
    const bool ok = s_st_ok;
    fmt_pct3(line, sizeof line, "CPU",
             ok ? st->cpu : 0, ok ? st->cpu_2m : 0, ok ? st->cpu_10m : 0);
    lcd_text_draw_padded_changed(8, 36, line, RG_COLS, RG_SCALE, fg, bg,
                                 s_line[0], sizeof s_line[0]);
    bar(8, 54, ok ? bar_pct(st->cpu) : 0u, acc, bg, &s_drawn[0]);

    fmt_pct3(line, sizeof line, "RAM",
             ok ? st->ram : 0, ok ? st->ram_2m : 0, ok ? st->ram_10m : 0);
    lcd_text_draw_padded_changed(8, 66, line, RG_COLS, RG_SCALE, fg, bg,
                                 s_line[1], sizeof s_line[1]);
    bar(8, 84, ok ? bar_pct(st->ram) : 0u, yel, bg, &s_drawn[1]);

    fmt_pct3(line, sizeof line, "NET",
             ok ? st->net : 0, ok ? st->net_2m : 0, ok ? st->net_10m : 0);
    lcd_text_draw_padded_changed(8, 96, line, RG_COLS, RG_SCALE, fg, bg,
                                 s_line[2], sizeof s_line[2]);
    bar(8, 114, ok ? bar_pct(st->net) : 0u, blu, bg, &s_drawn[2]);

    fmt_pct3(line, sizeof line, "GPU",
             ok ? st->gpu : (uint8_t)RG_NA,
             ok ? st->gpu_2m : (uint8_t)RG_NA,
             ok ? st->gpu_10m : (uint8_t)RG_NA);
    lcd_text_draw_padded_changed(8, 126, line, RG_COLS, RG_SCALE, fg, bg,
                                 s_line[3], sizeof s_line[3]);
    bar(8, 144, ok ? bar_pct(st->gpu) : 0u, mag, bg, &s_drawn[3]);

    fmt_tmp3(line, sizeof line, "TMP",
             ok ? st->tmp : (uint8_t)RG_NA,
             ok ? st->tmp_2m : (uint8_t)RG_NA,
             ok ? st->tmp_10m : (uint8_t)RG_NA);
    lcd_text_draw_padded_changed(8, 156, line, RG_COLS, RG_SCALE, fg, bg,
                                 s_line[4], sizeof s_line[4]);
    bar(8, 174, ok ? bar_pct(st->tmp) : 0u, org, bg, &s_drawn[4]);

    leds_load(ok ? st->cpu : 0u);
}
