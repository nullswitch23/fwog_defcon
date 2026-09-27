/* Host tests for the FFT / dominant-bin / rms / peak helpers.
 *
 * These matter more than their size suggests: the "dominant frequency" a bench
 * session reads off the console is only as trustworthy as this code, and a
 * bench is a bad place to discover an FFT bug. */
#include "common/dsp/spectrum.h"
#include "test_util.h"
#include <math.h>

#define N FWOG_FFT_MAX_N

static float s_re[N];
static float s_im[N];
static int16_t s_x[N];

/* Fill s_x with a sine at exactly `bin` cycles per window -- coherent, so
 * there is no leakage even before the Hann window. */
static void fill_bin(unsigned bin, float amplitude) {
    for (unsigned i = 0u; i < N; i++) {
        s_x[i] = (int16_t)(amplitude *
            sinf(2.0f * 3.14159265358979f * (float)bin * (float)i / (float)N));
    }
}

static void test_dominant_bin_finds_the_tone(void) {
    fill_bin(32u, 8000.0f);
    ASSERT_EQ(fwog_spectrum_dominant_bin(s_x, N, s_re, s_im), 32u);

    fill_bin(8u, 8000.0f);
    ASSERT_EQ(fwog_spectrum_dominant_bin(s_x, N, s_re, s_im), 8u);

    fill_bin(100u, 4000.0f);
    ASSERT_EQ(fwog_spectrum_dominant_bin(s_x, N, s_re, s_im), 100u);
}

static void test_dc_is_never_reported(void) {
    /* A large constant offset plus a small tone. Without the bin-0 exclusion
     * DC wins outright, and the function reports "0 Hz" for a room with a tone
     * playing in it -- a number, and useless. */
    for (unsigned i = 0u; i < N; i++) {
        s_x[i] = (int16_t)(12000.0f + 300.0f *
            sinf(2.0f * 3.14159265358979f * 20.0f * (float)i / (float)N));
    }
    const unsigned bin = fwog_spectrum_dominant_bin(s_x, N, s_re, s_im);
    ASSERT_TRUE(bin != 0u);
    ASSERT_TRUE(bin >= 19u && bin <= 21u);
}

static void test_rejects_bad_inputs(void) {
    fill_bin(32u, 8000.0f);
    ASSERT_EQ(fwog_spectrum_dominant_bin(NULL, N, s_re, s_im), 0u);
    ASSERT_EQ(fwog_spectrum_dominant_bin(s_x, N, NULL, s_im), 0u);
    ASSERT_EQ(fwog_spectrum_dominant_bin(s_x, 100u, s_re, s_im), 0u); /* not 2^k */
    ASSERT_EQ(fwog_spectrum_dominant_bin(s_x, 2u, s_re, s_im), 0u);   /* too small */
}

static void test_fft_of_a_constant_is_all_dc(void) {
    for (unsigned i = 0u; i < N; i++) { s_re[i] = 1.0f; s_im[i] = 0.0f; }
    fwog_fft_r2(s_re, s_im, N);
    ASSERT_TRUE(fabsf(s_re[0] - (float)N) < 0.01f);
    float worst = 0.0f;
    for (unsigned i = 1u; i < N; i++) {
        const float m = sqrtf(s_re[i] * s_re[i] + s_im[i] * s_im[i]);
        if (m > worst) worst = m;
    }
    ASSERT_TRUE(worst < 0.01f);
}

static void test_rms_and_peak(void) {
    /* A square wave of +-1000 has rms exactly 1000 and peak exactly 1000. */
    for (unsigned i = 0u; i < N; i++) s_x[i] = (i & 1u) ? 1000 : -1000;
    ASSERT_EQ(fwog_rms_i16(s_x, N), 1000u);
    ASSERT_EQ(fwog_peak_i16(s_x, N), 1000u);

    /* INT16_MIN: the case where an abs()-based peak silently returns -32768.
     * 32768 does not fit in an int16_t, which is why the return is uint16_t. */
    s_x[7] = -32768;
    ASSERT_EQ(fwog_peak_i16(s_x, N), 32768u);

    /* rms must not wrap: 256 samples of 32768^2 is 2.7e11, three orders of
     * magnitude past uint32_t. */
    for (unsigned i = 0u; i < N; i++) s_x[i] = -32768;
    ASSERT_EQ(fwog_rms_i16(s_x, N), 32768u);

    ASSERT_EQ(fwog_rms_i16(s_x, 0u), 0u);
    ASSERT_EQ(fwog_peak_i16(NULL, N), 0u);
}

int main(void) {
    test_dominant_bin_finds_the_tone();
    test_dc_is_never_reported();
    test_rejects_bad_inputs();
    test_fft_of_a_constant_is_all_dc();
    test_rms_and_peak();
    TEST_RETURN();
}
