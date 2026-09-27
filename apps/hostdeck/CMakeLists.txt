add_executable(hostdeck_display display/main.c)
target_link_libraries(hostdeck_display fwog_display_bsp)
fwog_display_app(hostdeck_display
    VERSION 003
    DESCRIPTION "Five-button macros. T9 LCD labels; chords/--payloads stay host JSON. No USB HID.")

add_executable(hostdeck_main main/main.c)
target_link_libraries(hostdeck_main fwog_main_bsp)
fwog_embed_display_image(hostdeck_main)
fwog_main_app(hostdeck_main
    VERSION 003
    DESCRIPTION "Main half: brings the display up so HostDeck can run")
