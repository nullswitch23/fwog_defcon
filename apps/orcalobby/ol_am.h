#ifndef OL_AM_H
#define OL_AM_H
#include "fwog_display.h"
#include <stddef.h>
#include <stdint.h>

void ol_am_enter(void);
void ol_am_frame(const uint8_t *buf, size_t n);
void ol_am_buttons(const fwog_power_t *p, bool y_long, bool g_long,
                   bool gray_long, bool red_tap);
void ol_am_paint(void);

#endif
