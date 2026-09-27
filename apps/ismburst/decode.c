#include "decode.h"
#include <string.h>

#if defined(HOST_TEST)
#define IB_DIAG(...) ((void)0)
#else
#include "common/diag.h"
#define IB_DIAG(...) DIAG(__VA_ARGS__)
#endif

static uint16_t s_pulse[IB_MAX_EDGES];
static uint16_t s_gap[IB_MAX_EDGES];
static uint8_t  s_bits[IB_MAX_BITS];
static uint8_t  s_cells[1024];

static const uint16_t k_hist_edge[IB_HIST_BINS - 1u] = {
    80u, 120u, 180u, 270u, 400u, 600u, 900u, 1350u, 2000u, 3000u, 4500u
};

static void set_label(ib_decode_t *o, const char *s) {
    unsigned i = 0;
    for (; s[i] != '\0' && i + 1u < IB_LABEL_LEN; i++) o->label[i] = s[i];
    for (; i < IB_LABEL_LEN; i++) o->label[i] = '\0';
}

static void pack_msb(const uint8_t *bits, unsigned n, uint8_t *out, unsigned cap) {
    memset(out, 0, cap);
    for (unsigned i = 0; i < n && (i / 8u) < cap; i++) {
        if (bits[i]) out[i / 8u] |= (uint8_t)(0x80u >> (i % 8u));
    }
}

static void store_hex(ib_decode_t *o, const uint8_t *bits, unsigned n) {
    unsigned nbytes = (n + 7u) / 8u;
    if (nbytes > IB_HEX_BYTES) nbytes = IB_HEX_BYTES;
    pack_msb(bits, n, o->hex, IB_HEX_BYTES);
    o->hex_n = (uint8_t)nbytes;
}

static void fill_hist(const uint16_t *dur, uint16_t n, uint8_t *hist) {
    memset(hist, 0, IB_HIST_BINS);
    if (n == 0u) return;
    for (uint16_t i = 0; i < n; i++) {
        unsigned b = 0;
        while (b < (IB_HIST_BINS - 1u) && dur[i] >= k_hist_edge[b]) b++;
        if (hist[b] < 255u) hist[b]++;
    }
    uint8_t mx = 0;
    for (unsigned i = 0; i < IB_HIST_BINS; i++) {
        if (hist[i] > mx) mx = hist[i];
    }
    if (mx == 0u) return;
    for (unsigned i = 0; i < IB_HIST_BINS; i++) {
        hist[i] = (uint8_t)((hist[i] * 24u) / mx);
        if (hist[i] == 0u && mx > 0u) {
            /* keep empty bins empty; non-empty already scaled */
        }
    }
}

static void split_edges(const uint16_t *dur, uint16_t n, bool first_high,
                        uint16_t *np, uint16_t *ng) {
    *np = *ng = 0;
    for (uint16_t i = 0; i < n; i++) {
        const bool high = first_high ? ((i & 1u) == 0u) : ((i & 1u) == 1u);
        if (high) {
            if (*np < IB_MAX_EDGES) s_pulse[(*np)++] = dur[i];
        } else if (*ng < IB_MAX_EDGES) {
            s_gap[(*ng)++] = dur[i];
        }
    }
}

static void two_means(const uint16_t *x, uint16_t n,
                      uint16_t *lo, uint16_t *hi, uint16_t *nlo, uint16_t *nhi) {
    *lo = *hi = *nlo = *nhi = 0;
    if (n == 0u) return;
    uint16_t mn = x[0], mx = x[0];
    uint32_t sum = 0;
    for (uint16_t i = 0; i < n; i++) {
        if (x[i] < mn) mn = x[i];
        if (x[i] > mx) mx = x[i];
        sum += x[i];
    }
    const uint16_t avg = (uint16_t)(sum / n);
    if ((uint32_t)(mx - mn) < (uint32_t)mn / 4u + 80u) {
        *lo = *hi = avg;
        *nlo = n;
        return;
    }
    uint32_t c0 = mn, c1 = mx;
    uint16_t k0 = 0, k1 = 0;
    for (int it = 0; it < 8; it++) {
        uint32_t s0 = 0, s1 = 0;
        k0 = k1 = 0;
        for (uint16_t i = 0; i < n; i++) {
            const uint32_t d0 = x[i] > c0 ? x[i] - c0 : c0 - x[i];
            const uint32_t d1 = x[i] > c1 ? x[i] - c1 : c1 - x[i];
            if (d0 <= d1) {
                s0 += x[i];
                k0++;
            } else {
                s1 += x[i];
                k1++;
            }
        }
        if (k0) c0 = s0 / k0;
        if (k1) c1 = s1 / k1;
    }
    if (c0 > c1) {
        const uint32_t t = c0;
        c0 = c1;
        c1 = t;
        const uint16_t tk = k0;
        k0 = k1;
        k1 = tk;
    }
    *lo = (uint16_t)c0;
    *hi = (uint16_t)c1;
    *nlo = k0;
    *nhi = k1;
}

