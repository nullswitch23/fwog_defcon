#ifndef FWOG_BATTLEBRIDGE_GAME_H
#define FWOG_BATTLEBRIDGE_GAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BB_PLAYERS     4u
#define BB_ENEMIES     8u
#define BB_PROJECTILES 64u
#define BB_WALLS       8u
#define BB_PICKUPS     6u
#define BB_ARENA_W     960u
#define BB_ARENA_H     600u
#define BB_GATE_GOAL   12u
#define BB_MATCH_TICKS (90u * 30u)
#define BB_LASER_BOUNCE 4u

enum {
    BB_PHASE_LOBBY = 0,
    BB_PHASE_COUNTDOWN,
    BB_PHASE_PLAY,
    BB_PHASE_RESULTS,
};

enum {
    BB_WEAP_PEA = 0,
    BB_WEAP_LASER,
    BB_WEAP_SPRAY,
};

enum {
    BB_PK_HEALTH = 0,
    BB_PK_SHIELD,
    BB_PK_LASER,
    BB_PK_SPRAY,
    BB_PK_GRIP,
};

enum {
    BB_BOTS_OFF = 0,
    BB_BOTS_SLEEPY,
    BB_BOTS_NORMAL,
    BB_BOTS_MELEE,
};

typedef struct {
    int8_t move_x, move_y;
    int8_t aim_x, aim_y;
    uint8_t fire;
    uint8_t ready;
} bb_input_t;

typedef struct {
    int32_t x, y;      /* Q8 pixels */
    int32_t vx, vy;    /* Q8 pixels/tick */
    uint16_t gate_x, gate_y;
    int8_t aim_x, aim_y;
    int8_t move_x, move_y;
    int16_t score;
    uint8_t hp, gates, kills, color;
    uint8_t active, ready, fire, cooldown;
    uint8_t respawn, invul;
    uint8_t shield, weap, gate_c, bot;
    uint16_t weap_ttl, shield_ttl, grip_ttl;
    uint8_t streak;
} bb_player_t;

typedef struct {
    int32_t x, y;
    uint8_t hp, active, cooldown, target;
} bb_enemy_t;

typedef struct {
    int32_t x, y;
    int16_t vx, vy;
    uint8_t active, owner, ttl, hostile, kind, bounce;
} bb_projectile_t;

typedef struct {
    uint16_t x, y, w, h;
} bb_wall_t;

typedef struct {
    uint16_t x, y;
    uint8_t active, kind;
} bb_pickup_t;

typedef struct {
    bb_player_t p[BB_PLAYERS];
    bb_enemy_t e[BB_ENEMIES];
    bb_projectile_t shot[BB_PROJECTILES];
    bb_wall_t wall[BB_WALLS];
    bb_pickup_t pk[BB_PICKUPS];
    uint32_t rng;
    uint16_t tick;
    uint16_t phase_ticks;
    uint8_t phase;
    uint8_t nwall;
    uint8_t bots_on;
    uint8_t sudden;
    uint8_t teams;
    uint8_t rematch_lock;
} bb_game_t;

void bb_game_init(bb_game_t *g, uint32_t seed);
int bb_game_connect(bb_game_t *g);
void bb_game_disconnect(bb_game_t *g, unsigned slot);
void bb_game_input(bb_game_t *g, unsigned slot, const bb_input_t *in);
void bb_game_force_start(bb_game_t *g);
void bb_game_set_bots(bb_game_t *g, unsigned skill);
void bb_game_set_teams(bb_game_t *g, bool on);
void bb_game_step(bb_game_t *g);
unsigned bb_game_active(const bb_game_t *g);
unsigned bb_game_enemies(const bb_game_t *g);
unsigned bb_game_projectiles(const bb_game_t *g);
unsigned bb_game_team_gates(const bb_game_t *g, unsigned team);

#endif
