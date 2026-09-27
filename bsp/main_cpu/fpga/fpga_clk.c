#include "fpga/fpga_clk.h"

uint32_t fwog_fpga_clk_divider(uint32_t sys_clk_hz) {
    if (sys_clk_hz < FWOG_FPGA_CLK_HZ) return 1u;
    /* Round up: a divider that is too small overclocks the gateware, which
       is the failure that matters. Too large merely runs it slow. */
    uint32_t d = (sys_clk_hz + FWOG_FPGA_CLK_HZ - 1u) / FWOG_FPGA_CLK_HZ;
    return d ? d : 1u;
}

uint32_t fwog_fpga_clk_hz_for(uint32_t sys_clk_hz, uint32_t div_int,
                              uint8_t div_frac) {
    /* The GPOUT divider is int.frac in units of 1/256, so the produced rate is
       sys * 256 / (int*256 + frac). Done in 64-bit: sys_clk_hz * 256 overflows
       32 bits above ~16.7 MHz, which every real clk_sys exceeds. */
    const uint64_t den = (uint64_t)div_int * 256u + (uint64_t)div_frac;
    if (den == 0u) return 0u;
    return (uint32_t)(((uint64_t)sys_clk_hz * 256u + den / 2u) / den);
}

#ifndef HOST_TEST
#include "hardware/clocks.h"
#include "platform/board.h"
#include "common/diag.h"

uint32_t fwog_fpga_clk_start(void) {
    uint32_t sys = (uint32_t)clock_get_hz(clk_sys);
    uint32_t div = fwog_fpga_clk_divider(sys);
    clock_gpio_init_int_frac(PIN_FPGA_CLK,
                             CLOCKS_CLK_GPOUT1_CTRL_AUXSRC_VALUE_CLK_SYS,
                             div, 0);
    uint32_t hz = sys / div;
    DIAG("[fpga] clk %u Hz (clk_sys %u / %u)\n",
         (unsigned)hz, (unsigned)sys, (unsigned)div);
    return hz;
}

uint32_t fwog_fpga_clk_start_div(uint32_t div_int, uint8_t div_frac) {
    const uint32_t sys = (uint32_t)clock_get_hz(clk_sys);
    if (div_int == 0u) div_int = 1u;
    clock_gpio_init_int_frac(PIN_FPGA_CLK,
                             CLOCKS_CLK_GPOUT1_CTRL_AUXSRC_VALUE_CLK_SYS,
                             div_int, div_frac);
    const uint32_t hz = fwog_fpga_clk_hz_for(sys, div_int, div_frac);
    DIAG("[fpga] clk %u Hz (clk_sys %u / %u+%u/256)\n",
         (unsigned)hz, (unsigned)sys, (unsigned)div_int, (unsigned)div_frac);
    return hz;
}
#endif
