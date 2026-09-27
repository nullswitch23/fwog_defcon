/* VoltPet v005 -- eight foes, dash-in, foe counters, no spark trails. */
#include "fwog_display.h"
#include "vp_proto.h"
#include "vp_art.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

FWOG_POWER_DEFAULT();

#define MIC_GAIN     8
#define MIC_SNACK    600u
#define PET_W        64
#define PET_H        52
#define FIELD_Y0     36
#define FIELD_Y1     132
#define VP_HOLD_MS   750u
#define VP_RELEASE_MS 1400u
#define VP_NSPEC     5u
#define PET_HOME_X   36
#define PET_HOME_Y   88
#define FOE_HOME_X   216
#define FOE_HOME_Y   90
#define ARENA_X      8
#define ARENA_Y      76
#define ARENA_W      304
#define ARENA_H      112

enum {
    ANIM_IDLE = 0, ANIM_DASH, ANIM_SPARK, ANIM_CHEER, ANIM_FOE
};

enum {
    SCR_HOME = 0, SCR_TRAIN, SCR_SLEEP, SCR_FIGHT,
    SCR_EAT, SCR_RELIC, SCR_STORY, SCR_FIND, SCR_NAME
};

enum { ACT_TRAIN = 0, ACT_SLEEP, ACT_FIGHT, ACT_EAT, ACT_RELIC, ACT_STORY, ACT_N };

static void go_home(void);
static void hatch(uint32_t now);
static void fight_foe_turn(uint32_t now);

typedef struct {
    const char *name;
    uint8_t     hp;
    uint8_t     atk;
    uint8_t     xp;
} vp_foe_t;

static const vp_foe_t k_foe[] = {
    {"Dust Bunny",    8,  2, 10},
    {"Static Fluff", 11,  3, 14},
    {"Spark Kit",    14,  4, 20},
    {"Echo Moth",    16,  4, 24},
    {"Pollen Sprite",15,  4, 22},
    {"Volt Wisp",    18,  5, 28},
    {"Thunder Pup",  22,  6, 34},
    {"Cloud Ram",    24,  6, 38},
};
#define FOE_N ((unsigned)(sizeof k_foe / sizeof k_foe[0]))

static const char *const k_spec[VP_NSPEC] = {
    "SUNCHEEK", "GOLDLOOP", "BOLTQUILL", "MEADOW", "RINGSPARK"
};

static const char *const k_act[ACT_N] = {
    "Train", "Sleep", "Fight", "Eat", "Relics", "Story"
};

static const char *const k_story[] = {
    "Isle of Ring is a humming meadow. Radio dew sits on the grass. VoltPets live there.",
    "They store sunshine in their cheeks, like a friend who always has a spark to share.",
    "When they run, the world blurs blue-gold. Quills whistle. They loop home before lunch.",
    "A VoltPet chooses you when it hears a kind frequency -- a laugh, a song, a stray beacon.",
    "Feed it shakes, songs, and radio fruit. Train together. Nap in the shade. Play-fight fluff.",
    "Treasures ride the airwaves. One hundred relics wait in the static, each with a story.",
};
#define STORY_N ((unsigned)(sizeof k_story / sizeof k_story[0]))

static bool           s_lcd, s_leds, s_accel, s_link, s_pdm;
static bool           s_chrome_dirty = true;
static bool           s_power_armed, s_saved_this_arm, s_loaded, s_dirty;
static bool           s_need_hatch;
static fwog_link_rx_t s_rx;
static uint8_t        s_hunger = 40, s_happy = 70, s_energy = 80, s_age;
static uint8_t        s_level = 1, s_art_count, s_last_art = 0xFF;
static uint8_t        s_art_bits[VP_ART_BYTES];
static uint16_t       s_xp, s_fights;
static uint32_t       s_steps, s_last_tick, s_last_paint, s_last_push;
static uint32_t       s_walk_acc, s_enc_due, s_rng = 1u;
static int16_t        s_rssi = -127;
static uint32_t       s_radio_hz;
static bool           s_radio_ok;
static uint8_t        s_scr = SCR_HOME, s_act, s_story_i, s_relic_i;
static int16_t        s_px = 132, s_py = 56, s_tx = 180, s_ty = 70;
static int16_t        s_lx = -1, s_ly = -1;
static int16_t        s_fx = -1, s_fy = -1;
static uint8_t        s_frame, s_php, s_fhp, s_foe, s_charged;
static uint8_t        s_train_pts, s_species, s_neglect;
static uint8_t        s_anim, s_move;
static uint32_t       s_train_until, s_find_until, s_led_until;
static uint32_t       s_anim_t0, s_anim_ms;
static bool           s_find_dup, s_toast_on;
static char           s_toast[44];
static uint32_t       s_toast_until;
static uint32_t       s_mic_rms;
static int16_t        s_pcm[PDM_SAMPLE_BUFFER_SIZE];
static fwog_cic_t     s_cic;
static char           s_name[VP_NAME_N];
static char           s_t9_buf[VP_NAME_N];
static int            s_t9_g, s_t9_li;
static uint32_t       s_yel_ms, s_grn_ms, s_gry_ms;
static bool           s_yel_was, s_grn_was, s_blu_was, s_gry_was;
static bool           s_yel_hold, s_grn_hold, s_gry_hold;

static uint16_t col_bg(void)  { return st7789_rgb565(8, 10, 18); }
static uint16_t col_fg(void)  { return st7789_rgb565(230, 230, 230); }
static uint16_t col_acc(void) { return st7789_rgb565(80, 200, 120); }
static uint16_t col_dim(void) { return st7789_rgb565(140, 150, 170); }
static uint16_t col_yel(void) { return st7789_rgb565(255, 214, 48); }

static uint32_t rnd(void) {
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

static const char *pet_label(void) {
    if (s_name[0]) return s_name;
    return k_spec[s_species < VP_NSPEC ? s_species : 0u];
}

static void toast(const char *s, uint32_t now, uint32_t ms) {
    snprintf(s_toast, sizeof s_toast, "%s", s);
    s_toast_on = true;
    s_toast_until = now + ms;
}

static void lcd_bringup(void) {
    st7789_init_begin();
    const absolute_time_t deadline = make_timeout_time_ms(500);
    while (!st7789_ready() && !time_reached(deadline)) {
        st7789_init_step();
        sleep_ms(1);
    }
    s_lcd = st7789_ready();
    if (s_lcd) {
        st7789_clear(col_bg());
        board_backlight(255);
        fwog_splash_boot();
        s_chrome_dirty = true;
    }
}

static void pack_save(vp_save_t *m, uint8_t cmd) {
    memset(m, 0, sizeof *m);
    m->type = VP_MSG_CMD;
    m->cmd = cmd;
    m->hunger = s_hunger;
    m->happy = s_happy;
    m->energy = s_energy;
    m->flags = (uint8_t)(1u | ((s_scr == SCR_SLEEP) ? 2u : 0u));
    m->xp = s_xp;
    m->level = s_level;
    m->age = s_age;
    m->fights_won = s_fights;
    m->steps = s_steps;
    m->magic = VP_MAGIC;
    m->last_art = s_last_art;
    m->art_count = s_art_count;
    memcpy(m->art_bits, s_art_bits, VP_ART_BYTES);
    memcpy(m->name, s_name, VP_NAME_N);
    m->name[VP_NAME_N - 1u] = '\0';
    m->_pad = s_species;
}

static void send_cmd(uint8_t cmd) {
    vp_save_t m;
    pack_save(&m, cmd);
    if (s_link) (void)fwog_link_uart_send_frame(&m, sizeof m);
}

static void apply_save(const vp_save_t *in) {
    s_hunger = in->hunger;
    s_happy = in->happy;
    s_energy = in->energy ? in->energy : 80u;
    s_xp = in->xp;
    s_level = in->level ? in->level : vp_level_for_xp(s_xp);
    s_age = in->age;
    s_fights = in->fights_won;
    s_steps = in->steps;
    s_last_art = in->last_art;
    s_art_count = in->art_count;
    memcpy(s_art_bits, in->art_bits, VP_ART_BYTES);
    memcpy(s_name, in->name, VP_NAME_N);
    vp_name_sanitize(s_name);
    if (in->_pad < VP_NSPEC) {
        s_species = in->_pad;
        s_need_hatch = false;
    } else {
        s_need_hatch = true;
    }
    s_loaded = true;
}

static void grant_xp(uint16_t add, uint32_t now) {
    uint16_t nx = (uint16_t)(s_xp + add);
    if (nx < s_xp) nx = 800;
    s_xp = nx;
    const uint8_t lv = vp_level_for_xp(s_xp);
    if (lv > s_level) {
        s_level = lv;
        char msg[44];
        snprintf(msg, sizeof msg, "Level up! %s sparkles!", pet_label());
        toast(msg, now, 2500u);
        if (s_happy < 230u) s_happy = (uint8_t)(s_happy + 16u);
    } else {
        s_level = lv;
    }
    s_dirty = true;
}

static unsigned art_nth(unsigned n) {
    unsigned seen = 0;
    for (unsigned i = 0; i < VP_NART; i++) {
        if (vp_art_owned(s_art_bits, i)) {
            if (seen == n) return i;
            seen++;
        }
    }
    return 0;
}

static void grant_art(uint8_t id, uint32_t now) {
    if (id >= VP_NART) id = (uint8_t)(id % VP_NART);
    s_find_dup = vp_art_owned(s_art_bits, id);
    if (!s_find_dup) {
        vp_art_give(s_art_bits, id);
        if (s_art_count < VP_NART) s_art_count++;
        if (s_happy < 240u) s_happy = (uint8_t)(s_happy + 10u);
    }
    s_last_art = id;
    s_scr = SCR_FIND;
    s_find_until = now + 5000u;
    s_chrome_dirty = true;
    s_lx = s_ly = -1;
    s_dirty = true;
}

static void feed(uint8_t kind, uint32_t now) {
    if (s_hunger == 0u) {
        toast("So full! Maybe a nap?", now, 1800u);
        return;
    }
    const uint8_t cut = (kind == 2u) ? 36u : 28u;
    s_hunger = (s_hunger > cut) ? (uint8_t)(s_hunger - cut) : 0u;
    if (s_happy < 240u) s_happy = (uint8_t)(s_happy + 10u);
    if (s_energy < 245u) s_energy = (uint8_t)(s_energy + 6u);
    s_dirty = true;
    if (kind == 0u) toast("Crunchy kinetic berry!", now, 1800u);
    else if (kind == 1u) toast("A song-shaped snack!", now, 1800u);
    else toast("Sparkfruit from the airwaves!", now, 1800u);
}

static uint8_t foe_hp(void) {
    unsigned h = (unsigned)k_foe[s_foe].hp + (unsigned)s_level + (unsigned)s_level / 3u;
    if (h > 48u) h = 48u;
    return (uint8_t)h;
}

static uint8_t foe_atk(void) {
    unsigned a = (unsigned)k_foe[s_foe].atk + (unsigned)s_level / 3u;
    if (a > 14u) a = 14u;
    return (uint8_t)a;
}

static void action_flash(uint32_t now, uint32_t ms) {
    s_led_until = now + ms;
}

static void start_fight(uint32_t now) {
    unsigned cap = 2u + (unsigned)s_level / 2u;
    if (cap > FOE_N) cap = FOE_N;
    s_foe = (uint8_t)(rnd() % cap);
    s_fhp = foe_hp();
    s_php = (uint8_t)(10u + s_level + s_energy / 50u);
    s_charged = 0;
    s_anim = ANIM_IDLE;
    s_move = 0;
    s_scr = SCR_FIGHT;
    s_chrome_dirty = true;
    s_lx = s_ly = s_fx = s_fy = -1;
    (void)now;
}

static void start_train(uint32_t now) {
    s_train_pts = 0;
    s_train_until = now + 5000u;
    s_scr = SCR_TRAIN;
    s_chrome_dirty = true;
    s_lx = s_ly = -1;
}

static void poll_link(uint32_t now) {
    uint8_t b;
    size_t n;
    while (fwog_link_uart_read(&b)) {
        if (!fwog_link_rx_byte(&s_rx, b, &n)) continue;
        if (fwog_ioexp_link_handle(s_rx.buf, n)) continue;
        if (n >= sizeof(vp_save_t) && s_rx.buf[0] == VP_MSG_ST) {
            vp_save_t in;
            if (vp_save_from_bytes(&in, s_rx.buf, sizeof(vp_save_t)))
                apply_save(&in);
            continue;
        }
        if (n >= sizeof(vp_rf_t) && s_rx.buf[0] == VP_MSG_RF) {
            vp_rf_t rf;
            memcpy(&rf, s_rx.buf, sizeof rf);
            s_radio_ok = rf.ok != 0u;
            s_rssi = rf.rssi;
            s_radio_hz = rf.hz;
            if (rf.burst) {
                if (s_scr == SCR_EAT) feed(2u, now);
                grant_art(rf.art_id, now);
            }
        }
    }
}

static void draw_wrap(uint16_t x, uint16_t y, const char *s,
                     unsigned cols, unsigned max_lines, uint16_t fg, uint16_t bg) {
    char buf[48];
    unsigned line = 0;
    if (cols >= sizeof buf) cols = sizeof buf - 1u;
    while (s && *s && line < max_lines) {
        unsigned n = 0;
        unsigned brk = 0;
        while (s[n] && n < cols) {
            if (s[n] == ' ') brk = n;
            n++;
        }
        if (s[n] && brk > 0u) n = brk;
        memcpy(buf, s, n);
        buf[n] = 0;
        lcd_text_draw_padded(x, (uint16_t)(y + line * 12u), buf, cols, 1, fg, bg);
        s += n;
        while (*s == ' ') s++;
        line++;
    }
    while (line < max_lines) {
        lcd_text_draw_padded(x, (uint16_t)(y + line * 12u), "", cols, 1, fg, bg);
        line++;
    }
}

static void erase_sprite(int16_t *lx, int16_t *ly) {
    if (*lx < 0) return;
    int16_t x = (int16_t)(*lx - 6);
    int16_t y = (int16_t)(*ly - 4);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    st7789_fill_rect((uint16_t)x, (uint16_t)y, PET_W + 24u, PET_H + 16u, col_bg());
    *lx = *ly = -1;
}

static void erase_pet(void) {
    erase_sprite(&s_lx, &s_ly);
}

static void draw_pet(int16_t x, int16_t y, unsigned pose) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    erase_pet();
    const int bob = (pose == 3u || pose == 4u) ? 0 : (int)((s_frame / 4u) & 1u);
    const int run = (pose == 1u) ? (int)((s_frame / 2u) & 1u) * 2 : 0;
    st7789_fill_rect((uint16_t)x, (uint16_t)(y > 2 ? y - 2 : 0), PET_W, PET_H + 4u, col_bg());
    uint8_t br = 255, bgc = 214, bb = 48;
    uint8_t qr = 255, qg = 180, qb = 40;
    uint8_t cr = 230, cg = 70, cb = 70;
    if (s_species == 1u) { br = 210; bgc = 190; bb = 40; qr = 80; qg = 200; qb = 90; }
    else if (s_species == 2u) { br = 240; bgc = 240; bb = 250; qr = 70; qg = 140; qb = 255; cr = 90; cg = 120; cb = 230; }
    else if (s_species == 3u) { br = 120; bgc = 210; bb = 90; qr = 40; qg = 160; qb = 70; cr = 240; cg = 120; cb = 60; }
    else if (s_species == 4u) { br = 255; bgc = 160; bb = 200; qr = 255; qg = 90; qb = 140; cr = 255; cg = 80; cb = 110; }
    const uint16_t body = st7789_rgb565(br, bgc, bb);
    const uint16_t dark = st7789_rgb565(40, 30, 20);
    const uint16_t cheek = st7789_rgb565(cr, cg, cb);
    const uint16_t shoe  = st7789_rgb565(220, 40, 50);
    const uint16_t white = st7789_rgb565(250, 250, 250);
    const uint16_t quill = st7789_rgb565(qr, qg, qb);
    const uint16_t bolt  = st7789_rgb565(255, 255, 180);
    const int yy = y + bob;
    st7789_fill_rect((uint16_t)(x + 28), (uint16_t)(yy + 2), 10, 14, quill);
    st7789_fill_rect((uint16_t)(x + 36), (uint16_t)(yy + 6), 10, 12, quill);
    st7789_fill_rect((uint16_t)(x + 22), (uint16_t)(yy + 8), 8, 10, quill);
    st7789_fill_rect((uint16_t)(x + 8), (uint16_t)(yy + 10), 28, 20, body);
    st7789_fill_rect((uint16_t)(x + 12), (uint16_t)(yy + 18), 34, 18, body);
    st7789_fill_rect((uint16_t)(x + 10), (uint16_t)(yy + 4), 8, 10, body);
    st7789_fill_rect((uint16_t)(x + 24), (uint16_t)(yy + 2), 8, 10, body);
    if (pose == 3u || (s_frame % 28u) == 27u) {
        st7789_fill_rect((uint16_t)(x + 14), (uint16_t)(yy + 18), 6, 2, dark);
        st7789_fill_rect((uint16_t)(x + 26), (uint16_t)(yy + 18), 6, 2, dark);
    } else {
        st7789_fill_rect((uint16_t)(x + 14), (uint16_t)(yy + 16), 6, 6, white);
        st7789_fill_rect((uint16_t)(x + 26), (uint16_t)(yy + 16), 6, 6, white);
        st7789_fill_rect((uint16_t)(x + 16), (uint16_t)(yy + 18), 3, 3, dark);
        st7789_fill_rect((uint16_t)(x + 28), (uint16_t)(yy + 18), 3, 3, dark);
    }
    st7789_fill_rect((uint16_t)(x + 8), (uint16_t)(yy + 22), 7, 6, cheek);
    st7789_fill_rect((uint16_t)(x + 31), (uint16_t)(yy + 22), 7, 6, cheek);
    if (pose == 4u) {
        st7789_fill_rect((uint16_t)(x + 4), (uint16_t)(yy + 14), 4, 4, bolt);
        st7789_fill_rect((uint16_t)(x + 40), (uint16_t)(yy + 10), 4, 4, bolt);
        st7789_fill_rect((uint16_t)(x + 46), (uint16_t)(yy + 22), 4, 4, bolt);
    }
    if (pose == 5u) {
        st7789_fill_rect((uint16_t)(x + 20), (uint16_t)(yy + 28), 12, 6, dark);
    } else if (pose != 3u) {
        const uint16_t mouth = (s_hunger > 80u) ? cheek : dark;
        st7789_fill_rect((uint16_t)(x + 20), (uint16_t)(yy + 28), 12, 3, mouth);
    }
    if (pose != 3u) {
        st7789_fill_rect((uint16_t)(x + 12 + run), (uint16_t)(yy + 38), 12, 6, shoe);
        st7789_fill_rect((uint16_t)(x + 28 - run), (uint16_t)(yy + 38), 12, 6, shoe);
    } else {
        st7789_fill_rect((uint16_t)(x + 8), (uint16_t)(yy + 34), 14, 6, shoe);
        lcd_text_draw_padded((uint16_t)(x + 44), (uint16_t)(yy + 4), "Zzz", 3, 1,
                             col_dim(), col_bg());
    }
    s_lx = x;
    s_ly = (int16_t)(y > 2 ? y - 2 : y);
}

