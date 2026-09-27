#include "bottlenose/esp_rom_flash.h"
#include "fwog_main.h"
#include "esp_rom_uart.pio.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"
#include <string.h>

#define PIN_G8 PIN_IO_UART_TX   /* GPIO 8  header UART1_TX */
#define PIN_G9 PIN_IO_UART_RX   /* GPIO 9  header UART1_RX */
#define BLK    0x400u
#define CMD_SYNC        0x08u
#define CMD_FLASH_BEGIN 0x02u
#define CMD_FLASH_DATA  0x03u
#define CMD_FLASH_END   0x04u
#define CMD_SPI_ATTACH  0x0Du
#define CMD_SPI_PARAMS  0x0Bu
#define CMD_GET_SECINFO 0x14u
#define ROM_BAUD        115200u

typedef enum { SER_NONE = 0, SER_PIO, SER_UART } ser_kind_t;

static ser_kind_t s_kind;
static PIO        s_pio;
static uint       s_sm_tx, s_sm_rx, s_off_tx, s_off_rx;
static bool       s_pio_has_tx;
static bool       s_bitbang_tx;
static uint32_t   s_bit_cy;

static void ser_deinit(void);

bool esp_rom_orient_swap_first(uint16_t e8, uint16_t e9) {
    /* GPIO9 heard, GPIO8 silent: APP. Do not spend the first SYNC on SWAP. */
    if (e8 == 0u && e9 > 0u) return false;
    return e9 <= e8; /* e8>=e9 or both 0 → SWAP first; e9>e8 → APP */
}

static float rom_clkdiv(void) {
    return (float)clock_get_hz(clk_sys) / (8.0f * (float)ROM_BAUD);
}

static void pins_in_pu(unsigned pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
}

void esp_rom_pins_listen(void) {
    ser_deinit();
    uart_deinit(uart1);
    pins_in_pu(PIN_G8);
    pins_in_pu(PIN_G9);
    gpio_init(PIN_IO_UART_CTS);
    gpio_set_dir(PIN_IO_UART_CTS, GPIO_IN);
    gpio_init(PIN_IO_UART_RTS);
    gpio_set_dir(PIN_IO_UART_RTS, GPIO_IN);
}

void esp_rom_pins_begin(void) {
    esp_rom_pins_listen();
}

static void ser_deinit(void) {
    if (s_kind == SER_PIO) {
        pio_sm_set_enabled(s_pio, s_sm_rx, false);
        pio_sm_unclaim(s_pio, s_sm_rx);
        pio_remove_program(s_pio, &rom_rx_program, s_off_rx);
        if (s_pio_has_tx) {
            pio_sm_set_enabled(s_pio, s_sm_tx, false);
            pio_sm_unclaim(s_pio, s_sm_tx);
            pio_remove_program(s_pio, &rom_tx_program, s_off_tx);
        }
        s_pio_has_tx = false;
    } else if (s_kind == SER_UART) {
        uart_deinit(uart1);
    }
    s_bitbang_tx = false;
    s_kind = SER_NONE;
}

void esp_rom_pins_end(void) {
    ser_deinit();
    esp_rom_pins_listen();
}

static fwog_io_result_t apply_orient(bool swap) {
    fwog_io_cfg_t cfg;
    fwog_io_cfg_default(&cfg);
    cfg.uart_cts_out = false;
    cfg.uart_rts_out = false;
    if (swap) {
        cfg.uart_tx_out = false; /* GPIO8 listen */
        cfg.uart_rx_out = true;  /* GPIO9 drive */
    } else {
        cfg.uart_tx_out = true;  /* GPIO8 drive */
        cfg.uart_rx_out = false; /* GPIO9 listen */
    }
    return fwog_io_dir_apply(&cfg);
}

static bool pio_pick(void) {
    s_pio = pio0;
    if (!pio_can_add_program(s_pio, &rom_rx_program)) s_pio = pio1;
    if (!pio_can_add_program(s_pio, &rom_rx_program)) {
        DIAG("[bn] PIO no room for ROM UART RX\n");
        return false;
    }
    return true;
}

