/* KitHome ToneBox. No original long-holds; GRAY/YEL/GRN use taps so
 * 700 ms hold still HOME. BLUE tap cycles category. I2S pio0 SM2; PDM SM1
 * only while this tile is live (2600 wink). */
#include "kh_tb.h"
#include "kh_link.h"
#include "tb_c5.h"
#include "tb_dials.h"
#include "tb_detect.h"
#include "tb_pin.h"
#include "tb_play.h"
#include "tb_seq.h"
#include "tb_serial.h"
#include "common/dsp/cic.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TB_RATE_HZ      8000u
#define TB_MAX_SAMPLES  24000u  /* 3 s: 12-digit dial + gaps, or a $1 pulse */
#define TB_AMP          10000
#define TB_PI           3.14159265f
#define TB_FMAX         2500u   /* i2s_audio.h: 8 kHz Nyquist is 4 kHz; clamp */
enum {
    CAT_DTMF = 0,
    CAT_BLUEBOX,
    CAT_COIN,
    CAT_PROG,
    CAT_MF,
    CAT_C5,
    CAT_SF,
    CAT_ABOUT,
};

typedef struct {
    const char *name;
    uint16_t    f1;
    uint16_t    f2;       /* 0 = single */
    uint16_t    on_ms;
    uint8_t     pulses;   /* 0 = silent (ABOUT). 1 = one shot of on_ms. */
    uint16_t    gap_ms;   /* silence between pulses; unused if pulses <= 1 */
} tb_tone_t;

typedef struct {
    const char *title;
    const tb_tone_t *tones;
    unsigned n;
} tb_cat_t;

/* DTMF, ITU-T Q.23. A-D are the extra column, not a priority override. */
static const tb_tone_t k_dtmf[] = {
    { "1", 697, 1209, 180, 1, 0 }, { "2", 697, 1336, 180, 1, 0 },
    { "3", 697, 1477, 180, 1, 0 }, { "A", 697, 1633, 180, 1, 0 },
    { "4", 770, 1209, 180, 1, 0 }, { "5", 770, 1336, 180, 1, 0 },
    { "6", 770, 1477, 180, 1, 0 }, { "B", 770, 1633, 180, 1, 0 },
    { "7", 852, 1209, 180, 1, 0 }, { "8", 852, 1336, 180, 1, 0 },
    { "9", 852, 1477, 180, 1, 0 }, { "C", 852, 1633, 180, 1, 0 },
    { "*", 941, 1209, 180, 1, 0 }, { "0", 941, 1336, 180, 1, 0 },
    { "#", 941, 1477, 180, 1, 0 }, { "D", 941, 1633, 180, 1, 0 },
};

#define TB_BB_PLAY      0
#define TB_BB_NATIONAL  1
#define TB_BB_INTL      2
#define TB_BB_BF2       3
#define TB_BB_PIN_GEN   4
#define TB_BB_WAIT2600  5
#define TB_BB_PIN_DIG   6
#define TB_BB_REDIAL    7
#define TB_BB_RELAY     8
#define TB_BB_N         9

static const char *const k_bb_items[TB_BB_N] = {
    "PLAY SEQ",
    "national KP1",
    "intl transit KP2",
    "bf pin2+dial",
    "gen PIN brute",
    "WAIT2600 seize",
    "PIN digits",
    "redial every",
    "RELAY H test",
};

/* ACTS US 1700+2200 trains, CA 2200 Hz, UK 1000 Hz — Dolphin redbox tables. */
static const tb_tone_t k_coin[] = {
    { "US 5c  1x66ms",     1700, 2200,  66, 1,  0 },
    { "US 10c 2x66ms",     1700, 2200,  66, 2, 66 },
    { "US 25c 5x33ms",     1700, 2200,  33, 5, 33 },
    { "US $1  650ms",      1700, 2200, 650, 1,  0 },
    { "CA 5c  2200 Hz",    2200,    0,  66, 1,  0 },
    { "CA 10c 2x66ms",     2200,    0,  66, 2, 66 },
    { "CA 25c 5x33ms",     2200,    0,  33, 5, 33 },
    { "UK 10p 1000 Hz",    1000,    0, 200, 1,  0 },
    { "UK 50p 1000 Hz",    1000,    0, 350, 1,  0 },
};

static const tb_tone_t k_prog[] = {
    { "dial 350+440",     350,  440, 600, 1, 0 },
    { "ring 440+480",     440,  480, 600, 1, 0 },
    { "busy 480+620",     480,  620, 400, 1, 0 },
    { "reorder 480+620",  480,  620, 400, 1, 0 },
    { "mW 1004 Hz",      1004,    0, 600, 1, 0 },
    { "1 kHz test",      1000,    0, 600, 1, 0 },
};

/* Bell R1 MF named pairs. 0 is 1300+1500 (not KP2). No KP-then-digits-then-ST. */
static const tb_tone_t k_mf[] = {
    { "MF 1  700+900",   700,  900, 400, 1, 0 },
    { "MF 2  700+1100",  700, 1100, 400, 1, 0 },
    { "MF 3  900+1100",  900, 1100, 400, 1, 0 },
    { "MF 4  700+1300",  700, 1300, 400, 1, 0 },
    { "MF 5  900+1300",  900, 1300, 400, 1, 0 },
    { "MF 6  1100+1300", 1100, 1300, 400, 1, 0 },
    { "MF 7  700+1500",  700, 1500, 400, 1, 0 },
    { "MF 8  900+1500",  900, 1500, 400, 1, 0 },
    { "MF 9  1100+1500", 1100, 1500, 400, 1, 0 },
    { "MF 0  1300+1500", 1300, 1500, 400, 1, 0 },
    { "MF KP 1100+1700", 1100, 1700, 400, 1, 0 },
    { "MF ST 1500+1700", 1500, 1700, 400, 1, 0 },
    { "MF KP2 1300+1700",1300, 1700, 400, 1, 0 },
    { "CCITT 11 700+1700", 700, 1700, 400, 1, 0 },
    { "CCITT 12 900+1700", 900, 1700, 400, 1, 0 },
};

/* CCITT No.5 / C5 named pairs from rdoetjes/tuts bluebox (c5.cpp).
 * Digit/KP/ST spectra match Bell R1. Supervisory names are C5's.
 * One named tone at a time — no KP→digits→ST, no serial hang-up relay. */
static const tb_tone_t k_c5[] = {
    { "C5 1  700+900",     700,  900, 400, 1, 0 },
    { "C5 2  700+1100",    700, 1100, 400, 1, 0 },
    { "C5 3  900+1100",    900, 1100, 400, 1, 0 },
    { "C5 4  700+1300",    700, 1300, 400, 1, 0 },
    { "C5 5  900+1300",    900, 1300, 400, 1, 0 },
    { "C5 6  1100+1300",  1100, 1300, 400, 1, 0 },
    { "C5 7  700+1500",    700, 1500, 400, 1, 0 },
    { "C5 8  900+1500",    900, 1500, 400, 1, 0 },
    { "C5 9  1100+1500",  1100, 1500, 400, 1, 0 },
    { "C5 0  1300+1500",  1300, 1500, 400, 1, 0 },
    { "C5 KP1 1100+1700", 1100, 1700, 400, 1, 0 },
    { "C5 KP2 1300+1700", 1300, 1700, 400, 1, 0 },
    { "C5 ST  1500+1700", 1500, 1700, 400, 1, 0 },
    { "C5 CODE11 700+1700", 700, 1700, 400, 1, 0 },
    { "C5 CODE12 900+1700", 900, 1700, 400, 1, 0 },
    { "C5 SEIZE 2400",     2400,    0, 500, 1, 0 },
    { "C5 ANSWER 2400",    2400,    0, 500, 1, 0 },
    { "C5 PROCEED 2600cap", 2500,   0, 500, 1, 0 },
    { "C5 BUSY 2600cap",   2500,    0, 500, 1, 0 },
    { "C5 CLEARBACK 2600c", 2500,   0, 500, 1, 0 },
    { "C5 CLEARFWD 24+26c", 2400, 2500, 500, 1, 0 },
};

static const tb_tone_t k_sf[] = {
    { "2400 Hz SF",      2400,    0, 500, 1, 0 },
    { "2600 cap 2500",   2500,    0, 500, 1, 0 },
    { "1700+2200 hold",  1700, 2200, 400, 1, 0 },
};

static const tb_tone_t k_about[] = {
    { "read the footer", 0, 0, 0, 0, 0 },
};

static const tb_tone_t k_bb_dummy[] = {
    { "PLAY SEQ", 0, 0, 0, 0, 0 },
};

static const tb_cat_t k_cat[] = {
    { "DTMF",     k_dtmf,       (unsigned)(sizeof k_dtmf  / sizeof k_dtmf[0])  },
    { "BLUEBOX",  k_bb_dummy,   TB_BB_N },
    { "COIN",     k_coin,       (unsigned)(sizeof k_coin  / sizeof k_coin[0])  },
    { "PROGRESS", k_prog,       (unsigned)(sizeof k_prog  / sizeof k_prog[0])  },
    { "MF R1",    k_mf,         (unsigned)(sizeof k_mf    / sizeof k_mf[0])    },
    { "C5",       k_c5,         (unsigned)(sizeof k_c5    / sizeof k_c5[0])    },
    { "SF / SPEC",k_sf,         (unsigned)(sizeof k_sf    / sizeof k_sf[0])    },
    { "ABOUT",    k_about,      (unsigned)(sizeof k_about / sizeof k_about[0]) },
};
#define TB_NCAT ((int)(sizeof k_cat / sizeof k_cat[0]))

static int16_t s_buf[TB_MAX_SAMPLES];
static tb_step_t s_steps[TB_SEQ_STEP_MAX];
static unsigned s_nsteps;
static tb_play_t s_play;
static bool    s_wait2600;
static unsigned s_pin_digits;
static unsigned s_redial;
static char    s_pin_text[24576];
static bool    s_btn_continue;
static bool    s_lcd;
static bool    s_i2s;
static bool    s_leds;
static int     s_cat;
static int     s_item;
static bool    s_chrome_dirty = true;
static bool    s_painted;
#ifndef HOST_TEST
static fwog_cic_t s_cic;
static int16_t   s_pcm[256];
static bool      s_pdm;
#endif

static uint16_t cap_hz(uint16_t hz) {
    if (hz == 0u) return 0u;
    if (hz < 20u) return 20u;
    if (hz > TB_FMAX) return (uint16_t)TB_FMAX;
    return hz;
}

static unsigned cat_n(void) {
    return k_cat[s_cat].n;
}

static const tb_tone_t *cur(void) {
    const tb_cat_t *c = &k_cat[s_cat];
    unsigned i = (unsigned)s_item;
    if (i >= c->n) i = 0;
    if (s_cat == CAT_BLUEBOX) return &k_bb_dummy[0];
    return &c->tones[i];
}

static const char *item_name(void) {
    if (s_cat == CAT_BLUEBOX) {
        unsigned i = (unsigned)s_item;
        if (i >= TB_BB_N) i = 0;
        return k_bb_items[i];
    }
    return cur()->name;
}

static int16_t sample_at(unsigned i, uint16_t f1, uint16_t f2,
                         unsigned n, unsigned fade) {
    float s = 0.0f;
    if (f1) {
        s += sinf(2.0f * TB_PI * (float)f1 * (float)i / (float)TB_RATE_HZ);
    }
    if (f2) {
        s += sinf(2.0f * TB_PI * (float)f2 * (float)i / (float)TB_RATE_HZ);
    }
    if (f1 && f2) s *= 0.5f;
    float env = 1.0f;
    if (fade) {
        if (i < fade) env = (float)i / (float)fade;
        else if (i + fade >= n) env = (float)(n - 1u - i) / (float)fade;
    }
    return (int16_t)((float)TB_AMP * s * env);
}

static unsigned fade_for(unsigned n) {
    if (n >= 400u) return 64u;
    if (n >= 80u) return 8u;
    return 0u;
}

static unsigned emit_tone(unsigned start, uint16_t f1, uint16_t f2, unsigned ms) {
    unsigned n = (TB_RATE_HZ * ms) / 1000u;
    if (start >= TB_MAX_SAMPLES) return 0;
    if (n > TB_MAX_SAMPLES - start) n = TB_MAX_SAMPLES - start;
    const unsigned fade = fade_for(n);
    f1 = cap_hz(f1);
    f2 = cap_hz(f2);
    for (unsigned i = 0; i < n; i++) {
        s_buf[start + i] = sample_at(i, f1, f2, n, fade);
    }
    return n;
}

static unsigned emit_silence(unsigned start, unsigned ms) {
    unsigned n = (TB_RATE_HZ * ms) / 1000u;
    if (start >= TB_MAX_SAMPLES) return 0;
    if (n > TB_MAX_SAMPLES - start) n = TB_MAX_SAMPLES - start;
    memset(&s_buf[start], 0, n * sizeof s_buf[0]);
    return n;
}

static unsigned fill_pulses(const tb_tone_t *t) {
    if (t->pulses == 0u || (t->f1 == 0u && t->f2 == 0u) || t->on_ms == 0u) {
        return 0;
    }
    unsigned pos = 0;
    for (unsigned p = 0; p < (unsigned)t->pulses; p++) {
        pos += emit_tone(pos, t->f1, t->f2, t->on_ms);
        if (p + 1u < (unsigned)t->pulses && t->gap_ms) {
            pos += emit_silence(pos, t->gap_ms);
        }
    }
    return pos;
}

static void play_buf(unsigned n);

static bool bb_load_text(const char *text) {
    s_nsteps = 0;
    if (!text) return false;
    return tb_seq_parse(text, s_steps, TB_SEQ_STEP_MAX, &s_nsteps) && s_nsteps > 0u;
}

static bool bb_load_builtin(unsigned which) {
    if (which >= tb_builtin_dials_n) return false;
    const tb_dial_t *d = &tb_builtin_dials[which];
    return bb_load_text(d->text);
}

static bool io_audio_idle(void) {
#ifndef HOST_TEST
    return !s_i2s || i2s_audio_is_idle();
#else
    return true;
#endif
}

static bool io_play_tone(uint16_t f1, uint16_t f2, unsigned ms) {
    unsigned n = emit_tone(0, cap_hz(f1), cap_hz(f2), ms);
    play_buf(n);
    return n > 0u;
}

static bool io_button_continue(void) {
    if (!s_btn_continue) return false;
    s_btn_continue = false;
    return true;
}

static bool io_listen_2600(void) {
#ifndef HOST_TEST
    const uint8_t *raw;
    size_t len;
    if (!s_pdm || !pdm_mic_take_raw_buffer(&raw, &len)) return false;
    const size_t n = pdm_mic_decode(&s_cic, raw, len, s_pcm);
    if (n < 32u) return false;
    return tb_detect_2600_wink(s_pcm, (unsigned)n);
#else
    return false;
#endif
}

static int io_serial_hangup(unsigned timeout_ms) {
    return tb_serial_hangup(timeout_ms);
}

static const tb_play_io_t k_play_io = {
    .audio_idle = io_audio_idle,
    .play_tone = io_play_tone,
    .serial_hangup = io_serial_hangup,
    .button_continue = io_button_continue,
    .listen_2600 = io_listen_2600,
};

static void bb_start_play(void) {
    if (s_nsteps == 0u) return;
    tb_play_start(&s_play, s_steps, s_nsteps, s_wait2600);
    s_chrome_dirty = true;
}

static void play_buf(unsigned n) {
    if (!s_i2s || n == 0u) return;
#ifndef HOST_TEST
    if (!i2s_audio_is_idle()) i2s_audio_stop();
    (void)i2s_audio_start(s_buf, n, true, false);
#else
    (void)n;
#endif
}

static void play_current(void) {
    if (!s_i2s) return;
    play_buf(fill_pulses(cur()));
}

static void bb_apply(void) {
    const int it = s_item;
    if (it == TB_BB_PLAY) {
        bb_start_play();
        return;
    }
    if (it == TB_BB_NATIONAL) {
        (void)bb_load_builtin(0);
        s_chrome_dirty = true;
        return;
    }
    if (it == TB_BB_INTL) {
        (void)bb_load_builtin(1);
        s_chrome_dirty = true;
        return;
    }
    if (it == TB_BB_BF2) {
        (void)bb_load_builtin(2);
        s_chrome_dirty = true;
        return;
    }
    if (it == TB_BB_PIN_GEN) {
        size_t len = 0;
        if (s_pin_digits == 3u) {
            (void)bb_load_builtin(2);
        } else if (tb_pin_generate(tb_dial_phone_tpl, 2u, s_redial,
                                   s_pin_text, sizeof s_pin_text, &len)) {
            (void)bb_load_text(s_pin_text);
        }
        s_chrome_dirty = true;
        return;
    }
    if (it == TB_BB_WAIT2600) {
        s_wait2600 = !s_wait2600;
        s_chrome_dirty = true;
        return;
    }
    if (it == TB_BB_PIN_DIG) {
        s_pin_digits = (s_pin_digits == 2u) ? 3u : 2u;
        s_chrome_dirty = true;
        return;
    }
    if (it == TB_BB_REDIAL) {
        if (s_redial == 3u) s_redial = 5u;
        else if (s_redial == 5u) s_redial = 10u;
        else s_redial = 3u;
        s_chrome_dirty = true;
        return;
    }
    if (it == TB_BB_RELAY) {
        const int r = tb_serial_hangup(1000u);
        if (r == 1) snprintf(s_play.msg, sizeof s_play.msg, "relay ok");
        else if (r == 0) snprintf(s_play.msg, sizeof s_play.msg, "relay fail");
        else snprintf(s_play.msg, sizeof s_play.msg, "relay err");
        s_chrome_dirty = true;
    }
}

static void leds_cat(void) {
    if (!s_leds) return;
    uint8_t r = 4, g = 4, b = 4;
    switch (s_cat) {
    case CAT_DTMF: g = 28; break;
    case CAT_BLUEBOX: r = 8; g = 12; b = 32; break;
    case CAT_COIN: r = 28; g = 8; break;
    case CAT_PROG: r = 28; g = 20; break;
    case CAT_MF:   b = 32; break;
    case CAT_C5:   r = 16; b = 28; break;
    case CAT_SF:   r = 28; break;
    default: r = g = b = 12; break;
    }
    for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++) {
        ws2812_set_color(i, r, g, b);
    }
    ws2812_process();
}

static void draw(void) {
    if (!s_lcd || !s_chrome_dirty) return;
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
    }

    st7789_fill_rect(0, 0, ST7789_W, 36, hdr);
    snprintf(line, sizeof line, "TONEBOX  %s", k_cat[s_cat].title);
    lcd_text_draw_padded(8, 10, line, 24, 2, acc, hdr);

    lcd_text_draw_padded(8, 48, item_name(), 26, 2, fg, bg);

    if (s_cat == CAT_ABOUT) {
        lcd_text_draw_padded(8, 80, "museum + bluebox pulse", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 96, "tuts tab sequences on I2S", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 112, "H relay on main UART 9600", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 128, "2600 detect via mic cap", 40, 1, dim, bg);
    } else if (s_cat == CAT_BLUEBOX) {
        snprintf(line, sizeof line, "steps %u  %s", s_nsteps,
                 tb_play_busy(&s_play) ? "RUN" : "idle");
        lcd_text_draw_padded(8, 80, line, 40, 1, acc, bg);
        lcd_text_draw_padded(8, 96, s_play.msg[0] ? s_play.msg : "pick + load", 40, 1, dim, bg);
        snprintf(line, sizeof line, "2600 wait %s  PIN %u  rd %u",
                 s_wait2600 ? "ON" : "off", s_pin_digits, s_redial);
        lcd_text_draw_padded(8, 112, line, 40, 1, dim, bg);
        lcd_text_draw_padded(8, 128, "GREEN apply  YELLOW stop", 40, 1, dim, bg);
    } else if (s_cat == CAT_COIN) {
        const tb_tone_t *t = cur();
        snprintf(line, sizeof line, "%u + %u Hz",
                 (unsigned)cap_hz(t->f1), (unsigned)cap_hz(t->f2));
        if (t->f2 == 0u) {
            snprintf(line, sizeof line, "%u Hz", (unsigned)cap_hz(t->f1));
        }
        lcd_text_draw_padded(8, 80, line, 40, 1, acc, bg);
        snprintf(line, sizeof line, "%u pulse x %u ms  gap %u",
                 (unsigned)t->pulses, (unsigned)t->on_ms, (unsigned)t->gap_ms);
        lcd_text_draw_padded(8, 96, line, 40, 1, dim, bg);
        lcd_text_draw_padded(8, 112, "ACTS / UK museum cadence", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 128, "into the speaker only", 40, 1, dim, bg);
    } else if (s_cat == CAT_C5) {
        const tb_tone_t *t = cur();
        if (t->f2) {
            snprintf(line, sizeof line, "%u + %u Hz   %u ms",
                     (unsigned)cap_hz(t->f1), (unsigned)cap_hz(t->f2),
                     (unsigned)t->on_ms);
        } else {
            snprintf(line, sizeof line, "%u Hz   %u ms",
                     (unsigned)cap_hz(t->f1), (unsigned)t->on_ms);
        }
        lcd_text_draw_padded(8, 80, line, 40, 1, acc, bg);
        lcd_text_draw_padded(8, 96, "CCITT No.5  (tuts c5.cpp)", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 112, "2600 Hz rows cap at 2500", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 128, "one named tone, not a seize", 40, 1, dim, bg);
    } else if (cur()->pulses == 0u) {
        lcd_text_draw_padded(8, 80, " ", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 96, " ", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 112, " ", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 128, " ", 40, 1, dim, bg);
    } else if (cur()->f2) {
        snprintf(line, sizeof line, "%u + %u Hz   %u ms",
                 (unsigned)cap_hz(cur()->f1), (unsigned)cap_hz(cur()->f2),
                 (unsigned)cur()->on_ms);
        lcd_text_draw_padded(8, 80, line, 40, 1, acc, bg);
        lcd_text_draw_padded(8, 96, "dual tone  (8 kHz DAC)", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 112, " ", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 128, " ", 40, 1, dim, bg);
    } else {
        snprintf(line, sizeof line, "%u Hz   %u ms",
                 (unsigned)cap_hz(cur()->f1), (unsigned)cur()->on_ms);
        lcd_text_draw_padded(8, 80, line, 40, 1, acc, bg);
        lcd_text_draw_padded(8, 96, "single tone  (8 kHz DAC)", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 112, " ", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 128, " ", 40, 1, dim, bg);
    }

    snprintf(line, sizeof line, "%d / %u", s_item + 1, cat_n());
    lcd_text_draw_padded(8, 148, line, 40, 1, dim, bg);

    lcd_text_draw_padded(8, 172, "GRAY/RED  item     BLUE cat", 40, 1, dim, bg);
    if (s_cat == CAT_BLUEBOX) {
        lcd_text_draw_padded(8, 188, "load dial  PLAY SEQ  ~ = key", 40, 1, dim, bg);
    } else {
        lcd_text_draw_padded(8, 188, "GREEN     play     (stop if busy)", 40, 1, dim, bg);
    }
    lcd_text_draw_padded(8, 212, "Couple to a line only if lawful.", 40, 1, warn, bg);
    lcd_text_draw_padded(8, 226, "Relay needs header UART wiring.", 40, 1, warn, bg);
    s_chrome_dirty = false;
}


void kh_tb_enter(bool pdm) {
    s_lcd = true;
    s_leds = true;
    s_chrome_dirty = true;
    s_painted = false;
    tb_play_init(&s_play);
    s_pin_digits = 2u;
    s_redial = 3u;
#ifndef HOST_TEST
    s_pdm = pdm;
    if (!s_i2s) {
        s_i2s = i2s_audio_init(pio0, 2u);
        if (s_i2s) i2s_audio_set_volume(8);
    }
    if (s_pdm) fwog_cic_init(&s_cic);
    tb_serial_display_attach();
#else
    (void)pdm;
#endif
    leds_cat();
}

void kh_tb_leave(void) {
#ifndef HOST_TEST
    tb_play_stop(&s_play);
    if (s_i2s) i2s_audio_stop();
#endif
}

void kh_tb_frame(const uint8_t *buf, size_t n) {
    tb_serial_display_on_frame(buf, n);
}

void kh_tb_tick(uint32_t now) {
#ifndef HOST_TEST
    if (s_i2s) i2s_audio_process();
    tb_play_poll(&s_play, now, &k_play_io);
#else
    (void)now;
#endif
}

void kh_tb_paint(void) { draw(); }

void kh_tb_leds(bool power_armed) {
    if (!power_armed) leds_cat();
}

void kh_tb_buttons(const fwog_power_t *p, bool y_long, bool g_long,
                   bool gray_long, bool red_tap) {
    if ((p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_GRAY)) && !gray_long) {
        if (--s_item < 0) s_item = (int)cat_n() - 1;
        s_chrome_dirty = true;
    }
    if (red_tap && (p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_RED))) {
        if (++s_item >= (int)cat_n()) s_item = 0;
        s_chrome_dirty = true;
    }
    if (p->buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_BLUE)) {
        if (++s_cat >= TB_NCAT) s_cat = 0;
        s_item = 0;
        s_chrome_dirty = true;
        leds_cat();
    }
    if ((p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_GREEN)) && !g_long) {
        s_btn_continue = true;
#ifndef HOST_TEST
        if (tb_play_busy(&s_play)) {
        } else if (s_i2s && !i2s_audio_is_idle()) {
            i2s_audio_stop();
        } else if (s_cat == CAT_BLUEBOX) {
            bb_apply();
        } else {
            play_current();
        }
#else
        if (s_cat == CAT_BLUEBOX) bb_apply();
        else play_current();
#endif
    }
    if ((p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) && !y_long) {
#ifndef HOST_TEST
        if (tb_play_busy(&s_play)) tb_play_stop(&s_play);
        else if (s_i2s && !i2s_audio_is_idle()) i2s_audio_stop();
        s_chrome_dirty = true;
#endif
    }
}
