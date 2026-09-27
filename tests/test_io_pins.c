#include "test_util.h"
#include "common/io_pins.h"
#include "common/io_cfg.h"
#include <string.h>

/* The refusal rules ARE the safety argument for `iopin`. Everything here is
 * about proving a rule refuses when it should and, just as importantly, that
 * it does not refuse when it should not -- a guard that rejects everything
 * looks safe and is useless. */

static void test_table(void) {
    /* Eleven breakout GPIO. Power (2/6), ground (19/20), SWD (16/18) and
       V PINS IN (4) are not in the table. */
    ASSERT_EQ(fwog_io_pin_count(), 11u);

    /* Header pin numbers, against docs.freewili.com/gpio/gpio-pinout/. These
       are the numbers the operator reads off the connector; getting one wrong
       sends them to the wrong LED and quietly invalidates a whole run. */
    ASSERT_EQ(fwog_io_pin_lookup(13)->header_pin, 1u);
    ASSERT_EQ(fwog_io_pin_lookup(27)->header_pin, 3u);
    ASSERT_EQ(fwog_io_pin_lookup(9)->header_pin,  5u);
    ASSERT_EQ(fwog_io_pin_lookup(10)->header_pin, 7u);
    ASSERT_EQ(fwog_io_pin_lookup(8)->header_pin,  9u);
    ASSERT_EQ(fwog_io_pin_lookup(11)->header_pin, 11u);
    ASSERT_EQ(fwog_io_pin_lookup(12)->header_pin, 12u);
    ASSERT_EQ(fwog_io_pin_lookup(15)->header_pin, 13u);
    ASSERT_EQ(fwog_io_pin_lookup(26)->header_pin, 14u);
    ASSERT_EQ(fwog_io_pin_lookup(14)->header_pin, 15u);
    ASSERT_EQ(fwog_io_pin_lookup(25)->header_pin, 17u);

    /* Listed in header-pin order, so the listing reads like the connector. */
    for (size_t i = 1; i < fwog_io_pin_count(); i++) {
        ASSERT_TRUE(fwog_io_pin_at(i - 1)->header_pin
                    < fwog_io_pin_at(i)->header_pin);
    }

    /* Exactly the FPGA's SPI bus is flagged, and nothing else. */
    ASSERT_TRUE(fwog_io_pin_lookup(12)->fpga_spi);
    ASSERT_TRUE(fwog_io_pin_lookup(13)->fpga_spi);
    ASSERT_TRUE(fwog_io_pin_lookup(14)->fpga_spi);
    ASSERT_TRUE(fwog_io_pin_lookup(15)->fpga_spi);
    ASSERT_TRUE(!fwog_io_pin_lookup(8)->fpga_spi);
    ASSERT_TRUE(!fwog_io_pin_lookup(25)->fpga_spi);
    ASSERT_TRUE(!fwog_io_pin_lookup(26)->fpga_spi);
    ASSERT_TRUE(!fwog_io_pin_lookup(27)->fpga_spi);

    /* Not breakout lines. 16/17 are I2C0 (PCA9517, outside the direction
       system entirely); 24 and 99 are not on the header at all. */
    ASSERT_TRUE(fwog_io_pin_lookup(16) == NULL);
    ASSERT_TRUE(fwog_io_pin_lookup(17) == NULL);
    ASSERT_TRUE(fwog_io_pin_lookup(24) == NULL);
    ASSERT_TRUE(fwog_io_pin_lookup(99) == NULL);

    ASSERT_TRUE(fwog_io_pin_at(fwog_io_pin_count()) == NULL);
}

/* The default config's directions must match the published connector -- the
 * seven OUT pins and four IN pins of gpio-pinout/. This is the check that
 * would catch a default drifting away from the documented header. */
static void test_default_directions(void) {
    fwog_io_cfg_t c;
    fwog_io_cfg_default(&c);

    const unsigned outs[] = { 8, 11, 13, 14, 15, 25, 27 };
    for (size_t i = 0; i < sizeof outs / sizeof outs[0]; i++) {
        ASSERT_TRUE(fwog_io_pin_is_output(&c, outs[i]));
    }
    const unsigned ins[] = { 9, 10, 12, 26 };
    for (size_t i = 0; i < sizeof ins / sizeof ins[0]; i++) {
        ASSERT_TRUE(!fwog_io_pin_is_output(&c, ins[i]));
    }
    /* A GPIO that is not a breakout line is never an output. */
    ASSERT_TRUE(!fwog_io_pin_is_output(&c, 16));
    ASSERT_TRUE(!fwog_io_pin_is_output(&c, 99));
}

