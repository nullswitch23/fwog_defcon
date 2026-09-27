#include "bootloader/bl_flash.h"
#include "test_util.h"

/* Mirrors of the two SDK constants the callers pass as `align`. The host
 * tree has no SDK, and these are fixed by the RP2040's flash interface
 * rather than by anything this repo chooses. */
#define SECTOR 4096u
#define PAGE    256u

static void test_layout_coincidence(void) {
    /* bl_flash_range_ok() rejects everything below FWOG_BL_MAX_SIZE, and
       bl_flash_erase_meta() erases at FWOG_APP_META_OFFSET. The console's
       `erase` works only because those two constants are equal, so the
       metadata sector is the FIRST legal sector rather than the last
       forbidden one. Nothing else in the build checks this: change one and
       not the other and `erase` starts returning false with no diagnostic. */
    ASSERT_EQ(FWOG_BL_MAX_SIZE, FWOG_APP_META_OFFSET);

    /* The exact call bl_flash_erase_meta() makes must pass. */
    ASSERT_TRUE(bl_flash_range_ok(FWOG_APP_META_OFFSET, FWOG_APP_META_SIZE,
                                  SECTOR));
    /* One sector lower is inside the reserve and must not. */
    ASSERT_TRUE(!bl_flash_range_ok(FWOG_APP_META_OFFSET - SECTOR,
                                   FWOG_APP_META_SIZE, SECTOR));
    /* The app slot, which is what the receiver actually writes. */
    ASSERT_TRUE(bl_flash_range_ok(FWOG_APP_OFFSET, PAGE, PAGE));
}

static void test_range_rejects_the_reserve(void) {
    /* Offset 0 is the bootloader's own vector table. This is the case the
       whole guard exists for. */
    ASSERT_TRUE(!bl_flash_range_ok(0u, SECTOR, SECTOR));
    /* The last byte of the reserve. */
    ASSERT_TRUE(!bl_flash_range_ok(FWOG_BL_MAX_SIZE - SECTOR, SECTOR, SECTOR));
    /* A range that STARTS legally is fine even though it is adjacent -- the
       guard is on the start, because length is bounded separately. */
    ASSERT_TRUE(bl_flash_range_ok(FWOG_BL_MAX_SIZE, SECTOR, SECTOR));
}

static void test_range_alignment(void) {
    /* Both ends must be aligned; the SDK's erase/program require it. */
    ASSERT_TRUE(!bl_flash_range_ok(FWOG_APP_OFFSET + 1u, SECTOR, SECTOR));
    ASSERT_TRUE(!bl_flash_range_ok(FWOG_APP_OFFSET, SECTOR + 1u, SECTOR));
    /* Zero length would be a no-op at best and an unbounded loop at worst. */
    ASSERT_TRUE(!bl_flash_range_ok(FWOG_APP_OFFSET, 0u, SECTOR));
    /* A zero align would divide by zero rather than reject. */
    ASSERT_TRUE(!bl_flash_range_ok(FWOG_APP_OFFSET, SECTOR, 0u));
    /* Page alignment is coarser-grained than sector alignment: a
       sector-aligned offset is always page-aligned, but not the reverse. */
    ASSERT_TRUE(bl_flash_range_ok(FWOG_APP_OFFSET + PAGE, PAGE, PAGE));
    ASSERT_TRUE(!bl_flash_range_ok(FWOG_APP_OFFSET + PAGE, PAGE, SECTOR));
}

static void test_range_upper_bound(void) {
    /* Exactly filling the device is legal. */
    ASSERT_TRUE(bl_flash_range_ok(FWOG_FLASH_SIZE - SECTOR, SECTOR, SECTOR));
    /* One sector past it is not. */
    ASSERT_TRUE(!bl_flash_range_ok(FWOG_FLASH_SIZE, SECTOR, SECTOR));
    /* The case a 32-bit sum would get wrong: off + len wraps to a small
       number and would compare as in-range. The check is 64-bit for this. */
    ASSERT_TRUE(!bl_flash_range_ok(0xFFFFF000u, SECTOR * 2u, SECTOR));
}

static void test_src_rejects_flash_only(void) {
    /* The bootrom reads the source with XIP disabled, so a flash pointer
       returns garbage or faults. */
    ASSERT_TRUE(!bl_flash_src_ok((uintptr_t)FWOG_XIP_BASE));
    ASSERT_TRUE(!bl_flash_src_ok((uintptr_t)FWOG_XIP_BASE + FWOG_APP_OFFSET));
    /* Last byte of the cached window. */
    ASSERT_TRUE(!bl_flash_src_ok((uintptr_t)FWOG_XIP_BASE +
                                 FWOG_FLASH_SIZE - 1u));

    /* RP2040 maps the same device four times and a pointer into any alias
       is equally fatal with XIP disabled. Rejecting only the cached window
       would let 0x11/0x12/0x13 through. */
    ASSERT_TRUE(!bl_flash_src_ok(0x11000000u));
    ASSERT_TRUE(!bl_flash_src_ok(0x12000000u));
    ASSERT_TRUE(!bl_flash_src_ok(0x13000000u));
    ASSERT_TRUE(!bl_flash_src_ok(0x13FFFFFFu));   /* last aliased byte */
    /* First address past all four windows. */
    ASSERT_TRUE(bl_flash_src_ok(0x14000000u));

    /* THE trap this check exists to avoid, and the reason it is a bounded
       range rather than a bare `src >= FWOG_XIP_BASE`: on RP2040 SRAM starts
       at 0x20000000, which is numerically ABOVE the 0x10000000 XIP base. An
       unbounded comparison would reject every legitimate .bss buffer -- i.e.
       every real caller in this BSP -- and updates would fail on hardware
       while every host test still passed. */
    ASSERT_TRUE(bl_flash_src_ok(0x20000000u));
    ASSERT_TRUE(bl_flash_src_ok(0x20041800u));   /* __StackBottom */
    /* Below the windows. */
    ASSERT_TRUE(bl_flash_src_ok(0u));
}

static void test_read_bounds(void) {
    /* Reads below the reserve ARE allowed: the bootloader may legitimately
       read its own image, and bl_flash_read_meta() reads the metadata
       sector. Only the device bound applies. */
    ASSERT_TRUE(bl_flash_read_ok(0u, 16u));
    ASSERT_TRUE(bl_flash_read_ok(FWOG_APP_META_OFFSET, FWOG_APP_META_SIZE));
    /* Exactly to the end of the device. */
    ASSERT_TRUE(bl_flash_read_ok(FWOG_FLASH_SIZE - 4u, 4u));
    /* One byte past. */
    ASSERT_TRUE(!bl_flash_read_ok(FWOG_FLASH_SIZE - 4u, 5u));
    ASSERT_TRUE(!bl_flash_read_ok(FWOG_FLASH_SIZE, 1u));
    /* Wrapping, as above. */
    ASSERT_TRUE(!bl_flash_read_ok(0xFFFFFFF0u, 0x20u));
    /* A zero-length read is harmless and must not be rejected -- memcpy of
       0 bytes is well defined and callers should not have to special-case. */
    ASSERT_TRUE(bl_flash_read_ok(0u, 0u));
}

int main(void) {
    test_layout_coincidence();
    test_range_rejects_the_reserve();
    test_range_alignment();
    test_range_upper_bound();
    test_src_rejects_flash_only();
    test_read_bounds();
    TEST_RETURN();
}
