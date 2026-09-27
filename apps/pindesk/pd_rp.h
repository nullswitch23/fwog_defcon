#ifndef PD_RP_H
#define PD_RP_H
#include "fwog_display.h"
#include <stddef.h>
#include <stdint.h>

void pd_rp_enter(void);
void pd_rp_frame(const uint8_t *buf, size_t n);
void pd_rp_paint(void);

#endif
