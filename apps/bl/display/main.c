/* FreeWili OG display serial bootloader.
 *
 * Owns the first moments of every display boot:
 *
 *   T0            200 MHz, park pins, read metadata, link up at 12.5 Mbaud,
 *                 sample buttons. LCD dark.
 *   T0..T+500ms   announce HELLO every 50 ms.
 *   main -> RUN            jump to the app; nothing was drawn.
 *   main -> UPDATE_BEGIN   reflash, verify, then wait for RUN.
 *   T+500ms, silent        screen up, buttons live, keep announcing.
 *   T+10s, silent          USB CDC console. Stay indefinitely.
 *
 * With main silent the bootloader deliberately does NOT auto-boot: main
 * being silent means the board is already broken, and a guaranteed console
 * window is worth more than starting an app whose peer is gone. Green boots
 * it manually.
 *
 * This is the one component on the board whose replacement needs physical
 * BOOTSEL access, so: no watchdog (it legitimately waits for main), no
 * dependency on anything in the app slot, and as little code as will do. */
#include "fwog_display.h"
#include "bootloader/bl_console.h"
#include "bootloader/bl_flash.h"
#include "bootloader/bl_jump.h"
#include "bootloader/bl_policy.h"
#include "bootloader/bl_receiver.h"
#include "bootloader/bl_ship.h"
#include "bootloader/bl_ui.h"
#include "pico/stdlib.h"
#include <string.h>

/* The bootloader owns the power path itself, through bootloader/bl_ship.c --
   the same BATFET_DIS write, in the one binary that cannot link application
   code. It does not call fwog_power_poll().

   It also calls board_init_pins() rather than board_init(), so the reference
   that forces this declaration is garbage-collected out of this image
   anyway. Declared regardless, so the guard does not depend on that staying
   true. */
FWOG_POWER_CUSTOM();

#ifndef FWOG_BL_VERSION
#define FWOG_BL_VERSION "dev"    /* built outside CMake */
#endif

/* All in .bss. fwog_link_rx_t alone is ~4.2 KB, which is twice the SDK's
 * default 2 KB stack. */
static fwog_link_rx_t   s_rx;
static fwog_bl_update_t s_upd;
static fwog_app_meta_t  s_meta;
static uint8_t          s_hello[sizeof(fwog_bl_hello_t)];

static void build_hello(bool app_valid) {
    fwog_bl_hello_t h;
    memset(&h, 0, sizeof h);
    h.type      = FWOG_BL_MSG_HELLO;
    h.proto_ver = FWOG_BL_PROTO_VER;
    h.app_valid = app_valid ? 1u : 0u;
    if (app_valid) {
        /* fwog_app_meta_valid() already confirmed version[] is terminated,
           so copying the whole array cannot produce an unterminated field
           on the wire -- which the peer's decoder would reject. */
        h.app_crc32 = s_meta.crc32;
        h.app_size  = s_meta.size;
        h.build_ts  = s_meta.build_ts;
        memcpy(h.version, s_meta.version, sizeof h.version);
    }
    /* bl_version is 16 bytes and `git describe` on a tagged build is easily
       longer, so truncate. The memset above guarantees termination. */
    size_t n = strlen(FWOG_BL_VERSION);
    if (n > FWOG_BL_BLVER_LEN - 1u) n = FWOG_BL_BLVER_LEN - 1u;
    memcpy(h.bl_version, FWOG_BL_VERSION, n);

    memcpy(s_hello, &h, sizeof h);
}

/* Which screen state the current situation implies. Derived rather than
 * mutated, so no code path can leave a stale state on the display. */
static bl_ui_state_t ui_state_now(bool app_valid, bool console_up) {
    switch (s_upd.state) {
    case FWOG_BL_RX_RECEIVING: return BL_UI_UPDATING;
    case FWOG_BL_RX_DONE_FAIL: return BL_UI_CRC_FAIL;
    default: break;
    }
    if (!app_valid) return BL_UI_NO_APP;
    return console_up ? BL_UI_CONSOLE : BL_UI_WAITING;
}

