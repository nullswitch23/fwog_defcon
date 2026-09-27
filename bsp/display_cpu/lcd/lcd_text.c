#include "lcd/lcd_text.h"
#include <string.h>

/* 6x8 column-major font, ASCII 0x20-0x7E, ported verbatim from
 * rmpLib/st7789.cpp's fontdata[]. Each byte is one column; bit N is
 * row N. Extracted programmatically -- do not hand-edit. */
static const uint8_t s_font[LCD_FONT_CHARS][LCD_GLYPH_W] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x5f, 0x00, 0x00, 0x00, 0x00, 0x00},      /* ! */
    {0x03, 0x00, 0x03, 0x00, 0x00, 0x00},      /* " */
    {0x28, 0x7c, 0x28, 0x7c, 0x28, 0x00},      /* # */
    {0x24, 0x7a, 0x2f, 0x12, 0x00, 0x00},      /* $ */
    {0x66, 0x10, 0x08, 0x66, 0x00, 0x00},      /* % */
    {0x36, 0x49, 0x49, 0x7c, 0x00, 0x00},      /* & */
    {0x03, 0x00, 0x00, 0x00, 0x00, 0x00},      /* ' */
    {0x1c, 0x22, 0x41, 0x00, 0x00, 0x00},      /* ( */
    {0x41, 0x22, 0x1c, 0x00, 0x00, 0x00},      /* ) */
    {0x54, 0x38, 0x54, 0x00, 0x00, 0x00},      /* * */
    {0x10, 0x38, 0x10, 0x00, 0x00, 0x00},      /* + */
    {0x80, 0x60, 0x00, 0x00, 0x00, 0x00},      /* , */
    {0x10, 0x10, 0x10, 0x00, 0x00, 0x00},      /* - */
    {0x60, 0x60, 0x00, 0x00, 0x00, 0x00},      /* . */
    {0x60, 0x18, 0x06, 0x01, 0x00, 0x00},      /* / */
    {0x3e, 0x41, 0x41, 0x3e, 0x00, 0x00},      /* 0 */
    {0x42, 0x7f, 0x40, 0x00, 0x00, 0x00},      /* 1 */
    {0x62, 0x51, 0x49, 0x46, 0x00, 0x00},      /* 2 */
    {0x21, 0x49, 0x4d, 0x33, 0x00, 0x00},      /* 3 */
    {0x18, 0x16, 0x11, 0x7f, 0x00, 0x00},      /* 4 */
    {0x4f, 0x49, 0x49, 0x31, 0x00, 0x00},      /* 5 */
    {0x3c, 0x4a, 0x49, 0x30, 0x00, 0x00},      /* 6 */
    {0x01, 0x61, 0x19, 0x07, 0x00, 0x00},      /* 7 */
    {0x36, 0x49, 0x49, 0x36, 0x00, 0x00},      /* 8 */
    {0x06, 0x49, 0x29, 0x1e, 0x00, 0x00},      /* 9 */
    {0x33, 0x00, 0x00, 0x00, 0x00, 0x00},      /* : */
    {0x80, 0x6c, 0x00, 0x00, 0x00, 0x00},      /* ; */
    {0x10, 0x28, 0x44, 0x00, 0x00, 0x00},      /* < */
    {0x28, 0x28, 0x28, 0x00, 0x00, 0x00},      /* = */
    {0x44, 0x28, 0x10, 0x00, 0x00, 0x00},      /* > */
    {0x02, 0x51, 0x09, 0x06, 0x00, 0x00},      /* ? */
    {0x3e, 0x49, 0x55, 0x5e, 0x00, 0x00},      /* @ */
    {0x7e, 0x09, 0x09, 0x7e, 0x00, 0x00},      /* A */
    {0x7f, 0x49, 0x49, 0x36, 0x00, 0x00},      /* B */
    {0x3e, 0x41, 0x41, 0x22, 0x00, 0x00},      /* C */
    {0x7f, 0x41, 0x41, 0x3e, 0x00, 0x00},      /* D */
    {0x7f, 0x49, 0x49, 0x41, 0x00, 0x00},      /* E */
    {0x7f, 0x09, 0x09, 0x01, 0x00, 0x00},      /* F */
    {0x3e, 0x41, 0x49, 0x79, 0x00, 0x00},      /* G */
    {0x7f, 0x08, 0x08, 0x7f, 0x00, 0x00},      /* H */
    {0x41, 0x7f, 0x41, 0x00, 0x00, 0x00},      /* I */
    {0x30, 0x40, 0x40, 0x3f, 0x00, 0x00},      /* J */
    {0x7f, 0x08, 0x14, 0x63, 0x00, 0x00},      /* K */
    {0x7f, 0x40, 0x40, 0x40, 0x00, 0x00},      /* L */
    {0x7f, 0x02, 0x04, 0x02, 0x7f, 0x00},      /* M */
    {0x7f, 0x02, 0x04, 0x7f, 0x00, 0x00},      /* N */
    {0x3e, 0x41, 0x41, 0x3e, 0x00, 0x00},      /* O */
    {0x7f, 0x09, 0x09, 0x06, 0x00, 0x00},      /* P */
    {0x3e, 0x41, 0x21, 0x5e, 0x00, 0x00},      /* Q */
    {0x7f, 0x09, 0x19, 0x66, 0x00, 0x00},      /* R */
    {0x46, 0x49, 0x49, 0x31, 0x00, 0x00},      /* S */
    {0x01, 0x01, 0x7f, 0x01, 0x01, 0x00},      /* T */
    {0x3f, 0x40, 0x40, 0x3f, 0x00, 0x00},      /* U */
    {0x7f, 0x40, 0x20, 0x1f, 0x00, 0x00},      /* V */
    {0x3f, 0x40, 0x20, 0x40, 0x3f, 0x00},      /* W */
    {0x77, 0x08, 0x08, 0x77, 0x00, 0x00},      /* X */
    {0x47, 0x48, 0x48, 0x3f, 0x00, 0x00},      /* Y */
    {0x71, 0x49, 0x45, 0x43, 0x00, 0x00},      /* Z */
    {0x7f, 0x41, 0x00, 0x00, 0x00, 0x00},      /* [ */
    {0x01, 0x06, 0x18, 0x60, 0x00, 0x00},      /* "\" */
    {0x41, 0x7f, 0x00, 0x00, 0x00, 0x00},      /* ] */
    {0x04, 0x02, 0x04, 0x00, 0x00, 0x00},      /* ^ */
    {0x40, 0x40, 0x40, 0x00, 0x00, 0x00},      /* _ */
    {0x01, 0x01, 0x00, 0x00, 0x00, 0x00},      /* ` */
    {0x20, 0x54, 0x54, 0x78, 0x00, 0x00},      /* a */
    {0x7f, 0x44, 0x44, 0x38, 0x00, 0x00},      /* b */
    {0x38, 0x44, 0x44, 0x28, 0x00, 0x00},      /* c */
    {0x38, 0x44, 0x44, 0x7f, 0x00, 0x00},      /* d */
    {0x38, 0x54, 0x54, 0x58, 0x00, 0x00},      /* e */
    {0x7e, 0x09, 0x09, 0x02, 0x00, 0x00},      /* f */
    {0x18, 0xa4, 0xa4, 0x7c, 0x00, 0x00},      /* g */
    {0x7f, 0x04, 0x04, 0x78, 0x00, 0x00},      /* h */
    {0x04, 0x7d, 0x40, 0x00, 0x00, 0x00},      /* i */
    {0x60, 0x80, 0x80, 0x7d, 0x00, 0x00},      /* j */
    {0x7f, 0x10, 0x28, 0x44, 0x00, 0x00},      /* k */
    {0x01, 0x7f, 0x40, 0x00, 0x00, 0x00},      /* l */
    {0x7c, 0x04, 0x78, 0x04, 0x78, 0x00},      /* m */
    {0x7c, 0x04, 0x04, 0x78, 0x00, 0x00},      /* n */
    {0x38, 0x44, 0x44, 0x38, 0x00, 0x00},      /* o */
    {0xfc, 0x24, 0x24, 0x18, 0x00, 0x00},      /* p */
    {0x18, 0x24, 0x24, 0xfc, 0x00, 0x00},      /* q */
    {0x7c, 0x08, 0x04, 0x04, 0x00, 0x00},      /* r */
    {0x48, 0x54, 0x54, 0x24, 0x00, 0x00},      /* s */
    {0x3e, 0x44, 0x44, 0x20, 0x00, 0x00},      /* t */
    {0x3c, 0x40, 0x40, 0x7c, 0x00, 0x00},      /* u */
    {0x7c, 0x40, 0x20, 0x1c, 0x00, 0x00},      /* v */
    {0x3c, 0x40, 0x20, 0x40, 0x3c, 0x00},      /* w */
    {0x6c, 0x10, 0x10, 0x6c, 0x00, 0x00},      /* x */
    {0x1c, 0xa0, 0xa0, 0x7c, 0x00, 0x00},      /* y */
    {0x64, 0x54, 0x4c, 0x00, 0x00, 0x00},      /* z */
    {0x08, 0x3e, 0x41, 0x00, 0x00, 0x00},      /* { */
    {0x7f, 0x00, 0x00, 0x00, 0x00, 0x00},      /* | */
    {0x41, 0x3e, 0x08, 0x00, 0x00, 0x00},      /* } */
    {0x08, 0x04, 0x08, 0x04, 0x00, 0x00},      /* ~ */
};

