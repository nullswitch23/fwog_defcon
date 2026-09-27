/* ChirpMail — two OGs, CC1101 packet text. Radio 0 TX/RX at 433.92. */
#include "fwog_main.h"
#include "cm_proto.h"
#include "cm_store.h"
#include "pico/stdlib.h"
#include <string.h>

FWOG_WATCHDOG_DEFAULT();

#define CM_HZ     433920000u
#define CM_SPI_HZ 1000000u

static cc1101_t       s_r;
static bool           s_ok;
static fwog_link_rx_t s_rx;
static uint8_t        s_seq;
static uint8_t        s_nrx;
static int16_t        s_rssi = -127;
static bool           s_tx_ok = true;

static bool radio_listen(void) {
    if (!s_ok) return false;
    bool ok = cc1101_idle(&s_r);
    ok = ok && cc1101_set_frequency(&s_r, CM_HZ);
    ok = ok && cc1101_set_modulation(&s_r, CC1101_MOD_2FSK);
    ok = ok && cc1101_set_data_rate(&s_r, 4.8f);
    ok = ok && cc1101_set_crc(&s_r, true);
    ok = ok && cc1101_set_packet_length(&s_r, CM_RF_LEN);
    ok = ok && cc1101_set_power(&s_r, 10);
    ok = ok && cc1101_flush_rx(&s_r);
    ok = ok && cc1101_rx(&s_r);
    return ok;
}

static void send_st(void) {
    cm_st_t m;
    memset(&m, 0, sizeof m);
    m.type = CM_MSG_ST;
    m.ok = s_ok ? 1u : 0u;
    m.tx_ok = s_tx_ok ? 1u : 0u;
    m.nrx = s_nrx;
    m.rssi = s_rssi;
    m.seq = s_seq;
    (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void send_rf(const char *text, uint8_t n) {
    uint8_t pkt[CM_RF_LEN];
    memset(pkt, 0, sizeof pkt);
    pkt[0] = (uint8_t)CM_RF_MAGIC0;
    pkt[1] = (uint8_t)CM_RF_MAGIC1;
    pkt[2] = s_seq;
    pkt[3] = n > CM_TEXT ? (uint8_t)CM_TEXT : n;
    if (text && pkt[3]) {
        memcpy(&pkt[4], text, pkt[3]);
    }
    (void)cc1101_idle(&s_r);
    (void)cc1101_set_frequency(&s_r, CM_HZ);
    (void)cc1101_set_modulation(&s_r, CC1101_MOD_2FSK);
    (void)cc1101_set_crc(&s_r, true);
    (void)cc1101_set_packet_length(&s_r, CM_RF_LEN);
    (void)cc1101_set_power(&s_r, 10);
    s_tx_ok = cc1101_send_packet(&s_r, pkt, CM_RF_LEN);
    s_seq++;
    DIAG("[chirpmail] tx n=%u ok=%d\n", (unsigned)pkt[3], (int)s_tx_ok);
    (void)radio_listen();
}

static void poll_rf(void) {
    if (!s_ok) return;
    const int raw = cc1101_rx_bytes_available(&s_r);
    if (raw <= 0 || (raw & 0x7F) == 0) return;
    uint8_t buf[40];
    const unsigned n = (unsigned)(raw & 0x7F);
    const unsigned want = n > sizeof buf ? (unsigned)sizeof buf : n;
    if (cc1101_receive_packet(&s_r, buf, (uint8_t)want) < 0) {
        (void)radio_listen();
        return;
    }
    /* Variable/fixed: length byte then payload, optional status. */
    unsigned off = 0;
    if (want >= 1u && buf[0] == CM_RF_LEN) off = 1u;
    if (want < off + 4u) {
        (void)radio_listen();
        return;
    }
    if (buf[off] != (uint8_t)CM_RF_MAGIC0 ||
        buf[off + 1u] != (uint8_t)CM_RF_MAGIC1) {
        (void)radio_listen();
        return;
    }
    cm_rx_t m;
    memset(&m, 0, sizeof m);
    m.type = CM_MSG_RX;
    m.seq = buf[off + 2u];
    m.n = buf[off + 3u];
    if (m.n > CM_TEXT) m.n = (uint8_t)CM_TEXT;
    memcpy(m.text, &buf[off + 4u], m.n);
    if (want >= off + CM_RF_LEN + 2u) {
        m.rssi = (int8_t)buf[off + CM_RF_LEN];
    }
    s_nrx++;
    s_rssi = (int16_t)m.rssi;
    (void)fwog_link_uart_send_frame(&m, sizeof m);
    DIAG("[chirpmail] rx n=%u seq=%u\n", (unsigned)m.n, (unsigned)m.seq);
    (void)radio_listen();
}

int main(void) {
    board_init();
    const fwog_display_result_t d = fwog_display_update_run();
    DIAG("[chirpmail] display: %s\n", fwog_display_result_text(d));

    for (int i = 0; i < 20; i++) {
        board_watchdog_kick();
        uint8_t b;
        while (fwog_link_uart_read(&b)) { /* discard; display may PULL later */ }
        sleep_ms(200);
    }

    board_watchdog_kick();
    cc1101_bus_init(CM_SPI_HZ);
    cc1101_bind(&s_r, CC1101_RADIO_CS0);
    s_ok = cc1101_bringup(&s_r) && cc1101_probe(&s_r) && radio_listen();
    DIAG("[chirpmail] radio0=%s\n", s_ok ? "ok" : "fail");
    fwog_link_rx_init(&s_rx);
    board_watchdog_kick();
    cm_store_init();

    while (true) {
        board_watchdog_kick();
        cm_store_poll_cdc();
        uint8_t b;
        size_t n;
        while (fwog_link_uart_read(&b)) {
            if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
            if (n >= sizeof(cm_cmd_t) && s_rx.buf[0] == CM_MSG_CMD) {
                cm_cmd_t c;
                memcpy(&c, s_rx.buf, sizeof c);
                if (c.cmd == CM_CMD_TX && s_ok) {
                    send_rf(c.text, c.n);
                }
                cm_store_on_cmd(&c);
            }
        }
        poll_rf();
        send_st();
        sleep_ms(40);
    }
}
