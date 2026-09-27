/* Five-button T9 letter groups. Same map as OpticClick / PingHalo. */
#ifndef FWOG_T9_H
#define FWOG_T9_H
#include <stddef.h>

#define FWOG_T9_NGROUP 8
extern const char *const fwog_t9_group[FWOG_T9_NGROUP];

char fwog_t9_cur(int group, int letter);
void fwog_t9_insert(char *buf, size_t cap, char c);
void fwog_t9_backspace(char *buf);
void fwog_t9_group_prev(int *group, int *letter);
void fwog_t9_group_next(int *group, int *letter);
void fwog_t9_letter_prev(int group, int *letter);
void fwog_t9_letter_next(int group, int *letter);

#endif
