cmake_minimum_required(VERSION 3.13)
project(wiliog_ogemu C)
set(CMAKE_C_STANDARD 11)

# Standalone host tree: no Pico SDK. HOST_TEST turns ST7789/lcd_text into a
# 320x240 RGB565 RAM panel and the five buttons into inject-only inputs.
set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(BSP  ${REPO}/bsp)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()
if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

set(OGEMU_COMMON
    ${CMAKE_CURRENT_SOURCE_DIR}/harness/ogemu_main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/harness/ogemu_time.c
    ${CMAKE_CURRENT_SOURCE_DIR}/harness/ogemu_script.c
    ${CMAKE_CURRENT_SOURCE_DIR}/harness/ogemu_png.c
    ${CMAKE_CURRENT_SOURCE_DIR}/harness/ogemu_board.c
    ${CMAKE_CURRENT_SOURCE_DIR}/harness/ogemu_world.c
    ${CMAKE_CURRENT_SOURCE_DIR}/harness/ogemu_live.c
    ${BSP}/display_cpu/lcd/st7789.c
    ${BSP}/display_cpu/lcd/lcd_text.c
    ${BSP}/display_cpu/lcd/fwog_t9.c
    ${BSP}/display_cpu/lcd/fwog_splash.c
    ${BSP}/display_cpu/lcd/fwog_t9.c
    ${BSP}/display_cpu/input/buttons.c
    ${BSP}/display_cpu/power/ship_mode.c
    ${BSP}/display_cpu/power/power_poll.c
    ${BSP}/display_cpu/leds/ws2812_driver.c
    ${BSP}/display_cpu/sensors/lis3dh.c
    ${BSP}/common/crc.c
    ${BSP}/common/dsp/cic.c
    ${BSP}/common/dsp/spectrum.c
    ${BSP}/common/dsp/dcblock.c
    ${BSP}/common/link/link_frame.c
)

# ogemu_add_app(hostdeck)
# Compiles apps/<name>/display/main.c as fwog_app_main against the stubs.
# Extra sources: ogemu_add_app(voltpet ${REPO}/apps/voltpet/vp_art.c)
function(ogemu_add_app name)
    set(app_main ${REPO}/apps/${name}/display/main.c)
    if(NOT EXISTS ${app_main})
        message(FATAL_ERROR "ogemu_add_app(${name}): missing ${app_main}")
    endif()
    add_executable(ogemu_${name} ${app_main} ${OGEMU_COMMON} ${ARGN})
    target_include_directories(ogemu_${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/harness
        ${BSP}/display_cpu
        ${BSP}
        ${REPO}/apps/${name}
        ${REPO}/apps/voltpet
        ${REPO}/apps/bandscope
        ${REPO}/apps/ismburst
        ${REPO}/apps/twinfox
        ${REPO}/apps/tireear
        ${REPO}/apps/fobreplay
        ${REPO}/apps/inertialtrail
        ${REPO}/apps/inertialtrailrf
        ${REPO}/apps/kithome
        ${REPO}/apps/pinghalo
        ${REPO}/apps/bledeck
        ${REPO}/apps/lanferry
        ${REPO}/apps/battlebridge
        ${REPO}/apps/headerkit
        ${REPO}/apps/qwiicbench
        ${REPO}/apps/rigglass
        ${REPO}/apps/airmaraud
        ${REPO}/apps/diskglass
        ${REPO}/apps/chirpmail
        ${REPO}/apps/glassbak
        ${REPO}/apps/opticclick
        ${REPO}/apps/talkclip
        ${REPO}/apps/ttyglass
        ${REPO}/apps/retia
        ${REPO}/apps/pitchfork)
    target_compile_definitions(ogemu_${name} PRIVATE HOST_TEST=1)
    if(WIN32)
        target_link_libraries(ogemu_${name} PRIVATE ws2_32)
    else()
        find_package(Threads REQUIRED)
        target_link_libraries(ogemu_${name} PRIVATE Threads::Threads)
    endif()
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_link_libraries(ogemu_${name} PRIVATE m)
    endif()
    set_source_files_properties(${app_main} PROPERTIES
        COMPILE_DEFINITIONS "main=fwog_app_main")
endfunction()

ogemu_add_app(hostdeck)
ogemu_add_app(template)
ogemu_add_app(voltpet ${REPO}/apps/voltpet/vp_art.c)
ogemu_add_app(micscope ${REPO}/apps/micscope/ms_db.c)
ogemu_add_app(pitchfork ${REPO}/apps/pitchfork/pf_note.c)
ogemu_add_app(bandscope)
ogemu_add_app(ismburst)
ogemu_add_app(twinfox)
ogemu_add_app(tireear)
ogemu_add_app(fobreplay)
ogemu_add_app(inertialtrail ${REPO}/apps/inertialtrail/pedometer.c)
target_include_directories(ogemu_inertialtrail PRIVATE
    ${REPO}/apps/inertialtrail)
ogemu_add_app(inertialtrailrf ${REPO}/apps/inertialtrail/pedometer.c)
target_include_directories(ogemu_inertialtrailrf PRIVATE
    ${REPO}/apps/inertialtrail)
ogemu_add_app(kithome)
ogemu_add_app(diskglass
    ${REPO}/apps/diskglass/dg_file.c
    ${REPO}/apps/diskglass/dg_xfer.c)
ogemu_add_app(pinghalo)
ogemu_add_app(bledeck)
ogemu_add_app(lanferry ${REPO}/apps/lanferry/qrcodegen.c)
ogemu_add_app(battlebridge ${REPO}/apps/lanferry/qrcodegen.c)
ogemu_add_app(headerkit)
ogemu_add_app(qwiicbench ${REPO}/apps/qwiicbench/qg_nick.c)
ogemu_add_app(rigglass)
ogemu_add_app(chirpmail)
ogemu_add_app(airmaraud)
ogemu_add_app(glassbak)
ogemu_add_app(opticclick
    ${REPO}/apps/opticclick/oc_codes.c
    ${BSP}/display_cpu/ir/ir_comm.c)
ogemu_add_app(talkclip)
ogemu_add_app(tonebox)
ogemu_add_app(ttyglass)
ogemu_add_app(retia
    ${REPO}/apps/retia/rp_device_view.c
    ${REPO}/apps/retia/rp_mode.c)

enable_testing()
add_test(NAME ogemu_hostdeck_smoke
    COMMAND ogemu_hostdeck
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/smoke.jsonl
        --dump ${CMAKE_BINARY_DIR}/hostdeck.png)
add_test(NAME ogemu_hostdeck_world
    COMMAND ogemu_hostdeck
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/world.jsonl
        --dump ${CMAKE_BINARY_DIR}/hostdeck_world.png)
add_test(NAME ogemu_voltpet_smoke
    COMMAND ogemu_voltpet
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/voltpet.jsonl
        --dump ${CMAKE_BINARY_DIR}/voltpet.png)
# The only test where a world-bus sensor value reaches the panel: the others
# prove the inject happened, this one proves an app read it.
add_test(NAME ogemu_micscope_smoke
    COMMAND ogemu_micscope
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/micscope.jsonl
        --dump ${CMAKE_BINARY_DIR}/micscope.png)
add_test(NAME ogemu_micscope_tone
    COMMAND ogemu_micscope
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/micscope_tone.jsonl
        --dump ${CMAKE_BINARY_DIR}/micscope_tone.png)
add_test(NAME ogemu_bandscope_smoke
    COMMAND ogemu_bandscope
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/bandscope.jsonl
        --dump ${CMAKE_BINARY_DIR}/bandscope.png)
add_test(NAME ogemu_ismburst_smoke
    COMMAND ogemu_ismburst
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/ismburst.jsonl
        --dump ${CMAKE_BINARY_DIR}/ismburst.png)
add_test(NAME ogemu_twinfox_smoke
    COMMAND ogemu_twinfox
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/twinfox.jsonl
        --dump ${CMAKE_BINARY_DIR}/twinfox.png)
add_test(NAME ogemu_tireear_smoke
    COMMAND ogemu_tireear
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/tireear.jsonl
        --dump ${CMAKE_BINARY_DIR}/tireear.png)
add_test(NAME ogemu_fobreplay_smoke
    COMMAND ogemu_fobreplay
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/fobreplay.jsonl
        --dump ${CMAKE_BINARY_DIR}/fobreplay.png)
add_test(NAME ogemu_inertialtrail_smoke
    COMMAND ogemu_inertialtrail
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/inertialtrail.jsonl
        --dump ${CMAKE_BINARY_DIR}/inertialtrail.png)
add_test(NAME ogemu_inertialtrailrf_smoke
    COMMAND ogemu_inertialtrailrf
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/inertialtrailrf.jsonl
        --dump ${CMAKE_BINARY_DIR}/inertialtrailrf.png)
add_test(NAME ogemu_kithome_smoke
    COMMAND ogemu_kithome
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/kithome.jsonl
        --dump ${CMAKE_BINARY_DIR}/kithome.png)
add_test(NAME ogemu_pinghalo_smoke
    COMMAND ogemu_pinghalo
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/pinghalo.jsonl
        --dump ${CMAKE_BINARY_DIR}/pinghalo.png)
add_test(NAME ogemu_bledeck_smoke
    COMMAND ogemu_bledeck
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/bledeck.jsonl
        --dump ${CMAKE_BINARY_DIR}/bledeck.png)
add_test(NAME ogemu_lanferry_smoke
    COMMAND ogemu_lanferry
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/lanferry.jsonl
        --dump ${CMAKE_BINARY_DIR}/lanferry.png)
add_test(NAME ogemu_battlebridge_smoke
    COMMAND ogemu_battlebridge
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/battlebridge.jsonl
        --dump ${CMAKE_BINARY_DIR}/battlebridge.png)
add_test(NAME ogemu_headerkit_smoke
    COMMAND ogemu_headerkit
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/headerkit.jsonl
        --dump ${CMAKE_BINARY_DIR}/headerkit.png)
add_test(NAME ogemu_qwiicbench_smoke
    COMMAND ogemu_qwiicbench
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/qwiicbench.jsonl
        --dump ${CMAKE_BINARY_DIR}/qwiicbench.png)
add_test(NAME ogemu_rigglass_smoke
    COMMAND ogemu_rigglass
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/rigglass.jsonl
        --dump ${CMAKE_BINARY_DIR}/rigglass.png)
add_test(NAME ogemu_airmaraud_smoke
    COMMAND ogemu_airmaraud
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/airmaraud.jsonl
        --dump ${CMAKE_BINARY_DIR}/airmaraud.png)
add_test(NAME ogemu_diskglass_smoke
    COMMAND ogemu_diskglass
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/diskglass.jsonl
        --dump ${CMAKE_BINARY_DIR}/diskglass.png)
add_test(NAME ogemu_chirpmail_smoke
    COMMAND ogemu_chirpmail
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/chirpmail.jsonl
        --dump ${CMAKE_BINARY_DIR}/chirpmail.png)
add_test(NAME ogemu_glassbak_smoke
    COMMAND ogemu_glassbak
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/glassbak.jsonl
        --dump ${CMAKE_BINARY_DIR}/glassbak.png)
add_test(NAME ogemu_pitchfork_smoke
    COMMAND ogemu_pitchfork
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/pitchfork.jsonl
        --dump ${CMAKE_BINARY_DIR}/pitchfork.png)
add_test(NAME ogemu_opticclick_smoke
    COMMAND ogemu_opticclick
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/opticclick.jsonl
        --dump ${CMAKE_BINARY_DIR}/opticclick.png)
add_test(NAME ogemu_talkclip_smoke
    COMMAND ogemu_talkclip
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/talkclip.jsonl
        --dump ${CMAKE_BINARY_DIR}/talkclip.png)
add_test(NAME ogemu_tonebox_smoke
    COMMAND ogemu_tonebox
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/tonebox.jsonl
        --dump ${CMAKE_BINARY_DIR}/tonebox.png)
add_test(NAME ogemu_ttyglass_smoke
    COMMAND ogemu_ttyglass
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/ttyglass.jsonl
        --dump ${CMAKE_BINARY_DIR}/ttyglass.png)
add_test(NAME ogemu_retia_smoke
    COMMAND ogemu_retia
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/retia.jsonl
        --dump ${CMAKE_BINARY_DIR}/retia.png)
set_tests_properties(ogemu_hostdeck_smoke ogemu_hostdeck_world ogemu_voltpet_smoke
                     ogemu_micscope_smoke ogemu_micscope_tone
                     ogemu_bandscope_smoke ogemu_ismburst_smoke
                     ogemu_twinfox_smoke ogemu_tireear_smoke
                     ogemu_fobreplay_smoke ogemu_inertialtrail_smoke
                     ogemu_inertialtrailrf_smoke
                     ogemu_kithome_smoke ogemu_pinghalo_smoke
                     ogemu_bledeck_smoke ogemu_lanferry_smoke
                     ogemu_battlebridge_smoke
                     ogemu_headerkit_smoke
                     ogemu_qwiicbench_smoke
                     ogemu_rigglass_smoke
                     ogemu_airmaraud_smoke
                     ogemu_diskglass_smoke
                     ogemu_chirpmail_smoke
                     ogemu_glassbak_smoke
                     ogemu_pitchfork_smoke
                     ogemu_opticclick_smoke
                     ogemu_talkclip_smoke
                     ogemu_tonebox_smoke
                     ogemu_ttyglass_smoke
                     ogemu_retia_smoke PROPERTIES
    WORKING_DIRECTORY ${REPO})
