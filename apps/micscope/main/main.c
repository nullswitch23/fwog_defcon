/* MicScope main: FatFs quiet-room cal for the display spectrogram. */
#include "fwog_main.h"
#include "fs/fwog_fs.h"
#include "ms_proto.h"
#include "pico/stdlib.h"
#include <string.h>

FWOG_WATCHDOG_DEFAULT();

static fwog_link_rx_t s_rx;
static ms_cal_t       s_cal;
static bool           s_vol;

static void cal_default(void) {
    memset(&s_cal, 0, sizeof s_cal);
    s_cal.type = MS_MSG_CAL;
    s_cal.magic = MS_MAGIC;
}

static void load_cal(void) {
    size_t n;
    cal_default();
    if (!s_vol && !fwog_fs_mount()) {
        DIAG("[micscope] no FatFs\n");
        return;
    }
    s_vol = true;
    if (!fwog_fs_open(MS_CAL_NAME, false, false)) return;
    n = sizeof s_cal;
    if (!fwog_fs_read(&s_cal, &n) || n != sizeof s_cal ||
        s_cal.magic != MS_MAGIC || !s_cal.ok) {
        (void)fwog_fs_close();
        cal_default();
        DIAG("[micscope] cal missing\n");
        return;
    }
    (void)fwog_fs_close();
    s_cal.type = MS_MSG_CAL;
    DIAG("[micscope] cal rms=%u\n", (unsigned)s_cal.rms);
}

static void save_cal(const ms_cal_t *m) {
    if (!s_vol && !fwog_fs_mount()) return;
    s_vol = true;
    board_watchdog_kick();
    if (!fwog_fs_open(MS_CAL_NAME, true, false)) {
        DIAG("[micscope] cal open fail\n");
        return;
    }
    s_cal = *m;
    s_cal.type = MS_MSG_CAL;
    s_cal.magic = MS_MAGIC;
    (void)fwog_fs_write(&s_cal, sizeof s_cal);
    (void)fwog_fs_close();
    board_watchdog_kick();
    DIAG("[micscope] cal saved rms=%u\n", (unsigned)s_cal.rms);
}

static void send_cal(void) {
    if (!s_cal.ok) return;
    (void)fwog_link_uart_send_frame(&s_cal, sizeof s_cal);
}

int main(void) {
    board_init();
    const fwog_display_result_t d = fwog_display_update_run();
    board_watchdog_kick();
    DIAG("[micscope] display: %s\n", fwog_display_result_text(d));

    s_vol = fwog_fs_mount();
    board_watchdog_kick();
    load_cal();
    send_cal();

    while (true) {
        uint8_t b;
        size_t n;
        board_watchdog_kick();
        while (fwog_link_uart_read(&b)) {
            if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
            if (n >= sizeof(ms_cmd_t) && s_rx.buf[0] == MS_MSG_CMD &&
                s_rx.buf[1] == (uint8_t)MS_CMD_GET) {
                send_cal();
            } else if (n >= sizeof(ms_cal_t) && s_rx.buf[0] == MS_MSG_CAL) {
                ms_cal_t in;
                memcpy(&in, s_rx.buf, sizeof in);
                if (in.magic == MS_MAGIC && in.ok) save_cal(&in);
            }
        }
        sleep_ms(10);
    }
}
