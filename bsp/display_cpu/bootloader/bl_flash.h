/* The hardware behind fwog_bl_flash_ops_t, plus the two whole-record
 * metadata operations the bootloader needs outside an update.
 *
 * Everything here disables interrupts across the flash operation. The SDK's
 * flash_range_erase()/flash_range_program() already run from RAM, so THIS
 * file does not need __not_in_flash_func -- but an interrupt handler living
 * in XIP flash that fires mid-erase does fault, and nothing else prevents
 * that. */
#ifndef FWOG_BL_FLASH_H
#define FWOG_BL_FLASH_H
#include <stdbool.h>
#include <stdint.h>
#include "common/app_meta.h"
#include "bootloader/bl_receiver.h"

/* ---- Pure: host-tested, no SDK ----
 *
 * The three guards below are the whole reason this file has a pure half.
 * They are defense in depth, not a live hole: bl_receiver.c bounds
 * h.offset + h.len against img_size, which it caps at FWOG_APP_MAX_SIZE, so
 * nothing reaching these through the receiver can wrap or land in the
 * reserve. They exist because the console and the main loop drive these ops
 * from further away than the receiver does, and because the failure they
 * prevent -- overwriting the bootloader's own 128 KB -- is the one on this
 * board that needs physical BOOTSEL access to recover, which the display CPU
 * does not have. */

/* True when [off, off+len) is a legal target for an erase or a program:
 * `align`-aligned at both ends, non-empty, entirely above the bootloader's
 * own reserve, and entirely inside the flash device.
 *
 * `align` is a parameter rather than a constant because the two callers
 * differ -- FLASH_SECTOR_SIZE for erase, FLASH_PAGE_SIZE for program -- and
 * both of those live in the SDK, which the host test tree does not have. */
bool bl_flash_range_ok(uint32_t off, uint32_t len, uint32_t align);

/* True when `src` is a legal source for a program: anywhere outside the
 * flash XIP windows. The bootrom reads the source with XIP disabled, so a
 * pointer into flash returns garbage or faults. All FOUR of RP2040's XIP
 * alias windows are rejected, not just the cached one at 0x10000000 --
 * 0x11/0x12/0x13000000 are the same bytes and are just as fatal.
 *
 * This is a BOUNDED check, deliberately. The obvious one-liner
 * `src >= FWOG_XIP_BASE` is wrong on this part: SRAM (0x20000000+) is
 * numerically ABOVE FWOG_XIP_BASE (0x10000000), so it would reject every
 * legitimate RAM buffer along with the flash pointers it is aimed at. */
bool bl_flash_src_ok(uintptr_t src);

/* True when [off, off+len) is a legal source for a read. Reads below the
 * reserve are allowed -- the bootloader may legitimately read its own
 * image -- so this bounds the flash device only. */
bool bl_flash_read_ok(uint32_t off, uint32_t len);

/* The vtable the update receiver drives. */
const fwog_bl_flash_ops_t *bl_flash_ops(void);

/* Copy the metadata record out of flash. Returns fwog_app_meta_valid() on
 * what was read, so an erased sector reads as "no app" rather than an
 * error. */
bool bl_flash_read_meta(fwog_app_meta_t *out);

/* Invalidate the app slot: the console's `erase`. One sector erase. */
bool bl_flash_erase_meta(void);

#endif
