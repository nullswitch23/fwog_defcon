/* Host tests for the ship-mode hold detector. No I2C, no SDK -- everything
 * behind #ifndef HOST_TEST in ship_mode.h is invisible here, so
 * fwog_ship_enter() is not linked and cannot be called by accident. That is
 * deliberate: this test binary must have no way to reach the one write that
 * powers a board off. */
#include "power/ship_mode.h"
#include "test_util.h"

/* Drive the machine from `from_ms` through `to_ms` inclusive, in `step_ms`
 * increments at a fixed pressed level, counting how many times FIRED came
 * back. The step size is a parameter because the detector must not care about
 * poll rate: elapsed time is measured, never counted. Written with unsigned
 * offsets rather than `t <= to_ms` so the wrap test can run through
 * 0xFFFFFFFF without the loop condition itself going wrong. */
static unsigned run(fwog_ship_state_t *s, bool pressed,
                    uint32_t from_ms, uint32_t to_ms, uint32_t step_ms) {
    const uint32_t span = (uint32_t)(to_ms - from_ms);
    unsigned fires = 0u;
    for (uint32_t off = 0u; off <= span; off += step_ms) {
        if (fwog_ship_step(s, pressed, (uint32_t)(from_ms + off)) == FWOG_SHIP_FIRED)
            fires++;
    }
    return fires;
}

static void test_continuous_hold_fires_once(void) {
    fwog_ship_state_t s = {0};
    ASSERT_EQ(run(&s, true, 1000u, 1000u + FWOG_SHIP_HOLD_MS, 5u), 1u);
    ASSERT_EQ(s.phase, FWOG_SHIP_FIRED);
    /* Still held, 3 s later: no second fire, and the latched phase stays. */
    ASSERT_EQ(run(&s, true, 1000u + FWOG_SHIP_HOLD_MS, 10000u, 5u), 0u);
    ASSERT_EQ(s.phase, FWOG_SHIP_FIRED);
    /* The return AFTER firing is ARMING, not FIRED -- see the header. */
    ASSERT_EQ(fwog_ship_step(&s, true, 11000u), FWOG_SHIP_ARMING);
    ASSERT_EQ(s.phase, FWOG_SHIP_FIRED);
}

static void test_release_one_ms_early_does_not_fire(void) {
    fwog_ship_state_t s = {0};
    ASSERT_EQ(run(&s, true, 0u, FWOG_SHIP_HOLD_MS - 1u, 1u), 0u);
    ASSERT_EQ(s.phase, FWOG_SHIP_ARMING);
    ASSERT_EQ(fwog_ship_step(&s, false, FWOG_SHIP_HOLD_MS - 1u), FWOG_SHIP_IDLE);
    ASSERT_EQ(s.phase, FWOG_SHIP_IDLE);
    /* And a press that arrives at exactly the old threshold starts over. */
    ASSERT_EQ(fwog_ship_step(&s, true, FWOG_SHIP_HOLD_MS), FWOG_SHIP_ARMING);
    ASSERT_EQ(fwog_ship_progress(&s, FWOG_SHIP_HOLD_MS), 0u);
}

static void test_six_one_second_presses_never_fire(void) {
    /* The accumulation case. Six seconds of total contact, no fire, because
     * every release discards the countdown. This is the guard that stands
     * between a pocket and a dark board. */
    fwog_ship_state_t s = {0};
    uint32_t t = 0u;
    unsigned fires = 0u;
    for (unsigned i = 0u; i < 6u; i++) {
        fires += run(&s, true, t, t + 1000u, 10u);
        t += 1000u;
        if (fwog_ship_step(&s, false, t) == FWOG_SHIP_FIRED) fires++;
        t += 50u;   /* finger off the button */
    }
    ASSERT_EQ(fires, 0u);
    ASSERT_EQ(s.phase, FWOG_SHIP_IDLE);
}

static void test_repress_restarts_from_zero(void) {
    fwog_ship_state_t s = {0};
    (void)run(&s, true, 0u, 5000u, 10u);
    ASSERT_TRUE(fwog_ship_progress(&s, 5000u) > 80u);
    (void)fwog_ship_step(&s, false, 5000u);
    ASSERT_EQ(fwog_ship_progress(&s, 5000u), 0u);
    ASSERT_EQ(fwog_ship_step(&s, true, 5001u), FWOG_SHIP_ARMING);
    ASSERT_EQ(fwog_ship_progress(&s, 5001u), 0u);
    /* 5001 + 5999 is 1 ms short of the NEW threshold, not the old one. */
    ASSERT_EQ(fwog_ship_step(&s, true, 5001u + FWOG_SHIP_HOLD_MS - 1u),
              FWOG_SHIP_ARMING);
    ASSERT_EQ(fwog_ship_step(&s, true, 5001u + FWOG_SHIP_HOLD_MS),
              FWOG_SHIP_FIRED);
}

static void test_progress_is_monotonic_and_bounded(void) {
    fwog_ship_state_t s = {0};
    ASSERT_EQ(fwog_ship_progress(&s, 12345u), 0u);   /* IDLE is always 0 */
    (void)fwog_ship_step(&s, true, 100u);
    ASSERT_EQ(fwog_ship_progress(&s, 100u), 0u);
    unsigned prev = 0u;
    for (uint32_t t = 100u; t <= 100u + FWOG_SHIP_HOLD_MS; t += 7u) {
        const unsigned p = fwog_ship_progress(&s, t);
        ASSERT_TRUE(p >= prev);
        ASSERT_TRUE(p <= 100u);
        prev = p;
    }
    ASSERT_EQ(fwog_ship_progress(&s, 100u + FWOG_SHIP_HOLD_MS), 100u);
    /* Clamped, not wrapped, well past the threshold -- the case that would
     * overflow if progress multiplied before clamping. */
    ASSERT_EQ(fwog_ship_progress(&s, 100u + 10u * FWOG_SHIP_HOLD_MS), 100u);
    ASSERT_EQ(fwog_ship_progress(&s, 100u + 4000000000u), 100u);
    (void)fwog_ship_step(&s, false, 99999u);
    ASSERT_EQ(fwog_ship_progress(&s, 99999u), 0u);
}

static void test_millisecond_wrap(void) {
    /* Press 3000 ms before the uint32_t wrap. Asserted rather than assumed:
     * the rest of this BSP uses uint32_t millisecond stamps and unsigned
     * subtraction handles wrap correctly, but "correctly" is a claim. */
    const uint32_t base = 0xFFFFFFFFu - 3000u;
    fwog_ship_state_t s = {0};
    ASSERT_EQ(fwog_ship_step(&s, true, base), FWOG_SHIP_ARMING);
    /* Crossing zero must not look like a huge elapsed time. */
    ASSERT_EQ(fwog_ship_step(&s, true, (uint32_t)(base + 3000u)), FWOG_SHIP_ARMING);
    ASSERT_EQ(fwog_ship_step(&s, true, (uint32_t)(base + 3001u)), FWOG_SHIP_ARMING);
    ASSERT_TRUE(fwog_ship_progress(&s, (uint32_t)(base + 3001u)) < 100u);
    ASSERT_EQ(fwog_ship_step(&s, true, (uint32_t)(base + FWOG_SHIP_HOLD_MS - 1u)),
              FWOG_SHIP_ARMING);
    ASSERT_EQ(fwog_ship_step(&s, true, (uint32_t)(base + FWOG_SHIP_HOLD_MS)),
              FWOG_SHIP_FIRED);
}

int main(void) {
    test_continuous_hold_fires_once();
    test_release_one_ms_early_does_not_fire();
    test_six_one_second_presses_never_fire();
    test_repress_restarts_from_zero();
    test_progress_is_monotonic_and_bounded();
    test_millisecond_wrap();
    TEST_RETURN();
}
