/* Texas Instruments BQ25896 battery charger at I2C_ADDR_CHARGER (0x6B) on
 * the display CPU's I2C1 -- bsp/display_cpu/platform/board.h.
 *
 * Ported from rmpLib/rpBatteryChargeBQ25896.{h,cpp}, specifically the
 * EmbeddedDevices::BQ25896<CELL> template class embedded inside the .cpp
 * (the outer rpBatteryChargeBQ25896 class is a thin wrapper around one
 * instance of it). Register addresses, bit positions and every scale/offset
 * constant below are transcribed from that file; do not "tidy" them without
 * re-checking the source.
 *
 * ---- Ship mode boundary: READ THIS BEFORE TOUCHING CTRL1 / REG09 ----
 *
 * CTRL1 (register 0x09) bit 5 is BATFET_DIS: setting it powers the whole
 * board off with no way back on unless USB wakes it (the hardware record). The
 * display serial bootloader's one-register ship-mode path already lives in
 * bsp/display_cpu/bootloader/bl_ship.c and reads-modifies-writes exactly
 * that bit. This driver deliberately does NOT port
 * rpBatteryChargeBQ25896::shutDown() / the underlying setBattFETEnable(),
 * which is the original's only caller of the disable direction -- do not
 * add it here, and do not call into bl_ship.c from here or vice versa.
 *
 * As of the ship-mode package there are now TWO files outside this one that
 * perform that write: bootloader/bl_ship.c and power/ship_mode.c. Neither
 * changes the rule for this file -- the write stays out of the
 * general-purpose charger driver, precisely so that no caller reaching in
 * here for telemetry can find it. See power/ship_mode.h for the full
 * boundary and for why those two do not share the six lines between them.
 *
 * Two functions below still touch CTRL1, because the original's real
 * charging-init sequence touches it:
 *   - bq25896_setup_boost() writes a fixed constant that clears bit 5 (the
 *     enable direction, never the disable direction).
 *   - Nothing here ever performs a read-modify-write that could carry a
 *     stale bit 5 forward into a set; see the comments at each function.
 *
 * ---- What was left out, deliberately ----
 *
 * setBatLoad() and setForceICO() are not ported (setDisableThermistor() now
 * is -- see bq25896_set_disable_thermistor() below). "Never exercised in
 * startCharging()" is true of all three, and is also true of five of the
 * six setters this port DID carry over, so it is not by itself a reason to
 * leave any particular one out -- setDisableThermistor() got the same
 * scrutiny setInput_Current_Limit() got (a clean two-bit read-modify-write
 * on VINDPM_OS/0x01, no side effects, no register overlap with anything
 * else) and is ported because it held up.
 *
 * setForceICO() is left out for a sharper reason than "never exercised":
 * it reads, modifies, and writes back CTRL1 (0x09) -- the exact register
 * BATFET_DIS lives in, which this file's own ship-mode boundary above says
 * nothing here should read-modify-write. setForceICO()'s original code
 * never touches bit 5 (it only sets/clears bits 7/1/0), so it would not by
 * itself risk ship mode -- but adding the first CTRL1 read-modify-write to
 * this driver for a feature nothing here needs is exactly the kind of
 * precedent that boundary exists to avoid, so it stays out.
 *
 * setBatLoad() (SYS_CTRL/0x03) has no such register-overlap concern; it is
 * left out solely because, unlike setDisableThermistor(), nobody has yet
 * given it the same close look setInput_Current_Limit() got, and after
 * finding three bugs in one never-exercised setter already, guessing is
 * worse than leaving it for that same scrutiny later.
 *
 * rpLoopTime-based "time spent charging" bookkeeping
 * (obTimeCharging/m_bWasCharging in the original) is UI/telemetry
 * state-tracking, not a charger register operation, and rpLoopTime is a
 * separate class outside this port's scope -- left out.
 */
#ifndef FWOG_BQ25896_H
#define FWOG_BQ25896_H
#include <stdbool.h>
#include <stdint.h>

/* ---- Register map, transcribed from the original's `enum class REG` ----
 * BOOST_CTRL, BAT_COMP and IDPM_LIM are never touched by any function below
 * (they weren't touched by the original either, outside a commented-out
 * code fragment quoting an unrelated vendor's API) but are listed for a
 * complete map. */
#define BQ25896_REG_ILIM        0x00u   /* input current limit (IINLIM) + EN_HIZ/EN_ILIM */
#define BQ25896_REG_VINDPM_OS   0x01u
#define BQ25896_REG_ADC_CTRL    0x02u
#define BQ25896_REG_SYS_CTRL    0x03u   /* CHG_CONFIG at bit 4 */
#define BQ25896_REG_ICHG        0x04u   /* fast charge current limit */
#define BQ25896_REG_IPRE_ITERM  0x05u   /* precharge (hi nibble) + termination (lo nibble) current limits */
#define BQ25896_REG_VREG        0x06u   /* charge voltage limit */
#define BQ25896_REG_TIMER       0x07u   /* watchdog timer select at bits 5:4 */
#define BQ25896_REG_BAT_COMP    0x08u
#define BQ25896_REG_CTRL1       0x09u   /* BATFET_DIS at bit 5 -- see the ship-mode boundary above */
#define BQ25896_REG_BOOST_CTRL  0x0Au
#define BQ25896_REG_VBUS_STAT   0x0Bu   /* charge/VBUS/VSYS status, read-only */
#define BQ25896_REG_FAULT       0x0Cu   /* fault + thermal rank, read-only */
#define BQ25896_REG_VINDPM      0x0Du   /* input voltage limit (minimum VBUS) */
#define BQ25896_REG_BATV        0x0Eu   /* battery voltage ADC, read-only */
#define BQ25896_REG_SYSV        0x0Fu   /* system voltage ADC, read-only */
#define BQ25896_REG_TSPCT       0x10u   /* thermistor ADC, read-only */
#define BQ25896_REG_VBUSV       0x11u   /* VBUS voltage ADC, read-only */
#define BQ25896_REG_ICHGR       0x12u   /* actual charge current ADC, read-only */
#define BQ25896_REG_IDPM_LIM    0x13u
#define BQ25896_REG_CTRL2       0x14u   /* REG_RST at bit 7 */

/* ---- Status/fault enums, values transcribed from the original's enum classes ---- */
typedef enum {
    BQ25896_VBUS_NO_INPUT = 0,
    BQ25896_VBUS_USB_HOST = 1,
    BQ25896_VBUS_ADAPTER  = 2,
    BQ25896_VBUS_OTG      = 7
} bq25896_vbus_stat_t;

typedef enum {
    BQ25896_CHG_NOT_CHARGING = 0,
    BQ25896_CHG_PRE_CHARGE   = 1,
    BQ25896_CHG_FAST_CHARGE  = 2,
    BQ25896_CHG_DONE         = 3
} bq25896_chg_stat_t;

typedef enum {
    BQ25896_VSYS_NOT_IN_VSYSMIN = 0,
    BQ25896_VSYS_IN_VSYSMIN     = 1
} bq25896_vsys_stat_t;

typedef enum {
    BQ25896_TS_NORMAL = 0,
    BQ25896_TS_WARM   = 2,
    BQ25896_TS_COOL   = 3,
    BQ25896_TS_COLD   = 5,
    BQ25896_TS_HOT    = 6
} bq25896_ts_rank_t;

typedef enum {
    BQ25896_FAULT_NORMAL           = 0,
    BQ25896_FAULT_INPUT            = 1,
    BQ25896_FAULT_THERMAL_SHUTDOWN = 2,
    BQ25896_FAULT_TIMER_EXPIRED    = 3
} bq25896_chg_fault_t;

typedef struct {
    bq25896_vbus_stat_t vbus_stat;
    bq25896_chg_stat_t  chg_stat;
    bq25896_vsys_stat_t vsys_stat;
} bq25896_stat_t;

typedef struct {
    bq25896_ts_rank_t   ts_rank;
    bq25896_chg_fault_t chg_fault;
} bq25896_fault_t;

typedef struct {
    bool     thermal_regulation;   /* BATV bit 7: in thermal regulation */
    uint16_t vbat_mv;
} bq25896_batv_t;

typedef struct {
    bool     vbus_attached;        /* VBUSV bit 7 */
    uint16_t vbus_mv;
} bq25896_vbusv_t;

/* One-shot telemetry snapshot -- the C equivalent of the original's
 * properties()/readAndDumpParameters() read set, minus the console dump
 * and the charging-timer bookkeeping (see the header note above). */
typedef struct {
    bq25896_vbus_stat_t vbus_stat;
    bq25896_chg_stat_t  chg_stat;
    bq25896_vsys_stat_t vsys_stat;
    bq25896_ts_rank_t    ts_rank;
    bq25896_chg_fault_t  chg_fault;
    bool     thermal_regulation;
    bool     vbus_attached;
    uint16_t vbat_mv;
    uint16_t vsys_mv;
    uint16_t vbus_mv;
    uint16_t ichg_ma;       /* actual charge current, not the limit */
    float    tspct_percent;
} bq25896_telemetry_t;

/* ---- Pure decode: status/fault byte -> enums. Host-tested, no I2C. ---- */
bq25896_stat_t  bq25896_decode_stat_reg(uint8_t reg0b);
bq25896_fault_t bq25896_decode_fault_reg(uint8_t reg0c);

/* True once any non-idle charge state is reported; true only at CHARGE_DONE.
 * Mirror rpBatteryChargeBQ25896::getIsCharging()/getIsChargingDone(). */
static inline bool bq25896_is_charging(bq25896_chg_stat_t s) {
    return s != BQ25896_CHG_NOT_CHARGING;
}
static inline bool bq25896_is_charging_done(bq25896_chg_stat_t s) {
    return s == BQ25896_CHG_DONE;
}

/* ---- Pure decode: ADC registers -> physical units. Host-tested, no I2C.
 * BATV/VBUSV/SYSV/ICHGR formulas and the TSPCT/NTC temperature chain are
 * transcribed verbatim from takeVBATData/takeVBUSData/takeVSYSData/
 * takeICHGData/takeTSPCTData/RtoTemp -- including which registers do and
 * do not mask off bit 7 before scaling, which the original is inconsistent
 * about (SYSV and ICHGR use the whole byte; BATV and VBUSV mask bit 7 out
 * as a separate flag first). That inconsistency is preserved rather than
 * "fixed", because bit 7 of SYSV/ICHGR is documented reserved-at-0, so it
 * makes no observable difference and unifying the two forms would be a
 * behavior change dressed up as a cleanup. ---- */
bq25896_batv_t  bq25896_decode_batv(uint8_t reg0e);          /* offset 2304 mV, 20 mV/LSB */
uint16_t        bq25896_decode_vsys_mv(uint8_t reg0f);       /* offset 2304 mV, 20 mV/LSB */
bq25896_vbusv_t bq25896_decode_vbusv(uint8_t reg11);         /* offset 2600 mV, 100 mV/LSB */
uint16_t        bq25896_decode_ichg_actual_ma(uint8_t reg12);/* 50 mA/LSB, polarity bit ignored like the original */
float           bq25896_decode_tspct_percent(uint8_t reg10); /* offset 21%, 0.465%/LSB */
/* NTC thermistor temperature in Celsius, from a TSPCT percent already read
 * back (matches the original's getTemperature(), which consumes its own
 * cached TSPCT rather than re-reading it). B=3950, R25=10k -- transcribed
 * from RtoTemp() unchanged.
 *
 * Returns NaN (check with isnan()) for tspct_percent at or above roughly
 * 85.6% -- above that the divider chain this is transcribed from runs
 * outside its own valid domain (see the definition). That threshold is
 * inside TSPCT (0x10)'s real ~21-140.6% output range, so callers that feed
 * this a live register read must check isnan() rather than assume a
 * number came back. */
float bq25896_ntc_temperature_c(float tspct_percent);

/* ---- Pure encode/decode: current & voltage limit registers ----
 * Millivolt/milliamp in, register field out (and back). The offset/step/
 * range constants and clamp values below are transcribed from the matching
 * original getter/setter, EXCEPT the input current limit pair (0x00,
 * ILIM): both bq25896_decode_input_current_limit_ma() and
 * bq25896_encode_input_current_limit() correct bugs in the original's
 * getInput_Current_Limit()/setInput_Current_Limit() that were never
 * exercised on hardware -- see each function's definition. Every other
 * encoder still clamps to the field's real range (some ranges are
 * narrower than the register's own capacity -- a deliberate safety ceiling
 * carried over from the original, not a bug -- see the definitions).
 * Encoders return only the field's own bits; callers (the I2C-bound
 * setters below) preserve whatever other bits share the register via
 * read-modify-write, exactly as the original does.
 *
 * A SEPARATE, deliberate deviation applies to every encoder here, not just
 * ILIM: the original computes each register code through a float (cur /=
 * step; cur *= count) and truncates with a `(byte)` cast, whereas this port
 * divides integer millivolts/milliamps directly. The two are NOT always
 * the same value, because the original's float chain accumulates rounding
 * error that a subsequent truncating cast can turn into an off-by-one --
 * always downward, since truncation of a value like 21.999998 (which is
 * what the float division actually produces for what is conceptually 22)
 * yields 21, not 22. Measured against this port's exact-integer encoders:
 * ICHG differs at 11 of 48 exact-step inputs, VREG at 14 of 49, VINDPM at
 * 29 of 128, and IPRECHG/ITERM at 4 of 16. Concrete example: the original's
 * setCharge_Voltage_Limit(4.192) computes 21.999998f and truncates to code
 * 21 (4176 mV); bq25896_encode_charge_voltage(4192) computes 22 exactly
 * (4192 mV) -- one LSB higher. Since none of these setters ever ran on
 * shipped hardware (every call site is commented out in startCharging()),
 * no proven behavior is being regressed, and the value this port produces
 * is what the datasheet field actually means; reproducing the float
 * truncation would mean deliberately re-introducing an accuracy bug for no
 * reason. Flagged here, rather than silently, because AGENTS.md requires
 * deviations to be explained and a maintainer diffing against the original
 * would otherwise find an unexplained one-LSB mismatch at roughly a
 * quarter of all inputs. ---- */
