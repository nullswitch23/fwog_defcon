#include "test_util.h"
#include "lcd/fwog_t9.h"
#include <string.h>

int main(void) {
    char buf[8];
    int g = 0, li = 0;

    ASSERT_EQ((int)fwog_t9_cur(0, 0), (int)'A');
    ASSERT_EQ((int)fwog_t9_cur(5, 3), (int)'S');
    buf[0] = '\0';
    fwog_t9_insert(buf, sizeof buf, 'M');
    fwog_t9_insert(buf, sizeof buf, 'E');
    ASSERT_EQ(strcmp(buf, "ME"), 0);
    fwog_t9_backspace(buf);
    ASSERT_EQ(strcmp(buf, "M"), 0);
    fwog_t9_group_next(&g, &li);
    ASSERT_EQ(g, 1);
    ASSERT_EQ(li, 0);
    fwog_t9_letter_next(1, &li);
    ASSERT_EQ((int)fwog_t9_cur(1, li), (int)'E');
    TEST_RETURN();
}
