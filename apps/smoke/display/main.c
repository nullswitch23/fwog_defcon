/* Display-CPU hardware smoke test.
 *
 * The first code this BSP has ever run on silicon. It exists to settle four
 * assumptions that plan 2 is built on, before 17 tasks get written against
 * them:
 *
 *   1. the display CPU boots and holds 200 MHz
 *   2. its USB CDC actually reaches the host  (bl_console assumes this, and
 *      it is the debug channel for all of bring-up)
 *   3. clk_peri follows clk_sys without a manual re-source  (the hardware record)
 *   4. PIN_LINK_TX/RX are the right way round  -- the legacy header's net
 *      names are written from the main CPU's point of view, and reading them
 *      as pin roles would leave the link dead in a way that looks like a
 *      software bug
 *
 * ---------------------------------------------------------------------------
 * GETTING BACK OUT
 *
 * This board's display CPU has no reachable BOOTSEL button, so once this
 * image is flashed the ONLY way back to a flashable state is a software path
 * inside it. Resetting the display -- including main pulling GUI_NRESET --
 * just reboots this same image. So there are four independent escapes, in
 * descending order of how much has to be working:
 *
 *   a. hold ANY face button while the display comes out of reset  (checked
 *      before the clock change and before every peripheral -- no 200 MHz, no
 *      USB, no I2C, no link needed for it to work)
 *   b. press ANY face button at any time while it runs
 *   c. send any character to its USB CDC port
 *   d. do nothing at all: it returns to BOOTSEL by itself after
 *      SMOKE_ESCAPE_MS
 *
 * (d) is the one that matters. It needs no working peripheral and no
 * intervention, so the board cannot be stranded by a peripheral that turns
 * out not to work. Seeing RPI-RP2 reappear on its own after ~2 minutes is
 * itself a passing result: it proves the escape path.
 *
 * The timeout is deliberately UNCONDITIONAL -- it is not reset by link
 * traffic. Making "main is still talking to me" extend it is the obvious
 * refinement and it is wrong: a floating or noisy RX pin produces a stream of
 * garbage bytes that reads exactly like a healthy peer, so the one failure
 * where the escape matters most is the one that would defeat it. A fixed
 * deadline cannot be talked out of firing.
 *
 * The cost is that a WORKING link also stops after SMOKE_ESCAPE_MS. That is
 * the right trade here: re-flashing to continue a test is cheap, and this CPU
 * has no button to fall back on. Do not "improve" this file by moving the
 * escape checks later, making the timeout conditional, or removing (d).
 * ------------------------------------------------------------------------ */
#include "fwog_display.h"
#include "hardware/clocks.h"
#include "hardware/uart.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"

/* CUSTOM, not DEFAULT, and deliberately so: on this app ANY button escapes to
   BOOTSEL (the hardware record), which is the whole point of a bare-board bring-up
   tool on a CPU with no BOOTSEL button. A red press therefore reaches
   to_bootsel() long before a 6 s ship-mode hold could complete, so wiring
   fwog_power_poll() here would add a countdown that can never finish. This
   app owns its button policy. */
FWOG_POWER_CUSTOM();

/* Rebuild with -DSMOKE_BAUD=12500000 to test the rate plan 2 actually wants.
 * 115200 first: it separates "are the pins right" from "is the trace good at
 * 12.5 Mbaud", which are different failures with different fixes. */
#ifndef SMOKE_BAUD
#define SMOKE_BAUD 115200u
#endif

#ifndef SMOKE_ESCAPE_MS
#define SMOKE_ESCAPE_MS 120000u
#endif

/* The echo is XORed rather than returned verbatim so a shorted or tied-back
 * pair cannot masquerade as a working link. */
#define SMOKE_XOR 0xA5u

static const uint8_t k_buttons[] = {
    PIN_BTN_GRAY, PIN_BTN_YELLOW, PIN_BTN_GREEN, PIN_BTN_BLUE, PIN_BTN_RED,
};

