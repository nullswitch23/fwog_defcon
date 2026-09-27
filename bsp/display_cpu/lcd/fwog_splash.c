#include "lcd/fwog_splash.h"
#include "lcd/st7789.h"
#include "lcd/lcd_text.h"
#include "platform/board.h"
#include <stdio.h>

static const char *s_name = "FreeWili OG";
static const char *s_ver  = "";
static bool        s_bound;

void fwog_splash_bind(const char *name, const char *version) {
    if (name != NULL && name[0] != '\0') s_name = name;
    s_ver = (version != NULL) ? version : "";
    s_bound = true;
}

#ifndef HOST_TEST
#include "lcd/splash_art.h"
#include "pico/stdlib.h"
#include "common/link/link_uart.h"
#include "common/link/link_frame.h"
#include "common/i2c_bus.h"
#include "io_expander/ioexp_link.h"
#include "io_expander/pcal6416.h"
#if (SPLASH_BOOT_PCM_SAMPLES > 0) || (SPLASH_SHIP_PCM_SAMPLES > 0)
#include "audio/i2s_audio.h"
#include "common/diag.h"
#include "hardware/pio.h"
#endif

/* Main can fwog_io_dir_apply() while this dwells — but only if the app
 * already called fwog_link_uart_init(). Most display apps splash first;
 * fwog_link_uart_read() is then a no-op until init. */
static void pump_ioexp(void) {
    static fwog_link_rx_t rx;
    static bool inited;
    uint8_t b;
    size_t n;
    if (!inited) {
        fwog_link_rx_init(&rx);
        inited = true;
    }
    while (fwog_link_uart_read(&b)) {
        if (!fwog_link_rx_byte(&rx, b, &n)) continue;
        (void)fwog_ioexp_link_handle(rx.buf, n);
    }
}

static void paint(bool boot) {
    if (!st7789_ready()) return;

    const uint16_t ink = st7789_rgb565(0, 0, 0);
    const uint16_t yel = st7789_rgb565(255, 214, 48);
    const uint16_t blu = st7789_rgb565(48, 120, 220);
    const uint16_t red = st7789_rgb565(232, 48, 40);
    const uint16_t grn = st7789_rgb565(48, 160, 96);
    char line[20];

    st7789_set_window(0u, 0u, SPLASH_W, SPLASH_H);
    st7789_blit(splash_image, SPLASH_PIXELS);

    const uint16_t tx = SPLASH_TITLE_X;
    const uint16_t ty = SPLASH_TITLE_Y;
    lcd_text_draw_padded(tx, ty, "FREEWILI OG", 18, 1, yel, ink);
    lcd_text_draw_padded(tx, (uint16_t)(ty + 12u),
                         boot ? s_name : "GOODBYE", 16, 2,
                         boot ? blu : red, ink);
    if (boot && s_ver[0] != '\0') {
        snprintf(line, sizeof line, "v%s", s_ver);
        lcd_text_draw_padded(tx, (uint16_t)(ty + 36u), line, 16, 1, grn, ink);
        lcd_text_draw_padded(tx, (uint16_t)(ty + 48u), "DEF CON", 16, 1, yel, ink);
    } else if (!boot) {
        lcd_text_draw_padded(tx, (uint16_t)(ty + 36u), s_name, 16, 1, yel, ink);
        lcd_text_draw_padded(tx, (uint16_t)(ty + 48u), "sign off", 16, 1, grn, ink);
    }
    st7789_dma_wait();
}

#if (SPLASH_BOOT_PCM_SAMPLES > 0) || (SPLASH_SHIP_PCM_SAMPLES > 0)
static void play_pcm(const uint8_t *pcm, unsigned n, bool is_8bit,
                     unsigned max_ms, float gain) {
    /* pio0 SM2 — WS2812 is SM0, PDM is SM1. Repeat init is a no-op. */
    bool started = i2s_audio_init(pio0, 2u);
    if (started) {
        /* 8-bit expand is (u8-128)*50, so a full-scale byte is ~19% of
         * int16. Ship uses ~5 so a peak-normalized clip reaches the amp. */
        i2s_audio_set_asset_gain(gain);
        started = i2s_audio_start(pcm, n, true, is_8bit);
    }
    if (!started) {
        DIAG("[splash] pcm not started\n");
        const unsigned wait = max_ms > 400u ? 400u : max_ms;
        for (unsigned i = 0; i < wait; i += 2u) {
            pump_ioexp();
            sleep_ms(2);
        }
        return;
    }
    const absolute_time_t deadline = make_timeout_time_ms(max_ms);
    while (!time_reached(deadline)) {
        pump_ioexp();
        i2s_audio_process();
        if (i2s_audio_is_idle()) break;
        sleep_ms(2);
    }
    i2s_audio_stop();
}
#endif

void fwog_splash_boot(void) {
    if (!s_bound) return;
    board_backlight(255);
    paint(true);
#if SPLASH_BOOT_PCM_SAMPLES > 0
    play_pcm(splash_boot_pcm, SPLASH_BOOT_PCM_SAMPLES, SPLASH_BOOT_PCM_8BIT,
             3000u, 1.0f);
#else
    for (unsigned i = 0; i < 1500u; i++) {
        pump_ioexp();
        sleep_ms(2);
    }
#endif
    /* App chrome is not an overlay. Wipe the box art before returning. */
    st7789_clear(st7789_rgb565(8, 10, 18));
    st7789_dma_wait();
    /* I2S splash DMA/PIO can leave i2c1 wedged; expander acks need it. */
    fwog_i2c_recover();
    (void)fwog_ioexp_init();
}

void fwog_splash_ship(void) {
    if (!s_bound) return;
    board_backlight(255);
    paint(false);
#if SPLASH_SHIP_PCM_SAMPLES > 0
    play_pcm(splash_ship_pcm, SPLASH_SHIP_PCM_SAMPLES, SPLASH_SHIP_PCM_8BIT,
             4000u, 5.0f);
#else
    sleep_ms(400);
#endif
}
#else
/* v001 ogemu: skip the 3 s dwell and the splash_art blob. Bind still
 * records the name so a later ship path has something to print. */
void fwog_splash_boot(void) { (void)s_bound; (void)s_name; (void)s_ver; }
void fwog_splash_ship(void) { (void)s_bound; }
#endif
