#include "common/diag.h"

#if FWOG_DIAG == FWOG_DIAG_USB
#include "pico/stdio.h"
void fwog_diag_init(void) { stdio_init_all(); }
#else
void fwog_diag_init(void) { }
#endif
