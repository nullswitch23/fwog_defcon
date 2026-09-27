#include "dg_xfer.h"
#include <string.h>

static bool cmd_valid(uint32_t code) {
    uint8_t inv = (uint8_t)((code >> 24) & 0xFFu);
    uint8_t cmd = (uint8_t)((code >> 16) & 0xFFu);
    return (uint8_t)(cmd ^ inv) == 0xFFu;
}

uint32_t dg_ir_pack(uint8_t cmd, uint16_t addr) {
    uint8_t inv = (uint8_t)~cmd;
    return ((uint32_t)inv << 24) | ((uint32_t)cmd << 16) | (uint32_t)addr;
}

uint8_t dg_ir_cmd(uint32_t code) {
    return (uint8_t)((code >> 16) & 0xFFu);
}

uint16_t dg_ir_addr(uint32_t code) {
    return (uint16_t)(code & 0xFFFFu);
}

uint16_t dg_ir_crc(const uint8_t *p, size_t n) {
    uint16_t c = 0xFFFFu;
    size_t i;
    unsigned b;
    if (!p) return c;
    for (i = 0; i < n; i++) {
        c ^= (uint16_t)p[i] << 8;
        for (b = 0; b < 8u; b++) {
            if (c & 0x8000u) c = (uint16_t)((c << 1) ^ 0x1021u);
            else c = (uint16_t)(c << 1);
        }
    }
    return c;
}

uint32_t dg_ir_start(uint16_t nbytes) {
    if (nbytes > DG_IR_MAX) nbytes = DG_IR_MAX;
    return dg_ir_pack(DG_IR_CMD_ST, (uint16_t)((DG_IR_MARK << 8) | (nbytes & 0xFFu)));
}

uint32_t dg_ir_data(uint8_t seq, uint8_t a, uint8_t b) {
    return dg_ir_pack(seq, (uint16_t)(((uint16_t)a << 8) | b));
}

uint32_t dg_ir_end(uint16_t crc) {
    return dg_ir_pack(DG_IR_CMD_END, crc);
}

void dg_ir_rx_reset(dg_ir_rx_t *s) {
    if (!s) return;
    memset(s, 0, sizeof *s);
}

bool dg_ir_rx_feed(dg_ir_rx_t *s, uint32_t code) {
    uint8_t cmd;
    uint16_t addr;
    if (!s || !cmd_valid(code)) return false;
    cmd = dg_ir_cmd(code);
    addr = dg_ir_addr(code);

    if (cmd == DG_IR_CMD_ST) {
        uint16_t n;
        if ((uint8_t)(addr >> 8) != DG_IR_MARK) return false;
        n = (uint16_t)(addr & 0xFFu);
        if (n == 0u || n > DG_IR_MAX) return false;
        dg_ir_rx_reset(s);
        s->nbytes = n;
        s->active = true;
        return false;
    }
    if (!s->active) return false;
    if (cmd == DG_IR_CMD_END) {
        uint16_t want = dg_ir_crc(s->buf, s->nbytes);
        bool ok = (s->got >= s->nbytes) && (addr == want);
        s->active = false;
        return ok;
    }
    if (cmd != s->seq) return false;
    if (s->got < s->nbytes) s->buf[s->got++] = (uint8_t)(addr >> 8);
    if (s->got < s->nbytes) s->buf[s->got++] = (uint8_t)(addr & 0xFFu);
    s->seq++;
    return false;
}
