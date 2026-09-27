#include "test_util.h"
#include "common/link/io_proto.h"
#include "common/io_cfg.h"
#include <string.h>

int main(void) {
    /* The hardware record: 0x00 is invalid, 0x01-0x1F are the bootloader's, and
       application protocols start at 0x20. These three must never move. */
    ASSERT_EQ(FWOG_IO_MSG_CONFIG_ENABLE, 0x20u);
    ASSERT_EQ(FWOG_IO_MSG_SET_DIRS,      0x21u);
    ASSERT_EQ(FWOG_IO_MSG_ACK,           0x22u);

    uint8_t buf[32];

    /* --- CONFIG_ENABLE --- */
    size_t n = fwog_io_proto_build_config_enable(buf, sizeof buf, 7u, true);
    ASSERT_EQ(n, sizeof(fwog_io_config_enable_t));
    ASSERT_EQ(fwog_io_proto_type(buf, n), FWOG_IO_MSG_CONFIG_ENABLE);
    ASSERT_EQ(((const fwog_io_config_enable_t *)buf)->seq, 7u);
    ASSERT_EQ(((const fwog_io_config_enable_t *)buf)->enable, 1u);

    n = fwog_io_proto_build_config_enable(buf, sizeof buf, 8u, false);
    ASSERT_EQ(((const fwog_io_config_enable_t *)buf)->seq, 8u);
    ASSERT_EQ(((const fwog_io_config_enable_t *)buf)->enable, 0u);

    /* --- SET_DIRS carries the seq, the dirword, the pull-up and both
       antennas --- */
    fwog_io_cfg_t c;
    fwog_io_cfg_default(&c);
    c.radio1_ant = FWOG_ANT_900MHZ;
    c.radio2_ant = FWOG_ANT_ISOLATION;
    n = fwog_io_proto_build_set_dirs(buf, sizeof buf, 9u, &c);
    ASSERT_EQ(n, sizeof(fwog_io_set_dirs_t));

    const fwog_io_set_dirs_t *m = (const fwog_io_set_dirs_t *)buf;
    ASSERT_EQ(m->type, FWOG_IO_MSG_SET_DIRS);
    ASSERT_EQ(m->seq, 9u);
    ASSERT_EQ(m->dirword, fwog_io_pack_dirword(&c));
    ASSERT_EQ(m->i2c_pullup, 1u);
    ASSERT_EQ(m->radio1_ant, (uint8_t)FWOG_ANT_900MHZ);
    ASSERT_EQ(m->radio2_ant, (uint8_t)FWOG_ANT_ISOLATION);

    /* --- ACK names the message it answers and echoes its seq --- */
    n = fwog_io_proto_build_ack(buf, sizeof buf, 9u, FWOG_IO_MSG_SET_DIRS,
                                false);
    ASSERT_EQ(n, sizeof(fwog_io_ack_t));
    const fwog_io_ack_t *a = (const fwog_io_ack_t *)buf;
    ASSERT_EQ(a->type, FWOG_IO_MSG_ACK);
    ASSERT_EQ(a->answering, FWOG_IO_MSG_SET_DIRS);
    ASSERT_EQ(a->seq, 9u);
    ASSERT_EQ(a->ok, 0u);

    /* --- A different seq is carried faithfully, not clamped or reused --
       this is the field wait_ack() relies on to reject a stale ack from a
       previous, timed-out attempt (F5). --- */
    n = fwog_io_proto_build_ack(buf, sizeof buf, 200u, FWOG_IO_MSG_CONFIG_ENABLE,
                                true);
    ASSERT_EQ(((const fwog_io_ack_t *)buf)->seq, 200u);
    ASSERT_EQ(((const fwog_io_ack_t *)buf)->answering,
              FWOG_IO_MSG_CONFIG_ENABLE);
    ASSERT_EQ(((const fwog_io_ack_t *)buf)->ok, 1u);

    /* --- A too-small buffer returns 0 and writes nothing --- */
    ASSERT_EQ(fwog_io_proto_build_set_dirs(buf, 1u, 0u, &c), 0u);

    /* --- A bootloader-range type is not ours --- */
    uint8_t stray[4] = { 0x0Au, 0, 0, 0 };
    ASSERT_EQ(fwog_io_proto_type(stray, sizeof stray), 0u);

    /* --- An empty payload is not ours --- */
    ASSERT_EQ(fwog_io_proto_type(buf, 0u), 0u);

    TEST_RETURN();
}
