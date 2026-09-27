add_executable(ttyglass_display display/main.c)
target_include_directories(ttyglass_display PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(ttyglass_display fwog_display_bsp)
fwog_display_app(ttyglass_display
    VERSION 004
    DESCRIPTION "Header UART RX: UART1/GP26/GP27/CTS, freeze, T9 find. 3.3 V.")

add_executable(ttyglass_main main/main.c tg_uart.c)
target_include_directories(ttyglass_main PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(ttyglass_main fwog_main_bsp)
pico_generate_pio_header(ttyglass_main ${CMAKE_CURRENT_LIST_DIR}/tg_uart.pio)
fwog_embed_display_image(ttyglass_main)
fwog_main_app(ttyglass_main
    VERSION 004
    DESCRIPTION "Main half: TtyGlass RX on UART1 or PIO header pads")
