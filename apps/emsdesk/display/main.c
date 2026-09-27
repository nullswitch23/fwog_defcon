/* EmsDesk 003 — landing + BandScope TwinFox TireEar ISMburst FobReplay ChirpMail OpticClick. */
#include "fwog_display.h"
#include "land_home.h"
#include "ed_link.h"
#include "ed_proto.h"
#include "ed_bs.h"
#include "ed_tf.h"
#include "ed_te.h"
#include "ed_ib.h"
#include "ed_fob.h"
#include "ed_cm.h"
#include "ed_oc.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

FWOG_POWER_DEFAULT();

#define ED_N        7
#define ED_HOLD_MS  700u
#define ED_TAP_MS   400u

static const char *k_name[ED_N] = {
    "BandScope", "TwinFox", "TireEar", "ISMburst",
    "FobReplay", "ChirpMail", "OpticClick"
};
static const char *k_blurb[ED_N] = {
    "sub-GHz RSSI sweep",
    "dual-CC1101 hunt",
    "TPMS 315/433",
    "ASK/2-FSK bursts",
    "unused-code queue",
    "433.92 mailbox",
    "IR capture / library"
};

static bool      s_lcd, s_link, s_leds;
static int       s_sel;
static int       s_app;
static bool      s_chrome_dirty = true;
static uint32_t  s_hold_y, s_hold_g, s_hold_gray, s_hold_red;
static bool      s_y_long, s_g_long, s_gray_long, s_red_tap;
static bool      s_power_armed, s_goodbye;
static fwog_link_rx_t s_rx;
static uint16_t  c_bg, c_acc, c_dim, c_fg;

bool ed_link_ok(void) { return s_link; }

void ed_link_send(const void *m, size_t n) {
    if (s_link) (void)fwog_link_uart_send_frame(m, n);
}

static void colors_init(void) {
    c_bg  = st7789_rgb565(8, 10, 18);
    c_acc = st7789_rgb565(80, 200, 120);
    c_dim = st7789_rgb565(140, 150, 170);
    c_fg  = st7789_rgb565(230, 230, 230);
}

static void send_sel(uint8_t app) {
    desk_sel_t m = { .type = DESK_MSG_SEL, .app = app };
    if (s_link) (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void screen_wipe(void) {
    if (s_lcd) st7789_clear(c_bg);
    s_chrome_dirty = true;
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
        board_backlight(255);
        fwog_splash_boot();
        screen_wipe();
    }
}

static void go_home(void) {
    s_app = 0;
    s_y_long = s_g_long = s_gray_long = true;
    send_sel(0);
    screen_wipe();
}

static void enter_sel(void) {
    s_app = s_sel + 1;
    send_sel((uint8_t)(s_sel + 1u));
    screen_wipe();
    if (s_app == 1) ed_bs_enter();
    else if (s_app == 2) ed_tf_enter();
    else if (s_app == 3) ed_te_enter();
    else if (s_app == 4) ed_ib_enter();
    else if (s_app == 5) ed_fob_enter();
    else if (s_app == 6) ed_cm_enter();
    else if (s_app == 7) ed_oc_enter();
}

static void paint_home(void) {
    if (!s_lcd || !s_chrome_dirty) return;
    land_paint_home("EmsDesk", "ISM kit, one radio live",
                    k_name, k_blurb, ED_N, s_sel,
                    c_bg, c_acc, c_dim, c_fg);
    s_chrome_dirty = false;
}

static void leds_tick(void) {
    if (!s_leds || s_power_armed) return;
    for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++) {
        const bool on = (s_app == 0) ? ((int)i == s_sel) : true;
        ws2812_set_color(i, 2u, on ? 22u : 2u, on ? 18u : 6u);
    }
    ws2812_process();
}

static void poll_link(void) {
    uint8_t b;
    size_t n;
    while (fwog_link_uart_read(&b)) {
        if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
        if (fwog_ioexp_link_handle(s_rx.buf, n)) continue;
        if (s_app == 1) ed_bs_frame(s_rx.buf, n);
        else if (s_app == 2) ed_tf_frame(s_rx.buf, n);
        else if (s_app == 3) ed_te_frame(s_rx.buf, n);
        else if (s_app == 4) ed_ib_frame(s_rx.buf, n);
        else if (s_app == 5) ed_fob_frame(s_rx.buf, n);
        else if (s_app == 6) ed_cm_frame(s_rx.buf, n);
        else if (s_app == 7) ed_oc_frame(s_rx.buf, n);
    }
}

int main(void) {
    board_init();
    fwog_splash_bind("EmsDesk", "004");
    s_leds = ws2812_init(pio0, 0u);
    colors_init();
    lcd_bringup();
    s_link = fwog_link_uart_init(FWOG_LINK_BAUD);
    fwog_link_rx_init(&s_rx);
    send_sel(0);

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
            (now - s_hold_red) >= ED_TAP_MS) {
            s_red_tap = false;
        }

        poll_link();
        if (s_goodbye || p.armed) {
            sleep_ms(2);
            continue;
        }

        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) {
            s_hold_y = now;
            s_y_long = false;
        }
        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GREEN)) {
            s_hold_g = now;
            s_g_long = false;
        }
        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GRAY)) {
            s_hold_gray = now;
            s_gray_long = false;
        }

        if (s_app != 0) {
            if ((p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) &&
                !s_y_long && now - s_hold_y >= ED_HOLD_MS) go_home();
            if ((p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GREEN)) &&
                !s_g_long && now - s_hold_g >= ED_HOLD_MS) go_home();
            if ((p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GRAY)) &&
                !s_gray_long && now - s_hold_gray >= ED_HOLD_MS) go_home();
        } else {
            if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_GRAY)) && !s_gray_long) {
                if (--s_sel < 0) s_sel = ED_N - 1;
                s_chrome_dirty = true;
            }
            if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_RED)) && s_red_tap) {
                if (++s_sel >= ED_N) s_sel = 0;
                s_chrome_dirty = true;
            }
            if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_GREEN)) && !s_g_long)
                enter_sel();
        }

        if (s_app == 0) {
            paint_home();
        } else {
            if (s_app == 1) {
                ed_bs_buttons(&p, now, s_y_long, s_g_long, s_gray_long, s_red_tap);
                ed_bs_paint();
            } else if (s_app == 2) {
                ed_tf_buttons(&p, now, s_y_long, s_g_long, s_gray_long, s_red_tap);
                ed_tf_paint();
            } else if (s_app == 3) {
                ed_te_buttons(&p, now, s_y_long, s_g_long, s_gray_long, s_red_tap);
                ed_te_paint();
            } else if (s_app == 4) {
                ed_ib_buttons(&p, now, s_y_long, s_g_long, s_gray_long, s_red_tap);
                ed_ib_paint();
            } else if (s_app == 5) {
                ed_fob_buttons(&p, now, s_y_long, s_g_long, s_gray_long, s_red_tap);
                ed_fob_paint();
            } else if (s_app == 6) {
                ed_cm_buttons(&p, now, s_y_long, s_g_long, s_gray_long, s_red_tap);
                ed_cm_paint();
            } else if (s_app == 7) {
                ed_oc_buttons(&p, now, s_y_long, s_g_long, s_gray_long, s_red_tap);
                ed_oc_paint();
            }
        }
        leds_tick();
        sleep_ms(2);
    }
}
