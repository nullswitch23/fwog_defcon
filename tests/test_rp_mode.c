#include "test_util.h"
#include "rp_mode.h"
#include <string.h>

int main(void) {
    rp_mode_t m = RP_MODE_COUNT;
    ASSERT_TRUE(rp_mode_parse("i2c", &m));
    ASSERT_EQ(m, RP_MODE_I2C);
    ASSERT_TRUE(rp_mode_parse("SPI ", &m));
    ASSERT_EQ(m, RP_MODE_SPI);
    ASSERT_TRUE(!rp_mode_parse("gpio", &m));
    ASSERT_TRUE(!rp_mode_parse("", &m));
    ASSERT_EQ(rp_mode_from_name("uart"), RP_MODE_UART);
    ASSERT_EQ(rp_mode_from_name("nope"), RP_MODE_HIZ);
    ASSERT_TRUE(strcmp(rp_mode_name(RP_MODE_DIO), "DIO") == 0);
    TEST_RETURN();
}
