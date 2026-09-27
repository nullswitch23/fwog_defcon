#include "fwog_main.h"
#include "hk_proto.h"
#include "pico/stdlib.h"
#include <string.h>

FWOG_WATCHDOG_DEFAULT();

static fwog_link_rx_t s_rx;
static bool s_io_ok;

static void cfg_i2c_only(fwog_io_cfg_t *cfg) {
    /* fwog_io_cfg_default() enables SPI, UART and GPIO outputs for general
     * breakout use. HeaderKit is an I2C scanner, so copy Retia's HiZ-first
     * policy and leave every unrelated header signal as an input. */
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

static void do_scan(void) {
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
    DIAG("[headerkit] scan n=%u\n", (unsigned)n);
}

int main(void) {
    board_init();
    const fwog_display_result_t d = fwog_display_update_run();
    DIAG("[headerkit] display: %s\n", fwog_display_result_text(d));

    /* Splash holds the display for 3 s; io_dir acks need the app loop. */
    for (int i = 0; i < 20; i++) {
        board_watchdog_kick();
        sleep_ms(200);
    }

    fwog_io_cfg_t cfg;
    cfg_i2c_only(&cfg);
    const fwog_io_result_t ir = fwog_io_dir_apply(&cfg);
    s_io_ok = (ir == FWOG_IO_OK);
    DIAG("[headerkit] io_dir=%d\n", (int)ir);
    board_init_i2c();
    fwog_link_rx_init(&s_rx);
    do_scan();

    while (true) {
        board_watchdog_kick();
        uint8_t b;
        size_t n;
        while (fwog_link_uart_read(&b)) {
            if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
            if (n >= sizeof(hk_cmd_t) && s_rx.buf[0] == HK_MSG_CMD) {
                hk_cmd_t c;
                memcpy(&c, s_rx.buf, sizeof c);
                if (c.cmd == HK_CMD_SCAN) do_scan();
            }
        }
        sleep_ms(10);
    }
}
