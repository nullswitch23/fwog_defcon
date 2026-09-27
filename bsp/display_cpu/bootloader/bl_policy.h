/* What the bootloader decides, separated from what it does.
 *
 * The timing constants are the design doc's boot sequence: dark for 500 ms,
 * then screen and announce, then the USB console at 10 s. */
#ifndef FWOG_BL_POLICY_H
#define FWOG_BL_POLICY_H
#include <stdbool.h>
#include <stdint.h>

#define BL_VISIBLE_AFTER_MS  500u
#define BL_CONSOLE_AFTER_MS  10000u
#define BL_HELLO_PERIOD_MS   50u
#define BL_BTN_SAMPLE_MS     10u
#define BL_DEBOUNCE_SAMPLES  4u    /* 4 x 10 ms = 40 ms of steady contact */

typedef enum {
    BL_PHASE_SILENT = 0,   /* LCD dark, announcing; a healthy boot ends here */
    BL_PHASE_VISIBLE,      /* LCD up, buttons live, still announcing         */
    BL_PHASE_CONSOLE       /* USB CDC up; stay indefinitely                  */
} bl_phase_t;

/* Monotonic in elapsed_ms: the caller may re-derive the phase every loop
 * and act only on changes. */
bl_phase_t bl_phase_for(uint32_t elapsed_ms, bool red_held);

/* Whether a received RUN should be honored.
 *
 * red_held suppresses RUN, which is stronger than the design doc's wording
 * ("never auto-boots, even with a valid app") but is the only reading that
 * works: on a healthy board main sends RUN about 50 ms after reset, so a
 * red that only suppressed a timer would never win. UPDATE_BEGIN is still
 * honored while red is held -- updating is a recovery path, not a boot. */
bool bl_should_auto_boot(bool red_held, bool app_valid, bool run_received);

typedef struct {
    uint8_t count;
    bool    latched;
} bl_debounce_t;

/* Feed one raw "pressed" sample, taken every BL_BTN_SAMPLE_MS. Returns true
 * exactly once per press, on the sample that completes BL_DEBOUNCE_SAMPLES
 * consecutive pressed readings. Any release rearms it.
 *
 * Edge-triggered rather than level, because both actions behind it are
 * one-way: green hands the CPU to the app and gray cuts battery power. */
bool bl_debounce_feed(bl_debounce_t *d, bool raw);

typedef struct {
    bool gray;
    bool green;
    bool red;
} bl_buttons_t;

#ifndef HOST_TEST
/* Sample the three buttons the bootloader cares about. Active low with
 * internal pull-ups, so this inverts; board_init_pins() must have run. */
void bl_buttons_sample(bl_buttons_t *b);
#endif

#endif
