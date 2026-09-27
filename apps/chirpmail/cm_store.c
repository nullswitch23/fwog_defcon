/* FatFs /chirpmail/CANNED.TXT plus CDC SLOT/END from the host helper. */
#include "cm_store.h"
#include "fwog_main.h"
#include "fs/fwog_fs.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

#define CM_DIR  "/chirpmail"
#define CM_PATH "/chirpmail/CANNED.TXT"

static const char *const k_def[8] = {
    "MEET NOC STEPS NOW",
    "HORN HAND IS ME",
    "BADGE SWAP VILLAGE",
    "SHAKA IF YOU COPY",
    "WHITEHAT WAVE 2FL",
    "FOX BY THE FLAG",
    "ACK PIN CODE",
    "SAME UF2 FIND ME",
};

static char     s_slot[CM_NSLOT][CM_TEXT + 1u];
static bool     s_vol;
static char     s_line[48];
static unsigned s_llen;

static void sanitize(char *dst, const char *src, unsigned n) {
    unsigned i = 0u;
    if (!dst) return;
    while (src && src[i] && i < n && i < CM_TEXT) {
        char c = src[i];
        if (c < 0x20 || c > 0x7E) c = ' ';
        dst[i++] = c;
    }
    dst[i] = '\0';
}

static void defaults(void) {
    unsigned i;
    memset(s_slot, 0, sizeof s_slot);
    for (i = 0; i < 8u; i++) sanitize(s_slot[i], k_def[i], CM_TEXT);
}

static bool vol(void) {
    if (s_vol) return true;
    s_vol = fwog_fs_mount();
    return s_vol;
}

static void save_file(void) {
    char buf[CM_NSLOT * (CM_TEXT + 2u) + 8u];
    unsigned i, o = 0u;
    if (!vol()) return;
    if (!fwog_fs_exists(CM_DIR)) (void)fwog_fs_mkdir(CM_DIR);
    for (i = 0; i < CM_NSLOT; i++) {
        unsigned n = (unsigned)strlen(s_slot[i]);
        if (o + n + 2u >= sizeof buf) break;
        memcpy(buf + o, s_slot[i], n);
        o += n;
        buf[o++] = '\n';
    }
    if (!fwog_fs_open(CM_PATH, true, false)) {
        DIAG("[chirpmail] CANNED.TXT open fail\n");
        return;
    }
    (void)fwog_fs_write(buf, o);
    (void)fwog_fs_close();
}

static void load_file(void) {
    char buf[640];
    size_t n = sizeof buf;
    unsigned i = 0u, o = 0u;
    char line[CM_TEXT + 1u];
    unsigned ln = 0u;
    defaults();
    if (!vol() || !fwog_fs_exists(CM_PATH)) {
        save_file();
        return;
    }
    if (!fwog_fs_open(CM_PATH, false, false)) return;
    if (!fwog_fs_read(buf, &n)) n = 0;
    (void)fwog_fs_close();
    while (o < n && i < CM_NSLOT) {
        char c = buf[o++];
        if (c == '\r') continue;
        if (c == '\n') {
            line[ln] = '\0';
            if (line[0] != '#') {
                sanitize(s_slot[i], line, CM_TEXT);
                i++;
            }
            ln = 0u;
            continue;
        }
        if (ln < CM_TEXT) line[ln++] = c;
    }
    if (ln && i < CM_NSLOT && line[0] != '#') {
        line[ln] = '\0';
        sanitize(s_slot[i], line, CM_TEXT);
    }
}

static void send_slot(uint8_t i) {
    cm_slot_t m;
    memset(&m, 0, sizeof m);
    m.type = CM_MSG_SLOT;
    m.slot = i;
    m.n = (uint8_t)strlen(s_slot[i]);
    memcpy(m.text, s_slot[i], m.n);
    (void)fwog_link_uart_send_frame(&m, sizeof m);
}

void cm_store_push(void) {
    unsigned i;
    for (i = 0; i < CM_NSLOT; i++) {
        board_watchdog_kick();
        send_slot((uint8_t)i);
    }
}

void cm_store_init(void) {
    board_watchdog_kick();
    load_file();
    DIAG("[chirpmail] canned %s\n", s_vol ? CM_PATH : "RAM defaults");
    cm_store_push();
}

void cm_store_on_cmd(const cm_cmd_t *c) {
    uint8_t slot;
    if (!c) return;
    if (c->cmd == CM_CMD_PULL) {
        cm_store_push();
        return;
    }
    if (c->cmd != CM_CMD_SET) return;
    slot = c->seq;
    if (slot >= CM_NSLOT) return;
    {
        char tmp[CM_TEXT + 1u];
        unsigned n = c->n > CM_TEXT ? CM_TEXT : c->n;
        memset(tmp, 0, sizeof tmp);
        if (n) memcpy(tmp, c->text, n);
        sanitize(s_slot[slot], tmp, CM_TEXT);
    }
    save_file();
    send_slot(slot);
}

static void handle_cdc_line(char *line) {
    unsigned slot = 0u;
    char text[CM_TEXT + 1u];
    if (!line || !line[0]) return;
    if (strcmp(line, "CM1") == 0) {
        DIAG("CM1\n");
        return;
    }
    if (strcmp(line, "END") == 0) {
        save_file();
        cm_store_push();
        DIAG("[chirpmail] canned from CDC\n");
        return;
    }
    if (strcmp(line, "LIST") == 0) {
        unsigned i;
        DIAG("CM1\n");
        for (i = 0; i < CM_NSLOT; i++) {
            DIAG("SLOT %02u %s\n", i, s_slot[i]);
        }
        DIAG("END\n");
        return;
    }
    text[0] = '\0';
    if (sscanf(line, "SLOT %u %20[^\n]", &slot, text) < 1) {
        if (sscanf(line, "SLOT %u", &slot) != 1) return;
        text[0] = '\0';
    }
    if (slot >= CM_NSLOT) return;
    sanitize(s_slot[slot], text, CM_TEXT);
}

void cm_store_poll_cdc(void) {
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c == '\r') continue;
        if (c == '\n') {
            s_line[s_llen] = '\0';
            handle_cdc_line(s_line);
            s_llen = 0u;
            continue;
        }
        if (s_llen + 1u < sizeof s_line) s_line[s_llen++] = (char)c;
        else s_llen = 0u;
    }
}
