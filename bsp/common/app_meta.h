/* Display-CPU flash layout and the app-slot metadata record.
 *
 * THE THREE OFFSETS BELOW ARE FROZEN. A bootloader flashed onto a board
 * cannot be told a new app offset, and the bootloader is the one component
 * on this board that needs physical BOOTSEL access to replace. Everything
 * else in the update path is negotiable; these are not.
 *
 * Pure -- no SDK, no hardware. Shared by the bootloader, the main-side
 * updater, and the host tests. */
#ifndef FWOG_APP_META_H
#define FWOG_APP_META_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "common/crc.h"

/*  0x10000000  bootloader     128 KB   flashed once by UF2; rarely changes
 *  0x10020000  app metadata     4 KB   one erase sector, alone
 *  0x10021000  app image        rest   ordinary SDK binary (boot2 + vectors)
 *
 * The offsets are flash-relative, which is what hardware_flash's
 * flash_range_erase()/flash_range_program() take. FWOG_APP_XIP_ADDR is the
 * same location as seen by the CPU. */
#define FWOG_BL_MAX_SIZE       0x20000u
#define FWOG_APP_META_OFFSET   0x20000u
#define FWOG_APP_META_SIZE     0x1000u
#define FWOG_APP_OFFSET        0x21000u
#define FWOG_XIP_BASE          0x10000000u
#define FWOG_APP_XIP_ADDR      (FWOG_XIP_BASE + FWOG_APP_OFFSET)
#define FWOG_FLASH_SIZE        (16u * 1024u * 1024u)
#define FWOG_APP_MAX_SIZE      (FWOG_FLASH_SIZE - FWOG_APP_OFFSET)

/* The metadata sector is erased at the START of an update and written only
 * after the image verifies, so an interruption leaves magic reading
 * 0xFFFFFFFF and the bootloader knows not to trust the app. That is
 * crash-safety with no state machine. */
#define FWOG_APP_META_MAGIC    0x4F474150u  /* "PAGO": app present and good */
#define FWOG_APP_VERSION_LEN   32u

typedef struct {
    uint32_t magic;         /* FWOG_APP_META_MAGIC once valid            */
    uint32_t size;          /* image bytes                               */
    uint32_t crc32;         /* over the image only, never the metadata   */
    uint32_t build_ts;      /* unix seconds, supplied by main            */
    char     version[FWOG_APP_VERSION_LEN];  /* human-readable, from main */
    uint32_t meta_crc32;    /* over the preceding fields                 */
} fwog_app_meta_t;

/* Every member is naturally aligned and the trailing uint32_t needs no
 * padding, so this layout is identical on the host and on Cortex-M0+. The
 * asserts make a compiler that disagrees a build break rather than a record
 * the bootloader silently rejects. */
_Static_assert(sizeof(fwog_app_meta_t) == 52,
               "fwog_app_meta_t layout changed -- flash records would not read back");
_Static_assert(offsetof(fwog_app_meta_t, meta_crc32) == 48,
               "meta_crc32 must be last: the CRC covers everything before it");

/* True when s[0..n-1] contains a NUL, so s is safe to pass to a %s. Version
 * strings arrive from flash and from the wire; neither is trusted. */
static inline bool fwog_str_bounded(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\0') return true;
    }
    return false;
}

/* CRC32 over every field before meta_crc32. */
uint32_t fwog_app_meta_crc(const fwog_app_meta_t *m);

/* Magic, plausible size, terminated version, and matching meta_crc32. */
bool fwog_app_meta_valid(const fwog_app_meta_t *m);

/* Populate a record ready to be written to flash. `version` is truncated to
 * fit and always terminated. */
void fwog_app_meta_fill(fwog_app_meta_t *m, uint32_t size, uint32_t crc32,
                        uint32_t build_ts, const char *version);

#endif
