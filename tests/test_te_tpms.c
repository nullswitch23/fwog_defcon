#include "test_util.h"
#include "te_tpms.h"
#include <string.h>

static void bits_from_bytes_lsb(const uint8_t *bytes, unsigned n, uint8_t *bits) {
    for (unsigned i = 0; i < n * 8u; i++)
        bits[i] = (bytes[i / 8u] >> (i % 8u)) & 1u;
}

int main(void) {
    uint8_t frame[8] = { 0x12, 0x34, 0x56, 0x78, 0x50, 0x46, 0x00, 0 };
    frame[7] = te_crc8(frame, 7, 0x07, 0);
    uint8_t bits[128];
    bits_from_bytes_lsb(frame, 8, bits);
    te_decode_t dec;
    ASSERT_TRUE(te_tpms_decode(3, bits, 64, &dec));
    ASSERT_TRUE(dec.ok);
    ASSERT_EQ((int)dec.id[0], 0x12);
    ASSERT_EQ((int)dec.profile, 3);

    ASSERT_EQ((int)te_crc8((const uint8_t *)"123456789", 9, 0x07, 0), 0x50);

    te_scan_ent_t log[4];
    unsigned n = 0;
    te_scan_merge(log, 4, &n, &dec, -50, 1000u);
    ASSERT_EQ((int)n, 1);
    te_scan_merge(log, 4, &n, &dec, -40, 5000u);
    ASSERT_EQ((int)n, 1);
    ASSERT_EQ((int)log[0].bursts, 2);
    ASSERT_EQ((int)log[0].peak_rssi, -40);

    TEST_RETURN();
}
