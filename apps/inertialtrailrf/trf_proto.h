#ifndef TRF_PROTO_H
#define TRF_PROTO_H
#include <stdint.h>

#ifdef _MSC_VER
#pragma pack(push, 1)
#define TRF_PACKED
#else
#define TRF_PACKED __attribute__((packed))
#endif

#define TRF_MSG_CMD 0x50u
#define TRF_MSG_ST  0x51u
#define TRF_MSG_LOG 0x52u
#define TRF_HZ      433920000u
#define TRF_HZ_TOP  315000000u

#define TRF_LOG_START 0u
#define TRF_LOG_ROW   1u
#define TRF_LOG_STOP  2u
#define TRF_LOG_MARK  3u

typedef struct TRF_PACKED {
    uint8_t  type;
    uint8_t  radio;    /* 0 top/radio0, 1 bottom/radio1 */
    uint8_t  _pad[2];
    uint32_t freq_hz;
} trf_cmd_t;
_Static_assert(sizeof(trf_cmd_t) == 8, "trf_cmd_t");

typedef struct TRF_PACKED {
    uint8_t  type;
    uint8_t  ok;       /* bit0 radio0, bit1 radio1 */
    int16_t  rssi0;
    int16_t  rssi1;
    uint16_t _pad;
    uint32_t freq0_hz;
    uint32_t freq1_hz;
} trf_status_t;
_Static_assert(sizeof(trf_status_t) == 16, "trf_status_t");

typedef struct TRF_PACKED {
    uint8_t  type;
    uint8_t  cmd;
    int16_t  rssi0;
    int16_t  rssi1;
    uint16_t _pad;
    uint32_t step;
    uint32_t freq0_hz;
    uint32_t freq1_hz;
    char     title[8]; /* 8.3 stem; empty → TRAILNNNN.CSV */
} trf_log_t;
_Static_assert(sizeof(trf_log_t) == 28, "trf_log_t");

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif
