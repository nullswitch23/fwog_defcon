#ifndef OL_PROTO_H
#define OL_PROTO_H
#include <stdint.h>
#ifdef _MSC_VER
#pragma pack(push, 1)
#define OL_PACKED
#else
#define OL_PACKED __attribute__((packed))
#endif
#define DESK_MSG_SEL 0x71u
typedef struct OL_PACKED {
    uint8_t type;
    uint8_t app;
    uint8_t _pad[2];
} desk_sel_t;
_Static_assert(sizeof(desk_sel_t) == 4, "desk_sel_t");
#ifdef _MSC_VER
#pragma pack(pop)
#endif
#endif
