#include "fwog_display.h"
#include "rp_device_view.h"
#include "rp_proto.h"
#include "pico/stdlib.h"
#include <string.h>

FWOG_POWER_DEFAULT();

static void drain_link(fwog_link_rx_t *rx, rp_state_t *st) {
    uint8_t b;
    size_t n;
    while (fwog_link_uart_read(&b)) {
        if (!fwog_link_rx_byte(rx, b, &n)) continue;
        if (fwog_ioexp_link_handle(rx->buf, n)) continue;
        if (n >= sizeof(rp_state_t) && rx->buf[0] == RP_MSG_STATE) {
            memcpy(st, rx->buf, sizeof *st);
        }
    }
}

int main(void) {
    board_init();
    fwog_link_rx_t rx;
    fwog_link_rx_init(&rx);
    /* Splash first with UART0 still in RESET (pump is a no-op). uart-before-
     * splash filled the RX FIFO during LCD init with no reader, then CTS
     * deadlocked io_dir ACKs — same class of bug as ChirpMail 005. Main
     * waits out the dwell before the first apply. */
    rp_device_view_init();
    (void)fwog_link_uart_init(FWOG_LINK_BAUD);

    rp_state_t st;
    memset(&st, 0, sizeof st);
    st.type = RP_MSG_STATE;
    strncpy(st.status, "Waiting", sizeof st.status);

    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        (void)fwog_power_poll(now);
        drain_link(&rx, &st);
        rp_device_view_paint(&st);
        drain_link(&rx, &st);
        sleep_ms(2);
    }
}
