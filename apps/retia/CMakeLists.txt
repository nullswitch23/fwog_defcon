set(_retia_dir ${CMAKE_CURRENT_LIST_DIR})

add_executable(retia_display
    display/main.c
    rp_device_view.c
    rp_mode.c)
target_include_directories(retia_display PRIVATE ${_retia_dir})
target_link_libraries(retia_display fwog_display_bsp)
fwog_display_app(retia_display
    VERSION 005
    DESCRIPTION "Retia LCD: mode, pinout and status for the breakout terminal")

add_executable(retia_main
    main/main.c
    rp_dispatch.c
    rp_cmdline.c
    rp_terminal.c
    rp_pins.c
    rp_modes.c
    rp_mode.c)
target_include_directories(retia_main PRIVATE ${_retia_dir})
target_link_libraries(retia_main fwog_main_bsp)
fwog_embed_display_image(retia_main)
fwog_main_app(retia_main
    VERSION 005
    DESCRIPTION "Retia: USB CDC HiZ/GPIO/I2C/UART/SPI terminal on the 3.3 V breakout")

add_custom_target(retia DEPENDS retia_display retia_main)
