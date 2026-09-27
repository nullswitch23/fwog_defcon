/* FunDesk PitchFork tile. Stand-alone GRAY hold is AUTO; FunDesk GRAY hold
 * is home, so AUTO is BLUE hold. GRAY tap still cycles the locked note. */
#include "fd_pf.h"
#include "fd_link.h"
#include "pf_note.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

#define PF_RMS_FLOOR 100u
#define PF_MIDI_LO   40
#define PF_MIDI_HI   76
#define PF_HOLD_MS   700u

static bool     s_lcd = true, s_pdm, s_chrome_dirty = true;
static bool     s_power_armed;
static bool     s_lock;
static uint32_t s_rms;
static unsigned s_hz;
static char     s_note[8];
static int      s_cents;
static int16_t  s_pcm[PDM_SAMPLE_BUFFER_SIZE];
static float    s_re[FWOG_FFT_MAX_N];
static float    s_im[FWOG_FFT_MAX_N];
static float    s_m2[FWOG_FFT_MAX_N / 2u];
static fwog_cic_t s_cic;
static char     s_line_note[8];
static char     s_line_st[16];
static char     s_line_hz[44];
static char     s_line_fl[12];
static char     s_line_sh[12];
static char     s_line_tgt[20];
static int      s_drawn_cents = 9999;
static bool     s_drawn_lock = true;
static int      s_target = -1;
static int      s_shown_midi;
static int      s_cand_midi;
static unsigned s_cand_n;
static uint32_t s_blu_ms;
static bool     s_blu_was, s_blu_hold;

static float mag2(unsigned i) {
    return s_re[i] * s_re[i] + s_im[i] * s_im[i];
}

static float hz_of_bin(unsigned bin) {
    float d = 0.0f;
    if (bin > FWOG_SPECTRUM_MIN_BIN && (bin + 1u) < (FWOG_FFT_MAX_N / 2u)) {
        d = pf_parabolic_delta(mag2(bin - 1u), mag2(bin), mag2(bin + 1u));
    }
    return ((float)bin + d) * (float)PDM_SAMPLE_RATE_HZ / (float)FWOG_FFT_MAX_N;
}

static unsigned maybe_fundamental(unsigned peak) {
    const float p = mag2(peak);
    unsigned best = peak;
    unsigned h;
    if (p <= 0.0f) return peak;
    for (h = 2u; h <= 4u; h++) {
        const unsigned lo = peak / h;
        if (lo < FWOG_SPECTRUM_MIN_BIN) continue;
        if (mag2(lo) > p * 0.18f) best = lo;
    }
    return best;
}

static void leds_cents(void) {
    if (!fd_leds_ok() || s_power_armed) return;
    if (!s_lock) {
        for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++)
            ws2812_set_color(i, 2, 2, 4);
        ws2812_process();
        return;
    }
    int lit = 3 + s_cents / 12;
    if (lit < 0) lit = 0;
    if (lit > 6) lit = 6;
    for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++) {
        uint8_t r = 2, g = 2, b = 4;
        if ((int)i == lit) {
            if (s_cents > -8 && s_cents < 8) g = 40;
            else if (s_cents < 0) b = 40;
            else r = 40;
        }
        ws2812_set_color(i, r, g, b);
    }
    ws2812_process();
}

static void paint(void) {
    if (!s_lcd) return;
    const uint16_t bg  = st7789_rgb565(8, 10, 18);
    const uint16_t acc = st7789_rgb565(80, 220, 120);
    const uint16_t dim = st7789_rgb565(160, 170, 190);
    const uint16_t fg  = st7789_rgb565(240, 240, 240);
    const uint16_t yel = st7789_rgb565(255, 214, 48);
    const uint16_t blu = st7789_rgb565(80, 160, 255);
    const uint16_t red = st7789_rgb565(255, 90, 70);
    const uint16_t hdr = st7789_rgb565(16, 28, 40);
    char line[44];
    const char *st;
    uint16_t st_col;

    if (s_chrome_dirty) {
        st7789_fill_rect(0, 0, ST7789_W, ST7789_H, bg);
        st7789_fill_rect(0, 0, ST7789_W, 28, hdr);
        lcd_text_draw_padded(8, 6, "PitchFork", 16, 2, acc, hdr);
        lcd_text_draw_padded(8, 196, "GRY tap note  BLU hold AUTO", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 212, "hold a tone 1m from mic", 40, 1, dim, bg);
        s_chrome_dirty = false;
        s_line_note[0] = s_line_st[0] = s_line_hz[0] = '\0';
        s_line_fl[0] = s_line_sh[0] = s_line_tgt[0] = '\0';
        s_drawn_cents = 9999;
        s_drawn_lock = true;
    }

    lcd_text_draw_padded_changed(8, 36,
                                 (s_target >= 0 || s_lock) ? s_note : "----", 6, 6,
                                 acc, bg, s_line_note, sizeof s_line_note);

    if (!s_lock) {
        st = "QUIET";
        st_col = dim;
    } else if (s_cents > -8 && s_cents < 8) {
        st = "IN TUNE";
        st_col = acc;
    } else if (s_cents < 0) {
        st = "FLAT";
        st_col = blu;
    } else {
        st = "SHARP";
        st_col = red;
    }
    lcd_text_draw_padded_changed(8, 92, st, 10, 3, st_col, bg,
                                 s_line_st, sizeof s_line_st);

    if (s_lock) {
        snprintf(line, sizeof line, "%u Hz   %+d c", s_hz, s_cents);
    } else {
        snprintf(line, sizeof line, "rms %u  wait for a tone", (unsigned)s_rms);
    }
    lcd_text_draw_padded_changed(8, 124, line, 36, 1, fg, bg,
                                 s_line_hz, sizeof s_line_hz);

    if (s_target < 0) snprintf(line, sizeof line, "AUTO  (3-frame hold)");
    else snprintf(line, sizeof line, "lock %s", s_note);
    lcd_text_draw_padded_changed(8, 132, line, 36, 1, dim, bg,
                                 s_line_tgt, sizeof s_line_tgt);

    lcd_text_draw_padded_changed(8, 144, "FLAT", 8, 2, blu, bg,
                                 s_line_fl, sizeof s_line_fl);
    lcd_text_draw_padded_changed(200, 144, "SHARP", 8, 2, red, bg,
                                 s_line_sh, sizeof s_line_sh);

    if (s_drawn_cents != s_cents || s_drawn_lock != s_lock) {
        st7789_fill_rect(8, 168, 304, 22, bg);
        st7789_fill_rect(158, 168, 4, 22, yel);
        if (s_lock) {
            int x = 160 + s_cents * 2;
            if (x < 16) x = 16;
            if (x > 296) x = 296;
            const uint16_t col = (s_cents > -8 && s_cents < 8) ? acc : yel;
            st7789_fill_rect((uint16_t)(x - 10), 168, 20, 22, col);
        }
        s_drawn_cents = s_cents;
        s_drawn_lock = s_lock;
    }
}

