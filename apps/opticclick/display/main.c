/* OpticClick — multi-PHY IR capture / replay + NEC brand library. */
#include "fwog_display.h"
#include "hardware/pio.h"
#include "oc_codes.h"
#include "oc_proto.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

FWOG_POWER_DEFAULT();

#define OC_HOLD_MS   750u
#define OC_BLAST_MS  280u
#define OC_MODE_CAP  0
#define OC_MODE_LIB  1
#define OC_SCR_MAIN  0
#define OC_SCR_CAT   1
#define OC_SCR_PHY   2

static const char *const k_t9[] = {
    "ABC", "DEF", "GHI", "JKL", "MNO", "PQRS", "TUV", "WXYZ",
};
#define OC_T9_GROUPS ((int)(sizeof k_t9 / sizeof k_t9[0]))

static bool     s_lcd;
static bool     s_leds;
static bool     s_ir;
static bool     s_link;
static int      s_mode = OC_MODE_CAP;
static int      s_scr = OC_SCR_MAIN;
static int      s_slot;
static int      s_lib;
static int      s_lib_fn;
static bool     s_armed;
static bool     s_slots_ok;
static bool     s_chrome_dirty = true;
static bool     s_painted;
static uint32_t s_code[OC_SLOTS];
static bool     s_have[OC_SLOTS];
static uint8_t  s_phy_slot[OC_SLOTS];
static int      s_phy_pick;
static char     s_brand[OC_SLOTS][OC_BRAND_LEN];
static char     s_func[OC_SLOTS][OC_FUNC_LEN];
static uint32_t s_flash_ms;
static bool     s_power_armed;
static uint32_t s_get_ms;

static fwog_link_rx_t s_rx;

/* Catalog (function pick + T9 brand) for s_slot. */
static oc_fn_t s_cat_fn;
static bool    s_cat_t9;
static char    s_cat_buf[OC_BRAND_LEN];
static int     s_t9_g;
static int     s_t9_li;

/* Hold: timer starts on settled `down` rising edge (diskglass pattern). */
static uint32_t s_yel_ms, s_grn_ms, s_gry_ms, s_blu_ms;
static bool     s_yel_was, s_grn_was, s_gry_was, s_blu_was;
static bool     s_yel_hold, s_grn_hold, s_gry_hold, s_blu_hold;

static ir_phy_t clamp_phy(uint8_t p) {
    if (p >= (uint8_t)IR_PHY_COUNT) return IR_PHY_NEC;
    return (ir_phy_t)p;
}

static void drain_rx(void) {
#ifndef HOST_TEST
    uint32_t dump;
    while (ir_comm_read(&dump)) { }
#endif
}

static void leds_idle(void) {
    if (!s_leds || s_power_armed) return;
    const uint8_t r = s_armed ? 40u : 2u;
    const uint8_t b = (s_mode == OC_MODE_LIB) ? 22u : 6u;
    for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++) {
        const bool lit = s_mode == OC_MODE_CAP
            ? (i == ((unsigned)s_slot % (unsigned)FWOG_LED_COUNT))
            : (i < (unsigned)s_lib + 1u);
        ws2812_set_color(i, r, lit ? 18u : 4u, b);
    }
    ws2812_process();
}

static void leds_flash(uint8_t r, uint8_t g, uint8_t b) {
    if (!s_leds || s_power_armed) return;
    for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++) {
        ws2812_set_color(i, r, g, b);
    }
    ws2812_process();
    s_flash_ms = to_ms_since_boot(get_absolute_time());
}

static void send_code(uint32_t code, ir_phy_t phy) {
#ifndef HOST_TEST
    if (s_ir) (void)ir_comm_set_phy(phy);
    ir_comm_send(code);
    drain_rx();
#endif
    leds_flash(40, 28, 4);
    DIAG("tx %s 0x%08x\n", ir_phy_name(phy), (unsigned)code);
}

static void pack_slots(oc_slots_msg_t *m) {
    memset(m, 0, sizeof *m);
    m->type = OC_MSG_SLOTS;
    m->magic = OC_MAGIC;
    for (unsigned i = 0; i < OC_SLOTS; i++) {
        m->slot[i].code = s_code[i];
        m->slot[i].have = s_have[i] ? 1u : 0u;
        m->slot[i].phy = s_phy_slot[i];
        if (s_have[i]) {
            strncpy(m->slot[i].brand, s_brand[i], OC_BRAND_LEN - 1u);
            strncpy(m->slot[i].func, s_func[i], OC_FUNC_LEN - 1u);
        }
    }
}

