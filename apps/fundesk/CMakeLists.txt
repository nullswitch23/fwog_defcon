set(_fd ${CMAKE_CURRENT_LIST_DIR})
set(_it ${_fd}/../inertialtrail)
set(_trf ${_fd}/../inertialtrailrf)
set(_vp ${_fd}/../voltpet)
set(_pf ${_fd}/../pitchfork)
set(_dg ${_fd}/../diskglass)

add_executable(fundesk_display
    display/main.c
    fd_it.c
    fd_trf.c
    fd_vp.c
    fd_pf.c
    ${_it}/pedometer.c
    ${_vp}/vp_art.c
    ${_pf}/pf_note.c)
target_include_directories(fundesk_display PRIVATE
    ${_fd} ${_it} ${_trf} ${_vp} ${_pf} ${_fd}/..)
target_link_libraries(fundesk_display fwog_display_bsp)
fwog_display_app(fundesk_display
    VERSION 004
    DESCRIPTION "FunDesk: InertialTrail, TrailRF, VoltPet, PitchFork. One accel/radio/PDM live.")

add_executable(fundesk_main
    main/main.c
    ${_dg}/dg_file.c
    ${_dg}/dg_fs.c)
target_include_directories(fundesk_main PRIVATE
    ${_fd} ${_it} ${_trf} ${_vp} ${_dg})
target_link_libraries(fundesk_main fwog_main_bsp fwog_main_fs)
fwog_embed_display_image(fundesk_main)
fwog_main_app(fundesk_main
    VERSION 004
    DESCRIPTION "Main half: FatFs /trail /trailrf voltpet.bin; CC1101 idle mux")

add_custom_target(fundesk DEPENDS fundesk_display fundesk_main)
