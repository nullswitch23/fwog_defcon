#include "test_util.h"
#include "ph_proto.h"

int main(void) {
    ASSERT_EQ((int)sizeof(ph_cmd_t), 4);
    ASSERT_EQ((int)sizeof(ph_dev_t), 20);
    ASSERT_EQ((int)sizeof(ph_status_t), 174);
    ASSERT_EQ((int)sizeof(ph_lib_ent_t), 20);
    ASSERT_EQ((int)sizeof(ph_lib_file_t), 164);
    ASSERT_EQ((int)sizeof(ph_lib_msg_t), 168);
    ASSERT_EQ((int)sizeof(ph_label_msg_t), 20);
    ASSERT_EQ((int)PH_CMD_LIBDEL, 9);
    ASSERT_EQ((int)PH_MSG_LABEL, 0x65);
    TEST_RETURN();
}
