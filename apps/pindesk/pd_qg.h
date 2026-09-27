#ifndef PD_QG_H
#define PD_QG_H
#include "fwog_display.h"
#include <stddef.h>
#include <stdint.h>

void pd_qg_enter(void);
void pd_qg_frame(const uint8_t *buf, size_t n);
void pd_qg_buttons(const fwog_power_t *p, bool y_long, bool g_long,
                   bool gray_long, bool red_tap);
void pd_qg_paint(void);

#endif
