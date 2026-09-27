/* MicScope — PDM spectrogram; Gray-hold quiet cal → dB SPL bars. */
#include "fwog_display.h"
#include "ms_proto.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

FWOG_POWER_DEFAULT();

#define MS_BAR_H  96u
#define MS_BAR_Y  96u
#define MS_BAR_X  8u
#define MS_BAR_W  9u
#define MS_HOLD_MS 700u

static bool     s_lcd;
static bool     s_pdm;
static bool     s_leds;
static bool     s_link;
static bool     s_chrome_dirty = true;
static bool     s_freeze;
static bool     s_cur_lock;
static bool     s_cal_ok;
static bool     s_gry_was;
static bool     s_gry_hold;
static uint32_t s_gry_ms;
static uint32_t s_rms;
static uint16_t s_peak;
static unsigned s_dom_hz;
static unsigned s_cur;
static uint32_t s_mag[MS_BARS];
static uint32_t s_cal_rms = 1u;
static uint32_t s_cal_mag[MS_BARS];
static uint8_t  s_hold[MS_BARS];
static uint32_t s_acc_n;
static uint64_t s_acc_rms;
static uint64_t s_acc_mag[MS_BARS];
static int16_t  s_pcm[PDM_SAMPLE_BUFFER_SIZE];
static float    s_re[FWOG_FFT_MAX_N];
static float    s_im[FWOG_FFT_MAX_N];
static fwog_cic_t     s_cic;
static fwog_link_rx_t s_rx;
static bool           s_power_armed;
static uint32_t       s_get_ms;

static char     s_vu_line[44];
static char     s_pdm_line[44];
static char     s_cur_line[44];
static uint8_t  s_drawn_bar[MS_BARS];
static uint8_t  s_drawn_hold[MS_BARS];
static int      s_drawn_cur = -1;
static bool     s_bars_inited;

static unsigned bar_group(void) {
    const unsigned usable = (FWOG_FFT_MAX_N / 2u) - FWOG_SPECTRUM_MIN_BIN;
    return usable / MS_BARS;
}

static unsigned bar_hz(unsigned i) {
    const unsigned g = bar_group();
    const unsigned bin = FWOG_SPECTRUM_MIN_BIN + i * g + g / 2u;
    return bin * PDM_SAMPLE_RATE_HZ / FWOG_FFT_MAX_N;
}

static void bars_dirty(void) {
    s_bars_inited = false;
}

static uint8_t bar_px(unsigned i) {
    if (s_cal_ok) {
        return (uint8_t)ms_bar_px(s_mag[i], s_cal_mag[i], MS_BAR_H);
    }
    {
        unsigned h = s_mag[i] / 800000u;
        if (h > MS_BAR_H) h = MS_BAR_H;
        return (uint8_t)h;
    }
}

static void send_get(void) {
    ms_cmd_t m;
    memset(&m, 0, sizeof m);
    m.type = MS_MSG_CMD;
    m.cmd = MS_CMD_GET;
    if (s_link) (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void send_cal(void) {
    ms_cal_t m;
    unsigned i;
    memset(&m, 0, sizeof m);
    m.type = MS_MSG_CAL;
    m.ok = s_cal_ok ? 1u : 0u;
    m.magic = MS_MAGIC;
    m.rms = s_cal_rms;
    for (i = 0; i < MS_BARS; i++) m.mag[i] = s_cal_mag[i];
    if (s_link) (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void apply_cal(const ms_cal_t *m) {
    unsigned i;
    if (!m || m->magic != MS_MAGIC || !m->ok) return;
    s_cal_rms = m->rms ? m->rms : 1u;
    for (i = 0; i < MS_BARS; i++) {
        s_cal_mag[i] = m->mag[i] ? m->mag[i] : 1u;
    }
    s_cal_ok = true;
    memset(s_hold, 0, sizeof s_hold);
    bars_dirty();
}

static void commit_cal(void) {
    unsigned i;
    if (s_acc_n == 0u) {
        s_cal_rms = s_rms ? s_rms : 1u;
        for (i = 0; i < MS_BARS; i++) {
            s_cal_mag[i] = s_mag[i] ? s_mag[i] : 1u;
        }
    } else {
        s_cal_rms = (uint32_t)(s_acc_rms / s_acc_n);
        if (s_cal_rms < 1u) s_cal_rms = 1u;
        for (i = 0; i < MS_BARS; i++) {
            s_cal_mag[i] = (uint32_t)(s_acc_mag[i] / s_acc_n);
            if (s_cal_mag[i] < 1u) s_cal_mag[i] = 1u;
        }
    }
    s_cal_ok = true;
    memset(s_hold, 0, sizeof s_hold);
    bars_dirty();
    send_cal();
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
        s_chrome_dirty = true;
    }
}

static void leds_vu(void) {
    unsigned lit;
    if (!s_leds || s_power_armed) return;
    if (s_cal_ok) {
        int spl = ms_spl_approx(s_rms, s_cal_rms);
        int over = spl - MS_QUIET_SPL;
        if (over < 0) over = 0;
        lit = (unsigned)over / 10u;
        if (lit > 7u) lit = 7u;
    } else {
        lit = (s_rms > 8000u) ? 7u : (unsigned)(s_rms / 1200u);
    }
    for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++) {
        const bool on = i < lit;
        ws2812_set_color(i, on ? 30u : 2u, on ? 18u : 4u, 8u);
    }
    ws2812_process();
}

static void paint(void) {
    if (!s_lcd) return;
    const uint16_t bg  = st7789_rgb565(8, 10, 18);
    const uint16_t fg  = st7789_rgb565(230, 230, 230);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    const uint16_t bar = st7789_rgb565(60, 180, 220);
    const uint16_t yel = st7789_rgb565(255, 214, 48);
    const uint16_t fail = st7789_rgb565(220, 80, 80);
    char line[44];
    int spl = 0;

    if (s_chrome_dirty) {
        st7789_fill_rect(0, 0, 320, 40, bg);
        lcd_text_draw_padded(8, 8, "MicScope", 18, 2, acc, bg);
        lcd_text_draw_padded(8, 204, "GREEN freeze   GRAY hold quiet cal",
                             40, 1, dim, bg);
        lcd_text_draw_padded(8, 220, "YEL/BLU cursor  RED clr  6s off",
                             40, 1, dim, bg);
        s_vu_line[0] = '\0';
        s_pdm_line[0] = '\0';
        s_cur_line[0] = '\0';
        s_drawn_cur = -1;
        bars_dirty();
        s_chrome_dirty = false;
    }

    if (s_cal_ok) {
        spl = ms_spl_approx(s_rms, s_cal_rms);
        snprintf(line, sizeof line, "~%3d dB SPL  peak %5u  %4u Hz",
                 spl, (unsigned)s_peak, s_dom_hz);
    } else {
        snprintf(line, sizeof line, "rms %5u  peak %5u  %4u Hz",
                 (unsigned)s_rms, (unsigned)s_peak, s_dom_hz);
    }
    lcd_text_draw_padded_changed(8, 48, line, 40, 1, fg, bg,
                                 s_vu_line, sizeof s_vu_line);

    if (!s_pdm) {
        snprintf(line, sizeof line, "PDM FAIL");
    } else if (s_freeze && s_cal_ok) {
        snprintf(line, sizeof line, "PDM 8 kHz  FREEZE  ~%d dB SPL", spl);
    } else if (s_freeze) {
        snprintf(line, sizeof line, "PDM 8 kHz  FREEZE");
    } else if (s_cal_ok) {
        snprintf(line, sizeof line, "PDM 8 kHz  quiet=~%d dB SPL", MS_QUIET_SPL);
    } else {
        snprintf(line, sizeof line, "hold GRAY in a quiet room");
    }
    lcd_text_draw_padded_changed(8, 64, line, 40, 1,
                                 s_pdm ? dim : fail, bg,
                                 s_pdm_line, sizeof s_pdm_line);

    if (s_cal_ok) {
        snprintf(line, sizeof line, "bar %02u  %4u Hz  +%u dB",
                 s_cur + 1u, bar_hz(s_cur),
                 (unsigned)((bar_px(s_cur) * MS_DB_RANGE) / MS_BAR_H));
    } else {
        snprintf(line, sizeof line, "bar %02u  %4u Hz  hold %u",
                 s_cur + 1u, bar_hz(s_cur), (unsigned)s_hold[s_cur]);
    }
    lcd_text_draw_padded_changed(8, 80, line, 40, 1, fg, bg,
                                 s_cur_line, sizeof s_cur_line);

    {
        const uint16_t x0 = MS_BAR_X, y0 = MS_BAR_Y, bh = MS_BAR_H, bw = MS_BAR_W;
        if (!s_bars_inited || s_drawn_cur != (int)s_cur) {
            st7789_fill_rect(x0, y0, (uint16_t)(MS_BARS * (bw + 1u)), bh, bg);
            memset(s_drawn_bar, 0xFF, sizeof s_drawn_bar);
            memset(s_drawn_hold, 0xFF, sizeof s_drawn_hold);
            s_bars_inited = true;
            s_drawn_cur = (int)s_cur;
        }
        for (unsigned i = 0; i < MS_BARS; i++) {
            const uint8_t live = bar_px(i);
            const uint8_t hold = s_hold[i];
            if (s_drawn_bar[i] == live && s_drawn_hold[i] == hold) continue;
            const uint16_t x = (uint16_t)(x0 + i * (bw + 1u));
            const uint16_t col = (i == s_cur) ? acc : bar;
            st7789_fill_rect(x, y0, bw, bh, bg);
            if (live > 0u) {
                st7789_fill_rect(x, (uint16_t)(y0 + bh - live), bw, live, col);
            }
            if (hold > live && hold > 1u) {
                st7789_fill_rect(x, (uint16_t)(y0 + bh - hold), bw, 2u, yel);
            }
            s_drawn_bar[i] = live;
            s_drawn_hold[i] = hold;
        }
    }
}

static void take_mic(void) {
    const uint8_t *raw;
    size_t len;
    unsigned peak_i = 0;
    uint32_t peak_m = 0;
    unsigned i;
    if (!s_pdm || !pdm_mic_take_raw_buffer(&raw, &len)) return;
    const size_t n = pdm_mic_decode(&s_cic, raw, len, s_pcm);
    if (n != PDM_SAMPLE_BUFFER_SIZE) return;
    s_rms = fwog_rms_i16(s_pcm, (unsigned)n);
    s_peak = fwog_peak_i16(s_pcm, (unsigned)n);
    fwog_dcblock_inplace(s_pcm, PDM_SAMPLE_BUFFER_SIZE);
    {
        const unsigned bin = fwog_spectrum_dominant_bin(s_pcm, FWOG_FFT_MAX_N,
                                                        s_re, s_im);
        s_dom_hz = bin * PDM_SAMPLE_RATE_HZ / FWOG_FFT_MAX_N;
    }

    {
        const unsigned group = bar_group();
        memset(s_mag, 0, sizeof s_mag);
        for (i = 0; i < MS_BARS; i++) {
            float peak = 0.f;
            unsigned k;
            uint32_t mag;
            uint8_t h;
            for (k = 0; k < group; k++) {
                const unsigned b = FWOG_SPECTRUM_MIN_BIN + i * group + k;
                const float m2 = s_re[b] * s_re[b] + s_im[b] * s_im[b];
                if (m2 > peak) peak = m2;
            }
            if (peak > (float)UINT32_MAX) peak = (float)UINT32_MAX;
            mag = (uint32_t)peak;
            s_mag[i] = mag;
            h = bar_px(i);
            if (h > s_hold[i]) s_hold[i] = h;
            if (mag > peak_m) {
                peak_m = mag;
                peak_i = i;
            }
        }
    }
    if (!s_cur_lock) s_cur = peak_i;

    if (s_gry_was && !s_gry_hold) {
        s_acc_rms += s_rms;
        for (i = 0; i < MS_BARS; i++) s_acc_mag[i] += s_mag[i];
        s_acc_n++;
    }
}

static void handle_buttons(const fwog_buttons_t *b, uint32_t now) {
    const bool gdown = (b->down & FWOG_BTN_BIT(FWOG_BTN_GRAY)) != 0;

    if (gdown && !s_gry_was) {
        unsigned i;
        s_gry_ms = now;
        s_gry_hold = false;
        s_acc_n = 0;
        s_acc_rms = 0;
        for (i = 0; i < MS_BARS; i++) s_acc_mag[i] = 0;
    }
    if (gdown && !s_gry_hold && (now - s_gry_ms) >= MS_HOLD_MS) {
        s_gry_hold = true;
        commit_cal();
    }
    s_gry_was = gdown;

    if (b->pressed & FWOG_BTN_BIT(FWOG_BTN_GREEN)) {
        s_freeze = !s_freeze;
    }
    if (b->pressed & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) {
        if (s_cur > 0u) s_cur--;
        s_cur_lock = true;
        bars_dirty();
    }
    if (b->pressed & FWOG_BTN_BIT(FWOG_BTN_BLUE)) {
        if (s_cur + 1u < MS_BARS) s_cur++;
        s_cur_lock = true;
        bars_dirty();
    }
    if (b->pressed & FWOG_BTN_BIT(FWOG_BTN_RED)) {
        memset(s_hold, 0, sizeof s_hold);
        s_cur_lock = false;
        s_freeze = false;
        bars_dirty();
    }
}

static void poll_link(void) {
    uint8_t b;
    size_t n;
    while (fwog_link_uart_read(&b)) {
        if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
        if (n >= sizeof(ms_cal_t) && s_rx.buf[0] == MS_MSG_CAL) {
            ms_cal_t in;
            memcpy(&in, s_rx.buf, sizeof in);
            apply_cal(&in);
        }
    }
}

int main(void) {
    board_init();
    fwog_splash_bind("MicScope", "004");
    s_leds = ws2812_init(pio0, 0u);
    s_pdm = pdm_mic_init(pio0, 1u);
    s_link = fwog_link_uart_init(FWOG_LINK_BAUD);
    fwog_cic_init(&s_cic);
    lcd_bringup();
    DIAG("[micscope] pdm=%s lcd=%s link=%s\n",
         s_pdm ? "ok" : "FAIL", s_lcd ? "ok" : "FAIL", s_link ? "ok" : "FAIL");
    send_get();

    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        const fwog_power_t p = fwog_power_poll(now);
        s_power_armed = p.armed;
        poll_link();
        handle_buttons(&p.buttons, now);
        if (!s_freeze || (s_gry_was && !s_gry_hold)) take_mic();
        if (!s_cal_ok && s_link && (now - s_get_ms) >= 2000u) {
            s_get_ms = now;
            send_get();
        }
        leds_vu();
        paint();
        sleep_ms(2);
    }
}
