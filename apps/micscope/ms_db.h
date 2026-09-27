/* Quiet-room dB helpers for MicScope. 35 dB SPL is an assumed indoor
 * floor, not a calibrator. Bars use power dB (10 log10 mag^2 ratio). */
#ifndef MS_DB_H
#define MS_DB_H
#include <stdint.h>

#define MS_QUIET_SPL  35
#define MS_DB_RANGE   60  /* full bar = this many dB above the quiet cal */

/* 20 log10(rms/cal) + MS_QUIET_SPL. cal 0 is treated as 1. Clamped 0..120. */
int ms_spl_approx(uint32_t rms, uint32_t cal_rms);

/* 10 log10(mag/cal) mapped into 0..h pixels over MS_DB_RANGE dB. */
unsigned ms_bar_px(uint32_t mag, uint32_t cal_mag, unsigned h);

#endif
