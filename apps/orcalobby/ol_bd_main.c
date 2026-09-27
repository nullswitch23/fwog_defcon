/* OrcaLobby BleDeck C6 tile. */
#include "ol_bd_main.h"
#include "fwog_main.h"
#include "bd_proto.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BN_LINE_MAX 160u

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

void ol_bd_main_enter(void) {
    memset(&s_st, 0, sizeof s_st);
    memset(&s_fl, 0, sizeof s_fl);
    s_llen = 0;
    s_dirty = true;
    push_st();
}

void ol_bd_main_leave(void) {
    fwog_bn_write(&s_fl, "BN HID STOP\n");
    fwog_bn_flash_cancel(&s_fl, flash_tick, NULL);
    s_st.adv = 0;
    s_st.conn = 0;
}

void ol_bd_main_frame(const uint8_t *buf, size_t n) {
    if (n >= sizeof(bd_cmd_t) && buf[0] == BD_MSG_CMD) {
        bd_cmd_t c;
        memcpy(&c, buf, sizeof c);
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
            char line[32];
            snprintf(line, sizeof line, "BN HID BAT=%u\n", (unsigned)c.on);
            fwog_bn_write(&s_fl, line);
        } else if (c.cmd == BD_CMD_FORGET) {
            fwog_bn_write(&s_fl, "BN HID FORGET\n");
        }
    }
}

void ol_bd_main_tick(uint32_t now) {
    bn_poll();
    if (!s_fl.rom_pins && (now - s_hello_ms) > 1000u) {
        s_hello_ms = now;
        fwog_bn_write(&s_fl, "BN HELLO og\n");
    }
    if (s_dirty || (now - s_push_ms) > 250u) {
        s_push_ms = now;
        push_st();
    }
}
