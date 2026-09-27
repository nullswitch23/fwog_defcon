/* Display -> main. Not 0x70: DiskGlass DG_MSG_CMD. Not 0x46: TE_MSG_ROW / IB_MSG_CMD
 * share that byte — those are dispatched by live tile, never both. */
#ifndef ED_PROTO_H
#define ED_PROTO_H
#include <stdint.h>
#ifdef _MSC_VER
#pragma pack(push, 1)
#define ED_PACKED
#else
#define ED_PACKED __attribute__((packed))
#endif
#define DESK_MSG_SEL 0x5Fu
typedef struct ED_PACKED {
    uint8_t type;
    uint8_t app;
    uint8_t _pad[2];
} desk_sel_t;
_Static_assert(sizeof(desk_sel_t) == 4, "desk_sel_t");
#ifdef _MSC_VER
#pragma pack(pop)
#endif
#endif
