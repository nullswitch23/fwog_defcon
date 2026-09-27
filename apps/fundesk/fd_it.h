#ifndef FD_IT_H
#define FD_IT_H
#include "fwog_display.h"
#include <stddef.h>
#include <stdint.h>

void fd_it_enter(void);
void fd_it_leave(void);
void fd_it_frame(const uint8_t *buf, size_t n);
void fd_it_buttons(const fwog_power_t *p, bool y_long, bool g_long,
                   bool gray_long, bool red_tap);
void fd_it_tick(uint32_t now);
void fd_it_paint(void);

#endif
