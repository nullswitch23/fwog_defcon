#include "test_util.h"
#include "qg_sense.h"
#include "qg_nick.h"
#include "qg_proto.h"
#include <string.h>

int main(void) {
    qg_chan_t ch[QG_MAX];
    uint8_t none[] = {0};
    ASSERT_EQ(qg_fill(ch, QG_MAX, none, 0), 0);
    ASSERT_EQ(qg_fill(NULL, QG_MAX, none, 1), 0);

    uint8_t addrs[] = {0x23u, 0x19u, 0x5Cu};
    ASSERT_EQ(qg_fill(ch, QG_MAX, addrs, 3), 3);
    ASSERT_TRUE(strcmp(ch[0].name, "BH1750") == 0);
    ASSERT_TRUE(strcmp(ch[0].val, "0x23") == 0);
    ASSERT_TRUE(strcmp(ch[1].name, "0x19") == 0);
    ASSERT_TRUE(strcmp(ch[1].val, "present") == 0);
    ASSERT_TRUE(strcmp(ch[2].name, "BH1750") == 0);
    ASSERT_TRUE(strcmp(ch[2].val, "0x5C") == 0);
    ASSERT_EQ((int)ch[0].addr, 0x23);
    ASSERT_EQ((int)ch[0].known, 1);
    ASSERT_EQ((int)ch[1].addr, 0x19);
    ASSERT_EQ((int)ch[1].known, 0);
    ASSERT_EQ((int)ch[2].known, 1);

    qg_nick_set(0x19u, "TMP117");
    ASSERT_TRUE(strcmp(qg_nick_get(0x19u), "TMP117") == 0);
    ASSERT_TRUE(qg_nick_get(0x23u) == NULL);
    qg_nick_set(0x19u, "");
    ASSERT_TRUE(qg_nick_get(0x19u) == NULL);

    ASSERT_EQ(qg_fill(ch, 1, addrs, 3), 1);
    ASSERT_TRUE(strcmp(ch[0].name, "BH1750") == 0);
    TEST_RETURN();
}