static const uint8_t s_blank[LCD_GLYPH_W] = {0, 0, 0, 0, 0, 0};

const uint8_t *lcd_font_glyph(char c) {
    const unsigned u = (unsigned char)c;
    if (u < LCD_FONT_FIRST || u > LCD_FONT_LAST) return s_blank;
    return s_font[u - LCD_FONT_FIRST];
}

unsigned lcd_text_cols(unsigned width_px, unsigned scale) {
    if (scale == 0u) return 0u;
    return width_px / (LCD_GLYPH_W * scale);
}

unsigned lcd_text_width_px(unsigned nchars, unsigned scale) {
    return nchars * LCD_GLYPH_W * scale;
}

unsigned lcd_font_expand(char c, unsigned scale, uint16_t fg, uint16_t bg,
                         uint16_t *out) {
    if (scale == 0u || !out) return 0u;
    const uint8_t *g = lcd_font_glyph(c);
    const unsigned h = LCD_GLYPH_H * scale;
    unsigned n = 0u;
    /* Row-major, matching the window's scan order. Column byte `col`, bit
       `row` -- the legacy layout, one byte per glyph column. */
    for (unsigned row = 0u; row < h; row++) {
        for (unsigned col = 0u; col < LCD_GLYPH_W; col++) {
            const bool on = (g[col] & (1u << (row / scale))) != 0u;
            for (unsigned s = 0u; s < scale; s++) out[n++] = on ? fg : bg;
        }
    }
    return n;
}

