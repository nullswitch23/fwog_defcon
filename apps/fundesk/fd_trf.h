#ifndef FD_TRF_H
#define FD_TRF_H
#include "fwog_display.h"
#include <stddef.h>
#include <stdint.h>

void fd_trf_enter(void);
void fd_trf_leave(void);
void fd_trf_frame(const uint8_t *buf, size_t n);
void fd_trf_buttons(const fwog_power_t *p, bool y_long, bool g_long,
                    bool gray_long, bool red_tap);
void fd_trf_tick(uint32_t now);
void fd_trf_paint(void);

#endif
