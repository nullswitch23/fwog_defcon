#include "test_util.h"
#include "common/link/link_frame.h"
#include <string.h>

static size_t feed(fwog_link_rx_t *rx, const uint8_t *b, size_t n, size_t *last_len) {
    size_t decodes = 0;
    for (size_t i = 0; i < n; i++) {
        size_t got = 0;
        if (fwog_link_rx_byte(rx, b[i], &got)) {
            decodes++;
            if (last_len) *last_len = got;
        }
    }
    return decodes;
}

int main(void) {
    uint8_t frame[FWOG_LINK_MAX_PAYLOAD + FWOG_LINK_OVERHEAD];
    fwog_link_rx_t rx;
    size_t len = 0;

    /* Round trip. */
    const uint8_t payload[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x7E, 0x00 };
    size_t n = fwog_link_encode(frame, sizeof frame, payload, sizeof payload);
    ASSERT_EQ(n, sizeof payload + FWOG_LINK_OVERHEAD);
    fwog_link_rx_init(&rx);
    ASSERT_EQ(feed(&rx, frame, n, &len), 1u);
    ASSERT_EQ(len, sizeof payload);
    ASSERT_TRUE(memcmp(rx.buf, payload, sizeof payload) == 0);

    /* A zero-length frame is legal. */
    n = fwog_link_encode(frame, sizeof frame, "", 0);
    ASSERT_EQ(n, FWOG_LINK_OVERHEAD);
    fwog_link_rx_init(&rx);
    ASSERT_EQ(feed(&rx, frame, n, &len), 1u);
    ASSERT_EQ(len, 0u);

    /* Corrupted payload is rejected. */
    n = fwog_link_encode(frame, sizeof frame, payload, sizeof payload);
    frame[4] ^= 0x20;
    fwog_link_rx_init(&rx);
    ASSERT_EQ(feed(&rx, frame, n, NULL), 0u);

    /* Oversized payload is refused by the encoder. */
    static uint8_t big[FWOG_LINK_MAX_PAYLOAD + 1];
    ASSERT_EQ(fwog_link_encode(frame, sizeof frame, big, sizeof big), 0u);

    /* Too small an output buffer is refused. */
    ASSERT_EQ(fwog_link_encode(frame, 3, payload, sizeof payload), 0u);

    /* Resync: leading garbage, including a bare SOF and a bogus length,
       must not prevent the following good frame from decoding. */
    uint8_t stream[64];
    size_t k = 0;
    stream[k++] = 0x11;
    stream[k++] = FWOG_LINK_SOF;
    stream[k++] = 0xFF;              /* len low  */
    stream[k++] = 0xFF;              /* len high -> way over max, must reset */
    stream[k++] = 0x22;
    n = fwog_link_encode(stream + k, sizeof stream - k, payload, sizeof payload);
    ASSERT_TRUE(n > 0);
    fwog_link_rx_init(&rx);
    ASSERT_EQ(feed(&rx, stream, k + n, &len), 1u);
    ASSERT_EQ(len, sizeof payload);

    /* A truncated frame must not wedge the decoder forever. The payload here
       deliberately contains no SOF byte, so each following frame can cost at
       most one false start; three frames therefore guarantee a decode. */
    const uint8_t clean[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
    uint8_t trunc[16];
    size_t t = fwog_link_encode(trunc, sizeof trunc, clean, 2);
    fwog_link_rx_init(&rx);
    feed(&rx, trunc, t - 3, NULL);           /* cut before the CRC lands */
    uint8_t good[64];
    size_t g = fwog_link_encode(good, sizeof good, clean, sizeof clean);
    size_t decodes = 0;
    for (int i = 0; i < 3; i++) decodes += feed(&rx, good, g, NULL);
    ASSERT_TRUE(decodes >= 1);

    TEST_RETURN();
}
