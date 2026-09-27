#ifndef FD_VP_H
#define FD_VP_H
#include "fwog_display.h"
#include <stddef.h>
#include <stdint.h>

void fd_vp_enter(void);
void fd_vp_leave(void);
void fd_vp_frame(const uint8_t *buf, size_t n);
void fd_vp_buttons(const fwog_power_t *p, bool y_long, bool g_long,
                   bool gray_long, bool red_tap);
void fd_vp_tick(uint32_t now);
void fd_vp_paint(void);

#endif
