#include "fwog_main.h"
#include "rp_dispatch.h"
#include "pico/stdlib.h"

FWOG_WATCHDOG_DEFAULT();

int main(void) {
    board_init();
    const fwog_display_result_t d = fwog_display_update_run();
    DIAG("[retia_main] display: %s\n", fwog_display_result_text(d));

    /* Splash holds the display ~3 s; io_dir acks need the app loop (link
     * comes up after splash). */
    for (int i = 0; i < 30; i++) {
        board_watchdog_kick();
        sleep_ms(200);
    }

    rp_ctx_t ctx;
    rp_ctx_init(&ctx);
    rp_dispatch_setup(&ctx);
    rp_dispatch_run(&ctx);
}
