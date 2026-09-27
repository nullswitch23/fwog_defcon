#include "fwog_main.h"
#include "bs_proto.h"
#include "pico/stdlib.h"
#include <string.h>

FWOG_WATCHDOG_DEFAULT();

#define BS_SPI_HZ 1000000u

static const uint32_t k_f0[3]   = { 300000000u, 387000000u, 779000000u };
static const uint32_t k_span[3] = {  48000000u,  77000000u, 149000000u };

static cc1101_t       s_radio;
static bool           s_ok;
static fwog_link_rx_t s_rx;
static uint8_t        s_band = 1;
static uint8_t        s_mode = BS_MODE_SWEEP;
static uint8_t        s_resume = BS_MODE_SWEEP;
static uint8_t        s_hunt_i;
static uint8_t        s_bar[BS_BINS];
static int16_t        s_peak = -120;
static uint16_t       s_peak_bin;
static uint32_t       s_peak_hz;
static bs_hit_t       s_hit[BS_TOP];
static uint8_t        s_n_hits;

static void hits_clear(void) {
    memset(s_hit, 0, sizeof s_hit);
    s_n_hits = 0;
}

static void hits_sort(void) {
    for (uint8_t i = 0; i < s_n_hits; i++) {
        for (uint8_t j = (uint8_t)(i + 1u); j < s_n_hits; j++) {
            if (s_hit[j].dbm > s_hit[i].dbm) {
                const bs_hit_t t = s_hit[i];
                s_hit[i] = s_hit[j];
                s_hit[j] = t;
            }
        }
    }
}

static void consider_hit(uint32_t hz, int16_t dbm, uint8_t band) {
    int slot = -1;
    for (uint8_t i = 0; i < s_n_hits; i++) {
        if (s_hit[i].hz == hz) {
            slot = (int)i;
            break;
        }
    }
    if (slot >= 0) {
        if (dbm <= s_hit[slot].dbm) return;
        s_hit[slot].dbm = dbm;
        s_hit[slot].band = band;
    } else if (s_n_hits < BS_TOP) {
        s_hit[s_n_hits].hz = hz;
        s_hit[s_n_hits].dbm = dbm;
        s_hit[s_n_hits].band = band;
        s_hit[s_n_hits]._pad = 0;
        s_n_hits++;
    } else {
        uint8_t weakest = 0;
        for (uint8_t i = 1; i < BS_TOP; i++) {
            if (s_hit[i].dbm < s_hit[weakest].dbm) weakest = i;
        }
        if (dbm <= s_hit[weakest].dbm) return;
        s_hit[weakest].hz = hz;
        s_hit[weakest].dbm = dbm;
        s_hit[weakest].band = band;
        s_hit[weakest]._pad = 0;
    }
    hits_sort();
}

static bool configure_ook(uint32_t hz) {
    if (!cc1101_freq_in_band(hz)) return false;
    bool ok = cc1101_idle(&s_radio);
    ok = ok && cc1101_set_frequency(&s_radio, hz);
    ok = ok && cc1101_set_modulation(&s_radio, CC1101_MOD_ASK);
    ok = ok && cc1101_set_pkt_format(&s_radio, 3u);
    ok = ok && cc1101_set_sync_mode(&s_radio, 0u);
    ok = ok && cc1101_set_crc(&s_radio, false);
    ok = ok && cc1101_set_rx_bandwidth(&s_radio, 270.0f);
    ok = ok && cc1101_set_data_rate(&s_radio, 10.0f);
    ok = ok && cc1101_rx(&s_radio);
    return ok;
}

static uint8_t active_band(void) {
    if (s_mode == BS_MODE_HUNT) return (uint8_t)(s_hunt_i % 3u);
    return (uint8_t)(s_band % 3u);
}

static void send_status(void) {
    bs_status_t m;
    memset(&m, 0, sizeof m);
    m.type = BS_MSG_ST;
    m.band = active_band();
    m.ok = s_ok ? 1u : 0u;
    m.mode = s_mode;
    m.f0_hz = k_f0[m.band];
    m.step_hz = k_span[m.band] / (BS_BINS - 1u);
    m.peak_hz = s_peak_hz;
    m.peak_dbm = s_peak;
    m.peak_bin = s_peak_bin;
    m.n_hits = s_n_hits;
    memcpy(m.hit, s_hit, sizeof m.hit);
    memcpy(m.bar, s_bar, BS_BINS);
    (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void sweep_band(uint8_t band) {
    const uint32_t f0 = k_f0[band];
    const uint32_t step = k_span[band] / (BS_BINS - 1u);
    s_peak = -120;
    s_peak_bin = 0;
    s_peak_hz = f0;
    for (unsigned i = 0; i < BS_BINS; i++) {
        board_watchdog_kick();
        const uint32_t hz = f0 + step * i;
        if (!configure_ook(hz)) {
            s_bar[i] = 0;
            continue;
        }
        sleep_ms(4);
        const int16_t rssi = cc1101_get_rssi(&s_radio);
        int h = (int)rssi + 110;
        if (h < 0) h = 0;
        if (h > 80) h = 80;
        s_bar[i] = (uint8_t)h;
        consider_hit(hz, rssi, band);
        if (rssi > s_peak) {
            s_peak = rssi;
            s_peak_bin = (uint16_t)i;
            s_peak_hz = hz;
        }
    }
}

static void handle_cmd(const bs_cmd_t *c) {
    const uint8_t band = (uint8_t)(c->band % 3u);
    bool changed = true;
    switch (c->cmd) {
    case BS_CMD_SWEEP:
        /* Keep top-5 across band switches; RED / BS_CMD_CLEAR wipes it. */
        s_band = band;
        s_mode = BS_MODE_SWEEP;
        s_resume = BS_MODE_SWEEP;
        break;
    case BS_CMD_FREEZE:
        if (s_mode == BS_MODE_FREEZE) {
            s_mode = s_resume;
        } else {
            s_resume = s_mode;
            s_mode = BS_MODE_FREEZE;
        }
        break;
    case BS_CMD_HUNT:
        if (s_mode == BS_MODE_HUNT ||
            (s_mode == BS_MODE_FREEZE && s_resume == BS_MODE_HUNT)) {
            s_band = band;
            s_mode = BS_MODE_SWEEP;
            s_resume = BS_MODE_SWEEP;
        } else {
            s_hunt_i = 0;
            s_mode = BS_MODE_HUNT;
            s_resume = BS_MODE_HUNT;
        }
        break;
    case BS_CMD_CLEAR:
        hits_clear();
        break;
    default:
        changed = false;
        break;
    }
    if (changed) send_status();
}

int main(void) {
    board_init();
    const fwog_display_result_t d = fwog_display_update_run();
    DIAG("[bandscope] display: %s\n", fwog_display_result_text(d));

    cc1101_bus_init(BS_SPI_HZ);
    cc1101_bind(&s_radio, CC1101_RADIO_CS0);
    s_ok = cc1101_bringup(&s_radio) && cc1101_probe(&s_radio);
    DIAG("[bandscope] radio0=%s\n", s_ok ? "ok" : "fail");
    fwog_link_rx_init(&s_rx);

    while (true) {
        board_watchdog_kick();
        uint8_t b;
        size_t n;
        while (fwog_link_uart_read(&b)) {
            if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
            if (n >= sizeof(bs_cmd_t) && s_rx.buf[0] == BS_MSG_CMD) {
                bs_cmd_t c;
                memcpy(&c, s_rx.buf, sizeof c);
                handle_cmd(&c);
            }
        }
        if (!s_ok) {
            send_status();
            sleep_ms(50);
            continue;
        }
        if (s_mode == BS_MODE_FREEZE) {
            sleep_ms(20);
            continue;
        }
        const uint8_t band = active_band();
        sweep_band(band);
        send_status();
        if (s_mode == BS_MODE_HUNT) s_hunt_i = (uint8_t)((s_hunt_i + 1u) % 3u);
        sleep_ms(20);
    }
}
