#include "power/bq25896.h"
#include <math.h>   /* logf() -- bq25896_ntc_temperature_c() is pure and used
                       from host tests too, so this must not live inside the
                       #ifndef HOST_TEST block below with the I2C half. */

/* ---- Pure decode: status/fault byte -> enums ----
 * Transcribed bit-for-bit from takeVBUSSTAT()/takeFaultData() in
 * rpBatteryChargeBQ25896.cpp. */

bq25896_stat_t bq25896_decode_stat_reg(uint8_t v) {
    bq25896_stat_t s;
    if (v & 0x80u)      s.vbus_stat = BQ25896_VBUS_OTG;
    else if (v & 0x40u) s.vbus_stat = BQ25896_VBUS_ADAPTER;
    else if (v & 0x20u) s.vbus_stat = BQ25896_VBUS_USB_HOST;
    else                s.vbus_stat = BQ25896_VBUS_NO_INPUT;

    if (v & 0x10u) {
        s.chg_stat = (v & 0x08u) ? BQ25896_CHG_DONE : BQ25896_CHG_FAST_CHARGE;
    } else {
        s.chg_stat = (v & 0x08u) ? BQ25896_CHG_PRE_CHARGE : BQ25896_CHG_NOT_CHARGING;
    }

    s.vsys_stat = (v & 0x01u) ? BQ25896_VSYS_IN_VSYSMIN : BQ25896_VSYS_NOT_IN_VSYSMIN;
    return s;
}

bq25896_fault_t bq25896_decode_fault_reg(uint8_t v) {
    bq25896_fault_t f;
    if (v & 0x04u) {
        f.ts_rank = (v & 0x02u) ? BQ25896_TS_HOT : BQ25896_TS_COLD;
    } else if (v & 0x02u) {
        f.ts_rank = (v & 0x01u) ? BQ25896_TS_COOL : BQ25896_TS_WARM;
    } else {
        f.ts_rank = BQ25896_TS_NORMAL;
    }

    if (v & 0x20u) {
        f.chg_fault = (v & 0x10u) ? BQ25896_FAULT_TIMER_EXPIRED : BQ25896_FAULT_THERMAL_SHUTDOWN;
    } else {
        f.chg_fault = (v & 0x10u) ? BQ25896_FAULT_INPUT : BQ25896_FAULT_NORMAL;
    }
    return f;
}

/* ---- Pure decode: ADC registers -> physical units ----
 * See the long comment above these declarations in the header for why
 * SYSV/ICHGR use the whole byte while BATV/VBUSV mask bit 7 first -- that
 * split is in the original and is preserved. */

bq25896_batv_t bq25896_decode_batv(uint8_t v) {
    bq25896_batv_t r;
    r.thermal_regulation = (v & 0x80u) != 0u;
    r.vbat_mv = (uint16_t)(((uint16_t)(v & 0x7Fu) * 20u) + 2304u);
    return r;
}

uint16_t bq25896_decode_vsys_mv(uint8_t v) {
    return (uint16_t)(((uint16_t)v * 20u) + 2304u);
}

bq25896_vbusv_t bq25896_decode_vbusv(uint8_t v) {
    bq25896_vbusv_t r;
    r.vbus_attached = (v & 0x80u) != 0u;
    r.vbus_mv = (uint16_t)(((uint16_t)(v & 0x7Fu) * 100u) + 2600u);
    return r;
}

uint16_t bq25896_decode_ichg_actual_ma(uint8_t v) {
    return (uint16_t)((uint16_t)v * 50u);
}

float bq25896_decode_tspct_percent(uint8_t v) {
    return ((float)v) * 0.465f + 21.0f;
}

float bq25896_ntc_temperature_c(float tspct_percent) {
    /* Transcribed from getTemperature()/RtoTemp(): VTS/RP/NTC divider chain
     * followed by a single-beta (B=3950) NTC log formula against a 10k
     * reference at 25C (298.15 K). math.h's logf() replaces the original's
     * <cmath> log(), which for float arguments resolves the same way.
     *
     * The divider is only valid while rp stays below 30100 (the NTC line's
     * fixed resistor): above that, `ntc` goes negative and logf() of a
     * non-positive argument is undefined. Measured against this exact
     * chain, that happens once tspct_percent reaches roughly 85.6% --
     * comfortably inside the ~21-140.6% range TSPCT (0x10)'s 8-bit
     * register can actually produce, so this is not a theoretical edge.
     * The original has this same domain hole and does not guard it either
     * (RtoTemp() calls log() unconditionally). Since this port makes the
     * function directly host-testable, an unguarded NaN silently
     * propagating into a caller's comparison would be a wrong answer no
     * test could see coming -- so the boundary below is deliberate: return
     * NAN explicitly, rather than leave it to happen to fall out of
     * logf(). */
    float vts   = 5.0f * tspct_percent / 100.0f;
    float rp    = (vts * 5230.0f) / (5.0f - vts);
    float ntc   = (rp * 30100.0f) / (30100.0f - rp);
    float ratio = ntc / 10000.0f;
    if (!(ratio > 0.0f)) return NAN;
    float t = logf(ratio);
    t /= 3950.0f;
    t += 1.0f / 298.15f;
    t = 1.0f / t;
    return t - 273.25f;
}

/* ---- Pure encode/decode: current & voltage limit registers ---- */

uint16_t bq25896_decode_input_current_limit_ma(uint8_t v) {
    /* getInput_Current_Limit() in the original masks off bits 7/6 (EN_HIZ,
     * EN_ILIM) and scales the remaining 6-bit IINLIM code with NO +100 mA
     * offset (`data * 0.05f`), even though the real IINLIM field the
     * datasheet describes -- and every other field in this same file that
     * has both a getter and a setter -- is offset+step. That omission
     * means the original getter under-reports by exactly 100 mA at every
     * code and was never caught because, like the setter, nothing in
     * startCharging()'s commented-out block ever exercised it. This port
     * adds the offset so decode is the true inverse of
     * bq25896_encode_input_current_limit() below, rather than carrying the
     * mismatch forward into a driver where read-back would quietly lie by
     * a constant 100 mA. */
    uint8_t code = (uint8_t)(v & 0x3Fu);
    return (uint16_t)(BQ25896_IINLIM_MIN_MA + (uint16_t)code * BQ25896_IINLIM_STEP_MA);
}

uint8_t bq25896_encode_input_current_limit(uint16_t ma) {
    /* setInput_Current_Limit() in the original has two bugs, and the call
     * site in startCharging() that would have exercised it is commented
     * out -- so neither bug has ever run on shipped hardware:
     *
     *   1. Its clamp compares an Amp-scaled `cur` against the bare literal
     *      100 (`cur < 100 ? 100 : cur`), evidently meant as 0.1 (100 mA).
     *      Since every realistic call passes a value under 100.0, the
     *      branch is always taken and `cur` becomes literally 100.0f before
     *      the rest of the formula runs -- but the very next line is
     *      `cur -= 100`, which zeroes that back out, so the code actually
     *      written is always 0 (the field's minimum -- 100 mA on the real
     *      datasheet's offset+step interpretation, 0 mA on the original's
     *      offset-less getter). The real-world symptom would have been a
     *      charger that refuses to draw more than the minimum input
     *      current, not an over-current condition; there is no point in
     *      this bug's chain where 100 A is written to hardware.
     *   2. Its bit-preserve for the two bits it shares the register with
     *      shifts them down to bit 0 before OR-ing back in
     *      (`((data>>7)&1) | ((data>>6)&1) | (byte)cur`), which corrupts
     *      their position instead of preserving it. Bit 7 there is EN_HIZ
     *      -- getting that wrong can silence the input path entirely, so
     *      this is not cosmetic.
     *
     * This function instead implements the field the real BQ25896 IINLIM
     * datasheet describes -- offset 100 mA, 50 mA/LSB, 100-3250 mA range --
     * which also makes it the mathematical inverse of the decoder above.
     * The corresponding I2C-bound setter preserves bits 7/6 correctly (by
     * masking, not shifting) rather than reproducing bug 2. */
    if (ma < BQ25896_IINLIM_MIN_MA) ma = BQ25896_IINLIM_MIN_MA;
    if (ma > BQ25896_IINLIM_MAX_MA) ma = BQ25896_IINLIM_MAX_MA;
    return (uint8_t)((ma - BQ25896_IINLIM_MIN_MA) / BQ25896_IINLIM_STEP_MA);
}

uint16_t bq25896_decode_ichg_limit_ma(uint8_t v) {
    return (uint16_t)((uint16_t)(v & 0x7Fu) * BQ25896_ICHG_STEP_MA);
}

uint8_t bq25896_encode_ichg_limit(uint16_t ma) {
    /* setFast_Charge_Current_Limit(): clamped to 3008 mA even though the
     * 7-bit field itself covers up to 8128 mA (127 * 64) -- a deliberate,
     * lower safety ceiling from the original for this board's cell, not a
     * bug like the input-current case above: LSB size and offset agree
     * exactly between the original's getter and setter here, unlike ILIM
     * where they didn't. (This call is commented out in startCharging()
     * just like the others below -- none of these setters ran on shipped
     * hardware; internal consistency is the distinction from ILIM, not
     * whether it was exercised.) */
    if (ma > BQ25896_ICHG_MAX_MA) ma = BQ25896_ICHG_MAX_MA;
    return (uint8_t)(ma / BQ25896_ICHG_STEP_MA);
}

uint16_t bq25896_decode_precharge_limit_ma(uint8_t v) {
    uint8_t code = (uint8_t)((v >> 4) & 0x0Fu);
    return (uint16_t)(BQ25896_IPRECHG_MIN_MA + (uint16_t)code * BQ25896_IPRECHG_STEP_MA);
}

uint8_t bq25896_encode_precharge_limit(uint16_t ma) {
    if (ma < BQ25896_IPRECHG_MIN_MA) ma = BQ25896_IPRECHG_MIN_MA;
    if (ma > BQ25896_IPRECHG_MAX_MA) ma = BQ25896_IPRECHG_MAX_MA;
    return (uint8_t)((ma - BQ25896_IPRECHG_MIN_MA) / BQ25896_IPRECHG_STEP_MA);
}

uint16_t bq25896_decode_termination_limit_ma(uint8_t v) {
    uint8_t code = (uint8_t)(v & 0x0Fu);
    return (uint16_t)(BQ25896_IPRECHG_MIN_MA + (uint16_t)code * BQ25896_IPRECHG_STEP_MA);
}

uint8_t bq25896_encode_termination_limit(uint16_t ma) {
    /* Same offset/step/range as precharge -- the original shares one
     * formula between setPreCharge_Current_Limit() and
     * setTermination_Current_Limit(), differing only in which nibble each
     * writes. */
    return bq25896_encode_precharge_limit(ma);
}

uint16_t bq25896_decode_charge_voltage_mv(uint8_t v) {
    uint8_t code = (uint8_t)(v >> 2);
    return (uint16_t)(BQ25896_VREG_MIN_MV + (uint16_t)code * BQ25896_VREG_STEP_MV);
}

uint8_t bq25896_encode_charge_voltage(uint16_t mv) {
    /* setCharge_Voltage_Limit(): clamped to 4608 mV even though the 6-bit
     * field covers up to 4848 mV (63 * 16 + 3840) -- the same kind of
     * deliberate ceiling as the fast-charge-current case above, preserved
     * as-is. */
    if (mv < BQ25896_VREG_MIN_MV) mv = BQ25896_VREG_MIN_MV;
    if (mv > BQ25896_VREG_MAX_MV) mv = BQ25896_VREG_MAX_MV;
    return (uint8_t)((mv - BQ25896_VREG_MIN_MV) / BQ25896_VREG_STEP_MV);
}

uint16_t bq25896_decode_vindpm_mv(uint8_t v) {
    uint8_t code = (uint8_t)(v & 0x7Fu);
    return (uint16_t)(BQ25896_VINDPM_BASE_MV + (uint16_t)code * BQ25896_VINDPM_STEP_MV);
}

uint8_t bq25896_encode_vindpm(uint16_t mv) {
    /* setMinVBUS(): at or below the 2600 mV base the original leaves the
     * field at code 0 rather than going negative (`if (volt > 2.6f)` only
     * adds anything above the base) -- preserved as the lower clamp. The
     * original has NO upper clamp at all (an arbitrarily large float is
     * cast straight to byte), which is undefined for anything that
     * overflows a byte; this adds the upper clamp the field's own 7 bits
     * imply (127 steps above the base) rather than reproducing that
     * overflow. */
    if (mv <= BQ25896_VINDPM_BASE_MV) return 0u;
    if (mv > BQ25896_VINDPM_MAX_MV) mv = BQ25896_VINDPM_MAX_MV;
    return (uint8_t)((mv - BQ25896_VINDPM_BASE_MV) / BQ25896_VINDPM_STEP_MV);
}

#ifndef HOST_TEST
#include <string.h>   /* memset() -- bq25896_read_raw_all() only */
#include "common/i2c_bus.h"
#include "platform/board.h"

/* Read-modify-write a single register: clear `clear_mask` bits, then set
 * `set_bits`. Every setter below that shares a register with other fields
 * goes through this so the bits it does not own survive untouched -- the
 * property the original's setInput_Current_Limit() got wrong (see the
 * encoder's comment) and every other setter got right. */
static bool rmw(uint8_t reg, uint8_t clear_mask, uint8_t set_bits) {
    uint8_t v;
    if (!fwog_i2c_read_regs(I2C_ADDR_CHARGER, reg, &v, 1u)) return false;
    v = (uint8_t)((v & (uint8_t)~clear_mask) | set_bits);
    return fwog_i2c_write_reg(I2C_ADDR_CHARGER, reg, v);
}

bool bq25896_reset(void) {
    return fwog_i2c_write_reg(I2C_ADDR_CHARGER, BQ25896_REG_CTRL2, 0x80u);
}

bool bq25896_adc_continuous_enable(void) {
    /* setADC_enabled(): OR in CONV_START (bit 7) and CONV_RATE (bit 6). No
     * other bit in ADC_CTRL is managed by this driver, so an OR is the
     * whole operation -- matches the original exactly (two separate `|=`
     * with no clear). */
    return rmw(BQ25896_REG_ADC_CTRL, 0u, 0xC0u);
}

bool bq25896_set_watchdog_enable(bool enable) {
    if (enable) return rmw(BQ25896_REG_TIMER, 0u, 0x30u);
    return rmw(BQ25896_REG_TIMER, 0x30u, 0u);
}

bool bq25896_setup_boost(void) {
    return fwog_i2c_write_reg(I2C_ADDR_CHARGER, BQ25896_REG_CTRL1, 0x83u);
}

bool bq25896_set_charge_enable(bool enable) {
    if (enable) return rmw(BQ25896_REG_SYS_CTRL, 0u, 0x10u);
    return rmw(BQ25896_REG_SYS_CTRL, 0x10u, 0u);
}

bool bq25896_set_disable_thermistor(bool enable) {
    /* setDisableThermistor(): mode==ENABLED sets bits 7:6 of VINDPM_OS,
     * mode==DISABLED clears them. Transcribed as a plain two-bit
     * read-modify-write; no other bit in this register is touched. */
    if (enable) return rmw(BQ25896_REG_VINDPM_OS, 0u, 0xC0u);
    return rmw(BQ25896_REG_VINDPM_OS, 0xC0u, 0u);
}

bool bq25896_start_charging(void) {
    if (!bq25896_reset())                    return false;
    if (!bq25896_adc_continuous_enable())    return false;
    if (!bq25896_set_watchdog_enable(false)) return false;
    if (!bq25896_setup_boost())              return false;
    if (!bq25896_set_charge_enable(true))    return false;
    return true;
}

bool bq25896_get_input_current_limit_ma(uint16_t *ma) {
    uint8_t v;
    if (!fwog_i2c_read_regs(I2C_ADDR_CHARGER, BQ25896_REG_ILIM, &v, 1u)) return false;
    *ma = bq25896_decode_input_current_limit_ma(v);
    return true;
}

bool bq25896_set_input_current_limit_ma(uint16_t ma) {
    uint8_t orig;
    if (!fwog_i2c_read_regs(I2C_ADDR_CHARGER, BQ25896_REG_ILIM, &orig, 1u)) return false;
    uint8_t v = (uint8_t)((orig & 0xC0u) | bq25896_encode_input_current_limit(ma));
    return fwog_i2c_write_reg(I2C_ADDR_CHARGER, BQ25896_REG_ILIM, v);
}

bool bq25896_get_fast_charge_current_limit_ma(uint16_t *ma) {
    uint8_t v;
    if (!fwog_i2c_read_regs(I2C_ADDR_CHARGER, BQ25896_REG_ICHG, &v, 1u)) return false;
    *ma = bq25896_decode_ichg_limit_ma(v);
    return true;
}

bool bq25896_set_fast_charge_current_limit_ma(uint16_t ma) {
    /* setFast_Charge_Current_Limit() reads REG::ICHG first but never uses
     * the value it read (`byte reg = read(REG::ICHG);` is dead) -- that
     * pointless read is not reproduced here, since bit 7 is unconditionally
     * forced regardless of what was there. */
    uint8_t v = (uint8_t)(0x80u | bq25896_encode_ichg_limit(ma));
    return fwog_i2c_write_reg(I2C_ADDR_CHARGER, BQ25896_REG_ICHG, v);
}

bool bq25896_get_precharge_current_limit_ma(uint16_t *ma) {
    uint8_t v;
    if (!fwog_i2c_read_regs(I2C_ADDR_CHARGER, BQ25896_REG_IPRE_ITERM, &v, 1u)) return false;
    *ma = bq25896_decode_precharge_limit_ma(v);
    return true;
}

bool bq25896_set_precharge_current_limit_ma(uint16_t ma) {
    uint8_t orig;
    if (!fwog_i2c_read_regs(I2C_ADDR_CHARGER, BQ25896_REG_IPRE_ITERM, &orig, 1u)) return false;
    uint8_t v = (uint8_t)((orig & 0x0Fu) | (uint8_t)(bq25896_encode_precharge_limit(ma) << 4));
    return fwog_i2c_write_reg(I2C_ADDR_CHARGER, BQ25896_REG_IPRE_ITERM, v);
}

bool bq25896_get_termination_current_limit_ma(uint16_t *ma) {
    uint8_t v;
    if (!fwog_i2c_read_regs(I2C_ADDR_CHARGER, BQ25896_REG_IPRE_ITERM, &v, 1u)) return false;
    *ma = bq25896_decode_termination_limit_ma(v);
    return true;
}

bool bq25896_set_termination_current_limit_ma(uint16_t ma) {
    uint8_t orig;
    if (!fwog_i2c_read_regs(I2C_ADDR_CHARGER, BQ25896_REG_IPRE_ITERM, &orig, 1u)) return false;
    uint8_t v = (uint8_t)((orig & 0xF0u) | bq25896_encode_termination_limit(ma));
    return fwog_i2c_write_reg(I2C_ADDR_CHARGER, BQ25896_REG_IPRE_ITERM, v);
}

bool bq25896_get_charge_voltage_limit_mv(uint16_t *mv) {
    uint8_t v;
    if (!fwog_i2c_read_regs(I2C_ADDR_CHARGER, BQ25896_REG_VREG, &v, 1u)) return false;
    *mv = bq25896_decode_charge_voltage_mv(v);
    return true;
}

bool bq25896_set_charge_voltage_limit_mv(uint16_t mv) {
    uint8_t orig;
    if (!fwog_i2c_read_regs(I2C_ADDR_CHARGER, BQ25896_REG_VREG, &orig, 1u)) return false;
    uint8_t v = (uint8_t)((orig & 0x03u) | (uint8_t)(bq25896_encode_charge_voltage(mv) << 2));
    return fwog_i2c_write_reg(I2C_ADDR_CHARGER, BQ25896_REG_VREG, v);
}

bool bq25896_get_min_vbus_mv(uint16_t *mv) {
    uint8_t v;
    if (!fwog_i2c_read_regs(I2C_ADDR_CHARGER, BQ25896_REG_VINDPM, &v, 1u)) return false;
    *mv = bq25896_decode_vindpm_mv(v);
    return true;
}

bool bq25896_set_min_vbus_mv(uint16_t mv) {
    uint8_t orig;
    if (!fwog_i2c_read_regs(I2C_ADDR_CHARGER, BQ25896_REG_VINDPM, &orig, 1u)) return false;
    uint8_t v = (uint8_t)((orig & 0x80u) | bq25896_encode_vindpm(mv));
    return fwog_i2c_write_reg(I2C_ADDR_CHARGER, BQ25896_REG_VINDPM, v);
}

bool bq25896_read_stat(bq25896_stat_t *out) {
    uint8_t v;
    if (!fwog_i2c_read_regs(I2C_ADDR_CHARGER, BQ25896_REG_VBUS_STAT, &v, 1u)) return false;
    *out = bq25896_decode_stat_reg(v);
    return true;
}

bool bq25896_read_fault(bq25896_fault_t *out) {
    uint8_t v;
    if (!fwog_i2c_read_regs(I2C_ADDR_CHARGER, BQ25896_REG_FAULT, &v, 1u)) return false;
    *out = bq25896_decode_fault_reg(v);
    return true;
}

bool bq25896_read_vbat_mv(uint16_t *mv, bool *thermal_regulation) {
    uint8_t v;
    if (!fwog_i2c_read_regs(I2C_ADDR_CHARGER, BQ25896_REG_BATV, &v, 1u)) return false;
    bq25896_batv_t d = bq25896_decode_batv(v);
    *mv = d.vbat_mv;
    *thermal_regulation = d.thermal_regulation;
    return true;
}

bool bq25896_read_vsys_mv(uint16_t *mv) {
    uint8_t v;
    if (!fwog_i2c_read_regs(I2C_ADDR_CHARGER, BQ25896_REG_SYSV, &v, 1u)) return false;
    *mv = bq25896_decode_vsys_mv(v);
    return true;
}

bool bq25896_read_vbus_mv(uint16_t *mv, bool *attached) {
    uint8_t v;
    if (!fwog_i2c_read_regs(I2C_ADDR_CHARGER, BQ25896_REG_VBUSV, &v, 1u)) return false;
    bq25896_vbusv_t d = bq25896_decode_vbusv(v);
    *mv = d.vbus_mv;
    *attached = d.vbus_attached;
    return true;
}

bool bq25896_read_ichg_actual_ma(uint16_t *ma) {
    uint8_t v;
    if (!fwog_i2c_read_regs(I2C_ADDR_CHARGER, BQ25896_REG_ICHGR, &v, 1u)) return false;
    *ma = bq25896_decode_ichg_actual_ma(v);
    return true;
}

bool bq25896_read_tspct_percent(float *pct) {
    uint8_t v;
    if (!fwog_i2c_read_regs(I2C_ADDR_CHARGER, BQ25896_REG_TSPCT, &v, 1u)) return false;
    *pct = bq25896_decode_tspct_percent(v);
    return true;
}

bool bq25896_read_all(bq25896_telemetry_t *out) {
    bq25896_stat_t stat;
    bq25896_fault_t fault;

    /* Zero the whole snapshot up front so a failure partway through leaves
     * *out in a defined all-zero state rather than whatever the caller's
     * (possibly uninitialised) struct already held. Cheaper than requiring
     * every caller to zero it themselves, and it turns "partially filled"
     * into "zero-filled from the point of failure onward", which is at
     * least a recognizable sentinel rather than stack garbage. */
    *out = (bq25896_telemetry_t){0};

    if (!bq25896_read_stat(&stat))   return false;
    if (!bq25896_read_fault(&fault)) return false;
    if (!bq25896_read_vbus_mv(&out->vbus_mv, &out->vbus_attached)) return false;
    if (!bq25896_read_vsys_mv(&out->vsys_mv)) return false;
    if (!bq25896_read_vbat_mv(&out->vbat_mv, &out->thermal_regulation)) return false;
    if (!bq25896_read_tspct_percent(&out->tspct_percent)) return false;
    if (!bq25896_read_ichg_actual_ma(&out->ichg_ma)) return false;

    out->vbus_stat = stat.vbus_stat;
    out->chg_stat  = stat.chg_stat;
    out->vsys_stat = stat.vsys_stat;
    out->ts_rank   = fault.ts_rank;
    out->chg_fault = fault.chg_fault;

    return bq25896_adc_continuous_enable();
}

bool bq25896_read_raw_all(uint8_t regs[BQ25896_REG_COUNT]) {
    /* Zero first: fwog_i2c_read_regs() reports success or failure but never a
     * partial length, so a failed transfer must not leave whatever was on the
     * caller's stack looking like a register dump. Same reasoning as
     * bq25896_read_all()'s zero-on-entry, and the same reasoning that made a
     * zero-init readback buffer a REVIEW FINDING elsewhere in this tree -- the
     * difference is that here the zeroes are never compared against anything,
     * only printed, so they cannot false-match. */
    memset(regs, 0, (size_t)BQ25896_REG_COUNT);
    return fwog_i2c_read_regs(I2C_ADDR_CHARGER, BQ25896_REG_ILIM,
                              regs, (size_t)BQ25896_REG_COUNT);
}
#endif
