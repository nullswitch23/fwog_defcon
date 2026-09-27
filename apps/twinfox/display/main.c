#include "fwog_display.h"
#include "tf_proto.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

FWOG_POWER_DEFAULT();

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
    if (s_link) (void)fwog_link_uart_send_frame(&m, sizeof m);
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

static void lcd_bringup(void) {
    absolute_time_t deadline;
    st7789_init_begin();
    deadline = make_timeout_time_ms(500);
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

static void poll_link(void) {
    uint8_t b;
    size_t n;
    while (fwog_link_uart_read(&b)) {
        if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
        if (fwog_ioexp_link_handle(s_rx.buf, n)) continue;
        if (n >= sizeof(tf_status_t) && s_rx.buf[0] == TF_MSG_ST) {
            memcpy(&s_st, s_rx.buf, sizeof s_st);
            s_st_ok = true;
            hist_push(s_st.rssi0, s_st.rssi1);
        }
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

int main(void) {
    bool ywas = false, gwas = false, rwas = false;
    bool yhold = false, ghold = false, rtap = false;
    uint32_t yms = 0, gms = 0, rms = 0;

    board_init();
    fwog_splash_bind("TwinFox", "005");
    s_leds = ws2812_init(pio0, 0u);
    lcd_bringup();
    apply_ant();
    s_link = fwog_link_uart_init(FWOG_LINK_BAUD);
    fwog_link_rx_init(&s_rx);
    send_cmd(TF_CMD_LISTEN);

    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        const fwog_power_t p = fwog_power_poll(now);
        const bool ydown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) != 0;
        const bool rydown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GRAY)) != 0;
        poll_link();

        const bool rdown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_RED)) != 0;

        /* Red down arms ship immediately (p.armed). Digit-right is a short
         * tap on release; a hold past TF_HOLD_MS is ship, not a cursor move. */
        if (rdown && !rwas) {
            rms = now;
            rtap = true;
        }
        if (rdown && rtap && (now - rms) >= TF_HOLD_MS) rtap = false;
        if (!rdown && rwas && rtap) {
            s_dig = (s_dig + 1) % TF_NDIG;
            s_chrome_dirty = true;
        }
        rwas = rdown;

        if (p.armed) {
            paint();
            sleep_ms(2);
            continue;
        }

        if (ydown && !ywas) {
            yms = now;
            yhold = false;
        }
        if (ydown && !yhold && (now - yms) >= TF_HOLD_MS) {
            yhold = true;
            s_beacon = !s_beacon;
            send_cmd(s_beacon ? TF_CMD_BEACON : TF_CMD_LISTEN);
            s_chrome_dirty = true;
        }
        if (!ydown && ywas && !yhold) nudge(-1);
        ywas = ydown;

        if (rydown && !gwas) {
            gms = now;
            ghold = false;
        }
        if (rydown && !ghold && (now - gms) >= TF_HOLD_MS) {
            ghold = true;
            park_next();
        }
        if (!rydown && gwas && !ghold) {
            if (--s_dig < 0) s_dig = TF_NDIG - 1;
            s_chrome_dirty = true;
        }
        gwas = rydown;

        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GREEN)) nudge(1);
        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_BLUE)) {
            s_freeze = !s_freeze;
            s_chrome_dirty = true;
            s_hist_dirty = true;
        }
        if (s_leds && !p.armed && s_st_ok) {
            int h = (int)s_st.rssi1 + 100;
            if (h < 0) h = 0;
            unsigned lit = (unsigned)h / 12u;
            if (lit > 7u) lit = 7u;
            for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++) {
                ws2812_set_color(i, s_beacon ? 20u : 2u, i < lit ? 30u : 2u, 8u);
            }
            ws2812_process();
        }
        paint();
        sleep_ms(2);
    }
}
