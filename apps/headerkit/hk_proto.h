#ifndef HK_PROTO_H
#define HK_PROTO_H
#include <stdint.h>

#define HK_MSG_CMD 0x48u
#define HK_MSG_ST  0x49u
#define HK_CMD_SCAN 1u
#define HK_MAX      16u

#ifdef _MSC_VER
#pragma pack(push, 1)
#define HK_PACKED
#else
#define HK_PACKED __attribute__((packed))
#endif

typedef struct HK_PACKED {
    uint8_t type;
    uint8_t cmd;
    uint8_t _pad[2];
} hk_cmd_t;
_Static_assert(sizeof(hk_cmd_t) == 4, "hk_cmd_t");

typedef struct HK_PACKED {
    uint8_t type;
    uint8_t n;
    uint8_t io_ok;
    uint8_t other_buses_hiz;
    uint8_t addr[HK_MAX];
} hk_status_t;
_Static_assert(sizeof(hk_status_t) == 20, "hk_status_t");

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif
