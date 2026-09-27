#include "sensors/mcp7940.h"

/* ---- Pure BCD<->binary conversion. Transcribed/reimplemented from
 * rpRTCmcp7940.cpp -- see the long comment in the header for exactly which
 * parts are a direct port and which are this port's additions. ---- */

uint8_t mcp7940_bcd_to_bin(uint8_t reg, uint8_t tens_mask) {
    uint8_t tens = (uint8_t)((reg & tens_mask) >> 4);
    uint8_t ones = (uint8_t)(reg & 0x0Fu);
    return (uint8_t)(tens * 10u + ones);
}

bool mcp7940_bcd_valid(uint8_t reg, uint8_t tens_mask) {
    uint8_t tens = (uint8_t)((reg & tens_mask) >> 4);
    uint8_t ones = (uint8_t)(reg & 0x0Fu);
    return tens <= 9u && ones <= 9u;
}

uint8_t mcp7940_bin_to_bcd(uint8_t bin) {
    uint8_t tens = (uint8_t)(bin / 10u);
    uint8_t ones = (uint8_t)(bin % 10u);
    return (uint8_t)((tens << 4) | ones);
}

#ifndef HOST_TEST
#include "common/i2c_bus.h"
#include "platform/board.h"

bool mcp7940_read_time(mcp7940_time_t *out) {
    uint8_t b[8];
    if (!fwog_i2c_read_regs(I2C_ADDR_RTC, MCP7940_REG_RTCSEC, b, 8u)) return false;

    uint8_t trim;
    if (!fwog_i2c_read_regs(I2C_ADDR_RTC, MCP7940_REG_OSCTRIM, &trim, 1u)) return false;

    /* b[7] is CONTROL (0x07) -- part of the original's 8-byte burst
     * (rpRTCmcp7940.cpp:23) and never used there either. Read for transfer
     * fidelity, discarded here the same way. */

    /* ---- Addition: reject a corrupt byte before decoding it (see the
     * header's "What was added" note). Not in the original. ---- */
    if (!mcp7940_bcd_valid(b[0], MCP7940_TENS_MASK_SEC_MIN) ||
        !mcp7940_bcd_valid(b[1], MCP7940_TENS_MASK_SEC_MIN) ||
        !mcp7940_bcd_valid(b[2], MCP7940_TENS_MASK_HOUR_DATE) ||
        !mcp7940_bcd_valid(b[4], MCP7940_TENS_MASK_HOUR_DATE) ||
        !mcp7940_bcd_valid(b[5], MCP7940_TENS_MASK_MONTH) ||
        !mcp7940_bcd_valid(b[6], MCP7940_TENS_MASK_YEAR)) {
        return false;
    }

    mcp7940_time_t t;
    t.sec   = mcp7940_bcd_to_bin(b[0], MCP7940_TENS_MASK_SEC_MIN);
    t.min   = mcp7940_bcd_to_bin(b[1], MCP7940_TENS_MASK_SEC_MIN);
    t.hour  = mcp7940_bcd_to_bin(b[2], MCP7940_TENS_MASK_HOUR_DATE);
    t.wkday = (uint8_t)(b[3] & MCP7940_RTCWKDAY_WKDAY);   /* not BCD -- a plain 3-bit count */
    t.date  = mcp7940_bcd_to_bin(b[4], MCP7940_TENS_MASK_HOUR_DATE);
    t.month = mcp7940_bcd_to_bin(b[5], MCP7940_TENS_MASK_MONTH);
    t.year  = mcp7940_bcd_to_bin(b[6], MCP7940_TENS_MASK_YEAR);
    t.trim  = trim;
    t.vbat_enabled = (b[3] & MCP7940_RTCWKDAY_VBATEN)  != 0u;
    t.pwrfail      = (b[3] & MCP7940_RTCWKDAY_PWRFAIL) != 0u;   /* deviation 3: exposed, never discarded on read */
    t.osc_started  = (b[0] & MCP7940_RTCSEC_ST)        != 0u;   /* ST -- deviation 2 */
    t.osc_running  = (b[3] & MCP7940_RTCWKDAY_OSCRUN)  != 0u;   /* OSCRUN -- deviation 2, new logic */

    /* ---- Addition: range-check the decoded values too. A BCD-valid byte
     * can still mean an impossible field -- month byte 0x99 passes
     * mcp7940_bcd_valid() (both nibbles are 9) but decodes to month 19.
     * Not in the original. ---- */
    if (t.sec > MCP7940_SEC_MAX || t.min > MCP7940_MIN_MAX || t.hour > MCP7940_HOUR_MAX ||
        t.wkday < MCP7940_WKDAY_MIN || t.wkday > MCP7940_WKDAY_MAX ||
        t.date < MCP7940_DATE_MIN || t.date > MCP7940_DATE_MAX ||
        t.month < MCP7940_MONTH_MIN || t.month > MCP7940_MONTH_MAX) {
        return false;
    }

    *out = t;
    return true;
}

bool mcp7940_write_time(const mcp7940_time_t *t) {
    /* ---- Addition: refuse an out-of-range time before any I2C traffic.
     * The original's writeTime() (rpRTCmcp7940.cpp:98-127) trusts the
     * caller's iRTC* fields completely -- see the header's "What was added"
     * note. ---- */
    if (t->sec > MCP7940_SEC_MAX || t->min > MCP7940_MIN_MAX || t->hour > MCP7940_HOUR_MAX ||
        t->wkday < MCP7940_WKDAY_MIN || t->wkday > MCP7940_WKDAY_MAX ||
        t->date < MCP7940_DATE_MIN || t->date > MCP7940_DATE_MAX ||
        t->month < MCP7940_MONTH_MIN || t->month > MCP7940_MONTH_MAX) {
        return false;
    }

    uint8_t b[7];
    b[0] = (uint8_t)(mcp7940_bin_to_bcd(t->sec) | MCP7940_RTCSEC_ST);  /* enable osc, transcribed from `toBCD(iRTCSec) | 0x80` */
    b[1] = mcp7940_bin_to_bcd(t->min);
    b[2] = mcp7940_bin_to_bcd(t->hour);
    /* ---- Deviation 3: the original writes `iRTCWkDay | 0x8` as a whole
     * byte, clearing PWRFAIL blind, every time. Per the MCP7940N datasheet,
     * Register 5-4 (RTCWKDAY) Note 2, that clear cannot be avoided by
     * software at all -- "Writing to the RTCWKDAY register will always
     * clear the PWRFAIL bit" -- so a read-modify-write here would not
     * change the outcome even if this function did one (mcp7940_enable_osc()
     * below still clears it too, for the same datasheet reason, despite
     * being a read-modify-write). Writing a fresh time IS this driver's way
     * of saying "the old time is gone, PWRFAIL no longer applies", so
     * clearing it here is a deliberate, informed choice, not the original's
     * blind write silently inherited. See the header for the full
     * reasoning and the exact datasheet citation. ---- */
    b[3] = (uint8_t)(t->wkday | MCP7940_RTCWKDAY_VBATEN);
    b[4] = mcp7940_bin_to_bcd(t->date);
    b[5] = mcp7940_bin_to_bcd(t->month);
    b[6] = mcp7940_bin_to_bcd(t->year);

    if (!fwog_i2c_write_regs(I2C_ADDR_RTC, MCP7940_REG_RTCSEC, b, 7u)) return false;
    return fwog_i2c_write_reg(I2C_ADDR_RTC, MCP7940_REG_OSCTRIM, t->trim);
}

bool mcp7940_enable_osc(void) {
    /* ---- Deviation 1 (REAL BUG in the original, not carried forward): see
     * the header for the full explanation. enableOsc() (rpRTCmcp7940.cpp:
     * 59-70) writes RTCSEC and RTCWKDAY as whole bytes (0x80, 0x08), zeroing
     * the BCD seconds field and the weekday field along with setting
     * ST/VBATEN. This is a read-modify-write instead: each register is read
     * first and only the bit this function means to set is OR'd in. ---- */
    uint8_t sec;
    if (!fwog_i2c_read_regs(I2C_ADDR_RTC, MCP7940_REG_RTCSEC, &sec, 1u)) return false;
    sec = (uint8_t)(sec | MCP7940_RTCSEC_ST);
    if (!fwog_i2c_write_reg(I2C_ADDR_RTC, MCP7940_REG_RTCSEC, sec)) return false;

    uint8_t wkday;
    if (!fwog_i2c_read_regs(I2C_ADDR_RTC, MCP7940_REG_RTCWKDAY, &wkday, 1u)) return false;
    /* Deviation 3, corrected: this read-modify-write does NOT preserve
     * PWRFAIL, despite reading wkday first and touching only the VBATEN
     * bit of it. Per the MCP7940N datasheet, Register 5-4 (RTCWKDAY)
     * Note 2, "Writing to the RTCWKDAY register will always clear the
     * PWRFAIL bit" -- the clear is a side effect of the write transaction
     * itself, not of which bits changed, so PWRFAIL is cleared here exactly
     * as it is by mcp7940_write_time() and by the original's enableOsc().
     * There is no software workaround; see the header. Weekday (bits 2:0)
     * DOES survive untouched, since this function only ORs VBATEN into
     * whatever was read -- that part of the read-modify-write still holds.
     * OSCRUN (bit 5) is read-only on the real part and ignores writes
     * either way, so it is unaffected regardless. */
    wkday = (uint8_t)(wkday | MCP7940_RTCWKDAY_VBATEN);
    return fwog_i2c_write_reg(I2C_ADDR_RTC, MCP7940_REG_RTCWKDAY, wkday);
}

#endif
