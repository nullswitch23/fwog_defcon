/* BleDeck — five buttons as a BLE HID remote via Bottlenose. */
#include "fwog_display.h"
#include "bd_proto.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

FWOG_POWER_DEFAULT();

typedef struct {
    const char *label;
    uint8_t     kind;   /* 0 key, 1 consumer */
    uint16_t    usage;
} bd_slot_t;

#define BD_NPAGE 4

/* 0 D-pad (AGENTS.md), 1 media, 2 talk, 3 desk. */
static const bd_slot_t k_page[BD_NPAGE][5] = {
    {
        { "OK",    0, 0x28 },
        { "Left",  0, 0x50 },
        { "Right", 0, 0x4F },
        { "Up",    0, 0x52 },
        { "Down",  0, 0x51 },
    },
    {
        { "Play",  1, 0x00CD },
        { "Vol-",  1, 0x00EA },
        { "Vol+",  1, 0x00E9 },
        { "Next",  1, 0x00B5 },
        { "Prev",  1, 0x00B6 },
    },
    {
        { "F5",    0, 0x3A },
        { "Esc",   0, 0x29 },
        { "PgDn",  0, 0x4E },
        { "PgUp",  0, 0x4B },
        { "B",     0, 0x05 },
    },
    {
        { "Mute",  1, 0x00E2 },
        { "Bri-",  1, 0x0070 },
        { "Bri+",  1, 0x006F },
        { "PrtSc", 0, 0x46 },
        { "Home",  1, 0x0223 },
    },
};

static const char *const k_page_name[BD_NPAGE] = {
    "D-pad", "media", "talk", "desk"
};

/* Indexed by fwog_btn_id_t: gray, yellow, green, blue, red. */
static const uint8_t k_btn_rgb[5][3] = {
    { 28, 28, 32 },
    { 48, 36,  0 },
    {  0, 40,  8 },
    {  8, 12, 48 },
    { 48,  0,  0 },
};

static const uint8_t k_slot_btn[5] = {
    FWOG_BTN_GREEN, FWOG_BTN_YELLOW, FWOG_BTN_BLUE, FWOG_BTN_GRAY, FWOG_BTN_RED
};

static bool           s_lcd, s_link, s_leds, s_chrome_dirty = true;
static bool           s_want_adv = true;
static int            s_page;
static bd_status_t    s_st;
static bool           s_st_ok;
static fwog_link_rx_t s_rx;
static uint8_t        s_view = 0xFF;
static uint8_t        s_drawn_pct = 0xFF;
static uint8_t        s_down;
static uint8_t        s_bat = 0xFF;
static uint32_t       s_bat_ms;
static char           s_line_a[44];
static char           s_line_b[44];
static char           s_line_c[44];
static char           s_line_d[44];
static char           s_line_e[44];
static char           s_line_f[44];

