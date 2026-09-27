#include "fwog_main.h"
#include "fs/fwog_fs.h"
#include "it_proto.h"
#include "dg_file.h"
#include "dg_fs.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

FWOG_WATCHDOG_DEFAULT();

static fwog_link_rx_t s_rx;
static bool s_log;
static char s_path[32];

static void walk_name(char *name, size_t cap, const char *dir,
                      const char *title, const char *fallback) {
    char stem[9];
    unsigned n = 0;
    unsigned i;
    char path[32];
    memset(stem, 0, sizeof stem);
    if (title) {
        for (i = 0; i < 8u && title[i] && n < 8u; i++) {
            char c = title[i];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            if (c < 'A' || c > 'Z') continue;
            stem[n++] = c;
        }
    }
    if (!stem[0]) {
        unsigned idx = dg_fs_next_index(dir, fallback);
        dg_format_name(name, cap, fallback, idx, "CSV");
        return;
    }
    if (n <= 4u) {
        unsigned idx = dg_fs_next_index(dir, stem);
        dg_format_name(name, cap, stem, idx, "CSV");
        return;
    }
    snprintf(name, cap, "%s.CSV", stem);
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (fwog_fs_exists(path)) {
        unsigned idx = dg_fs_next_index(dir, fallback);
        dg_format_name(name, cap, fallback, idx, "CSV");
    }
}

static void log_start(uint8_t mode, const char *title) {
    char name[20];
    char hdr[128];
    int n;
    char stem[9];
    if (s_log) {
        (void)fwog_fs_close();
        s_log = false;
    }
    if (!dg_fs_ensure_dir("/trail")) {
        DIAG("[trail] no /trail\n");
        return;
    }
    walk_name(name, sizeof name, "/trail", title, "WALK");
    snprintf(s_path, sizeof s_path, "/trail/%s", name);
    if (!fwog_fs_open(s_path, true, false)) {
        DIAG("[trail] open %s fail\n", s_path);
        return;
    }
    memset(stem, 0, sizeof stem);
    if (title) memcpy(stem, title, 8);
    n = snprintf(hdr, sizeof hdr,
                 "# diskglass inertialtrail v2 mode=%s title=%s\n"
                 "kind,ms,steps,dist_cm,mag,laps,x,y\n",
                 mode ? "map" : "pedo",
                 stem[0] ? stem : "-");
    if (n <= 0 || !fwog_fs_write(hdr, (size_t)n)) {
        (void)fwog_fs_close();
        DIAG("[trail] header write fail\n");
        return;
    }
    s_log = true;
    DIAG("[trail] start %s\n", s_path);
}

static void log_row(const it_log_t *row) {
    char line[80];
    int n;
    if (!s_log || !row) return;
    n = snprintf(line, sizeof line, "step,%u,%u,%u,%d,%u,%d,%d\n",
                 (unsigned)row->ms, (unsigned)row->steps,
                 (unsigned)row->dist_cm, (int)row->mag,
                 (unsigned)row->laps, (int)row->x, (int)row->y);
    if (n > 0) (void)fwog_fs_write(line, (size_t)n);
}

static void log_stop(void) {
    if (!s_log) return;
    (void)fwog_fs_close();
    s_log = false;
    DIAG("[trail] stop %s\n", s_path);
}

int main(void) {
    board_init();
    const fwog_display_result_t d = fwog_display_update_run();
    DIAG("[trail] display: %s\n", fwog_display_result_text(d));
    DIAG("[trail] FatFs %s\n", dg_fs_ready() ? "mounted" : "no volume");
    fwog_link_rx_init(&s_rx);

    while (true) {
        board_watchdog_kick();
        uint8_t b;
        size_t n;
        while (fwog_link_uart_read(&b)) {
            if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
            if (n < sizeof(it_log_t) || s_rx.buf[0] != IT_MSG_LOG) continue;
            it_log_t row;
            memcpy(&row, s_rx.buf, sizeof row);
            if (row.cmd == IT_LOG_START) log_start(row.mode, row.title);
            else if (row.cmd == IT_LOG_ROW) log_row(&row);
            else if (row.cmd == IT_LOG_STOP) log_stop();
        }
        sleep_ms(10);
    }
}