static void draw_foe(int16_t x, int16_t y, unsigned strike) {
    erase_sprite(&s_fx, &s_fy);
    st7789_fill_rect((uint16_t)x, (uint16_t)y, 56, 48, col_bg());
    const uint16_t dark = st7789_rgb565(30, 20, 40);
    const uint16_t eye  = st7789_rgb565(250, 250, 250);
    const int bob = strike ? 0 : (int)((s_frame / 3u) & 1u);
    const int jab = strike ? (int)((s_frame & 1u) * 6) : 0;
    const uint8_t id = (s_foe < FOE_N) ? s_foe : 0u;
    uint16_t body = st7789_rgb565(170, 120, 220);
    if (id == 1u) body = st7789_rgb565(240, 230, 120);
    else if (id == 2u) body = st7789_rgb565(255, 150, 70);
    else if (id == 3u) body = st7789_rgb565(90, 70, 160);
    else if (id == 4u) body = st7789_rgb565(120, 210, 90);
    else if (id == 5u) body = st7789_rgb565(80, 220, 230);
    else if (id == 6u) body = st7789_rgb565(80, 110, 210);
    else if (id == 7u) body = st7789_rgb565(190, 200, 210);
    const int yy = y + bob;
    if (id == 3u) {
        st7789_fill_rect((uint16_t)(x - 4 + jab), (uint16_t)(yy + 10), 18, 22, body);
        st7789_fill_rect((uint16_t)(x + 28 - jab), (uint16_t)(yy + 10), 18, 22, body);
        st7789_fill_rect((uint16_t)(x + 10), (uint16_t)(yy + 14), 22, 16, body);
    } else if (id == 5u) {
        st7789_fill_rect((uint16_t)(x + 8), (uint16_t)(yy + 8), 28, 28, body);
        st7789_fill_rect((uint16_t)(x + 2 + jab), (uint16_t)(yy + 4), 8, 8, body);
        st7789_fill_rect((uint16_t)(x + 36), (uint16_t)(yy + 20), 8, 8, body);
    } else if (id == 7u) {
        st7789_fill_rect((uint16_t)(x + 4), (uint16_t)(yy + 14), 40, 24, body);
        st7789_fill_rect((uint16_t)(x + 8 - jab), (uint16_t)(yy + 2), 8, 16, eye);
        st7789_fill_rect((uint16_t)(x + 28 - jab), (uint16_t)(yy + 2), 8, 16, eye);
    } else {
        st7789_fill_rect((uint16_t)(x + jab), (uint16_t)(yy + 8), 40, 28, body);
        st7789_fill_rect((uint16_t)(x + 8 + jab), (uint16_t)(yy), 24, 16, body);
        if (id == 2u || id == 6u) {
            st7789_fill_rect((uint16_t)(x + 6 + jab), (uint16_t)(yy), 8, 12, body);
            st7789_fill_rect((uint16_t)(x + 26 + jab), (uint16_t)(yy), 8, 12, body);
        }
        if (id == 4u) {
            st7789_fill_rect((uint16_t)(x + 16), (uint16_t)(yy + 2), 6, 6, st7789_rgb565(255, 240, 80));
            st7789_fill_rect((uint16_t)(x + 28), (uint16_t)(yy + 30), 6, 6, st7789_rgb565(255, 240, 80));
        }
    }
    st7789_fill_rect((uint16_t)(x + 12 + jab), (uint16_t)(yy + 12), 6, 6, eye);
    st7789_fill_rect((uint16_t)(x + 24 + jab), (uint16_t)(yy + 12), 6, 6, eye);
    st7789_fill_rect((uint16_t)(x + 14 + jab), (uint16_t)(yy + 14), 3, 3, dark);
    st7789_fill_rect((uint16_t)(x + 26 + jab), (uint16_t)(yy + 14), 3, 3, dark);
    if (strike && (id == 1u || id == 5u)) {
        st7789_fill_rect((uint16_t)(x - 2), (uint16_t)(yy + 18), 4, 4, eye);
        st7789_fill_rect((uint16_t)(x + 44), (uint16_t)(yy + 8), 4, 4, eye);
    }
    s_fx = x;
    s_fy = y;
}

static void wander(void) {
    if (s_px < s_tx) s_px = (int16_t)(s_px + 2);
    else if (s_px > s_tx) s_px = (int16_t)(s_px - 2);
    if (s_py < s_ty) s_py++;
    else if (s_py > s_ty) s_py--;
    const int dx = s_px - s_tx;
    const int dy = s_py - s_ty;
    if (dx * dx + dy * dy < 16) {
        s_tx = (int16_t)(12 + (rnd() % 240u));
        s_ty = (int16_t)(FIELD_Y0 + 8 + (rnd() % 40u));
    }
    if (s_px < 8) s_px = 8;
    if (s_px > 256) s_px = 256;
    if (s_py < FIELD_Y0) s_py = FIELD_Y0;
    if (s_py > FIELD_Y1 - PET_H) s_py = (int16_t)(FIELD_Y1 - PET_H);
}

static void t9_begin(void) {
    memset(s_t9_buf, 0, sizeof s_t9_buf);
    if (s_name[0]) memcpy(s_t9_buf, s_name, VP_NAME_N);
    s_t9_g = 0;
    s_t9_li = 0;
    s_scr = SCR_NAME;
    s_chrome_dirty = true;
    s_lx = s_ly = -1;
}

static void t9_finish(uint32_t now) {
    memcpy(s_name, s_t9_buf, VP_NAME_N);
    vp_name_sanitize(s_name);
    s_dirty = true;
    send_cmd(VP_CMD_PUSH);
    s_yel_hold = s_grn_hold = s_gry_hold = true;
    if (s_name[0]) toast(s_name, now, 1800u);
    else toast("Unnamed spark.", now, 1800u);
    go_home();
}

static void paint_t9(void) {
    const uint16_t bg = col_bg(), fg = col_fg(), acc = col_acc(), dim = col_dim();
    char line[24];
    char ch[2];
    lcd_text_draw_padded(8, 44, "name", 16, 1, dim, bg);
    snprintf(line, sizeof line, "[%s]", s_t9_buf);
    lcd_text_draw_padded(8, 56, line, 12, 2, fg, bg);
    lcd_text_draw_padded(8, 92, "grp", 8, 1, dim, bg);
    lcd_text_draw_padded(8, 104, fwog_t9_group[s_t9_g], 5, 4, acc, bg);
    lcd_text_draw_padded(180, 92, "char", 8, 1, dim, bg);
    ch[0] = fwog_t9_cur(s_t9_g, s_t9_li);
    ch[1] = '\0';
    lcd_text_draw_padded(180, 104, ch, 2, 4, fg, bg);
    lcd_text_draw_padded(8, 176, "GRY/RED grp  YEL/BLU char", 40, 1, dim, bg);
    lcd_text_draw_padded(8, 192, "GRN tap add  hold done", 40, 1, dim, bg);
    lcd_text_draw_padded(8, 208, "YEL hold backspace", 40, 1, dim, bg);
}

static void paint_home(void) {
    const uint16_t bg = col_bg(), fg = col_fg(), acc = col_acc(), dim = col_dim(), yel = col_yel();
    char line[48];
    wander();
    erase_pet();
    const unsigned moving = (s_px != s_tx) ? 1u : 0u;
    draw_pet(s_px, s_py, moving);
    snprintf(line, sizeof line, "hunger %3u  energy %3u  happy %3u",
             (unsigned)s_hunger, (unsigned)s_energy, (unsigned)s_happy);
    lcd_text_draw_padded(8, 136, line, 42, 1, fg, bg);
    snprintf(line, sizeof line, "xp %u  steps %u  relics %u/100",
             (unsigned)s_xp, (unsigned)s_steps, (unsigned)s_art_count);
    lcd_text_draw_padded(8, 148, line, 42, 1, dim, bg);
    if (s_radio_ok) {
        snprintf(line, sizeof line, "radio %ld dBm  %lu kHz",
                 (long)s_rssi, (unsigned long)(s_radio_hz / 1000u));
    } else {
        snprintf(line, sizeof line, "radio quiet -- waiting for a wave");
    }
    lcd_text_draw_padded(8, 160, line, 42, 1, dim, bg);
    for (unsigned i = 0; i < ACT_N; i++) {
        const unsigned col = i % 3u;
        const unsigned row = i / 3u;
        const uint16_t x = (uint16_t)(8u + col * 104u);
        const uint16_t y = (uint16_t)(176u + row * 16u);
        char cell[16];
        snprintf(cell, sizeof cell, "%s%-7s", i == s_act ? ">" : " ", k_act[i]);
        lcd_text_draw_padded(x, y, cell, 12, 1, i == s_act ? yel : dim, bg);
    }
    lcd_text_draw_padded(8, 212, "Y/B pick  Green go  Gray relics", 42, 1, acc, bg);
    lcd_text_draw_padded(8, 224, "Y hold new spark. Gray hold name.", 42, 1, dim, bg);
}

static void paint_overlay(uint32_t now) {
    const uint16_t bg = col_bg(), fg = col_fg(), acc = col_acc(), dim = col_dim(), yel = col_yel();
    char line[48];
    if (s_scr == SCR_TRAIN) {
        const uint32_t left = (s_train_until > now) ? (s_train_until - now) : 0u;
        lcd_text_draw_padded(8, 44, "Training montage!", 40, 1, yel, bg);
        lcd_text_draw_padded(8, 60, "Mash Green or shake the board.", 40, 1, fg, bg);
        snprintf(line, sizeof line, "effort %u   %u.%us left",
                 (unsigned)s_train_pts, (unsigned)(left / 1000u),
                 (unsigned)((left / 100u) % 10u));
        lcd_text_draw_padded(8, 76, line, 40, 1, acc, bg);
        draw_pet(132, 92, 1u);
        lcd_text_draw_padded(8, 212, "A good sweat earns experience.", 40, 1, dim, bg);
        return;
    }
    if (s_scr == SCR_SLEEP) {
        lcd_text_draw_padded(8, 44, "Napping in the meadow shade.", 40, 1, yel, bg);
        snprintf(line, sizeof line, "energy %u  dreams of looping the world",
                 (unsigned)s_energy);
        lcd_text_draw_padded(8, 60, line, 42, 1, fg, bg);
        draw_pet(132, 88, 3u);
        lcd_text_draw_padded(8, 212, "Green wakes.", 40, 1, dim, bg);
        return;
    }
    if (s_scr == SCR_FIGHT) {
        snprintf(line, sizeof line, "A wild %s wants to play!", k_foe[s_foe].name);
        lcd_text_draw_padded(8, 44, line, 42, 1, yel, bg);
        snprintf(line, sizeof line, "you %u hp    foe %u hp%s",
                 (unsigned)s_php, (unsigned)s_fhp,
                 s_anim == ANIM_DASH ? "  DASH" :
                 (s_anim == ANIM_FOE ? "  !!" : ""));
        lcd_text_draw_padded(8, 60, line, 42, 1, fg, bg);
        st7789_fill_rect(ARENA_X, ARENA_Y, ARENA_W, ARENA_H, bg);
        s_lx = s_ly = s_fx = s_fy = -1;
        {
            uint32_t t = (now >= s_anim_t0) ? (now - s_anim_t0) : 0u;
            int16_t px = PET_HOME_X, py = PET_HOME_Y;
            int16_t fx = FOE_HOME_X, fy = FOE_HOME_Y;
            unsigned ppose = 0u, strike = 0u;
            if (s_anim == ANIM_DASH && s_anim_ms > 0u) {
                const uint32_t mid = s_anim_ms / 2u;
                const int16_t peak = (int16_t)(FOE_HOME_X - 72);
                ppose = 1u;
                if (t < mid && mid)
                    px = (int16_t)(PET_HOME_X + (int16_t)(((int)peak - PET_HOME_X) * (int)t / (int)mid));
                else if (s_anim_ms > mid) {
                    const uint32_t u = t - mid;
                    px = (int16_t)(peak - (int16_t)(((int)peak - PET_HOME_X) * (int)u /
                                                    (int)(s_anim_ms - mid)));
                }
            } else if (s_anim == ANIM_SPARK) {
                ppose = 4u;
            } else if (s_anim == ANIM_CHEER) {
                ppose = 5u;
                py = (int16_t)(PET_HOME_Y - 6);
            } else if (s_anim == ANIM_FOE && s_anim_ms > 0u) {
                strike = 1u;
                const uint32_t mid = s_anim_ms / 2u;
                if (t < mid && mid)
                    fx = (int16_t)(FOE_HOME_X - (int16_t)(52 * (int)t / (int)mid));
                else if (s_anim_ms > mid) {
                    const uint32_t u = t - mid;
                    fx = (int16_t)(FOE_HOME_X - 52 + (int16_t)(52 * (int)u / (int)(s_anim_ms - mid)));
                }
            }
            draw_pet(px, py, ppose);
            draw_foe(fx, fy, strike);
        }
        lcd_text_draw_padded(8, 196, "Green Spark  Y Dash  B Cheer", 42, 1, acc, bg);
        lcd_text_draw_padded(8, 212, "Play-fights earn experience.", 42, 1, dim, bg);
        return;
    }
    if (s_scr == SCR_EAT) {
        lcd_text_draw_padded(8, 44, "Snack time!", 40, 1, yel, bg);
        lcd_text_draw_padded(8, 60, "Shake me, sing, or wait for radio.", 40, 1, fg, bg);
        snprintf(line, sizeof line, "hunger %u  mic %u  radio %ld dBm",
                 (unsigned)s_hunger, (unsigned)s_mic_rms, (long)s_rssi);
        lcd_text_draw_padded(8, 76, line, 42, 1, dim, bg);
        erase_pet();
        draw_pet(132, 96, 5u);
        lcd_text_draw_padded(8, 212, "Green heads home still hungry.", 40, 1, dim, bg);
        return;
    }
    if (s_scr == SCR_STORY) {
        lcd_text_draw_padded(8, 44, "Who is a VoltPet?", 40, 1, yel, bg);
        draw_wrap(8, 64, k_story[s_story_i], 40, 6, fg, bg);
        snprintf(line, sizeof line, "page %u/%u   Blue next  Green home",
                 s_story_i + 1u, STORY_N);
        lcd_text_draw_padded(8, 212, line, 42, 1, acc, bg);
        return;
    }
    if (s_scr == SCR_RELIC) {
        if (s_art_count == 0u) {
            lcd_text_draw_padded(8, 44, "No relics yet.", 40, 1, yel, bg);
            lcd_text_draw_padded(8, 64, "Park near a quiet radio burst.", 40, 1, fg, bg);
            lcd_text_draw_padded(8, 80, "Kind frequencies leave treasures.", 40, 1, dim, bg);
            lcd_text_draw_padded(8, 212, "Green home", 40, 1, acc, bg);
            return;
        }
        if (s_relic_i >= s_art_count) s_relic_i = 0;
        const unsigned id = art_nth(s_relic_i);
        const vp_art_t *a = vp_art_get(id);
        snprintf(line, sizeof line, "Relic %u/%u",
                 (unsigned)s_relic_i + 1u, (unsigned)s_art_count);
        lcd_text_draw_padded(8, 44, line, 40, 1, acc, bg);
        lcd_text_draw_padded(8, 60, a->name, 40, 1, yel, bg);
        snprintf(line, sizeof line, "%s   %s", a->slot, a->bonus);
        lcd_text_draw_padded(8, 76, line, 40, 1, fg, bg);
        draw_wrap(8, 96, a->lore, 40, 4, dim, bg);
        lcd_text_draw_padded(8, 212, "Y/B page  Green home", 40, 1, acc, bg);
        return;
    }
    if (s_scr == SCR_FIND) {
        const vp_art_t *a = vp_art_get(s_last_art);
        lcd_text_draw_padded(8, 44,
                             s_find_dup ? "A familiar spark in the air!" : "A relic rode in on the radio!",
                             42, 1, yel, bg);
        lcd_text_draw_padded(8, 64, a->name, 40, 1, fg, bg);
        snprintf(line, sizeof line, "%s   %s", a->slot, a->bonus);
        lcd_text_draw_padded(8, 80, line, 40, 1, acc, bg);
        draw_wrap(8, 100, a->lore, 40, 4, dim, bg);
        lcd_text_draw_padded(8, 212, "Green keeps it. Gray opens bag.", 40, 1, acc, bg);
        return;
    }
    if (s_scr == SCR_NAME) paint_t9();
}

