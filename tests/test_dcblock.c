/* Host tests for the one-pole DC-blocking high-pass. */
#include "common/dsp/dcblock.h"
#include "test_util.h"
#include <math.h>
#include <stdlib.h>

#define N 256u

static int16_t s_buf[N];

static void test_constant_input_decays_to_zero(void) {
    /* The whole point: a DC-biased stream must not survive. */
    for (unsigned i = 0u; i < N; i++) s_buf[i] = 12000;
    fwog_dcblock_inplace(s_buf, N);
    ASSERT_EQ(s_buf[0], 0);
    /* R=0.90 means the step response decays by 10% per sample; by sample 128
     * it is 0.9^127 of the original, i.e. nothing. */
    for (unsigned i = 128u; i < N; i++) ASSERT_TRUE(abs(s_buf[i]) < 2);
}

static void test_a_1khz_tone_survives(void) {
    /* At 8 kHz with R=0.90 the corner is about 127 Hz, so 1 kHz passes with
     * most of its amplitude intact. If someone retunes R downward for a more
     * aggressive corner, this bound is what tells them they went too far. */
    for (unsigned i = 0u; i < N; i++) {
        s_buf[i] = (int16_t)(8000.0f *
            sinf(2.0f * 3.14159265358979f * 1000.0f * (float)i / 8000.0f));
    }
    fwog_dcblock_inplace(s_buf, N);
    int16_t peak = 0;
    for (unsigned i = 32u; i < N; i++) {
        const int16_t a = (int16_t)abs(s_buf[i]);
        if (a > peak) peak = a;
    }
    ASSERT_TRUE(peak > 6000);   /* > 75% of the 8000 input peak */
}

static void test_bias_plus_tone_keeps_the_tone(void) {
    /* The real case: the idle PDM stream is heavily DC-biased and the tone
     * rides on top of it. After blocking, the tone must dominate. */
    for (unsigned i = 0u; i < N; i++) {
        s_buf[i] = (int16_t)(12000.0f + 2000.0f *
            sinf(2.0f * 3.14159265358979f * 1000.0f * (float)i / 8000.0f));
    }
    fwog_dcblock_inplace(s_buf, N);
    long sum = 0;
    int16_t peak = 0;
    for (unsigned i = 64u; i < N; i++) {
        sum += s_buf[i];
        const int16_t a = (int16_t)abs(s_buf[i]);
        if (a > peak) peak = a;
    }
    const long mean = sum / (long)(N - 64u);
    ASSERT_TRUE(labs(mean) < 100);   /* the 12000 bias is gone */
    ASSERT_TRUE(peak > 1400);        /* the 2000 tone is not */
}

static void test_degenerate_inputs(void) {
    fwog_dcblock_inplace(NULL, N);   /* must not crash */
    fwog_dcblock_inplace(s_buf, 0u);
    s_buf[0] = 1234;
    fwog_dcblock_inplace(s_buf, 1u);
    ASSERT_EQ(s_buf[0], 0);          /* first output is defined as 0 */
}

int main(void) {
    test_constant_input_decays_to_zero();
    test_a_1khz_tone_survives();
    test_bias_plus_tone_keeps_the_tone();
    test_degenerate_inputs();
    TEST_RETURN();
}
