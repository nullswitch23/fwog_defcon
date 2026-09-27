#include "pedometer.h"
#include <math.h>

static int32_t it_clamp_dead(int32_t v) {
    if (v < IT_DEAD_MIN_MG) return IT_DEAD_MIN_MG;
    if (v > IT_DEAD_MAX_MG) return IT_DEAD_MAX_MG;
    return v;
}

static int32_t it_clamp_thresh(int32_t v) {
    if (v < IT_THRESH_MIN_MG) return IT_THRESH_MIN_MG;
    if (v > IT_THRESH_MAX_MG) return IT_THRESH_MAX_MG;
    return v;
}

static void it_leak(int32_t *rest, int32_t *acc, int32_t sample) {
    *acc += sample - *rest;
    const int32_t d = *acc / (int32_t)IT_REST_EMA;
    *rest += d;
    *acc -= d * (int32_t)IT_REST_EMA;
}

int32_t it_mag_mg(int32_t x_mg, int32_t y_mg, int32_t z_mg) {
    const float m = sqrtf((float)x_mg * (float)x_mg +
                          (float)y_mg * (float)y_mg +
                          (float)z_mg * (float)z_mg);
    return (int32_t)(m + 0.5f);
}

void it_pedo_reset(it_pedo_t *p) {
    p->mag_sum = 0;
    p->mag_n = 0;
    p->rest_mg = 1000;
    p->thresh_mg = IT_DEFAULT_THRESH;
    p->peak_dyn = 0;
    p->peak_ax = 0;
    p->peak_ay = 0;
    p->last_step_ms = 0;
    p->have_step = false;
    p->in_peak = false;
    p->steps = 0;
}

void it_pedo_cal_add(it_pedo_t *p, int32_t mag_mg) {
    if (p->mag_n >= IT_CAL_SAMPLES) return;
    p->mag_sum += mag_mg;
    p->mag_n++;
}

bool it_pedo_cal_ready(const it_pedo_t *p) {
    return p->mag_n >= IT_CAL_SAMPLES;
}

void it_pedo_cal_finish(it_pedo_t *p) {
    if (p->mag_n == 0u) {
        p->rest_mg = 1000;
        return;
    }
    p->rest_mg = (int32_t)(p->mag_sum / (int64_t)p->mag_n);
    if (p->rest_mg < 200) p->rest_mg = 200;
}

void it_pedo_arm_residual(it_pedo_t *p, int32_t dead_mg) {
    p->rest_mg = 0;
    p->peak_dyn = 0;
    p->peak_ax = 0;
    p->peak_ay = 0;
    p->in_peak = false;
    int32_t t = dead_mg * 2;
    if (t < IT_DEFAULT_THRESH) t = IT_DEFAULT_THRESH;
    p->thresh_mg = it_clamp_thresh(t);
}

bool it_pedo_feed(it_pedo_t *p, int32_t mag_mg, uint32_t now_ms) {
    return it_pedo_feed_xyz(p, 0, 0, mag_mg, now_ms);
}

bool it_pedo_feed_xyz(it_pedo_t *p, int32_t rx, int32_t ry, int32_t rz,
                      uint32_t now_ms) {
    const int32_t mag_mg = it_mag_mg(rx, ry, rz);
    const int32_t dyn = mag_mg - p->rest_mg;
    const int32_t low = p->thresh_mg / 2;
    if (!p->in_peak) {
        if (dyn > p->thresh_mg) {
            p->in_peak = true;
            p->peak_dyn = dyn;
            p->peak_ax = rx;
            p->peak_ay = ry;
        }
        return false;
    }
    if (dyn > p->peak_dyn) {
        p->peak_dyn = dyn;
        p->peak_ax = rx;
        p->peak_ay = ry;
    }
    if (dyn >= low) return false;

    p->in_peak = false;
    if (p->have_step && (now_ms - p->last_step_ms) < IT_REFRACTORY_MS) {
        return false;
    }
    p->have_step = true;
    p->last_step_ms = now_ms;
    p->steps++;
    if (p->steps >= 2u) {
        const int32_t want = it_clamp_thresh(p->peak_dyn / 2);
        p->thresh_mg += (want - p->thresh_mg) / 4;
        p->thresh_mg = it_clamp_thresh(p->thresh_mg);
    }
    return true;
}

void it_rest_reset(it_rest_t *r) {
    r->x = r->y = 0;
    r->z = 1000;
    r->dead_mg = IT_DEAD_MIN_MG + IT_DEAD_PAD_MG;
    r->x_acc = r->y_acc = r->z_acc = r->dead_acc = 0;
    r->sx = r->sy = r->sz = 0;
    r->minx = r->maxx = 0;
    r->miny = r->maxy = 0;
    r->minz = r->maxz = 0;
    r->n = 0;
}

void it_rest_cal_add(it_rest_t *r, int32_t x, int32_t y, int32_t z) {
    if (r->n >= IT_CAL_SAMPLES) return;
    if (r->n == 0u) {
        r->minx = r->maxx = x;
        r->miny = r->maxy = y;
        r->minz = r->maxz = z;
    } else {
        if (x < r->minx) r->minx = x;
        if (x > r->maxx) r->maxx = x;
        if (y < r->miny) r->miny = y;
        if (y > r->maxy) r->maxy = y;
        if (z < r->minz) r->minz = z;
        if (z > r->maxz) r->maxz = z;
    }
    r->sx += x;
    r->sy += y;
    r->sz += z;
    r->n++;
}

bool it_rest_cal_ready(const it_rest_t *r) {
    return r->n >= IT_CAL_SAMPLES;
}

void it_rest_cal_finish(it_rest_t *r) {
    if (r->n == 0u) {
        r->x = r->y = 0;
        r->z = 1000;
        r->dead_mg = it_clamp_dead(IT_DEAD_MIN_MG + IT_DEAD_PAD_MG);
        r->x_acc = r->y_acc = r->z_acc = r->dead_acc = 0;
        return;
    }
    r->x = (int32_t)(r->sx / (int64_t)r->n);
    r->y = (int32_t)(r->sy / (int64_t)r->n);
    r->z = (int32_t)(r->sz / (int64_t)r->n);
    int32_t scatter = r->maxx - r->minx;
    int32_t dy = r->maxy - r->miny;
    int32_t dz = r->maxz - r->minz;
    if (dy > scatter) scatter = dy;
    if (dz > scatter) scatter = dz;
    r->dead_mg = it_clamp_dead((scatter / 2) + IT_DEAD_PAD_MG);
    r->x_acc = r->y_acc = r->z_acc = r->dead_acc = 0;
}

bool it_rest_apply(it_rest_t *r, int32_t x, int32_t y, int32_t z,
                   int32_t *ox, int32_t *oy, int32_t *oz) {
    const int32_t dx = x - r->x;
    const int32_t dy = y - r->y;
    const int32_t dz = z - r->z;
    const int32_t rmag = it_mag_mg(dx, dy, dz);
    if (rmag < r->dead_mg) {
        it_leak(&r->x, &r->x_acc, x);
        it_leak(&r->y, &r->y_acc, y);
        it_leak(&r->z, &r->z_acc, z);
        const int32_t want = it_clamp_dead((rmag * 3) + IT_DEAD_PAD_MG);
        it_leak(&r->dead_mg, &r->dead_acc, want);
        r->dead_mg = it_clamp_dead(r->dead_mg);
        if (ox) *ox = 0;
        if (oy) *oy = 0;
        if (oz) *oz = 0;
        return false;
    }
    if (ox) *ox = dx;
    if (oy) *oy = dy;
    if (oz) *oz = dz;
    return true;
}

void it_scribble_reset(it_scribble_t *s) {
    s->vx = s->vy = 0.0f;
    s->px = s->py = 0.0f;
    s->hx = 0.0f;
    s->hy = 1.0f;
    s->have_h = false;
    s->n = 0;
    s->i = 0;
    s->wrapped = false;
}

void it_scribble_feed(it_scribble_t *s, int32_t ax_mg, int32_t ay_mg,
                      float dt_s, bool still) {
    if (still) {
        s->vx *= 0.80f;
        s->vy *= 0.80f;
        if (s->vx > -0.02f && s->vx < 0.02f) s->vx = 0.0f;
        if (s->vy > -0.02f && s->vy < 0.02f) s->vy = 0.0f;
        return;
    }
    const float ax = (float)ax_mg * 0.00981f;
    const float ay = (float)ay_mg * 0.00981f;
    s->vx += ax * dt_s;
    s->vy += ay * dt_s;
    s->px += s->vx * dt_s;
    s->py += s->vy * dt_s;
}

void it_scribble_step(it_scribble_t *s, int32_t ax_mg, int32_t ay_mg,
                      int32_t stride_cm) {
    if (stride_cm < 1) stride_cm = 1;
    /* Screen up is sensor −Y: a still board reads ~Y−12, and a walking
     * pitch dumps gravity toward the buttons (sensor +Y / screen down). */
    int32_t ax = ax_mg;
    int32_t ay = -ay_mg;
    int32_t nx = 1;
    int32_t ny = 1;
    if (ax < 0) {
        ax = -ax;
        nx = -1;
    }
    if (ay < 0) {
        ay = -ay;
        ny = -1;
    }
    float hx;
    float hy;
    if (ax < IT_HEADING_MIN_MG && ay < IT_HEADING_MIN_MG) {
        hx = s->hx;
        hy = s->hy;
    } else if (ax > (ay * 2)) {
        hx = (float)nx;
        hy = 0.0f;
    } else if (ay > (ax * 2)) {
        hx = 0.0f;
        hy = (float)ny;
    } else {
        hx = (float)nx * 0.70710678f;
        hy = (float)ny * 0.70710678f;
    }
    s->hx = hx;
    s->hy = hy;
    s->have_h = true;
    const float ds = (float)stride_cm / 100.0f;
    s->px += hx * ds;
    s->py += hy * ds;
    s->vx = s->vy = 0.0f;
}

void it_scribble_push_point(it_scribble_t *s) {
    int32_t x = (int32_t)(s->px * 100.0f);
    int32_t y = (int32_t)(s->py * 100.0f);
    if (x > 32767) x = 32767;
    if (x < -32768) x = -32768;
    if (y > 32767) y = 32767;
    if (y < -32768) y = -32768;
    s->x[s->i] = (int16_t)x;
    s->y[s->i] = (int16_t)y;
    s->i++;
    if (s->i >= IT_SCRIBBLE_N) {
        s->i = 0;
        s->wrapped = true;
    }
    if (!s->wrapped) s->n = s->i;
}
