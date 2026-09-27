#ifndef FD_PF_H
#define FD_PF_H
#include "fwog_display.h"
#include <stddef.h>
#include <stdint.h>

void fd_pf_enter(void);
void fd_pf_leave(void);
void fd_pf_frame(const uint8_t *buf, size_t n);
void fd_pf_buttons(const fwog_power_t *p, bool y_long, bool g_long,
                   bool gray_long, bool red_tap);
void fd_pf_tick(uint32_t now);
void fd_pf_paint(void);

#endif
