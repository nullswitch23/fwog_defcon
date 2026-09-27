#include "test_util.h"
#include "common/app_meta.h"
#include <string.h>

static void make_good(fwog_app_meta_t *m) {
    fwog_app_meta_fill(m, 4096u, 0xDEADBEEFu, 1753500000u, "v1.2.3-4-gabcdef");
}

int main(void) {
    /* The layout is frozen. If any of these change, every bootloader
       already flashed onto a board is speaking a different layout. */
    ASSERT_EQ(FWOG_BL_MAX_SIZE, 0x20000u);
    ASSERT_EQ(FWOG_APP_META_OFFSET, 0x20000u);
    ASSERT_EQ(FWOG_APP_META_SIZE, 0x1000u);
    ASSERT_EQ(FWOG_APP_OFFSET, 0x21000u);
    ASSERT_EQ(FWOG_APP_XIP_ADDR, 0x10021000u);
    ASSERT_EQ(FWOG_APP_MAX_SIZE, 0xFDF000u);
    /* "PAGO" in flash byte order on a little-endian core. */
    ASSERT_EQ(FWOG_APP_META_MAGIC, 0x4F474150u);
    ASSERT_EQ(sizeof(fwog_app_meta_t), 52u);

    fwog_app_meta_t m;
    make_good(&m);
    ASSERT_EQ(m.magic, FWOG_APP_META_MAGIC);
    ASSERT_EQ(m.size, 4096u);
    ASSERT_EQ(m.crc32, 0xDEADBEEFu);
    ASSERT_EQ(m.build_ts, 1753500000u);
    ASSERT_TRUE(strcmp(m.version, "v1.2.3-4-gabcdef") == 0);
    ASSERT_TRUE(fwog_app_meta_valid(&m));

    /* An erased sector reads as all-ones and must never validate: that is
       the whole crash-safety mechanism. An update erases this sector first,
       so any interruption leaves exactly this state. */
    fwog_app_meta_t erased;
    memset(&erased, 0xFF, sizeof erased);
    ASSERT_TRUE(!fwog_app_meta_valid(&erased));

    /* An all-zero sector must not validate either. */
    fwog_app_meta_t zero;
    memset(&zero, 0x00, sizeof zero);
    ASSERT_TRUE(!fwog_app_meta_valid(&zero));

    /* Wrong magic. */
    make_good(&m); m.magic = 0x4F474151u;
    ASSERT_TRUE(!fwog_app_meta_valid(&m));

    /* Corrupted meta_crc32. */
    make_good(&m); m.meta_crc32 ^= 1u;
    ASSERT_TRUE(!fwog_app_meta_valid(&m));

    /* Any field flipped without recomputing the CRC. */
    make_good(&m); m.crc32 ^= 1u;
    ASSERT_TRUE(!fwog_app_meta_valid(&m));
    make_good(&m); m.build_ts ^= 1u;
    ASSERT_TRUE(!fwog_app_meta_valid(&m));
    make_good(&m); m.version[0] = 'x';
    ASSERT_TRUE(!fwog_app_meta_valid(&m));

    /* Size zero: there is no such thing as a zero-byte app. */
    make_good(&m); m.size = 0u; m.meta_crc32 = fwog_app_meta_crc(&m);
    ASSERT_TRUE(!fwog_app_meta_valid(&m));

    /* Size past the end of flash. Rejected here rather than at jump time,
       because the CRC verify would otherwise read off the end of XIP. */
    make_good(&m); m.size = FWOG_APP_MAX_SIZE + 1u;
    m.meta_crc32 = fwog_app_meta_crc(&m);
    ASSERT_TRUE(!fwog_app_meta_valid(&m));
    make_good(&m); m.size = FWOG_APP_MAX_SIZE;
    m.meta_crc32 = fwog_app_meta_crc(&m);
    ASSERT_TRUE(fwog_app_meta_valid(&m));

    /* A version field with no NUL in it. The bootloader prints this string
       and puts it in HELLO; an unterminated one runs off the struct. */
    make_good(&m); memset(m.version, 'A', sizeof m.version);
    m.meta_crc32 = fwog_app_meta_crc(&m);
    ASSERT_TRUE(!fwog_app_meta_valid(&m));

    /* fwog_app_meta_fill truncates an over-long version and still
       terminates it -- git describe on a long tag must not corrupt the
       record. */
    fwog_app_meta_fill(&m, 100u, 1u, 2u,
                       "0123456789012345678901234567890123456789");
    ASSERT_EQ(strlen(m.version), FWOG_APP_VERSION_LEN - 1u);
    ASSERT_TRUE(fwog_app_meta_valid(&m));

    /* The metadata CRC covers the preceding fields only, never itself. */
    make_good(&m);
    ASSERT_EQ(fwog_app_meta_crc(&m), fwog_crc32(&m, 48u));

    /* fwog_str_bounded, used on every string that arrives from flash or
       the wire. */
    ASSERT_TRUE(fwog_str_bounded("ok", 3u));
    ASSERT_TRUE(fwog_str_bounded("", 1u));
    ASSERT_TRUE(!fwog_str_bounded("no", 2u));
    ASSERT_TRUE(!fwog_str_bounded("x", 0u));

    TEST_RETURN();
}
