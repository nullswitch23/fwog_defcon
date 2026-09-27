#include "lcd/lcd_text.h"
#include "lcd/st7789.h"
#include "test_util.h"

static void test_metrics(void) {
    ASSERT_EQ(LCD_GLYPH_W, 6u);
    ASSERT_EQ(LCD_GLYPH_H, 8u);
    /* 320 px wide: 53 columns at scale 1, 26 at scale 2, 17 at scale 3. */
    ASSERT_EQ(lcd_text_cols(320u, 1u), 53u);
    ASSERT_EQ(lcd_text_cols(320u, 2u), 26u);
    ASSERT_EQ(lcd_text_cols(320u, 3u), 17u);
    ASSERT_EQ(lcd_text_width_px(16u, 3u), 288u);
    ASSERT_EQ(lcd_text_width_px(22u, 2u), 264u);
    /* Degenerate scale must not divide by zero. */
    ASSERT_EQ(lcd_text_cols(320u, 0u), 0u);
}

static void test_glyphs(void) {
    /* Spot-checked against rmpLib/st7789.cpp's fontdata[]. */
    const uint8_t *sp = lcd_font_glyph(' ');
    for (unsigned i = 0; i < LCD_GLYPH_W; i++) ASSERT_EQ(sp[i], 0x00u);

    const uint8_t *a = lcd_font_glyph('A');
    ASSERT_EQ(a[0], 0x7eu); ASSERT_EQ(a[1], 0x09u);
    ASSERT_EQ(a[2], 0x09u); ASSERT_EQ(a[3], 0x7eu);
    ASSERT_EQ(a[4], 0x00u); ASSERT_EQ(a[5], 0x00u);

    const uint8_t *zero = lcd_font_glyph('0');
    ASSERT_EQ(zero[0], 0x3eu); ASSERT_EQ(zero[1], 0x41u);

    /* Never NULL, including out of range, and never reads past the table. */
    ASSERT_TRUE(lcd_font_glyph('\0') != NULL);
    ASSERT_TRUE(lcd_font_glyph((char)0x1Fu) != NULL);
    ASSERT_TRUE(lcd_font_glyph((char)0x7Fu) != NULL);
    ASSERT_TRUE(lcd_font_glyph((char)0xFFu) != NULL);
    /* Out-of-range falls back to the blank glyph. */
    const uint8_t *bad = lcd_font_glyph((char)0x7Fu);
    for (unsigned i = 0; i < LCD_GLYPH_W; i++) ASSERT_EQ(bad[i], 0x00u);
}

/* Pins the orientation of lcd_font_expand(): column-major input (byte `col`
 * holds column `col`, bit `row` of that byte is pixel (col,row)), row-major
 * output (index row*LCD_GLYPH_W + col). A transposed or off-by-one-row
 * rasterizer must fail this -- with no MISO and no hardware run, this is
 * the only thing standing between a correct glyph and a dead-looking panel. */
static void test_expand_scale1_A(void) {
    const uint16_t FG = 0xFFFFu, BG = 0x0000u;
    uint16_t out[LCD_GLYPH_W * LCD_GLYPH_H];
    for (unsigned i = 0; i < LCD_GLYPH_W * LCD_GLYPH_H; i++) out[i] = 0x1234u;

    const unsigned n = lcd_font_expand('A', 1u, FG, BG, out);
    ASSERT_EQ(n, LCD_GLYPH_W * LCD_GLYPH_H);

    /* 'A' column bytes: 7e 09 09 7e 00 00. Hand-derived row-major grid
     * ('#' = fg, '.' = bg), row 0 at the top: */
    static const char *rows[LCD_GLYPH_H] = {
        ".##...",
        "#..#..",
        "#..#..",
        "####..",
        "#..#..",
        "#..#..",
        "#..#..",
        "......",
    };
    for (unsigned row = 0; row < LCD_GLYPH_H; row++) {
        for (unsigned col = 0; col < LCD_GLYPH_W; col++) {
            const uint16_t expect = (rows[row][col] == '#') ? FG : BG;
            ASSERT_EQ(out[row * LCD_GLYPH_W + col], expect);
        }
    }

    /* A transposition would swap (row=3,col=0) [lit] with (row=0,col=3)
     * [unlit] -- assert both explicitly so a transpose bug cannot pass by
     * accident even if the hand-derived grid above were mistranscribed. */
    ASSERT_EQ(out[3 * LCD_GLYPH_W + 0], FG);
    ASSERT_EQ(out[0 * LCD_GLYPH_W + 3], BG);
}

static void test_expand_scale2_doubles(void) {
    const uint16_t FG = 0xFFFFu, BG = 0x0000u;
    uint16_t s1[LCD_GLYPH_W * LCD_GLYPH_H];
    uint16_t s2[LCD_GLYPH_W * LCD_GLYPH_H * 4u];
    const unsigned n1 = lcd_font_expand('A', 1u, FG, BG, s1);
    const unsigned n2 = lcd_font_expand('A', 2u, FG, BG, s2);
    ASSERT_EQ(n2, n1 * 4u);

    const unsigned w2 = LCD_GLYPH_W * 2u;
    /* A lit pixel at (col=1,row=0) in scale 1 must become a solid 2x2
     * block at (2,0)..(3,1) in scale 2's row-major output. */
    ASSERT_EQ(s1[0 * LCD_GLYPH_W + 1], FG);
    ASSERT_EQ(s2[0 * w2 + 2], FG);
    ASSERT_EQ(s2[0 * w2 + 3], FG);
    ASSERT_EQ(s2[1 * w2 + 2], FG);
    ASSERT_EQ(s2[1 * w2 + 3], FG);

    /* An unlit pixel at (col=0,row=0) must become an all-bg 2x2 block. */
    ASSERT_EQ(s1[0 * LCD_GLYPH_W + 0], BG);
    ASSERT_EQ(s2[0 * w2 + 0], BG);
    ASSERT_EQ(s2[0 * w2 + 1], BG);
    ASSERT_EQ(s2[1 * w2 + 0], BG);
    ASSERT_EQ(s2[1 * w2 + 1], BG);
}

static void test_expand_space_is_blank(void) {
    const uint16_t FG = 0xFFFFu, BG = 0x0000u;
    uint16_t out[LCD_GLYPH_W * LCD_GLYPH_H];
    const unsigned n = lcd_font_expand(' ', 1u, FG, BG, out);
    ASSERT_EQ(n, LCD_GLYPH_W * LCD_GLYPH_H);
    for (unsigned i = 0; i < n; i++) ASSERT_EQ(out[i], BG);
}

static void test_draw_padded_hits_fb(void) {
    st7789_host_reset();
    lcd_text_log_clear();
    st7789_init_begin();
    for (int i = 0; i < 8 && !st7789_ready(); i++) st7789_init_step();
    ASSERT_TRUE(st7789_ready());
    const uint16_t bg = 0u, fg = 0xFFFFu;
    st7789_clear(bg);
    lcd_text_draw_padded(0u, 0u, "A", 1u, 1u, fg, bg);
    ASSERT_EQ(lcd_text_log_count(), 1u);
    ASSERT_TRUE(lcd_text_log_at(0u) != NULL);
    ASSERT_TRUE(lcd_text_log_at(0u)->str[0] == 'A');
    /* 'A' has a lit pixel at (col=0,row=3). */
    ASSERT_EQ(st7789_host_fb()[3u * ST7789_W + 0u], fg);
}

int main(void) {
    test_metrics();
    test_glyphs();
    test_expand_scale1_A();
    test_expand_scale2_doubles();
    test_expand_space_is_blank();
    test_draw_padded_hits_fb();
    TEST_RETURN();
}
