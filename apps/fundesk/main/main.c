#include "fwog_main.h"
#include "fs/fwog_fs.h"
#include "fd_proto.h"
#include "it_proto.h"
#include "trf_proto.h"
#include "vp_proto.h"
#include "dg_file.h"
#include "dg_fs.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

FWOG_WATCHDOG_DEFAULT();

/* SEL: 0 home, 1 Trail, 2 TrailRF, 3 VoltPet, 4 PitchFork.
 * CC1101: only TrailRF (both radios parked) or VoltPet (radio0 relic). */

static const uint32_t k_vp_hz[] = { 315000000u, 433920000u, 868000000u };

static fwog_link_rx_t s_rx;
static uint8_t s_sel;
static cc1101_t s_r[2];
static bool s_ok[2];

static bool s_it_log;
static char s_it_path[32];

static uint32_t s_trf_hz[2] = { TRF_HZ_TOP, TRF_HZ };
static bool s_trf_log;
static char s_trf_path[32];

static vp_save_t s_save;
static uint8_t s_vp_band;
static uint32_t s_vp_hop_at, s_vp_cool;
static bool s_vp_in;

static bool park(cc1101_t *r, uint32_t hz) {
    if (!cc1101_freq_in_band(hz)) return false;
    bool ok = cc1101_idle(r);
    ok = ok && cc1101_set_frequency(r, hz);
    ok = ok && cc1101_set_modulation(r, CC1101_MOD_ASK);
    ok = ok && cc1101_rx(r);
    return ok;
}

static void radios_idle(void) {
    if (s_ok[0]) (void)cc1101_idle(&s_r[0]);
    if (s_ok[1]) (void)cc1101_idle(&s_r[1]);
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

static void it_log_stop(void) {
    if (!s_it_log) return;
    (void)fwog_fs_close();
    s_it_log = false;
    DIAG("[trail] stop %s\n", s_it_path);
}

static void it_log_start(uint8_t mode, const char *title) {
    char name[20];
    char hdr[128];
    int n;
    char stem[9];
    it_log_stop();
    if (!dg_fs_ensure_dir("/trail")) {
        DIAG("[trail] no /trail\n");
        return;
    }
    walk_name(name, sizeof name, "/trail", title, "WALK");
    snprintf(s_it_path, sizeof s_it_path, "/trail/%s", name);
    if (!fwog_fs_open(s_it_path, true, false)) {
        DIAG("[trail] open %s fail\n", s_it_path);
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
    s_it_log = true;
    DIAG("[trail] start %s\n", s_it_path);
}

static void it_log_row(const it_log_t *row) {
    char line[80];
    int n;
    if (!s_it_log || !row) return;
    n = snprintf(line, sizeof line, "step,%u,%u,%u,%d,%u,%d,%d\n",
                 (unsigned)row->ms, (unsigned)row->steps,
                 (unsigned)row->dist_cm, (int)row->mag,
                 (unsigned)row->laps, (int)row->x, (int)row->y);
    if (n > 0) (void)fwog_fs_write(line, (size_t)n);
}

static void trf_log_stop(void) {
    if (!s_trf_log) return;
    (void)fwog_fs_close();
    s_trf_log = false;
    DIAG("[trailrf] stop %s\n", s_trf_path);
}

static void trf_log_start(const char *title) {
    char name[20];
    char hdr[96];
    char stem[9];
    int n;
    trf_log_stop();
    if (!dg_fs_ensure_dir("/trailrf")) {
        DIAG("[trailrf] no /trailrf\n");
        return;
    }
    walk_name(name, sizeof name, "/trailrf", title, "TRAIL");
    snprintf(s_trf_path, sizeof s_trf_path, "/trailrf/%s", name);
    if (!fwog_fs_open(s_trf_path, true, false)) {
        DIAG("[trailrf] open %s fail\n", s_trf_path);
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
    s_trf_log = true;
    DIAG("[trailrf] start %s\n", s_trf_path);
}

static void trf_log_row(const char *kind, uint32_t step, int16_t r0, int16_t r1,
                        uint32_t hz0, uint32_t hz1) {
    char line[80];
    int n;
    if (!s_trf_log || !kind) return;
    n = snprintf(line, sizeof line, "%s,%u,%d,%d,%u,%u\n",
                 kind, (unsigned)step, (int)r0, (int)r1,
                 (unsigned)hz0, (unsigned)hz1);
    if (n > 0) (void)fwog_fs_write(line, (size_t)n);
}

static void vp_load(void) {
    memset(&s_save, 0, sizeof s_save);
    s_save.type = VP_MSG_ST;
    s_save.hunger = 40;
    s_save.happy = 70;
    s_save.energy = 80;
    s_save.flags = 1u;
    s_save.level = 1;
    s_save.last_art = 0xFF;
    s_save._pad = 0xFF;
    s_save.magic = VP_MAGIC;
    if (!fwog_fs_mounted() && !fwog_fs_mount()) {
        DIAG("[voltpet] no volume (RAM pet until format)\n");
        return;
    }
    uint8_t raw[sizeof(vp_save_t)];
    size_t n = sizeof raw;
    if (!fwog_fs_open("voltpet.bin", false, false)) return;
    if (!fwog_fs_read(raw, &n) || !vp_save_from_bytes(&s_save, raw, n)) {
        (void)fwog_fs_close();
        memset(&s_save, 0, sizeof s_save);
        s_save.type = VP_MSG_ST;
        s_save.hunger = 40;
        s_save.happy = 70;
        s_save.energy = 80;
        s_save.flags = 1u;
        s_save.level = 1;
        s_save.last_art = 0xFF;
        s_save._pad = 0xFF;
        s_save.magic = VP_MAGIC;
        DIAG("[voltpet] save missing or corrupt; new pet\n");
        return;
    }
    (void)fwog_fs_close();
    DIAG("[voltpet] loaded xp=%u lv=%u relics=%u\n",
         (unsigned)s_save.xp, (unsigned)s_save.level,
         (unsigned)s_save.art_count);
}

static void vp_save_file(const vp_save_t *m) {
    board_watchdog_kick();
    if (!fwog_fs_mounted() && !fwog_fs_mount()) return;
    if (!fwog_fs_open("voltpet.bin", true, false)) {
        DIAG("[voltpet] save open failed\n");
        return;
    }
    const bool ok = fwog_fs_write(m, sizeof *m);
    (void)fwog_fs_close();
    board_watchdog_kick();
    DIAG("[voltpet] save %s\n", ok ? "ok" : "FAIL");
}

static bool vp_tune(uint32_t hz) {
    if (!s_ok[0] || !cc1101_freq_in_band(hz)) return false;
    bool ok = cc1101_idle(&s_r[0]);
    ok = ok && cc1101_set_frequency(&s_r[0], hz);
    ok = ok && cc1101_set_modulation(&s_r[0], CC1101_MOD_ASK);
    ok = ok && cc1101_set_rx_bandwidth(&s_r[0], 270.0f);
    ok = ok && cc1101_rx(&s_r[0]);
    return ok;
}

static void vp_send_rf(int16_t rssi, uint8_t burst, uint8_t art) {
    vp_rf_t m;
    memset(&m, 0, sizeof m);
    m.type = VP_MSG_RF;
    m.ok = s_ok[0] ? 1u : 0u;
    m.burst = burst;
    m.art_id = art;
    m.rssi = rssi;
    m.hz = k_vp_hz[s_vp_band % 3u];
    (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void leave_tile(uint8_t was) {
    if (was == 1u) it_log_stop();
    if (was == 2u) trf_log_stop();
    radios_idle();
}

static void enter_tile(uint8_t sel) {
    radios_idle();
    if (sel == 2u) {
        if (s_ok[0]) (void)park(&s_r[0], s_trf_hz[0]);
        if (s_ok[1]) (void)park(&s_r[1], s_trf_hz[1]);
        DIAG("[fundesk] TrailRF owns CC1101s\n");
    } else if (sel == 3u) {
        s_vp_band = 1;
        s_vp_in = false;
        if (s_ok[0]) (void)vp_tune(k_vp_hz[s_vp_band]);
        DIAG("[fundesk] VoltPet owns radio0\n");
    }
}

int main(void) {
    board_init();
    const fwog_display_result_t d = fwog_display_update_run();
    DIAG("[fundesk] display: %s\n", fwog_display_result_text(d));
    DIAG("[fundesk] FatFs %s\n", dg_fs_ready() ? "mounted" : "no volume");

    cc1101_bus_init(1000000u);
    cc1101_bind(&s_r[0], CC1101_RADIO_CS0);
    cc1101_bind(&s_r[1], CC1101_RADIO_CS1);
    s_ok[0] = cc1101_bringup(&s_r[0]) && cc1101_probe(&s_r[0]);
    s_ok[1] = cc1101_bringup(&s_r[1]) && cc1101_probe(&s_r[1]);
    radios_idle();
    DIAG("[fundesk] r0=%s r1=%s\n", s_ok[0] ? "ok" : "fail",
         s_ok[1] ? "ok" : "fail");

    vp_load();
    fwog_link_rx_init(&s_rx);
    (void)fwog_link_uart_init(FWOG_LINK_BAUD);

    while (true) {
        board_watchdog_kick();
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        uint8_t b;
        size_t n;
        while (fwog_link_uart_read(&b)) {
            if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
            if (n >= sizeof(desk_sel_t) && s_rx.buf[0] == DESK_MSG_SEL) {
                desk_sel_t s;
                memcpy(&s, s_rx.buf, sizeof s);
                if (s.app != s_sel) {
                    leave_tile(s_sel);
                    s_sel = s.app;
                    DIAG("[fundesk] sel=%u\n", (unsigned)s_sel);
                    enter_tile(s_sel);
                }
                continue;
            }
            if (s_sel == 1u && n >= sizeof(it_log_t) && s_rx.buf[0] == IT_MSG_LOG) {
                it_log_t row;
                memcpy(&row, s_rx.buf, sizeof row);
                if (row.cmd == IT_LOG_START) it_log_start(row.mode, row.title);
                else if (row.cmd == IT_LOG_ROW) it_log_row(&row);
                else if (row.cmd == IT_LOG_STOP) it_log_stop();
            }
            if (s_sel == 2u && n >= sizeof(trf_cmd_t) && s_rx.buf[0] == TRF_MSG_CMD) {
                trf_cmd_t c;
                memcpy(&c, s_rx.buf, sizeof c);
                unsigned i = (c.radio == 1u) ? 1u : 0u;
                if (c.freq_hz && cc1101_freq_in_band(c.freq_hz) && s_ok[i]) {
                    s_trf_hz[i] = c.freq_hz;
                    (void)park(&s_r[i], s_trf_hz[i]);
                }
            } else if (s_sel == 2u && n >= sizeof(trf_log_t) &&
                       s_rx.buf[0] == TRF_MSG_LOG) {
                trf_log_t log;
                memcpy(&log, s_rx.buf, sizeof log);
                if (log.cmd == TRF_LOG_START) trf_log_start(log.title);
                else if (log.cmd == TRF_LOG_ROW || log.cmd == TRF_LOG_MARK) {
                    trf_log_row(log.cmd == TRF_LOG_MARK ? "mark" : "step",
                                log.step, log.rssi0, log.rssi1,
                                log.freq0_hz ? log.freq0_hz : s_trf_hz[0],
                                log.freq1_hz ? log.freq1_hz : s_trf_hz[1]);
                } else if (log.cmd == TRF_LOG_STOP) trf_log_stop();
            }
            if (s_sel == 3u && n >= sizeof(vp_save_t) && s_rx.buf[0] == VP_MSG_CMD) {
                vp_save_t in;
                memcpy(&in, s_rx.buf, sizeof in);
                if (in.cmd == VP_CMD_PULL) {
                    s_save.type = VP_MSG_ST;
                    s_save.cmd = 0;
                    (void)fwog_link_uart_send_frame(&s_save, sizeof s_save);
                } else if (in.cmd == VP_CMD_PUSH && in.magic == VP_MAGIC) {
                    vp_name_sanitize(in.name);
                    s_save = in;
                    s_save.type = VP_MSG_ST;
                    vp_save_file(&s_save);
                    (void)fwog_link_uart_send_frame(&s_save, sizeof s_save);
                }
            }
        }

        if (s_sel == 2u) {
            trf_status_t st;
            memset(&st, 0, sizeof st);
            st.type = TRF_MSG_ST;
            st.ok = (s_ok[0] ? 1u : 0u) | (s_ok[1] ? 2u : 0u);
            st.rssi0 = s_ok[0] ? cc1101_get_rssi(&s_r[0]) : -127;
            st.rssi1 = s_ok[1] ? cc1101_get_rssi(&s_r[1]) : -127;
            st.freq0_hz = s_trf_hz[0];
            st.freq1_hz = s_trf_hz[1];
            (void)fwog_link_uart_send_frame(&st, sizeof st);
            sleep_ms(50);
            continue;
        }

        if (s_sel == 3u) {
            if (s_ok[0] && now >= s_vp_hop_at) {
                s_vp_hop_at = now + 2500u;
                s_vp_band = (uint8_t)((s_vp_band + 1u) % 3u);
                (void)vp_tune(k_vp_hz[s_vp_band]);
                s_vp_in = false;
            }
            const int16_t rssi = s_ok[0] ? cc1101_get_rssi(&s_r[0]) : (int16_t)-127;
            uint8_t burst = 0, art = 0;
            if (!s_vp_in && rssi > -74 && now >= s_vp_cool) {
                s_vp_in = true;
                burst = 1;
                art = (uint8_t)(((uint32_t)(rssi + 200) * 1103515245u +
                                 (k_vp_hz[s_vp_band % 3u] / 1000u) + now) % VP_NART);
                s_vp_cool = now + 12000u;
            } else if (s_vp_in && rssi < -86) {
                s_vp_in = false;
            }
            vp_send_rf(rssi, burst, art);
            sleep_ms(40);
            continue;
        }

        sleep_ms(20);
    }
}