static void apply_slots(const oc_slots_msg_t *m) {
    for (unsigned i = 0; i < OC_SLOTS; i++) {
        s_code[i] = m->slot[i].code;
        s_have[i] = m->slot[i].have != 0u;
        s_phy_slot[i] = (uint8_t)clamp_phy(m->slot[i].phy);
        memcpy(s_brand[i], m->slot[i].brand, OC_BRAND_LEN);
        memcpy(s_func[i], m->slot[i].func, OC_FUNC_LEN);
        s_brand[i][OC_BRAND_LEN - 1u] = '\0';
        s_func[i][OC_FUNC_LEN - 1u] = '\0';
    }
    s_slots_ok = true;
    s_chrome_dirty = true;
}

static void link_save(void) {
    oc_slots_msg_t m;
    if (!s_link) return;
    pack_slots(&m);
    (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void send_get(void) {
    oc_cmd_t m = { .type = OC_MSG_CMD, .cmd = (uint8_t)OC_CMD_GET };
    if (s_link) (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void blast_fn(oc_fn_t fn) {
    DIAG("# blast fn %s\n", oc_fn_name(fn));
#ifndef HOST_TEST
    if (s_ir) (void)ir_comm_set_phy(IR_PHY_NEC);
    for (unsigned i = 0; i < k_oc_nbrands; i++) {
        const uint32_t c = k_oc_brands[i].code[fn];
        if (c == 0u) continue;
        ir_comm_send(c);
        drain_rx();
        leds_flash(50, 8, 0);
        sleep_ms(OC_BLAST_MS);
    }
#else
    (void)fn;
#endif
    leds_idle();
}

static void cat_begin(int slot) {
    s_scr = OC_SCR_CAT;
    s_slot = slot;
    s_cat_fn = OC_FN_POWER;
    s_cat_t9 = false;
    s_cat_buf[0] = '\0';
    if (s_brand[slot][0]) {
        strncpy(s_cat_buf, s_brand[slot], OC_BRAND_LEN - 1u);
    }
    s_t9_g = 0;
    s_t9_li = 0;
    s_yel_hold = s_grn_hold = s_gry_hold = s_blu_hold = false;
    s_chrome_dirty = true;
}

static void phy_begin(void) {
    s_scr = OC_SCR_PHY;
#ifndef HOST_TEST
    s_phy_pick = (int)(s_ir ? ir_comm_phy() : IR_PHY_NEC);
#else
    s_phy_pick = (int)IR_PHY_NEC;
#endif
    s_armed = false;
    s_blu_hold = true;
    s_chrome_dirty = true;
}

static void phy_apply(void) {
    const ir_phy_t phy = clamp_phy((uint8_t)s_phy_pick);
#ifndef HOST_TEST
    if (s_ir) (void)ir_comm_set_phy(phy);
#else
    (void)phy;
#endif
    s_scr = OC_SCR_MAIN;
    s_chrome_dirty = true;
}

static void phy_cancel(void) {
    s_scr = OC_SCR_MAIN;
    s_chrome_dirty = true;
}

static void cat_finish(void) {
    strncpy(s_brand[s_slot], s_cat_buf, OC_BRAND_LEN - 1u);
    s_brand[s_slot][OC_BRAND_LEN - 1u] = '\0';
    strncpy(s_func[s_slot], oc_fn_name(s_cat_fn), OC_FUNC_LEN - 1u);
    s_func[s_slot][OC_FUNC_LEN - 1u] = '\0';
    s_scr = OC_SCR_MAIN;
    s_chrome_dirty = true;
    link_save();
}

static void cat_cancel(void) {
    s_scr = OC_SCR_MAIN;
    s_chrome_dirty = true;
}

static char t9_cur(void) {
    const char *g = k_t9[s_t9_g];
    if (!g[0]) return '?';
    if (s_t9_li < 0) s_t9_li = 0;
    while (g[s_t9_li]) return g[s_t9_li];
    return g[0];
}

static void t9_insert(char c) {
    const size_t n = strlen(s_cat_buf);
    if (n + 1u >= OC_BRAND_LEN) return;
    s_cat_buf[n] = c;
    s_cat_buf[n + 1u] = '\0';
    s_chrome_dirty = true;
}

static void t9_backspace(void) {
    const size_t n = strlen(s_cat_buf);
    if (n == 0u) return;
    s_cat_buf[n - 1u] = '\0';
    s_chrome_dirty = true;
}

static void draw_main_cap(void) {
    const uint16_t bg = st7789_rgb565(8, 10, 18);
    const uint16_t fg = st7789_rgb565(230, 230, 230);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    const uint16_t warn = st7789_rgb565(230, 180, 70);
    char line[44];

    snprintf(line, sizeof line, "slot %d / %u  %s",
             s_slot + 1, (unsigned)OC_SLOTS, s_slots_ok ? "saved" : "load...");
    lcd_text_draw_padded(8, 72, line, 40, 1, acc, bg);

    if (s_armed) {
        lcd_text_draw_padded(8, 96, "waiting for a remote...", 40, 1, warn, bg);
    } else if (s_have[s_slot]) {
        snprintf(line, sizeof line, "0x%08X  %s",
                 (unsigned)s_code[s_slot], s_func[s_slot][0] ? s_func[s_slot] : "?");
        lcd_text_draw_padded(8, 96, line, 40, 1, fg, bg);
        snprintf(line, sizeof line, "%s  %s",
                 s_brand[s_slot][0] ? s_brand[s_slot] : "(no label)",
                 ir_phy_name(clamp_phy(s_phy_slot[s_slot])));
        lcd_text_draw_padded(8, 112, line, 40, 1, dim, bg);
    } else {
        lcd_text_draw_padded(8, 96, "empty", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 112, " ", 40, 1, dim, bg);
    }
    lcd_text_draw_padded(8, 136, "GRN tap cap  hold label", 40, 1, dim, bg);
    lcd_text_draw_padded(8, 152, "YEL tap replay (loads PHY)", 40, 1, dim, bg);
    lcd_text_draw_padded(8, 168, "GRY hold save  BLU hold PHY", 40, 1, dim, bg);
    lcd_text_draw_padded(8, 184, "GRY/RED slot  BLU tap library", 40, 1, dim, bg);
}

static void draw_main_lib(void) {
    const uint16_t bg = st7789_rgb565(8, 10, 18);
    const uint16_t fg = st7789_rgb565(230, 230, 230);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    char line[44];
    const uint32_t c = k_oc_brands[s_lib].code[s_lib_fn];

    snprintf(line, sizeof line, "%s  %s", k_oc_brands[s_lib].brand, oc_fn_name((oc_fn_t)s_lib_fn));
    lcd_text_draw_padded(8, 72, line, 40, 1, acc, bg);
    if (c != 0u) {
        snprintf(line, sizeof line, "0x%08X", (unsigned)c);
        lcd_text_draw_padded(8, 96, line, 40, 1, fg, bg);
    } else {
        lcd_text_draw_padded(8, 96, "code unknown", 40, 1, dim, bg);
    }
    lcd_text_draw_padded(8, 120, "GRN send  YEL fn / hold blast", 40, 1, dim, bg);
    lcd_text_draw_padded(8, 136, "GRY/RED brand  BLU capture", 40, 1, dim, bg);
    lcd_text_draw_padded(8, 152, "BLU hold PHY (library is NEC)", 40, 1, dim, bg);
}

static void draw_cat(void) {
    const uint16_t bg = st7789_rgb565(8, 10, 18);
    const uint16_t fg = st7789_rgb565(230, 230, 230);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    char line[44];

    lcd_text_draw_padded(8, 48, "label capture", 40, 1, acc, bg);
    if (!s_cat_t9) {
        snprintf(line, sizeof line, "fn: %s", oc_fn_name(s_cat_fn));
        lcd_text_draw_padded(8, 72, line, 40, 2, fg, bg);
        lcd_text_draw_padded(8, 104, "GRY/RED fn  GRN tap -> brand", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 120, "BLU cancel", 40, 1, dim, bg);
    } else {
        snprintf(line, sizeof line, "[%s]", s_cat_buf);
        lcd_text_draw_padded(8, 72, line, 40, 1, fg, bg);
        snprintf(line, sizeof line, "grp %s  cur %c", k_t9[s_t9_g], t9_cur());
        lcd_text_draw_padded(8, 96, line, 40, 1, acc, bg);
        lcd_text_draw_padded(8, 120, "GRY/RED grp YEL/BLU letter", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 136, "GRN tap add hold done", 40, 1, dim, bg);
        lcd_text_draw_padded(8, 152, "YEL hold backspace", 40, 1, dim, bg);
    }
}

static void draw_phy(void) {
    const uint16_t bg = st7789_rgb565(8, 10, 18);
    const uint16_t fg = st7789_rgb565(230, 230, 230);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t dim = st7789_rgb565(140, 150, 170);
    char line[44];
    int i;

    lcd_text_draw_padded(8, 48, "load PHY", 40, 1, acc, bg);
    for (i = 0; i < (int)IR_PHY_COUNT; i++) {
        snprintf(line, sizeof line, "%c %s  %.0f Hz",
                 i == s_phy_pick ? '>' : ' ',
                 ir_phy_name((ir_phy_t)i),
                 (double)ir_phy_carrier_hz((ir_phy_t)i));
        lcd_text_draw_padded(8, (uint16_t)(72 + i * 16), line, 40, 1,
                             i == s_phy_pick ? fg : dim, bg);
    }
    lcd_text_draw_padded(8, 168, "GRY/RED pick  GRN load", 40, 1, dim, bg);
    lcd_text_draw_padded(8, 184, "BLU tap cancel", 40, 1, dim, bg);
}

static void draw(void) {
    if (!s_lcd || !s_chrome_dirty) return;
    const uint16_t bg = st7789_rgb565(8, 10, 18);
    const uint16_t fg = st7789_rgb565(230, 230, 230);
    const uint16_t acc = st7789_rgb565(80, 200, 120);
    const uint16_t warn = st7789_rgb565(230, 180, 70);
    const uint16_t hdr = st7789_rgb565(16, 28, 40);
    char line[44];

    if (!s_painted) {
        st7789_clear(bg);
        s_painted = true;
    }

    st7789_fill_rect(0, 0, ST7789_W, 36, hdr);
    lcd_text_draw_padded(8, 10, "OpticClick", 16, 2, acc, hdr);
    st7789_fill_rect(0, 36, ST7789_W, (uint16_t)(ST7789_H - 36), bg);

    if (s_scr == OC_SCR_CAT) {
        draw_cat();
    } else if (s_scr == OC_SCR_PHY) {
        draw_phy();
    } else {
        snprintf(line, sizeof line, "%s  %s  IR %s",
                 s_mode == OC_MODE_CAP ? "CAPTURE" : "LIBRARY",
#ifndef HOST_TEST
                 s_ir ? ir_phy_name(ir_comm_phy()) : "—",
                 s_ir ? "ok" : "FAIL");
#else
                 "—", "off");
#endif
        lcd_text_draw_padded(8, 48, line, 40, 1, fg, bg);
    }

    if (s_scr == OC_SCR_MAIN && s_mode == OC_MODE_CAP) {
        draw_main_cap();
    } else if (s_scr == OC_SCR_MAIN && s_mode == OC_MODE_LIB) {
        draw_main_lib();
    }

    lcd_text_draw_padded(8, 220, "Your gear. TSOP is 38 kHz.", 40, 1, warn, bg);
    s_chrome_dirty = false;
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
        s_painted = false;
        s_chrome_dirty = true;
    } else {
        DIAG("[opticclick] LCD init failed\n");
    }
}

static void poll_link(void) {
    uint8_t b;
    size_t  n;
    while (fwog_link_uart_read(&b)) {
        if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
        if (n >= sizeof(oc_slots_msg_t) && s_rx.buf[0] == OC_MSG_SLOTS) {
            oc_slots_msg_t in;
            memcpy(&in, s_rx.buf, sizeof in);
            if (in.magic == OC_MAGIC) apply_slots(&in);
        }
    }
}

static void handle_holds_taps(const fwog_power_t *p, uint32_t now) {
    const bool ydown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) != 0;
    const bool gdown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_GREEN)) != 0;
    const bool grydown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_GRAY)) != 0;
    const bool bdown = (p->buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) != 0;

    if (ydown && !s_yel_was) {
        s_yel_ms = now;
        s_yel_hold = false;
    }
    if (ydown && !s_yel_hold && (now - s_yel_ms) >= OC_HOLD_MS) {
        s_yel_hold = true;
        if (s_scr == OC_SCR_CAT && s_cat_t9) {
            t9_backspace();
        } else if (s_scr == OC_SCR_MAIN && s_mode == OC_MODE_LIB && s_ir) {
            blast_fn((oc_fn_t)s_lib_fn);
            s_chrome_dirty = true;
        }
    }
    if (!ydown && s_yel_was && !s_yel_hold) {
        if (s_scr == OC_SCR_MAIN && s_mode == OC_MODE_CAP) {
            if (s_have[s_slot] && s_ir)
                send_code(s_code[s_slot], clamp_phy(s_phy_slot[s_slot]));
        } else if (s_scr == OC_SCR_MAIN && s_mode == OC_MODE_LIB) {
            s_lib_fn++;
            if (s_lib_fn >= (int)OC_FN_COUNT) s_lib_fn = 0;
            s_chrome_dirty = true;
            if (!p->armed) leds_idle();
        } else if (s_scr == OC_SCR_CAT && s_cat_t9) {
            if (s_t9_li > 0) s_t9_li--;
            else {
                const char *g = k_t9[s_t9_g];
                while (g[s_t9_li + 1]) s_t9_li++;
            }
            s_chrome_dirty = true;
        }
    }
    s_yel_was = ydown;

    if (gdown && !s_grn_was) {
        s_grn_ms = now;
        s_grn_hold = false;
    }
    if (gdown && !s_grn_hold && (now - s_grn_ms) >= OC_HOLD_MS) {
        s_grn_hold = true;
        if (s_scr == OC_SCR_CAT && s_cat_t9) {
            cat_finish();
        } else if (s_scr == OC_SCR_MAIN && s_mode == OC_MODE_CAP &&
                   s_have[s_slot]) {
            cat_begin(s_slot);
        }
    }
    if (!gdown && s_grn_was && !s_grn_hold) {
        if (s_scr == OC_SCR_CAT) {
            if (!s_cat_t9) {
                s_cat_t9 = true;
                s_t9_g = 0;
                s_t9_li = 0;
                s_chrome_dirty = true;
            } else {
                t9_insert(t9_cur());
            }
        } else if (s_scr == OC_SCR_PHY) {
            phy_apply();
        } else if (s_scr == OC_SCR_MAIN && s_mode == OC_MODE_CAP) {
            if (s_armed) {
                s_armed = false;
                DIAG("# capture cancel\n");
            } else if (s_ir) {
                drain_rx();
                s_armed = true;
                DIAG("# capture armed slot %d\n", s_slot);
            }
            s_chrome_dirty = true;
            if (!p->armed) leds_idle();
        } else if (s_scr == OC_SCR_MAIN && s_mode == OC_MODE_LIB && s_ir) {
            const uint32_t c = k_oc_brands[s_lib].code[s_lib_fn];
            if (c != 0u) send_code(c, IR_PHY_NEC);
        }
    }
    s_grn_was = gdown;

    if (grydown && !s_gry_was) {
        s_gry_ms = now;
        s_gry_hold = false;
    }
    if (grydown && !s_gry_hold && (now - s_gry_ms) >= OC_HOLD_MS) {
        s_gry_hold = true;
        if (s_scr == OC_SCR_MAIN && s_mode == OC_MODE_CAP) {
            link_save();
            leds_flash(4, 40, 8);
        }
    }
    if (!grydown && s_gry_was && !s_gry_hold) {
        if (s_scr == OC_SCR_MAIN && s_mode == OC_MODE_CAP) {
            if (--s_slot < 0) s_slot = (int)OC_SLOTS - 1;
            s_armed = false;
            s_chrome_dirty = true;
            if (!p->armed) leds_idle();
        }
    }
    s_gry_was = grydown;

    if (bdown && !s_blu_was) {
        s_blu_ms = now;
        s_blu_hold = false;
    }
    if (bdown && !s_blu_hold && (now - s_blu_ms) >= OC_HOLD_MS) {
        s_blu_hold = true;
        if (s_scr == OC_SCR_MAIN) phy_begin();
    }
    if (!bdown && s_blu_was && !s_blu_hold) {
        if (s_scr == OC_SCR_PHY) {
            phy_cancel();
        } else if (s_scr == OC_SCR_MAIN) {
            s_mode = (s_mode == OC_MODE_CAP) ? OC_MODE_LIB : OC_MODE_CAP;
            s_armed = false;
            s_chrome_dirty = true;
            if (!p->armed) leds_idle();
        }
    }
    s_blu_was = bdown;
}

