#include "test_util.h"
#include "fpga/fpga_clk.h"

int main(void) {
    /* This board: clk_sys 200 MHz (the hardware record) -> exactly 25 MHz. */
    ASSERT_EQ(fwog_fpga_clk_divider(200000000u), 8u);

    /* The Pico SDK's 125 MHz default, which is what the REFERENCE runs.
       125/5 is exactly 25 MHz -- the correct divider for a 25 MHz target
       from a 125 MHz source, and NOT the reference's own divider of 4
       (125/4 = 31.25 MHz, an overclock of the gateware). Copying that
       constant is the bug this function exists to prevent. */
    ASSERT_EQ(fwog_fpga_clk_divider(125000000u), 5u);

    /* Rounds UP so the result is never FASTER than the target -- an
       overclock of the gateware is the failure mode that matters. */
    ASSERT_EQ(fwog_fpga_clk_divider(210000000u), 9u);   /* 8.4 -> 9 */
    ASSERT_EQ(fwog_fpga_clk_divider(100000000u), 4u);   /* exactly 4 */

    /* Degenerate inputs never return 0 -- a zero divider would be a
       divide-by-zero in clock_gpio_init_int_frac(). */
    ASSERT_EQ(fwog_fpga_clk_divider(0u), 1u);
    ASSERT_EQ(fwog_fpga_clk_divider(1000u), 1u);

    ASSERT_EQ(FWOG_FPGA_CLK_HZ, 25000000u);
    TEST_RETURN();
}
