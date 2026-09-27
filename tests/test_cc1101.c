#include "test_util.h"
#include "radio/cc1101.h"

int main(void) {
    /* ---- cc1101_decode_status(): datasheet Table 23, bit7=CHIP_RDYn,
     * bits6:4=STATE, bits3:0=FIFO_BYTES_AVAILABLE. Boundaries: all bits
     * clear, all bits set, and each field isolated so a port that shifts
     * the wrong nibble fails loudly. ---- */
    {
        cc1101_status_t st = cc1101_decode_status(0x00u);
        ASSERT_TRUE(st.chip_rdy == false);
        ASSERT_EQ(st.state, CC1101_STATE_IDLE);
        ASSERT_EQ(st.fifo_bytes, 0u);

        st = cc1101_decode_status(0xFFu);
        ASSERT_TRUE(st.chip_rdy == true);
        ASSERT_EQ(st.state, 7u);            /* TXFIFO_UNDERFLOW */
        ASSERT_EQ(st.fifo_bytes, 15u);

        /* CHIP_RDYn clear, RX state, 3 bytes available -- 0x13 */
        st = cc1101_decode_status(0x13u);
        ASSERT_TRUE(st.chip_rdy == false);
        ASSERT_EQ(st.state, CC1101_STATE_RX);
        ASSERT_EQ(st.fifo_bytes, 3u);
    }

    /* ---- cc1101_freq_in_band(): rpCC1101::validFrequency(). Every band
     * edge, plus the two gaps between bands and both extremes outside all
     * three. ---- */
    {
        ASSERT_TRUE(cc1101_freq_in_band(299999999u) == false);
        ASSERT_TRUE(cc1101_freq_in_band(300000000u) == true);
        ASSERT_TRUE(cc1101_freq_in_band(348000000u) == true);
        ASSERT_TRUE(cc1101_freq_in_band(348000001u) == false);
        ASSERT_TRUE(cc1101_freq_in_band(386999999u) == false);
        ASSERT_TRUE(cc1101_freq_in_band(387000000u) == true);
        ASSERT_TRUE(cc1101_freq_in_band(464000000u) == true);
        ASSERT_TRUE(cc1101_freq_in_band(464000001u) == false);
        ASSERT_TRUE(cc1101_freq_in_band(778999999u) == false);
        ASSERT_TRUE(cc1101_freq_in_band(779000000u) == true);
        ASSERT_TRUE(cc1101_freq_in_band(928000000u) == true);
        ASSERT_TRUE(cc1101_freq_in_band(928000001u) == false);
        ASSERT_TRUE(cc1101_freq_in_band(0u) == false);
    }

    /* ---- cc1101_freq_word(): fcarrier = (fXOSC/2^16)*FREQ. Reference
     * values computed independently from the closed form (not from the
     * C code under test) at CC1101_CRYSTAL_HZ = 26 MHz: band edges plus
     * 433.92 MHz, the well-known CC1101 default carrier (this triple --
     * 0x10/0xB0/0x71 -- appears in many published CC1101 register
     * dumps, an independent cross-check). ---- */
    {
        uint8_t f2, f1, f0;
        cc1101_freq_word(300000000u, CC1101_CRYSTAL_HZ, &f2, &f1, &f0);
        ASSERT_EQ(f2, 0x0Bu); ASSERT_EQ(f1, 0x89u); ASSERT_EQ(f0, 0xD8u);

        cc1101_freq_word(433920000u, CC1101_CRYSTAL_HZ, &f2, &f1, &f0);
        ASSERT_EQ(f2, 0x10u); ASSERT_EQ(f1, 0xB0u); ASSERT_EQ(f0, 0x71u);

        cc1101_freq_word(915000000u, CC1101_CRYSTAL_HZ, &f2, &f1, &f0);
        ASSERT_EQ(f2, 0x23u); ASSERT_EQ(f1, 0x31u); ASSERT_EQ(f0, 0x3Bu);

        cc1101_freq_word(928000000u, CC1101_CRYSTAL_HZ, &f2, &f1, &f0);
        ASSERT_EQ(f2, 0x23u); ASSERT_EQ(f1, 0xB1u); ASSERT_EQ(f0, 0x3Bu);

        /* NULL-safe: a caller who only wants one byte must not crash. */
        cc1101_freq_word(433920000u, CC1101_CRYSTAL_HZ, NULL, NULL, &f0);
        ASSERT_EQ(f0, 0x71u);
    }

    /* ---- cc1101_rssi_dbm(): datasheet 17.3. See the header for why the
     * reference's own `if (raw_rssi >= 128)` branch is dead and only the
     * `else` ever executes -- these values are what that surviving branch
     * actually computes for the full range of a signed byte. ---- */
    {
        ASSERT_EQ(cc1101_rssi_dbm((int8_t)0x00), -74);
        ASSERT_EQ(cc1101_rssi_dbm((int8_t)0x7Fu), -11);   /* +127 -> -11 */
        ASSERT_EQ((int)cc1101_rssi_dbm((int8_t)0x80u), -138); /* -128 -> -138 */
        ASSERT_EQ(cc1101_rssi_dbm((int8_t)0xFFu), -74);   /* -1   -> -74 */
        ASSERT_EQ((int)cc1101_rssi_dbm((int8_t)0xFEu), -75);  /* -2   -> -75 */
    }

    /* ---- cc1101_lqi_value()/cc1101_lqi_crc_ok(): datasheet 0x33 register
     * map, bit7=CRC_OK, bits6:0=LQI_EST -- the fixed reference bug (see the
     * header): no shift. Boundary bytes exercise the exact bits the
     * reference's `>>1` mistake would have corrupted. ---- */
    {
        ASSERT_EQ(cc1101_lqi_value(0x00u), 0u);
        ASSERT_TRUE(cc1101_lqi_crc_ok(0x00u) == false);

        ASSERT_EQ(cc1101_lqi_value(0xFFu), 0x7Fu);
        ASSERT_TRUE(cc1101_lqi_crc_ok(0xFFu) == true);

        /* CRC bit set, LQI value's own bit 0 set: 0x81 = 1000_0001.
         * The buggy `(reg>>1)&0x7F` would give 0x40 (folding CRC_OK in and
         * dropping bit 0); the fixed mask gives 0x01. */
        ASSERT_EQ(cc1101_lqi_value(0x81u), 0x01u);
        ASSERT_TRUE(cc1101_lqi_crc_ok(0x81u) == true);

        /* CRC bit clear, LQI value all ones: 0x7F. */
        ASSERT_EQ(cc1101_lqi_value(0x7Fu), 0x7Fu);
        ASSERT_TRUE(cc1101_lqi_crc_ok(0x7Fu) == false);
    }

    /* ---- cc1101_reg_merge(): the read-modify-write primitive every
     * compound-register setter uses. ---- */
    {
        ASSERT_EQ(cc1101_reg_merge(0xFFu, 0x0Fu, 0x00u), 0xF0u);
        ASSERT_EQ(cc1101_reg_merge(0x00u, 0x0Fu, 0xFFu), 0x0Fu);
        ASSERT_EQ(cc1101_reg_merge(0xA5u, 0x70u, 0x30u), 0xB5u); /* MOD_FORMAT=ASK on top of 0xA5 */
    }

    /* ---- cc1101_drate_encode(): mantissa/exponent search, ELECHOUSE::
     * setDRate(). Round trip at the low clamp (exact zero mantissa), a
     * known mid-range value (115.051 kBaud, the datasheet's own quoted
     * default), and the documented boundary quirk at the exact top of the
     * clamp range (see the header: this is a found-and-flagged reference
     * defect, not something this test is pretending is fine). ---- */
    {
        uint8_t m;
        ASSERT_EQ(cc1101_drate_encode(0.0247955f, &m), 0u);
        ASSERT_EQ(m, 0u);

        ASSERT_EQ(cc1101_drate_encode(250.0f, &m), 13u);
        ASSERT_EQ(m, 59u);

        /* the exact clamp maximum: returns 16, one past the 4-bit field --
         * see the header's NOTE. This assertion is the regression pin for
         * that documented quirk, not a claim that 16 is "correct". */
        ASSERT_EQ(cc1101_drate_encode(1621.83f, &m), 16u);

        /* clamped from above/below: an absurd input must not diverge from
         * the clamped boundary's own encoding. */
        ASSERT_EQ(cc1101_drate_encode(999999.0f, &m), cc1101_drate_encode(1621.83f, NULL));
        ASSERT_EQ(cc1101_drate_encode(0.0f, &m), cc1101_drate_encode(0.0247955f, NULL));
    }

    /* ---- cc1101_deviation_encode(): ELECHOUSE::setDeviation(). Both
     * clamp boundaries. NOTE: the low clamp gives 1, not 0 (the loop body's
     * trailing `c++` always runs, even on the same iteration the `f>=d`
     * branch fires -- there is no early exit in the reference), and the
     * high clamp gives 0x80, ONE PAST DEVIATN's valid 0x00-0x77 range
     * (DEVIATION_E and DEVIATION_M are each 3 bits; 0x80 sets bit 7, which
     * the datasheet marks "Not used"). Same class of boundary quirk as
     * cc1101_drate_encode()'s documented 1621.83 -> 16 case. Pinned here
     * faithfully; cc1101_set_deviation() masks bit 7 off before writing --
     * see its own comment in cc1101.c. ---- */
    {
        ASSERT_EQ(cc1101_deviation_encode(1.586914f), 0x01u);
        ASSERT_EQ(cc1101_deviation_encode(380.859375f), 0x80u);
        /* clamped: an absurd input matches the boundary's own encoding */
        ASSERT_EQ(cc1101_deviation_encode(0.0f), cc1101_deviation_encode(1.586914f));
        ASSERT_EQ(cc1101_deviation_encode(99999.0f), cc1101_deviation_encode(380.859375f));
    }

    /* ---- cc1101_rxbw_encode(): ELECHOUSE::setRxBW(). Values chosen so
     * both inner loops (the /2 ladder and the /1.25 ladder) each run a
     * different number of times, including the exact non-strict boundary
     * (101.5625 itself does NOT satisfy `> 101.5625`, so the first ladder
     * must not fire on it -- transcribed strictly, not "corrected" to
     * >=). ---- */
    {
        ASSERT_EQ(cc1101_rxbw_encode(812.5f), 0x00u);
        ASSERT_EQ(cc1101_rxbw_encode(203.0f), 0x80u);
        ASSERT_EQ(cc1101_rxbw_encode(101.5625f), 0xC0u);
        ASSERT_EQ(cc1101_rxbw_encode(58.0f), 0xF0u);
    }

    /* ---- cc1101_chsp_encode(): ELECHOUSE::setChsp(). Both clamp
     * boundaries and a mid-range value. ---- */
    {
        uint8_t m;
        ASSERT_EQ(cc1101_chsp_encode(25.390625f, &m), 0u);
        ASSERT_EQ(m, 0u);
        ASSERT_EQ(cc1101_chsp_encode(405.456543f, &m), 3u);
        ASSERT_EQ(m, 255u);
        ASSERT_EQ(cc1101_chsp_encode(199.951f, &m), 2u);
        ASSERT_EQ(m, 248u);
    }

    /* ---- cc1101_patable_lookup(): ELECHOUSE::setPA()'s band dispatch and
     * threshold ladders. One case per band, plus the default 433.92
     * MHz/12 dBm bring-up combination (falls past every named threshold in
     * band 2, into the final else), plus outside-all-bands. ---- */
    {
        int band;
        ASSERT_EQ(cc1101_patable_lookup(315.0f, -30, &band), 0x12u);
        ASSERT_EQ(band, 1);
        ASSERT_EQ(cc1101_patable_lookup(433.92f, 0, &band), 0x60u);
        ASSERT_EQ(band, 2);
        ASSERT_EQ(cc1101_patable_lookup(868.0f, 10, &band), 0xC5u);
        ASSERT_EQ(band, 3);
        ASSERT_EQ(cc1101_patable_lookup(915.0f, -6, &band), 0x38u);
        ASSERT_EQ(band, 4);

        /* the bring-up default: 433.92 MHz, 12 dBm -- past every named
         * threshold in band 2, into the unconditional else (0xC0). */
        ASSERT_EQ(cc1101_patable_lookup(433.92f, 12, &band), 0xC0u);
        ASSERT_EQ(band, 2);

        /* outside all four bands: band 0, entry 0 -- caller must not write
         * PATABLE on this result. */
        ASSERT_EQ(cc1101_patable_lookup(200.0f, 0, &band), 0u);
        ASSERT_EQ(band, 0);
    }

    /* ---- cc1101_patable_build(): ASK/OOK places the level at index 1
     * (index 0 is the OOK "off" symbol); every other modulation uses index
     * 0. ---- */
    {
        uint8_t table[8];
        cc1101_patable_build(0xC2u, true, table);
        ASSERT_EQ(table[0], 0u);
        ASSERT_EQ(table[1], 0xC2u);
        for (int i = 2; i < 8; i++) ASSERT_EQ(table[i], 0u);

        cc1101_patable_build(0xC2u, false, table);
        ASSERT_EQ(table[0], 0xC2u);
        ASSERT_EQ(table[1], 0u);
    }

    /* ---- cc1101_map(): the reference's bare `map()`, transcribed. ---- */
    {
        ASSERT_EQ(cc1101_map(300, 300, 348, 24, 28), 24);
        ASSERT_EQ(cc1101_map(348, 300, 348, 24, 28), 28);
        ASSERT_EQ(cc1101_map(433, 378, 464, 31, 38), 35);
    }

    /* ---- cc1101_calib_lookup(): ELECHOUSE::Calibrate()'s band dispatch.
     * One case per band including each band's own TEST0 threshold edge,
     * plus outside all bands. ---- */
    {
        uint8_t fsctrl0, test0;
        int band;

        cc1101_calib_lookup(300.0f, &fsctrl0, &test0, &band);
        ASSERT_EQ(band, 1); ASSERT_EQ(fsctrl0, 24u); ASSERT_EQ(test0, 0x0Bu);

        /* band 1's TEST0 threshold: just below vs at/above 322.88 MHz */
        cc1101_calib_lookup(322.0f, &fsctrl0, &test0, &band);
        ASSERT_EQ(test0, 0x0Bu);
        cc1101_calib_lookup(322.88f, &fsctrl0, &test0, &band);
        ASSERT_EQ(test0, 0x09u);

        cc1101_calib_lookup(433.92f, &fsctrl0, &test0, &band);
        ASSERT_EQ(band, 2); ASSERT_EQ(fsctrl0, 35u); ASSERT_EQ(test0, 0x09u);

        cc1101_calib_lookup(868.0f, &fsctrl0, &test0, &band);
        ASSERT_EQ(band, 3); ASSERT_EQ(fsctrl0, 73u); ASSERT_EQ(test0, 0x09u);

        /* band 4 always writes TEST0=0x09, no threshold at all. */
        cc1101_calib_lookup(900.0f, &fsctrl0, &test0, &band);
        ASSERT_EQ(band, 4); ASSERT_EQ(fsctrl0, 77u); ASSERT_EQ(test0, 0x09u);
        cc1101_calib_lookup(928.0f, &fsctrl0, &test0, &band);
        ASSERT_EQ(band, 4); ASSERT_EQ(fsctrl0, 79u); ASSERT_EQ(test0, 0x09u);

        cc1101_calib_lookup(200.0f, &fsctrl0, &test0, &band);
        ASSERT_EQ(band, 0);

        /* NULL-safe outputs */
        cc1101_calib_lookup(433.92f, NULL, NULL, NULL);
    }

    TEST_RETURN();
}
