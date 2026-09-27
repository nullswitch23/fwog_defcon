/* BattleBridge main: low-rate control/status bridge to the C6 game host. */
#include "fwog_main.h"
#include "bb_proto.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FWOG_WATCHDOG_DEFAULT();
#define BN_LINE_MAX 160u

static fwog_link_rx_t s_rx;
static bb_status_t s_st;
static fwog_bn_flash_t s_fl;
static char s_line[BN_LINE_MAX];
static unsigned s_llen;
static uint32_t s_hello_ms, s_stat_ms;
static bool s_want_game;
static uint8_t s_want_bots;
static bool s_want_teams;

static void send_bots_teams(void) {
    char line[32];
    snprintf(line, sizeof line, "BN GAME BOTS=%u\n", (unsigned)s_want_bots);
    fwog_bn_write(&s_fl, line);
    fwog_bn_write(&s_fl, s_want_teams ? "BN GAME TEAMS=1\n" : "BN GAME TEAMS=0\n");
}

static void copy_kv(const char *line, const char *key, char *dst, size_t n) {
    const char *p = strstr(line, key);
    if (!p || !n) return;
    p += strlen(key);
    size_t i = 0;
    while (*p && *p != ' ' && *p != '\r' && *p != '\n' && i + 1u < n) {
        dst[i++] = *p++;
    }
    dst[i] = '\0';
}

static unsigned number(const char *line, const char *key) {
    char tmp[16] = {0};
    copy_kv(line, key, tmp, sizeof tmp);
    return (unsigned)strtoul(tmp, NULL, 0);
}

static bool text_pass_ok(const char *pass) {
    static const char abc[] =
        "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    if (!pass || strlen(pass) != 8u) return false;
    for (int i = 0; i < 8; i++) {
        if (!strchr(abc, pass[i])) return false;
    }
    return true;
}

static void push_st(void) {
    s_st.type = BB_MSG_ST;
    s_st.flash = s_fl.flash;
    if (s_fl.flash) s_st.pct = s_fl.pct;
    if (s_fl.flash == FWOG_BN_FLASH_FAIL && s_fl.why[0]) {
        memset(s_st.why, 0, sizeof s_st.why);
        strncpy(s_st.why, s_fl.why, sizeof s_st.why - 1u);
    }
    (void)fwog_link_uart_send_frame(&s_st, sizeof s_st);
}

static void flash_tick(const fwog_bn_flash_t *st, void *ctx) {
    (void)st;
    (void)ctx;
    push_st();
}

static void handle_line(char *line) {
    if (strncmp(line, "BN ", 3) != 0) return;
    DIAG("[battlebridge] %s\n", line);
    if (strncmp(line, "BN HELLO", 8) == 0) {
        s_st.hello = 1;
        if (s_fl.flash == FWOG_BN_FLASH_OK) s_fl.flash = FWOG_BN_FLASH_IDLE;
        push_st();
        return;
    }
    if (strncmp(line, "BN GAME off", 11) == 0) {
        s_st.game_on = 0;
        s_st.players = 0;
        push_st();
        return;
    }
    if (strncmp(line, "BN GAME fail=", 13) == 0) {
        s_st.game_on = 0;
        copy_kv(line, "fail=", s_st.why, sizeof s_st.why);
        push_st();
        return;
    }
    if (strncmp(line, "BN GAME on ", 11) == 0) {
        /* Parse the complete newline-terminated UART line into a temporary
         * record. Publish one link frame only after the text password passes
         * validation, so the display/QR never sees a partial rewrite. */
        bb_status_t next = s_st;
        char ssid[sizeof next.ssid] = {0};
        char pass[sizeof next.pass] = {0};
        copy_kv(line, "ssid=", ssid, sizeof ssid);
        copy_kv(line, "pass=", pass, sizeof pass);
        if (strcmp(ssid, "FWOG-arena") != 0 || !text_pass_ok(pass)) {
            memset(s_st.why, 0, sizeof s_st.why);
            strncpy(s_st.why, "bad AP text", sizeof s_st.why - 1u);
            push_st();
            return;
        }
        next.game_on = 1;
        memcpy(next.ssid, ssid, sizeof next.ssid);
        memcpy(next.pass, pass, sizeof next.pass);
        next.players = (uint8_t)number(line, "players=");
        next.phase = (uint8_t)number(line, "phase=");
        next.tick = (uint16_t)number(line, "tick=");
        unsigned step = number(line, "step=") / 100u;
        if (step > 255u) step = 255u;
        next.step_100us = (uint8_t)step;
        unsigned heap = number(line, "heap=") / 1024u;
        if (heap > 65535u) heap = 65535u;
        next.heap_k = (uint16_t)heap;
        if (strstr(line, "bots=")) next.bots = (uint8_t)number(line, "bots=");
        if (strstr(line, "clock=")) next.clock_s = (uint8_t)number(line, "clock=");
        if (strstr(line, "sudden=")) next.sudden = (uint8_t)number(line, "sudden=");
        if (strstr(line, "teams=")) next.teams = (uint8_t)number(line, "teams=");
        memset(next.why, 0, sizeof next.why);
        s_st = next;
        push_st();
        return;
    }
    if (strncmp(line, "BN GAME sta ", 12) == 0) {
        s_st.game_on = 1;
        s_st.players = (uint8_t)number(line, "players=");
        s_st.phase = (uint8_t)number(line, "phase=");
        s_st.tick = (uint16_t)number(line, "tick=");
        unsigned step = number(line, "step=") / 100u;
        if (step > 255u) step = 255u;
        s_st.step_100us = (uint8_t)step;
        unsigned heap = number(line, "heap=") / 1024u;
        if (heap > 65535u) heap = 65535u;
        s_st.heap_k = (uint16_t)heap;
        if (strstr(line, "bots=")) s_st.bots = (uint8_t)number(line, "bots=");
        if (strstr(line, "clock=")) s_st.clock_s = (uint8_t)number(line, "clock=");
        if (strstr(line, "sudden=")) s_st.sudden = (uint8_t)number(line, "sudden=");
        if (strstr(line, "teams=")) s_st.teams = (uint8_t)number(line, "teams=");
        push_st();
        return;
    }
    if (strncmp(line, "BN GAME wipe", 12) == 0) {
        char pass[sizeof s_st.pass] = {0};
        copy_kv(line, "pass=", pass, sizeof pass);
        if (text_pass_ok(pass)) {
            memset(s_st.pass, 0, sizeof s_st.pass);
            memcpy(s_st.pass, pass, 8u);
            push_st();
        }
        return;
    }
}

static void bn_poll(void) {
    if (s_fl.rom_pins) return;
    for (;;) {
        int got = fwog_bn_getc();
        if (got < 0) break;
        char c = (char)got;
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
    DIAG("[battlebridge] display: %s\n", fwog_display_result_text(d));
    for (unsigned i = 0; i < 20u; i++) {
        board_watchdog_kick();
        sleep_ms(200);
    }
    DIAG("[battlebridge] io_dir=%d c6_image=%u\n",
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
            if (n < sizeof(bb_cmd_t) || s_rx.buf[0] != BB_MSG_CMD) continue;
            bb_cmd_t c;
            memcpy(&c, s_rx.buf, sizeof c);
            if (c.cmd == BB_CMD_GAME) {
                s_want_game = c.on != 0u;
                fwog_bn_write(&s_fl, s_want_game ?
                              "BN GAME START\n" : "BN GAME STOP\n");
                if (s_want_game) send_bots_teams();
            } else if (c.cmd == BB_CMD_GO) {
                fwog_bn_write(&s_fl, "BN GAME GO\n");
            } else if (c.cmd == BB_CMD_BOTS) {
                s_want_bots = c.on > 3u ? 3u : c.on;
                send_bots_teams();
            } else if (c.cmd == BB_CMD_TEAMS) {
                s_want_teams = c.on != 0u;
                send_bots_teams();
            } else if (c.cmd == BB_CMD_WIPE) {
                if (!s_fl.rom_pins) fwog_bn_write(&s_fl, "BN GAME WIPE\n");
            } else if (c.cmd == BB_CMD_FLASH) {
                if (c.on == 1u) {
                    if (s_want_game) {
                        fwog_bn_write(&s_fl, "BN GAME STOP\n");
                        s_want_game = false;
                    }
                    s_st.hello = 0;
                    s_st.game_on = 0;
                    fwog_bn_flash_arm(&s_fl, flash_tick, NULL);
                } else if (c.on == 2u) {
                    fwog_bn_flash_go(&s_fl, flash_tick, NULL);
                } else {
                    fwog_bn_flash_cancel(&s_fl, flash_tick, NULL);
                }
            }
        }
        if (!s_fl.rom_pins && now - s_hello_ms > 1000u) {
            s_hello_ms = now;
            fwog_bn_write(&s_fl, "BN HELLO og\n");
        }
        if (!s_fl.rom_pins && s_want_game && now - s_stat_ms > 500u) {
            s_stat_ms = now;
            fwog_bn_write(&s_fl, "BN GAME STAT\n");
        }
        sleep_ms(2);
    }
}
