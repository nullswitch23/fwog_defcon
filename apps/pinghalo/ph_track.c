#include "ph_track.h"
#include <string.h>

void ph_table_init(ph_table_t *t) {
    ph_table_clear(t);
}

void ph_table_clear(ph_table_t *t) {
    if (!t) return;
    memset(t, 0, sizeof *t);
}

static void sort_rssi(ph_table_t *t) {
    for (unsigned i = 0; i < t->n; i++) {
        for (unsigned j = i + 1u; j < t->n; j++) {
            if (t->dev[j].rssi > t->dev[i].rssi) {
                ph_dev_t d = t->dev[i];
                uint32_t s = t->seen_ms[i];
                t->dev[i] = t->dev[j];
                t->seen_ms[i] = t->seen_ms[j];
                t->dev[j] = d;
                t->seen_ms[j] = s;
            }
        }
    }
}

void ph_table_upsert(ph_table_t *t, uint32_t now_ms, int8_t rssi,
                     uint8_t apple, const uint8_t addr[6], const char *name) {
    if (!t || !addr) return;
    int found = -1;
    for (unsigned i = 0; i < t->n; i++) {
        if (memcmp(t->dev[i].addr, addr, 6) == 0) {
            found = (int)i;
            break;
        }
    }
    if (found < 0) {
        if (t->n < PH_STORE) {
            found = (int)t->n++;
        } else {
            found = 0;
            for (unsigned i = 1; i < PH_STORE; i++) {
                if (t->dev[i].rssi < t->dev[found].rssi) found = (int)i;
            }
            if (rssi < t->dev[found].rssi) return;
        }
    }
    t->dev[found].rssi = rssi;
    t->dev[found].apple = apple ? 1u : 0u;
    memcpy(t->dev[found].addr, addr, 6);
    memset(t->dev[found].name, 0, PH_NAME_N);
    if (name && name[0] && !(name[0] == '-' && name[1] == '\0')) {
        strncpy(t->dev[found].name, name, PH_NAME_N - 1u);
    }
    t->seen_ms[found] = now_ms;
    sort_rssi(t);
}

void ph_table_age(ph_table_t *t, uint32_t now_ms, uint32_t stale_ms,
                  const uint8_t keep[6]) {
    if (!t) return;
    unsigned w = 0;
    for (unsigned i = 0; i < t->n; i++) {
        const bool pinned = keep && memcmp(t->dev[i].addr, keep, 6) == 0;
        if (!pinned && (now_ms - t->seen_ms[i]) > stale_ms) continue;
        if (w != i) {
            t->dev[w] = t->dev[i];
            t->seen_ms[w] = t->seen_ms[i];
        }
        w++;
    }
    if (w < t->n) {
        memset(t->dev + w, 0, sizeof(t->dev[0]) * (t->n - w));
        memset(t->seen_ms + w, 0, sizeof(t->seen_ms[0]) * (t->n - w));
    }
    t->n = w;
}

const ph_dev_t *ph_table_find(const ph_table_t *t, const uint8_t addr[6]) {
    if (!t || !addr) return NULL;
    for (unsigned i = 0; i < t->n; i++) {
        if (memcmp(t->dev[i].addr, addr, 6) == 0) return &t->dev[i];
    }
    return NULL;
}

bool ph_dev_match(const ph_dev_t *d, uint8_t filter) {
    if (!d) return false;
    if (filter == PH_FILT_NAMED) return d->name[0] != '\0';
    if (filter == PH_FILT_APPLE) return d->apple != 0u;
    return true;
}

unsigned ph_table_match_count(const ph_table_t *t, uint8_t filter) {
    if (!t) return 0;
    unsigned n = 0;
    for (unsigned i = 0; i < t->n; i++) {
        if (ph_dev_match(&t->dev[i], filter)) n++;
    }
    return n;
}

unsigned ph_table_window(const ph_table_t *t, uint8_t filter, unsigned scroll,
                         ph_dev_t *out, unsigned max, const uint8_t pin[6]) {
    if (!t || !out || max == 0) return 0;
    unsigned wrote = 0;
    if (pin) {
        for (unsigned i = 0; i < t->n; i++) {
            if (memcmp(t->dev[i].addr, pin, 6) != 0) continue;
            if (!ph_dev_match(&t->dev[i], filter)) break;
            out[wrote++] = t->dev[i];
            break;
        }
    }
    unsigned skipped = 0;
    for (unsigned i = 0; i < t->n && wrote < max; i++) {
        if (pin && memcmp(t->dev[i].addr, pin, 6) == 0) continue;
        if (!ph_dev_match(&t->dev[i], filter)) continue;
        if (skipped < scroll) {
            skipped++;
            continue;
        }
        out[wrote++] = t->dev[i];
    }
    return wrote;
}

unsigned ph_snap_window(const ph_dev_t *snap, unsigned nsnap, unsigned scroll,
                        ph_dev_t *out, unsigned max) {
    if (!snap || !out || max == 0) return 0;
    unsigned wrote = 0;
    for (unsigned i = scroll; i < nsnap && wrote < max; i++) {
        out[wrote++] = snap[i];
    }
    return wrote;
}

void ph_trend_reset(ph_trend_t *s) {
    if (!s) return;
    memset(s, 0, sizeof *s);
}

static int8_t peak3(const int8_t *v) {
    int8_t p = v[0];
    if (v[1] > p) p = v[1];
    if (v[2] > p) p = v[2];
    return p;
}

uint8_t ph_trend_burst(ph_trend_t *s, int8_t rssi) {
    if (!s) return PH_TREND_UNK;
    if (s->n < PH_BURST_N) {
        s->burst[s->n++] = rssi;
    } else {
        memmove(s->burst, s->burst + 1, (PH_BURST_N - 1u) * sizeof s->burst[0]);
        s->burst[PH_BURST_N - 1u] = rssi;
    }
    if (s->n < PH_BURST_N) return PH_TREND_HOLD;
    const int8_t p_new = peak3(&s->burst[3]);
    const int8_t p_old = peak3(&s->burst[0]);
    if ((int)p_new >= (int)p_old + PH_DEADBAND_DB) return PH_TREND_CLOSE;
    if ((int)p_new <= (int)p_old - PH_DEADBAND_DB) return PH_TREND_FAR;
    return PH_TREND_HOLD;
}

void ph_hist_push(int8_t *hist, uint8_t *n, unsigned cap, int8_t rssi) {
    if (!hist || !n || cap == 0) return;
    if (*n < cap) {
        hist[(*n)++] = rssi;
        return;
    }
    memmove(hist, hist + 1, (cap - 1u) * sizeof hist[0]);
    hist[cap - 1u] = rssi;
}
