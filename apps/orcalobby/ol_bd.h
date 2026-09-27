#ifndef OL_BD_H
#define OL_BD_H
#include "fwog_display.h"
#include <stddef.h>
#include <stdint.h>

void ol_bd_enter(void);
void ol_bd_frame(const uint8_t *buf, size_t n);
void ol_bd_buttons(const fwog_power_t *p, bool y_long, bool g_long,
                   bool gray_long, bool red_tap);
void ol_bd_paint(void);

#endif
