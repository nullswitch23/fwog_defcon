set(_pd ${CMAKE_CURRENT_LIST_DIR})
set(_hk ${_pd}/../headerkit)
set(_qg ${_pd}/../qwiicbench)
set(_tg ${_pd}/../ttyglass)
set(_rp ${_pd}/../retia)

add_executable(pindesk_display
    display/main.c
    pd_hk.c
    pd_qg.c
    pd_tg.c
    pd_rp.c
    ${_qg}/qg_nick.c
    ${_rp}/rp_device_view.c
    ${_rp}/rp_mode.c)
target_include_directories(pindesk_display PRIVATE
    ${_pd} ${_hk} ${_qg} ${_tg} ${_rp} ${_pd}/..)
target_link_libraries(pindesk_display fwog_display_bsp)
fwog_display_app(pindesk_display
    VERSION 003
    DESCRIPTION "PinDesk: HeaderKit, QwiicBench, TtyGlass, Retia. GRY up RED down; hold home.")

add_executable(pindesk_main
    main/main.c
    ${_qg}/qg_sense.c
    ${_tg}/tg_uart.c
    ${_rp}/rp_dispatch.c
    ${_rp}/rp_cmdline.c
    ${_rp}/rp_terminal.c
    ${_rp}/rp_pins.c
    ${_rp}/rp_modes.c
    ${_rp}/rp_mode.c)
target_include_directories(pindesk_main PRIVATE
    ${_pd} ${_hk} ${_qg} ${_tg} ${_rp})
target_link_libraries(pindesk_main fwog_main_bsp)
pico_generate_pio_header(pindesk_main ${_tg}/tg_uart.pio)
fwog_embed_display_image(pindesk_main)
fwog_main_app(pindesk_main
    VERSION 003
    DESCRIPTION "Main half: I2C / header UART / Retia CDC mux for PinDesk")

add_custom_target(pindesk DEPENDS pindesk_display pindesk_main)
