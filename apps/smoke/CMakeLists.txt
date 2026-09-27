# Bare-board bring-up pair. Both halves must agree on SMOKE_BAUD, which is
# one reason they belong in one file.
add_executable(smoke_display display/main.c)
target_link_libraries(smoke_display fwog_display_bsp)

# DELIBERATELY NOT fwog_display_app(). This is the one display target that
# must NOT link at the bootloader's 0x10021000 app offset.
#
# smoke_display is a bare-board bring-up tool: it is flashed straight to a
# display CPU that may have no bootloader on it at all, which is exactly the
# situation it was written for. Linked at the app offset it would sit above
# an empty 0x10000000 and simply not boot -- and on a CPU with no reachable
# BOOTSEL button, an image that does not boot is the one thing that cannot be
# recovered from.
#
# THE COST, stated plainly: flashing this onto a board that DOES have the
# bootloader overwrites the bootloader. That is recoverable (this image
# carries its own escapes to BOOTSEL) but it means the bootloader has to be
# re-flashed afterwards. Use `fw bootloader` to put it back.
#
# Every OTHER display app must call fwog_display_app(). See the hardware record.
fwog_configure_stdio(smoke_display)     # REQUIRED: keeps stdio off UART0
pico_add_extra_outputs(smoke_display)   # .uf2 / .bin / .map

# fwog_usb_product() is orthogonal to the linker region above -- it only
# touches the USB descriptor, not where the image is linked -- so calling it
# directly does not reintroduce the app-offset problem this file exists to
# avoid. Without it, smoke_display would report the stock "Pico", and `fw
# bootsel --cpu display` would refuse to identify it: this is the bring-up
# tool for a CPU with no reachable BOOTSEL button, so that refusal would leave
# only `--port`, i.e. guessing which COM is the display.
fwog_usb_product(smoke_display display smoke 001)
fwog_usb_ids(smoke_display display)      # 093C:2055, so FwFinder can see it

# Same orthogonality argument: the UF2 info record is a const array in
# .rodata, so it does not touch where the image is linked either. Without it
# this would be the one FwOGapp a host tool could not describe -- and it is
# the bring-up image most likely to be found on an unknown board.
fwog_add_uf2_info(smoke_display display app smoke 001
    "Display-CPU smoke test: 200 MHz, USB CDC, link pins, button escape")
fwog_check_uf2_info(smoke_display display)

add_executable(smoke_main main/main.c)
target_link_libraries(smoke_main fwog_main_bsp)
fwog_main_app(smoke_main
    VERSION 001
    DESCRIPTION "Main-CPU smoke test: releases the display and sweeps 256 byte values over the link")

# Rebuild at the rate plan 2 wants once 115200 has passed:
#   cmake -DSMOKE_BAUD=12500000 ...
# Both halves take the same values, which is what keeps them in step.
foreach(_smoke_target smoke_display smoke_main)
    target_compile_definitions(${_smoke_target} PRIVATE
        $<$<BOOL:${SMOKE_BAUD}>:SMOKE_BAUD=${SMOKE_BAUD}>
        $<$<BOOL:${SMOKE_ESCAPE_MS}>:SMOKE_ESCAPE_MS=${SMOKE_ESCAPE_MS}>
    )
endforeach()
