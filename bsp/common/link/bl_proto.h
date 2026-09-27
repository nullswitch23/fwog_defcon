/* The display bootloader's wire protocol, carried inside link_frame frames.
 *
 * Payload byte 0 is the message type (the hardware record). 0x00 is invalid, the
 * bootloader owns 0x01-0x1F, and application protocols start at 0x20. The
 * types and struct layouts below are FROZEN: a bootloader in the field
 * speaks them, and it is the one component that needs physical BOOTSEL
 * access to replace.
 *
 * Structs go on the wire as-is. Both ends are little-endian Cortex-M0+ and
 * every member is naturally aligned with explicit padding, so there is no
 * serialization step and no endianness conversion. The _Static_asserts are
 * what make that safe rather than merely true today.
 *
 * Callers must memcpy into a local struct rather than casting a pointer
 * into a receive buffer: fwog_link_rx_t.buf has no alignment guarantee, and
 * a cast would be an unaligned uint32_t load.
 *
 * Pure -- no SDK, no hardware. */
#ifndef FWOG_BL_PROTO_H
#define FWOG_BL_PROTO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "common/app_meta.h"
#include "common/link/link_frame.h"

#define FWOG_BL_PROTO_VER    1u
#define FWOG_BL_CHUNK        4096u   /* one flash erase sector per DATA */
#define FWOG_BL_VERSION_LEN  FWOG_APP_VERSION_LEN
#define FWOG_BL_BLVER_LEN    16u
#define FWOG_BL_STATUS_TEXT_LEN 28u  /* incl. NUL; keeps fwog_bl_status_t at 32 */

/* `percent` value meaning "text only, no progress bar". Chosen outside 0-100
 * so the bar and the text never need two separate messages. */
#define FWOG_BL_STATUS_NO_BAR 0xFFu

typedef enum {
    FWOG_BL_MSG_HELLO        = 0x01,  /* display -> main */
    FWOG_BL_MSG_RUN          = 0x02,  /* main -> display */
    FWOG_BL_MSG_UPDATE_BEGIN = 0x03,  /* main -> display */
    FWOG_BL_MSG_READY        = 0x04,  /* display -> main, erase complete */
    FWOG_BL_MSG_DATA         = 0x05,  /* main -> display */
    FWOG_BL_MSG_ACK          = 0x06,  /* display -> main */
    FWOG_BL_MSG_NAK          = 0x07,  /* display -> main, resume here */
    FWOG_BL_MSG_UPDATE_END   = 0x08,  /* main -> display */
    FWOG_BL_MSG_RESULT       = 0x09,  /* display -> main */
    FWOG_BL_MSG_STATUS       = 0x0A   /* main -> display, a line for the screen */
    /* 0x0B-0x1F reserved for future bootloader messages. */
} fwog_bl_msg_t;

/* Everything the display knows about its own app slot, in one message.
 * build_ts is not in the design doc's field list; it is here because the
 * console's `info` needs it and main should log it, and protocol version 1
 * has not shipped, so adding it is free exactly once. */
typedef struct {
    uint8_t  type;                          /*  0 */
    uint8_t  proto_ver;                     /*  1 */
    uint8_t  app_valid;                     /*  2 */
    uint8_t  reserved;                      /*  3 */
    uint32_t app_crc32;                     /*  4 */
    uint32_t app_size;                      /*  8 */
    uint32_t build_ts;                      /* 12 */
    char     version[FWOG_BL_VERSION_LEN];  /* 16 */
    char     bl_version[FWOG_BL_BLVER_LEN]; /* 48 */
} fwog_bl_hello_t;                          /* 64 */

typedef struct {
    uint8_t  type;                          /*  0 */
    uint8_t  reserved[3];                   /*  1 */
    uint32_t size;                          /*  4 */
    uint32_t crc32;                         /*  8 */
    uint32_t build_ts;                      /* 12 */
    char     version[FWOG_BL_VERSION_LEN];  /* 16 */
} fwog_bl_update_begin_t;                   /* 48 */

/* `len` is redundant with the frame length. It is carried anyway because
 * the design doc specifies it, and fwog_bl_msg_type() cross-checks the two
 * so the redundancy can never become a second source of truth. */
