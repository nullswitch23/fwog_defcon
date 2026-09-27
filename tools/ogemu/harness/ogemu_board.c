#include "ogemu.h"
#include "platform/board.h"
#include "common/diag.h"
#include "input/buttons.h"
#include "power/power_poll.h"
#include "hardware/pio.h"
#include <stdint.h>

struct pio_hw pio0_hw;
struct pio_hw pio1_hw;

static uint8_t s_btn_level;

void fwog_diag_init(void) {}

void board_init_pins(void) {}
void board_init_i2c(void) {}

void board_init(void) {
    (void)*(const volatile unsigned char *)&fwog_power_policy_declared;
    fwog_buttons_init();
    s_btn_level = 0u;
    fwog_buttons_inject(FWOG_BTN_ALL, 0u);
    DIAG("[ogemu] display board_init (host, no RP2040)\n");
}

bool board_ioexp_ok(void) { return true; }

void board_backlight(uint8_t level) {
    DIAG("[ogemu] backlight %u\n", (unsigned)level);
}

void ogemu_btn_set(fwog_btn_id_t id, bool down) {
    if ((unsigned)id >= (unsigned)FWOG_BTN_COUNT) return;
    const uint8_t bit = (uint8_t)FWOG_BTN_BIT(id);
    if (down) s_btn_level |= bit;
    else      s_btn_level = (uint8_t)(s_btn_level & (uint8_t)~bit);
    fwog_buttons_inject(FWOG_BTN_ALL, s_btn_level);
}

void ogemu_btn_clear(void) {
    s_btn_level = 0u;
    fwog_buttons_inject(FWOG_BTN_ALL, 0u);
}
