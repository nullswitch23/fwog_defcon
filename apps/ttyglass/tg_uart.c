#include "tg_uart.h"
#include "tg_proto.h"
#include "fwog_main.h"
#include "tg_uart.pio.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

static PIO  s_pio;
static uint s_sm, s_off;
static bool s_hw, s_pio_on;

static void hiz(fwog_io_cfg_t *cfg) {
    fwog_io_cfg_default(cfg);
    cfg->spi_tx_out = cfg->spi_rx_out = false;
    cfg->spi_cs_out = cfg->spi_sclk_out = false;
    cfg->uart_tx_out = false;
    cfg->uart_rx_out = false;
    cfg->uart_cts_out = false;
    cfg->uart_rts_out = false;
    cfg->gpio26_out = false;
    cfg->gpio27_out = false;
    cfg->i2c_pullup = false;
}

static void pio_stop(void) {
    if (!s_pio_on) return;
    pio_sm_set_enabled(s_pio, s_sm, false);
    pio_sm_unclaim(s_pio, s_sm);
    pio_remove_program(s_pio, &tg_rx_program, s_off);
    s_pio_on = false;
}

static void hw_stop(void) {
    if (!s_hw) return;
    uart_deinit(uart1);
    s_hw = false;
}

static bool pio_start(unsigned gpio, uint32_t baud) {
    s_pio = pio0;
    if (!pio_can_add_program(s_pio, &tg_rx_program)) s_pio = pio1;
    if (!pio_can_add_program(s_pio, &tg_rx_program)) {
        DIAG("[ttyglass] PIO no room\n");
        return false;
    }
    const int sm = pio_claim_unused_sm(s_pio, false);
    if (sm < 0) {
        DIAG("[ttyglass] PIO no SM\n");
        return false;
    }
    s_sm = (uint)sm;
    s_off = pio_add_program(s_pio, &tg_rx_program);
    const float div = (float)clock_get_hz(clk_sys) / (8.0f * (float)baud);
    pio_gpio_init(s_pio, gpio);
    pio_sm_set_consecutive_pindirs(s_pio, s_sm, gpio, 1u, false);
    gpio_pull_up(gpio);
    pio_sm_config cr = tg_rx_program_get_default_config(s_off);
    sm_config_set_in_pins(&cr, gpio);
    sm_config_set_jmp_pin(&cr, gpio);
    sm_config_set_in_shift(&cr, true, true, 8);
    sm_config_set_fifo_join(&cr, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv(&cr, div);
    pio_sm_init(s_pio, s_sm, s_off, &cr);
    pio_sm_set_enabled(s_pio, s_sm, true);
    s_pio_on = true;
    return true;
}

void tg_uart_stop(void) {
    fwog_io_cfg_t cfg;
    pio_stop();
    hw_stop();
    hiz(&cfg);
    (void)fwog_io_dir_apply(&cfg);
}

void tg_uart_apply(int pin_i, uint32_t baud) {
    if (pin_i < 0 || pin_i >= TG_NPIN) pin_i = 0;
    const tg_pin_t *p = &k_tg_pin[pin_i];
    pio_stop();
    hw_stop();

    fwog_io_cfg_t cfg;
    hiz(&cfg);
    if (p->hw) cfg.uart_tx_out = true; /* UART1 TX idle on GP8 */
    const fwog_io_result_t ir = fwog_io_dir_apply(&cfg);
    DIAG("[ttyglass] pin %s GP%u hdr%u io=%d\n",
         p->tag, (unsigned)p->rx_gpio, (unsigned)p->hdr, (int)ir);

    if (p->hw) {
        gpio_set_function(PIN_IO_UART_TX, GPIO_FUNC_UART);
        gpio_set_function(PIN_IO_UART_RX, GPIO_FUNC_UART);
        gpio_pull_up(PIN_IO_UART_RX);
        uart_init(uart1, baud);
        uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
        uart_set_fifo_enabled(uart1, true);
        s_hw = true;
    } else {
        gpio_init(PIN_IO_UART_TX);
        gpio_set_dir(PIN_IO_UART_TX, GPIO_IN);
        gpio_init(PIN_IO_UART_RX);
        gpio_set_dir(PIN_IO_UART_RX, GPIO_IN);
        (void)pio_start(p->rx_gpio, baud);
    }
}

bool tg_uart_readable(void) {
    if (s_hw) return uart_is_readable(uart1);
    if (!s_pio_on) return false;
    return !pio_sm_is_rx_fifo_empty(s_pio, s_sm);
}

uint8_t tg_uart_getc(void) {
    if (s_hw) return (uint8_t)uart_getc(uart1);
    if (!s_pio_on) return 0;
    return (uint8_t)((pio_sm_get(s_pio, s_sm) >> 24) & 0xFFu);
}
