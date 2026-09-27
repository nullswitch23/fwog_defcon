#ifndef ED_TF_H
#define ED_TF_H
#include "fwog_display.h"
#include <stddef.h>
#include <stdint.h>

void ed_tf_enter(void);
void ed_tf_frame(const uint8_t *buf, size_t n);
void ed_tf_buttons(const fwog_power_t *p, uint32_t now,
                   bool y_long, bool g_long, bool gray_long, bool red_tap);
void ed_tf_paint(void);

#endif
