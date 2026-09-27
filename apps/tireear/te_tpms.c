#include "te_tpms.h"
#include <string.h>

static const te_profile_desc_t k_prof[TE_PROFILE_N] = {
    { "AUTO",     0, 0, 0, 0, 0, 0, 0 },
    { "PMV-107J", 9, 0x13, 0, 8, 0, 4, TE_PROF_PMV107J },
    { "Toyota",   9, 0x07, 0x80, 8, 0, 4, TE_PROF_TOYOTA },
    { "Schrader", 8, 0x07, 0, 7, 0, 4, TE_PROF_SCHRADER },
};

uint8_t te_crc8(const uint8_t *data, unsigned len, uint8_t poly, uint8_t init) {
    uint8_t crc = init;
    for (unsigned i = 0; i < len; i++) {
        crc ^= data[i];
        for (unsigned b = 0; b < 8u; b++) {
            if (crc & 0x80u)
                crc = (uint8_t)((crc << 1) ^ poly);
            else
                crc = (uint8_t)(crc << 1);
        }
    }
    return crc;
}

const te_profile_desc_t *te_profile_desc(uint8_t idx) {
    if (idx >= TE_PROFILE_N) return &k_prof[0];
    return &k_prof[idx];
}

unsigned te_profile_count(void) { return TE_PROFILE_N; }

uint16_t te_guess_bit_us(const uint16_t *dur, uint16_t n, uint32_t freq_hz,
                         bool fsk) {
    (void)freq_hz;
    if (n == 0u) return fsk ? 260u : 320u;
    uint32_t sum = 0;
    unsigned cnt = 0;
    for (uint16_t i = 0; i < n; i++) {
        if (dur[i] < 40u || dur[i] > 2000u) continue;
        sum += dur[i];
        cnt++;
    }
    if (cnt == 0u) return fsk ? 260u : 320u;
    uint16_t avg = (uint16_t)(sum / cnt);
    if (avg < 180u) avg = 180u;
    if (avg > 600u) avg = 600u;
    return avg;
}

unsigned te_edges_to_bits(const uint16_t *dur, uint16_t n, bool first_level,
                          uint16_t bit_us, uint8_t *bits, unsigned cap) {
    if (bit_us < 80u) bit_us = 80u;
    bool level = first_level;
    unsigned nb = 0;
    for (uint16_t i = 0; i < n && nb < cap; i++) {
        uint32_t left = dur[i];
        while (left >= (uint32_t)(bit_us / 2u) && nb < cap) {
            const uint32_t slice = left > (uint32_t)bit_us ? (uint32_t)bit_us : left;
            left -= slice;
            if (slice >= (uint32_t)((bit_us * 3u) / 4u)) bits[nb++] = level ? 1u : 0u;
        }
        level = !level;
    }
    return nb;
}

unsigned te_pack_lsb_first(const uint8_t *bits, unsigned n, uint8_t *out,
                           unsigned cap) {
    memset(out, 0, cap);
    const unsigned nbytes = (n + 7u) / 8u;
    if (nbytes > cap) return cap;
    for (unsigned i = 0; i < n; i++) {
        if (bits[i]) out[i / 8u] |= (uint8_t)(1u << (i % 8u));
    }
    return nbytes;
}

static int16_t kpa_to_psi_x10(int kpa_x4) {
    /* kPa = raw * 0.25 typical; psi = kPa * 0.145038 */
    const int32_t kpa_x100 = (int32_t)kpa_x4 * 25;
    return (int16_t)((kpa_x100 * 145038 + 500000) / 1000000);
}

static bool decode_pmv107j(const uint8_t *bytes, te_decode_t *o) {
    uint8_t b[9];
    memcpy(b, bytes, 9);
    if (te_crc8(b, 8, 0x13, 0) != b[8]) return false;
    const unsigned id = ((unsigned)b[0] << 26) | ((unsigned)b[1] << 18) |
                        ((unsigned)b[2] << 10) | ((unsigned)b[3] << 2) |
                        ((unsigned)b[4] >> 6);
    o->id[0] = (uint8_t)(id >> 24);
    o->id[1] = (uint8_t)(id >> 16);
    o->id[2] = (uint8_t)(id >> 8);
    o->id[3] = (uint8_t)id;
    o->id_len = 4;
    if ((b[6] ^ 0xffu) != b[5]) return false;
    const int psi_raw = (int)b[5] - 40;
    o->psi_x10 = (int16_t)(psi_raw * 363 / 10); /* (psi_raw/0.363) approx */
    o->temp_c = (int16_t)((int)b[7] - 40);
    o->battery_ok = (uint8_t)((b[4] & 0x20u) == 0u);
    o->status = b[4] & 0x3fu;
    return true;
}

static bool decode_toyota(const uint8_t *bytes, te_decode_t *o) {
    uint8_t b[9];
    memcpy(b, bytes, 9);
    if (te_crc8(b, 8, 0x07, 0x80) != b[8]) return false;
    o->id[0] = b[0];
    o->id[1] = b[1];
    o->id[2] = b[2];
    o->id[3] = b[3];
    o->id_len = 4;
    const unsigned pressure1 =
        ((unsigned)(b[4] & 0x7fu) << 1) | (b[5] >> 7);
    const unsigned temp = ((unsigned)(b[5] & 0x7fu) << 1) | (b[6] >> 7);
    if ((b[7] ^ 0xffu) != (uint8_t)pressure1) return false;
    o->psi_x10 = (int16_t)(((int)pressure1 - 28) * 25); /* quarter-psi units */
    o->temp_c = (int16_t)((int)temp - 40);
    o->battery_ok = 1u;
    o->status = (uint8_t)(((b[4] & 0x80u) >> 7) | (b[6] & 0x7fu));
    return true;
}

static bool decode_schrader(const uint8_t *bytes, te_decode_t *o) {
    uint8_t b[8];
    memcpy(b, bytes, 8);
    if (te_crc8(b, 7, 0x07, 0) != b[7]) return false;
    o->id[0] = b[0];
    o->id[1] = b[1];
    o->id[2] = b[2];
    o->id[3] = b[3];
    o->id_len = 4;
    o->psi_x10 = kpa_to_psi_x10((int)b[4]);
    o->temp_c = (int16_t)((int)b[5] - 50);
    o->battery_ok = (uint8_t)((b[6] & 1u) == 0u);
    o->status = b[6];
    return true;
}

static bool try_bytes(uint8_t prof, const uint8_t *raw, unsigned raw_n,
                        te_decode_t *o) {
    if (raw_n < 8u) return false;
    te_decode_t tmp;
    memset(&tmp, 0, sizeof tmp);
    if (prof == 1u || prof == 0u) {
        if (raw_n >= 9u && decode_pmv107j(raw, &tmp)) {
            *o = tmp;
            o->profile = 1u;
            return true;
        }
    }
    if (prof == 2u || prof == 0u) {
        if (raw_n >= 9u && decode_toyota(raw, &tmp)) {
            *o = tmp;
            o->profile = 2u;
            return true;
        }
    }
    if (prof == 3u || prof == 0u) {
        if (raw_n >= 8u && decode_schrader(raw, &tmp)) {
            *o = tmp;
            o->profile = 3u;
            return true;
        }
    }
    return false;
}

bool te_tpms_decode(uint8_t profile, const uint8_t *bits, unsigned n_bits,
                    te_decode_t *out) {
    if (!out || !bits || n_bits < 48u) return false;
    memset(out, 0, sizeof *out);
    uint8_t packed[TE_RAW_BYTES];
    const unsigned pn = te_pack_lsb_first(bits, n_bits, packed, sizeof packed);
    out->bits_n = (uint8_t)(n_bits > TE_RAW_BITS ? TE_RAW_BITS : n_bits);
    memcpy(out->bits, bits, out->bits_n);
    out->raw_n = (uint8_t)(pn > TE_RAW_BYTES ? TE_RAW_BYTES : pn);
    memcpy(out->raw, packed, out->raw_n);

    const uint8_t prof = profile >= TE_PROFILE_N ? 0u : profile;
    for (unsigned off = 0; off + 8u <= pn; off++) {
        te_decode_t hit;
        if (try_bytes(prof, packed + off, pn - off, &hit)) {
            hit.raw_n = out->raw_n;
            memcpy(hit.raw, out->raw, out->raw_n);
            hit.bits_n = out->bits_n;
            memcpy(hit.bits, out->bits, out->bits_n);
            hit.ok = true;
            *out = hit;
            return true;
        }
    }
    out->ok = false;
    return false;
}

static int id_cmp(const uint8_t *a, unsigned alen, const uint8_t *b,
                  unsigned blen) {
    if (alen != blen) return (int)alen - (int)blen;
    for (unsigned i = 0; i < alen; i++) {
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    }
    return 0;
}

void te_scan_merge(te_scan_ent_t *log, unsigned cap, unsigned *n,
                   const te_decode_t *dec, int16_t rssi, uint32_t now_ms) {
    if (!log || !n || !dec) return;
    const uint16_t t = (uint16_t)(now_ms / 1000u);
    unsigned slot = *n;
    if (dec->id_len > 0u) {
        for (unsigned i = 0; i < *n; i++) {
            if (id_cmp(log[i].id, log[i].id_len, dec->id, dec->id_len) == 0) {
                slot = i;
                break;
            }
        }
        if (slot == *n && *n >= cap) return;
    } else if (*n >= cap) {
        return;
    }
    te_scan_ent_t *e = &log[slot];
    if (slot == *n && *n < cap) (*n)++;
    else if (slot == *n && *n >= cap) return;

    if (e->bursts == 0u) {
        e->first_ms = t;
        e->id_len = dec->id_len;
        if (dec->id_len) memcpy(e->id, dec->id, dec->id_len);
        e->profile = dec->profile;
        e->raw_n = dec->raw_n;
        memcpy(e->raw, dec->raw, dec->raw_n);
    }
    e->last_ms = t;
    e->bursts++;
    if (rssi > e->peak_rssi) e->peak_rssi = rssi;
    if (dec->ok) {
        e->crc_ok = 1u;
        e->psi_x10 = dec->psi_x10;
        e->temp_c = dec->temp_c;
        e->profile = dec->profile;
    }
}
