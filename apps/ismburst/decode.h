/* rtl_433-style pulse analysis that fits on an RP2040 + CC1101.
 *
 * This is not a port of rtl_433 (a PC SDR program) and it is not a SKU
 * database of remotes. It takes OOK edge durations from CC1101 async-serial
 * RX and returns coding (PWM / PPM / Manchester), packed hex, and a handful
 * of weather/ISM family names only when the heuristic is actually confident.
 * Host-tested. */
#ifndef IB_DECODE_H
#define IB_DECODE_H
#include "ib_proto.h"
#include <stdbool.h>
#include <stdint.h>

#define IB_MAX_BITS 256u

typedef struct {
    uint8_t  guess;
    uint8_t  coding;
    uint16_t bits;
    uint16_t pw_short_us;
    uint16_t pw_long_us;
    uint16_t gap_short_us;
    uint16_t gap_long_us;
    int16_t  temp_c_x10;
    uint8_t  humidity;
    uint8_t  channel;
    uint8_t  hex_n;
    uint8_t  hex[IB_HEX_BYTES];
    uint8_t  hist[IB_HIST_BINS];
    char     label[IB_LABEL_LEN];
} ib_decode_t;

void ib_decode(const uint16_t *dur, uint16_t n, bool first_level,
               uint32_t freq_hz, uint32_t total_us, ib_decode_t *out);
/* Same analysis on CC1101 async-serial edges. `mod` is IB_MOD_ASK or
 * IB_MOD_2FSK: 2-FSK still yields a bit-period waveform on GDO0, so PWM /
 * PPM / Manchester and Oregon-like MC can run. ASK-only names (PT2262,
 * TPMS-shaped) stay off the FSK path. */
void ib_decode_mod(const uint16_t *dur, uint16_t n, bool first_level,
                   uint32_t freq_hz, uint32_t total_us, uint8_t mod,
                   ib_decode_t *out);

#endif
