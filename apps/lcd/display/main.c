/* ST7789 bring-up. Proves the panel on hardware before the bootloader
 * depends on it.
 *
 * Runs from the app slot, so it is delivered by the bootloader over the
 * link -- no BOOTSEL, and the bootloader is not overwritten.
 *
 * What to look for, in order:
 *   1.  Four full-screen fills: red, green, blue, white, each covering the
 *       WHOLE panel. The console prints a non-zero loop count per fill --
 *       zero means the fill is still effectively synchronous.
 *   1b. A white field with four 16x16 corner markers. Check the
 *       BOTTOM-RIGHT one: a fill clean everywhere except its bottom-right
 *       corner is the signature of a completion condition that checked the
 *       DMA channel but not the SPI shifter.
 *   2.  Four vertical bars, RED ON THE LEFT.
 *   3.  Five bootloader state strings at scale 3, upright and legible.
 *   4.  A bar sweeping 0-100% using the bootloader's own renderers.
 *   5.  The backlight fading down and back up smoothly, not snapping. */
#include <stdio.h>
#include "fwog_display.h"
#include "bootloader/bl_ui.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#if FWOG_DIAG == FWOG_DIAG_USB
#include "pico/stdio_usb.h"

/* Red held 6 s powers the board off. See power/power_poll.h. */
FWOG_POWER_DEFAULT();
#endif

