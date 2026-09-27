#ifndef ED_OC_H
#define ED_OC_H
#include "fwog_display.h"
#include <stddef.h>
#include <stdint.h>

void ed_oc_enter(void);
void ed_oc_frame(const uint8_t *buf, size_t n);
void ed_oc_buttons(const fwog_power_t *p, uint32_t now,
                   bool y_long, bool g_long, bool gray_long, bool red_tap);
void ed_oc_paint(void);

#endif
