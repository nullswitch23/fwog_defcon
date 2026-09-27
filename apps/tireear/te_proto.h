#ifndef TE_PROTO_H
#define TE_PROTO_H
#include <stdint.h>

#ifdef _MSC_VER
#pragma pack(push, 1)
#define TE_PACKED
#else
#define TE_PACKED __attribute__((packed))
#endif

#define TE_MSG_CMD 0x44u
#define TE_MSG_ST  0x45u
#define TE_MSG_ROW 0x46u

#define TE_PRESET_FREQ  1u
#define TE_PRESET_FSK   2u

#define TE_CMD_PRESET  0u
#define TE_CMD_PROFILE 1u
#define TE_CMD_SCAN    2u /* on: 1 start, 0 stop */
#define TE_CMD_CURSOR  3u /* on: 0 up, 1 down */
#define TE_CMD_MODE    4u /* on: TE_MODE_* */

#define TE_MODE_LISTEN 0u
#define TE_MODE_SCAN   1u
#define TE_MODE_REVIEW 2u

typedef struct TE_PACKED {
    uint8_t type;
    uint8_t cmd;
    uint8_t on;
    uint8_t preset; /* TE_CMD_PRESET: bit0 EU, bit1 FSK */
} te_cmd_t;
_Static_assert(sizeof(te_cmd_t) == 4, "te_cmd_t");

typedef struct TE_PACKED {
    uint8_t  type;
    uint8_t  preset;
    uint8_t  ok;
    uint8_t  mode;
    uint8_t  profile;
    uint8_t  scan_on;
    uint8_t  in_burst;
    uint8_t  crc_ok;
    int16_t  rssi;
    int16_t  last_rssi;
    uint16_t bursts;
    uint16_t last_ms;
    uint32_t freq_hz;
    uint16_t scan_left_ms;
    uint8_t  scan_n;
    uint8_t  cursor;
    int16_t  last_psi_x10;
    int16_t  last_temp_c;
    uint8_t  last_id[4];
    uint8_t  raw_n;
    uint8_t  raw_hex[9];
} te_status_t;
_Static_assert(sizeof(te_status_t) == 42, "te_status_t");

typedef struct TE_PACKED {
    uint8_t  type;
    uint8_t  idx;
    uint8_t  id[4];
    uint8_t  id_len;
    int16_t  psi_x10;
    int16_t  temp_c;
    int8_t   peak_rssi;
    uint8_t  bursts;
    uint16_t first_s;
    uint16_t last_s;
    uint8_t  profile;
    uint8_t  crc_ok;
    uint8_t  raw_n;
    uint8_t  raw[9];
} te_row_t;
_Static_assert(sizeof(te_row_t) == 29, "te_row_t");

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif
