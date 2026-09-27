#ifndef RP_PINS_H
#define RP_PINS_H
#include "rp_mode.h"
#include "rp_proto.h"
#include "common/io_cfg.h"
#include <stdbool.h>

typedef struct {
    rp_mode_t     mode;
    bool          pullups;
    uint32_t      uart_baud;
    uint8_t       uart_data_bits;
    uint32_t      i2c_hz;
    uint32_t      spi_hz;
    char          status[RP_STATUS_LEN];
} rp_ctx_t;

void rp_ctx_init(rp_ctx_t *ctx);
bool rp_pins_apply_mode(rp_ctx_t *ctx, rp_mode_t mode);
void rp_state_build(const rp_ctx_t *ctx, rp_state_t *st);

#endif
