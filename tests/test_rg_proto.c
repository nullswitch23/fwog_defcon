#include "test_util.h"
#include "rg_proto.h"

int main(void) {
    ASSERT_EQ((int)sizeof(rg_status_t), 32);
    ASSERT_EQ((int)RG_MSG_ST, 0x73);
    ASSERT_EQ((int)RG_NA, 255);
    TEST_RETURN();
}
