/* The bootloader's USB CDC console.
 *
 * Enumerates only after ten seconds of main silence, so it is invisible
 * during normal operation and cannot be triggered by accident. When main is
 * silent, main cannot relay anything, which is why the console has to be
 * the display's own USB.
 *
 * `bootsel` is the point of the whole thing: it returns the display to
 * RPI-RP2 so the bootloader itself can be replaced without opening the
 * enclosure. It is the only path that closes that hole.
 *
 * The console deliberately does NOT accept firmware over itself. Image
 * transfer is main's job over the link; a second transfer path would be a
 * second thing to get wrong. */
#ifndef FWOG_BL_CONSOLE_H
#define FWOG_BL_CONSOLE_H
#include <stdbool.h>
#include <stdint.h>

#define BL_LINE_MAX 64u

typedef enum {
    BL_CMD_NONE = 0,   /* blank line: not an error, prints nothing */
    BL_CMD_INFO,
    BL_CMD_BOOTSEL,
    BL_CMD_RUN,
    BL_CMD_SHIP,
    BL_CMD_ERASE,
    BL_CMD_HELP,
    BL_CMD_UNKNOWN
} bl_cmd_t;

typedef struct {
    char    buf[BL_LINE_MAX];
    uint8_t len;
    bool    overflow;
} bl_line_t;

void bl_line_init(bl_line_t *l);

/* Feed one received character. Returns true when a line is complete, with
 * buf NUL-terminated.
 *
 * CR, LF, and CRLF all terminate exactly one line. Backspace deletes.
 * A line longer than BL_LINE_MAX-1 is DISCARDED rather than truncated: a
 * truncated "shipwreck" would parse as "ship", and "ship" cuts power. */
bool bl_line_feed(bl_line_t *l, char c);

/* Parse a completed line. Leading and trailing whitespace is ignored and
 * matching is case-insensitive. No command takes an argument, so anything
 * after the verb yields BL_CMD_UNKNOWN rather than being ignored. */
bl_cmd_t bl_console_parse(const char *line);

#ifndef HOST_TEST
/* Enumerate USB and print the banner. Call once, at the ten-second mark.
 * Until this runs the display's USB is dark -- pico_stdio_usb does not
 * touch the peripheral until stdio_usb_init(), so a healthy boot never
 * appears on the host at all. */
void bl_console_init(const char *bl_version);

/* Drain input and execute. Non-blocking; call from the main loop.
 * Does not return if the command was `run`, `bootsel`, or a successful
 * `ship`. */
void bl_console_poll(void);
#endif

#endif
