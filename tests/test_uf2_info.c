/* Layout and CRC for the UF2 self-description record.
 *
 * This layout is a WIRE FORMAT BETWEEN TWO REPOSITORIES. fwOGappexplorer
 * scans a raw UF2 payload for the magic and reads these fields at these
 * offsets; it has no way to negotiate. The asserts below are what stop the
 * layout drifting out from under it, exactly as test_app_meta.c does for the
 * record the bootloader reads out of flash. */
#include "test_util.h"
#include "common/uf2_info.h"
#include "common/crc.h"
#include <stddef.h>
#include <string.h>

static void make_good(fwog_uf2_info_t *i) {
    memset(i, 0, sizeof *i);
    i->magic0 = FWOG_UF2_INFO_MAGIC0;
    i->magic1 = FWOG_UF2_INFO_MAGIC1;
    i->struct_version = FWOG_UF2_INFO_VERSION;
    i->cpu  = FWOG_UF2_CPU_DISPLAY;
    i->kind = FWOG_UF2_KIND_APP;
    i->app_version = 17u;
    memcpy(i->name, "bench", 6);
    memcpy(i->description, "Bench harness", 14);
    i->crc32 = fwog_uf2_info_crc(i);
}

int main(void) {
    fwog_uf2_info_t i;

    /* The layout is frozen. 216 bytes with no padding anywhere -- every
       member is naturally aligned and the trailing uint32_t needs none, so
       the host and Cortex-M0+ agree. */
    ASSERT_EQ(sizeof(fwog_uf2_info_t), 216u);
    ASSERT_EQ(offsetof(fwog_uf2_info_t, magic0), 0u);
    ASSERT_EQ(offsetof(fwog_uf2_info_t, magic1), 4u);
    ASSERT_EQ(offsetof(fwog_uf2_info_t, struct_version), 8u);
    ASSERT_EQ(offsetof(fwog_uf2_info_t, cpu), 10u);
    ASSERT_EQ(offsetof(fwog_uf2_info_t, kind), 11u);
    ASSERT_EQ(offsetof(fwog_uf2_info_t, app_version), 12u);
    ASSERT_EQ(offsetof(fwog_uf2_info_t, name), 16u);
    ASSERT_EQ(offsetof(fwog_uf2_info_t, description), 48u);
    ASSERT_EQ(offsetof(fwog_uf2_info_t, build), 176u);
    ASSERT_EQ(offsetof(fwog_uf2_info_t, build_ts), 208u);
    ASSERT_EQ(offsetof(fwog_uf2_info_t, crc32), 212u);

    /* The scanner looks for these eight bytes LITERALLY. Eight rather than
       four because it scans a raw payload: four bytes hit by accident in
       16 MB of flash, eight do not. */
    make_good(&i);
    ASSERT_EQ(memcmp(&i, "FWGOINFO", 8), 0);

    /* A well-formed record round-trips. */
    ASSERT_TRUE(fwog_uf2_info_valid(&i));

    /* A corrupted CRC is rejected. */
    make_good(&i);
    i.crc32 ^= 1u;
    ASSERT_TRUE(!fwog_uf2_info_valid(&i));

    /* Magic is checked independently of the CRC: a record whose CRC is
       self-consistent but whose magic is wrong is still not ours. */
    make_good(&i);
    i.magic1 = 0u;
    i.crc32 = fwog_uf2_info_crc(&i);
    ASSERT_TRUE(!fwog_uf2_info_valid(&i));

    make_good(&i);
    i.magic0 = 0u;
    i.crc32 = fwog_uf2_info_crc(&i);
    ASSERT_TRUE(!fwog_uf2_info_valid(&i));

    /* A struct_version we do not understand is rejected rather than parsed
       optimistically -- the fields may mean something else. */
    make_good(&i);
    i.struct_version = 2u;
    i.crc32 = fwog_uf2_info_crc(&i);
    ASSERT_TRUE(!fwog_uf2_info_valid(&i));

    /* Strings arrive from a scanned binary and are never trusted: an
       unterminated array must not reach a %s. */
    make_good(&i);
    memset(i.name, 'x', sizeof i.name);
    i.crc32 = fwog_uf2_info_crc(&i);
    ASSERT_TRUE(!fwog_uf2_info_valid(&i));

    make_good(&i);
    memset(i.description, 'x', sizeof i.description);
    i.crc32 = fwog_uf2_info_crc(&i);
    ASSERT_TRUE(!fwog_uf2_info_valid(&i));

    make_good(&i);
    memset(i.build, 'x', sizeof i.build);
    i.crc32 = fwog_uf2_info_crc(&i);
    ASSERT_TRUE(!fwog_uf2_info_valid(&i));

    /* The CRC covers every preceding byte. A field added later but left out
       of the CRC would be a silent hole, so prove the last one before it is
       covered. */
    make_good(&i);
    {
        uint32_t before = i.crc32;
        i.build_ts = 12345u;
        ASSERT_TRUE(fwog_uf2_info_crc(&i) != before);
    }

    /* Same relationship test_app_meta.c asserts: the CRC is taken over
       exactly the bytes before the crc32 member, not over the whole struct. */
    make_good(&i);
    ASSERT_EQ(fwog_uf2_info_crc(&i), fwog_crc32(&i, 212u));

    /* The CPU and kind discriminants are part of the wire format: the
       App Explorer keys on `cpu` to tell a main app's own record from the
       display image it carries. */
    ASSERT_EQ(FWOG_UF2_CPU_DISPLAY, 0u);
    ASSERT_EQ(FWOG_UF2_CPU_MAIN, 1u);
    ASSERT_EQ(FWOG_UF2_KIND_APP, 0u);
    ASSERT_EQ(FWOG_UF2_KIND_BOOTLOADER, 1u);
    ASSERT_EQ(FWOG_UF2_INFO_MAGIC0, 0x4F475746u);
    ASSERT_EQ(FWOG_UF2_INFO_MAGIC1, 0x4F464E49u);

    TEST_RETURN();
}
