#include "test_util.h"
#include "bootloader/bl_flash.h"
#include "bootloader/bl_receiver.h"
#include "common/link/bl_proto.h"
#include <string.h>

/* --- a RAM-backed fake flash ------------------------------------------
 * Covers [FWOG_APP_META_OFFSET, FWOG_APP_META_OFFSET + FAKE_SPAN). That is
 * the metadata sector followed by the first 32 KB of the app slot, which is
 * every address the receiver touches in these tests.
 *
 * It enforces the two rules the real hardware enforces and that a naive
 * fake would let slide: erase must be sector-aligned, and program must be
 * page-aligned onto erased (0xFF) bytes. A receiver that programmed twice
 * without erasing would pass against a plain memcpy fake and fail on the
 * bench.
 */
#define FAKE_BASE  FWOG_APP_META_OFFSET
#define FAKE_SPAN  (0x1000u + 0x8000u)

typedef struct {
    uint8_t  mem[FAKE_SPAN];
    unsigned erase_calls;
    unsigned program_calls;
    uint32_t erased_bytes;
    bool     fail_next_program;
    bool     fail_next_read;
    bool     fail_next_erase;
    bool     fault;          /* set on any rule violation */
} fake_flash_t;

static void fake_reset(fake_flash_t *f) {
    memset(f, 0, sizeof *f);
    memset(f->mem, 0xFF, sizeof f->mem);
}

static bool in_range(uint32_t off, uint32_t len) {
    return off >= FAKE_BASE && (uint64_t)off + len <= (uint64_t)FAKE_BASE + FAKE_SPAN;
}

/* The alignment and bounds rules are NOT re-implemented here: the fakes
 * delegate to the real predicates from bl_flash.c, so every update this
 * file drives -- including the ragged-tail case, whose final page is the
 * one real updates most often get wrong -- doubles as a regression test
 * that a legitimate update still survives the reserve and device guards.
 * Hand-duplicating the rules would let a future tightening of the real
 * guards pass here and fail on a board.
 *
 * What this does NOT cover: the boundary cases. The largest image here is
 * 12 KB, so nothing exercises an image whose final page lands exactly on
 * FWOG_FLASH_SIZE, and nothing here writes below FWOG_APP_META_OFFSET. Those
 * are checked directly in tests/test_bl_flash.c against the predicates, not
 * through the receiver.
 *
 * bl_flash_src_ok() is deliberately NOT delegated. It is a claim about the
 * TARGET's address map -- the RP2040 XIP windows at 0x10000000-0x13FFFFFF --
 * and the host addresses these fakes actually receive have no relationship
 * to it. Asserting it here would test a coincidence about where the host
 * linker put g_image, not the predicate. tests/test_bl_flash.c exercises it
 * directly with real target addresses, which is the only place it means
 * anything. */
static bool fake_erase(void *ctx, uint32_t off, uint32_t len) {
    fake_flash_t *f = (fake_flash_t *)ctx;
    if (!bl_flash_range_ok(off, len, 4096u) || !in_range(off, len)) {
        f->fault = true;
        return false;
    }
    if (f->fail_next_erase) { f->fail_next_erase = false; return false; }
    memset(f->mem + (off - FAKE_BASE), 0xFF, len);
    f->erase_calls++;
    f->erased_bytes += len;
    return true;
}

static bool fake_program(void *ctx, uint32_t off, const uint8_t *src, uint32_t len) {
    fake_flash_t *f = (fake_flash_t *)ctx;
    if (!bl_flash_range_ok(off, len, 256u) || !in_range(off, len)) {
        f->fault = true;
        return false;
    }
    /* NOR flash can only clear bits. Programming a byte that is not 0xFF is
       the bug this fake exists to catch. */
    for (uint32_t i = 0; i < len; i++) {
        if (f->mem[off - FAKE_BASE + i] != 0xFFu) { f->fault = true; return false; }
    }
    if (f->fail_next_program) { f->fail_next_program = false; return false; }
    memcpy(f->mem + (off - FAKE_BASE), src, len);
    f->program_calls++;
    return true;
}

static bool fake_read(void *ctx, uint32_t off, uint8_t *dst, uint32_t len) {
    fake_flash_t *f = (fake_flash_t *)ctx;
    if (!bl_flash_read_ok(off, len) || !in_range(off, len)) {
        f->fault = true;
        return false;
    }
    /* Deliberately does NOT touch dst on failure, matching bl_read(). A fake
       that zero-filled would hide a caller that used the buffer anyway. */
    if (f->fail_next_read) { f->fail_next_read = false; return false; }
    memcpy(dst, f->mem + (off - FAKE_BASE), len);
    return true;
}

/* --- helpers ---------------------------------------------------------- */

static uint8_t g_image[16384];
static uint8_t g_frame[sizeof(fwog_bl_data_hdr_t) + FWOG_BL_CHUNK];

static void fill_image(size_t n) {
    for (size_t i = 0; i < n; i++) g_image[i] = (uint8_t)(i * 7u + 3u);
}

static size_t make_begin(uint8_t *out, uint32_t size, uint32_t crc,
                         const char *version) {
    fwog_bl_update_begin_t ub;
    memset(&ub, 0, sizeof ub);
    ub.type = FWOG_BL_MSG_UPDATE_BEGIN;
    ub.size = size;
    ub.crc32 = crc;
    ub.build_ts = 1753500000u;
    memcpy(ub.version, version, strlen(version));
    memcpy(out, &ub, sizeof ub);
    return sizeof ub;
}

static size_t make_data(uint8_t *out, uint32_t offset, const uint8_t *src,
                        uint32_t len) {
    fwog_bl_data_hdr_t h;
    memset(&h, 0, sizeof h);
    h.type = FWOG_BL_MSG_DATA;
    h.offset = offset;
    h.len = len;
    memcpy(out, &h, sizeof h);
    memcpy(out + sizeof h, src, len);
    return sizeof h + len;
}

static uint8_t reply_type(const fwog_bl_reply_t *r) {
    return fwog_bl_msg_type(r->buf, r->len);
}

static uint32_t reply_offset(const fwog_bl_reply_t *r) {
    fwog_bl_ack_t a;
    memcpy(&a, r->buf, sizeof a);
    return a.offset;
}

static bool reply_result_ok(const fwog_bl_reply_t *r) {
    fwog_bl_result_t res;
    memcpy(&res, r->buf, sizeof res);
    return res.ok != 0u;
}

static bool meta_in_fake(const fake_flash_t *f, fwog_app_meta_t *out) {
    memcpy(out, f->mem, sizeof *out);   /* metadata sector is at FAKE_BASE */
    return fwog_app_meta_valid(out);
}

/* Drive a complete, well-behaved update. Returns the final RESULT reply. */
static void run_good_update(fwog_bl_update_t *u, fake_flash_t *f,
                            uint32_t size, fwog_bl_reply_t *result) {
    uint8_t msg[sizeof(fwog_bl_update_begin_t)];
    fwog_bl_reply_t rep;

    fill_image(size);
    uint32_t crc = fwog_crc32(g_image, size);

    size_t n = make_begin(msg, size, crc, "v1.0-test");
    ASSERT_TRUE(fwog_bl_update_on_msg(u, msg, n, &rep));
    ASSERT_EQ(reply_type(&rep), FWOG_BL_MSG_READY);

    for (uint32_t off = 0; off < size; ) {
        uint32_t len = (size - off > FWOG_BL_CHUNK) ? FWOG_BL_CHUNK : size - off;
        size_t dn = make_data(g_frame, off, g_image + off, len);
        ASSERT_TRUE(fwog_bl_update_on_msg(u, g_frame, dn, &rep));
        ASSERT_EQ(reply_type(&rep), FWOG_BL_MSG_ACK);
        ASSERT_EQ(reply_offset(&rep), off);
        off += len;
    }

    uint8_t end[1];
    size_t en = fwog_bl_encode_bare(end, FWOG_BL_MSG_UPDATE_END);
    ASSERT_TRUE(fwog_bl_update_on_msg(u, end, en, result));
    ASSERT_TRUE(!f->fault);
}

int main(void) {
    fake_flash_t f;
    fwog_bl_flash_ops_t ops = { fake_erase, fake_program, fake_read, &f };
    fwog_bl_update_t u;
    fwog_bl_reply_t rep;
    uint8_t msg[sizeof(fwog_bl_update_begin_t)];

    /* --- 1. a whole image, exactly two chunks --- */
    fake_reset(&f);
    fwog_bl_update_init(&u, &ops);
    ASSERT_EQ(u.state, FWOG_BL_RX_IDLE);
    run_good_update(&u, &f, 8192u, &rep);
    ASSERT_EQ(reply_type(&rep), FWOG_BL_MSG_RESULT);
    ASSERT_TRUE(reply_result_ok(&rep));
    ASSERT_EQ(u.state, FWOG_BL_RX_DONE_OK);
    ASSERT_TRUE(memcmp(f.mem + 0x1000u, g_image, 8192u) == 0);
    {
        fwog_app_meta_t m;
        ASSERT_TRUE(meta_in_fake(&f, &m));
        ASSERT_EQ(m.size, 8192u);
        ASSERT_EQ(m.crc32, fwog_crc32(g_image, 8192u));
        ASSERT_EQ(m.build_ts, 1753500000u);
        ASSERT_TRUE(strcmp(m.version, "v1.0-test") == 0);
    }

    /* --- 2. a partial final chunk, not a multiple of the page size --- */
    fake_reset(&f);
    fwog_bl_update_init(&u, &ops);
    run_good_update(&u, &f, 4096u + 100u, &rep);
    ASSERT_TRUE(reply_result_ok(&rep));
    ASSERT_TRUE(memcmp(f.mem + 0x1000u, g_image, 4196u) == 0);
    /* The tail page is padded with 0xFF, never with stale buffer bytes. */
    ASSERT_EQ(f.mem[0x1000u + 4196u], 0xFFu);
    ASSERT_EQ(f.mem[0x1000u + 4255u], 0xFFu);

    /* --- 3. an image smaller than one page --- */
    fake_reset(&f);
    fwog_bl_update_init(&u, &ops);
    run_good_update(&u, &f, 17u, &rep);
    ASSERT_TRUE(reply_result_ok(&rep));
    ASSERT_TRUE(memcmp(f.mem + 0x1000u, g_image, 17u) == 0);
    ASSERT_EQ(f.mem[0x1000u + 17u], 0xFFu);

    /* --- 4. the metadata sector is erased FIRST, before any image byte ---
       This is the crash-safety property: interrupt the update anywhere
       after UPDATE_BEGIN and the app reads as invalid. */
    fake_reset(&f);
    fwog_bl_update_init(&u, &ops);
    fill_image(8192u);
    {
        size_t n = make_begin(msg, 8192u, fwog_crc32(g_image, 8192u), "v2");
        /* Pre-poison the metadata sector with a VALID record, as a board
           with a working app would have. */
        fwog_app_meta_t old;
        fwog_app_meta_fill(&old, 4096u, 0x1234u, 1u, "old");
        memcpy(f.mem, &old, sizeof old);

        ASSERT_TRUE(fwog_bl_update_on_msg(&u, msg, n, &rep));
        ASSERT_EQ(reply_type(&rep), FWOG_BL_MSG_READY);

        fwog_app_meta_t m;
        ASSERT_TRUE(!meta_in_fake(&f, &m));   /* invalidated immediately */
        ASSERT_EQ(m.magic, 0xFFFFFFFFu);
    }

    /* --- 5. only the sectors the image needs are erased --- */
    fake_reset(&f);
    fwog_bl_update_init(&u, &ops);
    fill_image(4097u);
    {
        size_t n = make_begin(msg, 4097u, fwog_crc32(g_image, 4097u), "v3");
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, msg, n, &rep));
        /* 4 KB metadata + two 4 KB sectors to cover 4097 bytes. Erasing the
           whole 15.9 MB slot would take minutes. */
        ASSERT_EQ(f.erased_bytes, 0x1000u + 0x2000u);
    }

    /* --- 6. a duplicate DATA is re-ACKed, never reprogrammed ---
       Three chunks, so case 7 below has a real chunk to skip to. */
    fake_reset(&f);
    fwog_bl_update_init(&u, &ops);
    fill_image(12288u);
    {
        size_t n = make_begin(msg, 12288u, fwog_crc32(g_image, 12288u), "v4");
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, msg, n, &rep));

        size_t dn = make_data(g_frame, 0u, g_image, FWOG_BL_CHUNK);
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, g_frame, dn, &rep));
        ASSERT_EQ(reply_type(&rep), FWOG_BL_MSG_ACK);
        unsigned programs_after_first = f.program_calls;

        /* Main's ACK was lost, so it retransmits. Reprogramming the same
           already-programmed page would be a fault on real NOR flash --
           the fake enforces that -- so the receiver must ACK and skip. */
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, g_frame, dn, &rep));
        ASSERT_EQ(reply_type(&rep), FWOG_BL_MSG_ACK);
        ASSERT_EQ(reply_offset(&rep), 0u);
        ASSERT_EQ(f.program_calls, programs_after_first);
        ASSERT_TRUE(!f.fault);
    }

    /* --- 7. a gap produces a NAK naming the resume point --- */
    {
        /* Continuing from case 6: chunk 0 landed, so 4096 is expected next.
           Main skips it and sends chunk 2 instead -- a lost DATA frame. */
        size_t dn = make_data(g_frame, 8192u, g_image + 8192u, FWOG_BL_CHUNK);
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, g_frame, dn, &rep));
        ASSERT_EQ(reply_type(&rep), FWOG_BL_MSG_NAK);
        ASSERT_EQ(reply_offset(&rep), 4096u);

        /* Then the correct chunk still lands. */
        dn = make_data(g_frame, 4096u, g_image + 4096u, FWOG_BL_CHUNK);
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, g_frame, dn, &rep));
        ASSERT_EQ(reply_type(&rep), FWOG_BL_MSG_ACK);
        ASSERT_EQ(reply_offset(&rep), 4096u);
    }

    /* --- 8. a wrong image CRC is rejected and NO metadata is written --- */
    fake_reset(&f);
    fwog_bl_update_init(&u, &ops);
    fill_image(4096u);
    {
        /* Announce a CRC that the bytes will not produce. */
        size_t n = make_begin(msg, 4096u, fwog_crc32(g_image, 4096u) ^ 1u, "v5");
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, msg, n, &rep));
        size_t dn = make_data(g_frame, 0u, g_image, 4096u);
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, g_frame, dn, &rep));

        uint8_t end[1];
        size_t en = fwog_bl_encode_bare(end, FWOG_BL_MSG_UPDATE_END);
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, end, en, &rep));
        ASSERT_EQ(reply_type(&rep), FWOG_BL_MSG_RESULT);
        ASSERT_TRUE(!reply_result_ok(&rep));
        ASSERT_EQ(u.state, FWOG_BL_RX_DONE_FAIL);

        /* The computed CRC is reported so main can log both. */
        fwog_bl_result_t res;
        memcpy(&res, rep.buf, sizeof res);
        ASSERT_EQ(res.computed_crc32, fwog_crc32(g_image, 4096u));

        fwog_app_meta_t m;
        ASSERT_TRUE(!meta_in_fake(&f, &m));
    }

    /* --- 9. UPDATE_END before the last chunk fails, writes no metadata --- */
    fake_reset(&f);
    fwog_bl_update_init(&u, &ops);
    fill_image(8192u);
    {
        size_t n = make_begin(msg, 8192u, fwog_crc32(g_image, 8192u), "v6");
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, msg, n, &rep));
        size_t dn = make_data(g_frame, 0u, g_image, FWOG_BL_CHUNK);
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, g_frame, dn, &rep));

        uint8_t end[1];
        size_t en = fwog_bl_encode_bare(end, FWOG_BL_MSG_UPDATE_END);
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, end, en, &rep));
        ASSERT_TRUE(!reply_result_ok(&rep));
        ASSERT_EQ(u.state, FWOG_BL_RX_DONE_FAIL);
        fwog_app_meta_t m;
        ASSERT_TRUE(!meta_in_fake(&f, &m));
    }

    /* --- 10. a failing program aborts the update --- */
    fake_reset(&f);
    fwog_bl_update_init(&u, &ops);
    fill_image(4096u);
    {
        size_t n = make_begin(msg, 4096u, fwog_crc32(g_image, 4096u), "v7");
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, msg, n, &rep));
        f.fail_next_program = true;
        size_t dn = make_data(g_frame, 0u, g_image, 4096u);
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, g_frame, dn, &rep));
        ASSERT_EQ(reply_type(&rep), FWOG_BL_MSG_RESULT);
        ASSERT_TRUE(!reply_result_ok(&rep));
        ASSERT_EQ(u.state, FWOG_BL_RX_DONE_FAIL);
    }

    /* --- 10b. A rejected READ during verify fails the update, and does
           NOT write metadata. Before read returned bool this could only be
           caught indirectly, by the zero-filled buffer failing CRC32; now
           the receiver aborts on the read itself. The reported CRC must be
           0, not a stale or half-computed value -- "could not read the
           slot" is a different answer from "the slot is wrong". --- */
    fake_reset(&f);
    fwog_bl_update_init(&u, &ops);
    fill_image(4096u);
    {
        size_t n = make_begin(msg, 4096u, fwog_crc32(g_image, 4096u), "v7b");
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, msg, n, &rep));
        size_t dn = make_data(g_frame, 0u, g_image, 4096u);
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, g_frame, dn, &rep));
        ASSERT_EQ(reply_type(&rep), FWOG_BL_MSG_ACK);

        f.fail_next_read = true;
        uint8_t end[1];
        size_t en = fwog_bl_encode_bare(end, FWOG_BL_MSG_UPDATE_END);
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, end, en, &rep));
        ASSERT_EQ(reply_type(&rep), FWOG_BL_MSG_RESULT);
        ASSERT_TRUE(!reply_result_ok(&rep));
        ASSERT_EQ(u.state, FWOG_BL_RX_DONE_FAIL);
        ASSERT_EQ(u.computed_crc32, 0u);

        /* The metadata sector must still be erased: an update that could
           not be verified must not become bootable. */
        fwog_app_meta_t m;
        ASSERT_TRUE(!meta_in_fake(&f, &m));
    }

    /* --- 10c. A non-final chunk that ends mid-page is REJECTED outright.
           bl_receiver.h promises program() is always called page-aligned,
           and this is the one place a wire-legal message could break that:
           fwog_bl_msg_type() allows any len in [1, FWOG_BL_CHUNK]. Accepting
           it desyncs every later program() by `tail` bytes, which surfaces
           one chunk LATER as a misalignment rejection -- after the tail
           write has already burned a page NOR cannot rewrite. --- */
    fake_reset(&f);
    fwog_bl_update_init(&u, &ops);
    fill_image(8192u);
    {
        size_t n = make_begin(msg, 8192u, fwog_crc32(g_image, 8192u), "v7c");
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, msg, n, &rep));

        /* 300 bytes at offset 0, with 8192 announced: not the final chunk
           and not a page multiple. */
        size_t dn = make_data(g_frame, 0u, g_image, 300u);
        ASSERT_TRUE(!fwog_bl_update_on_msg(&u, g_frame, dn, &rep));
        ASSERT_EQ(f.program_calls, 0u);        /* nothing was written */
        ASSERT_TRUE(!f.fault);                 /* and no rule was violated */
        ASSERT_EQ(u.state, FWOG_BL_RX_RECEIVING);   /* session still usable */

        /* A page-multiple chunk at the same offset is still accepted, so
           the guard rejects the shape and not the offset. */
        dn = make_data(g_frame, 0u, g_image, 256u);
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, g_frame, dn, &rep));
        ASSERT_EQ(reply_type(&rep), FWOG_BL_MSG_ACK);

        /* The FINAL chunk may end mid-page -- that is the whole exception.
           Fill to 8192 with a ragged last chunk. */
        uint32_t off = 256u;
        while (off < 8192u) {
            uint32_t len = (8192u - off > FWOG_BL_CHUNK) ? FWOG_BL_CHUNK
                                                         : 8192u - off;
            dn = make_data(g_frame, off, g_image + off, len);
            ASSERT_TRUE(fwog_bl_update_on_msg(&u, g_frame, dn, &rep));
            ASSERT_EQ(reply_type(&rep), FWOG_BL_MSG_ACK);
            off += len;
        }
    }

    /* --- 10d. A failed erase in UPDATE_BEGIN must FAIL the session, not
           leave the previous one RECEIVING. Otherwise the next DATA at
           offset 0 compares below the stale next_offset, is taken for a
           retransmit, and gets re-ACKed -- so main walks an entire transfer
           believing it landed while nothing is written. --- */
    fake_reset(&f);
    fwog_bl_update_init(&u, &ops);
    fill_image(8192u);
    {
        /* Establish a session and get next_offset off zero. */
        size_t n = make_begin(msg, 8192u, fwog_crc32(g_image, 8192u), "v7d");
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, msg, n, &rep));
        size_t dn = make_data(g_frame, 0u, g_image, FWOG_BL_CHUNK);
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, g_frame, dn, &rep));
        ASSERT_EQ(u.next_offset, (uint32_t)FWOG_BL_CHUNK);

        /* Restart, with the metadata erase failing. */
        f.fail_next_erase = true;
        n = make_begin(msg, 8192u, fwog_crc32(g_image, 8192u), "v7d2");
        ASSERT_TRUE(!fwog_bl_update_on_msg(&u, msg, n, &rep));
        ASSERT_EQ(u.state, FWOG_BL_RX_DONE_FAIL);

        /* The stale session must be gone: DATA at 0 must not be mistaken
           for a retransmit and silently ACKed. */
        dn = make_data(g_frame, 0u, g_image, FWOG_BL_CHUNK);
        ASSERT_TRUE(!fwog_bl_update_on_msg(&u, g_frame, dn, &rep));
    }

    /* --- 11. UPDATE_BEGIN restarts a half-finished update --- */
    fake_reset(&f);
    fwog_bl_update_init(&u, &ops);
    fill_image(8192u);
    {
        size_t n = make_begin(msg, 8192u, fwog_crc32(g_image, 8192u), "v8");
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, msg, n, &rep));
        size_t dn = make_data(g_frame, 0u, g_image, FWOG_BL_CHUNK);
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, g_frame, dn, &rep));
        ASSERT_EQ(u.next_offset, 4096u);

        /* Main gave up and started over -- a realistic retry after a reset
           on its side. The receiver must re-erase and rewind, not refuse. */
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, msg, n, &rep));
        ASSERT_EQ(reply_type(&rep), FWOG_BL_MSG_READY);
        ASSERT_EQ(u.next_offset, 0u);
        ASSERT_EQ(u.state, FWOG_BL_RX_RECEIVING);

        dn = make_data(g_frame, 0u, g_image, FWOG_BL_CHUNK);
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, g_frame, dn, &rep));
        ASSERT_TRUE(!f.fault);
    }

    /* --- 12. rejections --- */
    fake_reset(&f);
    fwog_bl_update_init(&u, &ops);

    /* DATA before UPDATE_BEGIN. */
    {
        size_t dn = make_data(g_frame, 0u, g_image, 256u);
        ASSERT_TRUE(!fwog_bl_update_on_msg(&u, g_frame, dn, &rep));
        ASSERT_EQ(rep.len, 0u);
    }
    /* UPDATE_END before UPDATE_BEGIN. */
    {
        uint8_t end[1];
        size_t en = fwog_bl_encode_bare(end, FWOG_BL_MSG_UPDATE_END);
        ASSERT_TRUE(!fwog_bl_update_on_msg(&u, end, en, &rep));
        ASSERT_EQ(rep.len, 0u);
    }
    /* A malformed payload the protocol decoder rejects. */
    {
        uint8_t junk[3] = { FWOG_BL_MSG_ACK, 0u, 0u };
        ASSERT_TRUE(!fwog_bl_update_on_msg(&u, junk, sizeof junk, &rep));
        ASSERT_EQ(rep.len, 0u);
    }
    /* A size of zero, and a size past the end of flash. */
    {
        size_t n = make_begin(msg, 0u, 0u, "bad");
        ASSERT_TRUE(!fwog_bl_update_on_msg(&u, msg, n, &rep));
        n = make_begin(msg, FWOG_APP_MAX_SIZE + 1u, 0u, "bad");
        ASSERT_TRUE(!fwog_bl_update_on_msg(&u, msg, n, &rep));
        ASSERT_EQ(f.erase_calls, 0u);
    }
    /* A DATA chunk that runs past the announced image size. */
    {
        fill_image(4096u);
        size_t n = make_begin(msg, 300u, fwog_crc32(g_image, 300u), "v9");
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, msg, n, &rep));
        size_t dn = make_data(g_frame, 0u, g_image, 400u);
        ASSERT_TRUE(!fwog_bl_update_on_msg(&u, g_frame, dn, &rep));
        ASSERT_EQ(rep.len, 0u);
    }
    ASSERT_TRUE(!f.fault);

    /* --- 13. progress percentage --- */
    fake_reset(&f);
    fwog_bl_update_init(&u, &ops);
    ASSERT_EQ(fwog_bl_update_percent(&u), 0u);
    fill_image(8192u);
    {
        size_t n = make_begin(msg, 8192u, fwog_crc32(g_image, 8192u), "vA");
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, msg, n, &rep));
        ASSERT_EQ(fwog_bl_update_percent(&u), 0u);
        size_t dn = make_data(g_frame, 0u, g_image, FWOG_BL_CHUNK);
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, g_frame, dn, &rep));
        ASSERT_EQ(fwog_bl_update_percent(&u), 50u);
        dn = make_data(g_frame, 4096u, g_image + 4096u, FWOG_BL_CHUNK);
        ASSERT_TRUE(fwog_bl_update_on_msg(&u, g_frame, dn, &rep));
        ASSERT_EQ(fwog_bl_update_percent(&u), 100u);
    }

    TEST_RETURN();
}
