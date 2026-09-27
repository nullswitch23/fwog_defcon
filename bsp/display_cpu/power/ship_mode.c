#include "power/ship_mode.h"

fwog_ship_phase_t fwog_ship_step(fwog_ship_state_t *s, bool red_pressed,
                                 uint32_t now_ms) {
    if (!red_pressed) {
        /* Any release, from any phase, discards the countdown. There is no
         * accumulation path back into ARMING that skips this. */
        s->phase = FWOG_SHIP_IDLE;
        s->press_ms = 0u;
        return FWOG_SHIP_IDLE;
    }

    if (s->phase == FWOG_SHIP_IDLE) {
        s->phase = FWOG_SHIP_ARMING;
        s->press_ms = now_ms;
        return FWOG_SHIP_ARMING;
    }

    /* Already fired on an earlier step of this same hold: latched, and the
     * caller must not be told to fire again. The phase stays FIRED so an
     * inspector can see the latch; the RETURN is ARMING so the caller's
     * `== FWOG_SHIP_FIRED` test is true exactly once. See the header. */
    if (s->phase == FWOG_SHIP_FIRED) return FWOG_SHIP_ARMING;

    /* Unsigned subtraction -- correct across the uint32_t wrap for any
     * interval shorter than 2^31 ms, which 6000 comfortably is. */
    if ((uint32_t)(now_ms - s->press_ms) >= FWOG_SHIP_HOLD_MS) {
        s->phase = FWOG_SHIP_FIRED;
        return FWOG_SHIP_FIRED;
    }
    return FWOG_SHIP_ARMING;
}

unsigned fwog_ship_progress(const fwog_ship_state_t *s, uint32_t now_ms) {
    if (s->phase == FWOG_SHIP_IDLE) return 0u;

    const uint32_t elapsed = (uint32_t)(now_ms - s->press_ms);
    /* Clamp BEFORE the multiply, not after: elapsed * 100 overflows uint32_t
     * past ~42.9e6 ms, which a latched FIRED phase held long enough (or a
     * caller passing a stale now_ms) can reach. Clamping first makes the
     * arithmetic exact for every input rather than for most of them. */
    if (elapsed >= FWOG_SHIP_HOLD_MS) return 100u;
    return (unsigned)((elapsed * 100u) / FWOG_SHIP_HOLD_MS);
}

#ifndef HOST_TEST
#include "common/diag.h"
#include "common/i2c_bus.h"
#include "platform/board.h"
#include "power/bq25896.h"

/* Duplicated from bootloader/bl_ship.c on purpose -- see this file's header
 * for why the two must not share. bq25896.h supplies the register address (a
 * map constant, not a code path); the bit is defined here because bq25896.h
 * deliberately does not name BATFET_DIS at all. */
#define FWOG_SHIP_BATFET_DIS (1u << 5)

bool fwog_ship_enter(void) {
    uint8_t v;
    if (!fwog_i2c_read_regs(I2C_ADDR_CHARGER, BQ25896_REG_CTRL1, &v, 1u)) {
        DIAG("[ship] charger did not answer at 0x%02x\n", I2C_ADDR_CHARGER);
        return false;
    }
    const uint8_t out = (uint8_t)(v | FWOG_SHIP_BATFET_DIS);
    /* Logged BEFORE the write: if the write succeeds there is no CPU left to
     * print anything, so a line emitted afterwards would only ever appear on
     * the failure path -- and a bench operator would see nothing at all on the
     * one outcome they were trying to produce. */
    DIAG("[ship] BATFET_DIS: REG09 %02x -> %02x\n", (unsigned)v, (unsigned)out);
    if (!fwog_i2c_write_reg(I2C_ADDR_CHARGER, BQ25896_REG_CTRL1, out)) {
        DIAG("[ship] BATFET_DIS write failed\n");
        return false;
    }
    return true;
}
#else
#include "common/diag.h"

bool fwog_ship_enter(void) {
    /* Do not kill the process: agents need the frame after a 6 s hold.
     * Returning true matches the hardware contract (the write "succeeded")
     * without actually opening the pack FET. */
    DIAG("ogemu: ship would run (red held %u ms); not powering off\n",
         (unsigned)FWOG_SHIP_HOLD_MS);
    return true;
}
#endif
