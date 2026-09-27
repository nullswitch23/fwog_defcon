#include "test_util.h"
#include "vp_art.h"
#include "vp_proto.h"
#include <string.h>

static int ascii_ok(const char *s) {
    if (s == 0 || s[0] == 0) return 0;
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20u || c > 0x7Eu) return 0;
    }
    return 1;
}

int main(void) {
    ASSERT_EQ(VP_NART, 100u);
    ASSERT_EQ(sizeof(vp_save_t), 48u);
    ASSERT_EQ(VP_SAVE_V2, 36u);
    ASSERT_EQ(sizeof(vp_rf_t), 12u);
    ASSERT_EQ(vp_level_for_xp(0), 1u);
    ASSERT_EQ(vp_level_for_xp(39), 1u);
    ASSERT_EQ(vp_level_for_xp(40), 2u);
    ASSERT_EQ(vp_level_for_xp(800), 20u);

    vp_save_t loaded;
    uint8_t v2[VP_SAVE_V2];
    memset(v2, 0, sizeof v2);
    v2[2] = 40;
    v2[3] = 70;
    v2[4] = 80;
    v2[8] = 1;
    v2[20] = 0xFF;
    {
        const uint32_t mag = VP_MAGIC;
        memcpy(v2 + 16, &mag, 4);
    }
    ASSERT_TRUE(vp_save_from_bytes(&loaded, v2, sizeof v2));
    ASSERT_EQ(loaded.hunger, 40u);
    ASSERT_EQ(loaded.happy, 70u);
    ASSERT_EQ(loaded.name[0], 0);

    vp_save_t v3;
    memset(&v3, 0, sizeof v3);
    v3.magic = VP_MAGIC;
    v3.hunger = 12;
    memcpy(v3.name, "SPIKE", 6);
    ASSERT_TRUE(vp_save_from_bytes(&loaded, &v3, sizeof v3));
    ASSERT_EQ(loaded.hunger, 12u);
    ASSERT_TRUE(strcmp(loaded.name, "SPIKE") == 0);
    ASSERT_TRUE(!vp_save_from_bytes(&loaded, v2, 20u));

    uint8_t bits[VP_ART_BYTES];
    memset(bits, 0, sizeof bits);
    ASSERT_TRUE(!vp_art_owned(bits, 0));
    vp_art_give(bits, 0);
    vp_art_give(bits, 99);
    ASSERT_TRUE(vp_art_owned(bits, 0));
    ASSERT_TRUE(vp_art_owned(bits, 99));
    ASSERT_TRUE(!vp_art_owned(bits, 1));

    for (unsigned i = 0; i < VP_NART; i++) {
        const vp_art_t *a = vp_art_get(i);
        ASSERT_TRUE(a == &vp_arts[i]);
        ASSERT_TRUE(ascii_ok(a->name));
        ASSERT_TRUE(ascii_ok(a->slot));
        ASSERT_TRUE(ascii_ok(a->bonus));
        ASSERT_TRUE(ascii_ok(a->lore));
        ASSERT_TRUE(strlen(a->name) <= 28u);
        ASSERT_TRUE(strlen(a->slot) <= 12u);
        ASSERT_TRUE(strlen(a->bonus) <= 16u);
        ASSERT_TRUE(strlen(a->lore) <= 96u);
        ASSERT_TRUE(strlen(a->lore) >= 40u);
        for (unsigned j = 0; j < i; j++) {
            ASSERT_TRUE(strcmp(vp_arts[i].name, vp_arts[j].name) != 0);
        }
    }
    ASSERT_TRUE(vp_art_get(VP_NART) == &vp_arts[0]);
    TEST_RETURN();
}
