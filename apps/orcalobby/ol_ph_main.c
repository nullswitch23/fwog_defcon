/* OrcaLobby PingHalo C6 tile. */
#include "ol_ph_main.h"
#include "fwog_main.h"
#include "fs/fwog_fs.h"
#include "ph_proto.h"
#include "ph_track.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BN_LINE_MAX 160u

static ph_status_t     s_st;
static fwog_bn_flash_t s_fl;
static ph_table_t      s_tab;
static char            s_line[BN_LINE_MAX];
static unsigned        s_llen;
static uint32_t        s_push_ms;
static uint32_t        s_age_ms;
static bool            s_want_scan;
static uint32_t        s_scan_kick_ms;
static bool            s_dirty = true;
static bool            s_rx_dumped;
static uint8_t         s_rx_first[16];
static unsigned        s_rx_n;
static unsigned        s_scroll;
static uint8_t         s_lock_addr[6];
static bool            s_have_lock;
static bool            s_freeze;
static ph_dev_t        s_frz[PH_STORE];
static unsigned        s_frz_n;
static ph_lib_file_t   s_lib;
static bool            s_vol;
static bool            s_need_save;
static ph_trend_t      s_trend;
static uint16_t        s_rxb;
static uint16_t        s_e8, s_e9;
static int             s_last8 = -1, s_last9 = -1;
static bool            s_app_uart;
static uint32_t        s_probe_ms;

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

static int hexn(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static bool parse_mac(const char *s, uint8_t addr[6]) {
    if (!s || strlen(s) < 12u) return false;
    for (unsigned i = 0; i < 6u; i++) {
        const int hi = hexn(s[i * 2u]);
        const int lo = hexn(s[i * 2u + 1u]);
        if (hi < 0 || lo < 0) return false;
        addr[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static void overlay_lib(ph_dev_t *d) {
    if (!d) return;
    for (unsigned i = 0; i < PH_LIB_N; i++) {
        if (!s_lib.ent[i].have) continue;
        if (memcmp(s_lib.ent[i].addr, d->addr, 6) != 0) continue;
        memset(d->name, 0, PH_NAME_N);
        strncpy(d->name, s_lib.ent[i].label, PH_NAME_N - 1u);
        return;
    }
}

static void lib_default(void) {
    memset(&s_lib, 0, sizeof s_lib);
    s_lib.magic = PH_LIB_MAGIC;
}

static unsigned lib_have_n(const ph_lib_file_t *f) {
    unsigned n = 0u;
    if (!f) return 0u;
    for (unsigned i = 0u; i < PH_LIB_N; i++) {
        if (f->ent[i].have) n++;
    }
    return n;
}

static bool read_lib_path(const char *path, ph_lib_file_t *out) {
    ph_lib_file_t f;
    size_t n;

    if (!path || !out) return false;
    if (!fwog_fs_open(path, false, false)) return false;
    n = sizeof f;
    memset(&f, 0, sizeof f);
    if (!fwog_fs_read(&f, &n) || n != sizeof f || f.magic != PH_LIB_MAGIC) {
        (void)fwog_fs_close();
        return false;
    }
    (void)fwog_fs_close();
    *out = f;
    return true;
}

static void load_lib(void) {
    lib_default();
    if (!s_vol && !fwog_fs_mount()) {
        DIAG("[pinghalo] no FatFs\n");
        return;
    }
    s_vol = true;
    if (read_lib_path(PH_LIB_NAME, &s_lib)) {
        DIAG("[pinghalo] lib loaded n=%u\n", lib_have_n(&s_lib));
        return;
    }
    /* Crash mid-rename left the tmp; promote it. */
    if (read_lib_path(PH_LIB_TMP, &s_lib)) {
        (void)fwog_fs_remove(PH_LIB_NAME);
        (void)fwog_fs_rename(PH_LIB_TMP, PH_LIB_NAME);
        DIAG("[pinghalo] lib from tmp n=%u\n", lib_have_n(&s_lib));
        return;
    }
    DIAG("[pinghalo] lib missing\n");
}

static bool save_lib(const ph_lib_file_t *f) {
    ph_lib_file_t check;

    if (!f) return false;
    if (!s_vol && !fwog_fs_mount()) return false;
    s_vol = true;

    /* Whole commit under the long window so CREATE+write+rename finishes
     * even if the board then watchdog-resets. Truncating PHLIB.BIN first
     * is what left an empty library after that reboot. */
    board_watchdog_kick();
    board_watchdog_pause();
    (void)fwog_fs_remove(PH_LIB_TMP);
    if (!fwog_fs_open(PH_LIB_TMP, true, false)) {
        board_watchdog_resume();
        DIAG("[pinghalo] lib tmp open fail\n");
        return false;
    }
    board_watchdog_kick();
    (void)fwog_fs_preallocate((uint32_t)sizeof *f);
    if (!fwog_fs_write(f, sizeof *f)) {
        (void)fwog_fs_close();
        board_watchdog_resume();
        DIAG("[pinghalo] lib write fail\n");
        return false;
    }
    if (!fwog_fs_close()) {
        board_watchdog_resume();
        DIAG("[pinghalo] lib close fail\n");
        return false;
    }
    board_watchdog_kick();
    (void)fwog_fs_remove(PH_LIB_NAME);
    if (!fwog_fs_rename(PH_LIB_TMP, PH_LIB_NAME)) {
        board_watchdog_resume();
        DIAG("[pinghalo] lib rename fail\n");
        return false;
    }
    board_watchdog_resume();
    board_watchdog_kick();

    if (!read_lib_path(PH_LIB_NAME, &check) ||
        check.magic != PH_LIB_MAGIC ||
        lib_have_n(&check) != lib_have_n(f)) {
        DIAG("[pinghalo] lib verify fail\n");
        return false;
    }
    s_lib = check;
    DIAG("[pinghalo] lib saved n=%u\n", lib_have_n(&s_lib));
    return true;
}

static void request_save(void) {
    s_need_save = true;
    s_st.lib_op = PH_LIBOP_SAVE;
    s_dirty = true;
}

static bool apply_label(const uint8_t addr[6], const char *label) {
    int slot = -1;
    unsigned i;

    if (!addr) return false;
    for (i = 0u; i < PH_LIB_N; i++) {
        if (s_lib.ent[i].have && memcmp(s_lib.ent[i].addr, addr, 6) == 0) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0) {
        for (i = 0u; i < PH_LIB_N; i++) {
            if (!s_lib.ent[i].have) {
                slot = (int)i;
                break;
            }
        }
    }
    if (slot < 0) return false;
    s_lib.magic = PH_LIB_MAGIC;
    memset(&s_lib.ent[slot], 0, sizeof s_lib.ent[slot]);
    s_lib.ent[slot].have = 1u;
    memcpy(s_lib.ent[slot].addr, addr, 6);
    if (label) strncpy(s_lib.ent[slot].label, label, PH_NAME_N - 1u);
    for (i = 0u; i < s_frz_n; i++) {
        if (memcmp(s_frz[i].addr, addr, 6) == 0) {
            memset(s_frz[i].name, 0, PH_NAME_N);
            if (label) strncpy(s_frz[i].name, label, PH_NAME_N - 1u);
        }
    }
    request_save();
    return true;
}

static void send_lib(void) {
    ph_lib_msg_t m;
    memset(&m, 0, sizeof m);
    m.type = PH_MSG_LIB;
    m.cmd = PH_LIB_SET;
    m.magic = PH_LIB_MAGIC;
    memcpy(m.ent, s_lib.ent, sizeof m.ent);
    (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void hunt_reset(void) {
    ph_trend_reset(&s_trend);
    s_st.trend = PH_TREND_UNK;
    s_st.hist_n = 0u;
    memset(s_st.hist, 0, sizeof s_st.hist);
}

static void rebuild_window(void) {
    const unsigned max = s_have_lock ? 4u : (unsigned)PH_MAX;
    s_st.freeze = s_freeze ? 1u : 0u;
    memset(s_st.dev, 0, sizeof s_st.dev);
    if (s_freeze && s_frz_n) {
        if (s_scroll >= s_frz_n) s_scroll = s_frz_n - 1u;
        s_st.total = (uint8_t)(s_frz_n > 255u ? 255u : s_frz_n);
        s_st.n = (uint8_t)ph_snap_window(s_frz, s_frz_n, s_scroll,
                                         s_st.dev, (unsigned)PH_MAX);
        for (unsigned i = 0; i < s_st.n; i++) overlay_lib(&s_st.dev[i]);
    } else {
        const unsigned total = ph_table_match_count(&s_tab, s_st.filter);
        s_st.total = (uint8_t)(total > 255u ? 255u : total);
        if (s_scroll > 0u && s_scroll >= total) {
            s_scroll = total ? total - 1u : 0u;
        }
        s_st.n = (uint8_t)ph_table_window(&s_tab, s_st.filter, s_scroll,
                                          s_st.dev, max,
                                          s_have_lock ? s_lock_addr : NULL);
        if (s_have_lock) {
            bool at0 = s_st.n && memcmp(s_st.dev[0].addr, s_lock_addr, 6) == 0;
            if (!at0 && max) {
                if (s_st.n + 1u > max) s_st.n = (uint8_t)(max - 1u);
                if (s_st.n) {
                    memmove(&s_st.dev[1], &s_st.dev[0],
                            sizeof s_st.dev[0] * s_st.n);
                }
                memset(&s_st.dev[0], 0, sizeof s_st.dev[0]);
                memcpy(s_st.dev[0].addr, s_lock_addr, 6);
                s_st.dev[0].rssi = -127;
                s_st.n++;
            }
        }
        for (unsigned i = 0; i < s_st.n; i++) overlay_lib(&s_st.dev[i]);
    }
    s_st.locked = 0u;
    if (s_have_lock && s_st.n &&
        memcmp(s_st.dev[0].addr, s_lock_addr, 6) == 0) {
        s_st.locked = 1u;
    } else if (!s_have_lock) {
        s_st.trend = PH_TREND_UNK;
        s_st.hist_n = 0u;
    }
    if (s_st.n == 0u) {
        s_st.cursor = 0u;
    } else if (s_st.cursor >= s_st.n) {
        s_st.cursor = (uint8_t)(s_st.n - 1u);
    }
}

static void push_st(void) {
    s_st.type = PH_MSG_ST;
    s_st.flash = s_fl.flash;
    s_st.pct = s_fl.pct;
    memset(s_st.why, 0, sizeof s_st.why);
    strncpy(s_st.why, s_fl.why, sizeof s_st.why - 1u);
    (void)fwog_link_uart_send_frame(&s_st, sizeof s_st);
    s_dirty = false;
}

static void flash_tick(const fwog_bn_flash_t *st, void *ctx) {
    (void)st;
    (void)ctx;
    s_dirty = true;
    push_st();
}

static void handle_line(char *line) {
    if (strncmp(line, "BN ", 3) != 0) return;
    DIAG("[pinghalo] %s\n", line);
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (strncmp(line, "BN HELLO", 8) == 0) {
        const bool first = !s_st.hello;
        s_st.hello = 1u;
        memset(s_fl.why, 0, sizeof s_fl.why);
        if (s_fl.flash == FWOG_BN_FLASH_OK) s_fl.flash = FWOG_BN_FLASH_IDLE;
        if (!s_app_uart) {
            fwog_bn_link_app();
            s_app_uart = true;
        }
        if (first) {
            s_want_scan = true;
            s_scan_kick_ms = now + 80u;
        }
        s_dirty = true;
        return;
    }
    if (strncmp(line, "BN BLE off", 10) == 0) {
        s_st.scan_on = 0u;
        ph_table_clear(&s_tab);
        s_st.n = 0u;
        s_st.total = 0u;
        memset(s_st.dev, 0, sizeof s_st.dev);
        s_dirty = true;
        return;
    }
    if (strncmp(line, "BN BLE on", 9) == 0) {
        s_st.scan_on = 1u;
        s_dirty = true;
        return;
    }
    if (strncmp(line, "BN ADV ", 7) == 0) {
        char rssi_s[8], apple_s[4], mac_s[16], name[PH_NAME_N];
        uint8_t addr[6];
        copy_kv(line, "rssi=", rssi_s, sizeof rssi_s);
        copy_kv(line, "apple=", apple_s, sizeof apple_s);
        copy_kv(line, "mac=", mac_s, sizeof mac_s);
        copy_kv(line, "name=", name, sizeof name);
        if (!parse_mac(mac_s, addr)) return;
        const int8_t rssi = (int8_t)atoi(rssi_s);
        ph_table_upsert(&s_tab, now, rssi,
                        apple_s[0] == '1' ? 1u : 0u, addr, name);
        s_st.scan_on = 1u;
        if (s_have_lock && memcmp(addr, s_lock_addr, 6) == 0) {
            ph_hist_push(s_st.hist, &s_st.hist_n, PH_HIST, rssi);
            s_st.trend = ph_trend_burst(&s_trend, rssi);
            s_dirty = true;
        }
        if (!s_freeze) {
            rebuild_window();
            s_dirty = true;
        }
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
            DIAG("[pinghalo] uart rx n=%u", s_rx_n);
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


void ol_ph_main_enter(void) {
    static bool ready;
    board_watchdog_kick();
    if (!ready) {
        ph_table_init(&s_tab);
        s_vol = fwog_fs_mount();
        load_lib();
        board_watchdog_kick();
        ready = true;
    }
    memset(&s_st, 0, sizeof s_st);
    memset(&s_fl, 0, sizeof s_fl);
    s_app_uart = true;
    s_want_scan = true;
    s_llen = 0;
    s_have_lock = false;
    s_freeze = false;
    s_frz_n = 0;
    s_dirty = true;
    hunt_reset();
    send_lib();
    push_st();
}

void ol_ph_main_leave(void) {
    s_want_scan = false;
    fwog_bn_write(&s_fl, "BN BLE STOP\n");
    fwog_bn_flash_cancel(&s_fl, flash_tick, NULL);
}

void ol_ph_main_frame(const uint8_t *buf, size_t n) {
            if (n >= sizeof(ph_cmd_t) && buf[0] == PH_MSG_CMD) {
                ph_cmd_t c;
                memcpy(&c, buf, sizeof c);
                if (c.cmd == PH_CMD_SCAN) {
                    s_want_scan = c.on != 0u;
                    if (!s_want_scan) {
                        ph_table_clear(&s_tab);
                        s_st.n = 0u;
                        s_st.total = 0u;
                        s_st.scan_on = 0u;
                        memset(s_st.dev, 0, sizeof s_st.dev);
                        s_have_lock = false;
                        s_freeze = false;
                        s_frz_n = 0;
                        hunt_reset();
                        s_dirty = true;
                        fwog_bn_write(&s_fl, "BN BLE STOP\n");
                    } else {
                        s_scan_kick_ms = 0;
                    }
                } else if (c.cmd == PH_CMD_FLASH) {
                    if (c.on == 1u) {
                        if (s_want_scan) {
                            fwog_bn_write(&s_fl, "BN BLE STOP\n");
                            s_want_scan = false;
                        }
                        s_st.hello = 0;
                        s_rx_dumped = false;
                        s_rx_n = 0;
                        s_rxb = 0;
                        s_e8 = 0;
                        s_e9 = 0;
                        s_last8 = -1;
                        s_last9 = -1;
                        fwog_bn_flash_arm(&s_fl, flash_tick, NULL);
                    } else if (c.on == 2u) {
                        fwog_bn_flash_go(&s_fl, flash_tick, NULL);
                    } else {
                        fwog_bn_flash_cancel(&s_fl, flash_tick, NULL);
                    }
                } else if (c.cmd == PH_CMD_FILT) {
                    s_st.filter = (uint8_t)(c.on % 3u);
                    s_scroll = 0;
                    s_st.cursor = 0;
                    s_freeze = false;
                    s_frz_n = 0;
                    rebuild_window();
                    s_dirty = true;
                } else if (c.cmd == PH_CMD_CURSOR && s_st.n) {
                    if (c.on == 0u) {
                        if (s_st.cursor > 0u) {
                            s_st.cursor--;
                        } else if (s_scroll > 0u) {
                            s_scroll--;
                            rebuild_window();
                        }
                    } else {
                        if ((unsigned)s_st.cursor + 1u < s_st.n) {
                            s_st.cursor++;
                        } else if (s_scroll + s_st.n < s_st.total) {
                            s_scroll++;
                            rebuild_window();
                        }
                    }
                    s_dirty = true;
                } else if (c.cmd == PH_CMD_LOCK && s_st.n) {
                    const ph_dev_t *d = &s_st.dev[s_st.cursor];
                    if (s_have_lock && memcmp(s_lock_addr, d->addr, 6) == 0) {
                        s_have_lock = false;
                    } else {
                        memcpy(s_lock_addr, d->addr, 6);
                        s_have_lock = true;
                        s_st.cursor = 0u;
                        s_scroll = 0u;
                    }
                    s_freeze = false;
                    s_frz_n = 0;
                    hunt_reset();
                    rebuild_window();
                    s_dirty = true;
                } else if (c.cmd == PH_CMD_FREEZE) {
                    if (c.on) {
                        s_freeze = false;
                        s_frz_n = ph_table_window(
                            &s_tab, s_st.filter, 0, s_frz, PH_STORE,
                            s_have_lock ? s_lock_addr : NULL);
                        s_scroll = 0u;
                        s_st.cursor = 0u;
                        s_freeze = s_frz_n > 0u;
                    } else {
                        s_freeze = false;
                        s_frz_n = 0;
                    }
                    rebuild_window();
                    s_dirty = true;
                } else if (c.cmd == PH_CMD_LIBPIN && c.on < PH_LIB_N &&
                           s_lib.ent[c.on].have) {
                    memcpy(s_lock_addr, s_lib.ent[c.on].addr, 6);
                    s_have_lock = true;
                    s_freeze = false;
                    s_st.cursor = 0u;
                    s_scroll = 0u;
                    hunt_reset();
                    rebuild_window();
                    s_dirty = true;
                } else if (c.cmd == PH_CMD_LIBSAVE && c.on < PH_LIB_N) {
                    const ph_dev_t *d = NULL;
                    if (s_have_lock && s_st.n &&
                        memcmp(s_st.dev[0].addr, s_lock_addr, 6) == 0) {
                        d = &s_st.dev[0];
                    } else if (s_st.n && s_st.cursor < s_st.n) {
                        d = &s_st.dev[s_st.cursor];
                    }
                    memset(&s_lib.ent[c.on], 0, sizeof s_lib.ent[c.on]);
                    if (d) {
                        s_lib.ent[c.on].have = 1u;
                        memcpy(s_lib.ent[c.on].addr, d->addr, 6);
                        if (d->name[0]) {
                            strncpy(s_lib.ent[c.on].label, d->name,
                                    PH_NAME_N - 1u);
                        }
                    } else if (s_have_lock) {
                        s_lib.ent[c.on].have = 1u;
                        memcpy(s_lib.ent[c.on].addr, s_lock_addr, 6);
                    }
                    if (s_lib.ent[c.on].have) request_save();
                } else if (c.cmd == PH_CMD_LIBDEL && c.on < PH_LIB_N) {
                    if (s_lib.ent[c.on].have) {
                        memset(&s_lib.ent[c.on], 0, sizeof s_lib.ent[c.on]);
                        request_save();
                    }
                }
            } else if (n >= sizeof(ph_lib_msg_t) && buf[0] == PH_MSG_LIB) {
                ph_lib_msg_t in;
                memcpy(&in, buf, sizeof in);
                if (in.cmd == (uint8_t)PH_LIB_GET) {
                    send_lib();
                } else if (in.cmd == (uint8_t)PH_LIB_SET &&
                           in.magic == PH_LIB_MAGIC) {
                    s_lib.magic = PH_LIB_MAGIC;
                    memcpy(s_lib.ent, in.ent, sizeof s_lib.ent);
                    request_save();
                }
            } else if (n >= sizeof(ph_label_msg_t) &&
                       buf[0] == PH_MSG_LABEL) {
                ph_label_msg_t lb;
                memcpy(&lb, buf, sizeof lb);
                (void)apply_label(lb.addr, lb.label);
            }

}

void ol_ph_main_tick(uint32_t now) {
        if (s_fl.flash == FWOG_BN_FLASH_HOLD) {
            const uint16_t a = s_fl.e8, b = s_fl.e9;
            fwog_bn_flash_poll(&s_fl);
            if (s_fl.e8 != a || s_fl.e9 != b) s_dirty = true;
        }
        bn_poll();
        if (s_need_save) {
            s_st.lib_op = PH_LIBOP_SAVE;
            s_st.scan_on = 0u;
            push_st();
            board_watchdog_kick();
            fwog_bn_write(&s_fl, "BN BLE STOP\n");
            {
                unsigned i;
                for (i = 0u; i < 50u; i++) {
                    board_watchdog_kick();
                    bn_poll();
                    sleep_ms(10);
                }
            }
            board_watchdog_kick();
            {
                const bool ok = save_lib(&s_lib);
                s_need_save = false;
                s_st.lib_op = ok ? PH_LIBOP_OK : PH_LIBOP_FAIL;
            }
            send_lib();
            rebuild_window();
            s_dirty = true;
            if (s_want_scan && s_st.hello) {
                s_scan_kick_ms = to_ms_since_boot(get_absolute_time()) + 200u;
            }
            board_watchdog_kick();
        }

        if (s_st.hello && s_want_scan && !s_st.scan_on &&
            s_app_uart && !s_fl.rom_pins &&
            s_fl.flash == FWOG_BN_FLASH_IDLE &&
            (int32_t)(now - s_scan_kick_ms) >= 0) {
            DIAG("[pinghalo] BLE START\n");
            fwog_bn_write(&s_fl, "BN HELLO og\nBN BLE START\n");
            s_scan_kick_ms = now + 1000u;
        }
        if (!s_fl.rom_pins && (now - s_age_ms) > 500u) {
            s_age_ms = now;
            const unsigned before = s_tab.n;
            ph_table_age(&s_tab, now, PH_STALE_MS,
                         s_have_lock ? s_lock_addr : NULL);
            if (!s_freeze && s_tab.n != before) {
                rebuild_window();
                s_dirty = true;
            }
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
                DIAG("[pinghalo] %s hello=%u\n", s_fl.why, (unsigned)s_st.hello);
                s_dirty = true;
            }
        }
        if (s_dirty || (now - s_push_ms) > 250u) {
            s_push_ms = now;
            push_st();
        }
}
