#include "fwog_main.h"
#include "pico/stdlib.h"

FWOG_WATCHDOG_DEFAULT();

int main(void) {
    board_init();
    const fwog_display_result_t d = fwog_display_update_run();
    const absolute_time_t announce_until = make_timeout_time_ms(10000);

    while (true) {
        board_watchdog_kick();
        if (!time_reached(announce_until)) {
            DIAG("[hostdeck] display: %s\n", fwog_display_result_text(d));
        }
        sleep_ms(500);
    }
}
