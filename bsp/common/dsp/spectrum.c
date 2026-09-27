/* Spectral helpers -- see spectrum.h. */
#include "common/dsp/spectrum.h"
#include <math.h>

#ifndef FWOG_PI_F
#define FWOG_PI_F 3.14159265358979323846f
#endif

static int is_pow2_at_least(unsigned n, unsigned min) {
    return (n >= min) && ((n & (n - 1u)) == 0u);
}

void fwog_fft_r2(float *re, float *im, unsigned n) {
    if (re == 0 || im == 0 || !is_pow2_at_least(n, 2u)) return;

    /* Bit-reversal permutation. */
    for (unsigned i = 1u, j = 0u; i < n; i++) {
        unsigned bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }

    /* Cooley-Tukey butterflies. The twiddle is recomputed with sinf/cosf per
     * stage rather than carried by recurrence: a recurrence accumulates phase
     * error across 8 stages, and this runs once per bench command, not per
     * sample. Correctness is worth more than the microseconds here. */
    for (unsigned len = 2u; len <= n; len <<= 1) {
        const float ang = -2.0f * FWOG_PI_F / (float)len;
        for (unsigned i = 0u; i < n; i += len) {
            for (unsigned k = 0u; k < len / 2u; k++) {
                const float w_re = cosf(ang * (float)k);
                const float w_im = sinf(ang * (float)k);
                const unsigned a = i + k;
                const unsigned b = a + len / 2u;
                const float u_re = re[a], u_im = im[a];
                const float v_re = re[b] * w_re - im[b] * w_im;
                const float v_im = re[b] * w_im + im[b] * w_re;
                re[a] = u_re + v_re;  im[a] = u_im + v_im;
                re[b] = u_re - v_re;  im[b] = u_im - v_im;
            }
        }
    }
}

unsigned fwog_spectrum_dominant_bin(const int16_t *x, unsigned n,
                                    float *re, float *im) {
    if (x == 0 || re == 0 || im == 0 || !is_pow2_at_least(n, 4u)) return 0u;

    /* Hann window: 0.5 * (1 - cos(2*pi*i/(n-1))). */
    for (unsigned i = 0u; i < n; i++) {
        const float w = 0.5f * (1.0f - cosf(2.0f * FWOG_PI_F * (float)i /
                                            (float)(n - 1u)));
        re[i] = (float)x[i] * w;
        im[i] = 0.0f;
    }

    fwog_fft_r2(re, im, n);

    /* Bins 0..2 are excluded on purpose -- see the header. */
    if (n / 2u <= FWOG_SPECTRUM_MIN_BIN) return 0u;
    unsigned best = FWOG_SPECTRUM_MIN_BIN;
    float best_mag = -1.0f;
    for (unsigned i = FWOG_SPECTRUM_MIN_BIN; i < n / 2u; i++) {
        const float mag = re[i] * re[i] + im[i] * im[i];   /* compare squared */
        if (mag > best_mag) { best_mag = mag; best = i; }
    }
    return best;
}

uint32_t fwog_rms_i16(const int16_t *x, unsigned n) {
    if (x == 0 || n == 0u) return 0u;
    uint64_t acc = 0u;
    for (unsigned i = 0u; i < n; i++) {
        const int32_t v = (int32_t)x[i];
        acc += (uint64_t)((int64_t)v * (int64_t)v);
    }
    return (uint32_t)sqrt((double)acc / (double)n);
}

uint16_t fwog_peak_i16(const int16_t *x, unsigned n) {
    if (x == 0 || n == 0u) return 0u;
    uint16_t peak = 0u;
    for (unsigned i = 0u; i < n; i++) {
        /* Negate in int32_t: -(-32768) is 32768, which overflows int16_t. */
        const int32_t v = (int32_t)x[i];
        const uint16_t a = (uint16_t)((v < 0) ? -v : v);
        if (a > peak) peak = a;
    }
    return peak;
}
