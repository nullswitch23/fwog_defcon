#ifndef KH_DG_MAIN_H
#define KH_DG_MAIN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void kh_dg_main_init(void);
void kh_dg_main_frame(const uint8_t *buf, size_t n);
void kh_dg_main_idle_radio(void);
bool kh_dg_main_replay_armed(void);

#endif
