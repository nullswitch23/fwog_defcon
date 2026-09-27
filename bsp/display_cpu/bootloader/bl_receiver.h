/* The display bootloader's update receiver: everything between
 * UPDATE_BEGIN and RESULT, minus transport and minus flash hardware.
 *
 * Flash reaches this file only through fwog_bl_flash_ops_t, which is what
 * makes the whole update path host-testable against a RAM fake. Nothing
 * here includes the SDK -- deliberately: this is the code whose failure
 * bricks a board, and it is the code that must be exercised without one.
 *
 * The crash-safety property lives here and is worth stating plainly: the
 * metadata sector is erased on UPDATE_BEGIN, before a single image byte is
 * written, and rewritten only after the whole image verifies. Any
 * interruption in between leaves it erased, so the bootloader refuses the
 * app and main reflashes on the next boot. There is no journal and no
 * resume state in flash, because there does not need to be. */
#ifndef FWOG_BL_RECEIVER_H
#define FWOG_BL_RECEIVER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "common/app_meta.h"
#include "common/link/bl_proto.h"

/* All offsets are FLASH offsets (0-based at 0x10000000), not image
 * offsets -- the receiver does the translation. */
typedef struct {
    /* Erase [off, off+len). Both are multiples of 4096. */
    bool (*erase)(void *ctx, uint32_t off, uint32_t len);
    /* Program len bytes from src. off is a multiple of 256, len is a
     * multiple of 256, and src is guaranteed to be in RAM -- the bootrom
     * reads the source with XIP disabled, so a flash pointer would fault. */
    bool (*program)(void *ctx, uint32_t off, const uint8_t *src, uint32_t len);
    /* Read len bytes into dst. Used only for the verify pass.
     *
     * Returns false if the range is not readable, leaving dst UNSPECIFIED --
     * the caller must abort rather than use it. This returns bool rather
     * than void so a rejected read fails the update explicitly. The earlier
     * void form could only zero-fill and carry on, which failed closed only
     * as a side effect of the caller feeding the result to CRC32. */
    bool (*read)(void *ctx, uint32_t off, uint8_t *dst, uint32_t len);
    void *ctx;
} fwog_bl_flash_ops_t;

typedef enum {
    FWOG_BL_RX_IDLE = 0,
    FWOG_BL_RX_RECEIVING,
    FWOG_BL_RX_DONE_OK,
    FWOG_BL_RX_DONE_FAIL
} fwog_bl_rx_state_t;

typedef struct {
    const fwog_bl_flash_ops_t *ops;
    fwog_bl_rx_state_t state;
    uint32_t img_size;
    uint32_t img_crc32;      /* as announced by main */
    uint32_t build_ts;
    char     version[FWOG_APP_VERSION_LEN];
    uint32_t next_offset;    /* image offset expected in the next DATA */
    uint32_t computed_crc32; /* filled by the verify pass */
    /* Scratch for the sub-page tail of the final chunk and for the
     * metadata record, both of which must be programmed as a full 256-byte
     * page. Lives in the struct rather than on the stack because the
     * bootloader's stack is the SDK default 2 KB. */
    uint8_t  page[256];
} fwog_bl_update_t;

/* A reply payload to be framed and sent. len == 0 means "send nothing".
 * 64 bytes is sizeof(fwog_bl_hello_t), the largest thing this module or its
 * caller ever replies with. */
typedef struct {
    uint8_t buf[64];
    size_t  len;
} fwog_bl_reply_t;

void fwog_bl_update_init(fwog_bl_update_t *u, const fwog_bl_flash_ops_t *ops);

/* Feed one decoded link-frame payload. Returns false when the message is
 * malformed, is not one of UPDATE_BEGIN / DATA / UPDATE_END, or is not
 * legal in the current state; reply->len is 0 in that case and the caller
 * should ignore it rather than sending an error. (Silence is correct: main
 * retransmits on its own timeout, and a bootloader that answered every
 * malformed byte would amplify a noisy link.)
 *
 * Returns true when the message was handled. reply->len is then the payload
 * to send, which is always non-zero for a handled message. */
bool fwog_bl_update_on_msg(fwog_bl_update_t *u, const uint8_t *payload,
                           size_t len, fwog_bl_reply_t *reply);

/* 0..100, for the progress display. 0 before UPDATE_BEGIN. */
uint8_t fwog_bl_update_percent(const fwog_bl_update_t *u);

#endif
