/* The five front-panel buttons on the display CPU.
 *
 * Ported from rmetButtonManager (freewiliclassicdisplay/rmetButtonManager.cpp)
 * and rpButton. The legacy runs a four-state debounce machine per button off
 * a 5 ms timer, written out five times by copy-paste; the states are kept
 * exactly and the duplication is not.
 *
 * NOT used by the display bootloader, deliberately. bootloader/bl_policy.c
 * keeps its own four-line sampler and a stricter 40 ms debounce, because both
 * actions behind it -- boot the app, cut battery power -- are one-way and
 * destructive, and it lives in the one binary this board cannot re-flash
 * without a BOOTSEL button the display CPU does not have. Duplication is the
 * right call there; do not "unify" them.
 *
 * Host injection proves the state machine and the app wiring; it does not
 * prove the pin map. Only a press on a real board does that. */
#ifndef FWOG_BUTTONS_H
#define FWOG_BUTTONS_H
#include <stdbool.h>
#include <stdint.h>
#include "platform/board.h"

/* Order is load-bearing: it makes the packed mask below value-identical to
 * the legacy rmetButtonManager::buttonStateBitfield(), which is gray=1,
 * yellow=2, green=4, blue=8, red=0x10. The hardware record fixes the colors to pins. */
typedef enum {
    FWOG_BTN_GRAY = 0,
    FWOG_BTN_YELLOW,
    FWOG_BTN_GREEN,
    FWOG_BTN_BLUE,
    FWOG_BTN_RED,
    FWOG_BTN_COUNT
} fwog_btn_id_t;

#define FWOG_BTN_BIT(id)     (1u << (unsigned)(id))
#define FWOG_BTN_ALL         0x1Fu
#define FWOG_BTN_DEBOUNCE_MS 5u    /* rmetButtonManager::process()'s timer */

/* ---- Pure: host-tested, no SDK ---- */

typedef enum {
    FWOG_BTN_PHASE_IDLE = 0,      /* released and settled              */
    FWOG_BTN_PHASE_DEBOUNCE_DOWN, /* contact seen, timing it out       */
    FWOG_BTN_PHASE_DOWN,          /* pressed and settled               */
    FWOG_BTN_PHASE_DEBOUNCE_UP    /* release seen, timing it out       */
} fwog_btn_phase_t;

typedef struct {
    fwog_btn_phase_t phase;
    uint32_t since_ms;        /* when `phase` was entered                  */
    uint32_t down_since_ms;   /* when DOWN was first entered; see below    */
} fwog_btn_state_t;

typedef struct {
    bool down;        /* level: settled pressed. True through DEBOUNCE_UP
                         too, matching the legacy, which keeps m_bIsPressed
                         set while it debounces the release. */
    bool pressed;     /* edge: became down on this step. Exactly once.  */
    bool released;    /* edge: became up on this step. Exactly once.    */
} fwog_btn_event_t;

/* Advance one button. `raw` is the unfiltered "physically pressed" reading
 * (the caller has already inverted for active-low). `now_ms` is any
 * monotonic millisecond clock -- the caller may poll at any rate, including
 * an irregular one, because the elapsed time is measured rather than assumed.
 *
 * Time comparisons are unsigned subtraction, which is correct across the
 * ~49.7-day uint32_t wrap for any interval shorter than 2^31 ms. */
fwog_btn_event_t fwog_btn_step(fwog_btn_state_t *s, bool raw, uint32_t now_ms);

/* How long the button has been settled-down, or 0 if it is not down.
 *
 * A bouncy release does NOT restart this: returning to DOWN from
 * DEBOUNCE_UP leaves down_since_ms alone, so a flaky contact cannot make a
 * long press unreachable. Long-press and double-click are deliberately left
 * to the caller -- the legacy puts both in rpButtonStateManager at the UI
 * layer, with thresholds this driver has no business guessing. */
uint32_t fwog_btn_held_ms(const fwog_btn_state_t *s, uint32_t now_ms);

/* ---- Hardware (and HOST_TEST injection) ---- */

typedef struct {
    uint8_t down;      /* bit per fwog_btn_id_t: settled pressed        */
    uint8_t pressed;   /* bit per id: became pressed on this poll       */
    uint8_t released;  /* bit per id: became released on this poll      */
} fwog_buttons_t;

/* Zero the state and clear any injection. board_init_pins() has already
 * enabled the pull-ups, so this touches no hardware and is safe to call
 * more than once. */
void fwog_buttons_init(void);

/* Sample all five and advance every state machine. Call it as often as you
 * like; `now_ms` is what actually decides the timing. */
fwog_buttons_t fwog_buttons_poll(uint32_t now_ms);

/* Simulate presses from the host.
 *
 * `mask` selects which buttons are overridden; `level` gives their simulated
 * pressed state. Both are masked to the five valid bits. Injection replaces
 * the RAW pin sample, not the debounced result, so a simulated press runs
 * through the same state machine with the same timing as a real one -- what
 * a test exercises is the real path, not a bypass.
 *
 * Compiled into every build on purpose: it costs two bytes and one branch,
 * and needing a special build is exactly the problem when the unit you want
 * to drive is already in front of you. Off after reset. */
void fwog_buttons_inject(uint8_t mask, uint8_t level);

/* Hand every button back to its pin. */
void fwog_buttons_inject_clear(void);

#endif