static void send_cmd(uint8_t cmd, uint8_t on, uint16_t usage) {
    bd_cmd_t m = {
        .type = BD_MSG_CMD, .cmd = cmd, .on = on, .usage = usage
    };
    if (s_link) (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void fire(unsigned slot, int down) {
    const bd_slot_t *s = &k_page[s_page][slot];
    send_cmd(s->kind ? BD_CMD_CC : BD_CMD_KEY, down ? 1u : 0u, s->usage);
}

static uint8_t vbat_pct(uint16_t mv) {
    if (mv >= 4200u) return 100u;
    if (mv <= 3300u) return 0u;
    return (uint8_t)(((uint32_t)(mv - 3300u) * 100u) / 900u);
}

static void bat_tick(uint32_t now) {
    if (now - s_bat_ms < 2000u && s_bat != 0xFF) return;
    s_bat_ms = now;
    uint16_t mv = 0;
    bool thermal = false;
    if (!bq25896_read_vbat_mv(&mv, &thermal)) return;
    const uint8_t pct = vbat_pct(mv);
    if (pct == s_bat) return;
    s_bat = pct;
    send_cmd(BD_CMD_BAT, pct, 0);
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

static void slots_reset(void) {
    s_line_a[0] = s_line_b[0] = s_line_c[0] = s_line_d[0] = s_line_e[0] = '\0';
    s_line_f[0] = '\0';
    s_drawn_pct = 0xFF;
}

static uint8_t screen_id(uint8_t fl) {
    if (fl == BD_FLASH_HOLD) return 1u;
    if (fl == BD_FLASH_SYNC || fl == BD_FLASH_WRITE) return 2u;
    if (fl == BD_FLASH_OK) return 3u;
    if (fl == BD_FLASH_FAIL) return 4u;
    if (!s_st_ok || !s_st.hello) return 5u;
    return 6u;
}

static uint16_t slot_col(unsigned slot, uint16_t idle,
                         uint16_t g, uint16_t y, uint16_t b,
                         uint16_t gr, uint16_t r) {
    if (!(s_down & FWOG_BTN_BIT(k_slot_btn[slot]))) return idle;
    if (slot == 0) return g;
    if (slot == 1) return y;
    if (slot == 2) return b;
    if (slot == 3) return gr;
    return r;
}

static void paint(void) {
    if (!s_lcd) return;
    const uint16_t bg  = st7789_rgb565(8, 10, 18);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    const uint16_t fg  = st7789_rgb565(230, 230, 230);
    const uint16_t yel = st7789_rgb565(255, 214, 48);
    const uint16_t rec = st7789_rgb565(220, 40, 40);
    const uint16_t gn  = st7789_rgb565(48, 200, 80);
    const uint16_t blu = st7789_rgb565(80, 140, 255);
    const uint16_t gry = st7789_rgb565(180, 180, 190);
    char line[44];
    const uint8_t fl = s_st_ok ? s_st.flash : 0u;
    const uint8_t view = screen_id(fl);

    if (s_chrome_dirty) {
        st7789_fill_rect(0, 0, 320, 40, bg);
        lcd_text_draw_padded(8, 8, "BleDeck", 16, 2, acc, bg);
        s_chrome_dirty = false;
        s_view = 0xFF;
    }

    if (view != s_view) {
        st7789_fill_rect(0, 40, 320, 200, bg);
        slots_reset();
        s_view = view;
    }

    if (fl == BD_FLASH_HOLD) {
        lcd_text_draw_padded_changed(8, 56, "flash wiliOG C6 image", 36, 1, yel, bg,
                                     s_line_a, sizeof s_line_a);
        lcd_text_draw_padded_changed(8, 80, "1 hold BOOT on Bottlenose", 36, 1, fg, bg,
                                     s_line_b, sizeof s_line_b);
        lcd_text_draw_padded_changed(8, 96, "2 tap RESET, release BOOT", 36, 1, fg, bg,
                                     s_line_c, sizeof s_line_c);
        lcd_text_draw_padded_changed(8, 112, "3 press GREEN", 36, 1, fg, bg,
                                     s_line_d, sizeof s_line_d);
        lcd_text_draw_padded_changed(8, 210, "YELLOW cancels", 36, 1, dim, bg,
                                     s_line_e, sizeof s_line_e);
        return;
    }
    if (fl == BD_FLASH_SYNC || fl == BD_FLASH_WRITE) {
        lcd_text_draw_padded_changed(8, 56,
                                     fl == BD_FLASH_SYNC ? "syncing ROM..." : "writing flash",
                                     36, 1, yel, bg, s_line_a, sizeof s_line_a);
        snprintf(line, sizeof line, "%u %%", (unsigned)s_st.pct);
        lcd_text_draw_padded_changed(8, 88, line, 16, 2, acc, bg,
                                     s_line_b, sizeof s_line_b);
        if (s_drawn_pct != s_st.pct) {
            unsigned bar = (unsigned)s_st.pct * 3u;
            if (bar > 300u) bar = 300u;
            st7789_fill_rect(8, 130, 304, 18, bg);
            if (bar) st7789_fill_rect(8, 130, (uint16_t)bar, 18, acc);
            s_drawn_pct = s_st.pct;
        }
        lcd_text_draw_padded_changed(8, 210, "keep BOOT released", 36, 1, dim, bg,
                                     s_line_e, sizeof s_line_e);
        return;
    }
    if (fl == BD_FLASH_OK) {
        lcd_text_draw_padded_changed(8, 56, "C6 flash ok  tap RESET", 36, 1, acc, bg,
                                     s_line_a, sizeof s_line_a);
        lcd_text_draw_padded_changed(8, 76, "waiting for BN HELLO", 36, 1, dim, bg,
                                     s_line_b, sizeof s_line_b);
        lcd_text_draw_padded_changed(8, 210, "YELLOW flash again", 36, 1, dim, bg,
                                     s_line_e, sizeof s_line_e);
        return;
    }
    if (fl == BD_FLASH_FAIL) {
        lcd_text_draw_padded_changed(8, 56, "C6 flash failed", 36, 1, rec, bg,
                                     s_line_a, sizeof s_line_a);
        lcd_text_draw_padded_changed(8, 76, s_st.why[0] ? s_st.why : "see DIAG", 36, 1, fg, bg,
                                     s_line_b, sizeof s_line_b);
        lcd_text_draw_padded_changed(8, 96, "hold BOOT, tap RESET,", 36, 1, dim, bg,
                                     s_line_c, sizeof s_line_c);
        lcd_text_draw_padded_changed(8, 112, "YELLOW then GREEN", 36, 1, dim, bg,
                                     s_line_d, sizeof s_line_d);
        lcd_text_draw_padded_changed(8, 210, "YELLOW retry", 36, 1, dim, bg,
                                     s_line_e, sizeof s_line_e);
        return;
    }

    if (!s_st_ok || !s_st.hello) {
        lcd_text_draw_padded_changed(8, 56, "waiting for Bottlenose", 36, 1, dim, bg,
                                     s_line_a, sizeof s_line_a);
        lcd_text_draw_padded_changed(8, 76, "YELLOW flashes wiliOG", 36, 1, yel, bg,
                                     s_line_b, sizeof s_line_b);
        lcd_text_draw_padded_changed(8, 96, "image from this UF2.", 36, 1, dim, bg,
                                     s_line_c, sizeof s_line_c);
        lcd_text_draw_padded_changed(8, 210, "pair FWOG-BleDeck after hello", 36, 1, dim, bg,
                                     s_line_e, sizeof s_line_e);
        return;
    }

    if (s_bat != 0xFF) {
        snprintf(line, sizeof line, "%s  %u%%  %s",
                 s_st.conn ? "connected" : (s_st.adv ? "advertising" : "HID off"),
                 (unsigned)s_bat,
                 s_st.lock ? "locked" : "open pair");
    } else {
        snprintf(line, sizeof line, "%s  %s",
                 s_st.conn ? "connected" : (s_st.adv ? "advertising" : "HID off"),
                 s_st.lock ? "locked" : "open pair");
    }
    lcd_text_draw_padded_changed(8, 48, line, 40, 1, s_st.conn ? acc : yel, bg,
                                 s_line_a, sizeof s_line_a);
    snprintf(line, sizeof line, "page %d / %d  %s", s_page + 1, BD_NPAGE,
             k_page_name[s_page]);
    lcd_text_draw_padded_changed(8, 68, line, 36, 1, dim, bg,
                                 s_line_b, sizeof s_line_b);
    snprintf(line, sizeof line, "GREEN  %s", k_page[s_page][0].label);
    lcd_text_draw_padded_changed(8, 96, line, 36, 1,
                                 slot_col(0, fg, gn, yel, blu, gry, rec), bg,
                                 s_line_c, sizeof s_line_c);
    snprintf(line, sizeof line, "YEL/BLU %s / %s",
             k_page[s_page][1].label, k_page[s_page][2].label);
    lcd_text_draw_padded_changed(8, 112, line, 40, 1,
                                 slot_col((s_down & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) ? 1 : 2,
                                          fg, gn, yel, blu, gry, rec),
                                 bg, s_line_d, sizeof s_line_d);
    snprintf(line, sizeof line, "GRAY/RED %s / %s",
             k_page[s_page][3].label, k_page[s_page][4].label);
    lcd_text_draw_padded_changed(8, 128, line, 36, 1,
                                 slot_col((s_down & FWOG_BTN_BIT(FWOG_BTN_GRAY)) ? 3 : 4,
                                          fg, gn, yel, blu, gry, rec),
                                 bg, s_line_e, sizeof s_line_e);
    lcd_text_draw_padded_changed(8, 210, "YEL C6  BLU page  GRAY unpair",
                                 40, 1, dim, bg, s_line_f, sizeof s_line_f);
}

int main(void) {
    board_init();
    fwog_splash_bind("BleDeck", "004");
#ifndef HOST_TEST
    (void)bq25896_start_charging();
#endif
    s_leds = (ws2812_init(pio0, 0u) == 0);
    lcd_bringup();
    s_link = fwog_link_uart_init(FWOG_LINK_BAUD);
    fwog_link_rx_init(&s_rx);

    uint32_t hold_y = 0, hold_b = 0, hold_g = 0;
    uint8_t fired = 0;
    bool adv_sent = false;

    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        const fwog_power_t p = fwog_power_poll(now);
        uint8_t b;
        size_t n;
        while (fwog_link_uart_read(&b)) {
            if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
            if (fwog_ioexp_link_handle(s_rx.buf, n)) continue;
            if (n >= sizeof(bd_status_t) && s_rx.buf[0] == BD_MSG_ST) {
                memcpy(&s_st, s_rx.buf, sizeof s_st);
                s_st_ok = true;
            }
        }

        if (s_st_ok && s_st.hello && s_want_adv && !adv_sent &&
            s_st.flash == BD_FLASH_IDLE) {
            send_cmd(BD_CMD_ADV, 1u, 0);
            adv_sent = true;
        }

        const uint8_t fl = s_st_ok ? s_st.flash : 0u;
        const bool live = (fl == BD_FLASH_IDLE) && s_st_ok && s_st.hello;
        if (live) bat_tick(now);

        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) {
            hold_y = now;
            fired &= (uint8_t)~FWOG_BTN_BIT(FWOG_BTN_YELLOW);
            if (fl == BD_FLASH_HOLD || fl == BD_FLASH_FAIL || fl == BD_FLASH_OK) {
                send_cmd(BD_CMD_FLASH, 0u, 0);
            } else if (fl != BD_FLASH_SYNC && fl != BD_FLASH_WRITE && !s_st.hello) {
                send_cmd(BD_CMD_FLASH, 1u, 0);
                adv_sent = false;
            } else if (live) {
                s_down |= (uint8_t)FWOG_BTN_BIT(FWOG_BTN_YELLOW);
                fire(1, 1);
            }
        }
        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_BLUE)) {
            hold_b = now;
            fired &= (uint8_t)~FWOG_BTN_BIT(FWOG_BTN_BLUE);
            if (live) {
                s_down |= (uint8_t)FWOG_BTN_BIT(FWOG_BTN_BLUE);
                fire(2, 1);
            }
        }
        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GREEN)) {
            if (fl == BD_FLASH_HOLD) {
                send_cmd(BD_CMD_FLASH, 2u, 0);
            } else if (live) {
                s_down |= (uint8_t)FWOG_BTN_BIT(FWOG_BTN_GREEN);
                fire(0, 1);
            }
        }
        if (live && (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GRAY))) {
            hold_g = now;
            fired &= (uint8_t)~FWOG_BTN_BIT(FWOG_BTN_GRAY);
            s_down |= (uint8_t)FWOG_BTN_BIT(FWOG_BTN_GRAY);
            fire(3, 1);
        }
        if (live && (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_RED))) {
            s_down |= (uint8_t)FWOG_BTN_BIT(FWOG_BTN_RED);
            fire(4, 1);
        }

        if (live && (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) &&
            !(fired & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) &&
            now - hold_y >= 700u) {
            fired |= (uint8_t)FWOG_BTN_BIT(FWOG_BTN_YELLOW);
            if (s_down & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) fire(1, 0);
            s_down &= (uint8_t)~FWOG_BTN_BIT(FWOG_BTN_YELLOW);
            send_cmd(BD_CMD_FLASH, 1u, 0);
            adv_sent = false;
        }
        if (live && (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) &&
            !(fired & FWOG_BTN_BIT(FWOG_BTN_BLUE)) &&
            now - hold_b >= 700u) {
            fired |= (uint8_t)FWOG_BTN_BIT(FWOG_BTN_BLUE);
            if (s_down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) fire(2, 0);
            s_down &= (uint8_t)~FWOG_BTN_BIT(FWOG_BTN_BLUE);
            s_page = (s_page + 1) % BD_NPAGE;
        }
        if (live && (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GRAY)) &&
            !(fired & FWOG_BTN_BIT(FWOG_BTN_GRAY)) &&
            now - hold_g >= 700u) {
            fired |= (uint8_t)FWOG_BTN_BIT(FWOG_BTN_GRAY);
            if (s_down & FWOG_BTN_BIT(FWOG_BTN_GRAY)) fire(3, 0);
            s_down &= (uint8_t)~FWOG_BTN_BIT(FWOG_BTN_GRAY);
            send_cmd(BD_CMD_FORGET, 1u, 0);
        }

        if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_GREEN)) &&
            (s_down & FWOG_BTN_BIT(FWOG_BTN_GREEN))) {
            fire(0, 0);
            s_down &= (uint8_t)~FWOG_BTN_BIT(FWOG_BTN_GREEN);
        }
        if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) &&
            (s_down & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) &&
            !(fired & FWOG_BTN_BIT(FWOG_BTN_YELLOW))) {
            fire(1, 0);
            s_down &= (uint8_t)~FWOG_BTN_BIT(FWOG_BTN_YELLOW);
        }
        if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_BLUE)) &&
            (s_down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) &&
            !(fired & FWOG_BTN_BIT(FWOG_BTN_BLUE))) {
            fire(2, 0);
            s_down &= (uint8_t)~FWOG_BTN_BIT(FWOG_BTN_BLUE);
        }
        if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_GRAY)) &&
            (s_down & FWOG_BTN_BIT(FWOG_BTN_GRAY)) &&
            !(fired & FWOG_BTN_BIT(FWOG_BTN_GRAY))) {
            fire(3, 0);
            s_down &= (uint8_t)~FWOG_BTN_BIT(FWOG_BTN_GRAY);
        }
        if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_RED)) &&
            (s_down & FWOG_BTN_BIT(FWOG_BTN_RED))) {
            fire(4, 0);
            s_down &= (uint8_t)~FWOG_BTN_BIT(FWOG_BTN_RED);
        }

        if (s_leds && !p.armed) {
            uint8_t r = 4u, g = s_st.conn ? 28u : 8u, bl = 8u;
            if (s_down) {
                r = g = bl = 0;
                for (unsigned i = 0; i < (unsigned)FWOG_BTN_COUNT; i++) {
                    if (s_down & FWOG_BTN_BIT(i)) {
                        r = (uint8_t)(r + k_btn_rgb[i][0]);
                        g = (uint8_t)(g + k_btn_rgb[i][1]);
                        bl = (uint8_t)(bl + k_btn_rgb[i][2]);
                    }
                }
            } else {
                bl = (uint8_t)(8u + (unsigned)s_page * 8u);
            }
            for (unsigned i = 0; i < FWOG_LED_COUNT; i++) {
                ws2812_set_color(i, r, g, bl);
            }
            ws2812_process();
        }
        paint();
        sleep_ms(2);
    }
}
