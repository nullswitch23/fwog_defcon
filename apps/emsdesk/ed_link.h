#ifndef ED_LINK_H
#define ED_LINK_H
#include <stddef.h>
#include <stdbool.h>

bool ed_link_ok(void);
void ed_link_send(const void *m, size_t n);

#endif
