#include "test_util.h"
#include "pass_nvs.h"
#include <string.h>

static uint32_t s_seq;

static uint32_t rnd(void) {
    return s_seq++;
}

int main(void) {
    ASSERT_TRUE(fwog_pass_ok("K7mN2pQx"));
    ASSERT_TRUE(!fwog_pass_ok(NULL));
    ASSERT_TRUE(!fwog_pass_ok(""));
    ASSERT_TRUE(!fwog_pass_ok("short"));
    ASSERT_TRUE(!fwog_pass_ok("toolong12"));
    ASSERT_TRUE(!fwog_pass_ok("K7mN2pQ0"));
    ASSERT_TRUE(!fwog_pass_ok("K7mN2pQ1"));
    ASSERT_TRUE(!fwog_pass_ok("K7mN2pQI"));
    ASSERT_TRUE(!fwog_pass_ok("K7mN2pQl"));
    ASSERT_TRUE(!fwog_pass_ok("K7mN2pQO"));
    ASSERT_TRUE(!fwog_pass_ok("K7mN2pQ!"));

    char a[FWOG_PASS_LEN + 1], b[FWOG_PASS_LEN + 1];
    s_seq = 1;
    fwog_pass_gen(a, rnd);
    fwog_pass_gen(b, rnd);
    ASSERT_TRUE(fwog_pass_ok(a));
    ASSERT_TRUE(fwog_pass_ok(b));
    ASSERT_EQ(strlen(a), (unsigned)FWOG_PASS_LEN);
    ASSERT_TRUE(strcmp(a, b) != 0);

    TEST_RETURN();
}
