/* One-pole DC-blocking high-pass -- see dcblock.h. Transcribed from wilibsp's
 * bsp/dsp/dcblock.c (MIT, same copyright holder). */
#include "common/dsp/dcblock.h"

void fwog_dcblock_inplace(int16_t *buf, unsigned n) {
    if (buf == 0 || n == 0u) return;

    float prev_x = (float)buf[0];
    float prev_y = 0.0f;
    buf[0] = 0;   /* first output is defined as 0 -- no prior sample */

    for (unsigned i = 1u; i < n; i++) {
        const float x = (float)buf[i];
        float y = x - prev_x + FWOG_DCBLOCK_R * prev_y;
        prev_x = x;
        /* prev_y carries the UNCLAMPED value, as in the original: the clamp
         * below belongs to the int16 output, not to the filter's own state.
         * Feeding a clamped value back would change the pole's behaviour
         * whenever the signal saturates, which is exactly when the filter
         * most needs to stay linear. */
        prev_y = y;
        if (y > 32767.0f)       y = 32767.0f;
        else if (y < -32768.0f) y = -32768.0f;
        buf[i] = (int16_t)y;
    }
}
