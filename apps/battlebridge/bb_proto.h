#ifndef BB_PROTO_H
#define BB_PROTO_H
#include <stdint.h>

#ifdef _MSC_VER
#pragma pack(push, 1)
#define BB_PACKED
#else
#define BB_PACKED __attribute__((packed))
#endif

#define BB_MSG_CMD 0x7Au
#define BB_MSG_ST  0x7Bu

#define BB_CMD_GAME  1u
#define BB_CMD_FLASH 2u
#define BB_CMD_GO    3u
#define BB_CMD_BOTS  4u
#define BB_CMD_TEAMS 5u
#define BB_CMD_WIPE  6u

#define BB_FLASH_IDLE  0u
#define BB_FLASH_HOLD  1u
#define BB_FLASH_SYNC  2u
#define BB_FLASH_WRITE 3u
#define BB_FLASH_OK    4u
#define BB_FLASH_FAIL  5u

typedef struct BB_PACKED {
    uint8_t type;
    uint8_t cmd;
    uint8_t on;
    uint8_t _pad;
} bb_cmd_t;
_Static_assert(sizeof(bb_cmd_t) == 4, "bb_cmd_t");

typedef struct BB_PACKED {
    uint8_t type;
    uint8_t hello;
    uint8_t game_on;
    uint8_t players;
    uint8_t phase;
    uint8_t flash;
    uint8_t pct;
    uint8_t step_100us;
    uint16_t tick;
    uint16_t heap_k;
    char ssid[16];
    char pass[12];
    char why[13];
    uint8_t bots;
    uint8_t clock_s;
    uint8_t sudden;
    uint8_t teams;
} bb_status_t;
_Static_assert(sizeof(bb_status_t) == 57, "bb_status_t");

#ifdef _MSC_VER
#pragma pack(pop)
#endif
#endif
