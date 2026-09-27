#ifndef RP_MODES_H
#define RP_MODES_H
#include "rp_pins.h"

void rp_modes_help(rp_mode_t mode);
void rp_modes_dispatch(rp_ctx_t *ctx, const char *line);

#endif
