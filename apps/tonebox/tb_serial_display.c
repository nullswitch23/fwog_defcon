#include "tb_serial.h"
#include "tb_proto.h"
#ifndef HOST_TEST
#include "common/link/link_frame.h"
#include "common/link/link_uart.h"
#include "pico/stdlib.h"
#endif
#include <string.h>

#ifndef HOST_TEST
static fwog_link_rx_t s_rx;
static bool s_link;
static volatile int8_t s_hang_result; /* -2 idle, -1 err, 0 fail, 1 ok */
static uint32_t s_hang_deadline;
static void (*s_pump)(void);
#endif

bool tb_serial_display_init(void) {
#ifndef HOST_TEST
    s_link = fwog_link_uart_init(FWOG_LINK_BAUD);
    fwog_link_rx_init(&s_rx);
    s_hang_result = -2;
    return s_link;
#else
    return true;
#endif
}

void tb_serial_display_attach(void) {
#ifndef HOST_TEST
    s_link = true;
    s_hang_result = -2;
#else
    (void)0;
#endif
}

void tb_serial_display_set_pump(void (*fn)(void)) {
#ifndef HOST_TEST
    s_pump = fn;
#else
    (void)fn;
#endif
}

void tb_serial_display_on_frame(const uint8_t *buf, size_t n) {
#ifndef HOST_TEST
    if (n == sizeof(tb_hangup_rsp_t) && buf) {
        const tb_hangup_rsp_t *rsp = (const tb_hangup_rsp_t *)buf;
        if (rsp->type == TB_MSG_HANGUP_RSP) {
            if (rsp->result == 1u) s_hang_result = 1;
            else if (rsp->result == 0u) s_hang_result = 0;
            else s_hang_result = -1;
        }
    }
#else
    (void)buf;
    (void)n;
#endif
}

void tb_serial_display_poll(void) {
#ifndef HOST_TEST
    uint8_t b;
    size_t n;
    while (fwog_link_uart_read(&b)) {
        if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
        tb_serial_display_on_frame(s_rx.buf, n);
    }
#else
    (void)0;
#endif
}

int tb_serial_hangup(unsigned timeout_ms) {
#ifndef HOST_TEST
    if (!s_link) return -1;
    tb_hangup_req_t req = {
        .type = TB_MSG_HANGUP,
        .cmd = 'H',
        .timeout_ms = (uint16_t)(timeout_ms > 60000u ? 60000u : timeout_ms),
    };
    s_hang_result = -2;
    s_hang_deadline = to_ms_since_boot(get_absolute_time()) + timeout_ms + 500u;
    if (!fwog_link_uart_send_frame(&req, sizeof req)) return -1;
    while ((int32_t)(to_ms_since_boot(get_absolute_time()) - s_hang_deadline) < 0) {
        if (s_pump) s_pump();
        else tb_serial_display_poll();
        if (s_hang_result != -2) return (int)s_hang_result;
        sleep_ms(1);
    }
    return -1;
#else
    (void)timeout_ms;
    return 1;
#endif
}
