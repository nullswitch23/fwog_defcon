#include "fwog_display.h"
#include "ed_link.h"
#include "ed_te.h"
#include "te_proto.h"
#include "te_tpms.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



#define TE_HOLD_MS 700u

static bool           s_lcd, s_link;
static int            s_preset;
static bool           s_fsk;
static te_status_t    s_st;
static te_row_t       s_row;
static bool           s_st_ok, s_row_ok;
static bool           s_chrome_dirty = true;
static fwog_link_rx_t s_rx;
static char           s_line_mode[44];
static char           s_line_rssi[44];
static char           s_line_dec[44];
static char           s_line_scan[44];
static char           s_line_row[44];
static char           s_line_raw[44];
static uint8_t        s_hint_mode = 0xffu;
static uint32_t       s_blu_ms, s_grn_ms;
static bool           s_blu_was, s_grn_was;
static bool           s_blu_hold, s_grn_hold;

static void send_cmd(uint8_t cmd, uint8_t on, uint8_t preset) {
    te_cmd_t m = { .type = TE_MSG_CMD, .cmd = cmd, .on = on, .preset = preset };
    if (ed_link_ok()) (void)ed_link_send(&m, sizeof m);
}

static void send_preset(void) {
    send_cmd(TE_CMD_PRESET, 0,
             (uint8_t)((s_preset ? TE_PRESET_FREQ : 0u) |
                       (s_fsk ? TE_PRESET_FSK : 0u)));
}



void ed_te_frame(const uint8_t *buf, size_t n) {
        if (n >= sizeof(te_status_t) && buf[0] == TE_MSG_ST) {
            memcpy(&s_st, buf, sizeof s_st);
            s_st_ok = true;
        }
        if (n >= sizeof(te_row_t) && buf[0] == TE_MSG_ROW) {
            memcpy(&s_row, buf, sizeof s_row);
            s_row_ok = true;
        }
}

static const char *mode_label(uint8_t m) {
    switch (m) {
    case TE_MODE_SCAN:   return "SCAN";
    case TE_MODE_REVIEW: return "REVIEW";
    default:             return "listen";
    }
}

static void fmt_id(char *dst, size_t cap, const uint8_t *id, uint8_t len) {
    if (!len) {
        snprintf(dst, cap, "raw");
        return;
    }
    snprintf(dst, cap, "%02X%02X%02X%02X", id[0], id[1], id[2], id[3]);
    (void)len;
}

static void paint_listen(uint16_t bg, uint16_t fg, uint16_t dim, uint16_t acc) {
    char line[44];
    const unsigned pre = s_st_ok
        ? (unsigned)s_st.preset
        : (unsigned)((s_preset ? TE_PRESET_FREQ : 0u) |
                     (s_fsk ? TE_PRESET_FSK : 0u));
    const te_profile_desc_t *pd =
        te_profile_desc(s_st_ok ? s_st.profile : 1u);
    snprintf(line, sizeof line, "%s  %s  %s  %s",
             (pre & TE_PRESET_FREQ) ? "433 EU" : "315 NA",
             (pre & TE_PRESET_FSK) ? "2-FSK" : "ASK",
             mode_label(s_st_ok ? s_st.mode : TE_MODE_LISTEN),
             pd ? pd->name : "?");
    lcd_text_draw_padded_changed(8, 48, line, 44, 1, fg, bg,
                                 s_line_mode, sizeof s_line_mode);
    snprintf(line, sizeof line, "RSSI %+d  peak %+d  %u ms",
             s_st_ok ? (int)s_st.rssi : 0,
             s_st_ok ? (int)s_st.last_rssi : 0,
             s_st_ok ? (unsigned)s_st.last_ms : 0);
    lcd_text_draw_padded_changed(8, 72, line, 44, 1, fg, bg,
                                 s_line_rssi, sizeof s_line_rssi);
    if (s_st_ok && s_st.crc_ok) {
        snprintf(line, sizeof line, "ID %02X%02X%02X%02X  %d.%d psi  %d C",
                 s_st.last_id[0], s_st.last_id[1], s_st.last_id[2],
                 s_st.last_id[3],
                 (int)s_st.last_psi_x10 / 10, abs((int)s_st.last_psi_x10 % 10),
                 (int)s_st.last_temp_c);
    } else {
        snprintf(line, sizeof line, "bursts %u  OEM decode %s",
                 s_st_ok ? (unsigned)s_st.bursts : 0u,
                 (s_st_ok && s_st.crc_ok) ? "ok" : "pending");
    }
    lcd_text_draw_padded_changed(8, 96, line, 44, 1,
                                 (s_st_ok && s_st.crc_ok) ? acc : dim, bg,
                                 s_line_dec, sizeof s_line_dec);
    if (s_st_ok && s_st.raw_n) {
        unsigned i;
        char *p = line;
        *p++ = 'b';
        *p++ = ':';
        for (i = 0; i < s_st.raw_n && (size_t)(p - line) < sizeof line - 4u; i++)
            p += (unsigned)snprintf(p, (size_t)(line + sizeof line - p), "%02X",
                                    s_st.raw_hex[i]);
        lcd_text_draw_padded_changed(8, 120, line, 44, 1, dim, bg,
                                     s_line_raw, sizeof s_line_raw);
    } else {
        lcd_text_draw_padded_changed(8, 120, "", 44, 1, dim, bg,
                                     s_line_raw, sizeof s_line_raw);
    }
}

static void paint_scan(uint16_t bg, uint16_t fg, uint16_t dim, uint16_t acc) {
    char line[44];
    snprintf(line, sizeof line, "SCAN  %us left  IDs %u  %s",
             s_st_ok ? (unsigned)(s_st.scan_left_ms / 1000u) : 0u,
             s_st_ok ? (unsigned)s_st.scan_n : 0u,
             te_profile_desc(s_st_ok ? s_st.profile : 1u)->name);
    lcd_text_draw_padded_changed(8, 48, line, 44, 1, acc, bg,
                                 s_line_mode, sizeof s_line_mode);
    snprintf(line, sizeof line, "RSSI %+d  peak %+d  %u ms",
             s_st_ok ? (int)s_st.rssi : 0,
             s_st_ok ? (int)s_st.last_rssi : 0,
             s_st_ok ? (unsigned)s_st.last_ms : 0);
    lcd_text_draw_padded_changed(8, 72, line, 44, 1, fg, bg,
                                 s_line_rssi, sizeof s_line_rssi);
    snprintf(line, sizeof line, "bursts %u  this window",
             s_st_ok ? (unsigned)s_st.bursts : 0u);
    lcd_text_draw_padded_changed(8, 96, line, 44, 1, dim, bg,
                                 s_line_dec, sizeof s_line_dec);
    lcd_text_draw_padded_changed(8, 120, "", 44, 1, dim, bg,
                                 s_line_raw, sizeof s_line_raw);
}

static void paint_review(uint16_t bg, uint16_t fg, uint16_t dim, uint16_t acc) {
    char line[44];
    char id[16];
    snprintf(line, sizeof line, "REVIEW  %u IDs  row %u/%u",
             s_st_ok ? (unsigned)s_st.scan_n : 0u,
             s_st_ok ? (unsigned)s_st.cursor + 1u : 0u,
             s_st_ok ? (unsigned)s_st.scan_n : 0u);
    lcd_text_draw_padded_changed(8, 48, line, 44, 1, acc, bg,
                                 s_line_scan, sizeof s_line_scan);
    if (s_row_ok) {
        fmt_id(id, sizeof id, s_row.id, s_row.id_len);
        snprintf(line, sizeof line, "%s  %+d dBm  x%u",
                 id, (int)s_row.peak_rssi, (unsigned)s_row.bursts);
        lcd_text_draw_padded_changed(8, 72, line, 44, 1, fg, bg,
                                     s_line_row, sizeof s_line_row);
        if (s_row.crc_ok) {
            snprintf(line, sizeof line, "%d.%d psi  %d C  prof %u",
                     (int)s_row.psi_x10 / 10,
                     abs((int)s_row.psi_x10 % 10),
                     (int)s_row.temp_c, (unsigned)s_row.profile);
        } else {
            snprintf(line, sizeof line, "undecoded  raw %u B", (unsigned)s_row.raw_n);
        }
        lcd_text_draw_padded_changed(8, 96, line, 44, 1, fg, bg,
                                     s_line_dec, sizeof s_line_dec);
        snprintf(line, sizeof line, "first %us  last %us",
                 (unsigned)s_row.first_s, (unsigned)s_row.last_s);
        lcd_text_draw_padded_changed(8, 120, line, 44, 1, dim, bg,
                                     s_line_rssi, sizeof s_line_rssi);
    } else {
        lcd_text_draw_padded(8, 72, "waiting row...", 44, 1, dim, bg);
    }
}

