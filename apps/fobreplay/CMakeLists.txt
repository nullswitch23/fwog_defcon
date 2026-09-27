add_executable(fobreplay_display display/main.c)
target_include_directories(fobreplay_display PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(fobreplay_display fwog_display_bsp)
fwog_display_app(fobreplay_display
    VERSION 006
    DESCRIPTION "Sub-GHz ASK/OOK capture, T9 8.3 /fobreplay names, unused-code queue, PREDICT (CTR+KeeLoq). No jammer.")

add_executable(fobreplay_main main/main.c
    fob_predict.c
    fob_keeloq.c
    ${CMAKE_CURRENT_LIST_DIR}/../diskglass/dg_file.c
    ${CMAKE_CURRENT_LIST_DIR}/../diskglass/dg_fs.c)
target_include_directories(fobreplay_main PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../diskglass)
target_link_libraries(fobreplay_main fwog_main_bsp fwog_main_fs)
fwog_embed_display_image(fobreplay_main)
fwog_main_app(fobreplay_main
    VERSION 006
    DESCRIPTION "Main half: CC1101 capture, T9-named 8.3 FatFs files, queue replay, PREDICT counter/KeeLoq synthesis")

add_custom_target(fobreplay DEPENDS fobreplay_display fobreplay_main)
