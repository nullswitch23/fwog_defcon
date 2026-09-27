#include "fwog_display.h"
#include "pico/stdlib.h"

/* Red held 6 s powers the board off, with the LED countdown on the WS2812
   bar. Every display app must declare a policy -- board_init() references the
   symbol these macros define, so an app that declares neither does not link.
   See bsp/display_cpu/power/power_poll.h. */
FWOG_POWER_DEFAULT();

int main(void) {
    board_init();

    /* Poll fast, report slowly. The obvious `sleep_ms(1000)` loop would sample
       the buttons once a second, which is too coarse for the debouncer the
       ship-mode hold is built on -- the hold would still fire, but a brush
       against the button could look like a press. Copy this shape. */
    absolute_time_t next_beat = make_timeout_time_ms(1000);
    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        fwog_power_poll(now);

        if (time_reached(next_beat)) {
            next_beat = make_timeout_time_ms(1000);
            DIAG("[template_display] alive\n");
        }
        sleep_ms(2);
    }
}
