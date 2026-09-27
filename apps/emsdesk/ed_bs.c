#include "fwog_display.h"
#include "ed_link.h"
#include "ed_bs.h"
#include "bs_proto.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>



#define SPEC_X     32u
#define SPEC_Y     52u
#define SPEC_H     78u
#define HIST_Y     (SPEC_Y + SPEC_H)
#define HIST_H     16u
#define AXIS_Y     (HIST_Y + HIST_H + 2u)
#define OWN_Y      158u
#define HITS_HDR_Y 176u
#define HITS_Y     184u
#define FOOT_Y     226u
#define BIN_PX     4u
#define BAR_W      3u
#define BS_OWNED   4u
#define OWN_LIVE   22u
#define HOLD_MS    700u

static const char *k_band[] = { "300-348", "387-464", "779-928" };
static const char *k_band_s[] = { "315", "433", "900" };
static const char *k_mode[] = { "SWEEP", "FREEZE", "HUNT" };
static const char *k_tag[] = { "mine", "lab", "fob", "wx" };

typedef struct {
    uint32_t hz;
    uint8_t  band;
    uint8_t  tag;
    uint8_t  used;
} own_t;

static bool           s_lcd, s_link, s_leds;
static int            s_band = 1;
static bs_status_t    s_st;
static bool           s_st_ok;
static bool           s_painted;
static bool           s_chrome_dirty = true;
static bool           s_info_dirty = true;
static bool           s_spec_dirty = true;
static bool           s_hist_dirty = true;
static bool           s_hits_dirty = true;
static bool           s_own_dirty = true;
static uint8_t        s_hold[3][BS_BINS];
static uint8_t        s_hist[3][HIST_H][BS_BINS];
static uint8_t        s_drawn_live[BS_BINS];
static uint8_t        s_drawn_hold[BS_BINS];
static uint8_t        s_drawn_hist[HIST_H][BS_BINS];
static uint8_t        s_spec_band = 0xFF;
static uint8_t        s_hist_band = 0xFF;
static int            s_ant_band = -1;
static own_t          s_own[BS_OWNED];
static uint8_t        s_own_tag;
static uint8_t        s_own_repl;
static uint16_t       s_cur = 0xFFFFu; /* 0xFFFF = follow the live peak */
static bool           s_frozen;
static char           s_slot_own[2][41];
static char           s_slot_hits[6][41];
static fwog_link_rx_t s_rx;

static uint16_t c_bg, c_hdr, c_acc, c_dim, c_fg, c_bar, c_hold, c_peak, c_warn;
static uint16_t s_drawn_peak = 0xFFFFu;

static void colors_init(void) {
    c_bg   = st7789_rgb565(8, 10, 18);
    c_hdr  = st7789_rgb565(16, 28, 40);
    c_acc  = st7789_rgb565(80, 200, 120);
    c_dim  = st7789_rgb565(140, 150, 170);
    c_fg   = st7789_rgb565(230, 230, 230);
    c_bar  = st7789_rgb565(80, 170, 255);
    c_hold = st7789_rgb565(200, 140, 50);
    c_peak = st7789_rgb565(255, 230, 120);
    c_warn = st7789_rgb565(230, 180, 70);
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

static void send_cmd(uint8_t cmd) {
    bs_cmd_t m = { .type = BS_MSG_CMD, .cmd = cmd, .band = (uint8_t)s_band };
    if (ed_link_ok()) (void)ed_link_send(&m, sizeof m);
}

static void fmt_mhz(char *buf, size_t n, uint32_t hz) {
    snprintf(buf, n, "%u.%02u",
             (unsigned)(hz / 1000000u),
             (unsigned)((hz / 10000u) % 100u));
}

static uint16_t bar_px(uint8_t v) {
    uint16_t h = (uint16_t)((unsigned)v * SPEC_H / 80u);
    if (h > SPEC_H) h = SPEC_H;
    return h;
}

static uint16_t hist_col(uint8_t v) {
    if (v < 8u)  return c_bg;
    if (v < 22u) return st7789_rgb565(20, 40, 70);
    if (v < 40u) return st7789_rgb565(40, 90, 160);
    if (v < 58u) return c_bar;
    return c_peak;
}

static void spec_reset(void) {
    st7789_fill_rect(SPEC_X, SPEC_Y, (uint16_t)(BS_BINS * BIN_PX), SPEC_H, c_bg);
    memset(s_drawn_live, 0xFF, sizeof s_drawn_live);
    memset(s_drawn_hold, 0xFF, sizeof s_drawn_hold);
    s_drawn_peak = 0xFFFFu;
}

static void hist_reset(void) {
    st7789_fill_rect(SPEC_X, HIST_Y, (uint16_t)(BS_BINS * BIN_PX), HIST_H, c_bg);
    memset(s_drawn_hist, 0xFF, sizeof s_drawn_hist);
}

static void hist_push(uint8_t band, const uint8_t *bar) {
    band = (uint8_t)(band % 3u);
    memmove(&s_hist[band][1][0], &s_hist[band][0][0],
            (HIST_H - 1u) * BS_BINS);
    memcpy(s_hist[band][0], bar, BS_BINS);
}

static unsigned cur_bin(void) {
    if (s_cur < BS_BINS) return (unsigned)s_cur;
    if (s_st_ok && s_st.peak_bin < BS_BINS) return (unsigned)s_st.peak_bin;
    return 0u;
}

static uint32_t cur_hz(void) {
    if (!s_st_ok) return 0u;
    if (s_cur >= BS_BINS || s_cur == s_st.peak_bin) return s_st.peak_hz;
    return s_st.f0_hz + s_st.step_hz * (uint32_t)s_cur;
}

static int16_t cur_dbm(void) {
    if (!s_st_ok) return -120;
    const unsigned i = cur_bin();
    if (i == (unsigned)s_st.peak_bin) return s_st.peak_dbm;
    return (int16_t)((int)s_st.bar[i] - 110);
}

static uint8_t bin_h(unsigned i) {
    if (!s_st_ok || i >= BS_BINS) return 0u;
    uint8_t h = s_st.bar[i];
    const uint8_t hold = s_hold[s_st.band][i];
    if (hold > h) h = hold;
    return h;
}

static bool is_local_max(unsigned i) {
    const uint8_t h = bin_h(i);
    if (h < OWN_LIVE) return false;
    const uint8_t l = i > 0u ? bin_h(i - 1u) : 0u;
    const uint8_t r = i + 1u < BS_BINS ? bin_h(i + 1u) : 0u;
    return h >= l && h > r;
}

static unsigned next_peak(unsigned from, int dir) {
    for (unsigned n = 1u; n < BS_BINS; n++) {
        int i = (int)from + dir * (int)n;
        if (i < 0) i += (int)BS_BINS;
        if (i >= (int)BS_BINS) i -= (int)BS_BINS;
        if (is_local_max((unsigned)i)) return (unsigned)i;
    }
    int i = (int)from + dir;
    if (i < 0) i = (int)BS_BINS - 1;
    if (i >= (int)BS_BINS) i = 0;
    return (unsigned)i;
}

static void set_cur(unsigned i) {
    if (i >= BS_BINS) i = BS_BINS - 1u;
    if (s_cur == (uint16_t)i) return;
    s_cur = (uint16_t)i;
    s_spec_dirty = true;
    s_info_dirty = true;
}

static void paint_bin(unsigned i, uint8_t live, uint8_t hold, bool is_peak) {
    const uint16_t x = (uint16_t)(SPEC_X + i * BIN_PX);
    uint16_t hl = bar_px(live);
    uint16_t hh = bar_px(hold);
    if (hh < hl) hh = hl;
    st7789_fill_rect(x, SPEC_Y, BAR_W, SPEC_H, c_bg);
    if (hh > hl) {
        st7789_fill_rect(x, (uint16_t)(SPEC_Y + SPEC_H - hh),
                         BAR_W, (uint16_t)(hh - hl), c_hold);
    }
    if (hl > 0u) {
        const uint16_t col = is_peak ? c_peak : c_bar;
        st7789_fill_rect(x, (uint16_t)(SPEC_Y + SPEC_H - hl), BAR_W, hl, col);
    } else if (is_peak) {
        st7789_fill_rect(x, (uint16_t)(SPEC_Y + SPEC_H - 2u), BAR_W, 2, c_peak);
    }
    s_drawn_live[i] = live;
    s_drawn_hold[i] = hold;
}

static unsigned own_count(void) {
    unsigned n = 0;
    for (unsigned i = 0; i < BS_OWNED; i++) {
        if (s_own[i].used) n++;
    }
    return n;
}

static bool own_live(const own_t *o) {
    if (!s_st_ok || !o->used) return false;
    if (s_st.band != o->band || s_st.step_hz == 0u) return false;
    uint32_t bin = (o->hz > s_st.f0_hz) ? (o->hz - s_st.f0_hz) / s_st.step_hz : 0u;
    if (bin >= BS_BINS) bin = BS_BINS - 1u;
    return s_st.bar[bin] >= OWN_LIVE;
}

static void own_toggle(void) {
    if (!s_st_ok) return;
    const uint32_t hz = cur_hz();
    const uint8_t band = s_st.band;
    const uint32_t slop = s_st.step_hz ? s_st.step_hz / 2u : 250000u;
    for (unsigned i = 0; i < BS_OWNED; i++) {
        if (!s_own[i].used) continue;
        const uint32_t d = s_own[i].hz > hz ? s_own[i].hz - hz : hz - s_own[i].hz;
        if (s_own[i].band == band && d <= slop) {
            s_own[i].used = 0;
            s_own_dirty = true;
            return;
        }
    }
    int slot = -1;
    for (unsigned i = 0; i < BS_OWNED; i++) {
        if (!s_own[i].used) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0) {
        slot = (int)(s_own_repl % BS_OWNED);
        s_own_repl = (uint8_t)((s_own_repl + 1u) % BS_OWNED);
    }
    s_own[slot].hz = hz;
    s_own[slot].band = band;
    s_own[slot].tag = (uint8_t)(s_own_tag % 4u);
    s_own[slot].used = 1;
    s_own_tag = (uint8_t)((s_own_tag + 1u) % 4u);
    s_own_dirty = true;
}



void ed_bs_frame(const uint8_t *buf, size_t n) {
    if (n >= sizeof(bs_status_t) && buf[0] == BS_MSG_ST) {
            bs_status_t in;
            memcpy(&in, buf, sizeof in);
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
            if (first || memcmp(in.bar, s_st.bar, BS_BINS) != 0) {
                hist_push(in.band, in.bar);
                s_hist_dirty = true;
            }
            s_st = in;
            s_st_ok = true;
            if (info) s_info_dirty = true;
            if (spec) s_spec_dirty = true;
            if (hits) s_hits_dirty = true;
            if (own_count() > 0u) s_own_dirty = true;
    }
}

static void hold_clear(void) {
    memset(s_hold, 0, sizeof s_hold);
    memset(s_hist, 0, sizeof s_hist);
    memset(s_own, 0, sizeof s_own);
    s_spec_band = 0xFF;
    s_hist_band = 0xFF;
    s_spec_dirty = true;
    s_hist_dirty = true;
    s_hits_dirty = true;
    s_own_dirty = true;
}

static void paint_hist(void) {
    if (!s_st_ok) return;
    const uint8_t band = s_st.band;
    if (s_hist_band != band) {
        hist_reset();
        s_hist_band = band;
    }
    for (unsigned r = 0; r < HIST_H; r++) {
        for (unsigned i = 0; i < BS_BINS; i++) {
            const uint8_t v = s_hist[band][r][i];
            if (s_drawn_hist[r][i] == v) continue;
            s_drawn_hist[r][i] = v;
            st7789_fill_rect((uint16_t)(SPEC_X + i * BIN_PX),
                             (uint16_t)(HIST_Y + r), BAR_W, 1, hist_col(v));
        }
    }
}

static void paint_own(void) {
    char line[44];
    char a[20], b[20];
    char mhz[12];
    unsigned shown = 0;
    own_t vis[BS_OWNED];
    for (unsigned i = 0; i < BS_OWNED; i++) {
        if (s_own[i].used) vis[shown++] = s_own[i];
    }
    if (shown == 0u) {
        lcd_text_draw_padded_changed(8, OWN_Y, "own  freeze, YEL/BLU, GRN hold",
                                     40, 1, c_dim, c_bg, s_slot_own[0],
                                     sizeof s_slot_own[0]);
        lcd_text_draw_padded_changed(8, (uint16_t)(OWN_Y + 8u), "",
                                     40, 1, c_dim, c_bg, s_slot_own[1],
                                     sizeof s_slot_own[1]);
        return;
    }
    for (unsigned row = 0; row < 2u; row++) {
        a[0] = b[0] = 0;
        if (row * 2u < shown) {
            fmt_mhz(mhz, sizeof mhz, vis[row * 2u].hz);
            snprintf(a, sizeof a, "%s %s %s",
                     k_tag[vis[row * 2u].tag % 4u], mhz,
                     own_live(&vis[row * 2u]) ? "LIVE" : "---");
        }
        if (row * 2u + 1u < shown) {
            fmt_mhz(mhz, sizeof mhz, vis[row * 2u + 1u].hz);
            snprintf(b, sizeof b, "%s %s %s",
                     k_tag[vis[row * 2u + 1u].tag % 4u], mhz,
                     own_live(&vis[row * 2u + 1u]) ? "LIVE" : "---");
        }
        if (b[0] != 0) snprintf(line, sizeof line, "%-18s %s", a, b);
        else snprintf(line, sizeof line, "%s", a);
        lcd_text_draw_padded_changed(8, (uint16_t)(OWN_Y + row * 8u), line,
                                     40, 1, c_fg, c_bg, s_slot_own[row],
                                     sizeof s_slot_own[row]);
    }
}

/* Full-area fill_rect is what the panel shows as a flash: the DMA wipe
 * blanks the spectrum, then the bars are painted on top. Main sends a
 * status after every sweep (~2 Hz), and the old loop also painted every
 * 2 ms, so the body strobed continuously. Chrome stays put; only dirty
 * bins are patched. */
static void paint(void) {
    if (!s_lcd) return;
    char line[44];
    char mhz[12];

    if (!s_painted) {
        st7789_clear(c_bg);
        s_painted = true;
        s_chrome_dirty = s_info_dirty = s_spec_dirty = true;
        s_hist_dirty = s_hits_dirty = s_own_dirty = true;
        s_spec_band = 0xFF;
        s_hist_band = 0xFF;
    }

    if (s_chrome_dirty) {
        st7789_fill_rect(0, 0, ST7789_W, 28, c_hdr);
        lcd_text_draw_padded(8, 6, "BandScope", 12, 2, c_acc, c_hdr);
        lcd_text_draw_padded(8, FOOT_Y, "YEL/BLU band  GRN frz  hold=own",
                             40, 1, c_dim, c_bg);
        spec_reset();
        hist_reset();
        memset(s_slot_own, 0, sizeof s_slot_own);
        memset(s_slot_hits, 0, sizeof s_slot_hits);
        s_chrome_dirty = false;
        s_info_dirty = s_spec_dirty = s_hist_dirty = true;
        s_hits_dirty = s_own_dirty = true;
    }

    if (s_info_dirty) {
        const uint8_t mode = s_frozen ? BS_MODE_FREEZE :
                             (s_st_ok ? s_st.mode : BS_MODE_SWEEP);
        const char *ms = (mode < 3u) ? k_mode[mode] : "?";
        const uint16_t mc = (mode == BS_MODE_FREEZE) ? c_warn :
                            (mode == BS_MODE_HUNT) ? c_bar : c_acc;
        lcd_text_draw_padded(200, 10, ms, 8, 1, mc, c_hdr);
        lcd_text_draw_padded(8, FOOT_Y,
                             s_frozen ? "YEL/BLU peak  GRN hold=own" :
                                        "YEL/BLU band  GRN frz  hold=own",
                             40, 1, c_dim, c_bg);

        const int band = (s_st_ok && s_st.mode == BS_MODE_HUNT) ? (int)s_st.band : s_band;
        snprintf(line, sizeof line, "%s MHz", k_band[band]);
        lcd_text_draw_padded(8, 30, line, 40, 1, c_fg, c_bg);

        if (!s_st_ok) {
            lcd_text_draw_padded(8, 42, "waiting for main...", 40, 1, c_dim, c_bg);
        } else if (!s_st.ok) {
            lcd_text_draw_padded(8, 42, "radio 0 fail", 40, 1, c_warn, c_bg);
        } else {
            fmt_mhz(mhz, sizeof mhz, cur_hz());
            snprintf(line, sizeof line, "%s %+d dBm  @ %s MHz",
                     s_frozen ? "mark" : "peak", (int)cur_dbm(), mhz);
            lcd_text_draw_padded(8, 42, line, 40, 1, c_fg, c_bg);
        }

        if (s_st_ok) {
            char a[8], b[8], c[8];
            const uint32_t f1 = s_st.f0_hz + s_st.step_hz * (BS_BINS - 1u);
            const uint32_t fm = s_st.f0_hz + s_st.step_hz * ((BS_BINS - 1u) / 2u);
            snprintf(a, sizeof a, "%u", (unsigned)(s_st.f0_hz / 1000000u));
            snprintf(b, sizeof b, "%u", (unsigned)(fm / 1000000u));
            snprintf(c, sizeof c, "%u", (unsigned)(f1 / 1000000u));
            lcd_text_draw_padded(SPEC_X, AXIS_Y, a, 5, 1, c_dim, c_bg);
            lcd_text_draw_padded((uint16_t)(SPEC_X + 108u), AXIS_Y, b, 5, 1, c_dim, c_bg);
            lcd_text_draw_padded((uint16_t)(SPEC_X + 212u), AXIS_Y, c, 5, 1, c_dim, c_bg);
        }
        s_info_dirty = false;
    }

    if (s_spec_dirty) {
        if (!s_st_ok) {
            lcd_text_draw_padded(SPEC_X, (uint16_t)(SPEC_Y + SPEC_H / 2u),
                                 "no sweep yet", 22, 1, c_dim, c_bg);
        } else {
            const uint8_t band = s_st.band;
            if (s_spec_band != band) spec_reset();
            s_spec_band = band;
            for (unsigned i = 0; i < BS_BINS; i++) {
                const uint8_t live = s_st.bar[i];
                uint8_t hold = s_hold[band][i];
                if (live > hold) {
                    hold = live;
                    s_hold[band][i] = live;
                }
                const bool is_peak = (i == cur_bin());
                const bool was_peak = (i == (unsigned)s_drawn_peak);
                if (s_drawn_live[i] == live && s_drawn_hold[i] == hold &&
                    !is_peak && !was_peak) {
                    continue;
                }
                paint_bin(i, live, hold, is_peak);
            }
            s_drawn_peak = (uint16_t)cur_bin();
        }
        s_spec_dirty = false;
    }

    if (s_hist_dirty) {
        paint_hist();
        s_hist_dirty = false;
    }

    if (s_own_dirty) {
        paint_own();
        s_own_dirty = false;
    }

    if (s_hits_dirty) {
        lcd_text_draw_padded_changed(8, HITS_HDR_Y, "top 5", 40, 1, c_dim, c_bg,
                                     s_slot_hits[0], sizeof s_slot_hits[0]);
        for (unsigned i = 0; i < BS_TOP; i++) {
            const uint16_t y = (uint16_t)(HITS_Y + i * 8u);
            if (!s_st_ok || i >= s_st.n_hits) {
                lcd_text_draw_padded_changed(8, y, "", 40, 1, c_fg, c_bg,
                                             s_slot_hits[i + 1u],
                                             sizeof s_slot_hits[i + 1u]);
                continue;
            }
            fmt_mhz(mhz, sizeof mhz, s_st.hit[i].hz);
            const unsigned b = s_st.hit[i].band < 3u ? s_st.hit[i].band : 0u;
            snprintf(line, sizeof line, "%u  %s MHz  %+d dBm  %s",
                     i + 1u, mhz, (int)s_st.hit[i].dbm, k_band_s[b]);
            lcd_text_draw_padded_changed(8, y, line, 40, 1, c_fg, c_bg,
                                         s_slot_hits[i + 1u],
                                         sizeof s_slot_hits[i + 1u]);
        }
        s_hits_dirty = false;
    }
}

static void leds_peak(const fwog_power_t *p) {
    if (!s_leds || p->armed) return;
    int h = s_st_ok ? (int)s_st.peak_dbm + 110 : 0;
    if (h < 0) h = 0;
    unsigned lit = (unsigned)h / 12u;
    if (lit > 7u) lit = 7u;
    const bool hunt = s_st_ok && s_st.mode == BS_MODE_HUNT;
    for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++) {
        ws2812_set_color(i, hunt ? 18u : 2u, i < lit ? 28u : 2u, i < lit ? 8u : 4u);
    }
    ws2812_process();
}



static uint32_t s_bs_hy, s_bs_hb, s_bs_hg, s_bs_ry, s_bs_rb;
static uint8_t  s_bs_gfired, s_bs_bfired;

void ed_bs_enter(void) {
    colors_init();
    s_lcd = true;
    s_leds = true;
    s_st_ok = false;
    s_painted = false;
    s_chrome_dirty = s_info_dirty = s_spec_dirty = true;
    s_hist_dirty = s_hits_dirty = s_own_dirty = true;
    apply_antennas(s_band);
    send_cmd(BS_CMD_SWEEP);
}

void ed_bs_paint(void) { paint(); }

void ed_bs_buttons(const fwog_power_t *p, uint32_t now,
                   bool y_long, bool g_long, bool gray_long, bool red_tap) {
    (void)gray_long;
    const bool bdown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) != 0;
    if (p->buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) {
        s_bs_hy = s_bs_ry = now;
        if (s_frozen && s_st_ok) set_cur(next_peak(cur_bin(), -1));
        else {
            if (--s_band < 0) s_band = 2;
            apply_antennas(s_band);
            send_cmd(BS_CMD_SWEEP);
            s_info_dirty = true;
        }
    }
    if (p->buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_BLUE)) {
        s_bs_hb = s_bs_rb = now;
        if (s_frozen && s_st_ok) set_cur(next_peak(cur_bin(), 1));
        else {
            if (++s_band > 2) s_band = 0;
            apply_antennas(s_band);
            send_cmd(BS_CMD_SWEEP);
            s_info_dirty = true;
        }
    }
    if (s_frozen && s_st_ok &&
        (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) &&
        now - s_bs_hy >= HOLD_MS && now - s_bs_ry >= 90u) {
        s_bs_ry = now;
        set_cur(next_peak(cur_bin(), -1));
    }
    if (s_frozen && s_st_ok && bdown &&
        now - s_bs_hb >= HOLD_MS && now - s_bs_rb >= 90u) {
        s_bs_rb = now;
        set_cur(next_peak(cur_bin(), 1));
    }
    if (p->buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GREEN)) {
        s_bs_hg = now;
        s_bs_gfired = 0;
    }
    if (p->buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_BLUE)) s_bs_bfired = 0;
    if (!s_bs_bfired && s_st_ok && bdown && s_bs_hb != 0u &&
        now - s_bs_hb >= HOLD_MS) {
        s_bs_bfired = 1;
        own_toggle();
    }
    if (!bdown) s_bs_bfired = 0;
    if ((p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_GREEN)) && !g_long && !s_bs_gfired) {
        s_frozen = !s_frozen;
        if (s_frozen) set_cur(cur_bin());
        else s_cur = 0xFFFFu;
        s_info_dirty = true;
        s_spec_dirty = true;
        send_cmd(BS_CMD_FREEZE);
    }
    if ((p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_GRAY)) && !gray_long) {
        s_frozen = false;
        s_cur = 0xFFFFu;
        s_info_dirty = true;
        send_cmd(BS_CMD_HUNT);
    }
    if (red_tap && (p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_RED))) {
        hold_clear();
        send_cmd(BS_CMD_CLEAR);
    }
    (void)y_long;
}
