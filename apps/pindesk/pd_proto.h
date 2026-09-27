#ifndef PD_PROTO_H
#define PD_PROTO_H
#include <stdint.h>
#ifdef _MSC_VER
#pragma pack(push, 1)
#define PD_PACKED
#else
#define PD_PACKED __attribute__((packed))
#endif
/* Display -> main. Not 0x72: that is QwiicBench QG_MSG_ST. */
#define DESK_MSG_SEL 0x5Eu
typedef struct PD_PACKED {
    uint8_t type;
    uint8_t app;
    uint8_t _pad[2];
} desk_sel_t;
_Static_assert(sizeof(desk_sel_t) == 4, "desk_sel_t");
#ifdef _MSC_VER
#pragma pack(pop)
#endif
#endif
