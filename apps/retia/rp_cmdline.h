#ifndef RP_CMDLINE_H
#define RP_CMDLINE_H
#include <stdbool.h>
#include <stddef.h>

#define RP_CMD_MAX 256u

/* Read one command line (blocking). Echoes editing to the terminal. */
void rp_cmdline_read(const char *mode, char *out, size_t out_cap);
void rp_cmdline_reset(void);
/* Non-blocking. Returns true when a line is ready in out. */
bool rp_cmdline_poll(const char *mode, char *out, size_t out_cap);

#endif