#define BQ25896_IINLIM_MIN_MA   100u
#define BQ25896_IINLIM_MAX_MA  3250u
#define BQ25896_IINLIM_STEP_MA   50u
uint16_t bq25896_decode_input_current_limit_ma(uint8_t reg00);
uint8_t  bq25896_encode_input_current_limit(uint16_t ma);

#define BQ25896_ICHG_MAX_MA    3008u
#define BQ25896_ICHG_STEP_MA     64u
uint16_t bq25896_decode_ichg_limit_ma(uint8_t reg04);
uint8_t  bq25896_encode_ichg_limit(uint16_t ma);

#define BQ25896_IPRECHG_MIN_MA   64u
#define BQ25896_IPRECHG_MAX_MA 1024u
#define BQ25896_IPRECHG_STEP_MA  64u
uint16_t bq25896_decode_precharge_limit_ma(uint8_t reg05);
uint8_t  bq25896_encode_precharge_limit(uint16_t ma);
uint16_t bq25896_decode_termination_limit_ma(uint8_t reg05);
uint8_t  bq25896_encode_termination_limit(uint16_t ma);

#define BQ25896_VREG_MIN_MV    3840u
#define BQ25896_VREG_MAX_MV    4608u
#define BQ25896_VREG_STEP_MV     16u
uint16_t bq25896_decode_charge_voltage_mv(uint8_t reg06);
uint8_t  bq25896_encode_charge_voltage(uint16_t mv);

#define BQ25896_VINDPM_BASE_MV 2600u
#define BQ25896_VINDPM_STEP_MV  100u
#define BQ25896_VINDPM_MAX_MV  (BQ25896_VINDPM_BASE_MV + 127u * BQ25896_VINDPM_STEP_MV)
uint16_t bq25896_decode_vindpm_mv(uint8_t reg0d);
uint8_t  bq25896_encode_vindpm(uint16_t mv);

#ifndef HOST_TEST
/* ---- I2C-bound operations. Every one goes through fwog_i2c_* and is
 * therefore bounded by FWOG_I2C_TIMEOUT_US -- a wedged charger cannot hang
 * the caller. All return false on any I2C failure. ---- */

/* CTRL2 (0x14) REG_RST: soft-resets every register to its power-on default.
 * Transcribed from reset(), called first in startCharging(). */
bool bq25896_reset(void);

/* ADC_CTRL (0x02): set CONV_START + CONV_RATE (continuous conversion).
 * Transcribed from setADC_enabled(), called by begin() and again at the
 * end of properties() -- bq25896_read_all() below mirrors both call
 * sites. */
bool bq25896_adc_continuous_enable(void);

/* TIMER (0x07) bits 5:4: watchdog timer select. The original only ever
 * toggles both bits together (00 = disabled vs 11 = the longest timeout),
 * never an intermediate 40s/80s setting, so this mirrors that binary
 * choice rather than adding a timeout parameter nobody asked for. */
bool bq25896_set_watchdog_enable(bool enable);

/* CTRL1 (0x09) full-register overwrite of 0x83 = 0b1000_0011: sets bits
 * 7/1/0 and CLEARS bit 5 (BATFET_DIS) -- the enable direction, never the
 * disable/ship direction. Transcribed verbatim from setupBoost(), called
 * unconditionally in startCharging(). The name is the original's; it
 * writes CTRL1, not BOOST_CTRL (0x0A), and that mismatch is preserved as
 * transcribed rather than "corrected" into a guess at what was meant. Do
 * NOT turn this into a read-modify-write -- the unconditional overwrite is
 * what the original always did here, and a read-modify-write could carry a
 * stale bit 5 forward. */
bool bq25896_setup_boost(void);

/* SYS_CTRL (0x03) bit 4, CHG_CONFIG. Unrelated to CTRL1/BATFET_DIS. */
bool bq25896_set_charge_enable(bool enable);

