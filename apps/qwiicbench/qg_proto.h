#ifndef QG_PROTO_H
#define QG_PROTO_H
#include <stdint.h>

#ifdef _MSC_VER
#pragma pack(push, 1)
#define QG_PACKED
#else
#define QG_PACKED __attribute__((packed))
#endif

#define QG_MSG_CMD 0x71u
#define QG_MSG_ST  0x72u
#define QG_CMD_SCAN 1u
#define QG_MAX      4u
#define QG_NAME_N   10u
#define QG_VAL_N    12u

typedef struct QG_PACKED {
    uint8_t addr;
    uint8_t known;  /* 1 = BME280/BH1750; no T9 nick */
    char    name[QG_NAME_N];
    char    val[QG_VAL_N];
    int16_t plot;   /* sparkline sample, sensor units */
} qg_chan_t;
_Static_assert(sizeof(qg_chan_t) == 26, "qg_chan_t");

typedef struct QG_PACKED {
    uint8_t   type;
    uint8_t   n;
    uint8_t   io_ok;
    uint8_t   _pad;
    qg_chan_t chan[QG_MAX];
} qg_status_t;
_Static_assert(sizeof(qg_status_t) == 108, "qg_status_t");

typedef struct QG_PACKED {
    uint8_t type;
    uint8_t cmd;
    uint8_t _pad[2];
} qg_cmd_t;
_Static_assert(sizeof(qg_cmd_t) == 4, "qg_cmd_t");

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif
