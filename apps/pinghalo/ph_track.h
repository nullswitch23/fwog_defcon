#ifndef PH_TRACK_H
#define PH_TRACK_H

#include "ph_proto.h"
#include <stdbool.h>
#include <stdint.h>

#define PH_STORE    16u
#define PH_STALE_MS 8000u
#define PH_BURST_N  6u

typedef struct {
    ph_dev_t dev[PH_STORE];
    uint32_t seen_ms[PH_STORE];
    unsigned n;
} ph_table_t;

void ph_table_init(ph_table_t *t);
void ph_table_clear(ph_table_t *t);
void ph_table_upsert(ph_table_t *t, uint32_t now_ms, int8_t rssi,
                     uint8_t apple, const uint8_t addr[6], const char *name);
void ph_table_age(ph_table_t *t, uint32_t now_ms, uint32_t stale_ms,
                  const uint8_t keep[6]);
bool ph_dev_match(const ph_dev_t *d, uint8_t filter);
unsigned ph_table_match_count(const ph_table_t *t, uint8_t filter);
/* Fill up to `max` devices matching `filter`, skipping `scroll` matches.
 * Sorted by RSSI descending. If `pin` is non-NULL that address is row 0
 * (when it matches the filter) and does not count against scroll. */
unsigned ph_table_window(const ph_table_t *t, uint8_t filter, unsigned scroll,
                         ph_dev_t *out, unsigned max, const uint8_t pin[6]);
/* Page `scroll` into a frozen MAC list. Identities do not come from `t`. */
unsigned ph_snap_window(const ph_dev_t *snap, unsigned nsnap, unsigned scroll,
                        ph_dev_t *out, unsigned max);
const ph_dev_t *ph_table_find(const ph_table_t *t, const uint8_t addr[6]);

typedef struct {
    int8_t  burst[PH_BURST_N];
    uint8_t n;
} ph_trend_t;

void ph_trend_reset(ph_trend_t *s);
/* One Find My / ADV burst. Compares peak of last 3 bursts to the 3 before.
 * Silence between bursts must not be passed in. */
uint8_t ph_trend_burst(ph_trend_t *s, int8_t rssi);

void ph_hist_push(int8_t *hist, uint8_t *n, unsigned cap, int8_t rssi);

#endif
