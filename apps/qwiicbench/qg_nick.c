#include "qg_nick.h"
#include <string.h>

static uint8_t s_used[QG_NICK_CAP];
static uint8_t s_addr[QG_NICK_CAP];
static char    s_name[QG_NICK_CAP][QG_NAME_N];

static int find_addr(uint8_t addr) {
    unsigned i;
    for (i = 0; i < QG_NICK_CAP; i++) {
        if (s_used[i] && s_addr[i] == addr) return (int)i;
    }
    return -1;
}

static int find_free(void) {
    unsigned i;
    for (i = 0; i < QG_NICK_CAP; i++) {
        if (!s_used[i]) return (int)i;
    }
    return -1;
}

void qg_nick_set(uint8_t addr, const char *name) {
    int i = find_addr(addr);
    if (!name || !name[0]) {
        if (i >= 0) {
            s_used[i] = 0u;
            s_addr[i] = 0u;
            s_name[i][0] = '\0';
        }
        return;
    }
    if (i < 0) i = find_free();
    if (i < 0) i = 0;
    s_used[i] = 1u;
    s_addr[i] = addr;
    memset(s_name[i], 0, sizeof s_name[i]);
    strncpy(s_name[i], name, QG_NAME_N - 1u);
}

const char *qg_nick_get(uint8_t addr) {
    const int i = find_addr(addr);
    if (i < 0) return NULL;
    return s_name[i];
}
