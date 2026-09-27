#ifndef PD_LINK_H
#define PD_LINK_H
#include <stddef.h>
#include <stdbool.h>

bool pd_link_ok(void);
void pd_link_send(const void *m, size_t n);

#endif
