#include "leds/ws2812_driver.h"

/* ---- Pure: colour packing and clock-divider arithmetic. See the header for
 * why these two, specifically, are this driver's host-tested surface: they
 * are the two places a port of this file breaks silently (a red/green swap,
 * or a divider computed against the wrong clock). ---- */

uint32_t ws2812_pack_grb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)g << 24) | ((uint32_t)r << 16) | ((uint32_t)b << 8);
}

float ws2812_clkdiv(uint32_t sys_clk_hz) {
    int cycles_per_bit = WS2812_T1 + WS2812_T2 + WS2812_T3;
    return (float)sys_clk_hz / ((float)WS2812_FREQ_HZ * (float)cycles_per_bit);
}

/* The WS2812 PIO program, transcribed UNCHANGED from the four
 * obPIO.encode_* calls at rpNeoPixel.cpp:203-212 -- delay values, jump
 * targets, wrap points, AND side-set polarity, all exactly as encoded there.
 *
 * This is plain data with no SDK dependency, so it lives here (not behind
 * HOST_TEST) specifically so a test can pin these four words and catch
 * anyone "fixing" the polarity again -- see the hardware record fact 33
 * and the header's long note for why the polarity that looks backwards
 * against every stock WS2812 example is the one this board needs: IC6, a
 * TC7SZ04F inverter, sits between PIN_LED_DATA and the LED chain's DIN. */
const uint16_t ws2812_program_instructions[4] = {
    0x7221u,
    0x0123u,
    0x0400u,
    0xb442u,
};

#ifndef HOST_TEST
#include "common/diag.h"
#include "platform/board.h"
#include "hardware/clocks.h"

static const struct pio_program ws2812_program = {
    .instructions = ws2812_program_instructions,
    .length = 4u,
    .origin = -1,
};

/* One retained state machine's worth of driver state. Only one WS2812 chain
 * exists on this board, so -- like input/buttons.c's per-button debounce
 * state and lis3dh.c's s_track -- this is a single static instance rather
 * than a context struct threaded through every call. */
static PIO      s_pio;
static uint     s_sm;
static bool     s_ready;

typedef struct {
    uint8_t r, g, b;
} ws2812_pixel_t;

static ws2812_pixel_t s_pixels[FWOG_LED_COUNT];

bool ws2812_init(PIO pio, uint sm) {
    /* Mark not-ready up front, not just on the success path: a second
     * ws2812_init() call that fails (e.g. the shared PIO block filled up
     * after PDM/I2S loaded their own programs) must not leave
     * ws2812_process() still pushing words at whatever pio/sm an EARLIER,
     * possibly now-irrelevant call configured. */
    s_ready = false;

    if (!pio_can_add_program(pio, &ws2812_program)) {
        /* Checked, not asserted: this PIO block is meant to end up shared
         * with PDM and I2S (recon-dma.md), so a block that is already full
         * is a reachable integration mistake, not a "can't happen". */
        DIAG("[ws2812] no room for the PIO program on pio%u\n",
             (unsigned)pio_get_index(pio));
        return false;
    }
    int offset = pio_add_program(pio, &ws2812_program);

    pio_gpio_init(pio, PIN_LED_DATA);
    pio_sm_set_consecutive_pindirs(pio, sm, PIN_LED_DATA, 1u, true);

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, (uint)offset, (uint)offset + 3u);
    sm_config_set_sideset(&c, 1, /*optional=*/false, /*pindirs=*/false);
    sm_config_set_sideset_pins(&c, PIN_LED_DATA);
    /* out_shift_right=false (MSB first), autopull=true, threshold=24 --
     * rpNeoPixel.cpp:180's setupFIFOs(false, false, 0, false, true, true, 24,
     * false): input side unused (bUseIn=false), so both FIFOs join into TX,
     * matching PIO_FIFO_JOIN_TX below. */
    sm_config_set_out_shift(&c, false, true, 24u);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    float div = ws2812_clkdiv((uint32_t)clock_get_hz(clk_sys));
    sm_config_set_clkdiv(&c, div);

    pio_sm_init(pio, sm, (uint)offset, &c);
    pio_sm_set_enabled(pio, sm, true);

    s_pio = pio;
    s_sm = sm;
    for (unsigned i = 0; i < FWOG_LED_COUNT; i++) {
        s_pixels[i].r = 0u; s_pixels[i].g = 0u; s_pixels[i].b = 0u;
    }
    s_ready = true;
    return true;
}

void ws2812_set_color(unsigned pixel, uint8_t r, uint8_t g, uint8_t b) {
    if (pixel >= FWOG_LED_COUNT) return;   /* see the header's bounds-check note */
    s_pixels[pixel].r = r;
    s_pixels[pixel].g = g;
    s_pixels[pixel].b = b;
}

bool ws2812_get_color(unsigned pixel, uint8_t *r, uint8_t *g, uint8_t *b) {
    if (pixel >= FWOG_LED_COUNT) return false;
    if (r) *r = s_pixels[pixel].r;
    if (g) *g = s_pixels[pixel].g;
    if (b) *b = s_pixels[pixel].b;
    return true;
}

bool ws2812_ready(void) { return s_ready; }

void ws2812_process(void) {
    if (!s_ready) return;
    for (unsigned i = 0; i < FWOG_LED_COUNT; i++) {
        uint32_t word = ws2812_pack_grb(s_pixels[i].r, s_pixels[i].g, s_pixels[i].b);
        pio_sm_put_blocking(s_pio, s_sm, word);
    }
}
#else
#include "platform/board.h"
#include <string.h>

static struct { uint8_t r, g, b; } s_pixels[FWOG_LED_COUNT];
static bool s_ready;

bool ws2812_init(void *pio, unsigned sm) {
    (void)pio;
    (void)sm;
    memset(s_pixels, 0, sizeof s_pixels);
    s_ready = true;
    return true;
}

void ws2812_set_color(unsigned pixel, uint8_t r, uint8_t g, uint8_t b) {
    if (pixel >= (unsigned)FWOG_LED_COUNT) return;
    s_pixels[pixel].r = r;
    s_pixels[pixel].g = g;
    s_pixels[pixel].b = b;
}

bool ws2812_get_color(unsigned pixel, uint8_t *r, uint8_t *g, uint8_t *b) {
    if (pixel >= (unsigned)FWOG_LED_COUNT) return false;
    if (r) *r = s_pixels[pixel].r;
    if (g) *g = s_pixels[pixel].g;
    if (b) *b = s_pixels[pixel].b;
    return true;
}

void ws2812_process(void) { (void)s_ready; }

bool ws2812_ready(void) { return s_ready; }
#endif
