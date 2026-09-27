#include "lcd/fwog_t9.h"
#include <string.h>

const char *const fwog_t9_group[FWOG_T9_NGROUP] = {
    "ABC", "DEF", "GHI", "JKL", "MNO", "PQRS", "TUV", "WXYZ",
};

char fwog_t9_cur(int group, int letter) {
    const char *g;
    if (group < 0 || group >= FWOG_T9_NGROUP) group = 0;
    g = fwog_t9_group[group];
    if (letter < 0) letter = 0;
    if (g[letter]) return g[letter];
    return g[0];
}

void fwog_t9_insert(char *buf, size_t cap, char c) {
    size_t n;
    if (!buf || cap < 2u) return;
    n = strlen(buf);
    if (n + 1u >= cap) return;
    buf[n] = c;
    buf[n + 1u] = '\0';
}

void fwog_t9_backspace(char *buf) {
    size_t n;
    if (!buf) return;
    n = strlen(buf);
    if (!n) return;
    buf[n - 1u] = '\0';
}

void fwog_t9_group_prev(int *group, int *letter) {
    if (!group) return;
    if (--*group < 0) *group = FWOG_T9_NGROUP - 1;
    if (letter) *letter = 0;
}

void fwog_t9_group_next(int *group, int *letter) {
    if (!group) return;
    *group = (*group + 1) % FWOG_T9_NGROUP;
    if (letter) *letter = 0;
}

void fwog_t9_letter_prev(int group, int *letter) {
    const char *g;
    if (!letter) return;
    if (group < 0 || group >= FWOG_T9_NGROUP) group = 0;
    g = fwog_t9_group[group];
    if (*letter > 0) {
        (*letter)--;
        return;
    }
    *letter = 0;
    while (g[*letter + 1]) (*letter)++;
}

void fwog_t9_letter_next(int group, int *letter) {
    const char *g;
    if (!letter) return;
    if (group < 0 || group >= FWOG_T9_NGROUP) group = 0;
    g = fwog_t9_group[group];
    if (g[*letter + 1]) (*letter)++;
    else *letter = 0;
}
