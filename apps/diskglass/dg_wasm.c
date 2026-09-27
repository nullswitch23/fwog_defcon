#include "dg_wasm.h"
#include "dg_proto.h"
#include "wasm3.h"
#ifndef HOST_TEST
#include "watchdog/watchdog.h"
#include "pico/stdlib.h"
#include "common/diag.h"
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static dg_wasm_led_fn   s_led;
static dg_wasm_fb_fn    s_fb;
static dg_wasm_yield_fn s_yield;

void dg_wasm_set_led_hook(dg_wasm_led_fn fn) { s_led = fn; }
void dg_wasm_set_fb_hook(dg_wasm_fb_fn fn) { s_fb = fn; }
void dg_wasm_set_yield_hook(dg_wasm_yield_fn fn) { s_yield = fn; }

static bool wasm_abort(void) {
    return s_yield && s_yield();
}

bool dg_wasm_is(const void *buf, size_t n) {
    const uint8_t *p = (const uint8_t *)buf;
    return p && n >= 4u && p[0] == 0u && p[1] == 'a' && p[2] == 's' && p[3] == 'm';
}

#ifndef HOST_TEST
#define DG_WASM_KICK() board_watchdog_kick()
#else
#define DG_WASM_KICK() ((void)0)
#endif

#ifndef HOST_TEST
M3Result m3_Yield(void) {
    board_watchdog_kick();
    if (wasm_abort()) return DG_WASM_STOPPED;
    return m3Err_none;
}
#endif

static void host_log(const char *s) {
#ifndef HOST_TEST
    if (s && s[0]) DIAG("[wasm] %s\n", s);
#else
    (void)s;
#endif
}

m3ApiRawFunction(host_waitms) {
    m3ApiGetArg(int32_t, ms);
#ifndef HOST_TEST
    if (ms < 0) ms = 0;
    if (ms > 8000) ms = 8000;
    {
        int left = ms;
        while (left > 0) {
            int slice = left > 100 ? 100 : left;
            sleep_ms((uint32_t)slice);
            board_watchdog_kick();
            if (wasm_abort()) m3ApiTrap(DG_WASM_STOPPED);
            left -= slice;
        }
    }
#else
    (void)ms;
    if (wasm_abort()) m3ApiTrap(DG_WASM_STOPPED);
#endif
    m3ApiSuccess();
}

m3ApiRawFunction(host_millis) {
    m3ApiReturnType(uint32_t);
#ifndef HOST_TEST
    m3ApiReturn(to_ms_since_boot(get_absolute_time()));
#else
    m3ApiReturn(0u);
#endif
}

m3ApiRawFunction(host_rand) {
    m3ApiReturnType(int32_t);
    m3ApiReturn((int32_t)(rand() & 0x7fffffff));
}

m3ApiRawFunction(host_set_led) {
    m3ApiGetArg(int32_t, idx);
    m3ApiGetArg(int32_t, r);
    m3ApiGetArg(int32_t, g);
    m3ApiGetArg(int32_t, b);
    m3ApiGetArg(int32_t, dur);
    m3ApiGetArg(int32_t, mode);
    (void)dur;
    (void)mode;
    if (s_led) s_led((int)idx, (int)r, (int)g, (int)b);
    DG_WASM_KICK();
    if (wasm_abort()) m3ApiTrap(DG_WASM_STOPPED);
    m3ApiSuccess();
}

m3ApiRawFunction(host_show_gfx) {
    m3ApiGetArgMem(const uint8_t *, p);
    m3ApiGetArg(int32_t, w);
    m3ApiGetArg(int32_t, h);
    if (m3ApiIsNullPtr(p) || w <= 0 || h <= 0) m3ApiSuccess();
    if (w > (int32_t)DG_FB_W) w = (int32_t)DG_FB_W;
    if (h > (int32_t)DG_FB_H) h = (int32_t)DG_FB_H;
    {
        uint32_t n = (uint32_t)w * (uint32_t)h;
        if (n == 0u) m3ApiSuccess();
        m3ApiCheckMem(p, n);
#ifndef HOST_TEST
        board_watchdog_kick();
#endif
        if (s_fb) s_fb(p, (int)w, (int)h);
        if (wasm_abort()) m3ApiTrap(DG_WASM_STOPPED);
    }
    m3ApiSuccess();
}

m3ApiRawFunction(host_term) {
    m3ApiGetArgMem(const char *, s);
    if (s && !m3ApiIsNullPtr(s)) host_log(s);
    m3ApiSuccess();
}

m3ApiRawFunction(host_print_int) {
    m3ApiGetArgMem(const char *, fmt);
    m3ApiGetArg(int32_t, color);
    m3ApiGetArg(int32_t, dtype);
    m3ApiGetArg(int32_t, val);
    (void)color;
    (void)dtype;
    (void)fmt;
#ifndef HOST_TEST
    DIAG("[wasm] %d\n", (int)val);
#else
    (void)val;
#endif
    m3ApiSuccess();
}

m3ApiRawFunction(stub_v) { m3ApiSuccess(); }

m3ApiRawFunction(stub_v_i) {
    m3ApiGetArg(int32_t, a);
    (void)a;
    m3ApiSuccess();
}

m3ApiRawFunction(stub_v_ii) {
    m3ApiGetArg(int32_t, a);
    m3ApiGetArg(int32_t, b);
    (void)a; (void)b;
    m3ApiSuccess();
}

m3ApiRawFunction(stub_v_iii) {
    m3ApiGetArg(int32_t, a);
    m3ApiGetArg(int32_t, b);
    m3ApiGetArg(int32_t, c);
    (void)a; (void)b; (void)c;
    m3ApiSuccess();
}

m3ApiRawFunction(stub_v_star) {
    m3ApiGetArg(uint32_t, p);
    (void)p;
    m3ApiSuccess();
}

m3ApiRawFunction(stub_v_star_i) {
    m3ApiGetArg(uint32_t, p);
    m3ApiGetArg(int32_t, a);
    (void)p; (void)a;
    m3ApiSuccess();
}

m3ApiRawFunction(stub_v_iiiiii) {
    int k;
    for (k = 0; k < 6; k++) {
        m3ApiGetArg(int32_t, a);
        (void)a;
    }
    m3ApiSuccess();
}

m3ApiRawFunction(stub_v_9i) {
    int k;
    for (k = 0; k < 9; k++) {
        m3ApiGetArg(int32_t, a);
        (void)a;
    }
    m3ApiSuccess();
}

m3ApiRawFunction(stub_v_12i) {
    int k;
    for (k = 0; k < 12; k++) {
        m3ApiGetArg(int32_t, a);
        (void)a;
    }
    m3ApiSuccess();
}

m3ApiRawFunction(stub_v_14i) {
    int k;
    for (k = 0; k < 14; k++) {
        m3ApiGetArg(int32_t, a);
        (void)a;
    }
    m3ApiSuccess();
}

m3ApiRawFunction(stub_v_17i) {
    int k;
    for (k = 0; k < 17; k++) {
        m3ApiGetArg(int32_t, a);
        (void)a;
    }
    m3ApiSuccess();
}

m3ApiRawFunction(stub_i) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(0);
}

m3ApiRawFunction(stub_i_i) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, a);
    (void)a;
    m3ApiReturn(0);
}

m3ApiRawFunction(stub_i_ii) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, a);
    m3ApiGetArg(int32_t, b);
    (void)a; (void)b;
    m3ApiReturn(0);
}

m3ApiRawFunction(stub_i_star) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, p);
    (void)p;
    m3ApiReturn(0);
}

m3ApiRawFunction(stub_i_star_i) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, p);
    m3ApiGetArg(int32_t, a);
    (void)p; (void)a;
    m3ApiReturn(0);
}

m3ApiRawFunction(stub_i_i_star_i) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, a);
    m3ApiGetArg(uint32_t, p);
    m3ApiGetArg(int32_t, b);
    (void)a; (void)p; (void)b;
    m3ApiReturn(0);
}

m3ApiRawFunction(stub_i_ii_star_i) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, a);
    m3ApiGetArg(int32_t, b);
    m3ApiGetArg(uint32_t, p);
    m3ApiGetArg(int32_t, c);
    (void)a; (void)b; (void)p; (void)c;
    m3ApiReturn(0);
}

