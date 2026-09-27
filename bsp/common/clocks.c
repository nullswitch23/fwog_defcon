#include "common/clocks.h"

uint16_t fwog_vreg_mv_for_khz(uint32_t khz) {
    /* 133 MHz is the RP2040's datasheet maximum at the default voltage. */
    if (khz <= 133000u) return 1100u;
    return 1150u;   /* clamped -- see the header */
}

#ifndef HOST_TEST
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "pico/stdlib.h"

static enum vreg_voltage vreg_from_mv(uint16_t mv) {
    /* Only the two voltages fwog_vreg_mv_for_khz() can return are mapped.
       Keeping the pure function in millivolts is what lets it be tested
       without the SDK's enum. */
    return (mv >= 1150u) ? VREG_VOLTAGE_1_15 : VREG_VOLTAGE_1_10;
}

void fwog_clocks_init(uint32_t khz) {
    vreg_set_voltage(vreg_from_mv(fwog_vreg_mv_for_khz(khz)));
    sleep_ms(10);
    set_sys_clock_khz(khz, true);
}
#endif
