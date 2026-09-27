#include "io_expander/pcal6416.h"

/* Bit assignments, verbatim from rmpLib/fwIOExpand.cpp. Bit 0x0100 is
 * unassigned there and stays clear here. */
#define IOEXP_SPI_SCLK_DIR   0x8000u
#define IOEXP_SPI_CS_DIR     0x4000u
#define IOEXP_SPI_RX_DIR     0x2000u
#define IOEXP_SPI_TX_DIR     0x1000u
#define IOEXP_UART_TX_DIR    0x0800u
#define IOEXP_UART_RX_DIR    0x0400u
#define IOEXP_UART_RTS_DIR   0x0200u
#define IOEXP_I2C_PULLUP     0x0080u
#define IOEXP_IO_CONFIG      0x0040u
#define IOEXP_GPIO25_RP_DIR  0x0020u
#define IOEXP_ANT_V1_2       0x0010u   /* radio 2 */
#define IOEXP_ANT_V1_1       0x0008u   /* radio 1 */
#define IOEXP_ANT_V2_2       0x0004u   /* radio 2 */
#define IOEXP_ANT_V2_1       0x0002u   /* radio 1 */
#define IOEXP_UART_CTS_DIR   0x0001u

/* Each radio has its own V1/V2 pair; the encoding is the same shape for
 * both, so this takes the pair as arguments rather than duplicating the
 * switch. */
static uint16_t ant_bits(fwog_ant_t a, uint16_t v1, uint16_t v2) {
    switch (a) {
    case FWOG_ANT_400MHZ: return v1;
    case FWOG_ANT_200MHZ: return v2;
    case FWOG_ANT_900MHZ: return (uint16_t)(v1 | v2);
    case FWOG_ANT_ISOLATION:
    default:              return 0u;   /* unknown selects nothing */
    }
}

void fwog_ioexp_default(fwog_ioexp_cfg_t *cfg) {
    cfg->spi_sclk_out  = true;
    cfg->spi_cs_out    = true;
    cfg->spi_rx_out    = false;
    cfg->spi_tx_out    = true;
    cfg->uart_tx_out   = true;
    cfg->uart_rx_out   = false;
    cfg->uart_rts_out  = true;
    cfg->uart_cts_out  = false;
    cfg->gpio25_rp_out = true;
    cfg->i2c_pullup    = true;
    cfg->io_config     = false;
    cfg->radio1_ant    = FWOG_ANT_400MHZ;
    cfg->radio2_ant    = FWOG_ANT_400MHZ;
}

void fwog_ioexp_set_antennas(fwog_ioexp_cfg_t *cfg,
                             fwog_ant_t radio1, fwog_ant_t radio2) {
    cfg->radio1_ant = radio1;
    cfg->radio2_ant = radio2;
}

uint16_t fwog_ioexp_pack(const fwog_ioexp_cfg_t *cfg) {
    uint16_t w = 0u;
    if (cfg->spi_sclk_out)  w |= IOEXP_SPI_SCLK_DIR;
    if (cfg->spi_cs_out)    w |= IOEXP_SPI_CS_DIR;
    if (cfg->spi_rx_out)    w |= IOEXP_SPI_RX_DIR;
    if (cfg->spi_tx_out)    w |= IOEXP_SPI_TX_DIR;
    if (cfg->uart_tx_out)   w |= IOEXP_UART_TX_DIR;
    if (cfg->uart_rx_out)   w |= IOEXP_UART_RX_DIR;
    if (cfg->uart_rts_out)  w |= IOEXP_UART_RTS_DIR;
    if (cfg->uart_cts_out)  w |= IOEXP_UART_CTS_DIR;
    if (cfg->gpio25_rp_out) w |= IOEXP_GPIO25_RP_DIR;
    if (cfg->i2c_pullup)    w |= IOEXP_I2C_PULLUP;
    if (cfg->io_config)     w |= IOEXP_IO_CONFIG;
    w |= ant_bits(cfg->radio1_ant, IOEXP_ANT_V1_1, IOEXP_ANT_V2_1);
    w |= ant_bits(cfg->radio2_ant, IOEXP_ANT_V1_2, IOEXP_ANT_V2_2);
    return w;
}

#ifndef HOST_TEST
#include "common/diag.h"
#include "common/i2c_bus.h"

bool fwog_ioexp_apply(const fwog_ioexp_cfg_t *cfg) {
    uint16_t w = fwog_ioexp_pack(cfg);
    /* High byte to OUTPUT0, low byte to OUTPUT1, in one auto-incrementing
       transfer -- see the header. */
    uint8_t out[2] = { (uint8_t)(w >> 8), (uint8_t)(w & 0xFFu) };
    if (fwog_i2c_write_regs(FWOG_IOEXP_ADDR, PCAL6416_REG_OUTPUT0, out, 2u)) {
        return true;
    }
    DIAG("[ioexp] recover I2C after OUTPUT nack\n");
    fwog_i2c_recover();
    {
        const uint8_t all_outputs[2] = { 0x00u, 0x00u };
        (void)fwog_i2c_write_regs(FWOG_IOEXP_ADDR, PCAL6416_REG_CONFIG0,
                                 all_outputs, 2u);
    }
    return fwog_i2c_write_regs(FWOG_IOEXP_ADDR, PCAL6416_REG_OUTPUT0, out, 2u);
}

bool fwog_ioexp_init(void) {
    /* Both config ports to 0 = all sixteen pins are outputs. On the
       PCAL6416 a config bit of 1 is an input, and input is the power-on
       state, which is exactly why the unpulled pins float until now. */
    const uint8_t all_outputs[2] = { 0x00u, 0x00u };
    if (!fwog_i2c_write_regs(FWOG_IOEXP_ADDR, PCAL6416_REG_CONFIG0,
                             all_outputs, 2u)) {
        DIAG("[fwog] io expander not responding at 0x%02x\n",
             (unsigned)FWOG_IOEXP_ADDR);
        return false;
    }
    fwog_ioexp_cfg_t cfg;
    fwog_ioexp_default(&cfg);
    if (!fwog_ioexp_apply(&cfg)) {
        DIAG("[fwog] io expander config write failed\n");
        return false;
    }
    return true;
}
#endif