static void paint(void) {
    if (!s_lcd) return;
    const uint16_t bg  = st7789_rgb565(8, 10, 18);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    const uint16_t fg  = st7789_rgb565(230, 230, 230);
    const uint8_t mode = s_st_ok ? s_st.mode : TE_MODE_LISTEN;
    if (s_chrome_dirty) {
        st7789_fill_rect(0, 0, 320, 40, bg);
        lcd_text_draw_padded(8, 8, "TireEar", 18, 2, acc, bg);
        s_chrome_dirty = false;
        s_hint_mode = 0xffu;
    }
    if (s_hint_mode != mode) {
        const char *hint2 = "GRN scan  hold REVIEW  BLUhold OEM";
        if (mode == TE_MODE_SCAN)
            hint2 = "GRN stop+list  hold REVIEW  BLUhold OEM";
        else if (mode == TE_MODE_REVIEW)
            hint2 = "GRY up  RED down  GRN listen";
        st7789_fill_rect(0, 196, 320, 44, bg);
        lcd_text_draw_padded(8, 200, "YEL 315/433     BLU ASK/FSK", 42, 1, dim, bg);
        lcd_text_draw_padded(8, 216, hint2, 42, 1, dim, bg);
        s_hint_mode = mode;
    }
    if (mode == TE_MODE_REVIEW)
        paint_review(bg, fg, dim, acc);
    else if (mode == TE_MODE_SCAN)
        paint_scan(bg, fg, dim, acc);
    else
        paint_listen(bg, fg, dim, acc);
}



void ed_te_enter(void) {
    s_lcd = true;
    s_chrome_dirty = true;
    (void)fwog_ioexp_link_set_antennas(
        s_preset ? FWOG_ANT_400MHZ : FWOG_ANT_200MHZ,
        s_preset ? FWOG_ANT_400MHZ : FWOG_ANT_200MHZ);
    send_preset();
}

void ed_te_paint(void) { paint(); }

void ed_te_buttons(const fwog_power_t *p, uint32_t now,
                   bool y_long, bool g_long, bool gray_long, bool red_tap) {
    (void)y_long;
    if ((p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) && !y_long) {
        s_preset ^= 1;
        (void)fwog_ioexp_link_set_antennas(
            s_preset ? FWOG_ANT_400MHZ : FWOG_ANT_200MHZ,
            s_preset ? FWOG_ANT_400MHZ : FWOG_ANT_200MHZ);
        send_preset();
        s_chrome_dirty = true;
    }
    if (p->buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_BLUE)) {
        s_blu_ms = now;
        s_blu_hold = false;
    }
    if ((p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE))) {
        if (!s_blu_hold && now - s_blu_ms >= TE_HOLD_MS) {
            s_blu_hold = true;
            send_cmd(TE_CMD_PROFILE, 0, 0);
            s_chrome_dirty = true;
        }
    } else {
        s_blu_hold = false;
    }
    if ((p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_GREEN)) && !g_long) {
        const uint8_t mode = s_st_ok ? s_st.mode : TE_MODE_LISTEN;
        if (mode == TE_MODE_SCAN) send_cmd(TE_CMD_SCAN, 0, 0);
        else if (mode == TE_MODE_REVIEW) send_cmd(TE_CMD_MODE, TE_MODE_LISTEN, 0);
        else send_cmd(TE_CMD_SCAN, 1, 0);
        s_chrome_dirty = true;
    }
    /* GRN hold REVIEW remapped: BLUE hold already OEM; REVIEW via scan-stop. */
    if ((p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_GRAY)) && !gray_long)
        send_cmd(TE_CMD_CURSOR, 0, 0);
    if (red_tap && (p->buttons.released & FWOG_BTN_BIT(FWOG_BTN_RED)))
        send_cmd(TE_CMD_CURSOR, 1, 0);
}
