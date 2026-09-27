/* Inter-CPU messages for apps/diskglass. Types 0x70–0x73 sit above
 * PingHalo (0x62/0x63). Packed, little-endian, same rule as io_proto.h. */
#ifndef DG_PROTO_H
#define DG_PROTO_H
#include "dg_file.h"
#include "dg_xfer.h"
#include <stdint.h>

#define DG_MSG_CMD  0x70u
#define DG_MSG_LIST 0x71u
#define DG_MSG_META 0x72u
#define DG_MSG_DATA 0x73u
#define DG_MSG_PUT  0x74u
#define DG_MSG_HOST 0x75u

#define DG_CMD_LIST   0u
#define DG_CMD_OPEN   1u
#define DG_CMD_READ   2u
#define DG_CMD_CLOSE  3u
#define DG_CMD_REPLAY 4u
#define DG_CMD_PUT    5u
#define DG_CMD_DEL    6u
#define DG_CMD_RUN    7u
#define DG_CMD_STOP   8u

#define DG_RUN_ONCE   0u
#define DG_RUN_LOOP   1u

#define DG_HOST_LED  0u
#define DG_HOST_DONE 1u
#define DG_HOST_FB   2u

#define DG_FB_W   32u
#define DG_FB_H   24u
#define DG_FB_MAX (DG_FB_W * DG_FB_H)

#define DG_FLAG_DIR     0x01u
#define DG_FLAG_END     0x02u
#define DG_FLAG_MOUNTED 0x04u
#define DG_FLAG_ERR     0x08u
#define DG_FLAG_PLAYING 0x10u

#define DG_NAME_LEN 24u
#define DG_PATH_LEN 40u
#define DG_DATA_MAX 1024u
#define DG_CSV_HINT 384u
#define DG_MAX_ENT  48u

#ifdef _MSC_VER
#pragma pack(push, 1)
#define DGP_PACKED
#else
#define DGP_PACKED __attribute__((packed))
#endif

typedef struct DGP_PACKED {
    uint8_t  type;      /* DG_MSG_CMD */
    uint8_t  cmd;
    uint16_t index;
    uint32_t offset;
    uint16_t len;
    uint16_t _pad;
    char     path[DG_PATH_LEN];
} dg_cmd_t;
_Static_assert(sizeof(dg_cmd_t) == 52, "dg_cmd_t");

typedef struct DGP_PACKED {
    uint8_t  type;      /* DG_MSG_LIST */
    uint8_t  flags;
    uint16_t index;
    uint16_t count;
    uint16_t kind;
    uint32_t size;
    char     name[DG_NAME_LEN];
} dg_list_t;
_Static_assert(sizeof(dg_list_t) == 36, "dg_list_t");

typedef struct DGP_PACKED {
    uint8_t  type;      /* DG_MSG_META */
    uint8_t  flags;
    uint16_t kind;
    uint32_t size;
    uint32_t duration_us;
    uint32_t freq_hz;
    uint32_t sample_hz;
    int16_t  peak_rssi;
    uint16_t edges;
    char     name[DG_NAME_LEN];
    char     app[DG_APP_LEN];
} dg_meta_t;
_Static_assert(sizeof(dg_meta_t) == 60, "dg_meta_t");

typedef struct DGP_PACKED {
    uint8_t  type;      /* DG_MSG_DATA */
    uint8_t  flags;
    uint16_t n;
    uint32_t offset;
    uint8_t  bytes[DG_DATA_MAX];
} dg_data_t;
_Static_assert(sizeof(dg_data_t) == 1032, "dg_data_t");
_Static_assert(sizeof(dg_data_t) <= 4160u, "dg_data_t fits the link");

/* Display -> main: save a short received note under /inbox. */
typedef struct DGP_PACKED {
    uint8_t  type;      /* DG_MSG_PUT */
    uint8_t  flags;
    uint16_t n;
    char     name[DG_NAME_LEN];
    uint8_t  bytes[DG_IR_MAX];
} dg_put_t;
_Static_assert(sizeof(dg_put_t) == 268, "dg_put_t");

/* Main -> display while a WASM script runs (LED), or when RUN finishes. */
typedef struct DGP_PACKED {
    uint8_t type;   /* DG_MSG_HOST */
    uint8_t op;     /* DG_HOST_LED / DG_HOST_DONE / DG_HOST_FB */
    uint8_t idx, r, g, b;
    uint16_t code;
} dg_host_t;
_Static_assert(sizeof(dg_host_t) == 8, "dg_host_t");

/* 32x24 RGB332 tile; display nearest-neighbor scales to 320x240. */
typedef struct DGP_PACKED {
    uint8_t  type;  /* DG_MSG_HOST */
    uint8_t  op;    /* DG_HOST_FB */
    uint8_t  w, h;
    uint16_t n;
    uint8_t  px[DG_FB_MAX];
} dg_host_fb_t;
_Static_assert(sizeof(dg_host_fb_t) == 774, "dg_host_fb_t");

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif
