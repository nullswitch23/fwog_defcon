#include "test_util.h"
#include "common/clocks.h"

int main(void) {
    /* The RP2040 power-on default. Anything at or below the stock 133 MHz
       ceiling must NOT be over-volted just because this board usually runs
       at 200 MHz -- a bootloader fallback to 125 MHz should draw 1.10 V. */
    ASSERT_EQ(fwog_vreg_mv_for_khz(125000u), 1100u);
    ASSERT_EQ(fwog_vreg_mv_for_khz(133000u), 1100u);

    /* Above stock and up to this board's 200 MHz: 1.15 V. */
    ASSERT_EQ(fwog_vreg_mv_for_khz(133001u), 1150u);
    ASSERT_EQ(fwog_vreg_mv_for_khz(200000u), 1150u);
    ASSERT_EQ(fwog_vreg_mv_for_khz(FWOG_SYS_CLK_KHZ), 1150u);

    /* Beyond 200 MHz the table CLAMPS rather than climbing. Handing out
       1.20 V+ on a battery-powered board would be this function silently
       authorizing an overclock nobody reviewed. */
    ASSERT_EQ(fwog_vreg_mv_for_khz(250000u), 1150u);

    /* Monotonic and bounded across the whole plausible range. */
    uint16_t prev = 0;
    for (uint32_t khz = 12000u; khz <= 260000u; khz += 1000u) {
        uint16_t mv = fwog_vreg_mv_for_khz(khz);
        ASSERT_TRUE(mv >= prev);
        ASSERT_TRUE(mv >= 1100u && mv <= 1150u);
        prev = mv;
    }

    TEST_RETURN();
}