static bool pio_rx_sm(void) {
    const int sm_rx = pio_claim_unused_sm(s_pio, false);
    if (sm_rx < 0) {
        DIAG("[bn] PIO no SM for ROM UART RX\n");
        return false;
    }
    s_sm_rx = (uint)sm_rx;
    s_off_rx = pio_add_program(s_pio, &rom_rx_program);
    const uint rx = PIN_G8;
    const float div = rom_clkdiv();
    pio_gpio_init(s_pio, rx);
    pio_sm_set_consecutive_pindirs(s_pio, s_sm_rx, rx, 1u, false);
    gpio_pull_up(rx);
    pio_sm_config cr = rom_rx_program_get_default_config(s_off_rx);
    sm_config_set_in_pins(&cr, rx);
    sm_config_set_jmp_pin(&cr, rx);
    sm_config_set_in_shift(&cr, true, true, 8);
    sm_config_set_fifo_join(&cr, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv(&cr, div);
    pio_sm_init(s_pio, s_sm_rx, s_off_rx, &cr);
    pio_sm_set_enabled(s_pio, s_sm_rx, true);
    return true;
}

static bool pio_tx_sm(void) {
    if (!pio_can_add_program(s_pio, &rom_tx_program)) {
        DIAG("[bn] PIO no room for ROM UART TX\n");
        return false;
    }
    const int sm_tx = pio_claim_unused_sm(s_pio, false);
    if (sm_tx < 0) {
        DIAG("[bn] PIO no SM for ROM UART TX\n");
        return false;
    }
    s_sm_tx = (uint)sm_tx;
    s_off_tx = pio_add_program(s_pio, &rom_tx_program);
    const uint tx = PIN_G9;
    const float div = rom_clkdiv();
    pio_sm_set_pins_with_mask(s_pio, s_sm_tx, 1u << tx, 1u << tx);
    pio_sm_set_consecutive_pindirs(s_pio, s_sm_tx, tx, 1u, true);
    pio_gpio_init(s_pio, tx);
    pio_sm_config ct = rom_tx_program_get_default_config(s_off_tx);
    sm_config_set_out_pins(&ct, tx, 1);
    sm_config_set_sideset_pins(&ct, tx);
    sm_config_set_out_shift(&ct, true, false, 8);
    sm_config_set_fifo_join(&ct, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&ct, div);
    pio_sm_init(s_pio, s_sm_tx, s_off_tx, &ct);
    pio_sm_set_enabled(s_pio, s_sm_tx, true);
    s_pio_has_tx = true;
    return true;
}

static bool ser_pio_begin(void) {
    if (!pio_pick()) return false;
    if (!pio_can_add_program(s_pio, &rom_tx_program)) {
        if (s_pio == pio0) s_pio = pio1;
        if (!pio_can_add_program(s_pio, &rom_tx_program) ||
            !pio_can_add_program(s_pio, &rom_rx_program)) {
            DIAG("[bn] PIO no room for ROM UART\n");
            return false;
        }
    }
    if (!pio_rx_sm()) return false;
    if (!pio_tx_sm()) {
        pio_sm_set_enabled(s_pio, s_sm_rx, false);
        pio_sm_unclaim(s_pio, s_sm_rx);
        pio_remove_program(s_pio, &rom_rx_program, s_off_rx);
        return false;
    }
    s_kind = SER_PIO;
    return true;
}

static bool ser_uart_begin(void) {
    uart_deinit(uart1);
    gpio_set_function(PIN_G8, GPIO_FUNC_UART);
    gpio_set_function(PIN_G9, GPIO_FUNC_UART);
    gpio_pull_up(PIN_G9);
    uart_init(uart1, ROM_BAUD);
    uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(uart1, false, false);
    uart_set_fifo_enabled(uart1, true);
    s_kind = SER_UART;
    return true;
}

static bool ser_begin(bool swap) {
    ser_deinit();
    esp_rom_pins_listen();
    (void)apply_orient(swap);
    if (swap) return ser_pio_begin();
    return ser_uart_begin();
}

bool esp_rom_swap_rx_begin(void) {
    ser_deinit();
    uart_deinit(uart1);
    pins_in_pu(PIN_G9);
    gpio_init(PIN_IO_UART_CTS);
    gpio_set_dir(PIN_IO_UART_CTS, GPIO_IN);
    gpio_init(PIN_IO_UART_RTS);
    gpio_set_dir(PIN_IO_UART_RTS, GPIO_IN);
    if (!pio_pick() || !pio_rx_sm()) return false;
    s_kind = SER_PIO;
    s_pio_has_tx = false;
    DIAG("[bn] swap PIO RX GPIO8 baud=%u\n", (unsigned)ROM_BAUD);
    return true;
}

bool esp_rom_swap_begin(void) {
    ser_deinit();
    uart_deinit(uart1);
    const fwog_io_result_t ir = apply_orient(true);
    gpio_put(PIN_G9, 1);
    gpio_set_dir(PIN_G9, GPIO_OUT);
    gpio_set_function(PIN_G9, GPIO_FUNC_SIO);
    if (!pio_pick() || !pio_rx_sm()) {
        DIAG("[bn] swap PIO RX GPIO8 fail io=%d\n", (int)ir);
        return false;
    }
    s_kind = SER_PIO;
    s_pio_has_tx = false;
    s_bitbang_tx = true;
    s_bit_cy = clock_get_hz(clk_sys) / ROM_BAUD;
    DIAG("[bn] swap bitbang TX GPIO9 PIO RX GPIO8 cy=%u io=%d fully=%d fn9=%d\n",
         (unsigned)s_bit_cy, (int)ir,
         (int)fwog_io_dir_last_fully_applied(),
         (int)gpio_get_function(PIN_G9));
    return true;
}

bool esp_rom_swap_tx_ready(void) {
    return s_bitbang_tx || (s_kind == SER_PIO && s_pio_has_tx);
}

int esp_rom_swap_try_rx(void) {
    if (s_kind != SER_PIO) return -1;
    if (pio_sm_is_rx_fifo_empty(s_pio, s_sm_rx)) return -1;
    return (int)((pio_sm_get(s_pio, s_sm_rx) >> 24) & 0xFFu);
}

static void bitbang_tx_byte(uint8_t b) {
    if (s_bit_cy == 0u) s_bit_cy = clock_get_hz(clk_sys) / ROM_BAUD;
    const uint32_t irq = save_and_disable_interrupts();
    gpio_put(PIN_G9, 0);
    busy_wait_at_least_cycles(s_bit_cy);
    for (unsigned i = 0; i < 8u; i++) {
        gpio_put(PIN_G9, (b >> i) & 1u);
        busy_wait_at_least_cycles(s_bit_cy);
    }
    gpio_put(PIN_G9, 1);
    busy_wait_at_least_cycles(s_bit_cy);
    restore_interrupts(irq);
}

void esp_rom_swap_tx(uint8_t b) {
    if (s_bitbang_tx) {
        bitbang_tx_byte(b);
        return;
    }
    if (s_kind != SER_PIO || !s_pio_has_tx) return;
    pio_sm_put_blocking(s_pio, s_sm_tx, (uint32_t)b);
}

static void tx_byte(uint8_t b) {
    if (s_kind == SER_UART) {
        uart_putc_raw(uart1, b);
        return;
    }
    pio_sm_put_blocking(s_pio, s_sm_tx, (uint32_t)b);
}

static int rx_byte(uint32_t timeout_ms) {
    const uint32_t t0 = to_ms_since_boot(get_absolute_time());
    if (s_kind == SER_UART) {
        while (!uart_is_readable(uart1)) {
            if ((to_ms_since_boot(get_absolute_time()) - t0) > timeout_ms) return -1;
            tight_loop_contents();
        }
        return (int)uart_getc(uart1);
    }
    while (pio_sm_is_rx_fifo_empty(s_pio, s_sm_rx)) {
        if ((to_ms_since_boot(get_absolute_time()) - t0) > timeout_ms) return -1;
        tight_loop_contents();
    }
    /* Right-shift IN, autopush at 8: UART bits sit in ISR[31:24]. */
    return (int)((pio_sm_get(s_pio, s_sm_rx) >> 24) & 0xFFu);
}

static void slip_put(uint8_t b) {
    if (b == 0xC0u) {
        tx_byte(0xDBu);
        tx_byte(0xDCu);
    } else if (b == 0xDBu) {
        tx_byte(0xDBu);
        tx_byte(0xDDu);
    } else {
        tx_byte(b);
    }
}

static void slip_send(const uint8_t *p, size_t n) {
    tx_byte(0xC0u);
    for (size_t i = 0; i < n; i++) slip_put(p[i]);
    tx_byte(0xC0u);
}

static int slip_recv(uint8_t *out, size_t cap, uint32_t timeout_ms) {
    const uint32_t t0 = to_ms_since_boot(get_absolute_time());
    int b;
    do {
        board_watchdog_kick();
        b = rx_byte(20u);
        if ((to_ms_since_boot(get_absolute_time()) - t0) > timeout_ms) return -1;
    } while (b != 0xC0);
    size_t n = 0;
    bool esc = false;
    for (;;) {
        b = rx_byte(50u);
        if (b < 0) return -1;
        if (b == 0xC0) break;
        uint8_t v = (uint8_t)b;
        if (esc) {
            if (v == 0xDCu) v = 0xC0u;
            else if (v == 0xDDu) v = 0xDBu;
            esc = false;
        } else if (v == 0xDBu) {
            esc = true;
            continue;
        }
        if (n < cap) out[n++] = v;
    }
    return (int)n;
}

static uint8_t xor_sum(const uint8_t *p, size_t n) {
    uint8_t s = 0xEFu;
    for (size_t i = 0; i < n; i++) s ^= p[i];
    return s;
}

static void drain_rx(uint32_t ms) {
    const uint32_t t0 = to_ms_since_boot(get_absolute_time());
    while ((to_ms_since_boot(get_absolute_time()) - t0) < ms) {
        board_watchdog_kick();
        if (rx_byte(5u) < 0) return;
    }
}

static void drain_slip(uint32_t ms) {
    uint8_t dump[64];
    const uint32_t t0 = to_ms_since_boot(get_absolute_time());
    while ((to_ms_since_boot(get_absolute_time()) - t0) < ms) {
        board_watchdog_kick();
        if (slip_recv(dump, sizeof dump, 30u) < 0) return;
    }
}

static void diag_slip(const char *tag, const uint8_t *resp, int n) {
    char hex[48];
    unsigned h = 0;
    const int show = n < 0 ? 0 : (n > 16 ? 16 : n);
    hex[0] = '\0';
    for (int i = 0; i < show && h + 3u < sizeof hex; i++) {
        hex[h++] = "0123456789ABCDEF"[(resp[i] >> 4) & 0xFu];
        hex[h++] = "0123456789ABCDEF"[resp[i] & 0xFu];
        hex[h++] = (i + 1 < show) ? ' ' : '\0';
    }
    hex[sizeof hex - 1u] = '\0';
    const unsigned last0 = (n >= 2) ? resp[n - 2] : 0u;
    const unsigned last1 = (n >= 1) ? resp[n - 1] : 0u;
    const unsigned c = (n >= 2) ? resp[1] : 0u;
    DIAG("[bn] %s n=%d cmd=%02X last=%02X %02X %s\n",
         tag, n, c, last0, last1, hex);
}

static int cmd_recv(uint8_t want, uint8_t *resp, size_t cap, uint32_t timeout_ms) {
    const uint32_t t0 = to_ms_since_boot(get_absolute_time());
    for (;;) {
        board_watchdog_kick();
        const int n = slip_recv(resp, cap, 80u);
        if (n >= 2 && resp[0] == 0x01u && resp[1] == want) return n;
        if (n >= 2) {
            DIAG("[bn] leftover SLIP cmd=%02X want=%02X n=%d\n",
                 resp[1], want, n);
        }
        if ((to_ms_since_boot(get_absolute_time()) - t0) > timeout_ms) {
            return n;
        }
    }
}

static bool cmd(uint8_t c, const uint8_t *data, uint16_t len,
                uint32_t chk, uint32_t timeout_ms, uint32_t *value) {
    uint8_t pkt[8 + 16 + BLK];
    if (len > (16u + BLK)) return false;
    pkt[0] = 0x00;
    pkt[1] = c;
    pkt[2] = (uint8_t)(len & 0xFFu);
    pkt[3] = (uint8_t)(len >> 8);
    pkt[4] = (uint8_t)(chk);
    pkt[5] = (uint8_t)(chk >> 8);
    pkt[6] = (uint8_t)(chk >> 16);
    pkt[7] = (uint8_t)(chk >> 24);
    if (len) memcpy(pkt + 8, data, len);
    slip_send(pkt, 8u + len);
    uint8_t resp[64];
    const int n = cmd_recv(c, resp, sizeof resp, timeout_ms);
    if (n < 10 || resp[0] != 0x01u || resp[1] != c) return false;
    if (value) {
        memcpy(value, resp + 4, 4);
    }
    if (n >= 2 && resp[n - 2] != 0) return false;
    return true;
}

/* Attach ACK: matching 0x0D is enough. Status bytes on C6 are easy to misread. */
static bool cmd_attach(uint32_t timeout_ms, uint8_t *resp, int *n_out) {
    uint8_t pkt[16];
    uint8_t z8[8] = {0};
    pkt[0] = 0x00;
    pkt[1] = CMD_SPI_ATTACH;
    pkt[2] = 8;
    pkt[3] = 0;
    memset(pkt + 4, 0, 4);
    memcpy(pkt + 8, z8, 8);
    slip_send(pkt, 16);
    const int n = cmd_recv(CMD_SPI_ATTACH, resp, 64, timeout_ms);
    if (n_out) *n_out = n;
    return n >= 10 && resp[0] == 0x01u && resp[1] == CMD_SPI_ATTACH;
}

static bool do_sync(void) {
    uint8_t d[36];
    d[0] = 0x07;
    d[1] = 0x07;
    d[2] = 0x12;
    d[3] = 0x20;
    memset(d + 4, 0x55, 32);
    drain_rx(40u);
    for (int i = 0; i < 24; i++) {
        board_watchdog_kick();
        if (cmd(CMD_SYNC, d, 36, 0, 150u, NULL)) {
            drain_slip(250u);
            drain_rx(40u);
            return true;
        }
    }
    return false;
}

static esp_rom_err_t write_image(const uint8_t *img, size_t len,
                                 void (*progress)(unsigned pct)) {
    drain_slip(200u);
    drain_rx(40u);

    (void)cmd(CMD_GET_SECINFO, NULL, 0, 0, 300u, NULL);
    drain_slip(80u);

    uint8_t aresp[64];
    memset(aresp, 0, sizeof aresp);
    int an = 0;
    const bool attach_ok = cmd_attach(1200u, aresp, &an);
    if (!attach_ok) {
        diag_slip("SPI_ATTACH", aresp, an);
        drain_slip(80u);
    }

    uint8_t params[24];
    memset(params, 0, sizeof params);
    const uint32_t flash_sz = 4u * 1024u * 1024u;
    const uint32_t p[] = {0u, flash_sz, 64u * 1024u, 4u * 1024u, 256u, 0xFFFFu};
    memcpy(params, p, sizeof p);
    (void)cmd(CMD_SPI_PARAMS, params, 24, 0, 500u, NULL);

    const uint32_t blocks = (uint32_t)((len + BLK - 1u) / BLK);
    uint8_t begin[20];
    const uint32_t b[] = {(uint32_t)len, blocks, BLK, 0u, 0u};
    memcpy(begin, b, sizeof b);
    if (!cmd(CMD_FLASH_BEGIN, begin, 20, 0, 30000u, NULL)) {
        return attach_ok ? ESP_ROM_ERR_BEGIN : ESP_ROM_ERR_ATTACH;
    }
    if (progress) progress(1);

    uint8_t pkt[16 + BLK];
    for (uint32_t i = 0; i < blocks; i++) {
        board_watchdog_kick();
        const size_t off = (size_t)i * BLK;
        memset(pkt, 0xFFu, 16 + BLK);
        if (off < len) {
            size_t n = len - off;
            if (n > BLK) n = BLK;
            memcpy(pkt + 16, img + off, n);
        }
        const uint32_t hdr[] = {BLK, i, 0u, 0u};
        memcpy(pkt, hdr, 16);
        const uint8_t sum = xor_sum(pkt + 16, BLK);
        if (!cmd(CMD_FLASH_DATA, pkt, (uint16_t)(16u + BLK), sum, 3000u, NULL)) {
            return ESP_ROM_ERR_WRITE;
        }
        if (progress) progress((unsigned)((i + 1u) * 100u / blocks));
    }

    /* 1 = stay in the ROM loader (esptool execute=False). Reboot (0) while
     * BOOT is still held re-enters download; the LCD already says tap RESET
     * after a good write, and restore() needs OG UART back at 115200 first. */
    uint8_t stay[4] = {1, 0, 0, 0};
    if (!cmd(CMD_FLASH_END, stay, 4, 0, 2000u, NULL)) return ESP_ROM_ERR_END;
    if (progress) progress(100);
    return ESP_ROM_OK;
}

esp_rom_err_t esp_rom_flash(const uint8_t *img, size_t len,
                            void (*progress)(unsigned pct),
                            uint16_t e8, uint16_t e9) {
    if (!img || len < 256u) return ESP_ROM_ERR_NOIMG;

    const bool first_swap = esp_rom_orient_swap_first(e8, e9);
    const char *first = first_swap ? "swap" : "app";
    bool synced = false;
    bool used_swap = first_swap;

    for (int attempt = 0; attempt < 2; attempt++) {
        board_watchdog_kick();
        used_swap = (attempt == 0) ? first_swap : !first_swap;
        if (!ser_begin(used_swap)) continue;
        DIAG("[bn] ROM try %s e8=%u e9=%u\n",
             used_swap ? "swap" : "app", (unsigned)e8, (unsigned)e9);
        if (do_sync()) {
            synced = true;
            break;
        }
        ser_deinit();
        esp_rom_pins_listen();
    }

    DIAG("[bn] e8=%u e9=%u first=%s ok=%s\n",
         (unsigned)e8, (unsigned)e9, first,
         synced ? (used_swap ? "swap" : "app") : "none");

    if (!synced) return ESP_ROM_ERR_SYNC;

    const esp_rom_err_t e = write_image(img, len, progress);
    ser_deinit();
    return e;
}
