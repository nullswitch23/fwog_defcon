#ifndef KH_LINK_H
#define KH_LINK_H
#include <stdbool.h>
#include <stddef.h>

bool kh_link_ok(void);
void kh_link_send(const void *m, size_t n);
void kh_link_pump(void);

#endif
