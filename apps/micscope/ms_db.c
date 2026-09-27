#include "ms_db.h"
#include <math.h>

static float db10(uint32_t num, uint32_t den) {
    if (den < 1u) den = 1u;
    if (num < 1u) num = 1u;
    return 10.f * log10f((float)num / (float)den);
}

int ms_spl_approx(uint32_t rms, uint32_t cal_rms) {
    /* RMS is amplitude: 20 log10 = 10 log10 of power ~ rms^2. */
    float db = 2.f * db10(rms, cal_rms);
    int spl = MS_QUIET_SPL + (int)(db + (db >= 0.f ? 0.5f : -0.5f));
    if (spl < 0) spl = 0;
    if (spl > 120) spl = 120;
    return spl;
}

unsigned ms_bar_px(uint32_t mag, uint32_t cal_mag, unsigned h) {
    float db;
    int px;
    if (h == 0u) return 0u;
    db = db10(mag, cal_mag);
    if (db < 0.f) db = 0.f;
    px = (int)(db * (float)h / (float)MS_DB_RANGE + 0.5f);
    if (px < 0) px = 0;
    if (px > (int)h) px = (int)h;
    return (unsigned)px;
}
