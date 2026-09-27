#include "test_util.h"
#include "watchdog/watchdog.h"

int main(void) {
    uint32_t d = 0;

    /* The outermost pause is the only one that touches the hardware. */
    ASSERT_TRUE(fwog_wdt_pause_step(&d));
    ASSERT_EQ(d, 1u);

    /* Nested pauses must NOT re-arm the long window: doing so would restart
       the 8300 ms budget on every nesting level, so a caller that pauses in
       a loop could hold the watchdog off indefinitely. */
    ASSERT_TRUE(!fwog_wdt_pause_step(&d));
    ASSERT_EQ(d, 2u);
    ASSERT_TRUE(!fwog_wdt_pause_step(&d));
    ASSERT_EQ(d, 3u);

    /* Only the outermost resume restores the short window. */
    ASSERT_TRUE(!fwog_wdt_resume_step(&d));
    ASSERT_EQ(d, 2u);
    ASSERT_TRUE(!fwog_wdt_resume_step(&d));
    ASSERT_EQ(d, 1u);
    ASSERT_TRUE(fwog_wdt_resume_step(&d));
    ASSERT_EQ(d, 0u);

    /* An unbalanced resume must not underflow. A wrapped depth would make
       the NEXT pause a no-op, leaving the short window armed across a
       multi-second flash write -- a reset in the middle of an update. */
    ASSERT_TRUE(!fwog_wdt_resume_step(&d));
    ASSERT_EQ(d, 0u);
    ASSERT_TRUE(!fwog_wdt_resume_step(&d));
    ASSERT_EQ(d, 0u);

    /* ...and the next pause still works. */
    ASSERT_TRUE(fwog_wdt_pause_step(&d));
    ASSERT_EQ(d, 1u);

    TEST_RETURN();
}
