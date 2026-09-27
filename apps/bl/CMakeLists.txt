# The display serial bootloader. Display-only, and not an application: it
# links at 0x10000000, not the app offset.
add_executable(bl_display display/main.c)
target_link_libraries(bl_display fwog_display_bl)
# Links at 0x10000000 with LENGTH capped at 128k, so outgrowing the reserve
# is a link error rather than a bootloader that overlaps its own metadata.
fwog_link_bootloader(bl_display
    VERSION 001
    DESCRIPTION "Display serial bootloader: verifies the app slot and takes images over the link")

target_compile_definitions(bl_display PRIVATE
    FWOG_BL_VERSION="${FWOG_GIT_DESCRIBE}")
