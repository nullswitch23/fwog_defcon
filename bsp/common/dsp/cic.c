/* 3rd-order CIC decimator -- see cic.h for provenance, scaling and the
 * corrected droop table. Arithmetic transcribed unchanged from wilibsp's
 * bsp/dsp/cic.c (MIT, same copyright holder). */
#include "common/dsp/cic.h"

void fwog_cic_init(fwog_cic_t *c) {
    for (int i = 0; i < FWOG_CIC_ORDER; i++) { c->integ[i] = 0; c->comb[i] = 0; }
    c->phase = 0u;
}

int fwog_cic_push_bit(fwog_cic_t *c, int bit, int16_t *out) {
    const int32_t x = bit ? 1 : -1;          /* map PDM {0,1} -> {-1,+1} */

    /* Integrator cascade, at the full PDM rate. */
    c->integ[0] += x;
    for (int i = 1; i < FWOG_CIC_ORDER; i++) c->integ[i] += c->integ[i - 1];

    if (++c->phase < FWOG_CIC_DECIMATE) return 0;
    c->phase = 0u;

    /* Comb cascade, at the decimated rate. */
    int32_t v = c->integ[FWOG_CIC_ORDER - 1];
    for (int i = 0; i < FWOG_CIC_ORDER; i++) {
        const int32_t d = v - c->comb[i];
        c->comb[i] = v;
        v = d;
    }

    int32_t s = v >> (FWOG_CIC_SHIFT - 16);  /* scale toward int16 */
    if (s > 32767)  s = 32767;
    if (s < -32768) s = -32768;
    *out = (int16_t)s;
    return 1;
}
