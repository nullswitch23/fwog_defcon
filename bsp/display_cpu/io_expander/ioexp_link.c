#include "io_expander/ioexp_link.h"
#include "io_expander/pcal6416.h"
#include "common/link/io_proto.h"
#include "common/link/link_uart.h"
#include "common/diag.h"

/* The expander does not reset when this RP2040 does, so its contents
   survive a reboot. We keep our own picture and re-apply the whole word
   each time, exactly as the reference does. */
static fwog_ioexp_cfg_t s_cfg;
static bool             s_init;

static fwog_ioexp_cfg_t *cfg(void) {
    if (!s_init) { fwog_ioexp_default(&s_cfg); s_init = true; }
    return &s_cfg;
}

const fwog_ioexp_cfg_t *fwog_ioexp_link_cfg(void) { return cfg(); }

bool fwog_ioexp_link_set_antennas(fwog_ant_t radio1, fwog_ant_t radio2) {
    /* Start from the LIVE config, not from fwog_ioexp_default() -- see the
       header. The directions in here are main's and are in use. */
    fwog_ioexp_cfg_t *c = cfg();
    fwog_ioexp_set_antennas(c, radio1, radio2);
    return fwog_ioexp_apply(c);
}

static void send_ack(uint8_t seq, uint8_t answering, bool ok) {
    uint8_t buf[8];
    size_t n = fwog_io_proto_build_ack(buf, sizeof buf, seq, answering, ok);
    if (n) (void)fwog_link_uart_send_frame(buf, n);
}

bool fwog_ioexp_link_handle(const void *payload, size_t len) {
    uint8_t t = fwog_io_proto_type(payload, len);

    if (t == FWOG_IO_MSG_CONFIG_ENABLE) {
        const fwog_io_config_enable_t *m = payload;
        cfg()->io_config = (m->enable != 0u);
        bool ok = fwog_ioexp_apply(cfg());
        if (!ok) DIAG("[ioexp] CONFIG_ENABLE apply failed\n");
        send_ack(m->seq, t, ok);
        return true;
    }

    if (t == FWOG_IO_MSG_SET_DIRS) {
        const fwog_io_set_dirs_t *m = payload;
        fwog_ioexp_cfg_t *c = cfg();

        /* rpProcessHostCommand.cpp:65 -- the direction bits are latched
           ONLY while io_config is set. The antennas and the pull-up are
           applied either way, matching the reference exactly. */
        if (c->io_config) {
            c->uart_rts_out  = (m->dirword & FWOG_IO_DIR_UART_RTS)  != 0u;
            c->uart_rx_out   = (m->dirword & FWOG_IO_DIR_UART_RX)   != 0u;
            c->uart_tx_out   = (m->dirword & FWOG_IO_DIR_UART_TX)   != 0u;
            c->spi_tx_out    = (m->dirword & FWOG_IO_DIR_SPI_TX)    != 0u;
            c->spi_rx_out    = (m->dirword & FWOG_IO_DIR_SPI_RX)    != 0u;
            c->spi_cs_out    = (m->dirword & FWOG_IO_DIR_SPI_CS)    != 0u;
            c->spi_sclk_out  = (m->dirword & FWOG_IO_DIR_SPI_SCLK)  != 0u;
            c->uart_cts_out  = (m->dirword & FWOG_IO_DIR_UART_CTS)  != 0u;
            c->gpio25_rp_out = (m->dirword & FWOG_IO_DIR_GPIO25)    != 0u;
        }
        c->i2c_pullup = (m->i2c_pullup != 0u);
        c->radio1_ant = (fwog_ant_t)m->radio1_ant;
        c->radio2_ant = (fwog_ant_t)m->radio2_ant;

        bool ok = fwog_ioexp_apply(c);
        if (!ok) DIAG("[ioexp] SET_DIRS apply failed\n");
        send_ack(m->seq, t, ok);
        return true;
    }

    return false;
}
