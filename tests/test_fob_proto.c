#include "test_util.h"
#include "fob_proto.h"

int main(void) {
    ASSERT_EQ((int)sizeof(fob_cmd_t), 18);
    ASSERT_EQ((int)sizeof(fob_status_t), 68);
    ASSERT_EQ((int)FOB_CMD_SAVE, 8);
    ASSERT_EQ((int)FOB_STEM_N, 9);
    TEST_RETURN();
}
