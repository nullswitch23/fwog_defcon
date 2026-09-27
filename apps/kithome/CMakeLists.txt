set(_kh ${CMAKE_CURRENT_LIST_DIR})
set(_dg ${_kh}/../diskglass)
set(_lf ${_kh}/../lanferry)
set(_tc ${_kh}/../talkclip)
set(_tb ${_kh}/../tonebox)
set(_rg ${_kh}/../rigglass)
set(_bs ${_kh}/../bandscope)
set(_gb ${_kh}/../glassbak)
set(DG_WASM3 ${_dg}/third_party/wasm3)

add_executable(kithome_display
    display/main.c
    kh_dg.c
    kh_tc.c
    kh_tb.c
    kh_rg.c
    ${_dg}/dg_file.c
    ${_dg}/dg_xfer.c
    ${_lf}/qrcodegen.c
    ${_tb}/tb_c5.c
    ${_tb}/tb_seq.c
    ${_tb}/tb_play.c
    ${_tb}/tb_detect.c
    ${_tb}/tb_pin.c
    ${_tb}/tb_dials_data.c
    ${_tb}/tb_serial_display.c)
target_include_directories(kithome_display PRIVATE
    ${_kh}
    ${_kh}/..
    ${_dg}
    ${_lf}
    ${_tc}
    ${_tb}
    ${_rg}
    ${_bs}
    ${_gb})
target_link_libraries(kithome_display fwog_display_bsp)
fwog_display_app(kithome_display
    VERSION 007
    DESCRIPTION "Landing: HostDeck MicScope BandScope GlassBak DiskGlass TalkClip ToneBox RigGlass. Four tiles per page.")

add_executable(kithome_main
    main/main.c
    kh_dg_main.c
    kh_tc_main.c
    kh_rg_main.c
    ${_gb}/gb_fs.c
    ${_dg}/dg_file.c
    ${_dg}/dg_fs.c
    ${_dg}/dg_wasm.c
    ${DG_WASM3}/m3_bind.c
    ${DG_WASM3}/m3_code.c
    ${DG_WASM3}/m3_compile.c
    ${DG_WASM3}/m3_core.c
    ${DG_WASM3}/m3_emit.c
    ${DG_WASM3}/m3_env.c
    ${DG_WASM3}/m3_exec.c
    ${DG_WASM3}/m3_function.c
    ${DG_WASM3}/m3_info.c
    ${DG_WASM3}/m3_module.c
    ${DG_WASM3}/m3_parse.c
    ${_tb}/main/tb_serial_main.c)
target_include_directories(kithome_main PRIVATE
    ${_kh}
    ${_dg}
    ${_bs}
    ${_gb}
    ${_tc}
    ${_tb}
    ${_rg}
    ${DG_WASM3})
target_compile_definitions(kithome_main PRIVATE
    d_m3MaxFunctionStackHeight=256
    d_m3MaxLinearMemoryPages=4
    d_m3CodePageAlignSize=256
    d_m3VerboseErrorMessages=0)
target_link_libraries(kithome_main fwog_main_bsp fwog_main_fs)
fwog_embed_display_image(kithome_main)
fwog_main_app(kithome_main
    VERSION 007
    DESCRIPTION "Main half: CC1101 mux, FatFs, wasm3, TalkClip, ToneBox UART1, RigGlass CDC")
