/* Inter-CPU messages for apps/fobreplay. Types start at 0x30 so they sit
 * above the bootloader (0x01-0x1F) and the breakout I/O protocol (0x20-0x22).
 * Packed, little-endian, same rule as io_proto.h. */
#ifndef FOB_PROTO_H
#define FOB_PROTO_H
#include <stdint.h>

#ifdef _MSC_VER
#pragma pack(push, 1)
#define FOB_PACKED
#else
#define FOB_PACKED __attribute__((packed))
#endif

#define FOB_MSG_CMD    0x30u   /* display -> main */
#define FOB_MSG_STATUS 0x31u   /* main -> display */

#define FOB_CMD_LISTEN       0u
#define FOB_CMD_CAPTURE      1u
#define FOB_CMD_REPLAY       2u   /* single: last slot. queue: oldest unused. */
#define FOB_CMD_ABORT        3u
#define FOB_CMD_BURST        4u   /* queue: play every unused slot, oldest first */
#define FOB_CMD_CLEAR        5u
#define FOB_CMD_PREDICT_TX   6u   /* predict: synthesize + transmit next code */
#define FOB_CMD_PREDICT_BURST 7u  /* predict: transmit predicted code N times */
#define FOB_CMD_SAVE          8u  /* last capture -> /fobreplay/STEM.BIN (8.3) */

#define FOB_STEM_N 9u             /* 8.3 base + NUL */

#define FOB_MODE_SINGLE 0u
#define FOB_MODE_QUEUE  1u
#define FOB_MODE_PREDICT 2u

#define FOB_PRED_CTR 0u
#define FOB_PRED_KL  1u

#define FOB_ST_BOOT     0u
#define FOB_ST_LISTEN   1u
#define FOB_ST_WAIT     2u
#define FOB_ST_CAPTURE  3u
#define FOB_ST_HAVE     4u
#define FOB_ST_REPLAY   5u
#define FOB_ST_ERROR    6u
#define FOB_ST_BURST    7u
#define FOB_ST_PREDICT  8u

#define FOB_ENV_BINS    40u
#define FOB_SLOTS       8u
#define FOB_PRED_BURST_N 3u

typedef struct FOB_PACKED {
    uint8_t  type;      /* FOB_MSG_CMD */
    uint8_t  cmd;
    uint8_t  radio;     /* 0 = CS0, 1 = CS1 */
    uint8_t  mode;      /* FOB_MODE_* */
    uint32_t freq_hz;
    uint8_t  predictor; /* FOB_PRED_CTR or FOB_PRED_KL */
    char     name[FOB_STEM_N]; /* SAVE: A-Z0-9 stem, empty = ignore */
} fob_cmd_t;
_Static_assert(sizeof(fob_cmd_t) == 18, "fob_cmd_t wire size");

typedef struct FOB_PACKED {
    uint8_t  type;      /* FOB_MSG_STATUS */
    uint8_t  state;
    uint8_t  radio;
    uint8_t  ok;
    int16_t  rssi_dbm;
    uint16_t edges;
    uint32_t freq_hz;
    uint8_t  mode;
    uint8_t  slots;         /* occupied / predict captures */
    uint8_t  unused;        /* queue: not yet replayed */
    uint8_t  next;          /* queue: slot REPLAY plays, 0xFF if none */
    uint32_t last_total_us;
    uint8_t  env[FOB_ENV_BINS];
    uint8_t  predictor;     /* active predictor sub-mode */
    uint8_t  pred_key_ok;   /* 1 = KeeLoq key verified */
    uint16_t pred_counter;
    uint32_t pred_frame;
} fob_status_t;
_Static_assert(sizeof(fob_status_t) == 68, "fob_status_t wire size");

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif
