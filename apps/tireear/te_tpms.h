#ifndef TE_TPMS_H
#define TE_TPMS_H
#include <stdbool.h>
#include <stdint.h>

#define TE_MAX_EDGES   512u
#define TE_GAP_US      25000u
#define TE_RAW_BITS    192u
#define TE_RAW_BYTES   24u
#define TE_PROFILE_N   4u
#define TE_SCAN_MAX    32u
#define TE_SCAN_MS     45000u

typedef struct {
    const char *name;
    uint8_t     n_bytes;
    uint8_t     crc_poly;
    uint8_t     crc_init;
    uint8_t     crc_len; /* bytes covered by CRC */
    uint8_t     id_off;
    uint8_t     id_len;
    uint8_t     flags; /* TE_PROF_* */
} te_profile_desc_t;

#define TE_PROF_PMV107J  1u /* differential layout, realign + crc 0x13 */
#define TE_PROF_TOYOTA   2u
#define TE_PROF_SCHRADER 3u

typedef struct {
    bool     ok;
    uint8_t  profile;
    uint8_t  id[4];
    uint8_t  id_len;
    int16_t  psi_x10;   /* kPa*0.145038 -> psi, *10 */
    int16_t  temp_c;
    uint8_t  battery_ok;
    uint8_t  status;
    uint8_t  raw_n;
    uint8_t  raw[TE_RAW_BYTES];
    uint8_t  bits_n;
    uint8_t  bits[TE_RAW_BITS];
} te_decode_t;

typedef struct {
    uint8_t  id[4];
    uint8_t  id_len;
    int16_t  psi_x10;
    int16_t  temp_c;
    int8_t   peak_rssi;
    uint8_t  bursts;
    uint16_t first_ms;
    uint16_t last_ms;
    uint8_t  profile;
    uint8_t  crc_ok;
    uint8_t  raw_n;
    uint8_t  raw[TE_RAW_BYTES];
} te_scan_ent_t;

uint8_t te_crc8(const uint8_t *data, unsigned len, uint8_t poly, uint8_t init);

const te_profile_desc_t *te_profile_desc(uint8_t idx);
unsigned te_profile_count(void);

uint16_t te_guess_bit_us(const uint16_t *dur, uint16_t n, uint32_t freq_hz,
                         bool fsk);

unsigned te_edges_to_bits(const uint16_t *dur, uint16_t n, bool first_level,
                          uint16_t bit_us, uint8_t *bits, unsigned cap);

unsigned te_pack_lsb_first(const uint8_t *bits, unsigned n, uint8_t *out,
                           unsigned cap);

bool te_tpms_decode(uint8_t profile, const uint8_t *bits, unsigned n_bits,
                    te_decode_t *out);

void te_scan_merge(te_scan_ent_t *log, unsigned cap, unsigned *n,
                   const te_decode_t *dec, int16_t rssi, uint32_t now_ms);

#endif
