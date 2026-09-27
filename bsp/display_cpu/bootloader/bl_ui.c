#include "bootloader/bl_ui.h"
#include "common/link/bl_proto.h"

const char *bl_ui_state_text(bl_ui_state_t s) {
    switch (s) {
    case BL_UI_WAITING:  return "waiting for main";
    case BL_UI_UPDATING: return "updating";
    case BL_UI_NO_APP:   return "no valid app";
    case BL_UI_CRC_FAIL: return "image CRC failed";
    case BL_UI_CONSOLE:  return "console active";
    default:             return "?";
    }
}

void bl_ui_bargraph(char *out, uint8_t percent) {
    /* Integer division floors, which is exactly the required behavior --
       do not "improve" this by adding 50 before dividing. */
    const unsigned pct  = (percent > 100u) ? 0u : (unsigned)percent;
    const unsigned fill = (pct * BL_BAR_CELLS) / 100u;
    unsigned i = 0u;

    out[i++] = '[';
    for (unsigned c = 0u; c < BL_BAR_CELLS; c++) out[i++] = (c < fill) ? '#' : ' ';
    out[i++] = ']';
    out[i] = '\0';
}

#include <stdio.h>

/* Geometry is asserted in tests/test_bl_ui.c rather than trusted:
 * STATE is 17 columns at scale 3 and the longest state string is 16;
 * BAR is exactly the bar's 22 columns at scale 2, centered. */
static const bl_ui_zone_t s_zones[BL_ZONE_COUNT] = {
    [BL_ZONE_TITLE]   = {  0u,   4u, 320u,  8u, 1u},
    [BL_ZONE_STATE]   = {  0u,  88u, 320u, 24u, 3u},
    [BL_ZONE_BAR]     = { 28u, 150u, 264u, 16u, 2u},
    [BL_ZONE_VER_BL]  = {  0u, 216u, 320u,  8u, 1u},
    [BL_ZONE_VER_APP] = {  0u, 228u, 320u,  8u, 1u},
};

const bl_ui_zone_t *bl_ui_zone(bl_ui_zone_id_t id) {
    if ((unsigned)id >= (unsigned)BL_ZONE_COUNT) return NULL;
    return &s_zones[id];
}

bool bl_ui_zone_needs_clear(uint8_t last_scale, uint8_t new_scale) {
    return last_scale != new_scale;
}

void bl_ui_state_line(char *out, size_t cap, bl_ui_state_t s, uint8_t percent) {
    const char *text = bl_ui_state_text(s);
    if (s == BL_UI_UPDATING && percent <= 100u) {
        snprintf(out, cap, "%s %u%%", text, (unsigned)percent);
    } else {
        snprintf(out, cap, "%s", text);
    }
}

bl_ui_paint_action_t bl_ui_paint_step(bl_ui_paint_state_t st, bool ready,
                                      bool busy) {
    if (st == BL_UI_PAINT_IDLE) return ready ? BL_UI_PAINT_CLEAR
                                             : BL_UI_PAINT_NONE;
    if (st == BL_UI_PAINT_CLEARING && !busy) return BL_UI_PAINT_FINISH;
    return BL_UI_PAINT_NONE;
}

bool bl_ui_paint_drawable(bl_ui_paint_state_t st) {
    return st == BL_UI_PAINT_DONE;
}

#ifndef HOST_TEST
#include "common/diag.h"
#include "lcd/lcd_text.h"
#include "lcd/st7789.h"
#include "platform/board.h"

static const char *s_bl_version  = "?";
static const char *s_app_version = "?";

/* Scale currently drawn in each zone, 0 for "nothing yet". */
static uint8_t s_drawn_scale[BL_ZONE_COUNT];

/* Where the readiness edge has got to. See bl_ui_paint_step(). */
static bl_ui_paint_state_t s_paint;

/* What should be on screen once init finishes. A STATUS can arrive while
 * the panel is still coming up, and must not be lost. */
static bool          s_pending;
static bl_ui_state_t s_pend_state;
static uint8_t       s_pend_percent;
static char          s_pend_status[FWOG_BL_STATUS_TEXT_LEN];
static bool          s_pend_is_status;

#define COL_BG    0x0000u                  /* black  */
#define COL_FG    0xFFFFu                  /* white  */
#define COL_DIM   0x8410u                  /* gray   */

static void draw_zone(bl_ui_zone_id_t id, const char *text, unsigned scale,
                      uint16_t fg) {
    const bl_ui_zone_t *z = bl_ui_zone(id);
    if (!z || !bl_ui_paint_drawable(s_paint)) return;
    if (bl_ui_zone_needs_clear(s_drawn_scale[id], (uint8_t)scale)) {
        st7789_fill_rect(z->x, z->y, z->w, z->h, COL_BG);
    }
    lcd_text_draw_padded(z->x, z->y, text, lcd_text_cols(z->w, scale),
                         scale, fg, COL_BG);
    s_drawn_scale[id] = (uint8_t)scale;
}

