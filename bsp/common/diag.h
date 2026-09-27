/* DIAG() — the only text output channel in this BSP. Driver code must never
 * call printf directly.
 *
 * Default is USB CDC, because the OG is flashed by UF2 drag-and-drop and a
 * SEGGER RTT channel is unreadable without a debug probe. Build with
 * -DFWOG_DIAG=0 to compile diagnostics away entirely, which also drops
 * pico_stdio_usb and frees the USB peripheral for the application. */
#ifndef FWOG_DIAG_H
#define FWOG_DIAG_H

#define FWOG_DIAG_NONE 0
#define FWOG_DIAG_USB  1
#define FWOG_DIAG_RTT  2

#ifndef FWOG_DIAG
#define FWOG_DIAG FWOG_DIAG_USB
#endif

#if FWOG_DIAG == FWOG_DIAG_USB
#include <stdio.h>
#define DIAG(...) printf(__VA_ARGS__)
#elif FWOG_DIAG == FWOG_DIAG_RTT
#if defined(__has_include) && !__has_include("SEGGER_RTT.h")
#error "FWOG_DIAG_RTT selected but SEGGER RTT is not vendored into bsp/third_party/segger_rtt"
#endif
#include "SEGGER_RTT.h"
#define DIAG(...) SEGGER_RTT_printf(0, __VA_ARGS__)
#else
#define DIAG(...) ((void)0)
#endif

/* Prefixed to match fwog_crc*, fwog_link_*, and fwog_i2c_*. Every driver in
 * this BSP links fwog_common, so an unprefixed symbol here is a collision
 * waiting for the eighteenth driver. */
void fwog_diag_init(void);

#endif
