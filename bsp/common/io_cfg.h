/* The breakout I/O picture, shared by both CPUs.
 *
 * On this board a breakout line's direction is set in THREE places that must
 * agree: the FPGA's io_buffer block (main), the PCAL6416's level-shifter
 * direction bits (display), and the RP2040's own pad direction (main). If
 * they disagree, an output drives an output.
 *
 * This header is the single vocabulary all three are expressed in. It lives
 * under common/ rather than display_cpu/ because fwog_io_dir_apply() runs on
 * MAIN while the expander driver that consumes fwog_ant_t runs on DISPLAY --
 * and duplicating an antenna encoding that must agree byte-for-byte across a
 * link is exactly the drift this repo guards against elsewhere.
 *
 * Bit values below are transcribed from FreeWilliMain.cpp:583-618 and
 * confirmed against the display-side decoder at
 * rpProcessHostCommand.cpp:66-75, which agree. Do not "tidy" them. */
#ifndef FWOG_IO_CFG_H
#define FWOG_IO_CFG_H
#include <stdbool.h>
#include <stdint.h>

/* CC1101 antenna path. ISOLATION is "connected to nothing" and is the safe
 * value: it is what an unknown selection falls back to.
 *
 * Moved here from display_cpu/io_expander/pcal6416.h, which now includes
 * this header. The encoding is unchanged and tests/test_ioexp.c still pins
 * it against the legacy 0xDAB8. */
typedef enum {
    FWOG_ANT_ISOLATION = 0,
    FWOG_ANT_200MHZ,
    FWOG_ANT_400MHZ,
    FWOG_ANT_900MHZ
} fwog_ant_t;

/* `*_out` is true when MAIN drives the line and false when main receives on
 * it. GPIO numbers are main-CPU numbers. */
typedef struct {
    bool spi_tx_out;      /* GPIO 15 */
    bool spi_rx_out;      /* GPIO 12 */
    bool spi_cs_out;      /* GPIO 13 */
    bool spi_sclk_out;    /* GPIO 14 */
    bool uart_tx_out;     /* GPIO 8  */
    bool uart_rx_out;     /* GPIO 9  */
    bool uart_cts_out;    /* GPIO 10 */
    bool uart_rts_out;    /* GPIO 11 */
    bool gpio25_out;      /* expander only -- the FPGA does not see this.
                             Main's GPIO 25 is PIN_LED (main_cpu/platform/
                             board.h), driven GPIO_OUT permanently by
                             board_init_pins(); op_pads_apply()
                             (gpio/breakout.c) deliberately has no pad write
                             for GPIO 25 at all. The only self-consistent
                             value is therefore `true` -- fwog_io_dir_apply()
                             rejects `false` with FWOG_IO_ERR_UNSUPPORTED
                             before touching anything. This field exists
                             because the 9-bit wire word
                             (FWOG_IO_DIR_GPIO25 below) carries a bit for it,
                             not because the value is free to choose. */
    bool gpio26_out;      /* FPGA only -- not on the expander */
    bool gpio27_out;      /* FPGA only -- not on the expander */
    bool i2c_pullup;      /* breakout I2C bus pull-ups */
    fwog_ant_t radio1_ant;
    fwog_ant_t radio2_ant;
} fwog_io_cfg_t;

/* The 9-bit direction word that crosses the link. NOT a PCAL6416 register
 * word -- that is fwog_ioexp_pack()'s job, from a fwog_ioexp_cfg_t, and has
 * a completely different bit layout. */
#define FWOG_IO_DIR_UART_RTS  0x0001u   /* GPIO 11 */
#define FWOG_IO_DIR_UART_RX   0x0002u   /* GPIO 9  */
#define FWOG_IO_DIR_UART_TX   0x0004u   /* GPIO 8  */
#define FWOG_IO_DIR_SPI_TX    0x0008u   /* GPIO 15 */
#define FWOG_IO_DIR_SPI_RX    0x0010u   /* GPIO 12 */
#define FWOG_IO_DIR_SPI_CS    0x0020u   /* GPIO 13 */
#define FWOG_IO_DIR_SPI_SCLK  0x0040u   /* GPIO 14 */
#define FWOG_IO_DIR_UART_CTS  0x0080u   /* GPIO 10 */
#define FWOG_IO_DIR_GPIO25    0x0100u   /* expander only */

/* The state this board comes up in, matching fwog_ioexp_default(). */
void fwog_io_cfg_default(fwog_io_cfg_t *cfg);

uint16_t fwog_io_pack_dirword(const fwog_io_cfg_t *cfg);

/* Apply a received direction word to a config. Leaves gpio26/27, the
 * antennas and i2c_pullup untouched -- they do not travel in this word. */
void fwog_io_unpack_dirword(uint16_t word, fwog_io_cfg_t *cfg);

/* The FPGA's two register bytes. out[0] is the LOW byte of the direction
 * word; out[1] is (gpio26 << 1) | (gpio27 << 0).
 *
 * GPIO 25 is deliberately absent: the reference comments "FPGA does not
 * care about GP25" and only sends the low byte. Do not "fix" this by
 * widening it -- the gateware's register is two bytes. */
void fwog_io_pack_fpga(const fwog_io_cfg_t *cfg, uint8_t out[2]);

#endif
