#ifndef RP_DISPATCH_H
#define RP_DISPATCH_H
#include "rp_pins.h"

void rp_dispatch_setup(rp_ctx_t *ctx);
void rp_dispatch_run(rp_ctx_t *ctx);
void rp_dispatch_poll(rp_ctx_t *ctx);

#endif
