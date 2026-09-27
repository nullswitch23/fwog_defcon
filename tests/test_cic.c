/* Host tests for the 3rd-order CIC decimator and the raw-PDM byte binding.
 * Adapted from wilibsp's tests/test_cic.c (MIT, same copyright holder), plus
 * everything the byte-oriented binding and this repo's own bit order need.
 *
 * No SDK, no hardware. The tone round-trip below is the test that proves the
 * filter FILTERS, as opposed to merely producing numbers. */
#include "common/dsp/cic.h"
#include "common/dsp/spectrum.h"
#include "pdm/pdm_mic.h"
#include "test_util.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- 1. The core, adapted from wilibsp ---- */

static void test_one_output_per_decimate_bits(void) {
    fwog_cic_t c;
    fwog_cic_init(&c);
    int16_t pcm = 0;
    int outputs = 0;
    for (unsigned i = 0u; i < FWOG_CIC_DECIMATE * 10u; i++)
        if (fwog_cic_push_bit(&c, 1, &pcm)) outputs++;
    ASSERT_EQ(outputs, 10);
    /* Constant-1 plateaus at +16384 (2^18 >> 4, the documented headroom
     * scaling) once the comb pipeline fills. */
    ASSERT_EQ(pcm, 16384);
}

static void test_constant_zero_mirrors(void) {
    fwog_cic_t c;
    fwog_cic_init(&c);
    int16_t pcm = 0;
    for (unsigned i = 0u; i < FWOG_CIC_DECIMATE * 10u; i++)
        (void)fwog_cic_push_bit(&c, 0, &pcm);
    /* Symmetry catches a sign error in the {0,1} -> {-1,+1} mapping. */
    ASSERT_EQ(pcm, -16384);
}

static void test_nyquist_alternation_is_stopband(void) {
    fwog_cic_t c;
    fwog_cic_init(&c);
    int16_t pcm = 0;
    for (unsigned i = 0u; i < FWOG_CIC_DECIMATE * 10u; i++)
        (void)fwog_cic_push_bit(&c, (int)(i & 1u), &pcm);
    ASSERT_TRUE(abs(pcm) < 100);
}

/* ---- 2. Bit order within a byte: MSB first ----
 *
 * This is the test that would catch an LSB-first loop. pdm_mic.c configures
 * sm_config_set_in_shift(&c, false, false, 8u) -- shift LEFT -- so the first
 * bit sampled ends up in bit 7 of the pushed byte. An LSB-first decode still
 * produces plausible audio at the right amplitude and rate, with the wrong
 * content, which is why this is derived from the shift config and pinned here
 * rather than judged by listening. */

static void test_msb_first_byte_order(void) {
    /* 0xF0 is "four 1s then four 0s" read MSB-first; 0x0F is its reverse.
     * Under an LSB-first decode the two would swap, so any asymmetry in the
     * decoded result distinguishes the two orders. */
    uint8_t buf_f0[PDM_RAW_BUFFER_BYTES];
    uint8_t buf_0f[PDM_RAW_BUFFER_BYTES];
    memset(buf_f0, 0xF0, sizeof(buf_f0));
    memset(buf_0f, 0x0F, sizeof(buf_0f));

    static int16_t out_f0[PDM_SAMPLE_BUFFER_SIZE];
    static int16_t out_0f[PDM_SAMPLE_BUFFER_SIZE];
    fwog_cic_t a, b;
    fwog_cic_init(&a);
    fwog_cic_init(&b);
    const size_t n_f0 = pdm_mic_decode(&a, buf_f0, sizeof(buf_f0), out_f0);
    const size_t n_0f = pdm_mic_decode(&b, buf_0f, sizeof(buf_0f), out_0f);

    /* Geometry: 2048 bytes = 16384 bits, at 64x = exactly 256 samples. */
    ASSERT_EQ(n_f0, PDM_SAMPLE_BUFFER_SIZE);
    ASSERT_EQ(n_0f, PDM_SAMPLE_BUFFER_SIZE);

    /* Both patterns are 50% density, so both settle to 0 in steady state --
     * that is NOT what distinguishes them. The first decimation window is:
     * for 0xF0, bits 1111 0000 1111 0000 ... starting with a 1;
     * for 0x0F, bits 0000 1111 0000 1111 ... starting with a 0.
     * The CIC's transient through the first few outputs therefore differs in
     * sign, and pinning the first sample of each is what fails loudly if the
     * loop is reversed. */
    ASSERT_TRUE(out_f0[0] != out_0f[0]);
    ASSERT_TRUE(out_f0[0] > 0);   /* leading 1s push the integrator positive */
    ASSERT_TRUE(out_0f[0] < 0);   /* leading 0s push it negative */

    /* And swapping the decode order must not change either answer -- i.e. the
     * result is a property of the data, not of call sequence. */
    fwog_cic_t a2;
    fwog_cic_init(&a2);
    static int16_t out_again[PDM_SAMPLE_BUFFER_SIZE];
    (void)pdm_mic_decode(&a2, buf_f0, sizeof(buf_f0), out_again);
    ASSERT_EQ(out_again[0], out_f0[0]);
}

/* ---- 3. Buffer-boundary continuity ----
 * One stream decoded in a single call must equal the same stream decoded as
 * two calls sharing state. Restarting per buffer puts a click at every seam. */

static void test_buffer_boundary_continuity(void) {
    static uint8_t stream[2u * PDM_RAW_BUFFER_BYTES];
    /* A varying pattern, not a constant -- a constant would pass even if the
     * state were reset between calls. */
    for (size_t i = 0u; i < sizeof(stream); i++)
        stream[i] = (uint8_t)((i * 37u + (i >> 3)) & 0xFFu);

    static int16_t one_shot[2u * PDM_SAMPLE_BUFFER_SIZE];
    static int16_t split[2u * PDM_SAMPLE_BUFFER_SIZE];

    fwog_cic_t c1;
    fwog_cic_init(&c1);
    const size_t n1 = pdm_mic_decode(&c1, stream, sizeof(stream), one_shot);

    fwog_cic_t c2;
    fwog_cic_init(&c2);
    const size_t na = pdm_mic_decode(&c2, stream, PDM_RAW_BUFFER_BYTES, split);
    const size_t nb = pdm_mic_decode(&c2, stream + PDM_RAW_BUFFER_BYTES,
                                     PDM_RAW_BUFFER_BYTES, split + na);

    ASSERT_EQ(n1, 2u * PDM_SAMPLE_BUFFER_SIZE);
    ASSERT_EQ(na + nb, n1);
    int mismatches = 0;
    for (size_t i = 0u; i < n1; i++)
        if (one_shot[i] != split[i]) mismatches++;
    ASSERT_EQ(mismatches, 0);
}

/* ---- 4. A synthetic tone round-trips ----
 *
 * Generate a first-order sigma-delta PDM stream for a sine at the real
 * capture rate (PDM_SAMPLE_RATE_HZ * PDM_DECIMATION = 512 kHz), decode it, and
 * check the dominant frequency comes back. Needs no hardware, and is the only
 * check in this file that would notice a filter that decimates correctly but
 * destroys the signal. */

#define TONE_PDM_BYTES  (8u * PDM_RAW_BUFFER_BYTES)   /* 16384 B -> 2048 samples */
#define TONE_PCM_LEN    (TONE_PDM_BYTES * 8u / FWOG_CIC_DECIMATE)
#define TONE_FS_IN_HZ   ((float)(PDM_SAMPLE_RATE_HZ * PDM_DECIMATION))

static uint8_t  s_tone_pdm[TONE_PDM_BYTES];
static int16_t  s_tone_pcm[TONE_PCM_LEN];
static float    s_re[FWOG_FFT_MAX_N];
static float    s_im[FWOG_FFT_MAX_N];

/* First-order sigma-delta modulator: the standard error-feedback loop. */
static void make_pdm_tone(float freq_hz, float amplitude) {
    float integ = 0.0f;
    float y = 1.0f;
    for (size_t byte = 0u; byte < TONE_PDM_BYTES; byte++) {
        uint8_t v = 0u;
        for (int b = 7; b >= 0; b--) {          /* MSB first, as captured */
            const size_t n = byte * 8u + (size_t)(7 - b);
            const float x = amplitude *
                sinf(2.0f * 3.14159265358979f * freq_hz * (float)n / TONE_FS_IN_HZ);
            integ += x - y;
            y = (integ >= 0.0f) ? 1.0f : -1.0f;
            if (y > 0.0f) v |= (uint8_t)(1u << b);
        }
        s_tone_pdm[byte] = v;
    }
}

/* Decode the synthetic stream and return the FFT magnitude at the tone's own
 * bin, from the settled tail.
 *
 * NOT the peak sample, which was the first thing tried and is wrong here: a
 * first-order sigma-delta source has strongly high-pass-shaped quantisation
 * noise, and a peak measurement tracks that noise as much as the tone. Peak
 * gave 0.541 at 2 kHz where the analytic droop says 0.744 -- a 3 dB error,
 * enough to make the whole droop table look wrong. The bin magnitude isolates
 * the tone from broadband noise and reproduces the analytic table to three
 * significant figures. */
static float decode_tone_bin_mag(float freq_hz, float amplitude) {
    make_pdm_tone(freq_hz, amplitude);
    fwog_cic_t c;
    fwog_cic_init(&c);
    const size_t n = pdm_mic_decode(&c, s_tone_pdm, TONE_PDM_BYTES, s_tone_pcm);
    ASSERT_EQ(n, TONE_PCM_LEN);
    const int16_t *tail = s_tone_pcm + TONE_PCM_LEN - FWOG_FFT_MAX_N;
    const unsigned bin = fwog_spectrum_dominant_bin(tail, FWOG_FFT_MAX_N,
                                                    s_re, s_im);
    return sqrtf(s_re[bin] * s_re[bin] + s_im[bin] * s_im[bin]);
}

static void test_tone_round_trip(void) {
    /* Bin width at 8 kHz over 256 points is 31.25 Hz, so 1000 Hz is exactly
     * bin 32 -- coherent, no leakage even before the Hann window. */
    make_pdm_tone(1000.0f, 0.5f);
    fwog_cic_t c;
    fwog_cic_init(&c);
    const size_t n = pdm_mic_decode(&c, s_tone_pdm, TONE_PDM_BYTES, s_tone_pcm);
    ASSERT_EQ(n, TONE_PCM_LEN);

    const int16_t *tail = s_tone_pcm + TONE_PCM_LEN - FWOG_FFT_MAX_N;
    const unsigned bin = fwog_spectrum_dominant_bin(tail, FWOG_FFT_MAX_N,
                                                    s_re, s_im);
    /* 1000 Hz / (8000/256) = bin 32, +/- one bin for windowing. */
    ASSERT_TRUE(bin >= 31u && bin <= 33u);

    /* And it must be a real peak, not just the largest of a flat spectrum:
     * at least 10x the median magnitude across the half-spectrum. */
    float mags[FWOG_FFT_MAX_N / 2u];
    for (unsigned i = 0u; i < FWOG_FFT_MAX_N / 2u; i++)
        mags[i] = sqrtf(s_re[i] * s_re[i] + s_im[i] * s_im[i]);
    float sorted[FWOG_FFT_MAX_N / 2u];
    memcpy(sorted, mags, sizeof(sorted));
    for (unsigned i = 1u; i < FWOG_FFT_MAX_N / 2u; i++) {
        float k = sorted[i];
        unsigned j = i;
        while (j > 0u && sorted[j - 1u] > k) { sorted[j] = sorted[j - 1u]; j--; }
        sorted[j] = k;
    }
    const float median = sorted[FWOG_FFT_MAX_N / 4u];
    ASSERT_TRUE(mags[bin] > 10.0f * median);
}

/* ---- 5. The droop table, measured from the filter itself ----
 *
 * The spec's "roughly 3.9 dB at Nyquist" is the PER-STAGE figure; a 3rd-order
 * CIC at 64x is about 11.8 dB down at 4 kHz. These bounds are computed from
 * the analytic response in cic.h and checked against what the filter actually
 * does, so the number that decides the compensating-FIR question comes from
 * code rather than from prose. Tolerances are wide because a first-order
 * sigma-delta source is noisy; the point is to separate 11.8 dB from 3.9 dB,
 * not to measure to a tenth. */
static void test_droop_matches_the_analytic_table(void) {
    const float m500  = decode_tone_bin_mag(500.0f,  0.5f);
    const float m1000 = decode_tone_bin_mag(1000.0f, 0.5f);
    const float m2000 = decode_tone_bin_mag(2000.0f, 0.5f);
    const float m3500 = decode_tone_bin_mag(3500.0f, 0.5f);
    ASSERT_TRUE(m500 > 0.0f);

    /* Analytic ratios relative to 500 Hz, from cic.h's table:
     *   1000 Hz: 10^((-0.67 + 0.17)/20) = 0.944
     *   2000 Hz: 10^((-2.74 + 0.17)/20) = 0.744
     *   3500 Hz: 10^((-8.79 + 0.17)/20) = 0.371
     * Measured: 0.944, 0.742, 0.371. The +/-0.03 band is float slack across
     * compilers, not measurement uncertainty -- this agreement is tight. */
    const float r1000 = m1000 / m500;
    const float r2000 = m2000 / m500;
    const float r3500 = m3500 / m500;
    ASSERT_TRUE(r1000 > 0.914f && r1000 < 0.974f);
    ASSERT_TRUE(r2000 > 0.714f && r2000 < 0.774f);
    ASSERT_TRUE(r3500 > 0.341f && r3500 < 0.401f);

    /* THE POINT OF THIS TEST. A SINGLE-stage CIC at 3500 Hz would sit at
     * 10^((-2.93 + 0.06)/20) = 0.719 -- which the bound above already
     * excludes, but state it separately so the intent survives a future edit
     * to the tolerances. The spec's "roughly 3.9 dB at Nyquist" is the
     * per-stage figure; the real 3rd-order total is 11.8 dB, and this is the
     * assertion that tells the two apart. */
    ASSERT_TRUE(r3500 < 0.60f);
}

int main(void) {
    test_one_output_per_decimate_bits();
    test_constant_zero_mirrors();
    test_nyquist_alternation_is_stopband();
    test_msb_first_byte_order();
    test_buffer_boundary_continuity();
    test_tone_round_trip();
    test_droop_matches_the_analytic_table();
    TEST_RETURN();
}
