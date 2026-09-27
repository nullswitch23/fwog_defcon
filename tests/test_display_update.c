#include "test_util.h"
#include "display_update/display_update.h"
#include <string.h>

static fwog_display_image_info_t img(uint32_t size, uint32_t crc) {
    fwog_display_image_info_t i;
    memset(&i, 0, sizeof i);
    i.size = size;
    i.crc32 = crc;
    i.build_ts = 1753500000u;
    memcpy(i.version, "v1.0", 5);
    return i;
}

static fwog_bl_hello_t hello(bool valid, uint32_t size, uint32_t crc) {
    fwog_bl_hello_t h;
    memset(&h, 0, sizeof h);
    h.type = FWOG_BL_MSG_HELLO;
    h.proto_ver = FWOG_BL_PROTO_VER;
    h.app_valid = valid ? 1u : 0u;
    h.app_size = size;
    h.app_crc32 = crc;
    return h;
}

int main(void) {
    fwog_display_image_info_t i = img(100000u, 0xAABBCCDDu);

    /* Matching CRC and size: nothing to do. This is the common case on
       every boot of a healthy board, so it must be the cheap one. */
    {
        fwog_bl_hello_t h = hello(true, 100000u, 0xAABBCCDDu);
        ASSERT_TRUE(!fwog_display_update_needed(&h, &i));
    }

    /* Different CRC: reflash. The trigger is the CRC, never a version
       comparison -- so a rebuilt dev image deploys automatically and a
       downgrade works, with no version discipline required. */
    {
        fwog_bl_hello_t h = hello(true, 100000u, 0xAABBCCDEu);
        ASSERT_TRUE(fwog_display_update_needed(&h, &i));
    }

    /* Same CRC but a different size. A CRC32 collision across two
       different lengths is unlikely, not impossible, and the size check
       costs nothing. */
    {
        fwog_bl_hello_t h = hello(true, 99999u, 0xAABBCCDDu);
        ASSERT_TRUE(fwog_display_update_needed(&h, &i));
    }

    /* No valid app: reflash regardless of what the other fields say. */
    {
        fwog_bl_hello_t h = hello(false, 100000u, 0xAABBCCDDu);
        ASSERT_TRUE(fwog_display_update_needed(&h, &i));
    }
    {
        fwog_bl_hello_t h = hello(false, 0u, 0u);
        ASSERT_TRUE(fwog_display_update_needed(&h, &i));
    }

    /* A protocol version we do not speak. Refuse to update rather than
       stream 500 KB at a bootloader that may lay it out differently --
       the operator gets a diagnostic and the display keeps whatever app
       it has. */
    {
        fwog_bl_hello_t h = hello(true, 100000u, 0xAABBCCDEu);
        h.proto_ver = FWOG_BL_PROTO_VER + 1u;
        ASSERT_TRUE(!fwog_display_update_needed(&h, &i));
        h.proto_ver = 0u;
        ASSERT_TRUE(!fwog_display_update_needed(&h, &i));
    }

    /* An image we do not have cannot be sent. */
    {
        fwog_display_image_info_t empty = img(0u, 0u);
        fwog_bl_hello_t h = hello(false, 0u, 0u);
        ASSERT_TRUE(!fwog_display_update_needed(&h, &empty));
    }

    /* Result strings: every enumerator is printable, and each is distinct
       so a DIAG line identifies which path was taken. */
    {
        const fwog_display_result_t all[] = {
            FWOG_DISP_OK_RAN, FWOG_DISP_OK_UPDATED, FWOG_DISP_NO_HELLO,
            FWOG_DISP_UPDATE_FAILED, FWOG_DISP_LINK_DOWN, FWOG_DISP_IMAGE_BAD
        };
        for (unsigned a = 0; a < 6u; a++) {
            const char *ta = fwog_display_result_text(all[a]);
            ASSERT_TRUE(ta != NULL && ta[0] != '\0');
            for (unsigned b = a + 1u; b < 6u; b++) {
                ASSERT_TRUE(strcmp(ta, fwog_display_result_text(all[b])) != 0);
            }
        }
        ASSERT_TRUE(fwog_display_result_text((fwog_display_result_t)99) != NULL);
    }

    TEST_RETURN();
}
