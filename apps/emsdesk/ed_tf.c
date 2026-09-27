#include "fwog_display.h"
#include "ed_link.h"
#include "ed_tf.h"
#include "tf_proto.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>



#define TF_HOLD_MS  700u
#define TF_AVG      8u    /* 8 * 80 ms status ≈ 640 ms per column */
#define TF_HIST     36u
#define TF_COL_W    8u
#define TF_HIST_X   16u
#define TF_G1_Y     86u   /* u  hunt RSSI1 */
#define TF_G1_H     50u   /* gap before me so bars do not collide */
#define TF_G2_Y     156u  /* me local RSSI0 */
#define TF_G2_H     50u
#define TF_NDIG     6

static const uint32_t k_park_hz[] = {
    315000000u, 433920000u, 915000000u
};
#define TF_NPARK ((int)(sizeof k_park_hz / sizeof k_park_hz[0]))

static bool           s_lcd, s_link, s_leds, s_beacon, s_freeze;
static tf_status_t    s_st;
static bool           s_st_ok;
static bool           s_chrome_dirty = true;
static bool           s_hist_dirty = true;
static int8_t         s_h0[TF_HIST], s_h1[TF_HIST];
static uint8_t        s_hhead, s_hn;
static int32_t        s_sum0, s_sum1;
static uint8_t        s_avg_n;
static uint32_t       s_hz = 433920000u;
static int            s_dig; /* 0 = 100 MHz … 5 = 1 kHz */
static int            s_park = 1;
static fwog_link_rx_t s_rx;
static char           s_line_rssi[44];
static char           s_line_note[48];

static fwog_ant_t ant_for(uint32_t hz) {
    if (hz <= TF_BAND_LOW_MAX) return FWOG_ANT_200MHZ;
    if (hz < TF_BAND_HIGH_MIN) return FWOG_ANT_400MHZ;
    return FWOG_ANT_900MHZ;
}

static void apply_ant(void) {
    const fwog_ant_t a = ant_for(s_hz);
    (void)fwog_ioexp_link_set_antennas(a, a);
}

static void send_cmd(uint8_t cmd) {
    tf_cmd_t m = { .type = TF_MSG_CMD, .cmd = cmd, .freq_hz = s_hz };
    if (ed_link_ok()) (void)ed_link_send(&m, sizeof m);
}

static int8_t rssi_h(int16_t dbm, unsigned h) {
    int v = (int)dbm + 110;
    if (v < 0) v = 0;
    if (v > (int)h - 2) v = (int)h - 2;
    return (int8_t)v;
}

static void hist_push(int16_t a, int16_t b) {
    if (s_freeze) return;
    s_sum0 += a;
    s_sum1 += b;
    s_avg_n++;
    if (s_avg_n < TF_AVG) return;
    s_h0[s_hhead] = rssi_h((int16_t)(s_sum0 / (int)TF_AVG), TF_G2_H);
    s_h1[s_hhead] = rssi_h((int16_t)(s_sum1 / (int)TF_AVG), TF_G1_H);
    s_hhead = (uint8_t)((s_hhead + 1u) % TF_HIST);
    if (s_hn < TF_HIST) s_hn++;
    s_sum0 = s_sum1 = 0;
    s_avg_n = 0;
    s_hist_dirty = true;
}

static void hist_clear(void) {
    memset(s_h0, 0, sizeof s_h0);
    memset(s_h1, 0, sizeof s_h1);
    s_hhead = s_hn = s_avg_n = 0;
    s_sum0 = s_sum1 = 0;
    s_hist_dirty = true;
}



void ed_tf_frame(const uint8_t *buf, size_t n) {
    if (n >= sizeof(tf_status_t) && buf[0] == TF_MSG_ST) {
            memcpy(&s_st, buf, sizeof s_st);
            s_st_ok = true;
            hist_push(s_st.rssi0, s_st.rssi1);
    }
}

static uint32_t place_khz(int dig) {
    uint32_t p = 1u;
    int i;
    for (i = 5; i > dig; i--) p *= 10u;
    return p;
}

static void nudge(int dir) {
    const uint32_t khz = s_hz / TF_KHZ_STEP;
    const uint32_t pl = place_khz(s_dig);
    uint32_t d = (khz / pl) % 10u;
    int nd = (int)d + dir;
    uint32_t next;
    if (nd < 0) nd = 9;
    if (nd > 9) nd = 0;
    next = (khz - d * pl) + (uint32_t)nd * pl;
    s_hz = next * TF_KHZ_STEP;
    hist_clear();
    apply_ant();
    send_cmd(s_beacon ? TF_CMD_BEACON : TF_CMD_LISTEN);
    s_chrome_dirty = true;
}

static void park_next(void) {
    s_park = (s_park + 1) % TF_NPARK;
    s_hz = k_park_hz[s_park];
    hist_clear();
    apply_ant();
    send_cmd(s_beacon ? TF_CMD_BEACON : TF_CMD_LISTEN);
    s_chrome_dirty = true;
}

static void draw_trace(unsigned y, unsigned h, const int8_t *hist,
                       uint16_t ink, uint16_t bg) {
    unsigned i;
    st7789_fill_rect(TF_HIST_X, (uint16_t)y, (uint16_t)(TF_HIST * TF_COL_W),
                     (uint16_t)h, bg);
    for (i = 0; i < s_hn; i++) {
        const unsigned idx = (s_hhead + TF_HIST - s_hn + i) % TF_HIST;
        const uint16_t x = (uint16_t)(TF_HIST_X + i * TF_COL_W);
        uint16_t bar = (uint16_t)hist[idx];
        if (bar < 2u) bar = 2u;
        st7789_fill_rect(x, (uint16_t)(y + h - bar), TF_COL_W - 1u, bar, ink);
    }
}

static void paint(void) {
    if (!s_lcd) return;
    const uint16_t bg  = st7789_rgb565(8, 10, 18);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    const uint16_t fg  = st7789_rgb565(230, 230, 230);
    const uint16_t warn = st7789_rgb565(220, 160, 60);
    const uint16_t ink0 = st7789_rgb565(80, 120, 160);
    char mhz[12];
    char line[48];
    unsigned i;
    if (s_chrome_dirty) {
        st7789_fill_rect(0, 0, 320, 240, bg);
        lcd_text_draw_padded(8, 6, "TwinFox", 16, 2, acc, bg);
        snprintf(mhz, sizeof mhz, "%03u.%03u",
                 (unsigned)(s_hz / 1000000u),
                 (unsigned)((s_hz / 1000u) % 1000u));
        {
            const uint16_t x0 = 140;
            for (i = 0; i < 7u; i++) {
                char ch[2] = { mhz[i], 0 };
                int dig = -1;
                if (i < 3u) dig = (int)i;
                else if (i > 3u) dig = (int)i - 1;
                const bool cur = (dig == s_dig);
                lcd_text_draw_padded((uint16_t)(x0 + i * 12u), 8, ch, 1, 2,
                                     cur ? bg : fg, cur ? acc : bg);
            }
        }
        lcd_text_draw_padded(8, 40, tf_hz_ok(s_hz) ?
                             (s_beacon ? "BEACON" : "LISTEN") : "GAP",
                             10, 1, tf_hz_ok(s_hz) ? (s_beacon ? warn : acc) : warn, bg);
        lcd_text_draw_padded(8, 76, "u  hunt  ~0.64s/col", 36, 1, acc, bg);
        lcd_text_draw_padded(8, 146, "me  local", 36, 1, dim, bg);
        lcd_text_draw_padded(8, 224,
                             "GRY/RED digit  GRN+ YEL-  hold YEL TX  GRY band",
                             52, 1, dim, bg);
        s_line_rssi[0] = s_line_note[0] = '\0';
        s_chrome_dirty = false;
        s_hist_dirty = true;
    }

    snprintf(line, sizeof line, "CC1101 300-348  387-464  779-928 MHz");
    lcd_text_draw_padded_changed(8, 48, line, 42, 1, dim, bg,
                                 s_line_note, sizeof s_line_note);

    snprintf(line, sizeof line, "me %+d   u %+d%s",
             s_st_ok ? (int)s_st.rssi0 : 0, s_st_ok ? (int)s_st.rssi1 : 0,
             s_freeze ? "  FRZ" : "");
    lcd_text_draw_padded_changed(8, 62, line, 40, 1, fg, bg,
                                 s_line_rssi, sizeof s_line_rssi);

    if (s_hist_dirty) {
        draw_trace(TF_G1_Y, TF_G1_H, s_h1, acc, bg);
        draw_trace(TF_G2_Y, TF_G2_H, s_h0, ink0, bg);
        s_hist_dirty = false;
    }
}



static bool s_tf_ywas, s_tf_gwas, s_tf_rwas, s_tf_bwas;
static bool s_tf_yhold, s_tf_ghold, s_tf_rtap, s_tf_bhold;
static uint32_t s_tf_yms, s_tf_gms, s_tf_rms, s_tf_bms;

void ed_tf_enter(void) {
    s_lcd = true;
    s_chrome_dirty = true;
    apply_ant();
    send_cmd(TF_CMD_LISTEN);
}

void ed_tf_paint(void) { paint(); }

void ed_tf_buttons(const fwog_power_t *p, uint32_t now,
                   bool y_long, bool g_long, bool gray_long, bool red_tap) {
    (void)g_long;
    const bool ydown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) != 0;
    const bool rydown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_GRAY)) != 0;
    const bool rdown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_RED)) != 0;
    const bool bdown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) != 0;

    if (rdown && !s_tf_rwas) { s_tf_rms = now; s_tf_rtap = true; }
    if (rdown && s_tf_rtap && (now - s_tf_rms) >= TF_HOLD_MS) s_tf_rtap = false;
    if (!rdown && s_tf_rwas && s_tf_rtap && red_tap) {
        s_dig = (s_dig + 1) % TF_NDIG;
        s_chrome_dirty = true;
    }
    s_tf_rwas = rdown;
    if (p->armed) return;

    if (!ydown && s_tf_ywas && !y_long) nudge(-1);
    s_tf_ywas = ydown;

    if (rydown && !s_tf_gwas) { s_tf_gms = now; s_tf_ghold = false; }
    if (!rydown && s_tf_gwas && !gray_long) {
        if (--s_dig < 0) s_dig = TF_NDIG - 1;
        s_chrome_dirty = true;
    }
    s_tf_gwas = rydown;

    if (p->buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GREEN)) nudge(1);

    if (bdown && !s_tf_bwas) { s_tf_bms = now; s_tf_bhold = false; }
    if (bdown && !s_tf_bhold && (now - s_tf_bms) >= TF_HOLD_MS) {
        s_tf_bhold = true;
        s_beacon = !s_beacon;
        send_cmd(s_beacon ? TF_CMD_BEACON : TF_CMD_LISTEN);
        s_chrome_dirty = true;
    }
    if (!bdown && s_tf_bwas && !s_tf_bhold) {
        s_freeze = !s_freeze;
        s_chrome_dirty = true;
        s_hist_dirty = true;
    }
    s_tf_bwas = bdown;
    (void)s_tf_yhold;
}