m3ApiRawFunction(stub_i_star_i_star) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, a);
    m3ApiGetArg(int32_t, n);
    m3ApiGetArg(uint32_t, b);
    (void)a; (void)n; (void)b;
    m3ApiReturn(0);
}

m3ApiRawFunction(stub_i_iff) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, a);
    m3ApiGetArg(float, f);
    m3ApiGetArg(float, g);
    (void)a; (void)f; (void)g;
    m3ApiReturn(0);
}

m3ApiRawFunction(stub_v_fff_i) {
    m3ApiGetArg(float, a);
    m3ApiGetArg(float, b);
    m3ApiGetArg(float, c);
    m3ApiGetArg(int32_t, d);
    (void)a; (void)b; (void)c; (void)d;
    m3ApiSuccess();
}

m3ApiRawFunction(stub_v_iif_i) {
    m3ApiGetArg(int32_t, a);
    m3ApiGetArg(int32_t, b);
    m3ApiGetArg(float, f);
    m3ApiGetArg(int32_t, c);
    (void)a; (void)b; (void)f; (void)c;
    m3ApiSuccess();
}

m3ApiRawFunction(stub_v_star_i_f) {
    m3ApiGetArg(uint32_t, p);
    m3ApiGetArg(int32_t, a);
    m3ApiGetArg(float, f);
    (void)p; (void)a; (void)f;
    m3ApiSuccess();
}

m3ApiRawFunction(stub_v_star_star) {
    m3ApiGetArg(uint32_t, a);
    m3ApiGetArg(uint32_t, b);
    (void)a; (void)b;
    m3ApiSuccess();
}

typedef struct {
    const char *name;
    const char *sig;
    M3RawCall  fn;
} dg_import_t;

static const dg_import_t k_imp[] = {
    { "waitms", "v(i)", &host_waitms },
    { "millis", "i()", &host_millis },
    { "wilirand", "i()", &host_rand },
    { "setBoardLED", "v(iiiiii)", &host_set_led },
    { "showGfx", "v(iii)", &host_show_gfx },
    { "terminalWrite", "v(*)", &host_term },
    { "printInt", "v(*iii)", &host_print_int },
    { "printFloat", "v(*if)", &stub_v_star_i_f },
    { "setIO", "v(ii)", &stub_v_ii },
    { "getIO", "i(i)", &stub_i_i },
    { "getAllIO", "i()", &stub_i },
    { "i2cRead", "i(ii*i)", &stub_i_ii_star_i },
    { "i2cWrite", "i(ii*i)", &stub_i_ii_star_i },
    { "SPIReadWrite", "i(*i*)", &stub_i_star_i_star },
    { "sendIRData", "v(i)", &stub_v_i },
    { "setLEDShowMode", "v(i)", &stub_v_i },
    { "hasEvent", "i()", &stub_i },
    { "getEventData", "i(*)", &stub_i_star },
    { "exitToMainAppMenu", "v()", &stub_v },
    { "OpenFile", "i(*i)", &stub_i_star_i },
    { "closeFile", "i(i)", &stub_i_i },
    { "writeFile", "i(i*i)", &stub_i_i_star_i },
    { "readFile", "i(i**)", &stub_i_i_star_i },
    { "fileExists", "i(*)", &stub_i_star },
    { "makeDirectory", "i(*)", &stub_i_star },
    { "changeDirectory", "i(*)", &stub_i_star },
    { "removeFileOrDirectory", "i(*)", &stub_i_star },
    { "getVolumeInfo", "v(**)", &stub_v_star_star },
    { "RadioWrite", "i(i*i)", &stub_i_i_star_i },
    { "RadioRead", "i(i*i)", &stub_i_i_star_i },
    { "RadioSetTx", "i(i)", &stub_i_i },
    { "RadioSetRx", "i(i)", &stub_i_i },
    { "RadioSetIdle", "i(i)", &stub_i_i },
    { "RadioGetRSSI", "i(i)", &stub_i_i },
    { "PWMSetFreqDuty", "i(iff)", &stub_i_iff },
    { "PWMStop", "i(i)", &stub_i_i },
    { "playSoundFromFile", "v(*)", &stub_v_star },
    { "playSoundFromFrequencyAndDuration", "v(fffi)", &stub_v_fff_i },
    { "playSoundFromNumber", "v(iifi)", &stub_v_iif_i },
    { "addPanel", "v(iiiiiiiii)", &stub_v_9i },
    { "showPanel", "v(i)", &stub_v_i },
    { "addControlPicture", "v(iiiiii)", &stub_v_iiiiii },
    { "addControlPlot", "v(iiiiiiiiiiii)", &stub_v_12i },
    { "addControlNumber", "v(iiiiiiiiiiiiii)", &stub_v_14i },
    { "addControlLogList", "v(iiiiiiiiiiiiiiiii)", &stub_v_17i },
    { "setControlValue", "v(iii)", &stub_v_iii },
    { "setControlValueText", "v(ii*)", &stub_v_star_i },
    { "loadFPGAFromFile", "i(*)", &stub_i_star },
    { "UARTDataWrite", "i(*i)", &stub_i_star_i },
    { "UARTDataRead", "i(*i)", &stub_i_star_i },
    { "UARTDataRxCount", "i()", &stub_i },
};

