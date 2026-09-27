#include "test_util.h"
#include "sensors/lis3dh.h"

int main(void) {
    /* ---- lis3dh_assemble_axis(): transcribed from process()'s
     * `((int)btData[hi] << 8) | btData[lo]` assigned into a `short`
     * (rpAccel_lis3dh.cpp:176-178). Signed 16-bit little-endian pairs --
     * this is exactly where a port of this kind of driver breaks, so every
     * interesting boundary gets its own case. ---- */

    /* zero */
    ASSERT_EQ(lis3dh_assemble_axis(0x00u, 0x00u), 0);

    /* small positive: low byte only */
    ASSERT_EQ(lis3dh_assemble_axis(0x01u, 0x00u), 1);

    /* small negative: -1 is 0xFFFF */
    ASSERT_EQ((int)lis3dh_assemble_axis(0xFFu, 0xFFu), -1);

    /* most positive int16_t: 0x7FFF = 32767 */
    ASSERT_EQ(lis3dh_assemble_axis(0xFFu, 0x7Fu), 32767);

    /* most negative int16_t: 0x8000 = -32768. The classic sign-extension
     * failure point -- a buggy port that treats the assembled value as
     * unsigned, or sign-extends from the wrong bit, gets 32768 here instead. */
    ASSERT_EQ((int)lis3dh_assemble_axis(0x00u, 0x80u), -32768);

    /* a mid-range negative value, to catch a port that only special-cases
     * the all-ones/all-zeros extremes: 0xFF38 = -200 */
    ASSERT_EQ((int)lis3dh_assemble_axis(0x38u, 0xFFu), -200);

    /* ---- lis3dh_mg_per_lsb() / lis3dh_raw_to_mg(): addition, not a port --
     * see the header for the datasheet derivation (Table 4's Normal-mode
     * row, since CTRL_REG1/CTRL_REG4 always leave HR=0, LPen=0). Raw values
     * below are constructed as digit << 6 so the expected milli-g is just
     * digit * mg_per_lsb, exercising the left-justification shift and the
     * per-range scale together. ---- */
    ASSERT_EQ(lis3dh_mg_per_lsb(LIS3DH_RANGE_2G), 4);
    ASSERT_EQ(lis3dh_mg_per_lsb(LIS3DH_RANGE_4G), 8);
    ASSERT_EQ(lis3dh_mg_per_lsb(LIS3DH_RANGE_8G), 16);
    ASSERT_EQ(lis3dh_mg_per_lsb(LIS3DH_RANGE_16G), 48);

    /* raw 0 -> 0 mg, every range */
    ASSERT_EQ(lis3dh_raw_to_mg(0, LIS3DH_RANGE_2G), 0);
    ASSERT_EQ(lis3dh_raw_to_mg(0, LIS3DH_RANGE_16G), 0);

    /* one digit (raw = 1 << 6 = 64) at each range */
    ASSERT_EQ(lis3dh_raw_to_mg((int16_t)64, LIS3DH_RANGE_2G), 4);
    ASSERT_EQ(lis3dh_raw_to_mg((int16_t)64, LIS3DH_RANGE_4G), 8);
    ASSERT_EQ(lis3dh_raw_to_mg((int16_t)64, LIS3DH_RANGE_8G), 16);
    ASSERT_EQ(lis3dh_raw_to_mg((int16_t)64, LIS3DH_RANGE_16G), 48);

    /* one negative digit (raw = -64), 2g range: the arithmetic-shift
     * boundary -- a logical (unsigned) shift here would produce a huge
     * positive digit instead of -1. */
    ASSERT_EQ(lis3dh_raw_to_mg((int16_t)-64, LIS3DH_RANGE_2G), -4);

    /* full-scale positive: the 10-bit signed digit's max is 511
     * (0x1FF), raw = 511 << 6 = 32704, still in range for int16_t. */
    ASSERT_EQ(lis3dh_raw_to_mg((int16_t)32704, LIS3DH_RANGE_16G), 511 * 48);
    ASSERT_EQ(lis3dh_raw_to_mg((int16_t)32704, LIS3DH_RANGE_2G), 511 * 4);

    /* full-scale negative: the 10-bit signed digit's min is -512, whose
     * left-justified raw form is exactly INT16_MIN (0x8000) -- the same
     * value lis3dh_assemble_axis()'s most-negative case produces, so this
     * also confirms the two functions compose correctly end to end. */
    ASSERT_EQ(lis3dh_raw_to_mg((int16_t)-32768, LIS3DH_RANGE_16G), -512 * 48);
    ASSERT_EQ(lis3dh_raw_to_mg((int16_t)-32768, LIS3DH_RANGE_2G), -512 * 4);

    /* ---- lis3dh_check_motion(): ported processMoveThreshold()
     * (rpAccel_lis3dh.cpp:190-220). Pure: abs per-axis delta against a
     * threshold, no debounce, no hysteresis. The boundary is the whole
     * point -- the original compares with a strict `>`, so a delta EXACTLY
     * equal to the threshold must NOT count as motion. ---- */
    {
        lis3dh_sample_t prev = { 1000, 1000, 1000 };

        /* delta exactly at threshold (500) on X only -> not moving */
        lis3dh_sample_t at_threshold = { 1500, 1000, 1000 };
        lis3dh_motion_t m = lis3dh_check_motion(at_threshold, prev, 500);
        ASSERT_TRUE(m.x == false);
        ASSERT_TRUE(m.any == false);

        /* delta one past the threshold on X only -> moving, only X */
        lis3dh_sample_t past_threshold = { 1501, 1000, 1000 };
        m = lis3dh_check_motion(past_threshold, prev, 500);
        ASSERT_TRUE(m.x == true);
        ASSERT_TRUE(m.y == false);
        ASSERT_TRUE(m.z == false);
        ASSERT_TRUE(m.any == true);

        /* delta one short of the threshold -> not moving */
        lis3dh_sample_t short_of_threshold = { 1499, 1000, 1000 };
        m = lis3dh_check_motion(short_of_threshold, prev, 500);
        ASSERT_TRUE(m.x == false);
        ASSERT_TRUE(m.any == false);

        /* negative delta (current below previous) must abs() correctly */
        lis3dh_sample_t negative_delta = { 499, 1000, 1000 };
        m = lis3dh_check_motion(negative_delta, prev, 500);
        ASSERT_TRUE(m.x == true);   /* |499-1000| = 501 > 500 */

        /* all three axes moving at once -> each flag set, any true */
        lis3dh_sample_t all_moving = { 1600, 1600, 1600 };
        m = lis3dh_check_motion(all_moving, prev, 500);
        ASSERT_TRUE(m.x == true);
        ASSERT_TRUE(m.y == true);
        ASSERT_TRUE(m.z == true);
        ASSERT_TRUE(m.any == true);

        /* nothing moving -> any false even with three axes checked */
        lis3dh_sample_t none_moving = { 1000, 1000, 1000 };
        m = lis3dh_check_motion(none_moving, prev, 500);
        ASSERT_TRUE(m.x == false);
        ASSERT_TRUE(m.y == false);
        ASSERT_TRUE(m.z == false);
        ASSERT_TRUE(m.any == false);

        /* the default threshold constant, exercised end to end */
        lis3dh_sample_t at_default = { (int16_t)(1000 + LIS3DH_MOVE_THRESHOLD_DEFAULT), 1000, 1000 };
        m = lis3dh_check_motion(at_default, prev, LIS3DH_MOVE_THRESHOLD_DEFAULT);
        ASSERT_TRUE(m.x == false);   /* exactly at threshold: still not motion */
    }

    /* ---- lis3dh_advance(): the pure core of process() (:141-188), pinning
     * the read-failure semantics a review round caught this port getting
     * wrong on the first pass -- the sample is zeroed on any I2C failure,
     * exactly like the original's `m_iX=0; m_iY=0; m_iZ=0;`, but the motion
     * output is NEVER touched on a failure, because the original only ever
     * writes m_bMovingX/Y/Z/m_bMoving inside processMoveThreshold(), which a
     * failed read never reaches. A prior draft zeroed *out_motion too on
     * this path; this test exists so that regresses loudly if it ever
     * comes back. ---- */
    {
        const uint8_t burst[6] = { 0x00u, 0x01u, 0x00u, 0x02u, 0x00u, 0x03u };
        lis3dh_track_t track;

        /* STATUS read failure: out_sample zeroed, out_motion UNTOUCHED
         * (the sentinel survives), track->prev zeroed, returns false. */
        lis3dh_track_reset(&track);
        track.prev = (lis3dh_sample_t){ 111, 222, 333 };
        track.prev_valid = true;
        lis3dh_sample_t sample_sentinel = { -7, -8, -9 };
        lis3dh_motion_t motion_sentinel = { true, false, true, true };
        lis3dh_sample_t out_sample = sample_sentinel;
        lis3dh_motion_t out_motion = motion_sentinel;
        bool ok = lis3dh_advance(&track,
                                  /*status_read_ok=*/false, 0u,
                                  /*burst_read_ok=*/false, burst,
                                  500, &out_sample, &out_motion);
        ASSERT_TRUE(ok == false);
        ASSERT_EQ(out_sample.x, 0); ASSERT_EQ(out_sample.y, 0); ASSERT_EQ(out_sample.z, 0);
        ASSERT_TRUE(out_motion.x == motion_sentinel.x);
        ASSERT_TRUE(out_motion.y == motion_sentinel.y);
        ASSERT_TRUE(out_motion.z == motion_sentinel.z);
        ASSERT_TRUE(out_motion.any == motion_sentinel.any);
        ASSERT_EQ(track.prev.x, 0); ASSERT_EQ(track.prev.y, 0); ASSERT_EQ(track.prev.z, 0);

        /* ZYXDA clear: nothing touched at all, returns true. */
        lis3dh_track_reset(&track);
        track.prev = (lis3dh_sample_t){ 111, 222, 333 };
        track.prev_valid = true;
        out_sample = sample_sentinel;
        out_motion = motion_sentinel;
        ok = lis3dh_advance(&track,
                             /*status_read_ok=*/true, /*status=*/0x00u,
                             /*burst_read_ok=*/false, burst,
                             500, &out_sample, &out_motion);
        ASSERT_TRUE(ok == true);
        ASSERT_EQ(out_sample.x, sample_sentinel.x);
        ASSERT_TRUE(out_motion.x == motion_sentinel.x);
        ASSERT_EQ(track.prev.x, 111);   /* track untouched too */

        /* Burst read failure (ZYXDA set, but the 6-byte read fails): same
         * zero-sample/untouched-motion split as the STATUS failure. */
        lis3dh_track_reset(&track);
        track.prev = (lis3dh_sample_t){ 111, 222, 333 };
        track.prev_valid = true;
        out_sample = sample_sentinel;
        out_motion = motion_sentinel;
        ok = lis3dh_advance(&track,
                             /*status_read_ok=*/true, /*status=*/LIS3DH_STATUS_ZYXDA,
                             /*burst_read_ok=*/false, burst,
                             500, &out_sample, &out_motion);
        ASSERT_TRUE(ok == false);
        ASSERT_EQ(out_sample.x, 0); ASSERT_EQ(out_sample.y, 0); ASSERT_EQ(out_sample.z, 0);
        ASSERT_TRUE(out_motion.x == motion_sentinel.x);
        ASSERT_TRUE(out_motion.any == motion_sentinel.any);
        ASSERT_EQ(track.prev.x, 0);

        /* First successful read ever (prev_valid false): out_sample is
         * written to the new reading, but out_motion is left untouched --
         * the original never calls processMoveThreshold() on this first
         * sample either, so there is nothing to report yet. */
        lis3dh_track_reset(&track);
        out_sample = sample_sentinel;
        out_motion = motion_sentinel;
        ok = lis3dh_advance(&track,
                             true, LIS3DH_STATUS_ZYXDA,
                             true, burst,
                             500, &out_sample, &out_motion);
        ASSERT_TRUE(ok == true);
        ASSERT_EQ(out_sample.x, lis3dh_assemble_axis(burst[0], burst[1]));
        ASSERT_TRUE(out_motion.x == motion_sentinel.x);   /* untouched */
        ASSERT_TRUE(track.prev_valid == true);

        /* Second successful read: now a real motion check runs and
         * out_motion IS written (overwriting the sentinel). */
        const uint8_t burst2[6] = { 0x00u, 0x40u, 0x00u, 0x02u, 0x00u, 0x03u };
        out_sample = sample_sentinel;
        out_motion = motion_sentinel;
        ok = lis3dh_advance(&track,
                             true, LIS3DH_STATUS_ZYXDA,
                             true, burst2,
                             /*move_threshold=*/1,
                             &out_sample, &out_motion);
        ASSERT_TRUE(ok == true);
        /* burst2's X moved from 0x0100 to 0x4000 -- a real, large delta --
         * so motion.x must now be true, no longer the untouched sentinel. */
        ASSERT_TRUE(out_motion.x == true);
    }

    TEST_RETURN();
}
