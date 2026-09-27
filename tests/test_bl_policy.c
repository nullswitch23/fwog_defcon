#include "test_util.h"
#include "bootloader/bl_policy.h"
#include "bootloader/bl_ui.h"
#include "common/link/bl_proto.h"   /* only for FWOG_BL_STATUS_NO_BAR */
#include <string.h>

static unsigned count_char(const char *s, char c) {
    unsigned n = 0;
    for (; *s; s++) if (*s == c) n++;
    return n;
}

int main(void) {
    /* --- boot phases on a healthy board --- */
    /* The screen stays dark for the first 500 ms so a healthy boot is
       visually silent: no bootloader splash flashing before the app. */
    ASSERT_EQ(bl_phase_for(0u, false), BL_PHASE_SILENT);
    ASSERT_EQ(bl_phase_for(499u, false), BL_PHASE_SILENT);
    ASSERT_EQ(bl_phase_for(500u, false), BL_PHASE_VISIBLE);
    ASSERT_EQ(bl_phase_for(9999u, false), BL_PHASE_VISIBLE);
    /* Ten seconds of silence from main means the board is already broken,
       so the console appears and the bootloader stays put indefinitely. */
    ASSERT_EQ(bl_phase_for(10000u, false), BL_PHASE_CONSOLE);
    ASSERT_EQ(bl_phase_for(600000u, false), BL_PHASE_CONSOLE);

    /* --- red held at reset skips the silent window entirely --- */
    ASSERT_EQ(bl_phase_for(0u, true), BL_PHASE_VISIBLE);
    ASSERT_EQ(bl_phase_for(499u, true), BL_PHASE_VISIBLE);
    /* ...but does not skip or delay the console. */
    ASSERT_EQ(bl_phase_for(10000u, true), BL_PHASE_CONSOLE);

    /* --- auto-boot --- */
    ASSERT_TRUE(bl_should_auto_boot(false, true, true));
    /* No RUN yet: waiting is the default, never booting on a timer. */
    ASSERT_TRUE(!bl_should_auto_boot(false, true, false));
    /* RUN for an app that is not valid. Main can be wrong about us. */
    ASSERT_TRUE(!bl_should_auto_boot(false, false, true));
    /* Red held BEATS RUN. The design doc says red "never auto-boots"; on a
       healthy board main sends RUN within ~50 ms, so unless red also
       suppresses RUN there is no way to get into the bootloader at all. */
    ASSERT_TRUE(!bl_should_auto_boot(true, true, true));

    /* --- debounce --- */
    {
        bl_debounce_t d;
        memset(&d, 0, sizeof d);
        /* A press must survive BL_DEBOUNCE_SAMPLES consecutive samples. */
        for (unsigned i = 0; i < BL_DEBOUNCE_SAMPLES - 1u; i++) {
            ASSERT_TRUE(!bl_debounce_feed(&d, true));
        }
        ASSERT_TRUE(bl_debounce_feed(&d, true));   /* fires exactly here */
        /* ...and fires exactly once while held. Green boots the app and
           gray powers the board off; a repeat is not merely noise. */
        ASSERT_TRUE(!bl_debounce_feed(&d, true));
        ASSERT_TRUE(!bl_debounce_feed(&d, true));

        /* Release, then a fresh press fires again. */
        ASSERT_TRUE(!bl_debounce_feed(&d, false));
        for (unsigned i = 0; i < BL_DEBOUNCE_SAMPLES - 1u; i++) {
            ASSERT_TRUE(!bl_debounce_feed(&d, true));
        }
        ASSERT_TRUE(bl_debounce_feed(&d, true));

        /* Bounce -- a run of presses broken by a release -- never fires. */
        memset(&d, 0, sizeof d);
        for (unsigned i = 0; i < 50u; i++) {
            ASSERT_TRUE(!bl_debounce_feed(&d, (i % 2u) == 0u));
        }
    }

    /* --- screen strings ---
       These are the five states the design doc names. Plan 3 renders them
       on the ST7789 behind this same function, so the wording is fixed
       here and must not drift between the two implementations. */
    ASSERT_TRUE(strcmp(bl_ui_state_text(BL_UI_WAITING),  "waiting for main") == 0);
    ASSERT_TRUE(strcmp(bl_ui_state_text(BL_UI_UPDATING), "updating") == 0);
    ASSERT_TRUE(strcmp(bl_ui_state_text(BL_UI_NO_APP),   "no valid app") == 0);
    ASSERT_TRUE(strcmp(bl_ui_state_text(BL_UI_CRC_FAIL), "image CRC failed") == 0);
    ASSERT_TRUE(strcmp(bl_ui_state_text(BL_UI_CONSOLE),  "console active") == 0);
    /* Every enumerator maps to something printable -- no NULL to %s. */
    for (int s = 0; s <= BL_UI_CONSOLE; s++) {
        const char *t = bl_ui_state_text((bl_ui_state_t)s);
        ASSERT_TRUE(t != NULL && t[0] != '\0');
    }
    /* An out-of-range value must still be printable. */
    ASSERT_TRUE(bl_ui_state_text((bl_ui_state_t)99) != NULL);

    /* --- the progress bar --- */
    {
        char bar[BL_BAR_BUF];

        bl_ui_bargraph(bar, 0u);
        ASSERT_EQ(strlen(bar), BL_BAR_CELLS + 2u);
        ASSERT_TRUE(bar[0] == '[' && bar[BL_BAR_CELLS + 1u] == ']');
        ASSERT_TRUE(strchr(bar, '#') == NULL);

        bl_ui_bargraph(bar, 100u);
        for (unsigned i = 1u; i <= BL_BAR_CELLS; i++) ASSERT_TRUE(bar[i] == '#');

        bl_ui_bargraph(bar, 50u);
        ASSERT_EQ(count_char(bar, '#'), BL_BAR_CELLS / 2u);

        /* Floored, never rounded: a full bar means finished, so 99% must
           leave at least one cell empty. Rounding would show 100% here. */
        bl_ui_bargraph(bar, 99u);
        ASSERT_EQ(count_char(bar, '#'), BL_BAR_CELLS - 1u);

        /* Likewise 1% must draw something less than a cell -- zero. */
        bl_ui_bargraph(bar, 1u);
        ASSERT_EQ(count_char(bar, '#'), 0u);

        /* The no-bar sentinel is not a percentage; it must not wrap around
           into a nearly-full bar. */
        bl_ui_bargraph(bar, FWOG_BL_STATUS_NO_BAR);
        ASSERT_EQ(count_char(bar, '#'), 0u);
    }

    TEST_RETURN();
}
