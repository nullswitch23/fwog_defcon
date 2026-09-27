#include "common/link/bl_proto.h"
#include <string.h>

uint8_t fwog_bl_msg_type(const void *payload, size_t len) {
    if (len == 0u) return 0u;
    const uint8_t *p = (const uint8_t *)payload;
    switch (p[0]) {
    case FWOG_BL_MSG_RUN:
    case FWOG_BL_MSG_READY:
    case FWOG_BL_MSG_UPDATE_END:
        return (len == 1u) ? p[0] : 0u;

    case FWOG_BL_MSG_ACK:
    case FWOG_BL_MSG_NAK:
        return (len == sizeof(fwog_bl_ack_t)) ? p[0] : 0u;

    case FWOG_BL_MSG_RESULT:
        return (len == sizeof(fwog_bl_result_t)) ? p[0] : 0u;

    case FWOG_BL_MSG_HELLO: {
        if (len != sizeof(fwog_bl_hello_t)) return 0u;
        fwog_bl_hello_t h;
        memcpy(&h, p, sizeof h);   /* p may be unaligned -- see the header */
        if (!fwog_str_bounded(h.version, FWOG_BL_VERSION_LEN)) return 0u;
        if (!fwog_str_bounded(h.bl_version, FWOG_BL_BLVER_LEN)) return 0u;
        return FWOG_BL_MSG_HELLO;
    }

    case FWOG_BL_MSG_UPDATE_BEGIN: {
        if (len != sizeof(fwog_bl_update_begin_t)) return 0u;
        fwog_bl_update_begin_t ub;
        memcpy(&ub, p, sizeof ub);
        if (!fwog_str_bounded(ub.version, FWOG_BL_VERSION_LEN)) return 0u;
        return FWOG_BL_MSG_UPDATE_BEGIN;
    }

    case FWOG_BL_MSG_STATUS: {
        if (len != sizeof(fwog_bl_status_t)) return 0u;
        fwog_bl_status_t st;
        memcpy(&st, p, sizeof st);
        if (!fwog_str_bounded(st.text, FWOG_BL_STATUS_TEXT_LEN)) return 0u;
        /* Validating the range here is what lets the renderer treat percent
           as trustworthy arithmetic rather than re-checking it. */
        if (st.percent > 100u && st.percent != FWOG_BL_STATUS_NO_BAR) return 0u;
        return FWOG_BL_MSG_STATUS;
    }

    case FWOG_BL_MSG_DATA: {
        if (len < sizeof(fwog_bl_data_hdr_t)) return 0u;
        fwog_bl_data_hdr_t h;
        memcpy(&h, p, sizeof h);
        if (h.len == 0u || h.len > FWOG_BL_CHUNK) return 0u;
        if (len != sizeof(fwog_bl_data_hdr_t) + h.len) return 0u;
        return FWOG_BL_MSG_DATA;
    }

    default:
        /* Includes 0x00 (reserved invalid), the unassigned 0x0A-0x1F, and
           everything from 0x20 up, which belongs to application protocols
           and must never be interpreted here. */
        return 0u;
    }
}

size_t fwog_bl_encode_bare(uint8_t *out, uint8_t type) {
    out[0] = type;
    return 1u;
}

size_t fwog_bl_encode_ack(uint8_t *out, uint8_t type, uint32_t offset) {
    fwog_bl_ack_t a;
    memset(&a, 0, sizeof a);
    a.type = type;
    a.offset = offset;
    memcpy(out, &a, sizeof a);
    return sizeof a;
}

size_t fwog_bl_encode_status(uint8_t *out, const char *text, uint8_t percent) {
    fwog_bl_status_t st;
    memset(&st, 0, sizeof st);   /* also supplies the NUL terminator */
    st.type = FWOG_BL_MSG_STATUS;
    st.percent = (percent > 100u) ? FWOG_BL_STATUS_NO_BAR : percent;
    if (text) {
        size_t n = 0;
        while (n < FWOG_BL_STATUS_TEXT_LEN - 1u && text[n] != '\0') n++;
        memcpy(st.text, text, n);
    }
    memcpy(out, &st, sizeof st);
    return sizeof st;
}
