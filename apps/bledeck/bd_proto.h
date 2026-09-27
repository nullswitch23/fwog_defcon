#ifndef BD_PROTO_H
#define BD_PROTO_H
#include <stdint.h>

#ifdef _MSC_VER
#pragma pack(push, 1)
#define BD_PACKED
#else
#define BD_PACKED __attribute__((packed))
#endif

#define BD_MSG_CMD 0x64u
#define BD_MSG_ST  0x65u
#define BD_CMD_NOP   0u
#define BD_CMD_ADV   1u  /* on: advertise HID */
#define BD_CMD_FLASH 2u  /* on: 0 cancel, 1 hold, 2 write */
#define BD_CMD_KEY   3u  /* keyboard; usage = HID keycode */
#define BD_CMD_CC    4u  /* consumer; usage = HID usage */
#define BD_CMD_BAT   5u  /* on: pack percent 0-100 for HID Battery Service */
#define BD_CMD_FORGET 6u /* wipe C6 bonds and open pairing */

#define BD_FLASH_IDLE  0u
#define BD_FLASH_HOLD  1u
#define BD_FLASH_SYNC  2u
#define BD_FLASH_WRITE 3u
#define BD_FLASH_OK    4u
#define BD_FLASH_FAIL  5u

typedef struct BD_PACKED {
    uint8_t  type;
    uint8_t  cmd;
    uint8_t  on;
    uint8_t  _pad;
    uint16_t usage;
    uint16_t _pad2;
} bd_cmd_t;
_Static_assert(sizeof(bd_cmd_t) == 8, "bd_cmd_t");

typedef struct BD_PACKED {
    uint8_t type;
    uint8_t hello;
    uint8_t adv;
    uint8_t conn;
    uint8_t flash;
    uint8_t pct;
    uint8_t bat;
    uint8_t lock;  /* 1 = bonded, advertise only to that peer */
    char    why[16];
} bd_status_t;
_Static_assert(sizeof(bd_status_t) == 24, "bd_status_t");

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif
