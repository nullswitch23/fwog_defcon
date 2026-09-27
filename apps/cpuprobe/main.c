/*
 * ============================================================================
 *  cpuprobe - "which CPU am I running on?"
 * ============================================================================
 *
 *  The FreeWili OG carries two RP2040s: MAIN and DISPLAY. In BOOTSEL mode both
 *  present an identical RPI-RP2 mass-storage volume, so when two are mounted a
 *  host cannot tell them apart. This image answers the question: flash it to
 *  one unknown volume, read the USB CDC port, and it prints "main" or
 *  "display" once per second. The other drive is then known by elimination.
 *
 *  How it decides: the CC1101 sub-GHz radio is wired to MAIN only. We read the
 *  CC1101 VERSION register over spi0. 0x14 => a CC1101 answered => MAIN.
 *  Anything else => DISPLAY.
 *
 *  Consumed by fwOGappexplorer (src/flash/fwCpuProbe.cpp), which embeds the
 *  built .uf2 and parses this output. The two words are a PROTOCOL: it accepts
 *  exactly "main" and "display" and nothing else. Do not add a banner, a
 *  prefix, or a DIAG() line -- and note that DIAG() would prefix its own tag,
 *  which is why this file uses puts() where the rest of the tree would not.
 *
 * ----------------------------------------------------------------------------
 *  THIS IS NOT A DISPLAY APP AND IT IS NOT A MAIN APP
 * ----------------------------------------------------------------------------
 *
 *  It is the one binary in this tree that runs on a CPU nobody has identified
 *  yet, so it may make no CPU-specific assumption at all. It links
 *  `fwog_common` ONLY -- never `fwog_main_bsp` or `fwog_display_bsp`, and it
 *  calls neither `board_init()` nor `fwog_main_app()`/`fwog_display_app()`.
 *
 *  Linking a per-CPU BSP here would bring up the inter-CPU link, the watchdog,
 *  the display, the FPGA or the LCD on a processor that may be the other one.
 *  Driving main-CPU pins on the DISPLAY CPU is precisely the accident this
 *  whole tool exists to prevent. See AGENTS.md.
 *
 *  What it DOES take from the BSP is the board configuration every FreeWili OG
 *  binary needs and that is true of both CPUs alike:
 *
 *    - bsp/boards/freewili_og.h, via PICO_BOARD=freewili_og: 16 MB QSPI flash,
 *      PICO_FLASH_SPI_CLKDIV 4, boot2_w25q080, clk_peri following clk_sys.
 *    - fwog_clocks_init(), below: vreg then settle then 200 MHz, the same
 *      operating point every other binary in this tree boots at.
 *
 * ----------------------------------------------------------------------------
 *  !!!  SAFETY RULE - DO NOT BREAK THIS  !!!
 * ----------------------------------------------------------------------------
 *
 *  THIS FIRMWARE MUST NEVER CONFIGURE, DRIVE OR OTHERWISE TOUCH GPIO 29.
 *
 *  On the DISPLAY CPU, GPIO 29 is MIC_SIG - the *output* of a PDM microphone.
 *  A MAIN application image drives GPIO 29 (it is FPGA_RESET on MAIN) and,
 *  running on DISPLAY, fights the microphone's driver. That can physically
 *  damage the board. GPIO 29 being untouched is the entire reason this image
 *  is safe to flash to an unidentified CPU, which is the entire point of it
 *  existing. MAIN has a BOOTSEL button; DISPLAY has none.
 *
 *  The complete list of GPIOs this firmware may touch is:
 *
 *      GPIO 4   RADIO_SPI_MISO   (spi0 RX)
 *      GPIO 6   RADIO_SPI_SCLK   (spi0 SCK)
 *      GPIO 7   RADIO_SPI_MOSI   (spi0 TX)
 *      GPIO 18  RADIO_SPI_CS1    (plain SIO output, idle HIGH)
 *
 *  ...plus the USB pins, which are not GPIOs on RP2040.
 *
 *  Therefore:
 *    - Do NOT add a status LED. Not on GPIO 25, not anywhere.
 *    - Do NOT add a backlight, a display, a buzzer, a button read, or "just a
 *      quick" anything.
 *    - Do NOT enable UART stdio. UART0 is the inter-CPU link on BOTH CPUs;
 *      the top-level CMakeLists.txt forces PICO_STDIO_UART 0 and this app
 *      calls fwog_configure_stdio() as well.
 *    - Do NOT define PICO_STDIO_USB_RESET_BOOTSEL_ACTIVITY_LED. It would make
 *      the SDK's BOOTSEL path drive a pin.
 *    - Do NOT link fwog_main_bsp or fwog_display_bsp. See above.
 *
 *  Every extra pin is new risk on the CPU that has no recovery path. If you
 *  think you need one, you are wrong; go read this comment again.
 *
 * ----------------------------------------------------------------------------
 *  Provenance
 * ----------------------------------------------------------------------------
 *  The probe sequence is lifted from the shipping FreeWili firmware, where
 *  the identical two-byte exchange appears in both
 *  freewilimain/FreeWilliMain.cpp (CC1101_Comm_Check) and
 *  freewilidisplay/FreeWilliDisplay.cpp (CC1101_Comm_Check, called from
 *  powerup_verify at every DISPLAY power-up). Running it on DISPLAY is
 *  therefore already validated by shipping firmware.
 * ============================================================================
 */

#include <stdio.h>

#include "common/clocks.h"

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

/* ---------------------------------------------------------------------------
 * Pins. Names are MAIN's (bsp/main_cpu/platform/board.h agrees:
 * PIN_CC1101_MISO 4, PIN_CC1101_SCLK 6, PIN_CC1101_MOSI 7, PIN_CC1101_CS1 18,
 * and FWOG_RADIO_SPI is spi0). They are spelled
 * out here as literals rather than included from that header, because
 * including a per-CPU board.h is exactly the CPU-specific assumption this
 * image must not make -- and because these four numbers are the safety claim.
 * The same GPIO numbers on the DISPLAY CPU are the I2S speaker pins, which
 * DISPLAY's own shipping firmware already drives for this very check.
 * ------------------------------------------------------------------------ */
#define RADIO_SPI_MISO 4u /* spi0 RX  */
#define RADIO_SPI_SCLK 6u /* spi0 SCK */
#define RADIO_SPI_MOSI 7u /* spi0 TX  */
#define RADIO_SPI_CS1 18u /* plain output, active low, idle high */

#define RADIO_SPI_HW spi0
#define RADIO_SPI_BAUD 1000000u /* 1 MHz, as the shipping firmware uses */

/*
 * CC1101 status-register read. Status registers (0x30..0x3D) can only be read
 * with the burst bit set, hence 0xC0 (READ | BURST) rather than 0x80.
 * 0x31 is VERSION. (The shipping firmware names the local PARTNUM_REG; 0x30 is
 * PARTNUM and 0x31 is VERSION, so the name is a misnomer - the address and the
 * expected value 0x14 are both VERSION's.)
 */
#define CC1101_VERSION_ADDR 0x31u
#define CC1101_READ_BURST 0xC0u
#define CC1101_VERSION_EXPECTED 0x14u

/* Bounded spin instead of the shipping firmware's unbounded
 * `while (cs.get());`. If CS could never read back low - shorted, or held by
 * something else - an unbounded wait would hang this image with no output at
 * all, turning a recovery tool into a brick. We wait, then continue; a wrong
 * answer is recoverable, silence is not. */
#define CS_SETTLE_TIMEOUT_US 100u

