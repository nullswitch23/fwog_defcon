/* Host tests for the transcribed volume geometry and the chunked-erase
 * splitter. Getting an LBA-to-address mapping wrong writes into someone
 * else's flash and the symptom is arbitrary, so this is worth pinning. */
#include "fs/fs_geom.h"
#include "test_util.h"

static void test_the_transcribed_constants(void) {
    /* These four ARE the compatibility claim. If any of them changes, a board
     * formatted by this firmware is no longer readable by the stock firmware,
     * and the volume will still mount here -- so nothing else would notice. */
    ASSERT_EQ(FWOG_FS_FLASH_OFFSET, 8u * 1024u * 1024u);
    ASSERT_EQ(FWOG_FS_VOLUME_BYTES, 8u * 1024u * 1024u);
    ASSERT_EQ(FWOG_FS_SECTOR_SIZE, 4096u);
    ASSERT_EQ(FWOG_FS_SECTOR_COUNT, 2048u);
}

static void test_lba_to_offset(void) {
    ASSERT_TRUE(fwog_fs_lba_valid(0u));
    ASSERT_TRUE(fwog_fs_lba_valid(FWOG_FS_SECTOR_COUNT - 1u));
    ASSERT_TRUE(!fwog_fs_lba_valid(FWOG_FS_SECTOR_COUNT));
    ASSERT_TRUE(!fwog_fs_lba_valid(0xFFFFFFFFu));

    /* LBA 0 is the first sector of the volume, 8 MB into flash. */
    ASSERT_EQ(fwog_fs_lba_offset(0u), 8u * 1024u * 1024u);
    /* The last sector ends exactly at 16 MB -- the end of the part. */
    ASSERT_EQ(fwog_fs_lba_offset(FWOG_FS_SECTOR_COUNT - 1u),
              16u * 1024u * 1024u - 4096u);
    ASSERT_EQ(fwog_fs_lba_offset(1u), 8u * 1024u * 1024u + 4096u);
}

static void test_range_validation_is_overflow_safe(void) {
    ASSERT_TRUE(fwog_fs_range_valid(0u, 1u));
    ASSERT_TRUE(fwog_fs_range_valid(0u, FWOG_FS_SECTOR_COUNT));
    ASSERT_TRUE(fwog_fs_range_valid(FWOG_FS_SECTOR_COUNT - 1u, 1u));

    ASSERT_TRUE(!fwog_fs_range_valid(0u, 0u));                        /* empty */
    ASSERT_TRUE(!fwog_fs_range_valid(0u, FWOG_FS_SECTOR_COUNT + 1u)); /* too long */
    ASSERT_TRUE(!fwog_fs_range_valid(FWOG_FS_SECTOR_COUNT, 1u));      /* past end */
    ASSERT_TRUE(!fwog_fs_range_valid(FWOG_FS_SECTOR_COUNT - 1u, 2u)); /* straddles */

    /* The overflow case. An `lba + count <= COUNT` check wraps here and
     * reports this wild range as valid, which is a write to an arbitrary
     * flash address. */
    ASSERT_TRUE(!fwog_fs_range_valid(0xFFFFFF00u, 0x200u));
    ASSERT_TRUE(!fwog_fs_range_valid(0xFFFFFFFFu, 1u));
    ASSERT_TRUE(!fwog_fs_range_valid(1000u, 0xFFFFFFFFu));
}

/* The contract: iterating the splitter covers [0, total) exactly once, with no
 * gap and no overlap. Checked by marking a coverage array, for totals that do
 * and do not divide evenly by the chunk size. */
static void check_covers_exactly(uint32_t total) {
    static unsigned char seen[4096];
    ASSERT_TRUE(total <= sizeof(seen));
    for (uint32_t i = 0u; i < total; i++) seen[i] = 0u;

    uint32_t done = 0u;
    uint32_t chunk;
    unsigned iterations = 0u;
    while ((chunk = fwog_fs_erase_chunk(total, done)) != 0u) {
        ASSERT_TRUE(chunk <= FWOG_FS_ERASE_CHUNK_SECTORS);
        ASSERT_TRUE(done + chunk <= total);        /* never overshoots */
        for (uint32_t i = 0u; i < chunk; i++) seen[done + i]++;
        done += chunk;
        if (++iterations > total + 2u) break;      /* non-termination guard */
    }
    ASSERT_EQ(done, total);

    int wrong = 0;
    for (uint32_t i = 0u; i < total; i++) if (seen[i] != 1u) wrong++;
    ASSERT_EQ(wrong, 0);
}

static void test_erase_chunking(void) {
    ASSERT_EQ(fwog_fs_erase_chunk(0u, 0u), 0u);      /* nothing to do */
    ASSERT_EQ(fwog_fs_erase_chunk(10u, 10u), 0u);    /* already finished */
    ASSERT_EQ(fwog_fs_erase_chunk(10u, 99u), 0u);    /* past the end */
    ASSERT_EQ(fwog_fs_erase_chunk(1u, 0u), 1u);      /* short of a full chunk */
    ASSERT_EQ(fwog_fs_erase_chunk(100u, 0u), FWOG_FS_ERASE_CHUNK_SECTORS);

    check_covers_exactly(1u);
    check_covers_exactly(FWOG_FS_ERASE_CHUNK_SECTORS - 1u);   /* under */
    check_covers_exactly(FWOG_FS_ERASE_CHUNK_SECTORS);        /* exact */
    check_covers_exactly(FWOG_FS_ERASE_CHUNK_SECTORS + 1u);   /* over by one */
    check_covers_exactly(FWOG_FS_ERASE_CHUNK_SECTORS * 4u);   /* even multiple */
    check_covers_exactly(FWOG_FS_ERASE_CHUNK_SECTORS * 4u + 3u); /* ragged */
    check_covers_exactly(4096u);
}

/* The chunk size is a watchdog-safety constant, so state the bound it was
 * derived from rather than only the number. W25Q128JVPIQ worst case is 400 ms
 * sector erase + 48 ms sector program = 448 ms; the long watchdog window is
 * FWOG_WATCHDOG_LONG_MS = 8300 ms. */
static void test_chunk_fits_the_watchdog_window(void) {
    const unsigned worst_ms_per_sector = 448u;
    const unsigned long_window_ms = 8300u;
    const unsigned worst_chunk_ms =
        FWOG_FS_ERASE_CHUNK_SECTORS * worst_ms_per_sector;
    ASSERT_TRUE(worst_chunk_ms < long_window_ms);
    /* And with real margin -- at least 2x -- not merely "fits". */
    ASSERT_TRUE(worst_chunk_ms * 2u < long_window_ms);
}

int main(void) {
    test_the_transcribed_constants();
    test_lba_to_offset();
    test_range_validation_is_overflow_safe();
    test_erase_chunking();
    test_chunk_fits_the_watchdog_window();
    TEST_RETURN();
}
