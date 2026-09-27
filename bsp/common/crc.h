/* CRC primitives shared by the link framing, the display bootloader, and the
 * build-time image tool. Bitwise (no tables): small enough for the bootloader
 * and fast enough for everything here. Pure — no SDK, no hardware. */
#ifndef FWOG_CRC_H
#define FWOG_CRC_H
#include <stddef.h>
#include <stdint.h>

/* CRC-16/XMODEM: poly 0x1021, init 0x0000, no reflection, no final xor. Not
 * classic CRC-16/CCITT (init 0xFFFF) -- named for the variant it actually
 * is, since a host-side image tool written from a misleading name would
 * pick the wrong variant and produce images only rejected on hardware. */
uint16_t fwog_crc16_xmodem(const void *data, size_t len);

/* CRC-32/IEEE: poly 0x04C11DB7 reflected (0xEDB88320), init 0xFFFFFFFF,
 * reflected in/out, final xor 0xFFFFFFFF. */
#define FWOG_CRC32_INIT 0xFFFFFFFFu
uint32_t fwog_crc32_update(uint32_t crc, const void *data, size_t len);
uint32_t fwog_crc32(const void *data, size_t len);

static inline uint32_t fwog_crc32_final(uint32_t crc) {
    return crc ^ 0xFFFFFFFFu;
}

/* CRC-16/XMODEM over two non-contiguous spans, as if concatenated. The link
 * decoder needs this: the length field and the payload live in different
 * places and copying a 4 KB payload just to checksum it would be wasteful. */
uint16_t fwog_crc16_xmodem_span(const void *a, size_t alen,
                                const void *b, size_t blen);

#endif
