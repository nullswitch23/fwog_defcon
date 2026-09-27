# Where this BSP's build-helper scripts live (gen_uf2_info.py,
# check_uf2_info.py, genimage.py).
#
# Resolved relative to THIS FILE, never to CMAKE_SOURCE_DIR. CMAKE_SOURCE_DIR
# is the TOP-LEVEL project, so a consumer doing
# `add_subdirectory(third_party/wiliOGbsp/bsp)` had these paths resolve into
# THEIR tree, where the scripts do not exist, and the build failed. That made
# this BSP unusable as a submodule; the first downstream port worked around it
# with three shim scripts of its own.
#
# CACHE, not a plain set(), for the same reason FWOG_USB_MANUFACTURER is
# cached: the functions below are called from apps/*/CMakeLists.txt, a sibling
# scope that does not inherit a plain set() from bsp/.
#
# A consumer that vendors the scripts elsewhere can override it.
set(FWOG_TOOLS_DIR "${CMAKE_CURRENT_LIST_DIR}/../tools" CACHE PATH
    "Directory holding this BSP's build-helper scripts")

# Merged ESP32-C6 image (bootloader + table + app) bit-banged into Bottlenose
# over UART1. Relative to THIS FILE so a consumer of the BSP still finds it
# in this tree. Empty / missing is allowed: the flasher UI runs and reports
# "no C6 image" until you merge one (see firmware/bottlenose/README.md).
get_filename_component(_fwog_c6_default
    "${CMAKE_CURRENT_LIST_DIR}/../firmware/bottlenose/fwog_c6.bin" ABSOLUTE)
set(FWOG_C6_IMAGE "${_fwog_c6_default}" CACHE FILEPATH
    "Merged ESP32-C6 image embedded into Bottlenose-capable main UF2s")

# App Explorer's local drop folder. Only main-CPU UF2s belong here: those are
# what you flash, and they carry the paired display image. A display
# application UF2 in this folder would be offered as a flash target and
# brick the board (no metadata sector). Empty string disables the copy.
get_filename_component(_fwog_catalog "${CMAKE_CURRENT_LIST_DIR}/../catalog" ABSOLUTE)
set(FWOG_CATALOG_DIR "${_fwog_catalog}" CACHE PATH
    "Directory that receives each *_main.uf2 after a successful build")

# Diagnostics channel: 0=none, 1=USB CDC (default), 2=SEGGER RTT.
set(FWOG_DIAG 1 CACHE STRING "DIAG() channel: 0=none 1=USB-CDC 2=RTT")

# Inter-CPU link rate. 12500000 is clk_peri 200 MHz / 16, the PL011 maximum
# at a divisor of exactly 1.0 -- no margin. If the trace proves marginal at
# bring-up, the ladder is 10000000, 8333333, then 6250000. Both CPUs take
# the value from here, so they cannot disagree; `fw build --baud N` sets it.
set(FWOG_LINK_BAUD 12500000 CACHE STRING "inter-CPU link baud rate")

# Warnings on for the BSP's own sources only (PRIVATE, below): the Pico SDK's
# own libraries are not warning-clean under -Wextra and are not ours to fix.
set(FWOG_WARN_FLAGS "")
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(FWOG_WARN_FLAGS -Wall -Wextra)
endif()

# --- shared, CPU-agnostic ---
add_library(fwog_common STATIC
    common/crc.c
    common/clocks.c
    common/app_meta.c
    common/link/link_frame.c
    common/link/bl_proto.c
    common/link/io_proto.c
    common/diag.c
    common/io_cfg.c
    common/io_pins.c
    common/io_seq.c
    common/dsp/cic.c
    common/dsp/dcblock.c
    common/dsp/spectrum.c)
target_compile_options(fwog_common PRIVATE ${FWOG_WARN_FLAGS})
target_include_directories(fwog_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(fwog_common PUBLIC
    FWOG_DIAG=${FWOG_DIAG}
    FWOG_LINK_BAUD=${FWOG_LINK_BAUD}u)
target_link_libraries(fwog_common PUBLIC pico_stdlib hardware_clocks hardware_vreg)
if(FWOG_DIAG EQUAL 1)
    target_link_libraries(fwog_common PUBLIC pico_stdio_usb)
endif()

# Route stdio. The SDK defaults PICO_STDIO_UART to 1 and PICO_STDIO_USB to 0,
# which would send every DIAG() out UART0 -- the inter-CPU link -- and corrupt
# it, while never reaching USB at all. Every app MUST call this.
function(fwog_configure_stdio target)
    pico_enable_stdio_uart(${target} 0)   # UART0 is the link. Never stdio.
    if(FWOG_DIAG EQUAL 1)
        pico_enable_stdio_usb(${target} 1)
    else()
        pico_enable_stdio_usb(${target} 0)
    endif()

    # ---- Do not lose 1200-baud BOOTSEL ----
    #
    # A running CPU is put into BOOTSEL from the host by opening its USB CDC
    # port at 1200 baud (the hardware record). That is not something this BSP
    # implements: pico_stdio_usb links pico_usb_reset, whose
    # PICO_ENABLE_USB_RESET_VIA_BAUD_RATE defaults to 1. So the rule is not
    # "build it" but "do not break it", and these two checks are the whole
    # enforcement.
    #
    # It matters out of proportion to its size on the DISPLAY CPU, which has
    # no BOOTSEL button: 1200 baud is its only remote recovery, and losing it
    # has no symptom until the moment it is needed.
    if(DEFINED PICO_ENABLE_USB_RESET_VIA_BAUD_RATE
       AND NOT PICO_ENABLE_USB_RESET_VIA_BAUD_RATE)
        message(FATAL_ERROR
            "${target}: PICO_ENABLE_USB_RESET_VIA_BAUD_RATE is 0, which "
            "removes 1200-baud BOOTSEL. The display CPU has no BOOTSEL "
            "button and no other remote recovery. See the hardware record.")
    endif()

    # A WARNING and not an error: FWOG_DIAG=0 is a supported configuration
    # (the foundation design requires -DFWOG_DIAG=0 to compile diagnostics
    # away entirely, and build-nodiag/ exercises it). It does, however, drop
    # pico_stdio_usb entirely -- and 1200-baud BOOTSEL with it -- so a build
    # that chooses it should be told what it is giving up rather than
    # discovering it on a board that will not come back.
    if(NOT FWOG_DIAG EQUAL 1)
        message(WARNING
            "${target}: FWOG_DIAG=${FWOG_DIAG} links no USB CDC, so this "
            "binary has NO 1200-baud BOOTSEL. Recovering the display CPU "
            "will need the main CPU to reflash it over the link. See "
            "the hardware record.")
    endif()
endfunction()

# Override the FLASH memory region for one target.
#
# SDK 2.3.0 builds the FLASH region from a single generated line in
# pico_flash_region.ld, and pico_add_linker_script_override_path() prepends
# a -L that resolves before the SDK's own copy (pico_standard_link's
# CMakeLists emits override -L flags ahead of PICO_LINKER_SCRIPT_INCLUDE_DIRS).
# So a two-line file is the entire mechanism.
#
# Do NOT do this by vendoring memmap_default.ld instead: in 2.3.0 that is a
# tree of a dozen .incl files, and passing a custom script to
# pico_set_linker_script() trips pico_check_linker_script(), which turns off
# PICO_CRT0_ALLOCATE_SPACERS as a compatibility workaround.
function(fwog_flash_region target origin length)
    set(_dir ${CMAKE_CURRENT_BINARY_DIR}/${target}_flash_region)
    file(WRITE ${_dir}/pico_flash_region.ld
         "FLASH(rx) : ORIGIN = ${origin}, LENGTH = ${length}\n")
    pico_add_linker_script_override_path(${target} ${_dir}
                                         FILES pico_flash_region.ld)
endfunction()

# Make every binary say which CPU it is, in its USB product string.
#
# fw bootsel and fw flash identify a CPU by this string and then reboot ONLY
# that one into BOOTSEL at 1200 baud (facts 28 and 30). That is what keeps
# fact 15's one-CPU-at-a-time rule true by construction. It only works if
# identification is a property of the BSP: when it was left to each app, only
# bl_display and lcd_display ever set it, so a display CPU running
# template_display reported "Pico" -- exactly like main, which is the
# misidentification that ends with an image on the wrong processor.
#
# A PRIVATE compile definition on `target` reaches USBD_PRODUCT at all only
# because pico_stdio_usb's sources -- an SDK library, but INTERFACE -- compile
# straight into `target` rather than into a separate translation unit; a
# REAL static library would need the define PUBLIC to see it in its own
# objects. Don't "fix" PRIVATE to PUBLIC or delete this define as ineffective
# without that in mind.
#
# The length check is here for the same reason the bootloader's LENGTH cap is:
# USBD_DESC_STR_MAX defaults to 20 and stdio_usb_descriptors.c copies with
# `len < USBD_DESC_STR_MAX - 1`, so a too-long string is TRUNCATED SILENTLY at
# 19 characters and identification fails with no symptom at build time. Raising
# the cap to 48 costs 96 bytes of RAM instead of 40; the SDK #errors above 127.
# The manufacturer string every binary reports. Board-wide, not per-target:
# the SDK default is "Raspberry Pi", which is true of the silicon and wrong
# about the product a user is holding.
#
# Nothing IDENTIFIES a CPU by this -- fwfinder matches VID/PID, and fw.py
# reads the product string -- so it is presentation, and it is capped by the
# same 47-char USBD_DESC_STR_MAX budget as the product string.
#
# A CACHE variable, like FWOG_DIAG and FWOG_LINK_BAUD above, and for a reason
# worth keeping: fwog_usb_product() is CALLED FROM apps/*/CMakeLists.txt, a
# sibling directory scope that does not inherit this one. A plain set() here
# is invisible there, and the first attempt shipped
# USBD_MANUFACTURER="" -- silently, because an empty string passes a
# length check. A cache variable is visible in every scope.
set(FWOG_USB_MANUFACTURER "FreeWili OG" CACHE STRING
    "USB manufacturer string reported by every FwOGapp")

function(fwog_usb_product target cpu name version)
    set(_product "FWOG ${cpu} ${name} ${version}")
    string(LENGTH "${_product}" _len)
    if(_len GREATER 47)
        message(FATAL_ERROR
            "USB product string '${_product}' is ${_len} chars; the 47-char "
            "limit would truncate it and break CPU identification")
    endif()
    # Same cap, same silent-truncation failure mode: stdio_usb_descriptors.c
    # copies every string descriptor with `len < USBD_DESC_STR_MAX - 1`.
    #
    # The EMPTY check is not defensive padding -- it is the bug that actually
    # happened. A scope mistake made this variable empty here, and every
    # length check still passed, so the board would have enumerated with a
    # blank manufacturer and nothing would have said so.
    string(LENGTH "${FWOG_USB_MANUFACTURER}" _mlen)
    if(_mlen EQUAL 0)
        message(FATAL_ERROR
            "FWOG_USB_MANUFACTURER is empty at ${target}. It is a CACHE "
            "variable set in bsp/CMakeLists.txt precisely so it is visible "
            "in apps/ -- a plain set() there is not.")
    endif()
    if(_mlen GREATER 47)
        message(FATAL_ERROR
            "USB manufacturer string '${FWOG_USB_MANUFACTURER}' is ${_mlen} "
            "chars; the 47-char limit would truncate it silently")
    endif()
    target_compile_definitions(${target} PRIVATE
        USBD_PRODUCT="${_product}"
        USBD_MANUFACTURER="${FWOG_USB_MANUFACTURER}"
        USBD_DESC_STR_MAX=48)
endfunction()

# The VID/PID pair FreeWili's own finder matches on.
#
# From freewili/freewili-finder, include/usbdef.hpp and src/fwfinder.cpp:40:
#
#   0x093C:0x2054 -> USBDeviceType::SerialMain
#   0x093C:0x2055 -> USBDeviceType::SerialDisplay
#   0x2E8A:0x000A -> USBDeviceType::Serial, then disambiguated BY HUB PORT
#                    (port 1 = Main, port 2 = Display) under a comment in that
#                    file reading "for older firmware"
#
# Every binary here used to take the third branch, because nothing overrode
# the SDK default. FwFinder could therefore tell our two CPUs apart only by
# which port of the onboard hub (0x0424:0x2513) they sat on -- positional
# identification that breaks the moment a CPU is reached any other way.
# 0x093C is Intrepid Control Systems.
#
# Same PRIVATE-define mechanism and the same INTERFACE-linkage caveat as
# USBD_PRODUCT above: both macros are #ifndef-guarded in SDK 2.3.0's
# stdio_usb_descriptors.c, at lines 36 and 40.
#
# bcdDevice is deliberately NOT set, and must not be. It is hardcoded to
# 0x0100 at stdio_usb_descriptors.c:111 with no guard, and
# tud_descriptor_device_cb() at :142 is a STRONG symbol in a library whose
# sources compile into the target -- so our own definition would be a
# duplicate symbol, not an override. Reaching it means replacing the whole
# descriptor set, including the vendor reset interface that 1200-baud BOOTSEL
# rides on. That trades the display CPU's only remote recovery for a version
# field it already carries in its product string and its UF2 record.
function(fwog_usb_ids target cpu)
    if(cpu STREQUAL "display")
        set(_pid 0x2055)
    elseif(cpu STREQUAL "main")
        set(_pid 0x2054)
    else()
        message(FATAL_ERROR
            "fwog_usb_ids: cpu must be display or main, got '${cpu}'")
    endif()
    target_compile_definitions(${target} PRIVATE
        USBD_VID=0x093C
        USBD_PID=${_pid})
endfunction()

# Parse and VALIDATE the NAME/VERSION/DESCRIPTION every FwOGapp declares.
#
# NAME defaults to the target with its _display/_main suffix stripped -- the
# same `_short` fwog_usb_product() used to compute for itself. One name, one
# source, so the string on the USB wire and the string in the UF2 record
# cannot disagree.
#
# VERSION is three digits and bumped BY HAND. Deriving it from the git commit
# count was rejected: that moves when an unrelated file changes, is not
# per-app, and wraps at 999. A version that changes without the app changing
# is worse than no version at all.
#
# DESCRIPTION has no default on purpose. A description that defaults to
# something is a description nobody writes, and it is what the App Explorer
# shows a human deciding what to flash.
function(fwog_parse_app_args prefix target)
    cmake_parse_arguments(PARSE_ARGV 2 A "" "NAME;VERSION;DESCRIPTION" "")

    if(NOT A_NAME)
        string(REGEX REPLACE "_(display|main)$" "" A_NAME ${target})
    endif()
    if(NOT A_VERSION MATCHES "^[0-9][0-9][0-9]$")
        message(FATAL_ERROR
            "${target}: VERSION must be exactly three digits (000-999), got "
            "'${A_VERSION}'. Every FwOGapp declares one -- it reaches the USB "
            "product string and the UF2 info record. See AGENTS.md.")
    endif()
    if(NOT A_DESCRIPTION)
        message(FATAL_ERROR
            "${target}: DESCRIPTION is required. It is what the App Explorer "
            "shows a human choosing what to flash, so it cannot be defaulted.")
    endif()
    string(LENGTH "${A_DESCRIPTION}" _dlen)
    if(_dlen GREATER 127)
        message(FATAL_ERROR
            "${target}: DESCRIPTION is ${_dlen} chars; the record holds 127 "
            "plus a terminator. Shorten it rather than letting it truncate.")
    endif()

    set(${prefix}_NAME        "${A_NAME}"        PARENT_SCOPE)
    set(${prefix}_VERSION     "${A_VERSION}"     PARENT_SCOPE)
    set(${prefix}_DESCRIPTION "${A_DESCRIPTION}" PARENT_SCOPE)
endfunction()

# Generate and compile in the fwog_uf2_info_t record a host tool scans for.
#
# build/build_ts are EMPTY for a display APPLICATION. display_update.c decides
# whether to transfer the image at all by comparing its CRC32, so a
# build-varying byte in a display app forces a re-transfer and reflash on
# EVERY commit. The top-level CMakeLists.txt already refuses to compile
# FWOG_GIT_DESCRIBE into display apps for exactly this reason; this keeps that
# promise instead of quietly breaking it.
#
# The BOOTLOADER is a display-CPU binary and is NOT exempt from that rule by
# accident -- it is exempt on purpose: it is flashed by UF2 and has no
# CRC-skip path, so its bytes changing costs nothing.
function(fwog_add_uf2_info target cpu kind name version description)
    if(cpu STREQUAL "display")
        set(_cpu_id 0)
    else()
        set(_cpu_id 1)
    endif()
    if(kind STREQUAL "bootloader")
        set(_kind_id 1)
    else()
        set(_kind_id 0)
    endif()

    if(_cpu_id EQUAL 0 AND _kind_id EQUAL 0)
        set(_build "")
        set(_ts 0)
    else()
        set(_build "${FWOG_GIT_DESCRIBE}")
        set(_ts ${FWOG_GIT_COMMIT_TS})
    endif()

    # Strip leading zeros so C does not read "017" as octal.
    string(REGEX REPLACE "^0+([0-9])" "\\1" _vnum "${version}")

    set(_dir ${CMAKE_CURRENT_BINARY_DIR}/${target}_uf2_info)
    add_custom_command(
        OUTPUT ${_dir}/${target}_uf2_info.c
        COMMAND ${Python3_EXECUTABLE} ${FWOG_TOOLS_DIR}/gen_uf2_info.py
                --out ${_dir}/${target}_uf2_info.c
                --cpu ${_cpu_id} --kind ${_kind_id} --version ${_vnum}
                --name "${name}" --description "${description}"
                --build "${_build}" --build-ts ${_ts}
        DEPENDS ${FWOG_TOOLS_DIR}/gen_uf2_info.py
        COMMENT "Generating UF2 info record for ${target}"
        VERBATIM)
    target_sources(${target} PRIVATE ${_dir}/${target}_uf2_info.c)

    # Nothing in the firmware references the record, so --gc-sections drops it
    # -- MEASURED, not feared: the first build of this feature produced eight
    # binaries containing zero records, with bench_display.bin byte-identical
    # in size to before the change.
    #
    # __attribute__((retain)) is the documented answer and this toolchain
    # IGNORES it (-Wattributes). The SDK's KEEP'd .binary_info.keep.* section
    # is the other candidate and is wrong here: picotool walks
    # __binary_info_start..__binary_info_end as an array of POINTERS, so raw
    # bytes placed there would be read as 54 bogus ones.
    #
    # --undefined names a garbage-collection ROOT to GNU ld, which is
    # precisely what an unreferenced record needs, and costs no linker script
    # change.
    target_link_options(${target} PRIVATE -Wl,--undefined=fwog_uf2_info)
endfunction()

# Inspect the ARTEFACT, not the source.
#
# This is the only check in the tree that can catch the linker discarding the
# record, or a generator that wrote a CRC over the wrong bytes -- both of
# which leave every other part of the build succeeding. The discard is not
# hypothetical: it is what the first build of this feature did, in all eight
# binaries at once.
#
# Must run AFTER pico_add_extra_outputs(), which is what produces the .bin.
function(fwog_check_uf2_info target cpu)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${Python3_EXECUTABLE}
                ${FWOG_TOOLS_DIR}/check_uf2_info.py
                --bin $<TARGET_FILE_DIR:${target}>/${target}.bin
                --expect-cpu ${cpu}
        VERBATIM)
endfunction()

# Everything a display-CPU application must not forget, in one call:
# stdio off UART0, linked at the bootloader's app offset, UF2/bin/map out.
#
# The app offset is the reason this exists as a single function. A display
# app that links at the default 0x10000000 still builds, still produces a
# UF2, and OVERWRITES THE BOOTLOADER when flashed. That failure has no
# compile-time symptom, so the call that prevents it must be the same call
# every app already makes.
#
# 16m - 132k = 16 MB minus the 128 KB bootloader and the 4 KB metadata
# sector. Keep in step with FWOG_APP_OFFSET in bsp/common/app_meta.h;
# tests/test_app_meta.c asserts the C side and apps/CMakeLists asserts this
# side against it.
function(fwog_display_app target)
    fwog_parse_app_args(A ${target} ${ARGN})
    fwog_configure_stdio(${target})
    fwog_usb_product(${target} display "${A_NAME}" "${A_VERSION}")
    fwog_usb_ids(${target} display)
    fwog_add_uf2_info(${target} display app
                      "${A_NAME}" "${A_VERSION}" "${A_DESCRIPTION}")
    fwog_flash_region(${target} 0x10021000 "16m - 132k")
    pico_add_extra_outputs(${target})
    fwog_check_uf2_info(${target} display)
endfunction()

# Copy a main-CPU UF2 into FWOG_CATALOG_DIR after the uf2-info check.
# Display apps must not call this.
function(fwog_catalog_uf2 target)
    if(FWOG_CATALOG_DIR STREQUAL "")
        return()
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${FWOG_CATALOG_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE_DIR:${target}>/${target}.uf2"
            "${FWOG_CATALOG_DIR}/${target}.uf2"
        COMMENT "catalog: ${target}.uf2"
        VERBATIM)
endfunction()

# The main CPU has no bootloader, so its apps take the whole flash.
function(fwog_main_app target)
    fwog_parse_app_args(A ${target} ${ARGN})
    fwog_configure_stdio(${target})
    fwog_usb_product(${target} main "${A_NAME}" "${A_VERSION}")
    fwog_usb_ids(${target} main)
    fwog_add_uf2_info(${target} main app
                      "${A_NAME}" "${A_VERSION}" "${A_DESCRIPTION}")
    pico_add_extra_outputs(${target})
    fwog_check_uf2_info(${target} main)
    fwog_catalog_uf2(${target})
endfunction()

# The bootloader itself. The LENGTH cap is load-bearing: it turns "the
# bootloader outgrew its 128 KB reserve and now overlaps the metadata
# sector" into a link error instead of a board that boots once.
function(fwog_link_bootloader target)
    fwog_parse_app_args(A ${target} ${ARGN})
    fwog_configure_stdio(${target})
    fwog_usb_product(${target} display "${A_NAME}" "${A_VERSION}")
    # 093C:2055, the same display PID an application takes. A board sitting in
    # the bootloader should identify as a display CPU, because that is what it
    # is -- and fw flash must be able to find it the same way either side of a
    # jump into the app.
    fwog_usb_ids(${target} display)
    fwog_add_uf2_info(${target} display bootloader
                      "${A_NAME}" "${A_VERSION}" "${A_DESCRIPTION}")
    fwog_flash_region(${target} 0x10000000 "128k")
    pico_add_extra_outputs(${target})
    fwog_check_uf2_info(${target} display)
endfunction()

# i2c_bus.c and link_uart.c are compiled per-CPU, because they resolve
# FWOG_I2C / FWOG_LINK_UART and the PIN_* macros from that CPU's board.h.
# They are deliberately NOT part of fwog_common.

# --- display CPU ---
add_library(fwog_display_bsp STATIC
    display_cpu/platform/board.c
    display_cpu/lcd/st7789.c
    display_cpu/lcd/lcd_text.c
    display_cpu/lcd/fwog_t9.c
    display_cpu/lcd/fwog_splash.c
    display_cpu/lcd/splash_art.c
    display_cpu/io_expander/pcal6416.c
    display_cpu/io_expander/ioexp_link.c
    display_cpu/input/buttons.c
    display_cpu/power/bq25896.c
    display_cpu/power/ship_mode.c
    display_cpu/power/power_poll.c
    display_cpu/sensors/mcp7940.c
    display_cpu/sensors/lis3dh.c
    display_cpu/leds/ws2812_driver.c
    display_cpu/pdm/pdm_mic.c
    display_cpu/audio/i2s_audio.c
    display_cpu/ir/ir_comm.c
    common/i2c_bus.c
    common/link/link_uart.c)
target_compile_options(fwog_display_bsp PRIVATE ${FWOG_WARN_FLAGS})
target_include_directories(fwog_display_bsp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/display_cpu)
target_compile_definitions(fwog_display_bsp PUBLIC FWOG_CPU_DISPLAY=1)
target_link_libraries(fwog_display_bsp PUBLIC
    fwog_common hardware_i2c hardware_spi hardware_pwm
    hardware_clocks hardware_vreg hardware_dma hardware_pio)

# Override the display application embedded in EVERY main-CPU UF2.
#
# EMPTY BY DEFAULT, and that default is the important part: with nothing set,
# each main app carries the display app from its OWN folder (see
# fwog_embed_display_image below). Setting this forces one display image into
# every main binary, which is how you push a particular display app to a board
# using whichever main app you like -- but it reintroduces an ordering
# requirement, because the named target must already be declared.
set(FWOG_DISPLAY_FIRMWARE "" CACHE STRING
    "display app embedded into ALL main-CPU binaries (empty: each main app \
carries its own folder's display app)")

# Embed a display app's image into `target`.
#
#   fwog_embed_display_image(ogvegas_main)                  # ogvegas_display
#   fwog_embed_display_image(ogvegas_main lcd_display)      # explicit
#
# With no second argument this resolves the PAIR: `<name>_main` carries
# `<name>_display`, the app declared beside it in the same folder. That is
# what lets apps/<name>/CMakeLists.txt be self-contained -- the display half
# is always declared first in the same file, so it always exists by the time
# this runs, and the top-level add_subdirectory() order stops mattering.
#
# Precedence: explicit argument, then FWOG_DISPLAY_FIRMWARE if set, then the
# pair. The global override is last-resort on purpose: it is the one that can
# silently put an unrelated app's display inside your main UF2.
#
# The generated .S/.c become sources of `target`, and the custom command
# depends on the display target and its .bin, so touching display source
# regenerates the blob and relinks main. That is what makes `fw build`
# produce one UF2 carrying both firmwares.
function(fwog_embed_display_image target)
    if(ARGC GREATER 1)
        set(_disp ${ARGV1})
        set(_why "named explicitly by apps/.../CMakeLists.txt")
    elseif(FWOG_DISPLAY_FIRMWARE)
        set(_disp ${FWOG_DISPLAY_FIRMWARE})
        set(_why "set globally by FWOG_DISPLAY_FIRMWARE")
    else()
        # The pair. `_main` is the only suffix fwog_main_app() targets carry.
        string(REGEX REPLACE "_main$" "_display" _disp "${target}")
        set(_why "this app's own folder (the ${target}/${_disp} pair)")
        if(_disp STREQUAL target)
            message(FATAL_ERROR
                "fwog_embed_display_image(${target}): cannot infer a display "
                "app because the target name does not end in '_main'. Name "
                "the display app explicitly as the second argument.")
        endif()
    endif()
    if(NOT TARGET ${_disp})
        message(FATAL_ERROR
            "fwog_embed_display_image(${target}): '${_disp}' (${_why}) is not "
            "a target yet. A display app must be declared BEFORE the main app "
            "that embeds it -- within one apps/<name>/CMakeLists.txt that is "
            "automatic, but a cross-folder reference needs that folder's "
            "add_subdirectory() to come first in the top-level CMakeLists.txt.")
    endif()

    set(_gen ${CMAKE_CURRENT_BINARY_DIR}/${target}_display_image)
    # pico_add_extra_outputs() declares this path as a BYPRODUCT of the
    # display target's POST_BUILD objcopy, so naming it in DEPENDS gives
    # Ninja a real file dependency rather than only target ordering.
    set(_bin $<TARGET_FILE_DIR:${_disp}>/${_disp}.bin)

    add_custom_command(
        OUTPUT ${_gen}/display_image.S ${_gen}/display_image.c
        COMMAND ${Python3_EXECUTABLE} ${FWOG_TOOLS_DIR}/genimage.py
                --bin ${_bin}
                --out-dir ${_gen}
                --version ${FWOG_GIT_DESCRIBE}
        DEPENDS ${_disp} ${_bin} ${FWOG_TOOLS_DIR}/genimage.py
        COMMENT "Embedding ${_disp} into ${target}"
        VERBATIM)

    # display_image.c includes "display_update/display_image.h", which is
    # on the include path because `target` links fwog_main_bsp.
    target_sources(${target} PRIVATE
        ${_gen}/display_image.S
        ${_gen}/display_image.c)
endfunction()

# Embed firmware/bottlenose/fwog_c6.bin (or FWOG_C6_IMAGE) into a main app so
# Yellow-on-the-panel can program the Orca without a PC USB-C cable. Missing
# bin is not a configure error: gen_c6_image.py emits a 1-byte stub and the
# runtime reports "no C6 image".
function(fwog_embed_c6_image target)
    set(_c6_bin ${FWOG_C6_IMAGE})
    set(_c6_gen ${CMAKE_CURRENT_BINARY_DIR}/${target}_c6_image)
    set(_c6_deps ${FWOG_TOOLS_DIR}/gen_c6_image.py)
    if(EXISTS ${_c6_bin})
        list(APPEND _c6_deps ${_c6_bin})
    else()
        message(STATUS
            "${target}: no C6 image at ${_c6_bin} — flasher UI works, write "
            "reports no C6 image until you merge one")
    endif()
    add_custom_command(
        OUTPUT ${_c6_gen}/c6_image.S ${_c6_gen}/c6_image.c
        COMMAND ${Python3_EXECUTABLE} ${FWOG_TOOLS_DIR}/gen_c6_image.py
                --bin ${_c6_bin}
                --out-dir ${_c6_gen}
        DEPENDS ${_c6_deps}
        COMMENT "Embedding Bottlenose C6 image into ${target}"
        VERBATIM)
    target_sources(${target} PRIVATE
        ${_c6_gen}/c6_image.S
        ${_c6_gen}/c6_image.c)
endfunction()

# --- display serial bootloader ---
# Separate from fwog_display_bsp because keeping it separate makes its size
# visible in the map file. apps/lcd also links it now, to reach
# bl_ui_bargraph()/bl_ui_state_line() for its hardware-proof screens -- but
# with --gc-sections, only bl_ui.o actually gets pulled into that app; the
# receiver/flash/jump/ship/console objects are dead code there and dropped.
add_library(fwog_display_bl STATIC
    display_cpu/bootloader/bl_receiver.c
    display_cpu/bootloader/bl_flash.c
    display_cpu/bootloader/bl_jump.c
    display_cpu/bootloader/bl_policy.c
    display_cpu/bootloader/bl_ui.c
    display_cpu/bootloader/bl_ship.c
    display_cpu/bootloader/bl_console.c)
target_compile_options(fwog_display_bl PRIVATE ${FWOG_WARN_FLAGS})
target_link_libraries(fwog_display_bl PUBLIC
    fwog_display_bsp hardware_flash hardware_sync hardware_resets pico_bootrom)

# --- LVGL port (optional) ---
#
# LVGL IS NOT SHIPPED WITH THIS BSP. This library is the glue between LVGL and
# the ST7789 panel plus the five buttons; the consuming project supplies the
# library itself, exactly as it supplies the Pico SDK. That keeps LVGL off
# every app that does not want it, and keeps the LVGL version the app's
# choice rather than ours.
#
# The target only exists if `lvgl` does, and the check below is what turns
# "link fwog_display_lvgl and get an unhelpful missing-target error" into a
# message that says what to add. See display_cpu/lvgl/fwog_lvgl.h.
set(FWOG_LVGL_CONF "${CMAKE_CURRENT_LIST_DIR}/display_cpu/lvgl/lv_conf.h"
    CACHE FILEPATH "lv_conf.h tuned for this board; point LV_CONF_PATH here")

if(TARGET lvgl)
    add_library(fwog_display_lvgl STATIC display_cpu/lvgl/fwog_lvgl.c)
    target_compile_options(fwog_display_lvgl PRIVATE ${FWOG_WARN_FLAGS})
    target_link_libraries(fwog_display_lvgl PUBLIC fwog_display_bsp lvgl)
else()
    # Deliberately NO target and NO stub. An INTERFACE placeholder would link
    # cleanly and produce an app with no LVGL in it and nothing said about it;
    # a missing target is an error CMake reports by name. This message is what
    # tells the reader how to fix that error.
    message(STATUS
        "fwog: no `lvgl` target, so fwog_display_lvgl is not defined. Linking "
        "it will fail by name. To use it, add LVGL BEFORE this BSP:\n"
        "    set(LV_CONF_PATH \${FWOG_LVGL_CONF} CACHE STRING \"\" FORCE)\n"
        "    add_subdirectory(<your>/lvgl)\n"
        "  then add_subdirectory(<this bsp>/bsp). Developed against v9.2.2.")
endif()

# The embedded FPGA bitstream. Its size is asserted here rather than in C,
# because an extern array's length is not a compile-time constant -- and a
# truncated bitstream configures nothing while looking exactly like a wiring
# fault, which is a miserable thing to debug at a bench.
set(FWOG_FPGA_BIN ${CMAKE_CURRENT_SOURCE_DIR}/main_cpu/fpga/fpga_default_v5.bin)
file(SIZE ${FWOG_FPGA_BIN} _fwog_fpga_bin_size)
if(NOT _fwog_fpga_bin_size EQUAL 104090)
    message(FATAL_ERROR
        "fpga_default_v5.bin is ${_fwog_fpga_bin_size} bytes, expected 104090. "
        "Regenerate it with tools/fpga_header_to_bin.py, and update "
        "FWOG_FPGA_BITSTREAM_SIZE in main_cpu/fpga/fpga_bitstream.h if the "
        "gateware really did change.")
endif()

# --- main CPU ---
add_library(fwog_main_bsp STATIC
    main_cpu/platform/board.c
    main_cpu/watchdog/watchdog.c
    main_cpu/display_update/display_update.c
    main_cpu/radio/cc1101.c
    main_cpu/fpga/fpga_bitstream.S
    main_cpu/fpga/ice40.c
    main_cpu/fpga/fpga_clk.c
    main_cpu/fpga/io_dir.c
    main_cpu/gpio/breakout.c
    main_cpu/bottlenose/esp_rom_flash.c
    main_cpu/bottlenose/bn_flash.c
    common/i2c_bus.c
    common/link/link_uart.c)
pico_generate_pio_header(fwog_main_bsp
    ${CMAKE_CURRENT_SOURCE_DIR}/main_cpu/bottlenose/esp_rom_uart.pio)
target_compile_options(fwog_main_bsp PRIVATE ${FWOG_WARN_FLAGS})
target_include_directories(fwog_main_bsp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/main_cpu)
target_include_directories(fwog_main_bsp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/main_cpu/fpga)
set_source_files_properties(main_cpu/fpga/fpga_bitstream.S PROPERTIES
    OBJECT_DEPENDS ${FWOG_FPGA_BIN})
target_compile_definitions(fwog_main_bsp PUBLIC FWOG_CPU_MAIN=1)
target_link_libraries(fwog_main_bsp PUBLIC
    fwog_common hardware_i2c hardware_spi hardware_clocks
    hardware_vreg hardware_watchdog hardware_dma hardware_pio)

# --- main CPU filesystem (FatFs R0.15) ---
#
# A SEPARATE library, not part of fwog_main_bsp, for two reasons:
#
#   1. Size visibility. This is the first third-party dependency this BSP has
#      taken and it is not small -- ffunicode.c alone carries the CP932
#      code-page tables. Keeping it its own library makes that cost legible in
#      the map file, the same reasoning fwog_display_bl is kept separate.
#   2. Opt-in. An app that does not link this pays nothing for it. Nothing in
#      fwog_main_bsp references the filesystem, so a main app that has no use
#      for files does not carry FatFs.
#
# Apps that want files link fwog_main_fs IN ADDITION to fwog_main_bsp.
add_library(fwog_main_fs STATIC
    main_cpu/fs/fs_geom.c
    main_cpu/fs/fwog_fs.c
    main_cpu/fs/fs_flash_disk.c
    main_cpu/fs/ff/ff.c
    main_cpu/fs/ff/ffunicode.c
    main_cpu/fs/ff/ffsystem.c)
target_include_directories(fwog_main_fs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/main_cpu)
target_link_libraries(fwog_main_fs PUBLIC
    fwog_main_bsp hardware_flash hardware_sync)

# FWOG_WARN_FLAGS is deliberately NOT applied here: ff.c/ffunicode.c/
# ffsystem.c are vendored third-party sources, not warning-clean under
# -Wall -Wextra, and not ours to fix -- exactly the reasoning that keeps those
# flags off the SDK's own libraries at the top of this file. fs_geom.c,
# fwog_fs.c and fs_flash_disk.c ARE ours, so they get the flags.
set_source_files_properties(
    main_cpu/fs/ff/ff.c
    main_cpu/fs/ff/ffunicode.c
    main_cpu/fs/ff/ffsystem.c
    PROPERTIES COMPILE_OPTIONS "-w")
set_source_files_properties(
    main_cpu/fs/fs_geom.c
    main_cpu/fs/fwog_fs.c
    main_cpu/fs/fs_flash_disk.c
    PROPERTIES COMPILE_OPTIONS "${FWOG_WARN_FLAGS}")
