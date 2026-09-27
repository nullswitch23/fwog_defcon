/* GlassBak main: FatFs dump and CDC restore. */
#include "fwog_main.h"
#include "gb_fs.h"
#include "gb_proto.h"
#include "pico/stdlib.h"

FWOG_WATCHDOG_DEFAULT();

static fwog_link_rx_t s_rx;

int main(void) {
    board_init();
    const fwog_display_result_t d = fwog_display_update_run();
    board_watchdog_kick();
    DIAG("[glassbak] display: %s\n", fwog_display_result_text(d));
    fwog_link_rx_init(&s_rx);
    (void)fwog_link_uart_init(FWOG_LINK_BAUD);

    while (true) {
        uint8_t b;
        size_t n;
        board_watchdog_kick();
        gb_fs_poll_restore();
        while (fwog_link_uart_read(&b)) {
            if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
            if (n >= sizeof(gb_cmd_t) && s_rx.buf[0] == GB_MSG_CMD &&
                s_rx.buf[1] == GB_CMD_DUMP) {
                gb_fs_dump();
            }
        }
        sleep_ms(2);
    }
}
