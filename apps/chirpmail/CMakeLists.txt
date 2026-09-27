add_executable(chirpmail_display display/main.c)
target_include_directories(chirpmail_display PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(chirpmail_display fwog_display_bsp)
fwog_display_app(chirpmail_display
    VERSION 007
    DESCRIPTION "OG-to-OG text; 24 canned slots; T9 override; ACK PIN; host helper.")

add_executable(chirpmail_main main/main.c cm_store.c)
target_include_directories(chirpmail_main PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(chirpmail_main fwog_main_bsp fwog_main_fs)
fwog_embed_display_image(chirpmail_main)
fwog_main_app(chirpmail_main
    VERSION 007
    DESCRIPTION "Main half: ChirpMail RF plus FatFs /chirpmail/CANNED.TXT")
