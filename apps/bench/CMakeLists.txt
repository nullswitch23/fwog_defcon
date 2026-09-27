# The bench-rig pair, driven by tools/bench.py (`--cpu display` or `--cpu main`).
add_executable(bench_display display/main.c)
target_link_libraries(bench_display fwog_display_bsp)

# Links at 0x10021000 and adds the stdio/extra-outputs wiring every display
# app needs. Without this the app would overwrite the bootloader when
# flashed, with no compile-time symptom (the hardware record).
fwog_display_app(bench_display
    VERSION 001
    DESCRIPTION "Bench console for the display drivers: charger, RTC, IMU, LEDs, mic, audio, IR")

add_executable(bench_main main/main.c)
# fwog_main_fs is opt-in and IN ADDITION to fwog_main_bsp -- see bsp/CMakeLists
# .txt. This app links it because the `fs` bench commands are the only way the
# filesystem gets exercised at all.
target_link_libraries(bench_main fwog_main_bsp fwog_main_fs)
# Carries the display firmware inside this UF2, exactly as template_main does.
# Without it, flashing this app would leave the display CPU with whatever it
# last had and no update path. Must come after target_link_libraries: the
# generated source needs fwog_main_bsp's include path.
fwog_embed_display_image(bench_main)
fwog_main_app(bench_main
    VERSION 001
    DESCRIPTION "Bench console for the main CPU: the two CC1101 sub-GHz radios on SPI0")
