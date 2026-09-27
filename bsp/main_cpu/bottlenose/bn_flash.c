#include "bottlenose/bn_flash.h"
#include "bottlenose/c6_image.h"
#include "fwog_main.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

static bool s_swap_uart;
static bool s_last_rx_pio;

fwog_io_result_t fwog_bn_io_listen(void) {
    fwog_io_cfg_t cfg;
    fwog_io_cfg_default(&cfg);
    cfg.uart_tx_out = false;
    cfg.uart_rx_out = false;
    cfg.uart_cts_out = false;
    cfg.uart_rts_out = false;
    return fwog_io_dir_apply(&cfg);
}

static fwog_io_result_t io_apply_listen(void) {
    return fwog_bn_io_listen();
}

static fwog_io_result_t io_apply_orient(bool swap) {
    fwog_io_cfg_t cfg;
    fwog_io_cfg_default(&cfg);
    cfg.uart_cts_out = false;
    cfg.uart_rts_out = false;
    if (swap) {
        cfg.uart_tx_out = false;
        cfg.uart_rx_out = true;
    } else {
        cfg.uart_tx_out = true;
        cfg.uart_rx_out = false;
    }
    return fwog_io_dir_apply(&cfg);
}

fwog_io_result_t fwog_bn_io_swap(void) {
    fwog_io_cfg_t cfg;
    fwog_io_cfg_default(&cfg);
    cfg.uart_tx_out = false;
    cfg.uart_rx_out = true;
    cfg.uart_cts_out = false;
    cfg.uart_rts_out = false;
    return fwog_io_dir_apply(&cfg);
}

fwog_io_result_t fwog_bn_io_app(void) {
    /* Same data-pin dirs that received C6 ROM SLIP on GPIO9. Default
     * uart_rts_out=true points the shifter at GPIO 11; driving that pad
     * after a listen/SWAP pass fights the expander. Bottlenose has no
     * flow control. */
    fwog_io_cfg_t cfg;
    fwog_io_cfg_default(&cfg);
    cfg.uart_tx_out = true;
    cfg.uart_rx_out = false;
    cfg.uart_cts_out = false;
    cfg.uart_rts_out = false;
    return fwog_io_dir_apply(&cfg);
}

void fwog_bn_uart_listen(void) {
    gpio_init(PIN_IO_UART_CTS);
    gpio_set_dir(PIN_IO_UART_CTS, GPIO_IN);
    gpio_init(PIN_IO_UART_RTS);
    gpio_set_dir(PIN_IO_UART_RTS, GPIO_IN);
    s_swap_uart = esp_rom_swap_rx_begin();
    gpio_set_function(PIN_IO_UART_RX, GPIO_FUNC_UART);
    gpio_pull_up(PIN_IO_UART_RX);
    const uint actual = uart_init(uart1, FWOG_BN_BAUD);
    uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(uart1, false, false);
    uart_set_fifo_enabled(uart1, true);
    gpio_set_function(PIN_IO_UART_RX, GPIO_FUNC_UART);
    DIAG("[bn] listen PIO RX GPIO8 + uart1 RX GPIO9 baud=%u pio=%d\n",
         (unsigned)actual, (int)s_swap_uart);
}

bool fwog_bn_last_rx_pio(void) {
    return s_last_rx_pio;
}

bool fwog_bn_uart_swap_init(void) {
    s_swap_uart = esp_rom_swap_begin();
    DIAG("[bn] swap_init ok=%d tx=%d fully=%d\n",
         (int)s_swap_uart, (int)esp_rom_swap_tx_ready(),
         (int)fwog_io_dir_last_fully_applied());
    return s_swap_uart;
}

void fwog_bn_uart_app_init(void) {
    s_swap_uart = false;
    esp_rom_pins_end();
    uart_deinit(uart1);
    gpio_set_function(PIN_IO_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_IO_UART_RX, GPIO_FUNC_UART);
    gpio_pull_up(PIN_IO_UART_RX);
    gpio_set_input_enabled(PIN_IO_UART_RX, true);
    gpio_init(PIN_IO_UART_CTS);
    gpio_set_dir(PIN_IO_UART_CTS, GPIO_IN);
    gpio_init(PIN_IO_UART_RTS);
    gpio_set_dir(PIN_IO_UART_RTS, GPIO_IN);
    const uint actual = uart_init(uart1, FWOG_BN_BAUD);
    uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(uart1, false, false);
    uart_set_fifo_enabled(uart1, true);
    gpio_set_function(PIN_IO_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_IO_UART_RX, GPIO_FUNC_UART);
    gpio_set_input_enabled(PIN_IO_UART_RX, true);
    DIAG("[bn] app uart1 8N1 baud=%u want=%u tx=GPIO8 rx=GPIO9 no-flow "
         "fn8=%d fn9=%d\n",
         (unsigned)actual, (unsigned)FWOG_BN_BAUD,
         (int)gpio_get_function(PIN_IO_UART_TX),
         (int)gpio_get_function(PIN_IO_UART_RX));
}

