#ifndef OC_CODES_H
#define OC_CODES_H

#include <stdint.h>

typedef enum {
    OC_FN_POWER = 0,
    OC_FN_MUTE,
    OC_FN_CH_UP,
    OC_FN_CH_DN,
    OC_FN_VOL_UP,
    OC_FN_VOL_DN,
    OC_FN_COUNT
} oc_fn_t;

typedef struct {
    const char *brand;              /* short, 8.3-friendly, e.g. "Samsung" */
    uint32_t    code[OC_FN_COUNT];  /* 0 = unknown / do not blast */
} oc_brand_t;

extern const oc_brand_t k_oc_brands[];
extern const unsigned   k_oc_nbrands;

const char *oc_fn_name(oc_fn_t fn);

#endif
