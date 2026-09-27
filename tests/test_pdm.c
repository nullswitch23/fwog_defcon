#include "test_util.h"
#include "pdm/pdm_mic.h"

/* Tolerance for a float comparison; ASSERT_EQ casts through unsigned long
 * long, which is wrong for a divider that legitimately has a fractional
 * part, so these compare by hand -- same helper as test_ws2812.c. */
static int approx(float a, float b) {
    float d = a - b;
    if (d < 0) d = -d;
    return d < 0.001f;
}

int main(void) {
    /* ---- PDM_RAW_BUFFER_BYTES: 256 * (64/8) = 2048, matching the legacy
     * btBufferA/btBufferB[2048]. ---- */
    ASSERT_EQ(PDM_RAW_BUFFER_BYTES, 2048u);

    /* ---- pdm_mic_clkdiv(): pdm_microphone.c:118's divider arithmetic,
     * `clock_get_hz(clk_sys) / (sample_rate * PDM_DECIMATION * 4.0)`,
     * sample_rate=8000, PDM_DECIMATION=64. Tested at this board's actual
     * clk_sys (200 MHz) and the Pico SDK's default (125 MHz) side by side,
     * exactly like test_ws2812.c's ws2812_clkdiv() test and for the same
     * reason: an assumed 125 MHz divider would be wrong by 1.6x on this
     * board. ---- */

    /* 200 MHz (this board): 200,000,000 / (8000*64*4=2,048,000) = 97.65625 */
    ASSERT_TRUE(approx(pdm_mic_clkdiv(200000000u), 97.65625f));

    /* 125 MHz (Pico SDK default): 125,000,000 / 2,048,000 = 61.03515625 */
    ASSERT_TRUE(approx(pdm_mic_clkdiv(125000000u), 61.03515625f));

    /* the ratio between the two must be exactly 200/125 = 1.6 -- pins down
     * that the function is linear in its argument, same rationale as
     * test_ws2812.c. */
    ASSERT_TRUE(approx(pdm_mic_clkdiv(200000000u) / pdm_mic_clkdiv(125000000u), 1.6f));

    /* ---- pdm_mic_next_buffer_index(): pdm_microphone.c:245's
     * `(raw_buffer_write_index + 1) % PDM_RAW_BUFFER_COUNT` -- the exact
     * place a software-re-armed double buffer (trap 6) goes wrong if the
     * wrap is off. PDM_RAW_BUFFER_COUNT is 2, so this must alternate. ---- */
    ASSERT_EQ(pdm_mic_next_buffer_index(0u), 1u);
    ASSERT_EQ(pdm_mic_next_buffer_index(1u), 0u);

    /* ---- pdm_scale_sample(): rpMICpdm::process()'s volume-scaling
     * arithmetic (:94-105), transcribed bit-for-bit. ---- */

    /* volume==5 is the original's implicit no-op: neither branch taken. */
    ASSERT_EQ((uint16_t)pdm_scale_sample(1234, 5), (uint16_t)1234);
    ASSERT_EQ((uint16_t)pdm_scale_sample(-1234, 5), (uint16_t)(-1234));

    /* volume=10 (max, d=+5), raw=256: cur = 256*(256+26*5)/256 = 386. */
    ASSERT_EQ((uint16_t)pdm_scale_sample(256, 10), (uint16_t)386);
    /* Same magnitude, negated sign, for a negative sample. */
    ASSERT_EQ((uint16_t)pdm_scale_sample(-256, 10), (uint16_t)(-386));

    /* volume=6 (d=+1), raw=256: cur = 256*(256+26)/256 = 282. */
    ASSERT_EQ((uint16_t)pdm_scale_sample(256, 6), (uint16_t)282);

    /* volume=0 (min, d=-5): the header's trap 5 note documents that the
     * `< 5` branch's double negation makes it amplify identically to the
     * `> 5` branch at the same distance from 5, instead of attenuating. This
     * is the original's own arithmetic, transcribed unchanged -- so volume 0
     * must produce EXACTLY what volume 10 produces above (386), not its
     * attenuated inverse. A "fix" that made this attenuate instead would
     * fail this assertion, which is the point: it pins the transcribed
     * behaviour, not a corrected one. */
    ASSERT_EQ((uint16_t)pdm_scale_sample(256, 0), (uint16_t)386);

    /* volume=4 (d=-1): must match volume=6's 282 for the same reason. */
    ASSERT_EQ((uint16_t)pdm_scale_sample(256, 4), (uint16_t)282);

    /* ---- pdm_analyze_buffer(): rpMICpdm::process()'s main loop (:90-127),
     * minus the streaming/recording side effects. ---- */

    /* NULL / empty input: no crash, no output touched, false returned. */
    ASSERT_TRUE(pdm_analyze_buffer(NULL, 5u, 5, 4000, NULL, NULL, NULL) == false);
    {
        int16_t one[1] = { 100 };
        ASSERT_TRUE(pdm_analyze_buffer(one, 0u, 5, 4000, NULL, NULL, NULL) == false);
    }

    /* max_sample exceeds threshold: detected via the high branch. */
    {
        int16_t raw[5] = { 100, -200, 4500, -50, 10 };
        int16_t max_s = -1, min_s = -1;
        bool detected = pdm_analyze_buffer(raw, 5u, 5, 4000, NULL, &max_s, &min_s);
        ASSERT_TRUE(detected == true);
        ASSERT_EQ((uint16_t)max_s, (uint16_t)4500);
        ASSERT_EQ((uint16_t)min_s, (uint16_t)(-200));
    }

    /* Neither extreme exceeds threshold: not detected. */
    {
        int16_t raw[4] = { 100, -100, 200, -200 };
        bool detected = pdm_analyze_buffer(raw, 4u, 5, 4000, NULL, NULL, NULL);
        ASSERT_TRUE(detected == false);
    }

    /* min_sample exceeds threshold in the NEGATIVE direction, with max_sample
     * unremarkable -- exercises the `else if` branch specifically, not just
     * the first `if`. */
    {
        int16_t raw[3] = { 10, -5000, 20 };
        int16_t max_s = -1, min_s = -1;
        bool detected = pdm_analyze_buffer(raw, 3u, 5, 4000, NULL, &max_s, &min_s);
        ASSERT_TRUE(detected == true);
        ASSERT_EQ((uint16_t)max_s, (uint16_t)20);
        ASSERT_EQ((uint16_t)min_s, (uint16_t)(-5000));
    }

    /* Exactly AT the threshold on both sides: strictly-greater-than and
     * strictly-less-than must both hold, so this must NOT detect. Pins the
     * comparison operators against an off-by-one (>= / <=) mutation. */
    {
        int16_t raw[2] = { 4000, -4000 };
        bool detected = pdm_analyze_buffer(raw, 2u, 5, 4000, NULL, NULL, NULL);
        ASSERT_TRUE(detected == false);
    }

    /* scaled_out is filled per-sample via pdm_scale_sample(), independent of
     * the (raw-sample-based) max/min/detection logic -- rpMICpdm::process()
     * scales into a SEPARATE array from the one it measures extrema on. */
    {
        int16_t raw[2] = { 256, -256 };
        int16_t scaled[2] = { 0, 0 };
        (void)pdm_analyze_buffer(raw, 2u, 10, 30000, scaled, NULL, NULL);
        ASSERT_EQ((uint16_t)scaled[0], (uint16_t)386);
        ASSERT_EQ((uint16_t)scaled[1], (uint16_t)(-386));
    }

    /* ---- pdm_mic_needs_init(): the idempotency guard (trap 8). A second
     * pdm_mic_init() call while the driver is already ready must be treated
     * as a no-op -- see the header's trap 8 note for the leaked-DMA-channel-
     * plus-unacknowledged-IRQ hang a call that proceeds anyway would cause.
     * pdm_mic_init() itself is hardware-only, so this pure predicate is the
     * host-testable half of the guard; mutating either branch of it (see the
     * port report's mutation log) must fail exactly one of these two. ---- */
    ASSERT_TRUE(pdm_mic_needs_init(false) == true);   /* not ready: proceed */
    ASSERT_TRUE(pdm_mic_needs_init(true)  == false);  /* already ready: skip */

    /* ---- pdm_mic_program_instructions[]: pins the four raw PIO words
     * exactly, transcribed unchanged from pdm_microphone.c:28-35. See the
     * header's trap 3 note: this is the same edge the MP34DT06J datasheet
     * says carries valid data for this board's L/R=GND strapping, so unlike
     * ws2812's polarity there is no "looks backwards" tension here -- but the
     * pin is still worth protecting from an incidental edit. ---- */
    ASSERT_EQ(pdm_mic_program_instructions[0], 0xa042u);
    ASSERT_EQ(pdm_mic_program_instructions[1], 0x4001u);
    ASSERT_EQ(pdm_mic_program_instructions[2], 0x9040u);
    ASSERT_EQ(pdm_mic_program_instructions[3], 0xb042u);

    /* ---- pdm_density_spread(): the AC-sensitive metric.
     *
     * The bench session of 2026-07-28 could not validate this microphone
     * because the only figure it had was whole-buffer 1-bit density -- which
     * is DC bias, and an acoustic tone has zero mean, so that figure
     * physically could not see a tone no matter how loud. This function is
     * the fix: it takes the
     * 1-count of each `block_bytes`-sized SUB-block -- a boxcar decimation,
     * the crudest honest decimation filter, and the one thing computable
     * without the third-party OpenPDMFilter that trap 4 keeps out of this
     * BSP -- and returns the peak-to-peak spread across those blocks. ---- */
    {
        /* THE case the old metric could not see, and the reason this
         * function exists: half the buffer all-ones, half all-zeros. Net
         * 1-density is exactly 50%, so pdm_bit_bias() reports ZERO -- the
         * silence answer -- while the signal is in fact full-scale. The
         * spread must report the full swing. */
        uint8_t buf[64];
        unsigned blocks = 0u;
        for (unsigned i = 0; i < 64u; i++) buf[i] = (i < 32u) ? 0xFFu : 0x00u;
        ASSERT_EQ(pdm_density_spread(buf, sizeof buf, 16u, &blocks), 128u);
        ASSERT_EQ(blocks, 4u);
    }

    {
        /* Silence: PDM idles alternating, so every sub-block has an
         * identical 1-count and the spread is zero. This is the pair to the
         * case above -- together they are what the old metric could not
         * distinguish. */
        uint8_t buf[64];
        for (unsigned i = 0; i < 64u; i++) buf[i] = 0x55u;
        ASSERT_EQ(pdm_density_spread(buf, sizeof buf, 16u, NULL), 0u);
    }

    {
        /* A trailing PARTIAL block is dropped, not measured against a short
         * denominator -- 70 bytes at 16 bytes per block is 4 blocks and 6
         * bytes ignored. A partial block would otherwise read as an
         * artificially low 1-count and manufacture spread out of nothing. */
        uint8_t buf[70];
        unsigned blocks = 0u;
        for (unsigned i = 0; i < 70u; i++) buf[i] = 0xFFu;
        ASSERT_EQ(pdm_density_spread(buf, sizeof buf, 16u, &blocks), 0u);
        ASSERT_EQ(blocks, 4u);
    }

    {
        /* Guards. Fewer than two complete blocks has no spread to report --
         * returning a number there would be indistinguishable from real
         * silence, which is exactly the failure mode this whole function
         * exists to end. */
        uint8_t buf[16] = {0};
        unsigned blocks = 99u;
        ASSERT_EQ(pdm_density_spread(NULL, 64u, 16u, NULL), 0u);
        ASSERT_EQ(pdm_density_spread(buf, 0u, 16u, NULL), 0u);
        ASSERT_EQ(pdm_density_spread(buf, sizeof buf, 0u, NULL), 0u);
        ASSERT_EQ(pdm_density_spread(buf, sizeof buf, 16u, &blocks), 0u);
        ASSERT_EQ(blocks, 0u);   /* one block is not enough: report none */
    }

    TEST_RETURN();
}
