#include "test_util.h"
#include "pedometer.h"

static unsigned pulse(it_pedo_t *p, uint32_t t0, int32_t high) {
    unsigned n = 0;
    if (it_pedo_feed(p, high, t0)) n++;
    if (it_pedo_feed(p, high, t0 + 20u)) n++;
    if (it_pedo_feed(p, high, t0 + 40u)) n++;
    if (it_pedo_feed(p, 1000, t0 + 80u)) n++;
    return n;
}

int main(void) {
    ASSERT_EQ(it_mag_mg(0, 0, 0), 0);
    ASSERT_EQ(it_mag_mg(1000, 0, 0), 1000);
    ASSERT_EQ(it_mag_mg(0, 0, 1000), 1000);
    /* 3-4-5 triangle */
    ASSERT_EQ(it_mag_mg(300, 400, 0), 500);

    it_pedo_t p;
    it_pedo_reset(&p);
    ASSERT_TRUE(!it_pedo_cal_ready(&p));
    for (unsigned i = 0; i < IT_CAL_SAMPLES; i++) {
        it_pedo_cal_add(&p, 1000);
    }
    ASSERT_TRUE(it_pedo_cal_ready(&p));
    it_pedo_cal_finish(&p);
    ASSERT_EQ(p.rest_mg, 1000);

    /* A rest-level stream must not count. */
    uint32_t steps = 0;
    for (unsigned t = 0; t < 2000; t += 10) {
        if (it_pedo_feed(&p, 1000, t)) steps++;
    }
    ASSERT_EQ(steps, 0);

    /* Count on the falling edge. A bounce inside the peak is one step.
       A second peak inside the refractory is not. */
    it_pedo_reset(&p);
    p.rest_mg = 1000;
    ASSERT_EQ(pulse(&p, 0, 1300), 1);
    ASSERT_EQ(p.steps, 1);
    ASSERT_EQ(pulse(&p, 200, 1300), 0);
    ASSERT_EQ(pulse(&p, 500, 1300), 1);
    ASSERT_EQ(p.steps, 2);

    it_pedo_reset(&p);
    it_pedo_arm_residual(&p, 40);
    ASSERT_EQ(p.rest_mg, 0);
    ASSERT_TRUE(p.thresh_mg >= IT_DEFAULT_THRESH);
    ASSERT_TRUE(!it_pedo_feed(&p, 400, 0));
    ASSERT_TRUE(!it_pedo_feed(&p, 400, 20));
    ASSERT_TRUE(it_pedo_feed(&p, 0, 80));
    ASSERT_EQ(p.steps, 1);

    /* Ten walk-like peaks at ~500 ms. */
    it_pedo_reset(&p);
    p.rest_mg = 1000;
    unsigned walk = 0;
    for (unsigned i = 0; i < 10u; i++) {
        walk += pulse(&p, i * 500u, 1400);
    }
    ASSERT_EQ(walk, 10);
    ASSERT_EQ(p.steps, 10);

    it_scribble_t s;
    it_scribble_reset(&s);
    /* Constant accel piles up a position. Sign is not the point — motion is. */
    for (int i = 0; i < 20; i++) {
        it_scribble_feed(&s, 100, 0, 0.01f, false);
    }
    ASSERT_TRUE(s.px > 0.0f || s.px < 0.0f || s.vx != 0.0f);
    it_scribble_push_point(&s);
    ASSERT_EQ(s.n, 1);

    /* Step PDR: N strides of 70 cm along +X. */
    it_scribble_reset(&s);
    for (int i = 0; i < 10; i++) {
        it_scribble_step(&s, 200, 0, 70);
    }
    ASSERT_TRUE(s.px > 6.5f && s.px < 7.5f);
    ASSERT_TRUE(s.py > -0.2f && s.py < 0.2f);

    /* Hold-still cal seeds rest; samples inside the dead zone are still
       (residual 0) and leak the rest vector. A shove outside is motion. */
    it_rest_t r;
    it_rest_reset(&r);
    for (unsigned i = 0; i < IT_CAL_SAMPLES; i++) {
        it_rest_cal_add(&r, 10, -20, 1000);
    }
    ASSERT_TRUE(it_rest_cal_ready(&r));
    it_rest_cal_finish(&r);
    ASSERT_EQ(r.x, 10);
    ASSERT_EQ(r.y, -20);
    ASSERT_EQ(r.z, 1000);
    ASSERT_TRUE(r.dead_mg >= IT_DEAD_MIN_MG);

    int32_t ox, oy, oz;
    ASSERT_TRUE(!it_rest_apply(&r, 12, -18, 1002, &ox, &oy, &oz));
    ASSERT_EQ(ox, 0);
    ASSERT_EQ(oy, 0);
    ASSERT_EQ(oz, 0);

    ASSERT_TRUE(it_rest_apply(&r, 10, -20, 1400, &ox, &oy, &oz));
    ASSERT_TRUE(oz != 0);

    /* Integer EMA must still walk rest toward a small still offset. */
    it_rest_cal_finish(&r);
    r.x = 10;
    r.y = -20;
    r.z = 1000;
    r.dead_mg = 80;
    r.x_acc = r.y_acc = r.z_acc = r.dead_acc = 0;
    for (int i = 0; i < 80; i++) {
        ASSERT_TRUE(!it_rest_apply(&r, 30, -20, 1000, &ox, &oy, &oz));
    }
    ASSERT_TRUE(r.x > 10);

    it_scribble_reset(&s);
    s.vx = 1.0f;
    it_scribble_feed(&s, 100, 0, 0.01f, true);
    ASSERT_TRUE(s.vx < 1.0f);

    /* Weak XY (still tilt like X+44 Y−12) keeps default heading: up. */
    it_scribble_reset(&s);
    it_scribble_step(&s, 44, -12, 70);
    ASSERT_TRUE(s.px > -0.2f && s.px < 0.2f);
    ASSERT_TRUE(s.py > 0.6f && s.py < 0.8f);

    /* Sensor +Y is toward the buttons; plot up is sensor −Y. */
    it_scribble_reset(&s);
    it_scribble_step(&s, 0, -250, 70);
    ASSERT_TRUE(s.px > -0.2f && s.px < 0.2f);
    ASSERT_TRUE(s.py > 0.6f && s.py < 0.8f);

    it_scribble_reset(&s);
    it_scribble_step(&s, 0, 250, 70);
    ASSERT_TRUE(s.py < -0.6f && s.py > -0.8f);

    /* Heading latches at the peak, not the falling-edge sample. */
    it_pedo_reset(&p);
    it_pedo_arm_residual(&p, 40);
    ASSERT_TRUE(!it_pedo_feed_xyz(&p, 0, -40, 400, 0));
    ASSERT_TRUE(!it_pedo_feed_xyz(&p, 250, 0, 500, 20));
    ASSERT_TRUE(it_pedo_feed_xyz(&p, 0, -20, 0, 80));
    ASSERT_EQ(p.peak_ax, 250);
    ASSERT_EQ(p.peak_ay, 0);

    TEST_RETURN();
}
