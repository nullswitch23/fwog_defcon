#ifndef TF_PROTO_H
#define TF_PROTO_H
#include <stdint.h>

#ifdef _MSC_VER
#pragma pack(push, 1)
#define TF_PACKED
#else
#define TF_PACKED __attribute__((packed))
#endif

#define TF_MSG_CMD 0x42u
#define TF_MSG_ST  0x43u
#define TF_CMD_LISTEN 0u
#define TF_CMD_BEACON 1u

/* CC1101 silicon bands (datasheet). Gaps 348-387 and 464-779 are illegal. */
#define TF_BAND_LOW_MIN   300000000u
#define TF_BAND_LOW_MAX   348000000u
#define TF_BAND_MID_MIN   387000000u
#define TF_BAND_MID_MAX   464000000u
#define TF_BAND_HIGH_MIN  779000000u
#define TF_BAND_HIGH_MAX  928000000u
#define TF_KHZ_STEP       1000u

static inline int tf_hz_ok(uint32_t hz) {
    return (hz >= TF_BAND_LOW_MIN && hz <= TF_BAND_LOW_MAX) ||
           (hz >= TF_BAND_MID_MIN && hz <= TF_BAND_MID_MAX) ||
           (hz >= TF_BAND_HIGH_MIN && hz <= TF_BAND_HIGH_MAX);
}

typedef struct TF_PACKED {
    uint8_t  type;
    uint8_t  cmd;      /* LISTEN or BEACON (beacon is opt-in TX) */
    uint8_t  _pad[2];
    uint32_t freq_hz;
} tf_cmd_t;
_Static_assert(sizeof(tf_cmd_t) == 8, "tf_cmd_t");

typedef struct TF_PACKED {
    uint8_t  type;
    uint8_t  beacon;
    uint8_t  ok0;
    uint8_t  ok1;
    int16_t  rssi0;
    int16_t  rssi1;
    uint32_t freq_hz;
} tf_status_t;
_Static_assert(sizeof(tf_status_t) == 12, "tf_status_t");

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif
