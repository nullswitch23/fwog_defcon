#include "test_util.h"
#include "ph_track.h"
#include <string.h>

static void addr(uint8_t a[6], unsigned n) {
    memset(a, 0, 6);
    a[5] = (uint8_t)n;
}

static uint8_t bursts(ph_trend_t *tr, const int8_t *v, unsigned n) {
    uint8_t t = PH_TREND_UNK;
    for (unsigned i = 0; i < n; i++) t = ph_trend_burst(tr, v[i]);
    return t;
}

int main(void) {
    ph_table_t t;
    ph_table_init(&t);
    uint8_t a1[6], a2[6], a3[6];
    addr(a1, 1);
    addr(a2, 2);
    addr(a3, 3);

    ph_table_upsert(&t, 1000, -40, 1, a1, "AirTag");
    ph_table_upsert(&t, 1000, -70, 0, a2, NULL);
    ph_table_upsert(&t, 1000, -55, 0, a3, "Phone");
    ASSERT_EQ(3u, t.n);
    ASSERT_EQ((unsigned long long)(int)-40, (unsigned long long)(int)t.dev[0].rssi);
    ASSERT_TRUE(strcmp(t.dev[0].name, "AirTag") == 0);

    ASSERT_EQ(3u, ph_table_match_count(&t, PH_FILT_ALL));
    ASSERT_EQ(2u, ph_table_match_count(&t, PH_FILT_NAMED));
    ASSERT_EQ(1u, ph_table_match_count(&t, PH_FILT_APPLE));

    ph_dev_t win[2];
    ASSERT_EQ(2u, ph_table_window(&t, PH_FILT_ALL, 0, win, 2, NULL));
    ASSERT_EQ((unsigned long long)(int)-40, (unsigned long long)(int)win[0].rssi);
    ASSERT_EQ((unsigned long long)(int)-55, (unsigned long long)(int)win[1].rssi);
    ASSERT_EQ(1u, ph_table_window(&t, PH_FILT_ALL, 2, win, 2, NULL));
    ASSERT_EQ((unsigned long long)(int)-70, (unsigned long long)(int)win[0].rssi);

    {
        ph_dev_t snap[3], page[2];
        unsigned ns = ph_table_window(&t, PH_FILT_ALL, 0, snap, 3, NULL);
        ASSERT_EQ(3u, ns);
        ASSERT_EQ(2u, ph_snap_window(snap, ns, 0, page, 2));
        ASSERT_EQ(0, memcmp(page[0].addr, snap[0].addr, 6));
        ASSERT_EQ(1u, ph_snap_window(snap, ns, 2, page, 2));
        ASSERT_EQ(0, memcmp(page[0].addr, snap[2].addr, 6));
        ASSERT_EQ(0u, ph_snap_window(snap, ns, 9, page, 2));
    }

    ASSERT_EQ(2u, ph_table_window(&t, PH_FILT_ALL, 0, win, 2, a3));
    ASSERT_EQ(0, memcmp(win[0].addr, a3, 6));
    ASSERT_EQ((unsigned long long)(int)-40, (unsigned long long)(int)win[1].rssi);

    ph_table_upsert(&t, 1000, -30, 1, a1, "AirTag");
    ASSERT_EQ((unsigned long long)(int)-30, (unsigned long long)(int)t.dev[0].rssi);

    ph_table_age(&t, 1000 + PH_STALE_MS + 1u, PH_STALE_MS, NULL);
    ASSERT_EQ(0u, t.n);

    ph_table_upsert(&t, 9000, -20, 0, a2, "Keep");
    ph_table_upsert(&t, 100, -10, 0, a1, "Old");
    ph_table_age(&t, 9000, PH_STALE_MS, a1);
    ASSERT_EQ(2u, t.n);

    {
        uint8_t miss[6] = {9, 9, 9, 9, 9, 9};
        ASSERT_TRUE(ph_table_find(&t, a1) != NULL);
        ASSERT_TRUE(ph_table_find(&t, miss) == NULL);
    }

    /* Burst envelope, not EMA: a single jump must not flip CLOSE/FAR. */
    ph_trend_t tr;
    ph_trend_reset(&tr);
    ASSERT_EQ(PH_TREND_HOLD, (unsigned)ph_trend_burst(&tr, -70));
    ASSERT_EQ(PH_TREND_HOLD, (unsigned)ph_trend_burst(&tr, -50));

    ph_trend_reset(&tr);
    {
        const int8_t noisy[] = { -72, -68, -71, -69, -73, -70 };
        ASSERT_EQ(PH_TREND_HOLD, (unsigned)bursts(&tr, noisy, 6));
    }

    ph_trend_reset(&tr);
    {
        const int8_t closer[] = { -80, -76, -72, -68, -64, -60 };
        ASSERT_EQ(PH_TREND_CLOSE, (unsigned)bursts(&tr, closer, 6));
    }

    ph_trend_reset(&tr);
    {
        const int8_t away[] = { -50, -55, -60, -65, -70, -75 };
        ASSERT_EQ(PH_TREND_FAR, (unsigned)bursts(&tr, away, 6));
    }

    int8_t hist[PH_HIST];
    uint8_t hn = 0;
    memset(hist, 0, sizeof hist);
    for (int i = 0; i < (int)PH_HIST; i++) {
        ph_hist_push(hist, &hn, PH_HIST, (int8_t)(-90 + i));
    }
    ASSERT_EQ((unsigned)PH_HIST, (unsigned)hn);
    ASSERT_EQ((unsigned long long)(int)-90, (unsigned long long)(int)hist[0]);
    ASSERT_EQ((unsigned long long)(int)-67, (unsigned long long)(int)hist[PH_HIST - 1u]);
    ph_hist_push(hist, &hn, PH_HIST, 0);
    ASSERT_EQ((unsigned)PH_HIST, (unsigned)hn);
    ASSERT_EQ((unsigned long long)(int)-89, (unsigned long long)(int)hist[0]);
    ASSERT_EQ((unsigned long long)(int)0, (unsigned long long)(int)hist[PH_HIST - 1u]);

    TEST_RETURN();
}