fwog_io_result_t fwog_bn_link_app(void) {
    const fwog_io_result_t r = fwog_bn_io_app();
    fwog_bn_uart_app_init();
    return r;
}

int fwog_bn_getc(void) {
    const int pio_b = esp_rom_swap_try_rx();
    if (pio_b >= 0) {
        s_last_rx_pio = true;
        return pio_b;
    }
    if (uart_is_readable(uart1)) {
        s_last_rx_pio = false;
        return (int)uart_getc(uart1);
    }
    return -1;
}

void fwog_bn_write(const fwog_bn_flash_t *st, const char *s) {
    if (st && st->rom_pins) return;
    if (!s || !s[0]) return;
    if (s_swap_uart) {
        if (!esp_rom_swap_tx_ready()) {
            DIAG("[bn] tx drop (GPIO9 writer not up)\n");
            return;
        }
        unsigned n = 0;
        for (const char *p = s; *p != '\0'; p++) n++;
        DIAG("[bn] tx %u bytes\n", n);
        for (; *s != '\0'; s++) esp_rom_swap_tx((uint8_t)*s);
        return;
    }
    const uint32_t t0 = to_ms_since_boot(get_absolute_time());
    for (; *s != '\0'; s++) {
        while (!uart_is_writable(uart1)) {
            if ((to_ms_since_boot(get_absolute_time()) - t0) > 20u) return;
            tight_loop_contents();
        }
        uart_putc_raw(uart1, (uint8_t)*s);
    }
}

static void tick(fwog_bn_flash_t *st, fwog_bn_flash_cb_t cb, void *ctx) {
    if (cb) cb(st, ctx);
}

static void dump_rx(const char *tag, uint32_t wait_ms) {
    uint8_t buf[16];
    unsigned n = 0;
    const uint32_t t0 = to_ms_since_boot(get_absolute_time());
    while (n < 16u &&
           (to_ms_since_boot(get_absolute_time()) - t0) < wait_ms) {
        board_watchdog_kick();
        int b = fwog_bn_getc();
        if (b < 0) {
            sleep_ms(2);
            continue;
        }
        buf[n++] = (uint8_t)b;
    }
    DIAG("[bn] %s rx n=%u", tag, n);
    for (unsigned i = 0; i < n; i++) DIAG(" %02X", buf[i]);
    DIAG("\n");
}

static void restore(fwog_bn_flash_t *st) {
    esp_rom_pins_end();
    (void)fwog_bn_io_app();
    fwog_bn_uart_app_init();
    st->rom_pins = false;
    DIAG("[bn] restore APP uart1 fully=%d\n",
         (int)fwog_io_dir_last_fully_applied());
    for (int i = 0; i < 6; i++) {
        board_watchdog_kick();
        sleep_ms(50);
    }
    dump_rx("restore", 200u);
}

static void fail(fwog_bn_flash_t *st, const char *why,
                 fwog_bn_flash_cb_t cb, void *ctx) {
    st->flash = FWOG_BN_FLASH_FAIL;
    st->pct = 0;
    memset(st->why, 0, sizeof st->why);
    if (why) strncpy(st->why, why, sizeof st->why - 1u);
    tick(st, cb, ctx);
}

static void why_edges(fwog_bn_flash_t *st) {
    const unsigned a = st->e8 > 9999u ? 9999u : st->e8;
    const unsigned b = st->e9 > 9999u ? 9999u : st->e9;
    snprintf(st->why, sizeof st->why, "8=%u 9=%u", a, b);
}

