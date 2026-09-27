/* The FPGA's io_buffer direction register, from fwProgIODir.
 *
 * Transaction shape: CS low, one command byte, then the data bytes, CS high.
 * The command byte is (opcode | addr).
 *
 * ---- THE OPCODES AND THE ADDRESS ARE FreeWili 1's. DO NOT "MODERNISE". ----
 *
 * These were WRONG until 2026-07-28 and it was caught on hardware. The values
 * had been ported from the MULTI-TARGET FreeWili firmware tree, whose
 * fwProgIODir.h is the FreeWili **2** version. The two disagree completely:
 *
 *                     FW1 (FW1-only tree) FW2 (multi-target tree)
 *   io_buffer addr    0x01                0x02
 *   write opcode      0x40  (1 << 6)      0x00
 *   read  opcode      0x20  (1 << 5)      0x80  (1 << 7)
 *   reset opcode      0x80  (1 << 7)      --
 *
 * So the FW2 read command 0x80|0x02 = 0x82, issued against FW1 gateware,
 * sets the bit FW1 decodes as RESET -- every readback attempt was asking the
 * FPGA to reset, at the wrong register, having written with an opcode that
 * means nothing. `fwog_io_dir_apply()` returned FWOG_IO_ERR_FPGA_VERIFY
 * (result=4) on a board as a direct result.
 *
 * It looked correct for a long time because it was self-consistent with the
 * bitstream that was also taken from that same FW2-contaminated tree: the two
 * wrongs agreed with each other and `iodir` returned OK. See
 * fpga/fpga_bitstream.h for the matching bitstream correction, and note the
 * general lesson -- a protocol and a gateware sourced from the same wrong
 * place will validate each other perfectly.
 *
 * Authority for these values: the FreeWili 1-only firmware repository at tag
 * `spartahackFw1final`, freewilimain/fwProgIODir.{h,cpp} -- the last FW1
 * release.
 *
 * Only the io_buffer register is ported. HSBDIO (0x03), the I2C-slave address
 * register (0x04) and, in the FW2 tree, the SRAM swap handshake and CM0
 * mailbox are not used by this BSP.
 *
 * Only the SPI transport is ported. The reference's I2C alternative talks to
 * an FPGA i2c_slave at 0x30 over I2C0 -- which on THIS board is a user
 * breakout bus, not a system bus -- and a second transport for one register
 * doubles the surface for nothing.
 *
 * These run at 3.25 MHz, a DIFFERENT rate from configuration's 5 MHz, which
 * is why every call brackets itself with fwog_ice40_spi_claim()/_release()
 * exactly as the reference's claimSPI()/restoreSPI() do. */
#ifndef FWOG_FPGA_IO_DIR_H
#define FWOG_FPGA_IO_DIR_H
#include <stdbool.h>
#include <stdint.h>

#define FWOG_FPGA_REG_IOBUFFER 0x01u   /* FW1's btIOBufferAddr, NOT FW2's 0x02 */
#define FWOG_FPGA_REG_HZ       3250000u

bool fwog_fpga_dir_write(const uint8_t d[2]);
bool fwog_fpga_dir_read(uint8_t d[2]);

#endif
