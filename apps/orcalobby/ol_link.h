#ifndef OL_LINK_H
#define OL_LINK_H
#include <stddef.h>
#include <stdbool.h>

bool ol_link_ok(void);
void ol_link_send(const void *m, size_t n);
bool ol_leds_ok(void);

#endif
