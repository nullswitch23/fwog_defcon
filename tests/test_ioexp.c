#include "test_util.h"
#include "io_expander/pcal6416.h"
#include <string.h>

int main(void) {
    fwog_ioexp_cfg_t c;

    /* The power-on default this BSP drives. Read out of rmpLib's
       fwIOExpand::setDefault() + setIO() with FWPURPLE_VARIANT undefined:
       SPI SCLK/CS/Tx out, SPI Rx in, UART Tx/RTS out, UART Rx/CTS in,
       GPIO25_RP out, breakout I2C pull-ups on, IO_CONFIG off, and both
       radios on the 400 MHz antenna path. */
    fwog_ioexp_default(&c);
    ASSERT_EQ(fwog_ioexp_pack(&c), 0xDAB8u);

    /* The legacy comment records the state the external pull-ups establish
       as port0 0xDA, port1 0x20. Port 0 is bit-for-bit what we drive, so
       writing this part changes NO level-shifter direction and there is no
       bus-contention argument for writing it early -- which is what settled
       the bootloader question (see the PCAL6416 note in
       the peripheral catalog). The whole delta is on port 1: the antenna
       selects and the breakout I2C pull-up enable.

       Whether those port-1 bits are externally pulled or genuinely float is
       NOT settled -- 0x20 may be an observed read-back, which cannot tell
       the two apart. This assertion pins the delta, which is a fact about
       our packing either way. */
    ASSERT_EQ(fwog_ioexp_pack(&c) >> 8, 0xDAu);          /* port 0 */
    ASSERT_EQ(fwog_ioexp_pack(&c) & 0xFFu, 0xB8u);       /* port 1 */
    ASSERT_TRUE((0x20u ^ (fwog_ioexp_pack(&c) & 0xFFu)) == 0x98u);

    /* --- direction bits, one at a time from a cleared config --- */
    memset(&c, 0, sizeof c);
    c.radio1_ant = FWOG_ANT_ISOLATION;
    c.radio2_ant = FWOG_ANT_ISOLATION;
    ASSERT_EQ(fwog_ioexp_pack(&c), 0x0000u);

    memset(&c, 0, sizeof c); c.spi_sclk_out  = true;
    ASSERT_EQ(fwog_ioexp_pack(&c), 0x8000u);   /* main GPIO 14 */
    memset(&c, 0, sizeof c); c.spi_cs_out    = true;
    ASSERT_EQ(fwog_ioexp_pack(&c), 0x4000u);   /* main GPIO 13 */
    memset(&c, 0, sizeof c); c.spi_rx_out    = true;
    ASSERT_EQ(fwog_ioexp_pack(&c), 0x2000u);   /* main GPIO 12 */
    memset(&c, 0, sizeof c); c.spi_tx_out    = true;
    ASSERT_EQ(fwog_ioexp_pack(&c), 0x1000u);   /* main GPIO 15 */
    memset(&c, 0, sizeof c); c.uart_tx_out   = true;
    ASSERT_EQ(fwog_ioexp_pack(&c), 0x0800u);   /* main GPIO 8  */
    memset(&c, 0, sizeof c); c.uart_rx_out   = true;
    ASSERT_EQ(fwog_ioexp_pack(&c), 0x0400u);   /* main GPIO 9  */
    memset(&c, 0, sizeof c); c.uart_rts_out  = true;
    ASSERT_EQ(fwog_ioexp_pack(&c), 0x0200u);   /* main GPIO 11 */
    memset(&c, 0, sizeof c); c.uart_cts_out  = true;
    ASSERT_EQ(fwog_ioexp_pack(&c), 0x0001u);   /* main GPIO 10 */
    memset(&c, 0, sizeof c); c.gpio25_rp_out = true;
    ASSERT_EQ(fwog_ioexp_pack(&c), 0x0020u);
    memset(&c, 0, sizeof c); c.i2c_pullup    = true;
    ASSERT_EQ(fwog_ioexp_pack(&c), 0x0080u);
    memset(&c, 0, sizeof c); c.io_config     = true;
    ASSERT_EQ(fwog_ioexp_pack(&c), 0x0040u);

    /* Bit 0x0100 is unassigned in the legacy map and must stay clear no
       matter what is set. */
    fwog_ioexp_default(&c);
    ASSERT_EQ(fwog_ioexp_pack(&c) & 0x0100u, 0u);

    /* --- antenna paths. Radio 1 uses V1_1/V2_1, radio 2 uses V1_2/V2_2;
           the two radios are independent and must not bleed into each
           other. --- */
    memset(&c, 0, sizeof c);
    c.radio1_ant = FWOG_ANT_400MHZ;
    ASSERT_EQ(fwog_ioexp_pack(&c), 0x0008u);              /* V1_1 */
    c.radio1_ant = FWOG_ANT_200MHZ;
    ASSERT_EQ(fwog_ioexp_pack(&c), 0x0002u);              /* V2_1 */
    c.radio1_ant = FWOG_ANT_900MHZ;
    ASSERT_EQ(fwog_ioexp_pack(&c), 0x000Au);              /* V1_1|V2_1 */
    c.radio1_ant = FWOG_ANT_ISOLATION;
    ASSERT_EQ(fwog_ioexp_pack(&c), 0x0000u);

    memset(&c, 0, sizeof c);
    c.radio2_ant = FWOG_ANT_400MHZ;
    ASSERT_EQ(fwog_ioexp_pack(&c), 0x0010u);              /* V1_2 */
    c.radio2_ant = FWOG_ANT_200MHZ;
    ASSERT_EQ(fwog_ioexp_pack(&c), 0x0004u);              /* V2_2 */
    c.radio2_ant = FWOG_ANT_900MHZ;
    ASSERT_EQ(fwog_ioexp_pack(&c), 0x0014u);              /* V1_2|V2_2 */
    c.radio2_ant = FWOG_ANT_ISOLATION;
    ASSERT_EQ(fwog_ioexp_pack(&c), 0x0000u);

    /* Both radios at once, to catch a mask that overlaps. */
    memset(&c, 0, sizeof c);
    c.radio1_ant = FWOG_ANT_900MHZ;
    c.radio2_ant = FWOG_ANT_900MHZ;
    ASSERT_EQ(fwog_ioexp_pack(&c), 0x001Eu);

    /* An out-of-range enum must isolate rather than select an arbitrary
       path: the safe failure for an antenna switch is "connected to
       nothing", not "connected to whichever port the bit pattern hits". */
    memset(&c, 0, sizeof c);
    c.radio1_ant = (fwog_ant_t)99;
    c.radio2_ant = (fwog_ant_t)99;
    ASSERT_EQ(fwog_ioexp_pack(&c), 0x0000u);

    /* ---- fwog_ioexp_set_antennas() must move ONLY the antenna bits ----
     *
     * The regression this exists for: apps/bench/display's `ant` changed the
     * antennas by building a fresh fwog_ioexp_default() and applying the whole
     * 16-bit word, which also reset io_config and all nine level-shifter
     * directions -- main's, and in use -- while its comment claimed it
     * "changes ONLY the two antenna fields". The expander has one state and
     * every writer must start from it. */
    {
        fwog_ioexp_cfg_t base;
        fwog_ioexp_default(&base);
        /* A deliberately NON-default state, the kind main's iodir leaves
           behind: uart_tx flipped to input, uart_cts to output, io_config
           left asserted, pull-ups off. */
        base.uart_tx_out  = false;
        base.uart_cts_out = true;
        base.io_config    = true;
        base.i2c_pullup   = false;
        base.radio1_ant   = FWOG_ANT_400MHZ;
        base.radio2_ant   = FWOG_ANT_400MHZ;

        fwog_ioexp_cfg_t after = base;
        fwog_ioexp_set_antennas(&after, FWOG_ANT_900MHZ, FWOG_ANT_ISOLATION);

        /* Every non-antenna field survives, field by field -- comparing only
           the packed word would let two errors cancel. */
        ASSERT_EQ(after.uart_tx_out,  base.uart_tx_out);
        ASSERT_EQ(after.uart_cts_out, base.uart_cts_out);
        ASSERT_EQ(after.uart_rx_out,  base.uart_rx_out);
        ASSERT_EQ(after.uart_rts_out, base.uart_rts_out);
        ASSERT_EQ(after.spi_sclk_out, base.spi_sclk_out);
        ASSERT_EQ(after.spi_cs_out,   base.spi_cs_out);
        ASSERT_EQ(after.spi_rx_out,   base.spi_rx_out);
        ASSERT_EQ(after.spi_tx_out,   base.spi_tx_out);
        ASSERT_EQ(after.gpio25_rp_out, base.gpio25_rp_out);
        ASSERT_EQ(after.i2c_pullup,   base.i2c_pullup);
        ASSERT_EQ(after.io_config,    base.io_config);
        ASSERT_EQ(after.radio1_ant, FWOG_ANT_900MHZ);
        ASSERT_EQ(after.radio2_ant, FWOG_ANT_ISOLATION);

        /* And in the packed word, ONLY the four antenna bits differ. */
        const uint16_t ANT_MASK = 0x0010u | 0x0008u | 0x0004u | 0x0002u;
        const uint16_t wb = fwog_ioexp_pack(&base);
        const uint16_t wa = fwog_ioexp_pack(&after);
        ASSERT_EQ((uint16_t)((wb ^ wa) & (uint16_t)~ANT_MASK), 0u);

        /* Setting the antennas to what they already are must be a no-op. */
        fwog_ioexp_cfg_t same = base;
        fwog_ioexp_set_antennas(&same, base.radio1_ant, base.radio2_ant);
        ASSERT_EQ(fwog_ioexp_pack(&same), wb);
    }

    TEST_RETURN();
}
