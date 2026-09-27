#ifndef FD_LINK_H
#define FD_LINK_H
#include <stdbool.h>
#include <stddef.h>

bool fd_link_ok(void);
void fd_link_send(const void *m, size_t n);
bool fd_accel_ok(void);
bool fd_leds_ok(void);
bool fd_pdm_ok(void);

#endif
