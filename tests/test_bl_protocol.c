#include "test_util.h"
#include "common/link/bl_proto.h"
#include "common/link/link_frame.h"
#include <string.h>

int main(void) {
    /* Wire sizes are frozen: a field bootloader speaks this. */
    ASSERT_EQ(sizeof(fwog_bl_hello_t), 64u);
    ASSERT_EQ(sizeof(fwog_bl_update_begin_t), 48u);
    ASSERT_EQ(sizeof(fwog_bl_data_hdr_t), 12u);
    ASSERT_EQ(sizeof(fwog_bl_ack_t), 8u);
    ASSERT_EQ(sizeof(fwog_bl_result_t), 8u);
    ASSERT_EQ(sizeof(fwog_bl_status_t), 32u);

    /* Type 0x00 is invalid and 0x20 is where application protocols start,
       so every bootloader type must sit strictly between. */
    ASSERT_TRUE(FWOG_BL_MSG_HELLO >= 0x01 && FWOG_BL_MSG_STATUS <= 0x1F);

    /* The no-bar sentinel must not collide with a legal percentage. */
    ASSERT_TRUE(FWOG_BL_STATUS_NO_BAR > 100u);

    /* The largest DATA frame must fit the codec's payload limit. */
    ASSERT_TRUE(sizeof(fwog_bl_data_hdr_t) + FWOG_BL_CHUNK <= FWOG_LINK_MAX_PAYLOAD);

    uint8_t buf[FWOG_LINK_MAX_PAYLOAD];

    /* --- bare messages --- */
    ASSERT_EQ(fwog_bl_encode_bare(buf, FWOG_BL_MSG_RUN), 1u);
    ASSERT_EQ(fwog_bl_msg_type(buf, 1u), FWOG_BL_MSG_RUN);
    ASSERT_EQ(fwog_bl_encode_bare(buf, FWOG_BL_MSG_READY), 1u);
    ASSERT_EQ(fwog_bl_msg_type(buf, 1u), FWOG_BL_MSG_READY);
    ASSERT_EQ(fwog_bl_encode_bare(buf, FWOG_BL_MSG_UPDATE_END), 1u);
    ASSERT_EQ(fwog_bl_msg_type(buf, 1u), FWOG_BL_MSG_UPDATE_END);

    /* A bare message with extra bytes is not a bare message. */
    ASSERT_EQ(fwog_bl_msg_type(buf, 2u), 0u);

    /* --- ACK / NAK --- */
    ASSERT_EQ(fwog_bl_encode_ack(buf, FWOG_BL_MSG_ACK, 0x12345678u), 8u);
    ASSERT_EQ(fwog_bl_msg_type(buf, 8u), FWOG_BL_MSG_ACK);
    {
        fwog_bl_ack_t a;
        memcpy(&a, buf, sizeof a);
        ASSERT_EQ(a.offset, 0x12345678u);
        /* Reserved bytes are zeroed, so a receiver may one day check them. */
        ASSERT_EQ(a.reserved[0], 0u);
        ASSERT_EQ(a.reserved[1], 0u);
        ASSERT_EQ(a.reserved[2], 0u);
    }
    ASSERT_EQ(fwog_bl_msg_type(buf, 7u), 0u);   /* short */
    ASSERT_EQ(fwog_bl_msg_type(buf, 9u), 0u);   /* long  */

    /* --- HELLO --- */
    {
        fwog_bl_hello_t h;
        memset(&h, 0, sizeof h);
        h.type = FWOG_BL_MSG_HELLO;
        h.proto_ver = FWOG_BL_PROTO_VER;
        h.app_valid = 1u;
        memcpy(h.version, "app-1.0", 8);
        memcpy(h.bl_version, "bl-1.0", 7);
        ASSERT_EQ(fwog_bl_msg_type(&h, sizeof h), FWOG_BL_MSG_HELLO);

        /* Unterminated strings are rejected at decode, not printed. */
        memset(h.version, 'A', sizeof h.version);
        ASSERT_EQ(fwog_bl_msg_type(&h, sizeof h), 0u);
        memcpy(h.version, "app-1.0", 8);
        memset(h.bl_version, 'B', sizeof h.bl_version);
        ASSERT_EQ(fwog_bl_msg_type(&h, sizeof h), 0u);
    }

    /* --- UPDATE_BEGIN --- */
    {
        fwog_bl_update_begin_t ub;
        memset(&ub, 0, sizeof ub);
        ub.type = FWOG_BL_MSG_UPDATE_BEGIN;
        ub.size = 8192u; ub.crc32 = 0xAAu; ub.build_ts = 7u;
        memcpy(ub.version, "v9", 3);
        ASSERT_EQ(fwog_bl_msg_type(&ub, sizeof ub), FWOG_BL_MSG_UPDATE_BEGIN);
        ASSERT_EQ(fwog_bl_msg_type(&ub, sizeof ub - 1u), 0u);
        memset(ub.version, 'C', sizeof ub.version);
        ASSERT_EQ(fwog_bl_msg_type(&ub, sizeof ub), 0u);
    }

    /* --- DATA: len is carried AND cross-checked against the frame --- */
    {
        uint8_t d[sizeof(fwog_bl_data_hdr_t) + FWOG_BL_CHUNK];
        fwog_bl_data_hdr_t h;
        memset(d, 0, sizeof d);
        memset(&h, 0, sizeof h);
        h.type = FWOG_BL_MSG_DATA;
        h.offset = 4096u;
        h.len = FWOG_BL_CHUNK;
        memcpy(d, &h, sizeof h);
        ASSERT_EQ(fwog_bl_msg_type(d, sizeof d), FWOG_BL_MSG_DATA);

        /* A header len that disagrees with the frame length is refused.
           Carrying len explicitly is redundant with the frame length; the
           cross-check is what keeps the redundancy from becoming a second
           source of truth. */
        h.len = FWOG_BL_CHUNK - 1u; memcpy(d, &h, sizeof h);
        ASSERT_EQ(fwog_bl_msg_type(d, sizeof d), 0u);

        /* A short final chunk is legal. */
        h.len = 100u; memcpy(d, &h, sizeof h);
        ASSERT_EQ(fwog_bl_msg_type(d, sizeof(fwog_bl_data_hdr_t) + 100u),
                  FWOG_BL_MSG_DATA);

        /* Zero-length DATA is not. */
        h.len = 0u; memcpy(d, &h, sizeof h);
        ASSERT_EQ(fwog_bl_msg_type(d, sizeof(fwog_bl_data_hdr_t)), 0u);

        /* Oversize is not, even if the frame claims to hold it. */
        h.len = FWOG_BL_CHUNK + 1u; memcpy(d, &h, sizeof h);
        ASSERT_EQ(fwog_bl_msg_type(d, sizeof d), 0u);

        /* A frame too short to even hold the header is not. */
        h.len = FWOG_BL_CHUNK; memcpy(d, &h, sizeof h);
        ASSERT_EQ(fwog_bl_msg_type(d, 4u), 0u);
    }

    /* --- STATUS --- */
    {
        ASSERT_EQ(fwog_bl_encode_status(buf, "erasing", 42u), 32u);
        ASSERT_EQ(fwog_bl_msg_type(buf, 32u), FWOG_BL_MSG_STATUS);
        {
            fwog_bl_status_t st;
            memcpy(&st, buf, sizeof st);
            ASSERT_EQ(st.percent, 42u);
            ASSERT_TRUE(strcmp(st.text, "erasing") == 0);
        }

        /* Text-only is the common case and must survive the round trip. */
        fwog_bl_encode_status(buf, "waiting", FWOG_BL_STATUS_NO_BAR);
        ASSERT_EQ(fwog_bl_msg_type(buf, 32u), FWOG_BL_MSG_STATUS);
        {
            fwog_bl_status_t st;
            memcpy(&st, buf, sizeof st);
            ASSERT_EQ(st.percent, FWOG_BL_STATUS_NO_BAR);
        }

        /* Both boundaries of the legal range are legal. */
        fwog_bl_encode_status(buf, "", 0u);
        ASSERT_EQ(fwog_bl_msg_type(buf, 32u), FWOG_BL_MSG_STATUS);
        fwog_bl_encode_status(buf, "done", 100u);
        ASSERT_EQ(fwog_bl_msg_type(buf, 32u), FWOG_BL_MSG_STATUS);

        /* A nonsense percentage becomes "no bar", never a clamped 100 --
           a bar reading 100% would claim the operation finished. */
        fwog_bl_encode_status(buf, "confused", 200u);
        {
            fwog_bl_status_t st;
            memcpy(&st, buf, sizeof st);
            ASSERT_EQ(st.percent, FWOG_BL_STATUS_NO_BAR);
        }

        /* Over-long text is truncated, not overflowed, and stays terminated. */
        fwog_bl_encode_status(buf, "0123456789012345678901234567890123456789", 5u);
        ASSERT_EQ(fwog_bl_msg_type(buf, 32u), FWOG_BL_MSG_STATUS);
        {
            fwog_bl_status_t st;
            memcpy(&st, buf, sizeof st);
            ASSERT_EQ(strlen(st.text), FWOG_BL_STATUS_TEXT_LEN - 1u);
        }

        /* NULL text is "" rather than a crash. */
        fwog_bl_encode_status(buf, NULL, 0u);
        ASSERT_EQ(fwog_bl_msg_type(buf, 32u), FWOG_BL_MSG_STATUS);

        /* Wrong length, an unterminated string, and an out-of-range
           percentage are each refused at decode. */
        ASSERT_EQ(fwog_bl_msg_type(buf, 31u), 0u);
        {
            fwog_bl_status_t st;
            memset(&st, 0, sizeof st);
            st.type = FWOG_BL_MSG_STATUS;
            memset(st.text, 'D', sizeof st.text);
            ASSERT_EQ(fwog_bl_msg_type(&st, sizeof st), 0u);

            memset(&st, 0, sizeof st);
            st.type = FWOG_BL_MSG_STATUS;
            st.percent = 101u;   /* not reachable via the encoder */
            ASSERT_EQ(fwog_bl_msg_type(&st, sizeof st), 0u);
        }
    }

    /* --- rejections --- */
    buf[0] = 0x00u;
    ASSERT_EQ(fwog_bl_msg_type(buf, 1u), 0u);   /* type 0 is always invalid */
    buf[0] = 0x20u;
    ASSERT_EQ(fwog_bl_msg_type(buf, 1u), 0u);   /* application range */
    buf[0] = 0x0Bu;
    ASSERT_EQ(fwog_bl_msg_type(buf, 1u), 0u);   /* reserved, unassigned */
    ASSERT_EQ(fwog_bl_msg_type(buf, 0u), 0u);   /* empty payload */

    /* --- a full round trip through the frame codec --- */
    {
        uint8_t payload[8], frame[FWOG_LINK_MAX_PAYLOAD + FWOG_LINK_OVERHEAD];
        size_t plen = fwog_bl_encode_ack(payload, FWOG_BL_MSG_NAK, 0x1000u);
        size_t flen = fwog_link_encode(frame, sizeof frame, payload, plen);
        ASSERT_EQ(flen, plen + FWOG_LINK_OVERHEAD);

        fwog_link_rx_t rx;
        fwog_link_rx_init(&rx);
        size_t got = 0;
        bool done = false;
        for (size_t i = 0; i < flen; i++) done = fwog_link_rx_byte(&rx, frame[i], &got);
        ASSERT_TRUE(done);
        ASSERT_EQ(got, plen);
        ASSERT_EQ(fwog_bl_msg_type(rx.buf, got), FWOG_BL_MSG_NAK);
        {
            fwog_bl_ack_t a;
            memcpy(&a, rx.buf, sizeof a);
            ASSERT_EQ(a.offset, 0x1000u);
        }
    }

    TEST_RETURN();
}
