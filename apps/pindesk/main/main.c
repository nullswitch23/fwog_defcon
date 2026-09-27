#include "fwog_main.h"
#include "hk_proto.h"
#include "pd_proto.h"
#include "qg_proto.h"
#include "qg_sense.h"
#include "rp_dispatch.h"
#include "rp_cmdline.h"
#include "rp_pins.h"
#include "tg_proto.h"
#include "tg_uart.h"
#include "pico/stdlib.h"
#include <string.h>

FWOG_WATCHDOG_DEFAULT();

static const uint32_t k_baud[] = {
    9600u, 19200u, 38400u, 115200u, 230400u, 921600u
};
#define TG_NBAUD ((int)(sizeof k_baud / sizeof k_baud[0]))

static fwog_link_rx_t s_rx;
static uint8_t s_sel;
static bool s_io_ok;
static uint32_t s_qg_ms;
static int s_baud_i = 3;
static int s_pin_i;
static rp_ctx_t s_rp;
static bool s_rp_on;

static void cfg_i2c_only(fwog_io_cfg_t *cfg) {
    fwog_io_cfg_default(cfg);
    cfg->spi_tx_out = false;
    cfg->spi_rx_out = false;
    cfg->spi_cs_out = false;
    cfg->spi_sclk_out = false;
    cfg->uart_tx_out = false;
    cfg->uart_rx_out = false;
    cfg->uart_cts_out = false;
    cfg->uart_rts_out = false;
    cfg->gpio26_out = false;
    cfg->gpio27_out = false;
    cfg->i2c_pullup = true;
}

static void leave_tile(uint8_t was) {
    if (was == 3u) tg_uart_stop();
    if (was == 4u && s_rp_on) {
        (void)rp_pins_apply_mode(&s_rp, RP_MODE_HIZ);
        rp_cmdline_reset();
        s_rp_on = false;
    }
}

static void hk_scan(void) {
    uint8_t found[HK_MAX];
    const size_t n = fwog_i2c_scan(found, HK_MAX);
    hk_status_t st;
    memset(&st, 0, sizeof st);
    st.type = HK_MSG_ST;
    st.n = (uint8_t)n;
    st.io_ok = s_io_ok ? 1u : 0u;
    st.other_buses_hiz = s_io_ok ? 1u : 0u;
    memcpy(st.addr, found, n);
    (void)fwog_link_uart_send_frame(&st, sizeof st);
}

static void qg_push(void) {
    uint8_t found[QG_MAX];
    const size_t naddr = s_io_ok ? fwog_i2c_scan(found, QG_MAX) : 0u;
    qg_status_t st;
    memset(&st, 0, sizeof st);
    st.type = QG_MSG_ST;
    st.io_ok = s_io_ok ? 1u : 0u;
    st.n = (uint8_t)qg_fill(st.chan, QG_MAX, found, (unsigned)naddr);
    (void)fwog_link_uart_send_frame(&st, sizeof st);
}

static void enter_tile(uint8_t sel) {
    fwog_io_cfg_t cfg;
    if (sel == 1u || sel == 2u) {
        cfg_i2c_only(&cfg);
        s_io_ok = (fwog_io_dir_apply(&cfg) == FWOG_IO_OK);
        board_init_i2c();
        if (sel == 1u) hk_scan();
        else qg_push();
    } else if (sel == 3u) {
        s_baud_i = 3;
        s_pin_i = 0;
        tg_uart_apply(s_pin_i, k_baud[s_baud_i]);
    } else if (sel == 4u) {
        rp_ctx_init(&s_rp);
        rp_dispatch_setup(&s_rp);
        s_rp_on = true;
    }
}

int main(void) {
    board_init();
    const fwog_display_result_t d = fwog_display_update_run();
    DIAG("[pindesk] display: %s\n", fwog_display_result_text(d));

    for (int i = 0; i < 20; i++) {
        board_watchdog_kick();
        sleep_ms(200);
    }

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
                    DIAG("[pindesk] sel=%u\n", (unsigned)s_sel);
                    enter_tile(s_sel);
                }
                continue;
            }
            if (s_sel == 1u && n >= sizeof(hk_cmd_t) && s_rx.buf[0] == HK_MSG_CMD) {
                hk_cmd_t c;
                memcpy(&c, s_rx.buf, sizeof c);
                if (c.cmd == HK_CMD_SCAN) hk_scan();
            }
            if (s_sel == 2u && n >= sizeof(qg_cmd_t) && s_rx.buf[0] == QG_MSG_CMD) {
                qg_cmd_t c;
                memcpy(&c, s_rx.buf, sizeof c);
                if (c.cmd == QG_CMD_SCAN) qg_push();
            }
            if (s_sel == 3u && n >= sizeof(tg_cmd_t) && s_rx.buf[0] == TG_MSG_CMD) {
                tg_cmd_t c;
                memcpy(&c, s_rx.buf, sizeof c);
                s_baud_i = (int)c.baud_i;
                s_pin_i = (int)c.pin_i;
                if (s_baud_i < 0 || s_baud_i >= TG_NBAUD) s_baud_i = 3;
                if (s_pin_i < 0 || s_pin_i >= (int)TG_NPIN) s_pin_i = 0;
                tg_uart_apply(s_pin_i, k_baud[s_baud_i]);
            }
        }
        if (s_sel == 2u && (now - s_qg_ms) > 1000u) {
            s_qg_ms = now;
            qg_push();
        }
        if (s_sel == 3u && tg_uart_readable()) {
            tg_data_t m;
            memset(&m, 0, sizeof m);
            m.type = TG_MSG_DATA;
            while (m.n < TG_CHUNK && tg_uart_readable()) {
                m.data[m.n++] = tg_uart_getc();
            }
            (void)fwog_link_uart_send_frame(&m, sizeof m);
        }
        if (s_sel == 4u && s_rp_on) rp_dispatch_poll(&s_rp);
        sleep_ms(2);
    }
}
