/* BleDeck main — UART1 bridge to Bottlenose BLE HID + C6 flash. */
#include "fwog_main.h"
#include "bd_proto.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

FWOG_WATCHDOG_DEFAULT();

#define BN_LINE_MAX 160u

static fwog_link_rx_t  s_rx;
static bd_status_t     s_st;
static fwog_bn_flash_t s_fl;
static char            s_line[BN_LINE_MAX];
static unsigned        s_llen;
static uint32_t        s_hello_ms;
static uint32_t        s_push_ms;
static bool            s_dirty = true;

static void copy_kv(const char *line, const char *key, char *dst, size_t n) {
    const char *p = strstr(line, key);
    if (!p || n == 0) {
        if (n) dst[0] = '\0';
        return;
    }
    p += strlen(key);
    size_t i = 0;
    while (*p && *p != ' ' && *p != '\r' && *p != '\n' && i + 1u < n) {
        dst[i++] = *p++;
    }
    dst[i] = '\0';
}

static void push_st(void) {
    s_st.type = BD_MSG_ST;
    s_st.flash = s_fl.flash;
    s_st.pct = s_fl.pct;
    memset(s_st.why, 0, sizeof s_st.why);
    if (s_fl.flash == FWOG_BN_FLASH_FAIL) {
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

static void hid_line(const char *which, uint16_t usage, uint8_t down) {
    char buf[48];
    if (which[0] == 'K') {
        snprintf(buf, sizeof buf, "BN HID KEY code=%u down=%u\n",
                 (unsigned)usage, (unsigned)down);
    } else {
        snprintf(buf, sizeof buf, "BN HID CC usage=%u down=%u\n",
                 (unsigned)usage, (unsigned)down);
    }
    fwog_bn_write(&s_fl, buf);
}

static void handle_line(char *line) {
    if (strncmp(line, "BN ", 3) != 0) return;
    DIAG("[bledeck] %s\n", line);
    if (strncmp(line, "BN HELLO", 8) == 0) {
        s_st.hello = 1u;
        if (s_fl.flash == FWOG_BN_FLASH_OK) s_fl.flash = FWOG_BN_FLASH_IDLE;
        s_dirty = true;
        return;
    }
    if (strncmp(line, "BN HID off", 10) == 0) {
        s_st.adv = 0u;
        s_st.conn = 0u;
        s_dirty = true;
        return;
    }
    if (strncmp(line, "BN HID on", 9) == 0) {
        s_st.adv = 1u;
        s_dirty = true;
        return;
    }
    if (strncmp(line, "BN HID conn=", 12) == 0) {
        char tmp[4];
        copy_kv(line, "conn=", tmp, sizeof tmp);
        s_st.conn = tmp[0] == '1' ? 1u : 0u;
        s_st.adv = 1u;
        s_dirty = true;
    }
    if (strncmp(line, "BN HID lock=", 12) == 0) {
        char tmp[4];
        copy_kv(line, "lock=", tmp, sizeof tmp);
        s_st.lock = tmp[0] == '1' ? 1u : 0u;
        s_dirty = true;
    }
    if (strncmp(line, "BN HID forget", 13) == 0) {
        s_st.lock = 0u;
        s_dirty = true;
    }
    if (strncmp(line, "BN HID bat=", 11) == 0) {
        s_st.bat = (uint8_t)atoi(line + 11);
        s_dirty = true;
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
    DIAG("[bledeck] display: %s\n", fwog_display_result_text(d));

    for (int i = 0; i < 20; i++) {
        board_watchdog_kick();
        sleep_ms(200);
    }

    DIAG("[bledeck] io_dir=%d c6_image=%u\n",
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
            if (n >= sizeof(bd_cmd_t) && s_rx.buf[0] == BD_MSG_CMD) {
                bd_cmd_t c;
                memcpy(&c, s_rx.buf, sizeof c);
                if (c.cmd == BD_CMD_ADV) {
                    fwog_bn_write(&s_fl, c.on ? "BN HID START\n" : "BN HID STOP\n");
                } else if (c.cmd == BD_CMD_FLASH) {
                    if (c.on == 1u) {
                        fwog_bn_write(&s_fl, "BN HID STOP\n");
                        s_st.hello = 0;
                        s_st.adv = 0;
                        s_st.conn = 0;
                        s_st.lock = 0;
                        fwog_bn_flash_arm(&s_fl, flash_tick, NULL);
                    } else if (c.on == 2u) {
                        fwog_bn_flash_go(&s_fl, flash_tick, NULL);
                    } else {
                        fwog_bn_flash_cancel(&s_fl, flash_tick, NULL);
                    }
                } else if (c.cmd == BD_CMD_KEY) {
                    hid_line("KEY", c.usage, c.on);
                } else if (c.cmd == BD_CMD_CC) {
                    hid_line("CC", c.usage, c.on);
                } else if (c.cmd == BD_CMD_BAT) {
                    char buf[32];
                    snprintf(buf, sizeof buf, "BN HID BAT=%u\n", (unsigned)c.on);
                    fwog_bn_write(&s_fl, buf);
                } else if (c.cmd == BD_CMD_FORGET) {
                    fwog_bn_write(&s_fl, "BN HID FORGET\n");
                }
            }
        }

        if (!s_fl.rom_pins && (now - s_hello_ms) > 1000u) {
            s_hello_ms = now;
            fwog_bn_write(&s_fl, "BN HELLO og\n");
        }
        if (s_dirty || (now - s_push_ms) > 250u) {
            s_push_ms = now;
            push_st();
        }
        sleep_ms(2);
    }
}
