#include "common/link/link_uart.h"

uint32_t fwog_uart_actual_baud(uint32_t clk_hz, uint32_t baud) {
    if (clk_hz == 0u || baud == 0u) return 0u;

    /* Exactly the SDK's uart_set_baudrate() arithmetic. Duplicated rather
       than called because it is only reachable with a live UART, and this
       must answer before one is brought up -- and on the host. */
    uint32_t div = (uint32_t)(((uint64_t)clk_hz * 8u) / baud);
    uint32_t ibrd = div >> 7;
    uint32_t fbrd;
    if (ibrd == 0u) {
        /* The requested rate is above the PL011 ceiling of clk/16. The SDK
           clamps here WITHOUT reporting it. */
        ibrd = 1u;
        fbrd = 0u;
    } else if (ibrd >= 65535u) {
        ibrd = 65535u;
        fbrd = 0u;
    } else {
        fbrd = ((div & 0x7Fu) + 1u) / 2u;
    }
    return (uint32_t)(((uint64_t)clk_hz * 4u) / (64u * ibrd + fbrd));
}

bool fwog_uart_baud_ok(uint32_t clk_hz, uint32_t baud) {
    uint32_t actual = fwog_uart_actual_baud(clk_hz, baud);
    if (actual == 0u || baud == 0u) return false;
    uint32_t diff = (actual > baud) ? (actual - baud) : (baud - actual);
    return (uint64_t)diff * 100u <= (uint64_t)baud;   /* within 1% */
}

#ifndef HOST_TEST
#include "common/diag.h"
#include "common/link/link_frame.h"
#include "platform/board.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"

/* uart_init() is what takes UART0 out of RESET. Reading UARTFR (or
 * uart_write_blocking) while the block is still in reset stalls the
 * RP2040 bus forever. fwog_splash_boot() used to call fwog_link_uart_read()
 * on every display app before the app had inited the link — ChirpMail 004
 * hung on the splash blit and never reached its chrome. */
static bool s_link_up;

bool fwog_link_uart_init(uint32_t baud) {
    uint32_t clk = clock_get_hz(clk_peri);
    if (!fwog_uart_baud_ok(clk, baud)) {
        DIAG("[link] %u baud unreachable from clk_peri %u (would be %u)\n",
             (unsigned)baud, (unsigned)clk,
             (unsigned)fwog_uart_actual_baud(clk, baud));
        return false;
    }

    uart_init(FWOG_LINK_UART, baud);
    gpio_set_function(PIN_LINK_TX,  GPIO_FUNC_UART);
    gpio_set_function(PIN_LINK_RX,  GPIO_FUNC_UART);
    gpio_set_function(PIN_LINK_CTS, GPIO_FUNC_UART);
    gpio_set_function(PIN_LINK_RTS, GPIO_FUNC_UART);
    uart_set_hw_flow(FWOG_LINK_UART, true, true);
    uart_set_format(FWOG_LINK_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(FWOG_LINK_UART, true);

    /* Report what the hardware actually produced, not what was asked for.
       At the design rate these are equal by construction; if they ever
       diverge, this line is how anyone finds out. */
    DIAG("[link] uart0 %u baud (requested %u), clk_peri %u\n",
         (unsigned)fwog_uart_actual_baud(clk, baud), (unsigned)baud,
         (unsigned)clk);
    s_link_up = true;
    return true;
}

void fwog_link_uart_deinit(void) {
    s_link_up = false;
    uart_deinit(FWOG_LINK_UART);
}

void fwog_link_uart_write(const void *data, size_t len) {
    if (!s_link_up) return;
    uart_write_blocking(FWOG_LINK_UART, (const uint8_t *)data, len);
}

bool fwog_link_uart_read(uint8_t *b) {
    if (!s_link_up || !b) return false;
    if (!uart_is_readable(FWOG_LINK_UART)) return false;
    *b = uart_getc(FWOG_LINK_UART);
    return true;
}

bool fwog_link_uart_send_frame(const void *payload, size_t len) {
    /* One shared frame buffer: the link is strictly half-duplex
       stop-and-wait, so there is never a second send in flight. */
    static uint8_t frame[FWOG_LINK_MAX_PAYLOAD + FWOG_LINK_OVERHEAD];
    size_t n;
    if (!s_link_up) return false;
    n = fwog_link_encode(frame, sizeof frame, payload, len);
    if (n == 0u) return false;
    fwog_link_uart_write(frame, n);
    return true;
}
#endif
