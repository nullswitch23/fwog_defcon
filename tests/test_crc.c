#include "test_util.h"
#include "common/crc.h"
#include <string.h>

int main(void) {
    const char *chk = "123456789";
    size_t n = strlen(chk);

    /* Published check values for these two standard CRCs. */
    ASSERT_EQ(fwog_crc16_xmodem(chk, n), 0x31C3u);
    ASSERT_EQ(fwog_crc32(chk, n), 0xCBF43926u);

    /* Empty input: XMODEM seeds at 0; IEEE is init ^ xorout. */
    ASSERT_EQ(fwog_crc16_xmodem("", 0), 0x0000u);
    ASSERT_EQ(fwog_crc32("", 0), 0x00000000u);

    /* Chunked CRC32 must equal the one-shot result — the bootloader
       computes over 4 KB chunks while the build tool does it in one pass. */
    uint32_t c = FWOG_CRC32_INIT;
    c = fwog_crc32_update(c, chk, 4);
    c = fwog_crc32_update(c, chk + 4, n - 4);
    ASSERT_EQ(fwog_crc32_final(c), 0xCBF43926u);

    /* A single flipped bit must change the CRC. */
    char bad[10];
    memcpy(bad, chk, n + 1);
    bad[3] ^= 0x01;
    ASSERT_TRUE(fwog_crc32(bad, n) != 0xCBF43926u);
    ASSERT_TRUE(fwog_crc16_xmodem(bad, n) != 0x31C3u);

    TEST_RETURN();
}
