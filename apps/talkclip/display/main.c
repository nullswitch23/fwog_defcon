/* TalkClip v006 — VAD recorder. Gray-hold T9 names the last CLIP. */
#include "fwog_display.h"
#include "tc_proto.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

FWOG_POWER_DEFAULT();

#define TC_GAIN    8     /* CIC output is quiet; conversation needs a boost */
#define TC_HOLD_MS 750u
#define TC_STEM_N  9u    /* 8.3 stem + NUL */

static const uint32_t k_hang_ms[] = { 0u, 2000u, 10000u, 60000u };
#define TC_HANG_N ((unsigned)(sizeof k_hang_ms / sizeof k_hang_ms[0]))

static bool     s_lcd, s_pdm, s_leds, s_link;
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
static fwog_link_rx_t s_rx;

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
        s_chrome_dirty = true;
    }
}

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
    if (s_link) (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void send_rename(void) {
    tc_rename_t m;
    memset(&m, 0, sizeof m);
    m.type = TC_MSG_CMD;
    m.cmd = TC_CMD_RENAME;
    strncpy(m.from, s_last_path, TC_PATH_LEN - 1u);
    strncpy(m.name, s_t9_buf, 8u);
    if (s_link) (void)fwog_link_uart_send_frame(&m, sizeof m);
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
    lcd_text_draw_padded(8, 192, "GRN tap add  hold done", 40, 1, dim, bg);
    lcd_text_draw_padded(8, 208, "YEL hold BS  GRY hold cancel", 40, 1, dim, bg);
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
            lcd_text_draw_padded(8, 204, "GRAY tap thr  hold T9  BLUE hang", 40, 1, dim, bg);
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

static void poll_link(void) {
    uint8_t b;
    size_t n;
    while (fwog_link_uart_read(&b)) {
        if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
        if (n >= sizeof(tc_ack_t) && s_rx.buf[0] == TC_MSG_ACK) {
            tc_ack_t a;
            memcpy(&a, s_rx.buf, sizeof a);
            if (a.path[0]) {
                strncpy(s_last_path, a.path, sizeof s_last_path - 1u);
                s_last_path[sizeof s_last_path - 1u] = '\0';
            }
        }
    }
}

int main(void) {
    board_init();
    fwog_splash_bind("TalkClip", "006");
    s_leds = ws2812_init(pio0, 0u);
    s_pdm = pdm_mic_init(pio0, 1u);
    fwog_cic_init(&s_cic);
    lcd_bringup();
    s_link = fwog_link_uart_init(FWOG_LINK_BAUD);
    fwog_link_rx_init(&s_rx);
    DIAG("[talkclip] pdm=%s link=%s  (clips -> /talkclip for DiskGlass)\n",
         s_pdm ? "ok" : "FAIL", s_link ? "ok" : "FAIL");

    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        const fwog_power_t p = fwog_power_poll(now);
        s_power_armed = p.armed;
        if (p.armed && s_rec) rec_close();
        poll_link();

        if (s_t9) {
            const bool gdown =
                (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GREEN)) != 0;
            const bool ydown =
                (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) != 0;
            const bool bdown =
                (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) != 0;
            const bool rydown =
                (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GRAY)) != 0;

            if (gdown && !s_grn_was) {
                s_grn_ms = now;
                s_grn_hold = false;
            }
            if (gdown && !s_grn_hold && (now - s_grn_ms) >= TC_HOLD_MS) {
                s_grn_hold = true;
                t9_commit();
            }
            if (!gdown && s_grn_was && !s_grn_hold) {
                fwog_t9_insert(s_t9_buf, sizeof s_t9_buf,
                               fwog_t9_cur(s_t9_g, s_t9_li));
            }
            s_grn_was = gdown;

            if (ydown && !s_yel_was) {
                s_yel_ms = now;
                s_yel_hold = false;
            }
            if (ydown && !s_yel_hold && (now - s_yel_ms) >= TC_HOLD_MS) {
                s_yel_hold = true;
                fwog_t9_backspace(s_t9_buf);
            }
            if (!ydown && s_yel_was && !s_yel_hold)
                fwog_t9_letter_prev(s_t9_g, &s_t9_li);
            s_yel_was = ydown;

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
            if (rydown && !s_gry_hold && (now - s_gry_ms) >= TC_HOLD_MS) {
                s_gry_hold = true;
                t9_cancel();
            }
            if (!rydown && s_gry_was && !s_gry_hold)
                fwog_t9_group_prev(&s_t9_g, &s_t9_li);
            s_gry_was = rydown;

            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_RED))
                fwog_t9_group_next(&s_t9_g, &s_t9_li);
        } else {
            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GREEN)) {
                s_armed = !s_armed;
                if (!s_armed) rec_close();
            }
            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) {
                s_force = !s_force;
                if (!s_force && s_rec) rec_close();
            }
            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_RED)) {
                s_thresh += 150;
                if (s_thresh > 8000) s_thresh = 8000;
            }

            {
                const bool bdown =
                    (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) != 0;
                if (bdown && !s_blu_was) {
                    s_blu_ms = now;
                    s_blu_hold = false;
                }
                if (bdown && !s_blu_hold && (now - s_blu_ms) >= TC_HOLD_MS) {
                    s_blu_hold = true;
                    s_hang_i = (s_hang_i + 1u) % TC_HANG_N;
                    s_chrome_dirty = true;
                }
                s_blu_was = bdown;
            }

            /* Gray tap on release so a hold can open T9 without moving thresh. */
            {
                const bool gdown =
                    (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GRAY)) != 0;
                if (gdown && !s_gry_was) {
                    s_gry_ms = now;
                    s_gry_hold = false;
                }
                if (gdown && !s_gry_hold && (now - s_gry_ms) >= TC_HOLD_MS) {
                    s_gry_hold = true;
                    t9_begin();
                }
                if (!gdown && s_gry_was && !s_gry_hold) {
                    s_thresh -= 150;
                    if (s_thresh < 200) s_thresh = 200;
                }
                s_gry_was = gdown;
            }
        }

        take_mic(now);
        leds_rec();
        paint();
        sleep_ms(2);
    }
}
