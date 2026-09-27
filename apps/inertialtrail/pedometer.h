/* Software pedometer, rest/dead-zone tracker, and a heading-less scribble.
 *
 * Pure: no SDK. The LIS3DH interrupt pins are not wired, so steps are
 * peaks in residual acceleration after a still calibration. Count on the
 * falling edge of a peak (one footfall, not every threshold chatter).
 * The rest vector keeps tracking while the board is inside the dead zone.
 */
#ifndef IT_PEDOMETER_H
#define IT_PEDOMETER_H
#include <stdbool.h>
#include <stdint.h>

#define IT_CAL_SAMPLES     120u
#define IT_DEFAULT_THRESH  180   /* milli-g of residual, a step */
#define IT_THRESH_MIN_MG   80
#define IT_THRESH_MAX_MG   280
#define IT_REFRACTORY_MS   380u  /* ~158 spm ceiling; bounce is faster */
#define IT_SCRIBBLE_N      120u
#define IT_DEAD_MIN_MG     40
#define IT_DEAD_MAX_MG     160
#define IT_DEAD_PAD_MG     24    /* added to hold-still scatter */
#define IT_REST_EMA        64    /* rest += (sample - rest) / 64 */
#define IT_HEADING_MIN_MG  180   /* ignore weaker XY; keep last heading */

typedef struct {
    int64_t  mag_sum;
    unsigned mag_n;
    int32_t  rest_mg;
    int32_t  thresh_mg;
    int32_t  peak_dyn;
    int32_t  peak_ax, peak_ay; /* residual XY at the peak, not the valley */
    uint32_t last_step_ms;
    bool     have_step;
    bool     in_peak;
    uint32_t steps;
} it_pedo_t;

/* Gravity/bias vector plus a dead zone around it. */
typedef struct {
    int32_t  x, y, z;          /* rest, milli-g */
    int32_t  dead_mg;          /* half-width, milli-g of residual mag */
    int32_t  x_acc, y_acc, z_acc, dead_acc; /* EMA leftovers */
    int64_t  sx, sy, sz;
    int32_t  minx, maxx, miny, maxy, minz, maxz;
    unsigned n;
} it_rest_t;

typedef struct {
    float    vx, vy;
    float    px, py;
    float    hx, hy;       /* last step heading, unitless */
    bool     have_h;
    int16_t  x[IT_SCRIBBLE_N];
    int16_t  y[IT_SCRIBBLE_N];
    unsigned n;
    unsigned i;           /* next write, ring once full */
    bool     wrapped;
} it_scribble_t;

int32_t it_mag_mg(int32_t x_mg, int32_t y_mg, int32_t z_mg);

void it_pedo_reset(it_pedo_t *p);
void it_pedo_cal_add(it_pedo_t *p, int32_t mag_mg);
bool it_pedo_cal_ready(const it_pedo_t *p);
void it_pedo_cal_finish(it_pedo_t *p);
/* Residual peaks: rest_mg 0, thresh from the still dead zone. */
void it_pedo_arm_residual(it_pedo_t *p, int32_t dead_mg);
/* True when this sample closed a step peak. mag-only keeps tests simple. */
bool it_pedo_feed(it_pedo_t *p, int32_t mag_mg, uint32_t now_ms);
/* Same detector; latches residual XY at the peak of the bounce. */
bool it_pedo_feed_xyz(it_pedo_t *p, int32_t rx, int32_t ry, int32_t rz,
                      uint32_t now_ms);

void it_rest_reset(it_rest_t *r);
void it_rest_cal_add(it_rest_t *r, int32_t x, int32_t y, int32_t z);
bool it_rest_cal_ready(const it_rest_t *r);
void it_rest_cal_finish(it_rest_t *r);
/* Subtract rest. If residual mag is inside the dead zone, treat as still:
   leak rest toward the sample, zero the outputs, return false. */
bool it_rest_apply(it_rest_t *r, int32_t x, int32_t y, int32_t z,
                   int32_t *ox, int32_t *oy, int32_t *oz);

void it_scribble_reset(it_scribble_t *s);
/* still: zero-velocity update, do not integrate this sample. */
void it_scribble_feed(it_scribble_t *s, int32_t ax_mg, int32_t ay_mg,
                      float dt_s, bool still);
/* One stride. Heading from peak residual XY; sensor +Y is toward the
 * buttons so it is flipped onto the plot (screen up = sensor −Y). Weak
 * XY keeps the last heading (default: up the screen). */
void it_scribble_step(it_scribble_t *s, int32_t ax_mg, int32_t ay_mg,
                      int32_t stride_cm);
void it_scribble_push_point(it_scribble_t *s);

#endif
