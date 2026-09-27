/* FreeWili OG Bottlenose firmware for LanFerry + BattleBridge + PingHalo + BleDeck.
 *
 * Speaks a newline ASCII protocol on UART1 at 115200, GPIO16 TX / GPIO17 RX
 * (same as C6 ROM UART0). OG uart1 APP (TX GPIO8 / RX GPIO9) is the path
 * ROM download used. SWAP TX on GPIO9 (FPGA uart_rx_out) did not reach this
 * Orca's C6 RX. No CTS/RTS. Console logs go to USB Serial/JTAG, not this
 * UART. This image does not speak the original IO-app Orca protocol.
 *
 *   OG → C6    BN HELLO og / BN AP START / BN AP STOP / BN AP STAT / BN AP WIPE
 *              BN BLE START / BN BLE STOP
 *              BN HID START / BN HID STOP / BN HID KEY / BN HID CC
 *              BN HID FORGET / BN HID BAT=n
 *              BN GAME START / BN GAME STOP / BN GAME STAT / BN GAME GO
 *              BN GAME BOTS=0|1|2|3 / BN GAME TEAMS=0|1 / BN GAME WIPE
 *   C6 → OG    BN HELLO c6
 *              BN AP on ssid=... pass=... ip=192.168.4.1
 *              BN AP off / BN AP wipe
 *              BN STA clients=N files=N used=N last=...
 *              BN BLE on / BN BLE off
 *              BN ADV rssi=.. mac=aabbccddeeff apple=0|1 name=...
 *              BN HID on / BN HID off / BN HID conn=0|1
 *              BN HID lock=0|1 bonds=N / BN HID forget / BN HID bat=n
 *              BN GAME on ssid=... pass=... players=N phase=N tick=N heap=N
 *              BN GAME wipe
 *
 * SoftAP passwords are 8-char WPA2 (LanFerry alphabet) stored in C6 NVS
 * namespace fwog, keys ferry (FWOG-ferry) and arena (FWOG-arena). BN AP WIPE
 * and BN GAME WIPE mint a new one. BLE scan flags Apple
 * company ID 0x004C at advertisement level only. HID is a keyboard+consumer
 * remote named FWOG-BleDeck; scan and HID do not run at the same time.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "hid.h"

void ble_store_config_init(void);
#include "wifiproof.h"
#include "ferry.h"
#include "battlebridge.h"

static const char *TAG = "fwog-bn";

#define BN_UART        UART_NUM_1
#define BN_TX          16
#define BN_RX          17
#define BN_BAUD        115200
#define BN_LINE_MAX    160

static bool     s_ble_on;
static bool     s_ble_want;
static SemaphoreHandle_t s_tx;

static void bn_puts(const char *s) {
    if (s_tx) xSemaphoreTake(s_tx, portMAX_DELAY);
    uart_write_bytes(BN_UART, s, strlen(s));
    if (s_tx) xSemaphoreGive(s_tx);
}

static void copy_kv(const char *line, const char *key, char *dst, size_t n) {
    const char *p = strstr(line, key);
    if (!p || n == 0) {
        if (n) dst[0] = '\0';
        return;
    }
    p += strlen(key);
    size_t i = 0;
    while (*p && *p != ' ' && *p != '\r' && *p != '\n' && i + 1u < n) {
        dst[i++] = *p++;
    }
    dst[i] = '\0';
}

static void send_hello(void) { bn_puts("BN HELLO c6\n"); }

static bool wp_ap_is_on(void) {
    return ferry_ap_is_on() || battlebridge_is_on();
}

static void wp_ap_stop(void) {
    if (ferry_ap_is_on()) ferry_ap_stop();
    if (battlebridge_is_on()) battlebridge_stop();
}

static void parse_ad(const uint8_t *data, uint8_t len,
                     char *name, size_t nlen, int *apple) {
    *apple = 0;
    name[0] = '\0';
    uint8_t i = 0;
    while ((uint16_t)i + 1u < len) {
        const uint8_t elen = data[i];
        if (elen == 0 || (uint16_t)i + elen >= len) break;
        const uint8_t typ = data[i + 1];
        if ((typ == 0x08 || typ == 0x09) && elen >= 2 && nlen > 1) {
            size_t nl = (size_t)elen - 1u;
            if (nl >= nlen) nl = nlen - 1u;
            memcpy(name, data + i + 2, nl);
            name[nl] = '\0';
            for (char *p = name; *p; p++) {
                if (*p < 32 || *p > 126) *p = '.';
            }
        }
        if (typ == 0xFF && elen >= 3) {
            const uint16_t cid = (uint16_t)data[i + 2] | ((uint16_t)data[i + 3] << 8);
            if (cid == 0x004Cu) *apple = 1;
        }
        i = (uint8_t)(i + elen + 1u);
    }
}

static void report_adv(const struct ble_gap_disc_desc *d) {
    /* Throttle: same MAC within 400 ms only if RSSI moved by >= 3 dB. */
    typedef struct {
        uint8_t addr[6];
        int8_t  rssi;
        TickType_t t;
        uint8_t used;
    } adv_slot_t;
    static adv_slot_t cache[16];
    const TickType_t now = xTaskGetTickCount();
    int slot = -1, empty = -1;
    for (int i = 0; i < 16; i++) {
        if (!cache[i].used) {
            if (empty < 0) empty = i;
            continue;
        }
        if (memcmp(cache[i].addr, d->addr.val, 6) == 0) {
            slot = i;
            break;
        }
    }
    if (slot >= 0) {
        int dr = (int)d->rssi - (int)cache[slot].rssi;
        if (dr < 0) dr = -dr;
        if ((now - cache[slot].t) < pdMS_TO_TICKS(400) && dr < 3) return;
    } else {
        slot = empty >= 0 ? empty : 0;
        if (empty < 0) {
            TickType_t oldest = cache[0].t;
            slot = 0;
            for (int i = 1; i < 16; i++) {
                if (cache[i].t < oldest) {
                    oldest = cache[i].t;
                    slot = i;
                }
            }
        }
    }
    cache[slot].used = 1;
    memcpy(cache[slot].addr, d->addr.val, 6);
    cache[slot].rssi = d->rssi;
    cache[slot].t = now;

    char name[12];
    int apple = 0;
    parse_ad(d->data, d->length_data, name, sizeof name, &apple);
    char line[120];
    snprintf(line, sizeof line,
             "BN ADV rssi=%d mac=%02x%02x%02x%02x%02x%02x apple=%d name=%s\n",
             (int)d->rssi,
             d->addr.val[5], d->addr.val[4], d->addr.val[3],
             d->addr.val[2], d->addr.val[1], d->addr.val[0],
             apple, name[0] ? name : "-");
    bn_puts(line);
}

static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        if (s_ble_on) report_adv(&event->disc);
        return 0;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (s_ble_want) {
            struct ble_gap_disc_params p = { 0 };
            p.passive = 1;
            (void)ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &p,
                               gap_event, NULL);
        } else {
            s_ble_on = false;
            bn_puts("BN BLE off\n");
        }
        return 0;
    default:
        return 0;
    }
}

static void send_hid(void) {
    if (!fwog_hid_on()) {
        bn_puts("BN HID off\n");
        return;
    }
    bn_puts("BN HID on\n");
    char line[40];
    snprintf(line, sizeof line, "BN HID conn=%d\n", fwog_hid_connected() ? 1 : 0);
    bn_puts(line);
    snprintf(line, sizeof line, "BN HID lock=%d\n", fwog_hid_locked() ? 1 : 0);
    bn_puts(line);
}

static void ble_scan_start(void) {
    if (fwog_hid_on()) fwog_hid_stop();
    s_ble_want = true;
    struct ble_gap_disc_params p = { 0 };
    p.passive = 1;
    p.filter_duplicates = 0;
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc == 0 || rc == BLE_HS_EALREADY) {
        s_ble_on = true;
        bn_puts("BN BLE on\n");
    } else {
        ESP_LOGE(TAG, "ble_gap_disc rc=%d", rc);
        s_ble_want = false;
        bn_puts("BN BLE off\n");
    }
}

static void ble_scan_stop(void) {
    s_ble_want = false;
    (void)ble_gap_disc_cancel();
    s_ble_on = false;
    bn_puts("BN BLE off\n");
}

static void on_sync(void) {
    (void)ble_hs_util_ensure_addr(0);
    if (s_ble_want) ble_scan_start();
}

static void on_reset(int reason) {
    ESP_LOGW(TAG, "nimble reset %d", reason);
}

static void ble_host_task(void *param) {
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void handle_line(char *line) {
    if (strcmp(line, "BN AP STAT") != 0 && strncmp(line, "BN HELLO", 8) != 0 &&
        strcmp(line, "BN GAME STAT") != 0) {
        ESP_LOGI(TAG, "%s", line);
    }
    if (wifiproof_handle_line(line)) return;
    if (strncmp(line, "BN HELLO", 8) == 0) {
        send_hello();
        ferry_hello();
        if (battlebridge_is_on()) battlebridge_stat();
        if (s_ble_on) bn_puts("BN BLE on\n");
        send_hid();
        return;
    }
    if (strcmp(line, "BN AP START") == 0) {
        if (battlebridge_is_on()) battlebridge_stop();
        ferry_ap_start();
        return;
    }
    if (strcmp(line, "BN AP STOP") == 0) {
        ferry_ap_stop();
        return;
    }
    if (strcmp(line, "BN AP WIPE") == 0) {
        ferry_ap_wipe();
        return;
    }
    if (strcmp(line, "BN AP STAT") == 0) {
        ferry_stat();
        return;
    }
    if (strcmp(line, "BN BLE START") == 0) {
        if (battlebridge_is_on()) battlebridge_stop();
        ble_scan_start();
        return;
    }
    if (strcmp(line, "BN BLE STOP") == 0) {
        ble_scan_stop();
        return;
    }
    if (strcmp(line, "BN HID START") == 0) {
        if (battlebridge_is_on()) battlebridge_stop();
        ble_scan_stop();
        fwog_hid_start();
        send_hid();
        return;
    }
    if (strcmp(line, "BN HID STOP") == 0) {
        fwog_hid_stop();
        send_hid();
        return;
    }
    if (strncmp(line, "BN HID KEY ", 11) == 0) {
        char code_s[8], down_s[4];
        copy_kv(line, "code=", code_s, sizeof code_s);
        copy_kv(line, "down=", down_s, sizeof down_s);
        fwog_hid_key((uint8_t)atoi(code_s), down_s[0] == '1');
        return;
    }
    if (strncmp(line, "BN HID CC ", 10) == 0) {
        char use_s[8], down_s[4];
        copy_kv(line, "usage=", use_s, sizeof use_s);
        copy_kv(line, "down=", down_s, sizeof down_s);
        fwog_hid_cc((uint16_t)atoi(use_s), down_s[0] == '1');
        return;
    }
    if (strcmp(line, "BN HID FORGET") == 0) {
        fwog_hid_forget();
        send_hid();
        return;
    }
    if (strncmp(line, "BN HID BAT=", 11) == 0) {
        fwog_hid_set_battery((uint8_t)atoi(line + 11));
        return;
    }
    if (strcmp(line, "BN GAME START") == 0) {
        ble_scan_stop();
        fwog_hid_stop();
        if (ferry_ap_is_on()) ferry_ap_stop();
        (void)wifiproof_handle_line("WIFIPROOF OFF");
        battlebridge_start();
        return;
    }
    if (strcmp(line, "BN GAME STOP") == 0) {
        battlebridge_stop();
        return;
    }
    if (strcmp(line, "BN GAME STAT") == 0) {
        battlebridge_stat();
        return;
    }
    if (strcmp(line, "BN GAME GO") == 0) {
        battlebridge_go();
        return;
    }
    if (strncmp(line, "BN GAME BOTS=", 13) == 0) {
        unsigned skill = 0;
        if (line[13] >= '0' && line[13] <= '3') skill = (unsigned)(line[13] - '0');
        battlebridge_set_bots(skill);
        return;
    }
    if (strncmp(line, "BN GAME TEAMS=", 14) == 0) {
        battlebridge_set_teams(line[14] == '1');
        return;
    }
    if (strcmp(line, "BN GAME WIPE") == 0) {
        battlebridge_wipe();
        return;
    }
}

static void uart_task(void *param) {
    (void)param;
    char line[BN_LINE_MAX];
    unsigned n = 0;
    send_hello();
    for (;;) {
        uint8_t b;
        int r = uart_read_bytes(BN_UART, &b, 1, pdMS_TO_TICKS(1000));
        if (r <= 0) {
            send_hello();
            continue;
        }
        if (b == '\r') continue;
        if (b == '\n') {
            line[n] = '\0';
            if (n) handle_line(line);
            n = 0;
            continue;
        }
        if (n + 1u < BN_LINE_MAX) line[n++] = (char)b;
        else n = 0;
    }
}

void app_main(void) {
    s_tx = xSemaphoreCreateMutex();
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        e = nvs_flash_init();
    }
    ESP_ERROR_CHECK(e);

    uart_config_t uc = {
        .baud_rate = BN_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(BN_UART, 2048, 2048, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(BN_UART, &uc));
    ESP_ERROR_CHECK(uart_set_pin(BN_UART, BN_TX, BN_RX,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ferry_init(bn_puts, ap_netif);
    battlebridge_init(bn_puts);

    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_store_config_init();
    fwog_hid_register();
    fwog_hid_set_note(bn_puts);
    nimble_port_freertos_init(ble_host_task);

    const wifiproof_io_t wp_io = {
        .puts = bn_puts,
        .ap_stop = wp_ap_stop,
        .ap_is_on = wp_ap_is_on,
    };
    wifiproof_init(&wp_io);

    xTaskCreate(uart_task, "bn_uart", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "ready");
}
