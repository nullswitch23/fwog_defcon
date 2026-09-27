#ifndef KH_DG_H
#define KH_DG_H
#include "fwog_display.h"
#include <stddef.h>
#include <stdint.h>

void kh_dg_enter(void);
void kh_dg_leave(void);
void kh_dg_frame(const uint8_t *buf, size_t n);
void kh_dg_buttons(const fwog_power_t *p, bool y_long, bool g_long,
                   bool gray_long, bool red_tap);
void kh_dg_tick(uint32_t now);
void kh_dg_paint(void);
void kh_dg_leds(bool power_armed);

#endif
