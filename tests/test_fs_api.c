/* The public filesystem API, end to end, against a RAM-backed diskio.
 *
 * Everything here runs the REAL fwog_fs.c and the REAL FatFs -- only the disk
 * underneath is substituted (tests/fs_ramdisk.c, which mimics NOR erase
 * semantics rather than being a plain byte array). What it does NOT cover is
 * flash timing, and therefore nothing about the watchdog chunking; that is
 * fs_geom.c's splitter test plus a bench step. */
#include "fs/fwog_fs.h"
#include "fs/fs_geom.h"
#include "test_util.h"
#include <string.h>

void fs_ramdisk_wipe(void);
void fs_ramdisk_fail_writes_from(uint32_t lba);

static char s_buf[8192];

/* Start from a wiped disk with a fresh filesystem on it. */
static void fresh_volume(void) {
    (void)fwog_fs_close();
    (void)fwog_fs_unmount();
    fs_ramdisk_wipe();
    fs_ramdisk_fail_writes_from(0xFFFFFFFFu);
    ASSERT_TRUE(fwog_fs_format());
    ASSERT_TRUE(fwog_fs_mounted());
}

static void test_mount_refuses_an_unformatted_volume(void) {
    (void)fwog_fs_close();
    (void)fwog_fs_unmount();
    fs_ramdisk_wipe();
    /* An erased (all-0xFF) volume has no valid boot sector. Mount must FAIL
     * and must not quietly format -- auto-formatting to recover from a mount
     * error would destroy a user's files over what might be a transient read. */
    ASSERT_TRUE(!fwog_fs_mount());
    ASSERT_TRUE(!fwog_fs_mounted());
}

static void test_format_mount_and_volume_info(void) {
    fresh_volume();
    uint64_t freeb = 0u, totalb = 0u;
    ASSERT_TRUE(fwog_fs_volume_info(&freeb, &totalb));
    /* 8 MB volume, minus FAT and root-directory overhead. Bound it loosely on
     * both sides: a wrong geometry shows up here as a wildly wrong size. */
    ASSERT_TRUE(totalb > 7u * 1024u * 1024u);
    ASSERT_TRUE(totalb <= 8u * 1024u * 1024u);
    ASSERT_TRUE(freeb > 7u * 1024u * 1024u);
    ASSERT_TRUE(freeb <= totalb);
}

static void test_write_close_remount_read_back(void) {
    /* Persistence across a remount is the whole point of a filesystem. */
    fresh_volume();
    static const char msg[] = "the quick brown fox jumps over the lazy dog";
    ASSERT_TRUE(fwog_fs_open("hello.txt", true, false));
    ASSERT_TRUE(fwog_fs_write(msg, sizeof(msg) - 1u));
    ASSERT_TRUE(fwog_fs_close());

    ASSERT_TRUE(fwog_fs_unmount());
    ASSERT_TRUE(fwog_fs_mount());

    ASSERT_TRUE(fwog_fs_exists("hello.txt"));
    ASSERT_TRUE(fwog_fs_open("hello.txt", false, false));
    ASSERT_EQ(fwog_fs_size(), sizeof(msg) - 1u);
    size_t len = sizeof(s_buf);
    ASSERT_TRUE(fwog_fs_read(s_buf, &len));
    ASSERT_EQ(len, sizeof(msg) - 1u);
    ASSERT_EQ(memcmp(s_buf, msg, len), 0);
    ASSERT_TRUE(fwog_fs_close());
}

static void test_append_versus_truncate(void) {
    fresh_volume();
    ASSERT_TRUE(fwog_fs_open("a.txt", true, false));
    ASSERT_TRUE(fwog_fs_write("AAAA", 4u));
    ASSERT_TRUE(fwog_fs_close());

    /* append=true keeps what is there and writes at the end. */
    ASSERT_TRUE(fwog_fs_open("a.txt", true, true));
    ASSERT_EQ(fwog_fs_size(), 4u);
    ASSERT_EQ(fwog_fs_tell(), 4u);       /* opened AT the end */
    ASSERT_TRUE(fwog_fs_write("BBBB", 4u));
    ASSERT_TRUE(fwog_fs_close());

    ASSERT_TRUE(fwog_fs_open("a.txt", false, false));
    size_t len = sizeof(s_buf);
    ASSERT_TRUE(fwog_fs_read(s_buf, &len));
    ASSERT_EQ(len, 8u);
    ASSERT_EQ(memcmp(s_buf, "AAAABBBB", 8u), 0);
    ASSERT_TRUE(fwog_fs_close());

    /* append=false TRUNCATES. */
    ASSERT_TRUE(fwog_fs_open("a.txt", true, false));
    ASSERT_EQ(fwog_fs_size(), 0u);
    ASSERT_TRUE(fwog_fs_write("C", 1u));
    ASSERT_TRUE(fwog_fs_close());
    ASSERT_TRUE(fwog_fs_open("a.txt", false, false));
    ASSERT_EQ(fwog_fs_size(), 1u);
    ASSERT_TRUE(fwog_fs_close());
}

static void test_seek_tell_size(void) {
    fresh_volume();
    ASSERT_TRUE(fwog_fs_open("s.bin", true, false));
    for (unsigned i = 0u; i < 1000u; i++) s_buf[i] = (char)(i & 0xFFu);
    ASSERT_TRUE(fwog_fs_write(s_buf, 1000u));
    ASSERT_EQ(fwog_fs_tell(), 1000u);
    ASSERT_EQ(fwog_fs_size(), 1000u);
    ASSERT_TRUE(fwog_fs_close());

    ASSERT_TRUE(fwog_fs_open("s.bin", false, false));
    ASSERT_TRUE(fwog_fs_seek(500u));
    ASSERT_EQ(fwog_fs_tell(), 500u);
    size_t len = 4u;
    ASSERT_TRUE(fwog_fs_read(s_buf, &len));
    ASSERT_EQ(len, 4u);
    ASSERT_EQ((unsigned char)s_buf[0], 500u & 0xFFu);
    ASSERT_EQ(fwog_fs_tell(), 504u);

    /* A read at end of file is a SHORT READ, which is success with len 0 --
     * not an error. That distinction is the documented contract. */
    ASSERT_TRUE(fwog_fs_seek(1000u));
    len = 16u;
    ASSERT_TRUE(fwog_fs_read(s_buf, &len));
    ASSERT_EQ(len, 0u);
    ASSERT_TRUE(fwog_fs_close());
}

static void test_writes_across_sector_boundaries(void) {
    /* A write larger than one 4096-byte sector, started at an awkward offset
     * so it crosses a boundary mid-buffer. */
    fresh_volume();
    for (unsigned i = 0u; i < 6000u; i++) s_buf[i] = (char)((i * 31u) & 0xFFu);

    ASSERT_TRUE(fwog_fs_open("big.bin", true, false));
    ASSERT_TRUE(fwog_fs_write(s_buf, 1u));         /* land at offset 1 */
    ASSERT_TRUE(fwog_fs_write(s_buf, 6000u));      /* crosses 4096 at 4095 */
    ASSERT_TRUE(fwog_fs_close());

    ASSERT_TRUE(fwog_fs_open("big.bin", false, false));
    ASSERT_EQ(fwog_fs_size(), 6001u);
    static char back[8192];
    size_t len = sizeof(back);
    ASSERT_TRUE(fwog_fs_read(back, &len));
    ASSERT_EQ(len, 6001u);
    ASSERT_EQ(back[0], s_buf[0]);
    ASSERT_EQ(memcmp(back + 1, s_buf, 6000u), 0);
    ASSERT_TRUE(fwog_fs_close());
}

static void test_directories_and_enumeration(void) {
    fresh_volume();
    ASSERT_TRUE(fwog_fs_mkdir("logs"));
    ASSERT_TRUE(fwog_fs_exists("logs"));

    ASSERT_TRUE(fwog_fs_open("logs/one.txt", true, false));
    ASSERT_TRUE(fwog_fs_write("1", 1u));
    ASSERT_TRUE(fwog_fs_close());
    ASSERT_TRUE(fwog_fs_open("logs/two.txt", true, false));
    ASSERT_TRUE(fwog_fs_write("22", 2u));
    ASSERT_TRUE(fwog_fs_close());

    /* Walk by index until it returns false -- the documented loop shape. */
    char name[64];
    bool is_dir = false;
    unsigned count = 0u;
    int saw_one = 0, saw_two = 0;
    for (unsigned i = 0u; i < 32u; i++) {
        if (!fwog_fs_dir_entry("logs", i, name, sizeof(name), &is_dir)) break;
        count++;
        if (strcmp(name, "one.txt") == 0) { saw_one = 1; ASSERT_TRUE(!is_dir); }
        if (strcmp(name, "two.txt") == 0) { saw_two = 1; ASSERT_TRUE(!is_dir); }
    }
    ASSERT_EQ(count, 2u);
    ASSERT_TRUE(saw_one && saw_two);

    /* An index past the end is false, not a crash and not a stale name. */
    ASSERT_TRUE(!fwog_fs_dir_entry("logs", 99u, name, sizeof(name), &is_dir));

    /* The root sees the directory, flagged as one. */
    ASSERT_TRUE(fwog_fs_dir_entry("/", 0u, name, sizeof(name), &is_dir));
    ASSERT_EQ(strcmp(name, "logs"), 0);
    ASSERT_TRUE(is_dir);
}

static void test_rename_and_remove(void) {
    fresh_volume();
    ASSERT_TRUE(fwog_fs_open("old.txt", true, false));
    ASSERT_TRUE(fwog_fs_write("x", 1u));
    ASSERT_TRUE(fwog_fs_close());

    ASSERT_TRUE(fwog_fs_rename("old.txt", "new.txt"));
    ASSERT_TRUE(!fwog_fs_exists("old.txt"));
    ASSERT_TRUE(fwog_fs_exists("new.txt"));

    ASSERT_TRUE(fwog_fs_remove("new.txt"));
    ASSERT_TRUE(!fwog_fs_exists("new.txt"));
    ASSERT_TRUE(!fwog_fs_remove("new.txt"));   /* already gone */
}

static void test_long_file_names_round_trip(void) {
    /* FF_USE_LFN is 1 and is otherwise untested. A name longer than 8.3 that
     * survives a remount is the whole check -- if LFN were off, this would
     * come back mangled to an 8.3 name rather than failing outright. */
    fresh_volume();
    static const char *lfn =
        "a-considerably-longer-than-eight-dot-three-file-name.log";
    ASSERT_TRUE(fwog_fs_open(lfn, true, false));
    ASSERT_TRUE(fwog_fs_write("y", 1u));
    ASSERT_TRUE(fwog_fs_close());

    ASSERT_TRUE(fwog_fs_unmount());
    ASSERT_TRUE(fwog_fs_mount());
    ASSERT_TRUE(fwog_fs_exists(lfn));

    char name[128];
    bool is_dir = false;
    ASSERT_TRUE(fwog_fs_dir_entry("/", 0u, name, sizeof(name), &is_dir));
    ASSERT_EQ(strcmp(name, lfn), 0);

    /* And truncation into a short buffer must still NUL-terminate rather than
     * running off the end -- strncpy's classic failure. */
    char small[10];
    memset(small, 'Z', sizeof(small));
    ASSERT_TRUE(fwog_fs_dir_entry("/", 0u, small, sizeof(small), &is_dir));
    ASSERT_EQ(small[sizeof(small) - 1u], '\0');
    ASSERT_EQ(strlen(small), sizeof(small) - 1u);
}

