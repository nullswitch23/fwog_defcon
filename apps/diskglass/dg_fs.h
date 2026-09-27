#ifndef DG_FS_H
#define DG_FS_H
#include <stdbool.h>
#include <stdint.h>

bool dg_fs_ready(void);
bool dg_fs_ensure_dir(const char *path);
unsigned dg_fs_next_index(const char *dir, const char *prefix);

#endif
