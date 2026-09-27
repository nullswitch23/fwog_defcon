/* TtyGlass v004 — header RX allowlist, freeze/page, T9 find. Splash then tty. */
#include "fwog_display.h"
#include "tg_proto.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

FWOG_POWER_DEFAULT();

#define TG_ROWS     10u
#define TG_COLS     52u   /* 4 + 52*6 = 316, covers the splash gutter */
#define TG_HIST     80u
#define TG_HOLD_MS  750u
#define TG_T9_CAP   13u

static const uint32_t k_baud[] = {
    9600u, 19200u, 38400u, 115200u, 230400u, 921600u
};
#define TG_NBAUD ((int)(sizeof k_baud / sizeof k_baud[0]))

static bool     s_lcd, s_link, s_leds, s_hex;
static bool     s_chrome_dirty = true, s_term_dirty = true;
static bool     s_freeze, s_t9, s_hit;
static int      s_baud_i = 3;
static int      s_pin_i;
static char     s_hist[TG_HIST][TG_COLS + 1u];
static char     s_shown[TG_ROWS][TG_COLS + 1u];
static unsigned s_used = 1u;
static unsigned s_cur;
static unsigned s_c;
static unsigned s_view;          /* chronological start when frozen */
static char     s_t9_buf[TG_T9_CAP];
static int      s_t9_g, s_t9_li;
static char     s_line_stat[48];
static char     s_line_foot[48];
static fwog_link_rx_t s_rx;

static uint16_t k_bg, k_fg, k_acc, k_dim, k_warn, k_hdr;

static void colors(void) {
    k_bg   = st7789_rgb565(8, 10, 18);
    k_fg   = st7789_rgb565(220, 230, 200);
    k_acc  = st7789_rgb565(80, 220, 140);
    k_dim  = st7789_rgb565(140, 150, 170);
    k_warn = st7789_rgb565(230, 180, 70);
    k_hdr  = st7789_rgb565(16, 28, 40);
}

static unsigned chrono_idx(unsigned i) {
    /* i = 0 oldest .. s_used-1 newest (current line). */
    return (s_cur + TG_HIST - (s_used - 1u) + i) % TG_HIST;
}

static unsigned live_top(void) {
    if (s_used <= TG_ROWS) return 0;
    return s_used - TG_ROWS;
}

static unsigned view_top(void) {
    unsigned top = s_freeze ? s_view : live_top();
    const unsigned max_top = live_top();
    if (top > max_top) top = max_top;
    return top;
}

static void leds_mode(void) {
    uint8_t r = 4, g = 18, b = 8;
    if (!s_leds) return;
    if (s_t9) {
        r = 8; g = 12; b = 28;
    } else if (s_freeze) {
        r = 8; g = 8; b = 32;
    }
    for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++) {
        ws2812_set_color(i, r, g, b);
    }
    ws2812_process();
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
        board_backlight(255);
        fwog_splash_boot();
        colors();
        st7789_clear(k_bg);
        s_chrome_dirty = true;
        s_term_dirty = true;
        memset(s_shown, 0xFF, sizeof s_shown);
        s_line_stat[0] = s_line_foot[0] = '\0';
    }
}