static bool bimodal(uint16_t lo, uint16_t hi, uint16_t nlo, uint16_t nhi,
                    uint16_t n) {
    if (nhi == 0u || nlo == 0u) return false;
    if (nlo < n / 10u || nhi < n / 10u) return false;
    return hi > (uint16_t)((lo * 3u) / 2u + 80u);
}

static unsigned ppm_bits(const uint16_t *gap, uint16_t ng,
                         uint16_t lo, uint16_t hi,
                         uint8_t *bits, unsigned cap) {
    uint16_t thr = (uint16_t)(((uint32_t)lo + (uint32_t)hi) / 2u);
    if (hi <= lo) thr = (uint16_t)(lo + 200u);
    uint32_t row = ((uint32_t)hi * 18u) / 10u;
    if (row < (uint32_t)hi + 800u) row = (uint32_t)hi + 800u;
    if (row > 65535u) row = 65535u;
    unsigned n = 0;
    for (uint16_t i = 0; i < ng && n < cap; i++) {
        if (gap[i] > (uint16_t)row) continue;
        bits[n++] = (gap[i] > thr) ? 1u : 0u;
    }
    return n;
}

static unsigned pwm_bits(const uint16_t *pulse, uint16_t np,
                         uint16_t lo, uint16_t hi,
                         uint8_t *bits, unsigned cap) {
    uint16_t thr = (uint16_t)(((uint32_t)lo + (uint32_t)hi) / 2u);
    if (hi <= lo) thr = (uint16_t)(lo + 80u);
    unsigned n = 0;
    for (uint16_t i = 0; i < np && n < cap; i++) {
        if (pulse[i] > 8000u) continue;
        bits[n++] = (pulse[i] > thr) ? 1u : 0u;
    }
    return n;
}

static void invert_bits(uint8_t *b, unsigned n) {
    for (unsigned i = 0; i < n; i++) b[i] ^= 1u;
}

/* True when `n` is one 36-bit row (optional trailing 0) or k repeating
 * copies of the same 36 bits. rtl_433 wants four repeats; we accept two
 * because a 4 s / 25 ms-gap capture often only keeps a couple of frames.
 * Offset hunting is how a fan remote's payload window-matched type 0x9. */
static bool aligned_36(const uint8_t *bits, unsigned n, unsigned *rows) {
    *rows = 0;
    if (n < 36u) return false;
    if (n == 36u || n == 37u) {
        *rows = 1u;
        return true;
    }
    if ((n % 36u) > 1u) return false;
    const unsigned r = n / 36u;
    if (r < 2u || r > 8u) return false;
    for (unsigned i = 1; i < r; i++) {
        if (memcmp(bits, bits + i * 36u, 36) != 0) return false;
    }
    *rows = r;
    return true;
}

static bool in_us(uint16_t v, uint16_t lo, uint16_t hi) {
    return v >= lo && v <= hi;
}

/* rtl_433 prologue.c: pulse ~500 us, short gap ~2000, long ~4000.
 * gap_limit 7000, reset_limit 10000. No checksum exists. */
static bool prologue_timing(uint16_t pw, uint16_t glo, uint16_t ghi) {
    if (!in_us(pw, 300u, 800u)) return false;
    if (!in_us(glo, 1400u, 2800u)) return false;
    if (!in_us(ghi, 2800u, 5600u)) return false;
    return ghi > (uint16_t)((glo * 3u) / 2u + 200u);
}

