#ifndef TB_DETECT_H
#define TB_DETECT_H
#include <stdbool.h>
#include <stdint.h>

/* 2600 Hz wink detector (8 kHz PCM: target capped to 2500 Hz). */
bool tb_detect_2600_wink(const int16_t *pcm, unsigned n);

#endif
