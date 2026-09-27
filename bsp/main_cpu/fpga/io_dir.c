#include "fpga/io_dir.h"
#include "fpga/ice40.h"
#include "platform/board.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"

/* FW1 command opcodes -- see io_dir.h. These are NOT the FreeWili 2 values,
 * and the difference is not cosmetic: in FW1's encoding bit 7 is the RESET
 * command, so FW2's read opcode (0x80) is a reset here. */
#define FPGA_CMD_WR 0x40u   /* 1 << 6 */
#define FPGA_CMD_RD 0x20u   /* 1 << 5 */

bool fwog_fpga_dir_write(const uint8_t d[2]) {
    const uint8_t cmd = (uint8_t)(FPGA_CMD_WR | FWOG_FPGA_REG_IOBUFFER);

    fwog_ice40_spi_claim(FWOG_FPGA_REG_HZ);
    gpio_put(PIN_IO_SPI_CS, 0);
    spi_write_blocking(FWOG_FPGA_SPI, &cmd, 1);
    spi_write_blocking(FWOG_FPGA_SPI, d, 2);
    gpio_put(PIN_IO_SPI_CS, 1);
    fwog_ice40_spi_release();
    return true;   /* SPI writes cannot fail; the readback is the check */
}

bool fwog_fpga_dir_read(uint8_t d[2]) {
    const uint8_t cmd = (uint8_t)(FPGA_CMD_RD | FWOG_FPGA_REG_IOBUFFER);
    const uint8_t tx = 0u;

    fwog_ice40_spi_claim(FWOG_FPGA_REG_HZ);
    gpio_put(PIN_IO_SPI_CS, 0);
    spi_write_blocking(FWOG_FPGA_SPI, &cmd, 1);
    /* spi_read_blocking() loops on (rx_remaining || tx_remaining) until both
       hit zero -- it cannot return before all `len` bytes of d[] are
       written, which is what lets this function satisfy io_seq.h's "may
       return true only after writing BOTH bytes" contract. */
    spi_read_blocking(FWOG_FPGA_SPI, tx, d, 2);
    gpio_put(PIN_IO_SPI_CS, 1);
    fwog_ice40_spi_release();
    return true;
}
