#include "test_util.h"
#include "common/io_cfg.h"
#include <string.h>

int main(void) {
    fwog_io_cfg_t c;
    fwog_io_cfg_default(&c);

    /* Default: main drives SPI clock/CS/MOSI and UART TX/RTS, receives MISO
       and RX/CTS, GPIO25 out, both radios on the 400 MHz path -- the same
       picture fwog_ioexp_default() establishes (tests/test_ioexp.c). */
    ASSERT_TRUE(c.spi_sclk_out);
    ASSERT_TRUE(c.spi_cs_out);
    ASSERT_TRUE(c.spi_tx_out);
    ASSERT_TRUE(!c.spi_rx_out);
    ASSERT_TRUE(c.uart_tx_out);
    ASSERT_TRUE(c.uart_rts_out);
    ASSERT_TRUE(!c.uart_rx_out);
    ASSERT_TRUE(!c.uart_cts_out);
    ASSERT_TRUE(c.gpio25_out);
    ASSERT_TRUE(c.i2c_pullup);
    ASSERT_EQ(c.radio1_ant, FWOG_ANT_400MHZ);
    ASSERT_EQ(c.radio2_ant, FWOG_ANT_400MHZ);
    /* FreeWilliMain.cpp:1935 -- GPIO 26 comes up an INPUT and 27 an OUTPUT
       (obIOGPIO26_in.init(false,...), obIOGPIO27_out.init(true,...)). */
    ASSERT_TRUE(!c.gpio26_out);
    ASSERT_TRUE(c.gpio27_out);

    /* --- The 9-bit direction word, from FreeWilliMain.cpp:583-618 --- */
    memset(&c, 0, sizeof c);
    c.uart_rts_out = true;  ASSERT_EQ(fwog_io_pack_dirword(&c), 0x0001u);
    memset(&c, 0, sizeof c);
    c.uart_rx_out  = true;  ASSERT_EQ(fwog_io_pack_dirword(&c), 0x0002u);
    memset(&c, 0, sizeof c);
    c.uart_tx_out  = true;  ASSERT_EQ(fwog_io_pack_dirword(&c), 0x0004u);
    memset(&c, 0, sizeof c);
    c.spi_tx_out   = true;  ASSERT_EQ(fwog_io_pack_dirword(&c), 0x0008u);
    memset(&c, 0, sizeof c);
    c.spi_rx_out   = true;  ASSERT_EQ(fwog_io_pack_dirword(&c), 0x0010u);
    memset(&c, 0, sizeof c);
    c.spi_cs_out   = true;  ASSERT_EQ(fwog_io_pack_dirword(&c), 0x0020u);
    memset(&c, 0, sizeof c);
    c.spi_sclk_out = true;  ASSERT_EQ(fwog_io_pack_dirword(&c), 0x0040u);
    memset(&c, 0, sizeof c);
    c.uart_cts_out = true;  ASSERT_EQ(fwog_io_pack_dirword(&c), 0x0080u);
    memset(&c, 0, sizeof c);
    c.gpio25_out   = true;  ASSERT_EQ(fwog_io_pack_dirword(&c), 0x0100u);

    /* GPIO 26/27 are FPGA-only and must NOT appear in the wire word. */
    memset(&c, 0, sizeof c);
    c.gpio26_out = true; c.gpio27_out = true;
    ASSERT_EQ(fwog_io_pack_dirword(&c), 0x0000u);

    /* --- The FPGA's two bytes, from FreeWilliMain.cpp:605-610 ---
       byte 0 is the LOW byte of the dirword; byte 1 is (GP26<<1)|(GP27<<0).
       GPIO 25 is deliberately ABSENT: "FPGA does not care about GP25". */
    uint8_t f[2];
    fwog_io_cfg_default(&c);
    fwog_io_pack_fpga(&c, f);
    ASSERT_EQ(f[0], (uint8_t)(fwog_io_pack_dirword(&c) & 0xFFu));

    memset(&c, 0, sizeof c);
    c.gpio25_out = true;              /* sets 0x0100 -- the HIGH byte */
    fwog_io_pack_fpga(&c, f);
    ASSERT_EQ(f[0], 0x00u);           /* must not leak into byte 0 */
    ASSERT_EQ(f[1], 0x00u);           /* must not leak into byte 1 */

    memset(&c, 0, sizeof c);
    c.gpio26_out = true;
    fwog_io_pack_fpga(&c, f);
    ASSERT_EQ(f[1], 0x02u);
    memset(&c, 0, sizeof c);
    c.gpio27_out = true;
    fwog_io_pack_fpga(&c, f);
    ASSERT_EQ(f[1], 0x01u);

    /* --- Round trip through the wire word --- */
    fwog_io_cfg_t back;
    fwog_io_cfg_default(&c);
    memset(&back, 0, sizeof back);
    fwog_io_unpack_dirword(fwog_io_pack_dirword(&c), &back);
    ASSERT_EQ(fwog_io_pack_dirword(&back), fwog_io_pack_dirword(&c));

    TEST_RETURN();
}
