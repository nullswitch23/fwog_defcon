#ifndef OL_LF_MAIN_H
#define OL_LF_MAIN_H
#include <stddef.h>
#include <stdint.h>

void ol_lf_main_enter(void);
void ol_lf_main_leave(void);
void ol_lf_main_frame(const uint8_t *buf, size_t n);
void ol_lf_main_tick(uint32_t now);

#endif
