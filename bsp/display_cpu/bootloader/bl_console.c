#include "bootloader/bl_console.h"
#include <string.h>

void bl_line_init(bl_line_t *l) {
    l->len = 0u;
    l->overflow = false;
    l->buf[0] = '\0';
}

bool bl_line_feed(bl_line_t *l, char c) {
    if (c == '\r' || c == '\n') {
        if (l->overflow) {
            /* Discard, and rearm for the next line. Returning true with an
               empty buffer lets the caller print a prompt without the
               partial line ever being parsed. */
            bl_line_init(l);
            return true;
        }
        /* A CRLF pair would otherwise complete twice: the LF arrives with
           len already 0, which is indistinguishable from a real blank
           line. That is fine -- a blank line parses to BL_CMD_NONE and
           does nothing. */
        l->buf[l->len] = '\0';
        return true;
    }
    if (c == '\b' || c == 0x7Fu) {
        if (l->len > 0u) l->len--;
        return false;
    }
    if (l->len >= BL_LINE_MAX - 1u) {
        l->overflow = true;
        return false;
    }
    l->buf[l->len++] = c;
    return false;
}

static bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* Case-insensitive compare of [s, e) against a lowercase literal. */
static bool word_is(const char *s, const char *e, const char *lit) {
    size_t n = (size_t)(e - s);
    if (strlen(lit) != n) return false;
    for (size_t i = 0; i < n; i++) {
        if (lower(s[i]) != lit[i]) return false;
    }
    return true;
}

bl_cmd_t bl_console_parse(const char *line) {
    const char *s = line;
    while (*s && is_space(*s)) s++;
    if (*s == '\0') return BL_CMD_NONE;

    const char *e = s;
    while (*e && !is_space(*e)) e++;

    /* Anything after the verb makes it unknown. No command takes an
       argument, so "ship now" is a typo, and treating it as `ship` would
       power the board off on a mistake. */
    const char *rest = e;
    while (*rest && is_space(*rest)) rest++;
    if (*rest != '\0') return BL_CMD_UNKNOWN;

    if (word_is(s, e, "info"))    return BL_CMD_INFO;
    if (word_is(s, e, "bootsel")) return BL_CMD_BOOTSEL;
    if (word_is(s, e, "run"))     return BL_CMD_RUN;
    if (word_is(s, e, "ship"))    return BL_CMD_SHIP;
    if (word_is(s, e, "erase"))   return BL_CMD_ERASE;
    if (word_is(s, e, "help"))    return BL_CMD_HELP;
    if (word_is(s, e, "?"))       return BL_CMD_HELP;
    return BL_CMD_UNKNOWN;
}

#ifndef HOST_TEST
#include "bootloader/bl_flash.h"
#include "bootloader/bl_jump.h"
#include "bootloader/bl_ship.h"
#include "common/diag.h"
#include "pico/bootrom.h"
#include "pico/stdio.h"
#include <stdio.h>

static bl_line_t   s_line;
static const char *s_bl_version = "?";

static void print_help(void) {
    DIAG("commands:\n"
         "  info     bootloader and app-slot status\n"
         "  run      boot the app if valid\n"
         "  erase    invalidate the app metadata sector\n"
         "  bootsel  reboot into RPI-RP2 to replace THIS bootloader\n"
         "  ship     BATFET_DIS -- cuts battery power\n"
         "  help     this list\n");
}

static void print_info(void) {
    fwog_app_meta_t m;
    bool valid = bl_flash_read_meta(&m);
    DIAG("bootloader %s\n", s_bl_version);
    if (!valid) {
        DIAG("app: INVALID (magic %08x)\n", (unsigned)m.magic);
        return;
    }
    /* fwog_app_meta_valid() already confirmed version[] is terminated, so
       %s is safe here and only here. */
    DIAG("app: valid  size %u  crc %08x  built %u  version %s\n",
         (unsigned)m.size, (unsigned)m.crc32, (unsigned)m.build_ts, m.version);
    DIAG("app image: %s\n", bl_app_image_ok(&m) ? "vectors plausible"
                                                : "VECTORS BAD");
}

void bl_console_init(const char *bl_version) {
    if (bl_version) s_bl_version = bl_version;
    bl_line_init(&s_line);
    /* This is the call that actually enumerates USB. Nothing before this
       point in the bootloader touches the peripheral. */
    fwog_diag_init();
    DIAG("\n[fwog] display bootloader %s console\n", s_bl_version);
    print_help();
    DIAG("> ");
}

void bl_console_poll(void) {
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (!bl_line_feed(&s_line, (char)c)) continue;

        switch (bl_console_parse(s_line.buf)) {
        case BL_CMD_NONE:
            break;
        case BL_CMD_INFO:
            print_info();
            break;
        case BL_CMD_HELP:
            print_help();
            break;
        case BL_CMD_ERASE:
            DIAG(bl_flash_erase_meta() ? "app metadata erased\n"
                                       : "erase failed\n");
            break;
        case BL_CMD_RUN: {
            fwog_app_meta_t m;
            if (bl_flash_read_meta(&m) && bl_app_image_ok(&m)) {
                DIAG("booting app\n");
                bl_jump_to_app();          /* does not return */
            }
            DIAG("no valid app to run\n");
            break;
        }
        case BL_CMD_SHIP:
            DIAG("ship mode\n");
            stdio_flush();
            if (!bl_ship_mode()) DIAG("ship mode failed; still powered\n");
            break;
        case BL_CMD_BOOTSEL:
            DIAG("rebooting to RPI-RP2\n");
            stdio_flush();
            reset_usb_boot(0u, 0u);        /* does not return */
            break;
        case BL_CMD_UNKNOWN:
        default:
            DIAG("unknown command; try help\n");
            break;
        }
        bl_line_init(&s_line);
        DIAG("> ");
    }
}
#endif
