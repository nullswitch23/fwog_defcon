#include "sensors/lis3dh.h"

/* ---- Pure: assembly, conversion, motion threshold. See the header for the
 * datasheet citations behind each of these. ---- */

int16_t lis3dh_assemble_axis(uint8_t lo, uint8_t hi) {
    return (int16_t)((uint16_t)((uint16_t)hi << 8) | (uint16_t)lo);
}

int32_t lis3dh_mg_per_lsb(lis3dh_range_t range) {
    /* Table 4 "Mechanical characteristics", So (Sensitivity) row for Normal
     * mode (10-bit data output) -- the mode configure()/setRange() always
     * select (LPen=0, HR=0). See the header's derivation. */
    switch (range) {
    case LIS3DH_RANGE_2G:  return 4;
    case LIS3DH_RANGE_4G:  return 8;
    case LIS3DH_RANGE_8G:  return 16;
    case LIS3DH_RANGE_16G: return 48;
    default:               return 4;   /* not a value setRange() can produce */
    }
}

int32_t lis3dh_raw_to_mg(int16_t raw, lis3dh_range_t range) {
    /* OUT_X_L/OUT_X_H etc. are two's-complement LEFT-JUSTIFIED (datasheet
     * 8.16-8.18): in Normal (10-bit) mode the real data occupies bits 15:6,
     * with bits 5:0 as padding. An arithmetic right shift by 6 recovers the
     * signed 10-bit digit count; treating the raw 16-bit value as if it
     * were already that count would overstate every reading by 64x. Signed
     * right shift of a negative value is implementation-defined by the C
     * standard, but arithmetic (sign-preserving) on every target this BSP
     * builds for -- the same assumption lis3dh_assemble_axis() and the rest
     * of this codebase's bit manipulation already relies on. */
    int32_t digit = (int32_t)raw >> 6;
    return digit * lis3dh_mg_per_lsb(range);
}

lis3dh_motion_t lis3dh_check_motion(lis3dh_sample_t current,
                                     lis3dh_sample_t previous,
                                     int32_t move_threshold) {
    lis3dh_motion_t m;

    int32_t dx = (int32_t)current.x - (int32_t)previous.x;
    if (dx < 0) dx = -dx;   /* absolute value, transcribed from :193 */
    int32_t dy = (int32_t)current.y - (int32_t)previous.y;
    if (dy < 0) dy = -dy;   /* :201 */
    int32_t dz = (int32_t)current.z - (int32_t)previous.z;
    if (dz < 0) dz = -dz;   /* :209 */

    /* Strictly greater-than, matching :195/:203/:211 exactly -- a delta
     * equal to move_threshold is NOT motion. */
    m.x = dx > move_threshold;
    m.y = dy > move_threshold;
    m.z = dz > move_threshold;
    m.any = m.x || m.y || m.z;   /* :218, `m_bMoving = m_bMovingX | ... ` */
    return m;
}

void lis3dh_track_reset(lis3dh_track_t *track) {
    track->prev = (lis3dh_sample_t){0};
    track->prev_valid = false;
}

bool lis3dh_advance(lis3dh_track_t *track,
                     bool status_read_ok, uint8_t status,
                     bool burst_read_ok, const uint8_t burst[6],
                     int32_t move_threshold,
                     lis3dh_sample_t *out_sample,
                     lis3dh_motion_t *out_motion) {
    if (!status_read_ok) {
        /* :146-152 -- zero the retained/output sample exactly like the
         * original's `m_iX=0; m_iY=0; m_iZ=0;`. out_motion is deliberately
         * left completely untouched: m_bMovingX/Y/Z/m_bMoving are written
         * ONLY inside lis3dh_check_motion()'s call site below, which this
         * path never reaches -- see the header's "Read-failure semantics"
         * section for why widening the zeroing to motion would be an
         * undocumented (and wrong) deviation, not a faithful port. */
        track->prev = (lis3dh_sample_t){0};
        if (out_sample) *out_sample = (lis3dh_sample_t){0};
        return false;
    }

    if ((status & LIS3DH_STATUS_ZYXDA) == 0u) {
        /* No new data -- process() returns here (:156-157) without
         * touching m_iX/Y/Z, m_bMoving*, or the prev-sample bookkeeping.
         * Leave out_sample/out_motion exactly as the caller passed them. */
        return true;
    }

    if (!burst_read_ok) {
        /* :161-165 -- same zero-sample/leave-motion-alone split as the
         * STATUS-read failure above. */
        track->prev = (lis3dh_sample_t){0};
        if (out_sample) *out_sample = (lis3dh_sample_t){0};
        return false;
    }

    lis3dh_sample_t current;
    current.x = lis3dh_assemble_axis(burst[0], burst[1]);
    current.y = lis3dh_assemble_axis(burst[2], burst[3]);
    current.z = lis3dh_assemble_axis(burst[4], burst[5]);

    /* Motion is checked against whatever was current LAST time this
     * succeeded (track->prev), before track->prev is advanced below --
     * matching the original's `m_iXPrev = m_iX;` (old current becomes
     * previous) followed by `m_iX = <new reading>;` and then
     * processMoveThreshold(). out_motion is written ONLY when that check
     * actually runs -- on the very first successful read (prev_valid still
     * false), the original never calls processMoveThreshold() either, so
     * out_motion is left untouched here too, not zeroed or defaulted. */
    if (track->prev_valid) {
        lis3dh_motion_t motion = lis3dh_check_motion(current, track->prev,
                                                      move_threshold);
        if (out_motion) *out_motion = motion;
    } else {
        track->prev_valid = true;   /* :183-184, first successful read */
    }

    track->prev = current;
    if (out_sample) *out_sample = current;
    return true;
}

#ifndef HOST_TEST
#include "common/diag.h"
#include "common/i2c_bus.h"
#include "platform/board.h"

/* One physical accelerometer exists on this board, so this mirrors the same
 * one-instance-per-module pattern input/buttons.c uses for its own
 * per-button debounce state, rather than threading a context struct through
 * every caller for a part that is not, and will not become, multi-instance. */
static lis3dh_track_t s_track;

void lis3dh_init(void) {
    lis3dh_track_reset(&s_track);
}

bool lis3dh_configure(lis3dh_range_t range) {
    uint8_t whoami;
    if (!fwog_i2c_read_regs(I2C_ADDR_ACCEL, LIS3DH_REG_WHOAMI, &whoami, 1u)) {
        return false;
    }
    if (whoami != LIS3DH_WHOAMI_ID) {
        /* Original: obConsole.printInColor("No response from Accelometer",
         * ...) (:238) -- console formatting, replaced with DIAG() per house
         * style. configure() returns here too, leaving CTRL_REG1 at its
         * power-on default (power-down mode) -- the safe path (see the
         * header's "non-hazards" note). */
        DIAG("[lis3dh] whoami mismatch: got 0x%02x, expected 0x%02x\n",
             (unsigned)whoami, (unsigned)LIS3DH_WHOAMI_ID);
        return false;
    }

    if (!fwog_i2c_write_reg(I2C_ADDR_ACCEL, LIS3DH_REG_CTRL_REG1,
                             LIS3DH_CTRL1_100HZ_XYZ_ON)) {
        return false;
    }

    uint8_t fs;
    switch (range) {
    case LIS3DH_RANGE_2G:  fs = LIS3DH_CTRL4_FS_2G;  break;
    case LIS3DH_RANGE_4G:  fs = LIS3DH_CTRL4_FS_4G;  break;
    case LIS3DH_RANGE_8G:  fs = LIS3DH_CTRL4_FS_8G;  break;
    case LIS3DH_RANGE_16G: fs = LIS3DH_CTRL4_FS_16G; break;
    default:               fs = LIS3DH_CTRL4_FS_2G;  break;
    }
    /* setRange() writes CTRL_REG4 as a whole byte -- BDU, HR and both
     * self-test bits are forced to 0 by this write every time. See the
     * header's deviation note. */
    return fwog_i2c_write_reg(I2C_ADDR_ACCEL, LIS3DH_REG_CTRL_REG4, fs);
}

bool lis3dh_process(int32_t move_threshold,
                     lis3dh_sample_t *out_sample,
                     lis3dh_motion_t *out_motion) {
    uint8_t status = 0u;
    bool status_ok = fwog_i2c_read_regs(I2C_ADDR_ACCEL, LIS3DH_REG_STATUS,
                                         &status, 1u);

    uint8_t burst[6] = {0};
    bool burst_ok = false;
    if (status_ok && (status & LIS3DH_STATUS_ZYXDA) != 0u) {
        /* LIS3DH_AUTO_INCREMENT (0x80) is load-bearing here -- see the
         * header's "the one transfer that needs care". */
        burst_ok = fwog_i2c_read_regs(I2C_ADDR_ACCEL,
                                       LIS3DH_AUTO_INCREMENT | LIS3DH_REG_OUT_X_L,
                                       burst, 6u);
    }

    return lis3dh_advance(&s_track, status_ok, status, burst_ok, burst,
                           move_threshold, out_sample, out_motion);
}
#endif