int main(void) {
    /* fwog_clocks_init + board_init_pins, NOT board_init(): the bootloader
       must not bring up I2C, because that bus carries the battery charger
       and one stray write there powers the board off. board_init_pins()
       does configure the backlight PWM, but at zero duty, so the screen
       stays dark through a healthy boot exactly as required. */
    fwog_clocks_init(FWOG_SYS_CLK_KHZ);
    board_init_pins();

    /* Let the internal pull-ups charge the button lines before the
       reset-time sample. Reading immediately after gpio_pull_up() can see
       a pin still at its floating level, i.e. a phantom press. */
    sleep_us(500);
    bl_buttons_t btn;
    bl_buttons_sample(&btn);
    const bool red_at_reset = btn.red;

    bool app_valid = bl_flash_read_meta(&s_meta) && bl_app_image_ok(&s_meta);
    build_hello(app_valid);

    const bool link_up = fwog_link_uart_init(FWOG_LINK_BAUD);
    fwog_link_rx_init(&s_rx);
    fwog_bl_update_init(&s_upd, bl_flash_ops());

    bool ui_up = false, console_up = false, ui_dirty = false;
    /* Seeded from the real derivation, not a fixed BL_UI_WAITING: whenever
       app_valid is false -- the normal case mid-update -- the true state is
       BL_UI_NO_APP, and a wrong seed would make the render block's
       `ui != ui_shown` fire on ui_up's first iteration regardless of
       ui_dirty, clobbering whatever bl_ui_status() just drew for main.
       console_up is always false this early -- USB has not enumerated yet. */
    bl_ui_state_t ui_shown = ui_state_now(app_valid, false);
    uint8_t pct_shown = 0u;

    bl_debounce_t green_db, gray_db;
    memset(&green_db, 0, sizeof green_db);
    memset(&gray_db,  0, sizeof gray_db);

    const absolute_time_t t0 = get_absolute_time();
    absolute_time_t next_hello = t0;
    absolute_time_t next_btn   = make_timeout_time_ms(BL_BTN_SAMPLE_MS);

    while (true) {
        const uint32_t elapsed =
            (uint32_t)(absolute_time_diff_us(t0, get_absolute_time()) / 1000);
        const bl_phase_t phase = bl_phase_for(elapsed, red_at_reset);

        /* --- Drain the link first. It is the only thing here with a
               deadline: at 12.5 Mbaud a byte lands every 800 ns and the
               PL011 FIFO holds 32, so the whole slack is about 25 us.
               Hardware RTS covers the gaps, but only if this loop keeps
               emptying the FIFO. --- */
        uint8_t b;
        while (link_up && fwog_link_uart_read(&b)) {
            size_t n = 0;
            if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;

            const uint8_t mt = fwog_bl_msg_type(s_rx.buf, n);

            if (mt == FWOG_BL_MSG_RUN) {
                if (bl_should_auto_boot(red_at_reset, app_valid, true)) {
                    bl_jump_to_app();     /* does not return */
                }
                continue;
            }

            if (mt == FWOG_BL_MSG_STATUS) {
                fwog_bl_status_t st;
                memcpy(&st, s_rx.buf, sizeof st);
                /* Main explicitly asked for the screen, so it comes up now
                   -- even inside the 500 ms window where a healthy boot is
                   deliberately silent. What keeps that boot silent is that
                   main does not send STATUS on the fast path, not a
                   prohibition here: the whole point of the message is that
                   main gets to decide when the screen matters.

                   Deliberately does NOT set ui_dirty. Main's line outranks
                   the bootloader's own state line until the state actually
                   changes, which is when the render block below redraws. */
                if (!ui_up) {
                    bl_ui_init(FWOG_BL_VERSION,
                               app_valid ? s_meta.version : NULL);
                    ui_up = true;
                }
                bl_ui_status(st.text, st.percent);
                continue;
            }

            fwog_bl_reply_t rep;
            if (!fwog_bl_update_on_msg(&s_upd, s_rx.buf, n, &rep)) continue;
            if (rep.len) fwog_link_uart_send_frame(rep.buf, rep.len);

            if (s_upd.state == FWOG_BL_RX_DONE_OK) {
                /* Re-read from flash rather than trusting the receiver:
                   the point of the metadata record is that it is what a
                   cold boot would see. */
                app_valid = bl_flash_read_meta(&s_meta) && bl_app_image_ok(&s_meta);
                build_hello(app_valid);
            }
        }

        /* --- Announce, except while a transfer is in flight. --- */
        if (link_up && s_upd.state != FWOG_BL_RX_RECEIVING &&
            time_reached(next_hello)) {
            fwog_link_uart_send_frame(s_hello, sizeof(fwog_bl_hello_t));
            /* Re-armed from now, not from the previous deadline: a loop
               that stalled past several periods should resume announcing,
               not burst to catch up. */
            next_hello = make_timeout_time_ms(BL_HELLO_PERIOD_MS);
        }

        /* --- Phase transitions, each one-way. --- */
        if (phase >= BL_PHASE_VISIBLE && !ui_up) {
            bl_ui_init(FWOG_BL_VERSION, app_valid ? s_meta.version : NULL);
            ui_up = true;
            ui_dirty = true;
        }

        /* Advance the panel's staged bring-up. Must run every iteration:
           the LCD needs ~125 ms of datasheet delays and bl_ui_show() is
           only called when something changes. */
        if (ui_up) bl_ui_tick();

        if (phase == BL_PHASE_CONSOLE && !console_up) {
            bl_console_init(FWOG_BL_VERSION);   /* this enumerates USB */
            console_up = true;
            ui_dirty = true;
        }

        /* --- Render on change only. The percentage is quantized to 5%
               steps: at 4 KB per chunk a 500 KB image would otherwise
               redraw 125 times, and with the DIAG implementation that is
               125 lines of console noise. --- */
        if (ui_up) {
            const bl_ui_state_t ui = ui_state_now(app_valid, console_up);
            uint8_t pct = 0u;
            if (ui == BL_UI_UPDATING) {
                pct = (uint8_t)((fwog_bl_update_percent(&s_upd) / 5u) * 5u);
            }
            if (ui_dirty || ui != ui_shown || pct != pct_shown) {
                bl_ui_show(ui, pct);
                ui_shown = ui;
                pct_shown = pct;
                ui_dirty = false;
            }
        }

        if (console_up) bl_console_poll();

        /* --- Buttons, live once the screen is up. Both actions are
               one-way, so both are edge-triggered after 40 ms of steady
               contact. --- */
        if (ui_up && time_reached(next_btn)) {
            next_btn = make_timeout_time_ms(BL_BTN_SAMPLE_MS);
            bl_buttons_sample(&btn);

            /* Green is inert with no valid app -- the design doc says it
               should be shown inactive, but the zone table has no button
               affordance and nothing currently draws one. Known gap, not
               yet implemented. */
            if (bl_debounce_feed(&green_db, btn.green) && app_valid) {
                bl_jump_to_app();     /* does not return */
            }
            if (bl_debounce_feed(&gray_db, btn.gray)) {
                /* Ship mode. Usually does not return, because power goes
                   away; bl_ship_mode() reports its own failure. */
                (void)bl_ship_mode();
            }
        }
    }
}
