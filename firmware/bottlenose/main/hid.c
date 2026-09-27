/* BLE HID keyboard + consumer control for BleDeck. NimBLE GATT.
 *
 * Windows "try connecting your device again" is almost always HOGP
 * discovery without a bond: the name is in the advert, then GATT reads
 * of the Report Map fail or pairing is dropped. HOGP wants Security Mode
 * 1 Level 2, so Report Map / reports are encrypted, we initiate pairing
 * on connect, and REPEAT_PAIRING deletes the stale bond and retries when
 * pairing is open.
 *
 * After the first bond, advertising is whitelist-filtered to that peer
 * (pairing lock). Gray hold on the OG sends BN HID FORGET, which wipes
 * NVS bonds and opens undirected advertising again.
 */
#include "hid.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "nimble/hci_common.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "fwog-hid";
static const char k_name[] = "FWOG-BleDeck";

static uint16_t s_conn;
static uint16_t s_kbd_handle;
static uint16_t s_cc_handle;
static uint16_t s_bat_handle;
static bool     s_on;
static bool     s_conn_ok;
static bool     s_open; /* true after FORGET until a new bond encrypts */
static uint8_t  s_kbd[8];
static uint8_t  s_cc[2];
static uint8_t  s_proto = 1; /* report protocol */
static void   (*s_note)(const char *line);

/* Keyboard (ID 1) + Consumer (ID 2). Report IDs live in the Report Reference
 * descriptors, not in the payload we notify. */
static const uint8_t k_map[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
    0x85, 0x01,
    0x05, 0x07,
    0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
    0xC0,
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01,
    0x85, 0x02,
    0x15, 0x00, 0x26, 0xFF, 0x03,
    0x19, 0x00, 0x2A, 0xFF, 0x03,
    0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
    0xC0
};

/* bcdHID 1.11, country 0, RemoteWake|NormallyConnectable. */
static const uint8_t k_hid_info[] = { 0x11, 0x01, 0x00, 0x03 };
static const uint8_t k_kbd_ref[]  = { 0x01, 0x01 }; /* id 1, Input */
static const uint8_t k_cc_ref[]   = { 0x02, 0x01 };
static const uint8_t k_out_ref[]  = { 0x01, 0x02 }; /* id 1, Output LEDs */
/* USB vendor source, Intrepid 093C, product BD01, version 0001. */
static const uint8_t k_pnp[]      = { 0x02, 0x3C, 0x09, 0x01, 0xBD, 0x01, 0x00 };
static uint8_t s_battery = 100;

static ble_uuid16_t s_adv_uuids[2] = {
    BLE_UUID16_INIT(0x1812),
    BLE_UUID16_INIT(0x180F),
};

#define HID_READ  (BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC)
#define HID_NOTIFY (BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | \
                    BLE_GATT_CHR_F_NOTIFY)
#define HID_WRITE (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | \
                   BLE_GATT_CHR_F_WRITE_ENC)

static void note(const char *line) {
    ESP_LOGI(TAG, "%s", line);
    if (s_note) s_note(line);
}

static int copy_out(struct os_mbuf *om, const void *p, uint16_t n) {
    return os_mbuf_append(om, p, n) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int chr_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt,
                      void *arg) {
    (void)conn;
    (void)attr;
    const intptr_t which = (intptr_t)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        switch (which) {
        case 1: return copy_out(ctxt->om, k_hid_info, sizeof k_hid_info);
        case 2: return copy_out(ctxt->om, k_map, sizeof k_map);
        case 3: return copy_out(ctxt->om, &s_proto, 1);
        case 4: return copy_out(ctxt->om, s_kbd, sizeof s_kbd);
        case 5: return copy_out(ctxt->om, s_cc, sizeof s_cc);
        case 6: return copy_out(ctxt->om, &s_battery, 1);
        case 7: {
            const char *m = "FreeWili OG";
            return copy_out(ctxt->om, m, (uint16_t)strlen(m));
        }
        case 8: return copy_out(ctxt->om, k_pnp, sizeof k_pnp);
        default: return BLE_ATT_ERR_UNLIKELY;
        }
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t tmp[8];
        uint16_t n = sizeof tmp;
        (void)ble_hs_mbuf_to_flat(ctxt->om, tmp, n, &n);
        if (which == 3 && n >= 1) s_proto = tmp[0] ? 1u : 0u;
        return 0;
    }
    return 0;
}

static int dsc_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt,
                      void *arg) {
    (void)conn;
    (void)attr;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_DSC) return 0;
    const intptr_t which = (intptr_t)arg;
    if (which == 1) return copy_out(ctxt->om, k_kbd_ref, sizeof k_kbd_ref);
    if (which == 2) return copy_out(ctxt->om, k_cc_ref, sizeof k_cc_ref);
    if (which == 3) return copy_out(ctxt->om, k_out_ref, sizeof k_out_ref);
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def k_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180A),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0x2A29),
                .access_cb = chr_access,
                .arg = (void *)(intptr_t)7,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A50),
                .access_cb = chr_access,
                .arg = (void *)(intptr_t)8,
                .flags = BLE_GATT_CHR_F_READ,
            },
            { 0 }
        }
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180F),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0x2A19),
                .access_cb = chr_access,
                .arg = (void *)(intptr_t)6,
                .val_handle = &s_bat_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 }
        }
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1812),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4A),
                .access_cb = chr_access,
                .arg = (void *)(intptr_t)1,
                .flags = HID_READ,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4B),
                .access_cb = chr_access,
                .arg = (void *)(intptr_t)2,
                .flags = HID_READ,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4C),
                .access_cb = chr_access,
                .arg = (void *)(intptr_t)0,
                .flags = HID_WRITE,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4E),
                .access_cb = chr_access,
                .arg = (void *)(intptr_t)3,
                .flags = HID_READ | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4D),
                .access_cb = chr_access,
                .arg = (void *)(intptr_t)4,
                .val_handle = &s_kbd_handle,
                .flags = HID_NOTIFY,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2908),
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = dsc_access,
                        .arg = (void *)(intptr_t)1,
                    },
                    { 0 }
                }
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4D),
                .access_cb = chr_access,
                .arg = (void *)(intptr_t)5,
                .val_handle = &s_cc_handle,
                .flags = HID_NOTIFY,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2908),
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = dsc_access,
                        .arg = (void *)(intptr_t)2,
                    },
                    { 0 }
                }
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4D),
                .access_cb = chr_access,
                .arg = (void *)(intptr_t)0,
                .flags = HID_READ | HID_WRITE,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2908),
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = dsc_access,
                        .arg = (void *)(intptr_t)3,
                    },
                    { 0 }
                }
            },
            { 0 }
        }
    },
    { 0 }
};

static void notify(uint16_t handle, const void *p, uint16_t n) {
    if (!s_conn_ok || handle == 0) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(p, n);
    if (!om) return;
    int rc = ble_gatts_notify_custom(s_conn, handle, om);
    if (rc) ESP_LOGW(TAG, "notify rc=%d", rc);
}

static int bond_count(void) {
    ble_addr_t peers[8];
    int n = 0;
    if (ble_store_util_bonded_peers(peers, &n, 8) != 0) return 0;
    return n;
}

static bool locked(void) {
    return !s_open && bond_count() > 0;
}

static void note_lock(void) {
    char line[40];
    snprintf(line, sizeof line, "BN HID lock=%d bonds=%d\n",
             locked() ? 1 : 0, bond_count());
    note(line);
}

static void load_wl(void) {
    ble_addr_t peers[8];
    int n = 0;
    if (ble_store_util_bonded_peers(peers, &n, 8) != 0 || n <= 0) {
        (void)ble_gap_wl_set(NULL, 0);
        return;
    }
    int rc = ble_gap_wl_set(peers, (uint8_t)n);
    if (rc) ESP_LOGW(TAG, "wl_set %d n=%d", rc, n);
}

static int gap_hid(struct ble_gap_event *ev, void *arg);

static void advertise(void) {
    struct ble_hs_adv_fields f = { 0 };
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.appearance = 0x03C1;
    f.appearance_is_present = 1;
    f.uuids16 = s_adv_uuids;
    f.num_uuids16 = 2;
    f.uuids16_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&f);
    if (rc) {
        ESP_LOGE(TAG, "adv fields %d", rc);
        return;
    }
    /* Name in the scan response so the 31-byte adv stays HID-looking. */
    struct ble_hs_adv_fields rsp = { 0 };
    rsp.name = (uint8_t *)k_name;
    rsp.name_len = (uint8_t)strlen(k_name);
    rsp.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc) ESP_LOGW(TAG, "scan rsp %d", rc);

    const bool lock = locked();
    if (lock) load_wl();
    else (void)ble_gap_wl_set(NULL, 0);

    uint8_t own = BLE_OWN_ADDR_PUBLIC;
    (void)ble_hs_id_infer_auto(0, &own);
    struct ble_gap_adv_params p = { 0 };
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    p.filter_policy = lock ? BLE_HCI_ADV_FILT_CONN : BLE_HCI_ADV_FILT_NONE;
    rc = ble_gap_adv_start(own, NULL, BLE_HS_FOREVER, &p, gap_hid, NULL);
    if (rc && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "adv start %d", rc);
        s_on = false;
        note("BN HID off\n");
    } else {
        s_on = true;
        note("BN HID on\n");
        note_lock();
    }
}

static int gap_hid(struct ble_gap_event *ev, void *arg) {
    (void)arg;
    char line[48];
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status == 0) {
            s_conn = ev->connect.conn_handle;
            s_conn_ok = true;
            note("BN HID conn=1\n");
            int rc = ble_gap_security_initiate(s_conn);
            snprintf(line, sizeof line, "BN HID pair rc=%d\n", rc);
            note(line);
        } else {
            snprintf(line, sizeof line, "BN HID fail=%d\n",
                     (int)ev->connect.status);
            note(line);
            if (s_on) advertise();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        s_conn_ok = false;
        s_conn = 0;
        snprintf(line, sizeof line, "BN HID disc=0x%02x\n",
                 (unsigned)ev->disconnect.reason);
        note(line);
        note("BN HID conn=0\n");
        if (s_on) advertise();
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        snprintf(line, sizeof line, "BN HID enc=%d\n",
                 (int)ev->enc_change.status);
        note(line);
        if (ev->enc_change.status == 0) {
            struct ble_gap_conn_desc d;
            int n = bond_count();
            if (!s_open && n > 1 &&
                ble_gap_conn_find(ev->enc_change.conn_handle, &d) == 0) {
                note("BN HID refuse\n");
                (void)ble_store_util_delete_peer(&d.peer_id_addr);
                (void)ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
                return 0;
            }
            s_open = false;
            note_lock();
        }
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        if (locked()) {
            note("BN HID rebond ignore\n");
            return BLE_GAP_REPEAT_PAIRING_IGNORE;
        }
        struct ble_gap_conn_desc d;
        if (ble_gap_conn_find(ev->repeat_pairing.conn_handle, &d) == 0) {
            ble_store_util_delete_peer(&d.peer_id_addr);
        }
        note("BN HID rebond\n");
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (s_on && !s_conn_ok) advertise();
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        return 0;
    default:
        return 0;
    }
}

void fwog_hid_set_note(void (*cb)(const char *line)) {
    s_note = cb;
}

void fwog_hid_register(void) {
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(k_name);
    ble_svc_gap_device_appearance_set(0x03C1);
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    int rc = ble_gatts_count_cfg(k_svcs);
    if (rc) ESP_LOGE(TAG, "count_cfg %d", rc);
    rc = ble_gatts_add_svcs(k_svcs);
    if (rc) ESP_LOGE(TAG, "add_svcs %d", rc);
}

void fwog_hid_start(void) {
    (void)ble_gap_adv_stop();
    s_on = true;
    if (!s_conn_ok) advertise();
}

void fwog_hid_stop(void) {
    s_on = false;
    if (s_conn_ok) {
        (void)ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
        s_conn_ok = false;
        s_conn = 0;
    }
    (void)ble_gap_adv_stop();
}

void fwog_hid_forget(void) {
    s_open = true;
    ble_addr_t peers[8];
    int n = 0;
    if (ble_store_util_bonded_peers(peers, &n, 8) == 0) {
        for (int i = 0; i < n; i++) {
            (void)ble_store_util_delete_peer(&peers[i]);
        }
    }
    (void)ble_store_clear();
    (void)ble_gap_wl_set(NULL, 0);
    note("BN HID forget\n");
    note_lock();
    if (s_conn_ok) {
        (void)ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
    } else if (s_on) {
        (void)ble_gap_adv_stop();
        advertise();
    }
}

bool fwog_hid_on(void) { return s_on; }
bool fwog_hid_connected(void) { return s_conn_ok; }
bool fwog_hid_locked(void) { return locked(); }

void fwog_hid_key(uint8_t keycode, int down) {
    memset(s_kbd, 0, sizeof s_kbd);
    if (down && keycode) s_kbd[2] = keycode;
    notify(s_kbd_handle, s_kbd, sizeof s_kbd);
}

void fwog_hid_cc(uint16_t usage, int down) {
    uint16_t u = down ? usage : 0u;
    s_cc[0] = (uint8_t)u;
    s_cc[1] = (uint8_t)(u >> 8);
    notify(s_cc_handle, s_cc, sizeof s_cc);
}

void fwog_hid_set_battery(uint8_t pct) {
    if (pct > 100u) pct = 100u;
    if (pct == s_battery) return;
    s_battery = pct;
    char line[32];
    snprintf(line, sizeof line, "BN HID bat=%u\n", (unsigned)pct);
    note(line);
    notify(s_bat_handle, &s_battery, 1);
}
