add_executable(bandscope_display display/main.c)
target_include_directories(bandscope_display PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(bandscope_display fwog_display_bsp)
fwog_display_app(bandscope_display
    VERSION 004
    DESCRIPTION "Sub-GHz RSSI sweep. Freeze, YEL/BLU jump peaks, GRN hold marks. History strip; persist hold/top-5. Receive-only.")

add_executable(bandscope_main main/main.c)
target_include_directories(bandscope_main PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(bandscope_main fwog_main_bsp)
fwog_embed_display_image(bandscope_main)
fwog_main_app(bandscope_main
    VERSION 004
    DESCRIPTION "Main half: CC1101 RSSI sweep and peak-hunt for BandScope")
