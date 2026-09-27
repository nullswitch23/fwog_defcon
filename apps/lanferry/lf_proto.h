#ifndef LF_PROTO_H
#define LF_PROTO_H
#include <stdint.h>

#ifdef _MSC_VER
#pragma pack(push, 1)
#define LF_PACKED
#else
#define LF_PACKED __attribute__((packed))
#endif

#define LF_MSG_CMD 0x60u
#define LF_MSG_ST  0x61u
#define LF_CMD_NOP   0u
#define LF_CMD_AP    1u
#define LF_CMD_FLASH 2u   /* on: 0 cancel, 1 hold-BOOT, 2 write */
#define LF_CMD_WIPE  3u   /* new WPA2 pass + empty RAM pool */

#define LF_FLASH_IDLE  0u
#define LF_FLASH_HOLD  1u
#define LF_FLASH_SYNC  2u
#define LF_FLASH_WRITE 3u
#define LF_FLASH_OK    4u
#define LF_FLASH_FAIL  5u

#define LF_SSID_N 16u
#define LF_PASS_N 12u
#define LF_FILE_N 16u
#define LF_POOL_KB 124u

typedef struct LF_PACKED {
    uint8_t type;
    uint8_t cmd;
    uint8_t on;
    uint8_t _pad;
} lf_cmd_t;
_Static_assert(sizeof(lf_cmd_t) == 4, "lf_cmd_t");

typedef struct LF_PACKED {
    uint8_t  type;
    uint8_t  hello;
    uint8_t  ap_on;
    uint8_t  clients;
    uint8_t  flash;
    uint8_t  pct;
    uint8_t  nfiles;
    uint8_t  pool_4k;  /* unused on the wire; panel uses LF_POOL_KB */
    uint32_t bytes;
    char     ssid[LF_SSID_N];
    char     pass[LF_PASS_N];
    char     file[LF_FILE_N];
} lf_status_t;
_Static_assert(sizeof(lf_status_t) == 56, "lf_status_t");

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif
