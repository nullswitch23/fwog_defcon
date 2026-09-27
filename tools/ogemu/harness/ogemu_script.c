#include "ogemu.h"
#include "input/buttons.h"
#include "common/diag.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#define OGEMU_Q        256
#define OGEMU_ARG_MAX  256

typedef enum {
    CMD_TICK = 0,
    CMD_PRESS,
    CMD_RELEASE,
    CMD_DUMP,
    CMD_EXPECT,
    CMD_QUIT,
    CMD_ACCEL,
    CMD_MIC,
    CMD_RSSI,
    CMD_OOK,
    CMD_SHAKE,
    CMD_RF,
    CMD_LINK,
    CMD_MIC_TONE,
    CMD_BS,
    CMD_IB,
    CMD_TF,
    CMD_TE,
    CMD_FOB,
    CMD_TRF,
    CMD_PH,
    CMD_BD,
    CMD_LF,
    CMD_BB,
    CMD_HK,
    CMD_QG,
    CMD_RG,
    CMD_AM,
    CMD_DG_VOL,
    CMD_DG_LIST,
    CMD_DG_META
} ogemu_cmd_kind_t;

typedef struct {
    ogemu_cmd_kind_t kind;
    fwog_btn_id_t    btn;
    uint32_t         ms;
    int32_t          a, b, c, d, e;
    char             arg[OGEMU_ARG_MAX];
} ogemu_cmd_t;

static ogemu_cmd_t s_q[OGEMU_Q];
static unsigned s_head, s_tail;
static uint32_t s_tick_left;
static int s_quit = -1;
static char s_dump_png[OGEMU_ARG_MAX];
static char s_dump_txt[OGEMU_ARG_MAX];
static char s_script_dir[OGEMU_ARG_MAX];
static bool s_dumped;
static bool s_live;
#ifdef _WIN32
static CRITICAL_SECTION s_lock;
static int s_lock_ok;
#else
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

static void lock_init(void) {
#ifdef _WIN32
    if (!s_lock_ok) {
        InitializeCriticalSection(&s_lock);
        s_lock_ok = 1;
    }
#endif
}

static void qlock(void) {
    lock_init();
#ifdef _WIN32
    EnterCriticalSection(&s_lock);
#else
    pthread_mutex_lock(&s_lock);
#endif
}

static void qunlock(void) {
#ifdef _WIN32
    LeaveCriticalSection(&s_lock);
#else
    pthread_mutex_unlock(&s_lock);
#endif
}

void ogemu_set_live(bool on) { s_live = on; }
bool ogemu_live(void) { return s_live; }

static int parse_btn(const char *s, fwog_btn_id_t *out) {
    if (!s || !s[0]) return -1;
    char buf[16];
    unsigned i = 0u;
    for (; s[i] && i + 1u < sizeof buf; i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        buf[i] = c;
    }
    buf[i] = '\0';
    if (!strcmp(buf, "green")  || !strcmp(buf, "g") || !strcmp(buf, "enter"))
        { *out = FWOG_BTN_GREEN;  return 0; }
    if (!strcmp(buf, "yellow") || !strcmp(buf, "y") || !strcmp(buf, "left"))
        { *out = FWOG_BTN_YELLOW; return 0; }
    if (!strcmp(buf, "blue")   || !strcmp(buf, "b") || !strcmp(buf, "right"))
        { *out = FWOG_BTN_BLUE;   return 0; }
    if (!strcmp(buf, "gray") || !strcmp(buf, "grey") || !strcmp(buf, "h") ||
        !strcmp(buf, "up"))
        { *out = FWOG_BTN_GRAY;   return 0; }
    if (!strcmp(buf, "red")    || !strcmp(buf, "r") || !strcmp(buf, "down"))
        { *out = FWOG_BTN_RED;    return 0; }
    return -1;
}

static const char *json_after(const char *line, const char *key) {
    char quoted[40];
    snprintf(quoted, sizeof quoted, "\"%s\"", key);
    const char *p = strstr(line, quoted);
    if (!p) return NULL;
    p += strlen(quoted);
    while (*p == ' ' || *p == '\t' || *p == ':') p++;
    return p;
}

static int json_str(const char *line, const char *key, char *out, size_t cap) {
    const char *p = json_after(line, key);
    if (!p || *p != '"') return -1;
    p++;
    size_t n = 0u;
    while (*p && *p != '"' && n + 1u < cap) out[n++] = *p++;
    out[n] = '\0';
    return 0;
}

static int json_u32(const char *line, const char *key, uint32_t *out) {
    const char *p = json_after(line, key);
    if (!p) return -1;
    char *end = NULL;
    unsigned long v = strtoul(p, &end, 10);
    if (end == p) return -1;
    *out = (uint32_t)v;
    return 0;
}

static int json_i32(const char *line, const char *key, int32_t *out) {
    const char *p = json_after(line, key);
    if (!p) return -1;
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p) return -1;
    *out = (int32_t)v;
    return 0;
}

static int enqueue(ogemu_cmd_t c) {
    qlock();
    unsigned next = (s_tail + 1u) % OGEMU_Q;
    if (next == s_head) s_head = (s_head + 1u) % OGEMU_Q;
    s_q[s_tail] = c;
    s_tail = next;
    qunlock();
    return 0;
}

static int push_cmd(ogemu_cmd_kind_t kind, fwog_btn_id_t btn, uint32_t ms,
                    const char *arg) {
    ogemu_cmd_t c;
    memset(&c, 0, sizeof c);
    c.kind = kind;
    c.btn = btn;
    c.ms = ms;
    if (arg) {
        strncpy(c.arg, arg, OGEMU_ARG_MAX - 1u);
        c.arg[OGEMU_ARG_MAX - 1u] = '\0';
    }
    return enqueue(c);
}

static int parse_json_line(const char *line) {
    char cmd[32] = {0};
    if (json_str(line, "cmd", cmd, sizeof cmd) != 0 &&
        json_str(line, "op", cmd, sizeof cmd) != 0) {
        fprintf(stderr, "ogemu: JSON line missing cmd: %s\n", line);
        return -1;
    }
    char btn_s[16] = {0};
    char path[OGEMU_ARG_MAX] = {0};
    uint32_t ms = 0u;
    fwog_btn_id_t btn = FWOG_BTN_GREEN;
    (void)json_str(line, "btn", btn_s, sizeof btn_s);
    (void)json_str(line, "button", btn_s, sizeof btn_s);
    (void)json_u32(line, "ms", &ms);
    (void)json_str(line, "path", path, sizeof path);
    if (!path[0]) (void)json_str(line, "dump", path, sizeof path);
    if (!path[0]) (void)json_str(line, "file", path, sizeof path);
    char contains[OGEMU_ARG_MAX] = {0};
    (void)json_str(line, "contains", contains, sizeof contains);
    if (!contains[0]) (void)json_str(line, "text", contains, sizeof contains);

    if (!strcmp(cmd, "tick"))
        return push_cmd(CMD_TICK, btn, ms ? ms : 16u, NULL);
    if (!strcmp(cmd, "press")) {
        if (parse_btn(btn_s, &btn) != 0) {
            fprintf(stderr, "ogemu: bad btn in %s\n", line);
            return -1;
        }
        return push_cmd(CMD_PRESS, btn, 0u, NULL);
    }
    if (!strcmp(cmd, "release")) {
        if (parse_btn(btn_s, &btn) != 0) {
            fprintf(stderr, "ogemu: bad btn in %s\n", line);
            return -1;
        }
        return push_cmd(CMD_RELEASE, btn, 0u, NULL);
    }
    if (!strcmp(cmd, "hold")) {
        if (parse_btn(btn_s, &btn) != 0) {
            fprintf(stderr, "ogemu: bad btn in %s\n", line);
            return -1;
        }
        if (!ms) ms = 6000u;
        if (push_cmd(CMD_PRESS, btn, 0u, NULL) != 0) return -1;
        return push_cmd(CMD_TICK, btn, FWOG_BTN_DEBOUNCE_MS + ms, NULL);
    }
    if (!strcmp(cmd, "dump"))
        return push_cmd(CMD_DUMP, btn, 0u, path[0] ? path : NULL);
    if (!strcmp(cmd, "expect_text") || !strcmp(cmd, "expect"))
        return push_cmd(CMD_EXPECT, btn, 0u, contains);
    if (!strcmp(cmd, "quit") || !strcmp(cmd, "exit"))
        return push_cmd(CMD_QUIT, btn, 0u, NULL);
    if (!strcmp(cmd, "accel")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_ACCEL;
        (void)json_i32(line, "x", &c.a);
        (void)json_i32(line, "y", &c.b);
        if (json_i32(line, "z", &c.c) != 0) c.c = 1000;
        return enqueue(c);
    }
    if (!strcmp(cmd, "shake")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_SHAKE;
        c.ms = ms ? ms : 200u;
        return enqueue(c);
    }
    if (!strcmp(cmd, "mic") || !strcmp(cmd, "mic_rms")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_MIC;
        uint32_t rms = 0u;
        if (json_u32(line, "rms", &rms) != 0) (void)json_u32(line, "n", &rms);
        c.ms = rms;
        return enqueue(c);
    }
    if (!strcmp(cmd, "rssi")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_RSSI;
        int32_t dbm = -120;
        (void)json_i32(line, "dbm", &dbm);
        (void)json_i32(line, "rssi", &dbm);
        c.a = dbm;
        uint32_t hz = 0u;
        (void)json_u32(line, "hz", &hz);
        c.c = (int32_t)hz;
        return enqueue(c);
    }
    if (!strcmp(cmd, "ook")) {
        if (!path[0]) {
            fprintf(stderr, "ogemu: ook needs file=\n");
            return -1;
        }
        return push_cmd(CMD_OOK, btn, 0u, path);
    }
    if (!strcmp(cmd, "rf") || !strcmp(cmd, "relic")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_RF;
        int32_t dbm = -72;
        (void)json_i32(line, "dbm", &dbm);
        (void)json_i32(line, "rssi", &dbm);
        c.a = dbm;
        int32_t art = 0;
        (void)json_i32(line, "art", &art);
        c.b = art;
        uint32_t hz = 433920000u;
        (void)json_u32(line, "hz", &hz);
        c.c = (int32_t)hz;
        return enqueue(c);
    }
    if (!strcmp(cmd, "link")) {
        char hex[OGEMU_ARG_MAX] = {0};
        if (json_str(line, "hex", hex, sizeof hex) != 0 && !path[0]) {
            fprintf(stderr, "ogemu: link needs hex=\n");
            return -1;
        }
        return push_cmd(CMD_LINK, btn, 0u, hex[0] ? hex : path);
    }
    if (!strcmp(cmd, "mic_tone")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_MIC_TONE;
        uint32_t hz = 1000u, rms = 9000u;
        (void)json_u32(line, "hz", &hz);
        (void)json_u32(line, "rms", &rms);
        c.a = (int32_t)hz;
        c.ms = rms;
        return enqueue(c);
    }
    if (!strcmp(cmd, "bs_status") || !strcmp(cmd, "bs")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_BS;
        int32_t band = 1, mode = 0, peak_dbm = -48, hit_dbm = 0;
        uint32_t peak_hz = 433920000u;
        (void)json_i32(line, "band", &band);
        (void)json_i32(line, "mode", &mode);
        (void)json_i32(line, "peak_dbm", &peak_dbm);
        (void)json_i32(line, "dbm", &peak_dbm);
        (void)json_u32(line, "peak_hz", &peak_hz);
        (void)json_u32(line, "hz", &peak_hz);
        (void)json_i32(line, "hit_dbm", &hit_dbm);
        c.a = band;
        c.b = peak_dbm;
        c.c = (int32_t)peak_hz;
        c.ms = (uint32_t)mode;
        c.d = hit_dbm;
        return enqueue(c);
    }
    if (!strcmp(cmd, "ib_status") || !strcmp(cmd, "ib")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_IB;
        int32_t state = 3, rssi = -62, peak = -40, guess = 9;
        (void)json_i32(line, "state", &state);
        (void)json_i32(line, "rssi", &rssi);
        (void)json_i32(line, "dbm", &rssi);
        (void)json_i32(line, "peak_rssi", &peak);
        (void)json_i32(line, "guess", &guess);
        c.a = rssi;
        c.b = peak;
        c.c = guess;
        c.ms = (uint32_t)state;
        char label[OGEMU_ARG_MAX] = {0};
        char hex[80] = {0};
        (void)json_str(line, "label", label, sizeof label);
        (void)json_str(line, "hex", hex, sizeof hex);
        if (!label[0]) strncpy(label, "owned fob", sizeof label - 1u);
        snprintf(c.arg, sizeof c.arg, "%s|%s", label, hex);
        return enqueue(c);
    }
    if (!strcmp(cmd, "tf_status") || !strcmp(cmd, "tf")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_TF;
        int32_t r0 = -80, r1 = -42;
        uint32_t hz = 433920000u;
        (void)json_i32(line, "rssi0", &r0);
        (void)json_i32(line, "rssi1", &r1);
        (void)json_u32(line, "hz", &hz);
        c.a = r0;
        c.b = r1;
        c.c = (int32_t)hz;
        return enqueue(c);
    }
    if (!strcmp(cmd, "te_status") || !strcmp(cmd, "te")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_TE;
        int32_t rssi = -55, last = -40, bursts = 3, inb = 1, preset = 1;
        uint32_t last_ms = 8u;
        (void)json_i32(line, "rssi", &rssi);
        (void)json_i32(line, "last_rssi", &last);
        (void)json_i32(line, "bursts", &bursts);
        (void)json_i32(line, "in_burst", &inb);
        (void)json_i32(line, "preset", &preset);
        (void)json_u32(line, "last_ms", &last_ms);
        c.a = rssi;
        c.b = last;
        c.c = bursts;
        c.d = inb;
        c.e = preset;
        c.ms = last_ms;
        return enqueue(c);
    }
    if (!strcmp(cmd, "fob_status") || !strcmp(cmd, "fob")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_FOB;
        int32_t state = 4, rssi = -70, slots = 2, unused = 2, edges = 80;
        (void)json_i32(line, "state", &state);
        (void)json_i32(line, "rssi", &rssi);
        (void)json_i32(line, "slots", &slots);
        (void)json_i32(line, "unused", &unused);
        (void)json_i32(line, "edges", &edges);
        c.a = state;
        c.b = rssi;
        c.c = slots;
        c.d = unused;
        c.e = edges;
        return enqueue(c);
    }
    if (!strcmp(cmd, "trf_status") || !strcmp(cmd, "trf")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_TRF;
        int32_t rssi0 = -127;
        int32_t rssi1 = -127;
        uint32_t hz0 = 315000000u;
        uint32_t hz1 = 433920000u;
        (void)json_i32(line, "rssi", &rssi1); /* legacy single-strip → bottom */
        (void)json_i32(line, "rssi0", &rssi0);
        (void)json_i32(line, "rssi1", &rssi1);
        (void)json_u32(line, "hz", &hz1);
        (void)json_u32(line, "hz0", &hz0);
        (void)json_u32(line, "hz1", &hz1);
        c.a = rssi0;
        c.b = rssi1;
        c.c = (int32_t)hz0;
        c.d = (int32_t)hz1;
        return enqueue(c);
    }
    if (!strcmp(cmd, "ph_status") || !strcmp(cmd, "ph")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_PH;
        int32_t hello = 1, scan = 1, rssi = -42, apple = 1;
        (void)json_i32(line, "hello", &hello);
        (void)json_i32(line, "scan", &scan);
        (void)json_i32(line, "rssi", &rssi);
        (void)json_i32(line, "apple", &apple);
        c.a = hello;
        c.b = scan;
        c.c = rssi;
        c.d = apple;
        (void)json_str(line, "name", c.arg, sizeof c.arg);
        if (!c.arg[0]) strncpy(c.arg, "AirTag", sizeof c.arg - 1u);
        return enqueue(c);
    }
    if (!strcmp(cmd, "am_status") || !strcmp(cmd, "am")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_AM;
        int32_t hello = 0, wp_on = 0, sweep = 0, armed = 0, mode = 0;
        int32_t flash = 0, pct = 0, channel = 6, aps = 0, tx = 0;
        (void)json_i32(line, "hello", &hello);
        (void)json_i32(line, "wp_on", &wp_on);
        (void)json_i32(line, "on", &wp_on);
        (void)json_i32(line, "sweep", &sweep);
        (void)json_i32(line, "armed", &armed);
        (void)json_i32(line, "mode", &mode);
        (void)json_i32(line, "flash", &flash);
        (void)json_i32(line, "pct", &pct);
        (void)json_i32(line, "channel", &channel);
        (void)json_i32(line, "ch", &channel);
        (void)json_i32(line, "aps", &aps);
        (void)json_i32(line, "tx", &tx);
        c.a = hello | (wp_on << 8) | (sweep << 16) | (armed << 24);
        c.b = flash | (mode << 8) | (pct << 16);
        c.c = channel | (aps << 16);
        c.d = tx;
        char log[80] = {0}, ret[40] = {0}, why[24] = {0};
        (void)json_str(line, "log", log, sizeof log);
        (void)json_str(line, "ret", ret, sizeof ret);
        (void)json_str(line, "why", why, sizeof why);
        snprintf(c.arg, sizeof c.arg, "%s\t%s\t%s", log, ret, why);
        return enqueue(c);
    }
    if (!strcmp(cmd, "bd_status") || !strcmp(cmd, "bd")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_BD;
        int32_t hello = 1, adv = 1, conn = 1, lock = 0, flash = 0, pct = 0;
        (void)json_i32(line, "hello", &hello);
        (void)json_i32(line, "adv", &adv);
        (void)json_i32(line, "conn", &conn);
        (void)json_i32(line, "lock", &lock);
        (void)json_i32(line, "flash", &flash);
        (void)json_i32(line, "pct", &pct);
        c.a = hello;
        c.b = adv;
        c.c = conn;
        c.d = lock | (flash << 8) | (pct << 16);
        return enqueue(c);
    }
    if (!strcmp(cmd, "lf_status") || !strcmp(cmd, "lf")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_LF;
        int32_t hello = 1, ap_on = 0, clients = 0, flash = 0, pct = 0, nfiles = 0;
        uint32_t bytes = 0u;
        (void)json_i32(line, "hello", &hello);
        (void)json_i32(line, "ap_on", &ap_on);
        (void)json_i32(line, "ap", &ap_on);
        (void)json_i32(line, "clients", &clients);
        (void)json_i32(line, "flash", &flash);
        (void)json_i32(line, "pct", &pct);
        (void)json_i32(line, "nfiles", &nfiles);
        (void)json_i32(line, "files", &nfiles);
        (void)json_u32(line, "bytes", &bytes);
        c.a = hello | (ap_on << 8) | (clients << 16) | (flash << 24);
        c.b = pct | (nfiles << 8);
        c.d = (int32_t)bytes;
        char ssid[16] = {0}, pass[12] = {0}, file[16] = {0};
        (void)json_str(line, "ssid", ssid, sizeof ssid);
        (void)json_str(line, "pass", pass, sizeof pass);
        (void)json_str(line, "file", file, sizeof file);
        snprintf(c.arg, sizeof c.arg, "%s\t%s\t%s", ssid, pass, file);
        return enqueue(c);
    }
    if (!strcmp(cmd, "bb_status") || !strcmp(cmd, "bb")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_BB;
        int32_t hello = 1, game = 0, players = 0, phase = 0;
        int32_t flash = 0, pct = 0, tick = 0, heap_k = 240, step = 7;
        int32_t bots = 0, clock_s = 0, sudden = 0, teams = 0;
        (void)json_i32(line, "hello", &hello);
        (void)json_i32(line, "game_on", &game);
        (void)json_i32(line, "game", &game);
        (void)json_i32(line, "players", &players);
        (void)json_i32(line, "phase", &phase);
        (void)json_i32(line, "flash", &flash);
        (void)json_i32(line, "pct", &pct);
        (void)json_i32(line, "tick", &tick);
        (void)json_i32(line, "heap_k", &heap_k);
        (void)json_i32(line, "step", &step);
        (void)json_i32(line, "bots", &bots);
        (void)json_i32(line, "clock", &clock_s);
        (void)json_i32(line, "sudden", &sudden);
        (void)json_i32(line, "teams", &teams);
        c.a = hello | (game << 8) | (players << 16) | (phase << 24);
        c.b = flash | (pct << 8) | (step << 16);
        c.c = tick | (heap_k << 16);
        c.d = bots | (clock_s << 8) | (sudden << 16) | (teams << 24);
        char ssid[16] = {0}, pass[12] = {0}, why[16] = {0};
        (void)json_str(line, "ssid", ssid, sizeof ssid);
        (void)json_str(line, "pass", pass, sizeof pass);
        (void)json_str(line, "why", why, sizeof why);
        snprintf(c.arg, sizeof c.arg, "%s\t%s\t%s", ssid, pass, why);
        return enqueue(c);
    }
    if (!strcmp(cmd, "hk_status") || !strcmp(cmd, "hk")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_HK;
        int32_t io_ok = 1, hiz = 1;
        (void)json_i32(line, "io_ok", &io_ok);
        (void)json_i32(line, "hiz", &hiz);
        c.a = io_ok;
        c.b = hiz;
        (void)json_str(line, "addresses", c.arg, sizeof c.arg);
        return enqueue(c);
    }
    if (!strcmp(cmd, "qg_status") || !strcmp(cmd, "qg")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_QG;
        int32_t io_ok = 1, plot = 0;
        (void)json_i32(line, "io_ok", &io_ok);
        (void)json_i32(line, "plot", &plot);
        c.a = plot;
        c.b = io_ok;
        char name[16] = {0}, val[16] = {0};
        (void)json_str(line, "name", name, sizeof name);
        (void)json_str(line, "val", val, sizeof val);
        snprintf(c.arg, sizeof c.arg, "%s\t%s", name, val);
        return enqueue(c);
    }
    if (!strcmp(cmd, "rg_status") || !strcmp(cmd, "rg")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_RG;
        char host[16] = {0}, cpu[16] = "0,0,0", ram[16] = "0,0,0";
        char net[16] = "0,0,0", gpu[16] = "255,255,255", tmp[16] = "255,255,255";
        (void)json_str(line, "host", host, sizeof host);
        (void)json_str(line, "cpu", cpu, sizeof cpu);
        (void)json_str(line, "ram", ram, sizeof ram);
        (void)json_str(line, "net", net, sizeof net);
        (void)json_str(line, "gpu", gpu, sizeof gpu);
        (void)json_str(line, "tmp", tmp, sizeof tmp);
        snprintf(c.arg, sizeof c.arg, "%s\t%s\t%s\t%s\t%s\t%s",
                 host, cpu, ram, net, gpu, tmp);
        return enqueue(c);
    }
    if (!strcmp(cmd, "dg_vol")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_DG_VOL;
        int32_t count = 1;
        uint32_t free_b = 4000000u;
        (void)json_i32(line, "count", &count);
        (void)json_u32(line, "free", &free_b);
        c.a = count;
        c.c = (int32_t)free_b;
        return enqueue(c);
    }
    if (!strcmp(cmd, "dg_list")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_DG_LIST;
        int32_t index = 0, kind = 1, dir = 0, end = 1;
        uint32_t size = 16048u;
        (void)json_i32(line, "index", &index);
        (void)json_i32(line, "kind", &kind);
        (void)json_i32(line, "dir", &dir);
        (void)json_i32(line, "end", &end);
        (void)json_u32(line, "size", &size);
        c.a = index;
        c.b = kind;
        c.c = (int32_t)size;
        c.d = dir;
        c.e = end;
        (void)json_str(line, "name", c.arg, sizeof c.arg);
        if (!c.arg[0]) strncpy(c.arg, "CLIP0001.RAW", sizeof c.arg - 1u);
        return enqueue(c);
    }
    if (!strcmp(cmd, "dg_meta")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_DG_META;
        int32_t kind = 1;
        uint32_t size = 16048u, ms = 1000u, hz = 8000u;
        (void)json_i32(line, "kind", &kind);
        (void)json_u32(line, "size", &size);
        (void)json_u32(line, "ms", &ms);
        (void)json_u32(line, "hz", &hz);
        c.a = kind;
        c.b = (int32_t)size;
        c.c = (int32_t)ms;
        c.d = (int32_t)hz;
        (void)json_str(line, "name", c.arg, sizeof c.arg);
        if (!c.arg[0]) strncpy(c.arg, "CLIP0001.RAW", sizeof c.arg - 1u);
        return enqueue(c);
    }
    fprintf(stderr, "ogemu: unknown JSON cmd '%s'\n", cmd);
    return -1;
}

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

int ogemu_script_push_line(const char *raw) {
    if (!raw) return 0;
    char buf[512];
    strncpy(buf, raw, sizeof buf - 1u);
    buf[sizeof buf - 1u] = '\0';
    char *line = trim(buf);
    if (!line[0] || line[0] == '#' || (line[0] == '/' && line[1] == '/'))
        return 0;
    if (line[0] == '{') return parse_json_line(line);

    char verb[32] = {0};
    char a1[OGEMU_ARG_MAX] = {0};
    char a2[64] = {0};
    char a3[64] = {0};
    if (sscanf(line, "%31s %255s %63s %63s", verb, a1, a2, a3) < 1) return 0;

    fwog_btn_id_t btn = FWOG_BTN_GREEN;
    if (!strcmp(verb, "tick")) {
        uint32_t ms = (uint32_t)strtoul(a1, NULL, 10);
        return push_cmd(CMD_TICK, btn, ms ? ms : 16u, NULL);
    }
    if (!strcmp(verb, "press")) {
        if (parse_btn(a1, &btn) != 0) {
            fprintf(stderr, "ogemu: bad button '%s'\n", a1);
            return -1;
        }
        return push_cmd(CMD_PRESS, btn, 0u, NULL);
    }
    if (!strcmp(verb, "release")) {
        if (parse_btn(a1, &btn) != 0) {
            fprintf(stderr, "ogemu: bad button '%s'\n", a1);
            return -1;
        }
        return push_cmd(CMD_RELEASE, btn, 0u, NULL);
    }
    if (!strcmp(verb, "hold")) {
        if (parse_btn(a1, &btn) != 0) {
            fprintf(stderr, "ogemu: bad button '%s'\n", a1);
            return -1;
        }
        uint32_t hold_ms = (uint32_t)strtoul(a2, NULL, 10);
        if (!hold_ms) hold_ms = 6000u;
        if (push_cmd(CMD_PRESS, btn, 0u, NULL) != 0) return -1;
        return push_cmd(CMD_TICK, btn, FWOG_BTN_DEBOUNCE_MS + hold_ms, NULL);
    }
    if (!strcmp(verb, "dump"))
        return push_cmd(CMD_DUMP, btn, 0u, a1[0] ? a1 : NULL);
    if (!strcmp(verb, "expect_text") || !strcmp(verb, "expect")) {
        const char *needle = line;
        while (*needle && !isspace((unsigned char)*needle)) needle++;
        while (*needle && isspace((unsigned char)*needle)) needle++;
        return push_cmd(CMD_EXPECT, btn, 0u, needle);
    }
    if (!strcmp(verb, "quit") || !strcmp(verb, "exit"))
        return push_cmd(CMD_QUIT, btn, 0u, NULL);
    if (!strcmp(verb, "accel")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_ACCEL;
        c.a = (int32_t)strtol(a1, NULL, 10);
        c.b = (int32_t)strtol(a2, NULL, 10);
        c.c = a3[0] ? (int32_t)strtol(a3, NULL, 10) : 1000;
        return enqueue(c);
    }
    if (!strcmp(verb, "shake")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_SHAKE;
        c.ms = a1[0] ? (uint32_t)strtoul(a1, NULL, 10) : 200u;
        return enqueue(c);
    }
    if (!strcmp(verb, "mic") || !strcmp(verb, "mic_rms")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_MIC;
        c.ms = (uint32_t)strtoul(a1, NULL, 10);
        return enqueue(c);
    }
    if (!strcmp(verb, "rssi")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_RSSI;
        c.a = (int32_t)strtol(a1, NULL, 10);
        c.c = a2[0] ? (int32_t)strtoul(a2, NULL, 10) : 0;
        return enqueue(c);
    }
    if (!strcmp(verb, "ook")) {
        if (!a1[0]) {
            fprintf(stderr, "ogemu: ook needs a file path\n");
            return -1;
        }
        return push_cmd(CMD_OOK, btn, 0u, a1);
    }
    if (!strcmp(verb, "rf") || !strcmp(verb, "relic")) {
        ogemu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.kind = CMD_RF;
        c.b = (int32_t)strtol(a1, NULL, 10);
        c.a = a2[0] ? (int32_t)strtol(a2, NULL, 10) : -72;
        c.c = a3[0] ? (int32_t)strtoul(a3, NULL, 10) : 433920000;
        return enqueue(c);
    }
    if (!strcmp(verb, "link")) {
        if (!a1[0]) {
            fprintf(stderr, "ogemu: link needs hex\n");
            return -1;
        }
        return push_cmd(CMD_LINK, btn, 0u, a1);
    }
    fprintf(stderr, "ogemu: unknown command '%s'\n", verb);
    return -1;
}

const char *ogemu_script_dir(void) {
    return s_script_dir[0] ? s_script_dir : NULL;
}

int ogemu_script_load(const char *path) {
    s_script_dir[0] = '\0';
    if (path && path[0]) {
        strncpy(s_script_dir, path, sizeof s_script_dir - 1u);
        s_script_dir[sizeof s_script_dir - 1u] = '\0';
        char *slash = strrchr(s_script_dir, '/');
        char *bslash = strrchr(s_script_dir, '\\');
        if (bslash && (!slash || bslash > slash)) slash = bslash;
        if (slash) *slash = '\0';
        else {
            s_script_dir[0] = '.';
            s_script_dir[1] = '\0';
        }
    }
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "ogemu: cannot open script %s\n", path);
        return 1;
    }
    char line[512];
    int rc = 0;
    while (fgets(line, sizeof line, f)) {
        if (ogemu_script_push_line(line) != 0) { rc = 1; break; }
    }
    fclose(f);
    return rc;
}

void ogemu_set_dump_png(const char *path) {
    if (!path) { s_dump_png[0] = '\0'; return; }
    strncpy(s_dump_png, path, sizeof s_dump_png - 1u);
    s_dump_png[sizeof s_dump_png - 1u] = '\0';
}

void ogemu_set_dump_txt(const char *path) {
    if (!path) { s_dump_txt[0] = '\0'; return; }
    strncpy(s_dump_txt, path, sizeof s_dump_txt - 1u);
    s_dump_txt[sizeof s_dump_txt - 1u] = '\0';
}

void ogemu_request_quit(int code) { s_quit = code; }

bool ogemu_script_done(void) {
    qlock();
    bool empty = (s_head == s_tail) && s_tick_left == 0u;
    qunlock();
    return empty && !s_live;
}

static void do_dump(const char *png_override) {
    const char *png = (png_override && png_override[0]) ? png_override
                                                        : s_dump_png;
    if (png && png[0]) {
        if (ogemu_write_png(png) != 0) {
            fprintf(stderr, "ogemu: failed to write %s\n", png);
            s_quit = 1;
        } else {
            DIAG("[ogemu] wrote %s\n", png);
        }
    }
    const char *txt = s_dump_txt;
    char auto_txt[OGEMU_ARG_MAX];
    if ((!txt || !txt[0]) && png && png[0]) {
        strncpy(auto_txt, png, sizeof auto_txt - 1u);
        auto_txt[sizeof auto_txt - 1u] = '\0';
        char *dot = strrchr(auto_txt, '.');
        if (dot && !strchr(dot, '/') && !strchr(dot, '\\')) {
            strcpy(dot, ".txt");
            txt = auto_txt;
        }
    }
    if (txt && txt[0]) {
        if (ogemu_write_text(txt) != 0) {
            fprintf(stderr, "ogemu: failed to write %s\n", txt);
            s_quit = 1;
        } else {
            DIAG("[ogemu] wrote %s\n", txt);
        }
    }
    ogemu_print_text();
    s_dumped = true;
    ogemu_maybe_write_frame(1);
}

void ogemu_finish(void) {
    if (!s_dumped && ((s_dump_png[0]) || (s_dump_txt[0]))) do_dump(NULL);
    else if (!s_dumped) ogemu_print_text();
    ogemu_maybe_write_frame(1);
}

static int dequeue(ogemu_cmd_t *out) {
    qlock();
    if (s_head == s_tail) {
        qunlock();
        return 0;
    }
    *out = s_q[s_head];
    s_head = (s_head + 1u) % OGEMU_Q;
    qunlock();
    return 1;
}

static void apply_world(const ogemu_cmd_t *c) {
    switch (c->kind) {
    case CMD_ACCEL: ogemu_world_accel_mg(c->a, c->b, c->c); break;
    case CMD_SHAKE: ogemu_world_accel_mg(2500, 0, 1000); break;
    case CMD_MIC:   ogemu_world_mic_rms(c->ms); break;
    case CMD_RSSI:  ogemu_world_rssi((int16_t)c->a, (uint32_t)c->c); break;
    case CMD_OOK:   (void)ogemu_world_ook_file(c->arg); break;
    case CMD_RF:    ogemu_world_rf_burst((int16_t)c->a, (uint32_t)c->c,
                                        (unsigned)c->b); break;
    case CMD_LINK:  (void)ogemu_world_link_hex(c->arg); break;
    case CMD_MIC_TONE:
        ogemu_world_mic_tone((uint32_t)c->a, c->ms);
        break;
    case CMD_BS:
        (void)ogemu_world_bs_status((uint8_t)c->a, (uint8_t)c->ms,
                                    (int16_t)c->b, (uint32_t)c->c,
                                    (int16_t)c->d);
        break;
    case CMD_IB: {
        char buf[OGEMU_ARG_MAX];
        strncpy(buf, c->arg, sizeof buf - 1u);
        buf[sizeof buf - 1u] = '\0';
        char *hex = strchr(buf, '|');
        if (hex) {
            *hex++ = '\0';
        } else {
            hex = "";
        }
        (void)ogemu_world_ib_status((uint8_t)c->ms, (int16_t)c->a,
                                    (int16_t)c->b, (uint8_t)c->c,
                                    buf, hex);
        break;
    }
    case CMD_TF:
        (void)ogemu_world_tf_status((int16_t)c->a, (int16_t)c->b,
                                    (uint32_t)c->c);
        break;
    case CMD_TE:
        (void)ogemu_world_te_status((uint8_t)c->e, (int16_t)c->a,
                                    (int16_t)c->b, (uint16_t)c->c,
                                    (uint8_t)c->d, (uint16_t)c->ms);
        break;
    case CMD_FOB:
        (void)ogemu_world_fob_status((uint8_t)c->a, (int16_t)c->b,
                                     (uint8_t)c->c, (uint8_t)c->d,
                                     (uint16_t)c->e, 433920000u);
        break;
    case CMD_TRF:
        (void)ogemu_world_trf_status((int16_t)c->a, (int16_t)c->b,
                                     (uint32_t)c->c, (uint32_t)c->d);
        break;
    case CMD_PH:
        (void)ogemu_world_ph_status((uint8_t)c->a, (uint8_t)c->b,
                                    (int8_t)c->c, (uint8_t)c->d, c->arg);
        break;
    case CMD_AM: {
        char buf[OGEMU_ARG_MAX];
        strncpy(buf, c->arg, sizeof buf - 1u);
        buf[sizeof buf - 1u] = '\0';
        char *ret = strchr(buf, '\t');
        char *why = NULL;
        if (ret) {
            *ret++ = '\0';
            why = strchr(ret, '\t');
            if (why) *why++ = '\0';
        }
        (void)ogemu_world_am_status((uint8_t)(c->a & 0xFFu),
                                    (uint8_t)((c->a >> 8) & 0xFFu),
                                    (uint8_t)((c->a >> 16) & 0xFFu),
                                    (uint8_t)((c->a >> 24) & 0xFFu),
                                    (uint8_t)((c->b >> 8) & 0xFFu),
                                    (uint8_t)(c->b & 0xFFu),
                                    (uint8_t)((c->b >> 16) & 0xFFu),
                                    (uint8_t)(c->c & 0xFFu),
                                    (uint8_t)((c->c >> 16) & 0xFFu),
                                    (uint32_t)c->d,
                                    buf,
                                    ret ? ret : "",
                                    why ? why : "");
        break;
    }
    case CMD_BD:
        (void)ogemu_world_bd_status((uint8_t)c->a, (uint8_t)c->b,
                                    (uint8_t)c->c,
                                    (uint8_t)(c->d & 0xFFu),
                                    (uint8_t)((c->d >> 8) & 0xFFu),
                                    (uint8_t)((c->d >> 16) & 0xFFu));
        break;
    case CMD_LF: {
        char buf[OGEMU_ARG_MAX];
        strncpy(buf, c->arg, sizeof buf - 1u);
        buf[sizeof buf - 1u] = '\0';
        char *pass = strchr(buf, '\t');
        char *file = NULL;
        if (pass) {
            *pass++ = '\0';
            file = strchr(pass, '\t');
            if (file) *file++ = '\0';
        }
        (void)ogemu_world_lf_status((uint8_t)(c->a & 0xFFu),
                                    (uint8_t)((c->a >> 8) & 0xFFu),
                                    (uint8_t)((c->a >> 16) & 0xFFu),
                                    (uint8_t)((c->a >> 24) & 0xFFu),
                                    (uint8_t)(c->b & 0xFFu), (uint32_t)c->d,
                                    (uint8_t)((c->b >> 8) & 0xFFu),
                                    buf,
                                    pass ? pass : "",
                                    file ? file : "");
        break;
    }
    case CMD_BB: {
        char buf[OGEMU_ARG_MAX];
        strncpy(buf, c->arg, sizeof buf - 1u);
        buf[sizeof buf - 1u] = '\0';
        char *pass = strchr(buf, '\t');
        char *why = NULL;
        if (pass) {
            *pass++ = '\0';
            why = strchr(pass, '\t');
            if (why) *why++ = '\0';
        }
        (void)ogemu_world_bb_status(
            (uint8_t)(c->a & 0xFFu), (uint8_t)((c->a >> 8) & 0xFFu),
            (uint8_t)((c->a >> 16) & 0xFFu),
            (uint8_t)((c->a >> 24) & 0xFFu),
            (uint8_t)(c->b & 0xFFu), (uint8_t)((c->b >> 8) & 0xFFu),
            (uint16_t)(c->c & 0xFFFFu),
            (uint16_t)(((uint32_t)c->c >> 16) & 0xFFFFu),
            (uint8_t)((c->b >> 16) & 0xFFu),
            buf, pass ? pass : "", why ? why : "",
            (uint8_t)(c->d & 0xFFu),
            (uint8_t)(((uint32_t)c->d >> 8) & 0xFFu),
            (uint8_t)(((uint32_t)c->d >> 16) & 0xFFu),
            (uint8_t)(((uint32_t)c->d >> 24) & 0xFFu));
        break;
    }
    case CMD_HK:
        (void)ogemu_world_hk_status((uint8_t)c->a, (uint8_t)c->b, c->arg);
        break;
    case CMD_QG: {
        char buf[OGEMU_ARG_MAX];
        strncpy(buf, c->arg, sizeof buf - 1u);
        buf[sizeof buf - 1u] = '\0';
        char *val = strchr(buf, '\t');
        if (val) {
            *val++ = '\0';
        } else {
            val = "";
        }
        (void)ogemu_world_qg_status((uint8_t)c->b, buf, val, (int16_t)c->a);
        break;
    }
    case CMD_RG:
        (void)ogemu_world_rg_status(c->arg);
        break;
    case CMD_DG_VOL:
        (void)ogemu_world_dg_vol((unsigned)c->a, (uint32_t)c->c);
        break;
    case CMD_DG_LIST:
        (void)ogemu_world_dg_list((unsigned)c->a, c->arg, (uint16_t)c->b,
                                  (uint32_t)c->c, (int)c->d, (int)c->e);
        break;
    case CMD_DG_META:
        (void)ogemu_world_dg_meta(c->arg, (uint16_t)c->a, (uint32_t)c->b,
                                  (uint32_t)c->c * 1000u, (uint32_t)c->d);
        break;
    default: break;
    }
}

static void pump_immediate(void) {
    while (s_tick_left == 0u && s_quit < 0) {
        ogemu_cmd_t c;
        if (!dequeue(&c)) break;
        switch (c.kind) {
        case CMD_TICK:
            s_tick_left = c.ms ? c.ms : 1u;
            return;
        case CMD_PRESS:
            DIAG("[ogemu] press %u @ %u ms\n",
                 (unsigned)c.btn, (unsigned)ogemu_now_ms());
            ogemu_btn_set(c.btn, true);
            break;
        case CMD_RELEASE:
            DIAG("[ogemu] release %u @ %u ms\n",
                 (unsigned)c.btn, (unsigned)ogemu_now_ms());
            ogemu_btn_set(c.btn, false);
            break;
        case CMD_DUMP:
            do_dump(c.arg);
            break;
        case CMD_EXPECT:
            if (!ogemu_text_contains(c.arg)) {
                fprintf(stderr, "ogemu: expect_text missed \"%s\"\n", c.arg);
                ogemu_print_text();
                s_quit = 1;
                return;
            }
            DIAG("[ogemu] expect_text ok: \"%s\"\n", c.arg);
            break;
        case CMD_QUIT:
            s_quit = (s_quit < 0) ? 0 : s_quit;
            return;
        case CMD_ACCEL:
        case CMD_MIC:
        case CMD_RSSI:
        case CMD_OOK:
        case CMD_SHAKE:
        case CMD_RF:
        case CMD_LINK:
        case CMD_MIC_TONE:
        case CMD_BS:
        case CMD_IB:
        case CMD_TF:
        case CMD_TE:
        case CMD_FOB:
        case CMD_TRF:
        case CMD_PH:
        case CMD_BD:
        case CMD_LF:
        case CMD_BB:
        case CMD_HK:
        case CMD_QG:
        case CMD_RG:
        case CMD_AM:
        case CMD_DG_VOL:
        case CMD_DG_LIST:
        case CMD_DG_META:
            apply_world(&c);
            break;
        }
    }
    if (!s_live && s_tick_left == 0u && s_quit < 0) {
        qlock();
        bool empty = (s_head == s_tail);
        qunlock();
        if (empty) s_quit = 0;
    }
}

void ogemu_on_sleep(uint32_t ms) {
    uint32_t left = ms;
    pump_immediate();
    ogemu_maybe_write_frame(0);
    while (left && s_quit < 0) {
        if (s_tick_left) {
            uint32_t step = left < s_tick_left ? left : s_tick_left;
            ogemu_advance_ms(step);
            left -= step;
            s_tick_left -= step;
            if (s_tick_left == 0u) pump_immediate();
            ogemu_maybe_write_frame(0);
        } else if (s_live) {
            qlock();
            bool empty = (s_head == s_tail);
            qunlock();
            if (empty) {
#ifdef _WIN32
                Sleep(1);
#else
                usleep(1000);
#endif
                ogemu_advance_ms(1);
                if (left) left--;
                pump_immediate();
                ogemu_maybe_write_frame(0);
            } else {
                pump_immediate();
                ogemu_maybe_write_frame(0);
            }
        } else {
            ogemu_advance_ms(left);
            left = 0u;
            pump_immediate();
        }
    }
    if (s_quit >= 0) {
        ogemu_finish();
        exit(s_quit);
    }
}
