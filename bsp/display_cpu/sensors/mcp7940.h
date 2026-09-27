/* Microchip MCP7940 real-time clock at I2C_ADDR_RTC (0x6F) on the display
 * CPU's I2C1 -- bsp/display_cpu/platform/board.h.
 *
 * Ported from rmpLib/rpRTCmcp7940.{h,cpp}. The original defines no register
 * map at all -- every register number is a bare literal at its call site --
 * so the constants below are transcribed from the MCP7940 datasheet, then
 * checked against every read/write the original actually performs:
 * readTime() (rpRTCmcp7940.cpp:18-58) reads registers 0x00-0x07 as one
 * 8-byte burst and then register 0x08 separately; writeTime() (:98-127)
 * writes 0x00-0x06 as one 7-byte burst and then 0x08 separately. Both shapes
 * fit fwog_i2c_read_regs()/fwog_i2c_write_regs() (8-byte cap) directly -- no
 * helper gap, as the recon predicted.
 *
 * ---- Three deliberate deviations from the original -- read this before
 * touching mcp7940_enable_osc() / is_configured() / any RTCWKDAY write ----
 *
 * 1. REAL BUG in the original, not carried forward: enableOsc()
 *    (rpRTCmcp7940.cpp:59-70) writes RTCSEC and RTCWKDAY as whole bytes
 *    (0x80 and 0x08). That sets ST and VBATEN correctly but ALSO zeroes the
 *    BCD seconds field and the weekday field in the same stroke -- and the
 *    original's own header calls weekday 0 invalid (rpRTCmcp7940.h:26). It
 *    runs exactly once, only when !isConfigured(), i.e. on a cold RTC with a
 *    dead coin cell -- precisely the state hardware bring-up will hit.
 *    mcp7940_enable_osc() below is a read-modify-write instead: it reads
 *    RTCSEC and RTCWKDAY first and ORs in only ST and VBATEN, leaving
 *    seconds and weekday exactly as they were.
 *
 * 2. isConfigured() (rpRTCmcp7940.h:18) is `m_bOSCEnabled && m_bbVBATTEnabled`,
 *    and m_bOSCEnabled is set from ST (RTCSEC bit 7) alone -- which records
 *    only that a start was *requested*, not that the crystal is actually
 *    oscillating. The real confirmation is OSCRUN (RTCWKDAY bit 5), which
 *    the original never reads anywhere in this file. This port exposes
 *    both, decoded once per mcp7940_read_time() call and named so a caller
 *    cannot confuse them: mcp7940_is_configured() (ported: ST && VBATEN,
 *    same as the original) and mcp7940_is_oscillating() (new logic -- OSCRUN
 *    is not read by anything in the original).
 *
 * 3. PWRFAIL (RTCWKDAY bit 4, "the battery-backed time may be wrong") is
 *    written blind by both writeTime() (:108) and enableOsc() (:65) in the
 *    original, because both write RTCWKDAY as a whole byte without reading
 *    it first. This port decodes and exposes PWRFAIL on every read
 *    (mcp7940_time_t.pwrfail) so a caller can tell "the time I just read is
 *    meaningless" apart from a normal read.
 *
 *    That is the only thing software can do about it, though -- PWRFAIL
 *    cannot be preserved by ANY write to RTCWKDAY, read-modify-write or not.
 *    MCP7940N datasheet, Register 5-4 (RTCWKDAY), Note 2: "The PWRFAIL bit
 *    cannot be written to a '1' in software. Writing to the RTCWKDAY
 *    register will always clear the PWRFAIL bit." The clear is a side
 *    effect of the WRITE TRANSACTION itself, not of the value written, so
 *    mcp7940_enable_osc()'s read-modify-write below gives it no protection:
 *    that function still issues a write to RTCWKDAY (to set VBATEN), so it
 *    still clears PWRFAIL -- exactly like mcp7940_write_time(), and exactly
 *    like the original's enableOsc() did. This is not a regression against
 *    the original (the original clears it too, the same way); the point is
 *    that no software workaround exists, so a caller that cares about
 *    PWRFAIL MUST read and act on it BEFORE calling either
 *    mcp7940_enable_osc() or mcp7940_write_time() -- once either has run,
 *    the flag is gone regardless of what this driver does with the rest of
 *    the register.
 *
 * ---- What was added, deliberately, and kept separable from the port ----
 * The original has no range validation anywhere: readTime() trusts the wire
 * and writeTime() trusts the caller; only UI code upstream enforces ranges.
 * mcp7940_bcd_valid() (a BCD nibble > 9 is detectable) and the per-field
 * range checks inside mcp7940_read_time()/mcp7940_write_time() below are
 * additions, not transcription -- a corrupt read producing e.g. month 0x99
 * (a syntactically valid BCD byte that decodes to month 19) is now rejected
 * instead of silently handed to the caller. Each addition is called out at
 * its own site so a reader can tell port from addition at a glance.
 *
 * ---- Dropped ----
 * printTime() and getLongDateAsText() depend on the SDK's datetime_t and the
 * rpConsole object; this BSP never printfs from driver code (DIAG() only)
 * and has no console object to format into. getDayOfWeekAsText()/
 * getMonthAsText() are console text tables for those same dropped callers
 * and go with them. saveStartupTime()'s "remember the boot-time snapshot
 * separately" bookkeeping is UI/application state, not a register
 * operation, and is left for whichever app needs it. Register 0x07
 * (CONTROL) is read as part of the original's 8-byte burst and never used
 * there either -- kept in the burst here for fidelity to the original's
 * transfer shape, and discarded the same way; see mcp7940_read_time().
 */
#ifndef FWOG_MCP7940_H
#define FWOG_MCP7940_H
#include <stdbool.h>
#include <stdint.h>

/* ---- Register map, transcribed from the MCP7940 datasheet (the original
 * has no named constants for any of these -- see the header comment). ---- */
#define MCP7940_REG_RTCSEC   0x00u   /* bit 7 ST: oscillator start request */
#define MCP7940_REG_RTCMIN   0x01u
#define MCP7940_REG_RTCHOUR  0x02u   /* 24-hour BCD, as the original always reads it: tens in bits 5:4 */
#define MCP7940_REG_RTCWKDAY 0x03u   /* bit 5 OSCRUN, bit 4 PWRFAIL, bit 3 VBATEN, bits 2:0 weekday (1-7; 0 invalid) */
#define MCP7940_REG_RTCDATE  0x04u   /* day of month, BCD */
#define MCP7940_REG_RTCMTH   0x05u   /* month, BCD */
#define MCP7940_REG_RTCYEAR  0x06u   /* year 00-99, BCD */
#define MCP7940_REG_CONTROL  0x07u   /* read as part of the 8-byte burst, never used -- see mcp7940_read_time() */
#define MCP7940_REG_OSCTRIM  0x08u   /* opaque digital trim value; passed through, not interpreted here */

#define MCP7940_RTCSEC_ST        0x80u
#define MCP7940_RTCWKDAY_WKDAY   0x07u   /* NOT BCD -- a plain 3-bit count, unlike every other field below */
#define MCP7940_RTCWKDAY_VBATEN  0x08u
#define MCP7940_RTCWKDAY_PWRFAIL 0x10u
#define MCP7940_RTCWKDAY_OSCRUN  0x20u

/* Tens-digit mask for each BCD field, aligned so `(reg & mask) >> 4` gives
 * the tens digit directly. Transcribed from the per-field mask the
 * original's readTime() applies inline (rpRTCmcp7940.cpp:29-37): SEC/MIN use
 * 3 tens bits (0-5 seconds/minutes), HOUR/DATE use 2 (0-2 hours in 24h mode,
 * 0-3 day-of-month), MTH uses 1 (0-1), YEAR uses the full nibble (0-9). */
#define MCP7940_TENS_MASK_SEC_MIN   0x70u
#define MCP7940_TENS_MASK_HOUR_DATE 0x30u
#define MCP7940_TENS_MASK_MONTH     0x10u
#define MCP7940_TENS_MASK_YEAR      0xF0u

/* ---- Range constants used only by the validation addition described
 * above -- not part of the register map, not in the original. ---- */
#define MCP7940_SEC_MAX    59u
#define MCP7940_MIN_MAX    59u
#define MCP7940_HOUR_MAX   23u
#define MCP7940_WKDAY_MIN   1u
#define MCP7940_WKDAY_MAX   7u
#define MCP7940_DATE_MIN    1u
#define MCP7940_DATE_MAX   31u
#define MCP7940_MONTH_MIN   1u
#define MCP7940_MONTH_MAX  12u
/* No MCP7940_YEAR_MAX/MIN pair here, unlike every other field above: year
 * is bounded structurally rather than by an explicit range check.
 * mcp7940_bcd_valid() on MCP7940_TENS_MASK_YEAR already restricts the byte
 * to two BCD digits 0-9, i.e. exactly 0-99 -- the field's own full range --
 * so there is no invalid value left for a separate MAX constant to reject. */

/* One read/write snapshot -- the C equivalent of the original's public
 * iRTC* fields, plus the two status bits that used to live in
 * m_bOSCEnabled/m_bbVBATTEnabled and the PWRFAIL bit the original never
 * exposed at all (deviation 3 above). */
typedef struct {
    uint8_t sec;    /* 0-59 */
    uint8_t min;    /* 0-59 */
    uint8_t hour;   /* 0-23 -- always 24-hour, matching how the original decodes RTCHOUR */
    uint8_t wkday;  /* 1-7, Monday=1..Sunday=7 per the original header's comment; 0 is invalid */
    uint8_t date;   /* day of month, 1-31 */
    uint8_t month;  /* 1-12 */
    uint8_t year;   /* 0-99, meaning 2000-2099 */
    uint8_t trim;   /* OSCTRIM, opaque passthrough -- same as the original's iRTCTrim */
    bool vbat_enabled;  /* VBATEN: battery backup enabled */
    bool pwrfail;       /* PWRFAIL: set by the part when it lost power on battery backup -- see deviation 3 */
    bool osc_started;   /* ST: an oscillator start was requested (deviation 2) */
    bool osc_running;   /* OSCRUN: the crystal is actually confirmed oscillating (deviation 2, new logic) */
} mcp7940_time_t;

/* ---- Pure BCD<->binary conversion. Host-tested, no I2C. ---- */

/* Decode one MCP7940 BCD register byte using the field's own tens_mask (one
 * of the MCP7940_TENS_MASK_* constants above). Transcribed directly from the
 * per-field inline arithmetic in the original's readTime()
 * (rpRTCmcp7940.cpp:29-37: `((byte & mask) >> 4) * 10 + (byte & 0xF)`).
 * Performs NO validation, exactly like the original: a corrupt byte with an
 * out-of-range nibble still decodes to whatever the arithmetic produces.
 * Pair with mcp7940_bcd_valid() wherever a caller must tell a good read from
 * a corrupt one -- mcp7940_read_time() does. */
uint8_t mcp7940_bcd_to_bin(uint8_t reg, uint8_t tens_mask);

/* True if both the tens digit (masked by tens_mask, then shifted to 0-15)
 * and the ones digit (bits 3:0, 0-15) are valid BCD digits (<=9). This check
 * has no equivalent in the original -- it is the validation addition
 * described in the header comment, split out so mcp7940_read_time() can
 * reject a corrupt byte instead of silently decoding one. Note that a
 * BCD-valid byte can still decode to an out-of-range field value (e.g.
 * month byte 0x99 is BCD-valid -- both nibbles are 9 -- but decodes to
 * month 19); mcp7940_read_time()'s separate range check catches that case. */
bool mcp7940_bcd_valid(uint8_t reg, uint8_t tens_mask);

/* Encode a 0-99 binary value as BCD (tens in bits 7:4, ones in bits 3:0).
 * Reimplements toBCD() (rpRTCmcp7940.cpp:72-96) with plain /10, %10
 * arithmetic instead of the original's decrement-loop search for the same
 * result -- the loop is not worth preserving (see recon-i2c.md). Every field
 * this driver writes stays within 0-99 (year is the widest); values outside
 * that range are not a real input here and are not special-cased, matching
 * the original's own assumption (its loop also only behaves sensibly across
 * 0-99). */
uint8_t mcp7940_bin_to_bcd(uint8_t bin);

/* ---- Pure predicates over an already-read snapshot. Host-tested, no I2C --
 * mirrors how the original's own isConfigured() works: a plain getter over
 * fields readTime() already populated, not a fresh register read. ---- */

/* Ported: rpRTCmcp7940::isConfigured() is `m_bOSCEnabled && m_bbVBATTEnabled`
 * -- true once a start was requested AND battery backup is enabled. See
 * deviation 2 above for why this is NOT proof the crystal is running. */
static inline bool mcp7940_is_configured(const mcp7940_time_t *t) {
    return t->osc_started && t->vbat_enabled;
}

/* New logic, not a port (deviation 2): true only once OSCRUN confirms the
 * crystal is actually oscillating. Nothing in the original ever reads this
 * bit. A caller wanting to know "is this clock really ticking", as opposed
 * to "did we ask it to", should call this instead of
 * mcp7940_is_configured(). */
static inline bool mcp7940_is_oscillating(const mcp7940_time_t *t) {
    return t->osc_running;
}

#ifndef HOST_TEST
/* ---- I2C-bound operations. Every one goes through fwog_i2c_* and is
 * therefore bounded by FWOG_I2C_TIMEOUT_US -- a wedged RTC cannot hang the
 * caller. All return false on any I2C failure or (see above) a validation
 * failure. ---- */

/* Reads RTCSEC..CONTROL (0x00-0x07) as one 8-byte burst, then OSCTRIM (0x08)
 * separately -- the same two transfers readTime() makes. Addition: rejects
 * the read (returns false, *out untouched) if any BCD field fails
 * mcp7940_bcd_valid() or decodes to a value outside its field's real range
 * (sec/min 0-59, hour 0-23, weekday 1-7, date 1-31, month 1-12) -- the
 * original has no equivalent check and would hand a caller month 19 without
 * complaint. */
bool mcp7940_read_time(mcp7940_time_t *out);

/* Writes RTCSEC..RTCYEAR (0x00-0x06) as one 7-byte burst, then OSCTRIM
 * (0x08) separately -- the same two transfers writeTime() makes. Sets ST
 * (enables the oscillator) and VBATEN, exactly as `toBCD(iRTCSec) | 0x80`
 * and `iRTCWkDay | 0x8` did. Addition: refuses (returns false, no I2C
 * traffic at all) a *t outside the same ranges mcp7940_read_time() checks,
 * where the original's writeTime() trusts the caller completely. PWRFAIL is
 * cleared by this call, same as the original -- see deviation 3 above for
 * why that is kept rather than "fixed". */
bool mcp7940_write_time(const mcp7940_time_t *t);

/* Read-modify-write equivalent of enableOsc() -- see deviation 1 above for
 * why this cannot be the original's whole-byte writes. Sets ST (RTCSEC) and
 * VBATEN (RTCWKDAY) without disturbing seconds, weekday, or PWRFAIL. */
bool mcp7940_enable_osc(void);

#endif
#endif
