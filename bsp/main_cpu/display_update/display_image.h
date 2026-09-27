/* The display-CPU application embedded in this main-CPU binary.
 *
 * tools/genimage.py generates the two sources that define these symbols:
 * an .S that .incbins the image bytes into flash, and a .c holding this
 * identity record. The declarations live here, hand-written, so the
 * contract is reviewable and the generator has a fixed shape to fill.
 *
 * The blob is in flash and costs no RAM -- main streams it straight out of
 * XIP to the display over the link. */
#ifndef FWOG_DISPLAY_IMAGE_H
#define FWOG_DISPLAY_IMAGE_H
#include <stdint.h>
#include "common/app_meta.h"

typedef struct {
    uint32_t size;
    uint32_t crc32;      /* CRC-32/IEEE over the image bytes, nothing else */
    uint32_t build_ts;   /* unix seconds; the .bin's mtime, not wall clock */
    char     version[FWOG_APP_VERSION_LEN];
} fwog_display_image_info_t;

/* The image bytes. `_end` exists purely so the updater can check the
 * linked length against what the identity record claims -- a stale .bin
 * would otherwise be caught only by the display's CRC verify, seconds into
 * a pointless transfer. */
extern const uint8_t fwog_display_image[];
extern const uint8_t fwog_display_image_end[];
extern const fwog_display_image_info_t fwog_display_image_info;

#endif