static void take_mic(void) {
    const uint8_t *raw;
    size_t len;
    unsigned bin;
    float hz;
    if (!s_pdm || !pdm_mic_take_raw_buffer(&raw, &len)) return;
    const size_t n = pdm_mic_decode(&s_cic, raw, len, s_pcm);
    if (n != PDM_SAMPLE_BUFFER_SIZE) return;
    s_rms = fwog_rms_i16(s_pcm, (unsigned)n);
    fwog_dcblock_inplace(s_pcm, PDM_SAMPLE_BUFFER_SIZE);
    bin = fwog_spectrum_dominant_bin(s_pcm, FWOG_FFT_MAX_N, s_re, s_im);
    {
        const unsigned n2 = FWOG_FFT_MAX_N / 2u;
        unsigned i;
        unsigned hps;
        for (i = 0; i < n2; i++) s_m2[i] = mag2(i);
        hps = pf_hps_bin(s_m2, n2, FWOG_SPECTRUM_MIN_BIN);
        if (hps >= FWOG_SPECTRUM_MIN_BIN) bin = hps;
        else bin = maybe_fundamental(bin);
    }
    hz = hz_of_bin(bin);
    s_hz = (unsigned)(hz + 0.5f);
    if (s_target >= 0) pf_name_midi(s_target, s_note, sizeof s_note);
    if (s_rms < PF_RMS_FLOOR) {
        if (s_target < 0) {
            s_note[0] = '\0';
            s_shown_midi = 0;
            s_cand_n = 0;
        }
        s_cents = 0;
        s_hz = 0;
        s_lock = false;
        return;
    }
    if (s_target >= 0) {
        s_cents = (s_cents * 2 + pf_cents_vs_midi(hz, s_target)) / 3;
        s_lock = true;
        return;
    }
    {
        const int midi = pf_midi_from_hz(hz);
        s_shown_midi = pf_hold_midi(&s_cand_midi, &s_cand_n, midi, s_shown_midi);
        if (s_shown_midi == 0) {
            s_lock = false;
            s_cents = 0;
            return;
        }
        pf_name_midi(s_shown_midi, s_note, sizeof s_note);
        s_cents = (s_cents * 2 + pf_cents_vs_midi(hz, s_shown_midi)) / 3;
        s_lock = true;
    }
}

void fd_pf_enter(void) {
    s_lcd = true;
    s_chrome_dirty = true;
    s_blu_was = false;
    s_blu_hold = true;
    fwog_cic_init(&s_cic);
    s_pdm = fd_pdm_ok();
    if (!s_pdm) s_pdm = pdm_mic_init(pio0, 1u);
    DIAG("[fundesk] pitchfork pdm=%s (BLU hold AUTO)\n", s_pdm ? "ok" : "FAIL");
}

void fd_pf_leave(void) {
    /* Do not pdm_mic_stop(): a later enter would re-init and hang. */
}

void fd_pf_frame(const uint8_t *buf, size_t n) {
    (void)buf;
    (void)n;
}

void fd_pf_buttons(const fwog_power_t *p, bool y_long, bool g_long,
                   bool gray_long, bool red_tap) {
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    const bool bdown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) != 0;
    (void)y_long;
    (void)g_long;
    (void)red_tap;
    s_power_armed = p->armed;

    if (!p->armed) {
        if (bdown && !s_blu_was) {
            s_blu_ms = now;
            s_blu_hold = false;
        }
        if (bdown && !s_blu_hold && (now - s_blu_ms) >= PF_HOLD_MS) {
            s_blu_hold = true;
            s_target = -1;
            s_shown_midi = 0;
            s_cand_n = 0;
            s_note[0] = '\0';
            s_chrome_dirty = true;
        }
        if ((p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_GRAY)) && !gray_long) {
            if (s_target < 0) s_target = PF_MIDI_LO;
            else if (s_target >= PF_MIDI_HI) s_target = PF_MIDI_LO;
            else s_target++;
            pf_name_midi(s_target, s_note, sizeof s_note);
            s_chrome_dirty = true;
        }
    }
    s_blu_was = bdown;
}

void fd_pf_tick(uint32_t now) {
    (void)now;
    take_mic();
    leds_cents();
}

void fd_pf_paint(void) {
    paint();
}