static void radio_bus_init(void) {
    /* Chip select: plain SIO output, driven HIGH (inactive) before anything
     * else so we never present an active CS to a device we have not clocked. */
    gpio_init(RADIO_SPI_CS1);
    gpio_put(RADIO_SPI_CS1, true);
    gpio_set_dir(RADIO_SPI_CS1, GPIO_OUT);
    gpio_put(RADIO_SPI_CS1, true);

    /* spi0 at 1 MHz, mode 0 (CPOL=0, CPHA=0), 8 bit, MSB first.
     * spi_init() resets the block and applies 8-bit/mode-0/MSB itself; the
     * explicit spi_set_format() afterwards states the intent and is what makes
     * the mode a property of this file rather than of an SDK default.
     *
     * spi_init() derives its prescaler from clock_get_hz(clk_peri), so it must
     * run AFTER fwog_clocks_init() -- on this board clk_peri follows clk_sys
     * (PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK in the board header), so it
     * moves from 125 MHz to 200 MHz underneath us otherwise. 1 MHz is
     * reachable from either, but deriving after the change is the rule the
     * whole tree follows. */
    spi_init(RADIO_SPI_HW, RADIO_SPI_BAUD);
    spi_set_format(RADIO_SPI_HW, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(RADIO_SPI_MISO, GPIO_FUNC_SPI);
    gpio_set_function(RADIO_SPI_SCLK, GPIO_FUNC_SPI);
    gpio_set_function(RADIO_SPI_MOSI, GPIO_FUNC_SPI);
}

/*
 * Two single-byte transfers, exactly as the shipping firmware does it:
 *   1. clock out the address byte; the byte read back is the CC1101 status
 *      byte and is discarded.
 *   2. clock out 0x00; the byte read back is the VERSION value.
 * CS is low for both and returns high afterwards.
 */
static bool cc1101_comm_check(void) {
    uint8_t tx = CC1101_VERSION_ADDR | CC1101_READ_BURST; /* 0xF1 */
    uint8_t discard = 0;
    uint8_t version = 0;

    gpio_put(RADIO_SPI_CS1, false);

    /* Wait for CS to actually read back low (bounded - see above). */
    absolute_time_t deadline = make_timeout_time_us(CS_SETTLE_TIMEOUT_US);
    while (gpio_get(RADIO_SPI_CS1)) {
        if (time_reached(deadline)) {
            break;
        }
    }

    spi_write_read_blocking(RADIO_SPI_HW, &tx, &discard, 1);
    tx = 0x00;
    spi_write_read_blocking(RADIO_SPI_HW, &tx, &version, 1);

    gpio_put(RADIO_SPI_CS1, true);

    return version == CC1101_VERSION_EXPECTED;
}

int main(void) {
    /* The board's operating point: vreg 1.15 V, settle, then clk_sys 200 MHz,
     * with clk_peri following clk_sys via the board header. This is the ONE
     * thing this app takes from the BSP beyond the board header itself, and it
     * is CPU-agnostic (bsp/common/clocks.c, part of fwog_common). It touches
     * VREG, PLLs and CLOCKS -- no GPIO.
     *
     * First, before stdio and before anything derives a divider from
     * clock_get_hz(), exactly as board_init() orders it on both CPUs. */
    fwog_clocks_init(FWOG_SYS_CLK_KHZ);

    /* USB CDC only. UART stdio is off in three places: PICO_STDIO_UART 0 in
     * the top-level CMakeLists.txt, fwog_configure_stdio() in this app's
     * CMakeLists.txt, and no PICO_DEFAULT_UART in the board header. UART0 is
     * the inter-CPU link on BOTH CPUs and must never be driven here. */
    stdio_init_all();

    radio_bus_init();

    /*
     * Re-probe every second rather than caching one result from boot. A single
     * sample taken microseconds after power-up is the least trustworthy sample
     * we could take; repeating it lets a human (or the host app) see whether
     * the answer is stable. The cost is one 2-byte SPI transaction per second.
     *
     * There is deliberately NO self-reset timer here. This loop runs FOREVER.
     * An earlier draft carried a 12-second `reset_usb_boot()` deadline as
     * bring-up scaffolding; it is removed, because an image that walks itself
     * back into BOOTSEL is indistinguishable on the wire from the boot failure
     * this rebuild exists to fix, and the host reads this port for up to 25 s.
     * The way back is the 1200-baud touch, which is compiled in and verified.
     */
    for (;;) {
        bool is_main = cc1101_comm_check();

        /* One answer per line, newline-terminated and flushed, so a host
         * reading line-wise never blocks on a partial line. */
        puts(is_main ? "main" : "display");
        fflush(stdout);

        sleep_ms(1000);
    }
}
