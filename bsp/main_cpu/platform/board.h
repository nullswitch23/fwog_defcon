/* Authoritative pin map for the FreeWili OG *main* CPU (RP2040).
 *
 * Transcribed from FreeWilliMain_pin_definitions.h's final #else ("v4+")
 * branch; every value is asserted in tests/test_pinmap_main.c. */
#ifndef FWOG_MAIN_BOARD_H
#define FWOG_MAIN_BOARD_H
#include <stdint.h>
#include "common/clocks.h"

/* See the note in the display board.h: this converts a silent wrong-pin-map
 * hazard into a compile error. */
#if defined(FWOG_CPU_DISPLAY) && !defined(HOST_TEST)
#error "main platform/board.h included in a display-CPU translation unit"
#endif

/* ---- Two CC1101 sub-GHz radios sharing one SPI bus ---- */
#define PIN_CC1101_MISO     4
#define PIN_CC1101_CS0      5
#define PIN_CC1101_SCLK     6
#define PIN_CC1101_MOSI     7
#define PIN_CC1101_CS1      18
#define PIN_CC1101_GDO2_2   19
#define PIN_CC1101_GDO0_1   20
#define PIN_CC1101_GDO0_2   21
#define PIN_CC1101_GDO2_1   22

/* ---- iCE40 FPGA ---- */
#define PIN_FPGA_CLK        23
#define PIN_FPGA_DONE       24
#define PIN_FPGA_RESET      29   /* asserted LOW; polarity per legacy driver */

/* ---- Board ---- */
#define PIN_LED             25
#define PIN_GUI_NRESET      28   /* holds the display CPU in reset when LOW */

/* ---- Link to the display CPU (UART0). Crosses: our RX is display's TX. -- */
#define PIN_LINK_TX         0
#define PIN_LINK_RX         1
#define PIN_LINK_CTS        2
#define PIN_LINK_RTS        3

/* ---- User breakout I/O ---- */
#define PIN_IO_SPI_MISO     12
#define PIN_IO_SPI_CS       13
#define PIN_IO_SPI_SCLK     14
#define PIN_IO_SPI_MOSI     15
/* I2C0 on GPIO 16/17 is a USER BREAKOUT bus, not a system bus. Every I2C
 * device on this board (charger, RTC, accel) is display-side, on
 * the display CPU's I2C1 -- see display_cpu/platform/board.h. board_init()
 * therefore never touches these two pins: it leaves them as plain,
 * un-muxed, un-pulled-up GPIO. An application that wants the breakout I2C
 * bus calls board_init_i2c() itself. */
#define PIN_IO_I2C_SDA      16
#define PIN_IO_I2C_SCL      17
#define PIN_IO_UART_TX      8
#define PIN_IO_UART_RX      9
#define PIN_IO_UART_CTS     10
#define PIN_IO_UART_RTS     11
#define PIN_IO_GPIO_IN      26
#define PIN_IO_GPIO_OUT     27

#define PIN_I2C_SDA         PIN_IO_I2C_SDA
#define PIN_I2C_SCL         PIN_IO_I2C_SCL
#define FWOG_I2C_HZ         400000u

#ifndef HOST_TEST
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "hardware/uart.h"
#define FWOG_RADIO_SPI  spi0
#define FWOG_I2C        i2c0
#define FWOG_LINK_UART  uart0
#define FWOG_FPGA_SPI   spi1   /* breakout SPI on GPIO 12/13/14/15 */
#endif

/* Split for the same reason as the display CPU's board_init() (see that
 * board.h): callers who need to sequence bring-up precisely, and so a
 * future size-constrained context on this CPU can skip pieces it doesn't
 * need. Call order matters:
 *
 *   1. fwog_clocks_init() -- must run first, from common/clocks.h.
 *      Peripheral dividers computed from clock_get_hz() are wrong if
 *      read before the clock is set.
 *   2. board_init_pins() -- parks both radio chip selects HIGH, holds the
 *      FPGA and the display CPU in reset, and sets up the status LED.
 *
 * board_init_i2c() brings up I2C0 on the user breakout header (GPIO 16/17).
 * board_init() deliberately does NOT call it: I2C0 is not a system bus on
 * this CPU (see the PIN_IO_I2C_SDA/SCL comment above), so leaving it out of
 * the automatic sequence is the fix, not an oversight. Call
 * board_init_i2c() yourself if your app uses the breakout I2C bus.
 *
 * board_init() is the convenience wrapper: fwog_clocks_init(), then
 * board_watchdog_init() (before anything else can hang -- it's the only
 * recovery path this CPU has), then board_init_pins(), then fwog_diag_init()
 * and a startup banner, then fwog_fpga_clk_start() (25 MHz on GPIO 23, the
 * iCE40's configuration and system clock), then -- watchdog paused around
 * it -- fwog_ice40_load_default(), which blocks for roughly 170-200 ms
 * clocking the embedded 104 KB default bitstream (fpga/fpga_bitstream.h)
 * into the FPGA over SPI1 at 5 MHz and reports CDONE. That loader is linked
 * into every main-CPU app that calls board_init() -- which is every one of
 * them -- so every image built against fwog_main_bsp carries that 104 KB
 * regardless of whether the app itself ever touches fpga/ice40.h (see that
 * header's note on the property). A dead or unconfigured FPGA is logged,
 * not fatal: board_init() still returns so a board with no working FPGA can
 * say so, the same rule board_ioexp_ok() follows on the display side. */
void board_init_pins(void);
void board_init_i2c(void);
void board_init(void);

/* Releases GUI_NRESET so the display CPU boots.
 *
 * board_init() deliberately leaves the display held in reset: that is what
 * lets a main-CPU application bring the link up FIRST and then release the
 * display, so it catches the display's boot announcement rather than racing
 * it. Every main-CPU app must call this once it is ready, or the display
 * never runs at all. */
void board_release_display(void);

#endif
