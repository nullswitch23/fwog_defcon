add_executable(ismburst_display display/main.c)
target_include_directories(ismburst_display PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(ismburst_display fwog_display_bsp)
fwog_display_app(ismburst_display
    VERSION 007
    DESCRIPTION "T9 names on FAN/DOOR/SPARE, load BURST*.BIN, ASK+2-FSK capture/decode, hunt, owned replay.")

add_executable(ismburst_main main/main.c decode.c
    ${CMAKE_CURRENT_LIST_DIR}/../diskglass/dg_file.c
    ${CMAKE_CURRENT_LIST_DIR}/../diskglass/dg_fs.c)
target_include_directories(ismburst_main PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../diskglass)
target_link_libraries(ismburst_main fwog_main_bsp fwog_main_fs)
fwog_embed_display_image(ismburst_main)
fwog_main_app(ismburst_main
    VERSION 007
    DESCRIPTION "Main half: CC1101 ASK/2-FSK RX/TX, T9 slot names on /ismburst, BURST archive")
