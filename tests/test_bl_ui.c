#include <string.h>
#include "bootloader/bl_ui.h"
#include "common/link/bl_proto.h"
#include "lcd/lcd_text.h"
#include "test_util.h"

static void test_zones_fit_the_panel(void) {
    for (int i = 0; i < BL_ZONE_COUNT; i++) {
        const bl_ui_zone_t *z = bl_ui_zone((bl_ui_zone_id_t)i);
        ASSERT_TRUE(z != NULL);
        ASSERT_TRUE(z->x + z->w <= ST7789_W);
        ASSERT_TRUE(z->y + z->h <= ST7789_H);
        ASSERT_TRUE(z->scale >= 1u && z->scale <= 3u);
        /* The zone must be tall enough for its own text. */
        ASSERT_TRUE(z->h >= LCD_GLYPH_H * z->scale);
    }
    /* Out of range is NULL, not a read past the table. */
    ASSERT_TRUE(bl_ui_zone((bl_ui_zone_id_t)BL_ZONE_COUNT) == NULL);
}

static void test_state_strings_fit(void) {
    const bl_ui_zone_t *z = bl_ui_zone(BL_ZONE_STATE);
    const unsigned cols = lcd_text_cols(z->w, z->scale);
    ASSERT_EQ(cols, 17u);
    for (int s = BL_UI_WAITING; s <= BL_UI_CONSOLE; s++) {
        const char *t = bl_ui_state_text((bl_ui_state_t)s);
        ASSERT_TRUE(strlen(t) <= cols);
    }
    /* Main's status text needs scale 1 in the same zone. */
    ASSERT_TRUE(FWOG_BL_STATUS_TEXT_LEN - 1u <= lcd_text_cols(z->w, 1u));
}

static void test_bar_fits(void) {
    const bl_ui_zone_t *z = bl_ui_zone(BL_ZONE_BAR);
    /* '[' + BL_BAR_CELLS + ']' */
    const unsigned bar_chars = BL_BAR_CELLS + 2u;
    ASSERT_EQ(bar_chars, 22u);
    ASSERT_TRUE(lcd_text_cols(z->w, z->scale) >= bar_chars);
    ASSERT_EQ(lcd_text_width_px(bar_chars, z->scale), z->w);
    /* Centered on the panel. */
    ASSERT_EQ(z->x, (ST7789_W - z->w) / 2u);
}

static void test_state_line(void) {
    char buf[BL_UI_LINE_BUF];

    /* Non-updating states carry no percentage. */
    bl_ui_state_line(buf, sizeof buf, BL_UI_WAITING, 0u);
    ASSERT_TRUE(strcmp(buf, "waiting for main") == 0);
    bl_ui_state_line(buf, sizeof buf, BL_UI_CRC_FAIL, 40u);
    ASSERT_TRUE(strcmp(buf, "image CRC failed") == 0);

    /* Updating appends it. */
    bl_ui_state_line(buf, sizeof buf, BL_UI_UPDATING, 0u);
    ASSERT_TRUE(strcmp(buf, "updating 0%") == 0);
    bl_ui_state_line(buf, sizeof buf, BL_UI_UPDATING, 100u);
    ASSERT_TRUE(strcmp(buf, "updating 100%") == 0);

    /* Every composed line fits the STATE zone at its scale. */
    const bl_ui_zone_t *z = bl_ui_zone(BL_ZONE_STATE);
    const unsigned cols = lcd_text_cols(z->w, z->scale);
    for (unsigned p = 0u; p <= 100u; p++) {
        bl_ui_state_line(buf, sizeof buf, BL_UI_UPDATING, (uint8_t)p);
        ASSERT_TRUE(strlen(buf) <= cols);
    }
    /* Out-of-range percent draws no percentage rather than nonsense. */
    bl_ui_state_line(buf, sizeof buf, BL_UI_UPDATING, FWOG_BL_STATUS_NO_BAR);
    ASSERT_TRUE(strcmp(buf, "updating") == 0);
}

static void test_needs_clear(void) {
    /* Same scale redraws in place; a scale change must clear first, or the
       taller glyphs leave fragments behind. */
    ASSERT_TRUE(!bl_ui_zone_needs_clear(3u, 3u));
    ASSERT_TRUE(bl_ui_zone_needs_clear(3u, 1u));
    ASSERT_TRUE(bl_ui_zone_needs_clear(1u, 3u));
}

static void test_paint_step(void) {
    /* Nothing happens until the panel finishes its staged init. */
    ASSERT_EQ(bl_ui_paint_step(BL_UI_PAINT_IDLE, false, false),
              BL_UI_PAINT_NONE);
    ASSERT_EQ(bl_ui_paint_step(BL_UI_PAINT_IDLE, false, true),
              BL_UI_PAINT_NONE);

    /* The readiness edge starts the clear and nothing else. */
    ASSERT_EQ(bl_ui_paint_step(BL_UI_PAINT_IDLE, true, false),
              BL_UI_PAINT_CLEAR);

    /* While the clear is on the wire the caller is told to do nothing, so
       its loop keeps draining the link -- the entire point of this. */
    ASSERT_EQ(bl_ui_paint_step(BL_UI_PAINT_CLEARING, true, true),
              BL_UI_PAINT_NONE);

    /* Retired: chrome, replay, backlight. */
    ASSERT_EQ(bl_ui_paint_step(BL_UI_PAINT_CLEARING, true, false),
              BL_UI_PAINT_FINISH);

    /* Terminal. A second CLEAR here would wipe the screen on every tick. */
    ASSERT_EQ(bl_ui_paint_step(BL_UI_PAINT_DONE, true, false),
              BL_UI_PAINT_NONE);
    ASSERT_EQ(bl_ui_paint_step(BL_UI_PAINT_DONE, true, true),
              BL_UI_PAINT_NONE);
}

static void test_paint_drawable(void) {
    /* Drawing is gated until the clear has retired. A zone drawn during the
       clear would be painted over by it -- and the backlight is still down,
       so the rule "no garbage is ever visible" depends on this. */
    ASSERT_TRUE(!bl_ui_paint_drawable(BL_UI_PAINT_IDLE));
    ASSERT_TRUE(!bl_ui_paint_drawable(BL_UI_PAINT_CLEARING));
    ASSERT_TRUE(bl_ui_paint_drawable(BL_UI_PAINT_DONE));
}

int main(void) {
    test_zones_fit_the_panel();
    test_state_strings_fit();
    test_bar_fits();
    test_state_line();
    test_needs_clear();
    test_paint_step();
    test_paint_drawable();
    TEST_RETURN();
}
