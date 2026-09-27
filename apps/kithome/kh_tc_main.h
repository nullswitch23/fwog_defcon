#ifndef KH_TC_MAIN_H
#define KH_TC_MAIN_H
#include <stddef.h>
#include <stdint.h>

void kh_tc_main_init(void);
void kh_tc_main_frame(const uint8_t *buf, size_t n);
void kh_tc_main_close(void);

#endif
