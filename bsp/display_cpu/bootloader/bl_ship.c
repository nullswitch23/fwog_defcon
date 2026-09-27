#include "bootloader/bl_ship.h"
#include "common/diag.h"
#include "common/i2c_bus.h"
#include "platform/board.h"

#define BQ25896_REG_CTRL1   0x09u   /* REG09 in the datasheet */
#define BQ25896_BATFET_DIS  (1u << 5)

bool bl_ship_mode(void) {
    board_init_i2c();

    uint8_t v;
    if (!fwog_i2c_read_regs(I2C_ADDR_CHARGER, BQ25896_REG_CTRL1, &v, 1u)) {
        DIAG("[bl] ship: charger did not answer at 0x%02x\n", I2C_ADDR_CHARGER);
        return false;
    }
    if (!fwog_i2c_write_reg(I2C_ADDR_CHARGER, BQ25896_REG_CTRL1,
                            (uint8_t)(v | BQ25896_BATFET_DIS))) {
        DIAG("[bl] ship: BATFET_DIS write failed\n");
        return false;
    }
    DIAG("[bl] ship: BATFET_DIS set (REG09 %02x -> %02x)\n",
         (unsigned)v, (unsigned)(v | BQ25896_BATFET_DIS));
    return true;
}
