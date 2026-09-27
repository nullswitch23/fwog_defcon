#include "ogemu.h"
#include "common/diag.h"
#include "common/link/link_frame.h"
#include "sensors/lis3dh.h"
#include "pdm/pdm_mic.h"
#include "common/link/link_uart.h"
#include "io_expander/ioexp_link.h"
#include "power/bq25896.h"
#include "vp_proto.h"
#include "bs_proto.h"
#include "ib_proto.h"
#include "tf_proto.h"
#include "te_proto.h"
#include "fob_proto.h"
#include "trf_proto.h"
#include "ph_proto.h"
#include "bd_proto.h"
#include "lf_proto.h"
#include "bb_proto.h"
#include "hk_proto.h"
#include "qg_proto.h"
#include "rg_proto.h"
#include "am_proto.h"
#include "dg_proto.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int32_t  s_ax = 0, s_ay = 0, s_az = 1000;
static uint32_t s_mic_rms;
static uint32_t s_mic_hz;
static bool     s_mic_pending;
static uint8_t  s_pdm_dummy[PDM_RAW_BUFFER_BYTES];
static int16_t  s_rssi = -120;
static uint32_t s_hz = 433920000u;
static uint32_t s_ook[OGEMU_OOK_MAX];
static unsigned s_nook;
static bool     s_link_ok;
#define OGEMU_RXQ 4096u
static uint8_t  s_rxq[OGEMU_RXQ];
static unsigned s_rxh, s_rxt;

void ogemu_world_get_accel_mg(int32_t *x, int32_t *y, int32_t *z) {
    if (x) *x = s_ax;
    if (y) *y = s_ay;
    if (z) *z = s_az;
}

uint32_t ogemu_world_get_mic_rms(void) { return s_mic_rms; }

int16_t ogemu_world_get_rssi(uint32_t *hz) {
    if (hz) *hz = s_hz;
    return s_rssi;
}

unsigned ogemu_world_ook_count(void) { return s_nook; }

void ogemu_world_accel_mg(int32_t x, int32_t y, int32_t z) {
    s_ax = x;
    s_ay = y;
    s_az = z;
    DIAG("[ogemu] world accel mg %d %d %d\n", (int)x, (int)y, (int)z);
}

void ogemu_world_mic_rms(uint32_t rms) {
    s_mic_rms = rms;
    s_mic_pending = true;
    DIAG("[ogemu] world mic_rms %u\n", (unsigned)rms);
}

void ogemu_world_mic_tone(uint32_t hz, uint32_t rms) {
    s_mic_hz = hz;
    ogemu_world_mic_rms(rms);
    DIAG("[ogemu] world mic_tone %u Hz rms %u\n", (unsigned)hz, (unsigned)rms);
}

void ogemu_world_rssi(int16_t dbm, uint32_t hz) {
    s_rssi = dbm;
    if (hz) s_hz = hz;
    DIAG("[ogemu] world rssi %d dBm hz %u\n",
         (int)s_rssi, (unsigned)s_hz);
}

static FILE *open_ook(const char *path) {
    FILE *f = fopen(path, "r");
    if (f) return f;
    const char *dir = ogemu_script_dir();
    if (!dir || !path) return NULL;
    const char *base = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    char alt[512];
    snprintf(alt, sizeof alt, "%s/%s", dir, base);
    f = fopen(alt, "r");
    if (f) return f;
    snprintf(alt, sizeof alt, "%s/%s", dir, path);
    return fopen(alt, "r");
}

int ogemu_world_ook_file(const char *path) {
    s_nook = 0;
    if (!path || !path[0]) {
        DIAG("[ogemu] world ook (cleared)\n");
        return 0;
    }
    FILE *f = open_ook(path);
    if (!f) {
        fprintf(stderr, "ogemu: cannot open ook file %s\n", path);
        return -1;
    }
    char line[128];
    while (fgets(line, sizeof line, f) && s_nook < OGEMU_OOK_MAX) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\0' || *p == '\n' || *p == '\r') continue;
        unsigned long us = strtoul(p, NULL, 10);
        if (us == 0ul) continue;
        s_ook[s_nook++] = (uint32_t)us;
    }
    fclose(f);
    DIAG("[ogemu] world ook %s edges %u first %u us\n",
         path, s_nook, s_nook ? (unsigned)s_ook[0] : 0u);
    return 0;
}

/* ---- HOST_TEST driver shells ---- */

static int16_t mg_to_raw_2g(int32_t mg) {
    int32_t digit = mg / 4;
    if (digit > 511) digit = 511;
    if (digit < -512) digit = -512;
    return (int16_t)(digit << 6);
}

void lis3dh_init(void) {}

bool lis3dh_configure(lis3dh_range_t range) {
    (void)range;
    return true;
}

bool lis3dh_process(int32_t move_threshold,
                    lis3dh_sample_t *out_sample,
                    lis3dh_motion_t *out_motion) {
    (void)move_threshold;
    if (out_sample) {
        out_sample->x = mg_to_raw_2g(s_ax);
        out_sample->y = mg_to_raw_2g(s_ay);
        out_sample->z = mg_to_raw_2g(s_az);
    }
    if (out_motion) {
        const int32_t m2 = s_ax * s_ax + s_ay * s_ay + s_az * s_az;
        const bool moving = m2 > (int32_t)1300 * 1300 || m2 < (int32_t)700 * 700;
        out_motion->x = out_motion->y = out_motion->z = moving;
        out_motion->any = moving;
    }
    return true;
}

bool pdm_mic_init(void *pio, unsigned sm) {
    (void)pio;
    (void)sm;
    memset(s_pdm_dummy, 0x55, sizeof s_pdm_dummy);
    return true;
}

bool pdm_mic_take_raw_buffer(const uint8_t **buf, size_t *len) {
    if (!s_mic_pending) return false;
    s_mic_pending = false;
    if (buf) *buf = s_pdm_dummy;
    if (len) *len = sizeof s_pdm_dummy;
    return true;
}

void pdm_mic_stop(void) { s_mic_pending = false; }

size_t pdm_mic_decode(fwog_cic_t *st, const uint8_t *raw, size_t len,
                      int16_t *out) {
    (void)st;
    (void)raw;
    (void)len;
    if (!out) return 0;
    int32_t a = (int32_t)s_mic_rms;
    if (a > 32767) a = 32767;
    if (s_mic_hz == 0u) {
        /* Square wave whose amplitude tracks injected RMS. Not PDM; it is
         * what VoltPet/TalkClip's fwog_rms_i16 sees. Energy at Nyquist. */
        for (unsigned i = 0; i < PDM_SAMPLE_BUFFER_SIZE; i++) {
            out[i] = (int16_t)((i & 1u) ? a : -a);
        }
        return PDM_SAMPLE_BUFFER_SIZE;
    }
    /* Sine at s_mic_hz on the 8 kHz PDM grid. Amplitude chosen so RMS of
     * a full-scale sine matches the injected rms (A = rms * sqrt(2)). */
    const double amp = (double)s_mic_rms * 1.41421356237;
    const double ph = 2.0 * 3.14159265358979323846 *
                      (double)s_mic_hz / (double)PDM_SAMPLE_RATE_HZ;
    for (unsigned i = 0; i < PDM_SAMPLE_BUFFER_SIZE; i++) {
        double v = amp * sin(ph * (double)i);
        if (v > 32767.0) v = 32767.0;
        if (v < -32767.0) v = -32767.0;
        out[i] = (int16_t)v;
    }
    return PDM_SAMPLE_BUFFER_SIZE;
}

bool fwog_link_uart_init(uint32_t baud) {
    (void)baud;
    s_link_ok = true;
    s_rxh = s_rxt = 0;
    DIAG("[ogemu] link uart stub (no main CPU)\n");
    return true;
}

void fwog_link_uart_deinit(void) { s_link_ok = false; }

void fwog_link_uart_write(const void *data, size_t len) {
    (void)data;
    (void)len;
}

bool fwog_link_uart_read(uint8_t *b) {
    if (!b || s_rxh == s_rxt) return false;
    *b = s_rxq[s_rxh];
    s_rxh = (s_rxh + 1u) % OGEMU_RXQ;
    return true;
}

bool fwog_link_uart_send_frame(const void *payload, size_t len) {
    (void)payload;
    DIAG("[ogemu] link TX %u bytes (dropped)\n", (unsigned)len);
    return len <= FWOG_LINK_MAX_PAYLOAD;
}

int ogemu_link_rx_payload(const void *payload, size_t len) {
    uint8_t frame[FWOG_LINK_MAX_PAYLOAD + FWOG_LINK_OVERHEAD];
    size_t n = fwog_link_encode(frame, sizeof frame, payload, len);
    if (!n) {
        fprintf(stderr, "ogemu: link payload %u bytes will not encode\n",
                (unsigned)len);
        return -1;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned next = (s_rxt + 1u) % OGEMU_RXQ;
        if (next == s_rxh) {
            fprintf(stderr, "ogemu: link RX queue full\n");
            return -1;
        }
        s_rxq[s_rxt] = frame[i];
        s_rxt = next;
    }
    DIAG("[ogemu] link RX payload %u bytes (framed %u)\n",
         (unsigned)len, (unsigned)n);
    return 0;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

int ogemu_world_link_hex(const char *hex) {
    uint8_t buf[256];
    unsigned n = 0u;
    if (!hex) return -1;
    while (*hex && n < sizeof buf) {
        if (*hex == ' ' || *hex == '\t') { hex++; continue; }
        int hi = hex_nibble(*hex++);
        if (!*hex) return -1;
        int lo = hex_nibble(*hex++);
        if (hi < 0 || lo < 0) return -1;
        buf[n++] = (uint8_t)((hi << 4) | lo);
    }
    if (!n) return -1;
    return ogemu_link_rx_payload(buf, n);
}

void ogemu_world_rf_burst(int16_t dbm, uint32_t hz, unsigned art) {
    vp_rf_t p;
    memset(&p, 0, sizeof p);
    p.type = VP_MSG_RF;
    p.ok = 1u;
    p.burst = 1u;
    p.art_id = (uint8_t)(art % 100u);
    p.rssi = dbm;
    p.hz = hz ? hz : 433920000u;
    ogemu_world_rssi(dbm, p.hz);
    (void)ogemu_link_rx_payload(&p, sizeof p);
    DIAG("[ogemu] world rf burst art %u\n", (unsigned)p.art_id);
}

/* Sweep edges copied from apps/bandscope/main/main.c so peak_bin and the
 * axis labels match what the board would paint for that band. */
static const uint32_t k_bs_f0[3]   = { 300000000u, 387000000u, 779000000u };
static const uint32_t k_bs_span[3] = {  48000000u,  77000000u, 149000000u };

int ogemu_world_bs_status(uint8_t band, uint8_t mode, int16_t peak_dbm,
                          uint32_t peak_hz, int16_t hit_dbm) {
    if (band > 2u) band = 2u;
    if (mode > 2u) mode = 2u;
    if (!peak_hz) peak_hz = 433920000u;
    bs_status_t st;
    memset(&st, 0, sizeof st);
    st.type = BS_MSG_ST;
    st.band = band;
    st.ok = 1u;
    st.mode = mode;
    st.f0_hz = k_bs_f0[band];
    st.step_hz = k_bs_span[band] / (BS_BINS - 1u);
    st.peak_hz = peak_hz;
    st.peak_dbm = peak_dbm;
    if (st.step_hz) {
        uint32_t bin = (peak_hz > st.f0_hz) ? (peak_hz - st.f0_hz) / st.step_hz : 0u;
        if (bin >= BS_BINS) bin = BS_BINS - 1u;
        st.peak_bin = (uint16_t)bin;
    }
    st.n_hits = 1u;
    st.hit[0].hz = peak_hz;
    st.hit[0].dbm = hit_dbm ? hit_dbm : peak_dbm;
    st.hit[0].band = band;
    for (unsigned i = 0; i < BS_BINS; i++) {
        const int d = (int)i - (int)st.peak_bin;
        const int mag = 70 - (d < 0 ? -d : d) * 3;
        st.bar[i] = mag > 4 ? (uint8_t)mag : 4u;
    }
    ogemu_world_rssi(peak_dbm, peak_hz);
    DIAG("[ogemu] world bs_status band %u peak %d dBm %u Hz\n",
         (unsigned)band, (int)peak_dbm, (unsigned)peak_hz);
    return ogemu_link_rx_payload(&st, sizeof st);
}

static int hex_byte(const char **pp) {
    const char *p = *pp;
    while (*p == ' ' || *p == '\t') p++;
    int hi = hex_nibble(*p);
    if (hi < 0 || !p[1]) return -1;
    int lo = hex_nibble(p[1]);
    if (lo < 0) return -1;
    *pp = p + 2;
    return (hi << 4) | lo;
}

int ogemu_world_ib_status(uint8_t state, int16_t rssi, int16_t peak_rssi,
                          uint8_t guess, const char *label, const char *hex) {
    ib_status_t st;
    memset(&st, 0, sizeof st);
    st.type = IB_MSG_ST;
    st.state = state;
    st.ok = 1u;
    st.guess = guess;
    st.rssi = rssi;
    st.peak_rssi = peak_rssi ? peak_rssi : rssi;
    st.bursts = 1u;
    st.edges = 48u;
    st.duration_ms = 12u;
    st.bits = 24u;
    st.freq_hz = 433920000u;
    st.cap_hz = 433920000u;
    st.pw_short_us = 300u;
    st.pw_long_us = 900u;
    st.gap_short_us = 300u;
    st.gap_long_us = 900u;
    st.temp_c_x10 = (int16_t)IB_TEMP_NONE;
    st.humidity = IB_HUM_NONE;
    st.slot = 0u;
    st.slots = IB_RING;
    st.mod = IB_MOD_ASK;
    st.coding = (guess == IB_GUESS_PPM) ? IB_GUESS_PPM : IB_GUESS_PWM;
    st.saved = 0u;
    st.wait_s = (state == IB_ST_ARMED) ? 8u : 0u;
    st.hunt = (state == IB_ST_ARMED) ? 1u : 0u;
    if (state == IB_ST_IDLE || state == IB_ST_ARMED) {
        st.edges = 0u;
        st.bits = 0u;
        st.hex_n = 0u;
    } else {
        static const uint8_t k_hist[IB_HIST_BINS] =
            { 4, 18, 28, 12, 8, 22, 30, 10, 6, 16, 24, 9 };
        memcpy(st.hist, k_hist, sizeof k_hist);
        if (hex && hex[0]) {
            const char *p = hex;
            while (st.hex_n < IB_HEX_BYTES) {
                int b = hex_byte(&p);
                if (b < 0) break;
                st.hex[st.hex_n++] = (uint8_t)b;
            }
        } else {
            st.hex[0] = 0xA5u;
            st.hex[1] = 0x5Au;
            st.hex_n = 2u;
        }
        if (label) {
            strncpy(st.label, label, IB_LABEL_LEN - 1u);
            st.label[IB_LABEL_LEN - 1u] = '\0';
        }
    }
    ogemu_world_rssi(rssi, st.freq_hz);
    DIAG("[ogemu] world ib_status state %u rssi %d guess %u\n",
         (unsigned)state, (int)rssi, (unsigned)guess);
    return ogemu_link_rx_payload(&st, sizeof st);
}

int ogemu_world_tf_status(int16_t rssi0, int16_t rssi1, uint32_t freq_hz) {
    tf_status_t st;
    memset(&st, 0, sizeof st);
    st.type = TF_MSG_ST;
    st.ok0 = 1u;
    st.ok1 = 1u;
    st.rssi0 = rssi0;
    st.rssi1 = rssi1;
    st.freq_hz = freq_hz ? freq_hz : 433920000u;
    ogemu_world_rssi(rssi1, st.freq_hz);
    DIAG("[ogemu] world tf_status %d / %d dBm\n", (int)rssi0, (int)rssi1);
    return ogemu_link_rx_payload(&st, sizeof st);
}

int ogemu_world_te_status(uint8_t preset, int16_t rssi, int16_t last_rssi,
                          uint16_t bursts, uint8_t in_burst, uint16_t last_ms) {
    te_status_t st;
    memset(&st, 0, sizeof st);
    st.type = TE_MSG_ST;
    st.preset = preset ? TE_PRESET_FREQ : 0u;
    st.ok = 1u;
    st.mode = TE_MODE_LISTEN;
    st.profile = 1u;
    st.in_burst = in_burst ? 1u : 0u;
    st.rssi = rssi;
    st.last_rssi = last_rssi;
    st.bursts = bursts;
    st.last_ms = last_ms;
    st.freq_hz = st.preset ? 433920000u : 315000000u;
    ogemu_world_rssi(rssi, st.freq_hz);
    DIAG("[ogemu] world te_status bursts %u rssi %d\n",
         (unsigned)bursts, (int)rssi);
    return ogemu_link_rx_payload(&st, sizeof st);
}

int ogemu_world_fob_status(uint8_t state, int16_t rssi, uint8_t slots,
                           uint8_t unused, uint16_t edges, uint32_t freq_hz) {
    fob_status_t st;
    memset(&st, 0, sizeof st);
    st.type = FOB_MSG_STATUS;
    st.state = state;
    st.ok = 1u;
    st.rssi_dbm = rssi;
    st.edges = edges ? edges : 80u;
    st.freq_hz = freq_hz ? freq_hz : 433920000u;
    st.mode = FOB_MODE_QUEUE;
    st.slots = slots;
    st.unused = unused;
    st.next = unused ? 0u : 0xFFu;
    st.last_total_us = 2400u;
    st.env[2] = 20u;
    st.env[3] = 40u;
    st.env[4] = 18u;
    ogemu_world_rssi(rssi, st.freq_hz);
    DIAG("[ogemu] world fob_status state %u unused %u\n",
         (unsigned)state, (unsigned)unused);
    return ogemu_link_rx_payload(&st, sizeof st);
}

int ogemu_world_trf_status(int16_t rssi0, int16_t rssi1,
                           uint32_t freq0_hz, uint32_t freq1_hz) {
    trf_status_t st;
    memset(&st, 0, sizeof st);
    st.type = TRF_MSG_ST;
    st.ok = 3u;
    st.rssi0 = rssi0;
    st.rssi1 = rssi1;
    st.freq0_hz = freq0_hz ? freq0_hz : TRF_HZ_TOP;
    st.freq1_hz = freq1_hz ? freq1_hz : TRF_HZ;
    ogemu_world_rssi(rssi1, st.freq1_hz);
    DIAG("[ogemu] world trf_status %d/%d dBm %u/%u Hz\n",
         (int)rssi0, (int)rssi1, (unsigned)st.freq0_hz, (unsigned)st.freq1_hz);
    return ogemu_link_rx_payload(&st, sizeof st);
}

int ogemu_world_ph_status(uint8_t hello, uint8_t scan, int8_t rssi,
                          uint8_t apple, const char *name) {
    ph_status_t st;
    memset(&st, 0, sizeof st);
    st.type = PH_MSG_ST;
    st.hello = hello ? 1u : 0u;
    st.scan_on = scan ? 1u : 0u;
    if (st.hello && st.scan_on) {
        st.n = 1u;
        st.total = 1u;
        st.dev[0].rssi = rssi;
        st.dev[0].apple = apple ? 1u : 0u;
        st.dev[0].addr[5] = 0x42u;
        if (name && name[0]) strncpy(st.dev[0].name, name, PH_NAME_N - 1u);
    }
    DIAG("[ogemu] world ph_status hello %u scan %u rssi %d\n",
         (unsigned)st.hello, (unsigned)st.scan_on, (int)rssi);
    return ogemu_link_rx_payload(&st, sizeof st);
}

static void am_fill_aps(am_status_t *st) {
    static const struct {
        const char *bssid;
        const char *ssid;
        uint8_t ch;
    } k[] = {
        { "a4:83:e7:01:00:01", "lab-6",     6 },
        { "a4:83:e7:01:00:02", "attic",     1 },
        { "a4:83:e7:01:00:03", "",         11 },
        { "a4:83:e7:01:00:04", "fwog-test", 6 },
        { "a4:83:e7:01:00:05", "orca-c6",   9 },
        { "a4:83:e7:01:00:06", "bench-ap",  6 },
    };
    const unsigned n = (unsigned)(sizeof k / sizeof k[0]);
    st->n = (uint8_t)n;
    for (unsigned i = 0; i < n; i++) {
        strncpy(st->tgt[i].bssid, k[i].bssid, AM_BSSID_N - 1u);
        if (k[i].ssid[0]) strncpy(st->tgt[i].ssid, k[i].ssid, AM_SSID_N - 1u);
        st->tgt[i].ch = k[i].ch;
    }
}

int ogemu_world_am_status(uint8_t hello, uint8_t wp_on, uint8_t sweep,
                          uint8_t armed, uint8_t mode, uint8_t flash,
                          uint8_t pct, uint8_t channel, uint8_t aps,
                          uint32_t tx, const char *log, const char *ret,
                          const char *why) {
    am_status_t st;
    memset(&st, 0, sizeof st);
    st.type = AM_MSG_ST;
    st.hello = hello ? 1u : 0u;
    st.wp_on = wp_on ? 1u : 0u;
    st.sweep = sweep ? 1u : 0u;
    st.armed = armed ? 1u : 0u;
    st.mode = mode ? AM_MODE_DASSOC : AM_MODE_DEAUTH;
    st.flash = flash;
    st.pct = pct;
    st.channel = (channel >= 1u && channel <= 14u) ? channel : 6u;
    st.tx_count = tx;
    if (log && log[0]) strncpy(st.log, log, AM_LOG_N - 1u);
    if (ret && ret[0]) strncpy(st.last_ret, ret, AM_RET_N - 1u);
    if (why && why[0]) strncpy(st.why, why, sizeof st.why - 1u);
    if (aps) am_fill_aps(&st);
    DIAG("[ogemu] world am_status hello %u on %u sweep %u flash %u n %u\n",
         (unsigned)st.hello, (unsigned)st.wp_on, (unsigned)st.sweep,
         (unsigned)st.flash, (unsigned)st.n);
    return ogemu_link_rx_payload(&st, sizeof st);
}

int ogemu_world_bd_status(uint8_t hello, uint8_t adv, uint8_t conn,
                          uint8_t lock, uint8_t flash, uint8_t pct) {
    bd_status_t st;
    memset(&st, 0, sizeof st);
    st.type = BD_MSG_ST;
    st.hello = hello ? 1u : 0u;
    st.adv = adv ? 1u : 0u;
    st.conn = conn ? 1u : 0u;
    st.lock = lock ? 1u : 0u;
    st.flash = flash;
    st.pct = pct;
    DIAG("[ogemu] world bd_status hello %u conn %u lock %u flash %u\n",
         (unsigned)st.hello, (unsigned)st.conn, (unsigned)st.lock,
         (unsigned)st.flash);
    return ogemu_link_rx_payload(&st, sizeof st);
}

int ogemu_world_lf_status(uint8_t hello, uint8_t ap_on, uint8_t clients,
                          uint8_t flash, uint8_t pct, uint32_t bytes,
                          uint8_t nfiles,
                          const char *ssid, const char *pass, const char *file) {
    lf_status_t st;
    memset(&st, 0, sizeof st);
    st.type = LF_MSG_ST;
    st.hello = hello ? 1u : 0u;
    st.ap_on = ap_on ? 1u : 0u;
    st.clients = clients;
    st.flash = flash;
    st.pct = pct;
    st.nfiles = nfiles;
    st.bytes = bytes;
    if (ssid && ssid[0]) strncpy(st.ssid, ssid, sizeof st.ssid - 1u);
    if (pass && pass[0]) strncpy(st.pass, pass, sizeof st.pass - 1u);
    if (file && file[0]) strncpy(st.file, file, sizeof st.file - 1u);
    DIAG("[ogemu] world lf_status hello %u ap %u clients %u flash %u bytes %u\n",
         (unsigned)st.hello, (unsigned)st.ap_on, (unsigned)st.clients,
         (unsigned)st.flash, (unsigned)st.bytes);
    return ogemu_link_rx_payload(&st, sizeof st);
}

int ogemu_world_bb_status(uint8_t hello, uint8_t game_on, uint8_t players,
                          uint8_t phase, uint8_t flash, uint8_t pct,
                          uint16_t tick, uint16_t heap_k, uint8_t step_100us,
                          const char *ssid, const char *pass, const char *why,
                          uint8_t bots, uint8_t clock_s, uint8_t sudden,
                          uint8_t teams) {
    bb_status_t st;
    memset(&st, 0, sizeof st);
    st.type = BB_MSG_ST;
    st.hello = hello ? 1u : 0u;
    st.game_on = game_on ? 1u : 0u;
    st.players = players;
    st.phase = phase;
    st.flash = flash;
    st.pct = pct;
    st.tick = tick;
    st.heap_k = heap_k;
    st.step_100us = step_100us;
    st.bots = bots;
    st.clock_s = clock_s;
    st.sudden = sudden ? 1u : 0u;
    st.teams = teams ? 1u : 0u;
    if (ssid && ssid[0]) strncpy(st.ssid, ssid, sizeof st.ssid - 1u);
    if (pass && pass[0]) strncpy(st.pass, pass, sizeof st.pass - 1u);
    if (why && why[0]) strncpy(st.why, why, sizeof st.why - 1u);
    DIAG("[ogemu] world bb_status hello %u game %u players %u phase %u bots %u teams %u\n",
         (unsigned)st.hello, (unsigned)st.game_on, (unsigned)st.players,
         (unsigned)st.phase, (unsigned)st.bots, (unsigned)st.teams);
    return ogemu_link_rx_payload(&st, sizeof st);
}

int ogemu_world_hk_status(uint8_t io_ok, uint8_t other_buses_hiz,
                          const char *addresses) {
    hk_status_t st;
    memset(&st, 0, sizeof st);
    st.type = HK_MSG_ST;
    st.io_ok = io_ok ? 1u : 0u;
    st.other_buses_hiz = other_buses_hiz ? 1u : 0u;
    if (addresses && addresses[0]) {
        char buf[96];
        strncpy(buf, addresses, sizeof buf - 1u);
        buf[sizeof buf - 1u] = '\0';
        for (char *tok = strtok(buf, ", "); tok && st.n < HK_MAX;
             tok = strtok(NULL, ", ")) {
            char *end = NULL;
            unsigned long v = strtoul(tok, &end, 16);
            if (end != tok && *end == '\0' && v <= 0x7Fu) {
                st.addr[st.n++] = (uint8_t)v;
            }
        }
    }
    DIAG("[ogemu] world hk_status io %u hiz %u found %u\n",
         (unsigned)st.io_ok, (unsigned)st.other_buses_hiz, (unsigned)st.n);
    return ogemu_link_rx_payload(&st, sizeof st);
}

int ogemu_world_qg_status(uint8_t io_ok, const char *name, const char *val,
                          int16_t plot) {
    qg_status_t st;
    memset(&st, 0, sizeof st);
    st.type = QG_MSG_ST;
    st.io_ok = io_ok ? 1u : 0u;
    if (name && name[0]) {
        unsigned hex = 0;
        st.n = 1u;
        strncpy(st.chan[0].name, name, QG_NAME_N - 1u);
        if (val && val[0]) strncpy(st.chan[0].val, val, QG_VAL_N - 1u);
        st.chan[0].plot = plot;
        if (sscanf(name, "0x%x", &hex) == 1 && hex <= 0x7Fu) {
            st.chan[0].addr = (uint8_t)hex;
        }
        if (!strcmp(name, "BME280") || !strcmp(name, "BH1750")) {
            st.chan[0].known = 1u;
        }
    }
    DIAG("[ogemu] world qg_status io %u n %u %s %s\n",
         (unsigned)st.io_ok, (unsigned)st.n,
         st.n ? st.chan[0].name : "-", st.n ? st.chan[0].val : "-");
    return ogemu_link_rx_payload(&st, sizeof st);
}

static void rg_take3(const char *s, uint8_t *a, uint8_t *b, uint8_t *c) {
    unsigned v[3] = {0};
    int n = 0;
    if (!s) s = "";
    while (*s && n < 3) {
        if (*s >= '0' && *s <= '9') {
            unsigned x = 0;
            while (*s >= '0' && *s <= '9') {
                x = x * 10u + (unsigned)(*s++ - '0');
            }
            if (x > 255u) x = 255u;
            v[n++] = x;
            if (*s == ',' || *s == ':') s++;
        } else {
            s++;
        }
    }
    if (n == 0) return;
    *a = (uint8_t)v[0];
    *b = (uint8_t)(n >= 2 ? v[1] : v[0]);
    *c = (uint8_t)(n >= 3 ? v[2] : v[0]);
}

int ogemu_world_rg_status(const char *packed) {
    rg_status_t st;
    memset(&st, 0, sizeof st);
    st.type = RG_MSG_ST;
    st.gpu = st.gpu_2m = st.gpu_10m = (uint8_t)RG_NA;
    st.tmp = st.tmp_2m = st.tmp_10m = (uint8_t)RG_NA;
    char buf[256];
    memset(buf, 0, sizeof buf);
    if (packed) strncpy(buf, packed, sizeof buf - 1u);
    char *parts[6] = {0};
    unsigned i = 0;
    for (char *tok = strtok(buf, "\t"); tok && i < 6u; tok = strtok(NULL, "\t")) {
        parts[i++] = tok;
    }
    if (parts[0] && parts[0][0]) strncpy(st.host, parts[0], sizeof st.host - 1u);
    if (parts[1]) rg_take3(parts[1], &st.cpu, &st.cpu_2m, &st.cpu_10m);
    if (parts[2]) rg_take3(parts[2], &st.ram, &st.ram_2m, &st.ram_10m);
    if (parts[3]) rg_take3(parts[3], &st.net, &st.net_2m, &st.net_10m);
    if (parts[4]) rg_take3(parts[4], &st.gpu, &st.gpu_2m, &st.gpu_10m);
    if (parts[5]) rg_take3(parts[5], &st.tmp, &st.tmp_2m, &st.tmp_10m);
    DIAG("[ogemu] world rg_status host %s cpu %u net %u gpu %u tmp %u\n",
         st.host[0] ? st.host : "-", (unsigned)st.cpu, (unsigned)st.net,
         (unsigned)st.gpu, (unsigned)st.tmp);
    return ogemu_link_rx_payload(&st, sizeof st);
}

int ogemu_world_dg_vol(unsigned count, uint32_t free_bytes) {
    dg_list_t m;
    memset(&m, 0, sizeof m);
    m.type = DG_MSG_LIST;
    m.flags = DG_FLAG_MOUNTED;
    m.count = (uint16_t)count;
    m.size = free_bytes ? free_bytes : 4000000u;
    DIAG("[ogemu] world dg_vol count %u free %u\n",
         (unsigned)m.count, (unsigned)m.size);
    return ogemu_link_rx_payload(&m, sizeof m);
}

int ogemu_world_dg_list(unsigned index, const char *name, uint16_t kind,
                        uint32_t size, int is_dir, int end) {
    dg_list_t m;
    memset(&m, 0, sizeof m);
    m.type = DG_MSG_LIST;
    m.index = (uint16_t)index;
    m.kind = kind;
    m.size = size;
    m.flags = is_dir ? DG_FLAG_DIR : 0u;
    if (name) {
        strncpy(m.name, name, DG_NAME_LEN - 1u);
        m.name[DG_NAME_LEN - 1u] = '\0';
    }
    DIAG("[ogemu] world dg_list %u %s\n", (unsigned)index, m.name);
    if (ogemu_link_rx_payload(&m, sizeof m) != 0) return -1;
    if (end) {
        memset(&m, 0, sizeof m);
        m.type = DG_MSG_LIST;
        m.flags = DG_FLAG_END;
        m.count = (uint16_t)(index + 1u);
        return ogemu_link_rx_payload(&m, sizeof m);
    }
    return 0;
}

int ogemu_world_dg_meta(const char *name, uint16_t kind, uint32_t size,
                        uint32_t duration_us, uint32_t sample_hz) {
    dg_meta_t m;
    memset(&m, 0, sizeof m);
    m.type = DG_MSG_META;
    m.kind = kind ? kind : DG_KIND_PCM16;
    m.size = size ? size : 16048u;
    m.duration_us = duration_us ? duration_us : 1000000u;
    m.sample_hz = sample_hz ? sample_hz : 8000u;
    if (name) {
        strncpy(m.name, name, DG_NAME_LEN - 1u);
        m.name[DG_NAME_LEN - 1u] = '\0';
    }
    strncpy(m.app, "talkclip", DG_APP_LEN - 1u);
    DIAG("[ogemu] world dg_meta %s kind %u\n", m.name, (unsigned)m.kind);
    return ogemu_link_rx_payload(&m, sizeof m);
}

bool fwog_ioexp_link_handle(const void *payload, size_t len) {
    (void)payload;
    (void)len;
    return false;
}

bool fwog_ioexp_link_set_antennas(fwog_ant_t radio1, fwog_ant_t radio2) {
    (void)radio1;
    (void)radio2;
    return true;
}

bool bq25896_read_vbat_mv(uint16_t *mv, bool *thermal_regulation) {
    if (mv) *mv = 3900u;
    if (thermal_regulation) *thermal_regulation = false;
    return true;
}
