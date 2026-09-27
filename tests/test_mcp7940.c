#include "test_util.h"
#include "sensors/mcp7940.h"

int main(void) {
    /* ---- mcp7940_bcd_to_bin(): transcribed from the inline arithmetic in
     * the original's readTime() (rpRTCmcp7940.cpp:29-37). One check per
     * field, using each field's own tens mask, at that field's real max. */
    ASSERT_EQ(mcp7940_bcd_to_bin(0x00u, MCP7940_TENS_MASK_SEC_MIN), 0u);
    ASSERT_EQ(mcp7940_bcd_to_bin(0x59u, MCP7940_TENS_MASK_SEC_MIN), 59u);   /* max sec/min */
    ASSERT_EQ(mcp7940_bcd_to_bin(0x23u, MCP7940_TENS_MASK_HOUR_DATE), 23u); /* max 24h hour */
    ASSERT_EQ(mcp7940_bcd_to_bin(0x31u, MCP7940_TENS_MASK_HOUR_DATE), 31u); /* max day of month */
    ASSERT_EQ(mcp7940_bcd_to_bin(0x12u, MCP7940_TENS_MASK_MONTH), 12u);     /* max month */
    ASSERT_EQ(mcp7940_bcd_to_bin(0x01u, MCP7940_TENS_MASK_MONTH), 1u);     /* min month */
    ASSERT_EQ(mcp7940_bcd_to_bin(0x99u, MCP7940_TENS_MASK_YEAR), 99u);      /* max year */
    ASSERT_EQ(mcp7940_bcd_to_bin(0x00u, MCP7940_TENS_MASK_YEAR), 0u);       /* min year */
    /* ST (bit 7) sharing the SEC register must not leak into the seconds
     * decode -- readTime() masks it out via MCP7940_TENS_MASK_SEC_MIN
     * (0x70), which excludes bit 7. */
    ASSERT_EQ(mcp7940_bcd_to_bin(0x80u | 0x45u, MCP7940_TENS_MASK_SEC_MIN), 45u);

    /* ---- mcp7940_bin_to_bcd(): reimplemented with /10,%10 in place of the
     * original's decrement loop (rpRTCmcp7940.cpp:72-96); same result. ---- */
    ASSERT_EQ(mcp7940_bin_to_bcd(0u), 0x00u);
    ASSERT_EQ(mcp7940_bin_to_bcd(9u), 0x09u);
    ASSERT_EQ(mcp7940_bin_to_bcd(10u), 0x10u);
    ASSERT_EQ(mcp7940_bin_to_bcd(59u), 0x59u);
    ASSERT_EQ(mcp7940_bin_to_bcd(99u), 0x99u);

    /* ---- Round trip across the full 0-99 range every field this driver
     * writes can hold (year is the widest -- its tens mask covers the whole
     * nibble, so this is the only mask that can validate every value
     * without a field-specific ceiling getting in the way). ---- */
    for (unsigned v = 0; v <= 99u; v++) {
        uint8_t bcd = mcp7940_bin_to_bcd((uint8_t)v);
        ASSERT_EQ(mcp7940_bcd_to_bin(bcd, MCP7940_TENS_MASK_YEAR), v);
    }

    /* ---- mcp7940_bcd_valid(): the validation addition, not in the
     * original (see the header). A nibble > 9 is not a valid BCD digit. ---- */
    ASSERT_TRUE(mcp7940_bcd_valid(0x59u, MCP7940_TENS_MASK_SEC_MIN) == true);
    ASSERT_TRUE(mcp7940_bcd_valid(0x00u, MCP7940_TENS_MASK_SEC_MIN) == true);
    /* invalid ones nibble (0xA is not a BCD digit), independent of which
     * tens mask is in play */
    ASSERT_TRUE(mcp7940_bcd_valid(0x5Au, MCP7940_TENS_MASK_SEC_MIN) == false);
    ASSERT_TRUE(mcp7940_bcd_valid(0x1Au, MCP7940_TENS_MASK_MONTH) == false);
    /* invalid tens nibble -- only reachable through the YEAR mask, since
     * every narrower field mask caps the tens digit at 7 or below on its
     * own (SEC_MIN's 3 bits max out at 7, HOUR_DATE's 2 bits at 3, MONTH's
     * 1 bit at 1) */
    ASSERT_TRUE(mcp7940_bcd_valid(0xA0u, MCP7940_TENS_MASK_YEAR) == false);
    ASSERT_TRUE(mcp7940_bcd_valid(0xF9u, MCP7940_TENS_MASK_YEAR) == false);
    ASSERT_TRUE(mcp7940_bcd_valid(0x99u, MCP7940_TENS_MASK_YEAR) == true);   /* max valid year */

    /* The exact motivating case from the header/spec: a month byte of 0x99
     * is BCD-VALID (both nibbles are 9, neither exceeds 9) but decodes to
     * month 19 -- an impossible month. bcd_valid() alone cannot catch this;
     * it is why mcp7940_read_time() layers a separate range check on top
     * (that check lives on the I2C-bound side and isn't host-testable here,
     * but the pure half of the story -- "valid BCD, invalid month" -- is). */
    ASSERT_TRUE(mcp7940_bcd_valid(0x99u, MCP7940_TENS_MASK_MONTH) == true);
    ASSERT_EQ(mcp7940_bcd_to_bin(0x99u, MCP7940_TENS_MASK_MONTH), 19u);

    /* ---- mcp7940_is_configured() / mcp7940_is_oscillating(): pure
     * predicates over an already-populated snapshot (hazard 2 -- ST alone
     * is "requested", OSCRUN is "confirmed running"). ---- */
    mcp7940_time_t t = {0};
    t.osc_started = false; t.osc_running = false; t.vbat_enabled = false;
    ASSERT_TRUE(mcp7940_is_configured(&t) == false);
    ASSERT_TRUE(mcp7940_is_oscillating(&t) == false);

    t.osc_started = true; t.vbat_enabled = true; t.osc_running = false;
    /* Ported behaviour: isConfigured() is true from ST+VBATEN alone, even
     * though the crystal has not actually confirmed it is running yet --
     * this is exactly the gap hazard 2 describes. */
    ASSERT_TRUE(mcp7940_is_configured(&t) == true);
    ASSERT_TRUE(mcp7940_is_oscillating(&t) == false);

    t.osc_running = true;
    ASSERT_TRUE(mcp7940_is_oscillating(&t) == true);

    t.osc_started = true; t.vbat_enabled = false;
    /* VBATEN missing must still fail is_configured(), matching the
     * original's `&&`. */
    ASSERT_TRUE(mcp7940_is_configured(&t) == false);

    TEST_RETURN();
}
