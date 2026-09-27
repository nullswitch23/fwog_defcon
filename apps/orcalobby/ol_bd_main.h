#ifndef OL_BD_MAIN_H
#define OL_BD_MAIN_H
#include <stddef.h>
#include <stdint.h>

void ol_bd_main_enter(void);
void ol_bd_main_leave(void);
void ol_bd_main_frame(const uint8_t *buf, size_t n);
void ol_bd_main_tick(uint32_t now);

#endif
