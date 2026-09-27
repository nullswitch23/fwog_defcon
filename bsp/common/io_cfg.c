#include "common/io_cfg.h"

void fwog_io_cfg_default(fwog_io_cfg_t *cfg) {
    cfg->spi_tx_out   = true;
    cfg->spi_rx_out   = false;
    cfg->spi_cs_out   = true;
    cfg->spi_sclk_out = true;
    cfg->uart_tx_out  = true;
    cfg->uart_rx_out  = false;
    cfg->uart_cts_out = false;
    cfg->uart_rts_out = true;
    cfg->gpio25_out   = true;
    cfg->gpio26_out   = false;
    cfg->gpio27_out   = true;
    cfg->i2c_pullup   = true;
    cfg->radio1_ant   = FWOG_ANT_400MHZ;
    cfg->radio2_ant   = FWOG_ANT_400MHZ;
}

uint16_t fwog_io_pack_dirword(const fwog_io_cfg_t *cfg) {
    uint16_t w = 0u;
    if (cfg->uart_rts_out)  w |= FWOG_IO_DIR_UART_RTS;
    if (cfg->uart_rx_out)   w |= FWOG_IO_DIR_UART_RX;
    if (cfg->uart_tx_out)   w |= FWOG_IO_DIR_UART_TX;
    if (cfg->spi_tx_out)    w |= FWOG_IO_DIR_SPI_TX;
    if (cfg->spi_rx_out)    w |= FWOG_IO_DIR_SPI_RX;
    if (cfg->spi_cs_out)    w |= FWOG_IO_DIR_SPI_CS;
    if (cfg->spi_sclk_out)  w |= FWOG_IO_DIR_SPI_SCLK;
    if (cfg->uart_cts_out)  w |= FWOG_IO_DIR_UART_CTS;
    if (cfg->gpio25_out)    w |= FWOG_IO_DIR_GPIO25;
    return w;
}

void fwog_io_unpack_dirword(uint16_t word, fwog_io_cfg_t *cfg) {
    cfg->uart_rts_out  = (word & FWOG_IO_DIR_UART_RTS)  != 0u;
    cfg->uart_rx_out   = (word & FWOG_IO_DIR_UART_RX)   != 0u;
    cfg->uart_tx_out   = (word & FWOG_IO_DIR_UART_TX)   != 0u;
    cfg->spi_tx_out    = (word & FWOG_IO_DIR_SPI_TX)    != 0u;
    cfg->spi_rx_out    = (word & FWOG_IO_DIR_SPI_RX)    != 0u;
    cfg->spi_cs_out    = (word & FWOG_IO_DIR_SPI_CS)    != 0u;
    cfg->spi_sclk_out  = (word & FWOG_IO_DIR_SPI_SCLK)  != 0u;
    cfg->uart_cts_out  = (word & FWOG_IO_DIR_UART_CTS)  != 0u;
    cfg->gpio25_out    = (word & FWOG_IO_DIR_GPIO25)    != 0u;
}

void fwog_io_pack_fpga(const fwog_io_cfg_t *cfg, uint8_t out[2]) {
    out[0] = (uint8_t)(fwog_io_pack_dirword(cfg) & 0xFFu);
    out[1] = (uint8_t)(((cfg->gpio26_out ? 1u : 0u) << 1) |
                        (cfg->gpio27_out ? 1u : 0u));
}
