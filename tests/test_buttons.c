#include "input/buttons.h"
#include "test_util.h"
#include <string.h>

/* Drive one button's state machine and return the last event. */
static fwog_btn_event_t step(fwog_btn_state_t *s, bool raw, uint32_t t) {
    return fwog_btn_step(s, raw, t);
}

static void test_glitch_is_rejected(void) {
    /* A contact bounce shorter than the debounce window must produce
       nothing at all -- no edge, no level. This is the entire reason the
       DEBOUNCE_DOWN state exists. */
    fwog_btn_state_t s; memset(&s, 0, sizeof s);
    fwog_btn_event_t e;

    e = step(&s, false, 0u);
    ASSERT_TRUE(!e.down && !e.pressed && !e.released);

    e = step(&s, true, 10u);              /* contact seen */
    ASSERT_TRUE(!e.down && !e.pressed);   /* but not yet settled */

    e = step(&s, false, 12u);             /* gone again after 2 ms */
    ASSERT_TRUE(!e.down && !e.pressed && !e.released);
    ASSERT_EQ(s.phase, FWOG_BTN_PHASE_IDLE);
}

static void test_press_emits_one_edge(void) {
    fwog_btn_state_t s; memset(&s, 0, sizeof s);
    fwog_btn_event_t e;

    step(&s, true, 100u);                          /* enters DEBOUNCE_DOWN */
    e = step(&s, true, 104u);                      /* 4 ms: not yet */
    ASSERT_TRUE(!e.pressed && !e.down);

    e = step(&s, true, 105u);                      /* exactly 5 ms: settles */
    ASSERT_TRUE(e.pressed);
    ASSERT_TRUE(e.down);
    ASSERT_EQ(s.phase, FWOG_BTN_PHASE_DOWN);

    /* Edge exactly once: holding must not re-emit. */
    e = step(&s, true, 200u);
    ASSERT_TRUE(!e.pressed);
    ASSERT_TRUE(e.down);
    e = step(&s, true, 5000u);
    ASSERT_TRUE(!e.pressed);
    ASSERT_TRUE(e.down);
}

static void test_bouncy_release(void) {
    /* `down` must stay true while the RELEASE is debounced -- the legacy
       keeps m_bIsPressed set through debouncingUnpress -- and a bounce back
       to contact must not be reported as a new press. */
    fwog_btn_state_t s; memset(&s, 0, sizeof s);
    fwog_btn_event_t e;

    step(&s, true, 0u);
    e = step(&s, true, 5u);
    ASSERT_TRUE(e.pressed);

    e = step(&s, false, 100u);                     /* release seen */
    ASSERT_TRUE(e.down);                           /* still down */
    ASSERT_TRUE(!e.released);                      /* not yet */
    ASSERT_EQ(s.phase, FWOG_BTN_PHASE_DEBOUNCE_UP);

    e = step(&s, true, 102u);                      /* bounced back */
    ASSERT_TRUE(e.down);
    ASSERT_TRUE(!e.pressed);                       /* NOT a new press */
    ASSERT_EQ(s.phase, FWOG_BTN_PHASE_DOWN);

    e = step(&s, false, 200u);                     /* released again */
    ASSERT_TRUE(!e.released);
    e = step(&s, false, 205u);                     /* 5 ms later: settles */
    ASSERT_TRUE(e.released);
    ASSERT_TRUE(!e.down);
    ASSERT_EQ(s.phase, FWOG_BTN_PHASE_IDLE);

    /* Edge exactly once. */
    e = step(&s, false, 300u);
    ASSERT_TRUE(!e.released && !e.down);
}

static void test_held_survives_a_bounce(void) {
    /* A bouncy release must not restart the long-press clock, or a flaky
       contact would make a long press unreachable. */
    fwog_btn_state_t s; memset(&s, 0, sizeof s);

    step(&s, true, 1000u);
    step(&s, true, 1005u);                         /* settles down at 1005 */
    ASSERT_EQ(fwog_btn_held_ms(&s, 2005u), 1000u);

    step(&s, false, 3000u);                        /* into DEBOUNCE_UP */
    ASSERT_EQ(fwog_btn_held_ms(&s, 3000u), 1995u); /* still counting */
    step(&s, true, 3002u);                         /* bounced back to DOWN */
    ASSERT_EQ(fwog_btn_held_ms(&s, 4005u), 3000u); /* from 1005, not 3002 */
}

static void test_held_is_zero_when_up(void) {
    fwog_btn_state_t s; memset(&s, 0, sizeof s);
    ASSERT_EQ(fwog_btn_held_ms(&s, 12345u), 0u);
    step(&s, true, 10u);                           /* DEBOUNCE_DOWN, not down */
    ASSERT_EQ(fwog_btn_held_ms(&s, 12u), 0u);
}

static void test_millisecond_wraparound(void) {
    /* uint32_t ms wraps every ~49.7 days. Unsigned subtraction is correct
       across the wrap; a naive `now > since + N` is not. A board left on a
       bench for seven weeks must not stop debouncing. */
    fwog_btn_state_t s; memset(&s, 0, sizeof s);
    const uint32_t near_wrap = 0xFFFFFFFCu;        /* 4 ms before wrap */
    fwog_btn_event_t e;

    step(&s, true, near_wrap);
    e = step(&s, true, 1u);                        /* wrapped: 5 ms elapsed */
    ASSERT_TRUE(e.pressed);
    ASSERT_TRUE(e.down);

    /* And held time measures across the wrap too. */
    ASSERT_EQ(fwog_btn_held_ms(&s, 101u), 100u);
}

static void test_bit_order_matches_the_legacy(void) {
    /* rmetButtonManager::buttonStateBitfield() packs gray=1, yellow=2,
       green=4, blue=8, red=0x10. Anything already speaking that encoding
       keeps working only if this order is identical. */
    ASSERT_EQ(FWOG_BTN_BIT(FWOG_BTN_GRAY),   0x01u);
    ASSERT_EQ(FWOG_BTN_BIT(FWOG_BTN_YELLOW), 0x02u);
    ASSERT_EQ(FWOG_BTN_BIT(FWOG_BTN_GREEN),  0x04u);
    ASSERT_EQ(FWOG_BTN_BIT(FWOG_BTN_BLUE),   0x08u);
    ASSERT_EQ(FWOG_BTN_BIT(FWOG_BTN_RED),    0x10u);
    ASSERT_EQ(FWOG_BTN_COUNT, 5u);
}

int main(void) {
    test_glitch_is_rejected();
    test_press_emits_one_edge();
    test_bouncy_release();
    test_held_survives_a_bounce();
    test_held_is_zero_when_up();
    test_millisecond_wraparound();
    test_bit_order_matches_the_legacy();
    TEST_RETURN();
}