static void test_one_open_file_at_a_time(void) {
    fresh_volume();
    ASSERT_TRUE(fwog_fs_open("f1.txt", true, false));
    /* The second open must FAIL, not silently close the first and lose its
     * buffered writes. */
    ASSERT_TRUE(!fwog_fs_open("f2.txt", true, false));
    ASSERT_TRUE(fwog_fs_is_open());
    ASSERT_TRUE(fwog_fs_write("kept", 4u));
    ASSERT_TRUE(fwog_fs_close());

    ASSERT_TRUE(fwog_fs_open("f1.txt", false, false));
    ASSERT_EQ(fwog_fs_size(), 4u);
    ASSERT_TRUE(fwog_fs_close());
    /* f2 was never created. */
    ASSERT_TRUE(!fwog_fs_exists("f2.txt"));
}

static void test_operations_fail_cleanly_when_not_mounted(void) {
    (void)fwog_fs_close();
    (void)fwog_fs_unmount();
    ASSERT_TRUE(!fwog_fs_exists("x"));
    ASSERT_TRUE(!fwog_fs_open("x", false, false));
    ASSERT_TRUE(!fwog_fs_mkdir("x"));
    ASSERT_TRUE(!fwog_fs_remove("x"));
    ASSERT_TRUE(!fwog_fs_rename("x", "y"));
    ASSERT_TRUE(!fwog_fs_volume_info(NULL, NULL));
    /* And with nothing open, the file accessors are defined, not garbage. */
    ASSERT_EQ(fwog_fs_tell(), 0u);
    ASSERT_EQ(fwog_fs_size(), 0u);
    ASSERT_TRUE(!fwog_fs_seek(0u));
    size_t len = 4u;
    ASSERT_TRUE(!fwog_fs_read(s_buf, &len));
    ASSERT_TRUE(!fwog_fs_write("z", 1u));
}

static void test_volume_full_fails_rather_than_corrupting(void) {
    fresh_volume();
    /* Make the disk reject writes past an early sector, standing in for a
     * volume with no free space, so this does not have to write 8 MB. */
    fs_ramdisk_fail_writes_from(64u);

    ASSERT_TRUE(fwog_fs_open("fill.bin", true, false));
    memset(s_buf, 'F', sizeof(s_buf));
    bool saw_failure = false;
    for (unsigned i = 0u; i < 200u; i++) {
        if (!fwog_fs_write(s_buf, sizeof(s_buf))) { saw_failure = true; break; }
    }
    ASSERT_TRUE(saw_failure);   /* reported, not silently truncated */
    (void)fwog_fs_close();

    /* The volume must still be mountable afterwards -- a full disk is a
     * refusal, not corruption. */
    fs_ramdisk_fail_writes_from(0xFFFFFFFFu);
    ASSERT_TRUE(fwog_fs_unmount());
    ASSERT_TRUE(fwog_fs_mount());
}

static void test_preallocate(void) {
    fresh_volume();
    ASSERT_TRUE(fwog_fs_open("pre.bin", true, false));
    ASSERT_TRUE(fwog_fs_preallocate(64u * 1024u));
    ASSERT_EQ(fwog_fs_size(), 64u * 1024u);
    ASSERT_TRUE(fwog_fs_close());

    ASSERT_TRUE(fwog_fs_unmount());
    ASSERT_TRUE(fwog_fs_mount());
    ASSERT_TRUE(fwog_fs_open("pre.bin", false, false));
    ASSERT_EQ(fwog_fs_size(), 64u * 1024u);
    ASSERT_TRUE(fwog_fs_close());
}

int main(void) {
    test_mount_refuses_an_unformatted_volume();
    test_format_mount_and_volume_info();
    test_write_close_remount_read_back();
    test_append_versus_truncate();
    test_seek_tell_size();
    test_writes_across_sector_boundaries();
    test_directories_and_enumeration();
    test_rename_and_remove();
    test_long_file_names_round_trip();
    test_one_open_file_at_a_time();
    test_operations_fail_cleanly_when_not_mounted();
    test_volume_full_fails_rather_than_corrupting();
    test_preallocate();
    TEST_RETURN();
}
