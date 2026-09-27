/* A RAM-backed FatFs diskio for the host test tree, standing in for
 * bsp/main_cpu/fs/fs_flash_disk.c (which is entirely behind #ifndef HOST_TEST).
 *
 * This is what makes the largest package in the roadmap the one with the most
 * host coverage -- the whole of FatFs and the whole of fwog_fs.c run under
 * CTest with no hardware. That inverts the usual relationship between package
 * size and confidence, which is the point.
 *
 * The disk deliberately mimics NOR flash semantics rather than being a plain
 * byte array:
 *
 *   - Erased state is 0xFF, not 0x00.
 *   - A write ERASES the sector first, exactly as fs_write_one_sector() does.
 *     Without this the RAM disk would accept a program-without-erase that real
 *     flash would silently turn into a bitwise AND of old and new content, and
 *     the host tests would pass on code that cannot work on the board.
 *
 * It does NOT model erase/program timing, so it says nothing about the
 * watchdog chunking -- that is fs_geom.c's host-tested splitter plus a bench
 * step, and this file makes no claim about it. */
#include "fs/ff/ff.h"
#include "fs/ff/diskio.h"
#include "fs/fs_geom.h"
#include <string.h>

/* 8 MB. Big, but it is a host process, and using anything smaller would mean
 * testing a geometry the board does not have. */
static unsigned char s_disk[FWOG_FS_SECTOR_COUNT * FWOG_FS_SECTOR_SIZE];
static int s_initialised;

/* Test hooks. */
void fs_ramdisk_wipe(void) {
    memset(s_disk, 0xFF, sizeof(s_disk));   /* erased NOR reads as all ones */
    s_initialised = 0;
}

/* Simulates a volume with no free space, so the full-volume path can be tested
 * without writing 8 MB. When set, writes past this sector fail. */
static uint32_t s_fail_writes_at = 0xFFFFFFFFu;
void fs_ramdisk_fail_writes_from(uint32_t lba) { s_fail_writes_at = lba; }

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != 0u) return STA_NOINIT;
    return s_initialised ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != 0u) return STA_NOINIT;
    s_initialised = 1;
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0u || buff == NULL) return RES_PARERR;
    if (!s_initialised) return RES_NOTRDY;
    if (!fwog_fs_range_valid((uint32_t)sector, (uint32_t)count)) return RES_PARERR;
    memcpy(buff, s_disk + (size_t)sector * FWOG_FS_SECTOR_SIZE,
           (size_t)count * FWOG_FS_SECTOR_SIZE);
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0u || buff == NULL) return RES_PARERR;
    if (!s_initialised) return RES_NOTRDY;
    if (!fwog_fs_range_valid((uint32_t)sector, (uint32_t)count)) return RES_PARERR;
    if ((uint32_t)sector + count > s_fail_writes_at) return RES_ERROR;

    for (UINT i = 0u; i < count; i++) {
        unsigned char *dst = s_disk + ((size_t)sector + i) * FWOG_FS_SECTOR_SIZE;
        /* Erase then program, as the real driver does. */
        memset(dst, 0xFF, FWOG_FS_SECTOR_SIZE);
        memcpy(dst, buff + (size_t)i * FWOG_FS_SECTOR_SIZE, FWOG_FS_SECTOR_SIZE);
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv != 0u) return RES_PARERR;
    if (!s_initialised) return RES_NOTRDY;
    switch (cmd) {
    case CTRL_SYNC:         return RES_OK;
    case GET_SECTOR_COUNT:  *(LBA_t *)buff = (LBA_t)FWOG_FS_SECTOR_COUNT; return RES_OK;
    case GET_SECTOR_SIZE:   *(WORD  *)buff = (WORD)FWOG_FS_SECTOR_SIZE;   return RES_OK;
    case GET_BLOCK_SIZE:    *(DWORD *)buff = 1u;                          return RES_OK;
    default:                return RES_PARERR;
    }
}

/* The same fixed timestamp the real driver returns, for the same reason: main
 * has no clock. Kept identical so a test can assert on it. */
DWORD get_fattime(void) {
    return ((DWORD)(2026 - 1980) << 25) | ((DWORD)1 << 21) | ((DWORD)1 << 16);
}
