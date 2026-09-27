/* CC1101 async TPMS: GDO0 edge capture, OEM profiles, parking-lot scan log
 * (RAM only — main FatFs is available but scan results are not persisted). */
#include "fwog_main.h"
#include "ed_jobs.h"
#include "ed_proto.h"

#include "te_proto.h"
#include "te_tpms.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <string.h>



#define TE_MIN_EDGES 24u
#define TE_CAP_MS    120u
#define TE_RSSI_HI   -72
#define TE_RSSI_LO   -88

static const uint32_t k_hz[2] = { 315000000u, 433920000u };

static cc1101_t       s_radio;
static bool           s_ok;
static fwog_link_rx_t s_rx;
static uint8_t        s_preset;
static bool           s_fsk;
static uint8_t        s_profile = 1u;
static uint8_t        s_mode = TE_MODE_LISTEN;
static bool           s_scan_on;
static uint32_t       s_scan_end;
static te_scan_ent_t  s_log[TE_SCAN_MAX];
static unsigned       s_log_n;
static uint8_t        s_cursor;
static bool           s_in;
static uint16_t       s_bursts;
static int16_t        s_last_rssi;
static uint16_t       s_last_ms;
static uint32_t       s_t0;
static te_decode_t    s_last_dec;
static bool           s_row_dirty;

static void gdo0_in(void) {
    gpio_set_dir(s_radio.gdo0_pin, GPIO_IN);
    gpio_pull_up(s_radio.gdo0_pin);
}

static bool tune(uint32_t hz, bool fsk) {
    if (!cc1101_freq_in_band(hz)) return false;
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
    ok = ok && cc1101_set_data_rate(&s_radio, fsk ? 4.0f : 3.6f);
    if (fsk) ok = ok && cc1101_set_deviation(&s_radio, 25.4f);
    ok = ok && cc1101_set_power(&s_radio, 10);
    ok = ok && cc1101_write_reg(&s_radio, CC1101_REG_IOCFG0, 0x0Du);
    ok = ok && cc1101_write_reg(&s_radio, CC1101_REG_AGCCTRL2, 0x07u);
    ok = ok && cc1101_write_reg(&s_radio, CC1101_REG_AGCCTRL1, 0x00u);
    ok = ok && cc1101_write_reg(&s_radio, CC1101_REG_AGCCTRL0, 0x91u);
    gdo0_in();
    ok = ok && cc1101_flush_rx(&s_radio);
    ok = ok && cc1101_rx(&s_radio);
    return ok;
}

static void listen(void) {
    gdo0_in();
    (void)cc1101_write_reg(&s_radio, CC1101_REG_IOCFG0, 0x0Du);
    (void)cc1101_idle(&s_radio);
    (void)cc1101_flush_rx(&s_radio);
    (void)cc1101_rx(&s_radio);
}

static bool capture_burst(uint16_t *dur, uint16_t cap, uint16_t *n,
                          bool *first_level, int16_t *peak) {
    const int16_t r0 = cc1101_get_rssi(&s_radio);
    if (r0 < TE_RSSI_HI - 5) return false;
    *peak = r0;
    *n = 0;
    *first_level = cc1101_get_gdo0(&s_radio);
    bool last = *first_level;
    uint32_t t = time_us_32();
    const uint32_t end_ms = to_ms_since_boot(get_absolute_time()) + TE_CAP_MS;
    while (to_ms_since_boot(get_absolute_time()) < end_ms && *n < cap) {
        board_watchdog_kick();
        const int16_t rssi = cc1101_get_rssi(&s_radio);
        if (rssi > *peak) *peak = rssi;
        const bool bit = cc1101_get_gdo0(&s_radio);
        const uint32_t now = time_us_32();
        const uint32_t dt = now - t;
        if (bit != last) {
            dur[(*n)++] = (uint16_t)(dt > 65535u ? 65535u : dt);
            t = now;
            last = bit;
        } else if (*n >= TE_MIN_EDGES && dt > TE_GAP_US) {
            break;
        }
    }
    return *n >= TE_MIN_EDGES;
}

