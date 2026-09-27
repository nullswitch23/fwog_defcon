#include "tb_serial.h"
#include "tb_proto.h"
#include "fwog_main.h"
#include "common/io_cfg.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"
#include <string.h>

#define TB_RELAY_BAUD 9600u

static fwog_link_rx_t s_rx;
static bool s_uart;

static int wait_read_byte(uint32_t deadline_ms) {
    while ((int32_t)(to_ms_since_boot(get_absolute_time()) - deadline_ms) < 0) {
        if (uart_is_readable(uart1)) {
            return (int)(uint8_t)uart_getc(uart1);
        }
        tight_loop_contents();
    }
    return -1;
}

static int send_and_wait_h(unsigned timeout_ms) {
    const uint32_t deadline =
        to_ms_since_boot(get_absolute_time()) + (uint32_t)timeout_ms;
    uart_putc_raw(uart1, 'H');
    uart_tx_wait_blocking(uart1);
    const int b = wait_read_byte(deadline);
    if (b < 0) return -1;
    if (b == '1') return 1;
    if (b == '0') return 0;
    return -1;
}

void tb_serial_main_uart_init(void) {
    fwog_io_cfg_t cfg;
    fwog_io_cfg_default(&cfg);
    cfg.uart_tx_out = true;
    cfg.uart_rx_out = true;
    (void)fwog_io_dir_apply(&cfg);

    gpio_set_function(PIN_IO_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_IO_UART_RX, GPIO_FUNC_UART);
    gpio_pull_up(PIN_IO_UART_RX);
    uart_init(uart1, TB_RELAY_BAUD);
    uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(uart1, true);
    s_uart = true;
}

void tb_serial_main_init(void) {
    fwog_link_rx_init(&s_rx);
    (void)fwog_link_uart_init(FWOG_LINK_BAUD);
    tb_serial_main_uart_init();
}

bool tb_serial_main_handle(const uint8_t *buf, size_t n) {
    if (n != sizeof(tb_hangup_req_t) || !buf) return false;
    const tb_hangup_req_t *req = (const tb_hangup_req_t *)buf;
    if (req->type != TB_MSG_HANGUP) return false;

    tb_hangup_rsp_t rsp = { .type = TB_MSG_HANGUP_RSP, .result = 0xFFu };
    if (s_uart && req->cmd == 'H') {
        const int r = send_and_wait_h(req->timeout_ms ? req->timeout_ms : 1000u);
        if (r == 1) rsp.result = 1u;
        else if (r == 0) rsp.result = 0u;
        else rsp.result = 0xFFu;
    }
    (void)fwog_link_uart_send_frame(&rsp, sizeof rsp);
    return true;
}

void tb_serial_main_poll(void) {
    uint8_t b;
    size_t n;
    while (fwog_link_uart_read(&b)) {
        if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
        (void)tb_serial_main_handle(s_rx.buf, n);
    }
}