/* VINDPM_OS (0x01) bits 7:6: a two-bit read-modify-write, transcribed from
 * setDisableThermistor(). Despite the name, the original's own mode
 * argument sets both bits when passed ENABLED and clears them when passed
 * DISABLED -- so `enable` here means exactly that, matching the polarity
 * of bq25896_set_watchdog_enable()/bq25896_set_charge_enable() rather than
 * inventing a "disable" boolean that would read backwards against them.
 * No overlap with CTRL1/BATFET_DIS or any other register this driver
 * manages. */
bool bq25896_set_disable_thermistor(bool enable);

/* Mirrors rpBatteryChargeBQ25896::startCharging() exactly: reset, enable
 * continuous ADC, disable the watchdog, run the boost-setup write, enable
 * charging. The original's setBatLoad/setDisableThermistor/
 * setFast_Charge_Current_Limit/setCharge_Voltage_Limit/
 * setPreCharge_Current_Limit/setInput_Current_Limit calls in that same
 * function are all commented out there too, so leaving them out of this
 * sequence changes nothing the original actually did. setBattFETEnable is
 * the one call this port omits that the ORIGINAL did not comment out --
 * but it lives in shutDown(), not startCharging(); see the ship-mode
 * boundary at the top of this file. */
bool bq25896_start_charging(void);

bool bq25896_get_input_current_limit_ma(uint16_t *ma);
bool bq25896_set_input_current_limit_ma(uint16_t ma);
bool bq25896_get_fast_charge_current_limit_ma(uint16_t *ma);
bool bq25896_set_fast_charge_current_limit_ma(uint16_t ma);
bool bq25896_get_precharge_current_limit_ma(uint16_t *ma);
bool bq25896_set_precharge_current_limit_ma(uint16_t ma);
bool bq25896_get_termination_current_limit_ma(uint16_t *ma);
bool bq25896_set_termination_current_limit_ma(uint16_t ma);
bool bq25896_get_charge_voltage_limit_mv(uint16_t *mv);
bool bq25896_set_charge_voltage_limit_mv(uint16_t mv);
bool bq25896_get_min_vbus_mv(uint16_t *mv);      /* VINDPM; original has no getter, added for symmetry */
bool bq25896_set_min_vbus_mv(uint16_t mv);       /* transcribed from setMinVBUS() */

bool bq25896_read_stat(bq25896_stat_t *out);                          /* VBUS_STAT (0x0B) */
bool bq25896_read_fault(bq25896_fault_t *out);                        /* FAULT (0x0C) */
bool bq25896_read_vbat_mv(uint16_t *mv, bool *thermal_regulation);    /* BATV (0x0E) */
bool bq25896_read_vsys_mv(uint16_t *mv);                              /* SYSV (0x0F) */
bool bq25896_read_vbus_mv(uint16_t *mv, bool *attached);              /* VBUSV (0x11) */
bool bq25896_read_ichg_actual_ma(uint16_t *ma);                       /* ICHGR (0x12) */
bool bq25896_read_tspct_percent(float *pct);                         /* TSPCT (0x10) */

/* Reads every telemetry register in the same order as the original's
 * properties(), then re-arms continuous ADC conversion exactly as
 * properties() does at its own end (a one-shot conversion clears
 * CONV_START once it completes). Zeroes *out on entry, then returns false
 * -- with every field the failed read and everything after it left at
 * zero, not the caller's stack garbage -- on the first read that fails. */
bool bq25896_read_all(bq25896_telemetry_t *out);

/* ---- Raw register dump ----
 *
 * Every register the part will say, undecoded, in one auto-incrementing burst
 * transfer (the BQ25896 increments its own register pointer, so this is a
 * single fwog_i2c_read_regs() call and not a loop). This is the complement of
 * bq25896_read_all(): that one returns decoded telemetry, this one returns the
 * bytes, so a host can diagnose a charger in a state this driver's decoders do
 * not model.
 *
 * Zeroes *regs on entry, so a partial or failed transfer cannot leave the
 * caller's stack garbage looking like data.
 *
 * ---- READ-ONLY. Do not add the write-back direction. ----
 * Register 0x14 (CTRL2) bit 7 is REG_RST. Reading all 21 bytes is safe;
 * writing all 21 bytes back -- the obvious "restore" companion a future reader
 * will be tempted to add next to this -- would soft-reset the charger. That
 * function is not offered here and should not be added. */
#define BQ25896_REG_COUNT 0x15u   /* 0x00 .. 0x14 inclusive */
bool bq25896_read_raw_all(uint8_t regs[BQ25896_REG_COUNT]);
#else
/* ogemu: no I2C charger. Healthy pack so VoltPet does not starve from VBAT. */
bool bq25896_read_vbat_mv(uint16_t *mv, bool *thermal_regulation);
#endif

#endif
