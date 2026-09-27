#ifndef KH_RG_H
#define KH_RG_H
#include "fwog_display.h"
#include <stddef.h>
#include <stdint.h>

void kh_rg_enter(void);
void kh_rg_leave(void);
void kh_rg_frame(const uint8_t *buf, size_t n);
void kh_rg_paint(void);
void kh_rg_leds(void);

#endif
