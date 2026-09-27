add_executable(glassbak_display display/main.c)
target_include_directories(glassbak_display PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(glassbak_display fwog_display_bsp)
fwog_display_app(glassbak_display
    VERSION 001
    DESCRIPTION "FatFs USB dump/restore. Green dump; host GUI Push writes files back.")

add_executable(glassbak_main main/main.c gb_fs.c)
target_include_directories(glassbak_main PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(glassbak_main fwog_main_bsp fwog_main_fs)
fwog_embed_display_image(glassbak_main)
fwog_main_app(glassbak_main
    VERSION 001
    DESCRIPTION "Main half: FatFs dump on CDC and PUT restore to original paths")
