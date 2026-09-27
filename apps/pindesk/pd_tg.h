#ifndef PD_TG_H
#define PD_TG_H
#include "fwog_display.h"
#include <stddef.h>
#include <stdint.h>

void pd_tg_enter(void);
void pd_tg_frame(const uint8_t *buf, size_t n);
void pd_tg_buttons(const fwog_power_t *p, bool y_long, bool g_long,
                   bool gray_long, bool red_tap);
void pd_tg_paint(void);

#endif
