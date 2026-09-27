set(_ed ${CMAKE_CURRENT_LIST_DIR})
set(_bs ${_ed}/../bandscope)
set(_tf ${_ed}/../twinfox)
set(_te ${_ed}/../tireear)
set(_ib ${_ed}/../ismburst)
set(_fob ${_ed}/../fobreplay)
set(_cm ${_ed}/../chirpmail)
set(_oc ${_ed}/../opticclick)
set(_dg ${_ed}/../diskglass)

add_executable(emsdesk_display
    display/main.c
    ed_bs.c
    ed_tf.c
    ed_te.c
    ed_ib.c
    ed_fob.c
    ed_cm.c
    ed_oc.c
    ${_te}/te_tpms.c
    ${_oc}/oc_codes.c)
target_include_directories(emsdesk_display PRIVATE
    ${_ed} ${_ed}/.. ${_bs} ${_tf} ${_te} ${_ib} ${_fob} ${_cm} ${_oc})
target_link_libraries(emsdesk_display fwog_display_bsp)
fwog_display_app(emsdesk_display
    VERSION 004
    DESCRIPTION "EmsDesk: BandScope TwinFox TireEar ISMburst FobReplay ChirpMail OpticClick. One radio live.")

add_executable(emsdesk_main
    main/main.c
    ed_cap.c
    ed_m_bs.c
    ed_m_tf.c
    ed_m_te.c
    ed_m_ib.c
    ed_m_fob.c
    ed_m_cm.c
    ed_m_oc.c
    ${_te}/te_tpms.c
    ${_ib}/decode.c
    ${_fob}/fob_predict.c
    ${_fob}/fob_keeloq.c
    ${_cm}/cm_store.c
    ${_dg}/dg_file.c
    ${_dg}/dg_fs.c)
target_include_directories(emsdesk_main PRIVATE
    ${_ed} ${_bs} ${_tf} ${_te} ${_ib} ${_fob} ${_cm} ${_oc} ${_dg})
target_link_libraries(emsdesk_main fwog_main_bsp fwog_main_fs)
fwog_embed_display_image(emsdesk_main)
fwog_main_app(emsdesk_main
    VERSION 004
    DESCRIPTION "Main half: CC1101 mux for EmsDesk tiles; FatFs for ISM/Fob/Chirp/Optic")

add_custom_target(emsdesk DEPENDS emsdesk_display emsdesk_main)
