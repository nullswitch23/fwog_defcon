/* WIFIPROOF — off-by-default 802.11 raw TX/RX lab mode for ESP32-C6 PHY proof.
 * Does not alter AP/BLE/HID unless explicitly armed via console commands. */
#include "wifiproof.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_wifi.h"

static wifiproof_io_t s_io;
static bool s_on;
static uint8_t s_channel;
static uint32_t s_tx_count;
static esp_err_t s_last_ret = ESP_OK;

/* One UART line per BSSID (or when SSID is first learned). Printing every
 * beacon left ssid= past the OG's 115200 read, so the list got MACs only. */
#define WP_SEEN_MAX 24u
static struct {
    uint8_t mac[6];
    char ssid[16];
} s_seen[WP_SEEN_MAX];
static unsigned s_seen_n;

static void seen_clear(void) { s_seen_n = 0; }

static bool seen_skip(const uint8_t mac[6], const char *ssid) {
    for (unsigned i = 0; i < s_seen_n; i++) {
        if (memcmp(s_seen[i].mac, mac, 6) != 0) continue;
        if (ssid[0] && !s_seen[i].ssid[0]) {
            strncpy(s_seen[i].ssid, ssid, sizeof s_seen[i].ssid - 1u);
            s_seen[i].ssid[sizeof s_seen[i].ssid - 1u] = '\0';
            return false;
        }
        return true;
    }
    if (s_seen_n >= WP_SEEN_MAX) return true;
    memcpy(s_seen[s_seen_n].mac, mac, 6);
    memset(s_seen[s_seen_n].ssid, 0, sizeof s_seen[s_seen_n].ssid);
    if (ssid[0]) {
        strncpy(s_seen[s_seen_n].ssid, ssid, sizeof s_seen[s_seen_n].ssid - 1u);
    }
    s_seen_n++;
    return false;
}

static void wp_puts(const char *s) {
    if (s_io.puts) s_io.puts(s);
}

static int parse_mac(const char *s, uint8_t out[6]) {
    unsigned a[6];
    if (sscanf(s, "%02x:%02x:%02x:%02x:%02x:%02x",
               &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) != 6) {
        return -1;
    }
    for (int i = 0; i < 6; i++) out[i] = (uint8_t)a[i];
    return 0;
}

static void mac_fmt(const uint8_t m[6], char *buf, size_t n) {
    snprintf(buf, n, "%02x:%02x:%02x:%02x:%02x:%02x",
             m[0], m[1], m[2], m[3], m[4], m[5]);
}

static void hex_line(const uint8_t *buf, size_t len, char *out, size_t out_n) {
    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 3u < out_n; i++) {
        pos += (size_t)snprintf(out + pos, out_n - pos, "%02x", buf[i]);
    }
}

/* Beacon / probe-response SSID (IE 0) after the 24-byte hdr + 12-byte fixed. */
static void extract_ssid(const uint8_t *p, int len, char *out, size_t out_n) {
    out[0] = '\0';
    if (!p || out_n < 2 || len < 38) return;
    int end = len;
    if (end >= 4) end -= 4; /* FCS */
    int off = 36;
    while (off + 2 <= end) {
        const uint8_t id = p[off];
        const uint8_t tlen = p[off + 1];
        if (off + 2 + (int)tlen > end) break;
        if (id == 0) {
            size_t w = 0;
            size_t n = tlen;
            if (n > out_n - 1u) n = out_n - 1u;
            for (size_t i = 0; i < n; i++) {
                char c = (char)p[off + 2 + (int)i];
                if (c <= 32 || c >= 127) c = '_';
                out[w++] = c;
            }
            out[w] = '\0';
            return;
        }
        off += 2 + (int)tlen;
    }
}

static void promisc_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (!s_on || type != WIFI_PKT_MGMT) return;
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    const uint8_t *p = pkt->payload;
    if (pkt->rx_ctrl.sig_len < 24) return;

    const uint16_t fc = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    const uint8_t subtype = (uint8_t)((fc >> 4) & 0xFu);
    /* Only beacons (8) and probe responses (5). Auth/action/etc. flooded the
     * 115200 header UART so the OG lost the lines it actually lists. */
    if (subtype != 8u && subtype != 5u) return;
    char src[20];
    mac_fmt(p + 10, src, sizeof src);
    char ssid[16];
    extract_ssid(p, (int)pkt->rx_ctrl.sig_len, ssid, sizeof ssid);
    if (seen_skip(p + 10, ssid)) return;
    char line[96];
    snprintf(line, sizeof line,
             "[WIFIPROOF RX] subtype=%u src=%s ssid=%s\n",
             (unsigned)subtype, src, ssid[0] ? ssid : "-");
    wp_puts(line);
}

static esp_err_t wp_set_channel(uint8_t ch) {
    esp_err_t e = esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    if (e != ESP_OK) return e;
    s_channel = ch;
    return ESP_OK;
}

static esp_err_t wp_on(uint8_t channel) {
    if (channel < 1 || channel > 14) {
        wp_puts("[WIFIPROOF] bad channel\n");
        return ESP_ERR_INVALID_ARG;
    }
    if (s_io.ap_is_on && s_io.ap_is_on()) {
        if (s_io.ap_stop) s_io.ap_stop();
    }
    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_STA);
    wifi_config_t cfg = { 0 };
    (void)esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_err_t e = esp_wifi_start();
    if (e != ESP_OK && e != ESP_ERR_WIFI_CONN) {
        char line[64];
        snprintf(line, sizeof line, "[WIFIPROOF] wifi start %s\n", esp_err_to_name(e));
        wp_puts(line);
        return e;
    }
    e = wp_set_channel(channel);
    if (e != ESP_OK) {
        char line[64];
        snprintf(line, sizeof line, "[WIFIPROOF] set channel %s\n", esp_err_to_name(e));
        wp_puts(line);
        return e;
    }
    e = esp_wifi_set_promiscuous(true);
    if (e != ESP_OK) {
        char line[64];
        snprintf(line, sizeof line, "[WIFIPROOF] promisc %s\n", esp_err_to_name(e));
        wp_puts(line);
        return e;
    }
    e = esp_wifi_set_promiscuous_rx_cb(promisc_cb);
    if (e != ESP_OK) {
        char line[64];
        snprintf(line, sizeof line, "[WIFIPROOF] promisc cb %s\n", esp_err_to_name(e));
        wp_puts(line);
        return e;
    }
    seen_clear();
    s_on = true;
    char line[48];
    snprintf(line, sizeof line, "[WIFIPROOF] READY ch=%u\n", (unsigned)channel);
    wp_puts(line);
    return ESP_OK;
}

static esp_err_t wp_ch(uint8_t channel) {
    if (channel < 1 || channel > 14) {
        wp_puts("[WIFIPROOF] bad channel\n");
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_on) return wp_on(channel);
    esp_err_t e = wp_set_channel(channel);
    if (e != ESP_OK) {
        char line[64];
        snprintf(line, sizeof line, "[WIFIPROOF] set channel %s\n", esp_err_to_name(e));
        wp_puts(line);
        return e;
    }
    char line[48];
    snprintf(line, sizeof line, "[WIFIPROOF] READY ch=%u\n", (unsigned)channel);
    wp_puts(line);
    return ESP_OK;
}

static void wp_off(void) {
    if (!s_on) {
        wp_puts("[WIFIPROOF] off\n");
        return;
    }
    esp_wifi_set_promiscuous_rx_cb(NULL);
    esp_wifi_set_promiscuous(false);
    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_AP);
    s_on = false;
    s_channel = 0;
    seen_clear();
    wp_puts("[WIFIPROOF] off\n");
}

static esp_err_t wp_tx_frame(const uint8_t *frame, size_t len) {
    if (!s_on) {
        wp_puts("[WIFIPROOF] not armed (WIFIPROOF ON first)\n");
        return ESP_ERR_INVALID_STATE;
    }
    s_tx_count++;
    esp_err_t ret = esp_wifi_80211_tx(WIFI_IF_STA, frame, (int)len, false);
    s_last_ret = ret;
    return ret;
}

static void print_tx_result(esp_err_t ret, const uint8_t *frame, size_t len) {
    char hex[160];
    hex_line(frame, len, hex, sizeof hex);
    char line[200];
    snprintf(line, sizeof line, "[WIFIPROOF TX] ret=%s frame=%s\n",
             esp_err_to_name(ret), hex);
    wp_puts(line);
}

static esp_err_t build_deauth_disassoc(uint8_t subtype,
                                       const uint8_t dst[6],
                                       const uint8_t src[6],
                                       const uint8_t bssid[6],
                                       uint16_t reason,
                                       uint8_t out[26]) {
    const uint8_t fc0 = (uint8_t)(0x00u | (subtype << 4));
    out[0] = fc0;
    out[1] = 0x00;
    out[2] = 0x00;
    out[3] = 0x00;
    memcpy(out + 4, dst, 6);
    memcpy(out + 10, src, 6);
    memcpy(out + 16, bssid, 6);
    out[22] = 0x00;
    out[23] = 0x00;
    out[24] = (uint8_t)(reason & 0xffu);
    out[25] = (uint8_t)(reason >> 8);
    return ESP_OK;
}

static void cmd_deauth_disassoc(const char *line, uint8_t subtype, const char *label) {
    char dst_s[24], src_s[24], bssid_s[24], rc_s[12];
    const char *p = strchr(line, ' ');
    if (!p) return;
    p = strchr(p + 1, ' ');
    if (!p) return;
    p++;
    if (sscanf(p, "%23s %23s %23s %11s", dst_s, src_s, bssid_s, rc_s) < 3) {
        wp_puts("[WIFIPROOF] usage: WIFIPROOF DEAUTH|DASSOC <dst> <src> <bssid> [rc]\n");
        return;
    }
    uint8_t dst[6], src[6], bssid[6];
    if (parse_mac(dst_s, dst) || parse_mac(src_s, src) || parse_mac(bssid_s, bssid)) {
        wp_puts("[WIFIPROOF] bad mac\n");
        return;
    }
    unsigned rc = 4;
    if (rc_s[0]) rc = (unsigned)strtoul(rc_s, NULL, 0);

    uint8_t frame[26];
    build_deauth_disassoc(subtype, dst, src, bssid, (uint16_t)rc, frame);
    esp_err_t ret = wp_tx_frame(frame, sizeof frame);
    char hdr[64];
    snprintf(hdr, sizeof hdr, "[WIFIPROOF %s] rc=%u ", label, rc);
    wp_puts(hdr);
    print_tx_result(ret, frame, sizeof frame);
}

static void cmd_beacon(const char *line) {
    const char *p = strchr(line, ' ');
    if (!p) return;
    p = strchr(p + 1, ' ');
    if (!p) return;
    p++;
    char ssid[33], bssid_s[24];
    if (sscanf(p, "%32s %23s", ssid, bssid_s) != 2) {
        wp_puts("[WIFIPROOF] usage: WIFIPROOF BEACON <ssid> <bssid>\n");
        return;
    }
    uint8_t bssid[6];
    if (parse_mac(bssid_s, bssid)) {
        wp_puts("[WIFIPROOF] bad bssid\n");
        return;
    }
    size_t ssid_len = strlen(ssid);
    if (ssid_len == 0 || ssid_len > 32) {
        wp_puts("[WIFIPROOF] bad ssid\n");
        return;
    }

    uint8_t frame[128];
    size_t n = 0;
    frame[n++] = 0x80;
    frame[n++] = 0x00;
    frame[n++] = 0x00;
    frame[n++] = 0x00;
    memset(frame + n, 0xff, 6);
    n += 6;
    memcpy(frame + n, bssid, 6);
    n += 6;
    memcpy(frame + n, bssid, 6);
    n += 6;
    frame[n++] = 0x00;
    frame[n++] = 0x00;
    memset(frame + n, 0, 8);
    n += 8;
    frame[n++] = 0x64;
    frame[n++] = 0x00;
    frame[n++] = 0x01;
    frame[n++] = 0x04;
    frame[n++] = 0x00;
    frame[n++] = (uint8_t)(ssid_len + 2);
    frame[n++] = 0x00;
    memcpy(frame + n, ssid, ssid_len);
    n += ssid_len;
    frame[n++] = 0x01;
    frame[n++] = 0x08;
    frame[n++] = 0x82;
    frame[n++] = 0x84;
    frame[n++] = 0x8b;
    frame[n++] = 0x96;
    frame[n++] = 0x0c;
    frame[n++] = 0x12;
    frame[n++] = 0x18;
    frame[n++] = 0x24;
    frame[n++] = 0x03;
    frame[n++] = 0x01;
    frame[n++] = s_channel ? s_channel : 6;

    esp_err_t ret = wp_tx_frame(frame, n);
    wp_puts("[WIFIPROOF BEACON] ");
    print_tx_result(ret, frame, n);
}

static void cmd_txcount(void) {
    char line[80];
    snprintf(line, sizeof line,
             "[WIFIPROOF TXCOUNT] count=%lu last_ret=%s\n",
             (unsigned long)s_tx_count, esp_err_to_name(s_last_ret));
    wp_puts(line);
}

void wifiproof_init(const wifiproof_io_t *io) {
    if (io) s_io = *io;
}

bool wifiproof_handle_line(const char *line) {
    if (strncmp(line, "WIFIPROOF", 9) != 0) return false;

    if (strcmp(line, "WIFIPROOF OFF") == 0) {
        wp_off();
        return true;
    }
    if (strcmp(line, "WIFIPROOF TXCOUNT") == 0) {
        cmd_txcount();
        return true;
    }
    if (strncmp(line, "WIFIPROOF ON ", 13) == 0) {
        unsigned ch = (unsigned)strtoul(line + 13, NULL, 0);
        wp_on((uint8_t)ch);
        return true;
    }
    if (strncmp(line, "WIFIPROOF CH ", 13) == 0) {
        unsigned ch = (unsigned)strtoul(line + 13, NULL, 0);
        wp_ch((uint8_t)ch);
        return true;
    }
    if (strncmp(line, "WIFIPROOF DEAUTH ", 17) == 0) {
        cmd_deauth_disassoc(line, 12, "DEAUTH");
        return true;
    }
    if (strncmp(line, "WIFIPROOF DASSOC ", 17) == 0) {
        cmd_deauth_disassoc(line, 10, "DASSOC");
        return true;
    }
    if (strncmp(line, "WIFIPROOF BEACON ", 17) == 0) {
        cmd_beacon(line);
        return true;
    }

    wp_puts("[WIFIPROOF] unknown subcommand\n");
    return true;
}
