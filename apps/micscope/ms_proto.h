/* Inter-CPU quiet-room cal for MicScope. Types sit above VoltPet (0x4E). */
#ifndef MS_PROTO_H
#define MS_PROTO_H
#include <stdint.h>
#include "ms_db.h"

#define MS_MSG_CMD 0x50u
#define MS_MSG_CAL 0x51u

#define MS_CMD_GET 0u

#define MS_MAGIC     0x4D534331u /* MSC1 */
#define MS_BARS      32u
#define MS_CAL_NAME  "mscope.cal"

#ifdef _MSC_VER
#pragma pack(push, 1)
#define MS_PACKED
#else
#define MS_PACKED __attribute__((packed))
#endif

typedef struct MS_PACKED {
    uint8_t  type;   /* MS_MSG_CMD */
    uint8_t  cmd;
} ms_cmd_t;
_Static_assert(sizeof(ms_cmd_t) == 2, "ms_cmd_t");

typedef struct MS_PACKED {
    uint8_t  type;   /* MS_MSG_CAL */
    uint8_t  ok;     /* 1 = valid quiet-room cal */
    uint16_t _pad;
    uint32_t magic;
    uint32_t rms;
    uint32_t mag[MS_BARS];
} ms_cal_t;
_Static_assert(sizeof(ms_cal_t) == 140, "ms_cal_t");

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif
