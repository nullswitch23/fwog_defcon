add_executable(pinghalo_display display/main.c)
target_include_directories(pinghalo_display PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(pinghalo_display fwog_display_bsp)
fwog_display_app(pinghalo_display
    VERSION 023
    DESCRIPTION "BLE hunt: freeze locks MACs; T9 labels one MAC; YEL deletes a library slot")

add_executable(pinghalo_main main/main.c ph_track.c)
target_include_directories(pinghalo_main PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(pinghalo_main fwog_main_bsp fwog_main_fs)
fwog_embed_display_image(pinghalo_main)
fwog_embed_c6_image(pinghalo_main)
fwog_main_app(pinghalo_main
    VERSION 023
    DESCRIPTION "Main half: LABEL persist; BLE stop+drain before FatFs; LIBDEL")
