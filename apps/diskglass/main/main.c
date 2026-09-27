/* DiskGlass main: FatFs listing, file serve, optional ASK replay of OOK. */
#include "fwog_main.h"
#include "fs/fwog_fs.h"
#include "dg_proto.h"
#include "dg_fs.h"
#include "dg_wasm.h"
#include "smoke/dg_smoke_wasm.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

FWOG_WATCHDOG_DEFAULT();

#define DG_OOK_MAX 768u
#define DG_SPI_HZ  1000000u
#define DG_TX_FRAMES 4u
#define DG_TX_GAP_MS 25u

static fwog_link_rx_t s_rx;
static bool           s_vol;
static char           s_open[DG_PATH_LEN];
static bool           s_file_open;
static cc1101_t       s_radio;
static bool           s_radio_ok;

static uint16_t s_dur[DG_OOK_MAX];
static uint16_t s_nedges;
static bool     s_first_level;
static uint32_t s_ook_hz;

static uint8_t s_wasm[DG_WASM_MAX];
static bool    s_wasm_stop;

static bool wasm_yield(void) {
    uint8_t b;
    size_t n;
    board_watchdog_kick();
    while (fwog_link_uart_read(&b)) {
        if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
        if (n >= sizeof(dg_cmd_t) && s_rx.buf[0] == DG_MSG_CMD &&
            s_rx.buf[1] == (uint8_t)DG_CMD_STOP) {
            s_wasm_stop = true;
        }
    }
    return s_wasm_stop;
}

static void send_host(uint8_t op, uint8_t idx, uint8_t r, uint8_t g, uint8_t b,
                      uint16_t code) {
    dg_host_t m;
    memset(&m, 0, sizeof m);
    m.type = DG_MSG_HOST;
    m.op = op;
    m.idx = idx;
    m.r = r;
    m.g = g;
    m.b = b;
    m.code = code;
    (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void wasm_led(int idx, int r, int g, int b) {
    if (idx < 0) idx = 0;
    if (idx > 255) idx = 255;
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    send_host(DG_HOST_LED, (uint8_t)idx, (uint8_t)r, (uint8_t)g, (uint8_t)b, 0);
}

static void close_file(void);

static void wasm_fb(const uint8_t *px, int w, int h) {
    dg_host_fb_t m;
    uint16_t n;
    if (!px || w <= 0 || h <= 0) return;
    if (w > (int)DG_FB_W) w = (int)DG_FB_W;
    if (h > (int)DG_FB_H) h = (int)DG_FB_H;
    n = (uint16_t)((unsigned)w * (unsigned)h);
    memset(&m, 0, sizeof m);
    m.type = DG_MSG_HOST;
    m.op = DG_HOST_FB;
    m.w = (uint8_t)w;
    m.h = (uint8_t)h;
    m.n = n;
    memcpy(m.px, px, n);
    (void)fwog_link_uart_send_frame(&m, (size_t)(6u + n));
}

static void seed_smoke(void) {
    if (!s_vol) return;
    (void)dg_fs_ensure_dir("/scripts");
    if (fwog_fs_exists("/scripts/SMOKE.WASM")) return;
    close_file();
    if (!fwog_fs_open("/scripts/SMOKE.WASM", true, false)) {
        DIAG("[diskglass] seed smoke open fail\n");
        return;
    }
    if (!fwog_fs_write(k_dg_smoke_wasm, (size_t)DG_SMOKE_WASM_LEN)) {
        DIAG("[diskglass] seed smoke write fail\n");
    } else {
        DIAG("[diskglass] seeded /scripts/SMOKE.WASM\n");
    }
    (void)fwog_fs_close();
}

static void send_list(const dg_list_t *m) {
    (void)fwog_link_uart_send_frame(m, sizeof *m);
}

static void send_err(void) {
    dg_list_t m;
    memset(&m, 0, sizeof m);
    m.type = DG_MSG_LIST;
    m.flags = DG_FLAG_ERR;
    send_list(&m);
}

static void close_file(void) {
    if (s_file_open) {
        (void)fwog_fs_close();
        s_file_open = false;
    }
    s_open[0] = '\0';
}

static uint16_t kind_from_ext(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) return DG_KIND_UNKNOWN;
    if (!strcmp(dot, ".RAW") || !strcmp(dot, ".raw") ||
        !strcmp(dot, ".PCM") || !strcmp(dot, ".pcm"))
        return DG_KIND_PCM16;
    if (!strcmp(dot, ".CSV") || !strcmp(dot, ".csv")) return DG_KIND_CSV;
    if (!strcmp(dot, ".TXT") || !strcmp(dot, ".txt") ||
        !strcmp(dot, ".LOG") || !strcmp(dot, ".log") ||
        !strcmp(dot, ".MD")  || !strcmp(dot, ".md"))
        return DG_KIND_TEXT;
    if (!strcmp(dot, ".WASM") || !strcmp(dot, ".wasm"))
        return DG_KIND_WASM;
    if (!strcmp(dot, ".BIN") || !strcmp(dot, ".bin")) return DG_KIND_OOK;
    return DG_KIND_UNKNOWN;
}

static uint16_t peek_kind(const char *dir, const char *name, bool is_dir) {
    char path[DG_PATH_LEN];
    uint8_t head[64];
    size_t n;
    uint16_t k;
    if (is_dir) return DG_KIND_UNKNOWN;
    k = kind_from_ext(name);
    if (!dg_join_path(path, sizeof path, dir, name)) return k;
    close_file();
    if (!fwog_fs_open(path, false, false)) return k;
    n = sizeof head;
    if (!fwog_fs_read(head, &n)) {
        (void)fwog_fs_close();
        return k;
    }
    (void)fwog_fs_close();
    {
        uint16_t sniff = dg_sniff(head, n);
        if (sniff != DG_KIND_UNKNOWN) return sniff;
    }
    return k;
}

static void handle_list(const char *dir) {
    dg_list_t m;
    uint64_t free_b = 0, total_b = 0;
    unsigned i, count = 0;
    const char *d = (dir && dir[0]) ? dir : "/";
    char name[DG_NAME_LEN];
    bool is_dir;

    board_watchdog_kick();
    close_file();
    if (!s_vol && !dg_fs_ready()) {
        s_vol = false;
        DIAG("[diskglass] list: no volume\n");
        send_err();
        return;
    }
    s_vol = true;
    board_watchdog_kick();
    (void)fwog_fs_volume_info(&free_b, &total_b);
    board_watchdog_kick();

    /* Tell the display the volume is up BEFORE walking entries. The glass
     * retries LIST until MOUNTED/ERR; a slow dir walk must not look like
     * silence, and must not queue another full listing. */
    memset(&m, 0, sizeof m);
    m.type = DG_MSG_LIST;
    m.flags = DG_FLAG_MOUNTED;
    m.size = (uint32_t)free_b;
    send_list(&m);
    DIAG("[diskglass] list %s\n", d);

    for (i = 0; i < 256u && count < DG_MAX_ENT; i++) {
        uint16_t kind;
        uint32_t sz = 0;
        char path[DG_PATH_LEN];
        board_watchdog_kick();
        if (!fwog_fs_dir_entry(d, i, name, sizeof name, &is_dir)) break;
        if (name[0] == '.') continue;
        kind = peek_kind(d, name, is_dir);
        board_watchdog_kick();
        if (!is_dir && dg_join_path(path, sizeof path, d, name) &&
            fwog_fs_open(path, false, false)) {
            sz = fwog_fs_size();
            (void)fwog_fs_close();
        }
        memset(&m, 0, sizeof m);
        m.type = DG_MSG_LIST;
        m.flags = is_dir ? DG_FLAG_DIR : 0u;
        m.index = (uint16_t)count;
        m.count = 0;
        m.kind = kind;
        m.size = sz;
        strncpy(m.name, name, DG_NAME_LEN - 1u);
        send_list(&m);
        count++;
    }

    memset(&m, 0, sizeof m);
    m.type = DG_MSG_LIST;
    m.flags = DG_FLAG_END;
    m.count = (uint16_t)count;
    send_list(&m);
    DIAG("[diskglass] list done n=%u\n", count);
    board_watchdog_kick();
}

static bool load_meta(const char *path, dg_meta_t *out) {
    uint8_t head[64];
    size_t n = sizeof head;
    const char *base;
    uint32_t sz;
    uint16_t sniff;
    memset(out, 0, sizeof *out);
    out->type = DG_MSG_META;
    base = strrchr(path, '/');
    base = base ? base + 1 : path;
    strncpy(out->name, base, DG_NAME_LEN - 1u);
    if (!fwog_fs_open(path, false, false)) return false;
    s_file_open = true;
    strncpy(s_open, path, sizeof s_open - 1u);
    sz = fwog_fs_size();
    out->size = sz;
    if (!fwog_fs_read(head, &n)) return true;
    sniff = dg_sniff(head, n);
    out->kind = sniff != DG_KIND_UNKNOWN ? sniff : kind_from_ext(base);
    if (sniff == DG_KIND_PCM16 || sniff == DG_KIND_OOK) {
        dg_hdr_t h;
        if (n >= sizeof h) {
            memcpy(&h, head, sizeof h);
            if (dg_hdr_valid(&h)) {
                out->kind = h.kind;
                out->duration_us = h.duration_us;
                out->freq_hz = h.freq_hz;
                out->sample_hz = h.sample_hz;
                out->peak_rssi = h.peak_rssi;
                out->edges = h.edges;
                memcpy(out->app, h.app, DG_APP_LEN);
                if (h.kind == DG_KIND_PCM16 && out->duration_us == 0u) {
                    out->duration_us = dg_pcm_duration_ms(
                        h.payload_bytes, h.sample_hz ? h.sample_hz : 8000u) * 1000u;
                    if (!out->sample_hz) out->sample_hz = 8000u;
                }
            }
        }
    } else if (sniff == DG_KIND_IBST && n >= 20u) {
        uint32_t freq, total;
        uint16_t edges, ver;
        int16_t peak;
        memcpy(&ver, head + 4, 2);
        memcpy(&edges, head + 6, 2);
        memcpy(&freq, head + 8, 4);
        memcpy(&total, head + 12, 4);
        memcpy(&peak, head + 16, 2);
        (void)ver;
        out->freq_hz = freq;
        out->duration_us = total;
        out->peak_rssi = peak;
        out->edges = edges;
        strncpy(out->app, "ismburst", DG_APP_LEN - 1u);
    } else if (out->kind == DG_KIND_CSV) {
        strncpy(out->app, "trailrf", DG_APP_LEN - 1u);
    } else if (out->kind == DG_KIND_TEXT) {
        strncpy(out->app, "text", DG_APP_LEN - 1u);
    } else if (out->kind == DG_KIND_WASM) {
        strncpy(out->app, "wasm", DG_APP_LEN - 1u);
    }
    return true;
}

static void handle_put(const dg_put_t *in) {
    char path[DG_PATH_LEN];
    char name[DG_NAME_LEN];
    unsigned idx;
    size_t n;
    board_watchdog_kick();
    if (!in || in->n == 0u || in->n > DG_IR_MAX) return;
    if (!s_vol && !dg_fs_ready()) return;
    s_vol = true;
    if (!dg_fs_ensure_dir("/inbox")) {
        DIAG("[diskglass] put: no /inbox\n");
        return;
    }
    close_file();
    idx = dg_fs_next_index("/inbox", "IR");
    dg_format_name(name, sizeof name, "IR", idx, "TXT");
    if (!dg_join_path(path, sizeof path, "/inbox", name)) return;
    if (!fwog_fs_open(path, true, false)) {
        DIAG("[diskglass] put open fail %s\n", path);
        return;
    }
    n = in->n;
    if (!fwog_fs_write(in->bytes, n)) {
        (void)fwog_fs_close();
        DIAG("[diskglass] put write fail\n");
        return;
    }
    (void)fwog_fs_close();
    board_watchdog_kick();
    DIAG("[diskglass] put %s n=%u\n", path, (unsigned)n);
}

static void handle_open(const char *path) {
    dg_meta_t m;
    close_file();
    if (!s_vol && !dg_fs_ready()) {
        send_err();
        return;
    }
    if (!load_meta(path, &m)) {
        DIAG("[diskglass] open fail %s\n", path);
        send_err();
        return;
    }
    (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void handle_read(uint32_t offset, uint16_t len) {
    dg_data_t d;
    size_t n;
    if (!s_file_open) {
        DIAG("[diskglass] read: no file\n");
        return;
    }
    if (len == 0u || len > DG_DATA_MAX) len = DG_DATA_MAX;
    if (!fwog_fs_seek(offset)) {
        DIAG("[diskglass] seek %u fail\n", (unsigned)offset);
        return;
    }
    memset(&d, 0, sizeof d);
    d.type = DG_MSG_DATA;
    n = len;
    if (!fwog_fs_read(d.bytes, &n)) n = 0;
    d.n = (uint16_t)n;
    d.offset = offset;
    (void)fwog_link_uart_send_frame(&d, (size_t)(8u + n));
}

static void gdo0_as_input(void) {
    gpio_set_dir(s_radio.gdo0_pin, GPIO_IN);
    gpio_pull_up(s_radio.gdo0_pin);
}

static void gdo0_as_output(bool level) {
    gpio_disable_pulls(s_radio.gdo0_pin);
    gpio_set_dir(s_radio.gdo0_pin, GPIO_OUT);
    gpio_put(s_radio.gdo0_pin, level);
}

static void wait_us(uint32_t us) {
    const uint32_t t0 = time_us_32();
    while ((uint32_t)(time_us_32() - t0) < us) tight_loop_contents();
}

static bool configure_ook(uint32_t hz) {
    if (!s_radio_ok || !cc1101_freq_in_band(hz)) return false;
    bool ok = cc1101_idle(&s_radio);
    ok = ok && cc1101_set_frequency(&s_radio, hz);
    ok = ok && cc1101_set_modulation(&s_radio, CC1101_MOD_ASK);
    ok = ok && cc1101_set_pkt_format(&s_radio, 3u);
    ok = ok && cc1101_set_sync_mode(&s_radio, 0u);
    ok = ok && cc1101_set_crc(&s_radio, false);
    ok = ok && cc1101_set_white_data(&s_radio, false);
    ok = ok && cc1101_set_manchester(&s_radio, false);
    ok = ok && cc1101_set_append_status(&s_radio, false);
    ok = ok && cc1101_set_dc_filter_off(&s_radio, true);
    ok = ok && cc1101_set_rx_bandwidth(&s_radio, 270.0f);
    ok = ok && cc1101_set_data_rate(&s_radio, 10.0f);
    ok = ok && cc1101_set_power(&s_radio, 10);
    ok = ok && cc1101_write_reg(&s_radio, CC1101_REG_IOCFG0, 0x0Du);
    ok = ok && cc1101_write_reg(&s_radio, CC1101_REG_AGCCTRL2, 0x07u);
    ok = ok && cc1101_write_reg(&s_radio, CC1101_REG_AGCCTRL1, 0x00u);
    ok = ok && cc1101_write_reg(&s_radio, CC1101_REG_AGCCTRL0, 0x91u);
    gdo0_as_input();
    return ok;
}

static bool load_ook(const char *path) {
    uint8_t head[64];
    size_t n = sizeof head;
    uint16_t sniff;
    close_file();
    s_nedges = 0;
    if (!fwog_fs_open(path, false, false)) return false;
    if (!fwog_fs_read(head, &n)) {
        (void)fwog_fs_close();
        return false;
    }
    sniff = dg_sniff(head, n);
    if (sniff == DG_KIND_IBST) {
        uint16_t edges;
        uint32_t freq;
        memcpy(&edges, head + 6, 2);
        memcpy(&freq, head + 8, 4);
        s_first_level = head[18] != 0u;
        s_ook_hz = freq ? freq : 433920000u;
        if (edges > DG_OOK_MAX) edges = DG_OOK_MAX;
        (void)fwog_fs_seek(20u);
        n = (size_t)edges * 2u;
        if (!fwog_fs_read(s_dur, &n)) {
            (void)fwog_fs_close();
            return false;
        }
        s_nedges = (uint16_t)(n / 2u);
        (void)fwog_fs_close();
        return s_nedges >= 16u;
    }
    if (sniff == DG_KIND_OOK) {
        dg_hdr_t h;
        dg_ook_lead_t lead;
        size_t ln;
        if (n < sizeof h) {
            (void)fwog_fs_close();
            return false;
        }
        memcpy(&h, head, sizeof h);
        s_ook_hz = h.freq_hz ? h.freq_hz : 433920000u;
        (void)fwog_fs_seek(sizeof(dg_hdr_t));
        ln = sizeof lead;
        if (!fwog_fs_read(&lead, &ln) || ln != sizeof lead) {
            (void)fwog_fs_close();
            return false;
        }
        s_first_level = lead.first_level != 0u;
        if (lead.n > DG_OOK_MAX) lead.n = DG_OOK_MAX;
        n = (size_t)lead.n * 2u;
        if (!fwog_fs_read(s_dur, &n)) {
            (void)fwog_fs_close();
            return false;
        }
        s_nedges = (uint16_t)(n / 2u);
        (void)fwog_fs_close();
        return s_nedges >= 16u;
    }
    (void)fwog_fs_close();
    return false;
}

static bool replay_ook(void) {
    unsigned frame;
    if (s_nedges < 16u) return false;
    if (!configure_ook(s_ook_hz)) {
        DIAG("[diskglass] replay: radio not ready\n");
        return false;
    }
    (void)cc1101_idle(&s_radio);
    (void)cc1101_write_reg(&s_radio, CC1101_REG_IOCFG0, 0x2Eu);
    gdo0_as_output(s_first_level);
    (void)cc1101_write_reg(&s_radio, CC1101_REG_IOCFG0, 0x0Du);
    if (!cc1101_tx(&s_radio)) {
        gdo0_as_input();
        return false;
    }
    for (frame = 0; frame < DG_TX_FRAMES; frame++) {
        uint16_t e;
        bool level;
        board_watchdog_kick();
        wait_us(400);
        level = s_first_level;
        for (e = 0; e < s_nedges; e++) {
            gpio_put(s_radio.gdo0_pin, level);
            wait_us(s_dur[e]);
            level = !level;
            if ((e & 127u) == 0u) board_watchdog_kick();
        }
        gpio_put(s_radio.gdo0_pin, 0);
        if (frame + 1u < DG_TX_FRAMES) sleep_ms(DG_TX_GAP_MS);
    }
    (void)cc1101_idle(&s_radio);
    gdo0_as_input();
    (void)cc1101_write_reg(&s_radio, CC1101_REG_IOCFG0, 0x0Du);
    DIAG("[diskglass] replayed %u edges x%u\n",
         (unsigned)s_nedges, (unsigned)DG_TX_FRAMES);
    return true;
}

static void handle_replay(const char *path) {
    dg_meta_t m;
    const char *p = path[0] ? path : s_open;
    if (!load_ook(p)) {
        DIAG("[diskglass] replay: cannot load %s\n", p);
        return;
    }
    (void)replay_ook();
    /* Re-open so the view can keep reading. */
    if (load_meta(p, &m)) {
        m.flags = 0;
        (void)fwog_link_uart_send_frame(&m, sizeof m);
    }
}

static void handle_del(const char *path) {
    board_watchdog_kick();
    close_file();
    if (!path || !path[0]) {
        DIAG("[diskglass] del: empty path\n");
        send_err();
        return;
    }
    if (!s_vol && !dg_fs_ready()) {
        send_err();
        return;
    }
    if (!fwog_fs_remove(path)) {
        DIAG("[diskglass] del fail %s\n", path);
        return;
    }
    DIAG("[diskglass] del %s\n", path);
    board_watchdog_kick();
}

static void handle_run(const char *path, bool loop) {
    size_t n;
    char why[40];
    const char *p = (path && path[0]) ? path : s_open;
    board_watchdog_kick();
    close_file();
    s_wasm_stop = false;
    if (!s_vol && !dg_fs_ready()) {
        send_host(DG_HOST_DONE, 0, 0, 0, 0, 1);
        return;
    }
    if (!fwog_fs_open(p, false, false)) {
        DIAG("[diskglass] run open fail %s\n", p);
        send_host(DG_HOST_DONE, 0, 0, 0, 0, 1);
        return;
    }
    {
        uint32_t sz = fwog_fs_size();
        if (sz == 0u || sz > DG_WASM_MAX) {
            (void)fwog_fs_close();
            DIAG("[diskglass] run empty/too big %s n=%u\n", p, (unsigned)sz);
            send_host(DG_HOST_DONE, 0, 0, 0, 0, 2);
            return;
        }
        n = sz;
    }
    if (!fwog_fs_read(s_wasm, &n)) n = 0;
    (void)fwog_fs_close();
    board_watchdog_kick();
    if (n == 0u) {
        DIAG("[diskglass] run read fail %s\n", p);
        send_host(DG_HOST_DONE, 0, 0, 0, 0, 2);
        return;
    }
    DIAG("[diskglass] wasm %s n=%u%s\n", p, (unsigned)n, loop ? " loop" : "");
    board_watchdog_kick();
    dg_wasm_set_led_hook(wasm_led);
    dg_wasm_set_fb_hook(wasm_fb);
    dg_wasm_set_yield_hook(wasm_yield);
    why[0] = '\0';
    if (!dg_wasm_run(s_wasm, n, loop, why, sizeof why)) {
        dg_wasm_set_yield_hook(NULL);
        if (strcmp(why, DG_WASM_STOPPED) == 0) {
            DIAG("[diskglass] wasm stopped\n");
            send_host(DG_HOST_DONE, 0, 0, 0, 0, 4);
            return;
        }
        DIAG("[diskglass] wasm fail %s\n", why[0] ? why : "?");
        send_host(DG_HOST_DONE, 0, 0, 0, 0, 3);
        return;
    }
    dg_wasm_set_yield_hook(NULL);
    if (s_wasm_stop) {
        DIAG("[diskglass] wasm stopped\n");
        send_host(DG_HOST_DONE, 0, 0, 0, 0, 4);
        return;
    }
    DIAG("[diskglass] wasm ok\n");
    send_host(DG_HOST_DONE, 0, 0, 0, 0, 0);
}

int main(void) {
    board_init();
    const fwog_display_result_t d = fwog_display_update_run();
    board_watchdog_kick();
    DIAG("[diskglass] display: %s\n", fwog_display_result_text(d));

    s_vol = dg_fs_ready();
    board_watchdog_kick();
    DIAG("[diskglass] FatFs %s\n", s_vol ? "mounted" : "no volume");
    seed_smoke();
    board_watchdog_kick();

    cc1101_bus_init(DG_SPI_HZ);
    cc1101_bind(&s_radio, CC1101_RADIO_CS0);
    s_radio_ok = cc1101_bringup(&s_radio) && cc1101_probe(&s_radio);
    board_watchdog_kick();
    DIAG("[diskglass] radio0=%s (OOK replay only)\n",
         s_radio_ok ? "ok" : "fail");

    fwog_link_rx_init(&s_rx);

    while (true) {
        board_watchdog_kick();
        uint8_t b;
        size_t n;
        while (fwog_link_uart_read(&b)) {
            if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
            if (n >= 8u && s_rx.buf[0] == DG_MSG_PUT) {
                dg_put_t p;
                size_t take = n;
                if (take > sizeof p) take = sizeof p;
                memset(&p, 0, sizeof p);
                memcpy(&p, s_rx.buf, take);
                if (p.n > DG_IR_MAX) p.n = DG_IR_MAX;
                handle_put(&p);
                continue;
            }
            if (n < sizeof(dg_cmd_t) || s_rx.buf[0] != DG_MSG_CMD) continue;
            dg_cmd_t c;
            memcpy(&c, s_rx.buf, sizeof c);
            c.path[DG_PATH_LEN - 1u] = '\0';
            if (c.cmd == DG_CMD_LIST) {
                handle_list(c.path);
            } else if (c.cmd == DG_CMD_OPEN) {
                handle_open(c.path);
            } else if (c.cmd == DG_CMD_READ) {
                handle_read(c.offset, c.len);
            } else if (c.cmd == DG_CMD_CLOSE) {
                close_file();
            } else if (c.cmd == DG_CMD_REPLAY) {
                handle_replay(c.path);
            } else if (c.cmd == DG_CMD_DEL) {
                handle_del(c.path);
            } else if (c.cmd == DG_CMD_RUN) {
                handle_run(c.path, c.index == DG_RUN_LOOP);
            } else if (c.cmd == DG_CMD_STOP) {
                s_wasm_stop = true;
            }
        }
        sleep_ms(2);
    }
}