#ifdef HOST_TEST

static lcd_text_log_entry_t s_log[LCD_TEXT_LOG_CAP];
static unsigned s_log_n, s_log_head;

void lcd_text_log_clear(void) { s_log_n = s_log_head = 0u; }

unsigned lcd_text_log_count(void) {
    return (s_log_n < LCD_TEXT_LOG_CAP) ? s_log_n : LCD_TEXT_LOG_CAP;
}

const lcd_text_log_entry_t *lcd_text_log_at(unsigned i) {
    const unsigned n = lcd_text_log_count();
    if (i >= n) return NULL;
    if (s_log_n < LCD_TEXT_LOG_CAP) return &s_log[i];
    return &s_log[(s_log_head + i) % LCD_TEXT_LOG_CAP];
}

static void log_text(uint16_t x, uint16_t y, const char *str,
                     unsigned cols, unsigned scale) {
    lcd_text_log_entry_t *e;
    if (s_log_n < LCD_TEXT_LOG_CAP) {
        e = &s_log[s_log_n++];
    } else {
        e = &s_log[s_log_head];
        s_log_head = (s_log_head + 1u) % LCD_TEXT_LOG_CAP;
        s_log_n++;
    }
    e->x = x;
    e->y = y;
    e->cols = cols;
    e->scale = scale;
    if (!str) str = "";
    size_t n = 0u;
    while (str[n] && n + 1u < LCD_TEXT_LOG_STR) n++;
    memcpy(e->str, str, n);
    e->str[n] = '\0';
}
#endif

/* Largest supported scale. Buffer is 6*8*scale^2 uint16s (3456 B at 6).
 * Must stay `static` — not a stack local. See the draw_glyph note. */
#define LCD_MAX_SCALE 6u

static void draw_glyph(uint16_t x, uint16_t y, char c, unsigned scale,
                       uint16_t fg, uint16_t bg) {
    if (scale == 0u || scale > LCD_MAX_SCALE) return;
    const unsigned w = LCD_GLYPH_W * scale;
    const unsigned h = LCD_GLYPH_H * scale;
    if (x >= ST7789_W || y >= ST7789_H) return;
    if (x + w > ST7789_W || y + h > ST7789_H) return;

    /* static, not a stack local. The final whole-branch review measured the
       bootloader's deepest live call chain off bl_display.dis:
       main -> bl_ui_show -> render_state -> draw_zone ->
       lcd_text_draw_padded -> draw_glyph -> st7789_blit = 1508 bytes, of
       which this array alone was 864 -- against a 2048-byte stack
       (__StackTop - __StackBottom), leaving ~540 bytes for exception
       frames and the TinyUSB tud_task() that runs off an alarm IRQ once
       the console enumerates. That overflow would be silent, not a fault:
       unused SCRATCH_Y sits below __StackBottom.
       Safe as `static` because draw_glyph() is reached only from
       lcd_text_draw()/lcd_text_draw_padded() in a plain per-glyph loop on
       the bootloader's single main thread -- no recursion, no second core,
       no reentrancy. Same idiom as apps/bl/display/main.c's s_rx (4.2 KB
       in .bss, same reasoning). Do not move this back onto the stack. */
    static uint16_t px[LCD_GLYPH_W * LCD_GLYPH_H * LCD_MAX_SCALE * LCD_MAX_SCALE];
    const unsigned n = lcd_font_expand(c, scale, fg, bg, px);
    st7789_set_window(x, y, (uint16_t)w, (uint16_t)h);
    st7789_blit(px, n);
}

void lcd_text_draw(uint16_t x, uint16_t y, const char *str, unsigned scale,
                   uint16_t fg, uint16_t bg) {
#ifdef HOST_TEST
    log_text(x, y, str, 0u, scale);
#endif
    if (!str || scale == 0u) return;
    const unsigned adv = LCD_GLYPH_W * scale;
    for (unsigned i = 0u; str[i]; i++) {
        const uint32_t gx = (uint32_t)x + (uint32_t)i * adv;
        if (gx + adv > ST7789_W) break;          /* truncate, never wrap */
        draw_glyph((uint16_t)gx, y, str[i], scale, fg, bg);
    }
}

void lcd_text_draw_padded(uint16_t x, uint16_t y, const char *str,
                          unsigned cols, unsigned scale,
                          uint16_t fg, uint16_t bg) {
#ifdef HOST_TEST
    log_text(x, y, str, cols, scale);
#endif
    if (scale == 0u) return;
    const unsigned adv = LCD_GLYPH_W * scale;
    bool ended = (str == NULL);
    for (unsigned i = 0u; i < cols; i++) {
        const uint32_t gx = (uint32_t)x + (uint32_t)i * adv;
        if (gx + adv > ST7789_W) break;          /* truncate, never wrap */
        /* Once the string ends, keep emitting spaces to erase whatever the
           previous, longer string left in those cells -- but never index
           past the terminator. */
        if (!ended && str[i] == '\0') ended = true;
        draw_glyph((uint16_t)gx, y, ended ? ' ' : str[i], scale, fg, bg);
    }
}

bool lcd_text_draw_padded_changed(uint16_t x, uint16_t y, const char *str,
                                  unsigned cols, unsigned scale,
                                  uint16_t fg, uint16_t bg,
                                  char *slot, unsigned slot_n) {
    if (!slot || slot_n == 0u) {
        lcd_text_draw_padded(x, y, str, cols, scale, fg, bg);
        return true;
    }
    const char *s = str ? str : "";
    if (strncmp(s, slot, slot_n - 1u) == 0 && slot[0] != '\0') {
        return false;
    }
    strncpy(slot, s, slot_n - 1u);
    slot[slot_n - 1u] = '\0';
    lcd_text_draw_padded(x, y, str, cols, scale, fg, bg);
    return true;
}