int main(void) {
    board_init();
    fwog_splash_bind("OpticClick", "002");
    s_leds = ws2812_init(pio0, 0u);
#ifndef HOST_TEST
    s_ir = ir_comm_init(pio1, 0u);
#endif
    s_link = fwog_link_uart_init(FWOG_LINK_BAUD);
    lcd_bringup();
    leds_idle();
    send_get();
    DIAG("[opticclick] ir=%s lcd=%s link=%s\n",
         s_ir ? "ok" : "FAIL", s_lcd ? "ok" : "FAIL", s_link ? "ok" : "FAIL");

    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        const fwog_power_t p = fwog_power_poll(now);
        s_power_armed = p.armed;

        poll_link();
        if (!s_slots_ok && s_link && (now - s_get_ms) >= 2000u) {
            s_get_ms = now;
            send_get();
        }

#ifndef HOST_TEST
        if (s_ir && s_armed && s_scr == OC_SCR_MAIN) {
            uint32_t code;
            while (ir_comm_read(&code)) {
                const ir_phy_t phy = s_ir ? ir_comm_phy() : IR_PHY_NEC;
                if (phy == IR_PHY_NEC && !ir_nec_command_valid(code)) {
                    DIAG("rx drop 0x%08x (bad complement)\n", (unsigned)code);
                    continue;
                }
                s_code[s_slot] = code;
                s_have[s_slot] = true;
                s_phy_slot[s_slot] = (uint8_t)phy;
                s_armed = false;
                s_chrome_dirty = true;
                leds_flash(4, 40, 8);
                link_save();
                cat_begin(s_slot);
                DIAG("rx slot %d %s 0x%08x\n",
                     s_slot, ir_phy_name(phy), (unsigned)code);
            }
        }
#endif

        if (s_scr == OC_SCR_MAIN) {
            /* CAPTURE Gray tap is on release so hold can mean save. */
            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GRAY) &&
                s_mode == OC_MODE_LIB) {
                if (--s_lib < 0) s_lib = (int)k_oc_nbrands - 1;
                s_armed = false;
                s_chrome_dirty = true;
                if (!p.armed) leds_idle();
            }
            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_RED)) {
                if (s_mode == OC_MODE_CAP) {
                    if (++s_slot >= (int)OC_SLOTS) s_slot = 0;
                } else {
                    if (++s_lib >= (int)k_oc_nbrands) s_lib = 0;
                }
                s_armed = false;
                s_chrome_dirty = true;
                if (!p.armed) leds_idle();
            }
        } else if (s_scr == OC_SCR_PHY) {
            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GRAY)) {
                if (--s_phy_pick < 0) s_phy_pick = (int)IR_PHY_COUNT - 1;
                s_chrome_dirty = true;
            }
            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_RED)) {
                s_phy_pick = (s_phy_pick + 1) % (int)IR_PHY_COUNT;
                s_chrome_dirty = true;
            }
        } else {
            /* Catalog screen. */
            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_BLUE) && !s_cat_t9) {
                cat_cancel();
            }
            if (!s_cat_t9) {
                if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GRAY)) {
                    if (s_cat_fn == 0) s_cat_fn = (oc_fn_t)(OC_FN_COUNT - 1);
                    else s_cat_fn = (oc_fn_t)((int)s_cat_fn - 1);
                    s_chrome_dirty = true;
                }
                if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_RED)) {
                    s_cat_fn = (oc_fn_t)(((int)s_cat_fn + 1) % (int)OC_FN_COUNT);
                    s_chrome_dirty = true;
                }
            } else {
                if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GRAY)) {
                    s_t9_g--;
                    if (s_t9_g < 0) s_t9_g = OC_T9_GROUPS - 1;
                    s_t9_li = 0;
                    s_chrome_dirty = true;
                }
                if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_RED)) {
                    s_t9_g = (s_t9_g + 1) % OC_T9_GROUPS;
                    s_t9_li = 0;
                    s_chrome_dirty = true;
                }
                if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_BLUE)) {
                    const char *g = k_t9[s_t9_g];
                    if (g[s_t9_li + 1]) s_t9_li++;
                    else s_t9_li = 0;
                    s_chrome_dirty = true;
                }
            }
        }

        handle_holds_taps(&p, now);

        if (s_flash_ms != 0u && (now - s_flash_ms) > 120u) {
            s_flash_ms = 0;
            if (!p.armed) leds_idle();
        }

        draw();
        sleep_ms(2);
    }
}
