#ifndef BS_PROTO_H
#define BS_PROTO_H
#include <stdint.h>

#ifdef _MSC_VER
#pragma pack(push, 1)
#define BS_PACKED
#else
#define BS_PACKED __attribute__((packed))
#endif

#define BS_MSG_CMD 0x40u
#define BS_MSG_ST  0x41u
#define BS_BINS    64u
#define BS_TOP     5u

#define BS_CMD_SWEEP  0u
#define BS_CMD_FREEZE 1u
#define BS_CMD_HUNT   2u
#define BS_CMD_CLEAR  3u

#define BS_MODE_SWEEP  0u
#define BS_MODE_FREEZE 1u
#define BS_MODE_HUNT   2u

typedef struct BS_PACKED {
    uint8_t  type;
    uint8_t  cmd;
    uint8_t  band;     /* 0=315, 1=433, 2=900 */
    uint8_t  _pad;
} bs_cmd_t;
_Static_assert(sizeof(bs_cmd_t) == 4, "bs_cmd_t");

typedef struct BS_PACKED {
    uint32_t hz;
    int16_t  dbm;
    uint8_t  band;
    uint8_t  _pad;
} bs_hit_t;
_Static_assert(sizeof(bs_hit_t) == 8, "bs_hit_t");

typedef struct BS_PACKED {
    uint8_t  type;
    uint8_t  band;
    uint8_t  ok;
    uint8_t  mode;     /* BS_MODE_* */
    uint32_t f0_hz;
    uint32_t step_hz;
    uint32_t peak_hz;
    int16_t  peak_dbm;
    uint16_t peak_bin;
    uint8_t  n_hits;
    uint8_t  _pad[3];  /* keep hit[] 4-aligned; RP2040 faults unaligned u32 */
    bs_hit_t hit[BS_TOP];
    uint8_t  bar[BS_BINS];
} bs_status_t;
_Static_assert(sizeof(bs_status_t) == 128, "bs_status_t");

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif
