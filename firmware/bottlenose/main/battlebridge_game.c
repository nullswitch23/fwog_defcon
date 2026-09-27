#include "battlebridge_game.h"
#include <string.h>

#define Q             256
#define PLAYER_R      13
#define ENEMY_R       12
#define GATE_R        10
#define SHOT_R        4
#define PK_R          16
#define RESPAWN_TICKS 90
#define INVUL_TICKS   60
#define WEAP_TTL      300
#define SHIELD_TTL    300
#define GRIP_TTL      300
#define SHIELD_HP     50
#define SPEED_MAX     ((6 * Q * 85) / 100) /* 15% slower than the 6-px cap */
#define REMATCH_LOCK  60u                   /* 2 s at 30 Hz */

static const uint8_t k_color[BB_PLAYERS] = { 0, 1, 2, 3 };
static const uint8_t k_shot_dmg[] = { 16, 32, 7 };
static const uint8_t k_shot_ttl[] = { 60, 48, 26 };
static const uint8_t k_cool[]     = { 8, 11, 14 };

static void finish_round(bb_game_t *g);

static uint32_t rnd(bb_game_t *g) {
    uint32_t x = g->rng ? g->rng : 0x6D2B79F5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng = x;
    return x;
}

static int32_t wrap(int32_t v, int32_t n) {
    while (v < 0) v += n;
    while (v >= n) v -= n;
    return v;
}

static bool near_q8(int32_t ax, int32_t ay, int32_t bx, int32_t by, int r) {
    const int32_t dx = (ax - bx) / Q;
    const int32_t dy = (ay - by) / Q;
    return dx * dx + dy * dy <= r * r;
}

static bool circle_hits_wall(const bb_wall_t *w, int px, int py, int r) {
    if (!w->w || !w->h) return false;
    int cx = px;
    int cy = py;
    if (cx < (int)w->x) cx = (int)w->x;
    if (cx > (int)w->x + (int)w->w) cx = (int)w->x + (int)w->w;
    if (cy < (int)w->y) cy = (int)w->y;
    if (cy > (int)w->y + (int)w->h) cy = (int)w->y + (int)w->h;
    int dx = px - cx;
    int dy = py - cy;
    return dx * dx + dy * dy < r * r;
}

static bool blocked_xy(const bb_game_t *g, int32_t x, int32_t y, int r) {
    const int px = (int)(x / Q);
    const int py = (int)(y / Q);
    for (unsigned i = 0; i < g->nwall; i++) {
        if (circle_hits_wall(&g->wall[i], px, py, r)) return true;
    }
    return false;
}

static bool rects_overlap(int ax, int ay, int aw, int ah,
                          int bx, int by, int bw, int bh, int pad) {
    return ax < bx + bw + pad && ax + aw + pad > bx &&
           ay < by + bh + pad && ay + ah + pad > by;
}

static void normalize(int32_t dx, int32_t dy, int speed,
                      int16_t *vx, int16_t *vy) {
    int32_t ax = dx < 0 ? -dx : dx;
    int32_t ay = dy < 0 ? -dy : dy;
    int32_t m = ax > ay ? ax : ay;
    if (m < 1) {
        *vx = 0;
        *vy = 0;
        return;
    }
    *vx = (int16_t)(dx * speed / m);
    *vy = (int16_t)(dy * speed / m);
}

static void generate_map(bb_game_t *g) {
    memset(g->wall, 0, sizeof g->wall);
    memset(g->pk, 0, sizeof g->pk);
    g->nwall = 0;
    const unsigned want = 5u + (rnd(g) % 4u);
    for (unsigned t = 0; t < 40u && g->nwall < want && g->nwall < BB_WALLS; t++) {
        bb_wall_t w;
        if (rnd(g) & 1u) {
            w.w = (uint16_t)(90u + rnd(g) % 180u);
            w.h = (uint16_t)(16u + rnd(g) % 8u);
        } else {
            w.w = (uint16_t)(16u + rnd(g) % 8u);
            w.h = (uint16_t)(90u + rnd(g) % 160u);
        }
        w.x = (uint16_t)(50u + rnd(g) % (BB_ARENA_W - w.w - 100u));
        w.y = (uint16_t)(50u + rnd(g) % (BB_ARENA_H - w.h - 100u));
        bool ok = true;
        for (unsigned i = 0; i < g->nwall; i++) {
            const bb_wall_t *o = &g->wall[i];
            if (rects_overlap((int)w.x, (int)w.y, (int)w.w, (int)w.h,
                              (int)o->x, (int)o->y, (int)o->w, (int)o->h, 36)) {
                ok = false;
                break;
            }
        }
        if (!ok) continue;
        g->wall[g->nwall++] = w;
    }
}

static unsigned mate_of(unsigned i) { return i ^ 1u; }

static bool allies(const bb_game_t *g, unsigned a, unsigned b) {
    return g->teams && a < BB_PLAYERS && b < BB_PLAYERS && (a / 2u) == (b / 2u);
}

static void place_gate(bb_game_t *g, unsigned slot, uint16_t x, uint16_t y,
                       uint8_t c) {
    g->p[slot].gate_x = x;
    g->p[slot].gate_y = y;
    g->p[slot].gate_c = c;
    if (g->teams) {
        unsigned m = mate_of(slot);
        if (g->p[m].active) {
            g->p[m].gate_x = x;
            g->p[m].gate_y = y;
            g->p[m].gate_c = c;
        }
    }
}

static void new_gate(bb_game_t *g, bb_player_t *p) {
    unsigned slot = (unsigned)(p - g->p);
    uint8_t c = (uint8_t)(rnd(g) % 6u);
    for (unsigned tries = 0; tries < 24u; tries++) {
        uint16_t x = (uint16_t)(60u + rnd(g) % (BB_ARENA_W - 120u));
        uint16_t y = (uint16_t)(60u + rnd(g) % (BB_ARENA_H - 120u));
        int dx = (int)x - (int)(p->x / Q);
        int dy = (int)y - (int)(p->y / Q);
        if (dx * dx + dy * dy <= 180 * 180) continue;
        if (blocked_xy(g, (int32_t)x * Q, (int32_t)y * Q, GATE_R + 8)) continue;
        place_gate(g, slot, x, y, c);
        return;
    }
    place_gate(g, slot, (uint16_t)(BB_ARENA_W / 2u),
               (uint16_t)(BB_ARENA_H / 2u), c);
}

static void spawn_player(bb_game_t *g, unsigned slot) {
    bb_player_t *p = &g->p[slot];
    for (unsigned tries = 0; tries < 32u; tries++) {
        int32_t x = (int32_t)(40u + rnd(g) % (BB_ARENA_W - 80u)) * Q;
        int32_t y = (int32_t)(40u + rnd(g) % (BB_ARENA_H - 80u)) * Q;
        if (blocked_xy(g, x, y, PLAYER_R + 10)) continue;
        bool safe = true;
        for (unsigned i = 0; i < BB_PLAYERS; i++) {
            if (i != slot && g->p[i].active && g->p[i].hp &&
                near_q8(x, y, g->p[i].x, g->p[i].y, 120)) {
                safe = false;
                break;
            }
        }
        if (safe) {
            p->x = x;
            p->y = y;
            break;
        }
    }
    p->vx = p->vy = 0;
    p->hp = 100;
    p->respawn = 0;
    p->invul = INVUL_TICKS;
    p->shield = 0;
    p->shield_ttl = 0;
    p->grip_ttl = 0;
    if (!p->gate_x && !p->gate_y) new_gate(g, p);
}

static bb_projectile_t *alloc_shot(bb_game_t *g) {
    for (unsigned i = 0; i < BB_PROJECTILES; i++) {
        if (!g->shot[i].active) return &g->shot[i];
    }
    return NULL;
}

static void shoot_one(bb_game_t *g, unsigned owner, bool hostile, uint8_t kind,
                      int32_t x, int32_t y, int32_t ax, int32_t ay, int speed) {
    bb_projectile_t *s = alloc_shot(g);
    if (!s) return;
    memset(s, 0, sizeof *s);
    s->active = 1;
    s->owner = (uint8_t)owner;
    s->hostile = hostile ? 1u : 0u;
    s->kind = kind;
    s->bounce = (kind == BB_WEAP_LASER) ? (uint8_t)BB_LASER_BOUNCE : 0u;
    s->ttl = k_shot_ttl[kind < 3u ? kind : 0u];
    s->x = x;
    s->y = y;
    normalize(ax, ay, speed, &s->vx, &s->vy);
    if (s->vx == 0 && s->vy == 0) s->vx = (int16_t)speed;
}

static void player_fire(bb_game_t *g, unsigned i) {
    bb_player_t *p = &g->p[i];
    const uint8_t weap = p->weap < 3u ? p->weap : 0u;
    if (weap == BB_WEAP_SPRAY) {
        const int32_t ax = p->aim_x, ay = p->aim_y;
        shoot_one(g, i, false, weap, p->x, p->y, ax, ay, 8 * Q);
        shoot_one(g, i, false, weap, p->x, p->y, ax + ay / 4, ay - ax / 4, 8 * Q);
        shoot_one(g, i, false, weap, p->x, p->y, ax - ay / 4, ay + ax / 4, 8 * Q);
        shoot_one(g, i, false, weap, p->x, p->y, ax + ay / 2, ay - ax / 2, 7 * Q);
        shoot_one(g, i, false, weap, p->x, p->y, ax - ay / 2, ay + ax / 2, 7 * Q);
    } else if (weap == BB_WEAP_LASER) {
        shoot_one(g, i, false, weap, p->x, p->y, p->aim_x, p->aim_y, 14 * Q);
    } else {
        shoot_one(g, i, false, weap, p->x, p->y, p->aim_x, p->aim_y, 9 * Q);
    }
    p->cooldown = k_cool[weap];
}

static void damage_player(bb_game_t *g, unsigned victim, unsigned owner,
                          unsigned damage, bool hostile) {
    bb_player_t *p = &g->p[victim];
    if (!p->active || !p->hp || p->invul) return;
    if (p->shield) {
        if (damage >= p->shield) {
            damage -= p->shield;
            p->shield = 0;
            p->shield_ttl = 0;
        } else {
            p->shield = (uint8_t)(p->shield - damage);
            return;
        }
        if (!damage) return;
    }
    if (damage >= p->hp) {
        p->hp = 0;
        p->respawn = RESPAWN_TICKS;
        p->weap = 0;
        p->weap_ttl = 0;
        p->shield = 0;
        p->grip_ttl = 0;
        p->score -= 15;
        p->streak = 0;
        if (!hostile && owner < BB_PLAYERS && owner != victim &&
            !allies(g, owner, victim)) {
            g->p[owner].score += 100;
            g->p[owner].kills++;
            if (g->p[owner].streak < 255u) g->p[owner].streak++;
            if (g->p[owner].streak >= 3u && g->p[owner].weap == BB_WEAP_PEA) {
                g->p[owner].weap = BB_WEAP_LASER;
                g->p[owner].weap_ttl = 150;
            }
        }
    } else {
        p->hp = (uint8_t)(p->hp - damage);
        if (!hostile && owner < BB_PLAYERS && owner != victim &&
            !allies(g, owner, victim)) {
            g->p[owner].score += 5;
        }
    }
}

static void spawn_enemy(bb_game_t *g) {
    for (unsigned i = 0; i < BB_ENEMIES; i++) {
        if (g->e[i].active) continue;
        for (unsigned tries = 0; tries < 12u; tries++) {
            int32_t x = (int32_t)(rnd(g) % BB_ARENA_W) * Q;
            int32_t y = (int32_t)(rnd(g) % BB_ARENA_H) * Q;
            if (blocked_xy(g, x, y, ENEMY_R + 8)) continue;
            bb_enemy_t *e = &g->e[i];
            memset(e, 0, sizeof *e);
            e->active = 1;
            e->hp = 35;
            e->x = x;
            e->y = y;
            return;
        }
        return;
    }
}

static void spawn_pickup(bb_game_t *g) {
    int slot = -1;
    unsigned n = 0;
    for (unsigned i = 0; i < BB_PICKUPS; i++) {
        if (g->pk[i].active) n++;
        else if (slot < 0) slot = (int)i;
    }
    if (slot < 0 || n >= 4u) return;
    for (unsigned tries = 0; tries < 16u; tries++) {
        uint16_t x = (uint16_t)(50u + rnd(g) % (BB_ARENA_W - 100u));
        uint16_t y = (uint16_t)(50u + rnd(g) % (BB_ARENA_H - 100u));
        if (blocked_xy(g, (int32_t)x * Q, (int32_t)y * Q, PK_R + 8)) continue;
        g->pk[slot].active = 1;
        g->pk[slot].kind = (uint8_t)(rnd(g) % 5u);
        g->pk[slot].x = x;
        g->pk[slot].y = y;
        return;
    }
}

static void apply_pickup(bb_player_t *p, uint8_t kind) {
    if (kind == BB_PK_HEALTH) {
        unsigned hp = (unsigned)p->hp + 40u;
        p->hp = (uint8_t)(hp > 100u ? 100u : hp);
    } else if (kind == BB_PK_SHIELD) {
        p->shield = SHIELD_HP;
        p->shield_ttl = SHIELD_TTL;
    } else if (kind == BB_PK_LASER) {
        p->weap = BB_WEAP_LASER;
        p->weap_ttl = WEAP_TTL;
    } else if (kind == BB_PK_SPRAY) {
        p->weap = BB_WEAP_SPRAY;
        p->weap_ttl = WEAP_TTL;
    } else if (kind == BB_PK_GRIP) {
        p->grip_ttl = GRIP_TTL;
    }
}

static int nearest_player(const bb_game_t *g, int32_t x, int32_t y) {
    int best = -1;
    int64_t best_d = 0;
    for (unsigned i = 0; i < BB_PLAYERS; i++) {
        const bb_player_t *p = &g->p[i];
        if (!p->active || !p->hp) continue;
        int64_t dx = (p->x - x) / Q;
        int64_t dy = (p->y - y) / Q;
        int64_t d = dx * dx + dy * dy;
        if (best < 0 || d < best_d) {
            best = (int)i;
            best_d = d;
        }
    }
    return best;
}

void bb_game_init(bb_game_t *g, uint32_t seed) {
    memset(g, 0, sizeof *g);
    g->rng = seed ? seed : 1u;
    g->phase = BB_PHASE_LOBBY;
}

int bb_game_connect(bb_game_t *g) {
    int steal = -1;
    for (unsigned i = 0; i < BB_PLAYERS; i++) {
        if (!g->p[i].active) {
            memset(&g->p[i], 0, sizeof g->p[i]);
            g->p[i].active = 1;
            g->p[i].color = k_color[i];
            spawn_player(g, i);
            new_gate(g, &g->p[i]);
            return (int)i;
        }
        if (g->p[i].bot && steal < 0) steal = (int)i;
    }
    if (steal < 0) return -1;
    memset(&g->p[steal], 0, sizeof g->p[steal]);
    g->p[steal].active = 1;
    g->p[steal].color = k_color[steal];
    spawn_player(g, (unsigned)steal);
    new_gate(g, &g->p[steal]);
    return steal;
}

void bb_game_disconnect(bb_game_t *g, unsigned slot) {
    if (slot >= BB_PLAYERS) return;
    memset(&g->p[slot], 0, sizeof g->p[slot]);
    if (g->bots_on) {
        /* refill below via set/fill; keep the slot free for fill_bots */
    }
    if (bb_game_active(g) == 0u && !g->bots_on) {
        g->phase = BB_PHASE_LOBBY;
        g->phase_ticks = 0;
        g->sudden = 0;
        memset(g->wall, 0, sizeof g->wall);
        memset(g->pk, 0, sizeof g->pk);
        g->nwall = 0;
    }
}

void bb_game_input(bb_game_t *g, unsigned slot, const bb_input_t *in) {
    if (slot >= BB_PLAYERS || !g->p[slot].active || !in) return;
    if (g->p[slot].bot) return;
    bb_player_t *p = &g->p[slot];
    p->move_x = in->move_x;
    p->move_y = in->move_y;
    p->aim_x = in->aim_x;
    p->aim_y = in->aim_y;
    p->fire = in->fire ? 1u : 0u;
    if (g->phase == BB_PHASE_RESULTS && g->rematch_lock) return;
    p->ready = in->ready ? 1u : p->ready;
    if (g->phase == BB_PHASE_RESULTS && in->ready)
        bb_game_force_start(g);
}

static void spawn_bot(bb_game_t *g, unsigned i) {
    memset(&g->p[i], 0, sizeof g->p[i]);
    g->p[i].active = 1;
    g->p[i].bot = 1;
    g->p[i].ready = 1;
    g->p[i].color = k_color[i];
    spawn_player(g, i);
    new_gate(g, &g->p[i]);
}

static void fill_bots(bb_game_t *g) {
    if (!g->bots_on) return;
    for (unsigned i = 0; i < BB_PLAYERS; i++) {
        if (!g->p[i].active) spawn_bot(g, i);
    }
}

void bb_game_set_bots(bb_game_t *g, unsigned skill) {
    if (skill > BB_BOTS_MELEE) skill = BB_BOTS_MELEE;
    g->bots_on = (uint8_t)skill;
    if (!skill) {
        for (unsigned i = 0; i < BB_PLAYERS; i++) {
            if (g->p[i].bot) memset(&g->p[i], 0, sizeof g->p[i]);
        }
        if (bb_game_active(g) == 0u) {
            g->phase = BB_PHASE_LOBBY;
            g->sudden = 0;
        }
        return;
    }
    fill_bots(g);
}

void bb_game_set_teams(bb_game_t *g, bool on) {
    g->teams = on ? 1u : 0u;
    if (g->phase == BB_PHASE_LOBBY) {
        for (unsigned i = 0; i < BB_PLAYERS; i++) {
            if (g->p[i].active) new_gate(g, &g->p[i]);
        }
    }
}

static void begin_match(bb_game_t *g) {
    generate_map(g);
    for (unsigned i = 0; i < BB_PLAYERS; i++) {
        if (!g->p[i].active) continue;
        g->p[i].score = 0;
        g->p[i].gates = 0;
        g->p[i].kills = 0;
        g->p[i].streak = 0;
        spawn_player(g, i);
        new_gate(g, &g->p[i]);
        if (g->p[i].bot) g->p[i].ready = 1;
    }
    memset(g->e, 0, sizeof g->e);
    memset(g->shot, 0, sizeof g->shot);
    g->sudden = 0;
    g->phase = BB_PHASE_COUNTDOWN;
    g->phase_ticks = 90;
}

void bb_game_force_start(bb_game_t *g) {
    fill_bots(g);
    if (g->phase == BB_PHASE_PLAY || g->phase == BB_PHASE_COUNTDOWN) return;
    if (g->phase == BB_PHASE_RESULTS && g->rematch_lock) return;
    if (bb_game_active(g)) begin_match(g);
}

unsigned bb_game_team_gates(const bb_game_t *g, unsigned team) {
    unsigned best = 0;
    for (unsigned i = 0; i < BB_PLAYERS; i++) {
        if (!g->p[i].active) continue;
        if (g->teams && (i / 2u) != team) continue;
        if (!g->teams && i != team) continue;
        if (g->p[i].gates > best) best = g->p[i].gates;
    }
    return best;
}

unsigned bb_game_active(const bb_game_t *g) {
    unsigned n = 0;
    for (unsigned i = 0; i < BB_PLAYERS; i++) n += g->p[i].active ? 1u : 0u;
    return n;
}

unsigned bb_game_enemies(const bb_game_t *g) {
    unsigned n = 0;
    for (unsigned i = 0; i < BB_ENEMIES; i++) n += g->e[i].active ? 1u : 0u;
    return n;
}

unsigned bb_game_projectiles(const bb_game_t *g) {
    unsigned n = 0;
    for (unsigned i = 0; i < BB_PROJECTILES; i++) n += g->shot[i].active ? 1u : 0u;
    return n;
}

static void lobby_step(bb_game_t *g) {
    fill_bots(g);
    unsigned active = 0, humans = 0, human_ready = 0;
    for (unsigned i = 0; i < BB_PLAYERS; i++) {
        if (!g->p[i].active) continue;
        active++;
        if (g->p[i].bot) continue;
        humans++;
        human_ready += g->p[i].ready ? 1u : 0u;
    }
    if (active >= 2u && humans > 0u && human_ready == humans) {
        bb_game_force_start(g);
    }
}

static void try_move(bb_game_t *g, int32_t *x, int32_t *y,
                     int32_t vx, int32_t vy, int r) {
    int32_t nx = wrap(*x + vx, (int32_t)BB_ARENA_W * Q);
    int32_t ny = wrap(*y + vy, (int32_t)BB_ARENA_H * Q);
    if (!blocked_xy(g, nx, ny, r)) {
        *x = nx;
        *y = ny;
        return;
    }
    int32_t mx = wrap(*x + vx, (int32_t)BB_ARENA_W * Q);
    if (!blocked_xy(g, mx, *y, r)) {
        *x = mx;
        return;
    }
    int32_t my = wrap(*y + vy, (int32_t)BB_ARENA_H * Q);
    if (!blocked_xy(g, *x, my, r)) *y = my;
}

static void try_move_unstick(bb_game_t *g, int32_t *x, int32_t *y,
                             int32_t vx, int32_t vy, int r) {
    const int32_t ox = *x, oy = *y;
    const int32_t W = (int32_t)BB_ARENA_W * Q;
    const int32_t H = (int32_t)BB_ARENA_H * Q;
    const bool hitx = blocked_xy(g, wrap(*x + vx, W), *y, r);
    const bool hity = blocked_xy(g, *x, wrap(*y + vy, H), r);
    try_move(g, x, y, vx, vy, r);
    if (!hitx && !hity) return;
    if (hitx) {
        int32_t slide = vy ? vy : (2 * Q);
        int32_t before = *y;
        try_move(g, x, y, 0, slide, r);
        if (*y == before) try_move(g, x, y, 0, -slide, r);
        if (*y == before) try_move(g, x, y, 0, 4 * Q, r);
        if (*y == before) try_move(g, x, y, 0, -4 * Q, r);
    }
    if (hity && *x == ox) {
        int32_t slide = vx ? vx : (2 * Q);
        int32_t before = *x;
        try_move(g, x, y, slide, 0, r);
        if (*x == before) try_move(g, x, y, -slide, 0, r);
        if (*x == before) try_move(g, x, y, 4 * Q, 0, r);
        if (*x == before) try_move(g, x, y, -4 * Q, 0, r);
    }
    (void)oy;
}

static void avoid_walls(const bb_game_t *g, int32_t x, int32_t y, int r,
                        int8_t *mx, int8_t *my) {
    if (!*mx && !*my) return;
    const int look = 40;
    const int sx = *mx > 8 ? look : *mx < -8 ? -look : 0;
    const int sy = *my > 8 ? look : *my < -8 ? -look : 0;
    const bool hitx = sx && blocked_xy(g, x + sx * Q, y, r + 4);
    const bool hity = sy && blocked_xy(g, x, y + sy * Q, r + 4);
    if (hitx && hity) {
        int8_t rx = *my, ry = (int8_t)(-*mx);
        const int tx = rx > 8 ? look : rx < -8 ? -look : 0;
        const int ty = ry > 8 ? look : ry < -8 ? -look : 0;
        if ((tx && blocked_xy(g, x + tx * Q, y, r + 4)) ||
            (ty && blocked_xy(g, x, y + ty * Q, r + 4))) {
            rx = (int8_t)(-*my);
            ry = *mx;
        }
        *mx = rx;
        *my = ry;
        return;
    }
    if (hitx) *mx = 0;
    if (hity) *my = 0;
    if (!*mx && !*my) {
        *mx = (int8_t)(sy ? (sy > 0 ? 110 : -110) : 0);
        *my = (int8_t)(sx ? (sx > 0 ? 110 : -110) : 0);
        if (!*mx && !*my) *my = 110;
        const int tx = *mx > 8 ? look : *mx < -8 ? -look : 0;
        const int ty = *my > 8 ? look : *my < -8 ? -look : 0;
        if ((tx && blocked_xy(g, x + tx * Q, y, r + 4)) ||
            (ty && blocked_xy(g, x, y + ty * Q, r + 4))) {
            *mx = (int8_t)-*mx;
            *my = (int8_t)-*my;
        }
    }
}

static void player_step(bb_game_t *g, unsigned i) {
    bb_player_t *p = &g->p[i];
    if (!p->active) return;
    if (!p->hp) {
        if (p->respawn) p->respawn--;
        if (!p->respawn) spawn_player(g, i);
        return;
    }
    if (p->invul) p->invul--;
    if (p->cooldown) p->cooldown--;
    if (p->weap_ttl) {
        p->weap_ttl--;
        if (!p->weap_ttl) p->weap = BB_WEAP_PEA;
    }
    if (p->shield_ttl) {
        p->shield_ttl--;
        if (!p->shield_ttl) p->shield = 0;
    }
    if (p->grip_ttl) p->grip_ttl--;
    if (p->grip_ttl) {
        p->vx += (int32_t)p->move_x * 7;
        p->vy += (int32_t)p->move_y * 7;
        if (p->move_x || p->move_y) {
            p->vx = p->vx * 210 / 256;
            p->vy = p->vy * 210 / 256;
        } else {
            p->vx = p->vx * 96 / 256;
            p->vy = p->vy * 96 / 256;
        }
    } else {
        /* Ice: the first-revision skate. Grip pickup is what bites. */
        p->vx += (int32_t)p->move_x * 3;
        p->vy += (int32_t)p->move_y * 3;
        p->vx = p->vx * 242 / 256;
        p->vy = p->vy * 242 / 256;
    }
    if (p->vx > SPEED_MAX) p->vx = SPEED_MAX;
    if (p->vx < -SPEED_MAX) p->vx = -SPEED_MAX;
    if (p->vy > SPEED_MAX) p->vy = SPEED_MAX;
    if (p->vy < -SPEED_MAX) p->vy = -SPEED_MAX;
    if (p->bot) try_move_unstick(g, &p->x, &p->y, p->vx, p->vy, PLAYER_R);
    else try_move(g, &p->x, &p->y, p->vx, p->vy, PLAYER_R);

    if (p->fire && !p->cooldown) player_fire(g, i);
    if (near_q8(p->x, p->y, (int32_t)p->gate_x * Q,
                (int32_t)p->gate_y * Q, GATE_R + PLAYER_R)) {
        p->gates++;
        p->score += 25;
        if (g->teams) {
            unsigned m = mate_of(i);
            if (g->p[m].active) {
                g->p[m].gates = p->gates;
                g->p[m].score += 10;
            }
        }
        unsigned need = p->gates;
        if (g->sudden || need >= BB_GATE_GOAL) {
            finish_round(g);
        } else {
            new_gate(g, p);
        }
    }
    for (unsigned k = 0; k < BB_PICKUPS; k++) {
        if (!g->pk[k].active) continue;
        if (!near_q8(p->x, p->y, (int32_t)g->pk[k].x * Q,
                     (int32_t)g->pk[k].y * Q, PK_R + PLAYER_R)) continue;
        apply_pickup(p, g->pk[k].kind);
        g->pk[k].active = 0;
        p->score += 8;
    }
}

static void bot_think(bb_game_t *g, unsigned i) {
    bb_player_t *p = &g->p[i];
    if (!p->bot || !p->active || !p->hp) return;
    int px = (int)(p->x / Q), py = (int)(p->y / Q);
    int gox = 0, goy = 0;
    if (g->teams) {
        gox = (i & 1u) ? 52 : -52;
        goy = (i & 1u) ? -40 : 40;
    }
    int gx = (int)p->gate_x + gox - px, gy = (int)p->gate_y + goy - py;
    int g2 = gx * gx + gy * gy;

    int hunt_x = gx, hunt_y = gy, hunt_d2 = -1;
    int lead_x = 0, lead_y = 0;
    for (unsigned j = 0; j < BB_PLAYERS; j++) {
        if (j == i || !g->p[j].active || !g->p[j].hp) continue;
        if (allies(g, i, j)) continue;
        int hx = (int)(g->p[j].x / Q) - px;
        int hy = (int)(g->p[j].y / Q) - py;
        int d2 = hx * hx + hy * hy;
        if (hunt_d2 < 0 || d2 < hunt_d2) {
            hunt_d2 = d2;
            hunt_x = hx;
            hunt_y = hy;
            lead_x = (int)(g->p[j].vx / Q);
            lead_y = (int)(g->p[j].vy / Q);
        }
    }
    for (unsigned e = 0; e < BB_ENEMIES; e++) {
        if (!g->e[e].active) continue;
        int hx = (int)(g->e[e].x / Q) - px;
        int hy = (int)(g->e[e].y / Q) - py;
        int d2 = hx * hx + hy * hy;
        if (hunt_d2 < 0 || d2 < hunt_d2) {
            hunt_d2 = d2;
            hunt_x = hx;
            hunt_y = hy;
            lead_x = 0;
            lead_y = 0;
        }
    }

    /* Hunt ships and drones first. Skill stretches that radius; only a
     * nearby gate is worth peeling for. Sleepy stays on the gate more. */
    const unsigned sk = g->bots_on ? g->bots_on : BB_BOTS_NORMAL;
    int hunt_r = 420, fire_r = 300, take_gate = 70, fire_miss = 5, lead = 6;
    if (sk == BB_BOTS_SLEEPY) {
        hunt_r = 180;
        fire_r = 140;
        take_gate = 160;
        fire_miss = 3;
        lead = 3;
    } else if (sk == BB_BOTS_MELEE) {
        hunt_r = 780;
        fire_r = 480;
        take_gate = 36;
        fire_miss = 12;
        lead = 8;
    }
    int mx, my, aimx, aimy;
    p->fire = 0;
    if (hunt_d2 >= 0 && hunt_d2 < hunt_r * hunt_r && g2 > take_gate * take_gate) {
        mx = hunt_x;
        my = hunt_y;
        aimx = hunt_x + lead_x * lead;
        aimy = hunt_y + lead_y * lead;
        if (hunt_d2 < fire_r * fire_r && (rnd(g) % (unsigned)fire_miss) != 0u)
            p->fire = 1;
    } else {
        mx = gx;
        my = gy;
        aimx = gx;
        aimy = gy;
        if (hunt_d2 >= 0 && hunt_d2 < fire_r * fire_r) {
            aimx = hunt_x + lead_x * lead;
            aimy = hunt_y + lead_y * lead;
            if ((rnd(g) % 4u) != 0u) p->fire = 1;
        }
    }
    if (g->teams) {
        unsigned m = mate_of(i);
        if (g->p[m].active && g->p[m].hp) {
            int dx = px - (int)(g->p[m].x / Q);
            int dy = py - (int)(g->p[m].y / Q);
            int d2 = dx * dx + dy * dy;
            if (d2 < 4) {
                dx = (i & 1u) ? 50 : -50;
                dy = (i & 1u) ? -40 : 40;
                d2 = 1;
            }
            if (d2 < 110 * 110) {
                mx += dx * 4;
                my += dy * 4;
            }
        }
        if (hunt_d2 >= 0 && hunt_d2 < hunt_r * hunt_r) {
            int fx = -hunt_y, fy = hunt_x;
            if (i & 1u) {
                fx = -fx;
                fy = -fy;
            }
            mx += fx / 4;
            my += fy / 4;
        }
    }
    p->move_x = (int8_t)(mx > 12 ? 110 : mx < -12 ? -110 : mx * 8);
    p->move_y = (int8_t)(my > 12 ? 110 : my < -12 ? -110 : my * 8);
    avoid_walls(g, p->x, p->y, PLAYER_R, &p->move_x, &p->move_y);
    if (aimx > 127) aimx = 127;
    if (aimx < -127) aimx = -127;
    if (aimy > 127) aimy = 127;
    if (aimy < -127) aimy = -127;
    p->aim_x = (int8_t)aimx;
    p->aim_y = (int8_t)aimy;
    p->ready = 1;
}

static int unique_gate_leader(const bb_game_t *g) {
    if (g->teams) {
        unsigned a = bb_game_team_gates(g, 0), b = bb_game_team_gates(g, 1);
        if (a == b || (a == 0u && b == 0u)) return -1;
        return a > b ? 0 : 2;
    }
    int who = -1;
    unsigned best = 0, n = 0;
    for (unsigned i = 0; i < BB_PLAYERS; i++) {
        if (!g->p[i].active) continue;
        if (g->p[i].gates > best) {
            best = g->p[i].gates;
            who = (int)i;
            n = 1;
        } else if (g->p[i].gates == best) {
            n++;
        }
    }
    return (n == 1u && best > 0u) ? who : -1;
}

static void finish_round(bb_game_t *g) {
    g->phase = BB_PHASE_RESULTS;
    g->phase_ticks = 300;
    g->sudden = 0;
    g->rematch_lock = (uint8_t)REMATCH_LOCK;
    for (unsigned i = 0; i < BB_PLAYERS; i++) {
        if (g->p[i].active && !g->p[i].bot) g->p[i].ready = 0;
    }
}

static void enemy_step(bb_game_t *g, unsigned i) {
    bb_enemy_t *e = &g->e[i];
    if (!e->active) return;
    int target = nearest_player(g, e->x, e->y);
    if (target < 0) return;
    e->target = (uint8_t)target;
    bb_player_t *p = &g->p[target];
    int16_t vx, vy;
    normalize(p->x - e->x, p->y - e->y, Q, &vx, &vy);
    int8_t mx = (int8_t)(vx > 40 ? 110 : vx < -40 ? -110 : vx / 3);
    int8_t my = (int8_t)(vy > 40 ? 110 : vy < -40 ? -110 : vy / 3);
    avoid_walls(g, e->x, e->y, ENEMY_R, &mx, &my);
    normalize((int32_t)mx, (int32_t)my, Q, &vx, &vy);
    try_move_unstick(g, &e->x, &e->y, vx, vy, ENEMY_R);
    if (e->cooldown) e->cooldown--;
    if (!e->cooldown) {
        shoot_one(g, i, true, BB_WEAP_PEA, e->x, e->y,
                  p->x - e->x, p->y - e->y, 9 * Q);
        e->cooldown = (uint8_t)(35u + rnd(g) % 30u);
    }
}

static void shots_step(bb_game_t *g) {
    for (unsigned i = 0; i < BB_PROJECTILES; i++) {
        bb_projectile_t *s = &g->shot[i];
        if (!s->active) continue;
        int32_t nx = wrap(s->x + s->vx, (int32_t)BB_ARENA_W * Q);
        int32_t ny = wrap(s->y + s->vy, (int32_t)BB_ARENA_H * Q);
        if (blocked_xy(g, nx, ny, SHOT_R)) {
            if (s->kind == BB_WEAP_LASER && s->bounce) {
                const bool hitx = blocked_xy(g, nx, s->y, SHOT_R);
                const bool hity = blocked_xy(g, s->x, ny, SHOT_R);
                if (hitx) s->vx = (int16_t)-s->vx;
                if (hity) s->vy = (int16_t)-s->vy;
                if (!hitx && !hity) {
                    s->vx = (int16_t)-s->vx;
                    s->vy = (int16_t)-s->vy;
                }
                s->bounce--;
                nx = wrap(s->x + s->vx, (int32_t)BB_ARENA_W * Q);
                ny = wrap(s->y + s->vy, (int32_t)BB_ARENA_H * Q);
                if (blocked_xy(g, nx, ny, SHOT_R)) continue;
            } else {
                s->active = 0;
                continue;
            }
        }
        s->x = nx;
        s->y = ny;
        if (!s->ttl--) {
            s->active = 0;
            continue;
        }
        const uint8_t kind = s->kind < 3u ? s->kind : 0u;
        const unsigned dmg = k_shot_dmg[kind];
        if (s->hostile) {
            for (unsigned p = 0; p < BB_PLAYERS; p++) {
                if (near_q8(s->x, s->y, g->p[p].x, g->p[p].y,
                            PLAYER_R + SHOT_R)) {
                    damage_player(g, p, s->owner, dmg, true);
                    s->active = 0;
                    break;
                }
            }
            continue;
        }
        for (unsigned p = 0; p < BB_PLAYERS; p++) {
            if (p == s->owner) continue;
            if (allies(g, s->owner, p)) continue;
            if (near_q8(s->x, s->y, g->p[p].x, g->p[p].y,
                        PLAYER_R + SHOT_R)) {
                damage_player(g, p, s->owner, dmg, false);
                s->active = 0;
                break;
            }
        }
        if (!s->active) continue;
        for (unsigned e = 0; e < BB_ENEMIES; e++) {
            if (!g->e[e].active) continue;
            if (!near_q8(s->x, s->y, g->e[e].x, g->e[e].y,
                         ENEMY_R + SHOT_R)) continue;
            unsigned hit = kind == BB_WEAP_LASER ? 28u : 18u;
            if (g->e[e].hp <= hit) {
                g->e[e].active = 0;
                if (s->owner < BB_PLAYERS) g->p[s->owner].score += 25;
            } else {
                g->e[e].hp = (uint8_t)(g->e[e].hp - hit);
            }
            s->active = 0;
            break;
        }
    }
}

void bb_game_step(bb_game_t *g) {
    g->tick++;
    if (g->phase == BB_PHASE_LOBBY) {
        lobby_step(g);
        return;
    }
    if (g->phase == BB_PHASE_COUNTDOWN) {
        if (g->phase_ticks) g->phase_ticks--;
        if (!g->phase_ticks) {
            g->phase = BB_PHASE_PLAY;
            g->phase_ticks = (uint16_t)BB_MATCH_TICKS;
            g->sudden = 0;
        }
        return;
    }
    if (g->phase == BB_PHASE_RESULTS) {
        if (g->rematch_lock) g->rematch_lock--;
        if (g->phase_ticks) g->phase_ticks--;
        if (!g->phase_ticks) {
            for (unsigned i = 0; i < BB_PLAYERS; i++) {
                if (!g->p[i].active) continue;
                uint8_t active = g->p[i].active;
                uint8_t color = g->p[i].color;
                uint8_t bot = g->p[i].bot;
                memset(&g->p[i], 0, sizeof g->p[i]);
                g->p[i].active = active;
                g->p[i].color = color;
                g->p[i].bot = bot;
                g->p[i].ready = bot ? 1u : 0u;
                spawn_player(g, i);
                new_gate(g, &g->p[i]);
            }
            memset(g->e, 0, sizeof g->e);
            memset(g->shot, 0, sizeof g->shot);
            memset(g->wall, 0, sizeof g->wall);
            memset(g->pk, 0, sizeof g->pk);
            g->nwall = 0;
            g->sudden = 0;
            g->phase = BB_PHASE_LOBBY;
            fill_bots(g);
        }
        return;
    }

    if (g->bots_on) fill_bots(g);
    for (unsigned i = 0; i < BB_PLAYERS; i++) bot_think(g, i);
    for (unsigned i = 0; i < BB_PLAYERS; i++) player_step(g, i);
    if (g->phase != BB_PHASE_PLAY) return;
    if (g->phase_ticks) {
        g->phase_ticks--;
    } else if (!g->sudden) {
        if (unique_gate_leader(g) >= 0) finish_round(g);
        else g->sudden = 1;
    }
    if (g->phase != BB_PHASE_PLAY) return;
    if ((g->tick % 120u) == 0u &&
        bb_game_enemies(g) < 2u + bb_game_active(g)) {
        spawn_enemy(g);
    }
    if ((g->tick % 150u) == 0u) spawn_pickup(g);
    for (unsigned i = 0; i < BB_ENEMIES; i++) enemy_step(g, i);
    shots_step(g);
}
