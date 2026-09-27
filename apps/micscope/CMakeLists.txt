add_executable(micscope_display display/main.c ms_db.c)
target_include_directories(micscope_display PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(micscope_display fwog_display_bsp)
fwog_display_app(micscope_display
    VERSION 004
    DESCRIPTION "PDM spectrogram: Gray-hold quiet cal, bars as dB SPL approx. Clap detector, not a recorder.")

add_executable(micscope_main main/main.c)
target_include_directories(micscope_main PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(micscope_main fwog_main_bsp fwog_main_fs)
fwog_embed_display_image(micscope_main)
fwog_main_app(micscope_main
    VERSION 004
    DESCRIPTION "Main half: FatFs mscope.cal quiet-room persist for MicScope")
