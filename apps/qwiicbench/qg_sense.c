#include "qg_sense.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifndef HOST_TEST
#include "fwog_main.h"
#include "pico/stdlib.h"
#endif

static bool read1(uint8_t addr, uint8_t reg, uint8_t *v) {
#ifndef HOST_TEST
    return fwog_i2c_read_regs(addr, reg, v, 1u);
#else
    (void)addr;
    (void)reg;
    (void)v;
    return false;
#endif
}

static bool bme280_temp(uint8_t addr, char *val, size_t n, int16_t *plot) {
#ifndef HOST_TEST
    uint8_t cal[6], raw[3];
    if (!fwog_i2c_read_regs(addr, 0x88u, cal, 6u)) return false;
    if (!fwog_i2c_write_reg(addr, 0xF4u, 0x21u)) return false;
    sleep_ms(10);
    if (!fwog_i2c_read_regs(addr, 0xFAu, raw, 3u)) return false;
    const uint16_t T1 = (uint16_t)(cal[0] | ((uint16_t)cal[1] << 8));
    const int16_t T2 = (int16_t)(cal[2] | ((uint16_t)cal[3] << 8));
    const int16_t T3 = (int16_t)(cal[4] | ((uint16_t)cal[5] << 8));
    const int32_t adc = (int32_t)(((uint32_t)raw[0] << 12) |
                                  ((uint32_t)raw[1] << 4) |
                                  ((uint32_t)raw[2] >> 4));
    int32_t v1 = ((((adc >> 3) - ((int32_t)T1 << 1))) * (int32_t)T2) >> 11;
    int32_t v2 = (((((adc >> 4) - (int32_t)T1) * ((adc >> 4) - (int32_t)T1)) >> 12) *
                  (int32_t)T3) >> 14;
    const int32_t t = ((v1 + v2) * 5 + 128) >> 8;
    const int abs_t = t < 0 ? (int)-t : (int)t;
    snprintf(val, n, "%s%d.%02dC", t < 0 ? "-" : "", abs_t / 100, abs_t % 100);
    *plot = (int16_t)(t / 10);
    return true;
#else
    (void)addr;
    (void)val;
    (void)n;
    (void)plot;
    return false;
#endif
}

unsigned qg_fill(qg_chan_t *out, unsigned max,
                 const uint8_t *addr, unsigned naddr) {
    if (!out || !addr || max == 0) return 0;
    unsigned n = 0;
    for (unsigned i = 0; i < naddr && n < max; i++) {
        qg_chan_t *c = &out[n];
        memset(c, 0, sizeof *c);
        const uint8_t a = addr[i];
        uint8_t id = 0;
        c->addr = a;
        if (read1(a, 0xD0u, &id) && id == 0x60u) {
            c->known = 1u;
            strncpy(c->name, "BME280", QG_NAME_N - 1u);
            if (!bme280_temp(a, c->val, QG_VAL_N, &c->plot)) {
                snprintf(c->val, QG_VAL_N, "id 0x60");
            }
        } else if (a == 0x23u || a == 0x5Cu) {
            c->known = 1u;
            strncpy(c->name, "BH1750", QG_NAME_N - 1u);
            snprintf(c->val, QG_VAL_N, "0x%02X", (unsigned)a);
        } else {
            snprintf(c->name, QG_NAME_N, "0x%02X", (unsigned)a);
            snprintf(c->val, QG_VAL_N, "present");
        }
        n++;
    }
    return n;
}
