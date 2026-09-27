# The LVGL example.
#
# LVGL is not vendored in this BSP (see bsp/display_cpu/lvgl/fwog_lvgl.h), so
# this pair is declared ONLY when a target named `lvgl` exists. Configure with
# -DFWOG_LVGL_FETCH=ON and the top-level CMakeLists.txt fetches v9.2.2 for
# you; supply your own `lvgl` target before this BSP and that works too.
#
# Skipped silently-but-loudly otherwise: a STATUS line, and the targets simply
# do not exist. Do NOT make this unconditional -- every other app in this tree
# builds with nothing but the Pico SDK, and that is worth keeping.
if(NOT TARGET fwog_display_lvgl)
    message(STATUS
        "fwog: skipping the apps/lvgl example -- no `lvgl` target. "
        "Configure with -DFWOG_LVGL_FETCH=ON to build it.")
    return()
endif()

add_executable(lvgl_display display/main.c)
target_link_libraries(lvgl_display fwog_display_lvgl)
fwog_display_app(lvgl_display
    VERSION 001
    DESCRIPTION "LVGL example: a button-driven list on the ST7789, with the keypad input device")

add_executable(lvgl_main main/main.c)
target_link_libraries(lvgl_main fwog_main_bsp)
# Carries lvgl_display -- the app in this folder.
fwog_embed_display_image(lvgl_main)
fwog_main_app(lvgl_main
    VERSION 001
    DESCRIPTION "Carries the LVGL example display firmware")
