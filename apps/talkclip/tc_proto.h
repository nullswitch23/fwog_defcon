#ifndef TC_PROTO_H
#define TC_PROTO_H
#include <stdint.h>

#define TC_MSG_CMD 0x64u
#define TC_MSG_ACK 0x65u

#define TC_CMD_OPEN   0u
#define TC_CMD_PCM    1u
#define TC_CMD_CLOSE  2u
#define TC_CMD_RENAME 3u

#define TC_PCM_N 256u
#define TC_PATH_LEN 28u
#define TC_NAME_LEN 12u /* 8.3 stem + pad; A-Z, max 8 */

#ifdef _MSC_VER
#pragma pack(push, 1)
#define TC_PACKED
#else
#define TC_PACKED __attribute__((packed))
#endif

typedef struct TC_PACKED {
    uint8_t  type;      /* TC_MSG_CMD */
    uint8_t  cmd;
    uint16_t n;
    uint32_t seq;
    int16_t  pcm[TC_PCM_N];
} tc_pcm_t;
_Static_assert(sizeof(tc_pcm_t) == 520, "tc_pcm_t");

typedef struct TC_PACKED {
    uint8_t  type;      /* TC_MSG_ACK */
    uint8_t  ok;
    uint16_t _pad;
    char     path[TC_PATH_LEN];
} tc_ack_t;
_Static_assert(sizeof(tc_ack_t) == 32, "tc_ack_t");

/* Rename last /talkclip CLIP*.RAW to NAME.RAW (NAME is 1..8 A-Z). */
typedef struct TC_PACKED {
    uint8_t  type;      /* TC_MSG_CMD */
    uint8_t  cmd;       /* TC_CMD_RENAME */
    uint16_t _pad;
    char     from[TC_PATH_LEN];
    char     name[TC_NAME_LEN];
} tc_rename_t;
_Static_assert(sizeof(tc_rename_t) == 44, "tc_rename_t");

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif
