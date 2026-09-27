/* ASK/OOK bit decode, rolling-counter detection, and waveform synthesis
 * for FobReplay PREDICT mode. Self-contained; no external deps. */
#ifndef FOB_PREDICT_H
#define FOB_PREDICT_H
#include <stdbool.h>
#include <stdint.h>

#define FOB_PRED_MAX_BITS   128u
#define FOB_PRED_MAX_BYTES  (FOB_PRED_MAX_BITS / 8u)

#define FOB_PRED_CTR 0u
#define FOB_PRED_KL  1u

typedef struct {
    uint8_t  nbits;
    uint8_t  bits[FOB_PRED_MAX_BYTES];
    uint16_t short_us;
    uint16_t long_us;
    uint32_t bit_us;
    uint16_t preamble_edges;
} fob_decoded_t;

typedef struct {
    uint8_t  ncaptures;
    fob_decoded_t frame[8];
    /* PREDICT-CTR */
    bool     ctr_ok;
    bool     ctr_be;
    uint8_t  ctr_off;
    uint8_t  ctr_bits;
    uint16_t ctr_delta;
    uint16_t ctr_last;
    /* PREDICT-KL */
    bool     kl_ok;
    uint64_t kl_key;
    uint32_t kl_serial;
    /* synthesized prediction */
    bool     pred_ok;
    uint16_t pred_ctr;
    uint32_t pred_frame;
    fob_decoded_t pred_bits;
} fob_predict_t;

void fob_predict_init(fob_predict_t *p);
void fob_predict_clear(fob_predict_t *p);

bool fob_decode_ook(const uint16_t *dur, uint16_t n, bool first_level,
                    fob_decoded_t *out);

bool fob_predict_add(fob_predict_t *p, const uint16_t *dur, uint16_t n,
                     bool first_level, uint8_t predictor);

bool fob_predict_update(fob_predict_t *p, uint8_t predictor);

bool fob_predict_synth(const fob_predict_t *p, const uint16_t *ref_dur,
                         uint16_t ref_n, bool first_level,
                         uint16_t *out_dur, uint16_t *out_n, uint16_t max_n);

#endif
