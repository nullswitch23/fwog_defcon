add_executable(bledeck_display display/main.c)
target_include_directories(bledeck_display PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(bledeck_display fwog_display_bsp)
fwog_display_app(bledeck_display
    VERSION 004
    DESCRIPTION "BLE HID remote: D-pad/media/talk/desk pages; pairing lock; YEL flashes C6.")

add_executable(bledeck_main main/main.c)
target_include_directories(bledeck_main PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(bledeck_main fwog_main_bsp)
fwog_embed_display_image(bledeck_main)
fwog_embed_c6_image(bledeck_main)
fwog_main_app(bledeck_main
    VERSION 004
    DESCRIPTION "Main half: UART1 to Bottlenose HID and C6 ROM flash for BleDeck")
