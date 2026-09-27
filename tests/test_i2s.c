#include "test_util.h"
#include "audio/i2s_audio.h"

/* Tolerance for a float comparison -- same helper as test_ws2812.c and
 * test_pdm.c. Most values below compare exactly (the arithmetic involved is
 * exact in binary floating point: halving, quartering, and the divider
 * function is pure integer), but a couple of volume-table entries are not
 * exact decimal fractions in binary. */
static int approx(float a, float b) {
    float d = a - b;
    if (d < 0) d = -d;
    return d < 0.0001f;
}

int main(void) {
    /* ---- i2s_audio_pio_divider(): rpI2S.cpp:117-125's
     * update_pio_frequency() arithmetic, `sys_clk_hz * 4 / sample_rate_hz`,
     * INTEGER division -- tested at this board's actual clk_sys (200 MHz)
     * and the Pico SDK's default (125 MHz), at both 8000 Hz (this driver's
     * only live rate) and 22050 Hz (rpI2S::speak()'s TTS rate -- out of
     * scope here, but the arithmetic doesn't care). All four values below
     * are exact integers, computed by hand from the same formula, not
     * copied from a prior run -- so this also pins the function against a
     * silent switch to floating-point (which would round differently). ---- */
    ASSERT_EQ(i2s_audio_pio_divider(200000000u, 8000u), 100000u);
    ASSERT_EQ(i2s_audio_pio_divider(200000000u, 22050u), 36281u);
    ASSERT_EQ(i2s_audio_pio_divider(125000000u, 8000u), 62500u);
    ASSERT_EQ(i2s_audio_pio_divider(125000000u, 22050u), 22675u);

    /* The 200/125 MHz ratio must be exactly 1.6 (8/5) -- cross-multiplied to
     * stay in integer arithmetic: divider(200M,8000)*5 == divider(125M,8000)*8.
     * An assumed 125 MHz divider on this 200 MHz board would be wrong by
     * this exact factor (AGENTS.md, ws2812_driver.h, pdm_mic.h all make the
     * same point for their own dividers). */
    ASSERT_EQ(i2s_audio_pio_divider(200000000u, 8000u) * 5u,
             i2s_audio_pio_divider(125000000u, 8000u) * 8u);

    /* ---- i2s_audio_program_instructions[]: pins the eight raw PIO words
     * exactly, transcribed unchanged from rpI2S.cpp:78-89 -- the LIVE
     * variant, not the dead `...Oppsite[]` array the header documents as
     * unreferenced dead code. ---- */
    ASSERT_EQ(i2s_audio_program_instructions[0], 0x6801u);
    ASSERT_EQ(i2s_audio_program_instructions[1], 0x1840u);
    ASSERT_EQ(i2s_audio_program_instructions[2], 0x6001u);
    ASSERT_EQ(i2s_audio_program_instructions[3], 0xf02eu);
    ASSERT_EQ(i2s_audio_program_instructions[4], 0x6001u);
    ASSERT_EQ(i2s_audio_program_instructions[5], 0x1044u);
    ASSERT_EQ(i2s_audio_program_instructions[6], 0x6801u);
    ASSERT_EQ(i2s_audio_program_instructions[7], 0xf82eu);

    /* ---- i2s_audio_volume_multipliers[]: pins VOLUME_MULTIPLIERS exactly
     * (rpI2S.cpp:51-63). ---- */
    ASSERT_TRUE(approx(i2s_audio_volume_multipliers[0], 0.00f));
    ASSERT_TRUE(approx(i2s_audio_volume_multipliers[5], 0.25f));
    ASSERT_TRUE(approx(i2s_audio_volume_multipliers[8], 0.64f));
    ASSERT_TRUE(approx(i2s_audio_volume_multipliers[10], 1.00f));

    /* ---- i2s_audio_other_buffer(): the ping-pong flip. A three-buffer
     * scheme or a flip that returned its own input would fail here. ---- */
    ASSERT_EQ(i2s_audio_other_buffer(I2S_AUDIO_BUFFER_A), I2S_AUDIO_BUFFER_B);
    ASSERT_EQ(i2s_audio_other_buffer(I2S_AUDIO_BUFFER_B), I2S_AUDIO_BUFFER_A);

    /* ---- i2s_audio_fill_count(): fillBuffer()'s
     * `iCurrentBuffSize = min(I2S_AUDIO_BUFF_SIZE, iNumSamplesRemaining)`
     * (rpI2S.cpp:764,773-776). ---- */
    ASSERT_EQ(i2s_audio_fill_count(1024, 0, 1024u), 1024u);      /* exactly full */
    ASSERT_EQ(i2s_audio_fill_count(1500, 1000, 1024u), 500u);    /* remaining < capacity */
    ASSERT_EQ(i2s_audio_fill_count(2000, 500, 1024u), 1024u);    /* remaining > capacity, clamped */
    ASSERT_EQ(i2s_audio_fill_count(100, 100, 1024u), 0u);        /* exactly finished */
    ASSERT_EQ(i2s_audio_fill_count(100, 150, 1024u), 0u);        /* past finished, not negative */

    /* ---- i2s_audio_samples_remain(): `m_iTotalSamples == m_iCurrentSample`
     * inverted (rpI2S.cpp:944,964). ---- */
    ASSERT_TRUE(i2s_audio_samples_remain(100, 99) == true);
    ASSERT_TRUE(i2s_audio_samples_remain(100, 100) == false);
    ASSERT_TRUE(i2s_audio_samples_remain(100, 101) == false);

    /* ---- i2s_audio_expand_8bit(): rpI2S.cpp:811's unsigned 8-bit
     * expansion. The sign-handling case the header calls out explicitly:
     * a source byte of 255 (unsigned, near the top of its range) must
     * expand to a LARGE POSITIVE value (127*50=6350), not a large negative
     * one -- which is exactly what a wrong `int8_t` interpretation would
     * produce instead (255 as int8_t is -1, giving (-1-128)*50 = -6450). ---- */
    ASSERT_EQ((uint16_t)i2s_audio_expand_8bit(128u), (uint16_t)0);
    ASSERT_EQ((uint16_t)i2s_audio_expand_8bit(255u), (uint16_t)6350);
    ASSERT_EQ((uint16_t)i2s_audio_expand_8bit(0u), (uint16_t)(-6400));

    /* ---- i2s_audio_select_sample(): mono reads src[index] directly;
     * stereo reads src[index*2] -- the LEFT channel of an interleaved LR
     * pair, discarding the right sample entirely (rpI2S.cpp:842-845, minus
     * the dropped *0.8 -- see the header's trap 5 note). ---- */
    {
        const int16_t src[6] = { 10, -10, 20, -20, 30, -30 }; /* 3 LR frames */
        ASSERT_EQ((uint16_t)i2s_audio_select_sample(src, 2, true), (uint16_t)20);
        ASSERT_EQ((uint16_t)i2s_audio_select_sample(src, 1, false), (uint16_t)20);
        /* stereo never reads the odd (right-channel) index */
        ASSERT_EQ((uint16_t)i2s_audio_select_sample(src, 0, false), (uint16_t)10);
    }

    /* ---- i2s_audio_volume_multiplier(): clamp-then-multiply
     * (rpI2S.cpp:767-771). ---- */
    ASSERT_TRUE(approx(i2s_audio_volume_multiplier(10, 1.0f), 1.0f));
    ASSERT_TRUE(approx(i2s_audio_volume_multiplier(0, 1.0f), 0.0f));
    ASSERT_TRUE(approx(i2s_audio_volume_multiplier(-5, 1.0f), 0.0f));   /* clamped to 0 */
    ASSERT_TRUE(approx(i2s_audio_volume_multiplier(15, 1.0f), 1.0f));   /* clamped to 10 */
    ASSERT_TRUE(approx(i2s_audio_volume_multiplier(5, 0.5f), 0.125f));  /* 0.25 * 0.5 */

    /* ---- i2s_audio_apply_volume(): multiply-then-truncate
     * (rpI2S.cpp:849), NOT rounding -- 3.5 truncates to 3, -3.5 truncates
     * to -3 (toward zero), not -4 (a floor()-like implementation would fail
     * this). ---- */
    ASSERT_EQ((uint16_t)i2s_audio_apply_volume(1000, 0.5f), (uint16_t)500);
    ASSERT_EQ((uint16_t)i2s_audio_apply_volume(-1000, 0.5f), (uint16_t)(-500));
    ASSERT_EQ((uint16_t)i2s_audio_apply_volume(7, 0.5f), (uint16_t)3);
    ASSERT_EQ((uint16_t)i2s_audio_apply_volume(-7, 0.5f), (uint16_t)(-3));

    /* ---- i2s_audio_fill_buffer(): the 16-bit mono/stereo path, with
     * zero-padding of any tail beyond `count` (rpI2S.cpp:826-875's "every
     * other case" branch, minus the file-read branch and the dead
     * `#if (0)` block). ---- */
    {
        /* mono: straight copy (volume 10 => multiplier 1.0), tail zeroed */
        const int16_t src[4] = { 100, 200, 300, 400 };
        int16_t out[5] = { -1, -1, -1, -1, -1 };
        unsigned consumed = i2s_audio_fill_buffer(out, 5u, src, 0, 3u, true, 10, 1.0f);
        ASSERT_EQ(consumed, 3u);
        ASSERT_EQ((uint16_t)out[0], (uint16_t)100);
        ASSERT_EQ((uint16_t)out[1], (uint16_t)200);
        ASSERT_EQ((uint16_t)out[2], (uint16_t)300);
        ASSERT_EQ((uint16_t)out[3], (uint16_t)0);
        ASSERT_EQ((uint16_t)out[4], (uint16_t)0);
    }
    {
        /* stereo: left-channel-only down-mix */
        const int16_t src[8] = { 10, -1, 20, -2, 30, -3, 40, -4 };
        int16_t out[4] = { -1, -1, -1, -1 };
        unsigned consumed = i2s_audio_fill_buffer(out, 4u, src, 0, 2u, false, 10, 1.0f);
        ASSERT_EQ(consumed, 2u);
        ASSERT_EQ((uint16_t)out[0], (uint16_t)10);
        ASSERT_EQ((uint16_t)out[1], (uint16_t)20);
        ASSERT_EQ((uint16_t)out[2], (uint16_t)0);
        ASSERT_EQ((uint16_t)out[3], (uint16_t)0);
    }
    {
        /* volume scaling applied through the fill path, not just the
         * standalone multiplier/apply functions. */
        const int16_t src[1] = { 1000 };
        int16_t out[1] = { -1 };
        unsigned consumed = i2s_audio_fill_buffer(out, 1u, src, 0, 1u, true, 5, 1.0f);
        ASSERT_EQ(consumed, 1u);
        ASSERT_EQ((uint16_t)out[0], (uint16_t)250); /* 1000 * 0.25 */
    }

    /* ---- i2s_audio_fill_buffer_8bit(): the 8-bit path, WITH tail
     * zero-padding -- a deliberate fix over the original's asymmetric
     * playing8BitAudio branch (see the header's "buffer-fill deviations"
     * note). ---- */
    {
        const uint8_t src[3] = { 128u, 255u, 0u };
        int16_t out[4] = { -1, -1, -1, -1 };
        unsigned consumed = i2s_audio_fill_buffer_8bit(out, 4u, src, 0, 2u, 10, 1.0f);
        ASSERT_EQ(consumed, 2u);
        ASSERT_EQ((uint16_t)out[0], (uint16_t)0);      /* 128 -> 0 */
        ASSERT_EQ((uint16_t)out[1], (uint16_t)6350);   /* 255 -> 6350 */
        ASSERT_EQ((uint16_t)out[2], (uint16_t)0);      /* zero-padded tail */
        ASSERT_EQ((uint16_t)out[3], (uint16_t)0);      /* zero-padded tail */
    }
    {
        /* volume scaling applied to the 8-bit expansion. */
        const uint8_t src[1] = { 255u };
        int16_t out[1] = { -1 };
        unsigned consumed = i2s_audio_fill_buffer_8bit(out, 1u, src, 0, 1u, 5, 1.0f);
        ASSERT_EQ(consumed, 1u);
        ASSERT_EQ((uint16_t)out[0], (uint16_t)1587); /* 6350 * 0.25 = 1587.5 -> 1587 */
    }

    /* ---- Fix round 1: i2s_audio_chain_drained(), not
     * i2s_audio_samples_remain(), is the correct stop gate for a
     * hardware-chained pair. Reproduces the exact boundary the review
     * flagged: a source that is precisely two buffers long (2048 samples at
     * I2S_AUDIO_BUFF_SIZE == 1024). Both buffers are pre-filled before the
     * chain ever starts, so the FILL cursor reaches `total_samples` before
     * buffer A has played even once -- i2s_audio_samples_remain() already
     * says "stop" at that point, which is the bug: it would truncate all of
     * buffer B's real audio (the sound's entire second half).
     * i2s_audio_chain_drained(), keyed to the SIBLING buffer's own most
     * recent fill instead of the cursor, must not agree until buffer B has
     * actually been given its turn. ---- */
    {
        static const int16_t src[2 * I2S_AUDIO_BUFF_SIZE] = { 0 };
        int16_t buf_a[I2S_AUDIO_BUFF_SIZE];
        int16_t buf_b[I2S_AUDIO_BUFF_SIZE];
        const int total = 2 * (int)I2S_AUDIO_BUFF_SIZE;
        int current = 0;
        unsigned consumed_a, consumed_b;

        /* i2s_audio_start(): pre-fill both buffers, exactly as the driver's
         * own i2s_audio_start() does. */
        consumed_a = i2s_audio_fill_buffer(buf_a, I2S_AUDIO_BUFF_SIZE, src, current,
                                           i2s_audio_fill_count(total, current, I2S_AUDIO_BUFF_SIZE),
                                           true, 10, 1.0f);
        current += (int)consumed_a;
        consumed_b = i2s_audio_fill_buffer(buf_b, I2S_AUDIO_BUFF_SIZE, src, current,
                                           i2s_audio_fill_count(total, current, I2S_AUDIO_BUFF_SIZE),
                                           true, 10, 1.0f);
        current += (int)consumed_b;

        /* Sanity: the chain hasn't even started yet, and both buffers are
         * already full of real samples. */
        ASSERT_EQ(consumed_a, I2S_AUDIO_BUFF_SIZE);
        ASSERT_EQ(consumed_b, I2S_AUDIO_BUFF_SIZE);

        /* THE BUG: the fill cursor already says "nothing left" here --
         * BEFORE buffer A, let alone B, has played at all. */
        ASSERT_TRUE(i2s_audio_samples_remain(total, current) == false);

        /* THE FIX: the corrected gate must NOT agree. Buffer B -- the
         * sibling the hardware chain auto-triggers the instant A completes
         * -- holds a full buffer of real audio from its own most recent
         * fill, so the chain has not drained. */
        ASSERT_TRUE(i2s_audio_chain_drained(consumed_b) == false);

        /* process() call #1 (A completes): refill A for its NEXT turn.
         * Input is now genuinely exhausted, so this fill produces zero real
         * samples -- pure zero-padded silence (the earlier stale-buffer
         * fix's guarantee: i2s_audio_fill_buffer() always zero-pads, so
         * this is silence, not garbage). */
        {
            const unsigned recount = i2s_audio_fill_count(total, current, I2S_AUDIO_BUFF_SIZE);
            ASSERT_EQ(recount, 0u);
            consumed_a = i2s_audio_fill_buffer(buf_a, I2S_AUDIO_BUFF_SIZE, src, current,
                                               recount, true, 10, 1.0f);
            ASSERT_EQ(consumed_a, 0u);
        }

        /* process() call #2 (B completes, HAVING ACTUALLY PLAYED its full
         * real buffer -- the truncation the bug would have caused is now
         * provably avoided). The sibling is now A, whose most recent fill
         * (just above) was silent -- the chain has genuinely drained, and
         * i2s_audio_chain_drained() must now agree. */
        ASSERT_TRUE(i2s_audio_chain_drained(consumed_a) == true);
    }

    TEST_RETURN();
}
