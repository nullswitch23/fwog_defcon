/* KitHome TalkClip. YEL/GRN/GRY hold = HOME.
 * Remap: gray-hold T9 -> BLUE hold. Hang cycle is BLUE tap (was BLUE hold).
 * T9 commit was green hold -> BLUE hold. T9 backspace lost (YEL hold is home). */
#include "kh_tc.h"
#include "kh_link.h"
#include "tc_proto.h"
#include "lcd/fwog_t9.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

#define TC_GAIN    8     /* CIC output is quiet; conversation needs a boost */
#define TC_HOLD_MS 750u
#define TC_STEM_N  9u    /* 8.3 stem + NUL */

static const uint32_t k_hang_ms[] = { 0u, 2000u, 10000u, 60000u };
#define TC_HANG_N ((unsigned)(sizeof k_hang_ms / sizeof k_hang_ms[0]))

static bool     s_lcd, s_pdm, s_leds;
static bool     s_armed;
static bool     s_rec;
static bool     s_force;
static bool     s_t9;
static int16_t  s_thresh = 400;
static unsigned s_hang_i = 1u; /* default 2 s of sub-threshold tail */
static uint32_t s_rms;
static uint32_t s_quiet_since;
static uint32_t s_blu_ms, s_gry_ms, s_grn_ms, s_yel_ms;
static bool     s_blu_was, s_blu_hold;
static bool     s_gry_was, s_gry_hold;
static bool     s_grn_was, s_grn_hold;
static bool     s_yel_was, s_yel_hold;
static uint32_t s_samples;
static uint32_t s_clips;
static uint32_t s_seq;
static bool     s_chrome_dirty = true;
static bool     s_power_armed;
static char     s_line_st[16];
static char     s_line_rms[44];
static char     s_line_path[44];
static char     s_line_t9[16];
static char     s_line_grp[8];
static char     s_line_ch[4];
static char     s_last_path[TC_PATH_LEN];
static char     s_t9_buf[TC_STEM_N];
static int      s_t9_g, s_t9_li;
static uint16_t s_drawn_vu = 0xFFFFu;
static uint16_t s_drawn_tline = 0xFFFFu;
static int16_t  s_pcm[PDM_SAMPLE_BUFFER_SIZE];
static fwog_cic_t s_cic;

static void leds_rec(void) {
    if (!s_leds || s_power_armed) return;
    const uint8_t r = s_rec ? 50u : (s_armed ? 12u : 2u);
    for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++) {
        ws2812_set_color(i, r, s_armed ? 8u : 2u, 2u);
    }
    ws2812_process();
}

static void send_pcm(uint8_t cmd, const int16_t *pcm, uint16_t n) {
    tc_pcm_t m;
    memset(&m, 0, sizeof m);
    m.type = TC_MSG_CMD;
    m.cmd = cmd;
    m.n = n;
    m.seq = s_seq++;
    if (pcm && n > 0u) {
        if (n > TC_PCM_N) n = TC_PCM_N;
        memcpy(m.pcm, pcm, (size_t)n * sizeof(int16_t));
        m.n = n;
    }
    if (kh_link_ok()) (void)kh_link_send(&m, sizeof m);
}

static void send_rename(void) {
    tc_rename_t m;
    memset(&m, 0, sizeof m);
    m.type = TC_MSG_CMD;
    m.cmd = TC_CMD_RENAME;
    strncpy(m.from, s_last_path, TC_PATH_LEN - 1u);
    strncpy(m.name, s_t9_buf, 8u);
    if (kh_link_ok()) (void)kh_link_send(&m, sizeof m);
}

static void rec_open(void) {
    if (s_rec) return;
    s_rec = true;
    s_samples = 0;
    send_pcm(TC_CMD_OPEN, NULL, 0);
    DIAG("# talkclip start clip=%u thresh=%d\n",
         (unsigned)(s_clips + 1u), (int)s_thresh);
}

static void rec_close(void) {
    if (!s_rec) return;
    s_rec = false;
    s_clips++;
    send_pcm(TC_CMD_CLOSE, NULL, 0);
    DIAG("# talkclip stop clip=%u samples=%u\n",
         (unsigned)s_clips, (unsigned)s_samples);
}

static void t9_begin(void) {
    if (!s_last_path[0]) return;
    if (s_rec) rec_close();
    memset(s_t9_buf, 0, sizeof s_t9_buf);
    s_t9_g = 0;
    s_t9_li = 0;
    s_t9 = true;
    s_chrome_dirty = true;
}

static void t9_cancel(void) {
    s_t9 = false;
    s_chrome_dirty = true;
}

static void t9_commit(void) {
    if (!s_t9_buf[0] || !s_last_path[0]) return;
    send_rename();
    DIAG("# talkclip rename %s -> %s.RAW\n", s_last_path, s_t9_buf);
    s_t9 = false;
    s_chrome_dirty = true;
}

static void paint_t9(uint16_t bg, uint16_t acc, uint16_t fg, uint16_t dim) {
    char line[24];
    char ch[2];
    lcd_text_draw_padded(8, 44, "name last CLIP  8.3 A-Z", 40, 1, dim, bg);
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
    lcd_text_draw_padded(8, 192, "GRN tap add  BLU hold save", 40, 1, dim, bg);
    lcd_text_draw_padded(8, 208, "YEL/GRY hold home  YEL tap letter", 40, 1, dim, bg);
}

static void paint(void) {
    if (!s_lcd) return;
    const uint16_t bg  = st7789_rgb565(8, 10, 18);
    const uint16_t fg  = st7789_rgb565(230, 230, 230);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    const uint16_t rec = st7789_rgb565(220, 40, 40);
    char line[40];

    if (s_chrome_dirty) {
        st7789_fill_rect(0, 0, 320, 240, bg);
        lcd_text_draw_padded(8, 8, "TalkClip", 18, 2, acc, bg);
        if (s_t9) {
            lcd_text_draw_padded(8, 220, "v006: T9 -> NAME.RAW (8 A-Z)", 40, 1, dim, bg);
        } else {
            lcd_text_draw_padded(8, 188, "GREEN arm   YELLOW force clip", 40, 1, dim, bg);
            lcd_text_draw_padded(8, 204, "GRAY tap thr  BLU hold T9  tap hang", 40, 1, dim, bg);
            lcd_text_draw_padded(8, 220, "v006: hang 0/2/10/60s  T9 last CLIP", 40, 1, dim, bg);
        }
        s_line_st[0] = '\0';
        s_line_rms[0] = '\0';
        s_line_path[0] = '\0';
        s_line_t9[0] = '\0';
        s_line_grp[0] = '\0';
        s_line_ch[0] = '\0';
        s_drawn_vu = 0xFFFFu;
        s_drawn_tline = 0xFFFFu;
        s_chrome_dirty = false;
    }

    if (s_t9) {
        paint_t9(bg, acc, fg, dim);
        return;
    }

    lcd_text_draw_padded_changed(200, 12, s_rec ? "REC" : (s_armed ? "ARMED" : "IDLE"),
                                 10, 2, s_rec ? rec : dim, bg,
                                 s_line_st, sizeof s_line_st);

    snprintf(line, sizeof line, "rms %5u  thr %5d  hang %us",
             (unsigned)s_rms, (int)s_thresh,
             (unsigned)(k_hang_ms[s_hang_i] / 1000u));
    lcd_text_draw_padded_changed(8, 56, line, 40, 1, fg, bg,
                                 s_line_rms, sizeof s_line_rms);

    snprintf(line, sizeof line, "%s",
             s_last_path[0] ? s_last_path : "no clip yet");
    lcd_text_draw_padded_changed(8, 168, line, 40, 1, dim, bg,
                                 s_line_path, sizeof s_line_path);

    unsigned vu = s_rms / 80u;
    if (vu > 300u) vu = 300u;
    unsigned tline = (unsigned)s_thresh / 80u;
    if (tline > 300u) tline = 300u;
    if (s_drawn_vu != (uint16_t)vu || s_drawn_tline != (uint16_t)tline) {
        st7789_fill_rect(8, 80, 304, 18, bg);
        if (vu > 0u) st7789_fill_rect(8, 80, (uint16_t)vu, 18, acc);
        st7789_fill_rect((uint16_t)(8u + tline), 78, 2, 22, rec);
        s_drawn_vu = (uint16_t)vu;
        s_drawn_tline = (uint16_t)tline;
    }
}

static void take_mic(uint32_t now) {
    const uint8_t *raw;
    size_t len;
    if (!s_pdm || !pdm_mic_take_raw_buffer(&raw, &len)) return;
    const size_t n = pdm_mic_decode(&s_cic, raw, len, s_pcm);
    if (n != PDM_SAMPLE_BUFFER_SIZE) return;
    for (size_t i = 0; i < n; i++) {
        const int32_t v = (int32_t)s_pcm[i] * TC_GAIN;
        if (v > 32767) s_pcm[i] = 32767;
        else if (v < -32768) s_pcm[i] = (int16_t)-32768;
        else s_pcm[i] = (int16_t)v;
    }
    s_rms = fwog_rms_i16(s_pcm, (unsigned)n);
    if (s_t9) return;
    const bool loud = s_force || (s_armed && s_rms >= (uint32_t)s_thresh);

    if (loud) {
        rec_open();
        s_quiet_since = now;
    }
    if (s_rec) {
        send_pcm(TC_CMD_PCM, s_pcm, (uint16_t)n);
        s_samples += (uint32_t)n;
        if (!loud && (now - s_quiet_since) >= k_hang_ms[s_hang_i])
            rec_close();
    }
}


void kh_tc_enter(bool pdm) {
    s_lcd = true;
    s_leds = true;
    s_pdm = pdm;
    s_armed = false;
    s_rec = false;
    s_force = false;
    s_t9 = false;
    s_chrome_dirty = true;
    fwog_cic_init(&s_cic);
}

void kh_tc_leave(void) {
    rec_close();
    s_t9 = false;
    s_armed = false;
}

void kh_tc_frame(const uint8_t *buf, size_t n) {
    if (n >= sizeof(tc_ack_t) && buf && buf[0] == TC_MSG_ACK) {
        tc_ack_t a;
        memcpy(&a, buf, sizeof a);
        if (a.path[0]) {
            strncpy(s_last_path, a.path, sizeof s_last_path - 1u);
            s_last_path[sizeof s_last_path - 1u] = '\0';
        }
    }
}

void kh_tc_mic(uint32_t now) { take_mic(now); }

void kh_tc_paint(void) { paint(); }

void kh_tc_leds(bool power_armed) {
    s_power_armed = power_armed;
    leds_rec();
}

void kh_tc_buttons(const fwog_power_t *p, bool y_long, bool g_long,
                   bool gray_long, bool red_tap) {
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (s_t9) {
        const bool bdown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) != 0;
        if (bdown && !s_blu_was) {
            s_blu_ms = now;
            s_blu_hold = false;
        }
        if (bdown && !s_blu_hold && (now - s_blu_ms) >= TC_HOLD_MS) {
            s_blu_hold = true;
            t9_commit();
        }
        if (!bdown && s_blu_was && !s_blu_hold)
            fwog_t9_letter_next(s_t9_g, &s_t9_li);
        s_blu_was = bdown;

        if ((p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_GREEN)) && !g_long) {
            fwog_t9_insert(s_t9_buf, sizeof s_t9_buf,
                           fwog_t9_cur(s_t9_g, s_t9_li));
        }
        if ((p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) && !y_long)
            fwog_t9_letter_prev(s_t9_g, &s_t9_li);
        if ((p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_GRAY)) && !gray_long)
            fwog_t9_group_prev(&s_t9_g, &s_t9_li);
        if (red_tap && (p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_RED)))
            fwog_t9_group_next(&s_t9_g, &s_t9_li);
        return;
    }
    if ((p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_GREEN)) && !g_long) {
        s_armed = !s_armed;
        if (!s_armed) rec_close();
    }
    if ((p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) && !y_long) {
        s_force = !s_force;
        if (!s_force && s_rec) rec_close();
    }
    if (red_tap && (p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_RED))) {
        s_thresh += 150;
        if (s_thresh > 8000) s_thresh = 8000;
    }
    {
        const bool bdown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) != 0;
        if (bdown && !s_blu_was) {
            s_blu_ms = now;
            s_blu_hold = false;
        }
        if (bdown && !s_blu_hold && (now - s_blu_ms) >= TC_HOLD_MS) {
            s_blu_hold = true;
            t9_begin();
        }
        if (!bdown && s_blu_was && !s_blu_hold) {
            s_hang_i = (s_hang_i + 1u) % TC_HANG_N;
            s_chrome_dirty = true;
        }
        s_blu_was = bdown;
    }
    if ((p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_GRAY)) && !gray_long) {
        s_thresh -= 150;
        if (s_thresh < 200) s_thresh = 200;
    }
}
