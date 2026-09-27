#include "test_util.h"
#include "dg_wasm.h"
#include "smoke/dg_smoke_wasm.h"
#include <string.h>

/* Minimal module: export main() -> i32.const 42 */
static const uint8_t k_main42[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f,
    0x03, 0x02, 0x01, 0x00,
    0x07, 0x08, 0x01, 0x04, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x00,
    0x0a, 0x06, 0x01, 0x04, 0x00, 0x41, 0x2a, 0x0b
};

static int s_nled;
static int s_nfb;
static int s_fb_w;
static int s_fb_h;

static void on_led(int idx, int r, int g, int b) {
    (void)idx; (void)r; (void)g; (void)b;
    s_nled++;
}

static void on_fb(const uint8_t *px, int w, int h) {
    (void)px;
    s_nfb++;
    s_fb_w = w;
    s_fb_h = h;
}

static bool stop_after_two(void) {
    return s_nfb >= 2;
}

int main(void) {
    char err[40];
    ASSERT_TRUE(dg_wasm_is(k_main42, sizeof k_main42));
    ASSERT_TRUE(!dg_wasm_is("hello", 5));
    err[0] = 'x';
    ASSERT_TRUE(dg_wasm_run(k_main42, sizeof k_main42, false, err, sizeof err));
    ASSERT_TRUE(!dg_wasm_run((const uint8_t *)"nope", 4, false, err, sizeof err));

    ASSERT_TRUE(dg_wasm_is(k_dg_smoke_wasm, DG_SMOKE_WASM_LEN));
    dg_wasm_set_led_hook(on_led);
    dg_wasm_set_fb_hook(on_fb);
    err[0] = '\0';
    ASSERT_TRUE(dg_wasm_run(k_dg_smoke_wasm, DG_SMOKE_WASM_LEN, false, err, sizeof err));
    ASSERT_EQ(s_nfb, 48);
    ASSERT_EQ(s_nled, 48 * 7);
    ASSERT_EQ(s_fb_w, 32);
    ASSERT_EQ(s_fb_h, 24);

    s_nfb = 0;
    s_nled = 0;
    dg_wasm_set_yield_hook(stop_after_two);
    err[0] = '\0';
    ASSERT_TRUE(!dg_wasm_run(k_dg_smoke_wasm, DG_SMOKE_WASM_LEN, true, err, sizeof err));
    ASSERT_TRUE(strcmp(err, DG_WASM_STOPPED) == 0);
    ASSERT_EQ(s_nfb, 2);
    dg_wasm_set_yield_hook(NULL);
    TEST_RETURN();
}
