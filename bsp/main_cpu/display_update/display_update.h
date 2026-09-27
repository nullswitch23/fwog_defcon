/* Keeping the display CPU's firmware in step with main's, at every boot.
 *
 * Main can reset the display over GUI_NRESET, so main is always the one in
 * control of the sequence: hold the display in reset, bring the link up,
 * THEN release it. That ordering is what guarantees main catches the
 * bootloader's 500 ms announce window instead of racing it, and it is why
 * board_init() deliberately leaves GUI_NRESET asserted.
 *
 * A dead or absent display is never fatal. Main must come up regardless --
 * nothing but its own watchdog can reset main, so getting stuck here means
 * a board that needs its battery disconnected. */
#ifndef FWOG_DISPLAY_UPDATE_H
#define FWOG_DISPLAY_UPDATE_H
#include <stdbool.h>
#include "common/link/bl_proto.h"
#include "display_update/display_image.h"

typedef enum {
    FWOG_DISP_OK_RAN = 0,     /* HELLO matched the embedded image; RUN sent  */
    FWOG_DISP_OK_UPDATED,     /* reflashed and verified; RUN sent            */
    FWOG_DISP_NO_HELLO,       /* display silent -- absent, dead, or no BL    */
    FWOG_DISP_UPDATE_FAILED,  /* transfer or verify failed; retried next boot */
    FWOG_DISP_LINK_DOWN,      /* the configured baud is unreachable          */
    FWOG_DISP_IMAGE_BAD       /* the embedded image is not self-consistent   */
} fwog_display_result_t;

/* Never NULL, including for an out-of-range value. */
const char *fwog_display_result_text(fwog_display_result_t r);

/* Whether the display's app slot differs from the image main carries.
 *
 * The trigger is the image CRC32, never a version comparison: it is exact
 * and needs no version discipline, so a rebuilt dev image deploys
 * automatically and a downgrade works. The version string is for humans.
 *
 * Returns false for a protocol version we do not speak -- streaming an
 * image at a bootloader that may lay it out differently is worse than
 * leaving it alone -- and false when there is no image to send. */
bool fwog_display_update_needed(const fwog_bl_hello_t *h,
                                const fwog_display_image_info_t *info);

#ifndef HOST_TEST
/* Put a line of text, and optionally a progress bar, on the display's
 * bootloader screen. `percent` is 0-100, or FWOG_BL_STATUS_NO_BAR for text
 * only. Text longer than FWOG_BL_STATUS_TEXT_LEN-1 is truncated.
 *
 * Only reaches a display that is still IN the bootloader -- once the app is
 * running, the bootloader is gone and this goes nowhere. So it is useful
 * during an update, and while main is deliberately holding the display in
 * the bootloader. Requires fwog_link_uart_init() to have run.
 *
 * Public because main knows things the display cannot: which of several
 * steps it is on, what it is waiting for, why it has not released the
 * display yet. Returns false only if the frame could not be encoded. */
bool fwog_display_status(const char *text, uint8_t percent);

/* The whole boot-time sequence, against the embedded image. Call once from
 * main(), after board_init(). Replaces the direct board_release_display()
 * call that main-CPU applications used to make -- this function performs
 * the release itself, at the right moment.
 *
 * The watchdog stays enabled throughout: the update widens the window with
 * board_watchdog_pause() and kicks from inside every wait loop, because
 * the failure this protects against is precisely a hang partway through a
 * long operation. */
fwog_display_result_t fwog_display_update_run(void);
#endif

#endif
