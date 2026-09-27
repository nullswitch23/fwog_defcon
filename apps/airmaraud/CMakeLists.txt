add_executable(airmaraud_display display/main.c)
target_include_directories(airmaraud_display PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(airmaraud_display fwog_display_bsp)
fwog_display_app(airmaraud_display
    VERSION 010
    DESCRIPTION "2.4 GHz deauth lab via WIFIPROOF. Scan targets, arm to TX one frame.")

add_executable(airmaraud_main main/main.c)
target_include_directories(airmaraud_main PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(airmaraud_main fwog_main_bsp)
fwog_embed_display_image(airmaraud_main)
fwog_embed_c6_image(airmaraud_main)
fwog_main_app(airmaraud_main
    VERSION 010
    DESCRIPTION "Main half: UART1 WIFIPROOF console to Bottlenose C6 for AirMaraud")
