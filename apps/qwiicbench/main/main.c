#include "fwog_main.h"
#include "qg_proto.h"
#include "qg_sense.h"
#include "pico/stdlib.h"
#include <string.h>

FWOG_WATCHDOG_DEFAULT();

static fwog_link_rx_t s_rx;
static bool s_io_ok;
static uint32_t s_poll_ms;

static void cfg_i2c_only(fwog_io_cfg_t *cfg) {
    /* Same Hi-Z-first policy as HeaderKit: QwiicBench only needs I2C. */
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

static void push(void) {
    uint8_t found[QG_MAX];
    const size_t naddr = s_io_ok ? fwog_i2c_scan(found, QG_MAX) : 0u;
    qg_status_t st;
    memset(&st, 0, sizeof st);
    st.type = QG_MSG_ST;
    st.io_ok = s_io_ok ? 1u : 0u;
    st.n = (uint8_t)qg_fill(st.chan, QG_MAX, found, (unsigned)naddr);
    (void)fwog_link_uart_send_frame(&st, sizeof st);
}

int main(void) {
    board_init();
    const fwog_display_result_t d = fwog_display_update_run();
    DIAG("[qwiicbench] display: %s\n", fwog_display_result_text(d));

    for (int i = 0; i < 20; i++) {
        board_watchdog_kick();
        sleep_ms(200);
    }

    fwog_io_cfg_t cfg;
    cfg_i2c_only(&cfg);
    s_io_ok = (fwog_io_dir_apply(&cfg) == FWOG_IO_OK);
    board_init_i2c();
    fwog_link_rx_init(&s_rx);
    push();

    while (true) {
        board_watchdog_kick();
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        uint8_t b;
        size_t n;
        while (fwog_link_uart_read(&b)) {
            if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
            if (n >= sizeof(qg_cmd_t) && s_rx.buf[0] == QG_MSG_CMD) {
                qg_cmd_t c;
                memcpy(&c, s_rx.buf, sizeof c);
                if (c.cmd == QG_CMD_SCAN) push();
            }
        }
        if ((now - s_poll_ms) > 1000u) {
            s_poll_ms = now;
            push();
        }
        sleep_ms(10);
    }
}
