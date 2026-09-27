/* Inter-CPU link framing: one implementation shared by the application link
 * and the display bootloader. Wire format, little-endian:
 *
 *   [SOF 0x7E][len16][payload...][crc16]
 *
 * The CRC-16/XMODEM covers len16 and the payload, not the SOF. There is no
 * byte stuffing: a payload byte equal to SOF can start a false frame, which
 * the CRC then rejects, and the decoder resynchronizes.
 *
 * Payload convention (not interpreted by this codec -- fwog_link_encode()
 * and fwog_link_rx_byte() are payload-agnostic): byte 0 of the payload is
 * the message type. This is frozen and must not change: a display
 * bootloader shipped in the field will speak this framing. Type 0x00 is
 * invalid, so an all-zero payload never decodes as a legal message; types
 * 0x01-0x1F are reserved for the bootloader's protocol and application
 * protocols on this frame start at 0x20. See the hardware record.
 */
#ifndef FWOG_LINK_FRAME_H
#define FWOG_LINK_FRAME_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FWOG_LINK_SOF          0x7Eu
/* 4096-byte update chunk plus room for a message header. */
#define FWOG_LINK_MAX_PAYLOAD  4160u
/* SOF(1) + len(2) + crc(2). A caller sizing a buffer as
 * MAX_PAYLOAD + OVERHEAD always has room for the largest frame. */
#define FWOG_LINK_OVERHEAD     5u

/* Returns bytes written to out, or 0 if len exceeds FWOG_LINK_MAX_PAYLOAD or
 * out_cap is too small. */
size_t fwog_link_encode(uint8_t *out, size_t out_cap, const void *payload, size_t len);

typedef enum {
    FWOG_LINK_ST_SOF = 0,
    FWOG_LINK_ST_LEN_LO,
    FWOG_LINK_ST_LEN_HI,
    FWOG_LINK_ST_PAYLOAD,
    FWOG_LINK_ST_CRC_LO,
    FWOG_LINK_ST_CRC_HI
} fwog_link_state_t;

typedef struct {
    uint8_t  state;
    uint16_t len;
    uint16_t got;
    uint16_t crc_rx;
    uint8_t  buf[FWOG_LINK_MAX_PAYLOAD];
} fwog_link_rx_t;

void   fwog_link_rx_init(fwog_link_rx_t *rx);

/* Feed exactly one received byte. Returns true when a valid frame just
 * completed: the payload is then in rx->buf and its length is written to
 * *out_len. Returns false otherwise, leaving *out_len untouched.
 *
 * The length is an out-parameter rather than the return value because a
 * zero-length frame is legal, so a length of 0 cannot also mean "nothing
 * completed" — conflating them hands the caller stale buffer contents. */
bool fwog_link_rx_byte(fwog_link_rx_t *rx, uint8_t b, size_t *out_len);

#endif
