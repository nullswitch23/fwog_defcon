#include "test_util.h"
#include "oc_proto.h"

int main(void) {
    ASSERT_EQ((int)sizeof(oc_cmd_t), 2);
    ASSERT_EQ((int)sizeof(oc_slot_rec_t), 32);
    ASSERT_EQ((int)sizeof(oc_file_t), 772);
    ASSERT_EQ((int)sizeof(oc_slots_msg_t), 776);
    TEST_RETURN();
}
