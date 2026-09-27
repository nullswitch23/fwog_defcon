#ifndef AM_PROTO_H
#define AM_PROTO_H
#include <stdint.h>

#ifdef _MSC_VER
#pragma pack(push, 1)
#define AM_PACKED
#else
#define AM_PACKED __attribute__((packed))
#endif

#define AM_MSG_CMD 0x64u
#define AM_MSG_ST  0x65u

#define AM_CMD_START   1u  /* on: 1 WIFIPROOF ON, 0 OFF */
#define AM_CMD_ARM     2u  /* on: 1 fire one frame (gated) */
#define AM_CMD_CURSOR  3u  /* on: 0 up, 1 down */
#define AM_CMD_CHANNEL 4u  /* on: channel 1-14, or 0 cycle */
#define AM_CMD_MODE    5u  /* on: 0 DEAUTH, 1 DASSOC */
#define AM_CMD_FLASH   6u  /* on: 0 cancel, 1 hold, 2 write */
#define AM_CMD_SWEEP   7u  /* on: 1 hop 1–14 collecting APs, 0 stop hop */

#define AM_MAX      24u
#define AM_ROWS     5u
#define AM_BSSID_N  18u
#define AM_SSID_N   16u
#define AM_LOG_N    40u
#define AM_RET_N    32u

#define AM_MODE_DEAUTH  0u
#define AM_MODE_DASSOC  1u

#define AM_FLASH_IDLE  0u
#define AM_FLASH_HOLD  1u
#define AM_FLASH_SYNC  2u
#define AM_FLASH_WRITE 3u
#define AM_FLASH_OK    4u
#define AM_FLASH_FAIL  5u

typedef struct AM_PACKED {
    uint8_t type;
    uint8_t cmd;
    uint8_t on;
    uint8_t _pad;
} am_cmd_t;
_Static_assert(sizeof(am_cmd_t) == 4, "am_cmd_t");

typedef struct AM_PACKED {
    char    bssid[AM_BSSID_N];
    char    ssid[AM_SSID_N];
    uint8_t ch;
} am_target_t;
_Static_assert(sizeof(am_target_t) == 35, "am_target_t");

typedef struct AM_PACKED {
    uint8_t  type;
    uint8_t  hello;
    uint8_t  wp_on;
    uint8_t  armed;
    uint8_t  channel;
    uint8_t  n;
    uint8_t  cursor;
    uint8_t  mode;
    uint8_t  flash;
    uint8_t  pct;
    uint8_t  sweep;
    uint32_t tx_count;
    char     last_ret[AM_RET_N];
    char     log[AM_LOG_N];
    char     why[16];
    am_target_t tgt[AM_MAX];
} am_status_t;
_Static_assert(sizeof(am_status_t) == 943, "am_status_t");

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif
