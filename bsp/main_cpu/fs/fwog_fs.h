/* The main CPU's filesystem: FatFs R0.15 over the last 8 MB of flash.
 *
 * A thin C surface over the useful half of the reference's `rpFileSystem`,
 * named for what it does rather than transcribed shape-for-shape. The on-media
 * geometry is transcribed exactly, and that is the point -- see fs/fs_geom.h
 * for the layout and for why FatFs was chosen over LittleFS despite LittleFS
 * being the better filesystem for raw NOR.
 *
 * ---- FatFs is this BSP's FIRST third-party dependency ----
 * Vendored under fs/ff/ from the reference's own fatfsLib/ (R0.15,
 * FFCONF_DEF 80286) rather than fetched, so the two cannot drift across a
 * revision boundary and change the on-media format out from under a board that
 * has files on it. See fs/ff/README.md for exactly what was copied and the one
 * configuration change made.
 *
 * ================= THREE HAZARDS. READ BEFORE CALLING ANY WRITE. ============
 *
 * ---- Hazard 1: writes require core1 to be idle. THIS IS A PRECONDITION. ----
 *
 * flash_range_erase()/flash_range_program() put the QSPI interface into a
 * command mode in which XIP READS DO NOT WORK. Any code executing from flash
 * during that window faults. This driver handles two of the three
 * requirements itself -- interrupts are disabled across the operation, and the
 * function performing it is resident in RAM (__not_in_flash_func) -- but it
 * CANNOT handle the third:
 *
 *     THE OTHER CORE MUST NOT BE EXECUTING FROM FLASH.
 *
 * The reference does handle it, with WiliGlobLockFlash()/WiliGlobUnlockFlash()
 * around every write (FreeWilliMain.cpp:520-525, delegating to an obFlashProtect
 * lock). That it exists there is evidence the hazard is real on this product,
 * not theoretical.
 *
 * This BSP takes the simpler position DELIBERATELY: no main-CPU application
 * launches core1 today, so every write function below has the documented
 * precondition "core1 is idle", and there is no multicore lockout. Building
 * the reference's lock now would be carrying real complexity for a core that
 * does not run.
 *
 * IF YOU ADD A CORE1 WORKLOAD TO A MAIN-CPU APP, THIS BREAKS, and it breaks as
 * a hard fault on core1 at an unpredictable point -- not as a returned error.
 * The fix is multicore_lockout_victim_init() on core1 plus
 * multicore_lockout_start/end_blocking() around the writes here. That is a
 * real, small piece of work, and it is the responsibility of whoever starts
 * core1, which is why this paragraph names it concretely instead of saying
 * "not supported".
 *
 * ---- Hazard 2: the watchdog will fire during a long write ----
 *
 * Main's watchdog window is FWOG_WATCHDOG_MS (8300 ms, the RP2040 hardware
 * maximum) and it is the ONLY recovery path this CPU has (the hardware record). A
 * multi-sector write erases a 4 KB sector per sector written, at up to 400 ms
 * each on this part, so a 21-sector write can exceed even that window.
 *
 * Every write path below wraps itself in board_watchdog_pause()/resume() AND
 * kicks partway through, every FWOG_FS_ERASE_CHUNK_SECTORS sectors. The
 * pause/resume pair widens nothing now that the base window is already the
 * maximum -- the kicking is what actually protects the write; see
 * fs_geom.h for the derivation of the chunk size from the W25Q128JVPIQ's
 * datasheet maximums.
 *
 * The nesting arithmetic in board_watchdog_pause()/resume() is already
 * host-tested (fwog_wdt_pause_step()/fwog_wdt_resume_step()), so this package
 * reuses it and invents nothing.
 *
 * ---- Hazard 3: there is no clock, so every file gets the same timestamp ----
 *
 * FatFs asks the application for wall-clock time via get_fattime(). THE RTC IS
 * ON THE DISPLAY CPU -- MCP7940 at 0x6F on the display's I2C1. Main has no
 * local time source at all.
 *
 * v1 therefore returns a FIXED timestamp: 2026-01-01 00:00:00, a constant
 * epoch. Every file gets the same date and nothing pretends to know better. A
 * fixed timestamp is honest; a plausible-looking wrong one is not, and a
 * garbage stack value would be worse than either.
 *
 * The known follow-up, stated here rather than left as a surprise: wire the
 * MCP7940 across the inter-CPU link and have get_fattime() read it. That is a
 * clean separate increment -- it needs a request/response on the existing link
 * and a cache, because get_fattime() is called from inside FatFs at times when
 * a multi-millisecond blocking round trip would be unwelcome.
 *
 * ===========================================================================
 *
 * ---- ONE OPEN FILE AT A TIME ----
 * A deliberate RAM decision, not an oversight, and it matches the reference's
 * single m_iFileIndex convention. With this configuration (FF_FS_TINY 0,
 * FF_MAX_SS 4096) a FIL object carries its own 4096-byte sector buffer and a
 * FATFS object carries another, so the filesystem's static footprint is about
 * 8 KB. Allowing N open files would multiply the FIL half by N. If a caller
 * needs two files open at once, that is a design change with a RAM budget
 * attached, not a bigger array.
 *
 * ---- Deliberately NOT ported from rpFileSystem ----
 * printDirectory / printFile / printCurrentDirectory (console formatting),
 * loadDirectoryToLogList / buildDirectoryCache (log-list and menu-system glue),
 * and the SD-card state machine -- the OG has no SD card, which is also why
 * FF_VOLUMES dropped from 2 to 1.
 *
 * getFileFlashPointer() is also not ported, for a sharper reason than "UI
 * layer": it returns a raw XIP pointer into a file, which is only sound if the
 * file is contiguous. FAT files are not contiguous in general, so that pointer
 * is USUALLY right, which is the worst kind of wrong. If something later needs
 * memory-mapped file access it deserves its own design with the contiguity
 * precondition stated and enforced -- fwog_fs_preallocate() below is the piece
 * that would make it achievable.
 */
#ifndef FWOG_FS_H
#define FWOG_FS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- Volume ---- */

/* Mount the volume. Does NOT format on failure -- a failed mount reports
 * failure and nothing else.
 *
 * That is a deliberate refusal: auto-formatting to recover from a mount error
 * would destroy a user's files to work around what might be a transient read.
 * The reference agrees, in its own way -- its
 * `// FOR TESTING obFileSystem.formatFlash();` sits commented out immediately
 * above its mount(). */
bool fwog_fs_mount(void);
bool fwog_fs_unmount(void);
bool fwog_fs_mounted(void);

/* Create a new filesystem. DESTRUCTIVE, and named `format` rather than `init`
 * so that no caller reaches it by accident.
 *
 * Emits a DIAG() line BEFORE it starts, because a multi-second silent stall on
 * a board whose only recovery is the watchdog looks exactly like a hang.
 *
 * ---- What "format" does and does not do ----
 * This is f_mkfs: it writes a fresh boot sector, FATs and root directory. It
 * does NOT erase the whole 8 MB volume, so old file CONTENT remains on the
 * media until the sectors are reused. It takes seconds, not the tens of
 * seconds a full erase would.
 *
 * The P5 spec describes this as "erases 8 MB and takes tens of seconds". That
 * is not what f_mkfs does, and the difference matters twice: a caller
 * expecting a multi-minute operation is surprised by a fast one, and -- more
 * importantly -- anyone treating this as a way to sanitise a board before
 * handing it on is wrong. If sanitising is ever wanted it is a separate
 * function that walks every sector, and it needs the chunked-erase treatment
 * for 2048 sectors rather than for the handful this touches.
 *
 * PRECONDITION: core1 idle. See hazard 1. */
bool fwog_fs_format(void);

/* Free and total bytes. Both outputs may be NULL. */
bool fwog_fs_volume_info(uint64_t *free_bytes, uint64_t *total_bytes);

/* ---- Paths ---- */

bool fwog_fs_exists(const char *path);
bool fwog_fs_remove(const char *path);   /* PRECONDITION: core1 idle */
bool fwog_fs_rename(const char *from, const char *to);  /* core1 idle */
bool fwog_fs_mkdir(const char *path);    /* PRECONDITION: core1 idle */

/* ---- The one open file ----
 *
 * Opening while a file is already open FAILS rather than silently closing the
 * first one. A silent close loses buffered writes, and losing them at the
 * moment a caller made a mistake is the worst time to be quiet about it. */

bool fwog_fs_open(const char *path, bool for_writing, bool append);
bool fwog_fs_close(void);                /* PRECONDITION: core1 idle */
bool fwog_fs_is_open(void);

/* Read up to *len bytes; *len receives the count actually read. A short read
 * is success, not failure -- check *len against what you asked for to detect
 * end of file. */
bool fwog_fs_read(void *buf, size_t *len);

/* Write exactly `len` bytes. A short write is reported as FAILURE (unlike
 * read), because the only common cause is a full volume and a caller that
 * treats a partial write as success writes a truncated file and never learns.
 * PRECONDITION: core1 idle. */
bool fwog_fs_write(const void *buf, size_t len);

bool fwog_fs_seek(uint32_t pos);         /* PRECONDITION: core1 idle */
uint32_t fwog_fs_tell(void);
uint32_t fwog_fs_size(void);

/* Reserve `bytes` of CONTIGUOUS space for the currently open file (f_expand),
 * ported from preAllocateSpaceForFile() because it is cheap and it is what
 * makes contiguity achievable at all. PRECONDITION: core1 idle. */
bool fwog_fs_preallocate(uint32_t bytes);

/* ---- Directory enumeration by index ----
 *
 * Index-based rather than an opendir/readdir handle, mirroring the reference's
 * getDirectoryItemByIndex(), so a caller with no allocator and no handle to
 * keep can walk a directory. It is O(n) per call and therefore O(n^2) to walk
 * a whole directory -- fine for the tens of entries this board's directories
 * hold, and worth knowing before someone points it at a thousand.
 *
 * The scan is capped (FatFs MAX_DIR / 32-byte slots) and kicks the watchdog
 * every 16 readdirs on the device build: a circular chain or a directory of
 * deleted slots must not eat the 8.3 s window.
 *
 * `name_out` receives a NUL-terminated name truncated to `name_max`. Returns
 * false when `index` is past the end, which is the loop's termination
 * condition. */
bool fwog_fs_dir_entry(const char *dir, unsigned index,
                       char *name_out, size_t name_max, bool *is_dir);

#endif
