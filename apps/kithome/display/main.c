/* KitHome — landing for eight tools. YEL/GRN/GRY hold 700 ms = HOME. */
#include "fwog_display.h"
#include "land_home.h"
#include "bs_proto.h"
#include "gb_proto.h"
#include "kit_proto.h"
#include "kh_link.h"
#include "kh_dg.h"
#include "kh_tc.h"
#include "kh_tb.h"
#include "kh_rg.h"
#include "tb_serial.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <stddef.h>
#include <string.h>

FWOG_POWER_DEFAULT();

#define KIT_N        8
#define KIT_HOLD_MS  700u
#define KIT_TAP_MS   400u
#define MS_BARS      32u
#define SPEC_X       32u
#define SPEC_Y       54u
#define SPEC_H       106u
#define BIN_PX       4u
#define BAR_W        3u

typedef enum {
    KIT_HOME = 0,
    KIT_HD,
    KIT_MS,
    KIT_BS,
    KIT_GB,
    KIT_DG,
    KIT_TC,
    KIT_TB,
    KIT_RG
} kit_app_t;

static const char *k_name[KIT_N] = {
    "HostDeck", "MicScope", "BandScope", "GlassBak",
    "DiskGlass", "TalkClip", "ToneBox", "RigGlass"
};
static const char *k_blurb[KIT_N] = {
    "macros on this CDC",
    "PDM spectrogram / VU",
    "sub-GHz RSSI sweep",
    "FatFs backup / restore",
    "library / wasm / IR",
    "VAD clips /talkclip",
    "tone museum speaker",
    "host CPU/RAM/net"
};
static const char *k_band[] = { "300-348", "387-464", "779-928" };
static const char *k_mode[] = { "SWEEP", "FREEZE", "HUNT" };

typedef struct {
    const char *label;
    const char *chord;
} hd_slot_t;

static const hd_slot_t k_page[2][5] = {
    {
        { "Play",  "Consumer Play/Pause" },
        { "Mute",  "Consumer Mute" },
        { "Copy",  "Ctrl+C" },
        { "Paste", "Ctrl+V" },
        { "Enter", "Return" },
    },
    {
        { "Cut",   "Ctrl+X" },
        { "Undo",  "Ctrl+Z" },
        { "Save",  "Ctrl+S" },
        { "Tab",   "Tab" },
        { "Esc",   "Escape" },
    },
};

static bool        s_lcd, s_link, s_leds, s_pdm;
static kit_app_t   s_app = KIT_HOME;
static int         s_sel;
static int         s_hd_page;
static bool        s_chrome_dirty = true;
static uint32_t    s_hold_y;
static bool        s_y_long;
static uint32_t    s_hold_g;
static bool        s_g_long;
static uint32_t    s_hold_gray;
static bool        s_gray_long;
static uint32_t    s_hold_red;
static bool        s_red_tap;
static uint32_t    s_hold_b;
static uint8_t     s_fired;
static bool        s_power_armed;
static bool        s_goodbye;
static fwog_link_rx_t s_rx;

static uint32_t    s_rms;
static uint16_t    s_peak;
static unsigned    s_dom_hz;
static uint8_t     s_bar[MS_BARS];
static int16_t     s_pcm[PDM_SAMPLE_BUFFER_SIZE];
static float       s_re[FWOG_FFT_MAX_N];
static float       s_im[FWOG_FFT_MAX_N];
static fwog_cic_t  s_cic;
static char        s_vu_line[44];
static char        s_pdm_line[44];
static uint8_t     s_drawn_bar[MS_BARS];
static bool        s_bars_inited;

static int         s_band = 1;
static bs_status_t s_st;
static bool        s_st_ok;
static bool        s_info_dirty = true;
static bool        s_spec_dirty = true;
static bool        s_hits_dirty = true;
static uint8_t     s_hold[3][BS_BINS];
static uint8_t     s_drawn_live[BS_BINS];
static uint8_t     s_drawn_hold[BS_BINS];
static uint8_t     s_spec_band = 0xFF;
static int         s_ant_band = -1;
static uint16_t    s_drawn_peak = 0xFFFFu;
static uint16_t    c_bg, c_hdr, c_acc, c_dim, c_fg, c_barc, c_holdc, c_peakc, c_warn;

static void colors_init(void) {
    c_bg    = st7789_rgb565(8, 10, 18);
    c_hdr   = st7789_rgb565(16, 28, 40);
    c_acc   = st7789_rgb565(80, 200, 120);
    c_dim   = st7789_rgb565(140, 150, 170);
    c_fg    = st7789_rgb565(230, 230, 230);
    c_barc  = st7789_rgb565(80, 170, 255);
    c_holdc = st7789_rgb565(200, 140, 50);
    c_peakc = st7789_rgb565(255, 230, 120);
    c_warn  = st7789_rgb565(230, 180, 70);
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
        colors_init();
        st7789_clear(c_bg);
        board_backlight(255);
        fwog_splash_boot();
        s_chrome_dirty = true;
    }
}

static void send_sel(uint8_t app) {
    kit_sel_t m = { .type = KIT_MSG_SEL, .app = app };
    if (s_link) (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static fwog_ant_t ant_for_band(int b) {
    if (b == 0) return FWOG_ANT_200MHZ;
    if (b == 2) return FWOG_ANT_900MHZ;
    return FWOG_ANT_400MHZ;
}

static void apply_antennas(int band) {
    if (band == s_ant_band) return;
    s_ant_band = band;
    (void)fwog_ioexp_link_set_antennas(ant_for_band(band), ant_for_band(band));
}

static void send_bs(uint8_t cmd) {
    bs_cmd_t m = { .type = BS_MSG_CMD, .cmd = cmd, .band = (uint8_t)s_band };
    if (s_link) (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void spec_reset(void) {
    st7789_fill_rect(SPEC_X, SPEC_Y, (uint16_t)(BS_BINS * BIN_PX), SPEC_H, c_bg);
    memset(s_drawn_live, 0xFF, sizeof s_drawn_live);
    memset(s_drawn_hold, 0xFF, sizeof s_drawn_hold);
    s_drawn_peak = 0xFFFFu;
}

static void hold_clear(void) {
    memset(s_hold, 0, sizeof s_hold);
    s_spec_band = 0xFF;
    s_spec_dirty = true;
    s_hits_dirty = true;
}

static uint16_t bar_px(uint8_t v) {
    uint16_t h = (uint16_t)((unsigned)v * SPEC_H / 80u);
    if (h > SPEC_H) h = SPEC_H;
    return h;
}

static void paint_bin(unsigned i, uint8_t live, uint8_t hold, bool is_peak) {
    const uint16_t x = (uint16_t)(SPEC_X + i * BIN_PX);
    uint16_t hl = bar_px(live);
    uint16_t hh = bar_px(hold);
    if (hh < hl) hh = hl;
    st7789_fill_rect(x, SPEC_Y, BAR_W, SPEC_H, c_bg);
    if (hh > hl) {
        st7789_fill_rect(x, (uint16_t)(SPEC_Y + SPEC_H - hh),
                         BAR_W, (uint16_t)(hh - hl), c_holdc);
    }
    if (hl > 0u) {
        const uint16_t col = is_peak ? c_peakc : c_barc;
        st7789_fill_rect(x, (uint16_t)(SPEC_Y + SPEC_H - hl), BAR_W, hl, col);
    } else if (is_peak) {
        st7789_fill_rect(x, (uint16_t)(SPEC_Y + SPEC_H - 2u), BAR_W, 2, c_peakc);
    }
    s_drawn_live[i] = live;
    s_drawn_hold[i] = hold;
}

static void fmt_mhz(char *buf, size_t n, uint32_t hz) {
    snprintf(buf, n, "%u.%02u",
             (unsigned)(hz / 1000000u),
             (unsigned)((hz / 10000u) % 100u));
}

static void screen_wipe(void) {
    if (s_lcd) st7789_clear(c_bg);
    s_chrome_dirty = true;
}

static void poll_link(void);

bool kh_link_ok(void) { return s_link; }

void kh_link_send(const void *m, size_t n) {
    if (s_link) (void)fwog_link_uart_send_frame(m, n);
}

void kh_link_pump(void) { poll_link(); }

static void leave_app(kit_app_t was) {
    if (was == KIT_DG) kh_dg_leave();
    else if (was == KIT_TC) kh_tc_leave();
    else if (was == KIT_TB) kh_tb_leave();
    else if (was == KIT_RG) kh_rg_leave();
}

static void go_home(void) {
    leave_app(s_app);
    s_app = KIT_HOME;
    s_y_long = true;
    s_g_long = true;
    s_gray_long = true;
    send_sel(KIT_APP_HOME);
    screen_wipe();
}

static void enter_sel(void) {
    if (s_sel == 0) {
        s_app = KIT_HD;
        send_sel(KIT_APP_HOSTDECK);
    } else if (s_sel == 1) {
        s_app = KIT_MS;
        s_vu_line[0] = '\0';
        s_pdm_line[0] = '\0';
        s_bars_inited = false;
        send_sel(KIT_APP_MICSCOPE);
    } else if (s_sel == 2) {
        s_app = KIT_BS;
        s_info_dirty = s_spec_dirty = s_hits_dirty = true;
        s_spec_band = 0xFF;
        apply_antennas(s_band);
        send_sel(KIT_APP_BANDSCOPE);
        send_bs(BS_CMD_SWEEP);
    } else if (s_sel == 3) {
        s_app = KIT_GB;
        send_sel(KIT_APP_GLASSBAK);
    } else if (s_sel == 4) {
        s_app = KIT_DG;
        send_sel(KIT_APP_DISKGLASS);
        kh_dg_enter();
    } else if (s_sel == 5) {
        s_app = KIT_TC;
        send_sel(KIT_APP_TALKCLIP);
        kh_tc_enter(s_pdm);
    } else if (s_sel == 6) {
        s_app = KIT_TB;
        send_sel(KIT_APP_TONEBOX);
        kh_tb_enter(s_pdm);
    } else {
        s_app = KIT_RG;
        send_sel(KIT_APP_RIGGLASS);
        kh_rg_enter();
    }
    screen_wipe();
}

static void hd_fire(unsigned slot) {
    const hd_slot_t *s = &k_page[s_hd_page][slot];
    DIAG("MACRO page=%d slot=%u %s -> %s (HID not enumerated in v001)\n",
         s_hd_page, slot, s->label, s->chord);
}

static void take_mic(bool decode) {
    const uint8_t *raw;
    size_t len;
    if (!s_pdm || !pdm_mic_take_raw_buffer(&raw, &len)) return;
    if (!decode) return;
    const size_t n = pdm_mic_decode(&s_cic, raw, len, s_pcm);
    if (n != PDM_SAMPLE_BUFFER_SIZE) return;
    s_rms = fwog_rms_i16(s_pcm, (unsigned)n);
    s_peak = fwog_peak_i16(s_pcm, (unsigned)n);
    fwog_dcblock_inplace(s_pcm, PDM_SAMPLE_BUFFER_SIZE);
    const unsigned bin = fwog_spectrum_dominant_bin(s_pcm, FWOG_FFT_MAX_N,
                                                    s_re, s_im);
    s_dom_hz = bin * PDM_SAMPLE_RATE_HZ / FWOG_FFT_MAX_N;

    const unsigned usable = (FWOG_FFT_MAX_N / 2u) - FWOG_SPECTRUM_MIN_BIN;
    const unsigned group = usable / MS_BARS;
    memset(s_bar, 0, sizeof s_bar);
    for (unsigned i = 0; i < MS_BARS; i++) {
        float peak = 0.f;
        for (unsigned k = 0; k < group; k++) {
            const unsigned b = FWOG_SPECTRUM_MIN_BIN + i * group + k;
            const float mag = s_re[b] * s_re[b] + s_im[b] * s_im[b];
            if (mag > peak) peak = mag;
        }
        unsigned h = (unsigned)(peak / 800000.f);
        if (h > 120u) h = 120u;
        s_bar[i] = (uint8_t)h;
    }
}

static void poll_link(void) {
    uint8_t b;
    size_t n;
    while (fwog_link_uart_read(&b)) {
        if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
        if (fwog_ioexp_link_handle(s_rx.buf, n)) continue;
        if (s_app == KIT_DG) {
            kh_dg_frame(s_rx.buf, n);
            continue;
        }
        if (s_app == KIT_TC) {
            kh_tc_frame(s_rx.buf, n);
            continue;
        }
        if (s_app == KIT_TB) {
            kh_tb_frame(s_rx.buf, n);
            continue;
        }
        if (s_app == KIT_RG) {
            kh_rg_frame(s_rx.buf, n);
            continue;
        }
        if (n >= sizeof(bs_status_t) && s_rx.buf[0] == BS_MSG_ST) {
            bs_status_t in;
            memcpy(&in, s_rx.buf, sizeof in);
            if (in.band > 2u) in.band = 2u;
            const bool first = !s_st_ok;
            const bool band_chg = first || in.band != s_st.band;
            const bool info = first || band_chg || in.mode != s_st.mode ||
                in.ok != s_st.ok || in.peak_dbm != s_st.peak_dbm ||
                in.peak_hz != s_st.peak_hz || in.f0_hz != s_st.f0_hz;
            const bool spec = first || band_chg || in.peak_bin != s_st.peak_bin ||
                memcmp(in.bar, s_st.bar, BS_BINS) != 0;
            const bool hits = first || in.n_hits != s_st.n_hits ||
                memcmp(in.hit, s_st.hit, sizeof in.hit) != 0;
            if (in.mode != BS_MODE_HUNT) s_band = (int)in.band;
            apply_antennas((int)in.band);
            for (unsigned i = 0; i < BS_BINS; i++) {
                if (in.bar[i] > s_hold[in.band][i]) s_hold[in.band][i] = in.bar[i];
            }
            s_st = in;
            s_st_ok = true;
            if (info) s_info_dirty = true;
            if (spec) s_spec_dirty = true;
            if (hits) s_hits_dirty = true;
        }
    }
}

static void paint_home(void) {
    if (!s_lcd || !s_chrome_dirty) return;
    land_paint_home("KitHome", "eight tools, one UF2",
                    k_name, k_blurb, KIT_N, s_sel,
                    c_bg, c_acc, c_dim, c_fg);
    s_chrome_dirty = false;
}

static void send_gb_dump(void) {
    gb_cmd_t m = { .type = GB_MSG_CMD, .cmd = GB_CMD_DUMP };
    if (s_link) (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void paint_gb(void) {
    if (!s_lcd || !s_chrome_dirty) return;
    st7789_clear(c_bg);
    lcd_text_draw_padded(8, 8, "GlassBak", 18, 2, c_acc, c_bg);
    lcd_text_draw_padded(8, 48, "FatFs to this PC", 40, 1, c_fg, c_bg);
    lcd_text_draw_padded(8, 80, "python tools/fsbak/fsbak.py", 40, 1, c_dim, c_bg);
    lcd_text_draw_padded(8, 104, "GREEN dump   host Push restores", 40, 1, c_acc, c_bg);
    lcd_text_draw_padded(8, 140, "Files go back to original paths", 40, 1, c_dim, c_bg);
    lcd_text_draw_padded(8, 220, "YEL/GRN hold: home   RED 6s: ship", 40, 1, c_dim, c_bg);
    s_chrome_dirty = false;
}

static void paint_hd(void) {
    if (!s_lcd || !s_chrome_dirty) return;
    char line[40];
    st7789_clear(c_bg);
    lcd_text_draw_padded(8, 8, "HostDeck", 18, 2, c_acc, c_bg);
    snprintf(line, sizeof line, "page %d / 2", s_hd_page + 1);
    lcd_text_draw_padded(8, 40, line, 40, 1, c_dim, c_bg);
    lcd_text_draw_padded(8, 72,  "GREEN  Play/Cut", 40, 1, c_fg, c_bg);
    lcd_text_draw_padded(8, 88,  "YELLOW Mute/Undo  (hold: home)", 40, 1, c_fg, c_bg);
    lcd_text_draw_padded(8, 104, "BLUE   Copy/Save  (hold: page)", 40, 1, c_fg, c_bg);
    lcd_text_draw_padded(8, 120, "GRAY   Paste/Tab  (hold: home)", 40, 1, c_fg, c_bg);
    lcd_text_draw_padded(8, 136, "RED    Enter/Esc  (hold 6s: ship)", 40, 1, c_fg, c_bg);
    lcd_text_draw_padded(8, 168, k_page[s_hd_page][0].label, 12, 2, c_acc, c_bg);
    lcd_text_draw_padded(8, 200, "This PC. YEL/GRN hold: home", 40, 1, c_dim, c_bg);
    s_chrome_dirty = false;
}

static void paint_ms(void) {
    if (!s_lcd) return;
    char line[40];
    if (s_chrome_dirty) {
        st7789_clear(c_bg);
        lcd_text_draw_padded(8, 8, "MicScope", 18, 2, c_acc, c_bg);
        lcd_text_draw_padded(8, 220, "YEL or GRN hold: home", 40, 1, c_dim, c_bg);
        s_vu_line[0] = '\0';
        s_pdm_line[0] = '\0';
        s_bars_inited = false;
        s_chrome_dirty = false;
    }
    snprintf(line, sizeof line, "rms %5u  peak %5u  %4u Hz",
             (unsigned)s_rms, (unsigned)s_peak, s_dom_hz);
    lcd_text_draw_padded_changed(8, 48, line, 40, 1, c_fg, c_bg,
                                 s_vu_line, sizeof s_vu_line);
    lcd_text_draw_padded_changed(8, 64,
                                 s_pdm ? "PDM 8 kHz  clap the room" : "PDM FAIL",
                                 40, 1,
                                 s_pdm ? c_dim : st7789_rgb565(220, 80, 80), c_bg,
                                 s_pdm_line, sizeof s_pdm_line);
    const uint16_t x0 = 8, y0 = 88, bh = 120, bw = 9;
    if (!s_bars_inited) {
        st7789_fill_rect(x0, y0, (uint16_t)(MS_BARS * (bw + 1u)), bh, c_bg);
        memset(s_drawn_bar, 0xFF, sizeof s_drawn_bar);
        s_bars_inited = true;
    }
    const uint16_t bar = st7789_rgb565(60, 180, 220);
    for (unsigned i = 0; i < MS_BARS; i++) {
        const uint16_t h = s_bar[i];
        if (s_drawn_bar[i] == h) continue;
        const uint16_t x = (uint16_t)(x0 + i * (bw + 1u));
        st7789_fill_rect(x, y0, bw, bh, c_bg);
        if (h > 0u) {
            st7789_fill_rect(x, (uint16_t)(y0 + bh - h), bw, h, bar);
        }
        s_drawn_bar[i] = (uint8_t)h;
    }
}

static void paint_bs(void) {
    if (!s_lcd) return;
    char line[44];
    char mhz[12];

    if (s_chrome_dirty) {
        st7789_clear(c_bg);
        st7789_fill_rect(0, 0, ST7789_W, 28, c_hdr);
        lcd_text_draw_padded(8, 6, "BandScope", 12, 2, c_acc, c_hdr);
        lcd_text_draw_padded(8, 226, "YEL tap band  YEL/GRN hold home",
                             40, 1, c_dim, c_bg);
        spec_reset();
        s_chrome_dirty = false;
        s_info_dirty = s_spec_dirty = s_hits_dirty = true;
    }

    if (s_info_dirty) {
        const uint8_t mode = s_st_ok ? s_st.mode : BS_MODE_SWEEP;
        const char *ms = (mode < 3u) ? k_mode[mode] : "?";
        const uint16_t mc = (mode == BS_MODE_FREEZE) ? c_warn :
                            (mode == BS_MODE_HUNT) ? c_barc : c_acc;
        lcd_text_draw_padded(200, 10, ms, 8, 1, mc, c_hdr);

        const int band = (s_st_ok && s_st.mode == BS_MODE_HUNT) ? (int)s_st.band : s_band;
        snprintf(line, sizeof line, "%s MHz", k_band[band]);
        lcd_text_draw_padded(8, 30, line, 40, 1, c_fg, c_bg);

        if (!s_st_ok) {
            lcd_text_draw_padded(8, 42, "waiting for main...", 40, 1, c_dim, c_bg);
        } else if (!s_st.ok) {
            lcd_text_draw_padded(8, 42, "radio 0 fail", 40, 1, c_warn, c_bg);
        } else {
            fmt_mhz(mhz, sizeof mhz, s_st.peak_hz);
            snprintf(line, sizeof line, "peak %+d dBm  @ %s MHz",
                     (int)s_st.peak_dbm, mhz);
            lcd_text_draw_padded(8, 42, line, 40, 1, c_fg, c_bg);
        }

        if (s_st_ok) {
            char a[8], b[8], c[8];
            const uint32_t f1 = s_st.f0_hz + s_st.step_hz * (BS_BINS - 1u);
            const uint32_t fm = s_st.f0_hz + s_st.step_hz * ((BS_BINS - 1u) / 2u);
            snprintf(a, sizeof a, "%u", (unsigned)(s_st.f0_hz / 1000000u));
            snprintf(b, sizeof b, "%u", (unsigned)(fm / 1000000u));
            snprintf(c, sizeof c, "%u", (unsigned)(f1 / 1000000u));
            lcd_text_draw_padded(SPEC_X, 162, a, 5, 1, c_dim, c_bg);
            lcd_text_draw_padded((uint16_t)(SPEC_X + 108u), 162, b, 5, 1, c_dim, c_bg);
            lcd_text_draw_padded((uint16_t)(SPEC_X + 212u), 162, c, 5, 1, c_dim, c_bg);
        }
        s_info_dirty = false;
    }

    if (s_spec_dirty) {
        if (!s_st_ok) {
            lcd_text_draw_padded(SPEC_X, 96, "no sweep yet", 22, 1, c_dim, c_bg);
        } else {
            const uint8_t band = s_st.band;
            if (s_spec_band == 0xFFu) spec_reset();
            s_spec_band = band;
            for (unsigned i = 0; i < BS_BINS; i++) {
                const uint8_t live = s_st.bar[i];
                uint8_t hold = s_hold[band][i];
                if (live > hold) {
                    hold = live;
                    s_hold[band][i] = live;
                }
                const bool is_peak = (i == (unsigned)s_st.peak_bin);
                const bool was_peak = (i == (unsigned)s_drawn_peak);
                if (s_drawn_live[i] == live && s_drawn_hold[i] == hold &&
                    !is_peak && !was_peak) {
                    continue;
                }
                paint_bin(i, live, hold, is_peak);
            }
            s_drawn_peak = s_st.peak_bin;
        }
        s_spec_dirty = false;
    }

    if (s_hits_dirty) {
        lcd_text_draw_padded(8, 172, "top 5", 40, 1, c_dim, c_bg);
        for (unsigned i = 0; i < BS_TOP; i++) {
            const uint16_t y = (uint16_t)(182u + i * 8u);
            if (!s_st_ok || i >= s_st.n_hits) {
                lcd_text_draw_padded(8, y, "", 40, 1, c_fg, c_bg);
                continue;
            }
            fmt_mhz(mhz, sizeof mhz, s_st.hit[i].hz);
            snprintf(line, sizeof line, "%u  %s MHz  %+d dBm",
                     i + 1u, mhz, (int)s_st.hit[i].dbm);
            lcd_text_draw_padded(8, y, line, 40, 1, c_fg, c_bg);
        }
        s_hits_dirty = false;
    }
}

static void leds_tick(void) {
    if (!s_leds || s_power_armed) return;
    if (s_app == KIT_HOME) {
        for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++) {
            const bool on = (int)i == s_sel;
            ws2812_set_color(i, 2u, on ? 22u : 2u, on ? 18u : 6u);
        }
    } else if (s_app == KIT_HD) {
        for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++) {
            ws2812_set_color(i, 4u, (unsigned)s_hd_page ? 18u : 8u, 14u);
        }
    } else if (s_app == KIT_MS) {
        const unsigned lit = (s_rms > 8000u) ? 7u : (unsigned)(s_rms / 1200u);
        for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++) {
            const bool on = i < lit;
            ws2812_set_color(i, on ? 30u : 2u, on ? 18u : 4u, 8u);
        }
    } else if (s_app == KIT_GB) {
        for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++) {
            ws2812_set_color(i, 2u, 18u, 8u);
        }
    } else if (s_app == KIT_DG) {
        kh_dg_leds(s_power_armed);
        return;
    } else if (s_app == KIT_TC) {
        kh_tc_leds(s_power_armed);
        return;
    } else if (s_app == KIT_TB) {
        kh_tb_leds(s_power_armed);
        return;
    } else if (s_app == KIT_RG) {
        kh_rg_leds();
        return;
    } else {
        int h = s_st_ok ? (int)s_st.peak_dbm + 110 : 0;
        if (h < 0) h = 0;
        unsigned lit = (unsigned)h / 12u;
        if (lit > 7u) lit = 7u;
        const bool hunt = s_st_ok && s_st.mode == BS_MODE_HUNT;
        for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++) {
            ws2812_set_color(i, hunt ? 18u : 2u, i < lit ? 28u : 2u, i < lit ? 8u : 4u);
        }
    }
    ws2812_process();
}

int main(void) {
    board_init();
    fwog_splash_bind("KitHome", "007");
    s_leds = ws2812_init(pio0, 0u);
    s_pdm = pdm_mic_init(pio0, 1u);
    fwog_cic_init(&s_cic);
    colors_init();
    lcd_bringup();
    s_link = fwog_link_uart_init(FWOG_LINK_BAUD);
    fwog_link_rx_init(&s_rx);
    tb_serial_display_attach();
    tb_serial_display_set_pump(kh_link_pump);
    send_sel(KIT_APP_HOME);
    DIAG("[kithome] lcd=%s pdm=%s link=%s\n",
         s_lcd ? "ok" : "FAIL", s_pdm ? "ok" : "FAIL", s_link ? "ok" : "FAIL");

    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        const fwog_power_t p = fwog_power_poll(now);
        s_power_armed = p.armed;
        if (p.progress >= 100u) s_goodbye = true;

        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_RED)) {
            s_hold_red = now;
            s_red_tap = true;
        }
        if ((p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_RED)) &&
            (now - s_hold_red) >= KIT_TAP_MS) {
            s_red_tap = false;
        }

        poll_link();

        /* USB can keep SYS up after BATFET_DIS. Do not redraw the tile over
         * GOODBYE, and do not paint during the 6 s LED countdown. */
        if (s_goodbye || p.armed) {
            sleep_ms(2);
            continue;
        }

        if (s_app == KIT_MS) take_mic(true);
        else if (s_app == KIT_TC) kh_tc_mic(now);
        else if (s_app != KIT_TB) take_mic(false);
        if (s_app == KIT_DG) kh_dg_tick(now);
        if (s_app == KIT_TB) kh_tb_tick(now);

        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) {
            s_hold_y = now;
            s_y_long = false;
            s_fired &= (uint8_t)~FWOG_BTN_BIT(FWOG_BTN_YELLOW);
        }
        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GREEN)) {
            s_hold_g = now;
            s_g_long = false;
        }
        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GRAY)) {
            s_hold_gray = now;
            s_gray_long = false;
        }
        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_BLUE)) {
            s_hold_b = now;
            s_fired &= (uint8_t)~FWOG_BTN_BIT(FWOG_BTN_BLUE);
        }
        if (s_app != KIT_HOME) {
            if ((p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) &&
                !s_y_long && now - s_hold_y >= KIT_HOLD_MS) {
                go_home();
            }
            if ((p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GREEN)) &&
                !s_g_long && now - s_hold_g >= KIT_HOLD_MS) {
                go_home();
            }
            if ((p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GRAY)) &&
                !s_gray_long && now - s_hold_gray >= KIT_HOLD_MS) {
                go_home();
            }
        }

        if (s_app == KIT_HOME) {
            if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_GRAY)) &&
                !s_gray_long) {
                if (--s_sel < 0) s_sel = KIT_N - 1;
                s_chrome_dirty = true;
            }
            if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_RED)) && s_red_tap) {
                if (++s_sel >= KIT_N) s_sel = 0;
                s_chrome_dirty = true;
            }
            if (p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_GREEN)) enter_sel();
        } else if (s_app == KIT_HD) {
            if ((p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) &&
                !(s_fired & FWOG_BTN_BIT(FWOG_BTN_BLUE)) &&
                now - s_hold_b >= KIT_HOLD_MS) {
                s_fired |= (uint8_t)FWOG_BTN_BIT(FWOG_BTN_BLUE);
                s_hd_page = s_hd_page ? 0 : 1;
                s_chrome_dirty = true;
            }
            if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_GREEN)) && !s_g_long)
                hd_fire(0);
            if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) && !s_y_long)
                hd_fire(1);
            if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_BLUE)) &&
                !(s_fired & FWOG_BTN_BIT(FWOG_BTN_BLUE))) hd_fire(2);
            if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_GRAY)) && !s_gray_long)
                hd_fire(3);
            if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_RED)) && s_red_tap)
                hd_fire(4);
        } else if (s_app == KIT_BS) {
            if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) && !s_y_long) {
                if (--s_band < 0) s_band = 2;
                apply_antennas(s_band);
                send_bs(BS_CMD_SWEEP);
                s_info_dirty = true;
            }
            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_BLUE)) {
                if (++s_band > 2) s_band = 0;
                apply_antennas(s_band);
                send_bs(BS_CMD_SWEEP);
                s_info_dirty = true;
            }
            if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_GREEN)) && !s_g_long)
                send_bs(BS_CMD_FREEZE);
            if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_GRAY)) && !s_gray_long)
                send_bs(BS_CMD_HUNT);
            if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_RED)) && s_red_tap) {
                hold_clear();
                send_bs(BS_CMD_CLEAR);
            }
        } else if (s_app == KIT_GB) {
            if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_GREEN)) && !s_g_long) {
                send_gb_dump();
                DIAG("[kithome] glassbak dump\n");
            }
        } else if (s_app == KIT_DG) {
            kh_dg_buttons(&p, s_y_long, s_g_long, s_gray_long, s_red_tap);
        } else if (s_app == KIT_TC) {
            kh_tc_buttons(&p, s_y_long, s_g_long, s_gray_long, s_red_tap);
        } else if (s_app == KIT_TB) {
            kh_tb_buttons(&p, s_y_long, s_g_long, s_gray_long, s_red_tap);
        }

        if (s_app == KIT_HOME) paint_home();
        else if (s_app == KIT_HD) paint_hd();
        else if (s_app == KIT_MS) paint_ms();
        else if (s_app == KIT_GB) paint_gb();
        else if (s_app == KIT_BS) paint_bs();
        else if (s_app == KIT_DG) kh_dg_paint();
        else if (s_app == KIT_TC) kh_tc_paint();
        else if (s_app == KIT_TB) kh_tb_paint();
        else if (s_app == KIT_RG) kh_rg_paint();
        leds_tick();
        sleep_ms(2);
    }
}