static void buttons_init(void) {
    for (unsigned i = 0; i < count_of(k_buttons); i++) {
        gpio_init(k_buttons[i]);
        gpio_set_dir(k_buttons[i], GPIO_IN);
        gpio_pull_up(k_buttons[i]);   /* active low */
    }
}

static bool any_button_down(void) {
    for (unsigned i = 0; i < count_of(k_buttons); i++) {
        if (!gpio_get(k_buttons[i])) return true;
    }
    return false;
}

static void to_bootsel(const char *why) {
    DIAG("[smoke_display] -> BOOTSEL (%s)\n", why);
    sleep_ms(100);            /* let the CDC buffer drain before the reset */
    reset_usb_boot(0, 0);     /* does not return */
}

int main(void) {
    /* Escape (a), and it runs FIRST -- before fwog_clocks_init(), not after.
       Whether this board is stable at 200 MHz is one of the things this test
       exists to find out, so the overclock must not sit between power-on and
       the only escape that works when the chip is wedged. Here we are still
       at the SDK's default clock, which the ROM and boot2 already ran at, so
       this check is about as close to guaranteed as anything gets.

       GPIO and sleep_ms work fine at the default clock; nothing here needs
       the target frequency. */
    buttons_init();
    sleep_ms(20);             /* let the pull-ups settle before the first read */
    if (any_button_down()) reset_usb_boot(0, 0);

    fwog_clocks_init(FWOG_SYS_CLK_KHZ);

    board_init_pins();
    board_init_i2c();
    fwog_diag_init();

    /* USB CDC has to enumerate before anything printed is readable. A host
       that is already watching still misses the first line or two. */
    sleep_ms(2000);

    DIAG("\n=== smoke_display ===\n");
    DIAG("clk_sys  = %u Hz  (expect %u)\n",
         (unsigned)clock_get_hz(clk_sys), (unsigned)(FWOG_SYS_CLK_KHZ * 1000u));
    /* Must equal clk_sys, and it does NOT come for free: set_sys_clock_pll()
       defaults to parking clk_peri on PLL_USB at 48 MHz, and only
       PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK in the board header keeps it
       here. Measured at 48 MHz on this board before that was set, which would
       silently cap the link at 3 Mbaud and the LCD at 24 MHz. */
    DIAG("clk_peri = %u Hz  (must match clk_sys; 48 MHz means the board "
         "header's PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK was lost)\n",
         (unsigned)clock_get_hz(clk_peri));

    const uint actual = uart_init(FWOG_LINK_UART, SMOKE_BAUD);
    gpio_set_function(PIN_LINK_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_LINK_RX, GPIO_FUNC_UART);
    uart_set_hw_flow(FWOG_LINK_UART, false, false);
    uart_set_format(FWOG_LINK_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(FWOG_LINK_UART, true);
    DIAG("link: asked %u baud, got %u\n", (unsigned)SMOKE_BAUD, actual);

    DIAG("echoing link bytes ^ 0x%02X\n", SMOKE_XOR);
    DIAG("escape: any button, any USB char, or automatically in %u ms\n",
         (unsigned)SMOKE_ESCAPE_MS);

    const absolute_time_t deadline = make_timeout_time_ms(SMOKE_ESCAPE_MS);
    absolute_time_t next_report = make_timeout_time_ms(1000);
    uint32_t seen = 0;

    while (true) {
        if (any_button_down())    to_bootsel("button");
        if (time_reached(deadline)) to_bootsel("timeout");

        const int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) to_bootsel("usb char");

        while (uart_is_readable(FWOG_LINK_UART)) {
            const uint8_t b = (uint8_t)uart_getc(FWOG_LINK_UART);
            uart_putc_raw(FWOG_LINK_UART, (uint8_t)(b ^ SMOKE_XOR));
            seen++;
        }

        if (time_reached(next_report)) {
            DIAG("alive; link bytes echoed = %u\n", (unsigned)seen);
            next_report = make_timeout_time_ms(1000);
        }
    }
}