static void link_wili(IM3Module mod) {
    unsigned i;
    for (i = 0; i < (unsigned)(sizeof k_imp / sizeof k_imp[0]); i++) {
        M3Result r = m3_LinkRawFunction(mod, "wiliwasm",
                                        k_imp[i].name, k_imp[i].sig, k_imp[i].fn);
        (void)r;
    }
}

static IM3Function find_entry(IM3Runtime rt) {
    static const char *names[] = {
        "main", "_start", "__main_void", "start", "app_main"
    };
    unsigned i;
    for (i = 0; i < (unsigned)(sizeof names / sizeof names[0]); i++) {
        IM3Function f = NULL;
        if (m3_FindFunction(&f, rt, names[i]) == m3Err_none && f) return f;
    }
    return NULL;
}

bool dg_wasm_run(const uint8_t *bytes, size_t n, bool loop,
                 char *err, size_t err_cap) {
    IM3Environment env;
    IM3Runtime rt;
    IM3Module mod;
    IM3Function fn;
    M3Result r;
    if (err && err_cap) err[0] = '\0';
    if (!bytes || n < 8u || !dg_wasm_is(bytes, n)) {
        if (err && err_cap) snprintf(err, err_cap, "not wasm");
        return false;
    }
    env = m3_NewEnvironment();
    if (!env) {
        if (err && err_cap) snprintf(err, err_cap, "no env");
        return false;
    }
    rt = m3_NewRuntime(env, 8u * 1024u, NULL);
    if (!rt) {
        m3_FreeEnvironment(env);
        if (err && err_cap) snprintf(err, err_cap, "no runtime");
        return false;
    }
    DG_WASM_KICK();
    r = m3_ParseModule(env, &mod, bytes, (uint32_t)n);
    if (r) goto fail;
    DG_WASM_KICK();
    r = m3_LoadModule(rt, mod);
    if (r) goto fail;
    link_wili(mod);
    DG_WASM_KICK();
    (void)m3_CompileModule(mod);
    DG_WASM_KICK();
    (void)m3_RunStart(mod);
    r = m3_FindFunction(&fn, rt, "_start");
    if (r || !fn) {
        fn = find_entry(rt);
        if (fn) r = NULL;
    }
    if (!fn) {
        if (!r) r = "no export";
        goto fail;
    }
    do {
        DG_WASM_KICK();
        if (wasm_abort()) {
            r = DG_WASM_STOPPED;
            goto fail;
        }
        r = m3_CallV(fn);
        if (r && r != m3Err_none) goto fail;
        if (wasm_abort()) {
            r = DG_WASM_STOPPED;
            goto fail;
        }
    } while (loop);
    m3_FreeRuntime(rt);
    m3_FreeEnvironment(env);
    return true;
fail:
    if (err && err_cap) {
        snprintf(err, err_cap, "%s", r ? r : "fail");
    }
    m3_FreeRuntime(rt);
    m3_FreeEnvironment(env);
    return false;
}
