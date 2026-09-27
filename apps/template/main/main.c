#include "fwog_main.h"
#include "pico/stdlib.h"

/* Every main app must kick the 8.3 s watchdog board_init() arms, and must
   declare that it does -- board_init() references the symbol this macro
   defines, so an app declaring neither policy does not link. The kick itself
   is in the loop below; nothing can check that for you.
   See bsp/main_cpu/watchdog/watchdog.h. */
FWOG_WATCHDOG_DEFAULT();

int main(void) {
    board_init();

    /* Brings the display up: link, HELLO, reflash if the embedded image
       differs, then RUN. This performs board_release_display() itself, at
       the point in the sequence where the announce window is guaranteed to
       be caught -- so applications must NOT call it separately. */
    fwog_display_result_t d = fwog_display_update_run();

    /* Repeated, not printed once. The whole display handshake finishes in
       the first second or two of boot -- before USB CDC has enumerated and
       the host has asserted DTR -- and pico_stdio_usb drops anything written
       before then (the hardware record). A single DIAG here is written into the void
       every time, which is exactly what happened when this line was first
       used to check whether an update had run. Repeating it for a few
       seconds is the difference between a diagnostic and a decoration. */
    const absolute_time_t announce_until = make_timeout_time_ms(10000);

    while (true) {
        board_watchdog_kick();      /* required: see watchdog.h */
        if (!time_reached(announce_until)) {
            DIAG("[template_main] display: %s\n", fwog_display_result_text(d));
        } else {
            DIAG("[template_main] alive\n");
        }
        sleep_ms(500);
    }
}
