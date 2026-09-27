#include "gb_fs.h"
#include "common/diag.h"
#include "fs/fwog_fs.h"
#include "watchdog/watchdog.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

static bool s_vol;

static void hex_line(const uint8_t *p, unsigned n) {
    char line[80];
    unsigned i, o = 0u;
    static const char h[] = "0123456789abcdef";
    for (i = 0; i < n && o + 2u < sizeof line; i++) {
        line[o++] = h[p[i] >> 4];
        line[o++] = h[p[i] & 0x0Fu];
    }
    line[o] = '\0';
    DIAG("%s\n", line);
}

static void join_path(char *out, size_t cap, const char *dir, const char *name) {
    if (!out || cap < 2u) return;
    if (!dir || dir[0] == '\0' || (dir[0] == '/' && dir[1] == '\0')) {
        snprintf(out, cap, "/%s", name ? name : "");
    } else {
        snprintf(out, cap, "%s/%s", dir, name ? name : "");
    }
}

static bool vol_ok(void) {
    if (s_vol) return true;
    s_vol = fwog_fs_mount();
    return s_vol;
}

static void dump_file(const char *path) {
    uint8_t buf[32];
    size_t n;
    uint32_t size;

    board_watchdog_kick();
    if (!fwog_fs_open(path, false, false)) {
        DIAG("# skip %s (open)\n", path);
        return;
    }
    size = fwog_fs_size();
    DIAG("FILE %s %u\n", path, (unsigned)size);
    for (;;) {
        n = sizeof buf;
        board_watchdog_kick();
        if (!fwog_fs_read(buf, &n) || n == 0u) break;
        hex_line(buf, (unsigned)n);
    }
    (void)fwog_fs_close();
}

static void walk_dir(const char *dir, unsigned depth) {
    char name[48];
    char path[96];
    bool is_dir;
    unsigned i;

    if (depth > 8u) return;
    for (i = 0; i < 256u; i++) {
        board_watchdog_kick();
        if (!fwog_fs_dir_entry(dir, i, name, sizeof name, &is_dir)) break;
        if (name[0] == '.') continue;
        join_path(path, sizeof path, dir, name);
        if (is_dir) walk_dir(path, depth + 1u);
        else dump_file(path);
    }
}

void gb_fs_dump(void) {
    if (!vol_ok()) {
        DIAG("FSBK1\n");
        DIAG("# no FatFs volume\n");
        DIAG("END\n");
        return;
    }
    DIAG("FSBK1\n");
    walk_dir("/", 0u);
    DIAG("END\n");
    DIAG("[glassbak] dump done\n");
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (int)(c - 'A' + 10);
    return -1;
}

static void ensure_parents(const char *path) {
    char buf[96];
    unsigned i;
    if (!path || path[0] != '/') return;
    for (i = 1; path[i] && i < sizeof buf; i++) {
        if (path[i] != '/') continue;
        memcpy(buf, path, i);
        buf[i] = '\0';
        if (buf[1] != '\0' && !fwog_fs_exists(buf)) (void)fwog_fs_mkdir(buf);
    }
}

static char     s_line[160];
static unsigned s_llen;
static char     s_put_path[96];
static bool     s_put_open;
static uint32_t s_put_left;

static void put_close(void) {
    if (s_put_open) {
        (void)fwog_fs_close();
        s_put_open = false;
        DIAG("[glassbak] restored %s\n", s_put_path);
    }
}

static void put_begin(const char *path, uint32_t size) {
    put_close();
    if (!vol_ok() || !path || path[0] != '/') return;
    strncpy(s_put_path, path, sizeof s_put_path - 1u);
    s_put_path[sizeof s_put_path - 1u] = '\0';
    ensure_parents(s_put_path);
    if (!fwog_fs_open(s_put_path, true, false)) {
        DIAG("[glassbak] put open fail %s\n", s_put_path);
        return;
    }
    s_put_open = true;
    s_put_left = size;
}

static void put_hex(const char *hex) {
    uint8_t buf[32];
    unsigned n = 0u;
    while (hex && hex[0] && hex[1] && n < sizeof buf) {
        const int hi = hexval(hex[0]);
        const int lo = hexval(hex[1]);
        hex += 2;
        if (hi < 0 || lo < 0) continue;
        buf[n++] = (uint8_t)((hi << 4) | lo);
    }
    if (!s_put_open || n == 0u) return;
    if (s_put_left && n > s_put_left) n = (unsigned)s_put_left;
    board_watchdog_kick();
    (void)fwog_fs_write(buf, n);
    if (s_put_left > n) s_put_left -= n;
    else s_put_left = 0;
}

static void handle_line(char *line) {
    char path[96];
    unsigned size = 0u;
    if (!line || !line[0]) return;
    if (strcmp(line, "END") == 0) {
        put_close();
        return;
    }
    if (strncmp(line, "PUT ", 4) == 0) {
        if (sscanf(line + 4, "%95s %u", path, &size) == 2) put_begin(path, size);
        return;
    }
    if (line[0] == '#' || strcmp(line, "FSBK1") == 0) return;
    if (strncmp(line, "FILE ", 5) == 0) return;
    put_hex(line);
}

void gb_fs_poll_restore(void) {
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c == '\r') continue;
        if (c == '\n') {
            s_line[s_llen] = '\0';
            handle_line(s_line);
            s_llen = 0u;
            continue;
        }
        if (s_llen + 1u < sizeof s_line) s_line[s_llen++] = (char)c;
        else s_llen = 0u;
    }
}
