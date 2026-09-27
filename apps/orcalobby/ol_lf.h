#ifndef OL_LF_H
#define OL_LF_H
#include "fwog_display.h"
#include <stddef.h>
#include <stdint.h>

void ol_lf_enter(void);
void ol_lf_frame(const uint8_t *buf, size_t n);
void ol_lf_buttons(const fwog_power_t *p, bool y_long, bool g_long,
                   bool gray_long, bool red_tap);
void ol_lf_paint(void);

#endif
