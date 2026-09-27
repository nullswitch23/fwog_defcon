#include "fs/fs_geom.h"

bool fwog_fs_lba_valid(uint32_t lba) {
    return lba < FWOG_FS_SECTOR_COUNT;
}

bool fwog_fs_range_valid(uint32_t lba, uint32_t count) {
    if (count == 0u) return false;
    if (lba >= FWOG_FS_SECTOR_COUNT) return false;
    /* Subtract rather than add: `lba + count` would wrap for a large lba and
     * quietly report a wild range as in-bounds. */
    return count <= (FWOG_FS_SECTOR_COUNT - lba);
}

uint32_t fwog_fs_lba_offset(uint32_t lba) {
    if (!fwog_fs_lba_valid(lba)) return 0u;
    return FWOG_FS_FLASH_OFFSET + lba * FWOG_FS_SECTOR_SIZE;
}

uint32_t fwog_fs_erase_chunk(uint32_t total, uint32_t done) {
    if (done >= total) return 0u;
    const uint32_t left = total - done;
    return (left < FWOG_FS_ERASE_CHUNK_SECTORS) ? left
                                                : FWOG_FS_ERASE_CHUNK_SECTORS;
}
