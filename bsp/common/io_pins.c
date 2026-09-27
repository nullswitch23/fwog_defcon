#include "common/io_pins.h"
#include <string.h>

/* Ordered by header pin so `iopin`'s listing reads like the connector rather
 * than like GPIO numbering -- the operator is looking at the header, not at
 * the RP2040 datasheet. Power (2/6), ground (19/20) and SWD (16/18) are not
 * breakout GPIO and are deliberately absent.
 *
 * Pin 4 is V PINS IN and is also absent: it is not a GPIO, it is the supply
 * that every line here depends on. Without 1.1-5.5 V on it NOTHING in this
 * table functions, which is why tools/io_walk.py opens with an electrical
 * control rather than trusting an LED. */
static const fwog_io_pin_t PINS[] = {
    { 13,  1, "spi_cs",   true  },
    { 27,  3, "gpio27",   false },
    {  9,  5, "uart_rx",  false },
    { 10,  7, "uart_cts", false },
    {  8,  9, "uart_tx",  false },
    { 11, 11, "uart_rts", false },
    { 12, 12, "spi_rx",   true  },
    { 15, 13, "spi_tx",   true  },
    { 26, 14, "gpio26",   false },
    { 14, 15, "spi_sclk", true  },
    { 25, 17, "gpio25",   false },
};

size_t fwog_io_pin_count(void) {
    return sizeof PINS / sizeof PINS[0];
}

const fwog_io_pin_t *fwog_io_pin_at(size_t i) {
    return (i < fwog_io_pin_count()) ? &PINS[i] : NULL;
}

const fwog_io_pin_t *fwog_io_pin_lookup(unsigned gpio) {
    for (size_t i = 0; i < fwog_io_pin_count(); i++) {
        if (PINS[i].gpio == gpio) return &PINS[i];
    }
    return NULL;
}

bool fwog_io_pin_is_output(const fwog_io_cfg_t *cfg, unsigned gpio) {
    switch (gpio) {
    case 8:  return cfg->uart_tx_out;
    case 9:  return cfg->uart_rx_out;
    case 10: return cfg->uart_cts_out;
    case 11: return cfg->uart_rts_out;
    case 12: return cfg->spi_rx_out;
    case 13: return cfg->spi_cs_out;
    case 14: return cfg->spi_sclk_out;
    case 15: return cfg->spi_tx_out;
    case 25: return cfg->gpio25_out;
    case 26: return cfg->gpio26_out;
    case 27: return cfg->gpio27_out;
    default: return false;
    }
}

bool fwog_io_i2c_line(const char *name, unsigned *gpio_out) {
    if (!name) return false;
    if (strcmp(name, "sda") == 0) { *gpio_out = FWOG_IO_I2C_SDA_GPIO; return true; }
    if (strcmp(name, "scl") == 0) { *gpio_out = FWOG_IO_I2C_SCL_GPIO; return true; }
    return false;
}

bool fwog_io_is_i2c_line(unsigned gpio) {
    return gpio == FWOG_IO_I2C_SDA_GPIO || gpio == FWOG_IO_I2C_SCL_GPIO;
}

fwog_io_pin_result_t fwog_io_pin_check_drive(unsigned gpio,
                                             const fwog_io_cfg_t *cfg,
                                             fwog_io_config_state_t st) {
    const fwog_io_pin_t *p = fwog_io_pin_lookup(gpio);
    if (!p) return FWOG_IO_PIN_ERR_NOT_BREAKOUT;
    if (!fwog_io_pin_is_output(cfg, gpio)) return FWOG_IO_PIN_ERR_NOT_OUTPUT;
    if (p->fpga_spi && st != FWOG_IO_CONFIG_DISABLED) {
        return FWOG_IO_PIN_ERR_IO_CONFIG_ACTIVE;
    }
    return FWOG_IO_PIN_OK;
}
