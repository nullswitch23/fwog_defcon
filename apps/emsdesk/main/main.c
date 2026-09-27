#include "fwog_main.h"
#include "ed_jobs.h"
#include "ed_proto.h"
#include "pico/stdlib.h"
#include <string.h>

FWOG_WATCHDOG_DEFAULT();

static cc1101_t ed_r0, ed_r1;
static fwog_link_rx_t s_rx;
static uint8_t s_sel;

static void radios_idle(void) {
    (void)cc1101_idle(&ed_r0);
    (void)cc1101_idle(&ed_r1);
}

static void leave_tile(uint8_t was) {
    if (was == ED_SEL_BS) ed_bs_job_leave();
    else if (was == ED_SEL_TF) ed_tf_job_leave();
    else if (was == ED_SEL_TE) ed_te_job_leave();
    else if (was == ED_SEL_IB) ed_ib_job_leave();
    else if (was == ED_SEL_FOB) ed_fob_job_leave();
    else if (was == ED_SEL_CM) ed_cm_job_leave();
    else if (was == ED_SEL_OC) ed_oc_job_leave();
    radios_idle();
}

static void enter_tile(uint8_t sel) {
    radios_idle();
    if (sel == ED_SEL_BS) ed_bs_job_enter();
    else if (sel == ED_SEL_TF) ed_tf_job_enter();
    else if (sel == ED_SEL_TE) ed_te_job_enter();
    else if (sel == ED_SEL_IB) ed_ib_job_enter();
    else if (sel == ED_SEL_FOB) ed_fob_job_enter();
    else if (sel == ED_SEL_CM) ed_cm_job_enter();
    else if (sel == ED_SEL_OC) ed_oc_job_enter();
}

static void apply_sel(uint8_t app) {
    if (app == s_sel) return;
    DIAG("[emsdesk] sel=%u\n", (unsigned)app);
    leave_tile(s_sel);
    s_sel = app;
    enter_tile(s_sel);
}

int main(void) {
    board_init();
    const fwog_display_result_t d = fwog_display_update_run();
    DIAG("[emsdesk] display: %s\n", fwog_display_result_text(d));

    cc1101_bus_init(1000000u);
    cc1101_bind(&ed_r0, CC1101_RADIO_CS0);
    cc1101_bind(&ed_r1, CC1101_RADIO_CS1);
    (void)(cc1101_bringup(&ed_r0) && cc1101_idle(&ed_r0));
    (void)(cc1101_bringup(&ed_r1) && cc1101_idle(&ed_r1));

    fwog_link_rx_init(&s_rx);
    (void)fwog_link_uart_init(FWOG_LINK_BAUD);

    while (true) {
        board_watchdog_kick();
        if (s_sel == ED_SEL_HOME) {
            uint8_t b;
            size_t n;
            while (fwog_link_uart_read(&b)) {
                if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
                ed_note_sel(s_rx.buf, n);
            }
        } else if (s_sel == ED_SEL_BS) ed_bs_job_tick();
        else if (s_sel == ED_SEL_TF) ed_tf_job_tick();
        else if (s_sel == ED_SEL_TE) ed_te_job_tick();
        else if (s_sel == ED_SEL_IB) ed_ib_job_tick();
        else if (s_sel == ED_SEL_FOB) ed_fob_job_tick();
        else if (s_sel == ED_SEL_CM) ed_cm_job_tick();
        else if (s_sel == ED_SEL_OC) ed_oc_job_tick();

        {
            const uint8_t req = ed_take_sel();
            if (req != 0xFFu) apply_sel(req);
        }
        if (s_sel == ED_SEL_HOME) sleep_ms(20);
    }
}
