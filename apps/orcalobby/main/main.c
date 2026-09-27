#include "fwog_main.h"
#include "ol_am_main.h"
#include "ol_bd_main.h"
#include "ol_lf_main.h"
#include "ol_ph_main.h"
#include "ol_proto.h"
#include "pico/stdlib.h"
#include <string.h>

FWOG_WATCHDOG_DEFAULT();

static fwog_link_rx_t s_rx;
static uint8_t s_sel;

static void leave_tile(uint8_t was) {
    if (was == 1u) ol_lf_main_leave();
    else if (was == 2u) ol_ph_main_leave();
    else if (was == 3u) ol_bd_main_leave();
    else if (was == 4u) ol_am_main_leave();
}

static void enter_tile(uint8_t sel) {
    if (sel == 1u) ol_lf_main_enter();
    else if (sel == 2u) ol_ph_main_enter();
    else if (sel == 3u) ol_bd_main_enter();
    else if (sel == 4u) ol_am_main_enter();
}

int main(void) {
    board_init();
    const fwog_display_result_t d = fwog_display_update_run();
    DIAG("[orcalobby] display: %s\n", fwog_display_result_text(d));

    for (int i = 0; i < 20; i++) {
        board_watchdog_kick();
        sleep_ms(200);
    }

    DIAG("[orcalobby] io_dir=%d c6_image=%u\n",
         (int)fwog_bn_link_app(), fwog_c6_image_size);

    fwog_link_rx_init(&s_rx);
    (void)fwog_link_uart_init(FWOG_LINK_BAUD);

    while (true) {
        board_watchdog_kick();
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        uint8_t b;
        size_t n;
        while (fwog_link_uart_read(&b)) {
            if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
            if (n >= sizeof(desk_sel_t) && s_rx.buf[0] == DESK_MSG_SEL) {
                desk_sel_t s;
                memcpy(&s, s_rx.buf, sizeof s);
                if (s.app != s_sel) {
                    leave_tile(s_sel);
                    s_sel = s.app;
                    DIAG("[orcalobby] sel=%u\n", (unsigned)s_sel);
                    enter_tile(s_sel);
                }
                continue;
            }
            if (s_sel == 1u) ol_lf_main_frame(s_rx.buf, n);
            else if (s_sel == 2u) ol_ph_main_frame(s_rx.buf, n);
            else if (s_sel == 3u) ol_bd_main_frame(s_rx.buf, n);
            else if (s_sel == 4u) ol_am_main_frame(s_rx.buf, n);
        }
        if (s_sel == 1u) ol_lf_main_tick(now);
        else if (s_sel == 2u) ol_ph_main_tick(now);
        else if (s_sel == 3u) ol_bd_main_tick(now);
        else if (s_sel == 4u) ol_am_main_tick(now);
        sleep_ms(2);
    }
}
