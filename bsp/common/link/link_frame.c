#include "common/link/link_frame.h"
#include "common/crc.h"
#include <string.h>

size_t fwog_link_encode(uint8_t *out, size_t out_cap, const void *payload, size_t len) {
    if (len > FWOG_LINK_MAX_PAYLOAD) return 0;
    size_t need = len + FWOG_LINK_OVERHEAD; /* SOF + len16 + crc16 */
    if (out_cap < need) return 0;

    out[0] = FWOG_LINK_SOF;
    out[1] = (uint8_t)(len & 0xFFu);
    out[2] = (uint8_t)(len >> 8);
    memcpy(out + 3, payload, len);

    uint16_t crc = fwog_crc16_xmodem_span(out + 1, 2, payload, len);
    out[3 + len] = (uint8_t)(crc & 0xFFu);
    out[4 + len] = (uint8_t)(crc >> 8);
    return need;
}

void fwog_link_rx_init(fwog_link_rx_t *rx) {
    rx->state  = FWOG_LINK_ST_SOF;
    rx->len    = 0;
    rx->got    = 0;
    rx->crc_rx = 0;
}

bool fwog_link_rx_byte(fwog_link_rx_t *rx, uint8_t b, size_t *out_len) {
    switch (rx->state) {
    case FWOG_LINK_ST_SOF:
        if (b == FWOG_LINK_SOF) rx->state = FWOG_LINK_ST_LEN_LO;
        return false;

    case FWOG_LINK_ST_LEN_LO:
        rx->len = b;
        rx->state = FWOG_LINK_ST_LEN_HI;
        return false;

    case FWOG_LINK_ST_LEN_HI:
        rx->len |= (uint16_t)b << 8;
        if (rx->len > FWOG_LINK_MAX_PAYLOAD) {  /* implausible: resync */
            fwog_link_rx_init(rx);
            return false;
        }
        rx->got = 0;
        rx->state = rx->len ? FWOG_LINK_ST_PAYLOAD : FWOG_LINK_ST_CRC_LO;
        return false;

    case FWOG_LINK_ST_PAYLOAD:
        rx->buf[rx->got++] = b;
        if (rx->got == rx->len) rx->state = FWOG_LINK_ST_CRC_LO;
        return false;

    case FWOG_LINK_ST_CRC_LO:
        rx->crc_rx = b;
        rx->state = FWOG_LINK_ST_CRC_HI;
        return false;

    case FWOG_LINK_ST_CRC_HI: {
        rx->crc_rx |= (uint16_t)b << 8;
        uint8_t hdr[2] = { (uint8_t)(rx->len & 0xFFu), (uint8_t)(rx->len >> 8) };
        /* Exactly the bytes the encoder checksummed, in the same order. */
        uint16_t crc = fwog_crc16_xmodem_span(hdr, 2, rx->buf, rx->len);
        bool ok = (crc == rx->crc_rx);
        size_t len = rx->len;
        fwog_link_rx_init(rx);
        if (ok && out_len) *out_len = len;
        return ok;
    }

    default:
        fwog_link_rx_init(rx);
        return false;
    }
}