typedef struct {
    uint8_t  type;          /*  0 */
    uint8_t  reserved[3];   /*  1 */
    uint32_t offset;        /*  4  image-relative, not flash-relative */
    uint32_t len;           /*  8 */
    /* payload bytes follow */
} fwog_bl_data_hdr_t;       /* 12 */

/* ACK and NAK share a layout. ACK(offset) confirms that chunk; NAK(offset)
 * says "I am missing everything from here" and is main's resume point. */
typedef struct {
    uint8_t  type;
    uint8_t  reserved[3];
    uint32_t offset;
} fwog_bl_ack_t;            /* 8 */

typedef struct {
    uint8_t  type;
    uint8_t  ok;
    uint8_t  reserved[2];
    uint32_t computed_crc32;
} fwog_bl_result_t;         /* 8 */

/* A line of text for the bootloader's screen, pushed by main.
 *
 * The display cannot know which of several steps main is on, what it is
 * waiting for, or why it has not been released yet -- so main says it
 * directly rather than the bootloader guessing from protocol traffic.
 *
 * `percent` is 0-100, or FWOG_BL_STATUS_NO_BAR for a text-only line. Any
 * other value is rejected by fwog_bl_msg_type(), so a renderer that has been
 * handed a validated message may index a bar array with it directly. */
typedef struct {
    uint8_t  type;                          /*  0 */
    uint8_t  percent;                       /*  1 */
    uint8_t  reserved[2];                   /*  2 */
    char     text[FWOG_BL_STATUS_TEXT_LEN]; /*  4 */
} fwog_bl_status_t;                         /* 32 */

_Static_assert(sizeof(fwog_bl_hello_t) == 64, "HELLO layout is frozen");
_Static_assert(sizeof(fwog_bl_update_begin_t) == 48, "UPDATE_BEGIN layout is frozen");
_Static_assert(sizeof(fwog_bl_data_hdr_t) == 12, "DATA header layout is frozen");
_Static_assert(sizeof(fwog_bl_ack_t) == 8, "ACK/NAK layout is frozen");
_Static_assert(sizeof(fwog_bl_result_t) == 8, "RESULT layout is frozen");
_Static_assert(sizeof(fwog_bl_status_t) == 32, "STATUS layout is frozen");
_Static_assert(sizeof(fwog_bl_data_hdr_t) + FWOG_BL_CHUNK <= FWOG_LINK_MAX_PAYLOAD,
               "a full DATA chunk must fit one link frame");

/* Returns the message type of a decoded payload, or 0 if it is not a
 * well-formed bootloader message: unknown or out-of-range type, wrong
 * length for its type, a DATA header whose len disagrees with the frame, or
 * an unterminated string field. 0 is safe as the failure value because type
 * 0x00 is reserved as invalid.
 *
 * Validating length and string termination HERE means no caller has to.
 * Everything downstream may assume the message is the size it claims and
 * that its char arrays are printable. */
uint8_t fwog_bl_msg_type(const void *payload, size_t len);

/* Write a one-byte payload (RUN, READY, UPDATE_END). Returns 1.
 * `out` needs 1 byte. */
size_t fwog_bl_encode_bare(uint8_t *out, uint8_t type);

/* Write an ACK or NAK payload. Returns sizeof(fwog_bl_ack_t).
 * `out` needs 8 bytes. */
size_t fwog_bl_encode_ack(uint8_t *out, uint8_t type, uint32_t offset);

/* Write a STATUS payload. Returns sizeof(fwog_bl_status_t).
 * `out` needs 32 bytes.
 *
 * `text` is truncated to FWOG_BL_STATUS_TEXT_LEN-1 characters and always
 * NUL-terminated; NULL is treated as "". `percent` above 100 that is not
 * FWOG_BL_STATUS_NO_BAR is clamped to NO_BAR rather than to 100, because a
 * caller that computed a nonsense percentage is a caller whose bar would be
 * a lie -- dropping the bar is the honest failure. */
size_t fwog_bl_encode_status(uint8_t *out, const char *text, uint8_t percent);

#endif
