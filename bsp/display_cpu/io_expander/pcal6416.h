/* NXP PCAL6416A 16-bit I2C GPIO expander at 0x21 on the display CPU's I2C1.
 *
 * Ported from rmpLib/fwIOExpand.{h,cpp}, non-FWPURPLE_VARIANT path. Every
 * bit position and antenna encoding below was read out of that file; do not
 * "tidy" them.
 *
 * Despite living on the display's bus, almost everything this part drives
 * belongs to the MAIN CPU: the direction of its breakout level shifters,
 * the breakout I2C pull-ups, and the antenna path for both CC1101 radios.
 * The display owns it only because that is where the I2C bus is.
 *
 * The part is a separate chip on I2C: it does NOT reset when the display
 * RP2040 resets, so its contents survive a reboot into the bootloader.
 *
 * ---- WHO writes it, and when ----
 *
 * The display CPU's board_init(), and nothing else. In particular the
 * display serial bootloader must NOT write it. An earlier version of this
 * header said the bootloader "must too"; that was settled against on
 * 2026-07-27 and the reasoning is under "SETTLED" in the PCAL6416 note in
 * the peripheral catalog. The short form: the bootloader cannot reach
 * this chip without bringing up I2C1, which also carries the BQ25896
 * charger where one write powers the board off (the hardware record) -- and it
 * deliberately does not bring up I2C for exactly that reason. The write
 * buys nothing there anyway, because everything it changes serves main's
 * radios and the breakout header, neither of which can be in use while the
 * display sits in its bootloader waiting for main.
 *
 * One consequence to know: on a board where a display app has never yet
 * run, this chip has never been written. Because it survives RP2040 resets,
 * that window closes permanently the first time any display app boots.
 *
 * Whether port 1's antenna-select and pull-up-enable bits are externally
 * pulled at all is NOT settled -- see the same catalog note. It does not
 * change who writes this chip. */
#ifndef FWOG_PCAL6416_H
#define FWOG_PCAL6416_H
#include <stdbool.h>
#include <stdint.h>
#include "common/io_cfg.h"   /* fwog_ant_t now lives here -- see that header */

#define FWOG_IOEXP_ADDR 0x21u

/* PCAL6416 register map. Port pairs auto-increment, so OUTPUT0 + 2 bytes
 * writes both output ports in one transfer. */
#define PCAL6416_REG_INPUT0   0x00u
#define PCAL6416_REG_INPUT1   0x01u
#define PCAL6416_REG_OUTPUT0  0x02u
#define PCAL6416_REG_OUTPUT1  0x03u
#define PCAL6416_REG_CONFIG0  0x06u
#define PCAL6416_REG_CONFIG1  0x07u

/* The `*_out` fields are the direction of the MAIN CPU's breakout level
 * shifters: true means main drives that line, false means main receives on
 * it. The GPIO numbers in the comments are main-CPU numbers. */
typedef struct {
    bool spi_sclk_out;    /* main GPIO 14 */
    bool spi_cs_out;      /* main GPIO 13 */
    bool spi_rx_out;      /* main GPIO 12 */
    bool spi_tx_out;      /* main GPIO 15 */
    bool uart_tx_out;     /* main GPIO 8  */
    bool uart_rx_out;     /* main GPIO 9  */
    bool uart_rts_out;    /* main GPIO 11 */
    bool uart_cts_out;    /* main GPIO 10 */
    bool gpio25_rp_out;
    bool i2c_pullup;      /* breakout I2C bus pull-ups */
    bool io_config;
    fwog_ant_t radio1_ant;
    fwog_ant_t radio2_ant;
} fwog_ioexp_cfg_t;

/* The state this board comes up in: main driving SPI clock/CS/MOSI and
 * UART TX/RTS, receiving MISO and RX/CTS, breakout pull-ups on, both radios
 * on the 400 MHz path. Packs to 0xDAB8. */
void fwog_ioexp_default(fwog_ioexp_cfg_t *cfg);

/* Set BOTH antenna paths and change nothing else.
 *
 * Exists because "change the antennas" was being done by building a fresh
 * fwog_ioexp_default() and writing the whole 16-bit word -- which also reset
 * io_config and all nine level-shifter directions, silently, while the
 * comment above it claimed the opposite (apps/bench/display's `ant`, fixed
 * 2026-07-28). The expander has ONE state and every writer must start from
 * it, not from the defaults. Pure, so "only the antenna bits moved" is
 * host-tested rather than asserted. */
void fwog_ioexp_set_antennas(fwog_ioexp_cfg_t *cfg,
                             fwog_ant_t radio1, fwog_ant_t radio2);

/* Compose the 16-bit output word. The high byte goes to OUTPUT0 and the low
 * byte to OUTPUT1 -- the legacy code's "IO directions are in Little Endian"
 * comment describes the struct, not the wire: it writes the high byte
 * first. Pure, so the bit map is host-tested. */
uint16_t fwog_ioexp_pack(const fwog_ioexp_cfg_t *cfg);

#ifndef HOST_TEST
/* Write the configuration. Requires board_init_i2c() to have run. */
bool fwog_ioexp_apply(const fwog_ioexp_cfg_t *cfg);

/* Configure all 16 pins as outputs, then apply the default. Requires
 * board_init_i2c(). Called from the display CPU's board_init() and from
 * nowhere else -- see the note at the top of this file about why the
 * bootloader is deliberately not a caller.
 *
 * Returns false if the part does not answer. Callers should treat that as
 * non-fatal: a board with a dead expander should still boot far enough to
 * say so. */
bool fwog_ioexp_init(void);
#endif

#endif