static void send_cmd(void) {
    tg_cmd_t m = {
        .type = TG_MSG_CMD,
        .baud_i = (uint8_t)s_baud_i,
        .hex = s_hex ? 1u : 0u,
        .pin_i = (uint8_t)s_pin_i
    };
    if (s_link) (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void newline(void) {
    if (s_used < TG_HIST) s_used++;
    s_cur = (s_cur + 1u) % TG_HIST;
    memset(s_hist[s_cur], 0, sizeof s_hist[0]);
    s_c = 0;
    if (!s_freeze) s_view = live_top();
    s_term_dirty = true;
}

static void putc_term(char ch) {
    if (ch == '\r') return;
    if (ch == '\n') {
        newline();
        return;
    }
    if (s_c >= TG_COLS) newline();
    if (ch < 32 || ch > 126) ch = '.';
    s_hist[s_cur][s_c++] = ch;
    s_term_dirty = true;
}

static void put_hex(uint8_t b) {
    static const char *hex = "0123456789ABCDEF";
    putc_term(hex[b >> 4]);
    putc_term(hex[b & 0xFu]);
    putc_term(' ');
}

static void hist_clear(void) {
    memset(s_hist, 0, sizeof s_hist);
    s_used = 1u;
    s_cur = 0;
    s_c = 0;
    s_view = 0;
    s_hit = false;
    s_term_dirty = true;
}

static char up(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
    return c;
}

static bool line_has(const char *hay, const char *needle) {
    unsigned i, j;
    if (!needle || !needle[0] || !hay) return false;
    for (i = 0; hay[i]; i++) {
        for (j = 0; needle[j]; j++) {
            if (up(hay[i + j]) != up(needle[j])) break;
        }
        if (!needle[j]) return true;
    }
    return false;
}

static bool search_from(unsigned start) {
    unsigned n;
    if (!s_t9_buf[0] || s_used == 0u) return false;
    for (n = 0; n < s_used; n++) {
        const unsigned i = (start + 1u + n) % s_used;
        if (line_has(s_hist[chrono_idx(i)], s_t9_buf)) {
            s_freeze = true;
            s_view = i;
            if (s_view > live_top()) s_view = live_top();
            s_hit = true;
            s_term_dirty = true;
            s_chrome_dirty = true;
            leds_mode();
            return true;
        }
    }
    s_hit = false;
    s_chrome_dirty = true;
    return false;
}

static void page(int dir) {
    const unsigned max_top = live_top();
    int v = (int)s_view + dir;
    if (v < 0) v = 0;
    if (v > (int)max_top) v = (int)max_top;
    s_view = (unsigned)v;
    s_term_dirty = true;
    s_chrome_dirty = true;
}

static void paint_t9(void) {
    char ch[2] = { 0, 0 };
    lcd_text_draw_padded(8, 8, "TtyGlass SEARCH", 18, 2, k_acc, k_hdr);
    lcd_text_draw_padded(8, 44, "find in scrollback  A-Z", 40, 1, k_dim, k_bg);
    lcd_text_draw_padded(8, 64, s_t9_buf[0] ? s_t9_buf : "(empty = cancel)",
                         40, 1, k_fg, k_bg);
    lcd_text_draw_padded(8, 104, fwog_t9_group[s_t9_g], 5, 4, k_acc, k_bg);
    ch[0] = fwog_t9_cur(s_t9_g, s_t9_li);
    lcd_text_draw_padded(160, 104, ch, 2, 4, k_fg, k_bg);
    lcd_text_draw_padded(8, 172, "GRN tap insert  hold find", 40, 1, k_dim, k_bg);
    lcd_text_draw_padded(8, 188, "YEL hold bksp   GRY hold cancel", 40, 1, k_dim, k_bg);
    lcd_text_draw_padded(8, 212, s_hit ? "hit — frozen on match" : " ",
                         40, 1, k_warn, k_bg);
}

static void paint(void) {
    char line[48];
    unsigned i, top;
    if (!s_lcd || (!s_chrome_dirty && !s_term_dirty)) return;
    if (s_t9) {
        if (s_chrome_dirty) {
            st7789_fill_rect(0, 0, ST7789_W, 36, k_hdr);
            st7789_fill_rect(0, 36, ST7789_W, ST7789_H - 36, k_bg);
            s_chrome_dirty = false;
        }
        paint_t9();
        s_term_dirty = false;
        return;
    }
    if (s_chrome_dirty) {
        st7789_fill_rect(0, 0, ST7789_W, 36, k_hdr);
        st7789_fill_rect(0, 36, ST7789_W, ST7789_H - 36, k_bg);
        lcd_text_draw_padded(8, 10, "TtyGlass", 16, 2, k_acc, k_hdr);
        memset(s_shown, 0xFF, sizeof s_shown);
        s_line_stat[0] = s_line_foot[0] = '\0';
        s_chrome_dirty = false;
        s_term_dirty = true;
    }
    snprintf(line, sizeof line, "%u %s %s H%u%s",
             (unsigned)k_baud[s_baud_i],
             s_hex ? "HEX" : "ASC",
             k_tg_pin[s_pin_i].tag,
             (unsigned)k_tg_pin[s_pin_i].hdr,
             s_freeze ? " FRZ" : "");
    lcd_text_draw_padded_changed(140, 12, line, 30, 1, k_dim, k_hdr,
                                 s_line_stat, sizeof s_line_stat);
    top = view_top();
    for (i = 0; i < TG_ROWS; i++) {
        const char *row = "";
        if (top + i < s_used) row = s_hist[chrono_idx(top + i)];
        if (memcmp(row, s_shown[i], TG_COLS + 1u) == 0) continue;
        memset(s_shown[i], 0, sizeof s_shown[i]);
        strncpy(s_shown[i], row, TG_COLS);
        lcd_text_draw_padded(4, (uint16_t)(40u + i * 16u),
                             s_shown[i], TG_COLS, 1, k_fg, k_bg);
    }
    if (s_freeze) {
        snprintf(line, sizeof line, "FRZ %u/%u  GRY/RED page  BLU live",
                 view_top() + 1u, s_used);
    } else {
        snprintf(line, sizeof line, "GRY/RED baud  BLU freeze  GRY hold find");
    }
    lcd_text_draw_padded_changed(8, 204, line, 50, 1, k_dim, k_bg,
                                 s_line_foot, sizeof s_line_foot);
    lcd_text_draw_padded(8, 220, "YEL hex  hold RX pad  GRN clear",
                         50, 1, k_warn, k_bg);
    s_term_dirty = false;
}

static void t9_open(void) {
    s_t9 = true;
    s_t9_buf[0] = '\0';
    s_t9_g = 0;
    s_t9_li = 0;
    s_hit = false;
    s_chrome_dirty = true;
    leds_mode();
}

static void t9_close(void) {
    s_t9 = false;
    s_chrome_dirty = true;
    s_term_dirty = true;
    memset(s_shown, 0xFF, sizeof s_shown);
    leds_mode();
}

int main(void) {
    bool ywas = false, gwas = false, bwas = false, rwas = false;
    bool yhold = false, ghold = false, rhold = false;
    uint32_t yms = 0, gms = 0, rms = 0;

    board_init();
    fwog_splash_bind("TtyGlass", "004");
    colors();
    s_leds = ws2812_init(pio0, 0u);
    lcd_bringup();
    leds_mode();
    s_link = fwog_link_uart_init(FWOG_LINK_BAUD);
    fwog_link_rx_init(&s_rx);
    send_cmd();

    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        const fwog_power_t p = fwog_power_poll(now);
        const bool ydown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) != 0;
        const bool gdown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GREEN)) != 0;
        const bool bdown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) != 0;
        const bool rydown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GRAY)) != 0;
        uint8_t b;
        size_t n;

        while (fwog_link_uart_read(&b)) {
            if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
            if (fwog_ioexp_link_handle(s_rx.buf, n)) continue;
            if (n >= sizeof(tg_data_t) && s_rx.buf[0] == TG_MSG_DATA) {
                tg_data_t m;
                memcpy(&m, s_rx.buf, sizeof m);
                for (uint8_t i = 0; i < m.n && i < TG_CHUNK; i++) {
                    if (s_hex) put_hex(m.data[i]);
                    else putc_term((char)m.data[i]);
                }
            }
        }

        if (p.armed) {
            paint();
            sleep_ms(2);
            continue;
        }

        if (s_t9) {
            if (gdown && !gwas) {
                gms = now;
                ghold = false;
            }
            if (gdown && !ghold && (now - gms) >= TG_HOLD_MS) {
                ghold = true;
                if (s_t9_buf[0]) (void)search_from(s_view);
                else t9_close();
            }
            if (!gdown && gwas && !ghold) {
                fwog_t9_insert(s_t9_buf, sizeof s_t9_buf,
                               fwog_t9_cur(s_t9_g, s_t9_li));
                s_chrome_dirty = true;
            }
            gwas = gdown;

            if (ydown && !ywas) {
                yms = now;
                yhold = false;
            }
            if (ydown && !yhold && (now - yms) >= TG_HOLD_MS) {
                yhold = true;
                fwog_t9_backspace(s_t9_buf);
                s_chrome_dirty = true;
            }
            if (!ydown && ywas && !yhold) {
                fwog_t9_letter_prev(s_t9_g, &s_t9_li);
                s_chrome_dirty = true;
            }
            ywas = ydown;

            if (!bdown && bwas) {
                fwog_t9_letter_next(s_t9_g, &s_t9_li);
                s_chrome_dirty = true;
            }
            bwas = bdown;

            if (rydown && !rwas) {
                rms = now;
                rhold = false;
            }
            if (rydown && !rhold && (now - rms) >= TG_HOLD_MS) {
                rhold = true;
                t9_close();
            }
            if (!rydown && rwas && !rhold) {
                fwog_t9_group_prev(&s_t9_g, &s_t9_li);
                s_chrome_dirty = true;
            }
            rwas = rydown;

            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_RED)) {
                fwog_t9_group_next(&s_t9_g, &s_t9_li);
                s_chrome_dirty = true;
            }
            paint();
            sleep_ms(2);
            continue;
        }

        if (rydown && !rwas) {
            rms = now;
            rhold = false;
        }
        if (rydown && !rhold && (now - rms) >= TG_HOLD_MS) {
            rhold = true;
            t9_open();
            rwas = rydown;
            paint();
            sleep_ms(2);
            continue;
        }
        if (!rydown && rwas && !rhold) {
            if (s_freeze) page(-1);
            else {
                if (--s_baud_i < 0) s_baud_i = TG_NBAUD - 1;
                send_cmd();
                s_chrome_dirty = true;
            }
        }
        rwas = rydown;

        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_RED)) {
            if (s_freeze) page(1);
            else {
                if (++s_baud_i >= TG_NBAUD) s_baud_i = 0;
                send_cmd();
                s_chrome_dirty = true;
            }
        }
        if (ydown && !ywas) {
            yms = now;
            yhold = false;
        }
        if (ydown && !yhold && (now - yms) >= TG_HOLD_MS) {
            yhold = true;
            if (++s_pin_i >= (int)TG_NPIN) s_pin_i = 0;
            send_cmd();
            s_chrome_dirty = true;
        }
        if (!ydown && ywas && !yhold) {
            s_hex = !s_hex;
            s_chrome_dirty = true;
        }
        ywas = ydown;

        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_BLUE)) {
            s_freeze = !s_freeze;
            if (s_freeze) s_view = live_top();
            s_chrome_dirty = true;
            s_term_dirty = true;
            leds_mode();
        }
        if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GREEN)) {
            hist_clear();
            s_chrome_dirty = true;
        }

        paint();
        sleep_ms(2);
    }
}
