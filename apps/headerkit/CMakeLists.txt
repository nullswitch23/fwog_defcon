add_executable(headerkit_display display/main.c)
target_include_directories(headerkit_display PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(headerkit_display fwog_display_bsp)
fwog_display_app(headerkit_display
    VERSION 004
    DESCRIPTION "Safe 3.3 V I2C scanner plus physical side-header pinout; unrelated buses stay Hi-Z.")

add_executable(headerkit_main main/main.c)
target_include_directories(headerkit_main PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(headerkit_main fwog_main_bsp)
fwog_embed_display_image(headerkit_main)
fwog_main_app(headerkit_main
    VERSION 004
    DESCRIPTION "Main half: Hi-Z-safe breakout I2C scan for HeaderKit")
