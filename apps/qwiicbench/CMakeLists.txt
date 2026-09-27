add_executable(qwiicbench_display display/main.c qg_nick.c)
target_include_directories(qwiicbench_display PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(qwiicbench_display fwog_display_bsp)
fwog_display_app(qwiicbench_display
    VERSION 002
    DESCRIPTION "I2C sensor bench: scan, BME/BH names, T9 nick unknowns (RAM).")

add_executable(qwiicbench_main main/main.c qg_sense.c)
target_include_directories(qwiicbench_main PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(qwiicbench_main fwog_main_bsp)
fwog_embed_display_image(qwiicbench_main)
fwog_main_app(qwiicbench_main
    VERSION 002
    DESCRIPTION "Main half: Hi-Z other buses; breakout I2C scan; BME280/BH1750")
