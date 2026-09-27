add_executable(twinfox_display display/main.c)
target_include_directories(twinfox_display PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(twinfox_display fwog_display_bsp)
fwog_display_app(twinfox_display
    VERSION 005
    DESCRIPTION "Dual CC1101 hunt; me/u RSSI graphs; 1 kHz digits; 315/433/915 parks.")

add_executable(twinfox_main main/main.c)
target_include_directories(twinfox_main PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(twinfox_main fwog_main_bsp)
fwog_embed_display_image(twinfox_main)
fwog_main_app(twinfox_main
    VERSION 005
    DESCRIPTION "Main half: TwinFox retune both CC1101s; optional ASK beacon")
