/* One-pole DC-blocking high-pass for int16 PCM.
 *
 *     y[n] = x[n] - x[n-1] + R*y[n-1]
 *
 * ---- Provenance ----
 * Adapted from wilibsp (github.com/freewili/wilibsp), bsp/dsp/dcblock.{c,h}
 * -- MIT, same copyright holder. Transcribed unchanged apart from the fwog_
 * prefix and this header.
 *
 * ---- Why it exists ----
 * The idle PDM stream is heavily DC-biased, and room noise plus handling
 * carries strong sub-200 Hz energy. Left in, that leakage skirt floods the low
 * FFT bins and crushes the SNR of whatever tone is being looked for. A gentle
 * blocker is not enough; this one is deliberately aggressive.
 *
 * ---- APPLIED BY THE CALLER, IN THE ANALYSIS PATH ONLY ----
 * Never inside the CIC. Exactly as wilibsp has it. A decimator that silently
 * high-passes is lossy for every consumer, including ones that want the DC
 * term (a level or bias measurement, for instance).
 *
 * ---- The cutoff is NOT the same as wilibsp's, because the rate differs ----
 * wilibsp's header says "R=0.90 -> cutoff ~250 Hz @ 16 kHz". This BSP decimates
 * to PDM_SAMPLE_RATE_HZ = 8000, half that rate, so the SAME R gives roughly
 * HALF the cutoff: fc ~= fs*(1-R)/(2*pi) = 8000*0.10/6.283 = about 127 Hz.
 *
 * R is carried over at 0.90 anyway, for the same reason the CIC's scaling is:
 * it is a constant validated against real microphone levels on FW2, and 127 Hz
 * still comfortably passes the 1 kHz band of interest while cutting the
 * sub-200 Hz energy this exists for. But it is LESS aggressive here than it
 * was there, which is the opposite of the direction the original comment's
 * reasoning was pushing. If a bench measurement shows the low bins still
 * flooded, retuning R to about 0.80 restores wilibsp's ~250 Hz corner at this
 * board's rate -- that is the knob, and it is a measurement's decision, not
 * one to make in advance.
 */
#ifndef FWOG_DCBLOCK_H
#define FWOG_DCBLOCK_H
#include <stdint.h>

#ifndef FWOG_DCBLOCK_R
#define FWOG_DCBLOCK_R 0.90f
#endif

/* In-place high-pass filter of `n` samples. Stateless across calls: each block
 * re-initialises from buf[0], and buf[0] itself is defined as 0 because there
 * is no prior sample. That means this is NOT continuous across buffer
 * boundaries the way the CIC is -- it is an analysis-path filter applied to a
 * block that is about to be transformed, not a streaming stage. */
void fwog_dcblock_inplace(int16_t *buf, unsigned n);

#endif