static void paint(uint32_t now) {
    if (!s_lcd) return;
    const uint16_t bg = col_bg(), acc = col_acc(), dim = col_dim();
    char line[48];
    if (s_chrome_dirty) {
        st7789_fill_rect(0, 0, 320, 240, bg);
        lcd_text_draw_padded(8, 8, pet_label(), 12, 2, acc, bg);
        s_lx = s_ly = s_fx = s_fy = -1;
        s_chrome_dirty = false;
    }
    snprintf(line, sizeof line, "Lv.%u %s", (unsigned)s_level,
             k_spec[s_species < VP_NSPEC ? s_species : 0u]);
    lcd_text_draw_padded(200, 12, line, 18, 1, dim, bg);
    if (s_scr == SCR_HOME) paint_home();
    else paint_overlay(now);
    if (s_toast_on) {
        lcd_text_draw_padded(8, 224, s_toast, 42, 1, col_yel(), bg);
        if ((int32_t)(now - s_toast_until) >= 0) {
            s_toast_on = false;
            lcd_text_draw_padded(8, 224, "", 42, 1, dim, bg);
        }
    }
}

static void go_home(void) {
    s_scr = SCR_HOME;
    s_chrome_dirty = true;
    s_lx = s_ly = s_fx = s_fy = -1;
}

static void hatch(uint32_t now) {
    s_species = (uint8_t)(rnd() % VP_NSPEC);
    memset(s_name, 0, sizeof s_name);
    s_hunger = 40;
    s_happy = 70;
    s_energy = 80;
    s_xp = 0;
    s_level = 1;
    s_age = 0;
    s_fights = 0;
    s_steps = 0;
    s_art_count = 0;
    s_last_art = 0xFF;
    memset(s_art_bits, 0, sizeof s_art_bits);
    s_neglect = 0;
    s_need_hatch = false;
    s_loaded = true;
    s_dirty = true;
    char msg[44];
    snprintf(msg, sizeof msg, "A %s chose you!", k_spec[s_species]);
    toast(msg, now, 2800u);
    go_home();
    send_cmd(VP_CMD_PUSH);
    s_last_push = now;
}

static bool shaken(void) {
    lis3dh_sample_t samp;
    samp.x = samp.y = samp.z = 0x7FFF;
    if (!s_accel || !lis3dh_process(LIS3DH_MOVE_THRESHOLD_DEFAULT, &samp, NULL))
        return false;
    if (samp.x == 0x7FFF && samp.y == 0x7FFF && samp.z == 0x7FFF) return false;
    const int32_t ax = lis3dh_raw_to_mg(samp.x, LIS3DH_RANGE_2G);
    const int32_t ay = lis3dh_raw_to_mg(samp.y, LIS3DH_RANGE_2G);
    const int32_t az = lis3dh_raw_to_mg(samp.z, LIS3DH_RANGE_2G);
    const int64_t m2 = (int64_t)ax * ax + (int64_t)ay * ay + (int64_t)az * az;
    return m2 > (int64_t)1300 * 1300 || m2 < (int64_t)700 * 700;
}

static void take_mic(void) {
    const uint8_t *raw;
    size_t len;
    if (!s_pdm || !pdm_mic_take_raw_buffer(&raw, &len)) return;
    const size_t n = pdm_mic_decode(&s_cic, raw, len, s_pcm);
    if (n != PDM_SAMPLE_BUFFER_SIZE) return;
    for (size_t i = 0; i < n; i++) {
        const int32_t v = (int32_t)s_pcm[i] * MIC_GAIN;
        if (v > 32767) s_pcm[i] = 32767;
        else if (v < -32768) s_pcm[i] = (int16_t)-32768;
        else s_pcm[i] = (int16_t)v;
    }
    s_mic_rms = fwog_rms_i16(s_pcm, (unsigned)n);
}

static void fight_apply_player(uint32_t now) {
    if (s_move == ANIM_CHEER) {
        s_php = (uint8_t)(s_php + 3u);
        if (s_energy > 2u) s_energy = (uint8_t)(s_energy - 2u);
        return;
    }
    uint8_t dmg = (uint8_t)(2u + s_level / 3u);
    if (s_move == ANIM_DASH) dmg = (uint8_t)(dmg + 3u);
    if (s_fhp > dmg) s_fhp = (uint8_t)(s_fhp - dmg);
    else s_fhp = 0;
    if (s_fhp == 0u) {
        s_fights++;
        if (s_happy < 240u) s_happy = (uint8_t)(s_happy + 12u);
        if (s_energy > 6u) s_energy = (uint8_t)(s_energy - 6u);
        grant_xp(k_foe[s_foe].xp, now);
        toast("Your new friend is impressed!", now, 2500u);
        go_home();
        s_enc_due = now + 22000u + (rnd() % 18000u);
        s_dirty = true;
        s_anim = ANIM_IDLE;
    }
}

static void fight_begin_move(uint32_t now, uint8_t kind) {
    if (s_anim != ANIM_IDLE) return;
    s_move = kind;
    uint32_t ms = 340u;
    if (kind == ANIM_DASH) ms = 520u;
    else if (kind == ANIM_CHEER) ms = 280u;
    s_anim = kind;
    s_anim_t0 = now;
    s_anim_ms = ms;
    s_led_until = now + 200u;
}

static void fight_poll(uint32_t now) {
    if (s_scr != SCR_FIGHT || s_anim == ANIM_IDLE) return;
    if ((int32_t)(now - s_anim_t0) < (int32_t)s_anim_ms) return;
    if (s_anim == ANIM_FOE) {
        fight_foe_turn(now);
        s_anim = ANIM_IDLE;
        return;
    }
    fight_apply_player(now);
    if (s_scr != SCR_FIGHT) return;
    s_anim = ANIM_FOE;
    s_anim_t0 = now;
    s_anim_ms = 420u;
}

static void fight_foe_turn(uint32_t now) {
    uint8_t dmg = foe_atk();
    if ((rnd() % 8u) == 0u) dmg = 0;
    if (s_php > dmg) s_php = (uint8_t)(s_php - dmg);
    else s_php = 0;
    if (s_php == 0u) {
        if (s_energy > 18u) s_energy = (uint8_t)(s_energy - 18u);
        else s_energy = 0;
        if (s_happy > 8u) s_happy = (uint8_t)(s_happy - 8u);
        grant_xp(4u, now);
        toast("Got sleepy -- still smiling.", now, 2500u);
        go_home();
        s_enc_due = now + 25000u + (rnd() % 20000u);
        s_dirty = true;
    }
}

static void finish_train(uint32_t now) {
    const uint16_t add = (uint16_t)(8u + s_train_pts / 2u);
    grant_xp(add > 28u ? 28u : add, now);
    if (s_energy > 10u) s_energy = (uint8_t)(s_energy - 10u);
    else s_energy = 0;
    if (s_happy < 230u) s_happy = (uint8_t)(s_happy + 8u);
    toast("What a workout!", now, 2000u);
    go_home();
}

static void on_green(uint32_t now) {
    if (s_scr == SCR_HOME) {
        if (s_act == ACT_TRAIN) start_train(now);
        else if (s_act == ACT_SLEEP) { s_scr = SCR_SLEEP; s_chrome_dirty = true; s_lx = -1; }
        else if (s_act == ACT_FIGHT) start_fight(now);
        else if (s_act == ACT_EAT) { s_scr = SCR_EAT; s_chrome_dirty = true; s_lx = -1; }
        else if (s_act == ACT_RELIC) { s_scr = SCR_RELIC; s_chrome_dirty = true; }
        else { s_scr = SCR_STORY; s_story_i = 0; s_chrome_dirty = true; }
        return;
    }
    if (s_scr == SCR_TRAIN) {
        if (s_train_pts < 40u) s_train_pts++;
        action_flash(now, 160u);
        return;
    }
    if (s_scr == SCR_FIGHT) {
        fight_begin_move(now, ANIM_SPARK);
        return;
    }
    if (s_scr == SCR_SLEEP || s_scr == SCR_EAT || s_scr == SCR_STORY ||
        s_scr == SCR_RELIC || s_scr == SCR_FIND) {
        go_home();
    }
}

static void on_yellow(uint32_t now) {
    if (s_scr == SCR_HOME) s_act = (uint8_t)((s_act + ACT_N - 1u) % ACT_N);
    else if (s_scr == SCR_RELIC && s_art_count) {
        s_relic_i = (uint8_t)((s_relic_i + s_art_count - 1u) % s_art_count);
        s_chrome_dirty = true;
    } else if (s_scr == SCR_FIGHT) {
        fight_begin_move(now, ANIM_DASH);
    } else if (s_scr == SCR_STORY && s_story_i) {
        s_story_i--;
        s_chrome_dirty = true;
    }
}

static void on_blue(uint32_t now) {
    if (s_scr == SCR_HOME) s_act = (uint8_t)((s_act + 1u) % ACT_N);
    else if (s_scr == SCR_RELIC && s_art_count) {
        s_relic_i = (uint8_t)((s_relic_i + 1u) % s_art_count);
        s_chrome_dirty = true;
    } else if (s_scr == SCR_FIGHT) {
        fight_begin_move(now, ANIM_CHEER);
    } else if (s_scr == SCR_STORY) {
        if (s_story_i + 1u < STORY_N) s_story_i++;
        s_chrome_dirty = true;
    }
}

static void on_gray(void) {
    if (s_scr == SCR_FIND) {
        s_scr = SCR_RELIC;
        s_relic_i = 0;
        if (s_last_art != 0xFF && s_art_count) {
            for (unsigned i = 0; i < s_art_count; i++) {
                if (art_nth(i) == s_last_art) { s_relic_i = (uint8_t)i; break; }
            }
        }
        s_chrome_dirty = true;
        return;
    }
    s_scr = SCR_RELIC;
    s_chrome_dirty = true;
    s_lx = -1;
}

static void tick_life(uint32_t now) {
    if (now - s_last_tick < 2500u) return;
    s_last_tick = now;
    if (s_scr == SCR_SLEEP) {
        if (s_energy < 248u) s_energy = (uint8_t)(s_energy + 6u);
        if (s_hunger < 250u) s_hunger++;
        if (s_age < 250u && (now / 60000u) > s_age) s_age++;
    } else if (s_scr != SCR_FIGHT && s_scr != SCR_TRAIN) {
        if (s_hunger < 250u) s_hunger++;
        if (s_happy > 0u) s_happy--;
        if (s_energy > 0u && (s_frame & 1u)) s_energy--;
    }
    uint16_t mv = 0;
    if (bq25896_read_vbat_mv(&mv, NULL) && mv < 3500u && s_hunger < 230u) {
        s_hunger = (uint8_t)(s_hunger + 4u);
    }
    if (s_hunger > 200u && s_energy < 12u && s_happy < 25u && s_scr != SCR_SLEEP) {
        if (s_neglect < 250u) s_neglect++;
        if (s_neglect == 80u) toast("Please feed me soon...", now, 2800u);
        if (s_neglect == 160u) toast("Feeling very faint.", now, 2800u);
        if (s_neglect >= 220u) {
            toast("Went home to the isle.", now, 2800u);
            hatch(now);
            return;
        }
    } else if (s_neglect > 2u) {
        s_neglect = (uint8_t)(s_neglect - 2u);
    }
    s_dirty = true;
}

