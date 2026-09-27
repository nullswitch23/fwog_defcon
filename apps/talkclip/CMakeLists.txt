add_executable(talkclip_display display/main.c)
target_include_directories(talkclip_display PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(talkclip_display fwog_display_bsp)
fwog_display_app(talkclip_display
    VERSION 006
    DESCRIPTION "VAD notes; Blue-hold hang 0/2/10/60s; Gray-hold T9 names last CLIP; FatFs /talkclip")

add_executable(talkclip_main main/main.c
    ${CMAKE_CURRENT_LIST_DIR}/../diskglass/dg_file.c
    ${CMAKE_CURRENT_LIST_DIR}/../diskglass/dg_fs.c)
target_include_directories(talkclip_main PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../diskglass)
target_link_libraries(talkclip_main fwog_main_bsp fwog_main_fs)
fwog_embed_display_image(talkclip_main)
fwog_main_app(talkclip_main
    VERSION 006
    DESCRIPTION "Main half: FatFs writer + 8.3 rename for TalkClip (/talkclip)")
