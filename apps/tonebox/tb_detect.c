#include "tb_detect.h"
#include <math.h>

#define TB_DET_PI     3.14159265f
#define TB_DET_RATE   8000.0f
#define TB_DET_TARGET 2500.0f /* 2600 Hz at 8 kHz Nyquist cap */
#define TB_DET_THRESH 8.0e7f

bool tb_detect_2600_wink(const int16_t *pcm, unsigned n) {
    if (!pcm || n < 32u) return false;
    const float k = 2.0f * TB_DET_PI * TB_DET_TARGET / TB_DET_RATE;
    const float coeff = 2.0f * cosf(k);
    float q0 = 0.0f, q1 = 0.0f, q2 = 0.0f;
    for (unsigned i = 0; i < n; i++) {
        q0 = coeff * q1 - q2 + (float)pcm[i];
        q2 = q1;
        q1 = q0;
    }
    const float real = q1 - q2 * cosf(k);
    const float imag = q2 * sinf(k);
    const float pwr = real * real + imag * imag;
    return pwr >= TB_DET_THRESH;
}
