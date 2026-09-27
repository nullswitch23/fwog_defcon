#include "input/buttons.h"

/* Unsigned subtraction, so this stays correct across the uint32_t
   millisecond wrap -- `now >= since + N` would not. */
static bool elapsed(uint32_t since_ms, uint32_t now_ms, uint32_t want_ms) {
    return (uint32_t)(now_ms - since_ms) >= want_ms;
}

fwog_btn_event_t fwog_btn_step(fwog_btn_state_t *s, bool raw, uint32_t now_ms) {
    fwog_btn_event_t e = {false, false, false};

    switch (s->phase) {
    case FWOG_BTN_PHASE_IDLE:
        if (raw) {
            s->phase = FWOG_BTN_PHASE_DEBOUNCE_DOWN;
            s->since_ms = now_ms;
        }
        break;

    case FWOG_BTN_PHASE_DEBOUNCE_DOWN:
        if (!raw) {
            /* Gone before it settled: a glitch, and the entire reason this
               state exists. Report nothing at all. */
            s->phase = FWOG_BTN_PHASE_IDLE;
        } else if (elapsed(s->since_ms, now_ms, FWOG_BTN_DEBOUNCE_MS)) {
            s->phase = FWOG_BTN_PHASE_DOWN;
            s->since_ms = now_ms;
            s->down_since_ms = now_ms;
            e.pressed = true;
        }
        break;

    case FWOG_BTN_PHASE_DOWN:
        if (!raw) {
            s->phase = FWOG_BTN_PHASE_DEBOUNCE_UP;
            s->since_ms = now_ms;
        }
        break;

    case FWOG_BTN_PHASE_DEBOUNCE_UP:
        if (raw) {
            /* Bounced back before the release settled. Return to DOWN
               WITHOUT touching down_since_ms: a flaky contact must not
               restart a long-press measurement, and this is not a new
               press, so no edge is emitted. */
            s->phase = FWOG_BTN_PHASE_DOWN;
        } else if (elapsed(s->since_ms, now_ms, FWOG_BTN_DEBOUNCE_MS)) {
            s->phase = FWOG_BTN_PHASE_IDLE;
            s->since_ms = now_ms;
            e.released = true;
        }
        break;

    default:
        s->phase = FWOG_BTN_PHASE_IDLE;
        break;
    }

    /* Level, computed after the transition so an edge and its level agree
       on the same step. True through DEBOUNCE_UP as well, matching the
       legacy's m_bIsPressed. */
    e.down = (s->phase == FWOG_BTN_PHASE_DOWN ||
              s->phase == FWOG_BTN_PHASE_DEBOUNCE_UP);
    return e;
}

uint32_t fwog_btn_held_ms(const fwog_btn_state_t *s, uint32_t now_ms) {
    if (s->phase != FWOG_BTN_PHASE_DOWN &&
        s->phase != FWOG_BTN_PHASE_DEBOUNCE_UP) {
        return 0u;
    }
    return (uint32_t)(now_ms - s->down_since_ms);
}

#ifndef HOST_TEST
#include "hardware/gpio.h"

/* Indexed by fwog_btn_id_t. The hardware record fixes these; the legacy BUTTON1..5
   names carry no color information, which is why this table is by color. */
static const uint8_t s_pin[FWOG_BTN_COUNT] = {
    PIN_BTN_GRAY, PIN_BTN_YELLOW, PIN_BTN_GREEN, PIN_BTN_BLUE, PIN_BTN_RED
};
#endif

static fwog_btn_state_t s_state[FWOG_BTN_COUNT];
static uint8_t s_inject_mask;
static uint8_t s_inject_level;

void fwog_buttons_init(void) {
    for (unsigned i = 0; i < FWOG_BTN_COUNT; i++) {
        s_state[i].phase = FWOG_BTN_PHASE_IDLE;
        s_state[i].since_ms = 0u;
        s_state[i].down_since_ms = 0u;
    }
    s_inject_mask = 0u;
    s_inject_level = 0u;
}

void fwog_buttons_inject(uint8_t mask, uint8_t level) {
    s_inject_mask  = mask & FWOG_BTN_ALL;
    s_inject_level = level & FWOG_BTN_ALL;
}

void fwog_buttons_inject_clear(void) {
    s_inject_mask = 0u;
    s_inject_level = 0u;
}

fwog_buttons_t fwog_buttons_poll(uint32_t now_ms) {
    fwog_buttons_t out = {0u, 0u, 0u};
    for (unsigned i = 0; i < FWOG_BTN_COUNT; i++) {
        const uint8_t bit = (uint8_t)FWOG_BTN_BIT(i);
        /* Active low with internal pull-ups, so the pin inverts. Injection
           substitutes this raw reading rather than the debounced result --
           see the header. Under HOST_TEST there is no GPIO: uninjected
           buttons read released. */
        bool raw;
#ifdef HOST_TEST
        raw = (s_inject_mask & bit) ? ((s_inject_level & bit) != 0u) : false;
#else
        raw = (s_inject_mask & bit) ? ((s_inject_level & bit) != 0u)
                                    : !gpio_get(s_pin[i]);
#endif
        const fwog_btn_event_t e = fwog_btn_step(&s_state[i], raw, now_ms);
        if (e.down)     out.down     |= bit;
        if (e.pressed)  out.pressed  |= bit;
        if (e.released) out.released |= bit;
    }
    return out;
}
