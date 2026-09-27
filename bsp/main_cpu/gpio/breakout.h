/* The ONE way to change breakout I/O direction on this board.
 *
 * A breakout line's direction is set in three places that must agree -- the
 * FPGA's io_buffer, the PCAL6416's level shifters, and the RP2040's pads.
 * Two are on this CPU and one is on the display. If they disagree, an output
 * drives an output.
 *
 * There is deliberately no API that writes one without the others -- with
 * one narrow, deliberate exception: `cfg->gpio25_out == false` is rejected
 * outright (FWOG_IO_ERR_UNSUPPORTED) rather than partially applied, because
 * main's GPIO 25 is PIN_LED, permanently GPIO_OUT, and there is no pad write
 * for it to disagree through -- see gpio25_out's doc in common/io_cfg.h. The
 * order and the readback-abort for everything else are in common/io_seq.c,
 * host-tested; this file only binds real hardware to it.
 *
 * REQUIRES a display CPU running an application that services the link. The
 * display bootloader does not, and must not touch the expander at all (see
 * io_expander/pcal6416.h) -- so this is callable only after
 * fwog_display_update_run() has handed off. It fails cleanly rather than
 * hanging if the display does not answer -- but "fails cleanly" describes
 * how the call RETURNS, not what state it leaves behind. Two return paths
 * leave the three-way agreement genuinely broken, not merely "nothing
 * happened":
 *
 *   - FWOG_IO_ERR_EXPANDER: by the time the expander can fail, the FPGA and
 *     the RP2040's own pads have ALREADY moved to the new configuration
 *     (io_seq.h's ordering note) -- only the shifters may still disagree.
 *     fwog_io_dir_last() records the new config in this case (not the
 *     previous one), and fwog_io_dir_last_fully_applied() returns false so a
 *     caller can tell the difference from a clean success.
 *   - A wait_ack() timeout with the display reset mid-sequence: the
 *     bootloader it lands in correctly ignores 0x20-0x22 without acking (it
 *     must not touch the expander -- see io_expander/pcal6416.h), so the
 *     wait times out with the FPGA and pads already moved and the expander
 *     never told anything at all. From this call's point of view that is
 *     indistinguishable from FWOG_IO_ERR_ENABLE/FWOG_IO_ERR_EXPANDER's
 *     ordinary timeout case -- treat any non-OK, non-VERIFY return as a
 *     signal that the board may be in a mixed state, not as "no-op".
 *
 * A timeout specifically on the CONFIG_ENABLE(true) step (FWOG_IO_ERR_ENABLE)
 * has its own hazard: the display may have set io_config and acked LATE,
 * after this call already gave up. The expander is then left with IO_CONFIG
 * asserted permanently, which io_seq.h itself warns "would let a later
 * unrelated expander write disturb directions." The remedy is the same in
 * both directions: a later, successful fwog_io_dir_apply() re-opens and
 * re-closes the window as part of its own sequence, restoring the invariant.
 *
 * NOT REENTRANT. fwog_io_dir_apply() and fwog_io_dir_last() share file-scope
 * static state (the last-applied config, and the link receive buffer used
 * while waiting for the display's acks). Call both only from a single
 * context -- never from an ISR, and never from core1 concurrently with
 * core0 (or vice versa). A second call must not start until the first has
 * returned. */
#ifndef FWOG_BREAKOUT_H
#define FWOG_BREAKOUT_H
#include <stdbool.h>
#include "common/io_cfg.h"
#include "common/io_pins.h"
#include "common/io_seq.h"

/* How long to wait for the display's ack for each of the two display-side
 * steps (CONFIG_ENABLE, SET_DIRS). The dominant term is NOT the I2C
 * transaction -- that costs on the order of a millisecond at 400 kHz -- it
 * is the display application's own scheduling latency: apps/bench/display's
 * main loop services the link only once per pass, after console line
 * processing and a USB-CDC heartbeat write, either of which can plausibly
 * push a single pass past 50 ms. 250 ms gives that real latency headroom
 * while still sitting well inside the 8300 ms watchdog window even if BOTH
 * waits in one fwog_io_dir_apply() call time out. (It sat well inside the
 * 2000 ms window this constant was chosen against, too -- the margin only
 * grew.) Retia (and any display that blits a full 320×240 between UART
 * polls) can sit near 250 ms; 750 ms still fits eight retries inside the
 * 8.3 s watchdog. */
#define FWOG_IO_ACK_TIMEOUT_MS 750u

fwog_io_result_t fwog_io_dir_apply(const fwog_io_cfg_t *cfg);

/* The last configuration recorded -- successfully applied in full, OR
 * partially applied with FWOG_IO_ERR_EXPANDER (see the file header). Before
 * the first apply this is fwog_io_cfg_default(). Check
 * fwog_io_dir_last_fully_applied() to tell the two cases apart. */
const fwog_io_cfg_t *fwog_io_dir_last(void);

/* False when fwog_io_dir_last() reflects a config where the FPGA and the
 * RP2040's own pads moved but the expander did not (the
 * FWOG_IO_ERR_EXPANDER case) -- i.e. the three-way agreement is currently
 * broken. True otherwise, including before the first apply. */
bool fwog_io_dir_last_fully_applied(void);

/* --- diagnostic, for the FWOG_IO_ERR_FPGA_VERIFY investigation --- */

/* What fwog_io_dir_probe() saw. All four byte pairs are in the order the FPGA
 * returns them: [0] is the low byte of the dirword, [1] is
 * (gp26 << 1) | gp27. */
typedef struct {
    uint8_t want[2];      /* what fwog_io_pack_fpga() says cfg should write */
    uint8_t pre[2];       /* read BEFORE any write, window CLOSED */
    uint8_t open_pre[2];  /* read after the window OPENED, still before any
                             write this call -- so it shows what the register
                             kept from the PREVIOUS call. This is what
                             separates "the register clears when the window
                             closes" from "the register is merely unreadable
                             while the window is closed", which `pre` and
                             `post` alone cannot tell apart and which decides
                             whether a completed fwog_io_dir_apply() actually
                             leaves the FPGA holding the direction it
                             verified. */
    uint8_t open_rd[2];   /* read after the write, window OPEN -- the real
                             sequence's conditions, i.e. the byte pair whose
                             mismatch produces FWOG_IO_ERR_FPGA_VERIFY */
    uint8_t post[2];      /* read after the window closed again */
    bool enable_ok;       /* the CONFIG_ENABLE(true) ack */
    bool disable_ok;      /* the CONFIG_ENABLE(false) ack */
} fwog_io_dir_probe_t;

/* Read the FPGA's direction register at three points around one write, and
 * report the raw bytes. DIAGNOSTIC ONLY.
 *
 * This exists because `iodir` collapses everything the FPGA said into a
 * single result code: FWOG_IO_ERR_FPGA_VERIFY tells you the readback
 * disagreed but not HOW, and the two candidate causes -- a dead SPI read path
 * versus a register that is not latching -- are distinguished by the bytes
 * themselves. All 0x00 or all 0xFF means nothing is driving MISO; structured
 * but wrong means the transaction works and the value does not stick.
 *
 * Unlike fwog_io_dir_apply() this touches NEITHER the RP2040's pads NOR the
 * expander's shifter directions, so it cannot create an output-driving-output
 * conflict no matter what it reads. It does write the FPGA register -- that
 * is the point -- and it opens and closes the IO_CONFIG window, leaving it
 * closed on every path, exactly as fwog_io_sequence() does.
 *
 * ---- PASS THE CONFIG CURRENTLY IN FORCE, NEVER A DIFFERENT ONE. ----
 *
 * This function LEAVES `cfg`'s word in the FPGA register. Probing with a
 * config that is not the applied one therefore silently changes the board's
 * direction routing, and the FPGA's register is what actually gates the level
 * shifters -- the expander's own direction bits are not sufficient on their
 * own (the hardware record).
 *
 * Calling it with fwog_io_cfg_default() while a non-default direction was
 * applied cost an entire session on 2026-07-28: every measurement that
 * "proved" a direction change never reached the pins had a probe sitting
 * between the change and the measurement, reverting it. The instrument
 * destroyed the state it was built to observe, and it did so while printing
 * the evidence -- `open_pre=E901 open=6D01` says exactly that, and was read
 * past several times. Callers should pass *fwog_io_dir_last().
 *
 * `pre` and `post` are read with the window CLOSED on purpose. If the window
 * is the variable, `open_rd` and `post` differ; if the read path is simply
 * dead, all three reads are the same dead value and the window is exonerated
 * without a second experiment.
 *
 * Read buffers are seeded with 0xA5/0x5A rather than zero: fwog_fpga_dir_read()
 * is contracted to write both bytes, and seeing the seed survive would mean
 * that contract is broken -- which is worth being able to see rather than
 * having it hide behind a plausible-looking 0x00.
 *
 * Same non-reentrancy rules as fwog_io_dir_apply(). */
void fwog_io_dir_probe(const fwog_io_cfg_t *cfg, fwog_io_dir_probe_t *out);

/* --- driving and sampling one breakout line --- */

/* Main's current belief about the expander's IO_CONFIG bit. Exposed for
 * reporting; the drive guard consults it internally. */
fwog_io_config_state_t fwog_io_config_state(void);

/* Drive one breakout line to `level`.
 *
 * The refusal rules -- and the reasoning for each -- are
 * fwog_io_pin_check_drive()'s, in common/io_pins.h. Nothing is touched unless
 * the call returns FWOG_IO_PIN_OK.
 *
 * This is the ONLY way to set a breakout line's level. It deliberately does
 * NOT change any direction: fwog_io_dir_apply() owns direction, because a
 * direction has to move on three surfaces at once. If a line is an input,
 * this refuses rather than quietly flipping it -- exactly so `iopin` cannot
 * create by a side door the output-driving-output state io_seq.c exists to
 * prevent.
 *
 * The pad is re-asserted on every call rather than assumed, because
 * fwog_ice40_spi_release() leaves GPIO 12/14/15 as plain INPUTS after every
 * FPGA transaction -- so a line the config rightly calls an output may not be
 * one at the moment this is called. */
fwog_io_pin_result_t fwog_io_pin_drive(unsigned gpio, bool level);

/* Sample one breakout line. Drives nothing, so it needs neither the direction
 * nor the IO_CONFIG guard -- only that the pin is a breakout line at all.
 *
 * For a line the config calls an INPUT this forces the pad to GPIO_IN before
 * sampling. That is not tidiness: if such a line were left driven, gpio_get()
 * would report our own output latch and a loopback test would PASS while
 * sensing nothing. A false pass is worse than a failure. */
fwog_io_pin_result_t fwog_io_pin_read(unsigned gpio, bool *level_out);

/* Open or close the expander's IO_CONFIG window and LEAVE it that way.
 *
 * Every other path here brackets the window inside one operation
 * (io_seq.c opens at step 1 and closes at step 6 on every path), so nothing
 * can observe the bus WHILE it is asserted. This makes that state reachable so
 * the ordinary `iopin` commands can measure it -- deliberately a state-setter
 * and not a measurement, because the previous attempt at this question bundled
 * both into one bespoke function and its result then had to be trusted on its
 * own terms. Splitting them means the measuring is done by the path already
 * proven by the hardware record.
 *
 * The open question it exists for: is the breakout bus live at all while
 * IO_CONFIG is asserted, or are the level shifters gated during
 * reconfiguration? Direction changes themselves are settled -- they work, with
 * the window closed (the hardware record).
 *
 * LEAVING THE WINDOW OPEN IS NOT A NEUTRAL STATE. io_seq.h: an open window
 * "would let a later unrelated expander write disturb directions", and the
 * display latches direction bits only while it is set. Close it when done; a
 * later fwog_io_dir_apply() also restores the invariant by bracketing its own.
 *
 * Returns false if the display did not ack. On a timeout the window state is
 * UNKNOWN, not merely unchanged -- the display may have applied it and acked
 * late (see FWOG_IO_ACK_TIMEOUT_MS above) -- which is why
 * fwog_io_config_state() is tri-state and why the FPGA SPI group refuses to be
 * driven unless it reads DISABLED. */
bool fwog_io_config_window(bool open);

/* --- the breakout I2C lines, open-drain --- */

/* Pull an I2C line LOW, or RELEASE it. There is deliberately no way to drive
 * it high: I2C is open-drain, and driving high against a device pulling low is
 * contention. `low=false` releases the pad to an input and lets the external
 * pull-up take the line -- it never asserts a level.
 *
 * Releasing also disables the RP2040's internal pulls. That is what makes the
 * measurement mean something: with no internal pull-up competing, a released
 * line that reads HIGH proves an EXTERNAL pull-up is holding it, which is the
 * PCA9517's software-controllable 10k. With an internal pull-up left on, the
 * line would go high whether or not the external one existed and the test
 * would prove nothing.
 *
 * On a RELEASE, `rise_us_out` (if non-NULL) receives how long the line took to
 * read high, in microseconds, or FWOG_I2C_RISE_NONE if it never did within
 * FWOG_I2C_RISE_TIMEOUT_US. Measuring this is not decoration: sampling
 * immediately after a release caught the line still low on one of two
 * otherwise-identical passes, and the next console command milliseconds later
 * read it high. An open-drain line rises only as fast as a deliberately weak
 * pull-up can charge the bus capacitance, so "released" and "high" are not the
 * same instant, and a test that assumes they are is intermittently wrong.
 *
 * Never rising is NOT reported as an error: a line held low by a device on the
 * bus is a legitimate state and is indistinguishable here from a missing
 * pull-up. The number is reported; the judgement is the operator's.
 *
 * Returns FWOG_IO_PIN_ERR_NOT_BREAKOUT for any GPIO that is not SDA or SCL. */
#define FWOG_I2C_RISE_TIMEOUT_US 10000u
#define FWOG_I2C_RISE_NONE       0xFFFFFFFFu
fwog_io_pin_result_t fwog_io_i2c_pull(unsigned gpio, bool low,
                                      uint32_t *rise_us_out);

/* Sample an I2C line WITHOUT changing it -- no direction change, no pull
 * change. Reading must not disturb a line another step is deliberately
 * holding low, or a crosstalk check would destroy the state it is checking. */
fwog_io_pin_result_t fwog_io_i2c_sense(unsigned gpio, bool *level_out);

#endif
