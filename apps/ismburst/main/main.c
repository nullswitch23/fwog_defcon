/* CC1101 ASK / 2-FSK capture, decode, and owned-gadget replay.
 *
 * Named slots FAN/DOOR/SPARE (T9 nicknames persist as /ismburst/*.TXT).
 * Green arms one shot. Gray hold hunts until those slots are full. GDO0
 * async-serial edges (ASK or 2-FSK demod) go into a RAM ring (512 edges).
 * Decode is decode.c. Yellow hold saves a filled slot or loads the next
 * /ismburst/BURSTNNNN.BIN into an empty one. Blue hold TX's those timings
 * a few times, then stops. For ISM gadgets you own. Not a jammer, not
 * rolljam, not KeeLoq/Honda, not FobReplay. */
#include "fwog_main.h"
#include "fs/fwog_fs.h"
#include "ib_proto.h"
#include "decode.h"
#include "dg_file.h"
#include "dg_fs.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

FWOG_WATCHDOG_DEFAULT();

#define IB_GAP_US      25000u
#define IB_WAIT_MS     12000u
#define IB_CAPTURE_MS  4000u
#define IB_MIN_EDGES   16u
#define IB_SPI_HZ      1000000u
#define IB_STORE_MAGIC 0x54534249u /* 'IBST' */
#define IB_STORE_VER   2u

static const char *k_slot_name[IB_RING] = { "FAN", "DOOR", "SPARE" };
static uint8_t s_view;
static char    s_names[IB_RING][IB_LABEL_LEN];
static ib_name_t s_name_msg;

static const char *slot_path(unsigned i) {
    static const char *p[IB_RING] = {
        "/ismburst/FAN.BIN", "/ismburst/DOOR.BIN", "/ismburst/SPARE.BIN"
    };
    return p[i % IB_RING];
}

static const char *slot_name_path(unsigned i) {
    static const char *p[IB_RING] = {
        "/ismburst/FAN.TXT", "/ismburst/DOOR.TXT", "/ismburst/SPARE.TXT"
    };
    return p[i % IB_RING];
}

static void copy_slot_name(char *dst, const char *src) {
    unsigned i = 0;
    if (!dst) return;
    memset(dst, 0, IB_LABEL_LEN);
    if (!src) return;
    for (; src[i] != '\0' && i + 1u < IB_LABEL_LEN; i++) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ')
            dst[i] = c;
        else
            break;
    }
}

static void load_names(void) {
    unsigned i;
    memset(s_names, 0, sizeof s_names);
    for (i = 0; i < IB_RING; i++) {
        char buf[IB_LABEL_LEN];
        size_t n;
        memset(buf, 0, sizeof buf);
        if (!fwog_fs_exists(slot_name_path(i))) continue;
        if (!fwog_fs_open(slot_name_path(i), false, false)) continue;
        n = sizeof buf - 1u;
        (void)fwog_fs_read(buf, &n);
        (void)fwog_fs_close();
        copy_slot_name(s_names[i], buf);
        if (s_names[i][0]) {
            DIAG("[ismburst] name %s=%s\n", k_slot_name[i], s_names[i]);
        }
    }
}

static bool save_name(unsigned slot) {
    slot %= IB_RING;
    if (!fwog_fs_mounted() && !fwog_fs_mount()) return false;
    if (!dg_fs_ensure_dir("/ismburst")) return false;
    if (!s_names[slot][0]) {
        (void)fwog_fs_remove(slot_name_path(slot));
        return true;
    }
    if (!fwog_fs_open(slot_name_path(slot), true, false)) return false;
    {
        const size_t n = strlen(s_names[slot]);
        const bool ok = fwog_fs_write(s_names[slot], n);
        (void)fwog_fs_close();
        return ok;
    }
}

static void apply_name_msg(void) {
    const unsigned slot = (unsigned)s_name_msg.slot % IB_RING;
    copy_slot_name(s_names[slot], s_name_msg.name);
    s_view = (uint8_t)slot;
    if (save_name(slot)) {
        DIAG("[ismburst] named %s %s\n", k_slot_name[slot],
             s_names[slot][0] ? s_names[slot] : k_slot_name[slot]);
    } else {
        DIAG("[ismburst] name save failed %s\n", k_slot_name[slot]);
    }
}

typedef struct {
    bool     occupied;
    bool     first_level;
    uint8_t  mod;
    uint16_t n;
    uint32_t freq_hz;
    uint32_t total_us;
    int16_t  peak;
    uint16_t dur[IB_MAX_EDGES];
    ib_decode_t dec;
} ib_shot_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t ver;
    uint16_t n;
    uint32_t freq_hz;
    uint32_t total_us;
    int16_t  peak;
    uint8_t  first_level;
    uint8_t  mod;       /* IB_MOD_* ; v1 files read as ASK */
    uint16_t dur[IB_MAX_EDGES];
} ib_store_file_t;

static cc1101_t       s_radio;
static fwog_link_rx_t s_rx;
static ib_cmd_t       s_cmd;
static bool           s_cmd_fresh;
static uint8_t        s_state = IB_ST_IDLE;
static bool           s_ok;
static ib_shot_t      s_ring[IB_RING];
static ib_shot_t      s_disk;
static uint16_t       s_bursts;
static bool           s_in;
static uint32_t       s_t0;
static uint32_t       s_tuned;
static uint8_t        s_mod;
static uint8_t        s_wait_s;
static bool           s_hunt;
static unsigned       s_burst_i;
static bool           s_name_fresh;

static void gdo0_as_input(void) {
    gpio_set_dir(s_radio.gdo0_pin, GPIO_IN);
    gpio_pull_up(s_radio.gdo0_pin);
}

static void gdo0_as_output(bool level) {
    gpio_disable_pulls(s_radio.gdo0_pin);
    gpio_set_dir(s_radio.gdo0_pin, GPIO_OUT);
    gpio_put(s_radio.gdo0_pin, level);
}

static void wait_us(uint32_t us) {
    const uint32_t t0 = time_us_32();
    while ((uint32_t)(time_us_32() - t0) < us) tight_loop_contents();
}

static bool configure_radio(uint32_t hz, uint8_t mod) {
    if (!cc1101_freq_in_band(hz)) return false;
    const bool fsk = (mod == IB_MOD_2FSK);
    bool ok = cc1101_idle(&s_radio);
    ok = ok && cc1101_set_frequency(&s_radio, hz);
    ok = ok && cc1101_set_modulation(&s_radio,
        fsk ? CC1101_MOD_2FSK : CC1101_MOD_ASK);
    ok = ok && cc1101_set_pkt_format(&s_radio, 3u);
    ok = ok && cc1101_set_sync_mode(&s_radio, 0u);
    ok = ok && cc1101_set_crc(&s_radio, false);
    ok = ok && cc1101_set_white_data(&s_radio, false);
    ok = ok && cc1101_set_manchester(&s_radio, false);
    ok = ok && cc1101_set_append_status(&s_radio, false);
    ok = ok && cc1101_set_dc_filter_off(&s_radio, true);
    ok = ok && cc1101_set_rx_bandwidth(&s_radio, fsk ? 162.0f : 270.0f);
    ok = ok && cc1101_set_data_rate(&s_radio, fsk ? 2.4f : 10.0f);
    if (fsk) ok = ok && cc1101_set_deviation(&s_radio, 25.4f);
    ok = ok && cc1101_set_power(&s_radio, 10);
    ok = ok && cc1101_write_reg(&s_radio, CC1101_REG_IOCFG0, 0x0Du);
    ok = ok && cc1101_write_reg(&s_radio, CC1101_REG_AGCCTRL2, 0x07u);
    ok = ok && cc1101_write_reg(&s_radio, CC1101_REG_AGCCTRL1, 0x00u);
    ok = ok && cc1101_write_reg(&s_radio, CC1101_REG_AGCCTRL0, 0x91u);
    gdo0_as_input();
    if (ok) {
        s_tuned = hz;
        s_mod = fsk ? IB_MOD_2FSK : IB_MOD_ASK;
    }
    return ok;
}

static void listen(void) {
    gdo0_as_input();
    (void)cc1101_write_reg(&s_radio, CC1101_REG_IOCFG0, 0x0Du);
    (void)cc1101_idle(&s_radio);
    (void)cc1101_flush_rx(&s_radio);
    (void)cc1101_rx(&s_radio);
}

static void have_or_idle(void) {
    unsigned i;
    for (i = 0; i < IB_RING; i++) {
        if (s_ring[i].occupied) {
            s_state = IB_ST_HAVE;
            return;
        }
    }
    s_state = s_disk.occupied ? IB_ST_HAVE : IB_ST_IDLE;
}

static const ib_shot_t *view_shot(void) {
    if (s_view < IB_RING && s_ring[s_view].occupied) return &s_ring[s_view];
    if (s_disk.occupied) return &s_disk;
    return NULL;
}

static void fill_from_shot(ib_status_t *m, const ib_shot_t *s) {
    if (s == NULL || !s->occupied) return;
    m->guess = s->dec.guess;
    m->coding = s->dec.coding;
    m->peak_rssi = s->peak;
    m->edges = s->n;
    m->duration_ms = (uint16_t)(s->total_us / 1000u);
    m->bits = s->dec.bits;
    m->cap_hz = s->freq_hz;
    m->pw_short_us = s->dec.pw_short_us;
    m->pw_long_us = s->dec.pw_long_us;
    m->gap_short_us = s->dec.gap_short_us;
    m->gap_long_us = s->dec.gap_long_us;
    m->temp_c_x10 = s->dec.temp_c_x10;
    m->humidity = s->dec.humidity;
    m->channel = s->dec.channel;
    m->hex_n = s->dec.hex_n;
    memcpy(m->hex, s->dec.hex, IB_HEX_BYTES);
    memcpy(m->hist, s->dec.hist, IB_HIST_BINS);
    memcpy(m->label, s->dec.label, IB_LABEL_LEN);
}

static void send_status(int16_t rssi) {
    ib_status_t m;
    memset(&m, 0, sizeof m);
    m.type = IB_MSG_ST;
    m.state = s_state;
    m.ok = s_ok ? 1u : 0u;
    m.rssi = rssi;
    m.bursts = s_bursts;
    m.freq_hz = s_cmd.freq_hz;
    m.temp_c_x10 = (int16_t)IB_TEMP_NONE;
    m.humidity = (uint8_t)IB_HUM_NONE;
    m.slot = s_view < IB_RING ? s_view : 0u;
    m.slots = IB_RING;
    m.saved = 0u;
    if (s_view < IB_RING && fwog_fs_mounted() &&
        fwog_fs_exists(slot_path(s_view))) {
        m.saved = 1u;
    } else if (s_disk.occupied) {
        m.saved = 1u;
    }
    m.wait_s = s_wait_s;
    m.hunt = s_hunt ? 1u : 0u;
    m.mod = s_mod;
    memcpy(m.name, s_names[s_view % IB_RING], IB_LABEL_LEN);
    const ib_shot_t *s = view_shot();
    if (s != NULL) {
        fill_from_shot(&m, s);
        m.mod = s->mod;
    }
    (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void poll_link(void) {
    uint8_t b;
    size_t n;
    while (fwog_link_uart_read(&b)) {
        if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
        if (s_rx.buf[0] == IB_MSG_CMD && n >= 2u &&
            s_rx.buf[1] == IB_CMD_NAME && n >= sizeof(ib_name_t)) {
            memcpy(&s_name_msg, s_rx.buf, sizeof s_name_msg);
            s_name_fresh = true;
        } else if (n >= sizeof(ib_cmd_t) && s_rx.buf[0] == IB_MSG_CMD) {
            memcpy(&s_cmd, s_rx.buf, sizeof s_cmd);
            s_cmd_fresh = true;
        }
    }
}

static void shot_from_file(ib_shot_t *slot, const ib_store_file_t *f) {
    memset(slot, 0, sizeof *slot);
    slot->first_level = f->first_level != 0u;
    slot->n = f->n > IB_MAX_EDGES ? IB_MAX_EDGES : f->n;
    slot->freq_hz = f->freq_hz;
    slot->total_us = f->total_us;
    slot->peak = f->peak;
    slot->mod = (f->ver >= 2u && f->mod == IB_MOD_2FSK) ? IB_MOD_2FSK : IB_MOD_ASK;
    memcpy(slot->dur, f->dur, (size_t)slot->n * sizeof slot->dur[0]);
    slot->occupied = slot->n >= IB_MIN_EDGES;
    if (slot->occupied) {
        ib_decode_mod(slot->dur, slot->n, slot->first_level, slot->freq_hz,
                      slot->total_us, slot->mod, &slot->dec);
    }
}

static bool load_ibst_path(const char *path, ib_shot_t *slot) {
    static ib_store_file_t f;
    size_t n;
    if (!path || !slot) return false;
    if (!fwog_fs_open(path, false, false)) return false;
    n = sizeof f;
    const bool ok = fwog_fs_read(&f, &n);
    (void)fwog_fs_close();
    if (!ok || n != sizeof f || f.magic != IB_STORE_MAGIC ||
        (f.ver != 1u && f.ver != 2u) ||
        f.n < IB_MIN_EDGES || f.n > IB_MAX_EDGES) {
        return false;
    }
    shot_from_file(slot, &f);
    return slot->occupied;
}

static void load_store(void) {
    unsigned i;
    memset(&s_disk, 0, sizeof s_disk);
    memset(s_ring, 0, sizeof s_ring);
    s_view = 0;
    if (!fwog_fs_mounted() && !fwog_fs_mount()) {
        DIAG("[ismburst] no FatFs volume (store disabled until format)\n");
        return;
    }
    (void)dg_fs_ensure_dir("/ismburst");
    load_names();
    for (i = 0; i < IB_RING; i++) {
        if (load_ibst_path(slot_path(i), &s_ring[i])) {
            DIAG("[ismburst] loaded %s %u edges\n", k_slot_name[i],
                 (unsigned)s_ring[i].n);
        }
    }
    if (load_ibst_path(IB_STORE_NAME, &s_disk)) {
        if (!s_ring[0].occupied) s_ring[0] = s_disk;
        DIAG("[ismburst] loaded " IB_STORE_NAME " %u edges @ %u Hz\n",
             (unsigned)s_disk.n, (unsigned)s_disk.freq_hz);
    }
}

static bool write_ibst(const char *path, const ib_store_file_t *f) {
    if (!fwog_fs_open(path, true, false)) return false;
    const bool ok = fwog_fs_write(f, sizeof *f);
    (void)fwog_fs_close();
    return ok;
}

static bool save_store(const ib_shot_t *s) {
    if (s == NULL || !s->occupied || s->n < IB_MIN_EDGES) {
        DIAG("[ismburst] save: no shot\n");
        return false;
    }
    if (!fwog_fs_mounted() && !fwog_fs_mount()) {
        DIAG("[ismburst] save: no volume\n");
        return false;
    }
    static ib_store_file_t f;
    memset(&f, 0, sizeof f);
    f.magic = IB_STORE_MAGIC;
    f.ver = IB_STORE_VER;
    f.n = s->n;
    f.freq_hz = s->freq_hz;
    f.total_us = s->total_us;
    f.peak = s->peak;
    f.first_level = s->first_level ? 1u : 0u;
    f.mod = s->mod;
    memcpy(f.dur, s->dur, (size_t)s->n * sizeof s->dur[0]);
    if (!write_ibst(IB_STORE_NAME, &f)) {
        DIAG("[ismburst] save open failed\n");
        return false;
    }
    s_disk = *s;
    DIAG("[ismburst] saved " IB_STORE_NAME " %s %u edges\n",
         k_slot_name[s_view % IB_RING], (unsigned)s->n);
    if (dg_fs_ensure_dir("/ismburst")) {
        unsigned idx = dg_fs_next_index("/ismburst", "BURST");
        char name[20];
        char path[32];
        (void)write_ibst(slot_path(s_view), &f);
        dg_format_name(name, sizeof name, "BURST", idx, "BIN");
        snprintf(path, sizeof path, "/ismburst/%s", name);
        if (write_ibst(path, &f)) {
            DIAG("[ismburst] archive %s ok\n", path);
        }
    }
    return true;
}

static bool load_next_burst(void) {
    char name[48];
    char path[64];
    bool is_dir;
    unsigned i, seen, pass;
    ib_shot_t tmp;
    if (!dg_fs_ensure_dir("/ismburst")) return false;
    for (pass = 0; pass < 2u; pass++) {
        seen = 0;
        for (i = 0; i < 256u; i++) {
            unsigned idx;
            if (!fwog_fs_dir_entry("/ismburst", i, name, sizeof name, &is_dir)) break;
            if (is_dir) continue;
            idx = dg_index_from_name(name, "BURST");
            if (idx == 0u) continue;
            seen++;
            if (seen <= s_burst_i) continue;
            snprintf(path, sizeof path, "/ismburst/%s", name);
            if (!load_ibst_path(path, &tmp)) continue;
            s_burst_i = seen;
            s_ring[s_view % IB_RING] = tmp;
            DIAG("[ismburst] load %s -> %s\n", name, k_slot_name[s_view % IB_RING]);
            return true;
        }
        s_burst_i = 0;
        if (seen == 0u) break;
    }
    DIAG("[ismburst] no BURST*.BIN in /ismburst\n");
    return false;
}

static bool load_named_slot(unsigned slot) {
    ib_shot_t tmp;
    slot %= IB_RING;
    if (s_ring[slot].occupied) return true;
    if (!load_ibst_path(slot_path(slot), &tmp)) return false;
    s_ring[slot] = tmp;
    DIAG("[ismburst] load %s\n", k_slot_name[slot]);
    return true;
}

static bool capture_into(ib_shot_t *slot) {
    s_state = IB_ST_ARMED;
    listen();
    send_status(cc1101_get_rssi(&s_radio));

    const uint32_t wait_until = to_ms_since_boot(get_absolute_time()) + IB_WAIT_MS;
    uint32_t wait_st_ms = 0;
    bool last = cc1101_get_gdo0(&s_radio);
    bool saw = false;
    while (to_ms_since_boot(get_absolute_time()) < wait_until) {
        board_watchdog_kick();
        poll_link();
        if (s_cmd_fresh &&
            (s_cmd.cmd == IB_CMD_ABORT || s_cmd.cmd == IB_CMD_LISTEN ||
             s_cmd.cmd == IB_CMD_CLEAR)) {
            s_hunt = false;
            s_wait_s = 0u;
            return false;
        }
        const bool now = cc1101_get_gdo0(&s_radio);
        const int16_t rssi = cc1101_get_rssi(&s_radio);
        if (now != last && rssi > -85) {
            saw = true;
            last = now;
            break;
        }
        const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        {
            uint32_t left = (wait_until > now_ms) ? (wait_until - now_ms) : 0u;
            s_wait_s = (uint8_t)((left + 999u) / 1000u);
        }
        if (now_ms - wait_st_ms >= 200u) {
            wait_st_ms = now_ms;
            send_status(rssi);
        }
        sleep_ms(2);
    }
    if (!saw) {
        DIAG("[ismburst] arm timed out\n");
        s_wait_s = 0u;
        return false;
    }

    s_wait_s = 0u;
    s_state = IB_ST_CAPTURE;
    send_status(cc1101_get_rssi(&s_radio));
    memset(slot, 0, sizeof *slot);
    slot->first_level = cc1101_get_gdo0(&s_radio);
    slot->freq_hz = s_cmd.freq_hz;
    slot->mod = (s_cmd.mod == IB_MOD_2FSK) ? IB_MOD_2FSK : IB_MOD_ASK;
    slot->peak = cc1101_get_rssi(&s_radio);
    uint32_t t = time_us_32();
    last = slot->first_level;
    const uint32_t cap_until = to_ms_since_boot(get_absolute_time()) + IB_CAPTURE_MS;
    uint32_t last_kick = to_ms_since_boot(get_absolute_time());

    while (to_ms_since_boot(get_absolute_time()) < cap_until &&
           slot->n < IB_MAX_EDGES) {
        const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (now_ms - last_kick > 400u) {
            board_watchdog_kick();
            last_kick = now_ms;
            const int16_t rssi = cc1101_get_rssi(&s_radio);
            if (rssi > slot->peak) slot->peak = rssi;
        }
        const bool bit = cc1101_get_gdo0(&s_radio);
        const uint32_t now = time_us_32();
        const uint32_t dt = now - t;
        if (bit != last) {
            const uint16_t d = (uint16_t)(dt > 65535u ? 65535u : dt);
            slot->dur[slot->n++] = d;
            slot->total_us += d;
            t = now;
            last = bit;
        } else if (slot->n >= IB_MIN_EDGES && dt > IB_GAP_US) {
            break;
        }
    }
    if (slot->n < IB_MIN_EDGES) {
        DIAG("[ismburst] dropped short burst (%u edges)\n", (unsigned)slot->n);
        return false;
    }
    slot->occupied = true;
    return true;
}

static void commit_shot(ib_shot_t *tmp) {
    s_state = IB_ST_DECODE;
    send_status(tmp->peak);
    ib_decode_mod(tmp->dur, tmp->n, tmp->first_level, tmp->freq_hz, tmp->total_us,
                  tmp->mod, &tmp->dec);
    s_ring[s_view % IB_RING] = *tmp;
    DIAG("[ismburst] %s %s coding=%u bits=%u hex=",
         k_slot_name[s_view % IB_RING], tmp->dec.label,
         (unsigned)tmp->dec.coding, (unsigned)tmp->dec.bits);
    for (unsigned i = 0; i < tmp->dec.hex_n && i < IB_HEX_BYTES; i++) {
        DIAG("%02X", (unsigned)tmp->dec.hex[i]);
    }
    DIAG(" pw=%u/%u gap=%u/%u peak=%d dur=%u us\n",
         (unsigned)tmp->dec.pw_short_us, (unsigned)tmp->dec.pw_long_us,
         (unsigned)tmp->dec.gap_short_us, (unsigned)tmp->dec.gap_long_us,
         (int)tmp->peak, (unsigned)tmp->total_us);
}

static bool replay_edges(const ib_shot_t *s) {
    if (s == NULL || !s->occupied || s->n < IB_MIN_EDGES) {
        DIAG("[ismburst] replay: no store\n");
        return false;
    }
    if (s->freq_hz != s_tuned || s->mod != s_mod) {
        if (!configure_radio(s->freq_hz, s->mod)) return false;
    }
    s_state = IB_ST_REPLAY;
    send_status(s->peak);

    (void)cc1101_idle(&s_radio);
    (void)cc1101_write_reg(&s_radio, CC1101_REG_IOCFG0, 0x2Eu);
    gdo0_as_output(s->first_level);
    (void)cc1101_write_reg(&s_radio, CC1101_REG_IOCFG0, 0x0Du);
    if (!cc1101_tx(&s_radio)) {
        DIAG("[ismburst] TX enter failed\n");
        gdo0_as_input();
        return false;
    }

    for (unsigned frame = 0; frame < IB_TX_FRAMES; frame++) {
        board_watchdog_kick();
        wait_us(400);
        bool level = s->first_level;
        for (uint16_t e = 0; e < s->n; e++) {
            gpio_put(s_radio.gdo0_pin, level);
            wait_us(s->dur[e]);
            level = !level;
            if ((e & 127u) == 0u) board_watchdog_kick();
        }
        gpio_put(s_radio.gdo0_pin, 0);
        if (frame + 1u < IB_TX_FRAMES) sleep_ms(IB_TX_GAP_MS);
    }

    (void)cc1101_idle(&s_radio);
    gdo0_as_input();
    (void)cc1101_write_reg(&s_radio, CC1101_REG_IOCFG0, 0x0Du);
    DIAG("[ismburst] replayed %u edges x%u (owned ISM gadget; not a jammer)\n",
         (unsigned)s->n, (unsigned)IB_TX_FRAMES);
    return true;
}

int main(void) {
    board_init();
    const fwog_display_result_t d = fwog_display_update_run();
    DIAG("[ismburst] display: %s\n", fwog_display_result_text(d));

    s_cmd.type = IB_MSG_CMD;
    s_cmd.cmd = IB_CMD_LISTEN;
    s_cmd.freq_hz = 433920000u;
    s_cmd.mod = IB_MOD_ASK;

    load_store();

    cc1101_bus_init(IB_SPI_HZ);
    cc1101_bind(&s_radio, CC1101_RADIO_CS0);
    s_ok = cc1101_bringup(&s_radio) && cc1101_probe(&s_radio);
    DIAG("[ismburst] radio0=%s\n", s_ok ? "ok" : "fail");

    fwog_link_rx_init(&s_rx);

    if (s_ok && configure_radio(s_cmd.freq_hz, s_cmd.mod)) {
        listen();
        have_or_idle();
    } else {
        s_state = IB_ST_ERROR;
    }

    static ib_shot_t tmp;
    absolute_time_t next_st = make_timeout_time_ms(200);

    while (true) {
        board_watchdog_kick();
        poll_link();

        if (s_name_fresh) {
            s_name_fresh = false;
            apply_name_msg();
            send_status(s_ok ? cc1101_get_rssi(&s_radio) : -128);
        }

        if (s_cmd_fresh) {
            const uint8_t cmd = s_cmd.cmd;
            s_cmd_fresh = false;
            if (!s_ok) {
                s_state = IB_ST_ERROR;
            } else if ((s_cmd.freq_hz != s_tuned || s_cmd.mod != s_mod) &&
                       cmd != IB_CMD_REPLAY && cmd != IB_CMD_SAVE &&
                       cmd != IB_CMD_LOAD) {
                if (configure_radio(s_cmd.freq_hz, s_cmd.mod)) {
                    listen();
                    have_or_idle();
                } else {
                    s_state = IB_ST_ERROR;
                }
            }
            if (s_ok && s_state != IB_ST_ERROR) {
                if (cmd == IB_CMD_ARM || cmd == IB_CMD_HUNT) {
                    s_hunt = (cmd == IB_CMD_HUNT);
                    for (;;) {
                        if (!capture_into(&tmp)) break;
                        commit_shot(&tmp);
                        if (!s_hunt) break;
                        {
                            unsigned n;
                            uint8_t next = 0xFFu;
                            for (n = 1u; n <= IB_RING; n++) {
                                uint8_t j = (uint8_t)((s_view + n) % IB_RING);
                                if (!s_ring[j].occupied) {
                                    next = j;
                                    break;
                                }
                            }
                            if (next == 0xFFu) break;
                            s_view = next;
                        }
                    }
                    s_hunt = false;
                    s_wait_s = 0u;
                    listen();
                    have_or_idle();
                } else if (cmd == IB_CMD_CLEAR) {
                    s_hunt = false;
                    s_wait_s = 0u;
                    memset(s_ring, 0, sizeof s_ring);
                    listen();
                    have_or_idle();
                    DIAG("[ismburst] ring cleared\n");
                } else if (cmd == IB_CMD_PAGE) {
                    s_view = (uint8_t)(s_cmd.slot % IB_RING);
                    (void)load_named_slot(s_view);
                    have_or_idle();
                } else if (cmd == IB_CMD_LOAD) {
                    s_view = (uint8_t)(s_cmd.slot % IB_RING);
                    (void)load_next_burst();
                    have_or_idle();
                } else if (cmd == IB_CMD_SAVE) {
                    (void)save_store(view_shot());
                    have_or_idle();
                } else if (cmd == IB_CMD_REPLAY) {
                    (void)replay_edges(view_shot());
                    if (s_cmd.freq_hz != s_tuned || s_cmd.mod != s_mod) {
                        (void)configure_radio(s_cmd.freq_hz, s_cmd.mod);
                    }
                    listen();
                    have_or_idle();
                } else {
                    listen();
                    have_or_idle();
                }
            }
            send_status(s_ok ? cc1101_get_rssi(&s_radio) : -128);
        }

        if (s_ok && (s_state == IB_ST_IDLE || s_state == IB_ST_HAVE)) {
            const int16_t rssi = cc1101_get_rssi(&s_radio);
            const uint32_t now = to_ms_since_boot(get_absolute_time());
            if (!s_in && rssi > -72) {
                s_in = true;
                s_t0 = now;
            } else if (s_in && rssi < -88) {
                s_in = false;
                if (now - s_t0 >= 3u) s_bursts++;
            }
        }

        if (time_reached(next_st)) {
            next_st = make_timeout_time_ms(200);
            send_status(s_ok ? cc1101_get_rssi(&s_radio) : -128);
        }
        sleep_ms(2);
    }
}