/* Nexus (protocol 19) is also 36-bit PPM but ~1000/2000 us gaps. */
static bool nexus_timing(uint16_t pw, uint16_t glo, uint16_t ghi) {
    if (!in_us(pw, 300u, 800u)) return false;
    if (!in_us(glo, 700u, 1400u)) return false;
    if (!in_us(ghi, 1600u, 2800u)) return false;
    return ghi > (uint16_t)((glo * 3u) / 2u + 80u);
}

static void diag_prologue(const char *why, const uint8_t *b, unsigned n,
                          unsigned rows, uint16_t pw, uint16_t glo, uint16_t ghi,
                          int16_t temp, int humidity, uint8_t type) {
    IB_DIAG("[ismburst] prologue %s: type=0x%X bits=%u rows=%u "
            "hex=%02X%02X%02X%02X%02X temp=%d hum=%d pw=%u gap=%u/%u "
            "(heuristic, not a SKU lookup)\n",
            why, (unsigned)type, n, rows,
            (unsigned)b[0], (unsigned)b[1], (unsigned)b[2],
            (unsigned)b[3], (unsigned)b[4],
            (int)temp, humidity, (unsigned)pw, (unsigned)glo, (unsigned)ghi);
}

/* Prologue / FreeTec NC-7104 — rtl_433 protocol 3.
 *
 * This is NOT a device database. prologue.c has no checksum. It accepts a
 * 36-bit PPM row whose type nibble is 0x9 or 0x5 (the source comment says
 * "0110 (5)"; 0110 is 0x6 — we follow the code's 0x50 / 0x90 tests), then
 * unpacks temp/RH. Cheap 433 MHz remotes (fans, doorbells) use similar
 * PPM/PWM; v002 slid a 36-bit window across every offset and both
 * polarities, so a fan control printed "Prologue weather".
 *
 * v003 requires: aligned 36 bits or repeating 36-bit rows, Prologue PPM
 * timing, type 0x9/0x5, temp -40..60 C, humidity 0..100 or 0xCC. Failures
 * stay coding+hex (unknown / fixed-code OOK), never the weather name. */
static bool try_prologue(const uint8_t *bits, unsigned n, ib_decode_t *o,
                         uint16_t pw, uint16_t glo, uint16_t ghi) {
    unsigned rows = 0;
    uint8_t b[5];
    pack_msb(bits, 36, b, 5);
    const uint8_t type = (uint8_t)(b[0] >> 4);
    int16_t temp_raw = (int16_t)((b[2] << 8) | (b[3] & 0xF0u));
    temp_raw = (int16_t)(temp_raw >> 4);
    const int humidity = ((b[3] & 0x0Fu) << 4) | (b[4] >> 4);

    if (!aligned_36(bits, n, &rows)) {
        if (type == 0x09u || type == 0x05u) {
            diag_prologue("skip (need 36-bit row or repeats; "
                          "doorbell/fan PPM often lands here)",
                          b, n, 0, pw, glo, ghi, temp_raw, humidity, type);
        }
        return false;
    }
    if (type != 0x09u && type != 0x05u) return false;
    if (!prologue_timing(pw, glo, ghi)) {
        diag_prologue("skip (PPM timing is not ~500/2000/4000 us)",
                      b, n, rows, pw, glo, ghi, temp_raw, humidity, type);
        return false;
    }
    if (temp_raw < -400 || temp_raw > 600) {
        diag_prologue("skip (temp not in -40..60 C)",
                      b, n, rows, pw, glo, ghi, temp_raw, humidity, type);
        return false;
    }
    if (humidity != 0xCC && humidity > 100) {
        diag_prologue("skip (humidity not 0..100 or 0xCC)",
                      b, n, rows, pw, glo, ghi, temp_raw, humidity, type);
        return false;
    }
    /* Same all-zeros / all-ones reject Nexus uses — random remotes. */
    if ((b[0] == 0x90u && b[1] == 0u && b[2] == 0u && b[3] == 0u) ||
        (b[0] == 0x9Fu && b[1] == 0xFFu && b[2] == 0xFFu && b[3] == 0xFFu) ||
        (b[0] == 0x50u && b[1] == 0u && b[2] == 0u && b[3] == 0u)) {
        diag_prologue("skip (degenerate id/payload)",
                      b, n, rows, pw, glo, ghi, temp_raw, humidity, type);
        return false;
    }

    const int channel = (int)(b[1] & 0x03u) + 1;
    o->guess = IB_GUESS_PROLOGUE;
    o->temp_c_x10 = temp_raw;
    o->humidity = (humidity == 0xCC) ? (uint8_t)IB_HUM_NONE : (uint8_t)humidity;
    o->channel = (uint8_t)channel;
    o->bits = 36;
    store_hex(o, bits, 36);
    set_label(o, "Prologue weather");
    diag_prologue("match", b, n, rows, pw, glo, ghi, temp_raw, humidity, type);
    return true;
}

/* Nexus / FreeTec NC-7345 (rtl_433 protocol 19): 36-bit PPM, const 0xF.
 * Same no-slide rule as Prologue — a nibble coincidence is not a station. */
static bool try_nexus(const uint8_t *bits, unsigned n, ib_decode_t *o,
                      uint16_t pw, uint16_t glo, uint16_t ghi) {
    unsigned rows = 0;
    if (!aligned_36(bits, n, &rows)) return false;
    if (!nexus_timing(pw, glo, ghi)) return false;
    (void)rows;
    uint8_t b[5];
    pack_msb(bits, 36, b, 5);
    if ((b[3] & 0xF0u) != 0xF0u) return false;
    if ((b[1] & 0x30u) == 0x30u) return false;
    if ((b[0] == 0u && b[2] == 0u && b[3] == 0u) ||
        (b[0] == 0xFFu && b[2] == 0xFFu && b[3] == 0xFFu)) {
        return false;
    }
    const int channel = (int)((b[1] & 0x30u) >> 4) + 1;
    const int16_t temp_raw = (int16_t)(((int16_t)((b[1] << 12) | (b[2] << 4))) >> 4);
    const int humidity = ((b[3] & 0x0Fu) << 4) | (b[4] >> 4);
    if (humidity != 0 && humidity > 100) return false;
    if (temp_raw < -400 || temp_raw > 600) return false;
    o->guess = IB_GUESS_NEXUS;
    o->temp_c_x10 = temp_raw;
    o->humidity = (humidity == 0) ? (uint8_t)IB_HUM_NONE : (uint8_t)humidity;
    o->channel = (uint8_t)channel;
    o->bits = 36;
    store_hex(o, bits, 36);
    set_label(o, "Nexus weather");
    return true;
}

static bool scan_weather(uint8_t *bits, unsigned n, ib_decode_t *o,
                         uint16_t pw, uint16_t glo, uint16_t ghi) {
    if (n < 36u) return false;
    uint8_t tmp[IB_MAX_BITS];
    if (n > IB_MAX_BITS) n = IB_MAX_BITS;
    memcpy(tmp, bits, n);
    /* Whole-frame polarity only. Do not slide. Invert covers the case
     * where two-means swapped short/long gaps (the existing fixture). */
    for (int pol = 0; pol < 2; pol++) {
        if (pol == 1) invert_bits(tmp, n);
        if (try_prologue(tmp, n, o, pw, glo, ghi)) return true;
        if (try_nexus(tmp, n, o, pw, glo, ghi)) return true;
    }
    return false;
}

static unsigned manchester_bits(const uint16_t *dur, uint16_t n, bool first_high,
                                uint16_t unit, uint8_t *bits, unsigned cap) {
    if (unit < 80u) unit = 80u;
    unsigned nc = 0;
    bool level = first_high;
    for (uint16_t i = 0; i < n && nc + 8u < sizeof s_cells; i++) {
        unsigned k = ((unsigned)dur[i] + unit / 2u) / unit;
        if (k < 1u) k = 1u;
        if (k > 8u) k = 8u;
        for (unsigned j = 0; j < k && nc < sizeof s_cells; j++) {
            s_cells[nc++] = level ? 1u : 0u;
        }
        level = !level;
    }
    unsigned nb = 0;
    for (unsigned i = 0; i + 1u < nc && nb < cap; i += 2u) {
        const uint8_t a = s_cells[i], b = s_cells[i + 1u];
        if (a == b) continue; /* clock slip: skip a half rather than abort */
        bits[nb++] = a ? 1u : 0u; /* G.E. Thomas: 10=1, 01=0 */
    }
    return nb;
}

static unsigned run_of_ones(const uint8_t *bits, unsigned n) {
    unsigned best = 0, cur = 0;
    for (unsigned i = 0; i < n; i++) {
        if (bits[i]) {
            cur++;
            if (cur > best) best = cur;
        } else {
            cur = 0;
        }
    }
    return best;
}

static bool looks_tpms(uint32_t freq_hz, uint32_t total_us, uint16_t n,
                       uint16_t mean_pulse) {
    const bool band = (freq_hz == 315000000u || freq_hz == 433920000u);
    return band && n >= 40u && total_us >= 3000u && total_us <= 30000u &&
           mean_pulse > 0u && mean_pulse < 200u;
}

static bool looks_pt2262(uint16_t lo, uint16_t hi, uint16_t nlo, uint16_t nhi,
                         unsigned nbits) {
    if (nbits < 24u || nbits > 32u) return false;
    if (nlo < 6u || nhi < 6u || lo < 150u || hi < 400u) return false;
    const uint32_t ratio10 = ((uint32_t)hi * 10u) / (lo ? lo : 1u);
    return ratio10 >= 25u && ratio10 <= 40u;
}

void ib_decode(const uint16_t *dur, uint16_t n, bool first_level,
               uint32_t freq_hz, uint32_t total_us, ib_decode_t *out) {
    ib_decode_mod(dur, n, first_level, freq_hz, total_us, IB_MOD_ASK, out);
}

