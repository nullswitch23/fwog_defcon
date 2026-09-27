#include "test_util.h"
#include "battlebridge_game.h"
#include <stdbool.h>
#include <string.h>

static void ready_all(bb_game_t *g, unsigned n) {
    bb_input_t in;
    memset(&in, 0, sizeof in);
    in.ready = 1;
    for (unsigned i = 0; i < n; i++) bb_game_input(g, i, &in);
}

static void play(bb_game_t *g) {
    ready_all(g, BB_PLAYERS);
    bb_game_step(g);
    ASSERT_EQ(g->phase, BB_PHASE_COUNTDOWN);
    for (unsigned i = 0; i < 90u; i++) bb_game_step(g);
    ASSERT_EQ(g->phase, BB_PHASE_PLAY);
}

static void test_four_player_start_and_gate(void) {
    bb_game_t g;
    bb_game_init(&g, 1234);
    for (unsigned i = 0; i < BB_PLAYERS; i++) {
        ASSERT_EQ(bb_game_connect(&g), (int)i);
        ASSERT_EQ(g.p[i].color, (uint8_t)i);
        ASSERT_TRUE(g.p[i].gate_c < 6u);
    }
    ASSERT_EQ(bb_game_active(&g), 4u);
    ASSERT_EQ(bb_game_connect(&g), -1);
    play(&g);
    ASSERT_TRUE(g.nwall >= 5u);

    g.p[0].x = (int32_t)g.p[0].gate_x * 256;
    g.p[0].y = (int32_t)g.p[0].gate_y * 256;
    const uint8_t old_c = g.p[0].gate_c;
    bb_game_step(&g);
    ASSERT_EQ(g.p[0].gates, 1u);
    ASSERT_EQ(g.p[0].score, 25);
    ASSERT_TRUE(g.p[0].gate_x != 0u || g.p[0].gate_y != 0u);
    (void)old_c;
}

static void test_fire_enemy_and_random_respawn(void) {
    bb_game_t g;
    bb_game_init(&g, 99);
    ASSERT_EQ(bb_game_connect(&g), 0);
    ASSERT_EQ(bb_game_connect(&g), 1);
    bb_game_force_start(&g);
    for (unsigned i = 0; i < 90u; i++) bb_game_step(&g);

    bb_input_t in = { .aim_x = 127, .fire = 1 };
    bb_game_input(&g, 0, &in);
    bb_game_step(&g);
    ASSERT_TRUE(bb_game_projectiles(&g) > 0u);

    g.p[1].hp = 0;
    g.p[1].respawn = 1;
    const int32_t old_x = g.p[1].x;
    const int32_t old_y = g.p[1].y;
    bb_game_step(&g);
    ASSERT_EQ(g.p[1].hp, 100u);
    ASSERT_TRUE(g.p[1].invul > 0u);
    ASSERT_TRUE(g.p[1].x != old_x || g.p[1].y != old_y);
}

static void test_walls_block_and_round_reroll(void) {
    bb_game_t g;
    bb_game_init(&g, 7);
    bb_game_connect(&g);
    bb_game_connect(&g);
    bb_game_force_start(&g);
    ASSERT_TRUE(g.nwall >= 5u);
    const bb_wall_t first = g.wall[0];
    ASSERT_TRUE(first.w > 0u && first.h > 0u);

    g.phase = BB_PHASE_PLAY;
    g.p[0].hp = 100;
    g.p[0].invul = 0;
    /* Drive at the first wall from its left face. Sliding is allowed;
     * tunneling through the brick is not. */
    ASSERT_TRUE(first.x > 30u);
    g.p[0].x = (int32_t)(first.x - 18u) * 256;
    g.p[0].y = (int32_t)(first.y + first.h / 2u) * 256;
    g.p[0].vx = 5 * 256;
    g.p[0].vy = 0;
    g.p[0].move_x = 127;
    g.p[0].move_y = 0;
    g.p[0].fire = 0;
    for (unsigned i = 0; i < 15u; i++) bb_game_step(&g);
    ASSERT_TRUE((int)(g.p[0].x / 256) < (int)first.x + 10);

    g.phase = BB_PHASE_RESULTS;
    g.phase_ticks = 1;
    bb_game_step(&g);
    ASSERT_EQ(g.phase, BB_PHASE_LOBBY);
    ASSERT_EQ(g.nwall, 0u);
    bb_game_force_start(&g);
    ASSERT_TRUE(g.nwall >= 5u);
    ASSERT_TRUE(g.wall[0].x != first.x || g.wall[0].y != first.y ||
                g.wall[0].w != first.w || g.wall[0].h != first.h);
}

static void test_pickups_and_spray(void) {
    bb_game_t g;
    bb_game_init(&g, 42);
    bb_game_connect(&g);
    bb_game_connect(&g);
    bb_game_force_start(&g);
    for (unsigned i = 0; i < 90u; i++) bb_game_step(&g);

    g.p[0].hp = 40;
    g.pk[0].active = 1;
    g.pk[0].kind = BB_PK_HEALTH;
    g.pk[0].x = (uint16_t)(g.p[0].x / 256);
    g.pk[0].y = (uint16_t)(g.p[0].y / 256);
    bb_game_step(&g);
    ASSERT_EQ(g.p[0].hp, 80u);
    ASSERT_EQ(g.pk[0].active, 0u);

    g.p[0].hp = 80;
    g.pk[1].active = 1;
    g.pk[1].kind = BB_PK_SHIELD;
    g.pk[1].x = (uint16_t)(g.p[0].x / 256);
    g.pk[1].y = (uint16_t)(g.p[0].y / 256);
    bb_game_step(&g);
    ASSERT_TRUE(g.p[0].shield > 0u);

    memset(g.shot, 0, sizeof g.shot);
    g.p[0].weap = BB_WEAP_SPRAY;
    g.p[0].weap_ttl = 100;
    g.p[0].cooldown = 0;
    g.p[0].fire = 1;
    g.p[0].aim_x = 127;
    g.p[0].aim_y = 0;
    bb_game_step(&g);
    ASSERT_TRUE(bb_game_projectiles(&g) >= 5u);

    g.pk[2].active = 1;
    g.pk[2].kind = BB_PK_GRIP;
    g.pk[2].x = (uint16_t)(g.p[0].x / 256);
    g.pk[2].y = (uint16_t)(g.p[0].y / 256);
    bb_game_step(&g);
    ASSERT_TRUE(g.p[0].grip_ttl > 0u);
}

static void test_bots_fill_and_human_steal(void) {
    bb_game_t g;
    bb_game_init(&g, 3);
    bb_game_set_bots(&g, BB_BOTS_NORMAL);
    bb_game_step(&g);
    ASSERT_EQ(bb_game_active(&g), 4u);
    for (unsigned i = 0; i < BB_PLAYERS; i++) ASSERT_EQ(g.p[i].bot, 1u);
    ASSERT_EQ(bb_game_connect(&g), 0);
    ASSERT_EQ(g.p[0].bot, 0u);
    ASSERT_EQ(g.p[1].bot, 1u);
    ASSERT_EQ(g.phase, BB_PHASE_LOBBY);
    bb_game_force_start(&g);
    ASSERT_EQ(g.phase, BB_PHASE_COUNTDOWN);
}

static void test_laser_bounces_pea_dies(void) {
    bb_game_t g;
    bb_game_init(&g, 11);
    bb_game_connect(&g);
    bb_game_connect(&g);
    bb_game_force_start(&g);
    for (unsigned i = 0; i < 90u; i++) bb_game_step(&g);
    memset(g.wall, 0, sizeof g.wall);
    memset(g.e, 0, sizeof g.e);
    memset(g.shot, 0, sizeof g.shot);
    g.nwall = 1;
    g.wall[0].x = 400;
    g.wall[0].y = 80;
    g.wall[0].w = 40;
    g.wall[0].h = 440;
    g.p[0].x = 200 * 256;
    g.p[0].y = 300 * 256;
    g.p[0].hp = 100;
    g.p[0].invul = 0;
    g.p[0].weap = BB_WEAP_LASER;
    g.p[0].weap_ttl = 200;
    g.p[0].cooldown = 0;
    g.p[0].fire = 1;
    g.p[0].aim_x = 127;
    g.p[0].aim_y = 0;
    g.p[0].move_x = 0;
    g.p[0].move_y = 0;
    g.p[0].vx = 0;
    g.p[0].vy = 0;
    g.p[1].x = 880 * 256;
    g.p[1].y = 40 * 256;
    g.p[1].invul = 255;
    g.tick = 1;
    memset(g.e, 0, sizeof g.e);
    bb_game_step(&g);
    ASSERT_TRUE(bb_game_projectiles(&g) > 0u);
    int16_t vx0 = 0;
    for (unsigned i = 0; i < BB_PROJECTILES; i++) {
        if (g.shot[i].active) {
            vx0 = g.shot[i].vx;
            break;
        }
    }
    ASSERT_TRUE(vx0 > 0);
    bool bounced = false;
    for (unsigned t = 0; t < 80u; t++) {
        g.p[0].fire = 0;
        bb_game_step(&g);
        for (unsigned i = 0; i < BB_PROJECTILES; i++) {
            if (g.shot[i].active && g.shot[i].vx < 0) bounced = true;
        }
        if (bounced) break;
    }
    ASSERT_TRUE(bounced);

    memset(g.shot, 0, sizeof g.shot);
    g.p[0].weap = BB_WEAP_PEA;
    g.p[0].weap_ttl = 0;
    g.p[0].cooldown = 0;
    g.p[0].fire = 1;
    bb_game_step(&g);
    ASSERT_TRUE(bb_game_projectiles(&g) > 0u);
    for (unsigned t = 0; t < 80u; t++) {
        g.p[0].fire = 0;
        bb_game_step(&g);
    }
    ASSERT_EQ(bb_game_projectiles(&g), 0u);
}

static void test_match_clock_and_sudden(void) {
    bb_game_t g;
    bb_game_init(&g, 21);
    bb_game_connect(&g);
    bb_game_connect(&g);
    play(&g);
    ASSERT_EQ(g.phase_ticks, (uint16_t)BB_MATCH_TICKS);
    g.p[0].gates = 4;
    g.p[1].gates = 1;
    g.phase_ticks = 0;
    bb_game_step(&g);
    ASSERT_EQ(g.phase, BB_PHASE_RESULTS);

    bb_game_init(&g, 22);
    bb_game_connect(&g);
    bb_game_connect(&g);
    play(&g);
    g.p[0].gates = 3;
    g.p[1].gates = 3;
    g.phase_ticks = 0;
    bb_game_step(&g);
    ASSERT_EQ(g.phase, BB_PHASE_PLAY);
    ASSERT_EQ(g.sudden, 1u);
    g.p[0].x = (int32_t)g.p[0].gate_x * 256;
    g.p[0].y = (int32_t)g.p[0].gate_y * 256;
    bb_game_step(&g);
    ASSERT_EQ(g.phase, BB_PHASE_RESULTS);
}

static void test_ice_and_grip(void) {
    bb_game_t g;
    bb_game_init(&g, 8);
    bb_game_connect(&g);
    bb_game_connect(&g);
    play(&g);
    g.p[0].vx = 6 * 256;
    g.p[0].vy = 0;
    g.p[0].move_x = 0;
    g.p[0].move_y = 0;
    g.p[0].fire = 0;
    g.p[0].grip_ttl = 0;
    for (unsigned i = 0; i < 6u; i++) bb_game_step(&g);
    ASSERT_TRUE(g.p[0].vx > 2 * 256);

    g.p[0].vx = 6 * 256;
    g.p[0].vy = 0;
    g.p[0].grip_ttl = 200;
    for (unsigned i = 0; i < 6u; i++) bb_game_step(&g);
    ASSERT_TRUE(g.p[0].vx > -256 && g.p[0].vx < 256);
}

