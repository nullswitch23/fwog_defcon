#include "fwog_main.h"
#include "ed_jobs.h"
#include "ed_proto.h"

#include "tf_proto.h"
#include "pico/stdlib.h"
#include <string.h>



#define TF_HZ_DEFAULT 433920000u
#define TF_SPI_HZ     1000000u

static cc1101_t       s_r[2];
static bool           s_ok[2];
static fwog_link_rx_t s_rx;
static bool           s_beacon;
static uint8_t        s_seq;
static uint32_t       s_hz = TF_HZ_DEFAULT;
static absolute_time_t s_next_tx;

static bool listen_ook(cc1101_t *r, uint32_t hz) {
    if (!cc1101_freq_in_band(hz)) return false;
    bool ok = cc1101_idle(r);
    ok = ok && cc1101_set_frequency(r, hz);
    ok = ok && cc1101_set_modulation(r, CC1101_MOD_ASK);
    ok = ok && cc1101_set_rx_bandwidth(r, 270.0f);
    ok = ok && cc1101_rx(r);
    return ok;
}

static void retune(uint32_t hz) {
    if (!tf_hz_ok(hz) || !cc1101_freq_in_band(hz)) return;
    s_hz = hz;
    if (s_ok[1]) (void)listen_ook(&s_r[1], s_hz);
    if (s_ok[0] && !s_beacon) (void)listen_ook(&s_r[0], s_hz);
    DIAG("[twinfox] hz %u\n", (unsigned)s_hz);
}

static void send_status(void) {
    tf_status_t m;
    memset(&m, 0, sizeof m);
    m.type = TF_MSG_ST;
    m.beacon = s_beacon ? 1u : 0u;
    m.ok0 = s_ok[0] ? 1u : 0u;
    m.ok1 = s_ok[1] ? 1u : 0u;
    m.rssi0 = s_ok[0] ? cc1101_get_rssi(&s_r[0]) : -127;
    m.rssi1 = s_ok[1] ? cc1101_get_rssi(&s_r[1]) : -127;
    m.freq_hz = s_hz;
    (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static bool s_tf_live;

void ed_tf_job_leave(void) {
    s_tf_live = false;
    if (s_ok[0]) (void)cc1101_idle(&s_r[0]);
    if (s_ok[1]) (void)cc1101_idle(&s_r[1]);
}

void ed_tf_job_enter(void) {
    s_tf_live = true;


    cc1101_bus_init(TF_SPI_HZ);
    cc1101_bind(&s_r[0], CC1101_RADIO_CS0);
    cc1101_bind(&s_r[1], CC1101_RADIO_CS1);
    s_ok[0] = cc1101_bringup(&s_r[0]) && cc1101_probe(&s_r[0]);
    s_ok[1] = cc1101_bringup(&s_r[1]) && cc1101_probe(&s_r[1]);
    retune(s_hz);
    DIAG("[twinfox] r0=%s r1=%s\n", s_ok[0] ? "ok" : "fail", s_ok[1] ? "ok" : "fail");
    fwog_link_rx_init(&s_rx);
    s_next_tx = make_timeout_time_ms(400);
}

void ed_tf_job_tick(void) {
    if (!s_tf_live) return;

        board_watchdog_kick();
        uint8_t b;
        size_t n;
        while (fwog_link_uart_read(&b)) {
            if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
            ed_note_sel(s_rx.buf, n);
            if (n >= sizeof(tf_cmd_t) && s_rx.buf[0] == TF_MSG_CMD) {
                tf_cmd_t c;
                memcpy(&c, s_rx.buf, sizeof c);
                if (c.freq_hz) retune(c.freq_hz);
                s_beacon = (c.cmd == TF_CMD_BEACON);
                if (!s_beacon && s_ok[0]) (void)listen_ook(&s_r[0], s_hz);
            }
        }
        if (s_beacon && s_ok[0] && time_reached(s_next_tx)) {
            s_next_tx = make_timeout_time_ms(400);
            uint8_t pkt[5] = { 'F', 'W', 'O', 'G', s_seq++ };
            (void)cc1101_idle(&s_r[0]);
            (void)cc1101_set_frequency(&s_r[0], s_hz);
            (void)cc1101_set_power(&s_r[0], 10);
            (void)cc1101_send_packet(&s_r[0], pkt, sizeof pkt);
            (void)listen_ook(&s_r[0], s_hz);
        }
        send_status();
        sleep_ms(80);
    
}
