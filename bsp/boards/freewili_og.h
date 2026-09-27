/*
 * FreeWili OG (Classic) board header for the Raspberry Pi Pico SDK.
 *
 * Deliberately thin. It carries only facts true of BOTH RP2040s on the board;
 * the authoritative pin maps live in bsp/display_cpu/platform/board.h and
 * bsp/main_cpu/platform/board.h. In particular there is no default LED, I2C,
 * or SPI here, because those differ between the two CPUs.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

#ifndef _BOARDS_FREEWILI_OG_H
#define _BOARDS_FREEWILI_OG_H

pico_board_cmake_set(PICO_PLATFORM, rp2040)

// For board detection
#define FREEWILI_OG

// --- FLASH ---
// 16 MB external QSPI flash on both CPUs. CLKDIV 4 keeps flash at clk_sys/4,
// i.e. 50 MHz at the 200 MHz default, which leaves generous XIP margin.
//
// This value is load-bearing across the bootloader-to-app handoff: the
// bootloader jumps past the app's boot2, so whatever XIP configuration the
// bootloader established stays in force. Both binaries must take it from
// here so they cannot disagree.
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 4
#endif

pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (16 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

// --- PERIPHERAL CLOCK ---
// clk_peri follows clk_sys (200 MHz) instead of the SDK's default 48 MHz.
//
// This is NOT optional dressing: it is what makes the documented UART and SPI
// rates reachable. clk_peri is the clock feeding both, so at the SDK default
// the inter-CPU link would cap at 48/16 = 3 Mbaud and the ST7789 at 48/2 =
// 24 MHz -- against the 12.5 Mbaud and 50 MHz this BSP is designed around.
//
// Measured on hardware before this line existed: clk_sys 200 MHz, clk_peri
// 48 MHz. set_sys_clock_pll() does re-source clk_peri, but by default it
// moves it to PLL_USB, AWAY from clk_sys (SDK 2.3.0 hardware_clocks/clocks.c,
// the PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK #else branch). The SDK
// picks 48 MHz "to ensure continuity of peripheral operation" -- so that
// changing the system clock does not silently change every baud rate. That is
// a convenience default, not a limit on how fast clk_peri may run.
//
// The tradeoff we accept by setting this: peripheral rates now move with
// FWOG_SYS_CLK_KHZ. That is precisely why drivers must derive dividers and
// baud from clock_get_hz() rather than hardcoding them (the hardware record).
#ifndef PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK
#define PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK 1
#endif

// --- UART ---
// UART0 is the inter-CPU link on BOTH CPUs and must never carry stdio.
// Diagnostics go over USB CDC; see bsp/common/diag.h.
//
// PICO_DEFAULT_UART is deliberately LEFT UNDEFINED. This does NOT make an
// accidental pico_stdio_uart link fail to compile: the SDK wraps the body of
// stdio_uart_init() in "#ifdef uart_default", so with no default UART it just
// compiles down to an empty function -- a silent no-op, not a build break.
//
// What actually protects the link is `set(PICO_STDIO_UART 0 ... FORCE)` in
// the top-level CMakeLists.txt, set before pico_sdk_init(). The SDK's
// per-target stdio-UART property falls back to that global cache variable
// whenever a target never calls pico_enable_stdio_uart(), so an accidental
// link of pico_stdio_uart still can't wire itself onto GPIO 0/1. Leaving
// PICO_DEFAULT_UART undefined is still worth keeping: turning an accidental
// link into a silent no-op, instead of a target that quietly compiles with a
// live default UART, is one more layer between a mistake and link corruption.

// --- LED / I2C / SPI ---
// Intentionally absent: they differ per CPU. See each platform/board.h.

#endif
