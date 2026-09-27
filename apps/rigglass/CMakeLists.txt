add_executable(rigglass_display display/main.c)
target_include_directories(rigglass_display PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(rigglass_display fwog_display_bsp)
fwog_display_app(rigglass_display
    VERSION 002
    DESCRIPTION "Desk gauges: host CPU/RAM/net/GPU/temp now+2m+10m. Helper: tools/rigglass/rigglass.py")

add_executable(rigglass_main main/main.c)
target_include_directories(rigglass_main PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(rigglass_main fwog_main_bsp)
fwog_embed_display_image(rigglass_main)
fwog_main_app(rigglass_main
    VERSION 002
    DESCRIPTION "Main half: CDC telemetry parser for RigGlass")