/* Title and versions: drawn once, when the panel becomes ready. */
static void draw_chrome(void) {
    char line[40];
    draw_zone(BL_ZONE_TITLE, "FWOG bootloader", 1u, COL_DIM);
    snprintf(line, sizeof line, "bl  %s", s_bl_version);
    draw_zone(BL_ZONE_VER_BL, line, 1u, COL_DIM);
    snprintf(line, sizeof line, "app %s", s_app_version);
    draw_zone(BL_ZONE_VER_APP, line, 1u, COL_DIM);
}

static void render_state(bl_ui_state_t s, uint8_t percent) {
    char line[BL_UI_LINE_BUF];
    bl_ui_state_line(line, sizeof line, s, percent);
    const bl_ui_zone_t *z = bl_ui_zone(BL_ZONE_STATE);
    draw_zone(BL_ZONE_STATE, line, z->scale, COL_FG);

    if (s == BL_UI_UPDATING && percent <= 100u) {
        char bar[BL_BAR_BUF];
        bl_ui_bargraph(bar, percent);
        draw_zone(BL_ZONE_BAR, bar, bl_ui_zone(BL_ZONE_BAR)->scale, COL_FG);
    } else {
        draw_zone(BL_ZONE_BAR, "", bl_ui_zone(BL_ZONE_BAR)->scale, COL_FG);
    }
}

/* Main's line is up to 27 characters, so it renders at scale 1 in the same
 * zone the bootloader's own state uses at scale 3. */
static void render_status(const char *text, uint8_t percent) {
    draw_zone(BL_ZONE_STATE, text ? text : "", 1u, COL_FG);
    if (percent <= 100u) {
        char bar[BL_BAR_BUF];
        bl_ui_bargraph(bar, percent);
        draw_zone(BL_ZONE_BAR, bar, bl_ui_zone(BL_ZONE_BAR)->scale, COL_FG);
    } else {
        draw_zone(BL_ZONE_BAR, "", bl_ui_zone(BL_ZONE_BAR)->scale, COL_FG);
    }
}

void bl_ui_init(const char *bl_version, const char *app_version) {
    if (bl_version)  s_bl_version  = bl_version;
    if (app_version) s_app_version = app_version;
    DIAG("[bl] ui: bootloader %s, app %s\n", s_bl_version, s_app_version);
    st7789_init_begin();
}

void bl_ui_tick(void) {
    st7789_init_step();

    switch (bl_ui_paint_step(s_paint, st7789_ready(), st7789_busy())) {
    case BL_UI_PAINT_CLEAR:
        /* Starts ~25 ms of DMA and returns. The caller's loop keeps
           draining the link, announcing HELLO and sampling buttons while
           the panel is painted in hardware -- which is the whole reason
           this edge is a state machine and not a straight line. */
        st7789_clear(COL_BG);
        s_paint = BL_UI_PAINT_CLEARING;
        break;

    case BL_UI_PAINT_FINISH:
        /* Before any draw: draw_zone() is gated on this. */
        s_paint = BL_UI_PAINT_DONE;
        for (unsigned i = 0; i < BL_ZONE_COUNT; i++) s_drawn_scale[i] = 0u;
        draw_chrome();
        if (s_pending) {
            if (s_pend_is_status) render_status(s_pend_status, s_pend_percent);
            else                  render_state(s_pend_state, s_pend_percent);
            s_pending = false;
        }
        /* Backlight last, after the pending replay and not just the chrome:
           the rule is "no garbage ever visible", and the replayed frame is
           still the first thing this panel shows. */
        board_backlight(255u);
        break;

    default:
        break;
    }
}

void bl_ui_show(bl_ui_state_t s, uint8_t percent) {
    if (s == BL_UI_UPDATING) {
        DIAG("[bl] %s %u%%  (bl %s, app %s)\n", bl_ui_state_text(s),
             (unsigned)percent, s_bl_version, s_app_version);
    } else {
        DIAG("[bl] %s  (bl %s, app %s)\n", bl_ui_state_text(s),
             s_bl_version, s_app_version);
    }
    if (!bl_ui_paint_drawable(s_paint)) {
        s_pending = true; s_pend_is_status = false;
        s_pend_state = s; s_pend_percent = percent;
        return;
    }
    render_state(s, percent);
}

void bl_ui_status(const char *text, uint8_t percent) {
    if (percent <= 100u) {
        char bar[BL_BAR_BUF];
        bl_ui_bargraph(bar, percent);
        DIAG("[bl] %s %s %u%%\n", text ? text : "", bar, (unsigned)percent);
    } else {
        DIAG("[bl] %s\n", text ? text : "");
    }
    if (!bl_ui_paint_drawable(s_paint)) {
        s_pending = true; s_pend_is_status = true; s_pend_percent = percent;
        snprintf(s_pend_status, sizeof s_pend_status, "%s", text ? text : "");
        return;
    }
    render_status(text, percent);
}
#endif
