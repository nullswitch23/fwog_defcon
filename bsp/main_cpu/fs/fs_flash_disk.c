/* FatFs disk I/O over the last 8 MB of the main CPU's flash, plus
 * get_fattime(). See fwog_fs.h for the three hazards this file is the one
 * responsible for, and fs_geom.h for the transcribed geometry.
 *
 * The whole file is hardware-bound, so the host test tree substitutes
 * tests/fs_ramdisk.c instead -- which is what lets the WHOLE of FatFs and the
 * whole public API run under CTest. See tests/test_fs_api.c.
 */
#ifndef HOST_TEST

#include "fs/ff/ff.h"
#include "fs/ff/diskio.h"
#include "fs/fs_geom.h"
#include "common/diag.h"
#include "watchdog/watchdog.h"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include <string.h>

/* The SDK's own sector size must agree with the geometry we transcribed, or
 * every erase is the wrong length. A compile error is the right way to find
 * that out. */
_Static_assert(FLASH_SECTOR_SIZE == FWOG_FS_SECTOR_SIZE,
               "the SDK's FLASH_SECTOR_SIZE and fs_geom.h's "
               "FWOG_FS_SECTOR_SIZE disagree");

static bool s_initialised;

/* ---- The one function that must not execute from flash ----
 *
 * flash_range_erase()/flash_range_program() put the QSPI interface into a
 * command mode where XIP reads do not work, so this function's own
 * instructions must already be in RAM before it starts. __not_in_flash_func
 * is what puts them there. The reference has this commented out
 * (`//__not_in_flash_func(` at diskio.c:193) -- it survives there because the
 * compiler happened to inline it into a caller that was itself resident, which
 * is not a property to rely on.
 *
 * Interrupts are disabled across the pair for the same reason: any ISR that
 * lives in flash would fault if it fired mid-erase.
 *
 * The third requirement -- core1 not executing from flash -- is a documented
 * PRECONDITION, not something this can enforce. See fwog_fs.h hazard 1. */
static void __not_in_flash_func(fs_write_one_sector)(uint32_t flash_offset,
                                                     const uint8_t *data) {
    const uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(flash_offset, FWOG_FS_SECTOR_SIZE);
    flash_range_program(flash_offset, data, FWOG_FS_SECTOR_SIZE);
    restore_interrupts(ints);
}

/* ---- diskio interface ---- */

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != 0u) return STA_NOINIT;
    return s_initialised ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != 0u) return STA_NOINIT;
    /* Nothing to bring up: the flash is already running, it is what this CPU
     * is executing from. */
    s_initialised = true;
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0u || buff == NULL) return RES_PARERR;
    if (!s_initialised) return RES_NOTRDY;
    if (!fwog_fs_range_valid((uint32_t)sector, (uint32_t)count)) {
        DIAG("[fs] read out of range: lba %u count %u (volume is %u sectors)\n",
             (unsigned)sector, (unsigned)count, (unsigned)FWOG_FS_SECTOR_COUNT);
        return RES_PARERR;
    }

    /* Reads go through XIP, so this is a plain memcpy from the memory-mapped
     * window -- no command mode, no interrupt masking, no watchdog concern. */
    const uint8_t *src = (const uint8_t *)(XIP_BASE +
                                           fwog_fs_lba_offset((uint32_t)sector));
    memcpy(buff, src, (size_t)count * FWOG_FS_SECTOR_SIZE);
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0u || buff == NULL) return RES_PARERR;
    if (!s_initialised) return RES_NOTRDY;
    if (!fwog_fs_range_valid((uint32_t)sector, (uint32_t)count)) {
        DIAG("[fs] write out of range: lba %u count %u (volume is %u sectors)"
             "\n", (unsigned)sector, (unsigned)count,
             (unsigned)FWOG_FS_SECTOR_COUNT);
        return RES_PARERR;
    }

    /* Hazard 2. Widen the window for the whole run, then kick between chunks
     * -- widening alone is not enough, because 8300 ms is the RP2040's
     * hardware maximum and a long run of 448 ms worst-case sectors exceeds it.
     * See fs_geom.h for where FWOG_FS_ERASE_CHUNK_SECTORS comes from. */
    board_watchdog_pause();

    uint32_t done = 0u;
    uint32_t chunk;
    while ((chunk = fwog_fs_erase_chunk((uint32_t)count, done)) != 0u) {
        for (uint32_t i = 0u; i < chunk; i++) {
            const uint32_t lba = (uint32_t)sector + done + i;
            fs_write_one_sector(fwog_fs_lba_offset(lba),
                                buff + (size_t)(done + i) * FWOG_FS_SECTOR_SIZE);
        }
        done += chunk;
        /* Kick AFTER the chunk, so the next chunk starts with a full window
         * rather than with whatever was left of the previous one. */
        board_watchdog_kick();
    }

    board_watchdog_resume();
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv != 0u) return RES_PARERR;
    if (!s_initialised) return RES_NOTRDY;

    switch (cmd) {
    case CTRL_SYNC:
        /* Every write is synchronous already: fs_write_one_sector() does not
         * return until flash_range_program() has completed. Nothing to flush. */
        return RES_OK;
    case GET_SECTOR_COUNT:
        if (buff == NULL) return RES_PARERR;
        *(LBA_t *)buff = (LBA_t)FWOG_FS_SECTOR_COUNT;
        return RES_OK;
    case GET_SECTOR_SIZE:
        if (buff == NULL) return RES_PARERR;
        *(WORD *)buff = (WORD)FWOG_FS_SECTOR_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE:
        if (buff == NULL) return RES_PARERR;
        /* In units of SECTORS, not bytes. One erase block is one 4 KB sector
         * here, so the answer is 1 -- the reference returns 1 too. Returning
         * 4096 (the byte size) is the classic mistake and makes FatFs align
         * allocations to 16 MB. */
        *(DWORD *)buff = 1u;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

/* ---- Hazard 3: there is no clock on this CPU ----
 *
 * The MCP7940 RTC is on the DISPLAY CPU's I2C1 at 0x6F. Main has no local time
 * source, so every file gets the same fixed date rather than a plausible wrong
 * one or a garbage stack value. See fwog_fs.h hazard 3 for the follow-up that
 * would fix it properly.
 *
 * FatFs packs: bits 31-25 year-1980, 24-21 month, 20-16 day, 15-11 hour,
 * 10-5 minute, 4-0 second/2. This is 2026-01-01 00:00:00. */
DWORD get_fattime(void) {
    return ((DWORD)(2026 - 1980) << 25)
         | ((DWORD)1 << 21)
         | ((DWORD)1 << 16);
}

#endif /* HOST_TEST */
