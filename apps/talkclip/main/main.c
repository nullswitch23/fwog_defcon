/* TalkClip main: 8 kHz PCM from the display lands in /talkclip/CLIPNNNN.RAW. */
#include "fwog_main.h"
#include "fs/fwog_fs.h"
#include "tc_proto.h"
#include "dg_file.h"
#include "dg_fs.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

FWOG_WATCHDOG_DEFAULT();

static fwog_link_rx_t s_rx;
static bool           s_clip;
static uint32_t       s_bytes;
static char           s_path[32];

static void send_ack(bool ok) {
    tc_ack_t a;
    memset(&a, 0, sizeof a);
    a.type = TC_MSG_ACK;
    a.ok = ok ? 1u : 0u;
    strncpy(a.path, s_path, TC_PATH_LEN - 1u);
    (void)fwog_link_uart_send_frame(&a, sizeof a);
}

static void clip_open(void) {
    dg_hdr_t h;
    unsigned idx;
    char name[20];
    if (s_clip) {
        (void)fwog_fs_close();
        s_clip = false;
    }
    if (!dg_fs_ensure_dir("/talkclip")) {
        DIAG("[talkclip] no /talkclip\n");
        s_path[0] = '\0';
        send_ack(false);
        return;
    }
    idx = dg_fs_next_index("/talkclip", "CLIP");
    dg_format_name(name, sizeof name, "CLIP", idx, "RAW");
    snprintf(s_path, sizeof s_path, "/talkclip/%s", name);
    if (!fwog_fs_open(s_path, true, false)) {
        DIAG("[talkclip] open %s fail\n", s_path);
        s_path[0] = '\0';
        send_ack(false);
        return;
    }
    dg_hdr_init(&h, DG_KIND_PCM16, "talkclip");
    h.sample_hz = 8000u;
    h.payload_bytes = 0;
    if (!fwog_fs_write(&h, sizeof h)) {
        (void)fwog_fs_close();
        DIAG("[talkclip] header write fail\n");
        send_ack(false);
        return;
    }
    s_clip = true;
    s_bytes = 0;
    DIAG("[talkclip] start %s\n", s_path);
    send_ack(true);
}

static void clip_pcm(const int16_t *pcm, uint16_t n) {
    if (!s_clip || !pcm || n == 0u) return;
    if (n > TC_PCM_N) n = TC_PCM_N;
    if (!fwog_fs_write(pcm, (size_t)n * sizeof(int16_t))) {
        DIAG("[talkclip] write fail after %u bytes\n", (unsigned)s_bytes);
        (void)fwog_fs_close();
        s_clip = false;
        send_ack(false);
        return;
    }
    s_bytes += (uint32_t)n * 2u;
}

static void clip_close(void) {
    dg_hdr_t h;
    if (!s_clip) return;
    dg_hdr_init(&h, DG_KIND_PCM16, "talkclip");
    h.sample_hz = 8000u;
    h.payload_bytes = s_bytes;
    h.duration_us = dg_pcm_duration_ms(s_bytes, 8000u) * 1000u;
    (void)fwog_fs_seek(0);
    (void)fwog_fs_write(&h, sizeof h);
    (void)fwog_fs_close();
    s_clip = false;
    DIAG("[talkclip] close %s bytes=%u\n", s_path, (unsigned)s_bytes);
    send_ack(true);
}

static bool stem_ok(const char *n) {
    unsigned i;
    if (!n || n[0] == '\0') return false;
    for (i = 0; n[i]; i++) {
        if (i >= 8u) return false;
        if (n[i] < 'A' || n[i] > 'Z') return false;
    }
    return true;
}

static void clip_rename(const char *from, const char *name) {
    char dest[32];
    char file[16];
    if (s_clip) {
        DIAG("[talkclip] rename while open\n");
        send_ack(false);
        return;
    }
    if (!from || from[0] == '\0' || !stem_ok(name)) {
        send_ack(false);
        return;
    }
    if (strncmp(from, "/talkclip/", 10) != 0) {
        send_ack(false);
        return;
    }
    snprintf(file, sizeof file, "%s.RAW", name);
    if (!dg_join_path(dest, sizeof dest, "/talkclip", file)) {
        send_ack(false);
        return;
    }
    if (fwog_fs_exists(dest)) {
        DIAG("[talkclip] rename exists %s\n", dest);
        send_ack(false);
        return;
    }
    if (!fwog_fs_rename(from, dest)) {
        DIAG("[talkclip] rename fail %s -> %s\n", from, dest);
        send_ack(false);
        return;
    }
    strncpy(s_path, dest, sizeof s_path - 1u);
    s_path[sizeof s_path - 1u] = '\0';
    DIAG("[talkclip] rename %s\n", s_path);
    send_ack(true);
}

int main(void) {
    board_init();
    const fwog_display_result_t d = fwog_display_update_run();
    DIAG("[talkclip] display: %s\n", fwog_display_result_text(d));
    DIAG("[talkclip] FatFs %s\n", dg_fs_ready() ? "mounted" : "no volume");
    fwog_link_rx_init(&s_rx);

    while (true) {
        board_watchdog_kick();
        uint8_t b;
        size_t n;
        while (fwog_link_uart_read(&b)) {
            if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
            if (n < 8u || s_rx.buf[0] != TC_MSG_CMD) continue;
            tc_pcm_t m;
            if (n > sizeof m) n = sizeof m;
            memcpy(&m, s_rx.buf, n);
            if (m.cmd == TC_CMD_RENAME) {
                tc_rename_t r;
                if (n < sizeof r) continue;
                memcpy(&r, s_rx.buf, sizeof r);
                r.from[TC_PATH_LEN - 1u] = '\0';
                r.name[TC_NAME_LEN - 1u] = '\0';
                clip_rename(r.from, r.name);
            } else if (m.cmd == TC_CMD_OPEN) {
                clip_open();
            } else if (m.cmd == TC_CMD_PCM) {
                clip_pcm(m.pcm, m.n);
            } else if (m.cmd == TC_CMD_CLOSE) {
                clip_close();
            }
        }
        sleep_ms(2);
    }
}
