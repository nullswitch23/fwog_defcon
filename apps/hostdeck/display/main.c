/* HostDeck v003 — LCD slot labels via T9; chords/payloads stay host JSON. */
#include "fwog_display.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FWOG_POWER_DEFAULT();

#define HD_NPAGE       2
#define HD_NSLOT       5
#define HD_LABEL_N     12
#define HD_PAGE_MS     700u
#define HD_T9_HOLD_MS  750u /* PingHalo T9 hold */

typedef struct {
    const char *label;
    const char *chord;
    const char *payload; /* host-side --payloads index as decimal string, or NULL */
} hd_slot_t;

static const hd_slot_t k_slot[HD_NPAGE][HD_NSLOT] = {
    {
        { "Play",  "Consumer Play/Pause", NULL },
        { "Mute",  "Consumer Mute",       NULL },
        { "Copy",  "Ctrl+C",              NULL },
        { "Paste", "Ctrl+V",              "0"  },
        { "Enter", "Return",              NULL },
    },
    {
        { "Cut",   "Ctrl+X",              NULL },
        { "Undo",  "Ctrl+Z",              NULL },
        { "Save",  "Ctrl+S",              NULL },
        { "Tab",   "Tab",                 NULL },
        { "Esc",   "Escape",              NULL },
    },
};

static const char *const k_btn[HD_NSLOT] = {
    "GRN", "YEL", "BLU", "GRY", "RED"
};

static bool s_lcd, s_leds;
static int  s_page;
static int  s_focus;
static bool s_chrome_dirty = true;
static bool s_t9;
static bool s_t9_chrome;
static uint8_t s_suppress; /* ignore release of buttons still down after T9 */
static char s_label[HD_NPAGE][HD_NSLOT][HD_LABEL_N];
static char s_t9_buf[HD_LABEL_N];
static int  s_t9_g, s_t9_li;
static int  s_t9_slot;
static char s_line_t9[24];
static char s_line_grp[8];
static char s_line_ch[4];

static const char *slot_label(unsigned page, unsigned slot) {
    if (page >= HD_NPAGE || slot >= HD_NSLOT) {
        return "";
    }
    if (s_label[page][slot][0]) {
        return s_label[page][slot];
    }
    return k_slot[page][slot].label;
}

