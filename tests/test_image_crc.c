#include "test_util.h"
#include "common/crc.h"
#include <string.h>

int main(void) {
    /* The published CRC-32/IEEE check value. tools/tests/test_genimage.py
       asserts the identical constant against Python's zlib.crc32, which is
       what makes "the build tool and the bootloader cannot disagree" a
       fact rather than a hope. */
    ASSERT_EQ(fwog_crc32("123456789", 9u), 0xCBF43926u);

    /* Vectors an image generator actually hits. */
    ASSERT_EQ(fwog_crc32("", 0u), 0x00000000u);
    {
        uint8_t z[32];
        memset(z, 0x00, sizeof z);
        ASSERT_EQ(fwog_crc32(z, sizeof z), 0x190A55ADu);
        memset(z, 0xFF, sizeof z);
        ASSERT_EQ(fwog_crc32(z, sizeof z), 0xFF6CAB0Bu);
    }
    {
        /* A 256-byte ramp: the same vector test_genimage.py uses. */
        uint8_t ramp[256];
        for (unsigned i = 0; i < 256u; i++) ramp[i] = (uint8_t)i;
        ASSERT_EQ(fwog_crc32(ramp, sizeof ramp), 0x29058C73u);
    }

    /* Chunked equals one-shot. The bootloader verifies in 256-byte reads
       while the generator does the whole file at once; if these ever
       disagreed, every update would fail its CRC check. */
    {
        uint8_t buf[1000];
        for (unsigned i = 0; i < sizeof buf; i++) buf[i] = (uint8_t)(i * 31u + 7u);
        uint32_t one = fwog_crc32(buf, sizeof buf);
        uint32_t c = FWOG_CRC32_INIT;
        for (unsigned off = 0; off < sizeof buf; off += 256u) {
            unsigned n = sizeof buf - off;
            if (n > 256u) n = 256u;
            c = fwog_crc32_update(c, buf + off, n);
        }
        ASSERT_EQ(fwog_crc32_final(c), one);
    }

    TEST_RETURN();
}
