/* OrcaLobby 003 — landing + LanFerry / PingHalo / BleDeck / AirMaraud. */
#include "fwog_display.h"
#include "land_home.h"
#include "ol_am.h"
#include "ol_bd.h"
#include "ol_lf.h"
#include "ol_link.h"
#include "ol_ph.h"
#include "ol_proto.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

FWOG_POWER_DEFAULT();

#define OL_N        4
#define OL_HOLD_MS  700u
#define OL_TAP_MS   400u

static const char *k_name[OL_N] = {
    "LanFerry", "PingHalo", "BleDeck", "AirMaraud"
};
static const char *k_blurb[OL_N] = {
    "SoftAP file ferry",
    "BLE hunt / labels",
    "BLE HID remote",
    "2.4 GHz lab scan"
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

bool ol_link_ok(void) { return s_link; }

void ol_link_send(const void *m, size_t n) {
    if (s_link) (void)fwog_link_uart_send_frame(m, n);
}

bool ol_leds_ok(void) { return s_leds; }

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
    if (s_app == 1) ol_lf_enter();
    else if (s_app == 2) ol_ph_enter();
    else if (s_app == 3) ol_bd_enter();
    else if (s_app == 4) ol_am_enter();
}

static void paint_home(void) {
    if (!s_lcd || !s_chrome_dirty) return;
    land_paint_home("OrcaLobby", "C6 modes, no BattleBridge",
                    k_name, k_blurb, OL_N, s_sel,
                    c_bg, c_acc, c_dim, c_fg);
    s_chrome_dirty = false;
}

static void leds_tick(void) {
    if (!s_leds || s_power_armed || s_app != 0) return;
    for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++) {
        const bool on = ((int)i == s_sel);
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
        if (s_app == 1) ol_lf_frame(s_rx.buf, n);
        else if (s_app == 2) ol_ph_frame(s_rx.buf, n);
        else if (s_app == 3) ol_bd_frame(s_rx.buf, n);
        else if (s_app == 4) ol_am_frame(s_rx.buf, n);
    }
}

int main(void) {
    board_init();
    fwog_splash_bind("OrcaLobby", "004");
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
            (now - s_hold_red) >= OL_TAP_MS) {
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
                !s_y_long && now - s_hold_y >= OL_HOLD_MS) go_home();
            if ((p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GREEN)) &&
                !s_g_long && now - s_hold_g >= OL_HOLD_MS) go_home();
            if ((p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GRAY)) &&
                !s_gray_long && now - s_hold_gray >= OL_HOLD_MS) go_home();
        } else {
            if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_GRAY)) && !s_gray_long) {
                if (--s_sel < 0) s_sel = OL_N - 1;
                s_chrome_dirty = true;
            }
            if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_RED)) && s_red_tap) {
                if (++s_sel >= OL_N) s_sel = 0;
                s_chrome_dirty = true;
            }
            if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_GREEN)) && !s_g_long)
                enter_sel();
        }

        if (s_app == 0) {
            paint_home();
            leds_tick();
        } else if (s_app == 1) {
            ol_lf_buttons(&p, s_y_long, s_g_long, s_gray_long, s_red_tap);
            ol_lf_paint();
        } else if (s_app == 2) {
            ol_ph_buttons(&p, s_y_long, s_g_long, s_gray_long, s_red_tap);
            ol_ph_paint();
        } else if (s_app == 3) {
            ol_bd_buttons(&p, s_y_long, s_g_long, s_gray_long, s_red_tap);
            ol_bd_paint();
        } else if (s_app == 4) {
            ol_am_buttons(&p, s_y_long, s_g_long, s_gray_long, s_red_tap);
            ol_am_paint();
        }
        sleep_ms(2);
    }
}
