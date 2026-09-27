#include "fwog_main.h"
#include "tb_serial.h"
#include "pico/stdlib.h"

FWOG_WATCHDOG_DEFAULT();

int main(void) {
    board_init();
    tb_serial_main_init();
    const fwog_display_result_t d = fwog_display_update_run();
    const absolute_time_t announce_until = make_timeout_time_ms(10000);

    while (true) {
        board_watchdog_kick();
        tb_serial_main_poll();
        if (!time_reached(announce_until)) {
            DIAG("[tonebox] display: %s\n", fwog_display_result_text(d));
        }
        sleep_ms(2);
    }
}
