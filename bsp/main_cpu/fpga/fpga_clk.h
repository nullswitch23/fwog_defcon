/* The clock MAIN supplies to the iCE40 on PIN_FPGA_CLK (GPIO 23).
 *
 * The gateware in freewilifpga/ is synthesised for a 31.25 MHz core clock
 * (its README). We run it at 25 MHz, which at this BSP's 200 MHz clk_sys is
 * an EXACT integer divider of 8 -- no fractional part -- and comfortably
 * inside the design point.
 *
 * WHY THIS IS COMPUTED AND NOT COPIED. The reference defaults to
 * clk_sys/4 (MenuX/fwMenuFPGAClockSettings.h:24). That is 31.25 MHz at its
 * 125 MHz clk_sys, but 50 MHz at our 200 MHz -- it would overclock the part
 * by 60%. This is the standing "never hardcode a divider" invariant
 * (AGENTS.md) in its sharpest form: the one constant copied unchanged from
 * the reference would have been wrong by 2x. */
#ifndef FWOG_FPGA_CLK_H
#define FWOG_FPGA_CLK_H
#include <stdint.h>

#define FWOG_FPGA_CLK_HZ 25000000u

/* Integer divider from clk_sys to FWOG_FPGA_CLK_HZ, rounded UP so the
 * result is never faster than the target. Never returns 0. Pure, so the
 * arithmetic is host-tested against both this board's 200 MHz and the SDK's
 * 125 MHz default. */
uint32_t fwog_fpga_clk_divider(uint32_t sys_clk_hz);

/* clk_sys * 256 / (div_int * 256 + div_frac), rounded. Pure, so the
 * fractional arithmetic is host-tested rather than trusted on a bench. */
uint32_t fwog_fpga_clk_hz_for(uint32_t sys_clk_hz, uint32_t div_int,
                              uint8_t div_frac);

#ifndef HOST_TEST
/* Drive PIN_FPGA_CLK from clk_sys via clk_gpout1 at FWOG_FPGA_CLK_HZ.
 * Requires fwog_clocks_init() to have run -- the divider comes from
 * clock_get_hz(clk_sys). Returns the frequency actually produced. */
uint32_t fwog_fpga_clk_start(void);

/* Re-drive PIN_FPGA_CLK with an EXPLICIT divider, for experiments.
 *
 * This exists because the 25 MHz above is a DEVIATION from the gateware's
 * synthesised 31.25 MHz design point, taken on the reasoning that slower is
 * safer. That reasoning holds for setup timing and not for anything
 * rate-dependent, and on 2026-07-28 a breakout direction change was accepted
 * by all three surfaces -- FPGA register readback included -- while the
 * shifter direction pins never moved. A register clocked by the host's SCLK
 * working while the FPGA's own clock domain does not is exactly the shape of
 * that failure, so the clock has to become a variable we can sweep rather
 * than a constant we assume.
 *
 * 31.25 MHz needs a FRACTIONAL divider from this BSP's 200 MHz clk_sys
 * (200/6.4), and the RP2040's GPOUT fractional divider dithers -- the average
 * is right, individual periods are not. That jitter is itself a confound, so
 * a positive result here means "the clock rate is implicated", not "31.25 MHz
 * is the fix". Reproduce a clean result at clk_sys 125 MHz / divide 4, the way
 * the FW1 release firmware does it, before believing it.
 *
 * Returns the frequency produced. Does NOT reconfigure the FPGA -- the caller
 * decides whether to reload, since a bitstream reload is the cleaner way to
 * start the gateware on a clock it has never seen. */
uint32_t fwog_fpga_clk_start_div(uint32_t div_int, uint8_t div_frac);
#endif

#endif
