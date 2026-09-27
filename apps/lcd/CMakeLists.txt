# Display-only: there is no lcd_main. A folder with no main/ is how this tree
# says an app has no companion.
add_executable(lcd_display display/main.c)
target_link_libraries(lcd_display fwog_display_bsp fwog_display_bl)

# Links at 0x10021000 and adds the stdio/extra-outputs wiring every display
# app needs. Without this the app would overwrite the bootloader when
# flashed, with no compile-time symptom (the hardware record).
fwog_display_app(lcd_display
    VERSION 001
    DESCRIPTION "ST7789 panel bring-up: full-screen fills, then text, over the DMA path")
