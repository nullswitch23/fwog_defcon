add_executable(tonebox_display
    display/main.c
    tb_c5.c
    tb_seq.c
    tb_play.c
    tb_detect.c
    tb_pin.c
    tb_dials_data.c
    tb_serial_display.c)
target_include_directories(tonebox_display PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(tonebox_display fwog_display_bsp)
fwog_display_app(tonebox_display
    VERSION 004
    DESCRIPTION "Tone museum + bluebox pulse: C5/DTMF sequences, relay H, 2600 wait.")

add_executable(tonebox_main main/main.c main/tb_serial_main.c)
target_include_directories(tonebox_main PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(tonebox_main fwog_main_bsp)
fwog_embed_display_image(tonebox_main)
fwog_main_app(tonebox_main
    VERSION 004
    DESCRIPTION "Main half: display update + 9600 relay UART for ToneBox")
