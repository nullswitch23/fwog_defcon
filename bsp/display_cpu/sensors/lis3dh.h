/* ST LIS3DH accelerometer at I2C_ADDR_ACCEL (0x19) on the display CPU's
 * I2C1 -- bsp/display_cpu/platform/board.h.
 *
 * Ported from rmpLib/rpAccel_lis3dh.{h,cpp} (252 lines). Register #defines
 * exist at :11-18 there and are transcribed directly below. Only five
 * registers are ever touched: STATUS_REG (0x27), OUT_X_L (0x28), WHO_AM_I
 * (0x0F, expects 0x33), CTRL_REG1 (0x20), CTRL_REG4 (0x23). Datasheet
 * citations below are to ST's LIS3DH datasheet, DocID17530 Rev 2.
 *
 * ---- The one transfer that needs care ----
 * process() (:141-188) reads the six OUT_* registers with one burst read
 * from `0x80 | ACCEL_LIS3DH_REG_OUT_X_L` (:160). The 0x80 is the LIS3DH's
 * address auto-increment flag (SPI section 6.2 documents the same MS bit;
 * the I2C interface auto-increments on this device whenever bit 7 of the
 * register address is set) -- without it the six-byte transfer reads
 * OUT_X_L six times instead of OUT_X_L, OUT_X_H, OUT_Y_L, OUT_Y_H, OUT_Z_L,
 * OUT_Z_H in sequence, producing garbage that still looks like plausible
 * axis data. Preserved as LIS3DH_AUTO_INCREMENT below. All writes in this
 * driver are single-byte, so fwog_i2c_write_regs()'s 8-byte cap never
 * applies here.
 *
 * ---- Correction: there is NO g-conversion in the original ----
 * process() stores the raw signed 16-bit register values into m_iX/Y/Z and
 * stops. m_iRange only selects which CTRL_REG4 byte setRange() writes and
 * which unit label printAccel() prints -- it never scales anything read
 * back. Downstream consumers in the original firmware divide the raw value
 * by a fixed constant to make a brightness percentage, which is not a
 * physical unit conversion and is not reproduced here.
 *
 * lis3dh_raw_to_mg() below is an ADDITION, not a port: real raw-LSB ->
 * milli-g arithmetic, because it is the obvious thing a BSP consumer of an
 * accelerometer wants and it is pure, host-testable arithmetic. Getting it
 * right needs two facts the original ignores entirely:
 *
 * 1. Mode: setRange() (:114-138) writes CTRL_REG4 as a whole byte and is
 *    the only writer of that register, so HR (bit 3) is always 0. configure()
 *    (:244) writes CTRL_REG1 = 0x57 = 0b0101_0111: ODR[3:0]=0101 (100 Hz),
 *    LPen (bit 3) = 0, Zen/Yen/Xen = 1/1/1. LPen=0 and HR=0 together select
 *    **Normal mode (10-bit data output)** per Table 10 "Operating mode
 *    selection" -- neither low-power (LPen=1) nor high-resolution (HR=1).
 *    Table 4 "Mechanical characteristics" gives the So (Sensitivity) row for
 *    Normal mode: FS=+-2g -> 4 mg/digit, FS=+-4g -> 8, FS=+-8g -> 16,
 *    FS=+-16g -> 48. (High-resolution mode's row -- 1/2/4/12 mg/digit -- and
 *    low-power mode's row -- 16/32/64/192 -- both do NOT apply here.) Note 6
 *    under Table 4 cross-checks the 2g row independently: "1LSb = 4 mg at
 *    10-bit representation, +-2g full scale." Table 10 repeats the same 2g
 *    figure in its own "So @ +-2g" column. Three independent citations in
 *    the same document agree, which is as sure as a datasheet gets.
 *
 * 2. Justification: section 8.16 "OUT_X_L (28h), OUT_X_H (29h)" states "The
 *    value is expressed as two's complement left-justified" for all three
 *    axes, referring back to 3.2.1 for which bits that leaves valid. In
 *    Normal (10-bit) mode the valid data occupies bits 15:6 of the 16-bit
 *    pair; bits 5:0 are padding, not extra precision. So converting to
 *    "digits" requires an arithmetic right-shift by 6 BEFORE multiplying by
 *    the mg/digit figure above -- treating the raw 16-bit value as if it
 *    were already the 10-bit digit count (the classic left-justification
 *    error) overstates every reading by 64x.
 *
 * ---- setRange() clears BDU, HR and both self-test bits every call ----
 * (:114-138, deviation documented, not fixed: it writes CTRL_REG4 as a whole
 * byte and is that register's only writer, so calling it after anything else
 * has set BDU/HR/ST[1:0] would silently clear them too. Harmless today --
 * nothing else in this driver, or in the original, ever sets those bits --
 * but the milli-g conversion above depends on HR staying 0. Anyone who later
 * adds high-resolution support must revisit lis3dh_raw_to_mg()'s sensitivity
 * table (the HR row is 1/2/4/12 mg/digit, not 4/8/16/48) and the shift
 * (12-bit HR data is left-justified in bits 15:4, a shift of 4, not 6).)
 *
 * ---- Read-failure semantics: exactly what is zeroed, and what never was ----
 * process() (:141-188) has two I2C read sites -- the STATUS read and the
 * OUT_* burst read -- and both failure branches run the identical
 * `m_iX=0; m_iY=0; m_iZ=0; return;` (:147-151, :162-166). That zeroes the
 * CURRENT sample. Because the next successful call's first three lines are
 * `m_iXPrev = m_iX; m_iYPrev = m_iY; m_iZPrev = m_iZ;` -- BEFORE m_iX/Y/Z
 * are overwritten with the new reading -- a zeroed current sample also
 * becomes the PREVIOUS sample fed to the following call's motion check.
 * lis3dh_advance() below mirrors this with `track->prev`.
 *
 * Neither failure branch EVER touches m_bMovingX/Y/Z or m_bMoving. Those
 * four booleans are written in exactly one place in the whole file: inside
 * processMoveThreshold() (:190-220), which process() calls only when a read
 * has fully succeeded AND m_bPrevValuesLoaded was already true. A failed
 * read cannot reach that call, so on a real glitch the motion flags are left
 * exactly as they were -- stale, but never asserted to any particular value.
 * A first port of this driver zeroed the motion output on the failure path
 * too, reasoning (wrongly) that it was "the same zeroing as the sample" --
 * it is not: the original's zeroing is scoped to m_iX/Y/Z alone, and
 * widening it to the motion flags asserts "definitely not moving" at
 * exactly the moment the driver has no idea, which the original never does.
 * lis3dh_advance() below zeroes the sample output on either failure path but
 * leaves the motion output completely untouched -- no write of any kind --
 * so a caller keeping its own lis3dh_motion_t across polls sees a transient
 * I2C glitch zero the sample for that cycle while its last known motion
 * state survives unchanged, exactly like the original firmware would.
 *
 * ---- setTemperature() is NOT ported ----
 * rpAccel_lis3dh::setTemperature() (:28-41) reads the RP2040's own on-chip
 * ADC (its caller passes obADC.m_iChannelResults[0]) and applies the
 * standard Pico SDK internal-temperature-sensor formula. It performs no I2C
 * at all -- it is not accelerometer code, despite living in this class.
 * Its final `- 7.5` offset is documented in the original only as "my manual
 * cal", an unexplained empirical constant with no derivation given. Left out
 * entirely; it belongs with ADC code if anyone wants it, not here.
 *
 * ---- Dropped ----
 * printAccel()/printAccelToString() are console formatting (rpConsole,
 * snprintf) -- this BSP never printfs from driver code (DIAG() only) and has
 * no console object to format into.
 *
 * ---- Non-hazards (recon-i2c.md), confirmed against the datasheet too ----
 * No self-test bits are ever set (ST[1:0] stays 00, "Normal mode" per
 * Table 39) and no interrupt-latch configuration exists anywhere in the
 * original. configure() (:222-252) checks WHO_AM_I and returns early on a
 * mismatch, leaving CTRL_REG1 at its power-on default (0x07: power-down
 * mode, all axes enabled) -- the safe path. process() always polls
 * STATUS_REG's ZYXDA bit (0x08, Table 46/47) before reading OUT_*, so there
 * are no blind reads.
 */
#ifndef FWOG_LIS3DH_H
#define FWOG_LIS3DH_H
#include <stdbool.h>
#include <stdint.h>

/* ---- Register map, transcribed from rpAccel_lis3dh.cpp:11-18 and checked
 * against the datasheet's own register list (section 8). ---- */
#define LIS3DH_REG_STATUS     0x27u   /* STATUS_REG, section 8.15 */
#define LIS3DH_REG_OUT_X_L    0x28u   /* first of six OUT_* registers, 8.16-8.18 */
#define LIS3DH_REG_WHOAMI     0x0Fu   /* WHO_AM_I, section 8.5 */
#define LIS3DH_REG_CTRL_REG1  0x20u   /* section 8.8 */
#define LIS3DH_REG_CTRL_REG4  0x23u   /* section 8.11 */

#define LIS3DH_WHOAMI_ID      0x33u   /* Table 24: 00110011 */
#define LIS3DH_STATUS_ZYXDA   0x08u   /* STATUS_REG bit 3, Table 46/47 */

/* OR'd into a register address to auto-increment across a multi-byte I2C
 * transfer -- see the header comment above ("the one transfer that needs
 * care") for why this bit is not optional on the 6-byte OUT_* burst. */
#define LIS3DH_AUTO_INCREMENT 0x80u

/* CTRL_REG1 value transcribed verbatim from configure() (rpAccel_lis3dh.cpp
 * :244, comment "100hz, enable all axes"): ODR[3:0]=0101 (100 Hz, Table 31),
 * LPen=0 (bit 3, normal/high-res -- not low-power), Zen=Yen=Xen=1. */
#define LIS3DH_CTRL1_100HZ_XYZ_ON 0x57u

/* Full-scale selection. FS[1:0] live at CTRL_REG4 bits 5:4 (Table 37);
 * every other bit (BDU, BLE, HR, ST1, ST0, SIM) is forced to 0 by writing
 * the whole byte -- see setRange() and the deviation note above. */
typedef enum {
    LIS3DH_RANGE_2G = 0,
    LIS3DH_RANGE_4G,
    LIS3DH_RANGE_8G,
    LIS3DH_RANGE_16G
} lis3dh_range_t;

#define LIS3DH_CTRL4_FS_2G   0x00u   /* setRange() range2g,  btData[0]=0x00 */
#define LIS3DH_CTRL4_FS_4G   0x10u   /* setRange() range4g,  btData[0]=0x10 */
#define LIS3DH_CTRL4_FS_8G   0x20u   /* setRange() range8g,  btData[0]=0x20 */
#define LIS3DH_CTRL4_FS_16G  0x30u   /* setRange() range16g, btData[0]=0x30 */

/* m_iMoveThreshold's default (rpAccel_lis3dh.h:46), in raw LSBs -- the
 * original never converts this to a physical unit either. */
#define LIS3DH_MOVE_THRESHOLD_DEFAULT 500

/* One (x, y, z) sample. Raw signed 16-bit register values, exactly what
 * m_iX/m_iY/m_iZ hold in the original -- NOT milli-g. See
 * lis3dh_raw_to_mg() to convert. */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} lis3dh_sample_t;

/* Per-axis "is this axis moving" plus the OR of all three -- the C
 * equivalent of m_bMovingX/Y/Z and m_bMoving. */
typedef struct {
    bool x;
    bool y;
    bool z;
    bool any;
} lis3dh_motion_t;

/* Carried across successive polls -- the C equivalent of the original's
 * m_iXPrev/m_iYPrev/m_iZPrev and m_bPrevValuesLoaded. Zero it, or call
 * lis3dh_track_reset(), before the first poll. */
typedef struct {
    lis3dh_sample_t prev;
    bool prev_valid;
} lis3dh_track_t;

/* ---- Pure: host-tested, no I2C ---- */

/* Assemble one axis from its low/high bytes exactly as process() does
 * (rpAccel_lis3dh.cpp:176-178: `((int)btData[hi] << 8) | btData[lo]`,
 * assigned into a `short`). The values are signed 16-bit little-endian
 * pairs -- getting the sign extension right is exactly where a port of this
 * kind of driver breaks. The uint16_t intermediate makes the reinterpretation
 * as a signed two's-complement value well-defined in C, where the original
 * relied on its compiler's int->short narrowing conversion (implementation-
 * defined by the C++ standard, but two's-complement wraparound in practice
 * on every compiler this firmware ships on). */
int16_t lis3dh_assemble_axis(uint8_t lo, uint8_t hi);

/* Sensitivity, in milli-g per LSB of the 10-bit Normal-mode digit (see the
 * header's derivation) for a given full-scale range. Not a port -- see the
 * header. */
int32_t lis3dh_mg_per_lsb(lis3dh_range_t range);

/* Addition, not a port (see the header's long comment): convert one raw,
 * left-justified 16-bit register reading to milli-g, assuming Normal mode
 * (HR=0, LPen=0 -- what configure()/setRange() always leave the part in).
 * Right-shifts by 6 to undo the left-justification (arithmetic shift,
 * preserving sign) before scaling by lis3dh_mg_per_lsb(). */
int32_t lis3dh_raw_to_mg(int16_t raw, lis3dh_range_t range);

/* Ported: processMoveThreshold() (rpAccel_lis3dh.cpp:190-220). Absolute
 * per-axis delta against move_threshold, no debounce, no hysteresis, exactly
 * as the original. Strictly greater-than (the original's `if (iDeltaX >
 * m_iMoveThreshold)`) -- a delta EQUAL to the threshold is NOT motion;
 * preserved rather than "corrected" to >=. Deltas and the threshold are
 * plain raw LSBs, matching the original (m_iMoveThreshold is never
 * unit-converted there either). */
lis3dh_motion_t lis3dh_check_motion(lis3dh_sample_t current,
                                     lis3dh_sample_t previous,
                                     int32_t move_threshold);

/* Zero a tracking struct, matching a freshly constructed rpAccel_lis3dh
 * (m_bPrevValuesLoaded starts false). */
void lis3dh_track_reset(lis3dh_track_t *track);

/* Pure core of process() (rpAccel_lis3dh.cpp:141-188), factored out from the
 * I2C calls that feed it so the state machine -- including its read-failure
 * semantics -- is host-tested directly instead of only reachable behind
 * real hardware. Mirrors input/buttons.c's fwog_btn_step()/fwog_buttons_poll()
 * split: this is the "step" half, lis3dh_process() below is the "poll" half
 * that does the actual I2C and hands its outcome to this function.
 *
 * `status_read_ok`/`status` are the outcome of the STATUS_REG read.
 * `burst_read_ok`/`burst` are the outcome of the OUT_* burst read, and are
 * only meaningful (only get looked at) when status_read_ok is true and
 * ZYXDA (LIS3DH_STATUS_ZYXDA) is set in `status` -- exactly the condition
 * under which process() itself performs that second read.
 *
 * Returns false on either failure ('status_read_ok' false, or ZYXDA set but
 * 'burst_read_ok' false), true otherwise (including the "no new data"
 * case). See the header's "Read-failure semantics" section for exactly
 * which outputs change on the false path and which are deliberately left
 * alone: track->prev and *out_sample (if non-NULL) are zeroed; *out_motion
 * is NEVER written on a failure, matching the original never reaching
 * processMoveThreshold() on that path. On success with new data,
 * *out_motion is written only if `track` already held a valid previous
 * sample (the very first successful poll after a reset skips the check,
 * matching m_bPrevValuesLoaded, and leaves *out_motion untouched too). */
bool lis3dh_advance(lis3dh_track_t *track,
                     bool status_read_ok, uint8_t status,
                     bool burst_read_ok, const uint8_t burst[6],
                     int32_t move_threshold,
                     lis3dh_sample_t *out_sample,
                     lis3dh_motion_t *out_motion);

#ifndef HOST_TEST
/* ---- I2C-bound operations. Every one goes through fwog_i2c_* and is
 * therefore bounded by FWOG_I2C_TIMEOUT_US -- a wedged accelerometer cannot
 * hang the caller. ---- */

/* Reset this module's own lis3dh_track_t, matching a freshly constructed
 * rpAccel_lis3dh (m_bPrevValuesLoaded starts false). Does not touch the
 * bus. Safe to call more than once. */
void lis3dh_init(void);

/* Ported: configure() (rpAccel_lis3dh.cpp:222-252). Reads WHO_AM_I; on
 * mismatch or I2C failure, DIAGs and returns false WITHOUT writing
 * CTRL_REG1 -- the part is left at its power-on default (power-down mode),
 * the same safe path the original leaves it in. On a match, writes
 * CTRL_REG1 (100 Hz, all axes) then CTRL_REG4 for `range` via setRange()'s
 * whole-byte write, returning false (with CTRL_REG1 already written) if
 * that second write fails.
 *
 * The original's configure() is `void` and its own equivalent failure check
 * -- `if (!m_pI2C->write(...CTRL_REG4...)) { }` -- has an empty body, so it
 * exposes no success/failure signal of any kind to its caller; there is no
 * original return behaviour for this `bool` to match the "shape" of. The
 * bool return is new, and necessary, precisely because this is a function
 * rather than a class with member state a caller could inspect afterward. */
bool lis3dh_configure(lis3dh_range_t range);

/* Ported: process() (rpAccel_lis3dh.cpp:141-188), as a thin I2C shell around
 * lis3dh_advance() above. Reads STATUS_REG; if that succeeds and ZYXDA is
 * set, reads the 6-byte OUT_* burst with the auto-increment bit; hands both
 * outcomes to lis3dh_advance() together with this module's own
 * lis3dh_track_t, `out_sample` and `out_motion`. See lis3dh_advance()'s own
 * comment, and the header's "Read-failure semantics" section, for exactly
 * what changes and what is deliberately left alone on a failure.
 *
 * out_sample and out_motion may each be NULL if the caller does not want
 * that half of the result. */
bool lis3dh_process(int32_t move_threshold,
                     lis3dh_sample_t *out_sample,
                     lis3dh_motion_t *out_motion);
#else
/* Host / ogemu shells: same names display apps already call. Samples come
 * from the world inject bus (accel / shake JSONL), not I2C. */
void lis3dh_init(void);
bool lis3dh_configure(lis3dh_range_t range);
bool lis3dh_process(int32_t move_threshold,
                     lis3dh_sample_t *out_sample,
                     lis3dh_motion_t *out_motion);
#endif

#endif
