#include "test_util.h"
#include "platform/board.h"

int main(void) {
    /* LCD. The legacy header's DISPLAY_CS is really DC; DISPLAY_LCD_CS_
       ACTIVE_LOW is the real chip select. See rmpLib/st7789.hpp:36-37. */
    ASSERT_EQ(PIN_LCD_SCK, 10);
    ASSERT_EQ(PIN_LCD_MOSI, 11);
    ASSERT_EQ(PIN_LCD_DC, 12);
    ASSERT_EQ(PIN_LCD_CS, 13);
    ASSERT_EQ(PIN_LCD_BL, 25);
    ASSERT_EQ(PIN_LCD_TE, 21);

    /* Buttons by color, active low. */
    ASSERT_EQ(PIN_BTN_GRAY, 14);
    ASSERT_EQ(PIN_BTN_YELLOW, 15);
    ASSERT_EQ(PIN_BTN_GREEN, 22);
    ASSERT_EQ(PIN_BTN_BLUE, 23);
    ASSERT_EQ(PIN_BTN_RED, 24);

    ASSERT_EQ(PIN_LED_DATA, 7);
    ASSERT_EQ(FWOG_LED_COUNT, 7);   /* OG has 7, not FW2's 16 */

    ASSERT_EQ(PIN_IR_TX, 9);
    ASSERT_EQ(PIN_IR_RX, 16);
    ASSERT_EQ(PIN_MIC_CLK, 17);
    ASSERT_EQ(PIN_MIC_DATA, 29);
    ASSERT_EQ(PIN_I2S_DIN, 4);
    ASSERT_EQ(PIN_I2S_LRCLK, 5);
    ASSERT_EQ(PIN_I2S_BCLK, 6);
    ASSERT_EQ(PIN_I2C_SDA, 26);
    ASSERT_EQ(PIN_I2C_SCL, 27);
    ASSERT_EQ(I2C_ADDR_CHARGER, 0x6B);

    /* Link to main. Roles are identical on both CPUs because the RP2040
       pad mux fixes them; the traces cross, not the pin numbers. */
    ASSERT_EQ(PIN_LINK_TX, 0);
    ASSERT_EQ(PIN_LINK_RX, 1);
    ASSERT_EQ(PIN_LINK_CTS, 2);
    ASSERT_EQ(PIN_LINK_RTS, 3);

    ASSERT_EQ(PIN_MAIN_BOOT_OE, 8);
    TEST_RETURN();
}
