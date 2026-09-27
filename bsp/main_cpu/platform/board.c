#include "platform/board.h"
#include "watchdog/watchdog.h"
#include "common/clocks.h"
#include "common/diag.h"
#include "fpga/fpga_clk.h"
#include "fpga/ice40.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

void board_init_pins(void) {
    /* Park BOTH radio chip selects HIGH and hold the FPGA in reset before
     * anything can touch the shared SPI bus. */
    static const uint8_t cs_pins[] = { PIN_CC1101_CS0, PIN_CC1101_CS1 };
    for (unsigned i = 0; i < count_of(cs_pins); i++) {
        gpio_init(cs_pins[i]);
        gpio_set_dir(cs_pins[i], GPIO_OUT);
        gpio_put(cs_pins[i], 1);
    }
    gpio_init(PIN_FPGA_RESET);
    gpio_set_dir(PIN_FPGA_RESET, GPIO_OUT);
    gpio_put(PIN_FPGA_RESET, 0);        /* held in reset */

    /* CDONE is the iCE40's open-drain configuration-done output and needs a
     * pull-up -- this board has no external one (see the CRESET_B note in
     * fpga/ice40.h, which is the same net topology). The RP2040's
     * PADS_BANK0 reset value is IE=1, PUE=0, PDE=1: a ~60 kOhm pull-DOWN.
     * Left at that default, a configured FPGA reads back CDONE=0 through
     * the pull-down, ice40.c's loader skips the mandatory >=49 release
     * clocks, and board_init() reports fpga=FAILED for a part that actually
     * configured -- so this must be set before fwog_ice40_load_default()
     * ever samples the pin. */
    gpio_init(PIN_FPGA_DONE);
    gpio_set_dir(PIN_FPGA_DONE, GPIO_IN);
    gpio_pull_up(PIN_FPGA_DONE);

    /* Hold the display CPU in reset. The updater releases it when the link
     * is ready, which is what guarantees main catches the bootloader's
     * announce window instead of racing it. */
    gpio_init(PIN_GUI_NRESET);
    gpio_set_dir(PIN_GUI_NRESET, GPIO_OUT);
    gpio_put(PIN_GUI_NRESET, 0);

    gpio_init(PIN_LED);
    gpio_set_dir(PIN_LED, GPIO_OUT);
    gpio_put(PIN_LED, 0);
}

void board_init_i2c(void) {
    /* I2C0 on the user breakout header (GPIO 16/17). Not a system bus on
     * this CPU -- see the PIN_IO_I2C_SDA/SCL comment in board.h -- so this
     * is never called from board_init(); the application calls it. */
    i2c_init(FWOG_I2C, FWOG_I2C_HZ);
    gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_I2C_SDA);
    gpio_pull_up(PIN_I2C_SCL);
}

void board_init(void) {
    fwog_clocks_init(FWOG_SYS_CLK_KHZ);

    /* The watchdog goes on before anything else can hang: it is the only
     * recovery path this CPU has. */
    board_watchdog_init();

    /* Forces every main-CPU app to declare a watchdog policy: this is the
     * only reference to the symbol, so an app defining neither macro fails
     * to link -- provided this object is actually pulled out of the
     * fwog_main_bsp archive, which is guaranteed only because every main app
     * calls board_init() (or board_init_pins(), same translation unit). An
     * app calling neither would link clean with no declaration.
     * Unconditional on purpose -- see watchdog.h for why, and for what this
     * catches and what it does not. */
    (void)*(const volatile unsigned char *)&fwog_watchdog_policy_declared;

    board_init_pins();

    fwog_diag_init();
    DIAG("[fwog] main board_init: sys=%u kHz, wdt_reboot=%d\n",
         (unsigned)(clock_get_hz(clk_sys) / 1000u),
         (int)board_watchdog_caused_reboot());

    /* The gateware needs a clock before it can respond to anything. */
    (void)fwog_fpga_clk_start();

    /* ~200 ms at 5 MHz for 104 KB -- nowhere near the 8300 ms window unless
       something is wrong. The pause/resume pair widens nothing now that the
       base window is already the hardware maximum (see watchdog.h); it is
       kept because it marks this as one of the slow paths and because a
       lower base window would need it back.
       A failed configuration is not fatal: a board with a dead FPGA should
       still boot far enough to say so, the same rule board_ioexp_ok()
       follows on the display side. */
    board_watchdog_pause();
    bool fpga_ok = fwog_ice40_load_default();
    board_watchdog_resume();
    DIAG("[board] fpga=%s\n", fpga_ok ? "ok" : "FAILED");
}

void board_release_display(void) {
    gpio_put(PIN_GUI_NRESET, 1);
}
