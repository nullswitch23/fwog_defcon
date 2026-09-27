# ============================================================================
#  cpuprobe - the one app in this tree that is NEITHER a display app NOR a
#  main app, because it runs on a CPU nobody has identified yet.
# ============================================================================
#
#  Read apps/cpuprobe/main.c's header before changing anything here. Every
#  deviation below from what apps/template/CMakeLists.txt does is
#  deliberate and is a safety property, not a shortcut.
#
#  1. It links `fwog_common` and NOT `fwog_main_bsp` / `fwog_display_bsp`.
#     A per-CPU BSP's board_init() brings up the inter-CPU link, the watchdog,
#     the display, the FPGA or the LCD, and drives that CPU's pins. Doing that
#     on the other processor is the exact accident this image exists to
#     prevent. `fwog_common` is the CPU-agnostic library; the only thing this
#     app pulls out of it is fwog_clocks_init() (bsp/common/clocks.c), which
#     touches VREG, the PLLs and CLOCKS and no GPIO at all. Everything else in
#     the archive is left unreferenced and never reaches the link.
#
#     What the app gets from the BSP that actually matters is the BOARD
#     HEADER, and that arrives through PICO_BOARD=freewili_og in the top-level
#     CMakeLists.txt rather than through any library: 16 MB QSPI flash,
#     PICO_FLASH_SPI_CLKDIV 4, boot2_w25q080, and clk_peri following clk_sys.
#     The previous incarnation of this image was built for PICO_BOARD=pico and
#     did not boot on this silicon.
#
#  2. It does NOT call fwog_main_app() or fwog_display_app(). Both would
#     declare a CPU this binary has no right to claim -- see 3 and 4 -- and
#     fwog_display_app() would additionally link it at the display
#     bootloader's app offset 0x10021000, where a UF2 copy cannot run it.
#     Instead it calls the pieces that ARE CPU-agnostic, one at a time.
#
#  3. It does NOT call fwog_usb_ids(). That function only accepts "display" or
#     "main" and would stamp 093C:2055 or 093C:2054 on the binary. fw.py and
#     freewili-finder identify a CPU BY THAT PID FIRST, so either choice would
#     be an authoritative-looking claim that is wrong half the time -- about
#     the single question this image was written to answer. The SDK's stock
#     2E8A:000A is left in place: it identifies NOTHING, which is the truth
#     here, and it is what fwOGappexplorer's prober flow already expects.
#
#  4. It does NOT call fwog_usb_product(). That helper composes
#     "FWOG <display|main> <name> <version>", and fw.py matches the CPU
#     prefixes "FWOG display " and "FWOG main ". "FWOG probe 002" matches
#     neither, so nothing can mistake this binary for a CPU-specific image --
#     while still reading as ours to a human in Device Manager. The 47-char
#     USBD_DESC_STR_MAX cap that fwog_usb_product() enforces is honoured here
#     by inspection: 14 and 11 characters.
#
#  5. It does NOT call fwog_add_uf2_info() / fwog_check_uf2_info(). The
#     record's `cpu` field admits only FWOG_UF2_CPU_DISPLAY or
#     FWOG_UF2_CPU_MAIN (bsp/common/uf2_info.h; check_uf2_info.py rejects
#     anything else), so there is no honest value for an image that is
#     deliberately neither. A record claiming one would be read by the App
#     Explorer and shown to a human as fact. Contract rule 6 is met in spirit
#     by fwOGappexplorer's own provenance gate over this image
#     (cmake/ProbeProvenance.cmake there), which is stronger: it hashes the
#     built .uf2 and this app's sources and refuses to build on drift.
#
#  6. It DOES cap its flash region at the display bootloader's 128 KB reserve.
#     Not because it is a display binary -- it links at 0x10000000, the base,
#     which is where a UF2 copy runs from on either CPU -- but so that "the
#     prober grew past 128 KB" becomes a link error instead of an image whose
#     UF2 blocks reach the display CPU's metadata sector at 0x10020000 and its
#     application slot at 0x10021000. Today it is ~24 KB (cpuprobe.bin is
#     24,800 bytes), so the cap is four-figure headroom rather than a squeeze.
#
#  7. It DOES call fwog_configure_stdio(), which is what keeps stdio off
#     UART0 -- the inter-CPU link on BOTH CPUs -- and what fails the build if
#     1200-baud BOOTSEL is ever configured away. On the DISPLAY CPU that touch
#     is the only way back, and this image may well be running on it.
#
#  8. It PINS the binary-info build date, so that this image is BYTE-
#     REPRODUCIBLE. Nothing else in the tree needs that; this image does,
#     because fwOGappexplorer gates its exact bytes at configure time, at
#     embed time and again at run time. See PICO_PROGRAM_BUILD_DATE below.
# ============================================================================

add_executable(cpuprobe main.c)

# fwog_common brings pico_stdlib, hardware_clocks, hardware_vreg and (at
# FWOG_DIAG=1) pico_stdio_usb. hardware_spi is this app's own need.
target_link_libraries(cpuprobe fwog_common hardware_spi)

# stdio: USB CDC on, UART off, 1200-baud BOOTSEL asserted still present.
fwog_configure_stdio(cpuprobe)

pico_set_program_name(cpuprobe "FWOG cpuprobe")
pico_set_program_description(cpuprobe
    "Reports 'main' or 'display' over USB CDC by probing for the CC1101 radio")
pico_set_program_version(cpuprobe "002")
pico_set_program_url(cpuprobe "https://github.com/intrepidcs/wiliOGBsp")

target_compile_definitions(cpuprobe PRIVATE
    # Note 8: THE BUILD DATE IS PINNED, so this image is byte-reproducible.
    #
    # The SDK stamps the wall-clock date into the binary-info block:
    # pico_standard_binary_info/standard_binary_info.c does
    #
    #     #if !PICO_NO_BI_PROGRAM_BUILD_DATE
    #     #ifndef PICO_PROGRAM_BUILD_DATE
    #     #define PICO_PROGRAM_BUILD_DATE __DATE__
    #     #endif
    #     bi_decl_with_attr(bi_program_build_date_string(...), ...);
    #
    # and that is the ONLY use of __DATE__ (or __TIME__, or __TIMESTAMP__) in
    # the whole SDK and the whole BSP. Left alone it makes this image differ by
    # one byte the instant the clock rolls past midnight -- observed exactly:
    # the same tree built on two consecutive days differed at offset 38433 and
    # nowhere else, '8' -> '9'.
    #
    # For every other app in this tree that is harmless. For THIS one it is
    # not. cpuprobe is the one image fwOGappexplorer writes to a CPU it has NOT
    # identified, so its exact bytes are gated three times over there:
    # cmake/ProbeProvenance.cmake at configure time, tools/embed_firmware.py at
    # embed time, and identifyCpus() at run time before a single byte reaches a
    # board. A date in the image makes all three fail every midnight for no
    # reason -- and a gate that cries wolf nightly is a gate people learn to
    # route around, which is the exact behaviour it exists to prevent.
    #
    # PINNED rather than dropped (PICO_NO_BI_PROGRAM_BUILD_DATE=1). Dropping it
    # would remove a field from `picotool info -a` and shorten the binary-info
    # pointer table; pinning keeps the field, keeps the table shape, and lets
    # the value say for itself that it is not a wall clock. Nothing else in the
    # program-info block is touched: the name, description, version "002" and
    # url set above, and the USB product string below, are what fw.py and the
    # App Explorer identify this image by, and they are untouched.
    #
    # Like USBD_PRODUCT below, this is PRIVATE but still reaches the SDK's
    # standard_binary_info.c, because pico_standard_binary_info is an INTERFACE
    # library whose sources compile straight into this target. That is also why
    # this affects cpuprobe ONLY and no other app in the tree.
    PICO_PROGRAM_BUILD_DATE="reproducible"

    # See notes 3 and 4 above. USBD_VID/USBD_PID are deliberately NOT set.
    #
    # These reach pico_stdio_usb's stdio_usb_descriptors.c even though they are
    # PRIVATE, because pico_stdio_usb is an INTERFACE library whose sources
    # compile straight into this target -- the same mechanism, and the same
    # caveat, as fwog_usb_product() in bsp/CMakeLists.txt.
    USBD_PRODUCT="FWOG probe 002"
    USBD_MANUFACTURER="${FWOG_USB_MANUFACTURER}"
    USBD_DESC_STR_MAX=48

    # The 1200-baud touch back into BOOTSEL. NOT optional: the DISPLAY CPU has
    # no BOOTSEL button, so without this a DISPLAY running this image could not
    # be recovered. The SDK default is already 1; stated explicitly so a future
    # edit cannot silently turn it off. fwog_configure_stdio() above also fails
    # the build if PICO_ENABLE_USB_RESET_VIA_BAUD_RATE is turned off globally.
    PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE=1
    PICO_STDIO_USB_RESET_MAGIC_BAUD_RATE=1200

    # picotool-driven reset, same mechanism, over the vendor interface.
    PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=1
    PICO_STDIO_USB_RESET_INTERFACE_SUPPORT_RESET_TO_BOOTSEL=1

    # Do not wait for a host to open the port before printing; the app may
    # connect at any time and must find output already flowing.
    PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=0

    # NOTE: PICO_STDIO_USB_RESET_BOOTSEL_ACTIVITY_LED is deliberately NOT
    # defined. Defining it would make the SDK drive a GPIO on the way into
    # BOOTSEL. See the safety rule in main.c.
)

target_compile_options(cpuprobe PRIVATE -Wall -Wextra)

# Note 6: a link-time ceiling, not a load offset. ORIGIN stays at the base.
fwog_flash_region(cpuprobe 0x10000000 "128k")

# .uf2 plus .bin/.hex/.map/.dis -- the .dis and .map are what the GPIO-29
# evidence is verified against.
pico_add_extra_outputs(cpuprobe)
