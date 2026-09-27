#include "platform/board.h"
#include "common/clocks.h"
#include "common/diag.h"
#include "io_expander/pcal6416.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"
#include "power/power_poll.h"

/* Backlight PWM target frequency. A backlight boost converter's feedback
 * loop can't follow much above the low tens of kHz, and the divider must be
 * derived from clk_sys rather than hardcoded -- this project never hardcodes
 * a PWM divider. 8-bit duty resolution (wrap 255) is plenty for a backlight
 * and keeps the arithmetic below exact at the board's 200 MHz clock. */
#define FWOG_BACKLIGHT_HZ    10000u
#define FWOG_BACKLIGHT_WRAP  255u

void board_init_pins(void) {
    /* Park the LCD control lines before anything drives the SPI bus. */
    gpio_init(PIN_LCD_CS);
    gpio_set_dir(PIN_LCD_CS, GPIO_OUT);
    gpio_put(PIN_LCD_CS, 1);            /* deselected: active low */
    gpio_init(PIN_LCD_DC);
    gpio_set_dir(PIN_LCD_DC, GPIO_OUT);
    gpio_put(PIN_LCD_DC, 0);

    /* Backlight PWM configured but at zero duty, so nothing flashes at boot.
     * Both the divider and the wrap are derived from clock_get_hz(clk_sys)
     * -- never hardcoded -- and the divider is set explicitly rather than
     * left at the slice's power-on-reset default. Leaving it at reset
     * default was the bug: at 200 MHz with a wrap of 255 that runs the PWM
     * at roughly 781 kHz, far above what a backlight converter can follow,
     * and it would silently retune itself every time FWOG_SYS_CLK_KHZ
     * changed. */
    gpio_set_function(PIN_LCD_BL, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(PIN_LCD_BL);
    float bl_clkdiv = (float)clock_get_hz(clk_sys) /
                       ((float)FWOG_BACKLIGHT_HZ * (FWOG_BACKLIGHT_WRAP + 1u));
    pwm_set_clkdiv(slice, bl_clkdiv);
    pwm_set_wrap(slice, FWOG_BACKLIGHT_WRAP);
    pwm_set_gpio_level(PIN_LCD_BL, 0);
    pwm_set_enabled(slice, true);

    static const uint8_t buttons[] = {
        PIN_BTN_GRAY, PIN_BTN_YELLOW, PIN_BTN_GREEN, PIN_BTN_BLUE, PIN_BTN_RED
    };
    for (unsigned i = 0; i < count_of(buttons); i++) {
        gpio_init(buttons[i]);
        gpio_set_dir(buttons[i], GPIO_IN);
        gpio_pull_up(buttons[i]);       /* active low */
    }

    /* Park the main CPU's boot-mode control: driven output, LOW. This is
     * exactly what the shipped OG firmware does -- FreeWilliDisplay.cpp:890,
     * obBootMainLockout.init(true, false, none) -- so it is observed
     * behavior, not a guess at polarity. Left undriven the pin floats on a
     * weak pull-down and whether that reads as asserted is undocumented. */
    gpio_init(PIN_MAIN_BOOT_OE);
    gpio_set_dir(PIN_MAIN_BOOT_OE, GPIO_OUT);
    gpio_put(PIN_MAIN_BOOT_OE, 0);
}

void board_backlight(uint8_t level) {
    /* FWOG_BACKLIGHT_WRAP is 255, so the level maps to duty directly. */
    pwm_set_gpio_level(PIN_LCD_BL, level);
}

void board_init_i2c(void) {
    i2c_init(FWOG_I2C, FWOG_I2C_HZ);
    gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_I2C_SDA);
    gpio_pull_up(PIN_I2C_SCL);
}

/* Result of board_init()'s expander bring-up; see board_ioexp_ok(). */
static bool s_ioexp_ok;

void board_init(void) {
    /* Forces every display APPLICATION to declare a power policy: this is the
       only reference to the symbol, and only FWOG_POWER_DEFAULT or
       FWOG_POWER_CUSTOM defines it. `volatile` so the read is not optimised
       away, which would take the undefined reference with it.

       The bootloader is unaffected: it deliberately calls board_init_pins()
       rather than board_init(), so this function is garbage-collected out of
       that image. bl_display declares FWOG_POWER_CUSTOM() anyway, so the
       guard does not depend on that staying true.

       See power/power_poll.h for what this does NOT prove. */
    (void)*(const volatile unsigned char *)&fwog_power_policy_declared;

    fwog_clocks_init(FWOG_SYS_CLK_KHZ);
    board_init_pins();
    board_init_i2c();
    fwog_diag_init();

    /* Must follow board_init_i2c() (the expander is on I2C1) and
       fwog_diag_init() (which is stdio_init_all(), so a DIAG before it goes
       nowhere -- and fwog_ioexp_init() reports its own failure that way).
       Deliberately non-fatal: a board with a dead expander should still
       boot far enough to say so.

       This is the ONLY caller. The display serial bootloader must not write
       this part, because reaching it means bringing up the bus that carries
       the charger -- see the note at the top of io_expander/pcal6416.h. */
    s_ioexp_ok = fwog_ioexp_init();

    DIAG("[fwog] display board_init: sys=%u kHz, ioexp=%s\n",
         (unsigned)(clock_get_hz(clk_sys) / 1000u), s_ioexp_ok ? "ok" : "FAILED");
}

bool board_ioexp_ok(void) { return s_ioexp_ok; }
