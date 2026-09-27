add_executable(tireear_display display/main.c te_tpms.c)
target_include_directories(tireear_display PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(tireear_display fwog_display_bsp)
fwog_display_app(tireear_display
    VERSION 004
    DESCRIPTION "TPMS 315/433 ASK-FSK, OEM profiles, parking-lot scan and review.")

add_executable(tireear_main main/main.c te_tpms.c)
target_include_directories(tireear_main PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(tireear_main fwog_main_bsp)
fwog_embed_display_image(tireear_main)
fwog_main_app(tireear_main
    VERSION 004
    DESCRIPTION "Main half: CC1101 bit capture, OEM TPMS decode, scan log (RAM)")
