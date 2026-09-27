#ifndef TB_C5_H
#define TB_C5_H
#include <stdbool.h>
#include <stdint.h>

bool tb_dtmf_freqs(char digit, uint16_t *f1, uint16_t *f2);
bool tb_c5_freqs(const char *code, uint16_t *f1, uint16_t *f2);

#endif
