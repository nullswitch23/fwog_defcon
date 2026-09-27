#ifndef TB_PROTO_H
#define TB_PROTO_H
#include <stdint.h>

#ifdef _MSC_VER
#pragma pack(push, 1)
#define TB_PACKED
#else
#define TB_PACKED __attribute__((packed))
#endif

#define TB_MSG_HANGUP 0x54u
#define TB_MSG_HANGUP_RSP 0x55u

/* Display -> main: request relay hang-up (tuts serial byte 'H'). */
typedef struct TB_PACKED {
    uint8_t type;
    uint8_t cmd; /* always 'H' */
    uint16_t timeout_ms;
} tb_hangup_req_t;
_Static_assert(sizeof(tb_hangup_req_t) == 4, "tb_hangup_req_t");

/* Main -> display: 1 ok, 0 fail, 0xFF timeout/error. */
typedef struct TB_PACKED {
    uint8_t type;
    uint8_t result;
} tb_hangup_rsp_t;
_Static_assert(sizeof(tb_hangup_rsp_t) == 2, "tb_hangup_rsp_t");

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif
