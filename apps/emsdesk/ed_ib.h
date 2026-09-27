#ifndef ED_IB_H
#define ED_IB_H
#include "fwog_display.h"
#include <stddef.h>
#include <stdint.h>

void ed_ib_enter(void);
void ed_ib_frame(const uint8_t *buf, size_t n);
void ed_ib_buttons(const fwog_power_t *p, uint32_t now,
                   bool y_long, bool g_long, bool gray_long, bool red_tap);
void ed_ib_paint(void);

#endif
