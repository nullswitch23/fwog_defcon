/* Inter-CPU messages for apps/ismburst. Types reuse SkyBurst's 0x46/0x47
 * so they sit above TireEar (0x44/0x45) and below HeaderKit (0x48/0x49).
 * Packed, little-endian, same rule as io_proto.h. */
#ifndef IB_PROTO_H
#define IB_PROTO_H
#include <stdint.h>

#define IB_MSG_CMD 0x46u
#define IB_MSG_ST  0x47u

#define IB_CMD_LISTEN 0u
#define IB_CMD_ARM    1u
#define IB_CMD_ABORT  2u
#define IB_CMD_CLEAR  3u
#define IB_CMD_PAGE   4u
#define IB_CMD_SAVE   5u
#define IB_CMD_REPLAY 6u
#define IB_CMD_HUNT   7u  /* auto-rearm until the ring is full or abort */
#define IB_CMD_LOAD   8u  /* next /ismburst/BURSTNNNN.BIN into the current slot */
#define IB_CMD_NAME   9u  /* T9 slot nickname → /ismburst/FAN.TXT etc. */

#define IB_MOD_ASK    0u
#define IB_MOD_2FSK   1u

#define IB_ST_IDLE    0u
#define IB_ST_ARMED   1u
#define IB_ST_CAPTURE 2u
#define IB_ST_HAVE    3u
#define IB_ST_DECODE  4u
#define IB_ST_ERROR   5u
#define IB_ST_REPLAY  6u

#define IB_GUESS_UNKNOWN    0u
#define IB_GUESS_PWM        1u
#define IB_GUESS_PPM        2u
#define IB_GUESS_MANCHESTER 3u
#define IB_GUESS_PROLOGUE   4u
#define IB_GUESS_NEXUS      5u
#define IB_GUESS_OREGON     6u
#define IB_GUESS_PT2262     7u
#define IB_GUESS_TPMS       8u
#define IB_GUESS_FIXED      9u

#define IB_STORE_NAME     "ismburst.bin"
#define IB_TX_FRAMES      4u
#define IB_TX_GAP_MS      25u

#define IB_MAX_EDGES  512u
#define IB_RING       3u
#define IB_HEX_BYTES  12u
#define IB_HIST_BINS  12u
#define IB_LABEL_LEN  20u

#define IB_TEMP_NONE  0x7FFF
#define IB_HUM_NONE   0xFFu

#ifdef _MSC_VER
#pragma pack(push, 1)
#define IB_PACKED
#else
#define IB_PACKED __attribute__((packed))
#endif

typedef struct IB_PACKED {
    uint8_t  type;      /* IB_MSG_CMD */
    uint8_t  cmd;
    uint8_t  slot;      /* IB_CMD_PAGE / SAVE / LOAD / NAME: FAN=0 DOOR=1 SPARE=2 */
    uint8_t  mod;       /* IB_MOD_ASK or IB_MOD_2FSK */
    uint32_t freq_hz;
} ib_cmd_t;
_Static_assert(sizeof(ib_cmd_t) == 8, "ib_cmd_t");

typedef struct IB_PACKED {
    uint8_t type;       /* IB_MSG_CMD */
    uint8_t cmd;        /* IB_CMD_NAME */
    uint8_t slot;
    uint8_t _pad;
    char    name[IB_LABEL_LEN];
} ib_name_t;
_Static_assert(sizeof(ib_name_t) == 24, "ib_name_t");

typedef struct IB_PACKED {
    uint8_t  type;      /* IB_MSG_ST */
    uint8_t  state;
    uint8_t  ok;
    uint8_t  guess;
    int16_t  rssi;
    int16_t  peak_rssi;
    uint16_t bursts;
    uint16_t edges;
    uint16_t duration_ms;
    uint16_t bits;
    uint32_t freq_hz;
    uint32_t cap_hz;
    uint16_t pw_short_us;
    uint16_t pw_long_us;
    uint16_t gap_short_us;
    uint16_t gap_long_us;
    int16_t  temp_c_x10;
    uint8_t  humidity;
    uint8_t  channel;
    uint8_t  hex_n;
    uint8_t  slot;
    uint8_t  slots;
    uint8_t  coding;
    uint8_t  hex[IB_HEX_BYTES];
    uint8_t  hist[IB_HIST_BINS];
    char     label[IB_LABEL_LEN]; /* decode.c protocol string */
    uint8_t  saved;     /* 1 = ismburst.bin present on main FatFs */
    uint8_t  wait_s;    /* arm remaining seconds; 0 if not waiting */
    uint8_t  hunt;      /* 1 = auto-rearm into the ring */
    uint8_t  mod;       /* IB_MOD_ASK or IB_MOD_2FSK of the listen / shot */
    char     name[IB_LABEL_LEN];  /* T9 slot nickname; empty = FAN/DOOR/SPARE */
} ib_status_t;
_Static_assert(sizeof(ib_status_t) == 108, "ib_status_t");
_Static_assert(sizeof(ib_status_t) <= 4160u, "ib_status_t fits the link");

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif
