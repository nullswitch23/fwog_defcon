add_executable(inertialtrailrf_display display/main.c
    ${CMAKE_CURRENT_LIST_DIR}/../inertialtrail/pedometer.c)
target_include_directories(inertialtrailrf_display PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../inertialtrail)
target_link_libraries(inertialtrailrf_display fwog_display_bsp)
fwog_display_app(inertialtrailrf_display
    VERSION 009
    DESCRIPTION "Two parked CC1101 RSSI strips (Gray top / Blue bottom), T9 /trailrf title, step clock.")

add_executable(inertialtrailrf_main main/main.c
    ${CMAKE_CURRENT_LIST_DIR}/../diskglass/dg_file.c
    ${CMAKE_CURRENT_LIST_DIR}/../diskglass/dg_fs.c)
target_include_directories(inertialtrailrf_main PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../diskglass)
target_link_libraries(inertialtrailrf_main fwog_main_bsp fwog_main_fs)
fwog_embed_display_image(inertialtrailrf_main)
fwog_main_app(inertialtrailrf_main
    VERSION 009
    DESCRIPTION "Main half: dual CC1101 parked RSSI, FatFs /trailrf, optional T9 walk title")
