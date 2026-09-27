/* NEC 32-bit packing for short DiskGlass notes, OG to OG.
 *
 * Each valid frame carries a command byte (with NEC complement) and a
 * 16-bit address. Start/end use address high byte 0xD6 as a mark so a
 * TV remote is not a file. Payload is at most DG_IR_MAX bytes — the
 * PHY is ~10 frames/s. Host-tested; no PIO. */
#ifndef DG_XFER_H
#define DG_XFER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DG_IR_MAX     240u
#define DG_IR_MARK    0xD6u
#define DG_IR_CMD_ST  0xFEu
#define DG_IR_CMD_END 0xFFu

uint32_t dg_ir_pack(uint8_t cmd, uint16_t addr);
uint8_t  dg_ir_cmd(uint32_t code);
uint16_t dg_ir_addr(uint32_t code);
uint16_t dg_ir_crc(const uint8_t *p, size_t n);

uint32_t dg_ir_start(uint16_t nbytes);
uint32_t dg_ir_data(uint8_t seq, uint8_t a, uint8_t b);
uint32_t dg_ir_end(uint16_t crc);

typedef struct {
    uint8_t  buf[DG_IR_MAX];
    uint16_t nbytes;
    uint16_t got;
    uint8_t  seq;
    bool     active;
} dg_ir_rx_t;

void dg_ir_rx_reset(dg_ir_rx_t *s);
/* Feed one NEC word. Returns true when a complete payload is in s->buf. */
bool dg_ir_rx_feed(dg_ir_rx_t *s, uint32_t code);

#endif
