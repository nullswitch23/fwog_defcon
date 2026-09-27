#ifndef IT_PROTO_H
#define IT_PROTO_H
#include <stdint.h>

#ifdef _MSC_VER
#pragma pack(push, 1)
#define IT_PACKED
#else
#define IT_PACKED __attribute__((packed))
#endif

#define IT_MSG_LOG 0x61u

#define IT_LOG_START 0u
#define IT_LOG_ROW   1u
#define IT_LOG_STOP  2u

typedef struct IT_PACKED {
    uint8_t  type;     /* IT_MSG_LOG */
    uint8_t  cmd;      /* START / ROW / STOP */
    uint8_t  mode;     /* 0 pedo, 1 map */
    uint8_t  laps;
    uint32_t ms;
    uint32_t steps;
    uint32_t dist_cm;
    int16_t  mag;
    int16_t  x;
    int16_t  y;
    char     title[8]; /* 8.3 stem; empty → WALKNNNN.CSV */
    uint16_t _pad;
} it_log_t;
_Static_assert(sizeof(it_log_t) == 32, "it_log_t");

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif
