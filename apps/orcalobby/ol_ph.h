#ifndef OL_PH_H
#define OL_PH_H
#include "fwog_display.h"
#include <stddef.h>
#include <stdint.h>

void ol_ph_enter(void);
void ol_ph_frame(const uint8_t *buf, size_t n);
void ol_ph_buttons(const fwog_power_t *p, bool y_long, bool g_long,
                   bool gray_long, bool red_tap);
void ol_ph_paint(void);

#endif
