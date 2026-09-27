#include "bootloader/bl_flash.h"
#include <string.h>

/* The reserve guard below rejects everything under FWOG_BL_MAX_SIZE, and the
 * update's LAST act is programming the metadata record at
 * FWOG_APP_META_OFFSET. If those constants ever drift apart, this file
 * still compiles and still links, and the failure appears only on hardware
 * -- as an update that writes the entire image and then fails its final
 * step, leaving a board with a good image and no valid metadata. On the one
 * component with no reachable BOOTSEL button, that must be a build error,
 * not a host-test failure nobody ran. tests/test_bl_flash.c asserts the
 * equality as well; this asserts the property the code actually needs. */
_Static_assert(FWOG_APP_META_OFFSET >= FWOG_BL_MAX_SIZE,
               "metadata sector is inside the bootloader reserve, so "
               "bl_erase()/bl_program() would reject the update's "
               "durability write and every update would fail at its "
               "final step");

bool bl_flash_range_ok(uint32_t off, uint32_t len, uint32_t align) {
    if (align == 0u || len == 0u) return false;
    if ((off % align) || (len % align)) return false;
    /* Never erase or overwrite the bootloader's own 128 KB reserve. The
       boundary is exact on purpose: FWOG_APP_META_OFFSET equals
       FWOG_BL_MAX_SIZE, so bl_flash_erase_meta() -- the console's `erase` --
       sits on the first legal sector and is not caught by this. Change one
       constant without the other and that stops being true silently, which
       is why tests/test_bl_flash.c asserts the equality. */
    if (off < FWOG_BL_MAX_SIZE) return false;
    /* 64-bit so a large off + large len cannot wrap back into range. */
    if ((uint64_t)off + len > FWOG_FLASH_SIZE) return false;
    return true;
}

/* RP2040 maps the same flash device FOUR times: 0x10000000 is the cached
 * window and 0x11000000 / 0x12000000 / 0x13000000 are the same bytes with
 * different cache and allocation behavior. A pointer into any of them is
 * equally fatal once XIP is disabled, so the rejected region is all four
 * windows, not just the one the device is normally read through. Each
 * window is 16 MB of address space regardless of how big the part is, so
 * the span is fixed rather than derived from FWOG_FLASH_SIZE. */
#define XIP_ALIAS_SPAN 0x4000000u      /* 4 windows x 16 MB */

bool bl_flash_src_ok(uintptr_t src) {
    return !(src >= (uintptr_t)FWOG_XIP_BASE &&
             src <  (uintptr_t)FWOG_XIP_BASE + XIP_ALIAS_SPAN);
}

bool bl_flash_read_ok(uint32_t off, uint32_t len) {
    return (uint64_t)off + len <= FWOG_FLASH_SIZE;
}

#ifndef HOST_TEST
#include "hardware/flash.h"
#include "hardware/sync.h"

static bool bl_erase(void *ctx, uint32_t off, uint32_t len) {
    (void)ctx;
    if (!bl_flash_range_ok(off, len, FLASH_SECTOR_SIZE)) return false;
    uint32_t save = save_and_disable_interrupts();
    flash_range_erase(off, len);
    restore_interrupts(save);
    return true;
}

static bool bl_program(void *ctx, uint32_t off, const uint8_t *src, uint32_t len) {
    (void)ctx;
    if (!bl_flash_range_ok(off, len, FLASH_PAGE_SIZE)) return false;
    /* src MUST be in RAM: the bootrom reads it with XIP disabled, so a
       pointer into flash returns garbage or faults. Every caller in this
       BSP passes either the link receive buffer or fwog_bl_update_t.page,
       both of which are .bss -- but the obligation is enforced here rather
       than left to trust, so a flash address becomes a rejected call
       instead of silent corruption. */
    if (!bl_flash_src_ok((uintptr_t)src)) return false;
    uint32_t save = save_and_disable_interrupts();
    flash_range_program(off, src, len);
    restore_interrupts(save);
    return true;
}

static bool bl_read(void *ctx, uint32_t off, uint8_t *dst, uint32_t len) {
    (void)ctx;
    /* Bounded like the two above: an unchecked off could push
       FWOG_XIP_BASE + off out of the XIP window and read something that is
       not flash at all.

       A rejected read does not touch dst at all -- it no longer zero-fills.
       The
       earlier version had to, because this op returned void and the only
       way to fail closed was to hand the caller a buffer guaranteed to fail
       CRC32. That worked, but it meant writing len bytes when len might be
       exactly what made the range illegal. Now the caller is told, and
       verify() aborts the update without reading dst. */
    if (!bl_flash_read_ok(off, len)) return false;
    memcpy(dst, (const uint8_t *)(FWOG_XIP_BASE + off), len);
    return true;
}

static const fwog_bl_flash_ops_t s_ops = { bl_erase, bl_program, bl_read, NULL };

const fwog_bl_flash_ops_t *bl_flash_ops(void) { return &s_ops; }

bool bl_flash_read_meta(fwog_app_meta_t *out) {
    memcpy(out, (const void *)(FWOG_XIP_BASE + FWOG_APP_META_OFFSET), sizeof *out);
    return fwog_app_meta_valid(out);
}

bool bl_flash_erase_meta(void) {
    return bl_erase(NULL, FWOG_APP_META_OFFSET, FWOG_APP_META_SIZE);
}
#endif
