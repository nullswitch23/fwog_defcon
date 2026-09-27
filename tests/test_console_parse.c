#include "test_util.h"
#include "bootloader/bl_console.h"
#include <string.h>

/* Feed a whole string; returns true if a line completed on the last char. */
static bool feed(bl_line_t *l, const char *s) {
    bool done = false;
    for (const char *p = s; *p; p++) done = bl_line_feed(l, *p);
    return done;
}

int main(void) {
    /* --- command parsing --- */
    ASSERT_EQ(bl_console_parse("info"), BL_CMD_INFO);
    ASSERT_EQ(bl_console_parse("bootsel"), BL_CMD_BOOTSEL);
    ASSERT_EQ(bl_console_parse("run"), BL_CMD_RUN);
    ASSERT_EQ(bl_console_parse("ship"), BL_CMD_SHIP);
    ASSERT_EQ(bl_console_parse("erase"), BL_CMD_ERASE);
    ASSERT_EQ(bl_console_parse("help"), BL_CMD_HELP);
    ASSERT_EQ(bl_console_parse("?"), BL_CMD_HELP);

    /* Surrounding whitespace is forgiven -- terminals and paste add it. */
    ASSERT_EQ(bl_console_parse("  info  "), BL_CMD_INFO);
    ASSERT_EQ(bl_console_parse("\tinfo\t"), BL_CMD_INFO);
    ASSERT_EQ(bl_console_parse("info\r"), BL_CMD_INFO);

    /* Case is forgiven too: this is typed by a human under stress. */
    ASSERT_EQ(bl_console_parse("INFO"), BL_CMD_INFO);
    ASSERT_EQ(bl_console_parse("BootSel"), BL_CMD_BOOTSEL);

    /* An empty or blank line is not an error and must not print help --
       pressing enter to see if the console is alive is normal. */
    ASSERT_EQ(bl_console_parse(""), BL_CMD_NONE);
    ASSERT_EQ(bl_console_parse("   "), BL_CMD_NONE);
    ASSERT_EQ(bl_console_parse("\r"), BL_CMD_NONE);

    /* No command takes an argument, so anything trailing is a typo, not a
       command with extra words. "ship now" must NOT power the board off. */
    ASSERT_EQ(bl_console_parse("ship now"), BL_CMD_UNKNOWN);
    ASSERT_EQ(bl_console_parse("run app"), BL_CMD_UNKNOWN);
    ASSERT_EQ(bl_console_parse("info -v"), BL_CMD_UNKNOWN);

    /* Prefixes and near-misses are not the command. */
    ASSERT_EQ(bl_console_parse("in"), BL_CMD_UNKNOWN);
    ASSERT_EQ(bl_console_parse("infoo"), BL_CMD_UNKNOWN);
    ASSERT_EQ(bl_console_parse("boot"), BL_CMD_UNKNOWN);
    ASSERT_EQ(bl_console_parse("shipp"), BL_CMD_UNKNOWN);
    ASSERT_EQ(bl_console_parse("xyzzy"), BL_CMD_UNKNOWN);

    /* --- line assembly --- */
    bl_line_t l;
    bl_line_init(&l);

    ASSERT_TRUE(!feed(&l, "inf"));
    ASSERT_TRUE(feed(&l, "o\n"));
    ASSERT_TRUE(strcmp(l.buf, "info") == 0);
    ASSERT_TRUE(!l.overflow);

    /* CRLF must produce one line, not two. */
    bl_line_init(&l);
    ASSERT_TRUE(feed(&l, "run\r\n"));
    ASSERT_EQ(bl_console_parse(l.buf), BL_CMD_RUN);

    /* A bare CR also terminates: some terminals send only that. */
    bl_line_init(&l);
    ASSERT_TRUE(feed(&l, "help\r"));
    ASSERT_EQ(bl_console_parse(l.buf), BL_CMD_HELP);

    /* An empty line still completes, so the caller can echo a prompt. */
    bl_line_init(&l);
    ASSERT_TRUE(bl_line_feed(&l, '\n'));
    ASSERT_EQ(l.buf[0], '\0');

    /* Backspace, because a typo in `ship` is expensive. */
    bl_line_init(&l);
    ASSERT_TRUE(!feed(&l, "inxo"));
    ASSERT_TRUE(!bl_line_feed(&l, '\b'));
    ASSERT_TRUE(!bl_line_feed(&l, '\b'));
    ASSERT_TRUE(feed(&l, "fo\n"));
    ASSERT_TRUE(strcmp(l.buf, "info") == 0);
    /* Backspace on an empty buffer must not underflow. */
    bl_line_init(&l);
    for (int i = 0; i < 10; i++) ASSERT_TRUE(!bl_line_feed(&l, '\b'));
    ASSERT_TRUE(feed(&l, "run\n"));
    ASSERT_TRUE(strcmp(l.buf, "run") == 0);

    /* Overflow: the line is discarded rather than truncated, because a
       truncated "shipwreck" would parse as "ship". */
    bl_line_init(&l);
    for (unsigned i = 0; i < BL_LINE_MAX + 50u; i++) {
        ASSERT_TRUE(!bl_line_feed(&l, 'x'));
    }
    ASSERT_TRUE(l.overflow);
    ASSERT_TRUE(feed(&l, "\n"));
    ASSERT_EQ(l.buf[0], '\0');          /* nothing survived */
    ASSERT_TRUE(!l.overflow);           /* and the flag rearmed */

    /* An overflowed line must not leak into the next one. */
    ASSERT_TRUE(feed(&l, "ship\n"));
    ASSERT_EQ(bl_console_parse(l.buf), BL_CMD_SHIP);

    /* A line exactly at the limit is kept. */
    bl_line_init(&l);
    for (unsigned i = 0; i < BL_LINE_MAX - 1u; i++) {
        ASSERT_TRUE(!bl_line_feed(&l, 'y'));
    }
    ASSERT_TRUE(!l.overflow);
    ASSERT_TRUE(feed(&l, "\n"));
    ASSERT_EQ(strlen(l.buf), BL_LINE_MAX - 1u);

    TEST_RETURN();
}
