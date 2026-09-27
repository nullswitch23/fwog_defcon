/* DiskGlass v009 — FatFs browser; T9 IR compose; WASM; delete; IR/QR. */
#include "fwog_display.h"
#include "dg_proto.h"
#include "dg_xfer.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#ifndef HOST_TEST
#include "qrcodegen.h"
#endif

FWOG_POWER_DEFAULT();

#define DG_ROWS      8
#define DG_IR_GAP_MS 90u
#define DG_IR_WAIT   20000u
#define DG_T9_CAP    41u   /* one LCD row; IR path still DG_IR_MAX */

typedef enum {
    DG_SCR_LIST = 0,
    DG_SCR_VIEW,
    DG_SCR_QR,
    DG_SCR_DEL,
    DG_SCR_T9
} dg_scr_t;

typedef struct {
    char     name[DG_NAME_LEN];
    uint32_t size;
    uint16_t kind;
    uint8_t  is_dir;
} dg_ent_t;

static bool           s_lcd, s_link, s_leds, s_i2s, s_ir;
static bool           s_chrome_dirty = true;
static bool           s_power_armed;
static fwog_link_rx_t s_rx;
static dg_scr_t       s_scr = DG_SCR_LIST;
static char           s_path[DG_PATH_LEN] = "/";
static dg_ent_t       s_ent[DG_MAX_ENT];
static unsigned       s_nent;
static unsigned       s_count;
static int            s_cursor;
static int            s_scroll;
static bool           s_mounted;
static bool           s_vol_err;
static uint32_t       s_free_bytes;
static bool           s_list_end;
static uint32_t       s_list_ask_ms;
static uint32_t       s_rep_ms;
static uint32_t       s_blue_ms;
static bool           s_blue_hold;
static bool           s_blu_was;
static uint32_t       s_yel_ms;
static bool           s_yel_hold;
static bool           s_yel_was;
static uint32_t       s_grn_ms;
static bool           s_grn_hold;
static bool           s_grn_arm;
static bool           s_grn_was;
static bool           s_del_ready;
static bool           s_wasm_leave;
static bool           s_rows_dirty = true;
static bool           s_wasm_leds;
static bool           s_wasm_gfx;
static char           s_del_path[DG_PATH_LEN];
static char           s_del_name[DG_NAME_LEN];
static dg_meta_t      s_meta;
static bool           s_meta_ok;
static char           s_csv[DG_CSV_HINT + 4];
static unsigned       s_csv_n;
static int            s_text_line;
static char           s_status[44];
static bool           s_playing;
static bool           s_pcm_wait;
static uint32_t       s_play_off;
static char           s_open_path[DG_PATH_LEN];
#ifndef HOST_TEST
static int16_t        s_pcm[DG_DATA_MAX / 2u];
static unsigned       s_pcm_n;
static uint8_t        s_qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(6)];
static bool           s_qr_ok;
#endif
static bool           s_listen;
static uint32_t       s_listen_ms;
static dg_ir_rx_t     s_irx;
static uint8_t        s_tx_buf[DG_IR_MAX];
static uint16_t       s_tx_n;
static uint16_t       s_tx_off;
static uint8_t        s_tx_stage;   /* 0 idle, 1 start, 2 data, 3 end */
static uint32_t       s_tx_ms;
static char           s_line_title[44];
static char           s_line_vol[44];
static char           s_line_row[DG_ROWS][44];
static char           s_line_view[10][44];
static char           s_t9_buf[DG_T9_CAP];
static int            s_t9_g, s_t9_li;
static char           s_line_t9[44];
static char           s_line_grp[8];
static char           s_line_ch[4];

static bool is_text_kind(uint16_t k) {
    return k == DG_KIND_CSV || k == DG_KIND_TEXT;
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

static void send_cmd(uint8_t cmd, const char *path, uint32_t offset, uint16_t len) {
    dg_cmd_t m;
    memset(&m, 0, sizeof m);
    m.type = DG_MSG_CMD;
    m.cmd = cmd;
    m.offset = offset;
    m.len = len;
    m.index = 0;
    if (path) {
        strncpy(m.path, path, DG_PATH_LEN - 1u);
        m.path[DG_PATH_LEN - 1u] = '\0';
    }
    if (s_link) (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void send_run(bool loop) {
    dg_cmd_t m;
    memset(&m, 0, sizeof m);
    m.type = DG_MSG_CMD;
    m.cmd = DG_CMD_RUN;
    m.index = loop ? DG_RUN_LOOP : DG_RUN_ONCE;
    strncpy(m.path, s_open_path, DG_PATH_LEN - 1u);
    m.path[DG_PATH_LEN - 1u] = '\0';
    if (s_link) {
        (void)fwog_link_uart_send_frame(&m, sizeof m);
        s_wasm_leds = true; /* busy before first LED/FB host arrives */
    }
}

static void send_stop(void) {
    dg_cmd_t m;
    memset(&m, 0, sizeof m);
    m.type = DG_MSG_CMD;
    m.cmd = DG_CMD_STOP;
    if (s_link) (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static bool wasm_busy(void) {
    return s_wasm_leds || s_wasm_gfx || s_wasm_leave;
}

static void send_put(const char *name, const uint8_t *bytes, uint16_t n) {
    dg_put_t m;
    memset(&m, 0, sizeof m);
    m.type = DG_MSG_PUT;
    if (n > DG_IR_MAX) n = DG_IR_MAX;
    m.n = n;
    if (name) strncpy(m.name, name, DG_NAME_LEN - 1u);
    if (bytes && n) memcpy(m.bytes, bytes, n);
    if (s_link) (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void ask_list(void) {
    s_nent = 0;
    s_count = 0;
    s_list_end = false;
    s_mounted = false;
    s_cursor = 0;
    s_scroll = 0;
    s_rows_dirty = true;
    s_status[0] = '\0';
    s_list_ask_ms = to_ms_since_boot(get_absolute_time());
    send_cmd(DG_CMD_LIST, s_path, 0, 0);
}

static void leds_idle(void) {
    if (!s_leds || s_power_armed || s_wasm_leds) return;
    const uint8_t g = s_playing ? 40u : (s_listen || s_tx_stage ? 28u : 8u);
    const uint8_t b = s_mounted ? 18u : 2u;
    const uint8_t r = s_listen ? 18u : 2u;
    for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++) {
        ws2812_set_color(i, r, g, b);
    }
    ws2812_process();
}

static const char *parent_path(char *out, size_t cap) {
    size_t n;
    if (!out || cap < 2u) return "/";
    strncpy(out, s_path, cap - 1u);
    out[cap - 1u] = '\0';
    n = strlen(out);
    while (n > 1u && out[n - 1u] == '/') out[--n] = '\0';
    while (n > 1u && out[n - 1u] != '/') out[--n] = '\0';
    if (n > 1u && out[n - 1u] == '/') out[n - 1u] = '\0';
    if (n <= 1u) {
        out[0] = '/';
        out[1] = '\0';
    }
    return out;
}

static void cursor_move(int delta) {
    int prev;
    if (s_nent == 0u) return;
    prev = s_cursor;
    s_cursor += delta;
    while (s_cursor < 0) s_cursor += (int)s_nent;
    while (s_cursor >= (int)s_nent) s_cursor -= (int)s_nent;
    if (s_cursor != prev) s_rows_dirty = true;
}

static bool btn_repeat(const fwog_power_t *p, unsigned btn, uint32_t now) {
    const uint8_t bit = FWOG_BTN_BIT(btn);
    if (p->buttons.pressed & bit) {
        s_rep_ms = now;
        return true;
    }
    if ((p->buttons.down & bit) && (now - s_rep_ms) >= 400u) {
        s_rep_ms = now - 310u;
        return true;
    }
    return false;
}

static unsigned text_line_count(void) {
    unsigned lines = 0, col = 0;
    const char *p = s_csv;
    if (!s_csv_n) return 1u;
    while (*p) {
        if (*p == '\n') {
            lines++;
            col = 0;
        } else {
            col++;
            if (col >= 40u) {
                lines++;
                col = 0;
            }
        }
        p++;
    }
    if (col || lines == 0u) lines++;
    return lines;
}

static void text_line_at(unsigned want, char *out, size_t cap) {
    unsigned lines = 0, col = 0;
    const char *p = s_csv;
    const char *start = p;
    out[0] = '\0';
    if (!cap) return;
    while (*p) {
        if (lines == want && col == 0) start = p;
        if (*p == '\n') {
            if (lines == want) {
                size_t n = (size_t)(p - start);
                if (n > cap - 1u) n = cap - 1u;
                memcpy(out, start, n);
                out[n] = '\0';
                return;
            }
            lines++;
            col = 0;
            p++;
            continue;
        }
        col++;
        if (col >= 40u) {
            if (lines == want) {
                size_t n = (size_t)(p + 1 - start);
                if (n > cap - 1u) n = cap - 1u;
                memcpy(out, start, n);
                out[n] = '\0';
                return;
            }
            lines++;
            col = 0;
        }
        p++;
    }
    if (lines == want) {
        size_t n = strlen(start);
        if (n > cap - 1u) n = cap - 1u;
        memcpy(out, start, n);
        out[n] = '\0';
    }
}

static void paint_list(void) {
    const uint16_t bg  = st7789_rgb565(8, 10, 18);
    const uint16_t fg  = st7789_rgb565(230, 230, 230);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    const uint16_t sel = st7789_rgb565(140, 240, 160);
    const uint16_t ink = st7789_rgb565(8, 24, 12);
    const uint16_t yel = st7789_rgb565(230, 200, 80);
    char line[44];
    unsigned i;

    if (s_chrome_dirty) {
        st7789_fill_rect(0, 0, 320, 240, bg);
        lcd_text_draw_padded(8, 8, "DiskGlass", 18, 2, acc, bg);
        lcd_text_draw_padded(8, 204, "GRAY up  RED down  GREEN open", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 220, "tap BLUE T9  hold BLUE IR  YEL del", 40, 1, dim, bg);
        s_line_title[0] = '\0';
        s_line_vol[0] = '\0';
        for (i = 0; i < DG_ROWS; i++) s_line_row[i][0] = '\0';
        s_chrome_dirty = false;
        s_rows_dirty = true;
    }

    lcd_text_draw_padded_changed(200, 12, s_path, 18, 1, dim, bg,
                                 s_line_title, sizeof s_line_title);

    if (s_vol_err) {
        snprintf(line, sizeof line, "no FatFs volume");
    } else if (s_listen) {
        snprintf(line, sizeof line, "IR listen...  BLUE cancel");
    } else if (s_tx_stage) {
        snprintf(line, sizeof line, "IR send %u/%u",
                 (unsigned)s_tx_off, (unsigned)s_tx_n);
    } else if (s_status[0]) {
        snprintf(line, sizeof line, "%s", s_status);
    } else if (!s_list_end && s_nent == 0u) {
        snprintf(line, sizeof line, "waiting for main...");
    } else if (s_nent == 0u) {
        snprintf(line, sizeof line, "empty  %u KB free",
                 (unsigned)(s_free_bytes / 1024u));
    } else {
        snprintf(line, sizeof line, "%u files  %u KB free",
                 (unsigned)(s_count ? s_count : s_nent),
                 (unsigned)(s_free_bytes / 1024u));
    }
    lcd_text_draw_padded_changed(8, 40, line, 40, 1, yel, bg,
                                 s_line_vol, sizeof s_line_vol);

    if (s_cursor < s_scroll) {
        s_scroll = s_cursor;
        s_rows_dirty = true;
    }
    if (s_cursor >= s_scroll + (int)DG_ROWS) {
        s_scroll = s_cursor - (int)DG_ROWS + 1;
        s_rows_dirty = true;
    }
    if (s_scroll < 0) s_scroll = 0;

    if (s_rows_dirty) {
        for (i = 0; i < DG_ROWS; i++) s_line_row[i][0] = '\0';
        s_rows_dirty = false;
    }

    for (i = 0; i < DG_ROWS; i++) {
        const int idx = s_scroll + (int)i;
        const uint16_t y = (uint16_t)(56u + i * 16u);
        const bool on = (idx == s_cursor) && s_nent > 0u;
        const uint16_t row_bg = on ? sel : bg;
        const uint16_t row_fg = on ? ink : fg;
        char tagged[46];
        if (idx < 0 || idx >= (int)s_nent) {
            tagged[0] = on ? '*' : ' ';
            tagged[1] = '\0';
            if (s_line_row[i][0] == '\0' || strcmp(s_line_row[i], tagged) != 0) {
                st7789_fill_rect(0, y, 320, 16, bg);
                lcd_text_draw_padded(8, y, " ", 40, 1, dim, bg);
                strncpy(s_line_row[i], tagged, sizeof s_line_row[i] - 1u);
            }
            continue;
        }
        const dg_ent_t *e = &s_ent[idx];
        if (e->is_dir) {
            snprintf(line, sizeof line, "> %-20s  dir", e->name);
        } else {
            snprintf(line, sizeof line, "  %-12s %6u %s",
                     e->name, (unsigned)e->size, dg_kind_label(e->kind));
        }
        tagged[0] = on ? '*' : ' ';
        strncpy(tagged + 1, line, sizeof tagged - 2u);
        tagged[sizeof tagged - 1u] = '\0';
        if (strcmp(s_line_row[i], tagged) == 0) continue;
        st7789_fill_rect(0, y, 320, 16, row_bg);
        lcd_text_draw_padded(8, y, line, 40, 1, row_fg, row_bg);
        strncpy(s_line_row[i], tagged, sizeof s_line_row[i] - 1u);
        s_line_row[i][sizeof s_line_row[i] - 1u] = '\0';
    }
}

static void paint_del(void) {
    const uint16_t bg  = st7789_rgb565(8, 10, 18);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    const uint16_t yel = st7789_rgb565(230, 200, 80);
    const uint16_t rec = st7789_rgb565(220, 40, 40);
    if (!s_chrome_dirty) return;
    st7789_fill_rect(0, 0, 320, 240, bg);
    lcd_text_draw_padded(8, 8, "DiskGlass", 18, 2, acc, bg);
    lcd_text_draw_padded(8, 48, "delete this file?", 40, 1, yel, bg);
    lcd_text_draw_padded(8, 80, s_del_name[0] ? s_del_name : "(file)", 40, 1, rec, bg);
    lcd_text_draw_padded(8, 204, "GREEN deletes    YEL cancel", 40, 1, dim, bg);
    lcd_text_draw_padded(8, 220, "RED hold exits", 40, 1, dim, bg);
    s_chrome_dirty = false;
}

static void t9_begin(void) {
    memset(s_t9_buf, 0, sizeof s_t9_buf);
    s_t9_g = 0;
    s_t9_li = 0;
    s_scr = DG_SCR_T9;
    s_chrome_dirty = true;
    s_line_t9[0] = s_line_grp[0] = s_line_ch[0] = '\0';
}

static void t9_finish(void);

#ifndef HOST_TEST
static void qr_rebuild(void) {
    char tmp[96];
    uint8_t scratch[qrcodegen_BUFFER_LEN_FOR_VERSION(6)];
    unsigned n = s_csv_n;
    if (n > 80u) n = 80u;
    memcpy(tmp, s_csv, n);
    tmp[n] = '\0';
    s_qr_ok = n > 0u && qrcodegen_encodeText(tmp, scratch, s_qrcode,
        qrcodegen_Ecc_MEDIUM, 2, 6, qrcodegen_Mask_AUTO, true);
}

static void paint_qr(void) {
    const uint16_t bg  = st7789_rgb565(8, 10, 18);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    const uint16_t ink = st7789_rgb565(0, 0, 0);
    const uint16_t paper = st7789_rgb565(255, 255, 255);
    if (!s_chrome_dirty) return;
    st7789_fill_rect(0, 0, 320, 240, bg);
    lcd_text_draw_padded(8, 8, "DiskGlass QR", 18, 2, acc, bg);
    lcd_text_draw_padded(8, 220, "YEL back   phone scans this note", 40, 1, dim, bg);
    s_chrome_dirty = false;
    if (!s_qr_ok) {
        lcd_text_draw_padded(8, 80, "nothing to encode", 36, 1, dim, bg);
        return;
    }
    {
        const int n = qrcodegen_getSize(s_qrcode);
        const int scale = (n > 0 && n * 4 + 16 <= 200) ? 4 : 3;
        const int px = n * scale;
        const int x0 = (320 - (px + 8)) / 2;
        const int y0 = 40;
        int y;
        /* Quiet zone once. Repainting this every frame was the white stripe. */
        st7789_fill_rect((uint16_t)x0, (uint16_t)y0, (uint16_t)(px + 8),
                         (uint16_t)(px + 8), paper);
        for (y = 0; y < n; y++) {
            int run = 0, xs = 0, x;
            for (x = 0; x <= n; x++) {
                const bool dark = (x < n) && qrcodegen_getModule(s_qrcode, x, y);
                if (dark) {
                    if (!run) xs = x;
                    run++;
                } else if (run) {
                    st7789_fill_rect((uint16_t)(x0 + 4 + xs * scale),
                                     (uint16_t)(y0 + 4 + y * scale),
                                     (uint16_t)(run * scale), (uint16_t)scale, ink);
                    run = 0;
                }
            }
        }
    }
}
#endif

static void paint_view(void) {
    const uint16_t bg  = st7789_rgb565(8, 10, 18);
    const uint16_t fg  = st7789_rgb565(230, 230, 230);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    const uint16_t yel = st7789_rgb565(230, 200, 80);
    char line[44];
    unsigned i;

    if (s_chrome_dirty) {
        st7789_fill_rect(0, 0, 320, 240, bg);
        lcd_text_draw_padded(8, 8, "DiskGlass", 18, 2, acc, bg);
        if (is_text_kind(s_meta.kind)) {
            lcd_text_draw_padded(8, 204, "GRAY/BLUE scroll  GREEN QR", 40, 1, dim, bg);
            lcd_text_draw_padded(8, 220, "hold BLUE IR send   YEL back", 40, 1, dim, bg);
        } else if (s_meta.kind == DG_KIND_WASM) {
            lcd_text_draw_padded(8, 204, "GREEN tap once   hold GREEN loop", 40, 1, dim, bg);
            lcd_text_draw_padded(8, 220, "hold YEL stop/del  RED hold exits", 40, 1, dim, bg);
        } else if (s_meta.kind == DG_KIND_PCM16) {
            lcd_text_draw_padded(8, 204, "YEL back   GREEN play/stop", 40, 1, dim, bg);
            lcd_text_draw_padded(8, 220, "RED hold exits", 40, 1, dim, bg);
        } else {
            lcd_text_draw_padded(8, 204, "YEL back   BLUE replay OOK", 40, 1, dim, bg);
            lcd_text_draw_padded(8, 220, "RED hold exits", 40, 1, dim, bg);
        }
        for (i = 0; i < 10u; i++) s_line_view[i][0] = '\0';
        s_chrome_dirty = false;
    }

    snprintf(line, sizeof line, "%s", s_meta.name[0] ? s_meta.name : "(file)");
    lcd_text_draw_padded_changed(8, 40, line, 40, 1, yel, bg,
                                 s_line_view[0], sizeof s_line_view[0]);
    snprintf(line, sizeof line, "%s  %u bytes  %s",
             dg_kind_label(s_meta.kind), (unsigned)s_meta.size,
             s_meta.app[0] ? s_meta.app : "");
    lcd_text_draw_padded_changed(8, 56, line, 40, 1, fg, bg,
                                 s_line_view[1], sizeof s_line_view[1]);

    if (s_tx_stage) {
        snprintf(line, sizeof line, "IR send %u/%u",
                 (unsigned)s_tx_off, (unsigned)s_tx_n);
        lcd_text_draw_padded_changed(8, 72, line, 40, 1, acc, bg,
                                     s_line_view[2], sizeof s_line_view[2]);
    }

    if (s_meta.kind == DG_KIND_PCM16) {
        const uint32_t ms = s_meta.duration_us
            ? s_meta.duration_us / 1000u
            : dg_pcm_duration_ms(s_meta.size > 48u ? s_meta.size - 48u : s_meta.size,
                                 s_meta.sample_hz ? s_meta.sample_hz : 8000u);
        snprintf(line, sizeof line, "%s  %u.%03u s  %u Hz",
                 s_playing ? "PLAY" : "ready",
                 (unsigned)(ms / 1000u), (unsigned)(ms % 1000u),
                 (unsigned)(s_meta.sample_hz ? s_meta.sample_hz : 8000u));
        lcd_text_draw_padded_changed(8, 80, line, 40, 1, acc, bg,
                                     s_line_view[2], sizeof s_line_view[2]);
        lcd_text_draw_padded_changed(8, 96, "speaker is the display I2S", 40, 1, dim, bg,
                                     s_line_view[3], sizeof s_line_view[3]);
    } else if (is_text_kind(s_meta.kind)) {
        unsigned maxl = text_line_count();
        unsigned row;
        if (s_text_line < 0) s_text_line = 0;
        if ((unsigned)s_text_line + 6u > maxl && maxl > 6u)
            s_text_line = (int)maxl - 6;
        for (row = 0; row < 6u; row++) {
            text_line_at((unsigned)s_text_line + row, line, sizeof line);
            lcd_text_draw_padded_changed(8, (uint16_t)(80u + row * 16u),
                                         line[0] ? line : " ", 40, 1, fg, bg,
                                         s_line_view[3u + row],
                                         sizeof s_line_view[3u + row]);
        }
    } else if (s_meta.kind == DG_KIND_OOK || s_meta.kind == DG_KIND_IBST) {
        snprintf(line, sizeof line, "%u.%03u MHz  peak %+d dBm",
                 (unsigned)(s_meta.freq_hz / 1000000u),
                 (unsigned)((s_meta.freq_hz / 1000u) % 1000u),
                 (int)s_meta.peak_rssi);
        lcd_text_draw_padded_changed(8, 80, line, 40, 1, acc, bg,
                                     s_line_view[2], sizeof s_line_view[2]);
        snprintf(line, sizeof line, "%u edges  %u ms   %s",
                 (unsigned)s_meta.edges,
                 (unsigned)(s_meta.duration_us / 1000u),
                 (s_meta.flags & DG_FLAG_PLAYING) ? "TX" : "view");
        lcd_text_draw_padded_changed(8, 96, line, 40, 1, fg, bg,
                                     s_line_view[3], sizeof s_line_view[3]);
        lcd_text_draw_padded_changed(8, 120,
                                     "BLUE: 4 ASK frames then stop", 40, 1, dim, bg,
                                     s_line_view[4], sizeof s_line_view[4]);
    } else if (s_meta.kind == DG_KIND_WASM) {
        lcd_text_draw_padded_changed(8, 80, "wasm3  plasma + LEDs", 40, 1, acc, bg,
                                     s_line_view[2], sizeof s_line_view[2]);
        lcd_text_draw_padded_changed(8, 96, s_status, 40, 1, fg, bg,
                                     s_line_view[3], sizeof s_line_view[3]);
        lcd_text_draw_padded_changed(8, 120, "tap GREEN once, hold loops", 40, 1, dim, bg,
                                     s_line_view[4], sizeof s_line_view[4]);
    } else {
        lcd_text_draw_padded_changed(8, 80, "no player for this file", 40, 1, dim, bg,
                                     s_line_view[2], sizeof s_line_view[2]);
        lcd_text_draw_padded_changed(8, 96, s_status, 40, 1, fg, bg,
                                     s_line_view[3], sizeof s_line_view[3]);
    }
}

static void paint_t9(void) {
    const uint16_t bg  = st7789_rgb565(8, 10, 18);
    const uint16_t fg  = st7789_rgb565(230, 230, 230);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    char line[44];
    char ch[2];
    if (s_chrome_dirty) {
        st7789_fill_rect(0, 0, 320, 240, bg);
        lcd_text_draw_padded(8, 8, "DiskGlass T9", 18, 2, acc, bg);
        lcd_text_draw_padded(8, 176, "GRY/RED grp  YEL/BLU char", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 192, "GRN tap add  hold done", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 208, "YEL hold backspace", 40, 1, dim, bg);
        s_chrome_dirty = false;
        s_line_t9[0] = s_line_grp[0] = s_line_ch[0] = '\0';
    }
    lcd_text_draw_padded(8, 44, "note -> /inbox", 16, 1, dim, bg);
    snprintf(line, sizeof line, "[%s]", s_t9_buf);
    lcd_text_draw_padded_changed(8, 56, line, 40, 1, fg, bg,
                                 s_line_t9, sizeof s_line_t9);
    lcd_text_draw_padded_changed(8, 104, fwog_t9_group[s_t9_g], 5, 4, acc, bg,
                                 s_line_grp, sizeof s_line_grp);
    ch[0] = fwog_t9_cur(s_t9_g, s_t9_li);
    ch[1] = '\0';
    lcd_text_draw_padded_changed(180, 104, ch, 2, 4, fg, bg,
                                 s_line_ch, sizeof s_line_ch);
    lcd_text_draw_padded(8, 92, "grp", 8, 1, dim, bg);
    lcd_text_draw_padded(180, 92, "char", 8, 1, dim, bg);
}

static void paint(void) {
    if (!s_lcd) return;
    if (s_wasm_gfx) return;
#ifndef HOST_TEST
    if (s_scr == DG_SCR_QR) {
        paint_qr();
        return;
    }
#endif
    if (s_scr == DG_SCR_DEL) {
        paint_del();
        return;
    }
    if (s_scr == DG_SCR_T9) {
        paint_t9();
        return;
    }
    if (s_scr == DG_SCR_VIEW) paint_view();
    else paint_list();
}

static void enter_view(const dg_meta_t *m) {
    s_meta = *m;
    s_meta_ok = true;
    s_playing = false;
    s_pcm_wait = false;
    s_play_off = 0;
    s_text_line = 0;
    s_scr = DG_SCR_VIEW;
    s_chrome_dirty = true;
    if (!s_open_path[0] && m->name[0]) {
        if (m->name[0] == '/') {
            strncpy(s_open_path, m->name, sizeof s_open_path - 1u);
        } else {
            (void)dg_join_path(s_open_path, sizeof s_open_path, s_path, m->name);
        }
    }
    snprintf(s_status, sizeof s_status, "%u bytes", (unsigned)m->size);
    if (is_text_kind(m->kind)) {
        send_cmd(DG_CMD_READ, s_open_path, 0, DG_CSV_HINT);
    }
}

static void t9_finish(void) {
    uint16_t n = (uint16_t)strlen(s_t9_buf);
    s_chrome_dirty = true;
    if (n == 0u) {
        s_scr = DG_SCR_LIST;
        return;
    }
    if (n > DG_IR_MAX) n = (uint16_t)DG_IR_MAX;
    send_put("note.txt", (const uint8_t *)s_t9_buf, n);
    memset(s_csv, 0, sizeof s_csv);
    s_csv_n = n;
    if (s_csv_n > DG_CSV_HINT) s_csv_n = DG_CSV_HINT;
    memcpy(s_csv, s_t9_buf, s_csv_n);
    memset(&s_meta, 0, sizeof s_meta);
    s_meta.type = DG_MSG_META;
    s_meta.kind = DG_KIND_TEXT;
    s_meta.size = n;
    strncpy(s_meta.name, "IR note", DG_NAME_LEN - 1u);
    strncpy(s_meta.app, "ir", DG_APP_LEN - 1u);
    s_open_path[0] = '\0';
    enter_view(&s_meta);
}

static void open_cursor(void) {
    if (s_nent == 0u || s_cursor < 0 || s_cursor >= (int)s_nent) return;
    const dg_ent_t *e = &s_ent[s_cursor];
    if (e->is_dir) {
        char next[DG_PATH_LEN];
        if (!dg_join_path(next, sizeof next, s_path, e->name)) return;
        strncpy(s_path, next, sizeof s_path - 1u);
        s_path[sizeof s_path - 1u] = '\0';
        s_chrome_dirty = true;
        ask_list();
        return;
    }
    if (!dg_join_path(s_open_path, sizeof s_open_path, s_path, e->name)) return;
    send_cmd(DG_CMD_OPEN, s_open_path, 0, 0);
}

static void go_back(void) {
#ifndef HOST_TEST
    if (s_i2s) i2s_audio_stop();
#endif
    s_playing = false;
    s_tx_stage = 0;
    if (s_scr == DG_SCR_T9) {
        s_scr = DG_SCR_LIST;
        s_chrome_dirty = true;
        return;
    }
    if (s_scr == DG_SCR_QR) {
        s_scr = DG_SCR_VIEW;
        s_chrome_dirty = true;
        return;
    }
    if (s_scr == DG_SCR_DEL) {
        s_scr = DG_SCR_LIST;
        s_chrome_dirty = true;
        return;
    }
    if (s_scr == DG_SCR_VIEW) {
        send_cmd(DG_CMD_CLOSE, s_open_path, 0, 0);
        s_scr = DG_SCR_LIST;
        s_chrome_dirty = true;
        s_meta_ok = false;
        return;
    }
    if (s_path[0] == '/' && s_path[1] == '\0') return;
    {
        char up[DG_PATH_LEN];
        parent_path(up, sizeof up);
        strncpy(s_path, up, sizeof s_path - 1u);
        s_path[sizeof s_path - 1u] = '\0';
        s_chrome_dirty = true;
        ask_list();
    }
}

static void play_toggle(void) {
    if (!s_meta_ok || s_meta.kind != DG_KIND_PCM16) return;
    if (s_playing) {
#ifndef HOST_TEST
        if (s_i2s) i2s_audio_stop();
#endif
        s_playing = false;
        send_cmd(DG_CMD_CLOSE, s_open_path, 0, 0);
        return;
    }
    s_playing = true;
    s_pcm_wait = true;
    s_play_off = sizeof(dg_hdr_t);
    send_cmd(DG_CMD_OPEN, s_open_path, 0, 0);
    send_cmd(DG_CMD_READ, s_open_path, s_play_off, DG_DATA_MAX);
}

static void enter_del(const char *path, const char *name) {
    if (!path || !path[0]) return;
    strncpy(s_del_path, path, sizeof s_del_path - 1u);
    s_del_path[sizeof s_del_path - 1u] = '\0';
    memset(s_del_name, 0, sizeof s_del_name);
    if (name && name[0]) strncpy(s_del_name, name, sizeof s_del_name - 1u);
    s_scr = DG_SCR_DEL;
    s_chrome_dirty = true;
    s_del_ready = false;
    s_grn_arm = false;
}

static void confirm_del(void) {
    send_cmd(DG_CMD_DEL, s_del_path, 0, 0);
    s_scr = DG_SCR_LIST;
    s_chrome_dirty = true;
    s_meta_ok = false;
    snprintf(s_status, sizeof s_status, "deleted");
    ask_list();
}

static void ir_tx_begin(const uint8_t *p, uint16_t n) {
    if (!s_ir || !p || n == 0u) return;
    if (n > DG_IR_MAX) n = DG_IR_MAX;
    memcpy(s_tx_buf, p, n);
    s_tx_n = n;
    s_tx_off = 0;
    s_tx_stage = 1;
    s_tx_ms = 0;
}

static void ir_tx_pump(uint32_t now) {
#ifndef HOST_TEST
    uint32_t code;
    if (!s_tx_stage || !s_ir) return;
    if (s_tx_ms && (now - s_tx_ms) < DG_IR_GAP_MS) return;
    if (s_tx_stage == 1) {
        code = dg_ir_start(s_tx_n);
        ir_comm_send(code);
        s_tx_stage = 2;
        s_tx_off = 0;
    } else if (s_tx_stage == 2) {
        uint8_t a = (s_tx_off < s_tx_n) ? s_tx_buf[s_tx_off] : 0u;
        uint8_t b = (s_tx_off + 1u < s_tx_n) ? s_tx_buf[s_tx_off + 1u] : 0u;
        code = dg_ir_data((uint8_t)(s_tx_off / 2u), a, b);
        ir_comm_send(code);
        s_tx_off = (uint16_t)(s_tx_off + 2u);
        if (s_tx_off >= s_tx_n) s_tx_stage = 3;
    } else {
        code = dg_ir_end(dg_ir_crc(s_tx_buf, s_tx_n));
        ir_comm_send(code);
        s_tx_stage = 0;
        snprintf(s_status, sizeof s_status, "IR sent %u crc %04X",
                 (unsigned)s_tx_n,
                 (unsigned)dg_ir_crc(s_tx_buf, s_tx_n));
        DIAG("[diskglass] %s\n", s_status);
    }
    s_tx_ms = now;
#else
    (void)now;
#endif
}

static void ir_rx_pump(uint32_t now) {
#ifndef HOST_TEST
    uint32_t code;
    if (!s_listen || !s_ir) return;
    if ((now - s_listen_ms) > DG_IR_WAIT) {
        s_listen = false;
        snprintf(s_status, sizeof s_status, "IR timeout");
        return;
    }
    while (ir_comm_read(&code)) {
        if (!dg_ir_rx_feed(&s_irx, code)) continue;
        s_listen = false;
        memset(s_csv, 0, sizeof s_csv);
        s_csv_n = s_irx.nbytes;
        if (s_csv_n > DG_CSV_HINT) s_csv_n = DG_CSV_HINT;
        memcpy(s_csv, s_irx.buf, s_csv_n);
        send_put("note.txt", s_irx.buf, s_irx.nbytes);
        snprintf(s_status, sizeof s_status, "IR got %u crc %04X",
                 (unsigned)s_irx.nbytes,
                 (unsigned)dg_ir_crc(s_irx.buf, s_irx.nbytes));
        memset(&s_meta, 0, sizeof s_meta);
        s_meta.type = DG_MSG_META;
        s_meta.kind = DG_KIND_TEXT;
        s_meta.size = s_irx.nbytes;
        strncpy(s_meta.name, "IR note", DG_NAME_LEN - 1u);
        strncpy(s_meta.app, "ir", DG_APP_LEN - 1u);
        s_open_path[0] = '\0';
        enter_view(&s_meta);
        DIAG("[diskglass] IR got %u bytes\n", (unsigned)s_irx.nbytes);
        return;
    }
#else
    (void)now;
#endif
}

static uint16_t rgb332_to_565(uint8_t c) {
    uint8_t r = (uint8_t)((c >> 5) & 7u);
    uint8_t g = (uint8_t)((c >> 2) & 7u);
    uint8_t b = (uint8_t)(c & 3u);
    r = (uint8_t)((r << 5) | (r << 2) | (r >> 1));
    g = (uint8_t)((g << 5) | (g << 2) | (g >> 1));
    b = (uint8_t)(b * 85u);
    return st7789_rgb565(r, g, b);
}

static void paint_fb(const uint8_t *px, unsigned w, unsigned h) {
    uint16_t row[ST7789_W];
    unsigned y, x, k, sx, cw, ch;
    if (!s_lcd || !px || w == 0u || h == 0u) return;
    cw = ST7789_W / w;
    ch = ST7789_H / h;
    if (cw == 0u) cw = 1u;
    if (ch == 0u) ch = 1u;
    for (y = 0; y < h; y++) {
        sx = 0;
        for (x = 0; x < w; x++) {
            uint16_t c = rgb332_to_565(px[y * w + x]);
            unsigned i;
            for (i = 0; i < cw && sx < ST7789_W; i++, sx++) row[sx] = c;
        }
        while (sx < ST7789_W) {
            row[sx] = (sx > 0u) ? row[sx - 1u] : 0;
            sx++;
        }
        for (k = 0; k < ch; k++) {
            unsigned yy = y * ch + k;
            if (yy >= ST7789_H) break;
            st7789_set_window(0, (uint16_t)yy, ST7789_W, 1u);
            st7789_blit(row, ST7789_W);
        }
    }
}

static void poll_link(void) {
    uint8_t b;
    size_t n;
    while (fwog_link_uart_read(&b)) {
        if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
        if (fwog_ioexp_link_handle(s_rx.buf, n)) continue;
        if (n >= sizeof(dg_list_t) && s_rx.buf[0] == DG_MSG_LIST) {
            dg_list_t in;
            memcpy(&in, s_rx.buf, sizeof in);
            if (in.flags & DG_FLAG_ERR) {
                s_vol_err = true;
                s_mounted = false;
                s_list_end = true;
                s_chrome_dirty = true;
                continue;
            }
            if (in.flags & DG_FLAG_MOUNTED) {
                s_mounted = true;
                s_vol_err = false;
                s_free_bytes = in.size;
                s_count = in.count;
            }
            if (in.flags & DG_FLAG_END) {
                s_list_end = true;
                if (in.count) s_count = in.count;
                continue;
            }
            if (in.name[0] && in.index < DG_MAX_ENT) {
                dg_ent_t *e = &s_ent[in.index];
                memset(e, 0, sizeof *e);
                strncpy(e->name, in.name, DG_NAME_LEN - 1u);
                e->size = in.size;
                e->kind = in.kind;
                e->is_dir = (in.flags & DG_FLAG_DIR) ? 1u : 0u;
                if (s_nent <= in.index) s_nent = (unsigned)in.index + 1u;
                s_rows_dirty = true;
            }
        } else if (n >= sizeof(dg_meta_t) && s_rx.buf[0] == DG_MSG_META) {
            dg_meta_t in;
            memcpy(&in, s_rx.buf, sizeof in);
            enter_view(&in);
        } else if (n >= 8u && s_rx.buf[0] == DG_MSG_DATA) {
            dg_data_t in;
            size_t take = n;
            if (take > sizeof in) take = sizeof in;
            memcpy(&in, s_rx.buf, take);
            if (s_scr != DG_SCR_LIST && is_text_kind(s_meta.kind)) {
                unsigned cpy = in.n;
                if (cpy > DG_CSV_HINT) cpy = DG_CSV_HINT;
                memcpy(s_csv, in.bytes, cpy);
                s_csv[cpy] = '\0';
                s_csv_n = cpy;
            } else if (s_playing && s_meta.kind == DG_KIND_PCM16) {
#ifndef HOST_TEST
                unsigned ns = in.n / 2u;
                if (ns > (unsigned)(sizeof s_pcm / sizeof s_pcm[0])) {
                    ns = (unsigned)(sizeof s_pcm / sizeof s_pcm[0]);
                }
                memcpy(s_pcm, in.bytes, ns * 2u);
                s_pcm_n = ns;
                if (s_i2s && ns > 0u) {
                    (void)i2s_audio_start(s_pcm, ns, true, false);
                }
#endif
                s_play_off = in.offset + in.n;
                s_pcm_wait = false;
                if (in.n == 0u) s_playing = false;
            }
        } else if (n >= 2u && s_rx.buf[0] == DG_MSG_HOST) {
            uint8_t op = s_rx.buf[1];
            if (op == DG_HOST_FB && n >= 6u) {
                uint8_t w = s_rx.buf[2];
                uint8_t h = s_rx.buf[3];
                uint16_t pn;
                memcpy(&pn, &s_rx.buf[4], sizeof pn);
                if (w == 0u || h == 0u) continue;
                if ((uint32_t)w * (uint32_t)h > DG_FB_MAX) continue;
                if (pn > DG_FB_MAX) pn = (uint16_t)DG_FB_MAX;
                if (n < (size_t)(6u + pn)) continue;
                s_wasm_gfx = true;
                s_wasm_leds = true;
                paint_fb(&s_rx.buf[6], w, h);
            } else if (n >= sizeof(dg_host_t)) {
                dg_host_t in;
                memcpy(&in, s_rx.buf, sizeof in);
                if (in.op == DG_HOST_LED) {
                    s_wasm_leds = true;
                    if (s_leds && in.idx < (uint8_t)FWOG_LED_COUNT) {
                        ws2812_set_color(in.idx, in.r, in.g, in.b);
                        ws2812_process();
                    }
                } else if (in.op == DG_HOST_DONE) {
                    s_wasm_leds = false;
                    s_wasm_gfx = false;
                    s_chrome_dirty = true;
                    if (in.code == 0u) snprintf(s_status, sizeof s_status, "wasm ok");
                    else if (in.code == 4u) snprintf(s_status, sizeof s_status, "stopped");
                    else snprintf(s_status, sizeof s_status, "wasm fail %u",
                                  (unsigned)in.code);
                    if (s_wasm_leave) {
                        s_wasm_leave = false;
                        go_back();
                    }
                }
            }
        }
    }
}

static void play_pump(void) {
#ifndef HOST_TEST
    if (s_i2s) i2s_audio_process();
    if (!s_playing || s_meta.kind != DG_KIND_PCM16) return;
    if (s_pcm_wait) return;
    if (!s_i2s || !i2s_audio_is_idle()) return;
    if (s_meta.size && s_play_off >= s_meta.size) {
        s_playing = false;
        return;
    }
    s_pcm_wait = true;
    send_cmd(DG_CMD_READ, s_open_path, s_play_off, DG_DATA_MAX);
#else
    (void)0;
#endif
}

int main(void) {
    board_init();
    fwog_splash_bind("DiskGlass", "009");
    s_leds = ws2812_init(pio0, 0u);
#ifndef HOST_TEST
    s_i2s = i2s_audio_init(pio0, 1u);
    s_ir = ir_comm_init(pio1, 0u);
#endif
    lcd_bringup();
    s_link = fwog_link_uart_init(FWOG_LINK_BAUD);
    fwog_link_rx_init(&s_rx);
    DIAG("[diskglass] link=%s i2s=%s ir=%s\n",
         s_link ? "ok" : "FAIL", s_i2s ? "ok" : "off", s_ir ? "ok" : "off");
    ask_list();

    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        const fwog_power_t p = fwog_power_poll(now);
        s_power_armed = p.armed;
        poll_link();
        play_pump();
        ir_tx_pump(now);
        ir_rx_pump(now);

        if (s_scr == DG_SCR_T9) {
            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GRAY)) {
                fwog_t9_group_prev(&s_t9_g, &s_t9_li);
            }
            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_RED)) {
                fwog_t9_group_next(&s_t9_g, &s_t9_li);
            }
        } else if (s_scr == DG_SCR_LIST) {
            if (btn_repeat(&p, FWOG_BTN_GRAY, now)) cursor_move(-1);
            /* Red tap = down. Do not gate on p.armed: the 6 s exit hold
               arms on the same frame as pressed, which is why 004's
               !armed check ate every tap. Hold-repeat stays off — hold
               is still power-off. */
            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_RED)) {
                cursor_move(1);
            }
            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GREEN)) {
                open_cursor();
            }
        } else if (s_scr == DG_SCR_VIEW) {
            if (is_text_kind(s_meta.kind)) {
                unsigned maxl = text_line_count();
                if (btn_repeat(&p, FWOG_BTN_GRAY, now)) {
                    if (s_text_line > 0) s_text_line--;
                }
                if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_RED)) {
                    if ((unsigned)s_text_line + 6u < maxl) s_text_line++;
                }
                if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GREEN)) {
#ifndef HOST_TEST
                    qr_rebuild();
                    s_scr = DG_SCR_QR;
                    s_chrome_dirty = true;
#else
                    (void)maxl;
#endif
                }
            } else if (s_meta.kind == DG_KIND_WASM) {
                /* Green tap vs hold is handled below, like Yellow. */
            } else {
                if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GREEN)) {
                    play_toggle();
                }
            }
        } else if (s_scr == DG_SCR_DEL) {
            if (!(p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GREEN))) {
                s_del_ready = true;
            }
            if (s_del_ready &&
                (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GREEN))) {
                confirm_del();
            }
        }

        /* Blue hold 700 ms = IR listen (list) or IR send (text).
         * Blue tap on the list opens T9; tap while listening cancels. */
        {
            const bool bdown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) != 0;
            if (bdown && !s_blu_was) {
                s_blue_ms = now;
                s_blue_hold = false;
                if (s_scr == DG_SCR_LIST && s_listen) {
                    s_listen = false;
                    s_blue_hold = true; /* swallow the tap */
                    snprintf(s_status, sizeof s_status, "IR cancelled");
                }
            }
            if (!s_blue_hold && bdown && (now - s_blue_ms) >= 700u) {
                s_blue_hold = true;
                if (s_scr == DG_SCR_T9) {
                    /* composing: Blue tap is letter next, not IR */
                } else if (s_scr == DG_SCR_LIST && s_ir) {
                    dg_ir_rx_reset(&s_irx);
                    s_listen = true;
                    s_listen_ms = now;
                    s_status[0] = '\0';
                } else if (s_scr == DG_SCR_VIEW && is_text_kind(s_meta.kind) &&
                           s_tx_stage == 0 && s_csv_n > 0u) {
                    ir_tx_begin((const uint8_t *)s_csv, (uint16_t)s_csv_n);
                } else if (s_scr == DG_SCR_VIEW &&
                           (s_meta.kind == DG_KIND_OOK || s_meta.kind == DG_KIND_IBST)) {
                    send_cmd(DG_CMD_REPLAY, s_open_path, 0, 0);
                }
            }
            if (!bdown && s_blu_was && !s_blue_hold) {
                if (s_scr == DG_SCR_T9) {
                    fwog_t9_letter_next(s_t9_g, &s_t9_li);
                } else if (s_scr == DG_SCR_LIST) {
                    t9_begin();
                }
            }
            s_blu_was = bdown;
        }

        {
            const bool ydown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) != 0;
            const bool gdown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GREEN)) != 0;
            const bool chord = ydown && gdown;

            /* Rising/falling on the settled `down` level. `pressed` can
             * fire more than once while still held and reset the timer. */
            if (ydown && !s_yel_was) {
                s_yel_ms = now;
                s_yel_hold = false;
            }
            if (ydown && !s_yel_hold && (now - s_yel_ms) >= 700u) {
                s_yel_hold = true;
                if (s_scr == DG_SCR_T9) {
                    fwog_t9_backspace(s_t9_buf);
                } else if (wasm_busy()) {
                    s_wasm_leave = true;
                    snprintf(s_status, sizeof s_status, "stopping...");
                    send_stop();
                } else if (chord) {
                    /* Yellow+Green together is not delete. */
                } else if (s_scr == DG_SCR_LIST && s_nent > 0u &&
                    s_cursor >= 0 && s_cursor < (int)s_nent &&
                    !s_ent[s_cursor].is_dir) {
                    char path[DG_PATH_LEN];
                    if (dg_join_path(path, sizeof path, s_path, s_ent[s_cursor].name)) {
                        enter_del(path, s_ent[s_cursor].name);
                    }
                } else if (s_scr == DG_SCR_VIEW && s_open_path[0]) {
                    enter_del(s_open_path, s_meta.name);
                }
            }
            if (!ydown && s_yel_was && !s_yel_hold && !gdown) {
                if (s_scr == DG_SCR_T9) {
                    fwog_t9_letter_prev(s_t9_g, &s_t9_li);
                } else if (!wasm_busy()) {
                    go_back();
                }
            }
            s_yel_was = ydown;

            if (gdown && !s_grn_was) {
                s_grn_ms = now;
                s_grn_hold = false;
                s_grn_arm = ((s_scr == DG_SCR_T9) ||
                             (s_scr == DG_SCR_VIEW && s_meta.kind == DG_KIND_WASM &&
                              !wasm_busy() && s_open_path[0])) && !ydown;
            }
            if (s_grn_arm && !s_grn_hold && gdown && !ydown &&
                (now - s_grn_ms) >= 700u) {
                s_grn_hold = true;
                if (s_scr == DG_SCR_T9) {
                    t9_finish();
                } else if (s_scr == DG_SCR_VIEW && s_meta.kind == DG_KIND_WASM &&
                    !wasm_busy() && s_open_path[0]) {
                    snprintf(s_status, sizeof s_status, "looping... hold YEL stop");
                    send_run(true);
                } else {
                    s_grn_arm = false;
                }
            }
            if (!gdown && s_grn_was) {
                if (s_grn_arm && !s_grn_hold && !ydown) {
                    if (s_scr == DG_SCR_T9) {
                        fwog_t9_insert(s_t9_buf, sizeof s_t9_buf,
                                       fwog_t9_cur(s_t9_g, s_t9_li));
                    } else if (s_scr == DG_SCR_VIEW && s_meta.kind == DG_KIND_WASM &&
                        !wasm_busy() && s_open_path[0]) {
                        snprintf(s_status, sizeof s_status, "running...");
                        send_run(false);
                    }
                }
                s_grn_arm = false;
            }
            s_grn_was = gdown;
        }

        if (!s_list_end && s_scr == DG_SCR_LIST &&
            !s_mounted && !s_vol_err &&
            (now - s_list_ask_ms) >= 2000u) {
            s_list_ask_ms = now;
            send_cmd(DG_CMD_LIST, s_path, 0, 0);
        }

        if (!p.armed) leds_idle();
        paint();
        sleep_ms(2);
    }
}
