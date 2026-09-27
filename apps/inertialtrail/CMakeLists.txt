add_executable(inertialtrail_display display/main.c pedometer.c)
target_include_directories(inertialtrail_display PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(inertialtrail_display fwog_display_bsp)
fwog_display_app(inertialtrail_display
    VERSION 008
    DESCRIPTION "Peak-valley pedometer, origin-fixed stride map, optional T9 /trail title. Not indoor GPS.")

add_executable(inertialtrail_main main/main.c
    ${CMAKE_CURRENT_LIST_DIR}/../diskglass/dg_file.c
    ${CMAKE_CURRENT_LIST_DIR}/../diskglass/dg_fs.c)
target_include_directories(inertialtrail_main PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../diskglass)
target_link_libraries(inertialtrail_main fwog_main_bsp fwog_main_fs)
fwog_embed_display_image(inertialtrail_main)
fwog_main_app(inertialtrail_main
    VERSION 008
    DESCRIPTION "Main half: FatFs /trail CSV writer; optional T9 walk title")
