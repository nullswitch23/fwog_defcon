/* OpticClick main: FatFs persist for capture slots (last 8 MB volume). */
#include "fwog_main.h"
#include "fs/fwog_fs.h"
#include "oc_proto.h"
#include "pico/stdlib.h"
#include <string.h>

FWOG_WATCHDOG_DEFAULT();

static fwog_link_rx_t   s_rx;
static oc_slots_msg_t   s_slots;
static bool             s_vol;

static void slots_default(void) {
    memset(&s_slots, 0, sizeof s_slots);
    s_slots.type = OC_MSG_SLOTS;
    s_slots.magic = OC_MAGIC;
}

static void load_slots(void) {
    oc_file_t f;
    size_t    n;

    slots_default();
    if (!s_vol && !fwog_fs_mount()) {
        DIAG("[opticclick] no FatFs\n");
        return;
    }
    s_vol = true;
    if (!fwog_fs_open(OC_STORE_NAME, false, false)) {
        DIAG("[opticclick] store missing\n");
        return;
    }
    n = sizeof f;
    memset(&f, 0, sizeof f);
    if (!fwog_fs_read(&f, &n) || n < 4u || f.magic != OC_MAGIC) {
        (void)fwog_fs_close();
        slots_default();
        DIAG("[opticclick] store bad\n");
        return;
    }
    (void)fwog_fs_close();
    s_slots.type = OC_MSG_SLOTS;
    s_slots.magic = OC_MAGIC;
    {
        unsigned nslot = (n > 4u) ? (n - 4u) / 32u : 0u;
        if (nslot > OC_SLOTS) nslot = OC_SLOTS;
        memcpy(s_slots.slot, f.slot, nslot * sizeof f.slot[0]);
    }
    DIAG("[opticclick] store loaded slots=%u bytes=%u\n",
         (unsigned)OC_SLOTS, (unsigned)n);
}

static void save_slots(const oc_slots_msg_t *m) {
    oc_file_t f;

    if (!s_vol && !fwog_fs_mount()) return;
    s_vol = true;
    board_watchdog_kick();
    f.magic = OC_MAGIC;
    memcpy(f.slot, m->slot, sizeof f.slot);
    if (!fwog_fs_open(OC_STORE_NAME, true, false)) {
        DIAG("[opticclick] store open fail\n");
        return;
    }
    (void)fwog_fs_write(&f, sizeof f);
    (void)fwog_fs_close();
    board_watchdog_kick();
    s_slots = *m;
    s_slots.type = OC_MSG_SLOTS;
    s_slots.magic = OC_MAGIC;
    DIAG("[opticclick] store saved\n");
}

static void send_slots(void) {
    s_slots.type = OC_MSG_SLOTS;
    s_slots.magic = OC_MAGIC;
    (void)fwog_link_uart_send_frame(&s_slots, sizeof s_slots);
}

int main(void) {
    board_init();
    const fwog_display_result_t d = fwog_display_update_run();
    board_watchdog_kick();
    DIAG("[opticclick] display: %s\n", fwog_display_result_text(d));

    s_vol = fwog_fs_mount();
    board_watchdog_kick();
    load_slots();
    send_slots();

    while (true) {
        uint8_t b;
        size_t  n;
        board_watchdog_kick();
        while (fwog_link_uart_read(&b)) {
            if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
            if (n >= sizeof(oc_cmd_t) && s_rx.buf[0] == OC_MSG_CMD &&
                s_rx.buf[1] == (uint8_t)OC_CMD_GET) {
                send_slots();
            } else if (n >= sizeof(oc_slots_msg_t) &&
                       s_rx.buf[0] == OC_MSG_SLOTS) {
                oc_slots_msg_t in;
                memcpy(&in, s_rx.buf, sizeof in);
                if (in.magic == OC_MAGIC) save_slots(&in);
            }
        }
        sleep_ms(10);
    }
}
