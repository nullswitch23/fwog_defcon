#include "fwog_main.h"
#include "fs/fwog_fs.h"
#include "trf_proto.h"
#include "dg_file.h"
#include "dg_fs.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

FWOG_WATCHDOG_DEFAULT();

static cc1101_t s_r[2];
static bool s_ok[2];
static fwog_link_rx_t s_rx;
static uint32_t s_hz[2] = { TRF_HZ_TOP, TRF_HZ };
static bool s_log;
static char s_path[32];

static bool park(cc1101_t *r, uint32_t hz) {
    if (!cc1101_freq_in_band(hz)) return false;
    bool ok = cc1101_idle(r);
    ok = ok && cc1101_set_frequency(r, hz);
    ok = ok && cc1101_set_modulation(r, CC1101_MOD_ASK);
    ok = ok && cc1101_rx(r);
    return ok;
}

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

static void log_start(const char *title) {
    char name[20];
    char hdr[96];
    char stem[9];
    int n;
    if (s_log) {
        (void)fwog_fs_close();
        s_log = false;
    }
    if (!dg_fs_ensure_dir("/trailrf")) {
        DIAG("[trailrf] no /trailrf\n");
        return;
    }
    walk_name(name, sizeof name, "/trailrf", title, "TRAIL");
    snprintf(s_path, sizeof s_path, "/trailrf/%s", name);
    if (!fwog_fs_open(s_path, true, false)) {
        DIAG("[trailrf] open %s fail\n", s_path);
        return;
    }
    memset(stem, 0, sizeof stem);
    if (title) memcpy(stem, title, 8);
    n = snprintf(hdr, sizeof hdr,
                 "# diskglass trailrf v4 title=%s\n"
                 "kind,step,rssi0,rssi1,freq0_hz,freq1_hz\n",
                 stem[0] ? stem : "-");
    if (n <= 0 || !fwog_fs_write(hdr, (size_t)n)) {
        (void)fwog_fs_close();
        DIAG("[trailrf] header write fail\n");
        return;
    }
    s_log = true;
    DIAG("[trailrf] start %s\n", s_path);
}

static void log_row(const char *kind, uint32_t step, int16_t r0, int16_t r1,
                    uint32_t hz0, uint32_t hz1) {
    char line[80];
    int n;
    if (!s_log || !kind) return;
    n = snprintf(line, sizeof line, "%s,%u,%d,%d,%u,%u\n",
                 kind, (unsigned)step, (int)r0, (int)r1,
                 (unsigned)hz0, (unsigned)hz1);
    if (n > 0) (void)fwog_fs_write(line, (size_t)n);
}

static void log_stop(void) {
    if (!s_log) return;
    (void)fwog_fs_close();
    s_log = false;
    DIAG("[trailrf] stop %s\n", s_path);
}

int main(void) {
    board_init();
    const fwog_display_result_t d = fwog_display_update_run();
    DIAG("[trailrf] display: %s\n", fwog_display_result_text(d));
    DIAG("[trailrf] FatFs %s\n", dg_fs_ready() ? "mounted" : "no volume");
    cc1101_bus_init(1000000u);
    cc1101_bind(&s_r[0], CC1101_RADIO_CS0);
    cc1101_bind(&s_r[1], CC1101_RADIO_CS1);
    s_ok[0] = cc1101_bringup(&s_r[0]) && cc1101_probe(&s_r[0]);
    s_ok[1] = cc1101_bringup(&s_r[1]) && cc1101_probe(&s_r[1]);
    if (s_ok[0]) (void)park(&s_r[0], s_hz[0]);
    if (s_ok[1]) (void)park(&s_r[1], s_hz[1]);
    DIAG("[trailrf] r0=%s r1=%s\n", s_ok[0] ? "ok" : "fail",
         s_ok[1] ? "ok" : "fail");
    fwog_link_rx_init(&s_rx);

    while (true) {
        board_watchdog_kick();
        uint8_t b;
        size_t n;
        while (fwog_link_uart_read(&b)) {
            if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
            if (n >= sizeof(trf_cmd_t) && s_rx.buf[0] == TRF_MSG_CMD) {
                trf_cmd_t c;
                memcpy(&c, s_rx.buf, sizeof c);
                unsigned i = (c.radio == 1u) ? 1u : 0u;
                if (c.freq_hz && cc1101_freq_in_band(c.freq_hz) && s_ok[i]) {
                    s_hz[i] = c.freq_hz;
                    (void)park(&s_r[i], s_hz[i]);
                }
            } else if (n >= sizeof(trf_log_t) && s_rx.buf[0] == TRF_MSG_LOG) {
                trf_log_t log;
                memcpy(&log, s_rx.buf, sizeof log);
                if (log.cmd == TRF_LOG_START) log_start(log.title);
                else if (log.cmd == TRF_LOG_ROW || log.cmd == TRF_LOG_MARK) {
                    log_row(log.cmd == TRF_LOG_MARK ? "mark" : "step",
                            log.step, log.rssi0, log.rssi1,
                            log.freq0_hz ? log.freq0_hz : s_hz[0],
                            log.freq1_hz ? log.freq1_hz : s_hz[1]);
                } else if (log.cmd == TRF_LOG_STOP) log_stop();
            }
        }
        trf_status_t st;
        memset(&st, 0, sizeof st);
        st.type = TRF_MSG_ST;
        st.ok = (s_ok[0] ? 1u : 0u) | (s_ok[1] ? 2u : 0u);
        st.rssi0 = s_ok[0] ? cc1101_get_rssi(&s_r[0]) : -127;
        st.rssi1 = s_ok[1] ? cc1101_get_rssi(&s_r[1]) : -127;
        st.freq0_hz = s_hz[0];
        st.freq1_hz = s_hz[1];
        (void)fwog_link_uart_send_frame(&st, sizeof st);
        sleep_ms(50);
    }
}