static void labels_init(void) {
    unsigned p, s;
    for (p = 0; p < HD_NPAGE; p++) {
        for (s = 0; s < HD_NSLOT; s++) {
            memset(s_label[p][s], 0, HD_LABEL_N);
            strncpy(s_label[p][s], k_slot[p][s].label, HD_LABEL_N - 1u);
        }
    }
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

static void fire(unsigned slot) {
    const hd_slot_t *s;
    if (slot >= HD_NSLOT) {
        return;
    }
    s_focus = (int)slot;
    s_chrome_dirty = true;
    s = &k_slot[s_page][slot];
    if (s->payload != NULL) {
        const unsigned n = (unsigned)strtoul(s->payload, NULL, 10);
        DIAG("HOSTDECK payload=%u label=%s (payload list is host-side --payloads)\n",
             n, slot_label((unsigned)s_page, slot));
        return;
    }
    DIAG("MACRO page=%u slot=%u %s -> %s\n",
         (unsigned)s_page, slot, slot_label((unsigned)s_page, slot), s->chord);
}

static void t9_begin(unsigned slot) {
    s_t9_slot = (int)slot;
    s_focus = (int)slot;
    memset(s_t9_buf, 0, sizeof s_t9_buf);
    strncpy(s_t9_buf, slot_label((unsigned)s_page, slot), HD_LABEL_N - 1u);
    s_t9_g = 0;
    s_t9_li = 0;
    s_t9 = true;
    s_t9_chrome = true;
    s_line_t9[0] = s_line_grp[0] = s_line_ch[0] = '\0';
}

static void t9_finish(void) {
    memset(s_label[s_page][s_t9_slot], 0, HD_LABEL_N);
    strncpy(s_label[s_page][s_t9_slot], s_t9_buf, HD_LABEL_N - 1u);
    s_t9 = false;
    s_chrome_dirty = true;
}

static void paint_t9(uint16_t bg, uint16_t acc, uint16_t fg, uint16_t dim) {
    char line[24];
    char ch[2];
    if (s_t9_chrome) {
        st7789_clear(bg);
        lcd_text_draw_padded(8, 8, "HostDeck", 18, 2, acc, bg);
        snprintf(line, sizeof line, "rename %s p%d",
                 k_btn[s_t9_slot], s_page + 1);
        lcd_text_draw_padded(8, 40, line, 40, 1, dim, bg);
        lcd_text_draw_padded(8, 176, "GRY/RED grp  YEL/BLU char", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 192, "GRN tap add  hold done", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 208, "YEL hold backspace", 40, 1, dim, bg);
        s_t9_chrome = false;
    }
    lcd_text_draw_padded(8, 56, "name", 16, 1, dim, bg);
    snprintf(line, sizeof line, "[%s]", s_t9_buf);
    lcd_text_draw_padded_changed(8, 68, line, 12, 2, fg, bg,
                                 s_line_t9, sizeof s_line_t9);
    lcd_text_draw_padded(8, 100, "grp", 8, 1, dim, bg);
    lcd_text_draw_padded_changed(8, 112, fwog_t9_group[s_t9_g], 5, 4, acc, bg,
                                 s_line_grp, sizeof s_line_grp);
    lcd_text_draw_padded(180, 100, "char", 8, 1, dim, bg);
    ch[0] = fwog_t9_cur(s_t9_g, s_t9_li);
    ch[1] = '\0';
    lcd_text_draw_padded_changed(180, 112, ch, 2, 4, fg, bg,
                                 s_line_ch, sizeof s_line_ch);
}

static void paint(void) {
    const uint16_t bg  = st7789_rgb565(8, 10, 18);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    const uint16_t fg  = st7789_rgb565(230, 230, 230);
    const uint16_t yel = st7789_rgb565(220, 200, 80);
    char line[40];
    unsigned i;
    if (!s_lcd) {
        return;
    }
    if (s_t9) {
        paint_t9(bg, acc, fg, dim);
        return;
    }
    if (!s_chrome_dirty) {
        return;
    }
    st7789_clear(bg);
    lcd_text_draw_padded(8, 8, "HostDeck", 18, 2, acc, bg);
    snprintf(line, sizeof line, "page %d / 2   labels v003", s_page + 1);
    lcd_text_draw_padded(8, 40, line, 40, 1, dim, bg);
    for (i = 0; i < HD_NSLOT; i++) {
        snprintf(line, sizeof line, "%c %-4s %s",
                 (int)i == s_focus ? '>' : ' ',
                 k_btn[i], slot_label((unsigned)s_page, i));
        lcd_text_draw_padded(8, (uint16_t)(56u + i * 16u), line, 40, 1,
                             (int)i == s_focus ? acc : fg, bg);
    }
    lcd_text_draw_padded(8, 144, "tap fires  GRY hold rename", 40, 1, yel, bg);
    lcd_text_draw_padded(8, 160, "YEL/BLU hold page", 40, 1, dim, bg);
    lcd_text_draw_padded(8, 176, "RED hold 6s ship", 40, 1, dim, bg);
    lcd_text_draw_padded(8, 200, "chords stay on this PC JSON", 40, 1, dim, bg);
    s_chrome_dirty = false;
}

int main(void) {
    uint32_t hold_y = 0, hold_b = 0, hold_g = 0, hold_grn = 0;
    uint8_t fired = 0;
    bool yel_was = false, grn_was = false, blu_was = false, gry_was = false;
    bool yel_hold = false, grn_hold = false, blu_hold = false, gry_hold = false;

    board_init();
    fwog_splash_bind("HostDeck", "003");
    labels_init();
    s_leds = ws2812_init(pio0, 0u);
    lcd_bringup();
    DIAG("[hostdeck] MACRO chords and HOSTDECK payload= on this CDC; "
         "LCD labels are T9; load payloads with tools/hostdeck/ --payloads\n");

    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        const fwog_power_t p = fwog_power_poll(now);
        const bool ydown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) != 0;
        const bool gdown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GREEN)) != 0;
        const bool bdown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) != 0;
        const bool rydown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GRAY)) != 0;

        if (p.armed) {
            paint();
            sleep_ms(2);
            continue;
        }

        if (s_t9) {
            if (ydown && !yel_was) {
                hold_y = now;
                yel_hold = false;
            }
            if (ydown && !yel_hold && (now - hold_y) >= HD_T9_HOLD_MS) {
                yel_hold = true;
                fwog_t9_backspace(s_t9_buf);
            }
            if (!ydown && yel_was && !yel_hold) {
                fwog_t9_letter_prev(s_t9_g, &s_t9_li);
            }
            yel_was = ydown;

            if (gdown && !grn_was) {
                hold_grn = now;
                grn_hold = false;
            }
            if (gdown && !grn_hold && (now - hold_grn) >= HD_T9_HOLD_MS) {
                grn_hold = true;
                t9_finish();
                s_suppress = p.buttons.down;
                grn_was = gdown;
            } else {
                if (!gdown && grn_was && !grn_hold) {
                    fwog_t9_insert(s_t9_buf, sizeof s_t9_buf,
                                   fwog_t9_cur(s_t9_g, s_t9_li));
                }
                grn_was = gdown;

                if (bdown && !blu_was) {
                    hold_b = now;
                    blu_hold = false;
                }
                if (!bdown && blu_was && !blu_hold) {
                    fwog_t9_letter_next(s_t9_g, &s_t9_li);
                }
                blu_was = bdown;

                if (rydown && !gry_was) {
                    gry_hold = false;
                }
                if (!rydown && gry_was && !gry_hold) {
                    fwog_t9_group_prev(&s_t9_g, &s_t9_li);
                }
                gry_was = rydown;

                if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_RED)) {
                    fwog_t9_group_next(&s_t9_g, &s_t9_li);
                }
            }
        } else {
            if (!(p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_YELLOW))) {
                s_suppress &= (uint8_t)~FWOG_BTN_BIT(FWOG_BTN_YELLOW);
            }
            if (!(p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GREEN))) {
                s_suppress &= (uint8_t)~FWOG_BTN_BIT(FWOG_BTN_GREEN);
            }
            if (!(p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE))) {
                s_suppress &= (uint8_t)~FWOG_BTN_BIT(FWOG_BTN_BLUE);
            }
            if (!(p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GRAY))) {
                s_suppress &= (uint8_t)~FWOG_BTN_BIT(FWOG_BTN_GRAY);
            }
            if (!(p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_RED))) {
                s_suppress &= (uint8_t)~FWOG_BTN_BIT(FWOG_BTN_RED);
            }

            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) {
                hold_y = now;
                fired &= (uint8_t)~FWOG_BTN_BIT(FWOG_BTN_YELLOW);
            }
            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_BLUE)) {
                hold_b = now;
                fired &= (uint8_t)~FWOG_BTN_BIT(FWOG_BTN_BLUE);
            }
            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GRAY)) {
                hold_g = now;
                fired &= (uint8_t)~FWOG_BTN_BIT(FWOG_BTN_GRAY);
            }
            if ((p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) &&
                !(fired & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) &&
                now - hold_y >= HD_PAGE_MS) {
                fired |= (uint8_t)FWOG_BTN_BIT(FWOG_BTN_YELLOW);
                s_page = 0;
                s_chrome_dirty = true;
            }
            if ((p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) &&
                !(fired & FWOG_BTN_BIT(FWOG_BTN_BLUE)) &&
                now - hold_b >= HD_PAGE_MS) {
                fired |= (uint8_t)FWOG_BTN_BIT(FWOG_BTN_BLUE);
                s_page = 1;
                s_chrome_dirty = true;
            }
            if ((p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GRAY)) &&
                !(fired & FWOG_BTN_BIT(FWOG_BTN_GRAY)) &&
                now - hold_g >= HD_T9_HOLD_MS) {
                fired |= (uint8_t)FWOG_BTN_BIT(FWOG_BTN_GRAY);
                t9_begin((unsigned)s_focus);
                yel_was = ydown;
                yel_hold = false;
                grn_was = gdown;
                grn_hold = false;
                blu_was = bdown;
                blu_hold = false;
                gry_was = true;
                gry_hold = true;
            }

            if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_GREEN)) &&
                !(s_suppress & FWOG_BTN_BIT(FWOG_BTN_GREEN))) fire(0);
            if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) &&
                !(fired & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) &&
                !(s_suppress & FWOG_BTN_BIT(FWOG_BTN_YELLOW))) fire(1);
            if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_BLUE)) &&
                !(fired & FWOG_BTN_BIT(FWOG_BTN_BLUE)) &&
                !(s_suppress & FWOG_BTN_BIT(FWOG_BTN_BLUE))) fire(2);
            if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_GRAY)) &&
                !(fired & FWOG_BTN_BIT(FWOG_BTN_GRAY)) &&
                !(s_suppress & FWOG_BTN_BIT(FWOG_BTN_GRAY))) fire(3);
            if ((p.buttons.released & FWOG_BTN_BIT(FWOG_BTN_RED)) &&
                !(s_suppress & FWOG_BTN_BIT(FWOG_BTN_RED))) fire(4);
        }

        if (s_leds && !p.armed) {
            for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++) {
                ws2812_set_color(i, 4u, (unsigned)s_page ? 18u : 8u, 14u);
            }
            ws2812_process();
        }
        paint();
        sleep_ms(2);
    }
}
