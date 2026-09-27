#ifndef KIT_PROTO_H
#define KIT_PROTO_H
#include <stdint.h>

#ifdef _MSC_VER
#pragma pack(push, 1)
#define KIT_PACKED
#else
#define KIT_PACKED __attribute__((packed))
#endif

/* Display -> main: which landing tile is live. Main idles the CC1101
 * unless the selection is BandScope. */
#define KIT_MSG_SEL 0x60u

#define KIT_APP_HOME      0u
#define KIT_APP_HOSTDECK  1u
#define KIT_APP_MICSCOPE  2u
#define KIT_APP_BANDSCOPE 3u
#define KIT_APP_GLASSBAK  4u
#define KIT_APP_DISKGLASS 5u
#define KIT_APP_TALKCLIP  6u
#define KIT_APP_TONEBOX   7u
#define KIT_APP_RIGGLASS  8u

typedef struct KIT_PACKED {
    uint8_t type;
    uint8_t app;
    uint8_t _pad[2];
} kit_sel_t;
_Static_assert(sizeof(kit_sel_t) == 4, "kit_sel_t");

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif
