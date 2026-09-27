/* The record that lets a host tool describe a UF2 without being told.
 *
 * ---- Why this is a SCANNED record and not a pointer table ----
 *
 * The consumer is fwOGappexplorer, which already parses UF2 blocks
 * (src/catalog/fwUf2Header.cpp) and must work on a FILE, offline, before
 * anything reaches a board. So every string is a fixed inline array: a
 * consumer must never have to resolve an absolute XIP address across sparse
 * UF2 blocks. Display apps link at 0x10021000 rather than the usual base,
 * which is exactly the detail a pointer-chasing parser gets wrong.
 *
 * The Pico SDK's own binary_info was the alternative and was rejected for
 * that reason: it stores string POINTERS, and picotool is not a dependency
 * the App Explorer has.
 *
 * The magic is EIGHT bytes, not four. The consumer scans a raw payload, and
 * four bytes hit by accident in 16 MB of flash where eight do not.
 *
 * ---- A MAIN UF2 CONTAINS TWO OF THESE ----
 *
 * Main embeds the display image, so a scan of a main UF2 finds one
 * cpu=FWOG_UF2_CPU_MAIN record (the main app's own) and one
 * cpu=FWOG_UF2_CPU_DISPLAY record (belonging to the image it carries). That
 * is deliberate and is more than the App Explorer's hand-maintained
 * manifest.json could express -- it is how a host reports "main app X,
 * carrying display image Y". Consumers key on `cpu` and must not assume a
 * single hit.
 *
 * Pure -- no SDK, no hardware. Shared by both CPUs, the generated record and
 * the host tests, exactly like app_meta.h. */
#ifndef FWOG_UF2_INFO_H
#define FWOG_UF2_INFO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The eight magic bytes read "FWGOINFO" in flash on a little-endian core. */
#define FWOG_UF2_INFO_MAGIC0 0x4F475746u   /* "FWGO" */
#define FWOG_UF2_INFO_MAGIC1 0x4F464E49u   /* "INFO" */

#define FWOG_UF2_INFO_VERSION 1u

#define FWOG_UF2_CPU_DISPLAY 0u
#define FWOG_UF2_CPU_MAIN    1u

#define FWOG_UF2_KIND_APP        0u
#define FWOG_UF2_KIND_BOOTLOADER 1u

#define FWOG_UF2_NAME_LEN  32u
#define FWOG_UF2_DESC_LEN  128u
#define FWOG_UF2_BUILD_LEN 32u

typedef struct {
    uint32_t magic0;
    uint32_t magic1;
    uint16_t struct_version;
    uint8_t  cpu;               /* FWOG_UF2_CPU_*  */
    uint8_t  kind;              /* FWOG_UF2_KIND_* */
    uint16_t app_version;       /* 0..999, the declared 3-digit version */
    uint16_t reserved;          /* 0 */
    char     name[FWOG_UF2_NAME_LEN];
    char     description[FWOG_UF2_DESC_LEN];
    /* EMPTY on display apps, and that is REQUIRED, not an oversight.
     *
     * display_update.c decides whether to transfer the display image at all
     * by comparing its CRC32 against what the display reports:
     *
     *     return h->app_crc32 != info->crc32 || h->app_size != info->size;
     *
     * so any build-varying byte inside a display app changes that CRC and
     * forces a full re-transfer and display reflash on EVERY commit, with no
     * code change. The top-level CMakeLists.txt already refuses to compile
     * FWOG_GIT_DESCRIBE into display applications for precisely this reason;
     * this field keeps that promise rather than quietly breaking it.
     *
     * Main apps and the bootloader do carry them: both are flashed by UF2 and
     * neither has a CRC-skip path. build_ts is the git COMMIT timestamp, never
     * wall-clock -- a configure-time clock would make two builds of identical
     * source differ for nothing. Both are zero outside a repository. */
    char     build[FWOG_UF2_BUILD_LEN];
    uint32_t build_ts;
    uint32_t crc32;             /* over every preceding byte */
} fwog_uf2_info_t;

/* Same reasoning as app_meta.h: every member is naturally aligned and the
 * trailing uint32_t needs no padding, so the layout is identical on the host
 * and on Cortex-M0+. A compiler that disagrees becomes a build break rather
 * than a record the App Explorer silently fails to parse. */
_Static_assert(sizeof(fwog_uf2_info_t) == 216,
               "fwog_uf2_info_t layout changed -- fwOGappexplorer parses this");
_Static_assert(offsetof(fwog_uf2_info_t, crc32) == 212,
               "crc32 must be last: the CRC covers everything before it");

/* CRC-32/IEEE over every byte before `crc32`. */
uint32_t fwog_uf2_info_crc(const fwog_uf2_info_t *info);

/* Magic present, struct_version understood, CRC correct, and all three
 * strings NUL-terminated within their arrays.
 *
 * Records arrive from a scanned binary, so nothing here is trusted -- the
 * termination checks are what make the strings safe to pass to a %s, the same
 * guarantee fwog_str_bounded() provides for the app-slot metadata. */
bool fwog_uf2_info_valid(const fwog_uf2_info_t *info);

#endif