void ib_decode_mod(const uint16_t *dur, uint16_t n, bool first_level,
                   uint32_t freq_hz, uint32_t total_us, uint8_t mod,
                   ib_decode_t *out) {
    const bool fsk = (mod == IB_MOD_2FSK);
    memset(out, 0, sizeof *out);
    out->temp_c_x10 = (int16_t)IB_TEMP_NONE;
    out->humidity = (uint8_t)IB_HUM_NONE;
    set_label(out, fsk ? "unknown FSK" : "unknown OOK");
    if (dur == NULL || n < 8u) return;

    fill_hist(dur, n, out->hist);

    uint16_t np = 0, ng = 0;
    split_edges(dur, n, first_level, &np, &ng);

    uint16_t plo = 0, phi = 0, pnlo = 0, pnhi = 0;
    uint16_t glo = 0, ghi = 0, gnlo = 0, gnhi = 0;
    two_means(s_pulse, np, &plo, &phi, &pnlo, &pnhi);
    two_means(s_gap, ng, &glo, &ghi, &gnlo, &gnhi);
    out->pw_short_us = plo;
    out->pw_long_us = phi ? phi : plo;
    out->gap_short_us = glo;
    out->gap_long_us = ghi ? ghi : glo;

    const bool pbi = bimodal(plo, phi, pnlo, pnhi, np);
    const bool gbi = bimodal(glo, ghi, gnlo, gnhi, ng);
    const uint16_t mean_p = plo;

    if (!fsk && looks_tpms(freq_hz, total_us, n, mean_p)) {
        out->coding = IB_GUESS_UNKNOWN;
        out->guess = IB_GUESS_TPMS;
        out->bits = 0;
        set_label(out, "TPMS-shaped");
        return;
    }

    uint8_t coding = IB_GUESS_UNKNOWN;
    if (gbi && !pbi) {
        coding = IB_GUESS_PPM;
    } else if (pbi && !gbi) {
        coding = IB_GUESS_PWM;
    } else if (!pbi && !gbi) {
        const uint16_t a = plo, b = glo;
        const uint16_t d = a > b ? (uint16_t)(a - b) : (uint16_t)(b - a);
        if (a >= 180u && a <= 800u && d * 4u < (uint16_t)(a + b + 1u)) {
            coding = IB_GUESS_MANCHESTER;
        } else {
            coding = IB_GUESS_PPM;
        }
    } else {
        const uint16_t dlo = plo > glo ? (uint16_t)(plo - glo) : (uint16_t)(glo - plo);
        const uint16_t dhi = phi > ghi ? (uint16_t)(phi - ghi) : (uint16_t)(ghi - phi);
        if ((uint32_t)dlo * 3u < (uint32_t)plo + (uint32_t)glo &&
            (uint32_t)dhi * 3u < (uint32_t)phi + (uint32_t)ghi) {
            coding = IB_GUESS_MANCHESTER;
        } else {
            coding = IB_GUESS_PPM;
        }
    }
    out->coding = coding;
    out->guess = coding;
    if (coding == IB_GUESS_PWM) set_label(out, fsk ? "PWM FSK" : "PWM OOK");
    else if (coding == IB_GUESS_PPM) set_label(out, fsk ? "PPM FSK" : "PPM OOK");
    else if (coding == IB_GUESS_MANCHESTER) {
        set_label(out, fsk ? "Manchester FSK" : "Manchester OOK");
    }

    unsigned nb = 0;
    if (coding == IB_GUESS_PWM) {
        nb = pwm_bits(s_pulse, np, plo, phi, s_bits, IB_MAX_BITS);
    } else if (coding == IB_GUESS_MANCHESTER) {
        uint16_t unit = plo;
        if (glo && glo < unit) unit = glo;
        nb = manchester_bits(dur, n, first_level, unit, s_bits, IB_MAX_BITS);
    } else {
        nb = ppm_bits(s_gap, ng, glo, ghi, s_bits, IB_MAX_BITS);
    }
    out->bits = (uint16_t)nb;
    if (nb > 0u) store_hex(out, s_bits, nb);

    if (scan_weather(s_bits, nb, out, plo, glo, ghi)) return;

    /* Weather toys are usually PPM even if the histogram leaned PWM. */
    if (coding != IB_GUESS_PPM) {
        unsigned n2 = ppm_bits(s_gap, ng, glo, ghi, s_bits, IB_MAX_BITS);
        if (scan_weather(s_bits, n2, out, plo, glo, ghi)) {
            out->coding = IB_GUESS_PPM;
            return;
        }
        if (coding == IB_GUESS_PWM) {
            nb = pwm_bits(s_pulse, np, plo, phi, s_bits, IB_MAX_BITS);
            out->bits = (uint16_t)nb;
            if (nb > 0u) store_hex(out, s_bits, nb);
        }
    }

    if (!fsk && pbi && looks_pt2262(plo, phi, pnlo, pnhi, 24u) && nb >= 24u) {
        out->guess = IB_GUESS_PT2262;
        out->coding = IB_GUESS_PWM;
        out->bits = 24;
        store_hex(out, s_bits, 24);
        set_label(out, "PT2262-like OOK");
        return;
    }

    if (coding == IB_GUESS_MANCHESTER ||
        (plo >= 180u && plo <= 800u && glo >= 180u && glo <= 800u)) {
        uint16_t unit = plo < glo ? plo : glo;
        if (unit < 80u) unit = 80u;
        unsigned nm = manchester_bits(dur, n, first_level, unit, s_bits, IB_MAX_BITS);
        if (run_of_ones(s_bits, nm) >= 12u && nm >= 32u) {
            out->coding = IB_GUESS_MANCHESTER;
            out->guess = IB_GUESS_OREGON;
            out->bits = (uint16_t)nm;
            store_hex(out, s_bits, nm);
            set_label(out, fsk ? "Oregon-like FSK" : "Oregon-like MC");
            return;
        }
        invert_bits(s_bits, nm);
        if (run_of_ones(s_bits, nm) >= 12u && nm >= 32u) {
            out->coding = IB_GUESS_MANCHESTER;
            out->guess = IB_GUESS_OREGON;
            out->bits = (uint16_t)nm;
            store_hex(out, s_bits, nm);
            set_label(out, fsk ? "Oregon-like FSK" : "Oregon-like MC");
            return;
        }
    }

    /* Doorbell / fan / fixed-code remotes: coding + hex, not a weather name. */
    if (nb >= 20u && nb <= 40u &&
        (coding == IB_GUESS_PPM || coding == IB_GUESS_PWM)) {
        out->guess = IB_GUESS_FIXED;
        set_label(out, fsk ? "fixed-code FSK" : "fixed-code OOK");
    }
}
