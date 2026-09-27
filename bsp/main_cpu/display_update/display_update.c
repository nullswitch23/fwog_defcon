#include "display_update/display_update.h"

const char *fwog_display_result_text(fwog_display_result_t r) {
    switch (r) {
    case FWOG_DISP_OK_RAN:        return "app up to date";
    case FWOG_DISP_OK_UPDATED:    return "app updated";
    case FWOG_DISP_NO_HELLO:      return "no display";
    case FWOG_DISP_UPDATE_FAILED: return "update failed";
    case FWOG_DISP_LINK_DOWN:     return "link down";
    case FWOG_DISP_IMAGE_BAD:     return "embedded image bad";
    default:                      return "?";
    }
}

bool fwog_display_update_needed(const fwog_bl_hello_t *h,
                                const fwog_display_image_info_t *info) {
    if (h->proto_ver != FWOG_BL_PROTO_VER) return false;
    if (info->size == 0u) return false;
    if (!h->app_valid) return true;
    return h->app_crc32 != info->crc32 || h->app_size != info->size;
}

#ifndef HOST_TEST
#include "common/diag.h"
#include "common/link/link_uart.h"
#include "platform/board.h"
#include "watchdog/watchdog.h"
#include "pico/stdlib.h"
#include <string.h>

/* Generous, because the display's erase of a 500 KB slot is roughly a
 * second and its whole-image verify adds more. The watchdog window is
 * widened around all of it. */
#define HELLO_WAIT_MS   1000u
#define READY_WAIT_MS   15000u
#define ACK_WAIT_MS     200u
#define RESULT_WAIT_MS  15000u

/* .bss: together these are about 8.3 KB, which is a lot of stack and
 * nothing at all out of 264 KB of RAM. */
static fwog_link_rx_t s_rx;
static uint8_t s_payload[sizeof(fwog_bl_data_hdr_t) + FWOG_BL_CHUNK];

/* Wait for one well-formed bootloader message of any type. Returns its
 * type, or 0 on timeout; the payload is left in s_rx.buf. */
static uint8_t wait_msg(uint32_t timeout_ms, size_t *out_len) {
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (!time_reached(deadline)) {
        board_watchdog_kick();
        uint8_t b;
        while (fwog_link_uart_read(&b)) {
            size_t n = 0;
            if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
            uint8_t t = fwog_bl_msg_type(s_rx.buf, n);
            if (t != 0u) { *out_len = n; return t; }
        }
    }
    return 0u;
}

static bool send_begin(const fwog_display_image_info_t *info) {
    fwog_bl_update_begin_t ub;
    memset(&ub, 0, sizeof ub);
    ub.type     = FWOG_BL_MSG_UPDATE_BEGIN;
    ub.size     = info->size;
    ub.crc32    = info->crc32;
    ub.build_ts = info->build_ts;
    memcpy(ub.version, info->version, sizeof ub.version);
    return fwog_link_uart_send_frame(&ub, sizeof ub);
}

static bool send_data(uint32_t off, const uint8_t *src, uint32_t len) {
    fwog_bl_data_hdr_t h;
    memset(&h, 0, sizeof h);
    h.type   = FWOG_BL_MSG_DATA;
    h.offset = off;
    h.len    = len;
    memcpy(s_payload, &h, sizeof h);
    memcpy(s_payload + sizeof h, src, len);   /* src is XIP; reading is fine */
    return fwog_link_uart_send_frame(s_payload, sizeof h + len);
}

bool fwog_display_status(const char *text, uint8_t percent) {
    uint8_t payload[sizeof(fwog_bl_status_t)];
    size_t n = fwog_bl_encode_status(payload, text, percent);
    return fwog_link_uart_send_frame(payload, n);
}

static fwog_display_result_t do_update(const fwog_display_image_info_t *info) {
    size_t n = 0;

    /* Sent BEFORE UPDATE_BEGIN, not after: the display begins erasing the
       instant it receives UPDATE_BEGIN and cannot service the link again
       until that finishes, which is the longest stretch of an update with
       nothing on screen. This is the one status line whose ordering has to
       be reasoned about rather than just appended. */
    (void)fwog_display_status("erasing", FWOG_BL_STATUS_NO_BAR);

    if (!send_begin(info)) return FWOG_DISP_UPDATE_FAILED;
    if (wait_msg(READY_WAIT_MS, &n) != FWOG_BL_MSG_READY) {
        DIAG("[disp] no READY within %u ms\n", (unsigned)READY_WAIT_MS);
        return FWOG_DISP_UPDATE_FAILED;
    }

    /* A global attempt budget, not just a per-chunk retry count: a display
       that NAKs the same offset forever must not hold main here. Four
       attempts per chunk plus slack. */
    uint32_t chunks = (info->size + FWOG_BL_CHUNK - 1u) / FWOG_BL_CHUNK;
    uint32_t budget = chunks * 4u + 16u;

    uint32_t off = 0u;
    while (off < info->size) {
        if (budget == 0u) {
            DIAG("[disp] retry budget exhausted at offset %u\n", (unsigned)off);
            return FWOG_DISP_UPDATE_FAILED;
        }
        budget--;

        uint32_t len = info->size - off;
        if (len > FWOG_BL_CHUNK) len = FWOG_BL_CHUNK;
        if (!send_data(off, fwog_display_image + off, len)) {
            return FWOG_DISP_UPDATE_FAILED;
        }

        uint8_t t = wait_msg(ACK_WAIT_MS, &n);
        if (t == FWOG_BL_MSG_ACK) {
            fwog_bl_ack_t a;
            memcpy(&a, s_rx.buf, sizeof a);
            if (a.offset == off) off += len;
            /* An ACK for some other offset is stale. Resend this one. */
        } else if (t == FWOG_BL_MSG_NAK) {
            fwog_bl_ack_t a;
            memcpy(&a, s_rx.buf, sizeof a);
            /* The display names its own resume point, so a gap costs one
               round trip rather than restarting the transfer. Bounds-check
               it anyway: it arrived over a wire. */
            if (a.offset < info->size) off = a.offset;
        } else if (t == FWOG_BL_MSG_RESULT) {
            /* The bootloader gave up mid-transfer -- a flash failure. */
            DIAG("[disp] display aborted the transfer at %u\n", (unsigned)off);
            return FWOG_DISP_UPDATE_FAILED;
        }
        /* Timeout or anything else: fall through and resend. */
    }

    /* Same reasoning as "erasing": UPDATE_END starts a whole-image CRC pass
       the display cannot narrate while it runs. */
    (void)fwog_display_status("verifying", FWOG_BL_STATUS_NO_BAR);

    uint8_t end[1];
    fwog_bl_encode_bare(end, FWOG_BL_MSG_UPDATE_END);
    if (!fwog_link_uart_send_frame(end, 1u)) return FWOG_DISP_UPDATE_FAILED;

    if (wait_msg(RESULT_WAIT_MS, &n) != FWOG_BL_MSG_RESULT) {
        DIAG("[disp] no RESULT within %u ms\n", (unsigned)RESULT_WAIT_MS);
        return FWOG_DISP_UPDATE_FAILED;
    }
    fwog_bl_result_t res;
    memcpy(&res, s_rx.buf, sizeof res);
    if (!res.ok) {
        DIAG("[disp] image rejected: display computed %08x, expected %08x\n",
             (unsigned)res.computed_crc32, (unsigned)info->crc32);
        return FWOG_DISP_UPDATE_FAILED;
    }
    return FWOG_DISP_OK_UPDATED;
}

fwog_display_result_t fwog_display_update_run(void) {
    const fwog_display_image_info_t *info = &fwog_display_image_info;

    /* The .incbin'd bytes and the generated identity record must agree. A
       stale .bin would otherwise surface only as a CRC rejection, seconds
       into a transfer that was never going to succeed. */
    uint32_t linked = (uint32_t)(fwog_display_image_end - fwog_display_image);
    if (info->size == 0u || linked != info->size) {
        DIAG("[disp] embedded image is %u bytes, record says %u\n",
             (unsigned)linked, (unsigned)info->size);
        return FWOG_DISP_IMAGE_BAD;
    }
    if (!fwog_str_bounded(info->version, FWOG_APP_VERSION_LEN)) {
        DIAG("[disp] embedded version string is not terminated\n");
        return FWOG_DISP_IMAGE_BAD;
    }

    if (!fwog_link_uart_init(FWOG_LINK_BAUD)) return FWOG_DISP_LINK_DOWN;
    fwog_link_rx_init(&s_rx);

    /* board_init() left GUI_NRESET asserted. Releasing it only now is what
       guarantees we catch the announce window rather than racing it. */
    board_release_display();

    size_t n = 0;
    if (wait_msg(HELLO_WAIT_MS, &n) != FWOG_BL_MSG_HELLO) {
        DIAG("[disp] no HELLO in %u ms; continuing without the display\n",
             (unsigned)HELLO_WAIT_MS);
        return FWOG_DISP_NO_HELLO;
    }
    fwog_bl_hello_t h;
    memcpy(&h, s_rx.buf, sizeof h);
    /* fwog_bl_msg_type() confirmed both strings are terminated. */
    DIAG("[disp] hello: bl %s, app %s valid=%u size=%u crc=%08x\n",
         h.bl_version, h.version, (unsigned)h.app_valid,
         (unsigned)h.app_size, (unsigned)h.app_crc32);

    bool updated = false;
    if (fwog_display_update_needed(&h, info)) {
        DIAG("[disp] updating: %u bytes, crc %08x, version %s\n",
             (unsigned)info->size, (unsigned)info->crc32, info->version);
        /* Widen the window rather than disabling the watchdog: the failure
           being guarded against is a hang partway through exactly this. */
        board_watchdog_pause();
        fwog_display_result_t r = do_update(info);
        board_watchdog_resume();
        if (r != FWOG_DISP_OK_UPDATED) return r;
        updated = true;
    }

    uint8_t run[1];
    fwog_bl_encode_bare(run, FWOG_BL_MSG_RUN);
    fwog_link_uart_send_frame(run, 1u);
    return updated ? FWOG_DISP_OK_UPDATED : FWOG_DISP_OK_RAN;
}
#endif