int main(void) {
    board_init();

    /* Wait (bounded) for a host to attach before doing anything worth
       reporting. pico_stdio_usb DROPS output when nothing is connected --
       it does not buffer -- so a bring-up app that starts printing the
       instant it boots loses every line to the ~1 s Windows takes to bind
       the port after re-enumeration. That made the DMA spin counts below
       unobservable in practice.
       Bounded, not unbounded, for the same reason the LCD wait below is:
       this app must still run start-to-finish on a board with no host
       attached, so the panel can be watched on its own. */
#if FWOG_DIAG == FWOG_DIAG_USB
    const absolute_time_t host_deadline = make_timeout_time_ms(5000);
    while (!stdio_usb_connected() && !time_reached(host_deadline)) {
        tight_loop_contents();
    }
    /* The host binds the port a moment before it opens it; without this the
       first line still races the reader. */
    sleep_ms(300);
#endif

    DIAG("[lcd_display] alive\n");
    /* Re-reported here, not just from board_init(): that call happens before
       the wait above, so its line is dropped on exactly the runs where
       someone is watching. */
    DIAG("[lcd_display] ioexp=%s (PCAL6416 at 0x21)\n",
         board_ioexp_ok() ? "ok" : "FAILED");

    st7789_init_begin();
    /* Bounded, not unbounded: st7789_init_begin() can fail to leave
     * ST7789_INIT_IDLE (when clk_peri can't reach the panel rate -- see
     * the hardware record, a 48 MHz clk_peri measured on this exact board), which
     * makes st7789_init_step() a permanent no-op. An unbounded wait here
     * would then spin forever with no watchdog to recover it. 500 ms is
     * comfortably more than the ~125 ms a healthy init needs, so this
     * costs nothing when the panel is fine. Do not remove the deadline. */
    const absolute_time_t lcd_deadline = make_timeout_time_ms(500);
    while (!st7789_ready() && !time_reached(lcd_deadline)) st7789_init_step();
    if (!st7789_ready()) {
        DIAG("[lcd_display] panel never became ready -- check clk_peri "
             "(the hardware record); every draw below will be a no-op\n");
    } else {
        DIAG("[lcd_display] panel ready\n");
    }

    /* 1. Solid fills: proves windowing, byte order and the fill stream.
          st7789_clear() is now ASYNCHRONOUS -- it starts a DMA transfer and
          returns -- so the spin count below is direct evidence the CPU was
          free while the panel was painted. A zero would mean the transfer
          is still effectively synchronous and nothing was gained. */
    const uint16_t colors[] = {
        st7789_rgb565(255, 0, 0), st7789_rgb565(0, 255, 0),
        st7789_rgb565(0, 0, 255), st7789_rgb565(255, 255, 255),
    };
    uint32_t spin[4] = {0};
    for (unsigned i = 0; i < count_of(colors); i++) {
        st7789_clear(colors[i]);
        uint32_t spins = 0u;
        while (st7789_busy()) spins++;
        spin[i] = spins;
        DIAG("[lcd_display] fill %u: %u loops while DMA painted\n",
             i, (unsigned)spins);
        board_backlight(255);
        sleep_ms(400);
    }
    st7789_clear(0x0000u);
    st7789_dma_wait();

    /* 1b. Corner-to-corner check. A clean fill with a corrupted or missing
          bottom-right region is the exact signature of a completion
          condition that checked the DMA channel but not the SPI shifter,
          so put a marker in each corner of a white field and look at all
          four. The BOTTOM-RIGHT one is the one that matters. */
    st7789_clear(st7789_rgb565(255, 255, 255));
    st7789_fill_rect(0u, 0u, 16u, 16u, st7789_rgb565(255, 0, 0));
    st7789_fill_rect(ST7789_W - 16u, 0u, 16u, 16u, st7789_rgb565(0, 255, 0));
    st7789_fill_rect(0u, ST7789_H - 16u, 16u, 16u, st7789_rgb565(0, 0, 255));
    st7789_fill_rect(ST7789_W - 16u, ST7789_H - 16u, 16u, 16u, 0x0000u);
    st7789_dma_wait();
    DIAG("[lcd_display] corner markers up: check BOTTOM-RIGHT is a clean "
         "black square on white\n");
    sleep_ms(3000);
    st7789_clear(0x0000u);
    st7789_dma_wait();

    /* 2. Vertical bars: proves x addressing is not mirrored or offset.
          Red must be on the LEFT. */
    for (unsigned i = 0; i < count_of(colors); i++) {
        st7789_fill_rect((uint16_t)(i * 80u), 0u, 80u, 60u, colors[i]);
    }

    /* 3. The five bootloader strings at their real zone scale. */
    static const char *states[] = {
        "waiting for main", "updating", "no valid app",
        "image CRC failed", "console active",
    };
    for (unsigned i = 0; i < count_of(states); i++) {
        lcd_text_draw_padded(0u, 88u, states[i], 17u, 3u, 0xFFFFu, 0x0000u);
        sleep_ms(700);
    }

    /* 4. Bar sweep, using the bootloader's own renderer so what is proved
          here is what the bootloader will draw. */
    for (unsigned p = 0u; p <= 100u; p += 5u) {
        char bar[BL_BAR_BUF], line[BL_UI_LINE_BUF];
        bl_ui_bargraph(bar, (uint8_t)p);
        bl_ui_state_line(line, sizeof line, BL_UI_UPDATING, (uint8_t)p);
        lcd_text_draw_padded(0u,  88u, line, 17u, 3u, 0xFFFFu, 0x0000u);
        lcd_text_draw_padded(28u, 150u, bar, 22u, 2u, 0xFFFFu, 0x0000u);
        sleep_ms(120);
    }

    /* 5. Backlight ramp: proves board_backlight() really dims, which the
          legacy driver's on/off set_backlight() could not. */
    for (int lvl = 255; lvl >= 0; lvl -= 5) { board_backlight((uint8_t)lvl); sleep_ms(20); }
    for (int lvl = 0; lvl <= 255; lvl += 5) { board_backlight((uint8_t)lvl); sleep_ms(20); }

    DIAG("[lcd_display] done\n");
    DIAG("[lcd_display] SUMMARY dma spins per full-screen fill: "
         "%u %u %u %u (all must be non-zero)\n",
         (unsigned)spin[0], (unsigned)spin[1],
         (unsigned)spin[2], (unsigned)spin[3]);

    /* 6. Buttons, live until reset. The five have NEVER been pressed on a
          board in this project (the hardware record), so this is their first proof:
          it checks the pin map, the active-low inversion and the debounce
          all at once. Watch the panel, or the console, or both. */
    static const char *name[FWOG_BTN_COUNT] = {
        "GRAY", "YELW", "GREN", "BLUE", "RED"
    };
    const uint16_t col_up   = st7789_rgb565(40, 40, 40);
    const uint16_t col_down = st7789_rgb565(0, 220, 0);

    fwog_buttons_init();
    st7789_clear(0x0000u);
    lcd_text_draw_padded(0u, 4u, "press each button", 26u, 1u, 0xFFFFu, 0x0000u);
    for (unsigned i = 0; i < FWOG_BTN_COUNT; i++) {
        const uint16_t x = (uint16_t)(i * 64u);
        lcd_text_draw_padded(x, 90u, name[i], 5u, 1u, 0xFFFFu, 0x0000u);
        st7789_fill_rect(x, 104u, 60u, 40u, col_up);
    }
    st7789_dma_wait();
    board_backlight(255);
    DIAG("[lcd_display] buttons live: gray yellow green blue red\n");

    uint8_t shown = 0u;
    absolute_time_t next_beat = make_timeout_time_ms(5000);
    while (true) {
        const uint32_t now =
            (uint32_t)(to_us_since_boot(get_absolute_time()) / 1000u);
        /* Via fwog_power_poll() rather than fwog_buttons_poll(): one poll
           per iteration, because the debounce state it carries is what the
           ship-mode hold is built on. */
        const fwog_buttons_t b = fwog_power_poll(now).buttons;

        for (unsigned i = 0; i < FWOG_BTN_COUNT; i++) {
            const uint8_t bit = (uint8_t)FWOG_BTN_BIT(i);
            if (b.pressed & bit)  DIAG("[btn] %s pressed\n", name[i]);
            if (b.released & bit) DIAG("[btn] %s released\n", name[i]);
        }

        /* Repaint only on change: a fill per iteration would keep the DMA
           channel busy continuously and tell you nothing extra. */
        if (b.down != shown) {
            for (unsigned i = 0; i < FWOG_BTN_COUNT; i++) {
                const uint8_t bit = (uint8_t)FWOG_BTN_BIT(i);
                if ((b.down & bit) == (shown & bit)) continue;
                st7789_fill_rect((uint16_t)(i * 64u), 104u, 60u, 40u,
                                 (b.down & bit) ? col_down : col_up);
            }
            shown = b.down;
        }

        if (time_reached(next_beat)) {
            next_beat = make_timeout_time_ms(5000);
            /* ioexp rides the heartbeat rather than printing once at boot:
               a console that attaches late -- the normal case -- misses a
               one-shot line entirely, which is exactly what happened on the
               first two runs of this app. */
            DIAG("[btn] down mask 0x%02x (gray=1 yellow=2 green=4 blue=8 "
                 "red=0x10)  ioexp=%s\n",
                 (unsigned)b.down, board_ioexp_ok() ? "ok" : "FAILED");
        }
    }
}
