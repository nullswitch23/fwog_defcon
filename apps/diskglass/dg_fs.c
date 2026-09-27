#include "dg_fs.h"
#include "dg_file.h"
#include "fs/fwog_fs.h"

bool dg_fs_ready(void) {
    if (fwog_fs_mounted()) return true;
    return fwog_fs_mount();
}

bool dg_fs_ensure_dir(const char *path) {
    if (!path || !path[0]) return false;
    if (!dg_fs_ready()) return false;
    if (fwog_fs_exists(path)) return true;
    return fwog_fs_mkdir(path);
}

unsigned dg_fs_next_index(const char *dir, const char *prefix) {
    unsigned max = 0;
    char name[48];
    bool is_dir;
    unsigned i;
    if (!dg_fs_ready()) return 1u;
    for (i = 0; i < 256u; i++) {
        unsigned n;
        if (!fwog_fs_dir_entry(dir, i, name, sizeof name, &is_dir)) break;
        n = dg_index_from_name(name, prefix);
        if (n > max) max = n;
    }
    return max + 1u;
}