static void handle_burst(int16_t peak_rssi) {
    uint16_t dur[TE_MAX_EDGES];
    uint16_t n;
    bool first;
    int16_t pk = peak_rssi;
    if (!capture_burst(dur, TE_MAX_EDGES, &n, &first, &pk)) return;

    const uint16_t bit_us =
        te_guess_bit_us(dur, n, k_hz[s_preset], s_fsk);
    uint8_t bits[TE_RAW_BITS];
    const unsigned nb =
        te_edges_to_bits(dur, n, first, bit_us, bits, TE_RAW_BITS);
    te_decode_t dec;
    const uint8_t try_prof = s_profile;
    if (te_tpms_decode(try_prof, bits, nb, &dec)) {
        s_last_dec = dec;
    } else {
        memset(&s_last_dec, 0, sizeof s_last_dec);
        s_last_dec.raw_n = dec.raw_n;
        memcpy(s_last_dec.raw, dec.raw, dec.raw_n);
        s_last_dec.bits_n = dec.bits_n;
        memcpy(s_last_dec.bits, dec.bits, dec.bits_n);
    }
    s_bursts++;
    s_last_rssi = pk;

    if (s_mode == TE_MODE_SCAN && s_scan_on) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        te_scan_merge(s_log, TE_SCAN_MAX, &s_log_n, &s_last_dec, pk, now);
        s_row_dirty = true;
    }

    {
        uint8_t marc = 0;
        if (cc1101_read_reg(&s_radio, CC1101_STATUS_MARCSTATE, &marc))
            (void)cc1101_decode_status(marc);
    }
    listen();
}

static void send_row(uint8_t idx) {
    if (idx >= s_log_n) return;
    const te_scan_ent_t *e = &s_log[idx];
    te_row_t row;
    memset(&row, 0, sizeof row);
    row.type = TE_MSG_ROW;
    row.idx = idx;
    memcpy(row.id, e->id, 4);
    row.id_len = e->id_len;
    row.psi_x10 = e->psi_x10;
    row.temp_c = e->temp_c;
    row.peak_rssi = e->peak_rssi;
    row.bursts = e->bursts;
    row.first_s = e->first_ms;
    row.last_s = e->last_ms;
    row.profile = e->profile;
    row.crc_ok = e->crc_ok;
    row.raw_n = e->raw_n > 9u ? 9u : e->raw_n;
    memcpy(row.raw, e->raw, row.raw_n);
    (void)fwog_link_uart_send_frame(&row, sizeof row);
}

static void send_status(int16_t rssi) {
    te_status_t st;
    memset(&st, 0, sizeof st);
    st.type = TE_MSG_ST;
    st.preset = (uint8_t)((s_preset ? TE_PRESET_FREQ : 0u) |
                          (s_fsk ? TE_PRESET_FSK : 0u));
    st.ok = s_ok ? 1u : 0u;
    st.mode = s_mode;
    st.profile = s_profile;
    st.scan_on = s_scan_on ? 1u : 0u;
    st.in_burst = s_in ? 1u : 0u;
    st.crc_ok = s_last_dec.ok ? 1u : 0u;
    st.rssi = rssi;
    st.last_rssi = s_last_rssi;
    st.bursts = s_bursts;
    st.last_ms = s_last_ms;
    st.freq_hz = k_hz[s_preset];
    st.scan_n = (uint8_t)(s_log_n > 255u ? 255u : s_log_n);
    st.cursor = s_cursor;
    st.last_psi_x10 = s_last_dec.psi_x10;
    st.last_temp_c = s_last_dec.temp_c;
    if (s_last_dec.id_len) memcpy(st.last_id, s_last_dec.id, 4);
    st.raw_n = s_last_dec.raw_n > 9u ? 9u : s_last_dec.raw_n;
    memcpy(st.raw_hex, s_last_dec.raw, st.raw_n);
    if (s_scan_on) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        st.scan_left_ms = (s_scan_end > now) ? (uint16_t)(s_scan_end - now) : 0u;
    }
    (void)fwog_link_uart_send_frame(&st, sizeof st);
    if (s_row_dirty || s_mode == TE_MODE_REVIEW) {
        send_row(s_cursor);
        s_row_dirty = false;
    }
}

static void poll_cmd(void) {
    uint8_t b;
    size_t n;
    while (fwog_link_uart_read(&b)) {
        if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
        ed_note_sel(s_rx.buf, n);
        if (n < sizeof(te_cmd_t) || s_rx.buf[0] != TE_MSG_CMD) continue;
        te_cmd_t c;
        memcpy(&c, s_rx.buf, sizeof c);
        switch (c.cmd) {
        case TE_CMD_PRESET:
            s_preset = c.preset & TE_PRESET_FREQ ? 1u : 0u;
            s_fsk = (c.preset & TE_PRESET_FSK) != 0u;
            if (s_ok) (void)tune(k_hz[s_preset], s_fsk);
            break;
        case TE_CMD_PROFILE:
            s_profile = (uint8_t)((s_profile + 1u) % TE_PROFILE_N);
            break;
        case TE_CMD_SCAN:
            if (c.on) {
                s_mode = TE_MODE_SCAN;
                s_scan_on = true;
                s_scan_end = to_ms_since_boot(get_absolute_time()) + TE_SCAN_MS;
                s_log_n = 0;
                s_cursor = 0;
                s_bursts = 0;
                s_row_dirty = true;
            } else {
                s_scan_on = false;
                s_mode = TE_MODE_REVIEW;
                s_row_dirty = true;
            }
            break;
        case TE_CMD_CURSOR:
            if (s_log_n == 0u) break;
            if (c.on)
                s_cursor = (uint8_t)((s_cursor + 1u) % s_log_n);
            else
                s_cursor = (uint8_t)((s_cursor + s_log_n - 1u) % s_log_n);
            s_row_dirty = true;
            break;
        case TE_CMD_MODE:
            s_mode = c.on;
            if (s_mode == TE_MODE_LISTEN) {
                s_scan_on = false;
            }
            break;
        default:
            break;
        }
    }
}

static bool s_te_live;

void ed_te_job_leave(void) {
    s_te_live = false;
    if (s_ok) (void)cc1101_idle(&s_radio);
    s_scan_on = false;
}

void ed_te_job_enter(void) {
    s_te_live = true;

    cc1101_bus_init(1000000u);
    cc1101_bind(&s_radio, CC1101_RADIO_CS0);
    s_ok = cc1101_bringup(&s_radio) && cc1101_probe(&s_radio);
    if (s_ok) (void)tune(k_hz[0], false);
    fwog_link_rx_init(&s_rx);
}

void ed_te_job_tick(void) {
    if (!s_te_live) return;

        board_watchdog_kick();
        poll_cmd();
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        if (s_scan_on && now >= s_scan_end) {
            s_scan_on = false;
            s_mode = TE_MODE_REVIEW;
            s_row_dirty = true;
        }

        const int16_t rssi = s_ok ? cc1101_get_rssi(&s_radio) : -127;
        if (!s_in && rssi > TE_RSSI_HI) {
            s_in = true;
            s_t0 = now;
            s_last_rssi = rssi;
            handle_burst(rssi);
        } else if (s_in && rssi < TE_RSSI_LO) {
            s_in = false;
            s_last_ms = (uint16_t)(now - s_t0);
        } else if (s_in && rssi > s_last_rssi) {
            s_last_rssi = rssi;
        }

        send_status(rssi);
        sleep_ms(25);
    
}
