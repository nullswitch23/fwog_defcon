#include "test_util.h"
#include "ms_db.h"
#include "ms_proto.h"

int main(void) {
    ASSERT_EQ((int)sizeof(ms_cal_t), 140);
    ASSERT_EQ((int)sizeof(ms_cmd_t), 2);
    ASSERT_EQ(ms_spl_approx(100, 100), MS_QUIET_SPL);
    ASSERT_EQ(ms_spl_approx(1000, 100), MS_QUIET_SPL + 20);
    ASSERT_EQ(ms_spl_approx(100, 1000), MS_QUIET_SPL - 20);
    ASSERT_EQ((int)ms_bar_px(100, 100, 96), 0);
    /* +60 dB power = x1e6, full height. */
    ASSERT_EQ((int)ms_bar_px(100u * 1000000u, 100, 96), 96);
    /* +30 dB power = x1000, half height. */
    ASSERT_EQ((int)ms_bar_px(100u * 1000u, 100, 96), 48);
    TEST_RETURN();
}
