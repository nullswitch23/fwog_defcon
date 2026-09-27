/* TtyGlass main: listen on one header RX preset. UART1 or PIO. */
#include "fwog_main.h"
#include "tg_proto.h"
#include "tg_uart.h"
#include "pico/stdlib.h"
#include <string.h>

FWOG_WATCHDOG_DEFAULT();

static const uint32_t k_baud[] = {
    9600u, 19200u, 38400u, 115200u, 230400u, 921600u
};
#define TG_NBAUD ((int)(sizeof k_baud / sizeof k_baud[0]))

static fwog_link_rx_t s_rx;
static int s_baud_i = 3;
static int s_pin_i;

static void apply(void) {
    if (s_baud_i < 0 || s_baud_i >= TG_NBAUD) s_baud_i = 3;
    if (s_pin_i < 0 || s_pin_i >= (int)TG_NPIN) s_pin_i = 0;
    tg_uart_apply(s_pin_i, k_baud[s_baud_i]);
}

int main(void) {
    board_init();
    const fwog_display_result_t d = fwog_display_update_run();
    DIAG("[ttyglass] display: %s\n", fwog_display_result_text(d));

    for (int i = 0; i < 20; i++) {
        board_watchdog_kick();
        sleep_ms(200);
    }

    fwog_link_rx_init(&s_rx);
    apply();

    while (true) {
        board_watchdog_kick();
        uint8_t b;
        size_t n;
        while (fwog_link_uart_read(&b)) {
            if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
            if (n >= sizeof(tg_cmd_t) && s_rx.buf[0] == TG_MSG_CMD) {
                tg_cmd_t c;
                memcpy(&c, s_rx.buf, sizeof c);
                s_baud_i = (int)c.baud_i;
                s_pin_i = (int)c.pin_i;
                apply();
            }
        }
        if (tg_uart_readable()) {
            tg_data_t m;
            memset(&m, 0, sizeof m);
            m.type = TG_MSG_DATA;
            while (m.n < TG_CHUNK && tg_uart_readable()) {
                m.data[m.n++] = tg_uart_getc();
            }
            (void)fwog_link_uart_send_frame(&m, sizeof m);
        }
        sleep_ms(2);
    }
}
