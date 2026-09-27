/* Authoritative pin map for the FreeWili OG *display* CPU (RP2040).
 *
 * This file — not bsp/boards/freewili_og.h, and not the legacy
 * FreeWilliDisplay_pin_definitions.h — is the source of truth. Transcribed
 * from the legacy header's non-fwV1 branch and rmpLib/st7789.hpp; every
 * value is asserted in tests/test_pinmap_display.c. */
#ifndef FWOG_DISPLAY_BOARD_H
#define FWOG_DISPLAY_BOARD_H
#include <stdbool.h>
#include <stdint.h>
#include "common/clocks.h"

/* Both CPUs' board.h use the same short macro names by design, and the bsp
 * root is on every include path. Without this guard a display translation
 * unit could include "main_cpu/platform/board.h" and silently redefine
 * PIN_LINK_TX and friends -- a macro-redefinition warning, not an error, with
 * the last definition winning. This makes it a build break instead. */
#if defined(FWOG_CPU_MAIN) && !defined(HOST_TEST)
#error "display platform/board.h included in a main-CPU translation unit"
#endif

/* ---- LCD: ST7789 320x240 on SPI1 ---- */
#define PIN_LCD_SCK        10
#define PIN_LCD_MOSI       11
#define PIN_LCD_DC         12   /* legacy DISPLAY_CS — the legacy name lies */
#define PIN_LCD_CS         13   /* legacy DISPLAY_LCD_CS_ACTIVE_LOW */
#define PIN_LCD_BL         25   /* PWM backlight */
#define PIN_LCD_TE         21   /* tearing effect; unused by legacy firmware */
/* Panel ceiling: 16 ns minimum between SPI edges. At a 200 MHz system clock
 * the achievable rate below this is 50 MHz — see the spec's clocks section. */
#define FWOG_LCD_MAX_HZ    62500000u

/* ---- Buttons: active low, internal pull-ups ---- */
#define PIN_BTN_GRAY       14
#define PIN_BTN_YELLOW     15
#define PIN_BTN_GREEN      22
#define PIN_BTN_BLUE       23
#define PIN_BTN_RED        24

/* ---- WS2812 chain ----
 * PIN_LED_DATA does not drive the LED chain's DIN directly: IC6, a
 * TC7SZ04F single-gate CMOS inverter, sits between this GPIO and LED1.DIN
 * (schematic FreeWili-Black-Blue rev5_FINAL.pdf sheet 4/12; identically
 * IC59 on FreeWili_2_23.pdf sheet 6/24). The WS2812 PIO program's side-set
 * polarity is therefore deliberately inverted relative to every stock
 * WS2812 example -- see the hardware record fact 33 and
 * leds/ws2812_driver.h -- and must NOT be "corrected" to match a textbook
 * ws2812.pio; doing so would drive the LEDs with the wrong waveform. */
#define PIN_LED_DATA       7    /* legacy LED_SERIAL */
#define FWOG_LED_COUNT     7    /* OG has 7. FW2's 16 is a different board. */

/* ---- IR ----
 * PIN_IR_TX (Q10, an N-channel MOSFET low-side switch) is NON-inverting:
 * GPIO high turns the LED on. PIN_IR_RX (IC15, a TSOP75238TR integrated
 * 38 kHz demodulator) is ACTIVE LOW: it idles high and pulses low while a
 * 38 kHz burst is detected -- the opposite of PIN_LED_DATA's inverted path.
 * See ir/ir_comm.h's traps 1 and 2 and recon-schematic.md for the schematic
 * evidence; do not "correct" either polarity to match a generic example. */
#define PIN_IR_TX          9
#define PIN_IR_RX          16

/* ---- PDM microphone ---- */
#define PIN_MIC_CLK        17
#define PIN_MIC_DATA       29

/* ---- I2S speaker ---- */
#define PIN_I2S_DIN        4
#define PIN_I2S_LRCLK      5
#define PIN_I2S_BCLK       6

/* ---- I2C1: charger, RTC, accel ---- */
/* These three are the whole bus. There is no BQ27441 fuel gauge on this
 * board -- it is DNP, never populated in production, and the class that
 * suggests otherwise is FreeWili 2's. See the hardware record. */
#define PIN_I2C_SDA        26
#define PIN_I2C_SCL        27
#define FWOG_I2C_HZ        400000u
#define I2C_ADDR_CHARGER   0x6B
#define I2C_ADDR_RTC       0x6F
#define I2C_ADDR_ACCEL     0x19   /* LIS3DH, SA0 high; 0x18 is the alternate (SA0 low) */

/* ---- Link to the main CPU (UART0) ----
 * Identical role mapping to the main CPU, because the RP2040 pad mux fixes
 * it: GPIO0 is UART0 TX, GPIO1 is UART0 RX, GPIO2 CTS, GPIO3 RTS. The
 * crossing happens in the copper, not in these numbers. The legacy header's
 * "UART0_RX 0 / UART0_TX 1" are NET names seen from the main CPU, not roles.
 */
#define PIN_LINK_TX        0
#define PIN_LINK_RX        1
#define PIN_LINK_CTS       2
#define PIN_LINK_RTS       3

/* ---- Main CPU boot control ----
 * PROVENANCE: everything in this block is reasoned from the schematic, not
 * measured on a board -- see the hardware record fact 38 for the full
 * trace and the "still unverified" list. The bench check has NOT run.
 *
 * GPIO 8 is the output enable of an SN74LVC1G126 tri-state buffer
 * (recon-schematic.md) that gates the RED BUTTON onto the MAIN CPU's
 * QSPI_SS pin -- which is main's BOOTSEL strap at power-on and its flash
 * chip select while running. The '126's OE is active HIGH, so:
 *
 *   LOW  = buffer disabled, red button disconnected from main. A red press
 *          should not disturb main's flash reads, by this reasoning. This
 *          is the running state and what board_init_pins() below
 *          establishes, matching the legacy firmware's
 *          obBootMainLockout.init(true, false, none)
 *          (FreeWilliDisplay.cpp:890) bit for bit. The legacy name --
 *          "BootMainLockout" -- is the confirmation.
 *   HIGH, or undriven = red button reaches main. This is predicted to be how
 *          red-as-BOOTSEL works. "Undriven" does NOT mean high-Z: an RP2040
 *          pad resets with IE=1, PDE=1 -- an internal pull-DOWN of roughly
 *          50-80 kOhm. Against R41's 10 kOhm pull-up to 3V3 that forms a
 *          divider: about 2.75 V at 50 kOhm and 2.93 V at 80 kOhm. The
 *          '126's VIH is 2.0 V, so the buffer should still enable -- but
 *          with only about 0.75 V of margin, not a hard rail. board.c's
 *          board_init_pins() already says this correctly ("floats on a weak
 *          pull-down") -- keep the two in agreement, do not reintroduce
 *          "high-Z" here.
 *
 * Timing, corrected: an earlier version of this comment argued there is no
 * timing window to get right because main's own board_init_pins()
 * (main_cpu/platform/board.c) drives PIN_GUI_NRESET low before main samples
 * its strap. That is impossible: board_init_pins() is application code, and
 * it cannot run until AFTER main's boot ROM has already sampled the strap
 * and started executing from XIP -- it cannot cause anything at t=0.
 *
 * The prediction that there is no timing window to get right should still
 * hold, but for a different reason: for the first several milliseconds
 * after power-on, no code has run on EITHER cpu yet, so this GPIO simply
 * sits at its own pad reset default -- the weak pull-down above --
 * regardless of what main does. GUI_NRESET's power-on state is untraced and
 * is not needed for this argument. board_init_pins() driving GUI_NRESET low
 * is real, but it governs the STEADY STATE once both CPUs are already
 * running, not the power-on instant a BOOTSEL strap sample cares about.
 *
 * By the same reasoning, recovery should be self-healing: any main reset
 * re-asserts GUI_NRESET, which resets the display and returns this pin to
 * its pad-default pull-down, so red-as-BOOTSEL should reopen on every main
 * reset however main got there.
 *
 * Consequence worth knowing: because no code has run on the display CPU at
 * the instant main samples its strap, this pin should always be at its weak
 * pull-down then, and firmware contributes nothing to red-as-BOOTSEL.
 * Whether that pull-down reads as enabled is decided by the '126's OE
 * termination -- it is R41, 10 kOhm to 3V3 (see the divider above), so by
 * this reasoning it does -- see recon-schematic.md and the hardware record for
 * the full trace and what is still unverified. */
#define PIN_MAIN_BOOT_OE   8

#ifndef HOST_TEST
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "hardware/uart.h"
#define FWOG_LCD_SPI    spi1
#define FWOG_I2C        i2c1
#define FWOG_LINK_UART  uart0
#endif

/* Split so a size-constrained context -- the display serial bootloader --
 * can bring up clocks and park pins without dragging in I2C or PWM: it must
 * not touch the battery charger on I2C1, and it has no use for the
 * backlight. Call order matters:
 *
 *   1. fwog_clocks_init() -- must run first, from common/clocks.h.
 *      board_init_pins() computes the backlight PWM divider from
 *      clock_get_hz(), which is wrong if read before the clock is set.
 *      (clk_peri tracks clk_sys only because the board header sets
 *      PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK -- see the hardware record.)
 *   2. board_init_pins() -- LCD control lines, backlight PWM, buttons,
 *      MAIN_BOOT_OE. No ordering requirement relative to board_init_i2c().
 *   3. board_init_i2c()  -- I2C1 (charger, RTC, accel).
 *
 * board_init() is the convenience wrapper: all three in order, then
 * fwog_diag_init() and a startup banner. Most apps just call board_init(). */
void board_init_pins(void);
void board_init_i2c(void);
void board_init(void);

/* Whether board_init()'s PCAL6416 bring-up succeeded. False before
 * board_init() has run, and false on a board whose expander does not answer
 * -- which is deliberately non-fatal, so this is the only way to find out
 * afterwards.
 *
 * Exists because board_init() reports the result via DIAG before an app has
 * had any chance to wait for a USB host, and pico_stdio_usb DROPS output
 * when nothing is attached (the hardware record). The line is therefore lost on
 * exactly the runs where someone is trying to read it. */
bool board_ioexp_ok(void);

/* Backlight brightness, 0-255, as PWM duty on PIN_LCD_BL.
 *
 * Only ever sets the duty. It must NOT call gpio_set_function() or touch
 * the divider: board_init_pins() derives that from clock_get_hz() and is
 * the sole configurer of the slice (the hardware record). The legacy driver's
 * set_backlight() switched the pin to SIO and drove it high, which
 * discards both the divider and any dimming.
 *
 * Requires board_init_pins() to have run. */
void board_backlight(uint8_t level);

#endif
