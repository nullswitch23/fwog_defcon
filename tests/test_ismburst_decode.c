#include "test_util.h"
#include "decode.h"
#include <string.h>

static void emit_ppm_bits(uint16_t *dur, uint16_t *n, bool *first,
                          const uint8_t *bytes, unsigned nbits,
                          uint16_t pulse, uint16_t gap0, uint16_t gap1) {
    *first = true;
    *n = 0;
    for (unsigned i = 0; i < nbits; i++) {
        const unsigned bit = (bytes[i / 8u] >> (7u - (i % 8u))) & 1u;
        dur[(*n)++] = pulse;
        dur[(*n)++] = bit ? gap1 : gap0;
    }
}

static void emit_pwm_bits(uint16_t *dur, uint16_t *n, bool *first,
                          const uint8_t *bytes, unsigned nbits,
                          uint16_t short_us, uint16_t long_us, uint16_t gap) {
    *first = true;
    *n = 0;
    for (unsigned i = 0; i < nbits; i++) {
        const unsigned bit = (bytes[i / 8u] >> (7u - (i % 8u))) & 1u;
        dur[(*n)++] = bit ? long_us : short_us;
        dur[(*n)++] = gap;
    }
}

int main(void) {
    ASSERT_EQ(sizeof(ib_cmd_t), 8);
    ASSERT_EQ(sizeof(ib_status_t), 108);
    ASSERT_EQ(sizeof(ib_name_t), 24);

    uint16_t dur[IB_MAX_EDGES];
    uint16_t n = 0;
    bool first = true;
    ib_decode_t d;

    /* Prologue (rtl_433 protocol 3): type 9, id 0xA5, ch 2, 21.5 C, 48 %RH. */
    const uint8_t prologue[5] = { 0x9Au, 0x59u, 0x0Du, 0x73u, 0x00u };
    emit_ppm_bits(dur, &n, &first, prologue, 36, 500, 2000, 4000);
    ib_decode(dur, n, first, 433920000u, 0, &d);
    ASSERT_EQ(d.guess, IB_GUESS_PROLOGUE);
    ASSERT_EQ(d.coding, IB_GUESS_PPM);
    ASSERT_EQ(d.channel, 2);
    ASSERT_EQ(d.temp_c_x10, 215);
    ASSERT_EQ(d.humidity, 48);
    ASSERT_EQ(d.hex[0], 0x9A);
    ASSERT_TRUE(strcmp(d.label, "Prologue weather") == 0);
    ASSERT_TRUE(d.hist[0] != 0 || d.hist[1] != 0 || d.hist[7] != 0 ||
                d.hist[8] != 0 || d.hist[9] != 0);

    /* Same bits with inverted gap polarity still hit via the invert scan. */
    emit_ppm_bits(dur, &n, &first, prologue, 36, 500, 4000, 2000); /* inverted gaps */
    ib_decode(dur, n, first, 433920000u, 0, &d);
    ASSERT_EQ(d.guess, IB_GUESS_PROLOGUE);
    ASSERT_EQ(d.temp_c_x10, 215);

    /* Nexus (protocol 19): id 0x42, ch 1, 21.5 C, 48 %RH, const 0xF.
     * 12-bit temp 0x0D7 lives in b1[3:0]|b2, so b1 low nibble is 0. */
    const uint8_t nexus[5] = { 0x42u, 0x80u, 0xD7u, 0xF3u, 0x00u };
    emit_ppm_bits(dur, &n, &first, nexus, 36, 500, 1000, 2000);
    ib_decode(dur, n, first, 433920000u, 0, &d);
    ASSERT_EQ(d.guess, IB_GUESS_NEXUS);
    ASSERT_EQ(d.channel, 1);
    ASSERT_EQ(d.temp_c_x10, 215);
    ASSERT_EQ(d.humidity, 48);

    /* Oregon-like Manchester: sixteen 1s as a 400 us clock. */
    n = 0;
    first = true;
    for (unsigned i = 0; i < 16u; i++) {
        dur[n++] = 400;
        dur[n++] = 400;
    }
    for (unsigned i = 0; i < 16u; i++) { /* some 0s so it is not a DC blob */
        dur[n++] = 400;
        dur[n++] = 400;
    }
    /* The loop above is still all 1s (high-low = Thomas 1). Add mixed tails
     * by inserting a 0 (low-high) after flipping start of a pair — emit
     * extra 0 bits as low then high, which needs first_level still high
     * only if we ended on a completed 1 (last duration was a low gap). */
    ib_decode(dur, n, first, 433920000u, 0, &d);
    ASSERT_EQ(d.guess, IB_GUESS_OREGON);
    ASSERT_TRUE(d.bits >= 16);

    /* PT2262-like 1:3 PWM doorbell, 24 bits, not a weather frame. */
    const uint8_t fob[3] = { 0xA5u, 0x5Au, 0xA5u };
    emit_pwm_bits(dur, &n, &first, fob, 24, 350, 1050, 350);
    ib_decode(dur, n, first, 433920000u, 0, &d);
    ASSERT_EQ(d.guess, IB_GUESS_PT2262);
    ASSERT_EQ(d.coding, IB_GUESS_PWM);

    /* TPMS-shaped: short fast edges at 315 MHz. TireEar's job, not OEM decode. */
    n = 80;
    first = true;
    for (uint16_t i = 0; i < n; i++) dur[i] = 80;
    ib_decode(dur, n, first, 315000000u, 80u * 80u, &d);
    ASSERT_EQ(d.guess, IB_GUESS_TPMS);

    /* Too short to decode. */
    dur[0] = 500;
    ib_decode(dur, 4, true, 433920000u, 2000, &d);
    ASSERT_EQ(d.guess, IB_GUESS_UNKNOWN);

    /* Fan-like 36-bit PPM: type 0xA, remote gaps, not a weather station. */
    const uint8_t fan[5] = { 0xA5u, 0x5Au, 0xA5u, 0x5Au, 0xA0u };
    emit_ppm_bits(dur, &n, &first, fan, 36, 400, 800, 1600);
    ib_decode(dur, n, first, 433920000u, 0, &d);
    ASSERT_TRUE(d.guess != IB_GUESS_PROLOGUE);
    ASSERT_TRUE(d.guess != IB_GUESS_NEXUS);
    ASSERT_TRUE(strstr(d.label, "Prologue") == NULL);

    /* Same Prologue *bits* on fan/doorbell PPM timing (~400/800/1600) must
     * not inherit the weather name. Timing is part of the heuristic. */
    emit_ppm_bits(dur, &n, &first, prologue, 36, 400, 800, 1600);
    ib_decode(dur, n, first, 433920000u, 0, &d);
    ASSERT_TRUE(d.guess != IB_GUESS_PROLOGUE);
    ASSERT_TRUE(strstr(d.label, "Prologue") == NULL);

    /* 48-bit PPM with a valid Prologue payload at bit 8: v002's sliding
     * window would have claimed weather. Alignment forbids that. */
    const uint8_t slide[6] = { 0xFFu, 0x9Au, 0x59u, 0x0Du, 0x73u, 0x00u };
    emit_ppm_bits(dur, &n, &first, slide, 48, 500, 2000, 4000);
    ib_decode(dur, n, first, 433920000u, 0, &d);
    ASSERT_TRUE(d.guess != IB_GUESS_PROLOGUE);
    ASSERT_TRUE(strstr(d.label, "Prologue") == NULL);

    /* Type 0x9, Prologue timing, humidity 0xEE — not 0..100 or 0xCC. */
    const uint8_t badh[5] = { 0x9Au, 0x59u, 0x0Du, 0x7Eu, 0xE0u };
    emit_ppm_bits(dur, &n, &first, badh, 36, 500, 2000, 4000);
    ib_decode(dur, n, first, 433920000u, 0, &d);
    ASSERT_TRUE(d.guess != IB_GUESS_PROLOGUE);
    ASSERT_TRUE(strstr(d.label, "Prologue") == NULL);

    /* 2-FSK: same Oregon-like Manchester bitstream; ASK-only TPMS/PT2262 stay off. */
    n = 0;
    first = true;
    for (unsigned i = 0; i < 40u; i++) {
        dur[n++] = 400;
        dur[n++] = 400;
    }
    ib_decode_mod(dur, n, first, 433920000u, 0, IB_MOD_2FSK, &d);
    ASSERT_EQ(d.guess, IB_GUESS_OREGON);
    ASSERT_TRUE(strstr(d.label, "FSK") != NULL);

    n = 80;
    first = true;
    for (uint16_t i = 0; i < n; i++) dur[i] = 80;
    ib_decode_mod(dur, n, first, 315000000u, 80u * 80u, IB_MOD_2FSK, &d);
    ASSERT_TRUE(d.guess != IB_GUESS_TPMS);

    TEST_RETURN();
}
