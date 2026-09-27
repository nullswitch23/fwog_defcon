set(_ol ${CMAKE_CURRENT_LIST_DIR})
set(_lf ${_ol}/../lanferry)
set(_ph ${_ol}/../pinghalo)
set(_bd ${_ol}/../bledeck)
set(_am ${_ol}/../airmaraud)

add_executable(orcalobby_display
    display/main.c
    ol_lf.c
    ol_ph.c
    ol_bd.c
    ol_am.c
    ${_lf}/qrcodegen.c)
target_include_directories(orcalobby_display PRIVATE
    ${_ol} ${_lf} ${_ph} ${_bd} ${_am} ${_ol}/..)
target_link_libraries(orcalobby_display fwog_display_bsp)
fwog_display_app(orcalobby_display
    VERSION 004
    DESCRIPTION "OrcaLobby: LanFerry PingHalo BleDeck AirMaraud. No BattleBridge. One C6 mode. Hold BLU C6.")

add_executable(orcalobby_main
    main/main.c
    ol_lf_main.c
    ol_ph_main.c
    ol_bd_main.c
    ol_am_main.c
    ${_ph}/ph_track.c)
target_include_directories(orcalobby_main PRIVATE
    ${_ol} ${_lf} ${_ph} ${_bd} ${_am})
target_link_libraries(orcalobby_main fwog_main_bsp fwog_main_fs)
fwog_embed_display_image(orcalobby_main)
fwog_embed_c6_image(orcalobby_main)
fwog_main_app(orcalobby_main
    VERSION 004
    DESCRIPTION "Main half: one C6 mode at a time for OrcaLobby")

add_custom_target(orcalobby DEPENDS orcalobby_display orcalobby_main)
