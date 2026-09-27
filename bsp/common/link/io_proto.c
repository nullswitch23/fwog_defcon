#include "common/link/io_proto.h"
#include <string.h>

size_t fwog_io_proto_build_config_enable(void *out, size_t cap, uint8_t seq,
                                         bool enable) {
    if (cap < sizeof(fwog_io_config_enable_t)) return 0u;
    fwog_io_config_enable_t m;
    m.type   = FWOG_IO_MSG_CONFIG_ENABLE;
    m.seq    = seq;
    m.enable = enable ? 1u : 0u;
    memcpy(out, &m, sizeof m);
    return sizeof m;
}

size_t fwog_io_proto_build_set_dirs(void *out, size_t cap, uint8_t seq,
                                    const fwog_io_cfg_t *cfg) {
    if (cap < sizeof(fwog_io_set_dirs_t)) return 0u;
    fwog_io_set_dirs_t m;
    m.type       = FWOG_IO_MSG_SET_DIRS;
    m.seq        = seq;
    m.dirword    = fwog_io_pack_dirword(cfg);
    m.i2c_pullup = cfg->i2c_pullup ? 1u : 0u;
    m.radio1_ant = (uint8_t)cfg->radio1_ant;
    m.radio2_ant = (uint8_t)cfg->radio2_ant;
    memcpy(out, &m, sizeof m);
    return sizeof m;
}

size_t fwog_io_proto_build_ack(void *out, size_t cap, uint8_t seq,
                               uint8_t answering, bool ok) {
    if (cap < sizeof(fwog_io_ack_t)) return 0u;
    fwog_io_ack_t m;
    m.type      = FWOG_IO_MSG_ACK;
    m.answering = answering;
    m.seq       = seq;
    m.ok        = ok ? 1u : 0u;
    memcpy(out, &m, sizeof m);
    return sizeof m;
}

uint8_t fwog_io_proto_type(const void *payload, size_t len) {
    if (len == 0u) return 0u;
    uint8_t t = ((const uint8_t *)payload)[0];
    switch (t) {
    case FWOG_IO_MSG_CONFIG_ENABLE:
        return len >= sizeof(fwog_io_config_enable_t) ? t : 0u;
    case FWOG_IO_MSG_SET_DIRS:
        return len >= sizeof(fwog_io_set_dirs_t) ? t : 0u;
    case FWOG_IO_MSG_ACK:
        return len >= sizeof(fwog_io_ack_t) ? t : 0u;
    default:
        return 0u;
    }
}
