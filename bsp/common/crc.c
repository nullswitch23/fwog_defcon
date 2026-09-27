#include "common/crc.h"

static uint16_t crc16_feed(uint16_t crc, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)p[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint16_t fwog_crc16_xmodem(const void *data, size_t len) {
    return crc16_feed(0x0000u, data, len);
}

uint16_t fwog_crc16_xmodem_span(const void *a, size_t alen,
                                const void *b, size_t blen) {
    return crc16_feed(crc16_feed(0x0000u, a, alen), b, blen);
}

uint32_t fwog_crc32_update(uint32_t crc, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return crc;
}

uint32_t fwog_crc32(const void *data, size_t len) {
    return fwog_crc32_final(fwog_crc32_update(FWOG_CRC32_INIT, data, len));
}
