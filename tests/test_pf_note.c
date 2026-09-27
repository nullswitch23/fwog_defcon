#include "test_util.h"
#include "pf_note.h"
#include <math.h>
#include <string.h>

int main(void) {
    char name[8];
    int cents = 99;

    ASSERT_EQ(1, pf_from_hz(440, name, sizeof name, &cents));
    ASSERT_TRUE(strcmp(name, "A4") == 0);
    ASSERT_EQ(0, cents);

    ASSERT_EQ(1, pf_from_hz(880, name, sizeof name, &cents));
    ASSERT_TRUE(strcmp(name, "A5") == 0);

    ASSERT_EQ(1, pf_from_hz(370, name, sizeof name, &cents));
    ASSERT_TRUE(strcmp(name, "F#4") == 0);

    ASSERT_EQ(1, pf_from_hz(2960, name, sizeof name, &cents));
    ASSERT_TRUE(strcmp(name, "F#7") == 0);

    ASSERT_EQ(0, pf_from_hz(20, name, sizeof name, &cents));
    ASSERT_EQ(0, pf_from_hz(4000, name, sizeof name, &cents));

    ASSERT_TRUE(fabsf(pf_parabolic_delta(1.0f, 3.0f, 1.0f)) < 0.01f);
    ASSERT_TRUE(pf_parabolic_delta(1.0f, 2.0f, 1.5f) > 0.0f);

    {
        float m2[40];
        unsigned i;
        for (i = 0; i < 40u; i++) m2[i] = 0.1f;
        m2[24] = 10.0f; /* louder harmonic */
        m2[12] = 4.0f;
        m2[36] = 4.0f;
        ASSERT_EQ(pf_hps_bin(m2, 40u, 3u), 12u);
    }
    ASSERT_EQ(pf_midi_from_hz(440.0f), 69);
    ASSERT_EQ(pf_midi_from_hz(370.0f), 66); /* F#4 */
    {
        char nm[8];
        pf_name_midi(69, nm, sizeof nm);
        ASSERT_TRUE(strcmp(nm, "A4") == 0);
        ASSERT_TRUE(pf_cents_vs_midi(440.0f, 69) == 0);
    }
    {
        int cand = 0, shown = 0;
        unsigned n = 0;
        shown = pf_hold_midi(&cand, &n, 69, shown);
        shown = pf_hold_midi(&cand, &n, 69, shown);
        ASSERT_EQ(shown, 0);
        shown = pf_hold_midi(&cand, &n, 69, shown);
        ASSERT_EQ(shown, 69);
        shown = pf_hold_midi(&cand, &n, 70, shown);
        ASSERT_EQ(shown, 69);
        shown = pf_hold_midi(&cand, &n, 70, shown);
        shown = pf_hold_midi(&cand, &n, 70, shown);
        ASSERT_EQ(shown, 70);
    }

    TEST_RETURN();
}
