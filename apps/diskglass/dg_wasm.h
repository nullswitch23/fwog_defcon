/* wasm3 wrapper for DiskGlass. Host-tested with a tiny module.
 * Device build links the vendored interpreter and the wiliwasm imports. */
#ifndef DG_WASM_H
#define DG_WASM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DG_WASM_MAX 24576u

bool dg_wasm_is(const void *buf, size_t n);

typedef void (*dg_wasm_led_fn)(int idx, int r, int g, int b);
typedef void (*dg_wasm_fb_fn)(const uint8_t *px, int w, int h);

void dg_wasm_set_led_hook(dg_wasm_led_fn fn);
void dg_wasm_set_fb_hook(dg_wasm_fb_fn fn);

/* Return true from the hook to abort the current script. Polled from
 * waitms, showGfx, setBoardLED, and (on device) m3_Yield. */
typedef bool (*dg_wasm_yield_fn)(void);
void dg_wasm_set_yield_hook(dg_wasm_yield_fn fn);

#define DG_WASM_STOPPED "stopped"

/* Parse, link wiliwasm, call main/_start. err may be NULL.
 * If loop is true, _start is called again until the yield hook aborts.
 * False with err DG_WASM_STOPPED means the yield hook aborted. */
bool dg_wasm_run(const uint8_t *bytes, size_t n, bool loop,
                 char *err, size_t err_cap);

#endif