int main(void) {
    board_init();
    fwog_splash_bind("VoltPet", "005");
    s_leds = ws2812_init(pio0, 0u);
    s_pdm = pdm_mic_init(pio0, 1u);
    fwog_cic_init(&s_cic);
    lis3dh_init();
    s_accel = lis3dh_configure(LIS3DH_RANGE_2G);
    lcd_bringup();
    s_link = fwog_link_uart_init(FWOG_LINK_BAUD);
    fwog_link_rx_init(&s_rx);
    send_cmd(VP_CMD_PULL);
    DIAG("[voltpet] accel=%s pdm=%s link=%s\n",
         s_accel ? "ok" : "FAIL", s_pdm ? "ok" : "FAIL", s_link ? "ok" : "FAIL");

    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        if (s_rng == 1u) s_rng = now | 1u;
        const fwog_power_t p = fwog_power_poll(now);
        poll_link(now);
        if (!s_loaded && (now % 500u) < 8u) send_cmd(VP_CMD_PULL);
        if (!s_loaded && now > (s_link ? 6000u : 2500u)) hatch(now);
        if (s_need_hatch && s_rng != 1u) hatch(now);

        if (p.armed && !s_saved_this_arm) {
            send_cmd(VP_CMD_PUSH);
            s_saved_this_arm = true;
            s_dirty = false;
            DIAG("[voltpet] ship-armed, save sent\n");
        }
        if (!p.armed) s_saved_this_arm = false;
        s_power_armed = p.armed;

        if (s_scr == SCR_NAME) {
            const bool ydown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) != 0;
            const bool gdown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GREEN)) != 0;
            const bool bdown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_BLUE)) != 0;
            const bool rydown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GRAY)) != 0;

            if (ydown && !s_yel_was) {
                s_yel_ms = now;
                s_yel_hold = false;
            }
            if (ydown && !s_yel_hold && (now - s_yel_ms) >= VP_HOLD_MS) {
                s_yel_hold = true;
                fwog_t9_backspace(s_t9_buf);
            }
            if (!ydown && s_yel_was && !s_yel_hold)
                fwog_t9_letter_prev(s_t9_g, &s_t9_li);
            s_yel_was = ydown;

            if (gdown && !s_grn_was) {
                s_grn_ms = now;
                s_grn_hold = false;
            }
            if (gdown && !s_grn_hold && (now - s_grn_ms) >= VP_HOLD_MS) {
                s_grn_hold = true;
                t9_finish(now);
            }
            if (!gdown && s_grn_was && !s_grn_hold && s_scr == SCR_NAME)
                fwog_t9_insert(s_t9_buf, VP_NAME_N, fwog_t9_cur(s_t9_g, s_t9_li));
            s_grn_was = gdown;

            if (!bdown && s_blu_was)
                fwog_t9_letter_next(s_t9_g, &s_t9_li);
            s_blu_was = bdown;

            if (rydown && !s_gry_was) {
                s_gry_ms = now;
                s_gry_hold = false;
            }
            if (!rydown && s_gry_was && !s_gry_hold)
                fwog_t9_group_prev(&s_t9_g, &s_t9_li);
            s_gry_was = rydown;

            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_RED))
                fwog_t9_group_next(&s_t9_g, &s_t9_li);
        } else {
            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GREEN)) on_green(now);
            if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_BLUE)) on_blue(now);
            if ((p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_RED)) && s_scr != SCR_HOME)
                go_home();

            const bool ydown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) != 0;
            if (s_scr == SCR_HOME) {
                if (ydown && !s_yel_was) {
                    s_yel_ms = now;
                    s_yel_hold = false;
                }
                if (ydown && !s_yel_hold && (now - s_yel_ms) >= VP_RELEASE_MS) {
                    s_yel_hold = true;
                    hatch(now);
                }
                if (!ydown && s_yel_was && !s_yel_hold) on_yellow(now);
            } else if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_YELLOW)) {
                on_yellow(now);
            }
            s_yel_was = ydown;

            const bool rydown = (p.buttons.down & FWOG_BTN_BIT(FWOG_BTN_GRAY)) != 0;
            if (s_scr == SCR_HOME) {
                if (rydown && !s_gry_was) {
                    s_gry_ms = now;
                    s_gry_hold = false;
                }
                if (rydown && !s_gry_hold && (now - s_gry_ms) >= VP_HOLD_MS) {
                    s_gry_hold = true;
                    t9_begin();
                }
                if (!rydown && s_gry_was && !s_gry_hold) on_gray();
            } else if (p.buttons.pressed & FWOG_BTN_BIT(FWOG_BTN_GRAY)) {
                on_gray();
            }
            s_gry_was = rydown;
        }

        const bool shake = shaken();
        if (shake) {
            s_walk_acc++;
            if (s_scr == SCR_HOME && s_walk_acc > 8u) {
                s_walk_acc = 0;
                s_steps++;
                if (s_happy < 250u) s_happy = (uint8_t)(s_happy + 2u);
                s_dirty = true;
            }
            if (s_scr == SCR_EAT && s_walk_acc > 3u) {
                s_walk_acc = 0;
                feed(0u, now);
            }
            if (s_scr == SCR_TRAIN && s_train_pts < 40u) {
                s_train_pts++;
                action_flash(now, 160u);
            }
        } else if (s_scr != SCR_TRAIN) {
            s_walk_acc = 0;
        }

        take_mic();
        if (s_scr == SCR_EAT && s_mic_rms >= MIC_SNACK) {
            static uint32_t s_last_song;
            if (now - s_last_song > 1200u) {
                s_last_song = now;
                feed(1u, now);
            }
        }

        if (s_scr == SCR_TRAIN && (int32_t)(now - s_train_until) >= 0)
            finish_train(now);
        if (s_scr == SCR_FIGHT)
            fight_poll(now);
        if (s_scr == SCR_FIND && (int32_t)(now - s_find_until) >= 0)
            go_home();

        if (s_enc_due == 0u) s_enc_due = now + 18000u + (rnd() % 22000u);
        if (s_scr == SCR_HOME && s_energy >= 24u && now >= s_enc_due) {
            if ((rnd() % 5u) < 2u) start_fight(now);
            s_enc_due = now + 20000u + (rnd() % 25000u);
        }

        tick_life(now);
        if (s_dirty && now - s_last_push > 45000u) {
            send_cmd(VP_CMD_PUSH);
            s_last_push = now;
            s_dirty = false;
        }

        s_frame++;
        if (s_leds && !s_power_armed) {
            if ((int32_t)(now - s_led_until) < 0) {
                const bool on = ((now / 40u) & 1u) != 0;
                const uint8_t r = on ? 40u : 8u;
                const uint8_t g = on ? 28u : 4u;
                for (unsigned i = 0; i < (unsigned)FWOG_LED_COUNT; i++)
                    ws2812_set_color(i, r, g, 2u);
            } else {
                const bool find = (s_scr == SCR_FIND);
                ws2812_set_color(0, s_hunger / 5u, s_happy / 5u, s_energy / 6u);
                for (unsigned i = 1; i < (unsigned)FWOG_LED_COUNT; i++) {
                    ws2812_set_color(i, find ? 20u : 2u, find ? 18u : 2u, find ? 4u : 2u);
                }
            }
            ws2812_process();
        }
        if (now - s_last_paint >= 50u) {
            s_last_paint = now;
            paint(now);
        }
        sleep_ms(2);
    }
}
