add_executable(voltpet_display display/main.c vp_art.c)
target_include_directories(voltpet_display PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(voltpet_display fwog_display_bsp)
fwog_display_app(voltpet_display
    VERSION 005
    DESCRIPTION "Eight meadow foes, dash-in play-fights, random spark until you let go, T9 name, relics.")

add_executable(voltpet_main main/main.c)
target_include_directories(voltpet_main PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(voltpet_main fwog_main_bsp fwog_main_fs)
fwog_embed_display_image(voltpet_main)
fwog_main_app(voltpet_main
    VERSION 005
    DESCRIPTION "Main half: FatFs voltpet.bin (species + name + stats) and CC1101 relic-listen")
