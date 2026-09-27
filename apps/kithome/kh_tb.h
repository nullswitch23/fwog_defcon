#ifndef KH_TB_H
#define KH_TB_H
#include "fwog_display.h"
#include <stddef.h>
#include <stdint.h>

void kh_tb_enter(bool pdm);
void kh_tb_leave(void);
void kh_tb_frame(const uint8_t *buf, size_t n);
void kh_tb_buttons(const fwog_power_t *p, bool y_long, bool g_long,
                   bool gray_long, bool red_tap);
void kh_tb_tick(uint32_t now);
void kh_tb_paint(void);
void kh_tb_leds(bool power_armed);

#endif
