/* Thin register-access helpers over the CPU's I2C instance. Every call is
 * bounded by a timeout: a wedged device must never hang the caller, which on
 * the main CPU would mean a watchdog reset. */
#ifndef FWOG_I2C_BUS_H
#define FWOG_I2C_BUS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FWOG_I2C_TIMEOUT_US 10000u

/* Re-init the I2C block after a timed-out transfer. Safe to call from
 * either CPU; uses that CPU's PIN_I2C_* and FWOG_I2C. */
void fwog_i2c_recover(void);

bool fwog_i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val);

/* Write n bytes starting at `reg`, in one transfer. Devices with address
 * auto-increment (the PCAL6416's port pairs, for instance) require the
 * bytes to land without an intervening STOP, so this cannot be a loop over
 * fwog_i2c_write_reg. n is capped at FWOG_I2C_MAX_WRITE, which also bounds
 * the stack buffer inside. */
#define FWOG_I2C_MAX_WRITE 8u
bool fwog_i2c_write_regs(uint8_t addr, uint8_t reg, const uint8_t *src, size_t n);

/* n == 0 is rejected rather than quietly succeeding: a zero-length read
 * still transmits the register pointer, so returning true told the caller a
 * transfer happened that read nothing. The bootloader's ship-mode path is a
 * read-modify-write and must not treat that as success. */
bool fwog_i2c_read_regs(uint8_t addr, uint8_t reg, uint8_t *dst, size_t n);

/* Probes 0x08..0x77, writing each responding address to found[] (capacity
 * cap). Returns the number of devices seen. */
size_t fwog_i2c_scan(uint8_t *found, size_t cap);

#endif
