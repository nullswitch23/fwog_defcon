#include "common/i2c_bus.h"
#include "common/diag.h"
#include "platform/board.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include <string.h>

void fwog_i2c_recover(void) {
    /* Timeout/abort on this PL011-less I2C block leaves the controller
     * wedged; bit-bang a STOP then re-init. */
    i2c_deinit(FWOG_I2C);
    gpio_init(PIN_I2C_SCL);
    gpio_init(PIN_I2C_SDA);
    gpio_pull_up(PIN_I2C_SCL);
    gpio_pull_up(PIN_I2C_SDA);
    gpio_set_dir(PIN_I2C_SDA, GPIO_IN);
    gpio_set_dir(PIN_I2C_SCL, GPIO_OUT);
    gpio_put(PIN_I2C_SCL, 1);
    busy_wait_us(5);
    for (unsigned i = 0; i < 9u; i++) {
        gpio_put(PIN_I2C_SCL, 0);
        busy_wait_us(5);
        gpio_put(PIN_I2C_SCL, 1);
        busy_wait_us(5);
    }
    gpio_set_dir(PIN_I2C_SDA, GPIO_OUT);
    gpio_put(PIN_I2C_SDA, 0);
    busy_wait_us(5);
    gpio_put(PIN_I2C_SCL, 1);
    busy_wait_us(5);
    gpio_put(PIN_I2C_SDA, 1);
    busy_wait_us(5);
    board_init_i2c();
}

bool fwog_i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    int r = i2c_write_timeout_us(FWOG_I2C, addr, buf, 2, false,
                                 FWOG_I2C_TIMEOUT_US);
    if (r != 2) {
        DIAG("[i2c] wr 0x%02x reg=0x%02x r=%d\n",
             (unsigned)addr, (unsigned)reg, r);
        return false;
    }
    return true;
}

bool fwog_i2c_write_regs(uint8_t addr, uint8_t reg, const uint8_t *src, size_t n) {
    if (!src || n == 0u || n > FWOG_I2C_MAX_WRITE) return false;
    uint8_t buf[1u + FWOG_I2C_MAX_WRITE];
    buf[0] = reg;
    memcpy(&buf[1], src, n);
    int r = i2c_write_timeout_us(FWOG_I2C, addr, buf, n + 1u, false,
                                 FWOG_I2C_TIMEOUT_US);
    if (r != (int)(n + 1u)) {
        DIAG("[i2c] wr 0x%02x reg=0x%02x n=%u r=%d\n",
             (unsigned)addr, (unsigned)reg, (unsigned)n, r);
        return false;
    }
    return true;
}

bool fwog_i2c_read_regs(uint8_t addr, uint8_t reg, uint8_t *dst, size_t n) {
    if (n == 0) return false;   /* see the header: not a successful read */
    int r = i2c_write_timeout_us(FWOG_I2C, addr, &reg, 1, true,
                                 FWOG_I2C_TIMEOUT_US);
    if (r != 1) return false;
    r = i2c_read_timeout_us(FWOG_I2C, addr, dst, n, false,
                            FWOG_I2C_TIMEOUT_US);
    return r == (int)n;
}

size_t fwog_i2c_scan(uint8_t *found, size_t cap) {
    size_t n = 0;
    for (uint8_t a = 0x08; a < 0x78 && n < cap; a++) {
        uint8_t dummy;
        if (i2c_read_timeout_us(FWOG_I2C, a, &dummy, 1, false,
                                FWOG_I2C_TIMEOUT_US) >= 0) {
            found[n++] = a;
        }
    }
    return n;
}
