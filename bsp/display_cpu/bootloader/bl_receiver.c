#include "bootloader/bl_receiver.h"
#include "common/crc.h"
#include <string.h>

#define PAGE 256u
#define SECT 4096u

static void no_reply(fwog_bl_reply_t *r) { r->len = 0u; }

static void reply_bare(fwog_bl_reply_t *r, uint8_t type) {
    r->len = fwog_bl_encode_bare(r->buf, type);
}

static void reply_ack(fwog_bl_reply_t *r, uint8_t type, uint32_t offset) {
    r->len = fwog_bl_encode_ack(r->buf, type, offset);
}

static void reply_result(fwog_bl_reply_t *r, bool ok, uint32_t crc) {
    fwog_bl_result_t res;
    memset(&res, 0, sizeof res);
    res.type = FWOG_BL_MSG_RESULT;
    res.ok = ok ? 1u : 0u;
    res.computed_crc32 = crc;
    memcpy(r->buf, &res, sizeof res);
    r->len = sizeof res;
}

void fwog_bl_update_init(fwog_bl_update_t *u, const fwog_bl_flash_ops_t *ops) {
    memset(u, 0, sizeof *u);
    u->ops = ops;
    u->state = FWOG_BL_RX_IDLE;
}

uint8_t fwog_bl_update_percent(const fwog_bl_update_t *u) {
    if (u->img_size == 0u) return 0u;
    /* 64-bit so a 15.9 MB image cannot overflow the multiply. */
    return (uint8_t)(((uint64_t)u->next_offset * 100u) / u->img_size);
}

static bool on_begin(fwog_bl_update_t *u, const uint8_t *p,
                     fwog_bl_reply_t *reply) {
    fwog_bl_update_begin_t ub;
    memcpy(&ub, p, sizeof ub);   /* p may be unaligned */

    if (ub.size == 0u || ub.size > FWOG_APP_MAX_SIZE) return false;

    /* Erase the metadata FIRST. From this instant until the image verifies,
       the app slot is marked invalid, so any interruption self-heals. */
    if (!u->ops->erase(u->ops->ctx, FWOG_APP_META_OFFSET, FWOG_APP_META_SIZE)) {
        /* A failed erase after we already had a session (a restart) must not
           leave the previous session's size/CRC/offset in place: the
           metadata sector may now be erased while the app region is
           untouched or only partly erased, so those bounds no longer
           describe what is on the flash. Leaving state RECEIVING makes the
           next DATA at offset 0 look like a RETRANSMIT against the old
           next_offset, so the bootloader re-ACKs its way through an entire
           transfer without writing anything and only fails at the end. */
        u->state = FWOG_BL_RX_DONE_FAIL;
        return false;
    }

    /* Erase only the sectors this image needs. The app slot is 15.9 MB;
       erasing all of it would take minutes for a 500 KB image. */
    uint32_t span = (ub.size + (SECT - 1u)) & ~(SECT - 1u);
    if (!u->ops->erase(u->ops->ctx, FWOG_APP_OFFSET, span)) {
        /* Same reasoning: the metadata erase above already succeeded, so
           the app slot is invalid regardless. Do not leave a stale
           RECEIVING session pointed at bounds that no longer match what is
           actually erased. */
        u->state = FWOG_BL_RX_DONE_FAIL;
        return false;
    }

    u->img_size    = ub.size;
    u->img_crc32   = ub.crc32;
    u->build_ts    = ub.build_ts;
    memcpy(u->version, ub.version, sizeof u->version);
    u->next_offset = 0u;
    u->computed_crc32 = 0u;
    u->state = FWOG_BL_RX_RECEIVING;

    reply_bare(reply, FWOG_BL_MSG_READY);
    return true;
}

static bool on_data(fwog_bl_update_t *u, const uint8_t *p,
                    fwog_bl_reply_t *reply) {
    if (u->state != FWOG_BL_RX_RECEIVING) return false;

    fwog_bl_data_hdr_t h;
    memcpy(&h, p, sizeof h);
    const uint8_t *src = p + sizeof h;

    /* fwog_bl_msg_type() already checked len against the frame and the
       chunk ceiling; what it cannot know is the announced image size. */
    if ((uint64_t)h.offset + h.len > u->img_size) return false;

    /* Only the chunk that completes the image may end mid-page. Any other
       chunk with a sub-page length would desync every later program()
       call's page alignment -- the vtable's contract (bl_receiver.h)
       promises program() is always called page-aligned, and this is the
       one place that promise could otherwise be broken by a wire-legal
       message: fwog_bl_msg_type() allows any len in [1, FWOG_BL_CHUNK].
       Without this the desync shows up one chunk LATER, as a program()
       rejected for misalignment, after the tail write has already burned a
       page that NOR cannot rewrite. */
    const bool is_final_chunk = ((uint64_t)h.offset + h.len == u->img_size);
    if ((h.len % PAGE) != 0u && !is_final_chunk) return false;

    if (h.offset > u->next_offset) {
        /* A gap. Tell main exactly where to resume rather than making it
           guess from a bare NAK. */
        reply_ack(reply, FWOG_BL_MSG_NAK, u->next_offset);
        return true;
    }
    if (h.offset < u->next_offset) {
        /* A retransmit of something already written: our ACK was lost.
           Re-ACK without reprogramming -- NOR flash cannot rewrite a
           programmed page, so a second program would corrupt it. */
        reply_ack(reply, FWOG_BL_MSG_ACK, h.offset);
        return true;
    }

    uint32_t dst = FWOG_APP_OFFSET + h.offset;
    uint32_t whole = h.len & ~(PAGE - 1u);
    if (whole != 0u) {
        if (!u->ops->program(u->ops->ctx, dst, src, whole)) goto fail;
    }
    uint32_t tail = h.len - whole;
    if (tail != 0u) {
        /* The final chunk rarely ends on a page boundary. Pad with 0xFF --
           the erased value -- so the padding is indistinguishable from
           untouched flash, never stale bytes from the receive buffer. */
        memset(u->page, 0xFF, PAGE);
        memcpy(u->page, src + whole, tail);
        if (!u->ops->program(u->ops->ctx, dst + whole, u->page, PAGE)) goto fail;
    }

    u->next_offset += h.len;
    reply_ack(reply, FWOG_BL_MSG_ACK, h.offset);
    return true;

fail:
    u->state = FWOG_BL_RX_DONE_FAIL;
    reply_result(reply, false, 0u);
    return true;
}

/* CRC32 of the whole written image, into *out. Returns false if any read
 * was rejected, in which case *out is not written and the caller must fail
 * the update -- an unreadable page means we cannot know what is in the slot,
 * which is not the same as knowing it is wrong. */
static bool verify(fwog_bl_update_t *u, uint32_t *out) {
    uint8_t buf[PAGE];
    uint32_t crc = FWOG_CRC32_INIT;
    for (uint32_t done = 0; done < u->img_size; ) {
        uint32_t n = u->img_size - done;
        if (n > PAGE) n = PAGE;
        if (!u->ops->read(u->ops->ctx, FWOG_APP_OFFSET + done, buf, n)) {
            return false;
        }
        crc = fwog_crc32_update(crc, buf, n);
        done += n;
    }
    *out = fwog_crc32_final(crc);
    return true;
}

static bool on_end(fwog_bl_update_t *u, fwog_bl_reply_t *reply) {
    if (u->state != FWOG_BL_RX_RECEIVING) return false;

    if (u->next_offset != u->img_size) {
        /* Main declared the transfer finished while we are short. Fail
           loudly rather than verifying a partially-erased tail. */
        u->state = FWOG_BL_RX_DONE_FAIL;
        reply_result(reply, false, 0u);
        return true;
    }

    if (!verify(u, &u->computed_crc32)) {
        /* A read was rejected, so there is no CRC to report. verify() did
           not write computed_crc32, so clear it rather than leaving a stale
           value from an earlier attempt -- main DIAGs this field, and a
           mismatch value that was never computed is worse than a zero. */
        u->computed_crc32 = 0u;
        u->state = FWOG_BL_RX_DONE_FAIL;
        reply_result(reply, false, 0u);
        return true;
    }
    if (u->computed_crc32 != u->img_crc32) {
        u->state = FWOG_BL_RX_DONE_FAIL;
        reply_result(reply, false, u->computed_crc32);
        return true;
    }

    /* Only now does the app become valid. The record is 52 bytes but flash
       programs whole pages, so pad to 256 with the erased value. */
    fwog_app_meta_t m;
    fwog_app_meta_fill(&m, u->img_size, u->img_crc32, u->build_ts, u->version);
    memset(u->page, 0xFF, PAGE);
    memcpy(u->page, &m, sizeof m);
    if (!u->ops->program(u->ops->ctx, FWOG_APP_META_OFFSET, u->page, PAGE)) {
        u->state = FWOG_BL_RX_DONE_FAIL;
        reply_result(reply, false, u->computed_crc32);
        return true;
    }

    u->state = FWOG_BL_RX_DONE_OK;
    reply_result(reply, true, u->computed_crc32);
    return true;
}

bool fwog_bl_update_on_msg(fwog_bl_update_t *u, const uint8_t *payload,
                           size_t len, fwog_bl_reply_t *reply) {
    no_reply(reply);
    switch (fwog_bl_msg_type(payload, len)) {
    case FWOG_BL_MSG_UPDATE_BEGIN: return on_begin(u, payload, reply);
    case FWOG_BL_MSG_DATA:         return on_data(u, payload, reply);
    case FWOG_BL_MSG_UPDATE_END:   return on_end(u, reply);
    default:                       return false;
    }
}
