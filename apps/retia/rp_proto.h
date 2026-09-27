#ifndef RP_PROTO_H
#define RP_PROTO_H
#include <stdint.h>

#ifdef _MSC_VER
#pragma pack(push, 1)
#define RP_PACKED
#else
#define RP_PACKED __attribute__((packed))
#endif

#define RP_MSG_STATE 0x52u
#define RP_MAX_LINES 5u
#define RP_LINE_LEN  28u
#define RP_STATUS_LEN 32u

typedef struct RP_PACKED {
    uint8_t type;
    uint8_t mode;
    uint8_t line_count;
    uint8_t pullups;
    char    lines[RP_MAX_LINES][RP_LINE_LEN];
    char    status[RP_STATUS_LEN];
} rp_state_t;
_Static_assert(sizeof(rp_state_t) <= 180, "rp_state_t");

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif
