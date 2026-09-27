#include "rp_cmdline.h"
#include "rp_terminal.h"
#include "watchdog/watchdog.h"
#include "pico/stdlib.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define RP_HIST 8u

static char s_hist[RP_HIST][RP_CMD_MAX];
static unsigned s_hist_n;
static unsigned s_hist_pos;

static void redraw(const char *mode, const char *line) {
    rp_terminal_printf("\r%s> %s \033[K", mode, line);
    size_t back = strlen(line);
    for (size_t i = 0; i < back; i++) rp_terminal_print("\033[D");
}

static void history_add(const char *line) {
    if (!line[0]) return;
    if (s_hist_n > 0 && strcmp(s_hist[s_hist_n - 1u], line) == 0) return;
    if (s_hist_n < RP_HIST) {
        strncpy(s_hist[s_hist_n++], line, RP_CMD_MAX - 1u);
        s_hist[s_hist_n - 1u][RP_CMD_MAX - 1u] = '\0';
    } else {
        memmove(s_hist[0], s_hist[1], (RP_HIST - 1u) * RP_CMD_MAX);
        strncpy(s_hist[RP_HIST - 1u], line, RP_CMD_MAX - 1u);
        s_hist[RP_HIST - 1u][RP_CMD_MAX - 1u] = '\0';
    }
    s_hist_pos = s_hist_n;
}

static void history_recall(const char *mode, char *line, size_t *cursor, int dir) {
    if (s_hist_n == 0) return;
    if (dir < 0) {
        if (s_hist_pos == 0) return;
        s_hist_pos--;
    } else {
        if (s_hist_pos + 1u >= s_hist_n) {
            line[0] = '\0';
            *cursor = 0;
            redraw(mode, line);
            s_hist_pos = s_hist_n;
            return;
        }
        s_hist_pos++;
    }
    strncpy(line, s_hist[s_hist_pos], RP_CMD_MAX - 1u);
    line[RP_CMD_MAX - 1u] = '\0';
    *cursor = strlen(line);
    redraw(mode, line);
}

void rp_cmdline_read(const char *mode, char *out, size_t out_cap) {
    char line[RP_CMD_MAX];
    size_t len = 0;
    line[0] = '\0';

    while (true) {
        int c = getchar_timeout_us(100000);
        if (c == PICO_ERROR_TIMEOUT) {
            /* Sitting at the prompt used to miss kicks and reset every 8.3 s,
             * which also killed 1200-baud BOOTSEL from App Explorer. */
            board_watchdog_kick();
            continue;
        }
        if (c == '\r') continue;
        if (c == '\n') {
            rp_terminal_println("");
            line[len] = '\0';
            history_add(line);
            strncpy(out, line, out_cap - 1u);
            out[out_cap - 1u] = '\0';
            return;
        }
        if (c == 127 || c == '\b') {
            if (len == 0) continue;
            len--;
            line[len] = '\0';
            redraw(mode, line);
            continue;
        }
        if (c == '\t') {
            /* no autocomplete dictionary yet */
            continue;
        }
        if (c == 0x1b) {
            int c2 = getchar_timeout_us(50000);
            if (c2 == PICO_ERROR_TIMEOUT) continue;
            if (c2 == '[') {
                int c3 = getchar_timeout_us(50000);
                if (c3 == 'A') history_recall(mode, line, &len, -1);
                else if (c3 == 'B') history_recall(mode, line, &len, +1);
            }
            continue;
        }
        if (!isprint(c)) continue;
        if (len + 1u >= RP_CMD_MAX || len + 1u >= out_cap) continue;
        line[len++] = (char)c;
        line[len] = '\0';
        redraw(mode, line);
    }
}

static char s_poll_line[RP_CMD_MAX];
static size_t s_poll_len;
static bool s_poll_prompted;

void rp_cmdline_reset(void) {
    s_poll_line[0] = '\0';
    s_poll_len = 0;
    s_poll_prompted = false;
}

bool rp_cmdline_poll(const char *mode, char *out, size_t out_cap) {
    int c;
    if (!out || out_cap < 2u) return false;
    if (!s_poll_prompted) {
        rp_terminal_prompt(mode);
        s_poll_prompted = true;
        s_poll_len = 0;
        s_poll_line[0] = '\0';
    }
    c = getchar_timeout_us(0);
    if (c == PICO_ERROR_TIMEOUT) return false;
    if (c == '\r') return false;
    if (c == '\n') {
        rp_terminal_println("");
        s_poll_line[s_poll_len] = '\0';
        history_add(s_poll_line);
        strncpy(out, s_poll_line, out_cap - 1u);
        out[out_cap - 1u] = '\0';
        s_poll_prompted = false;
        s_poll_len = 0;
        s_poll_line[0] = '\0';
        return true;
    }
    if (c == 127 || c == '\b') {
        if (s_poll_len == 0) return false;
        s_poll_len--;
        s_poll_line[s_poll_len] = '\0';
        redraw(mode, s_poll_line);
        return false;
    }
    if (c == '\t') return false;
    if (c == 0x1b) {
        int c2 = getchar_timeout_us(50000);
        if (c2 == PICO_ERROR_TIMEOUT) return false;
        if (c2 == '[') {
            int c3 = getchar_timeout_us(50000);
            if (c3 == 'A') history_recall(mode, s_poll_line, &s_poll_len, -1);
            else if (c3 == 'B') history_recall(mode, s_poll_line, &s_poll_len, +1);
        }
        return false;
    }
    if (!isprint(c)) return false;
    if (s_poll_len + 1u >= RP_CMD_MAX || s_poll_len + 1u >= out_cap) return false;
    s_poll_line[s_poll_len++] = (char)c;
    s_poll_line[s_poll_len] = '\0';
    redraw(mode, s_poll_line);
    return false;
}
