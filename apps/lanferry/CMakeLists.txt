add_executable(lanferry_display display/main.c qrcodegen.c)
target_include_directories(lanferry_display PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(lanferry_display fwog_display_bsp)
fwog_display_app(lanferry_display
    VERSION 006
    DESCRIPTION "Bottlenose AP: 124K RAM mailbox, live pipe, WIFI QR. Yellow flashes C6. Gray hold wipes pass+files. Pass kept in C6 NVS.")

add_executable(lanferry_main main/main.c)
target_include_directories(lanferry_main PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(lanferry_main fwog_main_bsp)
fwog_embed_display_image(lanferry_main)
fwog_embed_c6_image(lanferry_main)
fwog_main_app(lanferry_main
    VERSION 006
    DESCRIPTION "Main half: UART1 to Bottlenose and C6 ROM flash for LanFerry")
