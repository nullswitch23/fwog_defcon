#include "pd_tg.h"
#include "pd_link.h"
#include "tg_proto.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

#define TG_ROWS 10u
#define TG_COLS 52u
#define TG_HIST 80u

static const uint32_t k_baud[] = {
    9600u, 19200u, 38400u, 115200u, 230400u, 921600u
};
#define TG_NBAUD ((int)(sizeof k_baud / sizeof k_baud[0]))

static bool     s_hex;
static bool     s_chrome_dirty = true, s_term_dirty = true;
static bool     s_freeze;
static int      s_baud_i = 3;
static int      s_pin_i;
static char     s_hist[TG_HIST][TG_COLS + 1u];
static char     s_shown[TG_ROWS][TG_COLS + 1u];
static unsigned s_used = 1u;
static unsigned s_cur;
static unsigned s_c;
static unsigned s_view;
static char     s_line_stat[48];
static char     s_line_foot[48];
static uint32_t s_blu_ms;
static bool     s_blu_was, s_blu_hold;

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

static void send_cmd(void) {
    tg_cmd_t m = {
        .type = TG_MSG_CMD,
        .baud_i = (uint8_t)s_baud_i,
        .hex = s_hex ? 1u : 0u,
        .pin_i = (uint8_t)s_pin_i
    };
    if (pd_link_ok()) pd_link_send(&m, sizeof m);
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
    s_term_dirty = true;
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

void pd_tg_enter(void) {
    colors();
    s_hex = false;
    s_freeze = false;
    s_baud_i = 3;
    s_pin_i = 0;
    hist_clear();
    s_chrome_dirty = true;
    s_term_dirty = true;
    memset(s_shown, 0xFF, sizeof s_shown);
    send_cmd();
}

void pd_tg_frame(const uint8_t *buf, size_t n) {
    if (n >= sizeof(tg_data_t) && buf[0] == TG_MSG_DATA) {
        tg_data_t m;
        uint8_t i;
        memcpy(&m, buf, sizeof m);
        for (i = 0; i < m.n && i < TG_CHUNK; i++) {
            if (s_hex) put_hex(m.data[i]);
            else putc_term((char)m.data[i]);
        }
    }
}

void pd_tg_buttons(const fwog_power_t *p, bool y_long, bool g_long,
                   bool gray_long, bool red_tap) {
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    const bool bdown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) != 0;

    if (bdown && !s_blu_was) {
        s_blu_ms = now;
        s_blu_hold = false;
    }
    if (bdown && !s_blu_hold && (now - s_blu_ms) >= 700u) {
        s_blu_hold = true;
        if (++s_pin_i >= (int)TG_NPIN) s_pin_i = 0;
        send_cmd();
        s_chrome_dirty = true;
    }
    if (!bdown && s_blu_was && !s_blu_hold) {
        s_freeze = !s_freeze;
        if (s_freeze) s_view = live_top();
        s_chrome_dirty = true;
        s_term_dirty = true;
    }
    s_blu_was = bdown;

    if ((p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_GRAY)) && !gray_long) {
        if (s_freeze) page(-1);
        else {
            if (--s_baud_i < 0) s_baud_i = TG_NBAUD - 1;
            send_cmd();
            s_chrome_dirty = true;
        }
    }
    if (red_tap && (p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_RED))) {
        if (s_freeze) page(1);
        else {
            if (++s_baud_i >= TG_NBAUD) s_baud_i = 0;
            send_cmd();
            s_chrome_dirty = true;
        }
    }
    if ((p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) && !y_long) {
        s_hex = !s_hex;
        s_chrome_dirty = true;
    }
    if ((p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_GREEN)) && !g_long) {
        hist_clear();
        s_chrome_dirty = true;
    }
}

void pd_tg_paint(void) {
    char line[48];
    unsigned i, top;
    if (!s_chrome_dirty && !s_term_dirty) return;
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
        snprintf(line, sizeof line, "GRY/RED baud  BLU freeze  hold RX");
    }
    lcd_text_draw_padded_changed(8, 204, line, 50, 1, k_dim, k_bg,
                                 s_line_foot, sizeof s_line_foot);
    lcd_text_draw_padded(8, 220, "YEL hex  GRN clear  hold YEL/GRN/GRY home",
                         50, 1, k_warn, k_bg);
    s_term_dirty = false;
}
