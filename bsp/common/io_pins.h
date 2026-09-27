/* The breakout header's pins, and the rules for driving one.
 *
 * Pure decision logic, no SDK -- gpio/breakout.c binds it to real pads. This
 * is the half worth host-testing: the rules below are the entire safety
 * argument for `iopin`, and a bench is a bad place to discover one was
 * inverted.
 *
 * Header pin numbers are from docs.freewili.com/gpio/gpio-pinout/. They agree
 * with fwog_ioexp_default()/fwog_io_cfg_default() on all nine
 * direction-controlled lines, which is independent confirmation of a map this
 * repo previously had only from rmpLib -- see the hardware record.
 *
 * GPIO numbers are MAIN-CPU numbers throughout, matching io_cfg.h. Note the
 * collision that has already caused confusion: main's GPIO 8 is the breakout
 * UART TX, while the DISPLAY's GPIO 8 is MAIN_BOOT_OE (the hardware record). Two
 * different CPUs, same number, unrelated nets. */
#ifndef FWOG_IO_PINS_H
#define FWOG_IO_PINS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "common/io_cfg.h"

typedef enum {
    FWOG_IO_PIN_OK = 0,
    FWOG_IO_PIN_ERR_NOT_BREAKOUT,      /* not a breakout line at all */
    FWOG_IO_PIN_ERR_NOT_OUTPUT,        /* the config calls this line an input */
    FWOG_IO_PIN_ERR_IO_CONFIG_ACTIVE   /* FPGA SPI group, window not closed */
} fwog_io_pin_result_t;

/* Main's belief about the PCAL6416's IO_CONFIG bit.
 *
 * Tri-state, not boolean, and the third state is not defensive padding.
 * breakout.h documents the case: a CONFIG_ENABLE(true) that TIMES OUT may
 * still have been applied and acked late by the display, so main giving up
 * does not mean the bit is clear. UNKNOWN is also the correct value at
 * startup, because the expander does NOT reset when this RP2040 does -- it
 * can hold a bit set by a previous boot. Only an acked CONFIG_ENABLE(false)
 * makes it DISABLED. */
typedef enum {
    FWOG_IO_CONFIG_UNKNOWN = 0,   /* value 0 so a zeroed static starts safe */
    FWOG_IO_CONFIG_DISABLED,
    FWOG_IO_CONFIG_ENABLED
} fwog_io_config_state_t;

typedef struct {
    uint8_t     gpio;        /* main-CPU GPIO number */
    uint8_t     header_pin;  /* breakout connector pin */
    const char *name;
    /* True for 12/13/14/15 -- the FPGA's own SPI slave bus. See
       fwog_io_pin_check_drive(). */
    bool        fpga_spi;
} fwog_io_pin_t;

size_t                fwog_io_pin_count(void);
const fwog_io_pin_t  *fwog_io_pin_at(size_t i);
const fwog_io_pin_t  *fwog_io_pin_lookup(unsigned gpio);

/* Is this line configured as an output? False for a GPIO that is not a
 * breakout line at all -- callers that need to tell those apart should call
 * fwog_io_pin_lookup() first. */
bool fwog_io_pin_is_output(const fwog_io_cfg_t *cfg, unsigned gpio);

/* May `gpio` be driven right now?
 *
 * Three refusals, checked in this order:
 *
 *   1. NOT_BREAKOUT   -- not a breakout line.
 *   2. NOT_OUTPUT     -- the config calls it an input. Driving a line whose
 *                        level shifter points inward is an output driving an
 *                        output, which is the hazard io_seq.c exists to
 *                        prevent; refusing here keeps `iopin` from creating
 *                        by a side door the state `iodir` is careful to avoid.
 *   3. IO_CONFIG_ACTIVE -- the line is in the FPGA's SPI group (12/13/14/15)
 *                        and IO_CONFIG is not known-disabled.
 *
 * On rule 3: the board owner states GPIO 13 is safe to drive whenever
 * IO_CONFIG is disabled, and the hardware record corroborates the mechanism from this
 * repo's own measurement -- the FPGA's direction register reads back 0x0000
 * whenever IO_CONFIG is deasserted, which is what a gated SPI-slave interface
 * looks like. A part that is not listening cannot be desynchronised by
 * toggling its chip select. So the guard is that gating, checked, rather than
 * an operator flag: IO_CONFIG is disabled in normal operation anyway
 * (fwog_ioexp_default() clears it and fwog_io_sequence() closes the window on
 * every path, including every error path), so this costs nothing and needs
 * nothing remembered.
 *
 * It covers 12/14/15 as well as 13 because the same gating argument applies to
 * the clock and data lines: if the slave is listening, SCLK and MOSI can clock
 * it, not just CS.
 *
 * A pin can trip more than one rule -- GPIO 12 defaults to an input AND is in
 * the SPI group. The order above is what decides which is reported; both
 * refuse, so the choice is cosmetic, and NOT_OUTPUT comes first because it is
 * the one an operator fixes (with `iodir`). */
fwog_io_pin_result_t fwog_io_pin_check_drive(unsigned gpio,
                                             const fwog_io_cfg_t *cfg,
                                             fwog_io_config_state_t st);

/* --- the breakout I2C lines --- */

/* Header pins 10 (SDA) and 8 (SCL). Deliberately NOT in the table above.
 *
 * They are a different kind of pin in three ways that all matter here:
 *
 *   1. They have no direction bit anywhere -- not in the FPGA's word, not in
 *      the expander's. fwog_io_pin_is_output() has no case for them and must
 *      not grow one.
 *   2. They are buffered by a PCA9517, not an SN74LXC1T45. That part is a
 *      bidirectional open-drain I2C buffer, not a direction-switched
 *      translator.
 *   3. I2C is OPEN-DRAIN. A line is pulled low or released; it is never
 *      driven high. Driving high into a device that is pulling low is
 *      contention, so the API below cannot express it at all -- the same
 *      approach as refusing to drive a configured input, applied to the
 *      failure mode this bus actually has.
 *
 * The pull-up that returns a released line to high is the PCA9517's
 * software-controllable 10k, gated by the expander's IOEXP_I2C_PULLUP bit
 * (fwog_ioexp_pack(); set in fwog_ioexp_default()). main's board_init() never
 * touches these pads -- board_init_i2c() is the app's to call and bench_main
 * does not -- so no RP2040 internal pull-up is competing, which is what makes
 * "released, and it went high" evidence about the EXTERNAL pull-up. */
#define FWOG_IO_I2C_SDA_GPIO 16u
#define FWOG_IO_I2C_SCL_GPIO 17u

/* "sda" or "scl" (exact, lower case) -> its GPIO. False for anything else,
 * including a bare number: naming the line rather than the pin is deliberate,
 * because the two are easy to transpose and the consequence is a confusing
 * result rather than an obvious error. */
bool fwog_io_i2c_line(const char *name, unsigned *gpio_out);

/* Is this one of the two I2C lines? */
bool fwog_io_is_i2c_line(unsigned gpio);

#endif
