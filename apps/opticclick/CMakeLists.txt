add_executable(opticclick_display display/main.c oc_codes.c)
target_include_directories(opticclick_display PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(opticclick_display fwog_display_bsp)
fwog_display_app(opticclick_display
    VERSION 002
    DESCRIPTION "Multi-PHY IR capture/replay; 24 FatFs slots load PHY on send; T9; NEC library blast.")

add_executable(opticclick_main main/main.c)
target_include_directories(opticclick_main PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(opticclick_main fwog_main_bsp fwog_main_fs)
fwog_embed_display_image(opticclick_main)
fwog_main_app(opticclick_main
    VERSION 002
    DESCRIPTION "Main half: FatFs OPTIC.BIN persist and link sync for OpticClick")
