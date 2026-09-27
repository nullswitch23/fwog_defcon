/* LanFerry main — UART1 bridge to Bottlenose, plus C6 ROM flash. */
#include "fwog_main.h"
#include "lf_proto.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

FWOG_WATCHDOG_DEFAULT();

#define BN_LINE_MAX 160u

static fwog_link_rx_t   s_rx;
static lf_status_t      s_st;
static fwog_bn_flash_t  s_fl;
static char             s_line[BN_LINE_MAX];
static unsigned         s_llen;
static uint32_t         s_hello_ms;
static uint32_t         s_stat_ms;
static bool             s_want_ap;

static void copy_kv(const char *line, const char *key, char *dst, size_t n) {
    const char *p = strstr(line, key);
    if (!p || n == 0) return;
    p += strlen(key);
    size_t i = 0;
    while (*p && *p != ' ' && *p != '\r' && *p != '\n' && i + 1u < n) {
        dst[i++] = *p++;
    }
    dst[i] = '\0';
}

static void push_st(void) {
    s_st.type = LF_MSG_ST;
    s_st.flash = s_fl.flash;
    if (s_fl.flash) s_st.pct = s_fl.pct;
    if (s_fl.flash == FWOG_BN_FLASH_FAIL && s_fl.why[0]) {
        memset(s_st.file, 0, sizeof s_st.file);
        strncpy(s_st.file, s_fl.why, sizeof s_st.file - 1u);
    }
    (void)fwog_link_uart_send_frame(&s_st, sizeof s_st);
}

static void flash_tick(const fwog_bn_flash_t *st, void *ctx) {
    (void)st;
    (void)ctx;
    push_st();
}

static int pass_ok(const char *p) {
    static const char abc[] =
        "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    if (!p || strlen(p) != 8u) return 0;
    for (int i = 0; i < 8; i++) {
        if (!strchr(abc, p[i])) return 0;
    }
    return 1;
}

static void handle_line(char *line) {
    if (strncmp(line, "BN ", 3) != 0) return;
    DIAG("[lanferry] %s\n", line);
    if (strncmp(line, "BN HELLO", 8) == 0) {
        s_st.hello = 1u;
        if (s_st.flash == LF_FLASH_OK) {
            s_fl.flash = FWOG_BN_FLASH_IDLE;
        }
        push_st();
        return;
    }
    if (strncmp(line, "BN AP wipe", 10) == 0) {
        s_st.nfiles = 0u;
        s_st.bytes = 0u;
        s_st.pct = 0u;
        memset(s_st.file, 0, sizeof s_st.file);
        char pass[LF_PASS_N];
        memset(pass, 0, sizeof pass);
        copy_kv(line, "pass=", pass, sizeof pass);
        if (pass_ok(pass)) {
            memset(s_st.pass, 0, sizeof s_st.pass);
            memcpy(s_st.pass, pass, 8u);
        }
        push_st();
        return;
    }
    if (strncmp(line, "BN AP fail", 10) == 0) {
        s_st.ap_on = 0u;
        s_st.clients = 0u;
        push_st();
        return;
    }
    if (strncmp(line, "BN AP off", 9) == 0) {
        s_st.ap_on = 0u;
        s_st.clients = 0u;
        push_st();
        return;
    }
    if (strncmp(line, "BN AP on", 8) == 0) {
        s_st.ap_on = 1u;
        char pass[LF_PASS_N];
        memset(pass, 0, sizeof pass);
        copy_kv(line, "ssid=", s_st.ssid, sizeof s_st.ssid);
        copy_kv(line, "pass=", pass, sizeof pass);
        if (pass_ok(pass)) {
            memset(s_st.pass, 0, sizeof s_st.pass);
            memcpy(s_st.pass, pass, 8u);
        }
        if (!s_st.ssid[0]) strncpy(s_st.ssid, "FWOG-ferry", sizeof s_st.ssid - 1u);
        push_st();
        return;
    }
    if (strncmp(line, "BN AP ", 6) == 0) {
        s_st.ap_on = 1u;
        push_st();
        return;
    }
    if (strncmp(line, "BN PIPE ", 8) == 0) {
        char tmp[8];
        memset(tmp, 0, sizeof tmp);
        copy_kv(line, "pct=", tmp, sizeof tmp);
        if (tmp[0] && !s_fl.flash) {
            int ok = 1;
            for (const char *p = tmp; *p; p++) {
                if (*p < '0' || *p > '9') ok = 0;
            }
            if (ok) {
                unsigned p = (unsigned)atoi(tmp);
                if (p <= 100u) s_st.pct = (uint8_t)p;
            }
        }
        push_st();
        return;
    }
    if (strncmp(line, "BN STA ", 7) == 0) {
        char tmp[12];
        copy_kv(line, "clients=", tmp, sizeof tmp);
        s_st.clients = (uint8_t)atoi(tmp);
        copy_kv(line, "files=", tmp, sizeof tmp);
        if (tmp[0]) s_st.nfiles = (uint8_t)atoi(tmp);
        if (!s_st.pct) {
            copy_kv(line, "last=", s_st.file, sizeof s_st.file);
            if (!s_st.file[0]) copy_kv(line, "file=", s_st.file, sizeof s_st.file);
        }
        copy_kv(line, "used=", tmp, sizeof tmp);
        if (!tmp[0]) copy_kv(line, "bytes=", tmp, sizeof tmp);
        s_st.bytes = (uint32_t)atoi(tmp);
        push_st();
    }
}

static void bn_poll(void) {
    if (s_fl.rom_pins) return;
    for (;;) {
        const int got = fwog_bn_getc();
        if (got < 0) break;
        const char c = (char)got;
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

int main(void) {
    board_init();
    const fwog_display_result_t d = fwog_display_update_run();
    DIAG("[lanferry] display: %s\n", fwog_display_result_text(d));

    for (int i = 0; i < 20; i++) {
        board_watchdog_kick();
        sleep_ms(200);
    }

    DIAG("[lanferry] io_dir=%d c6_image=%u\n",
         (int)fwog_bn_link_app(), fwog_c6_image_size);

    memset(&s_st, 0, sizeof s_st);
    memset(&s_fl, 0, sizeof s_fl);
    fwog_link_rx_init(&s_rx);
    push_st();

    while (true) {
        board_watchdog_kick();
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        bn_poll();

        uint8_t b;
        size_t n;
        while (fwog_link_uart_read(&b)) {
            if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
            if (n >= sizeof(lf_cmd_t) && s_rx.buf[0] == LF_MSG_CMD) {
                lf_cmd_t c;
                memcpy(&c, s_rx.buf, sizeof c);
                if (c.cmd == LF_CMD_AP) {
                    s_want_ap = c.on != 0u;
                    fwog_bn_write(&s_fl, s_want_ap ? "BN AP START\n" : "BN AP STOP\n");
                } else if (c.cmd == LF_CMD_WIPE) {
                    fwog_bn_write(&s_fl, "BN AP WIPE\n");
                } else if (c.cmd == LF_CMD_FLASH) {
                    if (c.on == 1u) {
                        if (s_want_ap) {
                            fwog_bn_write(&s_fl, "BN AP STOP\n");
                            s_want_ap = false;
                            s_st.ap_on = 0;
                        }
                        s_st.hello = 0;
                        fwog_bn_flash_arm(&s_fl, flash_tick, NULL);
                    } else if (c.on == 2u) {
                        fwog_bn_flash_go(&s_fl, flash_tick, NULL);
                    } else {
                        fwog_bn_flash_cancel(&s_fl, flash_tick, NULL);
                    }
                }
            }
        }

        if (!s_fl.rom_pins && (now - s_hello_ms) > 1000u) {
            s_hello_ms = now;
            fwog_bn_write(&s_fl, "BN HELLO og\n");
        }
        if (!s_fl.rom_pins && s_want_ap && (now - s_stat_ms) > 500u) {
            s_stat_ms = now;
            fwog_bn_write(&s_fl, "BN AP STAT\n");
        }
        sleep_ms(2);
    }
}
