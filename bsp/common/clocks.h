/* The one copy of this board's clock bring-up. Both CPUs and the display
 * bootloader call it; it lived in duplicate in the two board.c files until
 * the bootloader became the third caller.
 *
 * Pure -- fwog_vreg_mv_for_khz() -- is separated from the hardware so the
 * voltage table is host-testable. See AGENTS.md step 3. */
#ifndef FWOG_CLOCKS_H
#define FWOG_CLOCKS_H
#include <stdint.h>

#ifndef FWOG_SYS_CLK_KHZ
#define FWOG_SYS_CLK_KHZ 200000u
#endif

/* Core voltage in millivolts for a requested system clock.
 *
 * Returns 1100 (the RP2040 power-on default) at or below the stock 133 MHz
 * ceiling, and 1150 above it. It deliberately CLAMPS at 1150 rather than
 * continuing up the vreg ladder: a higher voltage is an overclocking
 * decision, and this function must not make one on a battery-powered board
 * as a side effect of somebody passing a large number. */
uint16_t fwog_vreg_mv_for_khz(uint32_t khz);

#ifndef HOST_TEST
/* Voltage first, then settle, then clock. Skipping the settle delay is a
 * classic source of intermittent boot failures.
 *
 * This does NOT re-source clk_peri, and it must not: set_sys_clock_khz()
 * already does, and by default it parks clk_peri on PLL_USB at 48 MHz --
 * *away* from clk_sys. What keeps clk_peri at clk_sys is
 * PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK in bsp/boards/freewili_og.h.
 * Measured on hardware: clk_peri read 48 MHz until that was set, which caps
 * the inter-CPU link at 3 Mbaud and the LCD at 24 MHz. See the hardware record.
 *
 * Must run before anything that derives a divider from clock_get_hz(). */
void fwog_clocks_init(uint32_t khz);
#endif

#endif