static void test_bot_slides_off_wall(void) {
    bb_game_t g;
    bb_game_init(&g, 9);
    bb_game_set_bots(&g, BB_BOTS_NORMAL);
    bb_game_force_start(&g);
    for (unsigned i = 0; i < 90u; i++) bb_game_step(&g);
    memset(g.wall, 0, sizeof g.wall);
    memset(g.e, 0, sizeof g.e);
    memset(g.shot, 0, sizeof g.shot);
    g.nwall = 1;
    g.wall[0].x = 400;
    g.wall[0].y = 80;
    g.wall[0].w = 40;
    g.wall[0].h = 440;
    g.p[0].x = 386 * 256;
    g.p[0].y = 300 * 256;
    g.p[0].vx = 4 * 256;
    g.p[0].vy = 0;
    g.p[0].gate_x = 700;
    g.p[0].gate_y = 300;
    g.p[0].hp = 100;
    g.p[0].invul = 255;
    g.p[0].fire = 0;
    ASSERT_EQ(g.p[0].bot, 1u);
    const int y0 = (int)(g.p[0].y / 256);
    for (unsigned i = 0; i < 50u; i++) bb_game_step(&g);
    const int y1 = (int)(g.p[0].y / 256);
    const int x1 = (int)(g.p[0].x / 256);
    ASSERT_EQ(g.p[0].bot, 1u);
    const int wy0 = (int)g.wall[0].y;
    const int wy1 = wy0 + (int)g.wall[0].h;
    if (y1 >= wy0 && y1 <= wy1) {
        ASSERT_TRUE(x1 < (int)g.wall[0].x + 25);
    }
    ASSERT_TRUE(y1 != y0 || x1 > (int)g.wall[0].x + (int)g.wall[0].w);
}

static void test_bot_hunts_before_gate(void) {
    bb_game_t g;
    bb_game_init(&g, 5);
    bb_game_set_bots(&g, BB_BOTS_NORMAL);
    bb_game_force_start(&g);
    for (unsigned i = 0; i < 90u; i++) bb_game_step(&g);
    memset(g.wall, 0, sizeof g.wall);
    memset(g.e, 0, sizeof g.e);
    g.nwall = 0;
    g.p[0].x = 200 * 256;
    g.p[0].y = 200 * 256;
    g.p[0].vx = 0;
    g.p[0].vy = 0;
    g.p[0].hp = 100;
    g.p[0].invul = 255;
    g.p[0].gate_x = 850;
    g.p[0].gate_y = 520;
    g.p[1].x = 280 * 256;
    g.p[1].y = 200 * 256;
    g.p[1].hp = 100;
    g.p[1].invul = 255;
    g.p[1].gate_x = 80;
    g.p[1].gate_y = 520;
    g.p[2].x = 900 * 256;
    g.p[2].y = 40 * 256;
    g.p[2].hp = 100;
    g.p[3].x = 40 * 256;
    g.p[3].y = 40 * 256;
    g.p[3].hp = 100;
    memset(g.e, 0, sizeof g.e);
    unsigned fired = 0;
    bb_game_step(&g);
    ASSERT_TRUE(g.p[0].move_x > 40);
    if (g.p[0].fire) fired++;
    for (unsigned i = 0; i < 11u; i++) {
        bb_game_step(&g);
        if (g.p[0].fire) fired++;
    }
    ASSERT_TRUE(fired > 0u);
}

static void test_kill_streak_upgrades_pea(void) {
    bb_game_t g;
    bb_game_init(&g, 17);
    bb_game_connect(&g);
    bb_game_connect(&g);
    play(&g);
    memset(g.e, 0, sizeof g.e);
    memset(g.shot, 0, sizeof g.shot);
    memset(g.wall, 0, sizeof g.wall);
    g.nwall = 0;
    g.p[0].x = 200 * 256;
    g.p[0].y = 200 * 256;
    g.p[0].vx = g.p[0].vy = 0;
    g.p[0].weap = BB_WEAP_PEA;
    g.p[0].weap_ttl = 0;
    g.p[0].streak = 0;
    g.p[0].kills = 0;
    g.p[0].invul = 255;
    g.p[1].x = 260 * 256;
    g.p[1].y = 200 * 256;
    g.p[1].invul = 0;
    for (unsigned k = 0; k < 3u; k++) {
        g.p[1].hp = 1;
        g.p[1].respawn = 0;
        g.p[0].cooldown = 0;
        g.p[0].fire = 1;
        g.p[0].aim_x = 127;
        g.p[0].aim_y = 0;
        memset(g.shot, 0, sizeof g.shot);
        for (unsigned t = 0; t < 12u && g.p[1].hp; t++) {
            bb_game_step(&g);
            g.p[0].fire = 0;
        }
        ASSERT_EQ(g.p[1].hp, 0u);
        g.p[1].respawn = 1;
        g.p[1].invul = 0;
        bb_game_step(&g);
        g.p[1].x = 260 * 256;
        g.p[1].y = 200 * 256;
        g.p[1].invul = 0;
    }
    ASSERT_TRUE(g.p[0].kills >= 3u);
    ASSERT_EQ(g.p[0].weap, (uint8_t)BB_WEAP_LASER);
}

static void test_rematch_from_results(void) {
    bb_game_t g;
    bb_game_init(&g, 19);
    bb_game_connect(&g);
    bb_game_connect(&g);
    play(&g);
    g.p[0].gates = 4;
    g.p[0].score = 90;
    g.phase = BB_PHASE_RESULTS;
    g.phase_ticks = 200;
    g.rematch_lock = 60;
    g.p[0].ready = 0;
    bb_input_t in = { .ready = 1 };
    bb_game_input(&g, 0, &in);
    ASSERT_EQ(g.phase, BB_PHASE_RESULTS);
    bb_game_force_start(&g);
    ASSERT_EQ(g.phase, BB_PHASE_RESULTS);
    for (unsigned i = 0; i < 60u; i++) bb_game_step(&g);
    ASSERT_EQ(g.rematch_lock, 0u);
    bb_game_input(&g, 0, &in);
    ASSERT_EQ(g.phase, BB_PHASE_COUNTDOWN);
    ASSERT_EQ(g.p[0].gates, 0u);
    ASSERT_EQ(g.p[0].score, 0);
}

static void test_teams_bots_unstack(void) {
    bb_game_t g;
    bb_game_init(&g, 29);
    bb_game_set_teams(&g, true);
    bb_game_set_bots(&g, BB_BOTS_NORMAL);
    bb_game_force_start(&g);
    for (unsigned i = 0; i < 90u; i++) bb_game_step(&g);
    memset(g.wall, 0, sizeof g.wall);
    memset(g.e, 0, sizeof g.e);
    memset(g.shot, 0, sizeof g.shot);
    g.nwall = 0;
    g.p[0].x = 40 * 256;
    g.p[0].y = 40 * 256;
    g.p[1].x = 920 * 256;
    g.p[1].y = 40 * 256;
    g.p[2].x = 480 * 256;
    g.p[2].y = 500 * 256;
    g.p[3].x = 480 * 256;
    g.p[3].y = 500 * 256;
    g.p[2].gate_x = g.p[3].gate_x = 480;
    g.p[2].gate_y = g.p[3].gate_y = 180;
    g.p[2].vx = g.p[2].vy = g.p[3].vx = g.p[3].vy = 0;
    for (unsigned i = 0; i < 24u; i++) bb_game_step(&g);
    int dx = (int)(g.p[2].x / 256) - (int)(g.p[3].x / 256);
    int dy = (int)(g.p[2].y / 256) - (int)(g.p[3].y / 256);
    ASSERT_TRUE(dx * dx + dy * dy > 40 * 40);
}

static void test_teams_ff_and_shared_gate(void) {
    bb_game_t g;
    bb_game_init(&g, 23);
    for (unsigned i = 0; i < BB_PLAYERS; i++) bb_game_connect(&g);
    bb_game_set_teams(&g, true);
    play(&g);
    memset(g.wall, 0, sizeof g.wall);
    memset(g.e, 0, sizeof g.e);
    memset(g.shot, 0, sizeof g.shot);
    g.nwall = 0;
    g.p[0].gate_x = 400;
    g.p[0].gate_y = 300;
    g.p[1].gate_x = 400;
    g.p[1].gate_y = 300;
    g.p[0].x = 400 * 256;
    g.p[0].y = 300 * 256;
    const uint8_t before = g.p[1].gates;
    bb_game_step(&g);
    ASSERT_EQ(g.p[0].gates, (uint8_t)(before + 1u));
    ASSERT_EQ(g.p[1].gates, g.p[0].gates);
    ASSERT_EQ(g.p[1].gate_x, g.p[0].gate_x);
    ASSERT_EQ(g.p[1].gate_y, g.p[0].gate_y);

    g.p[0].x = 200 * 256;
    g.p[0].y = 200 * 256;
    g.p[1].x = 240 * 256;
    g.p[1].y = 200 * 256;
    g.p[1].hp = 100;
    g.p[1].invul = 0;
    g.p[0].weap = BB_WEAP_PEA;
    g.p[0].cooldown = 0;
    g.p[0].fire = 1;
    g.p[0].aim_x = 127;
    g.p[0].aim_y = 0;
    memset(g.shot, 0, sizeof g.shot);
    for (unsigned t = 0; t < 10u; t++) {
        bb_game_step(&g);
        g.p[0].fire = 0;
    }
    ASSERT_EQ(g.p[1].hp, 100u);
}

static void test_bot_skill_sleepy_vs_melee(void) {
    bb_game_t g;
    bb_game_init(&g, 29);
    bb_game_set_bots(&g, BB_BOTS_SLEEPY);
    bb_game_force_start(&g);
    for (unsigned i = 0; i < 90u; i++) bb_game_step(&g);
    memset(g.wall, 0, sizeof g.wall);
    memset(g.e, 0, sizeof g.e);
    g.nwall = 0;
    g.p[0].x = 400 * 256;
    g.p[0].y = 200 * 256;
    g.p[0].vx = g.p[0].vy = 0;
    g.p[0].hp = 100;
    g.p[0].invul = 255;
    g.p[0].gate_x = 200;
    g.p[0].gate_y = 200;
    g.p[1].x = 700 * 256;
    g.p[1].y = 200 * 256;
    g.p[1].hp = 100;
    g.p[1].invul = 255;
    g.p[2].x = 40 * 256;
    g.p[2].y = 40 * 256;
    g.p[3].x = 40 * 256;
    g.p[3].y = 560 * 256;
    bb_game_step(&g);
    ASSERT_TRUE(g.p[0].move_x < -40);

    bb_game_set_bots(&g, BB_BOTS_MELEE);
    g.p[0].x = 400 * 256;
    g.p[0].y = 200 * 256;
    g.p[0].vx = g.p[0].vy = 0;
    bb_game_step(&g);
    ASSERT_TRUE(g.p[0].move_x > 40);
}

int main(void) {
    test_four_player_start_and_gate();
    test_fire_enemy_and_random_respawn();
    test_walls_block_and_round_reroll();
    test_pickups_and_spray();
    test_bots_fill_and_human_steal();
    test_laser_bounces_pea_dies();
    test_match_clock_and_sudden();
    test_ice_and_grip();
    test_bot_slides_off_wall();
    test_bot_hunts_before_gate();
    test_kill_streak_upgrades_pea();
    test_rematch_from_results();
    test_teams_bots_unstack();
    test_teams_ff_and_shared_gate();
    test_bot_skill_sleepy_vs_melee();
    TEST_RETURN();
}
