/* Public filesystem API over FatFs -- see fwog_fs.h for the three hazards and
 * the one-open-file decision. */
#include "fs/fwog_fs.h"
#include "fs/fs_geom.h"
#include "fs/ff/ff.h"
#include "common/diag.h"
#ifndef HOST_TEST
#include "watchdog/watchdog.h"
#endif
#include <string.h>

/* About 8 KB between them with this configuration (FF_FS_TINY 0,
 * FF_MAX_SS 4096): each carries its own sector buffer. See the header's
 * one-open-file note -- this is the RAM decision, made once, here. */
static FATFS s_fs;
static FIL   s_file;
static bool  s_mounted;
static bool  s_open;

/* Drive 0 is the only volume; FF_VOLUMES is 1 (the reference's drive 1 was an
 * SD card the OG does not have). An empty path means "the default drive",
 * which is drive 0, so callers never write a "0:" prefix. */
#define FWOG_FS_DRIVE ""

bool fwog_fs_mounted(void) { return s_mounted; }
bool fwog_fs_is_open(void) { return s_open; }

bool fwog_fs_mount(void) {
    if (s_mounted) return true;
    /* opt=1: mount now rather than deferring to the first access, so a failure
     * is reported HERE instead of surfacing later as a confusing file error. */
    const FRESULT r = f_mount(&s_fs, FWOG_FS_DRIVE, 1);
    if (r != FR_OK) {
        DIAG("[fs] mount failed (%d) -- NOT auto-formatting; use 'fs format' "
             "if the volume is genuinely new\n", (int)r);
        return false;
    }
    s_mounted = true;
    return true;
}

bool fwog_fs_unmount(void) {
    if (!s_mounted) return true;
    if (s_open) (void)fwog_fs_close();
    const FRESULT r = f_mount(NULL, FWOG_FS_DRIVE, 0);
    s_mounted = false;
    return r == FR_OK;
}

bool fwog_fs_format(void) {
    if (s_open) { DIAG("[fs] format refused: a file is open\n"); return false; }

    /* Before, not after: on success this returns having rewritten the
     * volume, and on a board whose only recovery is the watchdog a silent
     * multi-second stall is indistinguishable from a hang. */
    DIAG("[fs] FORMATTING %u KB volume at flash offset 0x%08x -- this "
         "DESTROYS the file table (file data is not erased); expect several "
         "seconds\n",
         (unsigned)(FWOG_FS_VOLUME_BYTES / 1024u),
         (unsigned)FWOG_FS_FLASH_OFFSET);

    /* f_mkfs needs a work buffer of at least FF_MAX_SS. Static, not stack:
     * 4 KB against the SDK's default 2 KB stack would smash it. */
    static BYTE work[FF_MAX_SS];
    MKFS_PARM parm = {0};
    parm.fmt = FM_ANY;      /* FAT12/16/32 as the size dictates; exFAT is off */
    parm.n_fat = 1;
    parm.align = 1;
    parm.n_root = 0;        /* let FatFs choose the root directory size */
    parm.au_size = 0;       /* and the cluster size */

    const bool was_mounted = s_mounted;
    if (was_mounted) (void)fwog_fs_unmount();

    const FRESULT r = f_mkfs(FWOG_FS_DRIVE, &parm, work, sizeof(work));
    if (r != FR_OK) {
        DIAG("[fs] format failed (%d)\n", (int)r);
        return false;
    }
    DIAG("[fs] format complete\n");
    return fwog_fs_mount();
}

bool fwog_fs_volume_info(uint64_t *free_bytes, uint64_t *total_bytes) {
    if (!s_mounted) return false;
    FATFS *fs = NULL;
    DWORD free_clusters = 0u;
#ifndef HOST_TEST
    /* First f_getfree after mount may scan the FAT. Kick so a dirty volume
     * cannot eat the whole 8.3 s window before the caller gets a turn. */
    board_watchdog_kick();
#endif
    if (f_getfree(FWOG_FS_DRIVE, &free_clusters, &fs) != FR_OK || fs == NULL)
        return false;
#ifndef HOST_TEST
    board_watchdog_kick();
#endif

    /* (n_fatent - 2) is the cluster count; csize is sectors per cluster.
     * Widened to 64-bit before multiplying: 2048 sectors of 4096 bytes is only
     * 8 MB and fits in 32 bits, but the expression is the same one a larger
     * volume would use and a silent overflow here reports a plausible wrong
     * size, which is worse than an obvious one. */
    const uint64_t sector_bytes = (uint64_t)FWOG_FS_SECTOR_SIZE;
    const uint64_t total_clusters = (uint64_t)fs->n_fatent - 2u;
    if (total_bytes) *total_bytes = total_clusters * fs->csize * sector_bytes;
    if (free_bytes)  *free_bytes  = (uint64_t)free_clusters * fs->csize * sector_bytes;
    return true;
}

bool fwog_fs_exists(const char *path) {
    if (!s_mounted || path == NULL) return false;
    FILINFO fno;
    return f_stat(path, &fno) == FR_OK;
}

bool fwog_fs_remove(const char *path) {
    if (!s_mounted || path == NULL) return false;
    return f_unlink(path) == FR_OK;
}

bool fwog_fs_rename(const char *from, const char *to) {
    if (!s_mounted || from == NULL || to == NULL) return false;
    return f_rename(from, to) == FR_OK;
}

bool fwog_fs_mkdir(const char *path) {
    if (!s_mounted || path == NULL) return false;
    return f_mkdir(path) == FR_OK;
}

bool fwog_fs_open(const char *path, bool for_writing, bool append) {
    if (!s_mounted || path == NULL) return false;
    /* Refuse rather than silently closing the previous file: a silent close
     * discards buffered writes, and the moment a caller has made a mistake is
     * the worst possible time to be quiet about losing their data. */
    if (s_open) {
        DIAG("[fs] open refused: a file is already open (one at a time)\n");
        return false;
    }

    BYTE mode;
    if (!for_writing) {
        mode = FA_READ;
    } else if (append) {
        mode = FA_WRITE | FA_OPEN_APPEND;   /* creates, and seeks to the end */
    } else {
        mode = FA_WRITE | FA_CREATE_ALWAYS; /* creates, and TRUNCATES */
    }

    if (f_open(&s_file, path, mode) != FR_OK) return false;
    s_open = true;
    return true;
}

bool fwog_fs_close(void) {
    if (!s_open) return true;
    const FRESULT r = f_close(&s_file);
    /* Cleared even on failure: leaving s_open true after a failed close means
     * no subsequent open can ever succeed, and the FIL is not reusable either
     * way. */
    s_open = false;
    return r == FR_OK;
}

bool fwog_fs_read(void *buf, size_t *len) {
    if (!s_open || buf == NULL || len == NULL) return false;
    UINT got = 0u;
    const FRESULT r = f_read(&s_file, buf, (UINT)*len, &got);
    *len = (size_t)got;
    /* A short read is SUCCESS -- it is how end-of-file presents. Callers
     * compare *len against what they asked for. */
    return r == FR_OK;
}

bool fwog_fs_write(const void *buf, size_t len) {
    if (!s_open || buf == NULL) return false;
    if (len == 0u) return true;
    UINT put = 0u;
    const FRESULT r = f_write(&s_file, buf, (UINT)len, &put);
    if (r != FR_OK) return false;
    /* Unlike read, a short write is FAILURE. Its only common cause is a full
     * volume, and a caller that treats it as success writes a truncated file
     * and never finds out. */
    if (put != (UINT)len) {
        DIAG("[fs] short write: %u of %u bytes (volume full?)\n",
             (unsigned)put, (unsigned)len);
        return false;
    }
    return true;
}

bool fwog_fs_seek(uint32_t pos) {
    if (!s_open) return false;
    return f_lseek(&s_file, (FSIZE_t)pos) == FR_OK;
}

uint32_t fwog_fs_tell(void) {
    if (!s_open) return 0u;
    return (uint32_t)f_tell(&s_file);
}

uint32_t fwog_fs_size(void) {
    if (!s_open) return 0u;
    return (uint32_t)f_size(&s_file);
}

bool fwog_fs_preallocate(uint32_t bytes) {
    if (!s_open) return false;
    /* opt=1: allocate now and set the file size, rather than only preparing a
     * contiguous region. That is what makes the space actually reserved --
     * opt=0 leaves it available for the next f_expand to take. */
    return f_expand(&s_file, (FSIZE_t)bytes, 1) == FR_OK;
}

bool fwog_fs_dir_entry(const char *dir, unsigned index,
                       char *name_out, size_t name_max, bool *is_dir) {
    if (!s_mounted || dir == NULL || name_out == NULL || name_max == 0u)
        return false;

    DIR d;
    if (f_opendir(&d, dir) != FR_OK) return false;

    /* FatFs dir_next() itself stops at MAX_DIR (2 MB of slots). Cap here too
     * so a circular cluster chain cannot spin forever, and kick: a directory
     * of deleted slots still costs a readdir per slot and this is O(index)
     * from the start on every call. */
    enum { FWOG_FS_DIR_SCAN_MAX = 65536u };

    bool found = false;
    for (unsigned i = 0u, scanned = 0u; scanned < FWOG_FS_DIR_SCAN_MAX; scanned++) {
        FILINFO fno;
#ifndef HOST_TEST
        if ((scanned & 15u) == 0u) board_watchdog_kick();
#endif
        if (f_readdir(&d, &fno) != FR_OK) break;
        if (fno.fname[0] == '\0') break;      /* end of directory */
        if (i != index) {
            i++;
            continue;
        }

        /* strncpy would not NUL-terminate on truncation. Copy explicitly. */
        size_t n = strlen(fno.fname);
        if (n > name_max - 1u) n = name_max - 1u;
        memcpy(name_out, fno.fname, n);
        name_out[n] = '\0';
        if (is_dir) *is_dir = (fno.fattrib & AM_DIR) != 0;
        found = true;
        break;
    }

    (void)f_closedir(&d);
    return found;
}
