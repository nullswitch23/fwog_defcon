#ifndef CM_PROTO_H
#define CM_PROTO_H
#include <stdint.h>

#define CM_MSG_CMD  0x4Cu
#define CM_MSG_ST   0x4Du
#define CM_MSG_RX   0x4Eu

#define CM_CMD_LISTEN 0u
#define CM_CMD_TX     1u
#define CM_CMD_SET    2u  /* seq = slot 0..23, text = canned line */
#define CM_CMD_PULL   3u  /* display wants the 24-slot bank */

#define CM_TEXT 20u
#define CM_INBOX 4u
#define CM_NSLOT 24u
#define CM_MSG_SLOT 0x4Fu

#define CM_RF_MAGIC0 'C'
#define CM_RF_MAGIC1 'M'
#define CM_RF_LEN    24u   /* magic2 + seq + n + text20 */

#ifdef _MSC_VER
#pragma pack(push, 1)
#define CM_PACKED
#else
#define CM_PACKED __attribute__((packed))
#endif

typedef struct CM_PACKED {
    uint8_t type;
    uint8_t cmd;
    uint8_t n;
    uint8_t seq;
    char    text[CM_TEXT];
} cm_cmd_t;
_Static_assert(sizeof(cm_cmd_t) == 24, "cm_cmd_t");

typedef struct CM_PACKED {
    uint8_t  type;
    uint8_t  ok;
    uint8_t  tx_ok;
    uint8_t  nrx;
    int16_t  rssi;
    uint8_t  seq;
    uint8_t  _pad;
} cm_st_t;
_Static_assert(sizeof(cm_st_t) == 8, "cm_st_t");

typedef struct CM_PACKED {
    uint8_t type;
    uint8_t n;
    uint8_t seq;
    int8_t  rssi;
    char    text[CM_TEXT];
} cm_rx_t;
_Static_assert(sizeof(cm_rx_t) == 24, "cm_rx_t");

typedef struct CM_PACKED {
    uint8_t type;
    uint8_t slot;
    uint8_t n;
    uint8_t _pad;
    char    text[CM_TEXT];
} cm_slot_t;
_Static_assert(sizeof(cm_slot_t) == 24, "cm_slot_t");

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif
