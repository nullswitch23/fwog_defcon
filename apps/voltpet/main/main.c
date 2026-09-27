#include "fwog_main.h"
#include "fs/fwog_fs.h"
#include "vp_proto.h"
#include "pico/stdlib.h"
#include <string.h>

FWOG_WATCHDOG_DEFAULT();

static const uint32_t k_hz[] = { 315000000u, 433920000u, 868000000u };

static fwog_link_rx_t s_rx;
static vp_save_t      s_save;
static cc1101_t       s_radio;
static bool           s_ok, s_in;
static uint8_t        s_band;
static uint32_t       s_hop_at;
static uint32_t       s_cool_until;

static void load_file(void) {
    memset(&s_save, 0, sizeof s_save);
    s_save.type = VP_MSG_ST;
    s_save.hunger = 40;
    s_save.happy = 70;
    s_save.energy = 80;
    s_save.flags = 1u;
    s_save.level = 1;
    s_save.last_art = 0xFF;
    s_save._pad = 0xFF;
    s_save.magic = VP_MAGIC;
    if (!fwog_fs_mounted() && !fwog_fs_mount()) {
        DIAG("[voltpet] no volume (RAM pet until format)\n");
        return;
    }
    uint8_t raw[sizeof(vp_save_t)];
    size_t n = sizeof raw;
    if (!fwog_fs_open("voltpet.bin", false, false)) return;
    if (!fwog_fs_read(raw, &n) || !vp_save_from_bytes(&s_save, raw, n)) {
        (void)fwog_fs_close();
        memset(&s_save, 0, sizeof s_save);
        s_save.type = VP_MSG_ST;
        s_save.hunger = 40;
        s_save.happy = 70;
        s_save.energy = 80;
        s_save.flags = 1u;
        s_save.level = 1;
        s_save.last_art = 0xFF;
        s_save._pad = 0xFF;
        s_save.magic = VP_MAGIC;
        DIAG("[voltpet] save missing or corrupt; new pet\n");
        return;
    }
    (void)fwog_fs_close();
    DIAG("[voltpet] loaded xp=%u lv=%u relics=%u steps=%u name=%s\n",
         (unsigned)s_save.xp, (unsigned)s_save.level,
         (unsigned)s_save.art_count, (unsigned)s_save.steps,
         s_save.name[0] ? s_save.name : "(none)");
}

static void save_file(const vp_save_t *m) {
    if (!fwog_fs_mounted() && !fwog_fs_mount()) return;
    if (!fwog_fs_open("voltpet.bin", true, false)) {
        DIAG("[voltpet] save open failed\n");
        return;
    }
    const bool ok = fwog_fs_write(m, sizeof *m);
    (void)fwog_fs_close();
    DIAG("[voltpet] save %s\n", ok ? "ok" : "FAIL");
}

static bool tune(uint32_t hz) {
    if (!s_ok || !cc1101_freq_in_band(hz)) return false;
    bool ok = cc1101_idle(&s_radio);
    ok = ok && cc1101_set_frequency(&s_radio, hz);
    ok = ok && cc1101_set_modulation(&s_radio, CC1101_MOD_ASK);
    ok = ok && cc1101_set_rx_bandwidth(&s_radio, 270.0f);
    ok = ok && cc1101_rx(&s_radio);
    return ok;
}

static void send_rf(int16_t rssi, uint8_t burst, uint8_t art) {
    vp_rf_t m;
    memset(&m, 0, sizeof m);
    m.type = VP_MSG_RF;
    m.ok = s_ok ? 1u : 0u;
    m.burst = burst;
    m.art_id = art;
    m.rssi = rssi;
    m.hz = k_hz[s_band % 3u];
    (void)fwog_link_uart_send_frame(&m, sizeof m);
}

int main(void) {
    board_init();
    const fwog_display_result_t d = fwog_display_update_run();
    DIAG("[voltpet] display: %s\n", fwog_display_result_text(d));
    load_file();
    fwog_link_rx_init(&s_rx);

    cc1101_bus_init(1000000u);
    cc1101_bind(&s_radio, CC1101_RADIO_CS0);
    s_ok = cc1101_bringup(&s_radio) && cc1101_probe(&s_radio);
    s_band = 1;
    if (s_ok) {
        s_ok = tune(k_hz[s_band]);
    }
    DIAG("[voltpet] radio0=%s\n", s_ok ? "ok" : "fail");

    while (true) {
        board_watchdog_kick();
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        uint8_t b;
        size_t n;
        while (fwog_link_uart_read(&b)) {
            if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
            if (n < sizeof(vp_save_t) || s_rx.buf[0] != VP_MSG_CMD) continue;
            vp_save_t in;
            memcpy(&in, s_rx.buf, sizeof in);
            if (in.cmd == VP_CMD_PULL) {
                s_save.type = VP_MSG_ST;
                s_save.cmd = 0;
                (void)fwog_link_uart_send_frame(&s_save, sizeof s_save);
            } else if (in.cmd == VP_CMD_PUSH && in.magic == VP_MAGIC) {
                vp_name_sanitize(in.name);
                s_save = in;
                s_save.type = VP_MSG_ST;
                save_file(&s_save);
                (void)fwog_link_uart_send_frame(&s_save, sizeof s_save);
            }
        }

        if (s_ok && now >= s_hop_at) {
            s_hop_at = now + 2500u;
            s_band = (uint8_t)((s_band + 1u) % 3u);
            (void)tune(k_hz[s_band]);
            s_in = false;
        }

        const int16_t rssi = s_ok ? cc1101_get_rssi(&s_radio) : (int16_t)-127;
        uint8_t burst = 0, art = 0;
        if (!s_in && rssi > -74 && now >= s_cool_until) {
            s_in = true;
            burst = 1;
            art = (uint8_t)(((uint32_t)(rssi + 200) * 1103515245u +
                             (k_hz[s_band % 3u] / 1000u) + now) % VP_NART);
            s_cool_until = now + 12000u;
            DIAG("[voltpet] relic %u rssi=%d hz=%lu\n",
                 (unsigned)art, (int)rssi,
                 (unsigned long)k_hz[s_band % 3u]);
        } else if (s_in && rssi < -86) {
            s_in = false;
        }
        send_rf(rssi, burst, art);
        sleep_ms(40);
    }
}
