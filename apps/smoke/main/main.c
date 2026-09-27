/* Main-CPU hardware smoke test. Pairs with apps/smoke/display.
 *
 * Drives the link end of the test: releases the display, sweeps all 256 byte
 * values across UART0, and checks each one comes back XORed. Between them the
 * two apps settle the assumptions plan 2 is built on -- 200 MHz, clk_peri,
 * both USB CDC ports, and the link pin roles -- before any of the bootloader
 * is written.
 *
 * ---------------------------------------------------------------------------
 * GETTING BACK OUT
 *
 * Unlike the display CPU, this one has a reachable hardware BOOTSEL (the red
 * button mechanism), so it is recoverable no matter what this image does.
 * That is why there is NO automatic timeout here: it would cut a soak test
 * short to buy safety that the hardware already provides. The display side
 * has one precisely because it has no such button.
 *
 * The one software escape here is convenience: send any character to this
 * CPU's USB CDC port.
 *
 * Expect the display to drop off the link on its own after ~2 minutes -- its
 * unconditional escape firing is a PASS, not a fault. This app says so rather
 * than just reporting a dead link.
 *
 * NOTE ON THE WATCHDOG: board_init() arms it (the hardware record -- this CPU has no
 * other recovery path, since a hung main cannot be reset without pulling the
 * battery). Its timeout is shorter than several waits in this file, so every
 * wait here kicks. A bare sleep_ms() longer than FWOG_WATCHDOG_MS would reset
 * the board mid-test and look exactly like a crash.
 * ------------------------------------------------------------------------ */
#include "fwog_main.h"
#include "hardware/clocks.h"
#include "hardware/uart.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"

/* Calls board_watchdog_kick() from its loops. See watchdog.h. */
FWOG_WATCHDOG_DEFAULT();

#ifndef SMOKE_BAUD
#define SMOKE_BAUD 115200u
#endif

/* Only used to tell the operator when the display is due to self-escape, so
   a link that goes quiet at that moment reads as expected rather than broken.
   Must match apps/smoke/display's value. */
#ifndef SMOKE_ESCAPE_MS
#define SMOKE_ESCAPE_MS 120000u
#endif

/* Must match apps/smoke/display. */
#define SMOKE_XOR 0xA5u

/* Generous: at 115200 a byte round trip is ~175 us, and the display may be
   busy printing when it arrives. Tight enough to still fail fast if the pins
   are wrong, in which case nothing ever comes back at all. */
#define ECHO_TIMEOUT_US 20000u

static void to_bootsel(const char *why) {
    DIAG("[smoke_main] -> BOOTSEL (%s)\n", why);
    sleep_ms(100);            /* let the CDC buffer drain before the reset */
    reset_usb_boot(0, 0);     /* does not return */
}

/* sleep_ms() that does not trip the watchdog. See the note above. */
static void wait_ms(uint32_t ms) {
    const absolute_time_t end = make_timeout_time_ms(ms);
    while (!time_reached(end)) {
        board_watchdog_kick();
        sleep_ms(5);
    }
    board_watchdog_kick();
}

static void drain_rx(void) {
    while (uart_is_readable(FWOG_LINK_UART)) (void)uart_getc(FWOG_LINK_UART);
}

static bool echo_once(uint8_t tx, uint8_t *rx) {
    uart_putc_raw(FWOG_LINK_UART, tx);
    const absolute_time_t end = make_timeout_time_us(ECHO_TIMEOUT_US);
    while (!uart_is_readable(FWOG_LINK_UART)) {
        if (time_reached(end)) return false;
    }
    *rx = (uint8_t)uart_getc(FWOG_LINK_UART);
    return true;
}

int main(void) {
    board_init();             /* clk, watchdog, pins, diag; display held in reset */

    wait_ms(2000);            /* our own USB CDC has to enumerate first */

    DIAG("\n=== smoke_main ===\n");
    DIAG("clk_sys  = %u Hz  (expect %u)\n",
         (unsigned)clock_get_hz(clk_sys), (unsigned)(FWOG_SYS_CLK_KHZ * 1000u));
    /* See the note in smoke_display: 48 MHz here means the board header's
       PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK was lost, and every baud
       rate below is a quarter of what it should be. */
    DIAG("clk_peri = %u Hz  (must match clk_sys)\n",
         (unsigned)clock_get_hz(clk_peri));
    DIAG("watchdog caused this boot: %s\n",
         board_watchdog_caused_reboot() ? "YES" : "no");

    const uint actual = uart_init(FWOG_LINK_UART, SMOKE_BAUD);
    gpio_set_function(PIN_LINK_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_LINK_RX, GPIO_FUNC_UART);
    uart_set_hw_flow(FWOG_LINK_UART, false, false);
    uart_set_format(FWOG_LINK_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(FWOG_LINK_UART, true);
    DIAG("link: asked %u baud, got %u\n", (unsigned)SMOKE_BAUD, actual);

    /* The link is up before the display is, so nothing it says can be missed. */
    DIAG("releasing the display...\n");
    board_release_display();

    /* It has its own 2 s USB settle before it starts echoing. */
    wait_ms(5000);

    DIAG("escape: any USB char here, or the red-button BOOTSEL mechanism.\n");
    DIAG("the display self-escapes to BOOTSEL ~%u ms after ITS boot; the link\n"
         "going quiet around then is expected, not a failure.\n",
         (unsigned)SMOKE_ESCAPE_MS);

    /* Runs indefinitely on purpose -- see the header note. */
    uint32_t round = 0;
    bool announced_expected_dropout = false;

    while (true) {
        board_watchdog_kick();

        const int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) to_bootsel("usb char");

        unsigned ok = 0, bad = 0, lost = 0;
        uint8_t first_bad_tx = 0, first_bad_rx = 0;

        drain_rx();           /* stale bytes from a previous round would alias */

        for (unsigned v = 0; v < 256u; v++) {
            board_watchdog_kick();
            uint8_t got = 0;
            if (!echo_once((uint8_t)v, &got)) {
                lost++;
            } else if (got == (uint8_t)(v ^ SMOKE_XOR)) {
                ok++;
            } else {
                if (!bad) { first_bad_tx = (uint8_t)v; first_bad_rx = got; }
                bad++;
            }
        }

        round++;
        if (ok == 256u) {
            DIAG("round %u: LINK OK -- 256/256 echoed correctly\n", (unsigned)round);
        } else if (lost == 256u && round > 1u) {
            /* It worked and then stopped: the display's own escape firing. A
               link that NEVER worked is a different diagnosis, below. */
            if (!announced_expected_dropout) {
                DIAG("round %u: link quiet -- the display has almost certainly "
                     "self-escaped to BOOTSEL on schedule. Expected. Re-flash it "
                     "to run again.\n", (unsigned)round);
                announced_expected_dropout = true;
            }
        } else if (lost == 256u) {
            DIAG("round %u: LINK DEAD from the start -- nothing ever came back.\n"
                 "  Check: is the display running? are PIN_LINK_TX/RX the right\n"
                 "  way round? did the display reach its echo loop?\n",
                 (unsigned)round);
        } else {
            DIAG("round %u: ok=%u corrupt=%u lost=%u  first bad: sent 0x%02X "
                 "got 0x%02X (expected 0x%02X)\n",
                 (unsigned)round, ok, bad, lost,
                 first_bad_tx, first_bad_rx, (uint8_t)(first_bad_tx ^ SMOKE_XOR));
        }

        wait_ms(1000);
    }
}