void fwog_bn_flash_arm(fwog_bn_flash_t *st, fwog_bn_flash_cb_t cb, void *ctx) {
    if (!st) return;
    if (fwog_c6_image_size < 256u) {
        fail(st, "no C6 image", cb, ctx);
        DIAG("[bn] no fwog_c6.bin embedded\n");
        return;
    }
    const fwog_io_result_t ir = io_apply_listen();
    esp_rom_pins_listen();
    st->rom_pins = true;
    st->flash = FWOG_BN_FLASH_HOLD;
    st->pct = 0;
    st->e8 = 0;
    st->e9 = 0;
    st->last8 = (uint8_t)gpio_get(PIN_IO_UART_TX);
    st->last9 = (uint8_t)gpio_get(PIN_IO_UART_RX);
    why_edges(st);
    tick(st, cb, ctx);
    DIAG("[bn] C6 flash armed size=%u io_dir=%d listen tx_out=0 rx_out=0\n",
         fwog_c6_image_size, (int)ir);
}

void fwog_bn_flash_poll(fwog_bn_flash_t *st) {
    if (!st || !st->rom_pins || st->flash != FWOG_BN_FLASH_HOLD) return;
    const uint8_t v8 = (uint8_t)gpio_get(PIN_IO_UART_TX);
    const uint8_t v9 = (uint8_t)gpio_get(PIN_IO_UART_RX);
    if (v8 != st->last8) {
        if (st->e8 < 0xFFFFu) st->e8++;
        st->last8 = v8;
    }
    if (v9 != st->last9) {
        if (st->e9 < 0xFFFFu) st->e9++;
        st->last9 = v9;
    }
    why_edges(st);
}

static fwog_bn_flash_t     *s_go;
static fwog_bn_flash_cb_t   s_cb;
static void                *s_ctx;

static void progress(unsigned pct) {
    if (!s_go) return;
    s_go->flash = FWOG_BN_FLASH_WRITE;
    s_go->pct = (uint8_t)pct;
    if (s_cb) s_cb(s_go, s_ctx);
    board_watchdog_kick();
}

void fwog_bn_flash_go(fwog_bn_flash_t *st, fwog_bn_flash_cb_t cb, void *ctx) {
    if (!st) return;
    if (!st->rom_pins) fwog_bn_flash_arm(st, cb, ctx);
    if (st->flash == FWOG_BN_FLASH_FAIL) return;
    const bool swap = esp_rom_orient_swap_first(st->e8, st->e9);
    (void)io_apply_orient(swap);
    st->flash = FWOG_BN_FLASH_SYNC;
    st->pct = 0;
    tick(st, cb, ctx);
    DIAG("[bn] C6 flash go e8=%u e9=%u first=%s\n",
         (unsigned)st->e8, (unsigned)st->e9, swap ? "swap" : "app");
    s_go = st;
    s_cb = cb;
    s_ctx = ctx;
    const esp_rom_err_t e = esp_rom_flash(fwog_c6_image, fwog_c6_image_size,
                                          progress, st->e8, st->e9);
    s_go = NULL;
    s_cb = NULL;
    s_ctx = NULL;
    restore(st);
    if (e == ESP_ROM_OK) {
        st->flash = FWOG_BN_FLASH_OK;
        st->pct = 100;
        memset(st->why, 0, sizeof st->why);
        tick(st, cb, ctx);
        DIAG("[bn] C6 flash ok\n");
        return;
    }
    const char *why = "fail";
    if (e == ESP_ROM_ERR_NOIMG) why = "no C6 image";
    else if (e == ESP_ROM_ERR_SYNC) why = "no ROM sync";
    else if (e == ESP_ROM_ERR_ATTACH) why = "spi attach";
    else if (e == ESP_ROM_ERR_BEGIN) why = "flash begin";
    else if (e == ESP_ROM_ERR_WRITE) why = "flash write";
    else if (e == ESP_ROM_ERR_END) why = "flash end";
    fail(st, why, cb, ctx);
    DIAG("[bn] C6 flash err %d %s e8=%u e9=%u\n",
         (int)e, why, (unsigned)st->e8, (unsigned)st->e9);
}

void fwog_bn_flash_cancel(fwog_bn_flash_t *st, fwog_bn_flash_cb_t cb, void *ctx) {
    if (!st) return;
    if (st->rom_pins) restore(st);
    st->flash = FWOG_BN_FLASH_IDLE;
    st->pct = 0;
    st->e8 = 0;
    st->e9 = 0;
    memset(st->why, 0, sizeof st->why);
    tick(st, cb, ctx);
}
