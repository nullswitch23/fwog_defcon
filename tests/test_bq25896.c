#include "test_util.h"
#include "power/bq25896.h"
#include <math.h>

static int close_enough(float a, float b, float eps) {
    float d = a - b;
    if (d < 0) d = -d;
    return d < eps;
}

int main(void) {
    /* ---- status register decode (VBUS_STAT, 0x0B) ----
       Transcribed from takeVBUSSTAT(): bit7 OTG, else bit6 ADAPTER, else
       bit5 USB_HOST, else NO_INPUT; bit4+bit3 select the charge state;
       bit0 selects the VSYSMIN flag. */
    ASSERT_EQ(bq25896_decode_stat_reg(0x80u).vbus_stat, BQ25896_VBUS_OTG);
    ASSERT_EQ(bq25896_decode_stat_reg(0x40u).vbus_stat, BQ25896_VBUS_ADAPTER);
    ASSERT_EQ(bq25896_decode_stat_reg(0x20u).vbus_stat, BQ25896_VBUS_USB_HOST);
    ASSERT_EQ(bq25896_decode_stat_reg(0x00u).vbus_stat, BQ25896_VBUS_NO_INPUT);
    /* bit7 wins over bit6/5 if more than one happens to be set */
    ASSERT_EQ(bq25896_decode_stat_reg(0xE0u).vbus_stat, BQ25896_VBUS_OTG);

    ASSERT_EQ(bq25896_decode_stat_reg(0x00u).chg_stat, BQ25896_CHG_NOT_CHARGING);
    ASSERT_EQ(bq25896_decode_stat_reg(0x08u).chg_stat, BQ25896_CHG_PRE_CHARGE);
    ASSERT_EQ(bq25896_decode_stat_reg(0x10u).chg_stat, BQ25896_CHG_FAST_CHARGE);
    ASSERT_EQ(bq25896_decode_stat_reg(0x18u).chg_stat, BQ25896_CHG_DONE);

    ASSERT_EQ(bq25896_decode_stat_reg(0x00u).vsys_stat, BQ25896_VSYS_NOT_IN_VSYSMIN);
    ASSERT_EQ(bq25896_decode_stat_reg(0x01u).vsys_stat, BQ25896_VSYS_IN_VSYSMIN);

    ASSERT_TRUE(bq25896_is_charging(BQ25896_CHG_NOT_CHARGING) == false);
    ASSERT_TRUE(bq25896_is_charging(BQ25896_CHG_PRE_CHARGE)   == true);
    ASSERT_TRUE(bq25896_is_charging(BQ25896_CHG_FAST_CHARGE)  == true);
    ASSERT_TRUE(bq25896_is_charging(BQ25896_CHG_DONE)         == true);
    ASSERT_TRUE(bq25896_is_charging_done(BQ25896_CHG_DONE)        == true);
    ASSERT_TRUE(bq25896_is_charging_done(BQ25896_CHG_FAST_CHARGE) == false);

    /* ---- fault register decode (FAULT, 0x0C) ----
       Transcribed from takeFaultData(): bit2 selects hot/cold vs
       warm/cool/normal, then bit1 (and bit0 within the warm/cool branch)
       pick the specific rank; bit5 selects thermal-shutdown/timer-expired
       vs normal/input-fault, then bit4 picks the specific fault. */
    ASSERT_EQ(bq25896_decode_fault_reg(0x00u).ts_rank, BQ25896_TS_NORMAL);
    ASSERT_EQ(bq25896_decode_fault_reg(0x02u).ts_rank, BQ25896_TS_WARM);
    ASSERT_EQ(bq25896_decode_fault_reg(0x03u).ts_rank, BQ25896_TS_COOL);
    ASSERT_EQ(bq25896_decode_fault_reg(0x04u).ts_rank, BQ25896_TS_COLD);
    ASSERT_EQ(bq25896_decode_fault_reg(0x06u).ts_rank, BQ25896_TS_HOT);

    ASSERT_EQ(bq25896_decode_fault_reg(0x00u).chg_fault, BQ25896_FAULT_NORMAL);
    ASSERT_EQ(bq25896_decode_fault_reg(0x10u).chg_fault, BQ25896_FAULT_INPUT);
    ASSERT_EQ(bq25896_decode_fault_reg(0x20u).chg_fault, BQ25896_FAULT_THERMAL_SHUTDOWN);
    ASSERT_EQ(bq25896_decode_fault_reg(0x30u).chg_fault, BQ25896_FAULT_TIMER_EXPIRED);

    /* ---- ADC register decode: BATV (0x0E), SYSV (0x0F), VBUSV (0x11),
       ICHGR (0x12), TSPCT (0x10). Offsets/steps/masking transcribed from
       takeVBATData/takeVSYSData/takeVBUSData/takeICHGData/takeTSPCTData. */
    ASSERT_EQ(bq25896_decode_batv(0x00u).vbat_mv, 2304u);
    ASSERT_TRUE(bq25896_decode_batv(0x00u).thermal_regulation == false);
    ASSERT_EQ(bq25896_decode_batv(0x0Au).vbat_mv, 2504u);          /* 10*20+2304 */
    ASSERT_EQ(bq25896_decode_batv(0xFFu).vbat_mv, 4844u);          /* (0xFF&0x7F)=127; 127*20+2304 */
    ASSERT_TRUE(bq25896_decode_batv(0xFFu).thermal_regulation == true);

    ASSERT_EQ(bq25896_decode_vsys_mv(0x00u), 2304u);
    ASSERT_EQ(bq25896_decode_vsys_mv(0xFFu), 7404u);               /* full byte, no mask: 255*20+2304 */

    ASSERT_EQ(bq25896_decode_vbusv(0x00u).vbus_mv, 2600u);
    ASSERT_TRUE(bq25896_decode_vbusv(0x00u).vbus_attached == false);
    ASSERT_EQ(bq25896_decode_vbusv(0x80u).vbus_mv, 2600u);
    ASSERT_TRUE(bq25896_decode_vbusv(0x80u).vbus_attached == true);
    ASSERT_EQ(bq25896_decode_vbusv(0xFFu).vbus_mv, 15300u);        /* (0xFF&0x7F)=127; 127*100+2600 */

    ASSERT_EQ(bq25896_decode_ichg_actual_ma(0x00u), 0u);
    ASSERT_EQ(bq25896_decode_ichg_actual_ma(200u), 10000u);        /* full byte, no mask: 200*50 */

    ASSERT_TRUE(close_enough(bq25896_decode_tspct_percent(0u), 21.0f, 0.001f));
    ASSERT_TRUE(close_enough(bq25896_decode_tspct_percent(100u), 67.5f, 0.001f));

    /* Temperature chain (RtoTemp via the VTS/RP/NTC divider). Expected
       values cross-checked with an independent double-precision
       computation of the same formula; the float path here loses a little
       precision, hence the wider epsilon. */
    ASSERT_TRUE(close_enough(bq25896_ntc_temperature_c(21.0f), 75.61f, 0.05f));
    ASSERT_TRUE(close_enough(bq25896_ntc_temperature_c(67.5f), 13.42f, 0.05f));

    /* Domain boundary: the VTS/RP/NTC divider chain runs outside its own
       valid domain once tspct_percent gets high enough that the NTC branch
       resistance would exceed the fixed 30100-ohm leg (see the function's
       comment). This is inside TSPCT's real ~21-140.6% output range, so a
       caller reading a live register must be able to detect it -- confirm
       both that a normal in-range reading stays a real number and that a
       past-domain one is deliberately NaN, not whatever logf() of a
       negative argument happens to produce. */
    ASSERT_TRUE(!isnan(bq25896_ntc_temperature_c(80.0f)));
    ASSERT_TRUE(isnan(bq25896_ntc_temperature_c(90.0f)));
    ASSERT_TRUE(isnan(bq25896_ntc_temperature_c(100.0f)));
    ASSERT_TRUE(isnan(bq25896_ntc_temperature_c(140.0f)));

    /* ---- input current limit (ILIM, 0x00): CORRECTED encoding ----
       See bq25896_encode_input_current_limit()'s comment for the two bugs
       in the original this deliberately does not reproduce. Offset 100 mA,
       step 50 mA, range 100-3250 mA (0-63 as a 6-bit code); bits 6/7
       (EN_HIZ/EN_ILIM) are not part of the value. */
    ASSERT_EQ(bq25896_encode_input_current_limit(100u), 0u);
    ASSERT_EQ(bq25896_decode_input_current_limit_ma(0u), 100u);
    ASSERT_EQ(bq25896_encode_input_current_limit(3250u), 63u);
    ASSERT_EQ(bq25896_decode_input_current_limit_ma(63u), 3250u);
    /* below minimum clamps up */
    ASSERT_EQ(bq25896_encode_input_current_limit(0u), 0u);
    ASSERT_EQ(bq25896_encode_input_current_limit(99u), 0u);
    /* above maximum clamps down */
    ASSERT_EQ(bq25896_encode_input_current_limit(3251u), 63u);
    ASSERT_EQ(bq25896_encode_input_current_limit(65535u), 63u);
    /* mid-step value truncates toward the lower step, not rounds */
    ASSERT_EQ(bq25896_encode_input_current_limit(149u), 0u);
    ASSERT_EQ(bq25896_encode_input_current_limit(150u), 1u);
    /* decode ignores bits 6/7 -- a full 0xFF register still reads back the
       maximum current, not garbage */
    ASSERT_EQ(bq25896_decode_input_current_limit_ma(0xFFu), 3250u);

    /* ---- fast charge current limit (ICHG, 0x04) ----
       Offset 0, step 64 mA, clamped to 3008 mA (the field itself covers up
       to 8128 mA -- a deliberate lower ceiling from the original, not a
       bug: see the encoder's comment). */
    ASSERT_EQ(bq25896_encode_ichg_limit(0u), 0u);
    ASSERT_EQ(bq25896_decode_ichg_limit_ma(0u), 0u);
    ASSERT_EQ(bq25896_encode_ichg_limit(3008u), 47u);
    ASSERT_EQ(bq25896_decode_ichg_limit_ma(47u), 3008u);
    ASSERT_EQ(bq25896_encode_ichg_limit(3009u), 47u);       /* one beyond the ceiling clamps */
    ASSERT_EQ(bq25896_encode_ichg_limit(8128u), 47u);       /* the field's own max still clamps to the ceiling */
    /* mid-step value truncates toward the lower step, not rounds */
    ASSERT_EQ(bq25896_encode_ichg_limit(63u), 0u);
    /* bit 7 must not leak into the decoded value */
    ASSERT_EQ(bq25896_decode_ichg_limit_ma(0x80u | 47u), 3008u);
    /* the field's own maximum (127 * 64 mA = 8128 mA) is unreachable by the
       encoder's 3008 mA safety ceiling, so nothing above pins the decoder's
       upper end on its own -- do that here directly against the raw code. */
    ASSERT_EQ(bq25896_decode_ichg_limit_ma(0x7Fu), 8128u);
    /* round-trip sweep: every code the encoder can produce must decode
       back to a value that re-encodes to the same code (exact equality
       isn't expected in mA, since multiple mA values map to one code, but
       the code itself must be stable under encode(decode(code))). */
    for (unsigned code = 0; code <= 47u; code++) {
        uint16_t ma = bq25896_decode_ichg_limit_ma((uint8_t)code);
        ASSERT_EQ(bq25896_encode_ichg_limit(ma), code);
    }

    /* ---- precharge / termination current limits (IPRE_ITERM, 0x05) ----
       Same offset(64)/step(64)/range(64-1024 mA) formula for both fields;
       they differ only by nibble (precharge in bits 7:4, termination in
       bits 3:0). */
    ASSERT_EQ(bq25896_encode_precharge_limit(64u), 0u);
    ASSERT_EQ(bq25896_decode_precharge_limit_ma(0x00u), 64u);
    ASSERT_EQ(bq25896_encode_precharge_limit(1024u), 15u);
    ASSERT_EQ(bq25896_decode_precharge_limit_ma(0xF0u), 1024u);
    ASSERT_EQ(bq25896_encode_precharge_limit(0u), 0u);      /* below minimum clamps up */
    ASSERT_EQ(bq25896_encode_precharge_limit(1025u), 15u);  /* above maximum clamps down */
    ASSERT_EQ(bq25896_encode_precharge_limit(65u), 0u);     /* mid-step truncates, does not round */

    ASSERT_EQ(bq25896_encode_termination_limit(64u), 0u);
    ASSERT_EQ(bq25896_decode_termination_limit_ma(0x00u), 64u);
    ASSERT_EQ(bq25896_encode_termination_limit(1024u), 15u);
    ASSERT_EQ(bq25896_decode_termination_limit_ma(0x0Fu), 1024u);
    /* the two fields share one byte and must not bleed into each other */
    ASSERT_EQ(bq25896_decode_precharge_limit_ma(0x0Fu), 64u);     /* low nibble only affects termination */
    ASSERT_EQ(bq25896_decode_termination_limit_ma(0xF0u), 64u);   /* high nibble only affects precharge */

    /* ---- charge voltage limit (VREG, 0x06) ----
       Offset 3840 mV, step 16 mV, clamped to 4608 mV (the 6-bit field
       covers up to 4848 mV -- again a deliberate ceiling, not a bug). */
    ASSERT_EQ(bq25896_encode_charge_voltage(3840u), 0u);
    ASSERT_EQ(bq25896_decode_charge_voltage_mv(0x00u), 3840u);
    ASSERT_EQ(bq25896_encode_charge_voltage(4608u), 48u);
    ASSERT_EQ(bq25896_decode_charge_voltage_mv((uint8_t)(48u << 2)), 4608u);
    ASSERT_EQ(bq25896_encode_charge_voltage(3839u), 0u);    /* below minimum clamps up */
    ASSERT_EQ(bq25896_encode_charge_voltage(4609u), 48u);   /* above maximum clamps down */
    ASSERT_EQ(bq25896_encode_charge_voltage(3855u), 0u);    /* mid-step truncates, does not round */
    /* the two reserved low bits must not perturb the decode */
    ASSERT_EQ(bq25896_decode_charge_voltage_mv((uint8_t)((48u << 2) | 0x03u)), 4608u);
    /* round-trip sweep over every code the encoder can produce */
    for (unsigned code = 0; code <= 48u; code++) {
        uint16_t mv = bq25896_decode_charge_voltage_mv((uint8_t)(code << 2));
        ASSERT_EQ(bq25896_encode_charge_voltage(mv), code);
    }

    /* ---- minimum VBUS / input voltage limit (VINDPM, 0x0D) ----
       Offset 2600 mV, step 100 mV; at or below the base the original
       leaves the code at 0 rather than going negative. The upper clamp
       (127 steps above the base = 15300 mV) is this port's addition -- the
       original has none and would cast an overflowing float straight to a
       byte; see the encoder's comment. */
    ASSERT_EQ(bq25896_encode_vindpm(2600u), 0u);
    ASSERT_EQ(bq25896_decode_vindpm_mv(0x00u), 2600u);
    ASSERT_EQ(bq25896_encode_vindpm(2000u), 0u);            /* at/below base clamps to 0 */
    ASSERT_EQ(bq25896_encode_vindpm(2601u), 0u);            /* one mV above base: not yet a full step */
    ASSERT_EQ(bq25896_encode_vindpm(2699u), 0u);            /* mid-step truncates, does not round */
    ASSERT_EQ(bq25896_encode_vindpm(2700u), 1u);
    ASSERT_EQ(bq25896_encode_vindpm(15300u), 127u);
    ASSERT_EQ(bq25896_decode_vindpm_mv(0x7Fu), 15300u);
    ASSERT_EQ(bq25896_encode_vindpm(15301u), 127u);         /* beyond the field's own max clamps */
    ASSERT_EQ(bq25896_encode_vindpm(65535u), 127u);
    /* bit 7 (reserved/other-field in the original) must not leak into decode */
    ASSERT_EQ(bq25896_decode_vindpm_mv(0xFFu), 15300u);

    TEST_RETURN();
}
