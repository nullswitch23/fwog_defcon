#ifndef KH_TC_H
#define KH_TC_H
#include "fwog_display.h"
#include <stddef.h>
#include <stdint.h>

void kh_tc_enter(bool pdm);
void kh_tc_leave(void);
void kh_tc_frame(const uint8_t *buf, size_t n);
void kh_tc_buttons(const fwog_power_t *p, bool y_long, bool g_long,
                   bool gray_long, bool red_tap);
void kh_tc_mic(uint32_t now);
void kh_tc_paint(void);
void kh_tc_leds(bool power_armed);

#endif
