/* Small spectral helpers for the PDM analysis path: a radix-2 FFT, a
 * Hann-windowed dominant-bin search, and rms/peak over int16 PCM.
 *
 * Written for this BSP rather than adapted -- wilibsp's findings pipeline does
 * its FFT on the host, off the device, so there was no sibling to carry over.
 *
 * These live under bsp/common/ with cic.c and dcblock.c because they are pure
 * arithmetic with no pin map, no SDK and no CPU affinity, the same reasoning
 * that puts crc.c there. That also makes them host-tested by default rather
 * than by exception, which matters here: the "dominant frequency" a bench
 * session reads off the console is only as trustworthy as this code, and a
 * bench is a bad place to discover an FFT bug.
 *
 * ---- Scale ----
 * FWOG_FFT_MAX_N is 256 because one raw PDM buffer decodes to exactly 256
 * samples (PDM_RAW_BUFFER_BYTES 2048 = 16384 bits, at 64x = 256), and at
 * PDM_SAMPLE_RATE_HZ that gives a 31.25 Hz bin. Callers must supply their own
 * scratch arrays; nothing here allocates, and on the display CPU the caller's
 * scratch must be file-scope static -- two float[256] arrays are 2 KB and the
 * SDK's default PICO_STACK_SIZE is 2048.
 */
#ifndef FWOG_SPECTRUM_H
#define FWOG_SPECTRUM_H
#include <stdint.h>

#define FWOG_FFT_MAX_N 256u

/* In-place radix-2 decimation-in-time complex FFT. `n` MUST be a power of two
 * and at least 2; anything else returns with the arrays untouched. */
void fwog_fft_r2(float *re, float *im, unsigned n);

/* The lowest bin the dominant-bin search will consider.
 *
 * THREE, not one, and the reason is measured rather than assumed. A Hann
 * window's main lobe is 4 bins wide, so a DC term does not stay in bin 0 -- it
 * spreads into bins 1 and 2. With a 12000-count bias and a 300-count tone at
 * bin 20 (roughly what a real PDM capture looks like: heavily DC-biased, small
 * AC), the measured magnitudes are:
 *
 *     bin 0: 1529939     bin 2: 2021      bin 20 (the tone): 19129
 *     bin 1:  769453     bin 3:  756
 *
 * Excluding only bin 0 therefore still reports bin 1 -- DC leakage -- and the
 * function confidently returns about 31 Hz for a room with a 625 Hz tone
 * playing in it. Excluding through bin 2 is what actually solves it; bin 3's
 * leakage is already 25x below the tone.
 *
 * At PDM_SAMPLE_RATE_HZ over FWOG_FFT_MAX_N points, bin 3 is 93.75 Hz -- below
 * dcblock.h's ~127 Hz corner anyway, so nothing usable is given up. */
#define FWOG_SPECTRUM_MIN_BIN 3u

/* Dominant spectral bin of `n` int16 samples.
 *
 * Copies `x` into `re` through a Hann window, zeroes `im`, transforms in
 * place, then returns the bin with the largest magnitude over
 * FWOG_SPECTRUM_MIN_BIN .. n/2-1.
 *
 * DC and its window leakage are EXCLUDED deliberately -- see
 * FWOG_SPECTRUM_MIN_BIN above. The PDM idle stream is heavily DC-biased (see
 * dcblock.h) and without this the low bins win essentially every measurement,
 * producing a number that is useless -- the same failure mode
 * pdm_density_spread()'s header describes for whole-buffer density. Running
 * fwog_dcblock_inplace() first is still recommended and is what the bench path
 * does; this exclusion is the belt to that's braces.
 *
 * The Hann window costs about 1.5 bins of resolution and buys roughly 31 dB of
 * sidelobe suppression. That is the right trade here: a bench tone is rarely
 * exactly on a bin centre, and without a window its leakage skirt can beat a
 * genuine neighbouring peak.
 *
 * `re` and `im` must each hold at least `n` floats and are left holding the
 * transform, so a caller can read magnitudes back out. Returns 0 if `n` is not
 * a valid power of two >= 4, if n/2 leaves no bin above FWOG_SPECTRUM_MIN_BIN
 * to search, or if any pointer is NULL.
 *
 * Convert to Hz with: bin * sample_rate_hz / n. */
unsigned fwog_spectrum_dominant_bin(const int16_t *x, unsigned n,
                                    float *re, float *im);

/* Root-mean-square of `n` samples. Returns 0 for n == 0 or a NULL pointer.
 * Accumulates in uint64_t: 256 samples of 32768^2 is 2.7e11, which overflows
 * uint32_t by three orders of magnitude, and this is exactly the kind of
 * silent wrap that turns a loud capture into a quiet reading. */
uint32_t fwog_rms_i16(const int16_t *x, unsigned n);

/* Largest absolute sample value. Returns uint16_t rather than int16_t because
 * |INT16_MIN| is 32768, which does not fit in an int16_t -- the obvious
 * `abs()` version silently returns -32768 for the loudest possible input. */
uint16_t fwog_peak_i16(const int16_t *x, unsigned n);

#endif
