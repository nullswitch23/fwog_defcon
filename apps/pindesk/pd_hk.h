#ifndef PD_HK_H
#define PD_HK_H
#include "fwog_display.h"
#include <stddef.h>
#include <stdint.h>

void pd_hk_enter(void);
void pd_hk_frame(const uint8_t *buf, size_t n);
void pd_hk_buttons(const fwog_power_t *p, bool y_long, bool g_long,
                   bool gray_long, bool red_tap);
void pd_hk_paint(void);

#endif
