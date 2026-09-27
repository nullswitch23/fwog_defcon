add_executable(diskglass_display
    display/main.c
    dg_file.c
    dg_xfer.c
    ${CMAKE_CURRENT_LIST_DIR}/../lanferry/qrcodegen.c)
target_include_directories(diskglass_display PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../lanferry)
target_link_libraries(diskglass_display fwog_display_bsp)
fwog_display_app(diskglass_display
    VERSION 009
    DESCRIPTION "FatFs library: T9 IR compose, loop WASM, delete, IR/QR.")

set(DG_WASM3 ${CMAKE_CURRENT_LIST_DIR}/third_party/wasm3)
add_executable(diskglass_main
    main/main.c
    dg_file.c
    dg_fs.c
    dg_wasm.c
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
    ${DG_WASM3}/m3_parse.c)
target_include_directories(diskglass_main PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${DG_WASM3})
target_compile_definitions(diskglass_main PRIVATE
    d_m3MaxFunctionStackHeight=256
    d_m3MaxLinearMemoryPages=4
    d_m3CodePageAlignSize=256
    d_m3VerboseErrorMessages=0)
target_link_libraries(diskglass_main fwog_main_bsp fwog_main_fs)
fwog_embed_display_image(diskglass_main)
fwog_main_app(diskglass_main
    VERSION 009
    DESCRIPTION "Main half: FatFs, T9 notes to /inbox, wasm3, ASK replay")
