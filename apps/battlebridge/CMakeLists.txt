add_executable(battlebridge_display
    display/main.c
    ${CMAKE_CURRENT_LIST_DIR}/../lanferry/qrcodegen.c)
target_include_directories(battlebridge_display PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../lanferry)
target_link_libraries(battlebridge_display fwog_display_bsp)
fwog_display_app(battlebridge_display
    VERSION 001
    DESCRIPTION "Four-browser C6 arena: 2v2, bots, ice. QR Gray-hold mints Wi-Fi pass.")

add_executable(battlebridge_main main/main.c)
target_include_directories(battlebridge_main PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(battlebridge_main fwog_main_bsp)
fwog_embed_display_image(battlebridge_main)
fwog_embed_c6_image(battlebridge_main)
fwog_main_app(battlebridge_main
    VERSION 001
    DESCRIPTION "Main half: low-rate lobby/status control and C6 ROM flash for BattleBridge")