static void test_check_drive(void) {
    fwog_io_cfg_t c;
    fwog_io_cfg_default(&c);

    /* Rule 1. */
    ASSERT_EQ(fwog_io_pin_check_drive(16, &c, FWOG_IO_CONFIG_DISABLED),
              FWOG_IO_PIN_ERR_NOT_BREAKOUT);

    /* Rule 2 -- an input is refused even with the window safely closed. */
    ASSERT_EQ(fwog_io_pin_check_drive(9, &c, FWOG_IO_CONFIG_DISABLED),
              FWOG_IO_PIN_ERR_NOT_OUTPUT);
    ASSERT_EQ(fwog_io_pin_check_drive(26, &c, FWOG_IO_CONFIG_DISABLED),
              FWOG_IO_PIN_ERR_NOT_OUTPUT);

    /* The non-FPGA outputs are allowed in EVERY IO_CONFIG state: the window
       gates the FPGA's SPI slave, and these lines do not reach it. If this
       ever starts failing, the guard has been over-applied. */
    const unsigned plain_outs[] = { 8, 11, 25, 27 };
    const fwog_io_config_state_t states[] = { FWOG_IO_CONFIG_UNKNOWN,
                                              FWOG_IO_CONFIG_DISABLED,
                                              FWOG_IO_CONFIG_ENABLED };
    for (size_t i = 0; i < sizeof plain_outs / sizeof plain_outs[0]; i++) {
        for (size_t s = 0; s < sizeof states / sizeof states[0]; s++) {
            ASSERT_EQ(fwog_io_pin_check_drive(plain_outs[i], &c, states[s]),
                      FWOG_IO_PIN_OK);
        }
    }

    /* Rule 3 -- the FPGA's SPI outputs, only with the window known-closed.
       13/14/15 are outputs by default; 12 is an input and trips rule 2
       first. */
    const unsigned spi_outs[] = { 13, 14, 15 };
    for (size_t i = 0; i < sizeof spi_outs / sizeof spi_outs[0]; i++) {
        ASSERT_EQ(fwog_io_pin_check_drive(spi_outs[i], &c,
                                          FWOG_IO_CONFIG_DISABLED),
                  FWOG_IO_PIN_OK);
        ASSERT_EQ(fwog_io_pin_check_drive(spi_outs[i], &c,
                                          FWOG_IO_CONFIG_ENABLED),
                  FWOG_IO_PIN_ERR_IO_CONFIG_ACTIVE);
        /* UNKNOWN must refuse exactly as ENABLED does. This is the whole
           reason the state is tri-state: main giving up on a CONFIG_ENABLE
           ack does not mean the display failed to set the bit. */
        ASSERT_EQ(fwog_io_pin_check_drive(spi_outs[i], &c,
                                          FWOG_IO_CONFIG_UNKNOWN),
                  FWOG_IO_PIN_ERR_IO_CONFIG_ACTIVE);
    }

    /* Ordering: GPIO 12 trips BOTH rule 2 and rule 3. Documented as
       NOT_OUTPUT-first because that is the one an operator fixes. */
    ASSERT_EQ(fwog_io_pin_check_drive(12, &c, FWOG_IO_CONFIG_ENABLED),
              FWOG_IO_PIN_ERR_NOT_OUTPUT);

    /* A zeroed state variable must be the SAFE one -- FWOG_IO_CONFIG_UNKNOWN
       is 0 so a static that was never assigned refuses rather than allows. */
    fwog_io_config_state_t zeroed;
    memset(&zeroed, 0, sizeof zeroed);
    ASSERT_EQ(fwog_io_pin_check_drive(13, &c, zeroed),
              FWOG_IO_PIN_ERR_IO_CONFIG_ACTIVE);

    /* Flipping a line to an output makes it drivable -- proving rule 2 reads
       the config rather than a hardcoded list. */
    c.gpio26_out = true;
    ASSERT_EQ(fwog_io_pin_check_drive(26, &c, FWOG_IO_CONFIG_DISABLED),
              FWOG_IO_PIN_OK);
}

static void test_i2c_lines(void) {
    unsigned g = 0xFFu;

    ASSERT_TRUE(fwog_io_i2c_line("sda", &g));
    ASSERT_EQ(g, 16u);
    ASSERT_TRUE(fwog_io_i2c_line("scl", &g));
    ASSERT_EQ(g, 17u);

    /* Names only, and exact. A bare pin number, the wrong case, or a near
       miss must all fail rather than resolve to something plausible: sda and
       scl are easy to transpose and a silent wrong answer is worse than an
       error. */
    ASSERT_TRUE(!fwog_io_i2c_line("SDA", &g));
    ASSERT_TRUE(!fwog_io_i2c_line("16", &g));
    ASSERT_TRUE(!fwog_io_i2c_line("sd", &g));
    ASSERT_TRUE(!fwog_io_i2c_line("sdax", &g));
    ASSERT_TRUE(!fwog_io_i2c_line("", &g));
    ASSERT_TRUE(!fwog_io_i2c_line(NULL, &g));

    ASSERT_TRUE(fwog_io_is_i2c_line(16));
    ASSERT_TRUE(fwog_io_is_i2c_line(17));
    ASSERT_TRUE(!fwog_io_is_i2c_line(15));
    ASSERT_TRUE(!fwog_io_is_i2c_line(26));

    /* The two families must not overlap. The I2C lines have no direction bit
       anywhere, so if one ever appeared in the breakout table,
       fwog_io_pin_check_drive() would happily consent to driving an
       open-drain bus line push-pull. */
    ASSERT_TRUE(fwog_io_pin_lookup(16) == NULL);
    ASSERT_TRUE(fwog_io_pin_lookup(17) == NULL);
    for (size_t i = 0; i < fwog_io_pin_count(); i++) {
        ASSERT_TRUE(!fwog_io_is_i2c_line(fwog_io_pin_at(i)->gpio));
    }

    /* And they are never reported as outputs, whatever the config says. */
    fwog_io_cfg_t c;
    fwog_io_cfg_default(&c);
    ASSERT_TRUE(!fwog_io_pin_is_output(&c, 16));
    ASSERT_TRUE(!fwog_io_pin_is_output(&c, 17));
}

int main(void) {
    test_table();
    test_default_directions();
    test_check_drive();
    test_i2c_lines();
    TEST_RETURN();
}
