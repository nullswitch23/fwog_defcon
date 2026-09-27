/* On-media geometry for the main CPU's FatFs volume, and the chunked-erase
 * splitter that keeps a long write inside the watchdog window.
 *
 * Pure arithmetic: no SDK, no flash, no FatFs. Host-tested
 * (tests/test_fs_geom.c), because getting an LBA-to-address mapping wrong
 * writes into someone else's flash and the symptom is arbitrary.
 *
 * ---- The geometry is TRANSCRIBED, not chosen ----
 *
 * From freewilimain/fatfsLib/diskio.c, the WILI_ORIGINAL branch:
 *
 *     #define FLASH_BASE_ADDRESS  ((1024*1024)*8)          // 8 MB into flash
 *     #define NUM_SECTORS         ((1024*1024)*8) / 4096   // 2048 sectors
 *     #define FLASH_SECTORSIZE    4096
 *
 * 8 MB in, 8 MB long, 4096-byte sectors, on the 16 MB W25Q128JVPIQ this board
 * carries (the hardware record; PICO_FLASH_SIZE_BYTES in bsp/boards/freewili_og.h).
 *
 * Matching it exactly is the entire reason FatFs was chosen over LittleFS.
 * LittleFS is the better fit for raw NOR on the merits -- power-fail safety and
 * wear levelling, neither of which this has -- and was rejected for exactly one
 * reason: it is on-media incompatible, so a board could hold one filesystem or
 * the other, never both. Matching the original's layout is what lets one board
 * hold files written by either firmware. Change any constant here and that
 * compatibility is silently gone; the volume will still mount under this
 * firmware and be unreadable to the other.
 *
 * ---- What this does NOT buy ----
 * FAT on raw NOR is not crash-safe. A power cut mid-write can corrupt the
 * volume, and no amount of testing changes that. It is the cost of the
 * compatibility decision above, recorded here next to the decision rather than
 * discovered later. There is also no wear levelling of any kind.
 */
#ifndef FWOG_FS_GEOM_H
#define FWOG_FS_GEOM_H
#include <stdbool.h>
#include <stdint.h>

/* Byte offset of the volume from the start of flash -- NOT an XIP address.
 * Callers that need to READ through XIP add XIP_BASE; callers that erase or
 * program pass this straight to flash_range_*, which take flash offsets. That
 * distinction is the single easiest thing to get wrong here, which is why the
 * two are never mixed in one type. */
#define FWOG_FS_FLASH_OFFSET   (8u * 1024u * 1024u)
#define FWOG_FS_SECTOR_SIZE    4096u
#define FWOG_FS_VOLUME_BYTES   (8u * 1024u * 1024u)
#define FWOG_FS_SECTOR_COUNT   (FWOG_FS_VOLUME_BYTES / FWOG_FS_SECTOR_SIZE)

/* ---- Chunked erase, so a long write cannot outlive the watchdog ----
 *
 * Main's watchdog is the ONLY recovery path this CPU has (the hardware record), and its
 * window is FWOG_WATCHDOG_MS = 8300 ms -- the RP2040's hardware maximum.
 * board_watchdog_pause() therefore widens NOTHING: there is nothing left to
 * widen to. Kicking partway through a multi-sector write is not a belt-and-
 * braces measure alongside the widening, it is the ONLY thing standing
 * between a long write and a reset. This constant is how far the write path
 * may go between kicks.
 *
 * DERIVATION, not a guess. The part is a W25Q128JVPIQ. Its datasheet gives 4 KB
 * sector erase (20h) as 45 ms typical and 400 ms MAXIMUM; page program (256 B)
 * is 0.4 ms typical, 3 ms maximum, so a full 4096-byte sector programs in at
 * most 16 * 3 = 48 ms. Worst case per sector is therefore about 448 ms.
 *
 *     8 sectors * 448 ms = 3.58 s, which is 43% of the 8300 ms window.
 *
 * That leaves better than 2x margin against the datasheet's own maximum, not
 * against its typical -- which matters, because the typical figure (45 ms) is
 * nine times faster and a chunk sized against it would fit 8300/93 = 89
 * sectors and then reset the board on the first slow erase near end-of-life.
 *
 * If this is ever ported to a slower part, shrink this. The failure mode of
 * getting it wrong is a watchdog reboot in the middle of a file write, which
 * on FAT means a corrupt volume. */
#define FWOG_FS_ERASE_CHUNK_SECTORS 8u

/* True if `lba` addresses a sector inside the volume. */
bool fwog_fs_lba_valid(uint32_t lba);

/* True if the whole run [lba, lba+count) is inside the volume. Rejects
 * count == 0, and is overflow-safe: `lba + count` is never computed, because
 * a caller passing a near-UINT32_MAX lba would wrap it back into range and the
 * check would pass for a write that lands anywhere. */
bool fwog_fs_range_valid(uint32_t lba, uint32_t count);

/* Byte offset of `lba` from the start of FLASH (add XIP_BASE to read).
 * Returns 0 for an invalid lba -- callers must have checked
 * fwog_fs_lba_valid() first; 0 is the offset of the volume's own first sector
 * and is deliberately NOT a usable sentinel, so this is not a check. */
uint32_t fwog_fs_lba_offset(uint32_t lba);

/* How many sectors the next chunk should cover, given `total` sectors to do
 * and `done` already finished. Returns 0 when there is nothing left, which is
 * the loop's termination condition.
 *
 * The contract the test pins: iterating this covers [0, total) exactly once,
 * with no gap and no overlap, for totals that do and do not divide evenly by
 * FWOG_FS_ERASE_CHUNK_SECTORS. */
uint32_t fwog_fs_erase_chunk(uint32_t total, uint32_t done);

#endif
